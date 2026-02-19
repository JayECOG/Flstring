# Examples from the `fl` Library

A curated collection of examples from the `fl` library, organised by topic to illustrate the various features.

## Basic Usage (`fl::string`)

This example demonstrates the basic usage of the `fl::string` class, including its Small-String Optimisation (SSO), basic operations, and capacity management.

```cpp
#include <iostream>
#include <fl.hpp>

// Basic fl::string usage examples
int main() {
    std::cout << "=== fl library examples ===" << std::endl;

    // Example 1: Small-string optimisation
    {
        std::cout << "1. Small-string optimisation:" << std::endl;

        fl::string short_str("Hi");
        std::cout << "  Short string: '" << short_str.c_str() << "'" << std::endl;
        std::cout << "  Size: " << short_str.size() << ", Capacity: " << short_str.capacity() << std::endl;
        std::cout << "  Uses SSO (stack storage)" << std::endl << std::endl;

        fl::string medium_str("This is a medium length string here");
        std::cout << "  Medium string size: " << medium_str.size() << std::endl;
        std::cout << "  Capacity: " << medium_str.capacity() << std::endl << std::endl;

        fl::string long_str("This is a very long string that definitely exceeds the SSO threshold and will use heap allocation");
        std::cout << "  Long string size: " << long_str.size() << std::endl;
        std::cout << "  Capacity: " << long_str.capacity() << " (heap allocated)" << std::endl << std::endl;
    }

    // Example 2: String operations
    {
        std::cout << "2. String operations:" << std::endl;

        fl::string str("Hello");
        std::cout << "  Original: '" << str.c_str() << "'" << std::endl;

        str.append(" World");
        std::cout << "  After append: '" << str.c_str() << "'" << std::endl;

        str.push_back('!');
        std::cout << "  After push_back: '" << str.c_str() << "'" << std::endl;

        fl::string sub = str.substr(0, 5);
        std::cout << "  Substring (0, 5): '" << sub.c_str() << "'" << std::endl << std::endl;
    }

    // Example 3: Searching and comparison
    {
        std::cout << "3. Search and comparison:" << std::endl;

        fl::string text("The quick brown fox jumps over the lazy dog");
        std::cout << "  Text: '" << text.c_str() << "'" << std::endl;

        auto pos = text.find("brown");
        if (pos != fl::string::npos) {
            std::cout << "  Found 'brown' at position: " << pos << std::endl;
        }

        auto char_pos = text.find('q');
        if (char_pos != fl::string::npos) {
            std::cout << "  Found 'q' at position: " << char_pos << std::endl;
        }

        fl::string s1("abc");
        fl::string s2("abc");
        std::cout << "  'abc' == 'abc': " << (s1 == s2 ? "true" : "false") << std::endl << std::endl;
    }

    // Example 4: Iterator support
    {
        std::cout << "4. Iterator support:" << std::endl;

        fl::string str("Iterator");
        std::cout << "  String: '" << str.c_str() << "'" << std::endl;
        std::cout << "  Characters: ";
        for (auto ch : str) {
            std::cout << ch << " ";
        }
        std::cout << std::endl << std::endl;
    }

    // Example 5: Capacity management
    {
        std::cout << "5. Capacity management:" << std::endl;

        fl::string str;
        std::cout << "  Empty string capacity: " << str.capacity() << std::endl;

        str.reserve(200);
        std::cout << "  After reserve(200): " << str.capacity() << std::endl;

        str = "Small";
        str.shrink_to_fit();
        std::cout << "  After shrink_to_fit with 'Small': " << str.capacity() << std::endl << std::endl;
    }

    // Example 6: String literals
    {
        std::cout << "6. String literals:" << std::endl;

        using namespace fl;
        fl::string s = "Literal"_fs;
        std::cout << "  Using _fs literal: '" << s.c_str() << "'" << std::endl << std::endl;
    }

    // Example 7: Copy and move semantics
    {
        std::cout << "7. Copy and move semantics:" << std::endl;

        fl::string original("Original Data");

        fl::string copy = original;
        std::cout << "  Copy: '" << copy.c_str() << "'" << std::endl;

        fl::string moved = std::move(original);
        std::cout << "  Moved: '" << moved.c_str() << "'" << std::endl;
        std::cout << "  Original after move: (empty: " << (original.empty() ? "true" : "false") << ")" << std::endl << std::endl;
    }

    std::cout << "=== Basic fl::string examples completed ===" << std::endl << std::endl;
    return 0;
}
```

