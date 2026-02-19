#include <fl/string.hpp>
#include <fl/immutable_string.hpp>
#include <fl/synchronised_string.hpp>
#include <thread>
#include <vector>
#include <iostream>
#include <string>

// SCENARIO 1: Configuration data - immutable, shared across threads
// This is the ideal use case for fl::immutable_string (atomic refcounting).
class Configuration {
    fl::immutable_string app_name_;
    fl::immutable_string db_url_;
    
public:
    Configuration(const char* name, const char* url)
        : app_name_(name), db_url_(url) {}
    
    // Safe and lock-free to return by value (O(1) atomic increment)
    fl::immutable_string app_name() const { return app_name_; }
    fl::immutable_string db_url() const { return db_url_; }
};

// SCENARIO 2: Thread-local processing - no synchronisation needed
// Use fl::string for maximum performance when only one thread owns the data.
void worker_local_processing(int id) {
    fl::string buffer;
    buffer += "Worker ";
    buffer += std::string_view(std::to_string(id));
    buffer += " processing data locally...";
    // ... work with buffer ...
}

// SCENARIO 3: Shared mutable state - explicit synchronisation
// Use fl::synchronised_string when multiple threads must modify the same string.
class SharedLogger {
    fl::synchronised_string log_buffer_;
    
public:
    void log(const char* msg) {
        // High-level API automatically handles locking
        log_buffer_ += msg;
        log_buffer_ += "\n";
    }
    
    void flush() {
        // Read-only access via callback: ensures consistent snapshot while printing
        log_buffer_.read([](const fl::string& s) {
            std::cout << "--- LOG DATA ---\n" << s << "----------------\n";
        });
        
        // Clear via callback
        log_buffer_.write([](fl::string& s) {
            s.clear();
        });
    }
};

int main() {
    std::cout << "fl Library - Thread-Safety Usage Patterns Examples\n\n";

    // 1. Immutable Config Pattern
    Configuration config("FlDemoApp", "sqlite:///demo.db");
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&config, i]() {
            // Concurrent read-only access is safe and lock-free
            auto url = config.db_url();
            (void)url;
            worker_local_processing(i);
        });
    }
    
    for (auto& t : threads) t.join();
    std::cout << "Configuration shared successfully across threads.\n";

    // 2. Synchronised Logger Pattern
    SharedLogger logger;
    threads.clear();
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&logger, i]() {
            for (int j = 0; j < 5; ++j) {
                std::string msg = "Thread " + std::to_string(i) + " report " + std::to_string(j);
                logger.log(msg.c_str());
            }
        });
    }
    
    for (auto& t : threads) t.join();
    logger.flush();

    std::cout << "\nExample patterns completed successfully.\n";
    return 0;
}
