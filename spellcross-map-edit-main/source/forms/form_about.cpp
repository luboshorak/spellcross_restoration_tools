#include "form_about.h"
#include "app_identity.h"
#include "app_version.h"

#include <wx/bitmap.h>
#include <wx/panel.h>
#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/accel.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/utils.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

#ifdef __WXMSW__
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace
{
    constexpr int ID_CHECK_UPDATES = wxID_HIGHEST + 401;
    constexpr int ID_OPEN_PROJECT = wxID_HIGHEST + 402;
    constexpr int ID_OPEN_RELEASE = wxID_HIGHEST + 403;

    struct VersionParts
    {
        std::vector<int> values;
        bool valid = false;
    };

    VersionParts ParseVersion(const std::string& text)
    {
        VersionParts result;
        size_t pos = 0;
        while (pos < text.size() && !std::isdigit(static_cast<unsigned char>(text[pos])))
            ++pos;
        if (pos == text.size())
            return result;

        while (pos < text.size())
        {
            if (!std::isdigit(static_cast<unsigned char>(text[pos])))
                break;

            int value = 0;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
            {
                value = value * 10 + (text[pos] - '0');
                ++pos;
            }
            result.values.push_back(value);

            if (pos >= text.size() || text[pos] != '.')
                break;
            ++pos;
        }

        result.valid = !result.values.empty();
        return result;
    }

    int CompareVersions(const std::string& left, const std::string& right, bool& comparable)
    {
        const VersionParts a = ParseVersion(left);
        const VersionParts b = ParseVersion(right);
        comparable = a.valid && b.valid;
        if (!comparable)
            return 0;

        const size_t count = std::max(a.values.size(), b.values.size());
        for (size_t i = 0; i < count; ++i)
        {
            const int av = i < a.values.size() ? a.values[i] : 0;
            const int bv = i < b.values.size() ? b.values[i] : 0;
            if (av < bv) return -1;
            if (av > bv) return 1;
        }
        return 0;
    }

#ifdef __WXMSW__
    class InternetHandle
    {
    public:
        InternetHandle() = default;
        explicit InternetHandle(HINTERNET handle) : m_handle(handle) {}
        ~InternetHandle() { if (m_handle) WinHttpCloseHandle(m_handle); }
        InternetHandle(const InternetHandle&) = delete;
        InternetHandle& operator=(const InternetHandle&) = delete;
        operator HINTERNET() const { return m_handle; }
        bool IsOk() const { return m_handle != nullptr; }
    private:
        HINTERNET m_handle = nullptr;
    };

    bool FetchLatestReleaseTag(std::string& tag, std::string& error)
    {
        // Use the release *list*, not /releases/latest: GitHub's "latest" endpoint
        // intentionally skips prereleases, while Spellcross Reloaded public builds
        // are often published as prereleases during development.
        InternetHandle session(WinHttpOpen(
            L"SpellcrossReloaded/" SPELLCROSS_VERSION_WSTRING,
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
        if (!session.IsOk())
        {
            error = "Could not initialize the Windows HTTP client.";
            return false;
        }

        WinHttpSetTimeouts(session, 4000, 4000, 5000, 5000);

        InternetHandle connection(WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection.IsOk())
        {
            error = "Could not connect to api.github.com.";
            return false;
        }

        InternetHandle request(WinHttpOpenRequest(
            connection,
            L"GET",
            L"/repos/luboshorak/spellcross_restoration_tools/releases?per_page=20",
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE));
        if (!request.IsOk())
        {
            error = "Could not create the GitHub update request.";
            return false;
        }

        const wchar_t* headers =
            L"Accept: application/vnd.github+json\r\n"
            L"X-GitHub-Api-Version: 2022-11-28\r\n"
            L"User-Agent: SpellcrossReloaded/" SPELLCROSS_VERSION_WSTRING L"\r\n";

        if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr))
        {
            error = "GitHub did not respond. Check your internet connection or proxy settings.";
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX))
        {
            error = "Could not read the GitHub response status.";
            return false;
        }

        if (status != 200)
        {
            error = "GitHub update check failed (HTTP " + std::to_string(status) + ").";
            return false;
        }

        std::string body;
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                error = "Could not read the GitHub response.";
                return false;
            }
            if (available == 0)
                break;

            const size_t oldSize = body.size();
            body.resize(oldSize + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, body.data() + oldSize, available, &read))
            {
                error = "Could not read the GitHub response.";
                return false;
            }
            body.resize(oldSize + read);
        }

        const std::regex tagRegex(R"REGEX("tag_name"\s*:\s*"([^"]+)")REGEX");
        std::sregex_iterator it(body.begin(), body.end(), tagRegex);
        const std::sregex_iterator end;
        if (it == end)
        {
            error = "No published GitHub release was found.";
            return false;
        }

        // GitHub returns releases in publication order, but select the highest
        // comparable numeric tag from the returned page so a later-published
        // hotfix/legacy tag cannot hide a genuinely newer build.
        std::string firstTag;
        std::string bestTag;
        for (; it != end; ++it)
        {
            const std::string candidate = (*it)[1].str();
            if (firstTag.empty())
                firstTag = candidate;

            bool candidateComparable = false;
            CompareVersions(candidate, SPELLCROSS_VERSION_STRING, candidateComparable);
            if (!candidateComparable)
                continue;

            if (bestTag.empty())
            {
                bestTag = candidate;
                continue;
            }

            bool tagsComparable = false;
            if (CompareVersions(bestTag, candidate, tagsComparable) < 0 && tagsComparable)
                bestTag = candidate;
        }

        tag = bestTag.empty() ? firstTag : bestTag;
        return !tag.empty();
    }
#endif

    wxStaticText* AddInfoRow(wxWindow* parent, wxFlexGridSizer* grid,
        const wxString& label, const wxString& value)
    {
        auto* key = new wxStaticText(parent, wxID_ANY, label);
        wxFont keyFont = key->GetFont();
        keyFont.SetWeight(wxFONTWEIGHT_BOLD);
        key->SetFont(keyFont);
        grid->Add(key, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, 10);

        auto* val = new wxStaticText(parent, wxID_ANY, value);
        grid->Add(val, 1, wxEXPAND | wxBOTTOM, 10);
        return val;
    }
}

