#include "cppkit/log/log.hpp"

namespace cppkit::log
{
    Logger& Logger::instance()
    {
        static Logger inst;
        return inst;
    }

    Logger::~Logger()
    {
        stop_ = true;
        queue_cv_.notify_one();

        if (bg_thread_.joinable())
        {
            bg_thread_.join();
        }

        flush();
    }

    bool Logger::init(const std::string& filename)
    {
        std::lock_guard lk(mtx_);
        base_filename_ = filename;
        current_open_path_.clear();
        if (!filename.empty())
        {
            try // 创建日志文件目录
            {
                if (const auto parent = std::filesystem::path(filename).parent_path(); !parent.empty())
                    std::filesystem::create_directories(parent);
            }
            catch (const std::filesystem::filesystem_error& e)
            {
                std::cerr << "Failed to create log file " << filename << ",err " << e.what() << "\n";
                return false;
            }

            try
            {
                ofs_ = std::make_unique<std::ofstream>(filename, std::ios::app);
                if (!ofs_->is_open())
                {
                    ofs_.reset();
                    return false;
                }
                current_open_path_ = filename;
            }
            catch (...)
            {
                ofs_.reset();
                return false;
            }

            current_date_ = fileDateString();
        }
        return true;
    }

    void Logger::setLevel(const Level lvl)
    {
        std::lock_guard lk(mtx_);
        level_ = lvl;
    }

    Level Logger::level() const
    {
        return level_;
    }

    void Logger::setToStdout(const bool on)
    {
        std::lock_guard lk(mtx_);
        to_stdout_ = on;
    }

    void Logger::setRotation(const Rotation r)
    {
        std::lock_guard lk(mtx_);
        rotation_ = r;
    }

    void Logger::setRotationSize(const std::uintmax_t bytes)
    {
        std::lock_guard lk(mtx_);
        rotation_size_ = bytes;
    }

    void Logger::setMaxFiles(const int max_files)
    {
        std::lock_guard lk(mtx_);
        max_files_ = std::max(1, max_files);
    }

    void Logger::setFileNamePattern(const std::string& pattern)
    {
        std::lock_guard lk(mtx_);
        filename_pattern_ = pattern;
        current_open_path_.clear();
    }

    // void Logger::logf(const Level lvl, const char* file, const int line, const char* func, const char* fmt, ...)
    // {
    //     if (lvl < level_ || level_ == Level::Off)
    //         return;
    //
    //     va_list ap;
    //     va_start(ap, fmt);
    //     const std::string body = vformat(fmt, ap);
    //     va_end(ap);
    //
    //     std::ostringstream oss;
    //     oss << "[" << currentTime() << "]"
    //         << "[" << levelToString(lvl) << "]"
    //         << "[" << file << ":" << line << " " << func << "] "
    //         << body << "\n";
    //
    //     const std::string out = oss.str();
    //
    //     std::lock_guard lk(mtx_);
    //
    //     // 是否需要基于日期切换文件
    //     if (!filename_pattern_.empty() && rotation_ == Rotation::Daily)
    //     {
    //         const std::string today = fileDateString();
    //         if (current_date_.empty())
    //             current_date_ = today;
    //         if (today != current_date_)
    //         {
    //             current_date_ = today;
    //             current_open_path_.clear();
    //         }
    //     }
    //
    //     ensureLogFileOpenLocked();
    //
    //     if (filename_pattern_.empty())
    //     {
    //         rotateIfNeededLocked();
    //     }
    //     write(out);
    // }

    void Logger::flush() const
    {
        // 先处理队列中的所有日志
        {
            std::unique_lock lk(queue_mtx_);
            // 如果是异步模式，将队列中的日志取出处理
            if (const_cast<Logger*>(this)->is_async_)
            {
                std::queue<std::string> temp_queue;
                std::swap(const_cast<Logger*>(this)->log_queue_, temp_queue);

                // 解锁队列，处理日志
                lk.unlock();

                // 持有文件锁处理所有日志
                std::lock_guard lk2(const_cast<Logger*>(this)->mtx_);
                while (!temp_queue.empty())
                {
                    const_cast<Logger*>(this)->processLogLineLocked(temp_queue.front());
                    temp_queue.pop();
                }
            }
        }

        // 刷新文件
        std::lock_guard lk(mtx_);
        if (ofs_ && ofs_->is_open())
            ofs_->flush();
        if (to_stdout_)
            std::cout.flush();
    }

