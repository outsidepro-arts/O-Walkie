#include "MainFrame.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <thread>
#include <string>

#include <nlohmann/json.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/intl.h>
#include <wx/slider.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stdpaths.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifndef OWALKIE_VERSION
#define OWALKIE_VERSION "dev"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

static bool PatternNameAsciiIEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

static std::string OwTranslatePatternDisplayName(const std::string& name) {
    return wxGetTranslation(wxString::FromUTF8(name)).utf8_string();
}

namespace {

// Release-burst anti-spam (aligned with Android WalkieService): quiet window before reset; refreshed on release and on blocked press.
constexpr int kPttReleaseBurstTimerMs = 1000;
constexpr int kPttReleaseBurstBlockThreshold = 3;

#ifdef _WIN32
MainFrame* g_mainFrameForPttHook = nullptr;
constexpr int kPttModShift = 1;
constexpr int kPttModCtrl = 2;
constexpr int kPttModAlt = 4;
constexpr int kPttModLShift = 1 << 3;
constexpr int kPttModRShift = 1 << 4;
constexpr int kPttModLCtrl = 1 << 5;
constexpr int kPttModRCtrl = 1 << 6;
constexpr int kPttModLAlt = 1 << 7;
constexpr int kPttModRAlt = 1 << 8;

int NormalizeHotkeyVKey(int vkey) {
    return vkey;
}

std::string VKeyToDisplayName(int vkey) {
    if (vkey <= 0) {
        return wxGetTranslation("Not set").utf8_string();
    }
    const UINT scan = MapVirtualKeyA(static_cast<UINT>(vkey), MAPVK_VK_TO_VSC);
    LONG lParam = static_cast<LONG>(scan << 16);
    char name[128]{};
    const int n = GetKeyNameTextA(lParam, name, static_cast<int>(sizeof(name)));
    if (n > 0) {
        return std::string(name, name + n);
    }
    return "VK " + std::to_string(vkey);
}

int CurrentModifierMask() {
    int mods = 0;
    const bool lShift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    const bool rShift = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    const bool lCtrl = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    const bool rCtrl = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
    const bool lAlt = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
    const bool rAlt = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
    if (lShift || rShift) mods |= kPttModShift;
    if (lCtrl || rCtrl) mods |= kPttModCtrl;
    if (lAlt || rAlt) mods |= kPttModAlt;
    if (lShift) mods |= kPttModLShift;
    if (rShift) mods |= kPttModRShift;
    if (lCtrl) mods |= kPttModLCtrl;
    if (rCtrl) mods |= kPttModRCtrl;
    if (lAlt) mods |= kPttModLAlt;
    if (rAlt) mods |= kPttModRAlt;
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
    // Keep side bits authoritative and derive generic bits from them.
    mods &= ~(kPttModShift | kPttModCtrl | kPttModAlt);
    if ((mods & (kPttModLShift | kPttModRShift)) != 0) mods |= kPttModShift;
    if ((mods & (kPttModLCtrl | kPttModRCtrl)) != 0) mods |= kPttModCtrl;
    if ((mods & (kPttModLAlt | kPttModRAlt)) != 0) mods |= kPttModAlt;
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

std::string PttComboToDisplayName(int vkey, int mods) {
    if (vkey <= 0) {
        return wxGetTranslation("Not set").utf8_string();
    }
    std::string out;
    if ((mods & kPttModLCtrl) != 0) {
        out += wxGetTranslation("Left Ctrl+").utf8_string();
    }
    if ((mods & kPttModRCtrl) != 0) {
        out += wxGetTranslation("Right Ctrl+").utf8_string();
    }
    if ((mods & (kPttModLCtrl | kPttModRCtrl)) == 0 && (mods & kPttModCtrl) != 0) {
        out += wxGetTranslation("Ctrl+").utf8_string();
    }
    if ((mods & kPttModLAlt) != 0) {
        out += wxGetTranslation("Left Alt+").utf8_string();
    }
    if ((mods & kPttModRAlt) != 0) {
        out += wxGetTranslation("Right Alt+").utf8_string();
    }
    if ((mods & (kPttModLAlt | kPttModRAlt)) == 0 && (mods & kPttModAlt) != 0) {
        out += wxGetTranslation("Alt+").utf8_string();
    }
    if ((mods & kPttModLShift) != 0) {
        out += wxGetTranslation("Left Shift+").utf8_string();
    }
    if ((mods & kPttModRShift) != 0) {
        out += wxGetTranslation("Right Shift+").utf8_string();
    }
    if ((mods & (kPttModLShift | kPttModRShift)) == 0 && (mods & kPttModShift) != 0) {
        out += wxGetTranslation("Shift+").utf8_string();
    }
    out += VKeyToDisplayName(vkey);
    return out;
}

bool IsPrimaryKeyMatch(int savedVKey, int pressedVKey) {
    if (savedVKey == pressedVKey) {
        return true;
    }
    if (savedVKey == VK_SHIFT) {
        return pressedVKey == VK_LSHIFT || pressedVKey == VK_RSHIFT;
    }
    if (savedVKey == VK_CONTROL) {
        return pressedVKey == VK_LCONTROL || pressedVKey == VK_RCONTROL;
    }
    if (savedVKey == VK_MENU) {
        return pressedVKey == VK_LMENU || pressedVKey == VK_RMENU;
    }
    return false;
}

bool AreRequiredModsPressed(int required, int current) {
    const auto hasAll = [&](int mask) { return (current & mask) == mask; };
    if ((required & kPttModLCtrl) != 0 && !hasAll(kPttModLCtrl)) return false;
    if ((required & kPttModRCtrl) != 0 && !hasAll(kPttModRCtrl)) return false;
    if ((required & kPttModLShift) != 0 && !hasAll(kPttModLShift)) return false;
    if ((required & kPttModRShift) != 0 && !hasAll(kPttModRShift)) return false;
    if ((required & kPttModLAlt) != 0 && !hasAll(kPttModLAlt)) return false;
    if ((required & kPttModRAlt) != 0 && !hasAll(kPttModRAlt)) return false;

    const bool requiresGenericCtrl = ((required & kPttModCtrl) != 0) && ((required & (kPttModLCtrl | kPttModRCtrl)) == 0);
    const bool requiresGenericShift = ((required & kPttModShift) != 0) && ((required & (kPttModLShift | kPttModRShift)) == 0);
    const bool requiresGenericAlt = ((required & kPttModAlt) != 0) && ((required & (kPttModLAlt | kPttModRAlt)) == 0);
    if (requiresGenericCtrl && (current & (kPttModLCtrl | kPttModRCtrl)) == 0) return false;
    if (requiresGenericShift && (current & (kPttModLShift | kPttModRShift)) == 0) return false;
    if (requiresGenericAlt && (current & (kPttModLAlt | kPttModRAlt)) == 0) return false;
    return true;
}

class HotkeyCaptureDialog final : public wxDialog {
public:
    HotkeyCaptureDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, _("Press key combination"), wxDefaultPosition, wxSize(380, 140),
                   wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* hint = new wxStaticText(this, wxID_ANY, _("Press any key or key combination (with Ctrl/Alt/Shift)."));
        keyText_ = new wxStaticText(this, wxID_ANY, _("Waiting..."));
        root->Add(hint, 0, wxALL, 12);
        root->Add(keyText_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        SetSizerAndFit(root);
        activeDialog_ = this;
        hook_ = SetWindowsHookExA(WH_KEYBOARD_LL, &HotkeyCaptureDialog::CaptureKeyboardProc, GetModuleHandle(nullptr), 0);
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            UninstallHook();
            event.Skip();
        });
        CentreOnParent();
    }

    ~HotkeyCaptureDialog() override {
        UninstallHook();
    }

    int CapturedVKey() const { return vkey_; }
    int CapturedMods() const { return mods_; }

private:
    void UninstallHook() {
        if (hook_) {
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }
        if (activeDialog_ == this) {
            activeDialog_ = nullptr;
        }
    }

    void CommitCapture(int rawVKey, int currentMods) {
        vkey_ = NormalizeHotkeyVKey(rawVKey);
        mods_ = StripPrimaryModifierFamily(NormalizeModifierMask(currentMods), vkey_);
        keyText_->SetLabel(wxString::FromUTF8(PttComboToDisplayName(vkey_, mods_)));
        UninstallHook();
        EndModal(wxID_OK);
    }

    void OnLowLevelKey(int rawVKey, bool isKeyDown, bool isKeyUp) {
        if (rawVKey <= 0) {
            return;
        }
        const bool isModifier = IsModifierVKey(rawVKey);
        if (isModifier) {
            const int bits = ModifierBitsForVKey(rawVKey);
            if (isKeyDown) {
                heldMods_ = NormalizeModifierMask(heldMods_ | bits);
                pendingModifierVKey_ = rawVKey;
                pendingModifierMods_ = StripPrimaryModifierFamily(heldMods_, rawVKey);
                keyText_->SetLabel(wxString::FromUTF8(PttComboToDisplayName(rawVKey, pendingModifierMods_)));
            } else if (isKeyUp) {
                if (pendingModifierVKey_ == rawVKey) {
                    CommitCapture(rawVKey, pendingModifierMods_);
                    return;
                }
                heldMods_ = NormalizeModifierMask(heldMods_ & ~bits);
            }
            return;
        }
        if (isKeyDown) {
            CommitCapture(rawVKey, heldMods_);
        }
    }

    static LRESULT CALLBACK CaptureKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION && activeDialog_) {
            const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (data) {
                const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
                const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
                if (isDown || isUp) {
                    activeDialog_->OnLowLevelKey(static_cast<int>(data->vkCode), isDown, isUp);
                    return 1;
                }
            }
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

private:
    wxStaticText* keyText_ = nullptr;
    int vkey_ = 0;
    int mods_ = 0;
    int heldMods_ = 0;
    int pendingModifierVKey_ = 0;
    int pendingModifierMods_ = 0;
    HHOOK hook_ = nullptr;
    static inline HotkeyCaptureDialog* activeDialog_ = nullptr;
};
#endif

void SkipKeyboardFocus(wxWindow* w) {
    if (w) {
        w->DisableFocusFromKeyboard();
    }
}

wxStdDialogButtonSizer* NewTranslatedOkCancelSizer(wxDialog* dlg) {
    auto* bs = new wxStdDialogButtonSizer;
    bs->AddButton(new wxButton(dlg, wxID_OK, _("OK")));
    bs->AddButton(new wxButton(dlg, wxID_CANCEL, _("Cancel")));
    bs->Realize();
    return bs;
}

constexpr int kMaxRogerCustomSignalMs = 1000;
constexpr int kMaxCallCustomSignalMs = 5000;

