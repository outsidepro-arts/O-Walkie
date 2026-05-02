#include "LocaleInit.h"

#include <fstream>
#include <memory>

#include <nlohmann/json.hpp>

#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/stdpaths.h>

wxString OwReadSavedUiLanguage() {
    wxString dir = wxStandardPaths::Get().GetUserDataDir();
    if (!wxFileName::DirExists(dir)) {
        return "en";
    }
    const wxString path = wxFileName(dir, "audio.json").GetFullPath();
    if (!wxFileName::FileExists(path)) {
        return "en";
    }
    try {
        std::ifstream in(path.utf8_string());
        if (!in) {
            return "en";
        }
        nlohmann::json j;
        in >> j;
        const std::string lang = j.value("ui_language", std::string("en"));
        if (lang == "ru") {
            return "ru";
        }
        return "en";
    } catch (...) {
        return "en";
    }
}

void OwInitAppLocale(const wxString& langCode) {
    static std::unique_ptr<wxLocale> s_locale;

    const bool wantRu = (langCode == "ru");
    const int langId = wantRu ? wxLANGUAGE_RUSSIAN : wxLANGUAGE_ENGLISH_US;

    s_locale = std::make_unique<wxLocale>();
    if (!s_locale->Init(langId)) {
        s_locale->Init(wxLANGUAGE_ENGLISH_US);
    }

    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    const wxString locRoot = exe.GetPathWithSep() + "locale";
    wxLocale::AddCatalogLookupPathPrefix(locRoot);
    s_locale->AddCatalog(wxT("owalkie"));
}
