/// @file main.cpp
/// @brief Application entry point — window creation, main loop, and shutdown.

#include "app.hpp"
#include "render.hpp"
#include "ui.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

int main() {
  using namespace clothdd;

  if (glfwInit() == GLFW_FALSE) {
    std::fprintf(stderr, "Failed to initialize GLFW.\n");
    return EXIT_FAILURE;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "ClothDD", nullptr, nullptr);
  if (window == nullptr) {
    std::fprintf(stderr, "Failed to create OpenGL window.\n");
    glfwTerminate();
    return EXIT_FAILURE;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(0);
  initImGui(window);

  AppState app;
  applyBaseline(app);

  glfwSetWindowUserPointer(window, &app);
  glfwSetKeyCallback(window, keyCallback);
  glfwSetMouseButtonCallback(window, mouseButtonCallback);
  glfwSetCursorPosCallback(window, cursorPositionCallback);
  glfwSetScrollCallback(window, scrollCallback);
  glfwSetCharCallback(window, charCallback);

  configureOpenGLState();
  printControls();

  double previousTime = glfwGetTime();
  double titleAccumulator = 0.0;
  int titleFrames = 0;

  while (glfwWindowShouldClose(window) == GLFW_FALSE) {
    const double now = glfwGetTime();
    float dt = static_cast<float>(now - previousTime);
    previousTime = now;
    dt = std::min(dt, 1.0f / 20.0f);

    updateSmoothedFps(app, dt);
    glfwPollEvents();
    pollCameraKeys(window, app, dt);

    beginImGuiFrame();
    drawImGuiPanel(app, app.smoothedFps);

    stepSimulation(app, now, dt);

    int framebufferWidth = 1;
    int framebufferHeight = 1;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    renderFrame(app, framebufferWidth, framebufferHeight);

    renderImGuiFrame();
    glfwSwapBuffers(window);

    titleAccumulator += dt;
    ++titleFrames;
    if (titleAccumulator >= 0.4) {
      const float titleFps = static_cast<float>(titleFrames / titleAccumulator);
      const char* presetName = (app.scenePreset == ScenePreset::DenseShowcase)
                                   ? "Dense Showcase"
                               : (app.scenePreset == ScenePreset::BallDrop)
                                   ? "Ball Drop"
                               : (app.scenePreset == ScenePreset::UltraDense)
                                   ? "Ultra Dense"
                                   : "Baseline";
      const char* domainLayout = app.squareDomainDecomposition ? "square" : "strips";
      char title[256];
      std::snprintf(title,
                    sizeof(title),
                    "ClothDD (%s) | FPS %.0f | %dx%d | iters %d substeps %d | domains %d %s",
                    presetName,
                    titleFps,
                    app.cloth.width(),
                    app.cloth.height(),
                    app.solverIterations,
                    app.substeps,
                    app.domainCount,
                    domainLayout);
      glfwSetWindowTitle(window, title);
      titleAccumulator = 0.0;
      titleFrames = 0;
    }
  }

  shutdownImGui();
  glfwDestroyWindow(window);
  glfwTerminate();
  return EXIT_SUCCESS;
}
