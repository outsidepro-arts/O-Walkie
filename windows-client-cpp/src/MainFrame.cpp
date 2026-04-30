#include "MainFrame.h"

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

namespace {

void SkipKeyboardFocus(wxWindow* w) {
    if (w) {
        w->DisableFocusFromKeyboard();
    }
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "O-Walkie Desktop CPP (wxWidgets)", wxDefaultPosition, wxSize(620, 520)),
      relay_(std::make_unique<RelayClient>()),
      audio_(std::make_unique<AudioEngine>()) {
    BuildUi();
    BindUi();

    relay_->SetStatusCallback([this](const std::string& msg) {
        this->CallAfter([this, msg] { SetStatus(msg); });
    });
    relay_->SetConnectedCallback([this](bool connected) {
        this->CallAfter([this, connected] {
            connected_ = connected;
            connectBtn_->SetLabel(connected ? "Disconnect" : "Connect");
            pttBtn_->Enable(connected);
        });
    });
    relay_->SetWelcomeCallback([this](const WelcomeConfig& cfg) {
        audio_->Reconfigure(cfg);
    });
    relay_->SetOpusFrameCallback([this](const std::vector<uint8_t>& opus) {
        audio_->OnIncomingOpusFrame(opus);
    });
    relay_->SetTxStopCallback([this] { this->CallAfter([this] { OnTxStop(); }); });

    audio_->SetEncodedFrameCallback([this](const uint8_t* data, size_t size, uint8_t signal) {
        relay_->SendOpusFrame(data, size, signal);
    });
    audio_->SetStatusCallback([this](const std::string& msg) {
        this->CallAfter([this, msg] { SetStatus(msg); });
    });
    audio_->SetLevelCallback([this](int percent) {
        this->CallAfter([this, percent] { signalGauge_->SetValue(percent); });
    });
    audio_->Initialize();
    PopulateAudioDeviceChoices();
    LoadConnectionSettings();
    ApplySelectedAudioDevicesToEngine();

    // Focus after first layout/show pass (wxWidgets tab traversal needs valid hierarchy).
    CallAfter([this] {
        if (hostCtrl_) {
            hostCtrl_->SetFocus();
        }
    });
}

MainFrame::~MainFrame() {
    SaveConnectionSettings();
    audio_->Shutdown();
    relay_->Disconnect();
}

void MainFrame::BuildUi() {
    // Put focusable controls on a wxPanel: wxFrame alone often breaks predictable Tab traversal
    // (see wxWidgets accessibility / forum guidance). wxTAB_TRAVERSAL keeps a single tab chain.
    auto* panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    auto* root = new wxBoxSizer(wxVERTICAL);
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

    // Explicit order for AT / predictable Shift+Tab (wx docs: MoveAfterInTabOrder).
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
    if (connected_) {
        audio_->StopTransmit();
        relay_->Disconnect();
        connected_ = false;
        connectBtn_->SetLabel("Connect");
        pttBtn_->Enable(false);
        SetStatus("Disconnected");
        return;
    }

    long wsPort = 0;
    long udpPort = 0;
    if (!wsPortCtrl_->GetValue().ToLong(&wsPort) || !udpPortCtrl_->GetValue().ToLong(&udpPort)) {
        SetStatus("Invalid ports");
        return;
    }

    auto ok = relay_->Connect(
        hostCtrl_->GetValue().ToStdString(),
        static_cast<int>(wsPort),
        static_cast<int>(udpPort),
        channelCtrl_->GetValue().ToStdString(),
        repeaterCheck_->GetValue()
    );
    if (!ok) {
        SetStatus("Connect failed");
    } else {
        SaveConnectionSettings();
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

wxString MainFrame::ConnectionConfigPath() {
    wxString dir = wxStandardPaths::Get().GetUserDataDir();
    if (!wxFileName::DirExists(dir)) {
        wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
    return wxFileName(dir, wxString("connection.json")).GetFullPath();
}

void MainFrame::LoadConnectionSettings() {
    const wxString path = ConnectionConfigPath();
    if (!wxFileName::FileExists(path)) {
        return;
    }
    try {
        const std::string pathUtf8 = path.utf8_string();
        std::ifstream in(pathUtf8);
        if (!in) {
            return;
        }
        nlohmann::json j;
        in >> j;
        if (j.contains("host") && j["host"].is_string()) {
            hostCtrl_->SetValue(wxString::FromUTF8(j["host"].get<std::string>()));
        }
        if (j.contains("ws_port")) {
            wsPortCtrl_->SetValue(wxString::Format("%d", j["ws_port"].get<int>()));
        }
        if (j.contains("udp_port")) {
            udpPortCtrl_->SetValue(wxString::Format("%d", j["udp_port"].get<int>()));
        }
        if (j.contains("channel") && j["channel"].is_string()) {
            channelCtrl_->SetValue(wxString::FromUTF8(j["channel"].get<std::string>()));
        }
        if (j.contains("repeater") && j["repeater"].is_boolean()) {
            repeaterCheck_->SetValue(j["repeater"].get<bool>());
        }
        const int inDev = j.value("audio_input_device", -1);
        const int outDev = j.value("audio_output_device", -1);
        SelectAudioDevicesInUi(inDev, outDev);
    } catch (...) {
    }
}

void MainFrame::SaveConnectionSettings() {
    long wsPort = 0;
    long udpPort = 0;
    wsPortCtrl_->GetValue().ToLong(&wsPort);
    udpPortCtrl_->GetValue().ToLong(&udpPort);
    nlohmann::json j;
    j["host"] = std::string(hostCtrl_->GetValue().utf8_string());
    j["ws_port"] = static_cast<int>(wsPort);
    j["udp_port"] = static_cast<int>(udpPort);
    j["channel"] = std::string(channelCtrl_->GetValue().utf8_string());
    j["repeater"] = repeaterCheck_->GetValue();
    j["audio_input_device"] = SelectedInputDeviceId();
    j["audio_output_device"] = SelectedOutputDeviceId();
    const std::string outPath = ConnectionConfigPath().utf8_string();
    std::ofstream out(outPath);
    if (!out) {
        return;
    }
    out << j.dump(2);
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
