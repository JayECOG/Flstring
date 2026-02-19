#include <fl/string.hpp>
#include <iostream>

int main() {
    fl::string s = "Hello World";
    
    // Test starts_with
    std::cout << "starts_with 'Hello': " << s.starts_with("Hello") << "\n";
    std::cout << "starts_with 'H': " << s.starts_with('H') << "\n";
    
    // Test ends_with
    std::cout << "ends_with 'World': " << s.ends_with("World") << "\n";
    std::cout << "ends_with 'd': " << s.ends_with('d') << "\n";
    
    // Test contains
    std::cout << "contains 'lo Wo': " << s.contains("lo Wo") << "\n";
    std::cout << "contains 'x': " << s.contains('x') << "\n";
    
    // Test compare
    fl::string s2 = "Hello World";
    fl::string s3 = "Hello Wurld";
    std::cout << "compare equal: " << s.compare(s2) << "\n";
    std::cout << "compare different: " << (s.compare(s3) < 0 ? "less" : "greater") << "\n";
    
    // Test assign
    s.assign("New String");
    std::cout << "After assign: " << s.data() << "\n";
    
    s.assign(5, '*');
    std::cout << "After assign(5, '*'): " << s.data() << "\n";
    
    return 0;
}
