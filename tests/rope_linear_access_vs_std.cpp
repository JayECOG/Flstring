#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "fl/rope.hpp"

namespace {
using clock_type = std::chrono::high_resolution_clock;

std::uint64_t checksum_linear_view(std::string_view view) {
    std::uint64_t sum = 0;
    for (char c : view) {
        sum += static_cast<unsigned char>(c);
    }
    return sum;
}

std::uint64_t checksum_indexed(const fl::rope& r) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < r.size(); ++i) {
        sum += static_cast<unsigned char>(r[i]);
    }
    return sum;
}
}  // namespace

int main() {
    constexpr std::size_t kChunks = 20000;
    constexpr std::size_t kChunkLen = 8;
    const std::string chunk(kChunkLen, 'a');

    fl::rope rope_value;
    std::string std_value;
    std_value.reserve(kChunks * kChunkLen);

    auto t0 = clock_type::now();
    for (std::size_t i = 0; i < kChunks; ++i) {
        rope_value += chunk.c_str();
    }
    auto t1 = clock_type::now();
    for (std::size_t i = 0; i < kChunks; ++i) {
        std_value += chunk;
    }
    auto t2 = clock_type::now();

    const auto rope_view = rope_value.linear_view();
    const auto std_view = std::string_view(std_value);

    if (rope_view.size() != std_view.size()) {
        std::cerr << "Size mismatch: rope=" << rope_view.size()
                  << " std=" << std_view.size() << "\n";
        return 1;
    }
    if (rope_view != std_view) {
        std::cerr << "Content mismatch\n";
        return 2;
    }

    auto t3 = clock_type::now();
    const auto rope_sum_linear = checksum_linear_view(rope_view);
    auto t4 = clock_type::now();
    const auto rope_sum_indexed = checksum_indexed(rope_value);
    auto t5 = clock_type::now();
    const auto std_sum = checksum_linear_view(std_view);
    auto t6 = clock_type::now();

    const auto rope_append_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto std_append_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    const auto rope_linear_us = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();
    const auto rope_index_us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
    const auto std_linear_us = std::chrono::duration_cast<std::chrono::microseconds>(t6 - t5).count();

    std::cout << "Append (us): rope=" << rope_append_us << " std=" << std_append_us << "\n";
    std::cout << "Linear view checksum (us): rope=" << rope_linear_us
              << " std=" << std_linear_us << "\n";
    std::cout << "Indexed checksum (us): rope=" << rope_index_us << "\n";
    std::cout << "Checksums: rope_linear=" << rope_sum_linear
              << " rope_index=" << rope_sum_indexed
              << " std=" << std_sum << "\n";

    return 0;
}