## The `string_builder`

This example demonstrates the use of the `fl::string_builder` for efficiently composing strings from multiple fragments.

```cpp
#include <iostream>
#include <fl.hpp>

// String builder pattern examples
int main() {
    std::cout << "=== String Builder Examples ===" << std::endl << std::endl;

    // Example 1: Basic builder
    {
        std::cout << "1. Basic builder:" << std::endl;

        fl::string_builder builder;
        builder.append("Hello").append(" ").append("World");

        fl::string result = std::move(builder).build();
        std::cout << "  Result: '" << result.c_str() << "'" << std::endl << std::endl;
    }

    // Example 2: Builder with reserve
    {
        std::cout << "2. Builder with capacity management:" << std::endl;

        fl::string_builder builder;
        builder.reserve(100);  // Pre-allocate to avoid reallocations

        for (int i = 0; i < 5; ++i) {
            builder.append("Item");
            if (i < 4) builder.append(", ");
        }

        fl::string result = std::move(builder).build();
        std::cout << "  Result: '" << result.c_str() << "'" << std::endl;
        std::cout << "  Size: " << result.size() << std::endl << std::endl;
    }

    // Example 3: Builder with growth policy
    {
        std::cout << "3. Builder with growth policies:" << std::endl;

        // Linear growth
        {
            fl::string_builder builder;
            builder.set_growth_policy(fl::growth_policy::linear)
                   .set_linear_growth(32);

            builder.append("Growing by fixed increments");
            fl::string result = std::move(builder).build();
            std::cout << "  Linear growth result: '" << result.c_str() << "'" << std::endl;
        }

        // Exponential growth
        {
            fl::string_builder builder;
            builder.set_growth_policy(fl::growth_policy::exponential);

            builder.append("Growing exponentially");
            fl::string result = std::move(builder).build();
            std::cout << "  Exponential growth result: '" << result.c_str() << "'" << std::endl << std::endl;
        }
    }

    // Example 4: Efficient list building
    {
        std::cout << "4. Building a CSV-like list:" << std::endl;

        fl::string_builder builder;
        builder.reserve(200);  // Estimate required space

        const int item_count = 10;
        for (int i = 0; i < item_count; ++i) {
            builder.append("Item");
            if (i < item_count - 1) builder.append(", ");
        }

        fl::string result = std::move(builder).build();
        std::cout << "  Result: '" << result.c_str() << "'" << std::endl << std::endl;
    }

    // Example 5: Reserve based on element count
    {
        std::cout << "5. Reserve based on element count:" << std::endl;

        fl::string_builder builder;
        const int element_count = 50;
        const int avg_element_size = 4;

        builder.reserve_for_elements(element_count, avg_element_size);
        std::cout << "  Capacity for " << element_count << " elements of ~"
                  << avg_element_size << " bytes: " << builder.capacity() << std::endl << std::endl;
    }

    // Example 6: Repeat character builder
    {
        std::cout << "6. Building with repeated characters:" << std::endl;

        fl::string_builder builder;
        builder.append("===")
               .append_repeat('=', 10)
               .append("===");

        fl::string result = std::move(builder).build();
        std::cout << "  Result: '" << result.c_str() << "'" << std::endl << std::endl;
    }

    // Example 7: Formatted building (simplified)
    {
        std::cout << "7. Builder with formatted values:" << std::endl;

        fl::string_builder builder;
        int value = 42;
        builder.append("The answer is: ")
               .append_formatted("{}", value);

        fl::string result = std::move(builder).build();
        std::cout << "  Result: '" << result.c_str() << "'" << std::endl << std::endl;
    }

    // Example 8: Builder chaining with clear
    {
        std::cout << "8. Builder reuse with clear:" << std::endl;

        fl::string_builder builder;
        builder.reserve(100);

        // First use
        builder.append("First");
        std::cout << "  First build: '" << builder.data() << "'" << std::endl;

        // Clear and reuse
        builder.clear();
        builder.append("Second");
        std::cout << "  Second build: '" << builder.data() << "'" << std::endl << std::endl;
    }

    // Example 9: Direct data access
    {
        std::cout << "9. Direct buffer access:" << std::endl;

        fl::string_builder builder;
        builder.append("Buffer");

        std::cout << "  Size: " << builder.size() << std::endl;
        std::cout << "  Capacity: " << builder.capacity() << std::endl;
        std::cout << "  First char: '" << builder[0] << "'" << std::endl << std::endl;
    }

    // Example 10: Move semantics
    {
        std::cout << "10. Move semantics:" << std::endl;

        fl::string_builder builder1;
        builder1.reserve(100).append("Source");

        // Move to another builder
        fl::string_builder builder2 = std::move(builder1);

        std::cout << "  Moved builder contains: '" << builder2.data() << "'" << std::endl;
        std::cout << "  Source is now empty: " << (builder1.empty() ? "true" : "false") << std::endl << std::endl;
    }

    std::cout << "=== Builder examples completed ===" << std::endl << std::endl;
    return 0;
}
```

