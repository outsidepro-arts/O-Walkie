#include "global_ptt_hook.h"

#include "global_ptt_plugin.h"

#include <algorithm>

namespace {

constexpr int kPttModShift = 1;
constexpr int kPttModCtrl = 2;
constexpr int kPttModAlt = 4;
constexpr int kPttModLShift = 1 << 3;
constexpr int kPttModRShift = 1 << 4;
constexpr int kPttModLCtrl = 1 << 5;
constexpr int kPttModRCtrl = 1 << 6;
constexpr int kPttModLAlt = 1 << 7;
constexpr int kPttModRAlt = 1 << 8;

int CurrentModifierMask() {
  int mods = 0;
  const bool l_shift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
  const bool r_shift = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
  const bool l_ctrl = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
  const bool r_ctrl = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
  const bool l_alt = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
  const bool r_alt = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
  if (l_shift || r_shift) {
    mods |= kPttModShift;
  }
  if (l_ctrl || r_ctrl) {
    mods |= kPttModCtrl;
  }
  if (l_alt || r_alt) {
    mods |= kPttModAlt;
  }
  if (l_shift) {
    mods |= kPttModLShift;
  }
  if (r_shift) {
    mods |= kPttModRShift;
  }
  if (l_ctrl) {
    mods |= kPttModLCtrl;
  }
  if (r_ctrl) {
    mods |= kPttModRCtrl;
  }
  if (l_alt) {
    mods |= kPttModLAlt;
  }
  if (r_alt) {
    mods |= kPttModRAlt;
  }
  return mods;
}

bool IsModifierVKey(int vkey) {
  return vkey == VK_SHIFT || vkey == VK_LSHIFT || vkey == VK_RSHIFT ||
         vkey == VK_CONTROL || vkey == VK_LCONTROL || vkey == VK_RCONTROL ||
         vkey == VK_MENU || vkey == VK_LMENU || vkey == VK_RMENU;
}

int ModifierBitsForVKey(int vkey) {
  switch (vkey) {
    case VK_LSHIFT:
      return kPttModShift | kPttModLShift;
    case VK_RSHIFT:
      return kPttModShift | kPttModRShift;
    case VK_SHIFT:
      return kPttModShift;
    case VK_LCONTROL:
      return kPttModCtrl | kPttModLCtrl;
    case VK_RCONTROL:
      return kPttModCtrl | kPttModRCtrl;
    case VK_CONTROL:
      return kPttModCtrl;
    case VK_LMENU:
      return kPttModAlt | kPttModLAlt;
    case VK_RMENU:
      return kPttModAlt | kPttModRAlt;
    case VK_MENU:
      return kPttModAlt;
    default:
      return 0;
  }
}

int NormalizeModifierMask(int mods) {
  mods &= ~(kPttModShift | kPttModCtrl | kPttModAlt);
  if ((mods & (kPttModLShift | kPttModRShift)) != 0) {
    mods |= kPttModShift;
  }
  if ((mods & (kPttModLCtrl | kPttModRCtrl)) != 0) {
    mods |= kPttModCtrl;
  }
  if ((mods & (kPttModLAlt | kPttModRAlt)) != 0) {
    mods |= kPttModAlt;
  }
  return mods;
}

int StripPrimaryModifierFamily(int mods, int vkey) {
  switch (vkey) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
      mods &= ~(kPttModShift | kPttModLShift | kPttModRShift);
      break;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
      mods &= ~(kPttModCtrl | kPttModLCtrl | kPttModRCtrl);
      break;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
      mods &= ~(kPttModAlt | kPttModLAlt | kPttModRAlt);
      break;
    default:
      break;
  }
  return mods;
}

std::string VKeyToDisplayName(int vkey) {
  if (vkey <= 0) {
    return "Not set";
  }
  const UINT scan = MapVirtualKeyA(static_cast<UINT>(vkey), MAPVK_VK_TO_VSC);
  LONG lparam = static_cast<LONG>(scan << 16);
  char name[128]{};
  const int n = GetKeyNameTextA(lparam, name, static_cast<int>(sizeof(name)));
  if (n > 0) {
    return std::string(name, name + n);
  }
  return "VK " + std::to_string(vkey);
}

