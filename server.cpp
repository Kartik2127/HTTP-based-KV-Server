#include "httplib.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <stdexcept>

#include "cppconn/driver.h"
#include "cppconn/exception.h"
#include "cppconn/prepared_statement.h"
#include "cppconn/resultset.h"
#include "cppconn/statement.h"

using namespace std;

class KVCache {
public:
    void create(const string& key, const string& value) {
        lock_guard<mutex> guard(cache_mutex_);
        cache_[key] = value;
        cout << "[CACHE CREATE] Key: " << key << endl;
    }

    string read(const string& key) {
        lock_guard<mutex> guard(cache_mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            cout << "[CACHE HIT] Key: " << key << endl;
            return it->second;
        } else {
            cout << "[CACHE MISS] Key: " << key << endl;
            return "";
        }
    }

    void del(const string& key) {
        lock_guard<mutex> guard(cache_mutex_);
        if (cache_.find(key) != cache_.end()) {
            cache_.erase(key);
            cout << "[CACHE DELETE] Key: " << key << endl;
        }
    }

private:
    mutex cache_mutex_;
    unordered_map<string, string> cache_;
};

class DBManager {
public:
    DBManager() {
        try {
            driver_ = get_driver_instance();
            con_.reset(driver_->connect("tcp://127.0.0.1:3306", "kv_server_user", "MyProjectPassword123!"));
            con_->setSchema("kv_store");
            cout << "[DB] Successfully connected to MySQL." << endl;
        } catch (sql::SQLException &e) {
            cerr << "[DB ERROR] Failed to connect: " << e.what() << endl;
            throw;
        }
    }

    void create(const string& key, const string& value) {
        lock_guard<mutex> guard(db_mutex_);
        try {
            unique_ptr<sql::PreparedStatement> pstmt;
            pstmt.reset(con_->prepareStatement(
                "INSERT INTO kv_pairs (id, value) VALUES (?, ?) "
                "ON DUPLICATE KEY UPDATE value = ?"
            ));
            
            pstmt->setString(1, key);
            pstmt->setString(2, value);
            pstmt->setString(3, value);
            pstmt->execute();
            
            cout << "[DB CREATE] Key: " << key << endl;
        } catch (sql::SQLException &e) {
            cerr << "[DB ERROR] Create failed: " << e.what() << endl;
        }
    }

    string read(const string& key) {
        lock_guard<mutex> guard(db_mutex_);
        try {
            unique_ptr<sql::PreparedStatement> pstmt;
            unique_ptr<sql::ResultSet> res;
            
            pstmt.reset(con_->prepareStatement("SELECT value FROM kv_pairs WHERE id = ?"));
            pstmt->setString(1, key);
            res.reset(pstmt->executeQuery());
            
            if (res->next()) {
                cout << "[DB READ] Key: " << key << endl;
                return res->getString("value");
            } else {
                cout << "[DB NOT FOUND] Key: " << key << endl;
                return "";
            }
        } catch (sql::SQLException &e) {
            cerr << "[DB ERROR] Read failed: " << e.what() << endl;
            return "";
        }
    }

    void del(const string& key) {
        lock_guard<mutex> guard(db_mutex_);
        try {
            unique_ptr<sql::PreparedStatement> pstmt;
            pstmt.reset(con_->prepareStatement("DELETE FROM kv_pairs WHERE id = ?"));
            pstmt->setString(1, key);
            pstmt->execute();
            cout << "[DB DELETE] Key: " << key << endl;
        } catch (sql::SQLException &e) {
            cerr << "[DB ERROR] Delete failed: " << e.what() << endl;
        }
    }

private:
    mutex db_mutex_;
    sql::Driver* driver_;
    unique_ptr<sql::Connection> con_;
};

int main() {
    try {
        httplib::Server svr;
        auto cache = make_shared<KVCache>();
        auto db = make_shared<DBManager>();

        svr.Post("/kv", [cache, db](const httplib::Request& req, httplib::Response& res) {
            if (req.has_param("key") && req.has_param("value")) {
                string key = req.get_param_value("key");
                string value = req.get_param_value("value");

                db->create(key, value);
                cache->create(key, value);

                res.set_content("Created: " + key + "=" + value, "text/plain");
            } else {
                res.status = 400;
                res.set_content("Missing 'key' or 'value' in form data", "text/plain");
            }
        });

        svr.Get("/kv/:key", [cache, db](const httplib::Request& req, httplib::Response& res) {
            string key = req.path_params.at("key");

            string value = cache->read(key);
            if (!value.empty()) {
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
        cout << "Server starting on port " << port << "..." << endl;

        if (!svr.listen("0.0.0.0", port)) {
            cerr << "Failed to start server!" << endl;
            return 1;
        }

    } catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
