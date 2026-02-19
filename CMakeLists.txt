cmake_minimum_required(VERSION 3.15)

# Try to set language before project
project(fl-string
    VERSION 1.0.0
    DESCRIPTION "fl: High-performance lightweight C++ string library"
    LANGUAGES NONE
)

# Enable C++ explicitly
enable_language(CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Determine the target CPU families so x86-only SIMD flags are gated explicitly.
set(FL_TARGET_ARCHITECTURES "")
if(CMAKE_SYSTEM_PROCESSOR)
    list(APPEND FL_TARGET_ARCHITECTURES ${CMAKE_SYSTEM_PROCESSOR})
endif()
if(CMAKE_OSX_ARCHITECTURES)
    foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        list(APPEND FL_TARGET_ARCHITECTURES ${arch})
    endforeach()
endif()
if(NOT FL_TARGET_ARCHITECTURES)
    list(APPEND FL_TARGET_ARCHITECTURES ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()
list(REMOVE_DUPLICATES FL_TARGET_ARCHITECTURES)

string(REPLACE ";" ", " FL_TARGET_ARCHITECTURES_DISPLAY "${FL_TARGET_ARCHITECTURES}")
set(FL_TARGET_IS_ARM FALSE)
set(FL_TARGET_IS_X86 FALSE)
foreach(arch IN LISTS FL_TARGET_ARCHITECTURES)
    string(TOLOWER "${arch}" arch_lower)
    if(arch_lower MATCHES "arm|arm64|aarch64")
        set(FL_TARGET_IS_ARM TRUE)
    endif()
    if(arch_lower MATCHES "x86_64|amd64|i[3-6]86")
        set(FL_TARGET_IS_X86 TRUE)
    endif()
endforeach()

set(FL_X86_ONLY_SIMD_FLAGS "-msse4.1" "-msse4.2" "-mavx2")
set(FL_SIMD_FLAG_VARIABLES
    CMAKE_C_FLAGS
    CMAKE_C_FLAGS_DEBUG
    CMAKE_C_FLAGS_RELEASE
    CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_C_FLAGS_MINSIZEREL
    CMAKE_CXX_FLAGS
    CMAKE_CXX_FLAGS_DEBUG
    CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_MINSIZEREL
)

function(fl_strip_x86_simd_flags variable)
    if(NOT DEFINED ${variable})
        return()
    endif()
    set(old_value "${${variable}}")
    foreach(flag IN LISTS FL_X86_ONLY_SIMD_FLAGS)
        string(REPLACE "${flag}" "" old_value "${old_value}")
    endforeach()
    string(STRIP "${old_value}" new_value)
    set(${variable} "${new_value}" PARENT_SCOPE)
endfunction()

if(FL_TARGET_IS_ARM)
    foreach(flag_var IN LISTS FL_SIMD_FLAG_VARIABLES)
        fl_strip_x86_simd_flags(${flag_var})
    endforeach()
    message(STATUS "Detected ARM target (${FL_TARGET_ARCHITECTURES_DISPLAY}); stripped x86-only SIMD flags from generic C/C++ flags.")
endif()

# Language policy: all source-code comments and documentation in this project
# must use British English spelling.  Common examples:
#   behaviour     (not behavior)       colour       (not color)
#   licence       (not license, noun)  recognise    (not recognize)
#   optimise      (not optimize)       synchronise  (not synchronize)
#   analyse       (not analyze)        initialise   (not initialize)
#   serialise     (not serialize)      customise    (not customize)
# American English spellings in comments or documentation are corrected during
# code review.  The canonical reference is the Oxford English Dictionary.


# Library target (header-only)
add_library(fl INTERFACE)
target_include_directories(fl INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Add compile options for warnings
if(MSVC)
    target_compile_options(fl INTERFACE /W4)
else()
    target_compile_options(fl INTERFACE -Wall -Wextra -Wpedantic)
    # Enable AVX2 if the compiler supports it; two_way::search uses this for
    # the vectorised first-char scan in find() on large haystacks.
    if(FL_TARGET_IS_X86 AND NOT FL_TARGET_IS_ARM)
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag(-mavx2 COMPILER_SUPPORTS_AVX2)
        if(COMPILER_SUPPORTS_AVX2)
            target_compile_options(fl INTERFACE -mavx2)
        else()
            message(STATUS "-mavx2 is not supported on this toolchain; skipping AVX2 flags.")
        endif()
    else()
        message(STATUS "Target (${FL_TARGET_ARCHITECTURES_DISPLAY}) is not x86-only; skipping AVX2 flags.")
    endif()
endif()

# Dependencies
include(FetchContent)
option(FL_FETCH_DEPS "Fetch missing dependencies with FetchContent" ON)
option(FL_BENCHMARK_THIRD_PARTY "Build third-party benchmarks (absl::Cord, Boost, Folly)" OFF)
option(FL_BENCHMARK_OUTPUT "Generate benchmark output files in build directory" ON)

# Compatible-licence flags.  Every optional benchmark dependency carries a
# permissive licence that is compatible with the FL Licence.  These options
# let consumers opt out of specific licence families when their own project's
# requirements demand it.
#
#   Dependency      SPDX Identifier   Used by
#   ----------      ---------------   -------
#   abseil-cpp      Apache-2.0        cross_library_bench
#   Boost           BSL-1.0           cross_library_bench
#   Folly           Apache-2.0        folly_benchmark
#
# The core fl library itself has NO third-party dependencies and introduces no
# additional licence obligations beyond the FL Licence.
option(FL_ALLOW_APACHE2_DEPS
    "Permit Apache-2.0 licensed dependencies (abseil-cpp, Folly) to be fetched and linked"
    ON)
option(FL_ALLOW_BSL1_DEPS
    "Permit BSL-1.0 licensed dependencies (Boost) to be fetched and linked"
    ON)

# Attach licence metadata to the interface target for downstream introspection.
set_target_properties(fl PROPERTIES
    FL_SPDX_EXPRESSION "LicenseRef-FL-1.0"
)

if(FL_BENCHMARK_THIRD_PARTY OR CMAKE_BUILD_TYPE MATCHES CI)
    # abseil-cpp — Apache-2.0
    if(FL_ALLOW_APACHE2_DEPS)
        find_package(absl QUIET CONFIG)
        if(NOT absl_FOUND AND FL_FETCH_DEPS)
            FetchContent_Declare(
                absl
                GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
                GIT_TAG 20240116.2
            )
            set(ABSL_PROPAGATE_CXX_STD ON)
            FetchContent_MakeAvailable(absl)
            set(absl_FOUND TRUE)
        endif()
    else()
        set(absl_FOUND FALSE)
        message(STATUS "FL_ALLOW_APACHE2_DEPS is OFF; skipping abseil-cpp (Apache-2.0).")
    endif()

    # Boost — BSL-1.0
    if(FL_ALLOW_BSL1_DEPS)
        find_package(Boost QUIET COMPONENTS container)
        if(NOT Boost_FOUND AND FL_FETCH_DEPS)
            set(BOOST_ENABLE_CMAKE ON)
            set(BOOST_INCLUDE_LIBRARIES container)
            FetchContent_Declare(
                boost
                GIT_REPOSITORY https://github.com/boostorg/boost.git
                GIT_TAG boost-1.85.0
            )
            FetchContent_MakeAvailable(boost)
            set(Boost_FOUND TRUE)
        endif()
    else()
        set(Boost_FOUND FALSE)
        message(STATUS "FL_ALLOW_BSL1_DEPS is OFF; skipping Boost (BSL-1.0).")
    endif()
else()
    set(absl_FOUND FALSE)
    set(Boost_FOUND FALSE)
endif()

message(STATUS "Note: fl/cpu_features.hpp has been removed; allocators now rely on default alignment heuristics.")

# Enable testing
include(CTest)

# Find Folly for benchmarks if requested — Apache-2.0
if((FL_BENCHMARK_THIRD_PARTY OR CMAKE_BUILD_TYPE MATCHES CI) AND FL_ALLOW_APACHE2_DEPS)
    find_package(Folly CONFIG)
    if(Folly_FOUND)
        message(STATUS "Found Folly: ${Folly_VERSION}")
    else()
        message(WARNING "Folly not found. Folly benchmarks will not be built. Please install Folly or set Folly_DIR.")
    endif()
else()
    set(Folly_FOUND FALSE)
    if(FL_BENCHMARK_THIRD_PARTY AND NOT FL_ALLOW_APACHE2_DEPS)
        message(STATUS "FL_ALLOW_APACHE2_DEPS is OFF; skipping Folly (Apache-2.0).")
    else()
        message(STATUS "FL_BENCHMARK_THIRD_PARTY not enabled; Folly benchmarks disabled.")
    endif()
endif()

# Installation targets
install(TARGETS fl
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

# Benchmark output directory
if(FL_BENCHMARK_OUTPUT)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/benchmark_results)
    message(STATUS "Benchmark output will be saved to: ${CMAKE_BINARY_DIR}/benchmark_results")
endif()

# Benchmarks
add_executable(rope_vs_std_string_benchmarks benchmarks/rope_vs_std_string_benchmarks.cpp)
target_link_libraries(rope_vs_std_string_benchmarks PRIVATE fl)

add_executable(string_vs_std_bench benchmarks/string_vs_std_bench.cpp)
target_link_libraries(string_vs_std_bench PRIVATE fl)

if(absl_FOUND AND Boost_FOUND)
    add_executable(cross_library_bench benchmarks/cross_library_bench.cpp)
    target_link_libraries(cross_library_bench PRIVATE
        fl
        absl::cord
        absl::strings
        Boost::container
    )
elseif(FL_BENCHMARK_THIRD_PARTY)
    message(WARNING "absl or Boost not found; skipping cross_library_bench target.")
else()
    message(STATUS "FL_BENCHMARK_THIRD_PARTY not enabled; skipping cross_library_bench target.")
endif()

if(Folly_FOUND)
    add_executable(folly_benchmark benchmarks/folly_comparison/benchmark_folly.cpp)
    target_link_libraries(folly_benchmark PRIVATE fl Folly::folly)
elseif(FL_BENCHMARK_THIRD_PARTY)
    message(WARNING "Folly not found; skipping folly_benchmark target.")
else()
    message(STATUS "FL_BENCHMARK_THIRD_PARTY not enabled; skipping folly_benchmark target.")
endif()

# Comprehensive benchmark suite
add_executable(comprehensive_bench benchmarks/comprehensive_bench.cpp)
target_link_libraries(comprehensive_bench PRIVATE fl)
# GCC/Clang false-positive -Warray-bounds from _FORTIFY_SOURCE analysis when
# fl::detail::copy_heap_hot is inlined through deep call chains.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(comprehensive_bench PRIVATE -Wno-array-bounds)
endif()

# Find-substring throughput benchmark (256–4096 byte haystacks)
add_executable(find_haystack_bench benchmarks/find_haystack_bench.cpp)
target_link_libraries(find_haystack_bench PRIVATE fl)

# Rope concat: rebalance() cost isolation benchmark
add_executable(rope_rebalance_bench benchmarks/rope_rebalance_bench.cpp)
target_link_libraries(rope_rebalance_bench PRIVATE fl)

# Allocator comparison: fl pool vs std::pmr::monotonic_buffer_resource
add_executable(pmr_vs_pool_bench benchmarks/pmr_vs_pool_bench.cpp)
target_link_libraries(pmr_vs_pool_bench PRIVATE fl)
# GCC/Clang false-positive -Warray-bounds from _FORTIFY_SOURCE analysis when
# fl::detail::copy_heap_hot is inlined through deep call chains.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pmr_vs_pool_bench PRIVATE -Wno-array-bounds)
endif()

# ASLR / allocator warm-up construction investigation (item 4)
add_executable(aslr_construction_bench benchmarks/aslr_construction_bench.cpp)
target_link_libraries(aslr_construction_bench PRIVATE fl)

# Tests
add_executable(rope_linear_access_vs_std tests/rope_linear_access_vs_std.cpp)
target_link_libraries(rope_linear_access_vs_std PRIVATE fl)
add_test(NAME rope_linear_access_vs_std COMMAND rope_linear_access_vs_std)

add_executable(fl_string_vs_std_full_test tests/fl_string_vs_std_full_test.cpp)
target_link_libraries(fl_string_vs_std_full_test PRIVATE fl)
add_test(NAME fl_string_vs_std_full_test COMMAND fl_string_vs_std_full_test)

add_executable(test_adaptive_find tests/test_adaptive_find.cpp)
target_link_libraries(test_adaptive_find PRIVATE fl)
add_test(NAME test_adaptive_find COMMAND test_adaptive_find)

add_executable(test_rope_access_index tests/test_rope_access_index.cpp)
target_link_libraries(test_rope_access_index PRIVATE fl)
# GCC/Clang false-positive -Warray-bounds from _FORTIFY_SOURCE analysis when
# fl::detail::copy_heap_hot / copy_small are inlined through deep call chains.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_rope_access_index PRIVATE -Wno-array-bounds)
endif()
add_test(NAME test_rope_access_index COMMAND test_rope_access_index)

# Package configuration files
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/fl-config-version.cmake
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/fl-config-version.cmake
    DESTINATION lib/cmake/fl
)
