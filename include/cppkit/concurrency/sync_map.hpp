#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <optional>
#include <functional>
#include <vector>

namespace cppkit::concurrency
{
    template <typename K, typename V>
    class SyncMap
    {
        struct Entry;
        using ValuePtr = std::shared_ptr<V>;
        using MapType = std::unordered_map<K, std::shared_ptr<Entry>>;

        static const ValuePtr& getExpunged()
        {
            static ValuePtr expunged = std::make_shared<V>();
            return expunged;
        }

        static bool isExpunged(const ValuePtr& p)
        {
            return p && p == getExpunged();
        }

        struct Entry
        {
            std::atomic<ValuePtr> p;

            explicit Entry(ValuePtr v) : p(v) {}

            ValuePtr load() const
            {
                ValuePtr ptr = p.load(std::memory_order_acquire);
                if (ptr == nullptr || isExpunged(ptr))
                {
                    return nullptr;
                }
                return ptr;
            }

            bool tryStore(ValuePtr i)
            {
                ValuePtr expected = p.load(std::memory_order_acquire);
                while (true)
                {
                    if (expected == nullptr) // 已被删除，不能直接存储
                    {
                        return false;
                    }
                    if (isExpunged(expected)) // 已被驱逐，需要升级到dirty
                    {
                        return false;
                    }
                    if (p.compare_exchange_weak(expected, i,
                                                std::memory_order_release,
                                                std::memory_order_acquire))
                    {
                        return true;
                    }
                }
            }

            // 尝试原子加载并删除值
            ValuePtr tryLoadAndDelete()
            {
                ValuePtr expected = p.load(std::memory_order_acquire);
                while (true)
                {
                    if (expected == nullptr || isExpunged(expected))
                    {
                        return nullptr;
                    }
                    if (p.compare_exchange_weak(expected, ValuePtr(nullptr),
                                                std::memory_order_release,
                                                std::memory_order_acquire))
                    {
                        return expected;
                    }
                }
            }

            // 尝试将expunged状态转换为nullptr
            bool unexpungeLocked()
            {
                ValuePtr expected = getExpunged();
                ValuePtr desired = nullptr;
                return p.compare_exchange_strong(expected, desired,
                                                 std::memory_order_relaxed,
                                                 std::memory_order_relaxed);
            }

            // 在持有锁时存储值
            void storeLocked(ValuePtr i)
            {
                p.store(i, std::memory_order_release);
            }

            // 尝试将Entry标记为expunged
            bool tryExpungeLocked()
            {
                ValuePtr expected = nullptr;
                ValuePtr expunged_val = getExpunged();

                // 尝试将nullptr改为expunged
                if (p.compare_exchange_strong(expected, expunged_val,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed))
                {
                    return true;  // 成功设置为expunged
                }

                if (isExpunged(expected))
                {
                    return true;  // 已经是expunged
                }
                return false;
            }

            // 检查Entry是否有有效值
            bool hasValue() const
            {
                ValuePtr ptr = p.load(std::memory_order_acquire);
                return ptr != nullptr && !isExpunged(ptr);
            }
        };

        struct ReadOnly
        {
            std::shared_ptr<MapType> m;
            bool amended = false;  // 是否有dirty map
        };

        std::mutex mu;

        std::atomic<std::shared_ptr<ReadOnly>> read;

        std::shared_ptr<MapType> dirty;

        int misses = 0;

    public:
        SyncMap()
        {
            auto initialRead = std::make_shared<ReadOnly>();
            initialRead->m = std::make_shared<MapType>();
            initialRead->amended = false;
            read.store(initialRead, std::memory_order_release);
        }

        std::pair<ValuePtr, bool> Load(const K& key)
        {
            auto r = read.load(std::memory_order_acquire);
            auto it = r->m->find(key);

            if (it == r->m->end() && r->amended)
            {
                // read中不存在，但有dirty map，需要检查
                std::lock_guard lock(mu);
                // 双重检查：可能另一个线程已经提升dirty
                r = read.load(std::memory_order_relaxed);
                it = r->m->find(key);

                if (it == r->m->end() && r->amended)
                {
                    if (dirty)
                    {
                        auto dit = dirty->find(key);
                        if (dit != dirty->end())
                        {
                            ValuePtr val = dit->second->load();
                            missLocked();
                            return {val, val != nullptr};
                        }
                    }
                    missLocked();
                    return {nullptr, false};
                }
            }

            if (it != r->m->end())
            {
                ValuePtr val = it->second->load();
                return {val, val != nullptr};
            }

            return {nullptr, false};
        }

