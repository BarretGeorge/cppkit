#pragma once

#include "common.hpp"
#include "cppkit/strings.hpp"
#include <memory>
#include <string>
#include <mutex>
#include <fstream>
#include <iostream>
#include <cstdarg>
#include <vector>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <queue>
#include <thread>
#include <condition_variable>
#include <source_location>
#include <atomic>
#if defined(__cpp_lib_format)
#include <format>
#else
#include "cppkit/fmt.hpp"
#endif

namespace cppkit::log
{
    enum class Level
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Fatal,
        Off
    };

    enum class Rotation
    {
        None = 0,
        Size,
        Daily
    };

    class Logger
    {
    public:
        static Logger& instance();

        ~Logger();

        Logger(const Logger&) = delete;

        Logger& operator=(const Logger&) = delete;

        bool init(const std::string& filename = "");

        void setLevel(Level lvl);

        Level level() const;

        void setToStdout(bool on);

        void setRotation(Rotation r);

        void setRotationSize(std::uintmax_t bytes);

        void setMaxFiles(int max_files);

        // 设置归档命名模式 pattern，支持的占位符：
        //   {date}      - 当前日期，格式 YYYY-MM-DD
        //   {year}      - 当前年份，格式 YYYY
        //   {month}     - 当前月份，格式 MM
        //   {day}       - 当前日，格式 DD
        //   {timestamp} - 当前时间戳，格式 YYYY-MM-DD_HH-MM-SS.mmm
        void setFileNamePattern(const std::string& pattern);

        // 格式化日志记录函数
        template <typename... Args>
        void logf(const Level lvl, const char* file, const int line, const char* func, const char* fmt, Args&&... args)
        {
            if (lvl < level_ || level_ == Level::Off)
                return;

            // 前置格式化：在锁外完成所有字符串拼接操作
            std::ostringstream oss;

            // 获取当前时间字符串
            const std::string timeStr = currentTime();

            // 获取日志级别字符串
            const std::string levelStr = levelToString(lvl);

            // 添加颜色（仅在输出到终端时）
            if (to_stdout_)
            {
                std::string colorCode;
                switch (lvl)
                {
                case Level::Trace: colorCode = "\033[37m";
                    break; // 白色
                case Level::Debug: colorCode = "\033[36m";
                    break; // 青色
                case Level::Info: colorCode = "\033[32m";
                    break; // 绿色
                case Level::Warn: colorCode = "\033[33m";
                    break; // 黄色
                case Level::Error: colorCode = "\033[31m";
                    break; // 红色
                case Level::Fatal: colorCode = "\033[35m";
                    break; // 紫色
                default: colorCode = "\033[0m";
                    break; // 重置
                }
                oss << colorCode;
            }

            oss << "[" << timeStr << "]"
                << "[" << levelStr << "]"
                << ":" << file << ":" << line << " " << func << "] ";


            // 拼接完整日志行
#if defined(__cpp_lib_format)
            oss << std::vformat(fmt, std::make_format_args(args...)) << "\n";
#else
            oss << cppkit::format(fmt, std::forward<Args>(args)...) << "\n";
#endif

            // 添加颜色重置码
            if (to_stdout_)
            {
                oss << "\033[0m";
            }

            const std::string logLine = oss.str();

            // 只在将日志写入队列时持有锁，减少锁持有时间
            {
                std::unique_lock lk(queue_mtx_);
                log_queue_.emplace(logLine);

                // 如果是异步模式，通知后台线程
                if (is_async_)
                {
                    queue_cv_.notify_one();
                    return; // 异步模式直接返回，不需要后续操作
                }
            }

            // 同步模式下继续处理
            std::unique_lock lk(mtx_);
            processLogLineLocked(logLine);
        }

        void flush() const;

        // 设置是否使用异步模式
        void setAsync(const bool async)
        {
            std::unique_lock lk(queue_mtx_);
            is_async_ = async;
            queue_cv_.notify_one();
        }

    private:
        Logger()
            : level_(Level::Info),
              rotation_(Rotation::None),
              rotation_size_(DEFAULT_LOG_ROTATION_SIZE),
              max_files_(DEFAULT_MAX_FILES),
              to_stdout_(true),
              is_async_(true), // 默认异步模式
              stop_(false)
        {
            // 启动后台线程
            bg_thread_ = std::thread([this]()
            {
                this->backgroundWorker();
            });
        }

        // 后台工作线程函数
        void backgroundWorker();

        // 处理单条日志行（需要持有mtx_）
        void processLogLineLocked(const std::string& log_line);

        // 确保日志文件已打开
        void ensureLogFileOpenLocked();

        void write(const std::string& s) const;

        void rotateIfNeededLocked();

        static std::filesystem::path expandPattern(const std::string& pattern,
                                                   const std::string& date,
                                                   const std::string& ts);

        void performRotationLocked();

        void cleanupArchivesLocked(const std::filesystem::path& base, const std::filesystem::path& sample_archive);

        static std::string levelToString(Level l);

        static std::string currentTime();

        static std::string fileDateString();

        static std::string timestampString();

        static std::string vformat(const char* fmt, va_list ap);

        // 互斥锁 保证文件操作和配置修改的线程安全
        mutable std::mutex mtx_;

        // 日志级别
        Level level_;

        // 归档策略
        Rotation rotation_;

        // 归档大小阈值，单位字节
        std::uintmax_t rotation_size_;

        // 最多保留的归档文件数
        int max_files_;

        // 日志文件输出流
        std::unique_ptr<std::ofstream> ofs_;

        // 是否输出到标准输出
        bool to_stdout_;

        // 基础日志文件路径
        std::string base_filename_;

        // 归档文件名模式
        std::string filename_pattern_;

        // 当前日志日期（用于每日归档）
        std::string current_date_;

        // 当前打开的日志文件流路径
        std::string current_open_path_;

        // 异步模式相关
        bool is_async_; // 是否使用异步模式
        std::atomic<bool> stop_; // 线程停止标志
        mutable std::mutex queue_mtx_; // 队列互斥锁
        std::condition_variable queue_cv_; // 队列条件变量
        std::queue<std::string> log_queue_; // 日志队列
        std::thread bg_thread_; // 后台处理线程
    };

