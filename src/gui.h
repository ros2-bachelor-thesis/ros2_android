#pragma once

#include <EGL/egl.h>
#include <GLES/gl.h>
#include <android/native_window.h>

#include <atomic>
#include <future>
#include <thread>

#include "controller.h"
#include "events.h"

namespace sensors_for_ros {
class GUI {
 public:
  GUI();
  ~GUI();

  void Start(ANativeWindow* window);
  void Stop();

  void HandleTouchEvent(int32_t action, float x, float y, int32_t tool_type);

  void SetController(Controller* controller);

 private:
  void InitializeDearImGui(ANativeWindow* window);
  void TerminateDearImGui();
  bool InitializeDisplay(ANativeWindow* window);
  void TerminateDisplay();
  void DrawFrame();
  void ShowROSDomainIdPicker();
  void DrawingLoop(ANativeWindow* window,
                   std::promise<void> promise_first_frame);

  EGLDisplay display_;
  EGLSurface surface_;
  EGLContext context_;
  int32_t width_;
  int32_t height_;

  std::thread draw_thread_;
  std::atomic<bool> exit_loop_;

  Controller* active_controller = nullptr;
};
}  // namespace sensors_for_ros
