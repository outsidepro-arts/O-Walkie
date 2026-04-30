#include <wx/app.h>

#include "MainFrame.h"

class OWalkieApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("OWalkieDesktop");
        SetVendorName("OutsideProArts");
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(OWalkieApp);
