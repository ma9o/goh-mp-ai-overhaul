#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace goh::store {

// Heap-backed dynamically growable table replacing fixed-size static std::array
// globals. Keeps zero bytes in PE .data (only a 24-byte std::vector header in
// .bss), grows automatically past InitialCap during large battles up to MaxCap,
// and supports drop-in operator[]/fill()/iteration for existing modules/tests.
template <class T, std::size_t InitialCap = 64, std::size_t MaxCap = 16384> class GrowableTable {
public:
    using value_type = T;
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;

    static constexpr std::size_t initial_capacity = InitialCap;
    static constexpr std::size_t max_capacity = MaxCap;

    T& operator[](std::size_t index) {
        ensure_index(index);
        return items_[index];
    }

    const T& at_or_default(std::size_t index) const noexcept {
        static const T empty_value{};
        return index < items_.size() ? items_[index] : empty_value;
    }

    // Appends a fresh default-constructed slot when all current slots are busy,
    // up to MaxCap. Returns nullptr only if MaxCap is reached.
    T* append_slot() {
        if (items_.size() >= MaxCap) return nullptr;
        if (items_.empty() && InitialCap > 0) {
            items_.resize(std::min(InitialCap, MaxCap));
            return &items_[0];
        }
        items_.emplace_back();
        return &items_.back();
    }

    void ensure_min_size(std::size_t count) {
        const std::size_t target = std::min(count, MaxCap);
        if (items_.size() < target) items_.resize(target);
    }

    void fill(const T& value) {
        for (auto& item : items_)
            item = value;
    }

    // Releases all backing heap memory (used on unload / scene reset).
    void clear() {
        items_.clear();
    }

    void release() {
        std::vector<T>{}.swap(items_);
    }

    std::size_t size() const noexcept {
        return items_.size();
    }

    bool empty() const noexcept {
        return items_.empty();
    }

    T* data() noexcept {
        return items_.data();
    }

    const T* data() const noexcept {
        return items_.data();
    }

    iterator begin() noexcept {
        return items_.begin();
    }

    iterator end() noexcept {
        return items_.end();
    }

    const_iterator begin() const noexcept {
        return items_.begin();
    }

    const_iterator end() const noexcept {
        return items_.end();
    }

private:
    void ensure_index(std::size_t index) {
        if (index < items_.size()) return;
        std::size_t next = items_.empty() ? std::max<std::size_t>(InitialCap, index + 1) : items_.size();
        while (next <= index)
            next = next ? next * 2 : 16;
        if (next > MaxCap && index < MaxCap) next = MaxCap;
        if (index >= next) next = index + 1;
        items_.resize(next);
    }

    std::vector<T> items_;
};

// Heap-backed sparse map replacing 65,536-entry static direct-indexed arrays
// (e.g. actor-ID side tables in tank_auto_repair). Occupies ~40 bytes in .bss
// and zero bytes in PE .data, with O(1) lookup and full 32-bit/16-bit key range.
template <class T, class Key = std::uint32_t> class SparseTable {
public:
    using key_type = Key;
    using mapped_type = T;

    T& operator[](Key key) {
        return map_[key];
    }

    T* find(Key key) noexcept {
        auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }

    const T* find(Key key) const noexcept {
        auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }

    void erase(Key key) {
        map_.erase(key);
    }

    // Compatible with std::array::fill({}) reset calls.
    void fill(const T& /*value*/) {
        map_.clear();
    }

    void clear() {
        map_.clear();
    }

    void release() {
        std::unordered_map<Key, T>{}.swap(map_);
    }

    std::size_t size() const noexcept {
        return map_.size();
    }

    bool empty() const noexcept {
        return map_.empty();
    }

    template <class Fn> void for_each(Fn&& fn) {
        for (auto& kv : map_)
            fn(kv.first, kv.second);
    }

    template <class Fn> void for_each(Fn&& fn) const {
        for (const auto& kv : map_)
            fn(kv.first, kv.second);
    }

private:
    std::unordered_map<Key, T> map_;
};

