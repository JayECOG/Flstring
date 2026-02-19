// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_ROPE_HPP
#define FL_ROPE_HPP

// Rope data structure for efficient string concatenation in the fl library.

#include <cstring>
#include <memory>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <concepts>
#include <compare>
#include <string>
#include <string_view>
#include <vector>
#include "fl/string.hpp"
#include "fl/substring_view.hpp"
#include "fl/profiling.hpp"

namespace fl {

// Forward declarations
class string;
class substring_view;

// ---------------------------------------------------------------------------
// rope_node_alloc -- dedicated slab allocator for rope leaf_node and
// concat_node combined allocations (object + shared_ptr control block).
//
// Motivation: both node types combined with their control blocks fit in <=128 B
// (leaf ~64 B, concat ~80 B).  Using the general pool_alloc<T> would work but
// calls pool_class_index() (7 comparisons) on every AVL rotation.  A dedicated
// two-class slab bypasses that loop entirely and provides 32 slots per class
// instead of the general pool's 8, giving better hit rates during bulk concat
// sequences that create O(log N) concat_nodes per operator+=.
//
// Layout of the thread-local rope_node_slab (TLS, per-thread, zero-init):
//
//   slots64[32]  -- 32 x ptr for <=64-byte allocations (leaf_node + ctrl)
//   slots128[32] -- 32 x ptr for <=128-byte allocations (concat_node + ctrl)
//   count64      -- number of live entries in slots64
//   count128     -- number of live entries in slots128
//
// Total TLS: 32*8 + 32*8 + 4 + 4 = 520 bytes = 9 cache lines.
// ---------------------------------------------------------------------------

namespace detail {
    constexpr int kRopeSlabDepth = 32;

    struct rope_node_slab {
        void* slots64[kRopeSlabDepth];   // leaf_node+ctrl (<=64 B), class 64.
        void* slots128[kRopeSlabDepth];  // concat_node+ctrl (<=128 B), class 128.
        int   count64  = 0;
        int   count128 = 0;
    };

    inline rope_node_slab& get_rope_slab() noexcept {
        static thread_local rope_node_slab slab{};
        return slab;
    }
} // namespace detail

    // ---------------------------------------------------------------------------
    // rope_linear_view -- small wrapper carrying ownership of the linear cache.
    // ---------------------------------------------------------------------------
    class rope_linear_view {
    public:
        using size_type = std::string_view::size_type;

        rope_linear_view() noexcept = default;
        rope_linear_view(std::shared_ptr<const std::string> owner, std::string_view view) noexcept
            : _owner(std::move(owner)), _view(view) {}

        constexpr operator std::string_view() const noexcept { return _view; }

        constexpr std::string_view view() const noexcept { return _view; }
        constexpr const char* data() const noexcept { return _view.data(); }
        constexpr size_type size() const noexcept { return _view.size(); }

    private:
        std::shared_ptr<const std::string> _owner{};
        std::string_view _view{};
    };

// Allocator used for all shared_ptr<leaf_node> / shared_ptr<concat_node>
// allocations inside fl::rope.  Never used outside rope internals.
//
// Implemented as a class template so that std::allocate_shared's internal
// rebind to _Sp_counted_ptr_inplace<T,...> produces an allocator whose
// value_type (and therefore pointer type) matches T -- required by libstdc++/
// libc++.  The byte-level dispatch (<=64 B -> slab64, <=128 B -> slab128) is
// computed from n * sizeof(T) so the same slab thresholds apply regardless of
// which concrete T the allocator is rebound to.
template <typename T = char>
struct basic_rope_node_alloc {
    using value_type = T;

    basic_rope_node_alloc() noexcept = default;
    template <typename U> basic_rope_node_alloc(const basic_rope_node_alloc<U>&) noexcept {}
    template <typename U> struct rebind { using other = basic_rope_node_alloc<U>; };

