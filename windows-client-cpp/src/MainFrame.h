#pragma once

#include <memory>

#include <wx/frame.h>

#include "AudioEngine.h"
#include "RelayClient.h"

class wxButton;
class wxGauge;
class wxStaticText;
class wxTextCtrl;
class wxToggleButton;
class wxCheckBox;

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
    void OnTxStop();
    void LoadConnectionSettings();
    void SaveConnectionSettings();
    static wxString ConnectionConfigPath();

private:
    wxTextCtrl* hostCtrl_ = nullptr;
    wxTextCtrl* wsPortCtrl_ = nullptr;
    wxTextCtrl* udpPortCtrl_ = nullptr;
    wxTextCtrl* channelCtrl_ = nullptr;
    wxCheckBox* repeaterCheck_ = nullptr;
    wxButton* connectBtn_ = nullptr;
    wxButton* pttBtn_ = nullptr;
    wxStaticText* statusText_ = nullptr;
    wxGauge* signalGauge_ = nullptr;

    std::unique_ptr<RelayClient> relay_;
    std::unique_ptr<AudioEngine> audio_;
    bool connected_ = false;
};
