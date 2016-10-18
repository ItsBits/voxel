#include "Voxel.hpp"
#include <GLFW/glfw3.h>
#include "Mouse.hpp"
#include "Keyboard.hpp"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

//==============================================================================
Voxel::Voxel(const char * location) :
    m_window{ Window::Hints{ 3, 1, 0, nullptr, location, 0.9f, 0.9f, 0.6f, 1.0f, false, 960, 540 } },
    m_world{ location },
    m_block_shader{
            {
                    { "shader/block.vert", GL_VERTEX_SHADER },
                    { "shader/block.frag", GL_FRAGMENT_SHADER }
            }
    }
{
  m_block_shader.use();
  m_block_VP_matrix_location = glGetUniformLocation(m_block_shader.id(), "VP_matrix");
}

//==============================================================================
void Voxel::run()
{
    m_window.makeContextCurrent();
    m_window.lockMouse();

    double last_time = glfwGetTime();

    while (!m_window.exitRequested())
    {
        const double current_time = glfwGetTime();
        double delta_time = current_time - last_time;
        last_time = current_time;

        glfwPollEvents();

        // update position and stuff
        m_player.updateCameraAndItems();
        m_player.updateVelocity(static_cast<float>(delta_time));
        m_player.applyVelocity(static_cast<float>(delta_time));
        m_camera.updateAspectRatio(static_cast<float>(m_window.aspectRatio()));
        m_camera.update(m_player.getPosition(), m_player.getYaw(), m_player.getPitch());

        m_block_shader.use();
        const glm::mat4 VP_matrix = m_camera.getViewProjectionMatrix();
        glUniformMatrix4fv(m_block_VP_matrix_location, 1, GL_FALSE, glm::value_ptr(VP_matrix));
        // TODO: call m_world.draw(center, frustum)
        m_world.draw();

        m_window.swapResizeClearBuffer();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    m_window.unlockMouse();
}
