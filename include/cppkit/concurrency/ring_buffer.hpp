#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>
#include <new>
#include <bit>
#include <thread>

namespace cppkit::concurrency
{
    template <typename T>
    class RingBuffer
    {
    public:
        // Capacity 必须是 2 的幂
        explicit RingBuffer(size_t capacity) 
            : _capacity(capacity), _mask(capacity - 1)
        {
            if (!std::has_single_bit(capacity)) {
                throw std::invalid_argument("Capacity must be power of 2");
            }

            _buffer = new Slot[capacity];
            
            // 初始化 Sequence 数组
            // 初始状态：slot 0 的票号是 0，slot 1 的票号是 1...
            // 这样 Producer (tail=0) 只能去写 slot 0
            for (size_t i = 0; i < capacity; ++i) {
                _buffer[i].sequence.store(i, std::memory_order_relaxed);
            }
        }

        ~RingBuffer()
        {
            delete[] _buffer;
        }

        // 禁止拷贝
        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        bool push(const T& data)
        {
            Slot* slot;
            size_t pos = _tail.load(std::memory_order_relaxed);

            while (true)
            {
                // 1. 找到当前 Tail 对应的槽位
                slot = &_buffer[pos & _mask];
                
                // 2. 读取该槽位的序列号
                size_t seq = slot->sequence.load(std::memory_order_acquire);
                
                // 3. 判断票号
                intptr_t diff = (intptr_t)seq - (intptr_t)pos;

                // Case A: diff == 0
                // 说明 sequence 就是这一轮的 pos。
                // 也就是：槽位是空的，且正好轮到我们在 pos 这个位置写入。
                if (diff == 0)
                {
                    // 尝试抢占 Tail：把 Tail 推进到 pos + 1
                    if (_tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    {
                        // 抢占成功！现在这个槽位归我了。
                        // 写入数据
                        slot->data = data;
                        
                        // 关键步骤：更新序列号
                        // 将序列号设为 pos + 1。
                        // 这表示：这个槽位现在装满数据了，等待 Consumer (head + 1) 来读。
                        slot->sequence.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                    // CAS 失败：说明有别的线程抢先推走了 Tail，重试。
                }
                // Case B: diff < 0 
                // 说明 seq < pos。这意味着槽位里的序列号是旧的。
                // 也就是：队列满了，Consumer 还没来得及把这一圈的数据读走。
                else if (diff < 0)
                {
                    return false; // Queue Full
                }
                // Case C: diff > 0
                // 说明 seq > pos。这意味着 Tail 指针落后了。
                // 比如另一个线程已经 push 完并修改了 sequence，或者我们的 pos 读到了旧值。
                else
                {
                    // 重新加载最新的 Tail，再次尝试
                    pos = _tail.load(std::memory_order_relaxed);
                }
            }
        }

        std::optional<T> pop()
        {
            Slot* slot;
            size_t pos = _head.load(std::memory_order_relaxed);

            while (true)
            {
                // 1. 找到 Head 对应的槽位
                slot = &_buffer[pos & _mask];
                
                // 2. 读取序列号
                size_t seq = slot->sequence.load(std::memory_order_acquire);
                
                // 3. 判断票号
                // 注意：Consumer 期望的票号是 pos + 1 (因为 Push 完后变成了 pos + 1)
                intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

                // Case A: diff == 0
                // 说明 seq == pos + 1。
                // 数据已就绪，且轮到我们在 pos 位置读取。
                if (diff == 0)
                {
                    // 尝试抢占 Head
                    if (_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    {
                        // 抢占成功
                        T data = slot->data; // 读取数据
                        
                        // 关键步骤：更新序列号
                        // 将序列号变成 pos + capacity。
                        // 这表示：这一圈的数据我读完了，下一圈（pos + capacity）Producer 可以来写了。
                        slot->sequence.store(pos + _capacity, std::memory_order_release);
                        return data;
                    }
                }
                // Case B: diff < 0
                // 说明 seq < pos + 1。也就是 seq 还是 pos (或者更早)。
                // 意味着 Producer 还没写完这个位置（或者队列是空的）。
                else if (diff < 0)
                {
                    return std::nullopt; // Queue Empty
                }
                // Case C: diff > 0
                // Head 落后了，重试
                else
                {
                    pos = _head.load(std::memory_order_relaxed);
                }
            }
        }

        size_t capacity() const
        {
            return _capacity;
        }

        size_t size() const
        {
            size_t head = _head.load(std::memory_order_acquire);
            size_t tail = _tail.load(std::memory_order_acquire);
            return tail - head;
        }

    private:
        struct Slot
        {
            std::atomic<size_t> sequence;
            T data;
        };

        // 避免伪共享 (False Sharing) 的对齐常量
#ifdef __cpp_lib_hardware_interference_size
        static constexpr size_t _cache_line_size = std::hardware_destructive_interference_size;
#else
        static constexpr size_t _cache_line_size = 64;
#endif

        // 数据区
        Slot* _buffer;
        size_t _capacity;
        size_t _mask;

        // 填充字节，防止 cache line 干扰
        char _pad0[_cache_line_size];
        
        // Head 和 Tail 必须在不同的 Cache Line 上，否则多核竞争会非常慢
        std::atomic<size_t> _head{0};
        
        char _pad1[_cache_line_size];
        
        std::atomic<size_t> _tail{0};
        
        char _pad2[_cache_line_size];
    };
}