    T* allocate(std::size_t n) {
        const std::size_t bytes = n * sizeof(T);
        auto& slab = detail::get_rope_slab();
        if (__builtin_expect(bytes <= 64u, 0)) {
            if (slab.count64 > 0)
                return static_cast<T*>(slab.slots64[--slab.count64]);
            // Cold miss: allocate exactly class-64 bytes.
            void* p = fl::alloc_hooks::allocate_bytes_aligned(
                64, fl::alloc_hooks::DEFAULT_ALIGNMENT);
            if (!p) throw std::bad_alloc{};
            return static_cast<T*>(p);
        }
        if (__builtin_expect(bytes <= 128u, 1)) {
            if (slab.count128 > 0)
                return static_cast<T*>(slab.slots128[--slab.count128]);
            // Cold miss: allocate exactly class-128 bytes.
            void* p = fl::alloc_hooks::allocate_bytes_aligned(
                128, fl::alloc_hooks::DEFAULT_ALIGNMENT);
            if (!p) throw std::bad_alloc{};
            return static_cast<T*>(p);
        }
        // Oversized (unexpected): fall back to general pool.
        void* p = fl::alloc_hooks::allocate_bytes_aligned(
            bytes, fl::alloc_hooks::DEFAULT_ALIGNMENT);
        if (!p) throw std::bad_alloc{};
        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        const std::size_t bytes = n * sizeof(T);
        auto& slab = detail::get_rope_slab();
        if (__builtin_expect(bytes <= 64u, 0)) {
            if (slab.count64 < detail::kRopeSlabDepth) {
                slab.slots64[slab.count64++] = p;
                return;
            }
            // Slab full: release at class-64 size (we always allocate 64 B here).
            fl::alloc_hooks::deallocate_bytes_aligned(
                p, 64, fl::alloc_hooks::DEFAULT_ALIGNMENT);
            return;
        }
        if (__builtin_expect(bytes <= 128u, 1)) {
            if (slab.count128 < detail::kRopeSlabDepth) {
                slab.slots128[slab.count128++] = p;
                return;
            }
            // Slab full: release at class-128 size.
            fl::alloc_hooks::deallocate_bytes_aligned(
                p, 128, fl::alloc_hooks::DEFAULT_ALIGNMENT);
            return;
        }
        fl::alloc_hooks::deallocate_bytes_aligned(
            p, bytes, fl::alloc_hooks::DEFAULT_ALIGNMENT);
    }

    template <typename U>
    bool operator==(const basic_rope_node_alloc<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const basic_rope_node_alloc<U>&) const noexcept { return false; }
};

// Convenience alias used at all allocate_shared call sites.
using rope_node_alloc = basic_rope_node_alloc<>;

// Rope/concat-tree data structure for efficient string concatenation.
//
// A rope is a tree-based string representation that enables amortised O(1)
// concatenation, linear in time, through tree composition. Instead of copying
// string data on every concatenation, ropes create internal nodes linking
// existing strings, deferring expensive linearisation (flattening) operations
// until iteration or string conversion is required.
//
// Performance characteristics:
// - Concatenation: O(1) amortised, constant-time tree node creation.
// - Flattening: O(n) where n is total string length.
// - Character access: O(log n) via tree traversal.
// - Substring extraction: O(n) for extracted range.
//
// Trade-offs:
// - Faster concatenation than linear copying (O(1) vs O(n)).
// - Slower character access than linear strings (O(log n) vs O(1)).
// - Higher memory overhead for small strings.
// - Optimal for concatenation-heavy, iteration-light workloads.
//
// Example usage:
//   fl::rope r1("hello");
//   fl::rope r2(" world");
//   fl::rope combined = r1 + r2;  // O(1) concatenation.
//   fl::string result = combined.flatten();  // O(n) linearisation.
class rope {
private:
    struct node;
    using node_ptr = std::shared_ptr<node>;
    [[nodiscard]] static node_ptr _make_leaf(std::string_view data) noexcept;
    static constexpr std::size_t kLeafMergeMax = 8192;
    static constexpr std::size_t kLeafAppendMax = 16384;
    // Depth at which rebalance() flattens the tree.  _balanced_concat maintains
    // an AVL invariant (depth <= 2*log2(N)) so a rope of N=5000 nodes has depth
    // ~13 without ever needing an explicit rebalance.  The old threshold of 8
    // caused O(n) flattening on every concat sequence longer than ~256 pieces.
    // Raising to 64 means rebalance() is a no-op for trees up to 2^32 nodes
    // built through operator+= (the AVL property keeps depth well below 64).
    // Callers that explicitly need a contiguous leaf (e.g. before passing to a
    // C API) should call flatten() directly rather than rebalance().
    static constexpr std::size_t kRebalanceDepthThreshold = 64;