## The Arena Allocator

This example demonstrates the use of the `fl::arena_allocator` and `fl::arena_buffer` for efficient temporary allocations.

```cpp
#include <iostream>
#include <fl.hpp>

// Arena allocator and temporary buffer examples
int main() {
    std::cout << "=== Arena and Temporary Buffer Examples ===" << std::endl << std::endl;

    // Example 1: Basic arena allocator
    {
        std::cout << "1. Basic arena allocator:" << std::endl;

        fl::arena_allocator<1024> arena;

        std::cout << "  Initial available stack: " << arena.available_stack() << " bytes" << std::endl;

        void* ptr1 = arena.allocate(32);
        std::cout << "  After allocating 32 bytes: " << arena.available_stack() << " bytes available" << std::endl;

        void* ptr2 = arena.allocate(64);
        std::cout << "  After allocating 64 bytes: " << arena.available_stack() << " bytes available" << std::endl << std::endl;
    }

    // Example 2: Arena reset
    {
        std::cout << "2. Arena reset:" << std::endl;

        fl::arena_allocator<256> arena;

        std::cout << "  Before allocation: " << arena.available_stack() << " bytes" << std::endl;

        arena.allocate(100);
        std::cout << "  After allocation: " << arena.available_stack() << " bytes" << std::endl;

        arena.reset();
        std::cout << "  After reset: " << arena.available_stack() << " bytes" << std::endl << std::endl;
    }

    // Example 3: Arena overflow to heap
    {
        std::cout << "3. Arena automatic heap fallback:" << std::endl;

        fl::arena_allocator<64> arena;  // Small stack

        std::cout << "  Stack size: 64 bytes" << std::endl;

        void* stack_alloc = arena.allocate(32);
        (void)stack_alloc;
        std::cout << "  Allocated 32 bytes (from stack)" << std::endl;

        void* heap_alloc = arena.allocate(64);  // This exceeds stack, uses heap
        (void)heap_alloc;
        std::cout << "  Allocated 64 bytes (fallback to heap)" << std::endl << std::endl;
    }

    // Example 4: Arena buffer basic usage
    {
        std::cout << "4. Arena buffer:" << std::endl;

        fl::arena_buffer<4096> buf;  // 4KB stack-backed buffer

        buf.append("Part1").append(" ").append("Part2");

        fl::string result = buf.to_string();
        std::cout << "  Content: '" << result.c_str() << "'" << std::endl << std::endl;
    }

    // Example 5: Pooled temporary buffer
    {
        std::cout << "5. Pooled temporary buffer:" << std::endl;

        auto temp = fl::get_pooled_temp_buffer();
        temp->append("Hello").append(" ").append("World");

        fl::string result = temp->to_string();
        std::cout << "  Result: '" << result.c_str() << "'" << std::endl;
        std::cout << "  String size: " << result.size() << std::endl << std::endl;
    }

    // Example 6: Arena buffer with custom stack size
    {
        std::cout << "6. Arena buffer with custom stack size:" << std::endl;

        fl::arena_buffer<2048> temp;  // 2KB stack

        for (int i = 0; i < 10; ++i) {
            temp.append("Item ").append_repeat('*', 1);
            if (i < 9) temp.append(", ");
        }

        fl::string result = temp.to_string();
        std::cout << "  Result: '" << result.c_str() << "'" << std::endl << std::endl;
    }

    // Example 7: Arena buffer clear and reuse
    {
        std::cout << "7. Arena buffer clear and reuse:" << std::endl;

        fl::arena_buffer<4096> buf;

        // First use
        buf.append("First");
        fl::string first = buf.to_string();
        std::cout << "  First use: '" << first.c_str() << "'" << std::endl;

        buf.clear();

        // Reuse
        buf.append("Second");
        fl::string second = buf.to_string();
        std::cout << "  Second use: '" << second.c_str() << "'" << std::endl << std::endl;
    }

    // Example 8: Practical pattern - request builder
    {
        std::cout << "8. Practical pattern - building request:" << std::endl;

        fl::arena_buffer<512> request;

        request.append("GET /api/users HTTP/1.1\r\n");
        request.append("Host: example.com\r\n");
        request.append("Content-Length: 0\r\n");
        request.append("\r\n");

        fl::string final_request = request.to_string();

        std::cout << "  Request built with " << final_request.size() << " bytes" << std::endl;
        std::cout << "  Efficiently built with stack-backed arena" << std::endl << std::endl;
    }

    std::cout << "=== Arena examples completed ===" << std::endl << std::endl;
    return 0;
}
```

