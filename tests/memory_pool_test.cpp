#include "cppkit/memory_pool.hpp"
#include <iostream>
#include <thread>
#include <cstring>

struct Student
{
    uint64_t id{};
    char name[50]{};
    float score{};

    Student() = default;

    Student(const uint64_t _id, const char* _name, const float _score)
        : id(_id), score(_score)
    {
        std::strncpy(name, _name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
};

// 单线程测试
void test_single_thread()
{
    cppkit::MemoryPool<Student> pool;
    Student* stu1 = pool.create();

    // 打印内存地址
    std::cout << "Student 1 Address: " << stu1 << std::endl;
    pool.destroy(stu1);

    Student* stu2 = pool.create(1002, "Bob", 88.0f);
    std::cout << "Student 2 Address: " << stu2 << std::endl;
    pool.destroy(stu2);
}

// 多线程测试
void test_multi_thread()
{
    cppkit::MemoryPool<Student, 1024, std::mutex> pool;
    auto worker = [&pool](const int thread_id)
    {
        for (int i = 0; i < 100; ++i)
        {
            Student* stu = pool.create(thread_id, "ThreadStudent", 75.0f + static_cast<float>(i));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            pool.destroy(stu);
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads)
    {
        t.join();
    }
}

int main()
{
    std::cout << sizeof(Student) << std::endl;
    test_single_thread();
    test_multi_thread();
}
