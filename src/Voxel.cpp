#include "Voxel.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include "Mouse.hpp"

//==============================================================================
static const std::vector<TextureArray::Source> BLOCK_TEXTURE_SOURCE
{
        { "assets/blocks/grass.png", 1 },
        { "assets/blocks/sand.png", 2 },
};
static const Texture::Filtering BLOCK_TEXTURE_FILTERING
{
        Texture::FarFiltering::LINEAR_TEXEL_LINEAR_MIPMAP,
        Texture::CloseFiltering::LINEAR_TEXEL, 500.0f
};

//==============================================================================
Voxel::Voxel(const char * location) :
    m_window{ Window::Hints{ 3, 1, 0, nullptr, location, 0.9f, 0.9f, 0.6f, 1.0f, false, 960, 540 } },
    m_world{ location },
    m_block_shader{
            {
                    { "shader/block.vert", GL_VERTEX_SHADER },
                    { "shader/block.frag", GL_FRAGMENT_SHADER }
            }
    },
    m_block_textures{ BLOCK_TEXTURE_SOURCE, 64, GL_TEXTURE0, BLOCK_TEXTURE_FILTERING } // TODO: make dynamic texture unit allocation
{
    m_block_shader.use();
    m_block_VP_matrix_location = glGetUniformLocation(m_block_shader.id(), "VP_matrix");
    block_texture_array_location = glGetUniformLocation(m_block_shader.id(), "block_texture_array");
    glUniform1i(block_texture_array_location, GL_TEXTURE0);
}

//==============================================================================
void Voxel::run()
{
    m_window.makeContextCurrent();
    m_window.unlockMouse();

    double last_time = glfwGetTime();

    int frame_counter = 0;
    double last_fps_update = last_time;

    while (!m_window.exitRequested())
    {
        const double current_time = glfwGetTime();
        double delta_time = current_time - last_time;
        last_time = current_time;

        // update FPS counter
        if (current_time - last_fps_update > 1.0 / FRAME_RATE_UPDATE_RATE)
        {
            const double frame_rate = static_cast<double>(frame_counter) / (current_time - last_fps_update);
            std::cout << "FPS: " << frame_rate << std::endl;
            frame_counter = 0;
            last_fps_update = current_time;
        }
        ++frame_counter;

        glfwPollEvents();

        const auto scroll = Mouse::getScrollMovement()(1);
        if (scroll > 0.1) m_window.unlockMouse();
        else if (scroll < -0.1) m_window.lockMouse();

        // update position and stuff
        m_player.updateCameraAndItems();
        m_player.updateVelocity(static_cast<float>(delta_time));
        m_player.applyVelocity(static_cast<float>(delta_time));
        m_camera.updateAspectRatio(static_cast<float>(m_window.aspectRatio()));
        m_camera.update(m_player.getPosition(), m_player.getYaw(), m_player.getPitch());

        m_block_shader.use();
        const glm::mat4 VP_matrix = m_camera.getViewProjectionMatrix();
        glUniformMatrix4fv(m_block_VP_matrix_location, 1, GL_FALSE, glm::value_ptr(VP_matrix));
        m_block_textures.bind(GL_TEXTURE0);
        // TODO: call m_world.draw(center, frustum)
        const auto center = m_player.getPosition();
        m_world.draw(intFloor(fVec3{ center.x, center.y, center.z }));

        m_window.swapResizeClearBuffer();

        // limit frame rate
        const auto time_after_render = glfwGetTime();
        const auto sleep_time = 1.0 / TARGET_FRAME_RATE - (time_after_render - current_time);
        if (sleep_time > 0.0)
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(sleep_time * 1000.0)));
    }

    m_window.unlockMouse();
}
