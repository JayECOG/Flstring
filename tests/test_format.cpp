#include <fl/format.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>

#define TEST(condition, name) \
    if (!(condition)) { \
        std::cerr << "FAIL: " << name << "\n"; \
        return 1; \
    } else { \
        std::cout << "PASS: " << name << "\n"; \
    }

static std::string render(auto&& writer) {
    char buffer[256];
    fl::buffer_sink sink(buffer, sizeof(buffer));
    writer(sink);
    sink.null_terminate();
    return std::string(buffer, sink.written());
}

int main() {
    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{{}}");
        });
        TEST(out == "{}", "escaped braces");
    }

    {
        std::string lhs = "alpha";
        std::string_view rhs = "beta";
        auto out = render([&](auto& sink) {
            fl::format_to(sink, std::string_view("right:{1} left:{0}"), lhs, rhs);
        });
        TEST(out == "right:beta left:alpha", "explicit positional indices");
    }

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{0:*>6} {0:<4}", 42);
        });
        TEST(out == "****42 42  ", "fill and alignment");
    }

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{}", std::string("hello"));
        });
        TEST(out == "hello", "std::string formatting");
    }

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{}", std::string_view("world"));
        });
        TEST(out == "world", "std::string_view formatting");
    }

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:>5} {:^6}", true, 'A');
        });
        TEST(out == " true    A  ", "bool and char text formatting");
    }

    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) {
                fl::format_to(sink, "{");
            });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "unmatched opening brace throws");
    }

    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) {
                fl::format_to(sink, "{1}", "only-one");
            });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "out-of-range index throws");
    }

    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) {
                fl::format_to(sink, "{0} {}", 1, 2);
            });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "mixed explicit and implicit indices throw");
    }

    // -- Dynamic width and precision from arguments ----------------------------

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:{}}", 42, 5);
        });
        TEST(out == "   42", "dynamic width from argument");
    }

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:>{}}", 7, 4);
        });
        TEST(out == "   7", "dynamic width with right align");
    }

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:{}.{}}", 3.1415926535, 8, 3);
        });
        // Right-aligned in 8 chars with 3 decimal places: 3.142 padded to 8.
        TEST(out == "   3.142", "dynamic width and precision");
    }

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:.{}}", 3.14159, 2);
        });
        TEST(out == "3.14", "dynamic precision from argument");
    }

    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:.{}}", "hello", 3);
        });
        TEST(out == "hel", "dynamic precision for string");
    }

    // Negative width → treated as zero (no padding).
    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:{}}", 42, -5);
        });
        TEST(out == "42", "negative dynamic width clamps to zero");
    }

    // Negative precision → precision_set = false (fmtlib compat).
    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:.{}}", 3.14159, -1);
        });
        // precision not set → default shortest representation.
        bool ok = (out == "3.14159" || out == "3.14159");
        TEST(ok, "negative dynamic precision unsets precision");
    }

    // Dynamic width with explicit index throws.
    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) {
                fl::format_to(sink, "{0:{}}", 42, 5);
            });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "dynamic width with explicit index throws");
    }

    std::cout << "\nAll tests passed!\n";
    return 0;
}