    // Base node in the rope tree structure.
    struct node {
        virtual ~node() = default;
        [[nodiscard]] virtual std::size_t length() const noexcept = 0;
        virtual void copy_to(std::span<char> dest) const noexcept = 0;
        virtual void copy_range_to(std::span<char> dest, std::size_t offset, std::size_t len) const noexcept = 0;
        [[nodiscard]] virtual char at(std::size_t pos) const noexcept = 0;
        [[nodiscard]] virtual std::shared_ptr<node> clone() const = 0;
        [[nodiscard]] virtual std::size_t depth() const noexcept = 0;
        [[nodiscard]] virtual bool is_leaf() const noexcept { return false; }
        [[nodiscard]] virtual bool is_concat() const noexcept { return false; }
        virtual bool try_append(const char* data, std::size_t len) = 0;
    };

    // Leaf node containing actual string data.
    struct leaf_node : node {
        fl::string storage;
        leaf_node(const char* str, std::size_t len)
            : storage(str, len) {}
        explicit leaf_node(std::string_view view)
            : storage(view) {}
        explicit leaf_node(fl::string&& s)
            : storage(std::move(s)) {}
        ~leaf_node() noexcept override = default;
        [[nodiscard]] std::size_t length() const noexcept override { return storage.size(); }
        void copy_to(std::span<char> dest) const noexcept override {
            assert(dest.size() >= storage.size());
            if (!storage.empty()) std::memcpy(dest.data(), storage.data(), storage.size());
        }
        void copy_range_to(std::span<char> dest, std::size_t offset, std::size_t len) const noexcept override {
            assert(offset + len <= storage.size());
            assert(dest.size() >= len);
            if (len > 0) std::memcpy(dest.data(), storage.data() + offset, len);
        }
        [[nodiscard]] char at(std::size_t pos) const noexcept override {
            assert(pos < storage.size());
            return storage[pos];
        }
        [[nodiscard]] std::size_t depth() const noexcept override { return 1; }
        [[nodiscard]] bool is_leaf() const noexcept override { return true; }
        [[nodiscard]] std::shared_ptr<node> clone() const override {
            return std::allocate_shared<leaf_node>(rope_node_alloc{}, std::string_view(storage));
        }
        bool try_append(const char* data, std::size_t len) override {
            // Threshold for leaf size to balance memory vs tree depth.
            if (storage.size() + len <= kLeafAppendMax) {
                if (storage.capacity() < storage.size() + len) {
                    storage.reserve(storage.size() + len);
                }
                storage.append(data, len);
                return true;
            }
            return false;
        }
    };

    // Concatenation node linking two subtrees.
    struct concat_node : node {
        std::shared_ptr<node> left;
        std::shared_ptr<node> right;
        std::size_t total_length;
        std::size_t depth_val;
        concat_node(std::shared_ptr<node> l, std::shared_ptr<node> r)
            : left(std::move(l)), right(std::move(r)),
              total_length(left->length() + right->length()),
              depth_val(1 + std::max(left->depth(), right->depth())) {}
        ~concat_node() noexcept override = default;
        [[nodiscard]] std::size_t length() const noexcept override { return total_length; }
        void copy_to(std::span<char> dest) const noexcept override {
            assert(dest.size() >= total_length);
            // Iterative implementation to avoid stack overflow for deep trees.
            struct stack_item {
                const node* n;
                std::size_t offset;
            };
            std::vector<stack_item> stack;
            stack.reserve(depth_val);
            stack.push_back({this, 0});

            while (!stack.empty()) {
                auto item = stack.back();
                stack.pop_back();

                if (item.n->is_leaf()) {
                    item.n->copy_to(dest.subspan(item.offset, item.n->length()));
                } else {
                    const auto* c = static_cast<const concat_node*>(item.n);
                    stack.push_back({c->right.get(), item.offset + c->left->length()});
                    stack.push_back({c->left.get(), item.offset});
                }
            }
        }
        void copy_range_to(std::span<char> dest, std::size_t offset, std::size_t len) const noexcept override {
            if (len == 0) return;
            assert(offset + len <= total_length);
            assert(dest.size() >= len);

            std::size_t left_len = left->length();
            if (offset < left_len) {
                std::size_t left_to_copy = std::min(len, left_len - offset);
                left->copy_range_to(dest, offset, left_to_copy);
                if (len > left_to_copy) {
                    right->copy_range_to(dest.subspan(left_to_copy), 0, len - left_to_copy);
                }
            } else {
                right->copy_range_to(dest, offset - left_len, len);
            }
        }
        [[nodiscard]] char at(std::size_t pos) const noexcept override {
            assert(pos < total_length);
            const node* curr = this;
            std::size_t p = pos;
            while (!curr->is_leaf()) {
                const auto* c = static_cast<const concat_node*>(curr);
                const std::size_t left_len = c->left->length();
                if (p < left_len) {
                    curr = c->left.get();
                } else {
                    curr = c->right.get();
                    p -= left_len;
                }
            }
            const auto* leaf = static_cast<const leaf_node*>(curr);
            return leaf->storage[p];
        }
        [[nodiscard]] std::size_t depth() const noexcept override {
            return depth_val;
        }
        [[nodiscard]] std::shared_ptr<node> clone() const override {
            return std::allocate_shared<concat_node>(rope_node_alloc{}, left->clone(), right->clone());
        }
        bool try_append(const char* data, std::size_t len) override {
            if (right && right.use_count() == 1 && right->try_append(data, len)) {
                total_length += len;
                return true;
            }
            return false;
        }
        [[nodiscard]] bool is_concat() const noexcept override { return true; }
    };