class SignalPatternEditorDialog final : public wxDialog {
public:
    SignalPatternEditorDialog(wxWindow* parent, AudioEngine* audio, bool callKind)
        : wxDialog(parent, wxID_ANY,
                   callKind ? _("Custom calling signal") : _("Custom Roger signal"),
                   wxDefaultPosition, wxSize(440, 400),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          audio_(audio),
          callKind_(callKind) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        root->Add(new wxStaticText(this, wxID_ANY, _("Name")), 0, wxBOTTOM, 4);
        nameCtrl_ = new wxTextCtrl(this, wxID_ANY);
        root->Add(nameCtrl_, 0, wxEXPAND | wxBOTTOM, 8);
        root->Add(new wxStaticText(this, wxID_ANY, _("Segments (Hz and ms; frequency 0 = pause)")), 0, wxBOTTOM, 4);
        listBox_ = new wxListBox(this, wxID_ANY);
        root->Add(listBox_, 1, wxEXPAND | wxBOTTOM, 8);
        repeatLabel_ = new wxStaticText(this, wxID_ANY, _("Repeat count"));
        repeatSpin_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 500, 1);
        auto* repeatRow = new wxBoxSizer(wxHORIZONTAL);
        repeatRow->Add(repeatLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        repeatRow->Add(repeatSpin_, 0);
        root->Add(repeatRow, 0, wxBOTTOM, 8);
        if (!callKind_) {
            repeatLabel_->Hide();
            repeatSpin_->Hide();
        }
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        addBtn_ = new wxButton(this, wxID_ANY, _("Add segment"));
        removeBtn_ = new wxButton(this, wxID_ANY, _("Remove"));
        playBtn_ = new wxButton(this, wxID_ANY, _("Play"));
        row->Add(addBtn_, 0, wxRIGHT, 8);
        row->Add(removeBtn_, 0, wxRIGHT, 8);
        row->Add(playBtn_, 0);
        root->Add(row, 0, wxBOTTOM, 8);
        root->Add(NewTranslatedOkCancelSizer(this), 0, wxEXPAND);
        SetSizerAndFit(root);

        addBtn_->Bind(wxEVT_BUTTON, &SignalPatternEditorDialog::OnAdd, this);
        removeBtn_->Bind(wxEVT_BUTTON, &SignalPatternEditorDialog::OnRemove, this);
        playBtn_->Bind(wxEVT_BUTTON, &SignalPatternEditorDialog::OnPlay, this);
        Bind(wxEVT_BUTTON, &SignalPatternEditorDialog::OnTrySave, this, wxID_OK);
    }

    SignalPattern TakeSavedPattern() { return std::move(saved_); }

private:
    int CycleDurationMs() const {
        int s = 0;
        for (const auto& p : points_) {
            s += p.durationMs;
        }
        return s;
    }

    int RepeatCountOr1() const { return callKind_ ? std::max(1, repeatSpin_->GetValue()) : 1; }

    int EffectiveTotalMs() const { return CycleDurationMs() * RepeatCountOr1(); }

    void RefreshList() {
        listBox_->Clear();
        for (size_t i = 0; i < points_.size(); ++i) {
            const auto& pt = points_[i];
            wxString line = wxString::Format("%d. ", static_cast<int>(i + 1));
            if (pt.freqHz <= 0.0) {
                line += _("Pause");
            } else {
                line += wxString::Format(_("%.2f Hz"), pt.freqHz);
            }
            line += wxString::Format(_(" / %d ms"), pt.durationMs);
            listBox_->Append(line);
        }
    }

    bool AppendPointFromInputs(double freqHz, int durationMs, wxString& errOut) {
        if (durationMs <= 0) {
            errOut = _("Duration must be positive.");
            return false;
        }
        if (freqHz < 0.0) {
            errOut = _("Frequency cannot be negative.");
            return false;
        }
        const int repeat = RepeatCountOr1();
        const int cycleAfter = CycleDurationMs() + durationMs;
        const int limit = callKind_ ? kMaxCallCustomSignalMs : kMaxRogerCustomSignalMs;
        if (cycleAfter * repeat > limit) {
            errOut = _("Total duration would exceed limit.");
            return false;
        }
        points_.push_back({freqHz, durationMs});
        RefreshList();
        return true;
    }

