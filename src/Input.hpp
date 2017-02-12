#pragma once

#include <cstddef>

#include "BitMap.hpp"
#include "Algebra.hpp"

#include <GLFW/glfw3.h>
#include <mutex>
#include <cassert>

/*
 * This is the weird part of the source code. Those are "static classes", because GLFW3 can't handle C++ classes.
 * set* functions should NOT be called by the user! Those are callbacks for GLFW3. Careful, because they are public.
 * Also, do NOT touch the mutex inside of this namespace.
 * Also, do NOT use non-default constructors of *::Snapshot and also avoid using *::getSnapshot(), because reasons...
 *
 * In short, only use:
 *   + setCallbacks()
 *   + pollEventsGetSnapshots()
 *   + results that you get from pollEventsGetSnapshots()
 */
namespace Input
{
    //==================================================================================================================
    extern std::mutex s_io_lock;

    //==================================================================================================================
#ifndef NDEBUG
    extern bool initialized;
#endif

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
            // friend class Keyboard; // obsolete because implicit friend right?

        public:
            //Snapshot() = delete;
            Snapshot() {}

            Snapshot(const BitMap<KEY_COUNT> & keys) : m_keys{ keys } {}

            KeyStatus getKey(const std::size_t key) const
            {
                assert(key < KEY_COUNT);
                return m_keys.get(key) ? KeyStatus::PRESSED : KeyStatus::RELEASED;
            }

        private:

            BitMap<KEY_COUNT> m_keys;

        };

        //==============================================================================================================
        static Snapshot getSnapshot() { return Snapshot{ s_keys }; }

    private:
        static BitMap<KEY_COUNT> s_keys;

    };

    //==================================================================================================================
    class Mouse
    {
    public:
        // Constructor
        Mouse() = delete;

        static constexpr auto KEY_COUNT = std::size_t{ 8 };

        // Setter GLFW callback functions

        //==============================================================================================================
        static void setPointer(GLFWwindow * window, double x_pos, double y_pos)
        {
            s_position = { x_pos, y_pos };
        }

        //==============================================================================================================
        static void setButton(GLFWwindow * window, int button, int action, int mods)
        {
            if (button < 0 || button >= Mouse::KEY_COUNT) return;

            // Set pressed keys
            if (action == GLFW_PRESS)
                s_keys.set(static_cast<std::size_t>(button));
            else if (action == GLFW_RELEASE)
                s_keys.clear(static_cast<std::size_t>(button));
        }

        //==============================================================================================================
        static void setScroll(GLFWwindow * window, double x_offset, double y_offset)
        {
            s_scroll_delta += { x_offset, y_offset };
        }

        //==============================================================================================================
        class Snapshot
        {
            // friend class Mouse; // obsolete because implicit friend right?

        public:
            //Snapshot() = delete;
            Snapshot() {}

            Snapshot(const BitMap<KEY_COUNT> & keys, const f64Vec2 & pointer_movement, const f64Vec2 & scroll_delta) :
              m_keys{ keys }, m_pointer_movement{ pointer_movement }, m_scroll_delta{ scroll_delta }
            {}

            KeyStatus getKey(const std::size_t key) const
            {
                assert(key < KEY_COUNT);
                return m_keys.get(key) ? KeyStatus::PRESSED : KeyStatus::RELEASED;
            }

            f64Vec2 getPointerMovement() const { return m_pointer_movement; }
            f64Vec2 getScrollMovement() const { return m_scroll_delta; }

        private:
            BitMap<KEY_COUNT> m_keys;

            // Pointer position
            f64Vec2 m_pointer_movement;

            // Scroll position
            f64Vec2 m_scroll_delta;

        };

        //==============================================================================================================
        static Snapshot getSnapshot() // TODO: change this, do not reset values, the user should handle this himselves
        {
            const auto last_position = s_last_position;
            s_last_position = s_position;

            const auto scroll_delta = s_scroll_delta;
            s_scroll_delta = { 0.0, 0.0 };

            return Snapshot{ s_keys, s_position - last_position, scroll_delta };
        }

    private:
        static BitMap<KEY_COUNT> s_keys;

        // Pointer position
        static f64Vec2 s_position, s_last_position;

        // Scroll position
        static f64Vec2 s_scroll_delta;

    };

    //==================================================================================================================
    void pollEventsGetSnapshots(Keyboard::Snapshot & keyboard_snapshot, Mouse::Snapshot & mouse_snapshot);

    void setCallbacks(GLFWwindow * window);

}