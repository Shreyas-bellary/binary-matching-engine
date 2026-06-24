// provides a fixed-capacity memory pool for objects, for better latency by avoiding heap allocations during runtime.
#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace exchange {

template <typename T, std::size_t MAX>
class MemoryPool {
    static_assert(MAX > 0, "MemoryPool requires a non-zero capacity");

private:
    alignas(T) std::byte storage_[MAX * sizeof(T)];
    std::array<T*, MAX> free_list_ {};
    std::size_t top_ = 0;

    [[nodiscard]] T* slot(std::size_t index) noexcept {
        return std::launder(
            reinterpret_cast<T*>(storage_ + index * sizeof(T)));
    }

public:
    using value_type = T;
    
    // constructor
    MemoryPool() noexcept {
        for (std::size_t i = 0; i < MAX; ++i) {
            free_list_[i] = slot(MAX - 1 - i);
        }
        top_ = MAX;
    }

    // destructor
    ~MemoryPool() = default;

    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&)                 = delete;
    MemoryPool& operator=(MemoryPool&&)      = delete;

    // returns raw storage for one T, or nullptr if the pool is exhausted
    [[nodiscard]] T* acquire() noexcept {
        if (top_ == 0) {
            return nullptr;
        }
        return free_list_[--top_];
    }

    //returns a slot to the pool
    void release(T* node) noexcept {
        assert(node != nullptr && "MemoryPool::release called with nullptr");
        assert(owns(node) && "MemoryPool::release called with foreign pointer");
        assert(top_ < MAX && "MemoryPool::release overflow (double free?)");
        free_list_[top_++] = node;
    }

    template <typename... Args>
    [[nodiscard]] T* construct(Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        T* p = acquire();
        if (p == nullptr) {
            return nullptr;
        }
        return ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
    }

    void destroy(T* node) noexcept {
        assert(node != nullptr && "MemoryPool::destroy called with nullptr");
        node->~T();
        release(node);
    }

    // number of slots currently available
    [[nodiscard]] std::size_t available() const noexcept { return top_; }
    [[nodiscard]] std::size_t capacity()  const noexcept { return MAX; }
    [[nodiscard]] bool empty()            const noexcept { return top_ == 0; }

    // true if `node` points at a valid, aligned slot within this pool
    [[nodiscard]] bool owns(const T* node) const noexcept {
        const auto* p = reinterpret_cast<const std::byte*>(node);
        if (p < storage_ || p >= storage_ + sizeof(storage_)) {
            return false;
        }
        const std::size_t offset =
            static_cast<std::size_t>(p - storage_);
        return (offset % sizeof(T)) == 0;
    }

    };

} 