    void OnAdd(wxCommandEvent&) {
        wxDialog dlg(this, wxID_ANY, _("New segment"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
        auto* sz = new wxBoxSizer(wxVERTICAL);
        auto* fRow = new wxBoxSizer(wxHORIZONTAL);
        fRow->Add(new wxStaticText(&dlg, wxID_ANY, _("Frequency (Hz)")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        auto* fCtrl = new wxTextCtrl(&dlg, wxID_ANY, "1000");
        fRow->Add(fCtrl, 1, wxEXPAND);
        sz->Add(fRow, 0, wxEXPAND | wxALL, 8);
        auto* dRow = new wxBoxSizer(wxHORIZONTAL);
        dRow->Add(new wxStaticText(&dlg, wxID_ANY, _("Duration (ms)")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        auto* dCtrl = new wxTextCtrl(&dlg, wxID_ANY, "50");
        dRow->Add(dCtrl, 1, wxEXPAND);
        sz->Add(dRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        sz->Add(NewTranslatedOkCancelSizer(&dlg), 0, wxEXPAND | wxALL, 8);
        dlg.SetSizerAndFit(sz);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }
        double freq = 0;
        if (!fCtrl->GetValue().ToDouble(&freq)) {
            wxMessageBox(_("Invalid frequency."), _("Invalid"), wxOK | wxICON_WARNING, this);
            return;
        }
        long durL = 0;
        if (!dCtrl->GetValue().ToLong(&durL)) {
            wxMessageBox(_("Duration must be an integer."), _("Invalid"), wxOK | wxICON_WARNING, this);
            return;
        }
        wxString err;
        if (!AppendPointFromInputs(freq, static_cast<int>(durL), err)) {
            wxMessageBox(err, _("Invalid"), wxOK | wxICON_WARNING, this);
        }
    }

    void OnRemove(wxCommandEvent&) {
        const int sel = listBox_->GetSelection();
        if (sel == wxNOT_FOUND || sel < 0 || static_cast<size_t>(sel) >= points_.size()) {
            return;
        }
        points_.erase(points_.begin() + sel);
        RefreshList();
    }

    void OnPlay(wxCommandEvent&) {
        if (points_.empty()) {
            wxMessageBox(_("Add at least one segment."), _("Preview"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        if (callKind_ && repeatSpin_->GetValue() < 1) {
            wxMessageBox(_("Invalid repeat count."), _("Preview"), wxOK | wxICON_WARNING, this);
            return;
        }
        SignalPattern pat;
        pat.id.clear();
        pat.name.clear();
        pat.points = BuildRepeatedPoints();
        pat.appendTail = !callKind_;
        if (!pat.points.empty() && audio_) {
            audio_->PlaySignalPatternPreview(pat);
        }
    }

    std::vector<SignalPatternPoint> BuildRepeatedPoints() const {
        const int rep = RepeatCountOr1();
        if (rep <= 1) {
            return points_;
        }
        std::vector<SignalPatternPoint> out;
        out.reserve(points_.size() * static_cast<size_t>(rep));
        for (int r = 0; r < rep; ++r) {
            out.insert(out.end(), points_.begin(), points_.end());
        }
        return out;
    }

    void OnTrySave(wxCommandEvent&) {
        const wxString nameWx = nameCtrl_->GetValue().Trim();
        if (nameWx.empty()) {
            wxMessageBox(_("Name is required."), _("Save"), wxOK | wxICON_WARNING, this);
            return;
        }
        if (points_.empty()) {
            wxMessageBox(_("Add at least one segment."), _("Save"), wxOK | wxICON_WARNING, this);
            return;
        }
        if (callKind_ && repeatSpin_->GetValue() < 1) {
            wxMessageBox(_("Repeat count must be at least 1."), _("Save"), wxOK | wxICON_WARNING, this);
            return;
        }
        const int total = EffectiveTotalMs();
        if (total > (callKind_ ? kMaxCallCustomSignalMs : kMaxRogerCustomSignalMs)) {
            wxMessageBox(_("Total duration exceeds limit."), _("Save"), wxOK | wxICON_WARNING, this);
            return;
        }
        saved_ = {};
        saved_.name = nameWx.utf8_string();
        saved_.points = BuildRepeatedPoints();
        saved_.appendTail = !callKind_;
        EndModal(wxID_OK);
    }

    wxTextCtrl* nameCtrl_ = nullptr;
    wxListBox* listBox_ = nullptr;
    wxSpinCtrl* repeatSpin_ = nullptr;
    wxStaticText* repeatLabel_ = nullptr;
    wxButton* addBtn_ = nullptr;
    wxButton* removeBtn_ = nullptr;
    wxButton* playBtn_ = nullptr;
    AudioEngine* audio_ = nullptr;
    bool callKind_ = false;
    std::vector<SignalPatternPoint> points_;
    SignalPattern saved_;
};

class SettingsDialog final : public wxDialog {
public:
    SettingsDialog(
        wxWindow* parent,
        MainFrame* host,
        const std::vector<NamedAudioDevice>& inputDevices,
        const std::vector<NamedAudioDevice>& outputDevices,
        int selectedInputId,
        int selectedOutputId,
        std::vector<SignalPattern> rogerPatterns,
        std::vector<SignalPattern> callPatterns,
        const std::string& selectedRogerId,
        const std::string& selectedCallId,
        int globalPttVKey,
        int globalPttMods,
        bool showMicLevelIndicator,
        bool pttToggleMode,
        const std::string& uiLanguage)
        : wxDialog(parent, wxID_ANY, _("Settings"), wxDefaultPosition, wxSize(640, 420), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          host_(host),
          inputDevices_(inputDevices),
          outputDevices_(outputDevices),
          rogerPatterns_(std::move(rogerPatterns)),
          callPatterns_(std::move(callPatterns)) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* grid = new wxFlexGridSizer(2, 8, 10);
        grid->AddGrowableCol(1, 1);

        grid->Add(new wxStaticText(this, wxID_ANY, _("Display language")), 0, wxALIGN_CENTER_VERTICAL);
        langChoice_ = new wxChoice(this, wxID_ANY);
        langChoice_->Append(_("English"));
        langChoice_->Append(_("Russian"));
        langChoice_->SetSelection(uiLanguage == "ru" ? 1 : 0);
        grid->Add(langChoice_, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, _("Microphone")), 0, wxALIGN_CENTER_VERTICAL);
        inputChoice_ = new wxChoice(this, wxID_ANY);
        inputChoice_->Append(_("System default"));
        inputIds_.push_back(-1);
        for (const auto& d : inputDevices_) {
            inputChoice_->Append(wxString::FromUTF8(d.name));
            inputIds_.push_back(d.index);
        }
        grid->Add(inputChoice_, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, _("Speaker")), 0, wxALIGN_CENTER_VERTICAL);
        outputChoice_ = new wxChoice(this, wxID_ANY);
        outputChoice_->Append(_("System default"));
        outputIds_.push_back(-1);
        for (const auto& d : outputDevices_) {
            outputChoice_->Append(wxString::FromUTF8(d.name));
            outputIds_.push_back(d.index);
        }
        grid->Add(outputChoice_, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, _("Roger pattern")), 0, wxALIGN_CENTER_VERTICAL);
        auto* rogerRow = new wxBoxSizer(wxHORIZONTAL);
        rogerChoice_ = new wxChoice(this, wxID_ANY);
        for (const auto& p : rogerPatterns_) {
            rogerChoice_->Append(wxString::FromUTF8(p.name));
            rogerIds_.push_back(p.id);
        }
        rogerRow->Add(rogerChoice_, 1, wxEXPAND | wxRIGHT, 8);
        rogerPlayBtn_ = new wxButton(this, wxID_ANY, _("Play"));
        rogerCustomBtn_ = new wxButton(this, wxID_ANY, _("Custom"));
        rogerDeleteBtn_ = new wxButton(this, wxID_ANY, _("Delete"));
        rogerRow->Add(rogerPlayBtn_, 0, wxRIGHT, 8);
        rogerRow->Add(rogerCustomBtn_, 0, wxRIGHT, 8);
        rogerRow->Add(rogerDeleteBtn_, 0);
        grid->Add(rogerRow, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, _("Call pattern")), 0, wxALIGN_CENTER_VERTICAL);
        auto* callRow = new wxBoxSizer(wxHORIZONTAL);
        callChoice_ = new wxChoice(this, wxID_ANY);
        for (const auto& p : callPatterns_) {
            callChoice_->Append(wxString::FromUTF8(p.name));
            callIds_.push_back(p.id);
        }
        callRow->Add(callChoice_, 1, wxEXPAND | wxRIGHT, 8);
        callPlayBtn_ = new wxButton(this, wxID_ANY, _("Play"));
        callCustomBtn_ = new wxButton(this, wxID_ANY, _("Custom"));
        callDeleteBtn_ = new wxButton(this, wxID_ANY, _("Delete"));
        callRow->Add(callPlayBtn_, 0, wxRIGHT, 8);
        callRow->Add(callCustomBtn_, 0, wxRIGHT, 8);
        callRow->Add(callDeleteBtn_, 0);
        grid->Add(callRow, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, _("Global PTT key")), 0, wxALIGN_CENTER_VERTICAL);
        auto* pttRow = new wxBoxSizer(wxHORIZONTAL);
        pttKeyLabel_ = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(PttComboToDisplayName(globalPttVKey, globalPttMods)));
        pttCaptureBtn_ = new wxButton(this, wxID_ANY, _("Assign key"));
        pttClearBtn_ = new wxButton(this, wxID_ANY, _("Clear"));
        pttRow->Add(pttKeyLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        pttRow->Add(pttCaptureBtn_, 0, wxRIGHT, 8);
        pttRow->Add(pttClearBtn_, 0);
        grid->Add(pttRow, 1, wxEXPAND);
        grid->Add(new wxStaticText(this, wxID_ANY, _("PTT toggle mode")), 0, wxALIGN_CENTER_VERTICAL);
        pttToggleCheck_ = new wxCheckBox(this, wxID_ANY, _("Tap or hotkey press toggles transmit on/off"));
        pttToggleCheck_->SetValue(pttToggleMode);
        grid->Add(pttToggleCheck_, 1, wxEXPAND);
        grid->Add(new wxStaticText(this, wxID_ANY, _("Microphone level indicator")), 0, wxALIGN_CENTER_VERTICAL);
        micLevelCheck_ = new wxCheckBox(this, wxID_ANY, _("Show VU meter"));
        micLevelCheck_->SetValue(showMicLevelIndicator);
        grid->Add(micLevelCheck_, 1, wxEXPAND);

        root->Add(grid, 1, wxEXPAND | wxALL, 12);
        root->Add(NewTranslatedOkCancelSizer(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        SetSizerAndFit(root);

        SelectById(inputChoice_, inputIds_, selectedInputId);
        SelectById(outputChoice_, outputIds_, selectedOutputId);
        SelectByString(rogerChoice_, rogerIds_, selectedRogerId);
        SelectByString(callChoice_, callIds_, selectedCallId);
        selectedPttVKey_ = globalPttVKey;
        selectedPttMods_ = globalPttMods;

        pttCaptureBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            HotkeyCaptureDialog dlg(this);
            if (dlg.ShowModal() == wxID_OK) {
                selectedPttVKey_ = dlg.CapturedVKey();
                selectedPttMods_ = dlg.CapturedMods();
                pttKeyLabel_->SetLabel(wxString::FromUTF8(PttComboToDisplayName(selectedPttVKey_, selectedPttMods_)));
            }
        });
        pttClearBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            selectedPttVKey_ = 0;
            selectedPttMods_ = 0;
            pttKeyLabel_->SetLabel(_("Not set"));
        });

        rogerChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdatePatternDeleteButtons(); });
        callChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdatePatternDeleteButtons(); });

        rogerPlayBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PlayRogerPreview(); });
        callPlayBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PlayCallPreview(); });

        rogerCustomBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!host_ || !host_->AudioEnginePtr()) {
                return;
            }
            SignalPatternEditorDialog ed(this, host_->AudioEnginePtr(), false);
            if (ed.ShowModal() == wxID_OK) {
                host_->UpsertCustomRogerPattern(ed.TakeSavedPattern());
                ReloadRogerCallChoicesFromHost();
            }
        });
        callCustomBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!host_ || !host_->AudioEnginePtr()) {
                return;
            }
            SignalPatternEditorDialog ed(this, host_->AudioEnginePtr(), true);
            if (ed.ShowModal() == wxID_OK) {
                host_->UpsertCustomCallPattern(ed.TakeSavedPattern());
                ReloadRogerCallChoicesFromHost();
            }
        });
        rogerDeleteBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!host_) {
                return;
            }
            const std::string id = PickString(rogerChoice_, rogerIds_);
            if (id.empty() || MainFrame::IsBuiltInRogerPatternId(id)) {
                return;
            }
            if (wxMessageBox(_("Delete selected custom signal?"), _("Confirm"), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
                return;
            }
            host_->DeleteCustomRogerPattern(id);
            ReloadRogerCallChoicesFromHost();
        });
        callDeleteBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!host_) {
                return;
            }
            const std::string id = PickString(callChoice_, callIds_);
            if (id.empty() || MainFrame::IsBuiltInCallPatternId(id)) {
                return;
            }
            if (wxMessageBox(_("Delete selected custom signal?"), _("Confirm"), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
                return;
            }
            host_->DeleteCustomCallPattern(id);
            ReloadRogerCallChoicesFromHost();
        });

        UpdatePatternDeleteButtons();
    }

    std::string SelectedUiLanguage() const {
        if (!langChoice_) {
            return "en";
        }
        return langChoice_->GetSelection() == 1 ? "ru" : "en";
    }

    int SelectedInputId() const { return PickId(inputChoice_, inputIds_); }
    int SelectedOutputId() const { return PickId(outputChoice_, outputIds_); }
    std::string SelectedRogerId() const { return PickString(rogerChoice_, rogerIds_); }
    std::string SelectedCallId() const { return PickString(callChoice_, callIds_); }
    int SelectedGlobalPttVKey() const { return selectedPttVKey_; }
    int SelectedGlobalPttMods() const { return selectedPttMods_; }
    bool ShowMicLevelIndicator() const { return micLevelCheck_ && micLevelCheck_->GetValue(); }
    bool PttToggleMode() const { return pttToggleCheck_ && pttToggleCheck_->GetValue(); }