    static node_ptr _balanced_concat(node_ptr l, node_ptr r) {
        if (!l) return r;
        if (!r) return l;

        // Deep leaf merging: try to merge small leaves at the boundary.
        if (l->is_leaf() && r->is_leaf()) {
            std::size_t total_len = l->length() + r->length();
            if (total_len <= kLeafMergeMax) {
                fl::string s = static_cast<const leaf_node*>(l.get())->storage;
                s.append(static_cast<const leaf_node*>(r.get())->storage);
                return std::allocate_shared<leaf_node>(rope_node_alloc{}, std::move(s));
            }
        } else if (l->is_concat() && r->is_leaf()) {
            auto* cl = static_cast<const concat_node*>(l.get());
            if (cl->right->is_leaf()) {
                std::size_t combined = cl->right->length() + r->length();
                if (combined <= kLeafMergeMax) {
                    return _balanced_concat(cl->left, _balanced_concat(cl->right, r));
                }
            }
        } else if (l->is_leaf() && r->is_concat()) {
            auto* cr = static_cast<const concat_node*>(r.get());
            if (cr->left->is_leaf()) {
                std::size_t combined = l->length() + cr->left->length();
                if (combined <= kLeafMergeMax) {
                    return _balanced_concat(_balanced_concat(l, cr->left), cr->right);
                }
            }
        }

        // AVL-style rebalancing (slack of 1).
        std::size_t hl = l->depth();
        std::size_t hr = r->depth();

        if (hl > hr + 1) {
            auto cl = static_cast<const concat_node*>(l.get());
            if (cl->left->depth() >= cl->right->depth()) {
                // Right rotation.
                return std::allocate_shared<concat_node>(rope_node_alloc{}, cl->left, _balanced_concat(cl->right, r));
            } else {
                // Left-right rotation.
                auto clr = static_cast<const concat_node*>(cl->right.get());
                return std::allocate_shared<concat_node>(rope_node_alloc{},
                    _balanced_concat(cl->left, clr->left),
                    _balanced_concat(clr->right, r)
                );
            }
        } else if (hr > hl + 1) {
            auto cr = static_cast<const concat_node*>(r.get());
            if (cr->right->depth() >= cr->left->depth()) {
                // Left rotation.
                return std::allocate_shared<concat_node>(rope_node_alloc{}, _balanced_concat(l, cr->left), cr->right);
            } else {
                // Right-left rotation.
                auto crl = static_cast<const concat_node*>(cr->left.get());
                return std::allocate_shared<concat_node>(rope_node_alloc{},
                    _balanced_concat(l, crl->left),
                    _balanced_concat(crl->right, cr->right)
                );
            }
        }

        return std::allocate_shared<concat_node>(rope_node_alloc{}, std::move(l), std::move(r));
    }

public:
    using value_type = char;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using const_reference = const char;
    using iterator = const char*;
    using const_iterator = const char*;

