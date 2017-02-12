#pragma once

#include <cstddef>

#include "BitMap.hpp"

#include <GLFW/glfw3.h>

namespace Input
{
    //==================================================================================================================
    enum class KeyStatus : bool { PRESSED  = true, RELEASED = false };

    //==================================================================================================================
    class Keyboard
    {
    public:
        // Constructor
        Keyboard() = delete;

        static constexpr auto KEY_COUNT = std::size_t{ 350 };

        //==============================================================================================================
        // Setter GLFW callback function
        static void setKey(GLFWwindow * window, int key, int scancode, int action, int mods)
        {
            // if unknown key do nothing
            if (key < 0 || key > KEY_COUNT - 1) return;

            // Set pressed keys
            if (action == GLFW_PRESS)
                s_keys.set(static_cast<std::size_t>(key));
            else if (action == GLFW_RELEASE)
                s_keys.clear(static_cast<std::size_t>(key));

            // Quit on escape
            if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
                glfwSetWindowShouldClose(window, GL_TRUE);
        }

        //==============================================================================================================
        class Snapshot
        {
        public:
            Snapshot(const BitMap<KEY_COUNT> & keys) : m_keys{ keys } {}

            KeyStatus getKey(const std::size_t key) const
            {
                assert(key < KEY_COUNT);
                return m_keys.get(key) ? KeyStatus::PRESSED : KeyStatus::RELEASED;
            }

        private:
            const BitMap<KEY_COUNT> m_keys;

        };

        //==============================================================================================================
        static Snapshot getSnapshot() { return Snapshot{ s_keys }; }

    private:
        static BitMap<KEY_COUNT> s_keys;

    };
}