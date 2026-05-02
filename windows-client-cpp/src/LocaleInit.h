#pragma once

#include <wx/string.h>

/// Read `ui_language` from audio.json in user data dir ("en" or "ru"). Default "en".
wxString OwReadSavedUiLanguage();

/// Init wxWidgets gettext catalog `owalkie` from `<exe>/locale`. Call once before UI.
void OwInitAppLocale(const wxString& langCode);
