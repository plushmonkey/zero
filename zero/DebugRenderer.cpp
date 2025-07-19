#include "DebugRenderer.h"

#ifdef GLFW_AVAILABLE
#include <GLFW/glfw3.h>
#endif

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/Logger.h>

namespace zero {

#ifdef GLFW_AVAILABLE

GLFWwindow* CreateGameWindow(int& width, int& height);

static void OnKeyboardChange(GLFWwindow* window, int key, int scancode, int key_action, int mods) {
  ZeroBot* bot = (ZeroBot*)glfwGetWindowUserPointer(window);

  if (!bot) return;

  if (key == GLFW_KEY_F10 && key_action == GLFW_RELEASE) {
    bot->bot_controller->actuator.enabled = !bot->bot_controller->actuator.enabled;
  }

  if (bot->bot_controller->actuator.enabled) return;

  switch (key) {
    case GLFW_KEY_LEFT: {
      bot->input.SetAction(InputAction::Left, key_action != GLFW_RELEASE);
    } break;
    case GLFW_KEY_RIGHT: {
      bot->input.SetAction(InputAction::Right, key_action != GLFW_RELEASE);
    } break;
    case GLFW_KEY_UP: {
      bot->input.SetAction(InputAction::Forward, key_action != GLFW_RELEASE);
    } break;
    case GLFW_KEY_DOWN: {
      bot->input.SetAction(InputAction::Backward, key_action != GLFW_RELEASE);
    } break;
    case GLFW_KEY_LEFT_SHIFT: {
      bot->input.SetAction(InputAction::Afterburner, key_action != GLFW_RELEASE);
    } break;
    case GLFW_KEY_LEFT_CONTROL: {
      bot->input.SetAction(InputAction::Bullet, key_action != GLFW_RELEASE);
    } break;
    case GLFW_KEY_TAB: {
      bot->input.SetAction(InputAction::Bomb, key_action != GLFW_RELEASE);
    } break;
    default: {
    } break;
  }
}

bool DebugRenderer::Initialize(s32 surface_width, s32 surface_height) {
  this->window = CreateGameWindow(surface_width, surface_height);

  if (!this->window) {
    Log(LogLevel::Error, "Failed to create GLFW window.");
    glfwTerminate();
    return false;
  }

  return true;
}

bool DebugRenderer::Begin() {
  if (!window) return true;
  if (glfwWindowShouldClose(window)) return false;

  glfwPollEvents();

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  return true;
}

void DebugRenderer::Present() {
  glfwSwapBuffers(window);
}

void DebugRenderer::Close() {
  if (!window) return;

  glfwDestroyWindow(window);
  window = nullptr;
}

GLFWwindow* CreateGameWindow(int& width, int& height) {
  GLFWwindow* window = nullptr;

  if (!glfwInit()) {
    Log(LogLevel::Error, "Failed to initialize window system.");
    return nullptr;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, false);
  glfwWindowHint(GLFW_SAMPLES, 0);

  // TODO: monitor selection
  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* mode = glfwGetVideoMode(monitor);

  if (g_Settings.window_type == WindowType::Fullscreen) {
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

    width = mode->width;
    height = mode->height;

    window = glfwCreateWindow(width, height, "zero", monitor, NULL);
  } else if (g_Settings.window_type == WindowType::BorderlessFullscreen) {
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    width = mode->width;
    height = mode->height;

    window = glfwCreateWindow(width, height, "zero", NULL, NULL);
  } else {
    window = glfwCreateWindow(width, height, "zero", NULL, NULL);
  }

  if (!window) {
    return nullptr;
  }

  glfwMakeContextCurrent(window);
  glfwSetKeyCallback(window, OnKeyboardChange);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    Log(LogLevel::Error, "Failed to initialize opengl context");
    return nullptr;
  }

  glViewport(0, 0, width, height);

  // Don't enable vsync with borderless fullscreen because glfw does dwm flushes instead.
  // There seems to be a bug with glfw that causes screen tearing if this is set.
  if (!(g_Settings.vsync && g_Settings.window_type == WindowType::BorderlessFullscreen)) {
    glfwSwapInterval(g_Settings.vsync);
  }

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  return window;
}

void DebugRenderer::SetWindowUserPointer(void* user) {
  glfwSetWindowUserPointer(window, user);
}

#else  // GLFW_AVAILABLE not set

bool DebugRenderer::Initialize(s32 surface_width, s32 surface_height) {
  return false;
}

void DebugRenderer::Close() {}

// Returns false if the window is closed and should be terminated.
bool DebugRenderer::Begin() {
  return false;
}

void DebugRenderer::Present() {}

void DebugRenderer::SetWindowUserPointer(void* user) {}

#endif

}  // namespace zero