#define REL_FILE_ (cppkit::shortFilename(__FILE__))

    // Trace 级别日志
#define CK_LOG_TRACE(fmt, ...) cppkit::log::Logger::instance().logf(cppkit::log::Level::Trace, REL_FILE_, __LINE__, __func__, fmt, ##__VA_ARGS__)

    // Debug 级别日志
#define CK_LOG_DEBUG(fmt, ...) cppkit::log::Logger::instance().logf(cppkit::log::Level::Debug, REL_FILE_, __LINE__, __func__, fmt, ##__VA_ARGS__)

    // Info 级别日志
#define CK_LOG_INFO(fmt, ...)  cppkit::log::Logger::instance().logf(cppkit::log::Level::Info,  REL_FILE_, __LINE__, __func__, fmt, ##__VA_ARGS__)

    // Warn 级别日志
#define CK_LOG_WARN(fmt, ...)  cppkit::log::Logger::instance().logf(cppkit::log::Level::Warn,  REL_FILE_, __LINE__, __func__, fmt, ##__VA_ARGS__)

    // Error 级别日志
#define CK_LOG_ERROR(fmt, ...) cppkit::log::Logger::instance().logf(cppkit::log::Level::Error, REL_FILE_, __LINE__, __func__, fmt, ##__VA_ARGS__)

    // Fatal 级别日志
#define CK_LOG_FATAL(fmt, ...) cppkit::log::Logger::instance().logf(cppkit::log::Level::Fatal, REL_FILE_, __LINE__, __func__, fmt, ##__VA_ARGS__)

    // 设置日志文件路径
#define CK_LOG_INIT_FILE(path) cppkit::log::Logger::instance().init(path)

    // 设置日志级别
#define CK_LOG_SET_LEVEL(lvl) cppkit::log::Logger::instance().setLevel(lvl)

    // 设置是否输出到标准输出
#define CK_LOG_SET_STDOUT(on) cppkit::log::Logger::instance().setToStdout(on)

    // 设置日志归档策略
#define CK_LOG_SET_ROTATION(r) cppkit::log::Logger::instance().setRotation(r)

    // 设置日志归档大小阈值，单位字节
#define CK_LOG_SET_ROTATION_SIZE(bytes) cppkit::log::Logger::instance().setRotationSize(bytes)

    // 设置最多保留的归档文件数
#define CK_LOG_SET_MAX_FILES(n) cppkit::log::Logger::instance().setMaxFiles(n)

    // 设置归档文件名模式
#define CK_LOG_SET_FILENAME_PATTERN(p) cppkit::log::Logger::instance().setFileNamePattern(p)

    // 刷新日志缓冲区
#define CK_LOG_FLUSH() cppkit::log::Logger::instance().flush()
} // namespace cppkit::log
