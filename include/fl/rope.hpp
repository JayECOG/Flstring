// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_ROPE_HPP
#define FL_ROPE_HPP

/// @file Rope data structure for efficient string concatenation.
///
/// An AVL-balanced binary concatenation tree providing amortised O(1)
/// concatenation by composing tree nodes rather than copying data.

#include <cstring>
#include <memory>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <mutex>
#include "fl/config.hpp"
#include <span>
#include <compare>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fl/string.hpp"
#include "fl/substring_view.hpp"
#include "fl/profiling.hpp"

namespace fl {

/// Alias for std::span (C++20 baseline).
template <typename T>
using span = std::span<T>;

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
        if (FL_UNLIKELY(bytes <= 64u)) {
            if (slab.count64 > 0)
                return static_cast<T*>(slab.slots64[--slab.count64]);
            // Cold miss: allocate exactly class-64 bytes.
            void* p = fl::alloc_hooks::allocate_bytes_aligned(
                64, fl::alloc_hooks::DEFAULT_ALIGNMENT);
            if (!p) throw std::bad_alloc{};
            return static_cast<T*>(p);
        }
        if (FL_LIKELY(bytes <= 128u)) {
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
        if (FL_UNLIKELY(bytes <= 64u)) {
            if (slab.count64 < detail::kRopeSlabDepth) {
                slab.slots64[slab.count64++] = p;
                return;
            }
            // Slab full: release at class-64 size (we always allocate 64 B here).
            fl::alloc_hooks::deallocate_bytes_aligned(
                p, 64, fl::alloc_hooks::DEFAULT_ALIGNMENT);
            return;
        }
        if (FL_LIKELY(bytes <= 128u)) {
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

/// Convenience alias used at all allocate_shared call sites.
using rope_node_alloc = basic_rope_node_alloc<>;

/** Rope (concat-tree) data structure for efficient string concatenation.
 *
 * A rope is a tree-based string representation that enables amortised O(1)
 * concatenation through tree composition.  Instead of copying string data on
 * every concatenation, ropes create internal nodes linking existing strings,
 * deferring expensive flattening operations until iteration or string
 * conversion is required.
 *
 * Performance characteristics:
 *   - Concatenation: O(1) amortised, constant-time tree node creation.
 *   - Flattening: O(n) where n is total string length.
 *   - Character access: O(log n) via tree traversal.
 *   - Substring extraction: O(n) for extracted range.
 *
 * Trade-offs:
 *   - Faster concatenation than linear copying (O(1) vs O(n)).
 *   - Slower character access than linear strings (O(log n) vs O(1)).
 *   - Higher memory overhead for small strings.
 *   - Optimal for concatenation-heavy, iteration-light workloads.
 *
 * Example usage:
 * @code
 *   fl::rope r1("hello");
 *   fl::rope r2(" world");
 *   fl::rope combined = r1 + r2;      // O(1) concatenation.
 *   fl::string result = combined.flatten();  // O(n) linearisation.
 * @endcode
 */
class rope {
private:
    struct node;
    using node_ptr = std::shared_ptr<node>;
    [[nodiscard]] static node_ptr _make_leaf(std::string_view data);
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

    // Node type discriminator — replaces virtual is_leaf()/is_concat() calls
    // with a branch on an integer field, eliminating virtual dispatch overhead
    // on hot paths (operator[], at(), copy_to, _balanced_concat, etc.).
    enum class node_type : uint8_t { leaf, concat };

    // Base node in the rope tree structure.
    struct node {
        const node_type type;  // Immutable after construction; set by derived ctors.

        virtual ~node() = default;
        [[nodiscard]] virtual std::size_t length() const noexcept = 0;
        virtual void copy_to(span<char> dest) const noexcept = 0;
        virtual void copy_range_to(span<char> dest, std::size_t offset, std::size_t len) const noexcept = 0;
        [[nodiscard]] virtual char at(std::size_t pos) const noexcept = 0;
        [[nodiscard]] virtual std::shared_ptr<node> clone() const = 0;
        [[nodiscard]] virtual std::size_t depth() const noexcept = 0;
        [[nodiscard]] virtual bool is_leaf() const noexcept { return type == node_type::leaf; }
        [[nodiscard]] virtual bool is_concat() const noexcept { return type == node_type::concat; }
        virtual bool try_append(const char* data, std::size_t len) = 0;

    protected:
        explicit node(node_type t) noexcept : type(t) {}
    };

    // Leaf node containing actual string data.
    struct leaf_node : node {
        fl::string storage;
        leaf_node(const char* str, std::size_t len)
            : node(node_type::leaf), storage(str, len) {}
        explicit leaf_node(std::string_view view)
            : node(node_type::leaf), storage(view) {}
        explicit leaf_node(fl::string&& s)
            : node(node_type::leaf), storage(std::move(s)) {}
        ~leaf_node() noexcept override = default;
        [[nodiscard]] std::size_t length() const noexcept override { return storage.size(); }
        FL_INLINE void copy_to(span<char> dest) const noexcept override {
            assert(dest.size() >= storage.size());
            if (FL_LIKELY(!storage.empty())) std::memcpy(dest.data(), storage.data(), storage.size());
        }
        FL_INLINE void copy_range_to(span<char> dest, std::size_t offset, std::size_t len) const noexcept override {
            assert(offset + len <= storage.size());
            assert(dest.size() >= len);
            if (FL_LIKELY(len > 0)) std::memcpy(dest.data(), storage.data() + offset, len);
        }
        [[nodiscard]] FL_INLINE char at(std::size_t pos) const noexcept override {
            assert(pos < storage.size());
            return storage[pos];
        }
        [[nodiscard]] std::size_t depth() const noexcept override { return 1; }
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
            : node(node_type::concat), left(std::move(l)), right(std::move(r)),
              total_length(left->length() + right->length()),
              depth_val(1 + std::max(left->depth(), right->depth())) {}
        ~concat_node() noexcept override = default;
        [[nodiscard]] std::size_t length() const noexcept override { return total_length; }
        // Iterative tree walk: replaces recursive virtual dispatch with an
        // explicit stack, eliminating both recursion overhead (function calls,
        // virtual dispatch) and stack-overflow risk on deep trees.
        //
        // Uses a fixed-size local array as the stack.  AVL-balanced trees
        // have depth O(log N) ≈ 1.44*log2(N), so for ropes of ≤ 2^31 nodes
        // the depth is at most ~45 frames — well within a 64-entry buffer.
        FL_INLINE void copy_to(span<char> dest) const noexcept override {
            assert(dest.size() >= total_length);
            struct frame { const node* n; char* write_pos; };
            frame stack[64];
            std::size_t sp = 0;
            stack[sp++] = {this, dest.data()};
            while (FL_LIKELY(sp > 0)) {
                const frame f = stack[--sp];
                const node* curr = f.n;
                char* pos = f.write_pos;
                if (FL_LIKELY(curr->type == node_type::leaf)) {
                    const auto* leaf = static_cast<const leaf_node*>(curr);
                    const std::size_t len = leaf->storage.size();
                    if (FL_LIKELY(len > 0)) {
                        std::memcpy(pos, leaf->storage.data(), len);
                    }
                } else {
                    const auto* concat = static_cast<const concat_node*>(curr);
                    const std::size_t left_len = concat->left->length();
                    // Push right first (LIFO → left processed before right).
                    stack[sp++] = {concat->right.get(), pos + left_len};
                    stack[sp++] = {concat->left.get(), pos};
                }
            }
        }
        void copy_range_to(span<char> dest, std::size_t offset, std::size_t len) const noexcept override {
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
            while (curr->type != node_type::leaf) {
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
    };

    inline static node_ptr _balanced_concat(node_ptr l, node_ptr r) {
        if (FL_UNLIKELY(!l)) return r;
        if (FL_UNLIKELY(!r)) return l;

        // Deep leaf merging: try to merge small leaves at the boundary.
        if (l->type == node_type::leaf && r->type == node_type::leaf) {
            std::size_t total_len = l->length() + r->length();
            if (FL_LIKELY(total_len <= kLeafMergeMax)) {
                fl::string s = static_cast<const leaf_node*>(l.get())->storage;
                s.append(static_cast<const leaf_node*>(r.get())->storage);
                // Pre-reserve capacity so subsequent try_append calls avoid
                // repeated reallocations during sequential append patterns.
                if (s.capacity() < kLeafAppendMax && kLeafAppendMax - s.size() > 512) {
                    s.reserve(std::min(s.size() + 2048, kLeafAppendMax));
                }
                return std::allocate_shared<leaf_node>(rope_node_alloc{}, std::move(s));
            }
        } else if (l->type == node_type::concat && r->type == node_type::leaf) {
            auto* cl = static_cast<const concat_node*>(l.get());
            if (cl->right->type == node_type::leaf) {
                std::size_t combined = cl->right->length() + r->length();
                if (FL_LIKELY(combined <= kLeafMergeMax)) {
                    return _balanced_concat(cl->left, _balanced_concat(cl->right, r));
                }
            }
        } else if (l->type == node_type::leaf && r->type == node_type::concat) {
            auto* cr = static_cast<const concat_node*>(r.get());
            if (cr->left->type == node_type::leaf) {
                std::size_t combined = l->length() + cr->left->length();
                if (FL_LIKELY(combined <= kLeafMergeMax)) {
                    return _balanced_concat(_balanced_concat(l, cr->left), cr->right);
                }
            }
        }

        // AVL-style rebalancing (slack of 1).
        // Move children from old nodes to avoid wasted atomic increments
        // — l and r are local; their subtrees are destroyed on return.
        std::size_t hl = l->depth();
        std::size_t hr = r->depth();

        if (FL_UNLIKELY(hl > hr + 1)) {
            auto* cl = static_cast<concat_node*>(l.get());  // non-const for move
            if (cl->left->depth() >= cl->right->depth()) {
                // Right rotation: steal both children from the old concat_node.
                node_ptr cl_left = std::move(cl->left);
                node_ptr cl_right = std::move(cl->right);
                return std::allocate_shared<concat_node>(rope_node_alloc{},
                    std::move(cl_left), _balanced_concat(std::move(cl_right), std::move(r)));
            } else {
                // Left-right rotation.
                auto* clr = static_cast<concat_node*>(cl->right.get());
                node_ptr cl_left = std::move(cl->left);
                node_ptr clr_left = std::move(clr->left);
                node_ptr clr_right = std::move(clr->right);
                return std::allocate_shared<concat_node>(rope_node_alloc{},
                    _balanced_concat(std::move(cl_left), std::move(clr_left)),
                    _balanced_concat(std::move(clr_right), std::move(r)));
            }
        } else if (FL_UNLIKELY(hr > hl + 1)) {
            auto* cr = static_cast<concat_node*>(r.get());  // non-const for move
            if (cr->right->depth() >= cr->left->depth()) {
                // Left rotation.
                node_ptr cr_left = std::move(cr->left);
                node_ptr cr_right = std::move(cr->right);
                return std::allocate_shared<concat_node>(rope_node_alloc{},
                    _balanced_concat(std::move(l), std::move(cr_left)), std::move(cr_right));
            } else {
                // Right-left rotation.
                auto* crl = static_cast<concat_node*>(cr->left.get());
                node_ptr cr_right = std::move(cr->right);
                node_ptr crl_left = std::move(crl->left);
                node_ptr crl_right = std::move(crl->right);
                return std::allocate_shared<concat_node>(rope_node_alloc{},
                    _balanced_concat(std::move(l), std::move(crl_left)),
                    _balanced_concat(std::move(crl_right), std::move(cr_right)));
            }
        }

        return std::allocate_shared<concat_node>(rope_node_alloc{}, std::move(l), std::move(r));
    }

public:
    using value_type = char;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using const_reference = const char;
    // Note: Both iterator and const_iterator are const-only.
    // rope is an immutable data structure at the character level;
    // mutation is done through insert/erase/replace operations.
    using iterator = std::string::const_iterator;
    using const_iterator = std::string::const_iterator;

    static constexpr size_type npos = static_cast<size_type>(-1);

    // ========== Constructors ==========

    rope() noexcept {
        _init_sro(fl::string());
    }

    rope(const char* s) {
        _init_sro(fl::string(s));
    }

    rope(const char* s, size_type n) {
        _init_sro(fl::string(s, n));
    }

    explicit rope(std::string_view sv) {
        _init_sro(fl::string(sv));
    }

    explicit rope(const fl::string& s) {
        _init_sro(fl::string(s));
    }
    explicit rope(fl::string&& s) noexcept {
        _init_sro(std::move(s));
    }

    rope(const substring_view& view);

    rope(const rope& other) {
        if (other._is_sro) {
            _is_sro = true;
            new (&_storage.inline_leaf) fl::string(other._storage.inline_leaf);
        } else {
            _is_sro = false;
            new (&_storage.root) node_ptr(other._storage.root);  // shared_ptr copy
        }
        // Copy cache members if any
        _linear_cache = other._linear_cache;
    }

    rope(rope&& other) noexcept {
        if (other._is_sro) {
            _is_sro = true;
            new (&_storage.inline_leaf) fl::string(std::move(other._storage.inline_leaf));
        } else {
            _is_sro = false;
            new (&_storage.root) node_ptr(std::move(other._storage.root));
            other._storage.root = nullptr;
        }
        _linear_cache = std::move(other._linear_cache);
    }

    rope& operator=(const rope& other) {
        if (this != &other) {
            // Destroy current state
            if (_is_sro) {
                _storage.inline_leaf.~string();
            } else {
                _storage.root.~node_ptr();
            }
            // Copy from other
            if (other._is_sro) {
                _is_sro = true;
                new (&_storage.inline_leaf) fl::string(other._storage.inline_leaf);
            } else {
                _is_sro = false;
                new (&_storage.root) node_ptr(other._storage.root);
            }
            // Only copy cache if content changed.
            // If both are SRO, the inline string IS the cache so _linear_cache
            // can be cleared (it will be repopulated on demand).
            // If both are tree mode, copy the cache.
            if (_is_sro && other._is_sro) {
                _linear_cache.reset();
            } else {
                _linear_cache = other._linear_cache;
            }
        }
        return *this;
    }

    rope& operator=(rope&& other) noexcept {
        if (this != &other) {
            // Destroy current state
            if (_is_sro) {
                _storage.inline_leaf.~string();
            } else {
                _storage.root.~node_ptr();
            }
            // Move from other
            if (other._is_sro) {
                _is_sro = true;
                new (&_storage.inline_leaf) fl::string(std::move(other._storage.inline_leaf));
            } else {
                _is_sro = false;
                new (&_storage.root) node_ptr(std::move(other._storage.root));
                other._storage.root = nullptr;
            }
            _linear_cache = std::move(other._linear_cache);
        }
        return *this;
    }
    ~rope() {
        if (_is_sro) {
            _storage.inline_leaf.~string();
        } else {
            _storage.root.~node_ptr();  // shared_ptr destructor
        }
    }

    // -- Capacity ----------------------------------------------------

    FL_INLINE size_type length() const noexcept {
        if (_is_sro) return _storage.inline_leaf.size();
        return _storage.root ? _storage.root->length() : 0;
    }

    FL_INLINE size_type size() const noexcept {
        return length();
    }

    FL_INLINE bool empty() const noexcept {
        return _is_empty();
    }

    /// Reserve capacity. In SRO mode, forwards to the inline string.
    void reserve(size_type new_cap) {
        if (_is_sro) {
            _storage.inline_leaf.reserve(new_cap);
        }
        // In tree mode, no-op (rope nodes manage their own capacity)
    }

    /// Current capacity. In SRO mode, returns inline string capacity.
    [[nodiscard]] size_type capacity() const noexcept {
        if (_is_sro) return _storage.inline_leaf.capacity();
        return length();  // tree mode: capacity == length
    }

    /// Shrink to fit. In SRO mode, forwards to inline string.
    void shrink_to_fit() {
        if (_is_sro) {
            _storage.inline_leaf.shrink_to_fit();
        }
        // In tree mode, no-op
    }

    // ========== Element Access ==========

    FL_INLINE char operator[](size_type pos) const noexcept {
        if (_is_sro) return _storage.inline_leaf[pos];
        assert(_storage.root != nullptr && "operator[] called on empty rope");
        const size_type total_len = _storage.root->length();
        assert(pos < total_len && "operator[] index out of bounds");
        if (FL_LIKELY(_storage.root->type == node_type::leaf)) {
            return static_cast<const leaf_node*>(_storage.root.get())->storage[pos];
        }
        if (FL_UNLIKELY(total_len >= 4096)) {
            return _at_via_access_index(pos);
        }
        return _storage.root->at(pos);
    }

    char at(size_type pos) const {
        if (_is_sro) return _storage.inline_leaf.at(pos);
        if (_storage.root == nullptr) throw std::out_of_range("rope::at: empty rope");
        const size_type total_len = _storage.root->length();
        if (pos >= total_len) {
            throw std::out_of_range("rope::at: position out of range");
        }
        if (_storage.root->type == node_type::leaf) {
            return static_cast<const leaf_node*>(_storage.root.get())->storage[pos];
        }
        if (total_len >= 4096) {
            return _at_via_access_index(pos);
        }
        return _storage.root->at(pos);
    }

    char front() const noexcept {
        if (_is_sro) return _storage.inline_leaf.front();
        assert(_storage.root != nullptr && !empty());
        return _storage.root->at(0);
    }

    char back() const noexcept {
        if (_is_sro) return _storage.inline_leaf.back();
        assert(_storage.root != nullptr && !empty());
        return _storage.root->at(length() - 1);
    }

    // ========== Concatenation Operations ==========

    // O(1) amortised via balanced tree concatenation.
    [[nodiscard]] rope operator+(const rope& other) const {
        if (_is_sro && other._is_sro) {
            // Try SSO append
            if (_storage.inline_leaf.size() + other._storage.inline_leaf.size() <= SSO_CAPACITY) {
                rope result;
                result._storage.inline_leaf = _storage.inline_leaf;
                result._storage.inline_leaf.append(other._storage.inline_leaf);
                return result;
            }
            // SRO+SRO exceeds SSO capacity: create tree-mode leaves and concat
            auto left_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, _storage.inline_leaf);
            auto right_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, other._storage.inline_leaf);
            return rope(_balanced_concat(std::move(left_leaf), std::move(right_leaf)));
        }
        if (empty()) return other;
        if (other.empty()) return *this;
        // Handle mixed SRO/tree mode: promote SRO side to tree
        if (_is_sro) {
            auto left_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, _storage.inline_leaf);
            return rope(_balanced_concat(std::move(left_leaf), other._storage.root));
        }
        if (other._is_sro) {
            auto right_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, other._storage.inline_leaf);
            return rope(_balanced_concat(_storage.root, std::move(right_leaf)));
        }
        return rope(_balanced_concat(_storage.root, other._storage.root));
    }

    [[nodiscard]] rope operator+(const char* cstr) const {
        if (!cstr || !*cstr) return *this;
        std::size_t len = std::strlen(cstr);
        if (_is_sro) {
            if (_storage.inline_leaf.size() + len <= SSO_CAPACITY) {
                rope result;
                result._storage.inline_leaf = _storage.inline_leaf;
                result._storage.inline_leaf.append(cstr, len);
                return result;
            }
            // SRO exceeds SSO capacity: promote to tree leaf and try leaf append
            auto leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, _storage.inline_leaf);
            if (leaf->try_append(cstr, len)) {
                return rope(std::move(leaf));
            }
            return rope(std::move(leaf)) + rope(cstr, len);
        }
        if (_storage.root && _storage.root->type == node_type::leaf && _storage.root->length() + len <= kLeafMergeMax) {
            fl::string s = static_cast<const leaf_node*>(_storage.root.get())->storage;
            s.append(cstr, len);
            return rope(std::move(s));
        }
        return *this + rope(cstr, len);
    }

