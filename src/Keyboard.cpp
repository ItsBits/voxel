#include "Keyboard.hpp"
#include "Debug.hpp"
#include <GLFW/glfw3.h>
#include <cassert>

//==============================================================================
Keyboard::Status Keyboard::s_key[Keyboard::KEY_COUNT]{};

//==============================================================================
void Keyboard::setKey(GLFWwindow * window, int key, int scancode, int action, int mods)
{
    // if unknown key do nothing
    if(key < 0 || key > Keyboard::KEY_COUNT - 1)
        return;

    // Set pressed keys
    if (action == GLFW_PRESS)
        s_key[key] = Status::PRESSED;
    else if (action == GLFW_RELEASE)
        s_key[key] = Status::RELEASED;

    // Quit on escape
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GL_TRUE);
}

//==============================================================================
Keyboard::Status Keyboard::getKey(int key)
{
    assert(key >= 0 && key < KEY_COUNT);
    return s_key[key];
}
