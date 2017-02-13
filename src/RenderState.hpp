#pragma once

#include "Algebra.hpp"

//==============================================================================
template<typename T> struct Movement { T begin, end; };

//==============================================================================
class RenderState
{
public:
    Movement<f32Vec3> player;
    float yaw, pitch;
    [[deprecated]] double scroll;

};