    // 后台工作线程函数
    void Logger::backgroundWorker()
    {
        while (true)
        {
            std::unique_lock lk(queue_mtx_);

            // 等待有日志或者停止信号
            queue_cv_.wait(lk, [this]()
            {
                return stop_ || !log_queue_.empty();
            });

            if (stop_ && log_queue_.empty())
            {
                break;
            }

            // 将队列中的日志取出
            std::queue<std::string> temp_queue;
            log_queue_.swap(temp_queue);

            // 解锁队列，处理日志
            lk.unlock();

            // 持有文件锁处理所有日志
            std::lock_guard lk2(mtx_);
            const bool hasWritten = !temp_queue.empty();
            while (!temp_queue.empty())
            {
                processLogLineLocked(temp_queue.front());
                temp_queue.pop();
            }
            if (hasWritten && ofs_ && ofs_->is_open())
            {
                ofs_->flush();
            }
        }
    }

    // 处理单条日志行（需要持有mtx_）
    void Logger::processLogLineLocked(const std::string& log_line)
    {
        // 是否需要基于日期切换文件
        if (!filename_pattern_.empty() && rotation_ == Rotation::Daily)
        {
            const std::string today = fileDateString();
            if (current_date_.empty())
                current_date_ = today;
            if (today != current_date_)
            {
                current_date_ = today;
                current_open_path_.clear();
            }
        }

        ensureLogFileOpenLocked();

        if (filename_pattern_.empty())
        {
            rotateIfNeededLocked();
        }
        write(log_line);
    }


    void Logger::ensureLogFileOpenLocked()
    {
        if (!filename_pattern_.empty())
        {
            const std::string date_for_name = current_date_.empty() ? fileDateString() : current_date_;
            const std::filesystem::path target = expandPattern(filename_pattern_, date_for_name, "");

            std::filesystem::path final_target = target;
            const std::filesystem::path base(base_filename_);
            if (!final_target.has_parent_path())
            {
                auto parent = base.parent_path();
                if (parent.empty())
                    parent = ".";
                final_target = parent / final_target;
            }

            if (const std::string final_path = final_target.string(); final_path != current_open_path_)
            {
                if (ofs_)
                {
                    ofs_->flush();
                    ofs_->close();
                    ofs_.reset();
                }

                try
                {
                    if (!final_target.parent_path().empty())
                        std::filesystem::create_directories(final_target.parent_path());
                }
                catch (...)
                {
                }
                try { ofs_ = std::make_unique<std::ofstream>(final_path, std::ios::app); }
                catch (...) { ofs_.reset(); }
                current_open_path_ = ofs_ && ofs_->is_open() ? final_path : std::string{};
            }
        }
        else
        {
            if (base_filename_.empty())
                return;

            if (base_filename_ != current_open_path_)
            {
                if (ofs_)
                {
                    ofs_->flush();
                    ofs_->close();
                    ofs_.reset();
                }
                try
                {
                    if (auto parent = std::filesystem::path(base_filename_).parent_path(); !parent.empty())
                        std::filesystem::create_directories(parent);
                }
                catch (...)
                {
                }
                try { ofs_ = std::make_unique<std::ofstream>(base_filename_, std::ios::app); }
                catch (...) { ofs_.reset(); }
                current_open_path_ = ofs_ && ofs_->is_open() ? base_filename_ : std::string{};
            }
        }
    }

    void Logger::write(const std::string& s) const
    {
        if (ofs_ && ofs_->is_open())
        {
            (*ofs_) << s;
        }
        if (to_stdout_)
        {
            std::cout << s;
        }
    }

    void Logger::rotateIfNeededLocked()
    {
        if (base_filename_.empty() || rotation_ == Rotation::None)
            return;

        try
        {
            if (rotation_ == Rotation::Size)
            {
                if (std::filesystem::exists(base_filename_))
                {
                    if (const auto sz = std::filesystem::file_size(base_filename_);
                        static_cast<std::uintmax_t>(sz) >= rotation_size_)
                    {
                        performRotationLocked();
                    }
                }
            }
            else if (rotation_ == Rotation::Daily)
            {
                const std::string today = fileDateString();
                if (current_date_.empty())
                    current_date_ = today;
                if (today != current_date_)
                {
                    performRotationLocked();
                    current_date_ = today;
                }
            }
        }
        catch (...)
        {
        }
    }

