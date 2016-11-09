#pragma once

class Profiler
{
public:
    Profiler() = delete;

    enum class Task : int { ChunksLoaded, MeshesGenerated, last };

    static void add(Task task, int value)
    {
        values[static_cast<int>(task)] += value;
    }

    static void reset(Task task)
    {
      values[static_cast<int>(task)] = 0;
    }

    static int get(Task task)
    {
      return values[static_cast<int>(task)];
    }

    static void resetAll()
    {
        for (auto & i : values) i = 0;
    }

private:
    static int values[static_cast<int>(Task::last)];

};
