#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <wx/frame.h>
#include <wx/timer.h>

#include "AudioEngine.h"
#include "RelayClient.h"

#ifdef _WIN32
#include <windows.h>
#endif

class wxButton;
class wxChoice;
class wxGauge;
class wxStaticText;
class wxTextCtrl;
class wxCheckBox;
class wxSlider;
class wxDialog;

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
    wxString HumanizeStatus(const wxString& status) const;
    void OnConnectClicked(wxCommandEvent& event);
    void OnPttDown(wxMouseEvent& event);
    void OnPttUp(wxMouseEvent& event);
    void OnPttButtonKeyDown(wxKeyEvent& event);
    void OnPttButtonKeyUp(wxKeyEvent& event);
    void OnCallSignalClicked(wxCommandEvent& event);
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
    void ApplyAudioSettingsToEngine();
    void OnSettingsClicked(wxCommandEvent& event);
    void UpdateMicLevelIndicatorVisibility();
    void SyncRxVolumeUi();
    void OnRxVolumeSlider(wxCommandEvent& event);
    void OnRepeaterToggled(wxCommandEvent& event);

    void ScheduleReconnect();
    void StopReconnectTimer();
    bool TryConnectWithCurrentFields();
    void StartReconnectAttemptAsync();
    void BeginPttTx();
    void EndPttTx();
    void RecordPttReleaseBurst();
    void ExtendPttReleaseBurstDecayTimer();
    void ResetPttReleaseBurstGuard();
    void SyncPttButtonForBurstGuard();

#ifdef _WIN32
    void InstallGlobalPttHook();
    void UninstallGlobalPttHook();
    static LRESULT CALLBACK GlobalPttKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
#endif

private:
    wxStaticText* profileLabel_ = nullptr;
    wxChoice* profileChoice_ = nullptr;
    wxButton* saveProfileBtn_ = nullptr;
    wxButton* newProfileBtn_ = nullptr;
    wxButton* deleteProfileBtn_ = nullptr;

    wxTextCtrl* hostCtrl_ = nullptr;
    wxTextCtrl* connectionNameCtrl_ = nullptr;
    wxTextCtrl* wsPortCtrl_ = nullptr;
    wxTextCtrl* udpPortCtrl_ = nullptr;
    wxTextCtrl* channelCtrl_ = nullptr;
    wxButton* settingsBtn_ = nullptr;
    wxCheckBox* repeaterCheck_ = nullptr;
    wxButton* connectBtn_ = nullptr;
    wxButton* pttBtn_ = nullptr;
    wxButton* callBtn_ = nullptr;
    wxGauge* signalGauge_ = nullptr;
    wxSlider* rxVolumeSlider_ = nullptr;
    wxStaticText* rxVolumeValueText_ = nullptr;

    std::unique_ptr<RelayClient> relay_;
    std::unique_ptr<AudioEngine> audio_;
    bool connected_ = false;
    bool userWantsSession_ = false;
    std::vector<NamedAudioDevice> inputDevices_;
    std::vector<NamedAudioDevice> outputDevices_;
    int selectedInputDeviceId_ = -1;
    int selectedOutputDeviceId_ = -1;
    std::string selectedRogerPatternId_ = "variant_1";
    std::string selectedCallPatternId_ = "call_variant_1";
    int globalPttVKey_ = 0;
    int globalPttMods_ = 0;
    bool showMicLevelIndicator_ = false;
    int rxVolumePercent_ = 100;
    bool globalPttPressed_ = false;
#ifdef _WIN32
    void* globalPttHook_ = nullptr;
#endif

    std::vector<ServerProfile> profiles_;
    int activeProfileIndex_ = 0;

    wxTimer reconnectTimer_;
    int reconnectBackoffMs_ = 1500;
    std::atomic<bool> reconnectAttemptInFlight_{false};
    int reconnectAttemptSeq_ = 0;
    std::atomic<uint64_t> reconnectScheduleTicket_{0};

    std::atomic<uint64_t> pttReleaseBurstTimerTicket_{0};
    std::atomic<int> pttReleaseBurstCount_{0};
    std::atomic<bool> pttReleaseBurstBlocked_{false};

    void ArmPttReleaseBurstDecay(uint64_t scheduleTicket);
};
