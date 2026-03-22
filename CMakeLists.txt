cmake_minimum_required(VERSION 3.16)
project(ClothDD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer optimized binaries for interactive simulation when no build type was provided.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

find_package(glad CONFIG QUIET)
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
  message(STATUS "glfw3 not found locally – fetching via FetchContent...")
  include(FetchContent)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
  )
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
endif()
find_package(OpenEXR QUIET)
find_package(Imath QUIET)

if(TARGET glfw::glfw)
  set(GLFW_TARGET glfw::glfw)
else()
  set(GLFW_TARGET glfw)
endif()

set(IMGUI_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
if(NOT EXISTS "${IMGUI_SOURCE_DIR}/imgui.cpp")
  message(FATAL_ERROR "Vendored ImGui not found at ${IMGUI_SOURCE_DIR}.")
endif()

add_executable(clothdd
  src/app.cpp
  src/cloth.cpp
  src/gpu_solver.cpp
  src/hdri.cpp
  src/main.cpp
  src/render.cpp
  src/texture.cpp
  src/ui.cpp
  ${IMGUI_SOURCE_DIR}/imgui.cpp
  ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
  ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
  ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
  ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)

target_include_directories(clothdd PRIVATE
  src
  ${IMGUI_SOURCE_DIR}
  ${IMGUI_SOURCE_DIR}/backends
)
set_source_files_properties(
  src/app.cpp
  src/cloth.cpp
  src/hdri.cpp
  src/main.cpp
  src/render.cpp
  src/texture.cpp
  src/ui.cpp
  PROPERTIES
  COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>"
  COMPILE_DEFINITIONS "$<$<PLATFORM_ID:Darwin>:GL_SILENCE_DEPRECATION>"
)

if(OpenEXR_FOUND)
  message(STATUS "OpenEXR found – HDRI background enabled.")
  target_compile_definitions(clothdd PRIVATE CLOTHDD_HAVE_OPENEXR)
  target_link_libraries(clothdd PRIVATE OpenEXR::OpenEXR Imath::Imath)
else()
  message(STATUS "OpenEXR not found – HDRI background disabled.")
endif()

if(glad_FOUND)
  message(STATUS "GLAD found – GPU compute enabled.")
  target_compile_definitions(clothdd PRIVATE CLOTHDD_HAVE_GLAD)
  target_link_libraries(clothdd PRIVATE glad::glad)
else()
  message(STATUS "GLAD not found – GPU compute disabled, using legacy GL.")
endif()

if(APPLE)
  target_link_libraries(clothdd PRIVATE ${GLFW_TARGET} "-framework OpenGL")
elseif(WIN32)
  target_link_libraries(clothdd PRIVATE ${GLFW_TARGET} opengl32)
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(clothdd PRIVATE ${GLFW_TARGET} OpenGL::GL)
endif()
