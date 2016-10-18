#pragma once

#include "Window.hpp"
#include "World.hpp"
#include "Shader.hpp"

//==============================================================================
class Voxel
{
public:
    Voxel(const char * location);

    void run();

private:
    Window m_window;
    World m_world;
    Shader m_block_shader;
};
