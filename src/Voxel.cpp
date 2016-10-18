#include "Voxel.hpp"
#include <GLFW/glfw3.h>
#include "Mouse.hpp"
#include "Keyboard.hpp"
#include <iostream>
#include <thread>

//==============================================================================
Voxel::Voxel(const char * location) :
    m_window{ Window::Hints{ 3, 1, 0, nullptr, location, 0.9f, 0.9f, 0.6f, 1.0f, false, 960, 540 } },
    m_world{ location }
{}

//==============================================================================
void Voxel::run()
{
    m_window.makeContextCurrent();
    m_window.lockMouse();

    while (!m_window.exitRequested())
    {
        glfwPollEvents();

        const auto mouse_pos = Mouse::getPointerMovement();
        const auto mouse_scroll = Mouse::getScrollMovement();
        const auto button_1 = Mouse::getButtonStatus(0);
        const auto button_2 = Mouse::getButtonStatus(1);
        const auto key_u = Keyboard::getKey(GLFW_KEY_U);

        std::cout
                << mouse_pos(0) << '|' << mouse_pos(1) << '|'
                << mouse_scroll(0) << '|' << mouse_scroll(1) << '|'
                << static_cast<bool>(button_1) << '|'
                << static_cast<bool>(button_2) << '|'
                << static_cast<bool>(key_u)
                << std::endl;

        // TODO: glUseShader(block_shader.id());

        // TODO: call m_world.draw(center, frustum)

        m_window.swapResizeClearBuffer();

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    m_window.unlockMouse();
}
