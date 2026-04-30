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
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

namespace {

void SkipKeyboardFocus(wxWindow* w) {
    if (w) {
        w->DisableFocusFromKeyboard();
    }
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "O-Walkie Desktop CPP (wxWidgets)", wxDefaultPosition, wxSize(680, 560)),
      relay_(std::make_unique<RelayClient>()),
      audio_(std::make_unique<AudioEngine>()),
      reconnectTimer_(this) {
    BuildUi();
    BindUi();

    reconnectTimer_.Bind(wxEVT_TIMER, &MainFrame::OnReconnectTimer, this);

    relay_->SetStatusCallback([this](const std::string& msg) {
        this->CallAfter([this, msg] { SetStatus(wxString::FromUTF8(msg)); });
    });
    relay_->SetConnectedCallback([this](bool connected) {
        this->CallAfter([this, connected] {
            connected_ = connected;
            if (!connected && !relay_->AutoReconnectDesired()) {
                userWantsSession_ = false;
                StopReconnectTimer();
            }
            connectBtn_->SetLabel((connected || userWantsSession_) ? "Disconnect" : "Connect");
            pttBtn_->Enable(connected);
            UpdateProfileControlsEnabled();
        });
    });
    relay_->SetWelcomeCallback([this](const WelcomeConfig& cfg) {
        audio_->Reconfigure(cfg);
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
        this->CallAfter([this, percent] { signalGauge_->SetValue(percent); });
    });
    audio_->Initialize();
    PopulateAudioDeviceChoices();
    MigrateLegacyConnectionJsonIfNeeded();
    LoadAllSettings();
    ApplySelectedAudioDevicesToEngine();
    UpdateProfileControlsEnabled();

    CallAfter([this] {
        if (hostCtrl_) {
            hostCtrl_->SetFocus();
        }
    });
}

MainFrame::~MainFrame() {
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
                const int inDev = j.value("audio_input_device", -1);
                const int outDev = j.value("audio_output_device", -1);
                SelectAudioDevicesInUi(inDev, outDev);
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
    j["audio_input_device"] = SelectedInputDeviceId();
    j["audio_output_device"] = SelectedOutputDeviceId();
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
    p.wsPort = static_cast<int>(ws);
    p.udpPort = static_cast<int>(udp);
    p.channel = channelCtrl_->GetValue().utf8_string();
    p.repeater = repeaterCheck_->GetValue();
}

void MainFrame::UpdateProfileControlsEnabled() {
    const bool idle = !connected_ && !userWantsSession_;
    profileChoice_->Enable(idle);
    saveProfileBtn_->Enable(idle);
    newProfileBtn_->Enable(idle);
    deleteProfileBtn_->Enable(idle && profiles_.size() > 1);
    hostCtrl_->Enable(idle);
    wsPortCtrl_->Enable(idle);
    udpPortCtrl_->Enable(idle);
    channelCtrl_->Enable(idle);
    repeaterCheck_->Enable(idle);
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
    wxTextEntryDialog dlg(this, "Profile name", "New server profile");
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }
    wxString name = dlg.GetValue().Trim();
    if (name.empty()) {
        return;
    }
    SyncActiveProfileFromUi();
    ServerProfile p;
    p.name = name.utf8_string();
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
    relay_->JoinWorkerThreads();
    connected_ = false;
    pttBtn_->Enable(false);
    connectBtn_->SetLabel(userWantsSession_ ? "Disconnect" : "Connect");
    UpdateProfileControlsEnabled();

    if (!userWantsSession_ || !relay_->AutoReconnectDesired()) {
        userWantsSession_ = false;
        SetStatus("Disconnected");
        return;
    }
    SetStatus("Connection lost");
    ScheduleReconnect();
}