    // ========== Constructors ==========

    rope() noexcept : _root(nullptr) {}

    rope(const char* cstr) noexcept
        : rope(cstr ? std::string_view(cstr) : std::string_view()) {}

    explicit rope(const char* data, size_type len)
        : rope((data && len) ? std::string_view(data, len) : std::string_view()) {}

    explicit rope(std::string_view view)
        : _root(_make_leaf(view)) {
    }

    rope(const string& str) noexcept;
    rope(const substring_view& view) noexcept;

    rope(const rope& other) noexcept = default;
    rope(rope&& other) noexcept = default;
    rope& operator=(const rope& other) noexcept = default;
    rope& operator=(rope&& other) noexcept = default;
    ~rope() noexcept = default;

    // ========== Capacity ==========

    size_type length() const noexcept {
        return _root ? _root->length() : 0;
    }

    size_type size() const noexcept {
        return length();
    }

    bool empty() const noexcept {
        return !_root || _root->length() == 0;
    }

    // ========== Element Access ==========

    // O(log n) tree traversal. Uses the linear cache or access index when
    // available on ropes larger than 4096 bytes for amortised O(1) lookups.
    char operator[](size_type pos) const noexcept {
        assert(_root && "operator[] called on empty rope");
        const size_type total_len = _root->length();
        assert(pos < total_len && "operator[] index out of bounds");
        if (_linear_cache && _linear_cache->size() == total_len) {
            return (*_linear_cache)[pos];
        }
        if (_root->is_leaf()) {
            return static_cast<const leaf_node*>(_root.get())->storage[pos];
        }
        if (total_len >= 4096) {
            return _at_via_access_index(pos);
        }
        return _root->at(pos);
    }

    // Like operator[], but throws std::out_of_range on invalid position.
    char at(size_type pos) const {
        if (!_root) {
            throw std::out_of_range("rope::at: position out of range");
        }
        const size_type total_len = _root->length();
        if (pos >= total_len) {
            throw std::out_of_range("rope::at: position out of range");
        }
        if (_linear_cache && _linear_cache->size() == total_len) {
            return (*_linear_cache)[pos];
        }
        if (_root->is_leaf()) {
            return static_cast<const leaf_node*>(_root.get())->storage[pos];
        }
        if (total_len >= 4096) {
            return _at_via_access_index(pos);
        }
        return _root->at(pos);
    }

    char front() const noexcept {
        assert(_root && !empty());
        return _root->at(0);
    }

    char back() const noexcept {
        assert(_root && !empty());
        return _root->at(_root->length() - 1);
    }

    // ========== Concatenation Operations ==========

    // O(1) amortised via balanced tree concatenation.
    [[nodiscard]] rope operator+(const rope& other) const {
        if (empty()) return other;
        if (other.empty()) return *this;
        return rope(_balanced_concat(_root, other._root));
    }

    rope operator+(const char* cstr) const {
        if (!cstr || !*cstr) return *this;
        std::size_t len = std::strlen(cstr);
        if (_root && _root->is_leaf() && _root->length() + len <= 2048) {
            fl::string s = static_cast<const leaf_node*>(_root.get())->storage;
            s.append(cstr, len);
            return rope(std::move(s));
        }
        return *this + rope(cstr, len);
    }

    rope operator+(const string& str) const {
        return *this + rope(str);
    }

    // O(1) amortised. Attempts in-place append when the root has exclusive
    // ownership; otherwise creates a new balanced concatenation node.
    rope& operator+=(const rope& other) {
        if (other.empty()) return *this;
        if (empty()) {
            *this = other;
            return *this;
        }
        if (_root && _root.use_count() == 1 && other._root && other._root->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(other._root.get());
            if (_root->try_append(leaf->storage.data(), leaf->storage.size())) {
                _invalidate_linear_cache();
                return *this;
            }
        }
        _root = _balanced_concat(_root, other._root);
        _invalidate_linear_cache();
        return *this;
    }

    rope& operator+=(const char* cstr) {
        if (!cstr || !*cstr) return *this;
        return *this += std::string_view(cstr);
    }

