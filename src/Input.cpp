#include "Input.hpp"

//==============================================================================
std::mutex Input::s_io_lock;

//==============================================================================
#ifndef NDEBUG
bool Input::initialized = false;
#endif

//==============================================================================
void Input::pollEventsGetSnapshots( // TODO: structured bindings
  Keyboard::Snapshot & keyboard_snapshot,
  Mouse::Snapshot & mouse_snapshot
)
{
    std::unique_lock<std::mutex> lock{ s_io_lock };

#ifndef NDEBUG
    assert(initialized && "Input callbacks have not been set.");
#endif

    glfwPollEvents();

    keyboard_snapshot = Input::Keyboard::getSnapshot();
    mouse_snapshot    = Input::Mouse::   getSnapshot();
}

//==============================================================================
void Input::setCallbacks(GLFWwindow * window)
{
    std::unique_lock<std::mutex> lock{ s_io_lock };

#ifndef NDEBUG
    assert(!initialized && "Input callbacks have already been set.");
    initialized = true;
#endif

    glfwSetKeyCallback(window, Input::Keyboard::setKey);
    glfwSetCursorPosCallback(window, Input::Mouse::setPointer);
    glfwSetMouseButtonCallback(window, Input::Mouse::setButton);
    glfwSetScrollCallback(window, Input::Mouse::setScroll);
}

//==============================================================================
BitMap<Input::Keyboard::KEY_COUNT> Input::Keyboard::s_keys;

//==============================================================================
BitMap<Input::Mouse::KEY_COUNT> Input::Mouse::s_keys;

//==============================================================================
f64Vec2 Input::Mouse::s_position = f64Vec2{ 0.0, 0.0 };
f64Vec2 Input::Mouse::s_last_position{ 0.0, 0.0 };
f64Vec2 Input::Mouse::s_scroll_delta{ 0.0, 0.0 };