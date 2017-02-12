#pragma once

#include "Algebra.hpp"

//==============================================================================
template<typename T> struct Movement { T begin, end; };

//==============================================================================
class RenderState
{
public:
    Movement<f32Vec3> m_player;

};