    // Avoids an intermediary fl::string allocation. When the root leaf has
    // exclusive ownership and capacity, try_append extends it in-place (O(1));
    // otherwise a new leaf is created and balanced-concatenated.
    rope& operator+=(std::string_view sv) {
        if (sv.empty()) return *this;
        if (empty()) {
            _root = _make_leaf(sv);
            return *this;
        }
        if (_root && _root.use_count() == 1 && _root->try_append(sv.data(), sv.size())) {
            _invalidate_linear_cache();
            return *this;
        }
        _root = _balanced_concat(_root, _make_leaf(sv));
        _invalidate_linear_cache();
        return *this;
    }

    rope& operator+=(const string& str) {
        return *this += std::string_view(str.data(), str.size());
    }

    // Eliminates ambiguity with the string_view overload.
    rope& operator+=(const std::string& str) {
        return *this += std::string_view(str.data(), str.size());
    }

    // ========== Comparison ==========

    [[nodiscard]] std::strong_ordering operator<=>(const rope& other) const noexcept {
        if (_root == other._root) return std::strong_ordering::equal;

        if (!_root) return other._root ? std::strong_ordering::less : std::strong_ordering::equal;
        if (!other._root) return std::strong_ordering::greater;

        if (_root->is_leaf() && other._root->is_leaf()) {
            const auto& lhs_leaf = static_cast<const leaf_node*>(_root.get())->storage;
            const auto& rhs_leaf = static_cast<const leaf_node*>(other._root.get())->storage;
            return lhs_leaf <=> rhs_leaf;
        }

        // Use cached linear version if available for both.
        if (_linear_cache && other._linear_cache && !_linear_cache->empty() && !other._linear_cache->empty()) {
            return (*_linear_cache) <=> (*other._linear_cache);
        }

        const auto& lhs = _ensure_linear_cache();
        const auto& rhs = other._ensure_linear_cache();
        return lhs <=> rhs;
    }

    [[nodiscard]] bool operator==(const rope& other) const noexcept {
        if (length() != other.length()) return false;
        if (_root == other._root) return true;
        if (!_root || !other._root) return false;

        if (_root->is_leaf() && other._root->is_leaf()) {
            const auto& lhs_leaf = static_cast<const leaf_node*>(_root.get())->storage;
            const auto& rhs_leaf = static_cast<const leaf_node*>(other._root.get())->storage;
            return std::memcmp(lhs_leaf.data(), rhs_leaf.data(), lhs_leaf.size()) == 0;
        }

        const auto& lhs = _ensure_linear_cache();
        const auto& rhs = other._ensure_linear_cache();
        return std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
    }

    // ========== Conversion and Linearisation ==========

    // Linearises the rope tree into a contiguous fl::string. O(n).
    string flatten() const;

    // Linearises the rope tree into a contiguous std::string. O(n).
    std::string to_std_string() const {
        auto result = _linearize_to_std_string();
        return result;
    }

    // O(n) for the extracted range.
    substring_view substr(size_type offset = 0,
                         size_type len = std::string::npos) const;

    // ========== Iteration (requires linearisation) ==========

    // Returns an iterator to the beginning. Triggers O(n) linearisation on
    // first call; subsequent calls reuse the cached linear representation.
    std::string::const_iterator begin() const {
        return _ensure_linear_cache().cbegin();
    }

    std::string::const_iterator end() const {
        return _ensure_linear_cache().cend();
    }

    // Returns a view over the linearised rope. O(n) to build on first call,
    // then O(1) for subsequent access. The returned view owns the cache so
    // the caller can continue using the data even if the rope mutates later.
    rope_linear_view linear_view() const {
        const auto& cache = _ensure_linear_cache();
        return rope_linear_view(_linear_cache, std::string_view(cache));
    }

    // ========== Rebalancing ==========

    // Flattens and rebuilds the rope tree as a single leaf node when depth
    // exceeds kRebalanceDepthThreshold. This is O(n) and is important for
    // ropes built through iterative concatenation to prevent deep nesting.
    void rebalance() noexcept {
        rebalance(kRebalanceDepthThreshold);
    }