    [[nodiscard]] rope operator+(const string& str) const {
        return *this + rope(str);
    }

    /// Symmetric operator+ overloads.
    [[nodiscard]] friend rope operator+(const char* lhs, const rope& rhs) {
        return rope(lhs) + rhs;
    }
    [[nodiscard]] friend rope operator+(char lhs, const rope& rhs) {
        return rope(&lhs, 1) + rhs;
    }
    [[nodiscard]] friend rope operator+(const rope& lhs, char rhs) {
        return lhs + rope(&rhs, 1);
    }

    // Rvalue overloads: steal from the rvalue to avoid copying its tree.
    [[nodiscard]] friend rope operator+(rope&& lhs, const rope& rhs) {
        if (lhs.empty()) return rhs;
        if (rhs.empty()) return std::move(lhs);
        if (lhs._is_sro && rhs._is_sro) {
            if (lhs._storage.inline_leaf.size() + rhs._storage.inline_leaf.size() <= SSO_CAPACITY) {
                lhs._storage.inline_leaf.append(rhs._storage.inline_leaf);
                return std::move(lhs);
            }
            auto right_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, rhs._storage.inline_leaf);
            lhs._transition_sro_to_tree();
            lhs._storage.root = _balanced_concat(std::move(lhs._storage.root), std::move(right_leaf));
            return std::move(lhs);
        }
        if (lhs._is_sro) {
            lhs._transition_sro_to_tree();
        }
        if (rhs._is_sro) {
            auto right_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, rhs._storage.inline_leaf);
            lhs._storage.root = _balanced_concat(std::move(lhs._storage.root), std::move(right_leaf));
        } else {
            lhs._storage.root = _balanced_concat(std::move(lhs._storage.root), rhs._storage.root);
        }
        lhs._invalidate_linear_cache();
        return std::move(lhs);
    }

    [[nodiscard]] friend rope operator+(const rope& lhs, rope&& rhs) {
        if (lhs.empty()) return std::move(rhs);
        if (rhs.empty()) return lhs;
        if (lhs._is_sro && rhs._is_sro) {
            if (lhs._storage.inline_leaf.size() + rhs._storage.inline_leaf.size() <= SSO_CAPACITY) {
                rhs._storage.inline_leaf.insert(0, lhs._storage.inline_leaf);
                return std::move(rhs);
            }
            auto left_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, lhs._storage.inline_leaf);
            rhs._transition_sro_to_tree();
            rhs._storage.root = _balanced_concat(std::move(left_leaf), std::move(rhs._storage.root));
            return std::move(rhs);
        }
        if (rhs._is_sro) {
            rhs._transition_sro_to_tree();
        }
        if (lhs._is_sro) {
            auto left_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, lhs._storage.inline_leaf);
            rhs._storage.root = _balanced_concat(std::move(left_leaf), std::move(rhs._storage.root));
        } else {
            rhs._storage.root = _balanced_concat(lhs._storage.root, std::move(rhs._storage.root));
        }
        rhs._invalidate_linear_cache();
        return std::move(rhs);
    }

    [[nodiscard]] friend rope operator+(rope&& lhs, rope&& rhs) {
        if (lhs.empty()) return std::move(rhs);
        if (rhs.empty()) return std::move(lhs);
        if (lhs._is_sro && rhs._is_sro) {
            if (lhs._storage.inline_leaf.size() + rhs._storage.inline_leaf.size() <= SSO_CAPACITY) {
                lhs._storage.inline_leaf.append(rhs._storage.inline_leaf);
                return std::move(lhs);
            }
            auto left_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, lhs._storage.inline_leaf);
            auto right_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, rhs._storage.inline_leaf);
            return rope(_balanced_concat(std::move(left_leaf), std::move(right_leaf)));
        }
        if (lhs._is_sro) lhs._transition_sro_to_tree();
        if (rhs._is_sro) rhs._transition_sro_to_tree();
        lhs._storage.root = _balanced_concat(std::move(lhs._storage.root), std::move(rhs._storage.root));
        rhs._storage.root.reset();  // prevent double-free
        lhs._invalidate_linear_cache();
        return std::move(lhs);
    }

    // O(1) amortised. Attempts in-place append when the root has exclusive
    // ownership; otherwise creates a new balanced concatenation node.
    rope& operator+=(const rope& other) {
        if (other.empty()) return *this;
        if (_is_sro && other._is_sro) {
            // Try in-place append if SSO capacity allows
            if (_storage.inline_leaf.size() + other._storage.inline_leaf.size() <= SSO_CAPACITY) {
                _storage.inline_leaf.append(other._storage.inline_leaf);
                return *this;
            }
            // Otherwise transition to tree and concat
            _transition_sro_to_tree();
        } else if (_is_sro) {
            _transition_sro_to_tree();
        }
        if (empty()) {
            *this = other;
            return *this;
        }
        // Try in-place append to the rightmost leaf.  When other is in SRO
        // mode its inline_leaf data is used directly; otherwise the tree
        // root must be a single exclusive-ownership leaf.
        if (_storage.root && _storage.root.use_count() == 1 &&
            (other._is_sro || other._storage.root->type == node_type::leaf)) {
            const char* src;
            std::size_t src_len;
            if (other._is_sro) {
                src = other._storage.inline_leaf.data();
                src_len = other._storage.inline_leaf.size();
            } else {
                const auto* leaf = static_cast<const leaf_node*>(other._storage.root.get());
                src = leaf->storage.data();
                src_len = leaf->storage.size();
            }
            if (_storage.root->try_append(src, src_len)) {
                _invalidate_linear_cache();
                return *this;
            }
        }
        // Promote SRO other to tree mode before concat.
        if (other._is_sro) {
            auto right_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, other._storage.inline_leaf);
            _storage.root = _balanced_concat(_storage.root, std::move(right_leaf));
        } else {
            _storage.root = _balanced_concat(_storage.root, other._storage.root);
        }
        _invalidate_linear_cache();
        return *this;
    }

    rope& operator+=(const char* cstr) {
        if (!cstr || !*cstr) return *this;
        if (_is_sro) {
            std::size_t len = std::strlen(cstr);
            if (_storage.inline_leaf.size() + len <= SSO_CAPACITY) {
                _storage.inline_leaf.append(cstr, len);
                return *this;
            }
            _transition_sro_to_tree();
        }
        return *this += std::string_view(cstr);
    }

    // Avoids an intermediary fl::string allocation. When the root leaf has
    // exclusive ownership and capacity, try_append extends it in-place (O(1));
    // otherwise a new leaf is created and balanced-concatenated.
    rope& operator+=(std::string_view sv) {
        if (sv.empty()) return *this;
        if (_is_sro) {
            if (_storage.inline_leaf.size() + sv.size() <= SSO_CAPACITY) {
                _storage.inline_leaf.append(sv.data(), sv.size());
                return *this;
            }
            _transition_sro_to_tree();
        }
        if (empty()) {
            _storage.root = _make_leaf(sv);
            return *this;
        }
        if (_storage.root && _storage.root.use_count() == 1 && _storage.root->try_append(sv.data(), sv.size())) {
            _invalidate_linear_cache();
            return *this;
        }
        // Create a leaf with pre-reserved capacity so subsequent try_append
        // calls can extend it without reallocation.
        fl::string s(sv);
        if (s.capacity() < kLeafAppendMax && kLeafAppendMax - s.size() > 512) {
            s.reserve(std::min(s.size() + 2048, kLeafAppendMax));
        }
        _storage.root = _balanced_concat(_storage.root,
            std::allocate_shared<leaf_node>(rope_node_alloc{}, std::move(s)));
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

    // -- Comparison ------------------------------------------------

    /// Three-way comparison via spaceship operator.
    [[nodiscard]] std::strong_ordering operator<=>(const rope& other) const noexcept {
        if (_is_sro && other._is_sro) {
            return _storage.inline_leaf <=> other._storage.inline_leaf;
        }
        // Handle mixed SRO/tree mode: compare via string_view
        if (_is_sro) {
            std::string_view lhs = _storage.inline_leaf;
            std::string rhs = other._linearize_to_std_string();
            return lhs <=> rhs;
        }
        if (other._is_sro) {
            std::string lhs = _linearize_to_std_string();
            std::string_view rhs = other._storage.inline_leaf;
            return lhs <=> rhs;
        }
        if (_storage.root == other._storage.root) return std::strong_ordering::equal;

        if (!_storage.root) return other._storage.root ? std::strong_ordering::less : std::strong_ordering::equal;
        if (!other._storage.root) return std::strong_ordering::greater;

        if (_storage.root->type == node_type::leaf && other._storage.root->type == node_type::leaf) {
            const auto& lhs_leaf = static_cast<const leaf_node*>(_storage.root.get())->storage;
            const auto& rhs_leaf = static_cast<const leaf_node*>(other._storage.root.get())->storage;
            return lhs_leaf <=> rhs_leaf;
        }

        // Tree-walking comparison avoids full O(n) linearisation on both sides.
        const int cmp = _compare_tree_with_rope(other);
        return cmp < 0 ? std::strong_ordering::less
             : cmp > 0 ? std::strong_ordering::greater
             : std::strong_ordering::equal;
    }

    [[nodiscard]] bool operator==(const rope& other) const noexcept {
        if (_is_sro && other._is_sro) {
            return _storage.inline_leaf == other._storage.inline_leaf;
        }
        if (length() != other.length()) return false;
        // Handle mixed SRO/tree mode: compare via string_view
        if (_is_sro) {
            std::string_view lhs = _storage.inline_leaf;
            std::string rhs = other._linearize_to_std_string();
            return lhs == rhs;
        }
        if (other._is_sro) {
            std::string lhs = _linearize_to_std_string();
            std::string_view rhs = other._storage.inline_leaf;
            return lhs == rhs;
        }
        if (_storage.root == other._storage.root) return true;
        if (!_storage.root || !other._storage.root) return false;

        if (_storage.root->type == node_type::leaf && other._storage.root->type == node_type::leaf) {
            const auto& lhs_leaf = static_cast<const leaf_node*>(_storage.root.get())->storage;
            const auto& rhs_leaf = static_cast<const leaf_node*>(other._storage.root.get())->storage;
            return std::memcmp(lhs_leaf.data(), rhs_leaf.data(), lhs_leaf.size()) == 0;
        }

        // Tree-walking comparison avoids full O(n) linearisation on both sides.
        return _compare_tree_with_rope(other) == 0;
    }

    // C++20 provides all relational operators via <=>, so no manual
    // operator<, <=, >, >= definitions are needed.

    /// Compare with a string_view (returns negative/zero/positive).
    [[nodiscard]] int compare(std::string_view sv) const noexcept {
        if (_is_sro) {
            const auto& s = _storage.inline_leaf;
            const size_type my_len = s.size();
            const size_type cmp_len = (std::min)(my_len, sv.size());
            const int result = std::memcmp(s.data(), sv.data(), cmp_len);
            if (result != 0) return result;
            if (my_len < sv.size()) return -1;
            if (my_len > sv.size()) return 1;
            return 0;
        }
        // Tree mode: walk leaves instead of linearising the full rope.
        return _compare_tree_with_sv(sv);
    }

    // ========== Prefix / Suffix / Containment Checks ==========

    // Checks if the rope starts with the given prefix.
    [[nodiscard]] bool starts_with(std::string_view sv) const noexcept {
        if (sv.size() > size()) return false;
        const auto& cache = _ensure_linear_cache();
        return std::string_view(cache).substr(0, sv.size()) == sv;
    }

    // Checks if the rope ends with the given suffix.
    [[nodiscard]] bool ends_with(std::string_view sv) const noexcept {
        if (sv.size() > size()) return false;
        const auto& cache = _ensure_linear_cache();
        return std::string_view(cache).substr(size() - sv.size()) == sv;
    }

    // Checks if the rope contains the given substring.
    [[nodiscard]] bool contains(std::string_view sv) const noexcept {
        return find(sv) != npos;
    }

    // ========== Conversion and Linearisation ==========

    // Linearises the rope tree into a contiguous fl::string. O(n).
    // For O(1) read-only access after the first linearisation, use data()
    // or c_str() instead — they share the cached result without copying.
    [[nodiscard]] string flatten() const;

    // Linearises the rope tree into a contiguous std::string. O(n).
    [[nodiscard]] std::string to_std_string() const {
        auto result = _linearize_to_std_string();
        return result;
    }

    /// Return as fl::string (alias for flatten() for API consistency with std::to_string pattern).
    [[nodiscard]] fl::string to_string() const {
        return flatten();
    }

    // O(n) for the extracted range.
    [[nodiscard]] substring_view substr(size_type offset = 0,
                                       size_type len = std::string::npos) const;

    // ========== Iteration (requires linearisation) ==========

    // Returns an iterator to the beginning. Triggers O(n) linearisation on
    // first call; subsequent calls reuse the cached linear representation.
    std::string::const_iterator begin() const {
        // _ensure_linear_cache handles SRO mode efficiently (populates from inline string)
        return _ensure_linear_cache().cbegin();
    }

    std::string::const_iterator end() const {
        return _ensure_linear_cache().cend();
    }

    /// Reverse iterators (const-only, matching begin()/end()).
    using reverse_iterator = std::reverse_iterator<const_iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]] const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(end());
    }
    [[nodiscard]] const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(begin());
    }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept {
        return rbegin();
    }
    [[nodiscard]] const_reverse_iterator crend() const noexcept {
        return rend();
    }

    // Returns a view over the linearised rope. O(n) to build on first call,
    // then O(1) for subsequent access. The returned view owns the cache so
    // the caller can continue using the data even if the rope mutates later.
    [[nodiscard]] rope_linear_view linear_view() const {
        std::shared_ptr<const std::string> owner;
        std::string_view view;
        {
            std::lock_guard<std::mutex> lock(_cache_mutex);
            const auto& cache = _ensure_linear_cache_locked();
            owner = _linear_cache;
            view = std::string_view(cache);
        }
        return rope_linear_view(std::move(owner), view);
    }

    // ========== Data Access (Linearisation) ==========

    // Returns a pointer to the underlying character data.
    // Triggers O(N) linearisation on first call; subsequent calls reuse the
    // cached linear representation (same trade-off as begin()/end()).
    [[nodiscard]] const char* data() const {
        if (_is_sro) return _storage.inline_leaf.data();
        return _ensure_linear_cache().data();
    }

    // Returns a null-terminated C string.
    // Triggers O(N) linearisation on first call (same as data()).
    [[nodiscard]] const char* c_str() const {
        if (_is_sro) return _storage.inline_leaf.c_str();
        return _ensure_linear_cache().c_str();
    }

    // ========== Rebalancing ==========

    // Flattens and rebuilds the rope tree as a single leaf node when depth
    // exceeds kRebalanceDepthThreshold. This is O(n) and is important for
    // ropes built through iterative concatenation to prevent deep nesting.
    void rebalance() {
        rebalance(kRebalanceDepthThreshold);
    }

    // Rebalances only when tree depth exceeds depth_threshold. Use a low
    // threshold (e.g. 8) to aggressively flatten after bulk concat. Use a high
    // threshold (e.g. kRebalanceDepthThreshold) to skip rebalancing for trees
    // that are already balanced by _balanced_concat. Callers that need a
    // guaranteed contiguous buffer should call flatten() directly.
    //
    // Complexity: O(n) when depth > depth_threshold, O(1) otherwise.
    void rebalance(size_type depth_threshold) {
        if (_is_sro) return;  // single leaf, already balanced
        if (empty()) return;
        if (_storage.root->type == node_type::leaf) return;
        if (_storage.root->depth() <= depth_threshold) return;
        auto linearised = to_std_string();
        _storage.root = std::allocate_shared<leaf_node>(rope_node_alloc{}, linearised.c_str(), linearised.length());
        _invalidate_linear_cache();

        // After rebalancing, try Tree→SRO transition
        _try_transition_tree_to_sro();
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
    bool flatten_if_deep(size_type depth_threshold) {
        if (_is_sro) return false;  // single leaf, already flat
        if (empty()) return false;
        if (_storage.root->type == node_type::leaf) return false;
        if (_storage.root->depth() <= depth_threshold) return false;
        auto linearised = to_std_string();
        _storage.root = std::allocate_shared<leaf_node>(rope_node_alloc{}, linearised.c_str(), linearised.length());
        _invalidate_linear_cache();

        // After flattening, try Tree→SRO transition
        _try_transition_tree_to_sro();

        return true;
    }

    [[nodiscard]] int depth() const noexcept {
        if (_is_sro) return 0;
        if (_storage.root == nullptr) return 0;
        return _storage.root->depth();
    }

    // ========== Search Operations ==========

    // Tree-walking find. Searches for the first occurrence of sv starting at pos.
    // Walks the tree left-to-right, delegating to fl::string::find() on each leaf,
    // and handles cross-boundary matches.
    [[nodiscard]] size_type find(std::string_view sv, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find(sv, pos);
        return _find_impl(sv, pos);
    }

    [[nodiscard]] size_type find(char ch, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find(ch, pos);
        return _find_impl(std::string_view(&ch, 1), pos);
    }

    [[nodiscard]] size_type find(const fl::string& str, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find(str, pos);
        return _find_impl(std::string_view(str.data(), str.size()), pos);
    }

    [[nodiscard]] size_type find(const char* cstr, size_type pos = 0) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find(cstr, pos);
        return _find_impl(std::string_view(cstr), pos);
    }

    [[nodiscard]] size_type find(const char* cstr, size_type pos, size_type count) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find(cstr, pos, count);
        return _find_impl(std::string_view(cstr, count), pos);
    }

    // Tree-walking reverse find.
    [[nodiscard]] size_type rfind(std::string_view sv, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.rfind(sv, pos);
        return _rfind_impl(sv, pos);
    }

    [[nodiscard]] size_type rfind(char ch, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.rfind(ch, pos);
        return _rfind_impl(std::string_view(&ch, 1), pos);
    }

    [[nodiscard]] size_type rfind(const fl::string& str, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.rfind(str, pos);
        return _rfind_impl(std::string_view(str.data(), str.size()), pos);
    }

    [[nodiscard]] size_type rfind(const char* cstr, size_type pos = npos) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.rfind(cstr, pos);
        return _rfind_impl(std::string_view(cstr), pos);
    }

    [[nodiscard]] size_type rfind(const char* cstr, size_type pos, size_type count) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.rfind(cstr, pos, count);
        return _rfind_impl(std::string_view(cstr, count), pos);
    }

    // Tree-walking find_first_of.
    [[nodiscard]] size_type find_first_of(std::string_view sv, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_first_of(sv, pos);
        return _find_first_of_impl(sv, pos);
    }

    [[nodiscard]] size_type find_first_of(char ch, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_first_of(ch, pos);
        return _find_impl(std::string_view(&ch, 1), pos);
    }

    [[nodiscard]] size_type find_first_of(const fl::string& str, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_first_of(str, pos);
        return _find_first_of_impl(std::string_view(str.data(), str.size()), pos);
    }

    [[nodiscard]] size_type find_first_of(const char* cstr, size_type pos = 0) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find_first_of(cstr, pos);
        return _find_first_of_impl(std::string_view(cstr), pos);
    }

    [[nodiscard]] size_type find_first_of(const char* cstr, size_type pos, size_type count) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find_first_of(cstr, pos, count);
        return _find_first_of_impl(std::string_view(cstr, count), pos);
    }

    // Tree-walking find_last_of.
    [[nodiscard]] size_type find_last_of(std::string_view sv, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_last_of(sv, pos);
        return _find_last_of_impl(sv, pos);
    }

    [[nodiscard]] size_type find_last_of(char ch, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_last_of(ch, pos);
        return _rfind_impl(std::string_view(&ch, 1), pos);
    }

    [[nodiscard]] size_type find_last_of(const fl::string& str, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_last_of(str, pos);
        return _find_last_of_impl(std::string_view(str.data(), str.size()), pos);
    }

    [[nodiscard]] size_type find_last_of(const char* cstr, size_type pos = npos) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find_last_of(cstr, pos);
        return _find_last_of_impl(std::string_view(cstr), pos);
    }

    [[nodiscard]] size_type find_last_of(const char* cstr, size_type pos, size_type count) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find_last_of(cstr, pos, count);
        return _find_last_of_impl(std::string_view(cstr, count), pos);
    }

    // Tree-walking find_first_not_of.
    [[nodiscard]] size_type find_first_not_of(std::string_view sv, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_first_not_of(sv, pos);
        return _find_first_not_of_impl(sv, pos);
    }

    [[nodiscard]] size_type find_first_not_of(char ch, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_first_not_of(ch, pos);
        return _find_first_not_of_impl(std::string_view(&ch, 1), pos);
    }

    [[nodiscard]] size_type find_first_not_of(const fl::string& str, size_type pos = 0) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_first_not_of(str, pos);
        return _find_first_not_of_impl(std::string_view(str.data(), str.size()), pos);
    }

    [[nodiscard]] size_type find_first_not_of(const char* cstr, size_type pos = 0) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find_first_not_of(cstr, pos);
        return _find_first_not_of_impl(std::string_view(cstr), pos);
    }

    [[nodiscard]] size_type find_first_not_of(const char* cstr, size_type pos, size_type count) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find_first_not_of(cstr, pos, count);
        return _find_first_not_of_impl(std::string_view(cstr, count), pos);
    }

    // Tree-walking find_last_not_of.
    [[nodiscard]] size_type find_last_not_of(std::string_view sv, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_last_not_of(sv, pos);
        return _find_last_not_of_impl(sv, pos);
    }

    [[nodiscard]] size_type find_last_not_of(char ch, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_last_not_of(ch, pos);
        return _find_last_not_of_impl(std::string_view(&ch, 1), pos);
    }

    [[nodiscard]] size_type find_last_not_of(const fl::string& str, size_type pos = npos) const noexcept {
        if (_is_sro) return _storage.inline_leaf.find_last_not_of(str, pos);
        return _find_last_not_of_impl(std::string_view(str.data(), str.size()), pos);
    }

    [[nodiscard]] size_type find_last_not_of(const char* cstr, size_type pos = npos) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find_last_not_of(cstr, pos);
        return _find_last_not_of_impl(std::string_view(cstr), pos);
    }

    [[nodiscard]] size_type find_last_not_of(const char* cstr, size_type pos, size_type count) const noexcept {
        if (!cstr) return npos;
        if (_is_sro) return _storage.inline_leaf.find_last_not_of(cstr, pos, count);
        return _find_last_not_of_impl(std::string_view(cstr, count), pos);
    }

    // ========== Modifying Operations ==========

    // Split the rope at position pos.
    // After this call, this rope contains [0, pos) and the returned rope contains [pos, size()).
    // If pos >= size(), returns an empty rope.
    // If pos == 0, moves the entire tree to the returned rope and this becomes empty.
    [[nodiscard]] rope split(size_type pos);

    // Insert sv at position pos. O(log N) amortised.
    rope& insert(size_type pos, std::string_view sv);

    // Insert another rope at position pos. O(log N) amortised.
    rope& insert(size_type pos, const rope& other);

    // Erase len characters starting at position pos. O(log N) amortised.
    rope& erase(size_type pos = 0, size_type len = npos);

    // Replace len characters at position pos with sv. O(log N) amortised.
    rope& replace(size_type pos, size_type len, std::string_view sv);

    // Replace len characters at position pos with other rope. O(log N) amortised.
    rope& replace(size_type pos, size_type len, const rope& other);

    /// Swap contents with another rope.
    void swap(rope& other) noexcept {
        using std::swap;
        if (_is_sro && other._is_sro) {
            swap(_storage.inline_leaf, other._storage.inline_leaf);
        } else if (!_is_sro && !other._is_sro) {
            swap(_storage.root, other._storage.root);
        } else {
            // Mixed mode: swap the entire state including discriminant
            rope tmp(std::move(*this));
            *this = std::move(other);
            other = std::move(tmp);
        }
        swap(_linear_cache, other._linear_cache);
        // swap _access_index and related cache state
        swap(_access_index, other._access_index);
        swap(_access_samples, other._access_samples);
        swap(_access_index_total_len, other._access_index_total_len);
    }

    friend void swap(rope& lhs, rope& rhs) noexcept {
        lhs.swap(rhs);
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

    struct leaf_info {
        const char* data;
        size_type len;
        size_type global_start;
    };

    bool _is_sro = false;  // true when using inline storage (SRO mode)

    union Storage {
        node_ptr root;          // tree mode: shared_ptr<node>
        fl::string inline_leaf; // SRO mode: inline string

        Storage() : root(nullptr) {}
        ~Storage() {}  // managed manually
    } _storage;

    bool _is_sro_mode() const noexcept { return _is_sro; }

    /// Check if rope is empty (null tree or empty inline leaf).
    bool _is_empty() const noexcept {
        if (_is_sro) return _storage.inline_leaf.empty();
        return _storage.root == nullptr;
    }

    /// Get mutable reference to the inline leaf (SRO mode only).
    fl::string& _sro_leaf() noexcept {
        assert(_is_sro);
        return _storage.inline_leaf;
    }
    const fl::string& _sro_leaf() const noexcept {
        assert(_is_sro);
        return _storage.inline_leaf;
    }

    /// Get mutable reference to the tree root (tree mode only).
    node_ptr& _tree_root() noexcept {
        assert(!_is_sro);
        return _storage.root;
    }
    const node_ptr& _tree_root() const noexcept {
        assert(!_is_sro);
        return _storage.root;
    }

    /// Transition from SRO mode to tree mode.
    /// Extracts the inline string, creates a leaf_node, and stores it as shared_ptr.
    void _transition_sro_to_tree() {
        assert(_is_sro);
        fl::string s = std::move(_storage.inline_leaf);
        _storage.inline_leaf.~string();  // destroy inline string
        new (&_storage.root) node_ptr(std::make_shared<leaf_node>(std::move(s)));
        _is_sro = false;
    }

    /// Construct in SRO mode from a string.
    void _init_sro(fl::string&& s) noexcept {
        _is_sro = true;
        new (&_storage.inline_leaf) fl::string(std::move(s));
    }

    /// Try to transition from tree mode to SRO mode when the tree is a
    /// single leaf.  This is called after rebalance() and flatten_if_deep()
    /// to reclaim the tree overhead for small ropes.
    void _try_transition_tree_to_sro() {
        assert(!_is_sro);
        if (_storage.root && _storage.root.use_count() == 1 && _storage.root->is_leaf()) {
            leaf_node* leaf = static_cast<leaf_node*>(_storage.root.get());
            fl::string s = std::move(leaf->storage);
            // Destroy tree storage
            _storage.root.~node_ptr();
            // Construct inline leaf
            _is_sro = true;
            new (&_storage.inline_leaf) fl::string(std::move(s));
        }
    }

    mutable std::shared_ptr<std::string> _linear_cache;  // Cached linearised data for iterator lifetime.
    mutable std::mutex _cache_mutex;
    mutable std::vector<access_chunk> _access_index;
    mutable std::vector<access_sample> _access_samples;
    mutable size_type _access_index_total_len = 0;

    explicit rope(node_ptr root) noexcept;
    [[nodiscard]] std::string _linearize_to_std_string() const;
    [[nodiscard]] const std::string& _ensure_linear_cache() const;
    [[nodiscard]] const std::string& _ensure_linear_cache_locked() const;
    void _invalidate_linear_cache() const noexcept;
    void _clear_caches_unlocked() const noexcept;
    void _build_access_index() const;
    void _build_access_index_locked() const;
    [[nodiscard]] char _at_via_access_index(size_type pos) const;

    // Collect all leaves into a vector with their global start positions.
    void _collect_leaves(std::vector<leaf_info>& leaves) const;

    // Split the subtree rooted at 'n' at position 'pos' (relative to n).
    // left_out receives the subtree for [0, pos), right_out for [pos, len).
    static void _split_node(const node_ptr& n, size_type pos,
                            node_ptr& left_out, node_ptr& right_out);

    // Tree-walking comparison helpers (avoid full linearisation).
    // Compares tree content against a string_view, returning neg/zero/pos.
    [[nodiscard]] int _compare_tree_with_sv(std::string_view sv) const;
    // Compares two trees leaf-by-leaf, returning neg/zero/pos.
    [[nodiscard]] int _compare_tree_with_rope(const rope& other) const;

    // Core find implementations (tree-walking).
    [[nodiscard]] size_type _find_impl(std::string_view sv, size_type pos) const;
    [[nodiscard]] size_type _rfind_impl(std::string_view sv, size_type pos) const;
    [[nodiscard]] size_type _find_first_of_impl(std::string_view sv, size_type pos) const;
    [[nodiscard]] size_type _find_last_of_impl(std::string_view sv, size_type pos) const;
    [[nodiscard]] size_type _find_first_not_of_impl(std::string_view sv, size_type pos) const;
    [[nodiscard]] size_type _find_last_not_of_impl(std::string_view sv, size_type pos) const;

    friend class string;
    friend class substring_view;
};