## Formatting and Sinks

This example demonstrates the use of the zero-allocation formatting system with various sinks.

```cpp
#include <iostream>
#include <sstream>
#include <fl.hpp>

// Formatting and sink examples
int main() {
    std::cout << "=== Formatting and Sinks Examples ===" << std::endl << std::endl;

    // Example 1: Buffer sink
    {
        std::cout << "1. Buffer sink (zero-allocation formatting):" << std::endl;

        char buffer[256];
        auto sink = fl::make_buffer_sink(buffer);

        sink.write("Hello", 5);
        sink.write(" ", 1);
        sink.write("Buffer", 6);
        sink.null_terminate();

        std::cout << "  Content: '" << buffer << "'" << std::endl;
        std::cout << "  Bytes written: " << sink.written() << std::endl << std::endl;
    }

    // Example 2: Growing sink
    {
        std::cout << "2. Growing sink (dynamic allocation):" << std::endl;

        auto sink = fl::make_growing_sink(256);

        sink->write("Dynamic ", 8);
        sink->write("buffering ", 10);
        sink->write("works!", 6);
        sink->null_terminate();

        std::cout << "  Content written successfully" << std::endl << std::endl;
    }

    // Example 3: Stream sink
    {
        std::cout << "3. Stream sink (output to std::cout):" << std::endl;

        auto sink = fl::make_stream_sink(std::cout);
        sink->write("Stream sink output\n", 19);
        std::cout << std::endl;
    }

    // Example 4: Null sink (benchmarking)
    {
        std::cout << "4. Null sink (for benchmarking):" << std::endl;

        auto sink = fl::make_null_sink();

        // Simulate writing lots of data without allocating
        for (int i = 0; i < 1000; ++i) {
            sink->write("Data", 4);
        }

        std::cout << "  Total bytes 'written': " << sink->bytes_written() << std::endl << std::endl;
    }

    // Example 5: Multi sink
    {
        std::cout << "5. Multi sink (write to multiple destinations):" << std::endl;

        // Create multiple sinks
        char buffer[256] = {0};
        fl::sinks::multi_sink multi;

        auto buf_sink = std::make_shared<fl::sinks::buffer_sink>(buffer, sizeof(buffer));
        auto cout_sink = fl::make_stream_sink(std::cout);

        multi.add_sink(buf_sink);
        multi.add_sink(cout_sink);

        multi.write("This goes to both buffer and stdout\n", 36);

        std::cout << "  Buffer contains: '" << buffer << "'" << std::endl << std::endl;
    }

    // Example 6: Sink character operations
    {
        std::cout << "6. Sink helper functions:" << std::endl;

        char buffer[256];
        fl::sinks::buffer_sink sink(buffer, sizeof(buffer));

        sink.write_char('H');
        sink.write_char('i');
        sink.null_terminate();

        std::cout << "  Result: '" << buffer << "'" << std::endl << std::endl;
    }

    // Example 7: Sink C-string writing
    {
        std::cout << "7. Sink write C-string:" << std::endl;

        char buffer[256];
        fl::sinks::buffer_sink sink(buffer, sizeof(buffer));

        sink.write_cstring("C-string");
        sink.write_cstring(" output");
        sink.null_terminate();

        std::cout << "  Result: '" << buffer << "'" << std::endl << std::endl;
    }

    // Example 8: Sink overflow handling
    {
        std::cout << "8. Sink overflow handling:" << std::endl;

        char buffer[10];
        fl::sinks::buffer_sink sink(buffer, sizeof(buffer));

        sink.write("12345", 5);
        std::cout << "  Written 5 bytes to 10-byte buffer" << std::endl;
        std::cout << "  Available space: " << sink.available() << " bytes" << std::endl;

        try {
            sink.write("123456", 6);  // This should fail
            std::cout << "  ERROR: overflow not detected!" << std::endl;
        } catch (const std::overflow_error& e) {
            std::cout << "  Overflow correctly detected: " << e.what() << std::endl << std::endl;
        }
    }

    // Example 9: Sink direct buffer access
    {
        std::cout << "9. Direct sink buffer access:" << std::endl;

        char buffer[256];
        fl::sinks::buffer_sink sink(buffer, sizeof(buffer));

        sink.write("Direct", 6);

        // Access written data
        const char* data = sink.buffer();
        std::cout << "  First character: '" << data[0] << "'" << std::endl;
        std::cout << "  Total written: " << sink.written() << " bytes" << std::endl << std::endl;
    }

    // Example 10: Practical example - building structured output
    {
        std::cout << "10. Practical example - structured output:" << std::endl;

        char buffer[512];
        fl::sinks::buffer_sink sink(buffer, sizeof(buffer));

        sink.write_cstring("System Report\n");
        sink.write_cstring("==============\n");
        sink.write_cstring("Items: 100\n");
        sink.write_cstring("Status: OK\n");
        sink.null_terminate();

        std::cout << buffer << std::endl;
    }

    std::cout << "=== Formatting examples completed ===" << std::endl << std::endl;
    return 0;
}
```

