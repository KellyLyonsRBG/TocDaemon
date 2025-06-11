#include "logger.h"
#include <filesystem>
#include <ctime>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Correct implementation of the singleton pattern
Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

// Logs the message with a timestamp and log level
void Logger::log(LogLevel level, const std::string& message, int lineNumber) {
    std::lock_guard<std::mutex> lock(mutex);
    std::string levelStr = getLevelString(level);

    std::string logMessage = "[" + levelStr + "] " + message;

    if (lineNumber != -1) {
        logMessage += " (Line: " + std::to_string(lineNumber) + ")";
    }

    // Print to console only if console output is enabled.
    if (consoleOutput) {
        std::cout << logMessage << std::endl;
    }

    // Always log to file.
    if (logFile.is_open()) {
        logFile << logMessage << std::endl;
    }
}


// Converts the enum to a string.
std::string Logger::getLevelString(LogLevel level) {
    switch (level) {
    case LogLevel::INFO:    return "INFO";
    case LogLevel::DEBUG:   return "DEBUG";
    case LogLevel::WARN:    return "WARN";
    case LogLevel::LOG_ERR: return "LOG_ERR";
    case LogLevel::FATAL:   return "FATAL";
    case LogLevel::START: return "    ";
    default:                return "UNKNOWN";
    }
}

// Sets whether the log message should also be printed to the console.
void Logger::setConsoleOutput(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    consoleOutput = enabled;
}

// Constructor implementation, initializing console output to true by default.
Logger::Logger() : consoleOutput(false) {
    // Get current date in YYMMDD format
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    std::ostringstream dateStream;
    dateStream << (localTime->tm_year % 100);  // YY (last two digits of the year)
    dateStream << std::setw(2) << std::setfill('0') << (localTime->tm_mon + 1); // MM
    dateStream << std::setw(2) << std::setfill('0') << localTime->tm_mday;  // DD

    std::string logFileName = "application_" + dateStream.str() + ".log";

    std::string cwd = std::filesystem::current_path().string();
    std::string fullPath = cwd + "/" + logFileName;

    logFile.open(fullPath, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Error: Unable to open log file at " << fullPath << std::endl;
    }
}

void Logger::printHeader() {
    log(LogLevel::START, "=================================================================");
    log(LogLevel::START, "                       SCTesting                                 ");
    log(LogLevel::START, "                 System Startup Initiated                        ");
    log(LogLevel::START, "=================================================================");
}

Logger::Logger(const std::string& name) : m_name(name) {}

void Logger::info(const std::string& msg) {
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
#else
    pid_t pid = getpid();
#endif
    std::cout << "[INFO][" << m_name << "][PID:" << pid << "] " << msg << std::endl;
}

void Logger::warn(const std::string& msg) {
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
#else
    pid_t pid = getpid();
#endif
    std::cout << "[WARN][" << m_name << "][PID:" << pid << "] " << msg << std::endl;
}

void Logger::error(const std::string& msg) {
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
#else
    pid_t pid = getpid();
#endif
    std::cerr << "[ERROR][" << m_name << "][PID:" << pid << "] " << msg << std::endl;
}