private:
    void ReloadRogerCallChoicesFromHost() {
        if (!host_) {
            return;
        }
        rogerPatterns_ = host_->MergedRogerPatternsForUi();
        callPatterns_ = host_->MergedCallPatternsForUi();
        rogerChoice_->Clear();
        rogerIds_.clear();
        for (const auto& p : rogerPatterns_) {
            rogerChoice_->Append(wxString::FromUTF8(p.name));
            rogerIds_.push_back(p.id);
        }
        callChoice_->Clear();
        callIds_.clear();
        for (const auto& p : callPatterns_) {
            callChoice_->Append(wxString::FromUTF8(p.name));
            callIds_.push_back(p.id);
        }
        SelectByString(rogerChoice_, rogerIds_, host_->SelectedRogerPatternId());
        SelectByString(callChoice_, callIds_, host_->SelectedCallPatternId());
        UpdatePatternDeleteButtons();
    }

    void UpdatePatternDeleteButtons() {
        const std::string rid = PickString(rogerChoice_, rogerIds_);
        const std::string cid = PickString(callChoice_, callIds_);
        rogerDeleteBtn_->Enable(!rid.empty() && !MainFrame::IsBuiltInRogerPatternId(rid));
        callDeleteBtn_->Enable(!cid.empty() && !MainFrame::IsBuiltInCallPatternId(cid));
    }

    static const SignalPattern* FindPatternById(const std::vector<SignalPattern>& list, const std::string& id) {
        for (const auto& p : list) {
            if (p.id == id) {
                return &p;
            }
        }
        return nullptr;
    }

    void PlayRogerPreview() {
        if (!host_ || !host_->AudioEnginePtr()) {
            return;
        }
        const std::string id = PickString(rogerChoice_, rogerIds_);
        const SignalPattern* pat = FindPatternById(rogerPatterns_, id);
        if (!pat || pat->points.empty()) {
            wxMessageBox(_("Nothing to play for this pattern."), _("Preview"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        host_->AudioEnginePtr()->PlaySignalPatternPreview(*pat);
    }

    void PlayCallPreview() {
        if (!host_ || !host_->AudioEnginePtr()) {
            return;
        }
        const std::string id = PickString(callChoice_, callIds_);
        const SignalPattern* pat = FindPatternById(callPatterns_, id);
        if (!pat || pat->points.empty()) {
            wxMessageBox(_("Nothing to play for this pattern."), _("Preview"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        host_->AudioEnginePtr()->PlaySignalPatternPreview(*pat);
    }

    static void SelectById(wxChoice* c, const std::vector<int>& ids, int id) {
        int sel = 0;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) {
                sel = static_cast<int>(i);
                break;
            }
        }
        c->SetSelection(sel);
    }
    static void SelectByString(wxChoice* c, const std::vector<std::string>& ids, const std::string& id) {
        int sel = 0;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) {
                sel = static_cast<int>(i);
                break;
            }
        }
        c->SetSelection(sel);
    }
    static int PickId(wxChoice* c, const std::vector<int>& ids) {
        const int i = c->GetSelection();
        if (i == wxNOT_FOUND || i < 0 || static_cast<size_t>(i) >= ids.size()) {
            return -1;
        }
        return ids[static_cast<size_t>(i)];
    }
    static std::string PickString(wxChoice* c, const std::vector<std::string>& ids) {
        const int i = c->GetSelection();
        if (i == wxNOT_FOUND || i < 0 || static_cast<size_t>(i) >= ids.size()) {
            return ids.empty() ? std::string{} : ids.front();
        }
        return ids[static_cast<size_t>(i)];
    }

    MainFrame* host_ = nullptr;
    const std::vector<NamedAudioDevice>& inputDevices_;
    const std::vector<NamedAudioDevice>& outputDevices_;
    std::vector<SignalPattern> rogerPatterns_;
    std::vector<SignalPattern> callPatterns_;
    wxChoice* inputChoice_ = nullptr;
    wxChoice* outputChoice_ = nullptr;
    wxChoice* rogerChoice_ = nullptr;
    wxChoice* callChoice_ = nullptr;
    wxButton* rogerPlayBtn_ = nullptr;
    wxButton* callPlayBtn_ = nullptr;
    wxButton* rogerCustomBtn_ = nullptr;
    wxButton* rogerDeleteBtn_ = nullptr;
    wxButton* callCustomBtn_ = nullptr;
    wxButton* callDeleteBtn_ = nullptr;
    wxStaticText* pttKeyLabel_ = nullptr;
    wxButton* pttCaptureBtn_ = nullptr;
    wxButton* pttClearBtn_ = nullptr;
    wxChoice* langChoice_ = nullptr;
    wxCheckBox* pttToggleCheck_ = nullptr;
    wxCheckBox* micLevelCheck_ = nullptr;
    std::vector<int> inputIds_;
    std::vector<int> outputIds_;
    std::vector<std::string> rogerIds_;
    std::vector<std::string> callIds_;
    int selectedPttVKey_ = 0;
    int selectedPttMods_ = 0;
};

wxString ToWxStatusText(const std::string& msg) {
    if (msg.empty()) {
        return {};
    }
    // Relay/ASIO error text on Windows may come in non-UTF8 system encoding.
    // Avoid dropping status to empty label when UTF-8 decode fails.
    wxString s = wxString::FromUTF8(msg.c_str());
    if (s.empty()) {
        s = wxString::From8BitData(msg.c_str());
    }
    return s;
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(
          nullptr,
          wxID_ANY,
          wxString::Format(_("O-Walkie Desktop (%s)"), wxString::FromUTF8(OWALKIE_VERSION)),
          wxDefaultPosition,
          wxSize(680, 560)),
      relay_(std::make_unique<RelayClient>()),
      audio_(std::make_unique<AudioEngine>()),
      reconnectTimer_(this) {
    BuildUi();
    CreateStatusBar(1);
    SetStatusBarPane(0);
    SetStatus("Disconnected");
    BindUi();

    reconnectTimer_.Bind(wxEVT_TIMER, &MainFrame::OnReconnectTimer, this);
#ifdef _WIN32
    InstallGlobalPttHook();
#endif

    relay_->SetStatusCallback([this](const std::string& msg) {
        this->CallAfter([this, msg] {
            wxString text = ToWxStatusText(msg);
            if (!text.empty()) {
                SetStatus(text);
            }
        });
    });
    relay_->SetConnectedCallback([this](bool connected) {
        this->CallAfter([this, connected] {
            connected_ = connected;
            if (connected) {
                reconnectBackoffMs_ = 1500;
                StopReconnectTimer();
            }
            connectBtn_->SetLabel((connected || userWantsSession_) ? _("Disconnect") : _("Connect"));
            ResetPttReleaseBurstGuard();
            RefreshPttUi();
            UpdateProfileControlsEnabled();
            // Guard path: if transport dropped but loss callback was missed/raced,
            // keep reconnect loop alive as long as user still wants the session.
            if (!connected && userWantsSession_ && !reconnectTimer_.IsRunning()) {
                ScheduleReconnect();
            }
        });
    });
    relay_->SetWelcomeCallback([this](const WelcomeConfig& cfg) {
        audio_->Reconfigure(cfg);
        audio_->PlayConnectedSignal();
    });
    relay_->SetOpusFrameCallback([this](const std::vector<uint8_t>& opus) {
        audio_->OnIncomingOpusFrame(opus);
    });
    relay_->SetTxStopCallback([this] { this->CallAfter([this] { OnTxStop(); }); });
    relay_->SetConnectionLostCallback([this] { this->CallAfter([this] { OnRelayConnectionLost(); }); });

    audio_->SetEncodedFrameCallback([this](const uint8_t* data, size_t size, uint8_t signal) {
        relay_->SendOpusFrame(data, size, signal);
    });
    audio_->SetStatusCallback([this](const std::string& msg) {
        this->CallAfter([this, msg] {
            wxString text = ToWxStatusText(msg);
            if (!text.empty()) {
                SetStatus(text);
            }
        });
    });
    audio_->SetLevelCallback([this](int percent) {
        this->CallAfter([this, percent] {
            if (!showMicLevelIndicator_) {
                return;
            }
            signalGauge_->SetValue(percent);
        });
    });
    audio_->Initialize();
    PopulateAudioDeviceChoices();
    MigrateLegacyConnectionJsonIfNeeded();
    LoadAllSettings();
    UpdateMicLevelIndicatorVisibility();
    ApplyAudioSettingsToEngine();
    SyncRxVolumeUi();
    UpdateProfileControlsEnabled();
    RefreshPttUi();

    CallAfter([this] {
        if (hostCtrl_) {
            hostCtrl_->SetFocus();
        }
    });
}

MainFrame::~MainFrame() {
#ifdef _WIN32
    UninstallGlobalPttHook();
#endif
    StopReconnectTimer();
    SaveAudioSettings();
    SaveProfilesToDisk();
    audio_->Shutdown();
    relay_->Disconnect();
}

wxString MainFrame::UserDataDir() {
    wxString dir = wxStandardPaths::Get().GetUserDataDir();
    if (!wxFileName::DirExists(dir)) {
        wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
    return dir;
}

wxString MainFrame::ProfilesPath() {
    return wxFileName(UserDataDir(), wxString("profiles.json")).GetFullPath();
}

wxString MainFrame::AudioSettingsPath() {
    return wxFileName(UserDataDir(), wxString("audio.json")).GetFullPath();
}

wxString MainFrame::LegacyConnectionPath() {
    return wxFileName(UserDataDir(), wxString("connection.json")).GetFullPath();
}

wxString MainFrame::CustomSignalPatternsPath() {
    return wxFileName(UserDataDir(), wxString("custom_signal_patterns.json")).GetFullPath();
}

bool MainFrame::IsBuiltInRogerPatternId(const std::string& id) {
    return id == "none" || id == "variant_1" || id == "variant_2" || id == "variant_3";
}

bool MainFrame::IsBuiltInCallPatternId(const std::string& id) {
    return id == "call_variant_1" || id == "call_variant_2" || id == "call_variant_3";
}

std::vector<SignalPattern> MainFrame::MergedRogerPatternsForUi() const {
    std::vector<SignalPattern> out;
    for (const auto& p : AudioEngine::RogerPatterns()) {
        SignalPattern q = p;
        q.name = OwTranslatePatternDisplayName(p.name);
        out.push_back(std::move(q));
    }
    out.insert(out.end(), customRogerPatterns_.begin(), customRogerPatterns_.end());
    return out;
}

std::vector<SignalPattern> MainFrame::MergedCallPatternsForUi() const {
    std::vector<SignalPattern> out;
    for (const auto& p : AudioEngine::CallPatterns()) {
        SignalPattern q = p;
        q.name = OwTranslatePatternDisplayName(p.name);
        out.push_back(std::move(q));
    }
    out.insert(out.end(), customCallPatterns_.begin(), customCallPatterns_.end());
    return out;
}

void MainFrame::ApplyCustomPatternsToEngine() {
    audio_->SetCustomSignalPatterns(customRogerPatterns_, customCallPatterns_);
}

void MainFrame::LoadCustomSignalPatternsFromDisk() {
    customRogerPatterns_.clear();
    customCallPatterns_.clear();
    try {
        const wxString path = CustomSignalPatternsPath();
        if (!wxFileName::FileExists(path)) {
            return;
        }
        std::ifstream in(path.utf8_string());
        if (!in) {
            return;
        }
        nlohmann::json j;
        in >> j;
        for (const auto& item : j.value("roger", nlohmann::json::array())) {
            SignalPattern p;
            p.id = item.value("id", std::string{});
            p.name = item.value("name", std::string{});
            p.appendTail = true;
            for (const auto& pt : item.value("points", nlohmann::json::array())) {
                SignalPatternPoint q;
                q.freqHz = pt.value("freqHz", 0.0);
                q.durationMs = pt.value("durationMs", 0);
                p.points.push_back(q);
            }
            if (!p.id.empty() && !p.points.empty()) {
                customRogerPatterns_.push_back(std::move(p));
            }
        }
        for (const auto& item : j.value("call", nlohmann::json::array())) {
            SignalPattern p;
            p.id = item.value("id", std::string{});
            p.name = item.value("name", std::string{});
            p.appendTail = false;
            for (const auto& pt : item.value("points", nlohmann::json::array())) {
                SignalPatternPoint q;
                q.freqHz = pt.value("freqHz", 0.0);
                q.durationMs = pt.value("durationMs", 0);
                p.points.push_back(q);
            }
            if (!p.id.empty() && !p.points.empty()) {
                customCallPatterns_.push_back(std::move(p));
            }
        }
    } catch (...) {
        customRogerPatterns_.clear();
        customCallPatterns_.clear();
    }
}

void MainFrame::SaveCustomSignalPatternsToDisk() const {
    try {
        nlohmann::json j;
        nlohmann::json arrRoger = nlohmann::json::array();
        for (const auto& p : customRogerPatterns_) {
            nlohmann::json o;
            o["id"] = p.id;
            o["name"] = p.name;
            nlohmann::json pts = nlohmann::json::array();
            for (const auto& q : p.points) {
                nlohmann::json ptj;
                ptj["freqHz"] = q.freqHz;
                ptj["durationMs"] = q.durationMs;
                pts.push_back(ptj);
            }
            o["points"] = pts;
            arrRoger.push_back(o);
        }
        j["roger"] = arrRoger;
        nlohmann::json arrCall = nlohmann::json::array();
        for (const auto& p : customCallPatterns_) {
            nlohmann::json o;
            o["id"] = p.id;
            o["name"] = p.name;
            nlohmann::json pts = nlohmann::json::array();
            for (const auto& q : p.points) {
                nlohmann::json ptj;
                ptj["freqHz"] = q.freqHz;
                ptj["durationMs"] = q.durationMs;
                pts.push_back(ptj);
            }
            o["points"] = pts;
            arrCall.push_back(o);
        }
        j["call"] = arrCall;
        std::ofstream out(CustomSignalPatternsPath().utf8_string());
        if (out) {
            out << j.dump(2);
        }
    } catch (...) {
    }
}

static std::string NewCustomPatternId() {
#ifdef _WIN32
    return "custom_" + std::to_string(GetTickCount64());
#else
    return "custom_" + std::to_string(
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
#endif
}

void MainFrame::UpsertCustomRogerPattern(SignalPattern pattern) {
    pattern.appendTail = true;
    for (auto& existing : customRogerPatterns_) {
        if (PatternNameAsciiIEq(existing.name, pattern.name)) {
            pattern.id = existing.id;
            existing = std::move(pattern);
            selectedRogerPatternId_ = existing.id;
            SaveCustomSignalPatternsToDisk();
            ApplyCustomPatternsToEngine();
            audio_->SetRogerPatternId(selectedRogerPatternId_);
            SaveAudioSettings();
            return;
        }
    }
    if (pattern.id.empty()) {
        pattern.id = NewCustomPatternId();
    }
    customRogerPatterns_.push_back(std::move(pattern));
    selectedRogerPatternId_ = customRogerPatterns_.back().id;
    SaveCustomSignalPatternsToDisk();
    ApplyCustomPatternsToEngine();
    audio_->SetRogerPatternId(selectedRogerPatternId_);
    SaveAudioSettings();
}

void MainFrame::UpsertCustomCallPattern(SignalPattern pattern) {
    pattern.appendTail = false;
    for (auto& existing : customCallPatterns_) {
        if (PatternNameAsciiIEq(existing.name, pattern.name)) {
            pattern.id = existing.id;
            existing = std::move(pattern);
            selectedCallPatternId_ = existing.id;
            SaveCustomSignalPatternsToDisk();
            ApplyCustomPatternsToEngine();
            audio_->SetCallPatternId(selectedCallPatternId_);
            SaveAudioSettings();
            return;
        }
    }
    if (pattern.id.empty()) {
        pattern.id = NewCustomPatternId();
    }
    customCallPatterns_.push_back(std::move(pattern));
    selectedCallPatternId_ = customCallPatterns_.back().id;
    SaveCustomSignalPatternsToDisk();
    ApplyCustomPatternsToEngine();
    audio_->SetCallPatternId(selectedCallPatternId_);
    SaveAudioSettings();
}

bool MainFrame::DeleteCustomRogerPattern(const std::string& id) {
    const auto it = std::remove_if(
        customRogerPatterns_.begin(), customRogerPatterns_.end(), [&](const SignalPattern& p) { return p.id == id; });
    if (it == customRogerPatterns_.end()) {
        return false;
    }
    customRogerPatterns_.erase(it, customRogerPatterns_.end());
    if (selectedRogerPatternId_ == id) {
        selectedRogerPatternId_ = "none";
        audio_->SetRogerPatternId(selectedRogerPatternId_);
        SaveAudioSettings();
    }
    SaveCustomSignalPatternsToDisk();
    ApplyCustomPatternsToEngine();
    return true;
}

bool MainFrame::DeleteCustomCallPattern(const std::string& id) {
    const auto it = std::remove_if(
        customCallPatterns_.begin(), customCallPatterns_.end(), [&](const SignalPattern& p) { return p.id == id; });
    if (it == customCallPatterns_.end()) {
        return false;
    }
    customCallPatterns_.erase(it, customCallPatterns_.end());
    if (selectedCallPatternId_ == id) {
        selectedCallPatternId_ = "call_variant_1";
        audio_->SetCallPatternId(selectedCallPatternId_);
        SaveAudioSettings();
    }
    SaveCustomSignalPatternsToDisk();
    ApplyCustomPatternsToEngine();
    return true;
}

void MainFrame::MigrateLegacyConnectionJsonIfNeeded() {
    if (wxFileName::FileExists(ProfilesPath())) {
        return;
    }
    const wxString leg = LegacyConnectionPath();
    if (!wxFileName::FileExists(leg)) {
        return;
    }
    try {
        std::ifstream in(leg.utf8_string());
        if (!in) {
            return;
        }
        nlohmann::json j;
        in >> j;
        ServerProfile p;
        p.name = "Default";
        if (j.contains("host") && j["host"].is_string()) {
            p.host = j["host"].get<std::string>();
        }
        if (j.contains("ws_port")) {
            p.wsPort = j["ws_port"].get<int>();
        }
        if (j.contains("udp_port")) {
            p.udpPort = j["udp_port"].get<int>();
        }
        if (j.contains("channel") && j["channel"].is_string()) {
            p.channel = j["channel"].get<std::string>();
        }
        if (j.contains("repeater") && j["repeater"].is_boolean()) {
            p.repeater = j["repeater"].get<bool>();
        }
        profiles_.clear();
        profiles_.push_back(p);
        activeProfileIndex_ = 0;

        nlohmann::json audio;
        audio["audio_input_device"] = j.value("audio_input_device", -1);
        audio["audio_output_device"] = j.value("audio_output_device", -1);
        const std::string audioPath = AudioSettingsPath().utf8_string();
        std::ofstream out(audioPath);
        if (out) {
            out << audio.dump(2);
        }

        SaveProfilesToDisk();
    } catch (...) {
    }
}

void MainFrame::LoadAllSettings() {
    profiles_.clear();
    try {
        const wxString path = ProfilesPath();
        if (wxFileName::FileExists(path)) {
            std::ifstream in(path.utf8_string());
            if (in) {
                nlohmann::json root;
                in >> root;
                activeProfileIndex_ = root.value("active", 0);
                if (root.contains("profiles") && root["profiles"].is_array()) {
                    for (const auto& item : root["profiles"]) {
                        ServerProfile p;
                        p.name = item.value("name", std::string("Profile"));
                        p.host = item.value("host", std::string("127.0.0.1"));
                        p.wsPort = item.value("ws_port", 5500);
                        p.udpPort = item.value("udp_port", 5505);
                        p.channel = item.value("channel", std::string("global"));
                        p.repeater = item.value("repeater", false);
                        profiles_.push_back(std::move(p));
                    }
                }
            }
        }
    } catch (...) {
    }
    if (profiles_.empty()) {
        profiles_.push_back(ServerProfile{});
        activeProfileIndex_ = 0;
    }
    if (activeProfileIndex_ < 0 || activeProfileIndex_ >= static_cast<int>(profiles_.size())) {
        activeProfileIndex_ = 0;
    }

    RepopulateProfileChoice();
    SyncUiFromActiveProfile();

    try {
        wxString audioPath = AudioSettingsPath();
        if (!wxFileName::FileExists(audioPath)) {
            audioPath = LegacyConnectionPath();
        }
        if (wxFileName::FileExists(audioPath)) {
            std::ifstream in(audioPath.utf8_string());
            if (in) {
                nlohmann::json j;
                in >> j;
                selectedInputDeviceId_ = j.value("audio_input_device", -1);
                selectedOutputDeviceId_ = j.value("audio_output_device", -1);
                selectedRogerPatternId_ = j.value("roger_pattern_id", std::string("variant_1"));
                selectedCallPatternId_ = j.value("call_pattern_id", std::string("call_variant_1"));
                globalPttVKey_ = j.value("ptt_hotkey_vkey", 0);
                globalPttMods_ = j.value("ptt_hotkey_mods", 0);
                pttToggleMode_ = j.value("ptt_toggle_mode", false);
                showMicLevelIndicator_ = j.value("show_mic_level_indicator", false);
                rxVolumePercent_ = j.value("rx_volume_percent", 100);
                uiLanguageCode_ = j.value("ui_language", std::string("en"));
                if (uiLanguageCode_ != "ru") {
                    uiLanguageCode_ = "en";
                }
            }
        }
    } catch (...) {
    }
    LoadCustomSignalPatternsFromDisk();
    ApplyCustomPatternsToEngine();
}

void MainFrame::SaveProfilesToDisk() {
    if (profiles_.empty()) {
        return;
    }
    if (activeProfileIndex_ < 0 || activeProfileIndex_ >= static_cast<int>(profiles_.size())) {
        activeProfileIndex_ = 0;
    }
    nlohmann::json root;
    root["active"] = activeProfileIndex_;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : profiles_) {
        nlohmann::json o;
        o["name"] = p.name;
        o["host"] = p.host;
        o["ws_port"] = p.wsPort;
        o["udp_port"] = p.udpPort;
        o["channel"] = p.channel;
        o["repeater"] = p.repeater;
        arr.push_back(o);
    }
    root["profiles"] = arr;
    try {
        std::ofstream out(ProfilesPath().utf8_string());
        if (out) {
            out << root.dump(2);
        }
    } catch (...) {
    }
}

void MainFrame::SaveAudioSettings() {
    nlohmann::json j;
    j["audio_input_device"] = selectedInputDeviceId_;
    j["audio_output_device"] = selectedOutputDeviceId_;
    j["roger_pattern_id"] = selectedRogerPatternId_;
    j["call_pattern_id"] = selectedCallPatternId_;
    j["ptt_hotkey_vkey"] = globalPttVKey_;
    j["ptt_hotkey_mods"] = globalPttMods_;
    j["ptt_toggle_mode"] = pttToggleMode_;
    j["show_mic_level_indicator"] = showMicLevelIndicator_;
    j["rx_volume_percent"] = std::clamp(rxVolumePercent_, 0, 200);
    j["ui_language"] = uiLanguageCode_;
    try {
        std::ofstream out(AudioSettingsPath().utf8_string());
        if (out) {
            out << j.dump(2);
        }
    } catch (...) {
    }
}

void MainFrame::RepopulateProfileChoice() {
    profileChoice_->Clear();
    for (const auto& p : profiles_) {
        profileChoice_->Append(wxString::FromUTF8(p.name));
    }
    if (!profiles_.empty()) {
        profileChoice_->SetSelection(std::clamp(activeProfileIndex_, 0, static_cast<int>(profiles_.size()) - 1));
    }
}

void MainFrame::SyncUiFromActiveProfile() {
    if (activeProfileIndex_ < 0 || activeProfileIndex_ >= static_cast<int>(profiles_.size())) {
        return;
    }
    const ServerProfile& p = profiles_[static_cast<size_t>(activeProfileIndex_)];
    connectionNameCtrl_->SetValue(wxString::FromUTF8(p.name));
    hostCtrl_->SetValue(wxString::FromUTF8(p.host));
    wsPortCtrl_->SetValue(wxString::Format("%d", p.wsPort));
    udpPortCtrl_->SetValue(wxString::Format("%d", p.udpPort));
    channelCtrl_->SetValue(wxString::FromUTF8(p.channel));
    repeaterCheck_->SetValue(p.repeater);
}

void MainFrame::SyncActiveProfileFromUi() {
    if (activeProfileIndex_ < 0 || activeProfileIndex_ >= static_cast<int>(profiles_.size())) {
        return;
    }
    ServerProfile& p = profiles_[static_cast<size_t>(activeProfileIndex_)];
    long ws = 0;
    long udp = 0;
    wsPortCtrl_->GetValue().ToLong(&ws);
    udpPortCtrl_->GetValue().ToLong(&udp);
    p.host = hostCtrl_->GetValue().utf8_string();
    p.name = connectionNameCtrl_->GetValue().utf8_string();
    if (p.name.empty()) {
        p.name = _("Connection").utf8_string();
    }
    p.wsPort = static_cast<int>(ws);
    p.udpPort = static_cast<int>(udp);
    p.channel = channelCtrl_->GetValue().utf8_string();
    p.repeater = repeaterCheck_->GetValue();
}

void MainFrame::UpdateProfileControlsEnabled() {
    const bool sess = connected_ || userWantsSession_;
    profileChoice_->Enable(!sess);
    saveProfileBtn_->Enable(!sess);
    newProfileBtn_->Enable(!sess);
    deleteProfileBtn_->Enable(!sess && profiles_.size() > 1);
    if (connectionNameCtrl_) {
        connectionNameCtrl_->SetEditable(!sess);
    }
    if (hostCtrl_) {
        hostCtrl_->SetEditable(!sess);
    }
    if (wsPortCtrl_) {
        wsPortCtrl_->SetEditable(!sess);
    }
    if (udpPortCtrl_) {
        udpPortCtrl_->SetEditable(!sess);
    }
    if (channelCtrl_) {
        channelCtrl_->SetEditable(!sess);
    }
    if (repeaterCheck_) {
        repeaterCheck_->Enable(true);
    }
}

void MainFrame::OnProfileChoice(wxCommandEvent&) {
    const int sel = profileChoice_->GetSelection();
    if (sel == wxNOT_FOUND || sel < 0 || sel >= static_cast<int>(profiles_.size())) {
        return;
    }
    activeProfileIndex_ = sel;
    SyncUiFromActiveProfile();
    SaveProfilesToDisk();
}

void MainFrame::OnSaveProfile(wxCommandEvent&) {
    SyncActiveProfileFromUi();
    SaveProfilesToDisk();
    RepopulateProfileChoice();
    SetStatus("Profile saved");
}

void MainFrame::OnNewProfile(wxCommandEvent&) {
    SyncActiveProfileFromUi();
    ServerProfile p;
    const wxString entered = connectionNameCtrl_ ? connectionNameCtrl_->GetValue().Trim() : wxString{};
    if (!entered.empty()) {
        p.name = entered.utf8_string();
    } else {
        p.name = wxString::Format(_("Connection %d"), static_cast<int>(profiles_.size()) + 1).utf8_string();
    }
    p.host = hostCtrl_->GetValue().utf8_string();
    long ws = 0;
    long udp = 0;
    wsPortCtrl_->GetValue().ToLong(&ws);
    udpPortCtrl_->GetValue().ToLong(&udp);
    p.wsPort = static_cast<int>(ws);
    p.udpPort = static_cast<int>(udp);
    p.channel = channelCtrl_->GetValue().utf8_string();
    p.repeater = repeaterCheck_->GetValue();
    profiles_.push_back(p);
    activeProfileIndex_ = static_cast<int>(profiles_.size()) - 1;
    SaveProfilesToDisk();
    RepopulateProfileChoice();
    SetStatus("Profile created");
}

void MainFrame::OnDeleteProfile(wxCommandEvent&) {
    if (profiles_.size() <= 1) {
        return;
    }
    const int sel = profileChoice_->GetSelection();
    if (sel == wxNOT_FOUND || sel < 0) {
        return;
    }
    profiles_.erase(profiles_.begin() + sel);
    activeProfileIndex_ = std::min(sel, static_cast<int>(profiles_.size()) - 1);
    SaveProfilesToDisk();
    RepopulateProfileChoice();
    SyncUiFromActiveProfile();
    SetStatus("Profile deleted");
}

void MainFrame::StopReconnectTimer() {
    reconnectScheduleTicket_.fetch_add(1, std::memory_order_relaxed);
    if (reconnectTimer_.IsRunning()) {
        reconnectTimer_.Stop();
    }
}

void MainFrame::ScheduleReconnect() {
    // userWantsSession_ is the UI source of truth; relay may lag on atomic flags during teardown.
    if (!userWantsSession_) {
        return;
    }
    const int delayMs = reconnectBackoffMs_;
    const uint64_t ticket = reconnectScheduleTicket_.fetch_add(1, std::memory_order_relaxed) + 1;
    SetStatus(wxString::Format("Reconnecting in %d ms", delayMs));
    std::thread([this, ticket, delayMs] {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        this->CallAfter([this, ticket] {
            if (ticket != reconnectScheduleTicket_.load(std::memory_order_relaxed)) {
                return;
            }
            if (!userWantsSession_ || relay_->IsConnected()) {
                return;
            }
            StartReconnectAttemptAsync();
        });
    }).detach();
}

void MainFrame::OnReconnectTimer(wxTimerEvent&) {
    // Legacy fallback path (timer no longer primary reconnect scheduler).
    if (!userWantsSession_ || relay_->IsConnected()) {
        return;
    }
    StartReconnectAttemptAsync();
}

bool MainFrame::TryConnectWithCurrentFields() {
    long wsPort = 0;
    long udpPort = 0;
    if (!wsPortCtrl_->GetValue().ToLong(&wsPort) || !udpPortCtrl_->GetValue().ToLong(&udpPort)) {
        SetStatus("Invalid ports");
        return false;
    }
    const std::string hostU8 = hostCtrl_->GetValue().utf8_string();
    const std::string chU8 = channelCtrl_->GetValue().utf8_string();
    return relay_->Connect(hostU8, static_cast<int>(wsPort), static_cast<int>(udpPort), chU8, repeaterCheck_->GetValue());
}

void MainFrame::StartReconnectAttemptAsync() {
    bool expected = false;
    if (!reconnectAttemptInFlight_.compare_exchange_strong(expected, true)) {
        return;
    }

    long wsPort = 0;
    long udpPort = 0;
    if (!wsPortCtrl_->GetValue().ToLong(&wsPort) || !udpPortCtrl_->GetValue().ToLong(&udpPort)) {
        reconnectAttemptInFlight_.store(false);
        SetStatus("Invalid ports");
        return;
    }
    const std::string hostU8 = hostCtrl_->GetValue().utf8_string();
    const std::string chU8 = channelCtrl_->GetValue().utf8_string();
    const bool repeater = repeaterCheck_->GetValue();
    const int attemptNo = ++reconnectAttemptSeq_;
    SetStatus(wxString::Format("Reconnect attempt #%d", attemptNo));

    std::thread([this, hostU8, chU8, wsPort, udpPort, repeater] {
        const bool ok = relay_->Connect(hostU8, static_cast<int>(wsPort), static_cast<int>(udpPort), chU8, repeater);
        this->CallAfter([this, ok] {
            reconnectAttemptInFlight_.store(false);
            if (!userWantsSession_) {
                return;
            }
            if (ok) {
                reconnectBackoffMs_ = 1500;
                reconnectAttemptSeq_ = 0;
                SaveAudioSettings();
                SetStatus("Reconnected");
                return;
            }
            SetStatus(wxString::Format("Reconnect failed, retry in %d ms", reconnectBackoffMs_));
            reconnectBackoffMs_ = std::min(reconnectBackoffMs_ * 2, 30000);
            ScheduleReconnect();
        });
    }).detach();
}

void MainFrame::OnRelayConnectionLost() {
    ResetPttReleaseBurstGuard();
    audio_->StopTransmit();
    globalPttPressed_ = false;
    globalPttToggleHookDown_ = false;
    connected_ = false;
    audio_->PlayConnectionErrorSignal();
    connectBtn_->SetLabel(userWantsSession_ ? _("Disconnect") : _("Connect"));
    UpdateProfileControlsEnabled();
    RefreshPttUi();

    if (!userWantsSession_) {
        SetStatus("Disconnected");
        return;
    }
    SetStatus("Connection lost — retrying");
    ScheduleReconnect();
}

void MainFrame::BuildUi() {
    auto* panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* profileRow = new wxFlexGridSizer(2, 8, 10);
    profileRow->AddGrowableCol(1, 1);
    profileLabel_ = new wxStaticText(panel, wxID_ANY, _("Connections"));
    SkipKeyboardFocus(profileLabel_);
    profileRow->Add(profileLabel_, 0, wxALIGN_CENTER_VERTICAL);
    profileChoice_ = new wxChoice(panel, wxID_ANY);
    profileChoice_->SetName(_("Connections"));
    profileRow->Add(profileChoice_, 1, wxEXPAND);
    root->Add(profileRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    auto* profileBtns = new wxBoxSizer(wxHORIZONTAL);
    saveProfileBtn_ = new wxButton(panel, wxID_ANY, _("Save"));
    saveProfileBtn_->SetName(_("Save connection"));
    newProfileBtn_ = new wxButton(panel, wxID_ANY, _("New"));
    newProfileBtn_->SetName(_("New connection"));
    deleteProfileBtn_ = new wxButton(panel, wxID_ANY, _("Delete"));
    deleteProfileBtn_->SetName(_("Delete connection"));
    profileBtns->Add(saveProfileBtn_, 0, wxRIGHT, 8);
    profileBtns->Add(newProfileBtn_, 0, wxRIGHT, 8);
    profileBtns->Add(deleteProfileBtn_, 0);
    root->Add(profileBtns, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* grid = new wxFlexGridSizer(2, 8, 10);
    grid->AddGrowableCol(1, 1);

    auto* labName = new wxStaticText(panel, wxID_ANY, _("Connection name"));
    SkipKeyboardFocus(labName);
    grid->Add(labName, 0, wxALIGN_CENTER_VERTICAL);
    connectionNameCtrl_ = new wxTextCtrl(panel, wxID_ANY, _("Default"));
    connectionNameCtrl_->SetName(_("Connection name"));
    grid->Add(connectionNameCtrl_, 1, wxEXPAND);

    auto* labHost = new wxStaticText(panel, wxID_ANY, _("Server host"));
    SkipKeyboardFocus(labHost);
    grid->Add(labHost, 0, wxALIGN_CENTER_VERTICAL);
    hostCtrl_ = new wxTextCtrl(panel, wxID_ANY, "127.0.0.1");
    hostCtrl_->SetName(_("Host"));
    grid->Add(hostCtrl_, 1, wxEXPAND);

    auto* labWs = new wxStaticText(panel, wxID_ANY, _("WebSocket port"));
    SkipKeyboardFocus(labWs);
    grid->Add(labWs, 0, wxALIGN_CENTER_VERTICAL);
    wsPortCtrl_ = new wxTextCtrl(panel, wxID_ANY, "5500");
    wsPortCtrl_->SetName(_("WebSocket port"));
    grid->Add(wsPortCtrl_, 1, wxEXPAND);

    auto* labUdp = new wxStaticText(panel, wxID_ANY, _("UDP port"));
    SkipKeyboardFocus(labUdp);
    grid->Add(labUdp, 0, wxALIGN_CENTER_VERTICAL);
    udpPortCtrl_ = new wxTextCtrl(panel, wxID_ANY, "5505");
    udpPortCtrl_->SetName(_("UDP port"));
    grid->Add(udpPortCtrl_, 1, wxEXPAND);

    auto* labCh = new wxStaticText(panel, wxID_ANY, _("Channel"));
    SkipKeyboardFocus(labCh);
    grid->Add(labCh, 0, wxALIGN_CENTER_VERTICAL);
    channelCtrl_ = new wxTextCtrl(panel, wxID_ANY, "global");
    channelCtrl_->SetName(_("Channel"));
    grid->Add(channelCtrl_, 1, wxEXPAND);

    auto* labRx = new wxStaticText(panel, wxID_ANY, _("Incoming volume"));
    SkipKeyboardFocus(labRx);
    grid->Add(labRx, 0, wxALIGN_CENTER_VERTICAL);
    auto* rxRow = new wxBoxSizer(wxHORIZONTAL);
    rxVolumeSlider_ = new wxSlider(panel, wxID_ANY, rxVolumePercent_, 0, 200, wxDefaultPosition, wxSize(200, -1));
    rxVolumeSlider_->SetName(_("Incoming volume"));
    rxVolumeValueText_ = new wxStaticText(panel, wxID_ANY, wxString::Format("%d%%", rxVolumePercent_));
    SkipKeyboardFocus(rxVolumeValueText_);
    rxRow->Add(rxVolumeSlider_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    rxRow->Add(rxVolumeValueText_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(rxRow, 1, wxEXPAND);

    root->Add(grid, 0, wxEXPAND | wxALL, 12);

    auto* audioBar = new wxBoxSizer(wxHORIZONTAL);
    settingsBtn_ = new wxButton(panel, wxID_ANY, _("Settings"));
    settingsBtn_->SetName(_("Open settings"));
    audioBar->Add(settingsBtn_, 0, wxRIGHT, 10);
    root->Add(audioBar, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    repeaterCheck_ = new wxCheckBox(panel, wxID_ANY, _("Repeater mode"));
    repeaterCheck_->SetName(_("Repeater mode"));
    root->Add(repeaterCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    connectBtn_ = new wxButton(panel, wxID_ANY, _("Connect"));
    connectBtn_->SetName(_("Connect or disconnect"));
    pttBtn_ = new wxButton(panel, wxID_ANY, _("Hold to Talk"));
    pttBtn_->SetName(_("Push to talk"));
    callBtn_ = new wxButton(panel, wxID_ANY, _("Call"));
    callBtn_->SetName(_("Call signal"));
    pttBtn_->Enable(false);
    callBtn_->Enable(false);
    actions->Add(connectBtn_, 0, wxRIGHT, 10);
    actions->Add(pttBtn_, 0, wxRIGHT, 10);
    actions->Add(callBtn_, 0, wxRIGHT, 10);
    root->Add(actions, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    signalGauge_ = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(220, -1));
    signalGauge_->SetName(_("Transmit level"));
    SkipKeyboardFocus(signalGauge_);

    root->Add(signalGauge_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    panel->SetSizer(root);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(panel, 1, wxEXPAND);
    SetSizer(outer);

    saveProfileBtn_->MoveAfterInTabOrder(profileChoice_);
    newProfileBtn_->MoveAfterInTabOrder(saveProfileBtn_);
    deleteProfileBtn_->MoveAfterInTabOrder(newProfileBtn_);
    connectionNameCtrl_->MoveAfterInTabOrder(deleteProfileBtn_);
    hostCtrl_->MoveAfterInTabOrder(connectionNameCtrl_);
    wsPortCtrl_->MoveAfterInTabOrder(hostCtrl_);
    udpPortCtrl_->MoveAfterInTabOrder(wsPortCtrl_);
    channelCtrl_->MoveAfterInTabOrder(udpPortCtrl_);
    rxVolumeSlider_->MoveAfterInTabOrder(channelCtrl_);
    settingsBtn_->MoveAfterInTabOrder(rxVolumeSlider_);
    repeaterCheck_->MoveAfterInTabOrder(settingsBtn_);
    connectBtn_->MoveAfterInTabOrder(repeaterCheck_);
    pttBtn_->MoveAfterInTabOrder(connectBtn_);
    callBtn_->MoveAfterInTabOrder(pttBtn_);
}

void MainFrame::BindUi() {
    profileChoice_->Bind(wxEVT_CHOICE, &MainFrame::OnProfileChoice, this);
    saveProfileBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnSaveProfile, this);
    newProfileBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnNewProfile, this);
    deleteProfileBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnDeleteProfile, this);
    connectBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnConnectClicked, this);
    settingsBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnSettingsClicked, this);
    pttBtn_->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnPttDown, this);
    pttBtn_->Bind(wxEVT_LEFT_UP, &MainFrame::OnPttUp, this);
    pttBtn_->Bind(wxEVT_LEAVE_WINDOW, &MainFrame::OnPttUp, this);
    pttBtn_->Bind(wxEVT_KEY_DOWN, &MainFrame::OnPttButtonKeyDown, this);
    pttBtn_->Bind(wxEVT_KEY_UP, &MainFrame::OnPttButtonKeyUp, this);
    pttBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnPttButtonClicked, this);
    callBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnCallSignalClicked, this);
    rxVolumeSlider_->Bind(wxEVT_SLIDER, &MainFrame::OnRxVolumeSlider, this);
    repeaterCheck_->Bind(wxEVT_CHECKBOX, &MainFrame::OnRepeaterToggled, this);
}

void MainFrame::SetStatus(const wxString& status) {
    SetStatusText(HumanizeStatus(status), 0);
}

wxString MainFrame::HumanizeStatus(const wxString& status) const {
    const wxString s = status.Lower();
    if (s.IsEmpty()) {
        return _("Disconnected");
    }
    if (s.Contains("welcome received") || s == "connected" || s == "reconnected") {
        return _("Connected");
    }
    if (s.StartsWith("reconnecting in") || s.StartsWith("reconnect attempt") || s.Contains("retry")) {
        return _("Reconnecting...");
    }
    if (s.Contains("transmitting")) {
        return _("Transmitting");
    }
    if (s.Contains("sending call")) {
        return _("Sending call signal...");
    }
    if (s.Contains("tx stopped")) {
        return _("Transmission stopped by server");
    }
    if (s.Contains("connect failed")) {
        return _("Unable to connect");
    }
    if (s.Contains("protocol mismatch") || s.Contains("missing samplerate")) {
        return _("Incompatible server protocol");
    }
    if (s.Contains("connection lost") && s.Contains("retrying")) {
        return _("Connection lost — retrying");
    }
    if (s.Contains("ws ended") || s.Contains("udp ended") || s.Contains("keepalive timeout") || s.Contains("connection lost")) {
        return _("Connection lost");
    }
    if (s.Contains("audio initialized")) {
        return _("Ready");
    }
    if (s.Contains("miniaudio") && s.Contains("failed")) {
        return _("Audio failed to initialize");
    }
    if (s == "disconnected") {
        return _("Disconnected");
    }
    if (s == "settings applied") {
        return _("Settings applied");
    }
    if (s == "profile saved") {
        return _("Profile saved");
    }
    if (s == "profile created") {
        return _("Profile created");
    }
    if (s == "profile deleted") {
        return _("Profile deleted");
    }
    if (s == "call signal failed") {
        return _("Call signal failed");
    }
    if (s.Contains("ptt paused")) {
        return _("PTT paused — too many quick releases");
    }
    if (s == "invalid ports") {
        return _("Invalid ports");
    }
    return status;
}

void MainFrame::SyncRxVolumeUi() {
    if (!rxVolumeSlider_ || !rxVolumeValueText_) {
        return;
    }
    rxVolumePercent_ = std::clamp(rxVolumePercent_, 0, 200);
    rxVolumeSlider_->SetValue(rxVolumePercent_);
    rxVolumeValueText_->SetLabel(wxString::Format("%d%%", rxVolumePercent_));
    audio_->SetRxVolumePercent(rxVolumePercent_);
}

void MainFrame::OnRxVolumeSlider(wxCommandEvent&) {
    if (!rxVolumeSlider_) {
        return;
    }
    rxVolumePercent_ = std::clamp(rxVolumeSlider_->GetValue(), 0, 200);
    if (rxVolumeValueText_) {
        rxVolumeValueText_->SetLabel(wxString::Format("%d%%", rxVolumePercent_));
    }
    audio_->SetRxVolumePercent(rxVolumePercent_);
    SaveAudioSettings();
}

void MainFrame::OnRepeaterToggled(wxCommandEvent&) {
    const bool v = repeaterCheck_ && repeaterCheck_->GetValue();
    if (activeProfileIndex_ >= 0 && activeProfileIndex_ < static_cast<int>(profiles_.size())) {
        profiles_[static_cast<size_t>(activeProfileIndex_)].repeater = v;
        SaveProfilesToDisk();
    }
    if (connected_) {
        relay_->SetRepeaterMode(v);
    }
}

void MainFrame::UpdateMicLevelIndicatorVisibility() {
    if (!signalGauge_) {
        return;
    }
    signalGauge_->Show(showMicLevelIndicator_);
    if (!showMicLevelIndicator_) {
        signalGauge_->SetValue(0);
    }
    Layout();
}

void MainFrame::OnConnectClicked(wxCommandEvent&) {
    StopReconnectTimer();

    if (connected_ || userWantsSession_) {
        userWantsSession_ = false;
        audio_->PlayManualDisconnectSignal();
        audio_->StopTransmit();
        relay_->Disconnect();
        ResetPttReleaseBurstGuard();
        connected_ = false;
        connectBtn_->SetLabel(_("Connect"));
        globalPttPressed_ = false;
        globalPttToggleHookDown_ = false;
        UpdateProfileControlsEnabled();
        RefreshPttUi();
        SetStatus("Disconnected");
        return;
    }

    SyncActiveProfileFromUi();
    userWantsSession_ = true;
    audio_->PlayManualConnectStartSignal();
    reconnectBackoffMs_ = 1500;
    UpdateProfileControlsEnabled();
    connectBtn_->SetLabel(_("Disconnect"));

    if (TryConnectWithCurrentFields()) {
        SaveProfilesToDisk();
        SaveAudioSettings();
        SetStatus("Connected");
    } else {
        userWantsSession_ = false;
        connectBtn_->SetLabel(_("Connect"));
        UpdateProfileControlsEnabled();
        audio_->PlayConnectionErrorSignal();
        SetStatus("Connect failed");
    }
}

void MainFrame::OnPttDown(wxMouseEvent& event) {
    if (pttToggleMode_) {
        event.Skip();
        return;
    }
    BeginPttTx();
    event.Skip();
}

void MainFrame::OnPttUp(wxMouseEvent& event) {
    if (pttToggleMode_) {
        event.Skip();
        return;
    }
    EndPttTx();
    event.Skip();
}

void MainFrame::OnPttButtonKeyDown(wxKeyEvent& event) {
    if (pttToggleMode_) {
        event.Skip();
        return;
    }
    if (event.GetKeyCode() == WXK_SPACE && connected_) {
        BeginPttTx();
        return;
    }
    event.Skip();
}

void MainFrame::OnPttButtonKeyUp(wxKeyEvent& event) {
    if (pttToggleMode_) {
        event.Skip();
        return;
    }
    if (event.GetKeyCode() == WXK_SPACE && connected_) {
        EndPttTx();
        return;
    }
    event.Skip();
}

void MainFrame::OnPttButtonClicked(wxCommandEvent&) {
    if (!pttToggleMode_) {
        return;
    }
    TogglePttTx();
}

void MainFrame::OnCallSignalClicked(wxCommandEvent&) {
    if (!connected_ || audio_->IsTransmitting() || audio_->IsSignalStreaming()) {
        return;
    }
    SetStatus("Sending call signal");
    if (audio_->StreamCallSignal()) {
        relay_->SendTxEofBurst();
        audio_->ScheduleRxResumeHoldoff();
        SetStatus("Connected");
    } else {
        SetStatus("Call signal failed");
    }
}

void MainFrame::BeginPttTx() {
    if (!connected_ || audio_->IsSignalStreaming() || audio_->IsTransmitting()) {
        RefreshPttUi();
        return;
    }
    if (pttReleaseBurstBlocked_.load(std::memory_order_relaxed)) {
        ExtendPttReleaseBurstDecayTimer();
        CallAfter([this] { SetStatus("PTT paused — too many quick releases"); });
        RefreshPttUi();
        return;
    }
    audio_->PlayPttPressSignal();
    if (audio_->StartTransmit()) {
        SetStatus("Transmitting");
    }
    RefreshPttUi();
}

void MainFrame::EndPttTx() {
    if (!connected_ || !audio_->IsTransmitting()) {
        RefreshPttUi();
        return;
    }
    RecordPttReleaseBurst();
    audio_->StopTransmit();
    audio_->ScheduleRxResumeHoldoff();
    audio_->StreamRogerSignal();
    relay_->SendTxEofBurst();
    SetStatus("Connected");
    RefreshPttUi();
}

void MainFrame::TogglePttTx() {
    if (!connected_) {
        return;
    }
    if (audio_->IsTransmitting()) {
        EndPttTx();
    } else {
        BeginPttTx();
    }
}

void MainFrame::RefreshPttUi() {
    if (!pttBtn_) {
        return;
    }
    const bool burstBlocked = pttReleaseBurstBlocked_.load(std::memory_order_relaxed);
    const bool allowPtt = connected_ && !burstBlocked;
    pttBtn_->Enable(allowPtt);
    if (!allowPtt) {
        pttBtn_->SetLabel(_("PTT unavailable"));
    } else if (pttToggleMode_) {
        pttBtn_->SetLabel(audio_->IsTransmitting() ? _("Stop talking") : _("Start talking"));
    } else {
        pttBtn_->SetLabel(_("Hold to Talk"));
    }
    if (callBtn_) {
        callBtn_->Enable(connected_ && !audio_->IsTransmitting() && !audio_->IsSignalStreaming());
    }
}

void MainFrame::ResetPttReleaseBurstGuard() {
    pttReleaseBurstTimerTicket_.fetch_add(1, std::memory_order_relaxed);
    pttReleaseBurstCount_.store(0, std::memory_order_relaxed);
    pttReleaseBurstBlocked_.store(false, std::memory_order_relaxed);
}

void MainFrame::SyncPttButtonForBurstGuard() {
    RefreshPttUi();
}

void MainFrame::ArmPttReleaseBurstDecay(const uint64_t scheduleTicket) {
    std::thread([this, scheduleTicket] {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPttReleaseBurstTimerMs));
        this->CallAfter([this, scheduleTicket] {
            if (pttReleaseBurstTimerTicket_.load(std::memory_order_relaxed) != scheduleTicket) {
                return;
            }
            pttReleaseBurstCount_.store(0, std::memory_order_relaxed);
            pttReleaseBurstBlocked_.store(false, std::memory_order_relaxed);
            SyncPttButtonForBurstGuard();
            if (connected_) {
                SetStatus("Connected");
            }
        });
    }).detach();
}

