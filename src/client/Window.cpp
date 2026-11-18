#include "Window.h"

#include "Log.h"

vrtc::Window::Window(const WindowDesc &desc) : desc(desc) {
    if (!glfwInit()) log::Fatal("window: glfwInit failed");

    // The shaders are #version 460 core, so ask for a matching context.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE); // section 10: the framebuffer is sRGB

    handle = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    if (!handle) {
        glfwTerminate();
        log::Fatal("window: glfwCreateWindow failed (is OpenGL 4.6 core available?)");
    }
    glfwMakeContextCurrent(handle);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(handle);
        glfwTerminate();
        log::Fatal("window: gladLoadGLLoader failed");
    }
    if (!GLAD_GL_VERSION_4_6) log::Fatal("window: OpenGL 4.6 is required");

    glfwSetWindowUserPointer(handle, this); // the window owns its own user pointer (section 19)
    glfwSetFramebufferSizeCallback(handle, FramebufferSizeCallback);
    glfwSetScrollCallback(handle, ScrollCallback);
    glfwGetFramebufferSize(handle, &framebufferWidth, &framebufferHeight);
    glfwSwapInterval(0); // vsync off so frame times measure real cost

    input.Attach(handle);
    log::Info("window: {}x{}, {}", framebufferWidth, framebufferHeight,
              reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
}

vrtc::Window::~Window() {
    if (handle) glfwDestroyWindow(handle);
    glfwTerminate();
}

void vrtc::Window::PollEvents() {
    glfwPollEvents();
    input.Update();
}

void vrtc::Window::FramebufferSizeCallback(GLFWwindow *window, int width, int height) {
    auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
    self->framebufferWidth = width;
    self->framebufferHeight = height;
    if (width > 0 && height > 0) glViewport(0, 0, width, height);
}

void vrtc::Window::ScrollCallback(GLFWwindow *window, double, double dy) {
    auto *self = static_cast<Window *>(glfwGetWindowUserPointer(window));
    self->input.OnScroll(dy);
}
