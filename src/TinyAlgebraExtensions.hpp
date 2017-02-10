#pragma once

#include "Algebra.hpp"
#include <glm/mat4x4.hpp>
#include <glm/glm.hpp>


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