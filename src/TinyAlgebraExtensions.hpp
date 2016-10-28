#pragma once

#include "TinyAlgebra.hpp"
#include <string>
#include <sstream>
#include <glm/mat4x4.hpp>

template<typename T, int S>
std::string toString(Vec<T, S> vector)
{
    std::ostringstream s_stream;

    s_stream << '(' << vector(0);

    for (int i = 1; i < S; ++i) s_stream << ',' << vector(i);

    s_stream << ')';

    return s_stream.str();
};


//==================================================================================================
template<typename T>
void matrixToFrustums(glm::mat4 MVP, T frustum_planes[6])
{
    // Credit: http://gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf

    MVP = glm::transpose(MVP);

    { const auto f = MVP[3] + MVP[0]; frustum_planes[0] = { f.x, f.y, f.z, f.w }; }
    { const auto f = MVP[3] - MVP[0]; frustum_planes[1] = { f.x, f.y, f.z, f.w }; }
    { const auto f = MVP[3] - MVP[1]; frustum_planes[2] = { f.x, f.y, f.z, f.w }; }
    { const auto f = MVP[3] + MVP[1]; frustum_planes[3] = { f.x, f.y, f.z, f.w }; }
    { const auto f = MVP[3] + MVP[2]; frustum_planes[4] = { f.x, f.y, f.z, f.w }; }
    { const auto f = MVP[3] - MVP[2]; frustum_planes[5] = { f.x, f.y, f.z, f.w }; }
}