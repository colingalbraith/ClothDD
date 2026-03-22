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

#ifdef CLOTHDD_HAVE_GLAD
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#endif
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_SAMPLES, 2);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "ClothDD", nullptr, nullptr);
  if (window == nullptr) {
    std::fprintf(stderr, "Failed to create OpenGL window.\n");
    glfwTerminate();
    return EXIT_FAILURE;
  }

  glfwMakeContextCurrent(window);
#ifdef CLOTHDD_HAVE_GLAD
  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    std::fprintf(stderr, "Failed to initialize GLAD.\n");
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_FAILURE;
  }
  std::printf("OpenGL %s (GPU compute enabled)\n", glGetString(GL_VERSION));
#endif
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

    {
      const double simStart = glfwGetTime();
      stepSimulation(app, now, dt);
      const double simEnd = glfwGetTime();
      app.simTimeMs = static_cast<float>((simEnd - simStart) * 1000.0);
    }

    int framebufferWidth = 1;
    int framebufferHeight = 1;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    {
      const double renderStart = glfwGetTime();
      renderFrame(app, framebufferWidth, framebufferHeight);
      renderImGuiFrame();
      const double renderEnd = glfwGetTime();
      app.renderTimeMs = static_cast<float>((renderEnd - renderStart) * 1000.0);
    }

    app.frameTimeMs = dt * 1000.0f;
    glfwSwapBuffers(window);

    titleAccumulator += dt;
    ++titleFrames;
    if (titleAccumulator >= 0.4) {
      const float titleFps = static_cast<float>(titleFrames / titleAccumulator);
      const char* presetName = (app.scenePreset == ScenePreset::ExtremeDense)
                                   ? "Extreme Dense"
                               : (app.scenePreset == ScenePreset::UltraDense)
                                   ? "Ultra Dense"
                               : (app.scenePreset == ScenePreset::DenseShowcase)
                                   ? "Dense Showcase"
                               : (app.scenePreset == ScenePreset::BallDrop)
                                   ? "Ball Drop"
                                   : "Baseline";
      const char* domainLayout = app.squareDomainDecomposition ? "square" : "strips";
      char title[256];
      std::snprintf(title,
                    sizeof(title),
                    "ClothDD (%s) | FPS %.0f | %dx%d | iters %d substeps %d | domains %d %s | threads %d",
                    presetName,
                    titleFps,
                    app.cloth.width(),
                    app.cloth.height(),
                    app.solverIterations,
                    app.substeps,
                    app.domainCount,
                    domainLayout,
                    app.cloth.workerThreadCount());
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
