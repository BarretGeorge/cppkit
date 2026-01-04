#pragma once

#include "io.hpp"
#include "cppkit/define.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <functional>
#include <mutex>

namespace cppkit::io
{
  class File
  {
    [[nodiscard]] bool checkPermission(std::filesystem::perms mask) const;

  public:
    explicit File(const std::string& path) : _path(std::filesystem::path(path))
    {
    }

    File() = delete;

    File(const File&) = delete;

    File(File&&) noexcept = default;

    File& operator=(const File&) = delete;

    File& operator=(File&&) noexcept = default;

    ~File() = default;

    // 获取文件大小，单位字节
    [[nodiscard]] size_t size() const;

    // 权限检查 是否可读
    [[nodiscard]] bool canRead() const;

    // 权限检查 是否可写
    [[nodiscard]] bool canWrite() const;

    // 权限检查 是否可执行
    [[nodiscard]] bool canExecute() const;

    // 创建新文件，若文件已存在则返回 false
    [[nodiscard]] bool createNewFile() const;

    // 删除文件或目录，若文件或目录不存在则返回 false
    [[nodiscard]] bool deleteFile() const;

    // 程序退出时删除文件或目录，若文件或目录不存在则返回 false
    [[nodiscard]] bool deleteOnExit() const;

    // 检查文件或目录是否存在
    [[nodiscard]] bool exists() const;

    // 获取文件或目录的绝对路径
    [[nodiscard]] std::string getAbsolutePath() const;

    // 获取文件或目录的名称
    [[nodiscard]] std::string getName() const;

    // 获取文件或目录的父路径
    [[nodiscard]] std::string getParent() const;

    // 判断是否是目录
    [[nodiscard]] bool isDirectory() const;

    // 判断是否是文件
    [[nodiscard]] bool isFile() const;

    // 列出目录下的所有文件和子目录
    [[nodiscard]] std::vector<File> listFiles() const;

    // 列出目录下的所有文件和子目录的路径字符串
    [[nodiscard]] std::vector<std::string> fileList() const;

    // 创建单级目录
    [[nodiscard]] bool mkdir() const;

    // 创建多级目录
    [[nodiscard]] bool mkdirs() const;

    // 重命名文件或目录
    [[nodiscard]] bool renameTo(const File& dest) const;

    // 读取文件内容到缓冲区
    size_t read(char* buffer, size_t size, size_t offset = 0) const;

    // 写入缓冲区内容到文件
    size_t write(const char* buffer, size_t size, size_t offset = 0, bool append = false) const;

    // 以块的形式读取文件内容，回调函数处理每块数据
    size_t read(const std::function<void(const char*, ssize_t)>& fun, size_t offset = 0, int chunk = DEFAULT_BUFFER_SIZE) const;

  private:
    bool open() const;

    void close() const;

    std::filesystem::path _path;
    mutable std::ifstream _ifs;
    mutable std::ofstream _ofs;
    mutable bool _isOpen{false};
  };
} // namespace cppkit::io