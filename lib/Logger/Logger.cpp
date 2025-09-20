#include "Logger.h"

#include <stdarg.h>
#include <utility>

Logger::Logger(const uint16_t port) :
    port_(port) {
}

Logger& Logger::getInstanceImpl(const uint16_t port) {
    static Logger instance{port};
    return instance;
}

void Logger::init(const uint16_t port) {
    getInstanceImpl(port);
}

bool Logger::begin() {
    if (!Serial) {
        Serial.begin(115200);
    }

    return true;
}

bool Logger::update() {
    return true;
}

uint16_t Logger::getPort() {
    return Logger::getInstance().port_;
}

void Logger::setSink(LogSink sink) {
    Logger::getInstance().sink_ = std::move(sink);
}

void Logger::log(const Level level, const String& file, const __FlashStringHelper* function, uint32_t line, const char* logmsg) {
    char time[12];
    current_time(time);

    String message;
    message.reserve(160);
    message += time;
    message += get_level_identifier(level);
    message += ' ';

    if (level < Level::DEBUG) {
        message += file;
        message += ':';
        message += line;
        message += '@';
        message += function;
        message += F("() ");
    }

    message += logmsg;

    Serial.println(message);

    if (sink_) {
        sink_(level, message);
    }
}

void Logger::logf(const Level level, const String& file, const __FlashStringHelper* function, uint32_t line, const char* format, ...) {
    va_list arg;
    va_start(arg, format);
    char temp[64];
    char* buffer = temp;
    size_t len = vsnprintf(temp, sizeof(temp), format, arg);
    va_end(arg);

    if (len > sizeof(temp) - 1) {
        buffer = new char[len + 1];

        if (!buffer) {
            return;
        }

        va_start(arg, format);
        vsnprintf(buffer, len + 1, format, arg);
        va_end(arg);
    }

    log(level, file, function, line, buffer);

    if (buffer != temp) {
        delete[] buffer;
    }
}

void Logger::current_time(char* timestamp) {
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    snprintf(timestamp, 12, "[%02d:%02d:%02d] ", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

String Logger::get_level_identifier(Logger::Level lvl) {
    switch (lvl) {
        case Level::TRACE:
            return "  TRACE";
        case Level::DEBUG:
            return "  DEBUG";
        case Level::INFO:
            return "   INFO";
        case Level::WARNING:
            return "WARNING";
        case Level::ERROR:
            return "  ERROR";
        case Level::FATAL:
            return "  FATAL";
        default:
            return " SILENT";
    }
}