// Pointer-stable heap-backed table (lazily backed by std::deque). Supports
// non-movable elements (e.g., structs containing std::atomic), guarantees
// pointers into existing slots never invalidate on growth, performs zero
// allocations at static-init / DllMain time (constexpr noexcept default ctor),
// and occupies 8 bytes in .bss with zero bytes in PE .data.
template <class T, std::size_t InitialCap = 64, std::size_t MaxCap = 16384> class StableTable {
public:
    using value_type = T;

    template <bool Const> struct Iter {
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = std::conditional_t<Const, const T*, T*>;
        using reference = std::conditional_t<Const, const T&, T&>;
        using iterator_category = std::random_access_iterator_tag;
        using DequePtr = std::conditional_t<Const, const std::deque<T>*, std::deque<T>*>;

        DequePtr buf{};
        std::size_t idx{};

        reference operator*() const noexcept {
            return (*buf)[idx];
        }
        pointer operator->() const noexcept {
            return &(*buf)[idx];
        }
        Iter& operator++() noexcept {
            ++idx;
            return *this;
        }
        Iter operator++(int) noexcept {
            Iter tmp = *this;
            ++idx;
            return tmp;
        }
        Iter& operator--() noexcept {
            --idx;
            return *this;
        }
        Iter& operator+=(difference_type n) noexcept {
            idx = static_cast<std::size_t>(static_cast<difference_type>(idx) + n);
            return *this;
        }
        Iter operator+(difference_type n) const noexcept {
            Iter tmp = *this;
            tmp += n;
            return tmp;
        }
        difference_type operator-(const Iter& o) const noexcept {
            return static_cast<difference_type>(idx) - static_cast<difference_type>(o.idx);
        }
        bool operator==(const Iter& o) const noexcept {
            return buf == o.buf && idx == o.idx;
        }
        bool operator!=(const Iter& o) const noexcept {
            return !(*this == o);
        }
        bool operator<(const Iter& o) const noexcept {
            return idx < o.idx;
        }
    };

    using iterator = Iter<false>;
    using const_iterator = Iter<true>;

    constexpr StableTable() noexcept = default;

    T& operator[](std::size_t index) {
        ensure_index(index);
        return (*items_)[index];
    }

    T* append_slot() {
        auto& q = ensure_deque();
        if (q.size() >= MaxCap) return nullptr;
        if (q.empty() && InitialCap > 0) {
            const std::size_t target = std::min(InitialCap, MaxCap);
            while (q.size() < target)
                q.emplace_back();
            return &q[0];
        }
        q.emplace_back();
        return &q.back();
    }

    void fill(const T& value) {
        if (!items_) return;
        for (auto& item : *items_)
            item = value;
    }

    void clear() noexcept {
        if (items_) items_->clear();
    }

    void release() noexcept {
        items_.reset();
    }

    std::size_t size() const noexcept {
        return items_ ? items_->size() : 0;
    }

    bool empty() const noexcept {
        return !items_ || items_->empty();
    }

    iterator begin() noexcept {
        return {items_.get(), 0};
    }

    iterator end() noexcept {
        return {items_.get(), size()};
    }

    const_iterator begin() const noexcept {
        return {items_.get(), 0};
    }

    const_iterator end() const noexcept {
        return {items_.get(), size()};
    }

private:
    std::deque<T>& ensure_deque() {
        if (!items_) items_ = std::make_unique<std::deque<T>>();
        return *items_;
    }

    void ensure_index(std::size_t index) {
        auto& q = ensure_deque();
        if (index < q.size()) return;
        std::size_t next = q.empty() ? std::max<std::size_t>(InitialCap, index + 1) : q.size();
        while (next <= index)
            next = next ? next * 2 : 16;
        if (next > MaxCap && index < MaxCap) next = MaxCap;
        if (index >= next) next = index + 1;
        while (q.size() < next)
            q.emplace_back();
    }

    std::unique_ptr<std::deque<T>> items_{};
};

template <class T, std::size_t N> inline T* try_append_slot(std::array<T, N>&) noexcept {
    return nullptr;
}

template <class Table> inline auto try_append_slot(Table& table) -> decltype(table.append_slot()) {
    try {
        return table.append_slot();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

// 8-byte heap-owned singleton box with zero bytes in PE .data. Ideal for
// lock-free worker-thread tables that must be allocated once before hooks
// enable and freed only after all workers drain.
template <class T> class LazyBox {
public:
    T& get() {
        if (!ptr_) ptr_ = std::make_unique<T>();
        return *ptr_;
    }

    T* try_get() noexcept {
        return ptr_.get();
    }

    const T* try_get() const noexcept {
        return ptr_.get();
    }

    void allocate() {
        if (!ptr_) ptr_ = std::make_unique<T>();
    }

    void reset_contents() {
        if (ptr_) *ptr_ = T{};
    }

    void release() noexcept {
        ptr_.reset();
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(ptr_);
    }

private:
    std::unique_ptr<T> ptr_;
};

// Fixed-size lock-free atomic array stored on the heap (8 bytes in .bss, zero
// bytes in PE .data). Allocate once before worker hooks enable; worker reads
// use try_get() to stay strictly wait-free with zero allocations.
template <class T, std::size_t N> class HeapAtomicArray {
public:
    using Array = std::array<std::atomic<T>, N>;

    void allocate() {
        if (!ptr_) {
            ptr_ = std::make_unique<Array>();
            for (auto& slot : *ptr_)
                slot.store(T{}, std::memory_order_relaxed);
        }
    }

    Array* try_get() noexcept {
        return ptr_.get();
    }

    const Array* try_get() const noexcept {
        return ptr_.get();
    }

    std::atomic<T>& operator[](std::size_t index) {
        allocate();
        return (*ptr_)[index];
    }

    constexpr std::size_t size() const noexcept {
        return N;
    }

    void clear_values() noexcept {
        if (!ptr_) return;
        for (auto& slot : *ptr_)
            slot.store(T{}, std::memory_order_relaxed);
    }

    void release() noexcept {
        ptr_.reset();
    }

private:
    std::unique_ptr<Array> ptr_;
};

} // namespace goh::store
