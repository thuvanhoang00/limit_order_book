#ifndef LOG_H
#define LOG_H
#include <fstream>
#include <iostream>
#include <utility>
#include <string>
#include <chrono>
#include "spinlock.h"
namespace thu
{

#define LOG(...) Log::getInstance().log(std::string("[") + __FUNCTION__ + "]", __VA_ARGS__)

// #define LOG(format, ...) Log::getInstance().log("[%s] " format, __FUNCTION__, ##__VA_ARGS__)

class Log
{
public:
    static Log& getInstance()
    {
        static Log instance;
        return instance;
    }

#if 0
    template<typename T, typename... Args>
    void log(T&& t, Args&&... args)
    {
        if(ofs.is_open())
        {
            (ofs << ... << std::forward<Args>(args)) << std::endl; // Fold expression (C++17)

        }
    }
#endif
    // void log()
    // {
    //     if(!ofs.is_open()) ofs.open("logfile.txt");

    //     ofs << "\n";
    // }

    template<typename T, typename... Args>
    void log(T&& t, Args&&... args)
    {
        if(!ofs.is_open()) ofs.open("logfile.txt");

        spin.lock();
        // add time to Log
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm = *std::localtime(&now_time);

        ofs << "[" << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << "]" << " ";
        
        ofs << t << " ";

        // log(args...);
        (ofs << ... << std::forward<Args>(args)) << std::endl;
        spin.unlock();
    }
private:
    Log() 
    {
        ofs.open("logfile.txt");
    }

    ~Log()
    {
        if(ofs.is_open()) ofs.close();
    }
    std::ofstream ofs;
    SpinLock spin;
};


} // namespace thu
#endif