        void Store(const K& key, V value)
        {
            ValuePtr valPtr = std::make_shared<V>(std::move(value));

            auto r = read.load(std::memory_order_acquire);
            auto it = r->m->find(key);

            // 先尝试无锁更新
            if (it != r->m->end() && it->second->tryStore(valPtr))
            {
                return;
            }

            // 需要加锁的情况：
            // 1. key不在read中
            // 2. key在read中但tryStore失败（被删除或驱逐）
            std::lock_guard lock(mu);
            r = read.load(std::memory_order_relaxed);
            it = r->m->find(key);

            if (it != r->m->end())
            {
                // Key 在 read 中
                if (it->second->unexpungeLocked())
                {
                    // 之前是expunged，需要添加到dirty
                    if (!dirty)
                    {
                        dirty = std::make_shared<MapType>();
                    }
                    (*dirty)[key] = it->second;
                }
                it->second->storeLocked(valPtr);
            }
            else
            {
                // Key 不在 read 中
                bool inDirty = dirty && dirty->find(key) != dirty->end();

                if (inDirty)
                {
                    // 在 dirty 中直接更新
                    (*dirty)[key]->storeLocked(valPtr);
                }
                else
                {
                    // 既不在read也不在dirty，需要创建新Entry
                    if (!r->amended)
                    {
                        // 首次添加新key，需要创建dirty map
                        dirtyLocked(r);
                        auto nextRead = std::make_shared<ReadOnly>(*r);
                        nextRead->amended = true;
                        read.store(nextRead, std::memory_order_release);
                    }

                    if (!dirty)
                    {
                        dirty = std::make_shared<MapType>();
                    }
                    (*dirty)[key] = std::make_shared<Entry>(valPtr);
                }
            }
        }

        void Delete(const K& key)
        {
            LoadAndDelete(key);
        }

        std::pair<ValuePtr, bool> LoadAndDelete(const K& key)
        {
            auto r = read.load(std::memory_order_acquire);
            auto it = r->m->find(key);

            if (it == r->m->end() && r->amended)
            {
                std::lock_guard lock(mu);
                r = read.load(std::memory_order_relaxed);
                it = r->m->find(key);

                if (it == r->m->end() && r->amended)
                {
                    if (dirty)
                    {
                        auto dit = dirty->find(key);
                        if (dit != dirty->end())
                        {
                            ValuePtr val = dit->second->tryLoadAndDelete();
                            missLocked();
                            return {val, val != nullptr};
                        }
                    }
                    missLocked();
                    return {nullptr, false};
                }
            }

            if (it != r->m->end())
            {
                ValuePtr old = it->second->tryLoadAndDelete();
                return {old, old != nullptr};
            }

            return {nullptr, false};
        }

        void Range(std::function<bool(const K&, const V&)> f)
        {
            auto r = read.load(std::memory_order_acquire);

            if (r->amended)
            {
                std::lock_guard lock(mu);
                r = read.load(std::memory_order_relaxed);
                if (r->amended)
                {
                    // 提升 dirty 为 read
                    auto newRead = std::make_shared<ReadOnly>();
                    newRead->m = dirty;
                    newRead->amended = false;

                    read.store(newRead, std::memory_order_release);
                    dirty = nullptr;
                    misses = 0;
                    r = newRead;
                }
            }

            for (const auto& [k, e] : *(r->m))
            {
                ValuePtr val = e->load();
                if (val != nullptr)
                {
                    if (!f(k, *val))
                    {
                        return;
                    }
                }
            }
        }

    private:
        void missLocked()
        {
            misses++;

            if (!dirty)
            {
                return;
            }

            if (misses < static_cast<int>(dirty->size()))
            {
                return;
            }

            // Miss次数过多，将dirty提升为read
            auto newRead = std::make_shared<ReadOnly>();
            newRead->m = dirty;
            newRead->amended = false;

            read.store(newRead, std::memory_order_release);

            dirty = nullptr;
            misses = 0;
        }

        void dirtyLocked(const std::shared_ptr<ReadOnly>& r)
        {
            if (dirty)
            {
                return;
            }

            dirty = std::make_shared<MapType>();

            for (const auto& [k, e] : *(r->m))
            {
                // 如果tryExpungeLocked返回false，说明entry有有效值，需要保留在dirty
                if (e->tryExpungeLocked())
                {
                    continue;
                }
                // Entry有值，添加到dirty
                (*dirty)[k] = e;
            }
        }
    };
} // namespace cppkit::concurrency
