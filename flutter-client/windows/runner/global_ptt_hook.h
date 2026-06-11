#ifndef RUNNER_GLOBAL_PTT_HOOK_H_
#define RUNNER_GLOBAL_PTT_HOOK_H_

#include <windows.h>

#include <functional>
#include <string>

// Custom window messages (posted to the Flutter HWND from the hook thread).
constexpr UINT WM_OWALKIE_PTT_DOWN = WM_APP + 0x501;
constexpr UINT WM_OWALKIE_PTT_UP = WM_APP + 0x502;
constexpr UINT WM_OWALKIE_PTT_CAPTURED = WM_APP + 0x503;

struct GlobalPttCaptureResult {
  int vkey = 0;
  int mods = 0;
  std::string display_name;
};

class GlobalPttHook {
 public:
  static GlobalPttHook& Instance();

  void SetWindow(HWND hwnd);
  void SetBinding(int vkey, int mods);
  void ClearBinding();
  int vkey() const { return vkey_; }
  int mods() const { return mods_; }
  bool HasBinding() const { return vkey_ > 0; }

  bool Install();
  void Uninstall();

  bool StartCapture();
  void CancelCapture();
  bool IsCapturing() const { return capturing_; }

  GlobalPttCaptureResult TakeCaptureResult();

  void HandlePttDown();
  void HandlePttUp();

 private:
  GlobalPttHook() = default;
  ~GlobalPttHook();

  GlobalPttHook(const GlobalPttHook&) = delete;
  GlobalPttHook& operator=(const GlobalPttHook&) = delete;

  static LRESULT CALLBACK LowLevelKeyboardProc(int nCode,
                                               WPARAM wparam,
                                               LPARAM lparam);

  void OnLowLevelKey(int raw_vkey, bool is_key_down, bool is_key_up);
  void CommitCapture(int raw_vkey, int current_mods);
  void EmitPttDown();
  void EmitPttUp();

  HWND hwnd_ = nullptr;
  HHOOK hook_ = nullptr;
  int vkey_ = 0;
  int mods_ = 0;
  bool capturing_ = false;
  bool ptt_pressed_ = false;

  int held_mods_ = 0;
  int pending_modifier_vkey_ = 0;
  int pending_modifier_mods_ = 0;

  GlobalPttCaptureResult capture_result_;
};

std::string PttComboToDisplayName(int vkey, int mods);

#endif  // RUNNER_GLOBAL_PTT_HOOK_H_
