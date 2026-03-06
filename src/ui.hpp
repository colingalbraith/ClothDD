/// @file ui.hpp
/// @brief ImGui integration — panel drawing, input callbacks, and lifecycle.

#pragma once

#include "app.hpp"

#include <GLFW/glfw3.h>

namespace clothdd {

void initImGui(GLFWwindow* window);
void beginImGuiFrame();
void drawImGuiPanel(AppState& app, float fps);
void renderImGuiFrame();
void shutdownImGui();

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void cursorPositionCallback(GLFWwindow* window, double x, double y);
void scrollCallback(GLFWwindow* window, double xOffset, double yOffset);
void charCallback(GLFWwindow* window, unsigned int character);

}  // namespace clothdd
