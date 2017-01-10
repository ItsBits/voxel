#pragma once

#include <cassert>

#define REL_CHUNK

#define SETTINGS_TARGET_FPS 300.0
#define V_SYNC true
#define MSAA_SAMPLES 0
#define RESOLUTION_X 1280
#define RESOLUTION_Y 720

#define MC_TEXTURE_LOC   nullptr // c-string folder location
#define MC_SAVE_FILE_LOC nullptr // c-string folder location

//==============================================================================
template<int S>
class GenericSettings
{
    static_assert(S > 0, "Can't have < 0 settings.");

public:
    GenericSettings() : m_current{ 0 } { for (auto & i : m_values) i = MAX_V /*/ 2*/; }

    void increment()
    {
        if (m_values[m_current] < MAX_V) m_values[m_current]++;
    }

    void decrement()
    {
        if (m_values[m_current] > 0) m_values[m_current]--;
    }

    void change(int value)
    {
        assert(value >= 0 && value < S && "Out of bounds.");
        m_current = value;
    }

    double get(int value) const
    {
        assert(value >= 0 && value < S && "Out of bounds.");
        return static_cast<double>(m_values[value]) / static_cast<double>(MAX_V);
    }

    int getInt(int value) const
    {
        assert(value >= 0 && value < S && "Out of bounds.");
        return m_values[value];
    }

    int current() const
    {
        return m_current;
    }

private:
    int m_values[S];
    int m_current;

    static constexpr int MAX_V{ 100 };
};
