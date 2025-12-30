#include "cppkit/object_pool.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <list>

struct Packet
{
  int id;
  double timestamp;
  char buffer[64];

  Packet(int i, double t) : id(i), timestamp(t)
  {
    // std::cout << "Packet constructed: " << id << "\n";
  }

  ~Packet()
  {
    // std::cout << "Packet destroyed: " << id << "\n";
  }
};

int main()
{
  using namespace cppkit;

  // 1. 基本用法：高性能对象池
  std::cout << "--- Testing Raw ObjectPool ---\n";

  // 定义一个线程安全的池
  ObjectPool<Packet, 100, StdMutexPolicy> packetPool;

  // 创建对象
  Packet* p1 = packetPool.create(1, 100.1);
  Packet* p2 = packetPool.create(2, 100.2);

  std::cout << "P1 addr: " << p1 << "\n";
  std::cout << "P2 addr: " << p2 << "\n";

  // 销毁对象 (此时内存回到池中，并没有还给操作系统)
  packetPool.destroy(p1);

  // 再次分配，应该复用 p1 的地址 (LIFO 行为)
  Packet* p3 = packetPool.create(3, 100.3);
  std::cout << "P3 addr: " << p3 << " (Should be same as P1)\n";

  packetPool.destroy(p2);
  packetPool.destroy(p3);
}