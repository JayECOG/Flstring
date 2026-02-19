# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 1.0.x   | Yes       |
| < 1.0   | No        |

Security fixes are applied to the latest release on the `main` branch.
Older versions will not receive backported patches.

## What Constitutes a Security Issue

The fl library is a header-only C++20 string library. It performs no network,
filesystem, IPC, or cryptographic operations. Security issues in this context
include:

- Buffer overflows or heap overflows in string operations
- Out-of-bounds reads or writes
- Use-after-free or use-after-move conditions
- Memory corruption (e.g., double-free, wild writes from the pool allocator)
- Data races leading to undefined behavior
- Any path to undefined behavior reachable through the public API under
  documented preconditions

## Reporting a Vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Instead, use one of the following channels:

1. **GitHub Security Advisories (preferred):** Navigate to the repository's
   "Security" tab and select "Report a vulnerability" to file a private
   advisory directly on GitHub.
2. **Private email:** Send a detailed report to `coping-satin-dad@duck.com` (used until I create a dedicated email for the library).
   Use a descriptive subject line such as "fl security report: [brief summary]".

### What to Include

- A clear description of the vulnerability and the affected component
  (e.g., `fl::string::insert`, pool allocator, SIMD search path).
- Step-by-step reproduction instructions, including compiler, platform, and
  optimization level.
- Impact assessment: what an attacker or faulty program can achieve (crash,
  information disclosure, arbitrary write, etc.).
- Affected versions, if known.
- A proposed fix or patch, if available.

### Response Timeline

- **Acknowledgement:** within 3 business days of receipt.
- **Initial assessment:** within 10 business days.
- **Fix or mitigation:** target release within 30 days for confirmed issues.

## Security Considerations

### Thread Safety Model

The fl library provides three string types with distinct thread-safety
guarantees:

| Type                        | Thread Safety                                      |
|-----------------------------|----------------------------------------------------|
| `fl::string`                | **Not thread-safe.** Single-owner; concurrent access from multiple threads is undefined behavior. |
| `fl::synchronised_string`   | **Thread-safe.** All access is serialized through an internal `std::shared_mutex`. Concurrent readers are permitted when no writer is active. |
| `fl::immutable_string`      | **Thread-safe.** Shared ownership via atomic reference counting. Immutable after construction; safe to read from any number of threads concurrently. |

### Debug Thread-Safety Diagnostics

Define `FL_DEBUG_THREAD_SAFETY` (or enable the corresponding CMake option) in
debug builds to activate runtime detection of concurrent access violations.
When enabled, every read, write, and move operation on `fl::string` is tracked
through an atomic state machine that aborts with a diagnostic message on
detecting a data race.

This mechanism compiles to a zero-overhead stub in release builds.

### Memory Safety

- The pool allocator uses thread-local storage. Allocation and deallocation on
  the hot path do not acquire global locks.
- Small strings (up to 23 bytes) are stored inline via SSO and never touch the
  heap allocator.
- SIMD search paths (SSE2/AVX2) operate only on memory owned by the string
  instance and respect alignment requirements.

### Minimal Attack Surface

- The core library contains **no** network, filesystem, or IPC code.
- The core library performs **no** cryptographic operations and must not be used
  as a source of cryptographic primitives.

## Security Best Practices for Users

1. **Use thread-safe types for shared data.** When a string is accessed from
   multiple threads, use `fl::synchronised_string` for mutable shared strings
   or `fl::immutable_string` for read-only shared strings. Never share a bare
   `fl::string` across threads without external synchronization.

2. **Enable `FL_DEBUG_THREAD_SAFETY` in debug and CI builds.** This catches
   data races early, before they manifest as hard-to-diagnose corruption in
   production.

3. **Use bounds-checked access on untrusted input.** Prefer `at()` over
   `operator[]` when indexing with values derived from external or untrusted
   sources. `at()` throws `std::out_of_range` on invalid indices.

4. **Do not assume null-termination on `fl::substring_view`.** A
   `substring_view` is a non-owning view into a region of another string.
   Its `data()` pointer is **not** null-terminated. Passing it directly to
   C APIs that expect a null-terminated `const char*` is undefined behavior.
   Copy the view into an `fl::string` or `std::string` first if a
   null-terminated representation is required.

## Disclosure Policy

This project follows a **coordinated disclosure** model:

1. The reporter privately discloses the vulnerability using one of the channels
   described above.
2. The maintainers acknowledge receipt, assess severity, and develop a fix.
3. A patched release is published, and a public advisory is issued
   simultaneously.
4. If no fix or mitigation is available within **90 days** of the initial
   report, the reporter may disclose the vulnerability publicly.

We appreciate the security research community's efforts in responsibly
disclosing issues and will credit reporters in the advisory (unless anonymity
is requested).

---

Copyright (c) 2026 Jayden Emmanuel. Licensed under the FL License. See
[LICENSE.txt](LICENSE.txt) for details.
