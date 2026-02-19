#include <iostream>
#include <sstream>
#include "../include/fl.hpp"

/// Formatting and sink examples
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
        
        // Write individual character
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

    std::cout << "=== Formatting examples completed ===" << std::endl;
    return 0;
}
