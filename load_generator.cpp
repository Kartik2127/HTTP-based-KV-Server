#include "httplib.h"
#include<iostream>
#include<string>
#include<vector>
#include<thread>
#include<chrono>
#include<atomic>
#include <functional>
#include<sstream>
#include<random>

using namespace  std;  
atomic<int> total_requests_completed(0);
atomic<long long> total_response_time_ms(0);

int popular_key_count=100;
vector<string> popular_keys;

void warmup(const string& host, int port){
    cout << "Populating the server with " << popular_key_count << " keys..." << flush;
    httplib::Client cli(host,port);

    for(int i=0;i<popular_key_count;i++){
        string key="Popular_key_"+to_string(i);
        popular_keys.push_back(key);

        httplib::Params params;
        params.emplace("key",key);
        params.emplace("value","Popular_value_"+to_string(i));
        cli.Post("/kv",params);
    }
    cout << " Done." << endl;
}

void put_all_task(const string& host,int port, int duration_seconds,int thread_id){
    httplib::Client cli(host,port);
    cli.set_connection_timeout(5,0);

    auto start_time=chrono::steady_clock::now();
    auto end_time=start_time+ chrono::seconds(duration_seconds);

    int request_count=0;
    while(chrono::steady_clock::now()<end_time){
        stringstream ss_key;
        ss_key<<"key_"<<thread_id<<"_"<<request_count;
        string key=ss_key.str();
        string value="Value_data";

        httplib::Params params;
        params.emplace("key",key);
        params.emplace("value",value);

        auto req_start_time=chrono::steady_clock::now();
        auto res=cli.Post("/kv",params);
        auto req_end_time=chrono::steady_clock::now();

        if(res and res->status==200){
            total_requests_completed++;
            auto response_time_ms=chrono::duration_cast<chrono::milliseconds>(req_end_time-req_start_time).count();
            total_response_time_ms+=response_time_ms;
        }
        else{
             cerr << "Error in request on thread " << thread_id << endl;
        }
        request_count++;
    }
}

void get_popular_task(const string& host,int port, int duration_seconds, int thread_id){
    httplib::Client cli(host,port);
    cli.set_connection_timeout(5,0);

    auto start_time=chrono::steady_clock::now();
    auto end_time=start_time+ chrono::seconds(duration_seconds);
    mt19937 gen(hash<thread::id>{}(this_thread::get_id()));
    uniform_int_distribution<> dist(0, popular_key_count - 1);

    while(chrono::steady_clock::now()<end_time){
        string key=popular_keys[dist(gen)];
        string path="/kv/" + key;
        auto req_start_time =chrono::steady_clock::now();
        auto res = cli.Get(path.c_str()); 
        auto req_end_time = std::chrono::steady_clock::now();

        if(res and res->status==200){
            total_requests_completed++;
            auto response_time_ms =chrono::duration_cast<chrono::milliseconds>(req_end_time - req_start_time).count();
            total_response_time_ms += response_time_ms;
        }
        else{
            cerr << "Error in request on thread " << thread_id << endl;
        }
    }
}

int main(int argc, char** argv){
    if(argc!=6){
        cerr<<"Error: Expected Number of Arguments Not Present"<<endl;
        return 1;
    }

    string host=argv[1];
    int port=stoi(argv[2]);
    int num_threads=stoi(argv[3]);
    int duration_seconds=stoi(argv[4]);
    string workload_type = argv[5];

    cout << "--- Starting Load Generator ---" <<endl;
    cout << "Target: " << host << ":" << port <<endl;
    cout << "Threads (Users): " << num_threads <<endl;
    cout << "Duration: " << duration_seconds << " seconds" <<endl;
    cout << "Workload: " << workload_type <<endl;

    function<void(const std::string&, int, int, int)> task_function;

    if (workload_type == "put_all") {
        task_function = put_all_task;
    } else if (workload_type == "get_popular") {
        task_function = get_popular_task;
        warmup(host, port);
    } else {
        cerr << "Error: Unknown workload type '" << workload_type << "'" <<endl;
        return 1;
    }

    vector<thread> threads;
    auto test_start_time=chrono::steady_clock::now();

    for(int i=0;i<num_threads;i++){
        threads.emplace_back(task_function,host,port,duration_seconds,i);
    }

    cout << "\nTest running... " <<flush;
    for (auto& t : threads) {
        t.join();
    }
    cout << "Done." <<endl;

    auto test_end_time =chrono::steady_clock::now();
    double test_duration_actual =chrono::duration<double>(test_end_time - test_start_time).count();
    cout << "\n--- Load Test Finished ---" <<endl;

    int total_requests=total_requests_completed.load();
    long long total_time_ms=total_response_time_ms.load();

    double average_throughput=static_cast<double>(total_requests)/test_duration_actual;
    double average_response_time= total_requests > 0 ? static_cast<double>(total_time_ms) / total_requests : 0.0;

    cout << "Total test duration: " << test_duration_actual << " seconds" <<endl;
    cout << "Total requests completed: " << total_requests <<endl;
    cout << "Average Throughput: " << average_throughput << " reqs/sec" <<endl;
    cout << "Average Response Time: " << average_response_time << " ms" <<endl;

    return 0;
}