bool IsPrimaryKeyMatch(int saved_vkey, int pressed_vkey) {
  if (saved_vkey == pressed_vkey) {
    return true;
  }
  if (saved_vkey == VK_SHIFT) {
    return pressed_vkey == VK_LSHIFT || pressed_vkey == VK_RSHIFT;
  }
  if (saved_vkey == VK_CONTROL) {
    return pressed_vkey == VK_LCONTROL || pressed_vkey == VK_RCONTROL;
  }
  if (saved_vkey == VK_MENU) {
    return pressed_vkey == VK_LMENU || pressed_vkey == VK_RMENU;
  }
  return false;
}

bool AreRequiredModsPressed(int required, int current) {
  const auto has_all = [&](int mask) { return (current & mask) == mask; };
  if ((required & kPttModLCtrl) != 0 && !has_all(kPttModLCtrl)) {
    return false;
  }
  if ((required & kPttModRCtrl) != 0 && !has_all(kPttModRCtrl)) {
    return false;
  }
  if ((required & kPttModLShift) != 0 && !has_all(kPttModLShift)) {
    return false;
  }
  if ((required & kPttModRShift) != 0 && !has_all(kPttModRShift)) {
    return false;
  }
  if ((required & kPttModLAlt) != 0 && !has_all(kPttModLAlt)) {
    return false;
  }
  if ((required & kPttModRAlt) != 0 && !has_all(kPttModRAlt)) {
    return false;
  }

  const bool requires_generic_ctrl =
      ((required & kPttModCtrl) != 0) &&
      ((required & (kPttModLCtrl | kPttModRCtrl)) == 0);
  const bool requires_generic_shift =
      ((required & kPttModShift) != 0) &&
      ((required & (kPttModLShift | kPttModRShift)) == 0);
  const bool requires_generic_alt =
      ((required & kPttModAlt) != 0) &&
      ((required & (kPttModLAlt | kPttModRAlt)) == 0);
  if (requires_generic_ctrl &&
      (current & (kPttModLCtrl | kPttModRCtrl)) == 0) {
    return false;
  }
  if (requires_generic_shift &&
      (current & (kPttModLShift | kPttModRShift)) == 0) {
    return false;
  }
  if (requires_generic_alt && (current & (kPttModLAlt | kPttModRAlt)) == 0) {
    return false;
  }
  return true;
}

void AppendModPrefix(std::string& out, const char* label) {
  out += label;
  out += '+';
}

}  // namespace

std::string PttComboToDisplayName(int vkey, int mods) {
  if (vkey <= 0) {
    return "Not set";
  }
  std::string out;
  if ((mods & kPttModLCtrl) != 0) {
    AppendModPrefix(out, "Left Ctrl");
  }
  if ((mods & kPttModRCtrl) != 0) {
    AppendModPrefix(out, "Right Ctrl");
  }
  if ((mods & (kPttModLCtrl | kPttModRCtrl)) == 0 && (mods & kPttModCtrl) != 0) {
    AppendModPrefix(out, "Ctrl");
  }
  if ((mods & kPttModLAlt) != 0) {
    AppendModPrefix(out, "Left Alt");
  }
  if ((mods & kPttModRAlt) != 0) {
    AppendModPrefix(out, "Right Alt");
  }
  if ((mods & (kPttModLAlt | kPttModRAlt)) == 0 && (mods & kPttModAlt) != 0) {
    AppendModPrefix(out, "Alt");
  }
  if ((mods & kPttModLShift) != 0) {
    AppendModPrefix(out, "Left Shift");
  }
  if ((mods & kPttModRShift) != 0) {
    AppendModPrefix(out, "Right Shift");
  }
  if ((mods & (kPttModLShift | kPttModRShift)) == 0 &&
      (mods & kPttModShift) != 0) {
    AppendModPrefix(out, "Shift");
  }
  out += VKeyToDisplayName(vkey);
  return out;
}

GlobalPttHook& GlobalPttHook::Instance() {
  static GlobalPttHook instance;
  return instance;
}

GlobalPttHook::~GlobalPttHook() {
  Uninstall();
}

void GlobalPttHook::SetWindow(HWND hwnd) {
  hwnd_ = hwnd;
}

void GlobalPttHook::SetBinding(int vkey, int mods) {
  vkey_ = vkey;
  mods_ = mods;
  ptt_pressed_ = false;
}

void GlobalPttHook::ClearBinding() {
  vkey_ = 0;
  mods_ = 0;
  ptt_pressed_ = false;
}

bool GlobalPttHook::Install() {
  if (hook_ != nullptr) {
    return true;
  }
  hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
  return hook_ != nullptr;
}

