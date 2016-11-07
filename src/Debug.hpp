#pragma once

#include <iostream>
#include <mutex>

class Debug
{
public:
    Debug() = delete;

    template<typename T, typename ... Args>
    static void printAlways(const T & val, Args && ... args)
    {
        std::unique_lock<std::mutex> lock{ m_lock };

        std::cout << '[' << val << "] ";
        printRest(args ...);
        std::cout << std::endl;
    }

    template<typename T, typename ... Args>
    static void print(const T & val, Args && ... args)
    {
      if (!DISPLAY_DEBUG)
          return;
      else
          printAlways(val, args ...);
    }

private:
    static constexpr bool DISPLAY_DEBUG{ false };

    static std::mutex m_lock;

    template<typename T, typename ... Args>
    static void printRest(const T & val, Args && ... args) { std::cout << val; printRest(args ...); }

    template<typename T>
    static void printRest(const T & val) { std::cout << val; }

    static void printRest() {}

};
