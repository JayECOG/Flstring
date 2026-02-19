#include "../include/fl.hpp"
#include <iostream>
#include <unordered_map>
#include <vector>

int main() {
    std::cout << "\n╔══════════════════════════════════════════════╗\n"
              << "║  Advanced fl String Types Examples           ║\n"
              << "╚══════════════════════════════════════════════╝\n\n";

    // =========== Example 1: substring_view for Efficient Slicing ===========
    std::cout << "1. substring_view: Lightweight, Non-Owning Views\n"
              << "───────────────────────────────────────────────\n\n";
    {
        fl::string data("Hello, World! Welcome to fl library.");
        
        // Create views into the string without copying
        fl::substring_view greeting(data.data(), 5);  // "Hello"
        fl::substring_view world(data.data() + 7, 5); // "World"
        
        std::cout << "Original string: " << data << "\n";
        std::cout << "View 1 (greeting): ";
        for (char c : greeting) std::cout << c;
        std::cout << "\nView 2 (world): ";
        for (char c : world) std::cout << c;
        std::cout << "\n\n";

        // Views enable efficient substring searching without allocation
        std::cout << "Substring operations on views:\n";
        std::cout << "  - Contains 'World': " << (greeting.contains("World") ? "Yes" : "No") << "\n";
        std::cout << "  - Starts with 'Hello': " 
                  << (greeting.starts_with(fl::substring_view("Hello", 5)) ? "Yes" : "No") 
                  << "\n";
        
        // Extract substring from view
        fl::substring_view sub = greeting.substr(1, 3);  // "ell"
        std::cout << "  - Substring of view [1:3]: ";
        for (char c : sub) std::cout << c;
        std::cout << "\n\n";
    }

    // =========== Example 2: Rope for Efficient Concatenation ===========
    std::cout << "2. rope: O(1) Concatenation with Tree Structure\n"
              << "───────────────────────────────────────────────\n\n";
    {
        std::cout << "Building a long string through repeated concatenation:\n\n";

        // Build a document through many concatenations
        fl::rope document;
        
        fl::rope header("=== Document Title ===\n");
        fl::rope section1("Section 1: Introduction\n");
        fl::rope content1("This is the introduction content.\n");
        fl::rope section2("Section 2: Details\n");
        fl::rope content2("Here are the detailed specifications.\n");
        fl::rope footer("=== End of Document ===\n");

        // Concatenate in tree structure (O(1) per operation)
        std::cout << "Concatenating 6 fragments:\n";
        document = header + section1 + content1 + section2 + content2 + footer;
        std::cout << "  - Total size: " << document.size() << " bytes\n";
        std::cout << "  - Tree depth: " << document.depth() << "\n\n";

        // Linearise for output
        std::cout << "Flattened document:\n";
        std::cout << document.to_std_string() << "\n";

        // Character access (O(log n) through tree traversal)
        std::cout << "Character access:\n";
        std::cout << "  - First character: '" << document[0] << "'\n";
        std::cout << "  - Character at position 30: '" << document[30] << "'\n";
        std::cout << "  - Last character: '" << document[document.size() - 1] << "'\n\n";
    }

    // =========== Example 3: Rope for Streaming Data ===========
    std::cout << "3. rope: Efficient Log Aggregation\n"
              << "──────────────────────────────────\n\n";
    {
        std::cout << "Aggregating log entries from multiple sources:\n\n";

        // Simulate collecting log entries from different sources
        std::vector<std::string> log_sources = {
            "[INFO] System startup completed",
            "[WARN] Low memory condition detected",
            "[INFO] User session initialized",
            "[ERROR] Connection timeout occurred",
            "[INFO] Recovery procedure started"
        };

        fl::rope log_entries;
        for (size_t i = 0; i < log_sources.size(); ++i) {
            if (i > 0) log_entries = log_entries + "\n";
            log_entries = log_entries + log_sources[i];
        }

        std::cout << "Aggregated " << log_sources.size() << " log entries:\n";
        std::cout << log_entries << "\n\n";

        // Extract substring for filtering
        fl::substring_view full_logs = log_entries.substr(0, log_entries.size());
        std::cout << "Count of ERROR entries: ";
        int error_count = 0;
        for (size_t pos = 0; (pos = full_logs.find("[ERROR]", pos)) != fl::substring_view::npos; ++pos) {
            ++error_count;
        }
        std::cout << error_count << "\n\n";
    }

    // =========== Example 4: immutable_string_view for Map Keys ===========
    std::cout << "4. immutable_string_view: Optimised Map Keys\n"
              << "─────────────────────────────────────────────\n\n";
    {
        std::cout << "Using immutable views as hash map keys:\n\n";

        // Create a map of configuration settings
        std::unordered_map<fl::immutable_string_view, std::string,
                          fl::immutable_string_hash,
                          fl::immutable_string_equal> config;

        // Use literal strings directly (no allocation for views)
        config[fl::immutable_string_view("database.host")] = "localhost";
        config[fl::immutable_string_view("database.port")] = "5432";
        config[fl::immutable_string_view("cache.enabled")] = "true";
        config[fl::immutable_string_view("cache.ttl")] = "3600";

        std::cout << "Configuration entries:\n";
        for (const auto& [key, value] : config) {
            std::cout << "  " << key << " = " << value << "\n";
        }
        std::cout << "\n";

        // Hash caching makes repeated lookups efficient
        fl::immutable_string_view db_port("database.port");
        std::cout << "Repeated lookups (benefits from hash caching):\n";
        for (int i = 0; i < 3; ++i) {
            auto it = config.find(db_port);
            std::cout << "  Lookup " << i+1 << ": " << it->second << "\n";
        }
        std::cout << "\n";
    }

    // =========== Example 5: Owning Immutable Strings ===========
    std::cout << "5. owning_immutable_string: Persistent Map Keys\n"
              << "───────────────────────────────────────────────\n\n";
    {
        std::cout << "Using owning strings for persistent storage:\n\n";

        // Simulate collecting keys that might not persist
        std::vector<std::string> temp_keys = {
            "user123",
            "session456",
            "cache789"
        };

        // Create owning copies for long-term storage
        std::unordered_map<fl::owning_immutable_string, int,
                          fl::immutable_string_hash,
                          fl::immutable_string_equal> key_counts;

        for (const auto& key : temp_keys) {
            fl::owning_immutable_string owned_key(key);
            key_counts[owned_key] = 0;
            // temp key goes out of scope; owned_key retains a copy
        }

        std::cout << "Keys stored (survived source destruction):\n";
        for (auto& [key, count] : key_counts) {
            std::cout << "  " << key << ": count=" << count << "\n";
        }
        std::cout << "\n";

        // Increment counters
        fl::owning_immutable_string lookup_key("user123");
        key_counts[lookup_key]++;
        
        std::cout << "After incrementing user123:\n";
        for (auto& [key, count] : key_counts) {
            std::cout << "  " << key << ": count=" << count << "\n";
        }
        std::cout << "\n";
    }

    // =========== Example 6: Combined Usage Pattern ===========
    std::cout << "6. Combined Usage: Building Efficient Collections\n"
              << "────────────────────────────────────────────────\n\n";
    {
        std::cout << "Pattern: Use views for read-only, owning for persistent:\n\n";

        // Build a document using efficient concatenation
        fl::rope document;
        document = document + "Project Summary\n";
        document = document + "================\n\n";
        document = document + "Status: In Progress\n";
        document = document + "Team Members: Alice, Bob, Charlie\n";

        // Convert to fl::string for processing
        fl::string doc_str = document.flatten();

        // Extract views from the string
        fl::substring_view first_line(doc_str.data(), 16);  // "Project Summary\n"
        
        std::cout << "Document built through:\n";
        std::cout << "  1. rope (efficient concatenation)\n";
        std::cout << "  2. fl::string (conversion to linear)\n";
        std::cout << "  3. substring_view (efficient slicing)\n\n";

        std::cout << "Result:\n" << doc_str << "\n";
    }

    // =========== Example 7: Performance Considerations ===========
    std::cout << "7. Performance: When to Use Each Type\n"
              << "────────────────────────────────────\n\n";
    {
        std::cout << "Type Selection Guide:\n\n";
        
        std::cout << "substring_view:\n"
                  << "  Use: When you need lightweight slices without copying\n"
                  << "  Best for: Search operations, filtering, string processing\n"
                  << "  Caveat: Requires source string to remain valid\n\n";

        std::cout << "rope:\n"
                  << "  Use: When building strings from many fragments\n"
                  << "  Best for: Logging, document assembly, streaming data\n"
                  << "  Caveat: Character access is O(log n), not O(1)\n\n";

        std::cout << "immutable_string_view:\n"
                  << "  Use: As keys in associative containers\n"
                  << "  Best for: Hash maps, interned strings, read-only data\n"
                  << "  Caveat: Requires stable memory (don't pass temporaries)\n\n";

        std::cout << "owning_immutable_string:\n"
                  << "  Use: When you need to persist string keys\n"
                  << "  Best for: Long-lived map entries, copying from temporaries\n"
                  << "  Advantage: Safe, manages its own memory\n\n";
    }

    std::cout << "╔══════════════════════════════════════════════╗\n"
              << "║         Examples Completed Successfully      ║\n"
              << "╚══════════════════════════════════════════════╝\n\n";

    return 0;
}
