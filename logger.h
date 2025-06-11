#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

enum class LogLevel { INFO, DEBUG, WARN, LOG_ERR, START, FATAL };

class Logger {
public:
    // Get the singleton instance.
    static Logger& getInstance();

    // Sets the log file to use.
    void setLogFile(const std::string& filename);

    // Logs a message at the given level.
    void log(LogLevel level, const std::string& message, int lineNumber = -1);

    // Control whether output is printed to the console.
    void setConsoleOutput(bool enabled);
    void printHeader();

private:
    Logger(); // Private constructor for singleton.

    std::ofstream logFile;
    std::mutex mutex;

    // Member variable to control console output.
    bool consoleOutput;

    // Returns a string representation of the log level.
    std::string getLevelString(LogLevel level);
    static Logger* instance; // Singleton instance
};

#endif // LOGGER_H