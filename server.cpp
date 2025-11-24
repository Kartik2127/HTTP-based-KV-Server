#include "httplib.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <unistd.h> 
#include <fcntl.h>
#include <vector>
#include <queue>
#include <list>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <sys/resource.h> 

#include "cppconn/driver.h"
#include "cppconn/exception.h"
#include "cppconn/prepared_statement.h"
#include "cppconn/resultset.h"
#include "cppconn/statement.h"

using namespace std;


void perform_heavy_computation(string& data) {
    volatile int result = 0;
    for(int i = 0; i < 50000; i++) {
        result += i;
        if (!data.empty()) {
            data[0] = (data[0] + 1) % 128;
        }
    }
}


class BoundedAsyncWALLogger {
public:
    BoundedAsyncWALLogger() : stop_flag_(false), max_queue_size_(1000) {
        logger_thread_ = thread(&BoundedAsyncWALLogger::process_logs, this);
    }

    ~BoundedAsyncWALLogger() {
        {
            lock_guard<mutex> lock(queue_mutex_);
            stop_flag_ = true;
        }
        cv_empty_.notify_one();
        if (logger_thread_.joinable()) {
            logger_thread_.join();
        }
    }

    void log(const string& data) {
        unique_lock<mutex> lock(queue_mutex_);
        cv_full_.wait(lock, [this] { 
            return log_queue_.size() < max_queue_size_; 
        });

        log_queue_.push(data);
        cv_empty_.notify_one(); 
    }

private:
    void process_logs() {
        int fd = open("wal_simulation.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        
        
        const size_t FIXED_BATCH_LIMIT = 100; 

        while (true) {
            unique_lock<mutex> lock(queue_mutex_);
            
            cv_empty_.wait(lock, [this] { return !log_queue_.empty() || stop_flag_; });

            if (stop_flag_ && log_queue_.empty()) break;

            
            queue<string> local_batch;
            size_t count = 0;
            
            while (!log_queue_.empty() && count < FIXED_BATCH_LIMIT) {
                local_batch.push(log_queue_.front());
                log_queue_.pop();
                count++;
            }
            
            cv_full_.notify_all(); 
            lock.unlock(); 

            string batch_data;
            while (!local_batch.empty()) {
                batch_data += local_batch.front() + "\n";
                local_batch.pop();
            }

            if (fd != -1 && !batch_data.empty()) {
                write(fd, batch_data.c_str(), batch_data.size());
                fdatasync(fd); 
            }
        }
        if (fd != -1) close(fd);
    }

    queue<string> log_queue_;
    mutex queue_mutex_;
    condition_variable cv_empty_;
    condition_variable cv_full_;
    bool stop_flag_;
    size_t max_queue_size_;
    thread logger_thread_;
};

class ShardedKVCache {
public:
    ShardedKVCache() : shards_(16) {}

    void create(const string& key, const string& value) {
        size_t idx = get_shard_idx(key);
        lock_guard<mutex> guard(shards_[idx].mtx);
        shards_[idx].data[key] = value;
    }

    string read(const string& key) {
        size_t idx = get_shard_idx(key);
        lock_guard<mutex> guard(shards_[idx].mtx);
        auto it = shards_[idx].data.find(key);
        if (it != shards_[idx].data.end()) {
            return it->second;
        }
        return "";
    }

    void del(const string& key) {
        size_t idx = get_shard_idx(key);
        lock_guard<mutex> guard(shards_[idx].mtx);
        shards_[idx].data.erase(key);
    }

private:
    struct Shard {
        mutex mtx;
        unordered_map<string, string> data;
    };

    vector<Shard> shards_;

    size_t get_shard_idx(const string& key) {
        hash<string> hasher;
        return hasher(key) % shards_.size();
    }
};

class ConnectionPool {
    string url_, user_, pass_, schema_;
    int pool_size_;
    list<shared_ptr<sql::Connection>> pool_;
    mutex pool_mutex_;
    condition_variable pool_cv_;

public:
    ConnectionPool(string url, string user, string pass, string schema, int size)
        : url_(url), user_(user), pass_(pass), schema_(schema), pool_size_(size) {
        
        sql::Driver* driver = get_driver_instance();
        for (int i = 0; i < pool_size_; ++i) {
            try {
                shared_ptr<sql::Connection> con(driver->connect(url_, user_, pass_));
                con->setSchema(schema_);
                pool_.push_back(con);
            } catch (sql::SQLException &e) {
                cerr << "Error creating connection pool: " << e.what() << endl;
            }
        }
        cout << "[POOL] Initialized with " << pool_.size() << " connections." << endl;
    }

