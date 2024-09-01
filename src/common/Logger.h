#pragma once

#include <queue>
#include <thread>
#include <condition_variable>
#include <utility>
#include <vector>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <memory>
#include <iostream>
#include <fstream>

class AsyncLogger {
public:
    enum class LogLevel {
        DEBUG_LOG,
        INFO_LOG,
        WARNING_LOG,
        ERROR_LOG,
        FATAL_LOG
    };

    struct LogEntry {
        LogLevel level;
        std::string file;
        int line;
        std::string message;
        std::chrono::system_clock::time_point timestamp;
        std::unordered_map<std::string, std::string> context;
    };

    class LogDestination {
    public:
        virtual ~LogDestination() = default;
        virtual void write(const LogEntry& entry) = 0;
    };

    class ConsoleDestination : public LogDestination {
    public:
        void write(const LogEntry& entry) override {
            std::cout << formatLogEntry(entry) << std::endl;
        }
    };

    class FileDestination : public LogDestination {
    public:
        FileDestination(std::string  filename, size_t maxFileSize)
                : m_filename(std::move(filename)), m_maxFileSize(maxFileSize) {
            openLogFile();
        }

        void write(const LogEntry& entry) override {
            /*if (m_logFile.tellp() >= m_maxFileSize) {
                //rotateLogFile();
            }*/
            m_logFile << formatLogEntry(entry) << std::endl;
        }

    private:
        void openLogFile() {
            m_logFile.open(m_filename, std::ios::app);
        }

        void rotateLogFile() {
            m_logFile.close();
            std::filesystem::path logPath(m_filename);
            std::string newName = logPath.stem().string() + "_" +
                                  std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
                                  logPath.extension().string();
            std::filesystem::rename(m_filename, newName);
            openLogFile();
        }

        std::string m_filename;
        std::ofstream m_logFile;
        size_t m_maxFileSize;
    };

    AsyncLogger() : m_logLevel(LogLevel::INFO_LOG), m_stopFlag(false) {
        m_loggerThread = std::thread(&AsyncLogger::loggerThreadFunction, this);
    }

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    ~AsyncLogger() {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_stopFlag = true;
        }
        m_condition.notify_one();
        if (m_loggerThread.joinable()) {
            m_loggerThread.join();
        }
    }

    static AsyncLogger& getInstance() {
        static AsyncLogger instance;
        return instance;
    }

    void setLogLevel(LogLevel level) {
        m_logLevel = level;
    }

    static LogLevel parseLogLevel(const std::string& level) {
        if (level == "DEBUG" || level == "debug") return LogLevel::DEBUG_LOG;
        if (level == "INFO" || level == "info") return LogLevel::INFO_LOG;
        if (level == "WARNING" || level == "warning") return LogLevel::WARNING_LOG;
        if (level == "ERROR" || level == "error") return LogLevel::ERROR_LOG;
        if (level == "FATAL" || level == "fatal") return LogLevel::FATAL_LOG;

        // Default to INFO if the level string is not recognized
        return LogLevel::INFO_LOG;
    }

    void addDestination(std::shared_ptr<LogDestination> destination) {
        std::lock_guard<std::mutex> lock(m_destinationsMutex);
        m_destinations.push_back(std::move(destination));
    }

    void setLogFilter(std::function<bool(const LogEntry&)> filter) {
        m_logFilter = std::move(filter);
    }

    void setContextValue(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        m_context[key] = value;
    }

    template<typename... Args>
    void log(LogLevel level, const char* file, int line, const char* format, Args... args) {
        if (level >= m_logLevel) {
            LogEntry entry{
                    level,
                    file,
                    line,
                    formatString(format, args...),
                    std::chrono::system_clock::now(),
                    {}
            };

            {
                std::lock_guard<std::mutex> lock(m_contextMutex);
                entry.context = m_context;
            }

            if (m_logFilter && !m_logFilter(entry)) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_logQueue.push(std::move(entry));
            }
            m_condition.notify_one();
        }
    }

private:
    void loggerThreadFunction();

    static std::string formatLogEntry(const LogEntry& entry) {
        std::ostringstream oss;
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);

        std::tm time_info{};
#if defined(_WIN32)
        localtime_s(&time_info, &now_c);
#else
        localtime_r(&now_c, &time_info);
#endif
        oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S") << " ";
        oss << "[" << getLevelString(entry.level) << "] ";
        oss << "[" << entry.file << ":" << entry.line << "] ";
        oss << entry.message;

        if (!entry.context.empty()) {
            oss << " {";
            for (const auto& [key, value] : entry.context) {
                oss << key << ": " << value << ", ";
            }
            oss.seekp(-2, std::ios_base::end);
            oss << "}";
        }

        return oss.str();
    }

    static const char* getLevelString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG_LOG: return "DEBUG";
            case LogLevel::INFO_LOG: return "INFO";
            case LogLevel::WARNING_LOG: return "WARNING";
            case LogLevel::ERROR_LOG: return "ERROR";
            case LogLevel::FATAL_LOG: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    template<typename... Args>
    static std::string formatString(const char* format, Args... args) {
        int size = snprintf(nullptr, 0, format, args...);
        std::string result(size + 1, '\0');
        snprintf(&result[0], size + 1, format, args...);
        return result;
    }

    LogLevel m_logLevel;
    std::queue<LogEntry> m_logQueue;
    std::thread m_loggerThread;
    std::mutex m_queueMutex;
    std::mutex m_destinationsMutex;
    std::mutex m_contextMutex;
    std::condition_variable m_condition;
    bool m_stopFlag;
    std::vector<std::shared_ptr<LogDestination>> m_destinations;
    std::function<bool(const LogEntry&)> m_logFilter;
    std::unordered_map<std::string, std::string> m_context;
};

// Macro definitions for easy logging
#define LOG_DEBUG(format, ...) AsyncLogger::getInstance().log(AsyncLogger::LogLevel::DEBUG_LOG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) AsyncLogger::getInstance().log(AsyncLogger::LogLevel::INFO_LOG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) AsyncLogger::getInstance().log(AsyncLogger::LogLevel::WARNING_LOG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) AsyncLogger::getInstance().log(AsyncLogger::LogLevel::ERROR_LOG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) AsyncLogger::getInstance().log(AsyncLogger::LogLevel::FATAL_LOG, __FILE__, __LINE__, format, ##__VA_ARGS__)