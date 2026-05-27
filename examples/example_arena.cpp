#include <iostream>
#include "../include/fl.hpp"

/// Arena allocator and temporary buffer examples
int main() {
    std::cout << "=== Arena and Temporary Buffer Examples ===" << std::endl << std::endl;

    // Example 1: Basic arena allocator
    {
        std::cout << "1. Basic arena allocator:" << std::endl;
        
        fl::arena_allocator<1024> arena;
        
        std::cout << "  Initial stack used: " << arena.used() << " bytes" << std::endl;
        
        void* ptr1 = arena.allocate(32);
        std::cout << "  After allocating 32 bytes: " << arena.used() << " bytes used" << std::endl;
        
        void* ptr2 = arena.allocate(64);
        std::cout << "  After allocating 64 bytes: " << arena.used() << " bytes used" << std::endl << std::endl;
    }

    // Example 2: Arena reset
    {
        std::cout << "2. Arena reset:" << std::endl;
        
        fl::arena_allocator<256> arena;
        
        std::cout << "  Before allocation: " << arena.used() << " bytes" << std::endl;
        
        arena.allocate(100);
        std::cout << "  After allocation: " << arena.used() << " bytes" << std::endl;
        
        arena.reset();
        std::cout << "  After reset: " << arena.used() << " bytes" << std::endl << std::endl;
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

    // Example 4: Temporary buffer basic usage
    {
        std::cout << "4. Temporary buffer:" << std::endl;
        
        fl::temp_buffer temp = fl::get_pooled_temp_buffer();  // 4KB stack by default
        
        temp->append("Part1").append(" ").append("Part2");
        
        // arena_buffer exposes size(), capacity() and data() directly.
        std::cout << "  Buffer size: " << temp->size() << std::endl;
        std::cout << "  Buffer capacity: " << temp->capacity() << std::endl;
        std::cout << "  Content: '" << temp->data() << "'" << std::endl << std::endl;
    }

    // Example 5: Temporary buffer to string
    {
        std::cout << "5. Convert temporary buffer to string:" << std::endl;
        
        fl::temp_buffer temp = fl::get_pooled_temp_buffer();
        temp->append("Hello").append(" ").append("World");
        
        fl::string result = temp->to_string();
        std::cout << "  Result: '" << result.c_str() << "'" << std::endl;
        std::cout << "  String size: " << result.size() << std::endl << std::endl;
    }

    // Example 6: Temporary buffer with custom stack size
    {
        std::cout << "6. Temporary buffer with custom stack size:" << std::endl;
        
        fl::arena_buffer<2048> temp_custom;  // 2KB stack (direct arena_buffer, not pooled temp_buffer)
        
        for (int i = 0; i < 10; ++i) {
            temp_custom.append("Item ").append_repeat('*', 1);
            if (i < 9) temp_custom.append(", ");
        }
        
        // arena_buffer exposes size(), capacity() and data() directly.
        std::cout << "  Buffer size: " << temp_custom.size() << std::endl;
        std::cout << "  Content: '" << temp_custom.data() << "'" << std::endl << std::endl;
    }

    // Example 7: Arena buffer with reserve
    {
        std::cout << "7. Arena buffer with reserve:" << std::endl;
        
        fl::temp_buffer temp = fl::get_pooled_temp_buffer();
        // arena_buffer exposes reserve() directly.
        temp->reserve(512);  // Pre-allocate
        
        std::cout << "  Capacity after reserve(512): " << temp->capacity() << std::endl;
        
        temp->append("Efficient building");
        std::cout << "  After append: size = " << temp->size() << std::endl << std::endl;
    }

    // Example 8: Arena buffer clear and reuse
    {
        std::cout << "8. Arena buffer clear and reuse:" << std::endl;
        
        fl::temp_buffer temp = fl::get_pooled_temp_buffer();
        
        // First use
        temp->append("First");
        // arena_buffer exposes size().
        std::cout << "  First use size: " << temp->size() << std::endl;
        
        temp->clear();
        std::cout << "  After clear: " << temp->size() << std::endl;
        
        // Reuse
        temp->append("Second");
        std::cout << "  Second use size: " << temp->size() << std::endl << std::endl;
    }

    // Example 9: Large data in arena
    {
        std::cout << "9. Building large strings with arena:" << std::endl;
        
        fl::temp_buffer temp = fl::get_pooled_temp_buffer();
        // arena_buffer exposes reserve().
        temp->reserve(2000);
        
        // Simulate building a large structure
        for (int i = 0; i < 100; ++i) {
            temp->append("Line ").append_repeat('x', 1).append("\n");
        }
        
        // arena_buffer exposes size().
        std::cout << "  Final size: " << temp->size() << " bytes" << std::endl;
        std::cout << "  No heap allocation for most of it!" << std::endl << std::endl;
    }

    // Example 10: Practical pattern - request builder
    {
        std::cout << "10. Practical pattern - building request:" << std::endl;
        
        fl::temp_buffer request = fl::get_pooled_temp_buffer();
        // arena_buffer exposes reserve().
        request->reserve(512);
        
        request->append("GET /api/users HTTP/1.1\r\n");
        request->append("Host: example.com\r\n");
        request->append("Content-Length: 0\r\n");
        request->append("\r\n");
        
        fl::string final_request = request->to_string();
        
        std::cout << "  Request size: " << final_request.size() << " bytes" << std::endl;
        std::cout << "  Efficiently built with temporary buffer" << std::endl << std::endl;
    }

    std::cout << "=== Arena examples completed ===" << std::endl;
    return 0;
}