FormAbout::FormAbout(wxWindow* parent, wxWindowID id, const wxString& title,
    const wxPoint& pos, const wxSize& size, long style)
    : wxDialog(parent, id, title, pos, size, style)
{
    spellcross_app::ApplyWindowIcon(this);
    SetMinSize(wxSize(650, 480));
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* root = new wxBoxSizer(wxVERTICAL);

    // Header -----------------------------------------------------------------
    auto* header = new wxPanel(this, wxID_ANY);
    header->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    auto* headerSizer = new wxBoxSizer(wxHORIZONTAL);

    wxIcon icon;
    icon.LoadFile("IDI_ICON2", wxBITMAP_TYPE_ICO_RESOURCE);
    if (icon.IsOk())
    {
        auto* logo = new wxStaticBitmap(header, wxID_ANY, wxBitmap(icon));
        headerSizer->Add(logo, 0, wxALIGN_CENTER_VERTICAL | wxALL, 18);
    }

    auto* titleSizer = new wxBoxSizer(wxVERTICAL);
    auto* appName = new wxStaticText(header, wxID_ANY, "Spellcross Reloaded");
    wxFont titleFont = appName->GetFont();
    titleFont.SetPointSize(20);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    appName->SetFont(titleFont);
    titleSizer->Add(appName, 0, wxBOTTOM, 3);

    auto* subtitle = new wxStaticText(header, wxID_ANY,
        "Fan-made restoration of Spellcross: The Last Battle");
    wxFont subtitleFont = subtitle->GetFont();
    subtitleFont.SetPointSize(10);
    subtitle->SetFont(subtitleFont);
    titleSizer->Add(subtitle, 0, wxBOTTOM, 8);

    const wxString buildLabel = "Version " + spellcross_app::VersionLabel() +
        "   |   build " + wxString::FromUTF8(__DATE__);
    auto* version = new wxStaticText(header, wxID_ANY, buildLabel);
    version->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    titleSizer->Add(version, 0);

    headerSizer->Add(titleSizer, 1, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM | wxRIGHT, 18);
    header->SetSizer(headerSizer);
    root->Add(header, 0, wxEXPAND);

    root->Add(new wxStaticLine(this), 0, wxEXPAND);

    // Description -------------------------------------------------------------
    auto* content = new wxPanel(this, wxID_ANY);
    content->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* contentSizer = new wxBoxSizer(wxVERTICAL);

    auto* description = new wxStaticText(content, wxID_ANY,
        "Spellcross Reloaded rebuilds the original campaign and tactical game on top of "
        "the Spellcross Map Editor codebase, while continuing to use the original game data. "
        "The project is experimental, community-driven and still under active development.");
    description->Wrap(600);
    contentSizer->Add(description, 0, wxEXPAND | wxALL, 16);

    // Project details ----------------------------------------------------------
    auto* projectBox = new wxStaticBoxSizer(wxVERTICAL, content, "Project");
    auto* grid = new wxFlexGridSizer(2, 8, 12);
    grid->AddGrowableCol(1, 1);
    AddInfoRow(content, grid, "Author", wxString::FromUTF8("Luboš Horák"));
    AddInfoRow(content, grid, "Version", spellcross_app::VersionLabel());
    AddInfoRow(content, grid, "Repository", "github.com/luboshorak/spellcross_restoration_tools");
    AddInfoRow(content, grid, "Based on", wxString::FromUTF8("Spellcross Map Editor by Stanislav Mašláň"));
    AddInfoRow(content, grid, "License", "MIT License");
    projectBox->Add(grid, 0, wxEXPAND | wxALL, 10);

    auto* projectButtons = new wxBoxSizer(wxHORIZONTAL);
    auto* openProject = new wxButton(content, ID_OPEN_PROJECT, "Open project on GitHub");
    projectButtons->Add(openProject, 0, wxRIGHT, 8);
    projectBox->Add(projectButtons, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
    contentSizer->Add(projectBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

    // Update section -----------------------------------------------------------
    auto* updateBox = new wxStaticBoxSizer(wxVERTICAL, content, "Updates");
    m_updateStatus = new wxStaticText(content, wxID_ANY,
        "Not checked. Use the button below to compare this build with GitHub releases.");
    m_updateStatus->Wrap(590);
    updateBox->Add(m_updateStatus, 0, wxEXPAND | wxALL, 10);

    auto* updateButtons = new wxBoxSizer(wxHORIZONTAL);
    m_checkUpdates = new wxButton(content, ID_CHECK_UPDATES, "Check for updates");
    m_openRelease = new wxButton(content, ID_OPEN_RELEASE, "Open GitHub releases");
    updateButtons->Add(m_checkUpdates, 0, wxRIGHT, 8);
    updateButtons->Add(m_openRelease, 0);
    updateBox->Add(updateButtons, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
    contentSizer->Add(updateBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

    content->SetSizer(contentSizer);
    root->Add(content, 1, wxEXPAND);

    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    auto* closeRow = new wxBoxSizer(wxHORIZONTAL);
    closeRow->AddStretchSpacer(1);
    auto* close = new wxButton(this, wxID_OK, "Close");
    close->SetDefault();
    closeRow->Add(close, 0);
    root->Add(closeRow, 0, wxEXPAND | wxALL, 12);

    SetSizerAndFit(root);
    if (GetSize().GetWidth() < 680)
        SetSize(wxSize(680, GetSize().GetHeight()));
    CentreOnParent();

    Bind(wxEVT_BUTTON, &FormAbout::OnClose, this, wxID_OK);
    Bind(wxEVT_BUTTON, &FormAbout::OnCheckUpdates, this, ID_CHECK_UPDATES);
    Bind(wxEVT_BUTTON, &FormAbout::OnOpenProject, this, ID_OPEN_PROJECT);
    Bind(wxEVT_BUTTON, &FormAbout::OnOpenRelease, this, ID_OPEN_RELEASE);

    std::vector<wxAcceleratorEntry> entries;
    entries.emplace_back(wxACCEL_NORMAL, WXK_ESCAPE, wxID_OK);
    SetAcceleratorTable(wxAcceleratorTable(entries.size(), entries.data()));
}

void FormAbout::OnClose(wxCommandEvent&)
{
    EndModal(wxID_OK);
}

void FormAbout::OnOpenProject(wxCommandEvent&)
{
    wxLaunchDefaultBrowser(wxString::FromUTF8(SPELLCROSS_GITHUB_URL));
}

void FormAbout::OnOpenRelease(wxCommandEvent&)
{
    const wxString url = m_latestReleaseUrl.empty()
        ? wxString::FromUTF8(SPELLCROSS_GITHUB_RELEASES_URL)
        : m_latestReleaseUrl;
    wxLaunchDefaultBrowser(url);
}

void FormAbout::OnCheckUpdates(wxCommandEvent&)
{
    m_checkUpdates->Disable();
    m_updateStatus->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    m_updateStatus->SetLabel("Checking GitHub releases...");
    m_updateStatus->GetParent()->Layout();
    wxYieldIfNeeded();

    wxBusyCursor busy;

#ifdef __WXMSW__
    std::string latestTag;
    std::string error;
    if (!FetchLatestReleaseTag(latestTag, error))
    {
        m_updateStatus->SetForegroundColour(wxColour(170, 45, 45));
        m_updateStatus->SetLabel("Update check failed: " + wxString::FromUTF8(error.c_str()));
        m_checkUpdates->Enable();
        m_updateStatus->GetParent()->Layout();
        return;
    }

    const std::string releaseUrl =
        std::string(SPELLCROSS_GITHUB_RELEASES_URL) + "/tag/" + latestTag;
    m_latestReleaseUrl = wxString::FromUTF8(releaseUrl.c_str());

    bool comparable = false;
    const int cmp = CompareVersions(SPELLCROSS_VERSION_STRING, latestTag, comparable);
    const wxString latest = wxString::FromUTF8(latestTag.c_str());

    if (!comparable)
    {
        m_updateStatus->SetForegroundColour(wxColour(100, 80, 20));
        m_updateStatus->SetLabel("Latest GitHub release: " + latest +
            ". The tag is not a standard numeric version, so it cannot be compared automatically.");
        m_openRelease->SetLabel("Open " + latest);
    }
    else if (cmp < 0)
    {
        m_updateStatus->SetForegroundColour(wxColour(20, 105, 55));
        m_updateStatus->SetLabel("A newer build is available: " + latest +
            " (installed: " + spellcross_app::VersionLabel() + ").");
        m_openRelease->SetLabel("Open " + latest + " on GitHub");
    }
    else if (cmp == 0)
    {
        m_updateStatus->SetForegroundColour(wxColour(20, 105, 55));
        m_updateStatus->SetLabel("You are up to date. Latest GitHub release: " + latest + ".");
        m_openRelease->SetLabel("Open " + latest + " on GitHub");
    }
    else
    {
        m_updateStatus->SetForegroundColour(wxColour(55, 85, 125));
        m_updateStatus->SetLabel("This build (" + spellcross_app::VersionLabel() +
            ") is newer than the latest public GitHub release (" + latest + ").");
        m_openRelease->SetLabel("Open latest public release");
    }
#else
    m_updateStatus->SetForegroundColour(wxColour(170, 45, 45));
    m_updateStatus->SetLabel("Automatic update checking is currently available in Windows builds.");
#endif

    m_checkUpdates->Enable();
    m_updateStatus->GetParent()->Layout();
}
