#pragma once

struct GLFWwindow;

//==============================================================================
class Keyboard
{
public:
  // Possible key status
  enum class Status : bool { PRESSED  = true, RELEASED = false };

  // Constructor
  Keyboard() = delete;

  // Setter GLFW callback function
  static void setKey(GLFWwindow * window, int key, int scancode, int action, int mods);

  // Get keyboard button status
  static Status getKey(int key);

private:
  static constexpr auto KEY_COUNT{ 350 };

  static Status s_key[KEY_COUNT];

};