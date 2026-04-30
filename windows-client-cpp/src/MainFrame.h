#pragma once

#include <memory>
#include <string>
#include <vector>

#include <wx/frame.h>
#include <wx/timer.h>

#include "AudioEngine.h"
#include "RelayClient.h"

class wxButton;
class wxChoice;
class wxGauge;
class wxStaticText;
class wxTextCtrl;
class wxCheckBox;

struct ServerProfile {
    std::string name{"Default"};
    std::string host{"127.0.0.1"};
    int wsPort = 5500;
    int udpPort = 5505;
    std::string channel{"global"};
    bool repeater = false;
};

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void BuildUi();
    void BindUi();
    void SetStatus(const wxString& status);
    void OnConnectClicked(wxCommandEvent& event);
    void OnPttDown(wxMouseEvent& event);
    void OnPttUp(wxMouseEvent& event);
    void OnPttButtonKeyDown(wxKeyEvent& event);
    void OnPttButtonKeyUp(wxKeyEvent& event);
    void OnTxStop();
    void OnReconnectTimer(wxTimerEvent& event);
    void OnRelayConnectionLost();

    void LoadAllSettings();
    void SaveAudioSettings();
    void SaveProfilesToDisk();
    static wxString UserDataDir();
    static wxString ProfilesPath();
    static wxString AudioSettingsPath();
    static wxString LegacyConnectionPath();
    void MigrateLegacyConnectionJsonIfNeeded();

    void RepopulateProfileChoice();
    void OnProfileChoice(wxCommandEvent& event);
    void OnSaveProfile(wxCommandEvent& event);
    void OnNewProfile(wxCommandEvent& event);
    void OnDeleteProfile(wxCommandEvent& event);
    void SyncUiFromActiveProfile();
    void SyncActiveProfileFromUi();
    void UpdateProfileControlsEnabled();

    void PopulateAudioDeviceChoices();
    void SelectAudioDevicesInUi(int inputDeviceId, int outputDeviceId);
    void ApplySelectedAudioDevicesToEngine();
    void OnRefreshAudioDevices(wxCommandEvent& event);
    void OnAudioDeviceChanged(wxCommandEvent& event);
    int SelectedInputDeviceId() const;
    int SelectedOutputDeviceId() const;

    void ScheduleReconnect();
    void StopReconnectTimer();
    bool TryConnectWithCurrentFields();

private:
    wxStaticText* profileLabel_ = nullptr;
    wxChoice* profileChoice_ = nullptr;
    wxButton* saveProfileBtn_ = nullptr;
    wxButton* newProfileBtn_ = nullptr;
    wxButton* deleteProfileBtn_ = nullptr;

    wxTextCtrl* hostCtrl_ = nullptr;
    wxTextCtrl* wsPortCtrl_ = nullptr;
    wxTextCtrl* udpPortCtrl_ = nullptr;
    wxTextCtrl* channelCtrl_ = nullptr;
    wxChoice* inputDeviceChoice_ = nullptr;
    wxChoice* outputDeviceChoice_ = nullptr;
    wxButton* refreshAudioBtn_ = nullptr;
    wxCheckBox* repeaterCheck_ = nullptr;
    wxButton* connectBtn_ = nullptr;
    wxButton* pttBtn_ = nullptr;
    wxStaticText* statusText_ = nullptr;
    wxGauge* signalGauge_ = nullptr;

    std::unique_ptr<RelayClient> relay_;
    std::unique_ptr<AudioEngine> audio_;
    bool connected_ = false;
    bool userWantsSession_ = false;
    std::vector<int> inputDevIds_;
    std::vector<int> outputDevIds_;

    std::vector<ServerProfile> profiles_;
    int activeProfileIndex_ = 0;

    wxTimer reconnectTimer_;
    int reconnectBackoffMs_ = 1500;
};