inline rope::node_ptr rope::_make_leaf(std::string_view data) {
    return data.empty() ? nullptr : std::allocate_shared<rope::leaf_node>(rope_node_alloc{}, data);
}

inline rope::rope(node_ptr root) noexcept {
    _is_sro = false;
    new (&_storage.root) node_ptr(std::move(root));
}

inline rope::rope(const substring_view& view) {
    _init_sro(fl::string(view.data(), view.size()));
}

inline fl::string rope::flatten() const {
    if (_is_sro) return _storage.inline_leaf;
    if (empty()) return fl::string();
    if (_storage.root && _storage.root->type == node_type::leaf) {
        return static_cast<const leaf_node*>(_storage.root.get())->storage;
    }

    // Check cached linearization first (O(1) fast path).
    // The _linear_cache is populated by flatten(), begin(), end(), etc.,
    // so sequential calls to any of these avoid O(N) tree traversal.
    {
        std::lock_guard<std::mutex> lock(_cache_mutex);
        if (_linear_cache && _linear_cache->size() == length()) {
            return fl::string(_linear_cache->data(), _linear_cache->size());
        }
    }

    const std::size_t len = length();
    fl::string result;

    // Uninitialized allocation: avoids the double memory bandwidth from
    // string(count, '\0') which would memset the entire buffer before
    // copy_to immediately overwrites it.  For large ropes (e.g., 16 KB+),
    // this cuts memory traffic in half (one write instead of two).
    //
    // Exception safety: copy_to is noexcept (memcpy only).  Even if it
    // weren't, _size is set correctly so ~string() would free properly.
    result._size = len;
    if (FL_UNLIKELY(len > SSO_CAPACITY)) {
        result._allocate_heap_exact(len);
    }
    // SSO case: _flags==0 (default), _data.sso is the buffer, _size=len.
    // Heap case: _allocate_heap_exact set HEAP_ALLOCATED_FLAG and _data.heap.
    // In both cases the buffer content is uninitialized -- copy_to fills it.

    // Copy tree data into the uninitialized buffer.
    if (_storage.root) {
        span<char> s(result.data(), result.size());
        _storage.root->copy_to(s);
    }

    // Null-terminate (copy_to copies exactly len bytes, no terminator).
    result.data()[len] = '\0';

    // Cache the linearized result for subsequent calls to flatten(),
    // begin(), end(), c_str(), etc.
    {
        std::lock_guard<std::mutex> lock(_cache_mutex);
        if (!_linear_cache || _linear_cache->size() != len) {
            _linear_cache = std::make_shared<std::string>(result.data(), result.size());
        }
    }

    return result;
}