void GlobalPttHook::Uninstall() {
  CancelCapture();
  if (hook_ != nullptr) {
    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
  }
  ptt_pressed_ = false;
}

bool GlobalPttHook::StartCapture() {
  if (!Install()) {
    return false;
  }
  capturing_ = true;
  held_mods_ = 0;
  pending_modifier_vkey_ = 0;
  pending_modifier_mods_ = 0;
  capture_result_ = {};
  return true;
}

void GlobalPttHook::CancelCapture() {
  capturing_ = false;
  held_mods_ = 0;
  pending_modifier_vkey_ = 0;
  pending_modifier_mods_ = 0;
}

GlobalPttCaptureResult GlobalPttHook::TakeCaptureResult() {
  GlobalPttCaptureResult result = capture_result_;
  capture_result_ = {};
  return result;
}

void GlobalPttHook::EmitPttDown() {
  if (hwnd_ != nullptr) {
    PostMessage(hwnd_, WM_OWALKIE_PTT_DOWN, 0, 0);
    return;
  }
  GlobalPttPlugin::Instance().OnPttDown();
}

void GlobalPttHook::EmitPttUp() {
  if (hwnd_ != nullptr) {
    PostMessage(hwnd_, WM_OWALKIE_PTT_UP, 0, 0);
    return;
  }
  GlobalPttPlugin::Instance().OnPttUp();
}

void GlobalPttHook::CommitCapture(int raw_vkey, int current_mods) {
  const int vkey = raw_vkey;
  const int mods =
      StripPrimaryModifierFamily(NormalizeModifierMask(current_mods), vkey);
  capture_result_.vkey = vkey;
  capture_result_.mods = mods;
  capture_result_.display_name = PttComboToDisplayName(vkey, mods);
  capturing_ = false;
  held_mods_ = 0;
  pending_modifier_vkey_ = 0;
  pending_modifier_mods_ = 0;
  if (hwnd_ != nullptr) {
    PostMessage(hwnd_, WM_OWALKIE_PTT_CAPTURED, 0, 0);
    return;
  }
  GlobalPttPlugin::Instance().OnCaptured();
}

void GlobalPttHook::OnLowLevelKey(int raw_vkey, bool is_key_down, bool is_key_up) {
  if (raw_vkey <= 0) {
    return;
  }

  if (capturing_) {
    if (is_key_down && raw_vkey == VK_ESCAPE) {
      CancelCapture();
      return;
    }
    const bool is_modifier = IsModifierVKey(raw_vkey);
    if (is_modifier) {
      const int bits = ModifierBitsForVKey(raw_vkey);
      if (is_key_down) {
        held_mods_ = NormalizeModifierMask(held_mods_ | bits);
        pending_modifier_vkey_ = raw_vkey;
        pending_modifier_mods_ =
            StripPrimaryModifierFamily(held_mods_, raw_vkey);
      } else if (is_key_up) {
        if (pending_modifier_vkey_ == raw_vkey) {
          CommitCapture(raw_vkey, pending_modifier_mods_);
          return;
        }
        held_mods_ = NormalizeModifierMask(held_mods_ & ~bits);
      }
      return;
    }
    if (is_key_down) {
      CommitCapture(raw_vkey, held_mods_);
    }
    return;
  }

  if (!HasBinding()) {
    return;
  }

  if (!IsPrimaryKeyMatch(vkey_, raw_vkey)) {
    return;
  }

  const int current_mods = CurrentModifierMask();
  const int required_mods = mods_;
  if (!is_key_up && !AreRequiredModsPressed(required_mods, current_mods)) {
    return;
  }

  if (is_key_down && !ptt_pressed_) {
    ptt_pressed_ = true;
    EmitPttDown();
  } else if (is_key_up && ptt_pressed_) {
    ptt_pressed_ = false;
    EmitPttUp();
  }
}

LRESULT CALLBACK GlobalPttHook::LowLevelKeyboardProc(int nCode,
                                                     WPARAM wparam,
                                                     LPARAM lparam) {
  if (nCode == HC_ACTION) {
    auto& hook = Instance();
    const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
    if (data != nullptr) {
      const bool is_down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);
      const bool is_up = (wparam == WM_KEYUP || wparam == WM_SYSKEYUP);
      if (is_down || is_up) {
        hook.OnLowLevelKey(static_cast<int>(data->vkCode), is_down, is_up);
        if (hook.IsCapturing()) {
          return 1;
        }
      }
    }
  }
  return CallNextHookEx(nullptr, nCode, wparam, lparam);
}
