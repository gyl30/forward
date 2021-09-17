//
// Created by gyl on 2021/9/17.
//

#ifndef FORWARD_LOG_H
#define FORWARD_LOG_H
#include <sstream>
#include <string.h>
#include <string>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

class LogHelp
{
   public:
    LogHelp(const char* f, int l, const char* v) : f(f), line(l)
    {
        ss << format_time() << " " << syscall(__NR_gettid) << " " << v << " ";
    }
    ~LogHelp()
    {
        ss << " " << f << ":" << line;
        printf("%s\n", ss.str().data());
    }
    template <typename T>
    LogHelp& operator<<(T t)
    {
        ss << t;
        return *this;
    }

   private:
    std::string format_time()
    {
        char buf[64] = {0};
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int64_t seconds = tv.tv_sec;
        int microseconds = tv.tv_usec;
        struct tm tm_time;
        gmtime_r(&seconds, &tm_time);
        snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d", tm_time.tm_year + 1900, tm_time.tm_mon + 1,
                 tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, microseconds);
        return buf;
    }
   private:
    std::stringstream ss;
    const char* f;
    int line;
};

#define LOG_INFO LogHelp(__FILENAME__, __LINE__, "INFO")
#define LOG_DEBUG LogHelp(__FILENAME__, __LINE__, "DEBUG")
#define LOG_WAR LogHelp(__FILENAME__, __LINE__, "WAR")
#define LOG_ERROR LogHelp(__FILENAME__, __LINE__, "ERROR")
#endif    // FORWARD_LOG_H
