#include <iostream>
#include "../include/fl.hpp"

/// String builder pattern examples
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

    // Example 5: Reserve for expected elements
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

    std::cout << "=== Builder examples completed ===" << std::endl;
    return 0;
}