    std::filesystem::path Logger::expandPattern(const std::string& pattern,
                                                const std::string& date,
                                                const std::string& ts)
    {
        std::string s = pattern;
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to)
        {
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos)
            {
                str.replace(pos, from.length(), to);
                pos += to.length();
            }
        };

        if (date.size() >= 10)
        {
            replace_all(s, "{date}", date);
            replace_all(s, "{year}", date.substr(0, 4));
            replace_all(s, "{month}", date.substr(5, 2));
            replace_all(s, "{day}", date.substr(8, 2));
        }
        else
        {
            replace_all(s, "{date}", date);
        }
        replace_all(s, "{timestamp}", ts);
        return s;
    }

    void Logger::performRotationLocked()
    {
        if (ofs_)
        {
            ofs_->flush();
            ofs_->close();
            ofs_.reset();
        }

        // 清空当前打开记录（因为基文件可能被移动/重建）
        current_open_path_.clear();

        const std::string ts = timestampString();
        const std::filesystem::path base(base_filename_);
        std::filesystem::path arch_path;

        if (!filename_pattern_.empty())
        {
            const std::string date_for_name = current_date_.empty() ? fileDateString() : current_date_;
            arch_path = expandPattern(filename_pattern_, date_for_name, ts);

            if (!arch_path.has_parent_path())
            {
                auto parent = base.parent_path();
                if (parent.empty())
                    parent = ".";
                arch_path = parent / arch_path;
            }

            try
            {
                if (!arch_path.parent_path().empty())
                    std::filesystem::create_directories(arch_path.parent_path());
            }
            catch (...)
            {
            }
        }
        else
        {
            arch_path = std::filesystem::path(base_filename_ + "." + ts);
        }

        int idx = 0;
        std::filesystem::path try_path = arch_path;
        while (std::filesystem::exists(try_path))
        {
            ++idx;
            try_path = std::filesystem::path(arch_path.string() + "." + std::to_string(idx));
        }
        arch_path = try_path;

        try
        {
            if (std::filesystem::exists(base))
            {
                std::filesystem::rename(base, arch_path);
            }
        }
        catch (...)
        {
        }

        try
        {
            try
            {
                if (auto parent = base.parent_path(); !parent.empty())
                    std::filesystem::create_directories(parent);
            }
            catch (...)
            {
            }
            ofs_ = std::make_unique<std::ofstream>(base_filename_, std::ios::app);
            current_open_path_ = ofs_ && ofs_->is_open() ? base_filename_ : std::string{};
        }
        catch (...)
        {
            ofs_.reset();
            current_open_path_.clear();
        }

        cleanupArchivesLocked(base, arch_path);
    }

    void Logger::cleanupArchivesLocked(const std::filesystem::path& base, const std::filesystem::path& sample_archive)
    {
        try
        {
            auto parent = sample_archive.parent_path();
            if (parent.empty())
                parent = ".";

            std::string prefix_name;
            if (!filename_pattern_.empty())
            {
                size_t p = filename_pattern_.find('{');
                std::string raw_prefix = (p == std::string::npos) ? filename_pattern_ : filename_pattern_.substr(0, p);
                std::filesystem::path pname(raw_prefix);
                std::string fname_prefix = pname.filename().string();
                if (fname_prefix.empty())
                {
                    fname_prefix = base.filename().string() + ".";
                }
                prefix_name = fname_prefix;
            }
            else
            {
                prefix_name = base.filename().string() + ".";
            }

            std::vector<std::filesystem::directory_entry> entries;
            for (auto& e : std::filesystem::directory_iterator(parent))
            {
                const auto& p = e.path();
                const std::string fname = p.filename().string();
                if (fname.rfind(prefix_name, 0) == 0)
                    entries.push_back(e);
            }

            std::sort(entries.begin(),
                      entries.end(),
                      [](const auto& a, const auto& b)
                      {
                          try { return a.last_write_time() > b.last_write_time(); }
                          catch (...) { return a.path().string() > b.path().string(); }
                      });

            for (size_t i = max_files_; i < entries.size(); ++i)
            {
                try { std::filesystem::remove(entries[i].path()); }
                catch (...)
                {
                }
            }
        }
        catch (...)
        {
        }
    }

    std::string Logger::levelToString(const Level l)
    {
        switch (l)
        {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
        case Level::Fatal:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    std::string Logger::currentTime()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::tm bt{};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&bt, &t);
#else
        localtime_r(&t, &bt);
#endif
        std::ostringstream oss;
        oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S") << "." << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    std::string Logger::fileDateString()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        std::tm bt{};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&bt, &t);
#else
        localtime_r(&t, &bt);
#endif
        std::ostringstream oss;
        oss << std::put_time(&bt, "%Y-%m-%d");
        return oss.str();
    }

    std::string Logger::timestampString()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        std::tm bt{};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&bt, &t);
#else
        localtime_r(&t, &bt);
#endif
        std::ostringstream oss;
        oss << std::put_time(&bt, "%Y-%m-%d_%H-%M-%S") << "." << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    std::string Logger::vformat(const char* fmt, va_list ap)
    {
        va_list ap_copy;
        va_copy(ap_copy, ap);
        const int len = vsnprintf(nullptr, 0, fmt, ap_copy);
        va_end(ap_copy);
        if (len < 0)
            return {fmt};

        std::vector<char> buf(len + 1);
        vsnprintf(buf.data(), buf.size(), fmt, ap);
        return {buf.data(), buf.data() + len};
    }
}