void MainFrame::ExtendPttReleaseBurstDecayTimer() {
    const uint64_t ticket = pttReleaseBurstTimerTicket_.fetch_add(1, std::memory_order_relaxed) + 1;
    ArmPttReleaseBurstDecay(ticket);
}

void MainFrame::RecordPttReleaseBurst() {
    const uint64_t myTicket = pttReleaseBurstTimerTicket_.fetch_add(1, std::memory_order_relaxed) + 1;
    const int n = pttReleaseBurstCount_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n >= kPttReleaseBurstBlockThreshold) {
        pttReleaseBurstBlocked_.store(true, std::memory_order_relaxed);
    }
    ArmPttReleaseBurstDecay(myTicket);
    CallAfter([this] {
        SyncPttButtonForBurstGuard();
        if (pttReleaseBurstBlocked_.load(std::memory_order_relaxed)) {
            SetStatus("PTT paused — too many quick releases");
        }
    });
}

void MainFrame::OnTxStop() {
    audio_->StopTransmit();
    SetStatus("TX stopped by server");
    RefreshPttUi();
}

void MainFrame::PopulateAudioDeviceChoices() {
    inputDevices_ = AudioEngine::ListInputDevices();
    outputDevices_ = AudioEngine::ListOutputDevices();
}

void MainFrame::ApplyAudioSettingsToEngine() {
    audio_->SetPreferredInputDevice(selectedInputDeviceId_);
    audio_->SetPreferredOutputDevice(selectedOutputDeviceId_);
    audio_->SetRogerPatternId(selectedRogerPatternId_);
    audio_->SetCallPatternId(selectedCallPatternId_);
    audio_->SetRxVolumePercent(rxVolumePercent_);
}

