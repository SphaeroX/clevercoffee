#pragma once

#include <Arduino.h>

#include <functional>
#include <stdint.h>

class Logger {
    public:
        enum class Level : int {
            TRACE = 0,
            DEBUG = 1,
            INFO = 2,
            WARNING = 3,
            ERROR = 4,
            FATAL = 5,
            SILENT = 6,
        };

        using LogSink = std::function<void(Level level, const String& message)>;

        static Logger& getInstance() {
            return getInstanceImpl();
        }

        static void init(const uint16_t port = 23);
        static bool begin();
        static bool update();
        static uint16_t getPort();
        void log(const Level level, const String& file, const __FlashStringHelper* function, uint32_t line, const char* logmsg);
        void logf(const Level level, const String& file, const __FlashStringHelper* function, uint32_t line, const char* format, ...);

        static void setLevel(Level level) {
            getInstance().level_ = level;
        }
        static Level getCurrentLevel() {
            return getInstance().level_;
        }

        static void setSink(LogSink sink);

    private:
        static Logger& getInstanceImpl(const uint16_t port = 23);
        Logger(const uint16_t port = 23);

        void current_time(char* timestamp);
        String get_level_identifier(Level lvl);

        Level level_{Level::INFO};
        uint16_t port_;
        LogSink sink_{};
};

#ifndef __FILE_NAME__
#define __FILE_NAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define IFLOG(level) if (Logger::Level::level >= Logger::getCurrentLevel())

#define LOG(level, ...)                                                                                                                                                                                                         \
    if (Logger::Level::level >= Logger::getCurrentLevel()) Logger::getInstance().log(Logger::Level::level, __FILE_NAME__, FPSTR(__func__), __LINE__, __VA_ARGS__)

#define LOGF(level, ...)                                                                                                                                                                                                        \
    if (Logger::Level::level >= Logger::getCurrentLevel()) Logger::getInstance().logf(Logger::Level::level, __FILE_NAME__, FPSTR(__func__), __LINE__, __VA_ARGS__)

