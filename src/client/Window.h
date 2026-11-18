#pragma once
// Window and GL 4.6 core context. Carried over with the section 19 fixes: stops on failed init, owns the GLFW user
// pointer, guards the aspect ratio against a minimised window, holds WindowDesc by value.
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Input.h"

namespace vrtc {
    struct WindowDesc {
        int width = 1280, height = 720;
        const char *title = "vrtc";
    };

    class Window {
        WindowDesc desc;
        GLFWwindow *handle = nullptr;
        int framebufferWidth = 0, framebufferHeight = 0;
        Input input;

        static void FramebufferSizeCallback(GLFWwindow *window, int width, int height);

        static void ScrollCallback(GLFWwindow *window, double dx, double dy);

    public:
        explicit Window(const WindowDesc &desc);

        ~Window();

        Window(const Window &) = delete;

        Window &operator=(const Window &) = delete;

        [[nodiscard]] GLFWwindow *Handle() const { return handle; }

        [[nodiscard]] Input &GetInput() { return input; }

        // Polls events and refreshes the input state.
        void PollEvents();

        [[nodiscard]] bool ShouldClose() const { return glfwWindowShouldClose(handle); }

        void SwapBuffers() const { glfwSwapBuffers(handle); }

        [[nodiscard]] int FramebufferWidth() const { return framebufferWidth; }

        [[nodiscard]] int FramebufferHeight() const { return framebufferHeight; }

        // True while minimised: nothing to draw into.
        [[nodiscard]] bool IsMinimised() const { return framebufferWidth == 0 || framebufferHeight == 0; }

        // 1 when the framebuffer has no height, so projection matrices never divide by zero.
        [[nodiscard]] float Aspect() const {
            return framebufferHeight > 0 ? static_cast<float>(framebufferWidth) / framebufferHeight : 1.f;
        }
    };
}
