#include "MainFrame.h"

#include <algorithm>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/slider.h>
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
MainFrame* g_mainFrameForPttHook = nullptr;
constexpr int kPttModShift = 1;
constexpr int kPttModCtrl = 2;
constexpr int kPttModAlt = 4;

int NormalizeHotkeyVKey(int vkey) {
    switch (vkey) {
        case VK_LSHIFT:
        case VK_RSHIFT:
            return VK_SHIFT;
        case VK_LCONTROL:
        case VK_RCONTROL:
            return VK_CONTROL;
        case VK_LMENU:
        case VK_RMENU:
            return VK_MENU;
        default:
            return vkey;
    }
}

std::string VKeyToDisplayName(int vkey) {
    if (vkey <= 0) {
        return "Not set";
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
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) mods |= kPttModShift;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) mods |= kPttModCtrl;
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) mods |= kPttModAlt;
    return mods;
}

std::string PttComboToDisplayName(int vkey, int mods) {
    if (vkey <= 0) {
        return "Not set";
    }
    std::string out;
    if ((mods & kPttModCtrl) != 0) out += "Ctrl+";
    if ((mods & kPttModAlt) != 0) out += "Alt+";
    if ((mods & kPttModShift) != 0) out += "Shift+";
    out += VKeyToDisplayName(vkey);
    return out;
}

class HotkeyCaptureDialog final : public wxDialog {
public:
    HotkeyCaptureDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "Press key combination", wxDefaultPosition, wxSize(380, 140),
                   wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* hint = new wxStaticText(this, wxID_ANY, "Press any key or key combination (with Ctrl/Alt/Shift).");
        keyText_ = new wxStaticText(this, wxID_ANY, "Waiting...");
        root->Add(hint, 0, wxALL, 12);
        root->Add(keyText_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        SetSizerAndFit(root);
        Bind(wxEVT_CHAR_HOOK, &HotkeyCaptureDialog::OnCharHook, this);
        CentreOnParent();
    }

    int CapturedVKey() const { return vkey_; }
    int CapturedMods() const { return mods_; }

private:
    void OnCharHook(wxKeyEvent& event) {
        int raw = static_cast<int>(event.GetRawKeyCode());
        if (raw <= 0) {
            const int key = event.GetKeyCode();
            switch (key) {
                case WXK_SHIFT:
                    raw = VK_SHIFT;
                    break;
                case WXK_CONTROL:
                    raw = VK_CONTROL;
                    break;
                case WXK_ALT:
                    raw = VK_MENU;
                    break;
                default:
                    break;
            }
        }
        if (raw <= 0) {
            event.Skip();
            return;
        }
        vkey_ = NormalizeHotkeyVKey(raw);
        mods_ = CurrentModifierMask();
        // Treat modifiers as ordinary primary keys when captured directly.
        if (vkey_ == VK_SHIFT) mods_ &= ~kPttModShift;
        if (vkey_ == VK_CONTROL) mods_ &= ~kPttModCtrl;
        if (vkey_ == VK_MENU) mods_ &= ~kPttModAlt;
        keyText_->SetLabel(wxString::FromUTF8(PttComboToDisplayName(vkey_, mods_)));
        EndModal(wxID_OK);
    }

private:
    wxStaticText* keyText_ = nullptr;
    int vkey_ = 0;
    int mods_ = 0;
};
#endif

void SkipKeyboardFocus(wxWindow* w) {
    if (w) {
        w->DisableFocusFromKeyboard();
    }
}

class SettingsDialog final : public wxDialog {
public:
    SettingsDialog(
        wxWindow* parent,
        const std::vector<NamedAudioDevice>& inputDevices,
        const std::vector<NamedAudioDevice>& outputDevices,
        int selectedInputId,
        int selectedOutputId,
        const std::vector<SignalPattern>& rogerPatterns,
        const std::vector<SignalPattern>& callPatterns,
        const std::string& selectedRogerId,
        const std::string& selectedCallId,
        int globalPttVKey,
        int globalPttMods,
        bool showMicLevelIndicator)
        : wxDialog(parent, wxID_ANY, "Settings", wxDefaultPosition, wxSize(520, 320), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          inputDevices_(inputDevices),
          outputDevices_(outputDevices),
          rogerPatterns_(rogerPatterns),
          callPatterns_(callPatterns) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* grid = new wxFlexGridSizer(2, 8, 10);
        grid->AddGrowableCol(1, 1);

        grid->Add(new wxStaticText(this, wxID_ANY, "Microphone"), 0, wxALIGN_CENTER_VERTICAL);
        inputChoice_ = new wxChoice(this, wxID_ANY);
        inputChoice_->Append("System default");
        inputIds_.push_back(-1);
        for (const auto& d : inputDevices_) {
            inputChoice_->Append(wxString::FromUTF8(d.name));
            inputIds_.push_back(d.index);
        }
        grid->Add(inputChoice_, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Speaker"), 0, wxALIGN_CENTER_VERTICAL);
        outputChoice_ = new wxChoice(this, wxID_ANY);
        outputChoice_->Append("System default");
        outputIds_.push_back(-1);
        for (const auto& d : outputDevices_) {
            outputChoice_->Append(wxString::FromUTF8(d.name));
            outputIds_.push_back(d.index);
        }
        grid->Add(outputChoice_, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Roger pattern"), 0, wxALIGN_CENTER_VERTICAL);
        rogerChoice_ = new wxChoice(this, wxID_ANY);
        for (const auto& p : rogerPatterns_) {
            rogerChoice_->Append(wxString::FromUTF8(p.name));
            rogerIds_.push_back(p.id);
        }
        grid->Add(rogerChoice_, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Call pattern"), 0, wxALIGN_CENTER_VERTICAL);
        callChoice_ = new wxChoice(this, wxID_ANY);
        for (const auto& p : callPatterns_) {
            callChoice_->Append(wxString::FromUTF8(p.name));
            callIds_.push_back(p.id);
        }
        grid->Add(callChoice_, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Global PTT key"), 0, wxALIGN_CENTER_VERTICAL);
        auto* pttRow = new wxBoxSizer(wxHORIZONTAL);
        pttKeyLabel_ = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(PttComboToDisplayName(globalPttVKey, globalPttMods)));
        pttCaptureBtn_ = new wxButton(this, wxID_ANY, "Assign key");
        pttClearBtn_ = new wxButton(this, wxID_ANY, "Clear");
        pttRow->Add(pttKeyLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        pttRow->Add(pttCaptureBtn_, 0, wxRIGHT, 8);
        pttRow->Add(pttClearBtn_, 0);
        grid->Add(pttRow, 1, wxEXPAND);
        grid->Add(new wxStaticText(this, wxID_ANY, "Microphone level indicator"), 0, wxALIGN_CENTER_VERTICAL);
        micLevelCheck_ = new wxCheckBox(this, wxID_ANY, "Show VU meter");
        micLevelCheck_->SetValue(showMicLevelIndicator);
        grid->Add(micLevelCheck_, 1, wxEXPAND);

        root->Add(grid, 1, wxEXPAND | wxALL, 12);
        root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
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
            pttKeyLabel_->SetLabel("Not set");
        });
    }

    int SelectedInputId() const { return PickId(inputChoice_, inputIds_); }
    int SelectedOutputId() const { return PickId(outputChoice_, outputIds_); }
    std::string SelectedRogerId() const { return PickString(rogerChoice_, rogerIds_); }
    std::string SelectedCallId() const { return PickString(callChoice_, callIds_); }
    int SelectedGlobalPttVKey() const { return selectedPttVKey_; }
    int SelectedGlobalPttMods() const { return selectedPttMods_; }
    bool ShowMicLevelIndicator() const { return micLevelCheck_ && micLevelCheck_->GetValue(); }

private:
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

private:
    const std::vector<NamedAudioDevice>& inputDevices_;
    const std::vector<NamedAudioDevice>& outputDevices_;
    const std::vector<SignalPattern>& rogerPatterns_;
    const std::vector<SignalPattern>& callPatterns_;
    wxChoice* inputChoice_ = nullptr;
    wxChoice* outputChoice_ = nullptr;
    wxChoice* rogerChoice_ = nullptr;
    wxChoice* callChoice_ = nullptr;
    wxStaticText* pttKeyLabel_ = nullptr;
    wxButton* pttCaptureBtn_ = nullptr;
    wxButton* pttClearBtn_ = nullptr;
    wxCheckBox* micLevelCheck_ = nullptr;
    std::vector<int> inputIds_;
    std::vector<int> outputIds_;
    std::vector<std::string> rogerIds_;
    std::vector<std::string> callIds_;
    int selectedPttVKey_ = 0;
    int selectedPttMods_ = 0;
};

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "O-Walkie Desktop", wxDefaultPosition, wxSize(680, 560)),
      relay_(std::make_unique<RelayClient>()),
      audio_(std::make_unique<AudioEngine>()),
      reconnectTimer_(this) {
    BuildUi();
    BindUi();

    reconnectTimer_.Bind(wxEVT_TIMER, &MainFrame::OnReconnectTimer, this);
#ifdef _WIN32
    InstallGlobalPttHook();
#endif

    relay_->SetStatusCallback([this](const std::string& msg) {
        this->CallAfter([this, msg] { SetStatus(wxString::FromUTF8(msg)); });
    });
    relay_->SetConnectedCallback([this](bool connected) {
        this->CallAfter([this, connected] {
            connected_ = connected;
            if (connected) {
                reconnectBackoffMs_ = 1500;
            }
            if (!connected && !relay_->AutoReconnectDesired()) {
                userWantsSession_ = false;
                StopReconnectTimer();
            }
            connectBtn_->SetLabel((connected || userWantsSession_) ? "Disconnect" : "Connect");
            pttBtn_->Enable(connected);
            callBtn_->Enable(connected);
            UpdateProfileControlsEnabled();
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
        this->CallAfter([this, msg] { SetStatus(wxString::FromUTF8(msg)); });
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
                showMicLevelIndicator_ = j.value("show_mic_level_indicator", false);
                rxVolumePercent_ = j.value("rx_volume_percent", 100);
            }
        }
    } catch (...) {
    }
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
    j["show_mic_level_indicator"] = showMicLevelIndicator_;
    j["rx_volume_percent"] = std::clamp(rxVolumePercent_, 0, 200);
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
        p.name = "Connection";
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
        p.name = "Connection " + std::to_string(static_cast<int>(profiles_.size()) + 1);
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
    if (reconnectTimer_.IsRunning()) {
        reconnectTimer_.Stop();
    }
}

void MainFrame::ScheduleReconnect() {
    if (!userWantsSession_ || !relay_->AutoReconnectDesired()) {
        return;
    }
    SetStatus(wxString::Format("Reconnecting in %d ms", reconnectBackoffMs_));
    reconnectTimer_.StartOnce(reconnectBackoffMs_);
}

void MainFrame::OnReconnectTimer(wxTimerEvent&) {
    if (!userWantsSession_ || !relay_->AutoReconnectDesired()) {
        return;
    }
    relay_->JoinWorkerThreads();
    if (relay_->IsConnected()) {
        reconnectBackoffMs_ = 1500;
        return;
    }
    if (TryConnectWithCurrentFields()) {
        reconnectBackoffMs_ = 1500;
        SaveAudioSettings();
        return;
    }
    reconnectBackoffMs_ = std::min(reconnectBackoffMs_ * 2, 30000);
    ScheduleReconnect();
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

void MainFrame::OnRelayConnectionLost() {
    audio_->StopTransmit();
    globalPttPressed_ = false;
    relay_->JoinWorkerThreads();
    connected_ = false;
    audio_->PlayConnectionErrorSignal();
    pttBtn_->Enable(false);
    callBtn_->Enable(false);
    connectBtn_->SetLabel(userWantsSession_ ? "Disconnect" : "Connect");
    UpdateProfileControlsEnabled();

    if (!userWantsSession_ || !relay_->AutoReconnectDesired()) {
        userWantsSession_ = false;
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
    profileLabel_ = new wxStaticText(panel, wxID_ANY, "Connections");
    SkipKeyboardFocus(profileLabel_);
    profileRow->Add(profileLabel_, 0, wxALIGN_CENTER_VERTICAL);
    profileChoice_ = new wxChoice(panel, wxID_ANY);
    profileChoice_->SetName("Connections");
    profileRow->Add(profileChoice_, 1, wxEXPAND);
    root->Add(profileRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    auto* profileBtns = new wxBoxSizer(wxHORIZONTAL);
    saveProfileBtn_ = new wxButton(panel, wxID_ANY, "Save");
    saveProfileBtn_->SetName("Save connection");
    newProfileBtn_ = new wxButton(panel, wxID_ANY, "New");
    newProfileBtn_->SetName("New connection");
    deleteProfileBtn_ = new wxButton(panel, wxID_ANY, "Delete");
    deleteProfileBtn_->SetName("Delete connection");
    profileBtns->Add(saveProfileBtn_, 0, wxRIGHT, 8);
    profileBtns->Add(newProfileBtn_, 0, wxRIGHT, 8);
    profileBtns->Add(deleteProfileBtn_, 0);
    root->Add(profileBtns, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* grid = new wxFlexGridSizer(2, 8, 10);
    grid->AddGrowableCol(1, 1);

    auto* labName = new wxStaticText(panel, wxID_ANY, "Connection name");
    SkipKeyboardFocus(labName);
    grid->Add(labName, 0, wxALIGN_CENTER_VERTICAL);
    connectionNameCtrl_ = new wxTextCtrl(panel, wxID_ANY, "Default");
    connectionNameCtrl_->SetName("Connection name");
    grid->Add(connectionNameCtrl_, 1, wxEXPAND);

    auto* labHost = new wxStaticText(panel, wxID_ANY, "Server host");
    SkipKeyboardFocus(labHost);
    grid->Add(labHost, 0, wxALIGN_CENTER_VERTICAL);
    hostCtrl_ = new wxTextCtrl(panel, wxID_ANY, "127.0.0.1");
    hostCtrl_->SetName("Host");
    grid->Add(hostCtrl_, 1, wxEXPAND);

    auto* labWs = new wxStaticText(panel, wxID_ANY, "WebSocket port");
    SkipKeyboardFocus(labWs);
    grid->Add(labWs, 0, wxALIGN_CENTER_VERTICAL);
    wsPortCtrl_ = new wxTextCtrl(panel, wxID_ANY, "5500");
    wsPortCtrl_->SetName("WebSocket port");
    grid->Add(wsPortCtrl_, 1, wxEXPAND);

    auto* labUdp = new wxStaticText(panel, wxID_ANY, "UDP port");
    SkipKeyboardFocus(labUdp);
    grid->Add(labUdp, 0, wxALIGN_CENTER_VERTICAL);
    udpPortCtrl_ = new wxTextCtrl(panel, wxID_ANY, "5505");
    udpPortCtrl_->SetName("UDP port");
    grid->Add(udpPortCtrl_, 1, wxEXPAND);

    auto* labCh = new wxStaticText(panel, wxID_ANY, "Channel");
    SkipKeyboardFocus(labCh);
    grid->Add(labCh, 0, wxALIGN_CENTER_VERTICAL);
    channelCtrl_ = new wxTextCtrl(panel, wxID_ANY, "global");
    channelCtrl_->SetName("Channel");
    grid->Add(channelCtrl_, 1, wxEXPAND);

    auto* labRx = new wxStaticText(panel, wxID_ANY, "Incoming volume");
    SkipKeyboardFocus(labRx);
    grid->Add(labRx, 0, wxALIGN_CENTER_VERTICAL);
    auto* rxRow = new wxBoxSizer(wxHORIZONTAL);
    rxVolumeSlider_ = new wxSlider(panel, wxID_ANY, rxVolumePercent_, 0, 200, wxDefaultPosition, wxSize(200, -1));
    rxVolumeSlider_->SetName("Incoming volume");
    rxVolumeValueText_ = new wxStaticText(panel, wxID_ANY, wxString::Format("%d%%", rxVolumePercent_));
    SkipKeyboardFocus(rxVolumeValueText_);
    rxRow->Add(rxVolumeSlider_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    rxRow->Add(rxVolumeValueText_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(rxRow, 1, wxEXPAND);

    root->Add(grid, 0, wxEXPAND | wxALL, 12);

    auto* audioBar = new wxBoxSizer(wxHORIZONTAL);
    settingsBtn_ = new wxButton(panel, wxID_ANY, "Settings");
    settingsBtn_->SetName("Open settings");
    audioBar->Add(settingsBtn_, 0, wxRIGHT, 10);
    root->Add(audioBar, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    repeaterCheck_ = new wxCheckBox(panel, wxID_ANY, "Repeater mode");
    repeaterCheck_->SetName("Repeater mode");
    root->Add(repeaterCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    connectBtn_ = new wxButton(panel, wxID_ANY, "Connect");
    connectBtn_->SetName("Connect or disconnect");
    pttBtn_ = new wxButton(panel, wxID_ANY, "Hold to Talk");
    pttBtn_->SetName("Push to talk");
    callBtn_ = new wxButton(panel, wxID_ANY, "Call");
    callBtn_->SetName("Call signal");
    pttBtn_->Enable(false);
    callBtn_->Enable(false);
    actions->Add(connectBtn_, 0, wxRIGHT, 10);
    actions->Add(pttBtn_, 0, wxRIGHT, 10);
    actions->Add(callBtn_, 0, wxRIGHT, 10);
    root->Add(actions, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    signalGauge_ = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(220, -1));
    signalGauge_->SetName("Transmit level");
    SkipKeyboardFocus(signalGauge_);

    root->Add(signalGauge_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    statusText_ = new wxStaticText(panel, wxID_ANY, "Status: Idle");
    statusText_->SetName("Connection status");
    SkipKeyboardFocus(statusText_);
    root->Add(statusText_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

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
    callBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnCallSignalClicked, this);
    rxVolumeSlider_->Bind(wxEVT_SLIDER, &MainFrame::OnRxVolumeSlider, this);
    repeaterCheck_->Bind(wxEVT_CHECKBOX, &MainFrame::OnRepeaterToggled, this);
}

void MainFrame::SetStatus(const wxString& status) {
    statusText_->SetLabel("Status: " + status);
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
        connected_ = false;
        connectBtn_->SetLabel("Connect");
        pttBtn_->Enable(false);
        callBtn_->Enable(false);
        UpdateProfileControlsEnabled();
        SetStatus("Disconnected");
        return;
    }

    SyncActiveProfileFromUi();
    userWantsSession_ = true;
    audio_->PlayManualConnectStartSignal();
    reconnectBackoffMs_ = 1500;
    UpdateProfileControlsEnabled();
    connectBtn_->SetLabel("Disconnect");

    if (TryConnectWithCurrentFields()) {
        SaveProfilesToDisk();
        SaveAudioSettings();
        SetStatus("Connected");
    } else {
        userWantsSession_ = false;
        connectBtn_->SetLabel("Connect");
        UpdateProfileControlsEnabled();
        audio_->PlayConnectionErrorSignal();
        SetStatus("Connect failed");
    }
}

void MainFrame::OnPttDown(wxMouseEvent& event) {
    BeginPttTx();
    event.Skip();
}

void MainFrame::OnPttUp(wxMouseEvent& event) {
    EndPttTx();
    event.Skip();
}

void MainFrame::OnPttButtonKeyDown(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_SPACE && connected_) {
        BeginPttTx();
        return;
    }
    event.Skip();
}

void MainFrame::OnPttButtonKeyUp(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_SPACE && connected_) {
        EndPttTx();
        return;
    }
    event.Skip();
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
        return;
    }
    audio_->PlayPttPressSignal();
    if (audio_->StartTransmit()) {
        SetStatus("Transmitting");
    }
}

void MainFrame::EndPttTx() {
    if (!connected_ || !audio_->IsTransmitting()) {
        return;
    }
    audio_->StopTransmit();
    audio_->ScheduleRxResumeHoldoff();
    audio_->StreamRogerSignal();
    relay_->SendTxEofBurst();
    SetStatus("Connected");
}


void MainFrame::OnTxStop() {
    audio_->StopTransmit();
    SetStatus("TX stopped by server");
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
    SettingsDialog dlg(
        this,
        inputDevices_,
        outputDevices_,
        selectedInputDeviceId_,
        selectedOutputDeviceId_,
        AudioEngine::RogerPatterns(),
        AudioEngine::CallPatterns(),
        selectedRogerPatternId_,
        selectedCallPatternId_,
        globalPttVKey_,
        globalPttMods_,
        showMicLevelIndicator_);
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
    UpdateMicLevelIndicatorVisibility();
    ApplyAudioSettingsToEngine();
    SaveAudioSettings();
    SetStatus("Settings applied");
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
        if (data && g_mainFrameForPttHook->globalPttVKey_ > 0 &&
            NormalizeHotkeyVKey(static_cast<int>(data->vkCode)) == g_mainFrameForPttHook->globalPttVKey_) {
            const int currentMods = CurrentModifierMask();
            const int requiredMods = g_mainFrameForPttHook->globalPttMods_;
            const bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            if (!isKeyUp && (currentMods & requiredMods) != requiredMods) {
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
