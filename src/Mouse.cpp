#include "Mouse.hpp"
#include <GLFW/glfw3.h>
#include <cassert>

//==============================================================================
Mouse::Status Mouse::s_button[Mouse::BUTTON_COUNT]{};

//==============================================================================
f64Vec2 Mouse::s_position = f64Vec2{ 0.0, 0.0 };
f64Vec2 Mouse::s_last_position{ 0.0, 0.0 };
f64Vec2 Mouse::s_scroll_delta{ 0.0, 0.0 };

//==============================================================================
void Mouse::setPointer(GLFWwindow * window, double x_pos, double y_pos)
{
  s_position = { x_pos, y_pos };
}

//==============================================================================
void Mouse::setButton(GLFWwindow * window, int button, int action, int mods)
{
  if (button < 0 || button >= Mouse::BUTTON_COUNT)
    return;

  // Set pressed keys
  if (action == GLFW_PRESS)
    s_button[button] = Mouse::Status::PRESSED;
  else if (action == GLFW_RELEASE)
    s_button[button] = Mouse::Status::RELEASED;
}

//==============================================================================
void Mouse::setScroll(GLFWwindow * window, double x_offset, double y_offset)
{
  s_scroll_delta += { x_offset, y_offset };
}

//==============================================================================
f64Vec2 Mouse::getPointerMovement()
{
  const auto delta{ s_position - s_last_position };
  s_last_position = s_position;
  return delta;
}

//==============================================================================
f64Vec2 Mouse::getScrollMovement()
{
  const auto delta{ s_scroll_delta };
  s_scroll_delta = { 0.0, 0.0 };
  return delta;
}

//==============================================================================
void Mouse::reset(GLFWwindow * window)
{
  s_scroll_delta = { 0.0, 0.0 };

  glfwGetCursorPos(window, &s_position[0], &s_position[1]);

  s_last_position = s_position;

  for (auto & b : s_button)
    b = Status::RELEASED;
}

//==============================================================================
Mouse::Status Mouse::getButtonStatus(int button)
{
  assert(button >= 0 && button < Mouse::BUTTON_COUNT);
  return s_button[button];
}