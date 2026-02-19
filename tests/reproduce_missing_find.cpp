#include <fl/string.hpp>
#include <iostream>

int main() {
    fl::string s = "Hello World";
    fl::string sub = "World";
    
    // This should compile if compatible with std::string
    auto pos = s.find(sub); 
    
    if (pos != fl::string::npos) {
        std::cout << "Found at " << pos << "\n";
    } else {
        std::cout << "Not found\n";
    }
    
    return 0;
}