void MainFrame::OnSettingsClicked(wxCommandEvent&) {
    PopulateAudioDeviceChoices();
    LoadCustomSignalPatternsFromDisk();
    ApplyCustomPatternsToEngine();
    const std::string langBefore = uiLanguageCode_;
    SettingsDialog dlg(
        this,
        this,
        inputDevices_,
        outputDevices_,
        selectedInputDeviceId_,
        selectedOutputDeviceId_,
        MergedRogerPatternsForUi(),
        MergedCallPatternsForUi(),
        selectedRogerPatternId_,
        selectedCallPatternId_,
        globalPttVKey_,
        globalPttMods_,
        showMicLevelIndicator_,
        pttToggleMode_,
        uiLanguageCode_);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }
    selectedInputDeviceId_ = dlg.SelectedInputId();
    selectedOutputDeviceId_ = dlg.SelectedOutputId();
    selectedRogerPatternId_ = dlg.SelectedRogerId();
    selectedCallPatternId_ = dlg.SelectedCallId();
    globalPttVKey_ = dlg.SelectedGlobalPttVKey();
    globalPttMods_ = dlg.SelectedGlobalPttMods();
    showMicLevelIndicator_ = dlg.ShowMicLevelIndicator();
    uiLanguageCode_ = dlg.SelectedUiLanguage();
    if (uiLanguageCode_ != "ru") {
        uiLanguageCode_ = "en";
    }
    const bool newToggle = dlg.PttToggleMode();
    if (pttToggleMode_ && !newToggle && audio_->IsTransmitting()) {
        EndPttTx();
    }
    pttToggleMode_ = newToggle;
    globalPttPressed_ = false;
    globalPttToggleHookDown_ = false;
    UpdateMicLevelIndicatorVisibility();
    ApplyAudioSettingsToEngine();
    SaveAudioSettings();
    RefreshPttUi();
    SetStatus("Settings applied");
    if (langBefore != uiLanguageCode_) {
        wxMessageBox(
            _("Please restart O-Walkie Desktop for the new language to take effect."),
            _("Language"),
            wxOK | wxICON_INFORMATION,
            this);
    }
}

