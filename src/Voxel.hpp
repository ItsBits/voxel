#pragma once

#include "Settings.hpp"
#include "Window.hpp"
#include "World.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "Player.hpp"
#include "TextureArray.hpp"
#include "Text.hpp"
#include "TripleBuffer.hpp"
#include "RenderState.hpp"

//==============================================================================
class Voxel
{
public:
    Voxel(const std::string &name, const std::size_t TPS);

    void run();

private:
    Window m_window;
    World m_world;
    Shader m_block_shader;
    Camera<float> m_camera;
    GLint m_block_VP_matrix_location;
    GLint m_block_light_location;
    GLint m_block_lighting_location;
    GLint m_chunk_position_location;
    Player m_player;
    TextureArray m_block_textures;

    Shader m_text_shader;
    Text m_screen_text;
    GLint m_text_ratio_location;
    GLint m_font_size_location;
    TextureArray m_font_textures;

#define LIGHT_L 0
#define RED_L 1
#define GRE_L 2
#define BLU_L 3
#define SPD_P 4
    GenericSettings<10> m_settings;

    static constexpr double FRAME_RATE_UPDATE_RATE{ 6.0 };

    static constexpr double TARGET_FRAME_RATE{ // TODO: figure out why low value < 50.0 makes the keyboard feel sticky (GLFW fault!)
            SETTINGS_TARGET_FPS
    };

    void updateSettings(const Input::Keyboard::Snapshot & keyboard_snapshot);

    void logic_loop();
    void logic_step(const std::size_t tick, const unsigned int state_index);
    void render_loop();
    void render_loop_old();

    std::size_t m_TPS;

    std::thread m_logic_thread;

    TripleBuffer m_triple_buffer;

    // TODO: lerp
    // TODO: figure out initialization (renderer starts with garbage)
    // TODO: figure out the latency and wether it can be reduced
    RenderState m_render_state[3];

};
