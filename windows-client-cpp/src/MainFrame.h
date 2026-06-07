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
class wxScrollEvent;

struct ServerProfile {
    std::string name{"Default"};
    std::string host{"127.0.0.1"};
    int port = 5500;
    std::string channel{"global"};
    bool repeater = false;
};

class MainFrame : public wxFrame {
public:
    explicit MainFrame(const std::string& connectUri = "");
    ~MainFrame() override;

    AudioEngine* AudioEnginePtr() { return audio_.get(); }
    std::string SelectedRogerPatternId() const { return selectedRogerPatternId_; }
    std::string SelectedCallPatternId() const { return selectedCallPatternId_; }
    std::vector<SignalPattern> MergedRogerPatternsForUi() const;
    std::vector<SignalPattern> MergedCallPatternsForUi() const;
    void UpsertCustomRogerPattern(SignalPattern pattern);
    void UpsertCustomCallPattern(SignalPattern pattern);
    bool DeleteCustomRogerPattern(const std::string& id);
    bool DeleteCustomCallPattern(const std::string& id);
    static wxString CustomSignalPatternsPath();
    static bool IsBuiltInRogerPatternId(const std::string& id);
    static bool IsBuiltInCallPatternId(const std::string& id);

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
    void LoadAllSettings();
    void LoadCustomSignalPatternsFromDisk();
    void SaveCustomSignalPatternsToDisk() const;
    void ApplyCustomPatternsToEngine();
    void SaveAudioSettings();
    void SaveProfilesToDisk();
    static wxString PortableConfigDir();
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
    void OnShareConnectionLink(wxCommandEvent& event);
    void OnImportConnectionLink(wxCommandEvent& event);

    bool TryConnectWithCurrentFields();
    void BeginPttTx();
    void EndPttTx();
    void StartTxCountdownFromServer();
    void StopTxCountdownFromServer();
    void StartBusyTimeoutCountdown();
    void StopBusyTimeoutCountdown();
    void OnServerPttUnlockFromRelay();
    void OnServerPttLockFromRelay(int displaySec);
    void OnRxBroadcastStartFromRelay(bool busyMode);
    void OnRxBroadcastEndFromRelay();
    void ForceAbortOutgoingForServerPttLock();
    void TogglePttTx();
    void RefreshPttUi();
    /** Transport ready for PTT/RX (authoritative; UI connected_ may lag CallAfter). */
    bool IsRelaySessionReady() const;
    void OnPttButtonClicked(wxCommandEvent& event);
    void RecordPttReleaseBurst();
    void ExtendPttReleaseBurstDecayTimer();
    void ResetPttReleaseBurstGuard();
    void SyncPttButtonForBurstGuard();
    bool ApplyConnectUri(const std::string& uri);

#ifdef _WIN32
    void EnsureUserProtocolRegistration();
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
    wxTextCtrl* portCtrl_ = nullptr;
    wxTextCtrl* channelCtrl_ = nullptr;
    wxButton* settingsBtn_ = nullptr;
    wxButton* shareConnectionBtn_ = nullptr;
    wxButton* importConnectionBtn_ = nullptr;
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
    std::vector<SignalPattern> customRogerPatterns_;
    std::vector<SignalPattern> customCallPatterns_;
    int globalPttVKey_ = 0;
    int globalPttMods_ = 0;
    bool pttToggleMode_ = false;
    bool showMicLevelIndicator_ = false;
    int rxVolumePercent_ = 100;
    double vibrationImitationHz_ = 100.0;
    int vibrationImitationVolumePercent_ = 40;
    std::string uiLanguageCode_{"en"};
    bool protocolRegistrationHandled_ = false;
    bool globalPttPressed_ = false;
    bool globalPttToggleHookDown_ = false;
    bool suppressHoldPttUntilRelease_ = false;
#ifdef _WIN32
    void* globalPttHook_ = nullptr;
#endif

    std::vector<ServerProfile> profiles_;
    int activeProfileIndex_ = 0;


    std::atomic<uint64_t> pttReleaseBurstTimerTicket_{0};
    std::atomic<int> pttReleaseBurstCount_{0};
    std::atomic<bool> pttReleaseBurstBlocked_{false};
    std::atomic<uint64_t> txCountdownTicket_{0};
    bool busyModeEnabled_ = false;
    bool serverRxBroadcastActive_ = false;
    bool serverPttLocked_ = false;
    int pttLockDisplaySec_ = 0;
    std::atomic<uint64_t> busyCountdownTicket_{0};

    void ArmPttReleaseBurstDecay(uint64_t scheduleTicket);
};
