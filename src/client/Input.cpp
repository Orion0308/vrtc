#include "Input.h"

void vrtc::Input::Attach(GLFWwindow *handle) {
    window = handle;
    glfwGetCursorPos(window, &cursor.x, &cursor.y);
}

void vrtc::Input::Update() {
    previous = current;
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        current[key] = glfwGetKey(window, key) == GLFW_PRESS ? 1 : 0;
    }
    mousePrevious = mouseCurrent;
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        mouseCurrent[button] = glfwGetMouseButton(window, button) == GLFW_PRESS ? 1 : 0;
    }

    glm::dvec2 now;
    glfwGetCursorPos(window, &now.x, &now.y);
    delta = now - cursor;
    cursor = now;
    if (skipNextDelta) {
        delta = {};
        skipNextDelta = false;
    }

    scrollThisFrame = scroll;
    scroll = 0;
}

void vrtc::Input::SetCaptured(bool on) {
    if (on == captured) return;
    captured = on;
    glfwSetInputMode(window, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, on ? GLFW_TRUE : GLFW_FALSE);
    glfwGetCursorPos(window, &cursor.x, &cursor.y);
    skipNextDelta = true;
}
