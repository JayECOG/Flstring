---
name: Performance Issue
about: Report performance degradation or optimization opportunity
title: "[PERF] "
labels: ["performance"]
assignees: []
---

## Description
A clear description of the performance issue you've observed.

## Environment
- **OS**: (e.g., Ubuntu 22.04, macOS 13.5, Windows 11)
- **CPU**: (e.g., Intel i9-13900K, Apple M2)
- **Compiler**: (e.g., GCC 12 with -O3, Clang 15 with -O3)
- **C++ Standard**: C++20
- **fl::string version**: (commit hash or tag)

## Benchmark Setup
Describe how you measured the performance issue:
- What operation(s) are slow?
- What is the input size/characteristics?
- How many iterations did you run?

## Minimal Reproducible Example
```cpp
#include <fl.hpp>
#include <chrono>

int main() {
    // Code that demonstrates the performance issue
    auto start = std::chrono::high_resolution_clock::now();

    // Operation under test

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Time: " << duration.count() << " µs\n";
}
```

## Observed Performance
- **Expected**: [expected time/throughput]
- **Actual**: [actual time/throughput]
- **Regression**: [X% slower/faster than X]

## Profiling Data
If available, include:
```
[Output from perf, flamegraph, or other profiling tool]
```

## Comparison with std::string
How does this compare with `std::string` performance?

```
Operation           | std::string | fl::string | Ratio
--------------------|-------------|------------|-------
                    |             |            |
```

## Additional Context
- Is this a regression from a recent change?
- Does it affect specific use cases (SSO, heap, etc.)?
- Have you tried with different compiler flags?

## Checklist
- [ ] I have reproduced the issue with a minimal example
- [ ] I have compared with std::string behavior
- [ ] I have tested with multiple compiler versions
- [ ] The performance issue is consistent across runs
