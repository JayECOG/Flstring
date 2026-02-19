# Contributing to fl (Forever Lightweight)

Thank you for your interest in contributing to the **fl** C++ string library. This document outlines the guidelines, conventions, and processes that keep the project healthy, consistent, and welcoming to all contributors.

---

## Table of Contents

1.  [Code of Conduct](#1-code-of-conduct)
2.  [Getting Started](#2-getting-started)
3.  [Development Environment Setup](#3-development-environment-setup)
4.  [Building the Project](#4-building-the-project)
5.  [Running Tests](#5-running-tests)
6.  [Running Benchmarks](#6-running-benchmarks)
7.  [Code Style Guidelines](#7-code-style-guidelines)
8.  [Commit Message Conventions](#8-commit-message-conventions)
9.  [Pull Request Process](#9-pull-request-process)
10. [Reporting Bugs](#10-reporting-bugs)
11. [Requesting Features](#11-requesting-features)
12. [Performance Contributions](#12-performance-contributions)
13. [Documentation](#13-documentation)
14. [License](#14-license)
15. [Developer Certificate of Origin (DCO)](#15-developer-certificate-of-origin-dco)
16. [Contributor Licence Agreement (CLA)](#16-contributor-licence-agreement-cla)

---

## 1. Code of Conduct

All participation in this project is governed by the
[Code of Conduct](CODE_OF_CONDUCT.md). Please read it before contributing.

In brief: be respectful, constructive, and professional. Harassment, personal
attacks, and deliberately inflammatory behaviour will not be tolerated.
Violations may be reported to the project maintainer as described in
`CODE_OF_CONDUCT.md`.

---

## 2. Getting Started

### Fork and Clone

1. **Fork** the repository on GitHub.
2. **Clone** your fork locally:

   ```bash
   git clone https://github.com/<your-username>/Flstring.git
   cd Flstring
   ```

3. **Add the upstream remote** so you can stay in sync:

   ```bash
   git remote add upstream https://github.com/JayECOG/Flstring.git
   ```

### Branch Workflow

Always create a feature branch from the latest `main`:

```bash
git fetch upstream
git checkout -b <branch-name> upstream/main
```

**Branch naming conventions:**

| Prefix        | Purpose                        | Example                          |
| :------------ | :----------------------------- | :------------------------------- |
| `feat/`       | New feature                    | `feat/simd-memcmp`              |
| `fix/`        | Bug fix                        | `fix/sso-off-by-one`            |
| `perf/`       | Performance improvement        | `perf/avx2-find-throughput`     |
| `refactor/`   | Code refactoring               | `refactor/rope-node-layout`     |
| `docs/`       | Documentation only             | `docs/update-api-reference`     |
| `test/`       | Test additions or corrections  | `test/builder-growth-policies`  |
| `bench/`      | Benchmark additions            | `bench/pmr-allocator-compare`   |
| `ci/`         | CI/CD pipeline changes         | `ci/add-sanitizer-job`          |

---

## 3. Development Environment Setup

### Prerequisites

| Requirement      | Minimum Version | Notes                                               |
| :--------------- | :-------------- | :-------------------------------------------------- |
| C++ compiler     | See below       | Must support C++20                                  |
| CMake            | 3.15            | Build system                                        |
| Git              | 2.0+            | Version control                                     |
| ccache (optional)| Any             | Recommended for faster rebuilds                     |

### Supported Compilers

| Compiler   | Minimum Version | Platform         |
| :--------- | :-------------- | :--------------- |
| GCC        | 10+             | Linux            |
| Clang      | 10+             | Linux, macOS     |
| MSVC       | 2019+ (v16.8+)  | Windows          |

The library requires full C++20 support. Earlier compiler versions that lack complete C++20 implementations (e.g., `<concepts>`, `<span>`, three-way comparison) will not work.

### Core Dependencies

The core library is **header-only** and has **no external dependencies** beyond the C++ standard library. Simply include the headers and compile.

### Optional Third-Party Dependencies (Benchmarks Only)

The following libraries are only required when building the cross-library benchmarks with `-DFL_BENCHMARK_THIRD_PARTY=ON`:

- **Abseil (absl)** -- for `absl::Cord` comparisons
- **Boost.Container** -- for Boost string comparisons
- **Folly** -- for Facebook Folly `fbstring` comparisons (must be installed separately; not fetched automatically)

When `FL_FETCH_DEPS=ON` (the default), CMake will use `FetchContent` to download Abseil and Boost automatically if they are not found on the system. Folly must be installed manually.

---

## 4. Building the Project

### Standard Build (Release)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Debug Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

### Build with Third-Party Benchmarks

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DFL_BENCHMARK_THIRD_PARTY=ON \
  -DFL_FETCH_DEPS=ON
cmake --build build --parallel
```

### CMake Options

| Option                        | Default | Description                                                         |
| :---------------------------- | :------ | :------------------------------------------------------------------ |
| `FL_FETCH_DEPS`               | `ON`    | Fetch missing dependencies (absl, Boost) via FetchContent           |
| `FL_BENCHMARK_THIRD_PARTY`    | `OFF`   | Build third-party benchmarks (absl::Cord, Boost, Folly)             |
| `FL_BENCHMARK_OUTPUT`         | `ON`    | Create `build/benchmark_results/` for benchmark output files        |
| `FL_ALLOW_APACHE2_DEPS`       | `ON`    | Permit Apache-2.0 licensed dependencies (abseil-cpp, Folly)         |
| `FL_ALLOW_BSL1_DEPS`          | `ON`    | Permit BSL-1.0 licensed dependencies (Boost)                        |

### Windows with MSYS2/MinGW

A CMake preset is provided for MSYS2 MinGW64 builds:

```bash
cmake --preset msys2-mingw64
cmake --build build_msys --parallel
```

### Using the Library in Your Project

Since `fl` is header-only, you can integrate it with CMake's `FetchContent` or by adding the `include/` directory to your include path:

```cmake
# Option 1: add_subdirectory
add_subdirectory(path/to/Flstring)
target_link_libraries(your_target PRIVATE fl)

# Option 2: include path only
target_include_directories(your_target PRIVATE path/to/Flstring/include)
```

The umbrella header `#include <fl.hpp>` pulls in every public component. You may also include individual headers (e.g., `#include <fl/string.hpp>`) for faster compile times.

---

## 5. Running Tests

The project registers four CTest test executables:

| Test Executable               | What It Covers                                         |
| :---------------------------- | :----------------------------------------------------- |
| `rope_linear_access_vs_std`   | Rope linear access correctness vs `std::string`        |
| `fl_string_vs_std_full_test`  | Comprehensive `fl::string` operations vs `std::string` |
| `test_adaptive_find`          | Adaptive find algorithm correctness                    |
| `test_rope_access_index`      | Rope indexed access correctness                        |

### Run All Tests

```bash
ctest --test-dir build --output-on-failure --verbose
```

### Run a Specific Test

```bash
ctest --test-dir build -R test_adaptive_find --output-on-failure
```

### Run a Test Executable Directly

```bash
./build/fl_string_vs_std_full_test
```

### Test Requirements for Contributions

- All existing tests **must pass** before submitting a pull request.
- Bug fixes **must** include a test that reproduces the bug and verifies the fix.
- New features **must** include corresponding tests covering both normal behaviour and edge cases.
- Tests should be added to the `tests/` directory and registered in `CMakeLists.txt` with `add_test()`.

---

## 6. Running Benchmarks

The project includes nine benchmark executables. Seven are always built; two are conditional on third-party libraries.

### Core Benchmarks (Always Built)

| Executable                    | Description                                             |
| :---------------------------- | :------------------------------------------------------ |
| `string_vs_std_bench`         | `fl::string` vs `std::string` core operations           |
| `rope_vs_std_string_benchmarks` | Rope concatenation vs `std::string`                  |
| `comprehensive_bench`         | Comprehensive benchmark suite across all components     |
| `find_haystack_bench`         | Substring find throughput (256--4096 byte haystacks)    |
| `rope_rebalance_bench`        | Rope `rebalance()` cost isolation                       |
| `pmr_vs_pool_bench`           | fl pool allocator vs `std::pmr::monotonic_buffer_resource` |
| `aslr_construction_bench`     | ASLR / allocator warm-up construction investigation     |

### Conditional Benchmarks

| Executable            | Condition                                      | Description                        |
| :-------------------- | :--------------------------------------------- | :--------------------------------- |
| `cross_library_bench` | `FL_BENCHMARK_THIRD_PARTY=ON` + absl + Boost   | Cross-library string comparison    |
| `folly_benchmark`     | `FL_BENCHMARK_THIRD_PARTY=ON` + Folly installed | Folly `fbstring` comparison        |

### Running Benchmarks

```bash
# Run a specific benchmark
./build/string_vs_std_bench

# Run all core benchmarks sequentially
for bench in string_vs_std_bench rope_vs_std_string_benchmarks comprehensive_bench \
             find_haystack_bench rope_rebalance_bench pmr_vs_pool_bench \
             aslr_construction_bench; do
  echo "=== $bench ==="
  ./build/$bench
done
```

### Saving Benchmark Results

When `FL_BENCHMARK_OUTPUT=ON` (the default), CMake creates a `build/benchmark_results/` directory. You can redirect output there:

```bash
./build/string_vs_std_bench > build/benchmark_results/string_vs_std_bench.txt 2>&1
```

### Tips for Reliable Benchmarks

- **Build type:** Always benchmark with `-DCMAKE_BUILD_TYPE=Release`.
- **CPU frequency scaling:** Disable turbo boost and frequency scaling for consistent results.
  ```bash
  # Linux: set performance governor
  sudo cpupower frequency-set -g performance
  ```
- **CPU pinning:** Pin the benchmark process to a specific core to avoid migration.
  ```bash
  taskset -c 0 ./build/string_vs_std_bench
  ```
- **System load:** Close other applications and avoid running benchmarks under heavy system load.
- **Multiple runs:** Run benchmarks several times and look for consistency. Discard outlier runs.

---

## 7. Code Style Guidelines

### Language (British English)

All source-code comments, documentation, commit messages, and pull request descriptions in this project **must use British English spelling**.

This is a hard requirement enforced during code review. American English spellings will be corrected before a contribution is merged.

| British (Required)  | American (Rejected)  |
| :------------------ | :------------------- |
| `behaviour`         | `behavior`           |
| `colour`            | `color`              |
| `licence` (noun)    | `license` (noun)     |
| `recognise`         | `recognize`          |
| `optimise`          | `optimize`           |
| `synchronise`       | `synchronize`        |
| `analyse`           | `analyze`            |
| `initialise`        | `initialize`         |
| `serialise`         | `serialize`          |
| `customise`         | `customize`          |
| `catalogue`         | `catalog`            |
| `centre`            | `center`             |
| `neighbouring`      | `neighboring`        |
| `practise` (verb)   | `practice` (verb)    |

> Note: the American English spelling `synchronized` is preserved **only** where it appears as a C++ type alias (`fl::synchronized_string`) for ABI or API compatibility reasons. In all other contexts — including comments describing that type — the British spelling `synchronised` is required.

The canonical reference is the **Oxford English Dictionary**.

### Comment Style

This project follows the **Google C++ Style Guide** for comments:

- Use `//` line comments. Do **not** use `///`, `/** */`, or Doxygen tags.
- Place a comment block above functions and classes describing their purpose.
- Use `// ` (with a space after the slashes) for inline and trailing comments.

```cpp
// Returns the number of code units in the string, excluding the null
// terminator.  Runs in O(1) time for both SSO and heap-backed strings.
size_type size() const noexcept { return size_; }
```

### License Boilerplate

Every header file **must** begin with the standard license boilerplate:

```cpp
// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.
```

### Include Guards

Use `#ifndef` / `#define` / `#endif` include guards. The guard name follows the pattern `FL_<FILENAME>_HPP`:

```cpp
#ifndef FL_STRING_HPP
#define FL_STRING_HPP
// ...
#endif  // FL_STRING_HPP
```

### C++ Standard

- Target **C++20**. Use C++20 features freely: `<concepts>`, `<span>`, three-way comparison (`<=>`), `constexpr` improvements, etc.
- Do **not** use compiler-specific extensions unless they are guarded by preprocessor detection (e.g., SIMD intrinsics gated on `__SSE2__` / `__AVX2__` / `_M_X64`).

### Naming Conventions

| Entity              | Convention       | Example                          |
| :------------------ | :--------------- | :------------------------------- |
| Namespaces          | `snake_case`     | `fl`, `fl::detail`               |
| Classes / structs   | `snake_case`     | `string`, `rope`, `arena_buffer` |
| Member functions    | `snake_case`     | `find()`, `substr()`, `size()`   |
| Member variables    | `snake_case_`    | `size_`, `data_`, `capacity_`    |
| Free functions      | `snake_case`     | `to_string()`, `format()`        |
| Constants / macros  | `UPPER_SNAKE`    | `SSO_CAPACITY`, `FL_CONFIG_HPP`  |
| Template parameters | `PascalCase`     | `Allocator`, `GrowthPolicy`      |
| Type aliases        | `snake_case`     | `size_type`, `value_type`        |

### Header-Only Constraints

The core library is header-only. All implementations must reside in header files under `include/`. If you add a new component:

1. Create the header in `include/fl/`.
2. Include it in `include/fl.hpp` (the umbrella header).
3. Use `inline` for non-template free functions to avoid ODR violations.
4. Prefer `constexpr` and `noexcept` where applicable.

### General Code Guidelines

- **Warning-clean:** Code must compile without warnings under `-Wall -Wextra -Wpedantic` (GCC/Clang) and `/W4` (MSVC).
- **Exception safety:** Provide at least the basic guarantee. Document any operations that offer the strong or nothrow guarantee.
- **No external dependencies** for the core library. The only allowed includes are C++ standard library headers and other `fl` headers.
- **Thread safety:** `fl::string` is not thread-safe for concurrent mutation. Thread-safe types (`fl::synchronised_string`, `fl::immutable_string`) are explicitly documented as such.

---

## 8. Commit Message Conventions

Use the following structured format for commit messages:

```
<type>: <subject>

<body (optional)>

Co-Authored-By: <name> <email>
```

### Type Prefixes

| Type         | When to Use                                       |
| :----------- | :------------------------------------------------ |
| `feat`       | A new feature or public API addition               |
| `fix`        | A bug fix                                          |
| `perf`       | A performance improvement                          |
| `refactor`   | Code restructuring with no behaviour change        |
| `test`       | Adding or updating tests                           |
| `bench`      | Adding or updating benchmarks                      |
| `docs`       | Documentation changes only                         |
| `ci`         | CI/CD pipeline changes                             |
| `chore`      | Build system, tooling, or maintenance changes      |
| `style`      | Code formatting, whitespace, comment cleanup       |

### Rules

- **Subject line:** Imperative mood, lowercase, no trailing period, max 72 characters.
- **Body:** Explain *why* the change was made, not just *what* was changed. Wrap at 72 characters.
- **Co-Authored-By:** If the commit was co-authored (e.g., with an AI tool), include the trailer.

### Examples

```
feat: add SIMD-accelerated two-way substring search

Implements a vectorised first-character scan using AVX2 intrinsics when
available, falling back to SSE2 or scalar code.  This targets the hot
path in fl::string::find() for haystacks larger than 64 bytes.

Co-Authored-By: Claude <noreply@anthropic.com>
```

```
fix: correct off-by-one in SSO capacity check

Strings exactly 23 bytes long were incorrectly triggering heap
allocation instead of using the SSO buffer.
```

---

## 9. Pull Request Process

### Before Opening a PR

1. **Sync with upstream:** Rebase your branch onto the latest `main` to minimise merge conflicts.
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```
2. **Run all tests:** Ensure `ctest --test-dir build --output-on-failure` passes cleanly.
3. **Run relevant benchmarks:** If your change affects performance, run the relevant benchmarks and note the results.
4. **Check for warnings:** Build in Release mode and verify zero compiler warnings.
5. **Sign off every commit:** Ensure every commit in your branch carries a `Signed-off-by` trailer
   certifying the Developer Certificate of Origin (see [Section 15](#15-developer-certificate-of-origin-dco)):
   ```
   Signed-off-by: Your Full Name <your.email@example.com>
   ```
   Commits without a valid Sign-off will not be merged.
6. **CLA status:** If this is your first Contribution, or if you are contributing on behalf of a
   legal entity, ensure you have executed the Contributor Licence Agreement before opening the PR
   (see [Section 16](#16-contributor-licence-agreement-cla)).

### Opening the PR

1. Push your branch to your fork:
   ```bash
   git push origin <branch-name>
   ```
2. Open a pull request against the `main` branch of the upstream repository.
3. Fill out the **pull request template** (`.github/PULL_REQUEST_TEMPLATE.md`) completely:
   - Describe what the PR does and link related issues.
   - Select the type of change (bug fix, feature, performance, etc.).
   - List the changes made.
   - Include test results and benchmark results where applicable.
   - Complete the checklist.

### Review Checklist

Reviewers will evaluate PRs against this checklist:

- [ ] Code compiles without warnings on GCC, Clang, and MSVC.
- [ ] All existing tests pass.
- [ ] New tests are included for new functionality or bug fixes.
- [ ] Code follows the style guidelines in this document.
- [ ] Every new header has the license boilerplate.
- [ ] Comments follow Google C++ style (`//`, no Doxygen tags).
- [ ] All comments and documentation use **British English** spelling.
- [ ] No external dependencies added to the core library.
- [ ] Documentation is updated if the public API changed.
- [ ] Commit messages follow the conventions described above.
- [ ] No performance regressions (benchmarks run for performance-sensitive changes).
- [ ] Every commit carries a valid `Signed-off-by` trailer (DCO).
- [ ] CLA has been executed (first-time and entity contributors).

### CI Pipeline

Every pull request triggers the GitHub Actions CI pipeline (`.github/workflows/ci.yml`), which:

- Builds the project on **Ubuntu (GCC + Clang)**, **macOS (Clang)**, and **Windows (MSVC)**.
- Runs all CTest unit tests.
- Runs the full benchmark suite with third-party benchmarks enabled.
- Performs a code quality check for compiler warnings.
- Uploads benchmark results as build artifacts (retained for 30 days).

All CI checks **must pass** before a PR can be merged.

---

## 10. Reporting Bugs

If you encounter a bug, please file an issue using the **Bug Report** template:

[Open a Bug Report](../../issues/new?template=bug_report.md)

The template (located at `.github/ISSUE_TEMPLATE/bug_report.md`) asks for:

- A clear description of the bug.
- Your environment (OS, compiler, C++ standard, `fl` version/commit).
- Step-by-step reproduction instructions.
- A minimal reproducible example in C++.
- Expected vs. actual behaviour.
- Any error output or stack traces.

Please search existing issues before filing a new one to avoid duplicates.

---

## 11. Requesting Features

Feature suggestions are welcome. Please use the **Feature Request** template:

[Open a Feature Request](../../issues/new?template=feature_request.md)

The template (located at `.github/ISSUE_TEMPLATE/feature_request.md`) asks for:

- A description of the proposed feature.
- Motivation and the problem it solves.
- A proposed solution with example usage code.
- Alternatives you have considered.
- Compatibility and performance implications.

Feature requests should align with the library's "Forever Lightweight" philosophy: minimal overhead, zero-cost abstractions, and no mandatory external dependencies for the core.

---

## 12. Performance Contributions

Performance is central to the `fl` library. Contributions that improve performance are highly valued, but they come with additional requirements.

### Performance Issue Reports

If you observe a performance regression or optimization opportunity, use the **Performance Issue** template:

[Open a Performance Issue](../../issues/new?template=performance_issue.md)

The template (located at `.github/ISSUE_TEMPLATE/performance_issue.md`) asks for detailed environment info, benchmark setup, observed vs. expected performance, and profiling data.

### Requirements for Performance PRs

1. **Benchmark evidence:** Include before-and-after benchmark results in the PR description using the table format from the PR template:
   ```
   Operation           | Before     | After      | Change
   --------------------|------------|------------|--------
   find (4 KB)         | 1200 ns    | 340 ns     | -71.7%
   ```

2. **No regressions:** Run the full benchmark suite and confirm that no other operations regressed. If a trade-off is involved, document it clearly.

3. **Reproducibility:** Describe how to reproduce the benchmark results (compiler, flags, CPU, OS). Include the exact commands used.

4. **Test coverage:** Performance changes must not break correctness. All existing tests must continue to pass, and new tests should be added if the optimisation changes control flow or data layout.

5. **Platform awareness:** If your optimisation uses platform-specific features (SIMD intrinsics, OS APIs), it must be gated behind appropriate preprocessor checks and must not break compilation on other platforms.

### Adding New Benchmarks

New benchmarks should be placed in the `benchmarks/` directory:

1. Create your benchmark source file in `benchmarks/`.
2. Register it in the root `CMakeLists.txt` following the existing pattern:
   ```cmake
   add_executable(my_new_bench benchmarks/my_new_bench.cpp)
   target_link_libraries(my_new_bench PRIVATE fl)
   ```
3. If it depends on third-party libraries, guard it with the appropriate `_FOUND` check (e.g., `if(absl_FOUND AND Boost_FOUND)`).

---

## 13. Documentation

### Where Documentation Lives

| Path              | Content                                                        |
| :---------------- | :------------------------------------------------------------- |
| `docs/API.md`     | Public API reference                                           |
| `docs/Features.md`| Feature descriptions and design rationale                      |
| `docs/Examples.md`| Usage examples                                                 |
| `docs/Formatting.md` | Formatting system documentation                             |
| `docs/Getting_Started.md` | Quick-start guide                                      |
| `docs/Performance.md` | Performance characteristics and benchmark methodology      |
| `docs/Developer_Guide.md` | Architecture and internals guide for contributors       |
| `docs/PHILOSOPHY.md` | Library design philosophy ("Forever Lightweight")           |
| `docs/Issue_Triage_Playbook.md` | Issue triage guidelines for maintainers            |
| `examples/`       | Compilable example programs                                    |

### When to Update Documentation

- **New public API:** Update `docs/API.md` with the new types, functions, or parameters.
- **New feature:** Update `docs/Features.md` and add a usage example to `docs/Examples.md` or `examples/`.
- **Behavioural change:** Update any affected documentation files to reflect the new behaviour.
- **Performance change:** Update `docs/Performance.md` with revised data.

### Documentation Style

- Write in clear, technical **British English** (see [Language](#language-british-english) above).
- Use code fences with `cpp` syntax highlighting for code examples.
- Keep examples minimal and self-contained.
- Ensure code in documentation compiles and runs correctly.

---

## 14. License

The `fl` library is licensed under the **FL License** (see [LICENSE.txt](LICENSE.txt)).

By submitting a contribution (pull request, patch, or any other form), you agree that your contribution is licensed under the same FL License terms. As stated in Section 5 of the FL License:

> Unless You explicitly state otherwise, any Contribution intentionally submitted for inclusion in the Work by You to the Licensor shall be under the terms and conditions of this License, without any additional terms or conditions.

All new source files must include the standard license boilerplate at the top:

```cpp
// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.
```

---

## 15. Developer Certificate of Origin (DCO)

Every commit submitted to the Project **must** carry a `Signed-off-by` trailer certifying the
[Developer Certificate of Origin](DCO.md). This is a lightweight, per-commit provenance record
that asserts you have the legal right to submit the Contribution under the FL Licence.

### What the Sign-off Certifies

By adding the trailer, you certify (in summary) that:

- The Contribution is your own original work, **or** is based on prior work available under a
  compatible open-source licence.
- You have the legal authority to submit it — including any required Employer consent.
- You have disclosed any patent claims you are aware of that could cover the Contribution.
- You understand the Contribution may be relicensed by the Copyright Holder under a future
  version of the FL Licence.

Read [DCO.md](DCO.md) in full before signing off for the first time.

### Adding the Sign-off

Append the following trailer to every commit message, preceded by a blank line:

```
Signed-off-by: Your Full Name <your.email@example.com>
```

Git can append this automatically with the `-s` flag:

```bash
git commit -s -m "feat: describe your change"
```

Or configure an alias:

```bash
git config --global alias.cs "commit -s"
```

### Local Pre-commit Hook (Optional)

To have Git automatically validate the `Signed-off-by` trailer **before** committing, you can install
the local pre-commit hook provided in the repository:

```bash
cp scripts/pre-commit-dco .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

Or, to make the hook apply to all developers who clone the repository, configure git to look for
hooks in the `scripts/` directory:

```bash
git config core.hooksPath scripts
```

If you accidentally commit without the sign-off, you can amend and sign the last commit:

```bash
git commit --amend -s
git push --force-with-lease
```

### Retroactive Certification

If you have already committed without a Sign-off and wish to certify those commits retroactively,
contact the maintainer with a written statement as described in Section 6 of [DCO.md](DCO.md).
Do **not** amend or rebase merged commits.

---

## 16. Contributor Licence Agreement (CLA)

The [Contributor Licence Agreement](CLA.md) is a formal legal instrument governing the terms on
which Contributions are accepted. It provides:

- A broad copyright licence grant to the Copyright Holder and all downstream recipients.
- A patent licence grant covering the Licensed Patents.
- Representations and warranties regarding provenance, authority, and third-party IP.
- Governing law: **Commonwealth of the Bahamas**.
- Per-contributor liability caps in each Contributor's local currency, ensuring fairness across
  jurisdictions.

### Who Must Sign the CLA

| Contributor Type                         | CLA Required?                        |
| :--------------------------------------- | :----------------------------------- |
| Individual (first Contribution)          | Yes — execute before first PR        |
| Individual (subsequent Contributions)    | No — existing CLA covers all work    |
| Legal entity / corporation               | Yes — entity CLA required            |
| Employee contributing on behalf of employer | Yes — employer (entity) CLA required |

### How to Execute the CLA

1. Read [CLA.md](CLA.md) carefully in its entirety.
2. Complete the signature block (individual or entity form as appropriate).
3. Submit the executed CLA to the Copyright Holder by one of the methods described in
   Clause 24.1 of the CLA:
   - Open a pull request with your completed signature block in a comment; or
   - Send a signed copy (scanned PDF or typed electronic signature) to the Copyright Holder
     via the contact method listed in [SECURITY.md](SECURITY.md).
4. The Copyright Holder will confirm acceptance. Do not open a substantive pull request until
   acceptance is confirmed for first-time and entity contributors.

### Relationship between DCO and CLA

The DCO and CLA are complementary instruments:

| Instrument | Scope                                | Per-Commit? |
| :--------- | :----------------------------------- | :---------- |
| DCO        | Per-commit provenance certification  | Yes         |
| CLA        | Formal licence grant and IP warranty | No (once)   |

Both are required. The DCO Sign-off on each commit confirms that the specific Contribution was
made in accordance with both the DCO and the CLA.

---

## Project Structure Reference

```
Flstring/
├── include/
│   ├── fl.hpp                         # Umbrella header (includes all components)
│   └── fl/
│       ├── config.hpp                 # Configuration and feature detection
│       ├── string.hpp                 # Core fl::string with SSO
│       ├── arena.hpp                  # Arena allocators and temporary buffers
│       ├── builder.hpp                # String builder with growth policies
│       ├── format.hpp                 # Formatting utilities
│       ├── sinks.hpp                  # Output sink abstractions
│       ├── substring_view.hpp         # Non-owning string views
│       ├── rope.hpp                   # Tree-based efficient concatenation
│       ├── immutable_string.hpp       # Immutable strings with hash caching
│       ├── synchronised_string.hpp    # Thread-safe mutable string
│       ├── synchronized_string.hpp    # Alias (American English spelling)
│       ├── alloc_hooks.hpp            # Allocation hooks
│       ├── profiling.hpp              # Profiling utilities
│       └── debug/
│           └── thread_safety.hpp      # Thread-safety debug instrumentation
├── tests/                             # Unit tests (registered with CTest)
├── benchmarks/                        # Performance benchmarks
├── examples/                          # Compilable usage examples
├── docs/                              # Project documentation
├── .github/
│   ├── workflows/
│   │   ├── ci.yml                     # GitHub Actions CI pipeline
│   ├── ISSUE_TEMPLATE/
│   │   ├── bug_report.md              # Bug report template
│   │   ├── feature_request.md         # Feature request template
│   │   └── performance_issue.md       # Performance issue template
│   └── PULL_REQUEST_TEMPLATE.md       # Pull request template
├── CMakeLists.txt                     # Root build configuration
├── LICENSE.txt                        # FL License
├── DCO.md                             # Developer Certificate of Origin
├── CLA.md                             # Contributor Licence Agreement
└── CONTRIBUTING.md                    # This file
```

---

Thank you for helping make `fl` better. Every contribution -- whether a bug report, a documentation fix, a test case, or a performance optimisation -- is valued.
