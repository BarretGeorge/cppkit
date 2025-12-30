#include "cppkit/concurrency/coroutine.hpp"
#include "cppkit/concurrency/ring_buffer.hpp"
#include <iostream>
#include <thread>

using namespace cppkit::concurrency;

// 定义一个加法协程
Task<int> asyncAdd(const int a, const int b)
{
  // 模拟异步操作（让出 CPU）
  co_await yield();
  co_return a + b;
}

// 定义一个简单的循环协程
Task<void> asyncPrint()
{
  for (int i = 0; i < 3; ++i)
  {
    printf("Simple Task: iteration %d\n", i);
    co_await yield();
  }
}

// 定义一个根协程，用来编排业务逻辑
Task<void> mainAsyncLogic()
{
  std::cout << "[Root] Starting application logic..." << std::endl;

  // 1. 启动打印任务 (Fire and forget, 或者你可以保存它 co_await 它)
  auto printer = asyncPrint();
  // 把它扔给调度器独立运行
  printer.schedule_on(*Scheduler::current());

  // 2. 启动加法任务并等待结果
  std::cout << "[Root] Waiting for addition..." << std::endl;

  // 这里使用 co_await，它会自动：
  // 1. 挂起当前 mainAsyncLogic
  // 2. 将 async_add 加入调度队列
  // 3. 当 async_add 完成后，恢复 mainAsyncLogic 并拿到返回值
  const int result = co_await asyncAdd(5, 7);

  printf("[Root] Addition Result: %d\n", result);

  std::cout << "[Root] Logic finished." << std::endl;
  co_return;
}

void testRingBuffer()
{
  using namespace cppkit::concurrency;

  cppkit::concurrency::RingBuffer<int> ringBuffer(8);

  constexpr size_t workSize = 10000;

  // 并发测试
  std::thread writeGroup[workSize];
  std::thread readGroup[workSize];

  for (size_t i = 0; i < workSize; ++i)
  {
    writeGroup[i] = std::thread(
        [i, &ringBuffer]()
        {
          while (!ringBuffer.push(static_cast<int>(i)))
          {
            std::this_thread::yield();
          }
        });

    readGroup[i] = std::thread(
        [i, &ringBuffer]()
        {
          std::optional<int> value;
          while (!(value = ringBuffer.pop()))
          {
            std::this_thread::yield();
          }
        });
  }

  for (size_t i = 0; i < workSize; ++i)
  {
    writeGroup[i].join();
    readGroup[i].join();
  }


  std::cout << "RingBuffer test completed." << std::endl;
  std::cout << ringBuffer.size() << std::endl;
}

int main()
{
  Scheduler scheduler;

  // 1. 创建根任务
  auto root = mainAsyncLogic();

  // 2. 将根任务放入调度器
  root.schedule_on(scheduler);

  // 3. 运行调度器
  // 调度器会一直运行，直到所有任务（包括 root 和 printer）都执行完毕
  scheduler.run();

  std::cout << "Scheduler finished. Exiting main." << std::endl;

  testRingBuffer();
  return 0;
}
