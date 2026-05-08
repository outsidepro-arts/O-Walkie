#pragma once

#include <wx/string.h>

/// Read `ui_language` from config/audio json ("en" or "ru"), preferring portable `<exe>/config`.
wxString OwReadSavedUiLanguage();

/// Init wxWidgets gettext catalog `owalkie` from `<exe>/locale`. Call once before UI.
void OwInitAppLocale(const wxString& langCode);
