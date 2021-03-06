#pragma once

#include "Algebra.hpp"

struct GLFWwindow;

class Mouse
{
public:
  // Possible button status
  enum class Status : bool { PRESSED  = true, RELEASED = false };

  // Constructor
  Mouse() = delete;

  // Setter GLFW callback functions
  static void setPointer(GLFWwindow * window, double x_pos, double y_pos);
  static void setButton(GLFWwindow * window, int button, int action, int mods);
  static void setScroll(GLFWwindow * window, double x_offset, double y_offset);

  static f64Vec2 getPointerMovement();
  static f64Vec2 getScrollMovement();
  static Status getButtonStatus(int button);

  static void reset(GLFWwindow * window);

private:
  static constexpr int BUTTON_COUNT{ 8 };

  // Button status
  static Status s_button[BUTTON_COUNT];

  // Pointer position
  static f64Vec2 s_position, s_last_position;

  // Scroll position
  static f64Vec2 s_scroll_delta;

};