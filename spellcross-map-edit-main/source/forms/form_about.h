#pragma once

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/string.h>

class FormAbout : public wxDialog
{
public:
    FormAbout(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxString& title = wxT("About Spellcross Reloaded"),
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    ~FormAbout() override = default;

private:
    wxStaticText* m_updateStatus = nullptr;
    wxButton* m_checkUpdates = nullptr;
    wxButton* m_openRelease = nullptr;
    wxString m_latestReleaseUrl;

    void OnClose(wxCommandEvent& event);
    void OnCheckUpdates(wxCommandEvent& event);
    void OnOpenProject(wxCommandEvent& event);
    void OnOpenRelease(wxCommandEvent& event);
};