inline fl::substring_view rope::substr(size_type offset, size_type len) const {
    if (_is_sro) {
        // Use substr_view() not substr() — substr() returns a temporary
        // fl::string whose data becomes dangling when the view is converted.
        return _storage.inline_leaf.substr_view(offset, len);
    }
    if (offset >= length()) return fl::substring_view();
    size_type rlen = std::min(len, length() - offset);
    if (rlen == 0) return fl::substring_view();

    if (_storage.root && _storage.root->type == node_type::leaf) {
        const auto* leaf = static_cast<const leaf_node*>(_storage.root.get());
        return fl::substring_view(
            leaf->storage.data() + offset,
            rlen,
            std::static_pointer_cast<const void>(_storage.root)
        );
    }

    // Create an owning fl::string to hold only the required range.
    auto owner = std::allocate_shared<fl::string>(fl::pool_alloc<fl::string>{}, rlen, '\0');
    if (_storage.root) {
        span<char> s(owner->data(), owner->size());
        _storage.root->copy_range_to(s, offset, rlen);
    }

    // Create substring_view with shared ownership.
    return fl::substring_view(owner->data(), rlen, std::static_pointer_cast<const void>(owner));
}

inline std::string rope::_linearize_to_std_string() const {
    if (_is_sro) {
        return std::string(_storage.inline_leaf.data(), _storage.inline_leaf.size());
    }
    if (empty()) {
        return std::string();
    }
    if (_storage.root && _storage.root->type == node_type::leaf) {
        const auto& storage = static_cast<const leaf_node*>(_storage.root.get())->storage;
        return std::string(storage.data(), storage.size());
    }

    // Uninitialized allocation via fl::string internals: avoids the double
    // memory bandwidth from std::string(count, '\0') which would zero-init
    // the entire buffer before copy_to immediately overwrites it.
    // For large ropes (e.g., 16 KB+), this cuts memory traffic in half.
    const std::size_t len = length();
    fl::string fl_result;
    fl_result._size = len;
    if (FL_UNLIKELY(len > SSO_CAPACITY)) {
        fl_result._allocate_heap_exact(len);
    }
    // SSO case: _flags==0 (default), _data.sso is the buffer, _size=len.
    // Heap case: _allocate_heap_exact set HEAP_ALLOCATED_FLAG and _data.heap.
    // In both cases the buffer content is uninitialized -- copy_to fills it.

    if (_storage.root) {
        span<char> s(fl_result.data(), fl_result.size());
        _storage.root->copy_to(s);
    }

    // Null-terminate (copy_to copies exactly len bytes, no terminator).
    fl_result.data()[len] = '\0';

    // Convert to std::string — a single memcpy of the data, no double write.
    return std::string(fl_result.data(), fl_result.size());
}

