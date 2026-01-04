#pragma once

#include <vector>
#include <memory>
#include <concepts>
#include <mutex>
#include <atomic>
#include <utility>

namespace cppkit
{
    // 无锁
    struct NoLock
    {
        void lock() noexcept
        {
        }

        void unlock() noexcept
        {
        }
    };

    // 自旋锁
    struct SpinLock
    {
        std::atomic_flag flag = ATOMIC_FLAG_INIT;

        void lock() noexcept
        {
            while (flag.test_and_set(std::memory_order_acquire))
            {
                flag.wait(true, std::memory_order_relaxed);
            }
        }

        void unlock() noexcept
        {
            flag.clear(std::memory_order_release);
            flag.notify_one();
        }
    };

    // 锁概念
    template <typename L>
    concept Lockable = requires(L l)
    {
        { l.lock() } -> std::same_as<void>;
        { l.unlock() } -> std::same_as<void>;
    };

    // 可池化对象概念
    template <typename T>
    concept Poolable = std::is_object_v<T> && !std::is_const_v<T>;

    template <Poolable T, size_t ChunkSize = 1024, Lockable LockPolicy = NoLock>
    class MemoryPool
    {
    public:
        using value_type = T;

        MemoryPool() = default;
        ~MemoryPool() { clear(); }

        MemoryPool(const MemoryPool&) = delete;
        MemoryPool& operator=(const MemoryPool&) = delete;

        MemoryPool(MemoryPool&& other) noexcept
        {
            std::scoped_lock lock(other.lock_);
            chunks_ = std::move(other.chunks_);
            free_list_ = other.free_list_;
            other.free_list_ = nullptr;
        }

        template <typename... Args>
        [[nodiscard]] T* create(Args&&... args)
        {
            void* mem = allocate_raw();

            return std::construct_at(static_cast<T*>(mem), std::forward<Args>(args)...);
        }

        void destroy(T* ptr)
        {
            if (ptr)
            {
                std::destroy_at(ptr);
                deallocateRaw(ptr);
            }
        }

        void clear()
        {
            std::scoped_lock lock(lock_);
            chunks_.clear();
            free_list_ = nullptr;
        }

    private:
        union Node
        {
            T element;
            Node* next;

            Node()
            {
            }

            ~Node()
            {
            }
        };

        [[no_unique_address]] LockPolicy lock_;

        std::vector<std::unique_ptr<Node[]>> chunks_;
        Node* free_list_ = nullptr;

        [[nodiscard]] void* allocate_raw()
        {
            std::scoped_lock lock(lock_);

            if (free_list_ == nullptr)
            {
                allocateNewChunk();
            }

            Node* current = free_list_;
            free_list_ = current->next;
            return current;
        }

        void deallocateRaw(void* ptr)
        {
            std::scoped_lock lock(lock_);

            Node* node = static_cast<Node*>(ptr);
            node->next = free_list_;
            free_list_ = node;
        }

        void allocateNewChunk()
        {
            auto chunk = std::make_unique<Node[]>(ChunkSize);
            for (size_t i = 0; i < ChunkSize - 1; ++i)
            {
                chunk[i].next = &chunk[i + 1];
            }
            chunk[ChunkSize - 1].next = free_list_;
            free_list_ = &chunk[0];
            chunks_.push_back(std::move(chunk));
        }
    };
} // namespace cppkit
