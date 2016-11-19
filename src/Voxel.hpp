#pragma once

#include "Window.hpp"
#include "World.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "Player.hpp"
#include "TextureArray.hpp"
#include "Text.hpp"

//==============================================================================
class Voxel
{
public:
    Voxel(const std::string & name);

    void run();

private:
    Window m_window;
    World m_world;
    Shader m_block_shader;
    Camera<float> m_camera;
    GLint m_block_VP_matrix_location;
    Player m_player;
    TextureArray m_block_textures;

    Shader m_text_shader;
    Text m_screen_text;
    GLint m_text_ratio_location;
    GLint m_font_size_location;
    TextureArray m_font_textures;

    static constexpr double FRAME_RATE_UPDATE_RATE{ 1.0 };
    static constexpr double TARGET_FRAME_RATE{ 80.0 }; // TODO: figure out why low value < 50.0 makes the keyboard feel sticky

};
