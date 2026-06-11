#ifndef RUNNER_GLOBAL_PTT_PLUGIN_H_
#define RUNNER_GLOBAL_PTT_PLUGIN_H_

#include <flutter/flutter_engine.h>

#include <windows.h>

class GlobalPttPlugin {
 public:
  static GlobalPttPlugin& Instance();

  void Register(flutter::FlutterEngine* engine, HWND hwnd);
  void Unregister();

  void OnPttDown();
  void OnPttUp();
  void OnCaptured();

 private:
  GlobalPttPlugin() = default;

  void EmitEvent(const char* event);

  flutter::FlutterEngine* engine_ = nullptr;
  HWND hwnd_ = nullptr;
};

void RegisterGlobalPttPlugin(flutter::FlutterEngine* engine, HWND hwnd);

#endif  // RUNNER_GLOBAL_PTT_PLUGIN_H_