    // Rebalances only when tree depth exceeds depth_threshold. Use a low
    // threshold (e.g. 8) to aggressively flatten after bulk concat. Use a high
    // threshold (e.g. kRebalanceDepthThreshold) to skip rebalancing for trees
    // that are already balanced by _balanced_concat. Callers that need a
    // guaranteed contiguous buffer should call flatten() directly.
    //
    // Complexity: O(n) when depth > depth_threshold, O(1) otherwise.
    void rebalance(size_type depth_threshold) noexcept {
        if (empty()) return;
        if (_root->is_leaf()) return;
        if (_root->depth() <= depth_threshold) return;
        auto linearised = to_std_string();
        _root = std::allocate_shared<leaf_node>(rope_node_alloc{}, linearised.c_str(), linearised.length());
        _invalidate_linear_cache();
    }

    // Flattens the rope to a single contiguous leaf if and only if the tree
    // depth exceeds depth_threshold. Unlike rebalance(), the invariant
    // "depth == 1 after this call" holds whenever the method returns true.
    //
    // Intended for callers that need a guaranteed contiguous const char* before
    // passing rope data to a C API, but only want to pay the O(n) cost when
    // the tree is actually deep (e.g. flatten_if_deep(32) before a
    // write(fd, r.data(), r.size()) call without touching shallow ropes).
    //
    // Returns true if the tree was flattened, false if it was already shallow
    // enough (depth <= depth_threshold) or was already a single leaf.
    //
    // Complexity: O(n) when depth > depth_threshold, O(1) otherwise.
    bool flatten_if_deep(size_type depth_threshold) noexcept {
        if (empty()) return false;
        if (_root->is_leaf()) return false;
        if (_root->depth() <= depth_threshold) return false;
        auto linearised = to_std_string();
        _root = std::allocate_shared<leaf_node>(rope_node_alloc{}, linearised.c_str(), linearised.length());
        _invalidate_linear_cache();
        return true;
    }

    // Returns the current depth of the rope tree, for diagnostics.
    std::size_t depth() const noexcept {
        return _root ? _root->depth() : 0;
    }

private:
    struct access_chunk {
        std::shared_ptr<const node> owner;  // Keep the leaf alive while indexed.
        const char* data;
        size_type start;
        size_type len;
    };

    struct access_sample {
        size_type start;
        size_type chunk_index;
    };

    node_ptr _root;  // Root of the rope tree.
    mutable std::shared_ptr<std::string> _linear_cache;  // Cached linearised data for iterator lifetime.
    mutable std::vector<access_chunk> _access_index;
    mutable std::vector<access_sample> _access_samples;
    mutable size_type _access_index_total_len = 0;

    explicit rope(node_ptr root) noexcept;
    [[nodiscard]] std::string _linearize_to_std_string() const;
    [[nodiscard]] const std::string& _ensure_linear_cache() const;
    void _invalidate_linear_cache() const noexcept;
    void _build_access_index() const;
    [[nodiscard]] char _at_via_access_index(size_type pos) const noexcept;

