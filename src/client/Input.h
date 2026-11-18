#pragma once
// Polled keyboard and mouse state for the main thread. Window owns one and refreshes it after glfwPollEvents.
#include <array>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

namespace vrtc {
    class Input {
        GLFWwindow *window = nullptr;
        std::array<uint8_t, GLFW_KEY_LAST + 1> current{}, previous{};
        std::array<uint8_t, GLFW_MOUSE_BUTTON_LAST + 1> mouseCurrent{}, mousePrevious{};
        glm::dvec2 cursor{};
        glm::dvec2 delta{};
        double scroll = 0; // accumulated since the last Update
        double scrollThisFrame = 0;
        bool captured = false;
        bool skipNextDelta = false; // the first delta after capturing is a jump to ignore

    public:
        void Attach(GLFWwindow *handle);

        // Call once per frame after glfwPollEvents.
        void Update();

        [[nodiscard]] bool Down(int key) const { return current[key] != 0; }

        [[nodiscard]] bool Pressed(int key) const { return current[key] && !previous[key]; }

        [[nodiscard]] bool MouseDown(int button) const { return mouseCurrent[button] != 0; }

        [[nodiscard]] bool MousePressed(int button) const { return mouseCurrent[button] && !mousePrevious[button]; }

        // Cursor movement this frame in pixels; zero while the cursor is not captured.
        [[nodiscard]] glm::vec2 MouseDelta() const { return captured ? glm::vec2(delta) : glm::vec2(0.f); }

        [[nodiscard]] double ScrollDelta() const { return scrollThisFrame; }

        // Hides the cursor and routes its movement to MouseDelta.
        void SetCaptured(bool on);

        [[nodiscard]] bool Captured() const { return captured; }

        void OnScroll(double dy) { scroll += dy; }
    };
}
