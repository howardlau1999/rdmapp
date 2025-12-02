#pragma once

#include <rdmapp/detail/debug.h>
#include <atomic>
#include <sstream>
#include <iostream>
#include <cstdio>

namespace RDMA_EC {

class Logger {
public:
    static void set_enabled(bool enabled) {
        enabled_.store(enabled);
    }
    
    static bool is_enabled() {
        return enabled_.load();
    }
    
    class LogStream {
    public:
        LogStream(bool enabled, const char* prefix) : enabled_(enabled), prefix_(prefix) {}
        
        ~LogStream() {
            if (enabled_) {
                printf("%s %s\n", prefix_, ss_.str().c_str());
            }
        }
        
        template<typename T>
        LogStream& operator<<(const T& value) {
            if (enabled_) {
                ss_ << value;
            }
            return *this;
        }
        
        LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
            if (enabled_) {
                ss_ << manip;
            }
            return *this;
        }
        
    private:
        bool enabled_;
        const char* prefix_;
        std::ostringstream ss_;
    };
    
    static LogStream info() {
        return LogStream(enabled_.load(), "[INFO]");
    }
    
    static LogStream debug() {
        return LogStream(enabled_.load(), "[DEBUG]");
    }
    
    static LogStream error() {
        return LogStream(true, "[ERROR]");
    }

private:
    static std::atomic<bool> enabled_;
};

} // namespace RDMA_EC

