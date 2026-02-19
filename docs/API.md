# `fl` API Reference

C++20, header-only string toolkit optimised for low-allocation workloads.

```cpp
#include <fl.hpp>          // pulls in the full library
```

Individual component headers may be included directly from `fl/` to keep
translation units trim.

---

## Contents

- [`fl::string`](#flstring)
- [`fl::string_builder`](#flstring_builder)
- [`fl::substring_view`](#flsubstring_view)
- [`fl::rope`](#flrope)
- [`fl::immutable_string` / `fl::immutable_string_view`](#flimmutable_string--flimmutable_string_view)
- [`fl::synchronised_string`](#flsynchronised_string)
- [Arena utilities](#arena-utilities)
- [Sinks](#sinks)
- [Formatting](#formatting)
- [Allocator utilities](#allocator-utilities)

---

## `fl::string`

**Header:** `#include <fl/string.hpp>`

A `std::string`-compatible string class with 23-byte Small String Optimisation
(SSO), pool-backed heap allocation, and SIMD-accelerated search. Strings up to
23 characters are stored entirely on the stack with no heap allocation.

**Thread safety:** Single-owner, not thread-safe. In debug builds with
`FL_DEBUG_THREAD_SAFETY` defined, concurrent access is detected at runtime.
Use `fl::synchronised_string` for thread-safe mutation.

### Constants

```cpp
static constexpr size_type npos = static_cast<size_type>(-1);
```

### Member types

| Type | Definition |
|------|-----------|
| `value_type` | `char` |
| `size_type` | `std::size_t` |
| `difference_type` | `std::ptrdiff_t` |
| `reference` | `char&` |
| `const_reference` | `const char&` |
| `pointer` | `char*` |
| `const_pointer` | `const char*` |
| `iterator` | `char*` |
| `const_iterator` | `const char*` |
| `reverse_iterator` | `std::reverse_iterator<char*>` |
| `const_reverse_iterator` | `std::reverse_iterator<const char*>` |

### Constructors

```cpp
string() noexcept;                                        // empty (SSO)
string(const char* cstr) noexcept;                        // from null-terminated C-string
string(const char* cstr, size_type len);                  // from pointer + length
template <std::size_t N>
string(const char (&cstr)[N]);                            // from string literal (no strlen)
string(const std::string& s) noexcept;                    // from std::string
string(std::string_view s) noexcept;                      // from std::string_view
string(size_type count, char ch);                         // repeated character
string(const string& other);                              // copy
string(string&& other) noexcept;                          // move
```

### Assignment

```cpp
string& operator=(const string& other) noexcept;
string& operator=(string&& other) noexcept;
string& operator=(const char* cstr) noexcept;
string& operator=(std::string_view s) noexcept;

string& assign(const char* cstr, size_type len) noexcept;
string& assign(const char* cstr) noexcept;
string& assign(const string& other) noexcept;
string& assign(std::string_view sv) noexcept;
string& assign(size_type count, char ch) noexcept;

void swap(string& other) noexcept;
```

### Element access

```cpp
const_reference operator[](size_type pos) const noexcept;
reference       operator[](size_type pos) noexcept;

const_reference at(size_type pos) const;   // throws std::out_of_range
reference       at(size_type pos);         // throws std::out_of_range

const_reference front() const noexcept;
reference       front() noexcept;

const_reference back() const noexcept;
reference       back() noexcept;

const_pointer data() const noexcept;
pointer       data() noexcept;

const char* c_str() const noexcept;        // always null-terminated
```

### Iterators

```cpp
iterator       begin() noexcept;         const_iterator begin() const noexcept;
iterator       end() noexcept;           const_iterator end() const noexcept;
const_iterator cbegin() const noexcept;  const_iterator cend() const noexcept;
reverse_iterator       rbegin() noexcept;
const_reverse_iterator rbegin() const noexcept;
reverse_iterator       rend() noexcept;
const_reverse_iterator rend() const noexcept;
```

### Capacity

```cpp
size_type size() const noexcept;
size_type length() const noexcept;          // same as size()
size_type capacity() const noexcept;        // SSO_CAPACITY (23) or heap capacity
bool      empty() const noexcept;

void reserve(size_type cap);
void shrink_to_fit();
void clear() noexcept;
void resize(size_type new_size, char fill = '\0') noexcept;
```

### Modifiers

```cpp
// Append
string& append(std::string_view sv) noexcept;
string& append(const char* cstr) noexcept;
string& append(const char* cstr, size_type len) noexcept;
string& append(const string& other) noexcept;
string& append(char ch) noexcept;
string& append(size_type count, char ch) noexcept;
template <std::input_iterator InputIter>
string& append(InputIter first, InputIter last) noexcept;

string& operator+=(const char* cstr) noexcept;
string& operator+=(const string& str) noexcept;
string& operator+=(char ch) noexcept;
string& operator+=(std::string_view s) noexcept;

void push_back(char ch) noexcept;
void pop_back() noexcept;

// Erase
string& erase(size_type pos = 0, size_type len = npos) noexcept;
iterator erase(const_iterator pos) noexcept;
iterator erase(const_iterator first, const_iterator last) noexcept;

// Insert
string& insert(size_type pos, const string& str) noexcept;
string& insert(size_type pos, const char* cstr, size_type len) noexcept;
string& insert(size_type pos, const char* cstr) noexcept;
string& insert(size_type pos, size_type count, char ch) noexcept;
iterator insert(const_iterator pos, char ch) noexcept;
iterator insert(const_iterator pos, size_type count, char ch) noexcept;

// Replace
string& replace(size_type pos, size_type len, const string& str) noexcept;
string& replace(size_type pos, size_type len, const char* cstr, size_type clen) noexcept;
string& replace(size_type pos, size_type len, const char* cstr) noexcept;
string& replace(size_type pos, size_type len, size_type count, char ch) noexcept;
```

### Search

```cpp
// find: returns position or npos
size_type find(char ch, size_type pos = 0) const noexcept;
size_type find(const char* substr, size_type pos = 0) const noexcept;
size_type find(std::string_view sv, size_type pos = 0) const noexcept;
template <std::size_t N>
size_type find(const char (&substr)[N], size_type pos = 0) const noexcept;

// rfind: last occurrence
size_type rfind(char ch, size_type pos = npos) const noexcept;
size_type rfind(std::string_view sv, size_type pos = npos) const noexcept;

// set membership
size_type find_first_of(char ch, size_type pos = 0) const noexcept;
size_type find_first_of(std::string_view sv, size_type pos = 0) const noexcept;
size_type find_last_of(char ch, size_type pos = npos) const noexcept;
size_type find_last_of(std::string_view sv, size_type pos = npos) const noexcept;
size_type find_first_not_of(char ch, size_type pos = 0) const noexcept;
size_type find_first_not_of(std::string_view sv, size_type pos = 0) const noexcept;
size_type find_last_not_of(char ch, size_type pos = npos) const noexcept;
size_type find_last_not_of(std::string_view sv, size_type pos = npos) const noexcept;
```

> **Implementation note:** Single-character `find` uses `memchr`. Substrings
> ≤ 64 KB delegate to `std::string_view::find` (glibc `memmem`). Substrings in
> haystacks ≥ 64 KB use a Two-Way O(n+m) / O(1)-space algorithm with an AVX2
> pre-scan when built with `-mavx2`.

### Substring and views

```cpp
string substr(size_type pos = 0, size_type len = npos) const; // allocates; throws std::out_of_range on bad pos

// Zero-copy views (non-owning; caller must ensure fl::string outlives the view)
fl::substring_view substr_view(size_type pos = 0, size_type len = npos) const noexcept;
fl::substring_view slice(size_type pos = 0, size_type len = npos) const noexcept; // alias
fl::substring_view left_view(size_type count) const noexcept;
fl::substring_view right_view(size_type count) const noexcept;
fl::substring_view find_view(std::string_view needle, size_type pos = 0) const noexcept; // empty on no match
```

### Comparison

```cpp
std::strong_ordering operator<=>(const string& other) const noexcept;
bool operator==(const string& other) const noexcept;

int compare(const string& other) const noexcept;
int compare(std::string_view sv) const noexcept;
int compare(size_type pos, size_type len, std::string_view sv) const; // throws std::out_of_range

bool starts_with(std::string_view sv) const noexcept;
bool starts_with(char ch) const noexcept;
bool ends_with(std::string_view sv) const noexcept;
bool ends_with(char ch) const noexcept;
bool contains(std::string_view sv) const noexcept;
bool contains(char ch) const noexcept;
```

### Conversion

```cpp
operator std::string_view() const noexcept;   // implicit, no allocation
```

### Non-member operators

```cpp
string operator+(const string& lhs, const string& rhs);
string operator+(string&& lhs, const string& rhs);
string operator+(const string& lhs, string&& rhs);
string operator+(string&& lhs, string&& rhs);
string operator+(const string& lhs, const char* rhs);
string operator+(string&& lhs, const char* rhs);
string operator+(const char* lhs, const string& rhs);
string operator+(const char* lhs, string&& rhs);

std::ostream& operator<<(std::ostream& os, const string& s);  // via string_view
```

---

## `fl::string_builder`

**Header:** `#include <fl/builder.hpp>`

A move-only, fluent string builder with configurable growth policy and
zero-allocation ownership transfer to `fl::string`.

**Copy:** Disabled (`= delete`). Move-only.

### `fl::growth_policy`

```cpp
enum class growth_policy {
    linear,      // grow by a fixed increment (default: 32 bytes)
    exponential, // grow by 1.5–2× (default)
};
```

### Constructors

```cpp
string_builder() noexcept;                         // default, exponential growth
explicit string_builder(size_type initial_capacity) noexcept;
string_builder(string_builder&& other) noexcept;
```

### Configuration

```cpp
string_builder& set_growth_policy(growth_policy policy) noexcept;
string_builder& set_linear_growth(size_type increment) noexcept;
string_builder& reserve(size_type cap) noexcept;
string_builder& reserve_for_elements(size_type element_count,
                                     size_type avg_element_size = 16) noexcept;
```

### Append

```cpp
string_builder& append(const char* cstr) noexcept;
string_builder& append(const char* cstr, size_type len) noexcept;
string_builder& append(const string& str) noexcept;
string_builder& append(std::string_view sv) noexcept;
string_builder& append(std::span<const char> s) noexcept;
string_builder& append(char ch) noexcept;
string_builder& append_repeat(char ch, size_type count) noexcept;

template <std::input_iterator InputIter>
string_builder& append(InputIter first, InputIter last) noexcept;

// Single-placeholder formatting ({} for one value)
template <typename T>  // T: integral, floating_point, or string_view-convertible
string_builder& append_formatted(const char* fmt, T value) noexcept;

string_builder& operator+=(const char* cstr) noexcept;
string_builder& operator+=(const string& str) noexcept;
string_builder& operator+=(char ch) noexcept;
string_builder& operator+=(std::string_view sv) noexcept;
```

### Introspection

```cpp
size_type   size() const noexcept;
size_type   capacity() const noexcept;
bool        empty() const noexcept;
const char* data() const noexcept;
char*       data() noexcept;
char&       operator[](size_type pos) noexcept;
const char& operator[](size_type pos) const noexcept;
char*       begin() noexcept;   const char* begin() const noexcept;
char*       end() noexcept;     const char* end() const noexcept;
```

### Finalise

```cpp
void   clear() noexcept;                // reset size, keep capacity
string build() && noexcept;             // rvalue-only: transfers ownership to fl::string
```

> `build()` is rvalue-qualified. Call it via `std::move(builder).build()`.

### Example

```cpp
fl::string_builder sb(256);
sb.append("Hello").append(", ").append("world").append('!');
fl::string result = std::move(sb).build();   // zero-copy ownership transfer for heap strings
```

---

## `fl::substring_view`

**Header:** `#include <fl/substring_view.hpp>`

Lightweight, non-owning view over a character range. Optionally holds a
`shared_ptr<const void>` to keep the underlying allocation alive.

**Lifetime:** When constructed from `fl::string` directly (e.g., via
`string::substr_view()`), no shared ownership is acquired — the caller must
ensure the source `fl::string` outlives the view. When constructed from
`std::string` or with an explicit `owner` pointer, lifetime is managed.

### Constants

```cpp
static constexpr size_type npos = std::string::npos;
```

### Constructors

```cpp
substring_view() noexcept;                                    // empty
explicit substring_view(const char* cstr) noexcept;           // from null-terminated string
substring_view(const char* data, size_type len,
               std::shared_ptr<const void> owner = nullptr) noexcept;
substring_view(const string& str,
               size_type offset = 0,
               size_type len = std::string::npos) noexcept;   // no lifetime management
substring_view(const std::string& str,
               size_type offset = 0,
               size_type len = std::string::npos) noexcept;   // shared ownership of copy
substring_view(const substring_view&) noexcept = default;
substring_view(substring_view&&) noexcept = default;
```

### Element access

```cpp
reference       operator[](size_type pos) const noexcept;   // assert checked
const_reference at(size_type pos) const;                    // throws std::out_of_range
reference       front() const noexcept;
reference       back() const noexcept;
const_pointer   data() const noexcept;
const_pointer   c_str() const noexcept;   // same as data(); NOT null-terminated
```

### Capacity

```cpp
size_type size() const noexcept;
size_type length() const noexcept;
bool      empty() const noexcept;
```

### Iterators

```cpp
const_iterator         begin() / end() / cbegin() / cend() const noexcept;
const_reverse_iterator rbegin() / rend() / crbegin() / crend() const noexcept;
```

### Search

```cpp
size_type find(char ch, size_type offset = 0) const noexcept;
size_type find(const substring_view& substr, size_type offset = 0) const noexcept;
size_type find(const char* substr, size_type offset = 0) const noexcept;
size_type rfind(char ch) const noexcept;
size_type rfind(const substring_view& substr) const noexcept;
```

### Substring / predicates

```cpp
substring_view substr(size_type offset = 0, size_type len = std::string::npos) const noexcept;
bool starts_with(const substring_view& prefix) const noexcept;
bool ends_with(const substring_view& suffix) const noexcept;
bool contains(const substring_view& substr) const noexcept;
```

### Comparison

```cpp
std::strong_ordering operator<=>(const substring_view& other) const noexcept;
bool operator==(const substring_view& other) const noexcept;
bool operator!=(const substring_view& other) const noexcept;
bool operator==(const char* cstr) const noexcept;
bool operator!=(const char* cstr) const noexcept;
```

### Conversion

```cpp
std::string to_string() const;         // allocates a copy
fl::string  to_fl_string() const;      // allocates a copy
```

### Non-member helpers

```cpp
template <std::size_t N>
substring_view make_substring_view(const char (&arr)[N]) noexcept;

struct substring_view_hash  { std::size_t operator()(const substring_view&) const noexcept; };
struct substring_view_equal { bool operator()(const substring_view&, const substring_view&) const noexcept; };

std::ostream& operator<<(std::ostream&, const substring_view&);
```

---

## `fl::rope`

**Header:** `#include <fl/rope.hpp>`

Persistent, balanced binary tree for efficient concatenation-heavy workloads.
Leaf merging and AVL-style rebalancing keep tree depth at O(log N).

**Complexity summary:**

| Operation | Complexity |
|-----------|-----------|
| `operator+` / `operator+=` | O(1) amortised (O(n) when merging small leaves) |
| `operator[]` / `at` | O(log n); O(1) if leaf or cache warm |
| `flatten()` / `to_std_string()` | O(n) |
| `substr()` | O(n) for the extracted range |
| Iterator (`begin`/`end`) | O(n) to linearise on first access |

### Member types

```cpp
using value_type      = char;
using size_type       = std::size_t;
using difference_type = std::ptrdiff_t;
using const_reference = const char;
using iterator        = const char*;
using const_iterator  = const char*;
```

### Constructors

```cpp
rope() noexcept;                                   // empty
rope(const char* cstr) noexcept;
explicit rope(const char* data, size_type len);
explicit rope(std::string_view view);
rope(const fl::string& str) noexcept;
rope(const fl::substring_view& view) noexcept;
rope(const rope&) noexcept = default;              // shared node ownership (O(1))
rope(rope&&) noexcept = default;
```

### Capacity

```cpp
size_type length() const noexcept;
size_type size() const noexcept;   // alias for length()
bool      empty() const noexcept;
```

### Element access

```cpp
char operator[](size_type pos) const noexcept;   // O(log n)
char at(size_type pos) const;                    // O(log n); throws std::out_of_range
char front() const noexcept;
char back() const noexcept;
```

### Concatenation

```cpp
rope operator+(const rope& other) const;
rope operator+(const char* cstr) const;
rope operator+(const fl::string& str) const;

rope& operator+=(const rope& other);
rope& operator+=(std::string_view sv);           // avoids intermediate fl::string allocation
rope& operator+=(const char* cstr);
rope& operator+=(const fl::string& str);
rope& operator+=(const std::string& str);
```

### Comparison

```cpp
std::strong_ordering operator<=>(const rope& other) const noexcept;
bool operator==(const rope& other) const noexcept;
```

### Conversion and linearisation

```cpp
fl::string   flatten() const;                    // O(n): contiguous fl::string copy
std::string  to_std_string() const;              // O(n): contiguous std::string copy
fl::substring_view substr(size_type offset = 0,
                          size_type len = std::string::npos) const;
```

### Iteration

```cpp
// Both linearise internally on first call — O(n) once, then O(1) via cache.
std::string::const_iterator begin() const;
std::string::const_iterator end() const;
std::string_view            linear_view() const;  // zero-copy view of linearised cache
```

### Rebalancing

```cpp
void rebalance() noexcept;                             // no-op unless depth > 64
void rebalance(size_type depth_threshold) noexcept;    // force if depth > threshold
bool flatten_if_deep(size_type depth_threshold) noexcept; // returns true if flattened
```

### Diagnostics

```cpp
std::size_t depth() const noexcept;   // current tree depth
```

> **Usage pattern:** Prefer repeated `operator+=` (builds a balanced AVL tree
> with O(1) per append on average, no forced rebalance until depth > 64).
> Call `flatten()` before passing the result to C APIs that require a
> contiguous buffer.

---

## `fl::immutable_string` / `fl::immutable_string_view`

**Header:** `#include <fl/immutable_string.hpp>`

### `fl::immutable_string_view`

Non-owning view over a character range with lazy-cached FNV-1a hash for
efficient use as map keys.

```cpp
immutable_string_view() noexcept;
immutable_string_view(const char* cstr) noexcept;
explicit immutable_string_view(const char* data, size_type len) noexcept;

const char* data() const noexcept;
size_type   size() const noexcept;
size_type   length() const noexcept;
bool        empty() const noexcept;
char        operator[](size_type pos) const noexcept;
char        at(size_type pos) const;    // throws std::out_of_range
char        front() const noexcept;
char        back() const noexcept;
const char* begin() const noexcept;
const char* end() const noexcept;

bool operator==(const immutable_string_view&) const noexcept;
bool operator!=(const immutable_string_view&) const noexcept;
bool operator< / > / <= / >=(const immutable_string_view&) const noexcept;

bool        contains(char c) const noexcept;
bool        contains(const immutable_string_view& s) const noexcept;
size_type   find(char c, size_type pos = 0) const noexcept;
size_type   find(const immutable_string_view& needle, size_type pos = 0) const noexcept;
size_type   hash() const noexcept;     // cached after first call
std::string to_string() const;

static constexpr size_type npos = static_cast<size_type>(-1);
```

### `fl::immutable_string`

Thread-safe, atomic reference-counted immutable string. All copies are O(1)
lock-free operations. No mutation operations exist.

**Thread safety:** All operations are safe for concurrent use from multiple
threads.

```cpp
immutable_string() noexcept;
explicit immutable_string(const char* str);
immutable_string(const char* str, size_type len);
immutable_string(immutable_string_view view);
explicit immutable_string(const std::string& str);
immutable_string(const immutable_string& other) noexcept;   // atomic refcount increment
immutable_string(immutable_string&& other) noexcept;
immutable_string& operator=(const immutable_string& other) noexcept;
immutable_string& operator=(immutable_string&& other) noexcept;

const char*            data() const noexcept;
size_type              size() const noexcept;
size_type              length() const noexcept;
bool                   empty() const noexcept;
char                   operator[](std::size_t pos) const noexcept;
immutable_string_view  view() const noexcept;
size_type              hash() const noexcept;   // cached after first call
std::string            to_string() const;
operator immutable_string_view() const noexcept;
```

### Non-member operators and helpers

```cpp
bool operator==(const immutable_string&, const immutable_string&) noexcept;
bool operator!=(const immutable_string&, const immutable_string&) noexcept;
bool operator==(const immutable_string_view&, const char*) noexcept;
bool operator==(const char*, const immutable_string_view&) noexcept;
bool operator==(const immutable_string&, const immutable_string_view&) noexcept;
bool operator==(const immutable_string_view&, const immutable_string&) noexcept;

std::ostream& operator<<(std::ostream&, const immutable_string_view&);
std::ostream& operator<<(std::ostream&, const immutable_string&);

struct immutable_string_hash {
    std::size_t operator()(const immutable_string_view&) const noexcept;
    std::size_t operator()(const immutable_string&) const noexcept;
};
struct immutable_string_equal {
    using is_transparent = void;
    bool operator()(const immutable_string_view&, const immutable_string_view&) const noexcept;
    bool operator()(const immutable_string&, const immutable_string&) const noexcept;
};

using owning_immutable_string = immutable_string;   // compat alias
```

---

## `fl::synchronised_string`

**Header:** `#include <fl/synchronised_string.hpp>`

Mutex-guarded mutable string. Concurrent readers share a `std::shared_lock`;
writers acquire an exclusive `std::unique_lock`. Access is primarily through
callbacks to prevent raw reference leakage.

```cpp
synchronised_string() = default;
explicit synchronised_string(const char* cstr);
explicit synchronised_string(const fl::string& s);
synchronised_string(const synchronised_string& other);
synchronised_string(synchronised_string&& other) noexcept;
synchronised_string& operator=(const synchronised_string& other);
synchronised_string& operator=(synchronised_string&& other) noexcept;
```

### Observers

```cpp
size_type   size() const noexcept;
bool        empty() const noexcept;
fl::string  to_fl_string() const noexcept;
fl::string  snapshot() const noexcept;          // alias for to_fl_string()
int compare(const synchronised_string& other) const noexcept;
int compare(std::string_view sv) const noexcept;
```

### Modifiers

```cpp
synchronised_string& operator+=(const char* cstr);
synchronised_string& operator+=(const fl::string& other);
synchronised_string& operator+=(std::string_view sv);
synchronised_string& operator+=(const std::string& s);
synchronised_string& operator+=(char ch);
synchronised_string& append(const char* buf, size_type len);
synchronised_string& append(const fl::string& other);
synchronised_string& append(std::string_view sv);
void push_back(char ch);
void pop_back();
void clear();
void swap(synchronised_string& other) noexcept;
```

### Callback-based access (recommended)

```cpp
// Read-only: acquires shared lock, permits concurrent readers.
template <std::invocable<const fl::string&> Func>
auto read(Func&& f) const
    noexcept(std::is_nothrow_invocable_v<Func, const fl::string&>)
    -> std::invoke_result_t<Func, const fl::string&>;

// Write: acquires exclusive lock.
template <std::invocable<fl::string&> Func>
auto write(Func&& f)
    noexcept(std::is_nothrow_invocable_v<Func, fl::string&>)
    -> std::invoke_result_t<Func, fl::string&>;
```

```cpp
// US-spelling alias
using synchronized_string = synchronised_string;
```

### Example

```cpp
fl::synchronised_string shared;

// Writer thread
shared.write([](fl::string& s) { s.append("hello"); });

// Reader thread
auto len = shared.read([](const fl::string& s) { return s.size(); });
```

---

## Arena utilities

**Header:** `#include <fl/arena.hpp>`

### `fl::arena_allocator<StackSize>`

Stack-first bump allocator. Allocations up to `StackSize` bytes (default 4096)
are served from an inline stack buffer; larger allocations fall back to the heap.
Not copyable or movable.

```cpp
template <std::size_t StackSize = 4096>
class arena_allocator {
    void*       allocate(std::size_t size);
    void        deallocate(void* ptr, std::size_t size) noexcept;
    void        reset() noexcept;                    // free heap blocks; reset stack pointer
    std::size_t available_stack() const noexcept;    // remaining bytes in inline buffer
    std::size_t total_allocated() const noexcept;    // stack used + total heap bytes
};
```

### `fl::arena_buffer<StackSize>`

Append-only character buffer backed by an `arena_allocator`. Not copyable or
movable. Default initial capacity: 256 bytes.

```cpp
template <std::size_t StackSize = 4096>
class arena_buffer {
    explicit arena_buffer(size_type initial_capacity = 256);

    arena_buffer& append(const char* cstr) noexcept;
    arena_buffer& append(const char* cstr, size_type len) noexcept;
    arena_buffer& append(char ch) noexcept;
    arena_buffer& append_repeat(char ch, size_type count) noexcept;

    fl::string    to_string() const;   // allocates a contiguous copy
    void          clear() noexcept;    // reset size; keep capacity
    void          reset() noexcept;    // release heap blocks; reinitialise
};
```

### Pooled temporary buffers

```cpp
using temp_buffer = std::unique_ptr<arena_buffer<4096>, /*pool deleter*/>;

// Acquire a reusable temporary buffer from a thread-local pool (max 8 pooled).
// Returns the buffer to the pool on destruction instead of freeing it.
temp_buffer get_pooled_temp_buffer();
```

---

## Sinks

**Header:** `#include <fl/sinks.hpp>`

Output destinations for the formatting API, all inside `fl::sinks`.

### `fl::sinks::output_sink` (base class)

```cpp
virtual void write(const char* data, std::size_t len) = 0;  // must override
virtual void flush() {}                                      // optional
void write_char(char ch);
void write_string(const fl::string& str);
void write_cstring(const char* cstr);
```

### Concrete sinks

| Class | Description |
|-------|-------------|
| `sinks::buffer_sink` | Writes to a pre-allocated `char*` buffer; throws `std::overflow_error` on overflow |
| `sinks::file_sink` | Writes to a `FILE*`; throws `std::runtime_error` on open/write failure |
| `sinks::stream_sink` | Writes to a `std::ostream` reference |
| `sinks::growing_sink` | Auto-growing `std::vector<char>` buffer |
| `sinks::null_sink` | Discards all output; counts discarded bytes |
| `sinks::multi_sink` | Fan-out to multiple `shared_ptr<output_sink>` targets |

#### `sinks::buffer_sink`

```cpp
buffer_sink(char* buffer, std::size_t capacity) noexcept;
std::size_t written() const noexcept;
std::size_t available() const noexcept;
void        null_terminate();
void        reset() noexcept;
char*       buffer() noexcept;
```

#### `sinks::file_sink`

```cpp
explicit file_sink(const char* filename, bool append = false);   // opens file
explicit file_sink(std::FILE* file, bool owns = false) noexcept; // wraps existing FILE*
void flush() override;
```

#### `sinks::stream_sink`

```cpp
explicit stream_sink(std::ostream& stream) noexcept;
```

#### `sinks::growing_sink`

```cpp
explicit growing_sink(std::size_t initial_capacity = 256);
fl::string  to_fl_string() const;
std::size_t size() const noexcept;
const char* data() const noexcept;
void        null_terminate();
void        reset() noexcept;
```

#### `sinks::null_sink`

```cpp
null_sink() noexcept;
std::size_t bytes_written() const noexcept;
void        reset() noexcept;
```

#### `sinks::multi_sink`

```cpp
multi_sink();
void add_sink(std::shared_ptr<output_sink> sink);
```

### Factory helpers

```cpp
template <std::size_t N>
sinks::buffer_sink make_buffer_sink(char (&buffer)[N]) noexcept;

std::shared_ptr<sinks::file_sink>    make_file_sink(const char* filename, bool append = false);
std::shared_ptr<sinks::stream_sink>  make_stream_sink(std::ostream& stream) noexcept;
std::shared_ptr<sinks::growing_sink> make_growing_sink(std::size_t initial_capacity = 256);
std::shared_ptr<sinks::null_sink>    make_null_sink() noexcept;
```

---

## Formatting

**Header:** `#include <fl/format.hpp>`

```cpp
template <typename... Args>
void format_to(sinks::buffer_sink& sink, const char* fmt, Args&&... args);
```

Formats `args` into `sink` using a `{}`-based format string.

### Format specification syntax

Placeholders take the form `{}` (positional, sequential) or `{:spec}`.

| Specifier | Meaning |
|-----------|---------|
| `{}`      | Default formatting |
| `{:N}`    | Minimum field width N, right-aligned |
| `{:<N}`   | Left-align in width N |
| `{:>N}`   | Right-align in width N |
| `{:^N}`   | Centre-align in width N |
| `{:fN}`   | Fill character f, width N (e.g., `{:0>8}`) |
| `{:=N}`   | Numeric padding (sign/prefix, then fill, then digits) |
| `{:+}`    | Force sign prefix for integers |
| `{:#N}`   | Show base prefix (`0x`, `0b`, `0`) for `x`/`b`/`o` types |
| `{:.P}`   | Precision P (floating point digits; string truncation) |
| `{:d/x/X/b/B/o}` | Decimal/hex/binary/octal integer type |
| `{:f/e/E/g/G}` | Fixed/scientific/general float type |
| `{{` / `}}` | Escaped `{` / `}` literals |

### Example

```cpp
char buf[128];
fl::sinks::buffer_sink sink(buf, sizeof(buf));

fl::format_to(sink, "Hello, {}! Count: {:>8}", "world", 42);
sink.null_terminate();
// buf == "Hello, world! Count:       42"
```

---

## Allocator utilities

**Header:** `#include <fl/alloc_hooks.hpp>`

### `fl::pool_alloc<T>`

C++ `Allocator`-concept-compliant wrapper around the fl thread-local pool.
Routes allocations through `fl::alloc_hooks::allocate_bytes_aligned`.
Suitable for use with `std::allocate_shared<T>(fl::pool_alloc<T>{}, ...)`.

```cpp
template <typename T>
struct pool_alloc {
    using value_type = T;

    pool_alloc() noexcept = default;
    template <typename U> pool_alloc(const pool_alloc<U>&) noexcept;

    T*   allocate(std::size_t n);         // throws std::bad_alloc on failure
    void deallocate(T* p, std::size_t n) noexcept;

    template <typename U> bool operator==(const pool_alloc<U>&) const noexcept;
    template <typename U> bool operator!=(const pool_alloc<U>&) const noexcept;
};
```

### Pool size helpers

```cpp
// Returns the full usable capacity for a given raw allocation size when the
// allocation is served by the pool (i.e., pool class size minus one for the
// null terminator). Returns raw_size - 1 for non-pooled sizes.
std::size_t fl::alloc_hooks::pool_alloc_usable_capacity(std::size_t raw_size) noexcept;
```

---

## Usage examples

### Basic string operations

```cpp
#include <fl.hpp>

fl::string s = "Hello";
s += ", world!";
assert(s.size() == 13);
assert(s.starts_with("Hello"));
assert(s.find("world") == 7);

fl::substring_view view = s.slice(7, 5);   // "world", zero-copy
fl::string copy = view.to_fl_string();
```

### String builder

```cpp
fl::string_builder sb(128);
for (int i = 0; i < 100; ++i) {
    sb.append_formatted("{}: value\n", i);
}
fl::string result = std::move(sb).build();
```

### Rope for bulk concatenation

```cpp
fl::rope r;
for (const auto& line : lines) {
    r += line;          // O(1) amortised; no intermediate std::string
}
fl::string flat = r.flatten();   // O(n) linearise once
```

### Immutable shared string

```cpp
fl::immutable_string a("shared key");
fl::immutable_string b = a;   // O(1) atomic copy

std::unordered_map<fl::immutable_string_view,
                   int,
                   fl::immutable_string_hash,
                   fl::immutable_string_equal> map;
map[a.view()] = 42;
```

### Formatting to a fixed buffer

```cpp
char buf[64];
fl::sinks::buffer_sink sink(buf, sizeof(buf));
fl::format_to(sink, "x={:>6.2f}", 3.14159);
sink.null_terminate();
```
