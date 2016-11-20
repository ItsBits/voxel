#pragma once

#include <iostream>
#include <mutex>

#define print(...) printDebug(__func__, __VA_ARGS__)

class Debug
{
public:
    Debug() = delete;

    template<typename T, typename ... Args>
    static void printDebug(const T & val, Args && ... args)
    {
#ifndef NDEBUG
          printAlways(val, args ...);
#endif
    }

private:
    static std::mutex m_lock;

    template<typename T, typename ... Args>
    static void printAlways(const T & val, Args && ... args)
    {
        std::unique_lock<std::mutex> lock{ m_lock };

        std::cout << '[' << val << "] ";
        printRest(args ...);
        std::cout << std::endl;
    }

    template<typename T, typename ... Args>
    static void printRest(const T & val, Args && ... args) { std::cout << val; printRest(args ...); }

    template<typename T>
    static void printRest(const T & val) { std::cout << val; }

};
