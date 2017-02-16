#pragma once

#include "Algebra.hpp"
#include <chrono>

//==============================================================================
template<typename T> struct Change { T begin, end; };

//==============================================================================
class RenderState
{
public:
    Change<f32Vec3> player;
    Change<float> yaw, pitch;
    [[deprecated]] double scroll;
    Change<std::chrono::time_point<std::chrono::steady_clock>> time;
};