void MainFrame::BuildUi() {
    auto* panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* profileRow = new wxFlexGridSizer(2, 8, 10);
    profileRow->AddGrowableCol(1, 1);
    profileLabel_ = new wxStaticText(panel, wxID_ANY, "Profile");
    SkipKeyboardFocus(profileLabel_);
    profileRow->Add(profileLabel_, 0, wxALIGN_CENTER_VERTICAL);
    profileChoice_ = new wxChoice(panel, wxID_ANY);
    profileChoice_->SetName("Server profile");
    profileRow->Add(profileChoice_, 1, wxEXPAND);
    root->Add(profileRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    auto* profileBtns = new wxBoxSizer(wxHORIZONTAL);
    saveProfileBtn_ = new wxButton(panel, wxID_ANY, "Save profile");
    saveProfileBtn_->SetName("Save profile");
    newProfileBtn_ = new wxButton(panel, wxID_ANY, "New profile");
    newProfileBtn_->SetName("New profile");
    deleteProfileBtn_ = new wxButton(panel, wxID_ANY, "Delete profile");
    deleteProfileBtn_->SetName("Delete profile");
    profileBtns->Add(saveProfileBtn_, 0, wxRIGHT, 8);
    profileBtns->Add(newProfileBtn_, 0, wxRIGHT, 8);
    profileBtns->Add(deleteProfileBtn_, 0);
    root->Add(profileBtns, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* grid = new wxFlexGridSizer(2, 8, 10);
    grid->AddGrowableCol(1, 1);

    auto* labHost = new wxStaticText(panel, wxID_ANY, "Host");
    SkipKeyboardFocus(labHost);
    grid->Add(labHost, 0, wxALIGN_CENTER_VERTICAL);
    hostCtrl_ = new wxTextCtrl(panel, wxID_ANY, "127.0.0.1");
    hostCtrl_->SetName("Host");
    grid->Add(hostCtrl_, 1, wxEXPAND);

    auto* labWs = new wxStaticText(panel, wxID_ANY, "WS port");
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

    auto* labIn = new wxStaticText(panel, wxID_ANY, "Microphone");
    SkipKeyboardFocus(labIn);
    grid->Add(labIn, 0, wxALIGN_CENTER_VERTICAL);
    inputDeviceChoice_ = new wxChoice(panel, wxID_ANY);
    inputDeviceChoice_->SetName("Microphone device");
    grid->Add(inputDeviceChoice_, 1, wxEXPAND);

    auto* labOut = new wxStaticText(panel, wxID_ANY, "Speaker");
    SkipKeyboardFocus(labOut);
    grid->Add(labOut, 0, wxALIGN_CENTER_VERTICAL);
    outputDeviceChoice_ = new wxChoice(panel, wxID_ANY);
    outputDeviceChoice_->SetName("Speaker device");
    grid->Add(outputDeviceChoice_, 1, wxEXPAND);

    root->Add(grid, 0, wxEXPAND | wxALL, 12);

    auto* audioBar = new wxBoxSizer(wxHORIZONTAL);
    refreshAudioBtn_ = new wxButton(panel, wxID_ANY, "Refresh audio devices");
    refreshAudioBtn_->SetName("Refresh audio devices");
    audioBar->Add(refreshAudioBtn_, 0, wxRIGHT, 10);
    root->Add(audioBar, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    repeaterCheck_ = new wxCheckBox(panel, wxID_ANY, "Repeater mode");
    repeaterCheck_->SetName("Repeater mode");
    root->Add(repeaterCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    connectBtn_ = new wxButton(panel, wxID_ANY, "Connect");
    connectBtn_->SetName("Connect or disconnect");
    pttBtn_ = new wxButton(panel, wxID_ANY, "Hold to Talk");
    pttBtn_->SetName("Push to talk");
    pttBtn_->Enable(false);
    actions->Add(connectBtn_, 0, wxRIGHT, 10);
    actions->Add(pttBtn_, 0, wxRIGHT, 10);
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
    hostCtrl_->MoveAfterInTabOrder(deleteProfileBtn_);
    wsPortCtrl_->MoveAfterInTabOrder(hostCtrl_);
    udpPortCtrl_->MoveAfterInTabOrder(wsPortCtrl_);
    channelCtrl_->MoveAfterInTabOrder(udpPortCtrl_);
    inputDeviceChoice_->MoveAfterInTabOrder(channelCtrl_);
    outputDeviceChoice_->MoveAfterInTabOrder(inputDeviceChoice_);
    refreshAudioBtn_->MoveAfterInTabOrder(outputDeviceChoice_);
    repeaterCheck_->MoveAfterInTabOrder(refreshAudioBtn_);
    connectBtn_->MoveAfterInTabOrder(repeaterCheck_);
    pttBtn_->MoveAfterInTabOrder(connectBtn_);
}

void MainFrame::BindUi() {
    profileChoice_->Bind(wxEVT_CHOICE, &MainFrame::OnProfileChoice, this);
    saveProfileBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnSaveProfile, this);
    newProfileBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnNewProfile, this);
    deleteProfileBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnDeleteProfile, this);
    connectBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnConnectClicked, this);
    refreshAudioBtn_->Bind(wxEVT_BUTTON, &MainFrame::OnRefreshAudioDevices, this);
    inputDeviceChoice_->Bind(wxEVT_CHOICE, &MainFrame::OnAudioDeviceChanged, this);
    outputDeviceChoice_->Bind(wxEVT_CHOICE, &MainFrame::OnAudioDeviceChanged, this);
    pttBtn_->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnPttDown, this);
    pttBtn_->Bind(wxEVT_LEFT_UP, &MainFrame::OnPttUp, this);
    pttBtn_->Bind(wxEVT_LEAVE_WINDOW, &MainFrame::OnPttUp, this);
    pttBtn_->Bind(wxEVT_KEY_DOWN, &MainFrame::OnPttButtonKeyDown, this);
    pttBtn_->Bind(wxEVT_KEY_UP, &MainFrame::OnPttButtonKeyUp, this);
}