#ifdef _WIN32
void MainFrame::InstallGlobalPttHook() {
    g_mainFrameForPttHook = this;
    if (!globalPttHook_) {
        globalPttHook_ = SetWindowsHookExA(WH_KEYBOARD_LL, MainFrame::GlobalPttKeyboardProc, GetModuleHandle(nullptr), 0);
    }
}

void MainFrame::UninstallGlobalPttHook() {
    if (globalPttHook_) {
        UnhookWindowsHookEx(static_cast<HHOOK>(globalPttHook_));
        globalPttHook_ = nullptr;
    }
    if (g_mainFrameForPttHook == this) {
        g_mainFrameForPttHook = nullptr;
    }
}

LRESULT CALLBACK MainFrame::GlobalPttKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_mainFrameForPttHook) {
        const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const int pressedVKey = data ? static_cast<int>(data->vkCode) : 0;
        if (data && g_mainFrameForPttHook->globalPttVKey_ > 0 &&
            IsPrimaryKeyMatch(g_mainFrameForPttHook->globalPttVKey_, pressedVKey)) {
            const int currentMods = CurrentModifierMask();
            const int requiredMods = g_mainFrameForPttHook->globalPttMods_;
            const bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            if (!isKeyUp && !AreRequiredModsPressed(requiredMods, currentMods)) {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            if (g_mainFrameForPttHook->pttToggleMode_) {
                if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
                    if (!g_mainFrameForPttHook->globalPttToggleHookDown_) {
                        g_mainFrameForPttHook->globalPttToggleHookDown_ = true;
                        g_mainFrameForPttHook->CallAfter([frame = g_mainFrameForPttHook] { frame->TogglePttTx(); });
                    }
                } else if (isKeyUp) {
                    g_mainFrameForPttHook->globalPttToggleHookDown_ = false;
                }
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && !g_mainFrameForPttHook->globalPttPressed_) {
                g_mainFrameForPttHook->globalPttPressed_ = true;
                g_mainFrameForPttHook->CallAfter([frame = g_mainFrameForPttHook] { frame->BeginPttTx(); });
            } else if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) && g_mainFrameForPttHook->globalPttPressed_) {
                g_mainFrameForPttHook->globalPttPressed_ = false;
                g_mainFrameForPttHook->CallAfter([frame = g_mainFrameForPttHook] { frame->EndPttTx(); });
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif
