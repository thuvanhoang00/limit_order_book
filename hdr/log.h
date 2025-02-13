#ifndef LOG_H
#define LOG_H
#include <fstream>
#include <iostream>
#include <utility>

namespace thu
{

#define LOG Log::getInstance().log

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
    void log()
    {
        if(!ofs.is_open()) ofs.open("logfile.txt");
        ofs << "\n";
    }

    template<typename T, typename... Args>
    void log(T&& t, Args&&... args)
    {
        if(!ofs.is_open()) ofs.open("logfile.txt");

        ofs << t << " ";
        log(args...);
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
};


} // namespace thu
#endif