void MainFrame::SetStatus(const wxString& status) {
    statusText_->SetLabel("Status: " + status);
}

void MainFrame::OnConnectClicked(wxCommandEvent&) {
    StopReconnectTimer();

    if (connected_ || userWantsSession_) {
        userWantsSession_ = false;
        audio_->StopTransmit();
        relay_->Disconnect();
        connected_ = false;
        connectBtn_->SetLabel("Connect");
        pttBtn_->Enable(false);
        UpdateProfileControlsEnabled();
        SetStatus("Disconnected");
        return;
    }

    SyncActiveProfileFromUi();
    userWantsSession_ = true;
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
        SetStatus("Connect failed");
    }
}

void MainFrame::OnPttDown(wxMouseEvent& event) {
    if (connected_) {
        audio_->StartTransmit();
        SetStatus("Transmitting");
    }
    event.Skip();
}

void MainFrame::OnPttUp(wxMouseEvent& event) {
    if (connected_) {
        audio_->StopTransmit();
        relay_->SendTxEofBurst();
        SetStatus("Connected");
    }
    event.Skip();
}

void MainFrame::OnPttButtonKeyDown(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_SPACE && connected_) {
        if (!audio_->IsTransmitting()) {
            audio_->StartTransmit();
            SetStatus("Transmitting");
        }
        return;
    }
    event.Skip();
}

void MainFrame::OnPttButtonKeyUp(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_SPACE && connected_) {
        if (audio_->IsTransmitting()) {
            audio_->StopTransmit();
            relay_->SendTxEofBurst();
            SetStatus("Connected");
        }
        return;
    }
    event.Skip();
}

void MainFrame::OnTxStop() {
    audio_->StopTransmit();
    SetStatus("TX stopped by server");
}

void MainFrame::PopulateAudioDeviceChoices() {
    inputDeviceChoice_->Clear();
    inputDevIds_.clear();
    inputDeviceChoice_->Append("System default");
    inputDevIds_.push_back(-1);
    for (const auto& d : AudioEngine::ListInputDevices()) {
        inputDeviceChoice_->Append(wxString::FromUTF8(d.name));
        inputDevIds_.push_back(d.index);
    }
    inputDeviceChoice_->SetSelection(0);

    outputDeviceChoice_->Clear();
    outputDevIds_.clear();
    outputDeviceChoice_->Append("System default");
    outputDevIds_.push_back(-1);
    for (const auto& d : AudioEngine::ListOutputDevices()) {
        outputDeviceChoice_->Append(wxString::FromUTF8(d.name));
        outputDevIds_.push_back(d.index);
    }
    outputDeviceChoice_->SetSelection(0);
}

void MainFrame::SelectAudioDevicesInUi(int inputDeviceId, int outputDeviceId) {
    int inSel = 0;
    for (size_t i = 0; i < inputDevIds_.size(); ++i) {
        if (inputDevIds_[i] == inputDeviceId) {
            inSel = static_cast<int>(i);
            break;
        }
    }
    inputDeviceChoice_->SetSelection(inSel);

    int outSel = 0;
    for (size_t i = 0; i < outputDevIds_.size(); ++i) {
        if (outputDevIds_[i] == outputDeviceId) {
            outSel = static_cast<int>(i);
            break;
        }
    }
    outputDeviceChoice_->SetSelection(outSel);
}

void MainFrame::ApplySelectedAudioDevicesToEngine() {
    audio_->SetPreferredInputDevice(SelectedInputDeviceId());
    audio_->SetPreferredOutputDevice(SelectedOutputDeviceId());
}

void MainFrame::OnRefreshAudioDevices(wxCommandEvent&) {
    const int inId = SelectedInputDeviceId();
    const int outId = SelectedOutputDeviceId();
    PopulateAudioDeviceChoices();
    SelectAudioDevicesInUi(inId, outId);
    ApplySelectedAudioDevicesToEngine();
    SetStatus("Audio device list refreshed");
}

void MainFrame::OnAudioDeviceChanged(wxCommandEvent&) {
    ApplySelectedAudioDevicesToEngine();
    SaveAudioSettings();
}

int MainFrame::SelectedInputDeviceId() const {
    const int i = inputDeviceChoice_->GetSelection();
    if (i == wxNOT_FOUND || i < 0 || static_cast<size_t>(i) >= inputDevIds_.size()) {
        return -1;
    }
    return inputDevIds_[static_cast<size_t>(i)];
}

int MainFrame::SelectedOutputDeviceId() const {
    const int i = outputDeviceChoice_->GetSelection();
    if (i == wxNOT_FOUND || i < 0 || static_cast<size_t>(i) >= outputDevIds_.size()) {
        return -1;
    }
    return outputDevIds_[static_cast<size_t>(i)];
}