    friend class string;
    friend class substring_view;
};

inline rope::node_ptr rope::_make_leaf(std::string_view data) noexcept {
    return data.empty() ? nullptr : std::allocate_shared<rope::leaf_node>(rope_node_alloc{}, data);
}

inline rope::rope(const string& str) noexcept
    : rope(std::string_view(str.data(), str.size())) {}

inline rope::rope(const substring_view& view) noexcept
    : rope(view.data(), view.size()) {}

inline rope::rope(node_ptr root) noexcept : _root(std::move(root)) {}

inline fl::string rope::flatten() const {
    if (empty()) return fl::string();
    if (_root && _root->is_leaf()) {
        return static_cast<const leaf_node*>(_root.get())->storage;
    }
    fl::string result(length(), '\0');
    if (_root) {
        std::span<char> span(result.data(), result.size());
        _root->copy_to(span);
    }
    return result;
}

inline fl::substring_view rope::substr(size_type offset, size_type len) const {
    if (offset >= length()) return fl::substring_view();
    size_type rlen = std::min(len, length() - offset);
    if (rlen == 0) return fl::substring_view();

    if (_root && _root->is_leaf()) {
        const auto* leaf = static_cast<const leaf_node*>(_root.get());
        return fl::substring_view(
            leaf->storage.data() + offset,
            rlen,
            std::static_pointer_cast<const void>(_root)
        );
    }

    // Create an owning fl::string to hold only the required range.
    auto owner = std::allocate_shared<fl::string>(fl::pool_alloc<fl::string>{}, rlen, '\0');
    if (_root) {
        std::span<char> span(owner->data(), owner->size());
        _root->copy_range_to(span, offset, rlen);
    }

    // Create substring_view with shared ownership.
    return fl::substring_view(owner->data(), rlen, std::static_pointer_cast<const void>(owner));
}

inline std::string rope::_linearize_to_std_string() const {
    if (empty()) {
        return std::string();
    }
    if (_root && _root->is_leaf()) {
        const auto& storage = static_cast<const leaf_node*>(_root.get())->storage;
        return std::string(storage.data(), storage.size());
    }
    std::string result(length(), '\0');
    if (_root) {
        std::span<char> span(result.data(), result.size());
        _root->copy_to(span);
    }
    return result;
}

inline const std::string& rope::_ensure_linear_cache() const {
    const size_type current_length = length();
    if (current_length == 0) {
        if (!_linear_cache) {
            _linear_cache = std::make_shared<std::string>();
        } else {
            _linear_cache->clear();
        }
        return *_linear_cache;
    }
    if (!_linear_cache || _linear_cache->size() != current_length) {
        _linear_cache = std::make_shared<std::string>(_linearize_to_std_string());
    }
    return *_linear_cache;
}

inline void rope::_build_access_index() const {
    const size_type total = length();
    if (total == 0) {
        _access_index.clear();
        _access_index_total_len = 0;
        return;
    }
    if (_access_index_total_len == total && !_access_index.empty()) {
        return;
    }

    _access_index.clear();
    _access_samples.clear();
    _access_index.reserve(_root ? _root->depth() * 2 : 0);

    struct item { const node* n; size_type start; };
    std::vector<item> stack;
    stack.reserve(_root ? _root->depth() * 2 : 0);
    stack.push_back({_root.get(), 0});

    while (!stack.empty()) {
        item curr = stack.back();
        stack.pop_back();

        if (curr.n->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(curr.n);
            const size_type len = leaf->storage.size();
            if (len > 0) {
                _access_index.push_back({
                    std::shared_ptr<const node>(_root, curr.n),
                    leaf->storage.data(),
                    curr.start,
                    len
                });
            }
        } else {
            const auto* concat = static_cast<const concat_node*>(curr.n);
            const size_type left_len = concat->left->length();
            stack.push_back({concat->right.get(), curr.start + left_len});
            stack.push_back({concat->left.get(), curr.start});
        }
    }

    if (_access_index.size() >= 128) {
        constexpr size_type sample_stride = 16;
        _access_samples.reserve((_access_index.size() + sample_stride - 1) / sample_stride);
        for (size_type i = 0; i < _access_index.size(); i += sample_stride) {
            _access_samples.push_back({_access_index[i].start, i});
        }
    }

    _access_index_total_len = total;
}

inline char rope::_at_via_access_index(size_type pos) const noexcept {
    _build_access_index();
    if (_access_index.empty()) {
        return '\0';
    }

    size_type lo = 0;
    size_type hi = _access_index.size();

    if (!_access_samples.empty()) {
        size_type slo = 0;
        size_type shi = _access_samples.size();
        while (slo < shi) {
            const size_type mid = slo + ((shi - slo) / 2);
            if (_access_samples[mid].start <= pos) {
                slo = mid + 1;
            } else {
                shi = mid;
            }
        }
        if (slo > 0) {
            const size_type base = _access_samples[slo - 1].chunk_index;
            lo = base;
            hi = std::min(_access_index.size(), base + 32);
        }
    }

    while (lo < hi) {
        const size_type mid = lo + ((hi - lo) / 2);
        const auto& chunk = _access_index[mid];
        if (pos < chunk.start) {
            hi = mid;
        } else if (pos >= chunk.start + chunk.len) {
            lo = mid + 1;
        } else {
            return chunk.data[pos - chunk.start];
        }
    }

    return _root->at(pos);
}

inline void rope::_invalidate_linear_cache() const noexcept {
    _linear_cache.reset();
    _access_index.clear();
    _access_samples.clear();
    _access_index_total_len = 0;
}

// ============================================================================
// Stream output operator for rope.
// ============================================================================

// O(n) due to linearisation.
inline std::ostream& operator<<(std::ostream& os, const rope& r) {
    return os << r.to_std_string();
}

}  // namespace fl

#endif  // FL_ROPE_HPP
