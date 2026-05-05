#include <wx/app.h>

#include <optional>
#include <string>

#include "LocaleInit.h"
#include "MainFrame.h"

class OWalkieApp final : public wxApp {
public:
    static std::optional<std::string> ParseConnectUri(int argc, wxChar** argv) {
        std::optional<std::string> parsed;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = wxString(argv[i]).utf8_string();
            if (arg == "--connect-uri") {
                if ((i + 1) < argc) {
                    parsed = wxString(argv[i + 1]).utf8_string();
                }
                break;
            }
            if (arg.rfind("owalkie://", 0) == 0) {
                parsed = arg;
                break;
            }
        }
        return parsed;
    }

    bool OnInit() override {
        SetAppName("OWalkieDesktop");
        SetVendorName("OutsideProArts");
        OwInitAppLocale(OwReadSavedUiLanguage());
        const auto connectUri = ParseConnectUri(argc, argv);
        auto* frame = new MainFrame(connectUri.value_or(std::string()));
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(OWalkieApp);