inline const std::string& rope::_ensure_linear_cache() const {
    if (_is_sro) {
        // In SRO mode, the inline string IS the cache.
        // We need a std::string reference; populate _linear_cache if needed.
        std::lock_guard<std::mutex> lock(_cache_mutex);
        if (!_linear_cache || _linear_cache->size() != _storage.inline_leaf.size()) {
            _linear_cache = std::make_shared<std::string>(_storage.inline_leaf.data(), _storage.inline_leaf.size());
        }
        return *_linear_cache;
    }
    std::lock_guard<std::mutex> lock(_cache_mutex);
    return _ensure_linear_cache_locked();
}

inline const std::string& rope::_ensure_linear_cache_locked() const {
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
    std::lock_guard<std::mutex> lock(_cache_mutex);
    _build_access_index_locked();
}

inline void rope::_build_access_index_locked() const {
    // Should only be called in tree mode; guard defensively.
    if (_is_sro) {
        _access_index.clear();
        _access_samples.clear();
        _access_index_total_len = 0;
        return;
    }
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
    _access_index.reserve(_storage.root ? _storage.root->depth() * 2 : 0);

    struct item { const node* n; size_type start; };
    std::vector<item> stack;
    stack.reserve(_storage.root ? _storage.root->depth() * 2 : 0);
    stack.push_back({_storage.root.get(), 0});

    while (!stack.empty()) {
        item curr = stack.back();
        stack.pop_back();

        if (curr.n->type == node_type::leaf) {
            const auto* leaf = static_cast<const leaf_node*>(curr.n);
            const size_type len = leaf->storage.size();
            if (len > 0) {
                _access_index.push_back({
                    std::shared_ptr<const node>(_storage.root, curr.n),
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

inline char rope::_at_via_access_index(size_type pos) const {
    std::lock_guard<std::mutex> lock(_cache_mutex);
    _build_access_index_locked();
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

    return _storage.root->at(pos);
}

inline void rope::_invalidate_linear_cache() const noexcept {
    // Fast path: nothing cached, skip the mutex entirely.
    if (!_linear_cache && _access_index.empty()) return;
    std::lock_guard<std::mutex> lock(_cache_mutex);
    _clear_caches_unlocked();
}

inline void rope::_clear_caches_unlocked() const noexcept {
    _linear_cache.reset();
    _access_index.clear();
    _access_samples.clear();
    _access_index_total_len = 0;
}

// ============================================================================
// Leaf collection helper.
// ============================================================================

inline void rope::_collect_leaves(std::vector<leaf_info>& leaves) const {
    // Should only be called in tree mode; guard defensively.
    if (_is_sro || !_storage.root) return;
    struct frame { const node* n; size_type start; };
    frame stack[64];
    size_type sp = 0;
    stack[sp++] = {_storage.root.get(), 0};
    while (FL_LIKELY(sp > 0)) {
        const frame f = stack[--sp];
        const node* curr = f.n;
        if (FL_LIKELY(curr->type == node_type::leaf)) {
            const auto* leaf = static_cast<const leaf_node*>(curr);
            const size_type len = leaf->storage.size();
            if (FL_LIKELY(len > 0)) {
                leaves.push_back({leaf->storage.data(), len, f.start});
            }
        } else {
            const auto* concat = static_cast<const concat_node*>(curr);
            const size_type left_len = concat->left->length();
            // Push right first (LIFO -> left processed before right).
            stack[sp++] = {concat->right.get(), f.start + left_len};
            stack[sp++] = {concat->left.get(), f.start};
        }
    }
}

// ============================================================================
// _compare_tree_with_sv -- leaf-walking compare against string_view (avoids
// full O(n) linearisation).  Early-exits on the first differing byte.
// ============================================================================

inline int rope::_compare_tree_with_sv(std::string_view sv) const {
    if (!_storage.root) return sv.empty() ? 0 : -1;

    std::vector<leaf_info> leaves;
    _collect_leaves(leaves);

    const size_type my_len = length();
    const size_type cmp_len = (std::min)(my_len, sv.size());

    size_type remaining = cmp_len;
    for (const auto& leaf : leaves) {
        const size_type chunk = (std::min)(leaf.len, remaining);
        const int res = std::memcmp(leaf.data, sv.data() + (cmp_len - remaining), chunk);
        if (res != 0) return res < 0 ? -1 : 1;
        remaining -= chunk;
        if (remaining == 0) break;
    }

    if (my_len < sv.size()) return -1;
    if (my_len > sv.size()) return 1;
    return 0;
}

// ============================================================================
// _compare_tree_with_rope -- leaf-walking compare between two tree-mode ropes
// (avoids full O(n) linearisation on both sides).  Early-exits on the first
// differing byte.
// ============================================================================

inline int rope::_compare_tree_with_rope(const rope& other) const {
    std::vector<leaf_info> leaves_a, leaves_b;
    _collect_leaves(leaves_a);
    other._collect_leaves(leaves_b);

    std::size_t i = 0, j = 0;
    std::size_t off_a = 0, off_b = 0;

    while (i < leaves_a.size() && j < leaves_b.size()) {
        const char* da = leaves_a[i].data + off_a;
        std::size_t la = leaves_a[i].len - off_a;
        const char* db = leaves_b[j].data + off_b;
        std::size_t lb = leaves_b[j].len - off_b;

        const std::size_t chunk = (std::min)(la, lb);
        const int res = std::memcmp(da, db, chunk);
        if (res != 0) return res < 0 ? -1 : 1;

        off_a += chunk;
        off_b += chunk;
        if (off_a >= leaves_a[i].len) { ++i; off_a = 0; }
        if (off_b >= leaves_b[j].len) { ++j; off_b = 0; }
    }

    if (i >= leaves_a.size() && j >= leaves_b.size()) return 0;
    return (i >= leaves_a.size()) ? -1 : 1;
}

// ============================================================================
// _split_node -- recursive tree split helper.
// ============================================================================

inline void rope::_split_node(const node_ptr& n, size_type pos,
                              node_ptr& left_out, node_ptr& right_out) {
    if (FL_UNLIKELY(!n)) {
        left_out = nullptr;
        right_out = nullptr;
        return;
    }

    if (FL_LIKELY(n->type == node_type::leaf)) {
        const auto* leaf = static_cast<const leaf_node*>(n.get());
        const size_type len = leaf->storage.size();
        if (pos == 0) {
            left_out = nullptr;
            right_out = n;
        } else if (pos >= len) {
            left_out = n;
            right_out = nullptr;
        } else {
            // Split the leaf into two leaves at position pos.
            auto left_leaf = std::allocate_shared<leaf_node>(
                rope_node_alloc{}, std::string_view(leaf->storage.data(), pos));
            auto right_leaf = std::allocate_shared<leaf_node>(
                rope_node_alloc{}, std::string_view(leaf->storage.data() + pos, len - pos));
            left_out = std::move(left_leaf);
            right_out = std::move(right_leaf);
        }
        return;
    }

    const auto* concat = static_cast<const concat_node*>(n.get());
    const size_type left_len = concat->left->length();

    if (pos < left_len) {
        // Split point is inside the left subtree.
        node_ptr left_left, left_right;
        _split_node(concat->left, pos, left_left, left_right);
        left_out = std::move(left_left);
        right_out = _balanced_concat(std::move(left_right), concat->right);
    } else if (pos == left_len) {
        // Split point is exactly at the boundary.
        left_out = concat->left;
        right_out = concat->right;
    } else {
        // Split point is inside the right subtree.
        node_ptr right_left, right_right;
        _split_node(concat->right, pos - left_len, right_left, right_right);
        left_out = _balanced_concat(concat->left, std::move(right_left));
        right_out = std::move(right_right);
    }
}

// ============================================================================
// split() -- public split operation.
// ============================================================================

inline rope rope::split(size_type pos) {
    if (_is_sro) {
        fl::string left = _storage.inline_leaf.substr(0, pos);
        fl::string right = _storage.inline_leaf.substr(pos);
        _storage.inline_leaf = std::move(left);
        return rope(std::move(right));
    }
    if (FL_UNLIKELY(pos >= size())) {
        return rope();
    }
    if (FL_UNLIKELY(pos == 0)) {
        rope result;
        result._storage.root = std::move(_storage.root);
        _storage.root = nullptr;
        _invalidate_linear_cache();
        return result;
    }

    node_ptr left, right;
    _split_node(_storage.root, pos, left, right);
    _storage.root = std::move(left);
    _invalidate_linear_cache();
    return rope(std::move(right));
}

// ============================================================================
// _find_impl -- tree-walking forward find.
// ============================================================================

inline rope::size_type rope::_find_impl(std::string_view sv, size_type pos) const {
    if (FL_UNLIKELY(sv.empty())) return pos;
    if (FL_UNLIKELY(pos >= size() || !_storage.root)) return npos;

    std::vector<leaf_info> leaves;
    _collect_leaves(leaves);

    const size_type sv_len = sv.size();

    // Find the first leaf that contains or is after pos.
    size_type leaf_idx = 0;
    while (leaf_idx < leaves.size() &&
           leaves[leaf_idx].global_start + leaves[leaf_idx].len <= pos) {
        ++leaf_idx;
    }
    if (FL_UNLIKELY(leaf_idx >= leaves.size())) return npos;

    while (FL_LIKELY(leaf_idx < leaves.size())) {
        const auto& leaf = leaves[leaf_idx];
        const size_type local_pos = (leaf.global_start <= pos)
                                        ? (pos - leaf.global_start)
                                        : 0;

        // Search within this leaf using std::string_view::find.
        const std::string_view leaf_sv(leaf.data, leaf.len);
        const size_type found = leaf_sv.find(sv, local_pos);
        if (FL_LIKELY(found != std::string_view::npos)) {
            return leaf.global_start + found;
        }

        // Check cross-boundary match with the next leaf.
        if (sv_len > 1 && leaf_idx + 1 < leaves.size()) {
            const auto& next_leaf = leaves[leaf_idx + 1];

            // Number of bytes needed from end of current leaf and start of next.
            const size_type from_curr = (std::min)(sv_len - 1, leaf.len);
            const size_type from_next = (std::min)(sv_len - 1, next_leaf.len);
            const size_type buf_size = from_curr + from_next;

            if (buf_size >= sv_len) {
                // Small buffer on stack for typical patterns.
                char boundary_buf[256];
                std::string dynamic_buf;
                char* buf = boundary_buf;
                if (FL_UNLIKELY(buf_size > sizeof(boundary_buf))) {
                    dynamic_buf.resize(buf_size);
                    buf = &dynamic_buf[0];
                }

                std::memcpy(buf, leaf.data + leaf.len - from_curr, from_curr);
                std::memcpy(buf + from_curr, next_leaf.data, from_next);

                const std::string_view boundary_sv(buf, buf_size);
                const size_type bfound = boundary_sv.find(sv);
                if (FL_UNLIKELY(bfound != std::string_view::npos)) {
                    const size_type global = leaf.global_start + leaf.len - from_curr + bfound;
                    if (global >= pos) return global;
                }
            }
        }

        ++leaf_idx;
    }

    return npos;
}

// ============================================================================
// _rfind_impl -- tree-walking reverse find.
// ============================================================================

inline rope::size_type rope::_rfind_impl(std::string_view sv, size_type pos) const {
    if (FL_UNLIKELY(sv.empty())) return pos;
    if (FL_UNLIKELY(size() == 0 || !_storage.root)) return npos;

    std::vector<leaf_info> leaves;
    _collect_leaves(leaves);

    const size_type sv_len = sv.size();
    const size_type total_len = size();

    // Clamp pos to the last valid position.
    if (pos >= total_len) pos = total_len - 1;

    // Find the last leaf that contains or is before pos.
    size_type leaf_idx = leaves.size();
    while (leaf_idx > 0) {
        --leaf_idx;
        if (leaves[leaf_idx].global_start <= pos) break;
    }
    if (leaves.empty() || leaves[leaf_idx].global_start > pos) return npos;

    // Walk leaves right-to-left.
    while (true) {
        const auto& leaf = leaves[leaf_idx];
        const size_type local_pos = (std::min)(pos - leaf.global_start, leaf.len - 1);

        // Search within this leaf.
        const std::string_view leaf_sv(leaf.data, leaf.len);
        const size_type found = leaf_sv.rfind(sv, local_pos);
        if (FL_LIKELY(found != std::string_view::npos)) {
            return leaf.global_start + found;
        }

        // Check cross-boundary match with the previous leaf.
        if (sv_len > 1 && leaf_idx > 0) {
            const auto& prev_leaf = leaves[leaf_idx - 1];

            const size_type from_prev = (std::min)(sv_len - 1, prev_leaf.len);
            const size_type from_curr = (std::min)(sv_len - 1, leaf.len);
            const size_type buf_size = from_prev + from_curr;

            if (buf_size >= sv_len) {
                char boundary_buf[256];
                std::string dynamic_buf;
                char* buf = boundary_buf;
                if (FL_UNLIKELY(buf_size > sizeof(boundary_buf))) {
                    dynamic_buf.resize(buf_size);
                    buf = &dynamic_buf[0];
                }

                // Boundary buffer: end of prev_leaf + start of current leaf.
                std::memcpy(buf, prev_leaf.data + prev_leaf.len - from_prev, from_prev);
                std::memcpy(buf + from_prev, leaf.data, from_curr);

                const std::string_view boundary_sv(buf, buf_size);
                const size_type bfound = boundary_sv.rfind(sv);
                if (FL_UNLIKELY(bfound != std::string_view::npos)) {
                    const size_type global = prev_leaf.global_start + prev_leaf.len - from_prev + bfound;
                    if (global <= pos) return global;
                }
            }
        }

        if (leaf_idx == 0) break;
        --leaf_idx;
    }

    return npos;
}

// ============================================================================
// _find_first_of_impl -- tree-walking find_first_of.
// ============================================================================

inline rope::size_type rope::_find_first_of_impl(std::string_view sv, size_type pos) const {
    if (FL_UNLIKELY(sv.empty())) return npos;
    if (FL_UNLIKELY(pos >= size() || !_storage.root)) return npos;

    std::vector<leaf_info> leaves;
    _collect_leaves(leaves);

    const size_type sv_len = sv.size();

    // Find the first relevant leaf.
    size_type leaf_idx = 0;
    while (leaf_idx < leaves.size() &&
           leaves[leaf_idx].global_start + leaves[leaf_idx].len <= pos) {
        ++leaf_idx;
    }
    if (FL_UNLIKELY(leaf_idx >= leaves.size())) return npos;

    while (FL_LIKELY(leaf_idx < leaves.size())) {
        const auto& leaf = leaves[leaf_idx];
        const size_type local_pos = (leaf.global_start <= pos)
                                        ? (pos - leaf.global_start)
                                        : 0;

        const std::string_view leaf_sv(leaf.data, leaf.len);
        const size_type found = leaf_sv.find_first_of(sv, local_pos);
        if (FL_LIKELY(found != std::string_view::npos)) {
            return leaf.global_start + found;
        }

        // Cross-boundary: check if any char from sv appears at the boundary.
        if (leaf_idx + 1 < leaves.size()) {
            const auto& next_leaf = leaves[leaf_idx + 1];
            const size_type from_curr = (std::min)(sv_len - 1, leaf.len);
            const size_type from_next = (std::min)(sv_len - 1, next_leaf.len);
            const size_type buf_size = from_curr + from_next;

            if (buf_size > 0) {
                char boundary_buf[256];
                std::string dynamic_buf;
                char* buf = boundary_buf;
                if (FL_UNLIKELY(buf_size > sizeof(boundary_buf))) {
                    dynamic_buf.resize(buf_size);
                    buf = &dynamic_buf[0];
                }

                std::memcpy(buf, leaf.data + leaf.len - from_curr, from_curr);
                std::memcpy(buf + from_curr, next_leaf.data, from_next);

                const std::string_view boundary_sv(buf, buf_size);
                const size_type bfound = boundary_sv.find_first_of(sv);
                if (FL_UNLIKELY(bfound != std::string_view::npos)) {
                    const size_type global = leaf.global_start + leaf.len - from_curr + bfound;
                    if (global >= pos) return global;
                }
            }
        }

        ++leaf_idx;
    }

    return npos;
}

// ============================================================================
// _find_last_of_impl -- tree-walking find_last_of.
// ============================================================================

inline rope::size_type rope::_find_last_of_impl(std::string_view sv, size_type pos) const {
    if (FL_UNLIKELY(sv.empty())) return npos;
    if (FL_UNLIKELY(size() == 0 || !_storage.root)) return npos;

    std::vector<leaf_info> leaves;
    _collect_leaves(leaves);

    const size_type sv_len = sv.size();
    const size_type total_len = size();

    if (pos >= total_len) pos = total_len - 1;

    size_type leaf_idx = leaves.size();
    while (leaf_idx > 0) {
        --leaf_idx;
        if (leaves[leaf_idx].global_start <= pos) break;
    }
    if (leaves.empty() || leaves[leaf_idx].global_start > pos) return npos;

    while (true) {
        const auto& leaf = leaves[leaf_idx];
        const size_type local_pos = (std::min)(pos - leaf.global_start, leaf.len - 1);

        const std::string_view leaf_sv(leaf.data, leaf.len);
        const size_type found = leaf_sv.find_last_of(sv, local_pos);
        if (FL_LIKELY(found != std::string_view::npos)) {
            return leaf.global_start + found;
        }

        // Cross-boundary with previous leaf.
        if (leaf_idx > 0) {
            const auto& prev_leaf = leaves[leaf_idx - 1];
            const size_type from_prev = (std::min)(sv_len - 1, prev_leaf.len);
            const size_type from_curr = (std::min)(sv_len - 1, leaf.len);
            const size_type buf_size = from_prev + from_curr;

            if (buf_size > 0) {
                char boundary_buf[256];
                std::string dynamic_buf;
                char* buf = boundary_buf;
                if (FL_UNLIKELY(buf_size > sizeof(boundary_buf))) {
                    dynamic_buf.resize(buf_size);
                    buf = &dynamic_buf[0];
                }

                std::memcpy(buf, prev_leaf.data + prev_leaf.len - from_prev, from_prev);
                std::memcpy(buf + from_prev, leaf.data, from_curr);

                const std::string_view boundary_sv(buf, buf_size);
                const size_type bfound = boundary_sv.find_last_of(sv);
                if (FL_UNLIKELY(bfound != std::string_view::npos)) {
                    const size_type global = prev_leaf.global_start + prev_leaf.len - from_prev + bfound;
                    if (global <= pos) return global;
                }
            }
        }

        if (leaf_idx == 0) break;
        --leaf_idx;
    }

    return npos;
}

// ============================================================================
// _find_first_not_of_impl -- tree-walking find_first_not_of.
// ============================================================================

inline rope::size_type rope::_find_first_not_of_impl(std::string_view sv, size_type pos) const {
    if (FL_UNLIKELY(pos >= size() || !_storage.root)) return npos;

    std::vector<leaf_info> leaves;
    _collect_leaves(leaves);

    const size_type sv_len = sv.size();

    size_type leaf_idx = 0;
    while (leaf_idx < leaves.size() &&
           leaves[leaf_idx].global_start + leaves[leaf_idx].len <= pos) {
        ++leaf_idx;
    }
    if (FL_UNLIKELY(leaf_idx >= leaves.size())) return npos;

    while (FL_LIKELY(leaf_idx < leaves.size())) {
        const auto& leaf = leaves[leaf_idx];
        const size_type local_pos = (leaf.global_start <= pos)
                                        ? (pos - leaf.global_start)
                                        : 0;

        const std::string_view leaf_sv(leaf.data, leaf.len);
        const size_type found = leaf_sv.find_first_not_of(sv, local_pos);
        if (FL_LIKELY(found != std::string_view::npos)) {
            return leaf.global_start + found;
        }

        // Cross-boundary: check boundary region.
        if (sv_len > 0 && leaf_idx + 1 < leaves.size()) {
            const auto& next_leaf = leaves[leaf_idx + 1];
            const size_type from_curr = (std::min)(sv_len, leaf.len);
            const size_type from_next = (std::min)(sv_len, next_leaf.len);
            const size_type buf_size = from_curr + from_next;

            if (buf_size > 0) {
                char boundary_buf[256];
                std::string dynamic_buf;
                char* buf = boundary_buf;
                if (FL_UNLIKELY(buf_size > sizeof(boundary_buf))) {
                    dynamic_buf.resize(buf_size);
                    buf = &dynamic_buf[0];
                }

                std::memcpy(buf, leaf.data + leaf.len - from_curr, from_curr);
                std::memcpy(buf + from_curr, next_leaf.data, from_next);

                const std::string_view boundary_sv(buf, buf_size);
                const size_type bfound = boundary_sv.find_first_not_of(sv);
                if (FL_UNLIKELY(bfound != std::string_view::npos)) {
                    const size_type global = leaf.global_start + leaf.len - from_curr + bfound;
                    if (global >= pos) return global;
                }
            }
        }

        ++leaf_idx;
    }

    return npos;
}

// ============================================================================
// _find_last_not_of_impl -- tree-walking find_last_not_of.
// ============================================================================

inline rope::size_type rope::_find_last_not_of_impl(std::string_view sv, size_type pos) const {
    if (FL_UNLIKELY(size() == 0 || !_storage.root)) return npos;

    std::vector<leaf_info> leaves;
    _collect_leaves(leaves);

    const size_type sv_len = sv.size();
    const size_type total_len = size();

    if (pos >= total_len) pos = total_len - 1;

    size_type leaf_idx = leaves.size();
    while (leaf_idx > 0) {
        --leaf_idx;
        if (leaves[leaf_idx].global_start <= pos) break;
    }
    if (leaves.empty() || leaves[leaf_idx].global_start > pos) return npos;

    while (true) {
        const auto& leaf = leaves[leaf_idx];
        const size_type local_pos = (std::min)(pos - leaf.global_start, leaf.len - 1);

        const std::string_view leaf_sv(leaf.data, leaf.len);
        const size_type found = leaf_sv.find_last_not_of(sv, local_pos);
        if (FL_LIKELY(found != std::string_view::npos)) {
            return leaf.global_start + found;
        }

        // Cross-boundary with previous leaf.
        if (sv_len > 0 && leaf_idx > 0) {
            const auto& prev_leaf = leaves[leaf_idx - 1];
            const size_type from_prev = (std::min)(sv_len, prev_leaf.len);
            const size_type from_curr = (std::min)(sv_len, leaf.len);
            const size_type buf_size = from_prev + from_curr;

            if (buf_size > 0) {
                char boundary_buf[256];
                std::string dynamic_buf;
                char* buf = boundary_buf;
                if (FL_UNLIKELY(buf_size > sizeof(boundary_buf))) {
                    dynamic_buf.resize(buf_size);
                    buf = &dynamic_buf[0];
                }

                std::memcpy(buf, prev_leaf.data + prev_leaf.len - from_prev, from_prev);
                std::memcpy(buf + from_prev, leaf.data, from_curr);

                const std::string_view boundary_sv(buf, buf_size);
                const size_type bfound = boundary_sv.find_last_not_of(sv);
                if (FL_UNLIKELY(bfound != std::string_view::npos)) {
                    const size_type global = prev_leaf.global_start + prev_leaf.len - from_prev + bfound;
                    if (global <= pos) return global;
                }
            }
        }

        if (leaf_idx == 0) break;
        --leaf_idx;
    }

    return npos;
}

// ============================================================================
// insert() -- O(log N) amortised insertion.
// ============================================================================

inline rope& rope::insert(size_type pos, std::string_view sv) {
    if (FL_UNLIKELY(pos > size())) {
        throw std::out_of_range("rope::insert: position out of range");
    }
    if (FL_UNLIKELY(sv.empty())) return *this;

    if (_is_sro) {
        if (_storage.inline_leaf.size() + sv.size() <= fl::string{}.capacity()) {
            _storage.inline_leaf.insert(pos, sv);
            return *this;
        }
        _transition_sro_to_tree();
    }

    // Split the rope at pos into left and right.
    node_ptr left, right;
    _split_node(_storage.root, pos, left, right);

    // Create a new leaf from sv.
    node_ptr new_leaf = _make_leaf(sv);

    // Concatenate: left + new_leaf + right.
    _storage.root = _balanced_concat(_balanced_concat(std::move(left), std::move(new_leaf)), std::move(right));
    _invalidate_linear_cache();
    return *this;
}

inline rope& rope::insert(size_type pos, const rope& other) {
    if (FL_UNLIKELY(pos > size())) {
        throw std::out_of_range("rope::insert: position out of range");
    }
    if (FL_UNLIKELY(other.empty())) return *this;

    // Handle SRO mode: transition to tree if needed
    if (_is_sro) {
        if (other._is_sro) {
            // Both SRO: try in-place SSO append
            if (_storage.inline_leaf.size() + other._storage.inline_leaf.size() <= fl::string{}.capacity()) {
                _storage.inline_leaf.insert(pos, other._storage.inline_leaf);
                return *this;
            }
        }
        _transition_sro_to_tree();
    }

    node_ptr left, right;
    _split_node(_storage.root, pos, left, right);

    if (other._is_sro) {
        auto other_leaf = std::allocate_shared<leaf_node>(rope_node_alloc{}, other._storage.inline_leaf);
        _storage.root = _balanced_concat(_balanced_concat(std::move(left), std::move(other_leaf)), std::move(right));
    } else {
        _storage.root = _balanced_concat(_balanced_concat(std::move(left), other._storage.root), std::move(right));
    }
    _invalidate_linear_cache();
    return *this;
}

// ============================================================================
// erase() -- O(log N) amortised erasure.
// ============================================================================

inline rope& rope::erase(size_type pos, size_type len) {
    if (_is_sro) {
        _storage.inline_leaf.erase(pos, len);
        return *this;
    }
    if (FL_UNLIKELY(pos >= size())) {
        if (FL_UNLIKELY(pos > size())) {
            throw std::out_of_range("rope::erase: position out of range");
        }
        return *this;  // pos == size() -> nothing to erase.
    }
    if (FL_UNLIKELY(len == 0)) return *this;

    // Clamp len to available characters.
    len = (std::min)(len, size() - pos);

    // Split at pos: left | middle
    node_ptr left, middle;
    _split_node(_storage.root, pos, left, middle);

    // Split middle at len: middle_left | right
    // (middle_left is discarded -- it's the erased portion)
    node_ptr discarded, right;
    _split_node(middle, len, discarded, right);

    // Concatenate: left + right
    _storage.root = _balanced_concat(std::move(left), std::move(right));
    _invalidate_linear_cache();
    return *this;
}

// ============================================================================
// replace() -- O(log N) amortised replacement.
// ============================================================================

inline rope& rope::replace(size_type pos, size_type len, std::string_view sv) {
    if (FL_UNLIKELY(pos > size())) {
        throw std::out_of_range("rope::replace: position out of range");
    }

    if (_is_sro) {
        _storage.inline_leaf.replace(pos, len, sv);
        return *this;
    }

    // Erase then insert.
    erase(pos, len);
    insert(pos, sv);
    return *this;
}

inline rope& rope::replace(size_type pos, size_type len, const rope& other) {
    if (FL_UNLIKELY(pos > size())) {
        throw std::out_of_range("rope::replace: position out of range");
    }

    if (_is_sro && other._is_sro) {
        // Both SRO: do in-place string replace
        _storage.inline_leaf.replace(pos, len, other._storage.inline_leaf);
        return *this;
    }

    erase(pos, len);
    insert(pos, other);
    return *this;
}

// ============================================================================
// Stream output operator for rope.
// ============================================================================

// O(n) due to linearisation.
inline std::ostream& operator<<(std::ostream& os, const rope& r) {
    return os << r.to_std_string();
}

/// Stream input operator.
inline std::istream& operator>>(std::istream& is, rope& r) {
    std::string s;
    is >> s;
    if (is) {
        r = rope(std::string_view(s));
    }
    return is;
}

}  // namespace fl

// ============================================================================
// std::hash specialization for fl::rope.
// ============================================================================

namespace std {
    template<>
    struct hash<fl::rope> {
        [[nodiscard]] size_t operator()(const fl::rope& r) const noexcept {
            return hash<fl::string>()(r.flatten());
        }
    };
}  // namespace std

#endif  // FL_ROPE_HPP