    shared_ptr<sql::Connection> getConnection() {
        unique_lock<mutex> lock(pool_mutex_);
        pool_cv_.wait(lock, [this] { return !pool_.empty(); });

        auto con = pool_.front();
        pool_.pop_front();
        return con;
    }

    void releaseConnection(shared_ptr<sql::Connection> con) {
        unique_lock<mutex> lock(pool_mutex_);
        pool_.push_back(con);
        lock.unlock();
        pool_cv_.notify_one();
    }
};

class DBManager {
public:
    DBManager(shared_ptr<BoundedAsyncWALLogger> logger) : logger_(logger) {
        pool_ = make_unique<ConnectionPool>(
            "tcp://127.0.0.1:3306", "kv_server_user", "MyProjectPassword123!", "kv_store", 20
        );
    }

    void create(const string& key, const string& value) {
       
        logger_->log(key + ":" + value);
        auto con = pool_->getConnection();
        try {
            unique_ptr<sql::PreparedStatement> pstmt;
            pstmt.reset(con->prepareStatement(
                "INSERT INTO kv_pairs (id, value) VALUES (?, ?) ON DUPLICATE KEY UPDATE value = ?"
            ));
            pstmt->setString(1, key);
            pstmt->setString(2, value);
            pstmt->setString(3, value);
            pstmt->execute();
        } catch (sql::SQLException &e) {
            cerr << "DB Error: " << e.what() << endl;
        }
        pool_->releaseConnection(con);
    }

    string read(const string& key) {
        auto con = pool_->getConnection();
        string result = "";
        try {
            unique_ptr<sql::PreparedStatement> pstmt;
            unique_ptr<sql::ResultSet> res;
            pstmt.reset(con->prepareStatement("SELECT value FROM kv_pairs WHERE id = ?"));
            pstmt->setString(1, key);
            res.reset(pstmt->executeQuery());
            
            if (res->next()) {
                result = res->getString("value");
            }
        } catch (sql::SQLException &e) {}
        pool_->releaseConnection(con);
        return result;
    }

    void del(const string& key) {
        auto con = pool_->getConnection();
        try {
            unique_ptr<sql::PreparedStatement> pstmt;
            pstmt.reset(con->prepareStatement("DELETE FROM kv_pairs WHERE id = ?"));
            pstmt->setString(1, key);
            pstmt->execute();
        } catch (sql::SQLException &e) {}
        pool_->releaseConnection(con);
    }

private:
    unique_ptr<ConnectionPool> pool_;
    shared_ptr<BoundedAsyncWALLogger> logger_;
};


int main() {
    try {
        struct rlimit limit;
        if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
            limit.rlim_cur = 65535;
            if (limit.rlim_max < 65535) limit.rlim_max = 65535;
            setrlimit(RLIMIT_NOFILE, &limit);
        }

        httplib::Server svr;
        auto logger = make_shared<BoundedAsyncWALLogger>(); 
        auto cache = make_shared<ShardedKVCache>();
        auto db = make_shared<DBManager>(logger);

        svr.Post("/kv", [cache, db](const httplib::Request& req, httplib::Response& res) {
            if (req.has_param("key") && req.has_param("value")) {
                string key = req.get_param_value("key");
                string value = req.get_param_value("value");

                db->create(key, value);
                cache->create(key, value);

                res.set_content("Created", "text/plain");
            } else {
                res.status = 400;
            }
        });

        svr.Get("/kv/:key", [cache, db](const httplib::Request& req, httplib::Response& res) {
            string key = req.path_params.at("key");

            string value = cache->read(key);
            if (!value.empty()) {
                perform_heavy_computation(value);
                res.set_content(value, "text/plain");
            } else {
                value = db->read(key);
                if (!value.empty()) {
                    cache->create(key, value);
                    res.set_content(value, "text/plain");
                } else {
                    res.status = 404;
                    res.set_content("Key not found", "text/plain");
                }
            }
        });

        svr.Delete("/kv/:key", [cache, db](const httplib::Request& req, httplib::Response& res) {
            string key = req.path_params.at("key");
            db->del(key);
            cache->del(key);
            res.set_content("Deleted " + key, "text/plain");
        });

        int port = 8080;
        cout << "Starting server (Fixed Batch Cap 100) on port " << port << "..." << endl;
        if (!svr.listen("0.0.0.0", port)) {
            return 1;
        }

    } catch (const exception& e) {
        cerr << "Fatal Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}