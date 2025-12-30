#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <concepts>
#include <cstddef>
#include <cassert>
#include <new>

namespace cppkit
{

  // 定义一个线程策略 Concept，方便切换由锁或无锁
  template <typename T>
  concept ThreadPolicy = requires(T t) {
    { t.lock() } -> std::same_as<void>;
    { t.unlock() } -> std::same_as<void>;
  };

  // 默认的互斥锁策略
  struct StdMutexPolicy
  {
    std::mutex m_mutex;
    void lock() { m_mutex.lock(); }
    void unlock() { m_mutex.unlock(); }
  };

  // 单线程策略（无锁，性能最高）
  struct NoLockPolicy
  {
    void lock() {}
    void unlock() {}
  };

  /**
 * @brief 固定大小对象内存池
 * @tparam T 对象类型
 * @tparam ChunkSize 每个大块包含的对象数量 (默认 1024)
 * @tparam LockPolicy 线程安全策略
 */
  template <typename T, size_t ChunkSize = 1024, ThreadPolicy LockPolicy = NoLockPolicy>
  class ObjectPool
  {
  public:
    // C++20 约束: 确保对象大小至少能装得下一个指针，用于空闲链表
    static_assert(sizeof(T) >= sizeof(void*), "Object size must be at least sizeof(void*) to hold free list pointer");

    ObjectPool() = default;

    // 禁止拷贝，允许移动
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

    ~ObjectPool() { clear(); }

    /**
     * @brief 分配一个对象所需的内存
     * @return T* 指向未构造内存的指针
     */
    [[nodiscard]] T* allocate()
    {
      std::lock_guard lock(m_policy);

      // 1. 如果空闲链表中有块，直接弹出
      if (m_free_head)
      {
        T* ptr = reinterpret_cast<T*>(m_free_head);
        m_free_head = m_free_head->next;
        return ptr;
      }

      // 2. 如果没有空闲块，且当前 Chunk 用完了，分配新的 Chunk
      if (m_current_chunk_idx >= ChunkSize)
      {
        allocate_new_chunk();
      }

      // 3. 从当前 Chunk 分配
      // 计算地址：Chunk起始地址 + 偏移量
      // 注意：这里需要处理对齐
      T* ptr = reinterpret_cast<T*>(m_chunks.back().get() + (m_current_chunk_idx * m_aligned_size));
      m_current_chunk_idx++;
      return ptr;
    }

    /**
     * @brief 释放内存（归还给池，不调用析构函数）
     * @param ptr 对象指针
     */
    void deallocate(T* ptr)
    {
      if (!ptr)
        return;

      std::lock_guard lock(m_policy);

      // 将归还的内存块强制转换为 FreeNode
      // 经典的 "Intrusive Linked List" 技术
      auto* node = reinterpret_cast<FreeNode*>(ptr);

      // 头插法插入空闲链表
      node->next = m_free_head;
      m_free_head = node;
    }

    /**
     * @brief 构造对象（分配内存 + 调用构造函数）
     */
    template <typename... Args>
    T* create(Args&&... args)
    {
      T* ptr = allocate();
      if (ptr)
      {
        std::construct_at(ptr, std::forward<Args>(args)...);
      }
      return ptr;
    }

    /**
     * @brief 销毁对象（调用析构函数 + 释放内存）
     */
    void destroy(T* ptr)
    {
      if (ptr)
      {
        std::destroy_at(ptr);
        deallocate(ptr);
      }
    }

    void clear()
    {
      std::lock_guard lock(m_policy);
      m_chunks.clear();
      m_free_head = nullptr;
      m_current_chunk_idx = ChunkSize; // 强制下次分配触发新 Chunk
    }

  private:
    // 嵌入式空闲链表节点
    struct FreeNode
    {
      FreeNode* next;
    };

    // 计算对齐后的大小，满足 T 的对齐要求
    static constexpr size_t m_aligned_size = []()
    {
      size_t size = sizeof(T);
      size_t align = alignof(T);
      // 如果 size 不是 align 的倍数，向上取整
      return (size + align - 1) & ~(align - 1);
    }();

    // 使用 std::byte 进行原始内存管理
    using ChunkType = std::unique_ptr<std::byte[]>;

    void allocate_new_chunk()
    {
      // 分配一大块连续内存
      size_t total_bytes = m_aligned_size * ChunkSize;
      auto new_chunk = std::make_unique<std::byte[]>(total_bytes);

      m_chunks.push_back(std::move(new_chunk));
      m_current_chunk_idx = 0;
    }

    std::vector<ChunkType> m_chunks;        // 存储所有申请的大块内存
    FreeNode* m_free_head = nullptr;        // 空闲链表头指针
    size_t m_current_chunk_idx = ChunkSize; // 当前 Chunk 使用到的索引

    // 假如是多线程环境，加上 [[no_unique_address]] 优化空结构体
    [[no_unique_address]] LockPolicy m_policy;
  };

} // namespace cppkit