## Advanced Types (`substring_view`, `rope`, `immutable_string_view`, `immutable_string`)

This example demonstrates the use of the advanced string types: `substring_view`, `rope`, `immutable_string_view`, and `immutable_string`. Note that `owning_immutable_string` is a type alias for `immutable_string` and can be used interchangeably.

```cpp
#include <fl.hpp>
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

    // =========== Example 5: immutable_string (aliased as owning_immutable_string) ===========
    std::cout << "5. immutable_string: Persistent Map Keys\n"
              << "─────────────────────────────────────────\n\n";
    {
        std::cout << "Using immutable_string for persistent storage:\n"
                  << "(owning_immutable_string is a type alias for immutable_string)\n\n";

        // Simulate collecting keys that might not persist
        std::vector<std::string> temp_keys = {
            "user123",
            "session456",
            "cache789"
        };

        // Create owning copies for long-term storage
        std::unordered_map<fl::immutable_string, int,
                          fl::immutable_string_hash,
                          fl::immutable_string_equal> key_counts;

        for (const auto& key : temp_keys) {
            fl::immutable_string owned_key(key);
            key_counts[owned_key] = 0;
            // temp key goes out of scope; owned_key retains a copy
        }

        std::cout << "Keys stored (survived source destruction):\n";
        for (auto& [key, count] : key_counts) {
            std::cout << "  " << key << ": count=" << count << "\n";
        }
        std::cout << "\n";

        // Increment counters
        fl::immutable_string lookup_key("user123");
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

        std::cout << "immutable_string (owning_immutable_string):\n"
                  << "  Use: When you need to persist string keys\n"
                  << "  Best for: Long-lived map entries, copying from temporaries\n"
                  << "  Advantage: Safe, manages its own memory\n\n";
    }

    std::cout << "╔══════════════════════════════════════════════╗\n"
              << "║         Advanced Types Examples Completed    ║\n"
              << "╚══════════════════════════════════════════════╝\n\n";

    return 0;
}
```

## Thread Safety (`fl::immutable_string`, `fl::synchronised_string`)

This example demonstrates the use of the thread-safe string types, `fl::immutable_string` and `fl::synchronised_string`.

```cpp
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
    buffer += std::to_string(id);
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
```
