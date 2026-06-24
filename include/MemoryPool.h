#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace exchange {

// acquire() returns a pointer or nullptr on exhaustion.
// release() returns a node to the free list.
template <typename T, std::size_t MAX>
class MemoryPool {
public:
    MemoryPool();
    ~MemoryPool();

    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    T* acquire();
    void release(T* node);

    [[nodiscard]] std::size_t available() const noexcept;
    [[nodiscard]] std::size_t capacity()  const noexcept { return MAX; }

private:
    alignas(T) std::byte storage_[MAX * sizeof(T)];
    std::vector<T*> free_list_;
};

} 
