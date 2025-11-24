#include "httplib.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <sstream>
#include <random>
#include <iomanip>

using namespace std;

atomic<long long> total_requests_completed(0);
atomic<long long> total_requests_failed(0);
atomic<long long> total_response_time_ms(0);
atomic<bool> test_running(true);

int popular_key_count = 100;
vector<string> popular_keys;

void monitor_performance() {
    long long last_requests = 0;
    long long last_response_time = 0;

    while (test_running) {
        this_thread::sleep_for(chrono::seconds(1));
        
        long long current_requests = total_requests_completed.load();
        long long current_response_time = total_response_time_ms.load();
        long long failed = total_requests_failed.load();
        
        long long delta_requests = current_requests - last_requests;
        long long delta_time = current_response_time - last_response_time;
        
        double throughput = (double)delta_requests; 
        double avg_latency = (delta_requests > 0) ? (double)delta_time / delta_requests : 0.0;

        cout << "[METRICS] " << fixed << setprecision(2) << throughput << " " << avg_latency << " " << failed << endl;

        last_requests = current_requests;
        last_response_time = current_response_time;
    }
}

void warmup(const string& host, int port) {
    httplib::Client cli(host, port);
    cli.set_connection_timeout(5, 0);

    for (int i = 0; i < popular_key_count; i++) {
        string key = "Popular_key_" + to_string(i);
        popular_keys.push_back(key);

        httplib::Params params;
        params.emplace("key", key);
        params.emplace("value", "Popular_value_" + to_string(i));
        
        cli.Post("/kv", params);
    }
}

void put_all_task(const string& host, int port, int duration_seconds, int thread_id) {
    httplib::Client cli(host, port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);
    
    auto start_time = chrono::steady_clock::now();
    auto end_time = start_time + chrono::seconds(duration_seconds);

    int request_count = 0;
    while (chrono::steady_clock::now() < end_time) {
        stringstream ss_key;
        ss_key << "key_" << thread_id << "_" << request_count;
        
        httplib::Params params;
        params.emplace("key", ss_key.str());
        params.emplace("value", "Value_data_payload_12345");

        auto req_start = chrono::steady_clock::now();
        
        try {
            auto res = cli.Post("/kv", params);
            auto req_end = chrono::steady_clock::now();

            if (res && res->status == 200) {
                total_requests_completed++;
                total_response_time_ms += chrono::duration_cast<chrono::milliseconds>(req_end - req_start).count();
            } else {
                total_requests_failed++;
            }
        } catch (...) {
            total_requests_failed++;
        }
        request_count++;
    }
}

void get_popular_task(const string& host, int port, int duration_seconds, int thread_id) {
    httplib::Client cli(host, port);
    cli.set_connection_timeout(2, 0);
    
    auto start_time = chrono::steady_clock::now();
    auto end_time = start_time + chrono::seconds(duration_seconds);
    mt19937 gen(hash<thread::id>{}(this_thread::get_id()));
    uniform_int_distribution<> dist(0, popular_key_count - 1);

    while (chrono::steady_clock::now() < end_time) {
        string key = popular_keys[dist(gen)];
        auto req_start = chrono::steady_clock::now();
        
        try {
            auto res = cli.Get(("/kv/" + key).c_str());
            auto req_end = chrono::steady_clock::now();

            if (res && res->status == 200) {
                total_requests_completed++;
                total_response_time_ms += chrono::duration_cast<chrono::milliseconds>(req_end - req_start).count();
            } else {
                total_requests_failed++;
            }
        } catch (...) {
            total_requests_failed++;
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 6) return 1;

    string host = argv[1];
    int port = stoi(argv[2]);
    int num_threads = stoi(argv[3]);
    int duration_seconds = stoi(argv[4]);
    string workload_type = argv[5];

    function<void(const std::string&, int, int, int)> task_function;

    if (workload_type == "put_all") {
        task_function = put_all_task;
    } else if (workload_type == "get_popular") {
        warmup(host, port);
        task_function = get_popular_task;
    }

    thread monitor(monitor_performance);
    vector<thread> threads;
    
    auto start_clock = chrono::steady_clock::now();

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(task_function, host, port, duration_seconds, i);
    }

    for (auto& t : threads) t.join();
    
    auto end_clock = chrono::steady_clock::now();
    
    test_running = false;
    monitor.join();

    auto duration_ms = chrono::duration_cast<chrono::milliseconds>(end_clock - start_clock).count();
    double duration_s = duration_ms / 1000.0;
    long long total_reqs = total_requests_completed.load();
    long long total_time = total_response_time_ms.load();
    long long total_fails = total_requests_failed.load();
    
    double avg_throughput = (duration_s > 0) ? total_reqs / duration_s : 0.0;
    double avg_latency = (total_reqs > 0) ? (double)total_time / total_reqs : 0.0;

    cout << "\n========================================" << endl;
    cout << "Final Benchmark Results" << endl;
    cout << "========================================" << endl;
    cout << "Duration           : " << duration_s << " s" << endl;
    cout << "Total Requests     : " << total_reqs << endl;
    cout << "Total Failed       : " << total_fails << endl;
    cout << "Average Throughput : " << fixed << setprecision(2) << avg_throughput << " req/s" << endl;
    cout << "Average Latency    : " << fixed << setprecision(2) << avg_latency << " ms" << endl;
    cout << "========================================" << endl;

    return 0;
}