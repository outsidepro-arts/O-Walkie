#include <wx/app.h>

#include "LocaleInit.h"
#include "MainFrame.h"

class OWalkieApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("OWalkieDesktop");
        SetVendorName("OutsideProArts");
        OwInitAppLocale(OwReadSavedUiLanguage());
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(OWalkieApp);
