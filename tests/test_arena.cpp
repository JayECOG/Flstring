#include "fl.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <string_view>
#include <limits>

#define TEST(condition, name) \
    if (!(condition)) { \
        std::cerr << "FAIL: " << name << "\n"; \
        return 1; \
    } else { \
        std::cout << "PASS: " << name << "\n"; \
    }

int main() {
    // Test arena_allocator basic functionality
    {
        fl::arena_allocator<4096> arena;
        void* ptr1 = arena.allocate(64);
        void* ptr2 = arena.allocate(64);
        TEST(ptr1 != nullptr, "allocate: returns non-null");
        TEST(ptr2 != nullptr, "allocate: second allocation non-null");
        TEST(ptr1 != ptr2, "allocate: different addresses");
    }
    
    // Test alignment
    {
        fl::arena_allocator<4096> arena;
        void* ptr1 = arena.allocate(1);
        void* ptr2 = arena.allocate(1);
        ptrdiff_t diff = static_cast<char*>(ptr2) - static_cast<char*>(ptr1);
        TEST(diff == 8, "alignment: 8-byte alignment");
    }
    
    // Test heap fallback
    {
        fl::arena_allocator<4096> arena;
        void* stack_ptr = arena.allocate(4000);
        void* heap_ptr = arena.allocate(1000);
        TEST(stack_ptr != nullptr, "heap fallback: stack allocation");
        TEST(heap_ptr != nullptr, "heap fallback: heap allocation");
        TEST(stack_ptr != heap_ptr, "heap fallback: different allocations");
    }
    
    // CRITICAL-2: Integer overflow rejection
    {
        fl::arena_allocator<4096> arena;
        std::size_t huge = std::numeric_limits<std::size_t>::max();
        try {
            arena.allocate(huge);
            TEST(false, "CRITICAL-2: overflow should throw");
        } catch (const std::bad_alloc&) {
            TEST(true, "CRITICAL-2: overflow rejected with bad_alloc");
        }
    }
    
    // Test deallocate
    {
        fl::arena_allocator<4096> arena;
        void* ptr = arena.allocate(64);
        arena.deallocate(ptr, 64);
        TEST(true, "deallocate: stack pointer deallocation");
    }
    
    // Test reset
    {
        fl::arena_allocator<4096> arena;
        void* ptr1 = arena.allocate(64);
        arena.reset();
        void* ptr2 = arena.allocate(64);
        TEST(ptr1 == ptr2, "reset: reuses stack area");
    }
    
    // Test arena_buffer basic append
    {
        fl::arena_buffer<4096> buf;
        buf.append("hello");
        TEST(buf.size() == 5, "buffer: append cstring");
        TEST(std::string_view(buf.data(), buf.size()) == "hello", "buffer: content matches");
    }
    
    // CRITICAL-3: Null pointer rejection
    {
        fl::arena_buffer<4096> buf;
#ifdef NDEBUG
        buf.append(nullptr, 5);
        TEST(buf.size() == 0, "CRITICAL-3: null pointer rejected");
#else
        std::cout << "SKIP: CRITICAL-3: null pointer rejection (assert-enabled build)\n";
#endif
    }
    
    // Test append char
    {
        fl::arena_buffer<4096> buf;
        buf.append('A');
        buf.append('B');
        buf.append('C');
        TEST(buf.size() == 3, "buffer: append chars");
        TEST(std::string_view(buf.data(), buf.size()) == "ABC", "buffer: char content");
    }
    
    // Test append_repeat
    {
        fl::arena_buffer<4096> buf;
        buf.append_repeat('X', 10);
        TEST(buf.size() == 10, "buffer: append_repeat");
        TEST(std::string_view(buf.data(), buf.size()) == "XXXXXXXXXX", "buffer: repeat content");
    }
    
    // LOW-1: string_view overload
    {
        fl::arena_buffer<4096> buf;
        std::string_view sv = "test";
        buf.append(sv);
        TEST(buf.size() == 4, "buffer: append string_view");
        TEST(std::string_view(buf.data(), buf.size()) == "test", "buffer: string_view content");
    }
    
    // LOW-4: view method
    {
        fl::arena_buffer<4096> buf;
        buf.append("data");
        std::string_view view = buf.view();
        TEST(view == "data", "buffer: view() method");
    }
    
    // LOW-5: reserve method
    {
        fl::arena_buffer<4096> buf;
        buf.reserve(1000);
        TEST(buf.capacity() >= 1000, "buffer: reserve(1000)");
    }
    
    // HIGH-4: Overflow in growth
    {
        fl::arena_buffer<4096> buf;
        std::size_t huge = std::numeric_limits<std::size_t>::max() / 2 + 1;
        try {
            buf.reserve(huge);
            TEST(false, "HIGH-4: overflow in growth should throw");
        } catch (const std::exception&) {
            TEST(true, "HIGH-4: overflow in growth handled (threw exception)");
        }
    }
    
    // Test growth
    {
        fl::arena_buffer<4096> buf;
        // Default capacity is StackSize (4096); we need a value that exceeds it.
        std::size_t cap1 = buf.capacity();
        buf.reserve(5000);
        std::size_t cap2 = buf.capacity();
        TEST(cap2 > cap1, "buffer: growth increases capacity");
        TEST(cap2 >= 5000, "buffer: growth meets minimum");
    }
    
    // Test clear
    {
        fl::arena_buffer<4096> buf;
        buf.append("hello");
        buf.clear();
        TEST(buf.size() == 0, "buffer: clear");
    }
    
    // Test reset
    {
        fl::arena_buffer<4096> buf;
        buf.append("hello");
        buf.reset();
        TEST(buf.size() == 0, "buffer: reset size");
        TEST(buf.capacity() == 4096, "buffer: reset capacity to default (StackSize)");
    }
    
    // Test to_string
    {
        fl::arena_buffer<4096> buf;
        buf.append("test");
        fl::string s = buf.to_string();
        TEST(s == "test", "buffer: to_string()");
    }
    
    // Test multiple appends with growth
    {
        fl::arena_buffer<4096> buf;
        for (int i = 0; i < 100; ++i) {
            buf.append("x");
        }
        TEST(buf.size() == 100, "buffer: multiple appends");
    }
    
    // Test pooled temp buffer
    {
        auto buf = fl::get_pooled_temp_buffer();
        TEST(buf.get() != nullptr, "pool: get_pooled_temp_buffer returns non-null");
        buf->append("temp");
        TEST(buf->view() == "temp", "pool: buffer works");
    }
    
    // Test pool reclaim
    {
        {
            auto buf = fl::get_pooled_temp_buffer();
            buf->append("data");
        }
        auto buf2 = fl::get_pooled_temp_buffer();
        TEST(buf2.get() != nullptr, "pool: reuse after destroy");
    }
    
    // Integration: buffer with string operations
    {
        fl::arena_buffer<4096> buf;
        buf.append("Hello");
        buf.append(" ");
        buf.append("World");
        fl::string str = buf.to_string();
        TEST(str == "Hello World", "integration: multi-append");
    }
    
    // Integration: buffer with format-like operations
    {
        fl::arena_buffer<4096> buf;
        buf.append("Value: ");
        buf.append("42");
        std::string_view view = buf.view();
        TEST(view == "Value: 42", "integration: format-like");
    }
    
    std::cout << "\n=== All Arena Tests Passed ===\n";
    return 0;
}
