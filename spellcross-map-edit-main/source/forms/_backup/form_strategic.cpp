#include "form_strategic.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <array>

static wxFont MakeUiFont(int px, bool bold = false)
{
    wxFont f(wxFontInfo(px).Family(wxFONTFAMILY_TELETYPE));
    f.SetFaceName("Terminal");
    if(bold)
        f.SetWeight(wxFONTWEIGHT_BOLD);
    return f;
}

static wxStaticText* MakeCell(wxWindow* parent, const wxString& text, int align = wxALIGN_LEFT)
{
    auto* t = new wxStaticText(parent, wxID_ANY, text);
    t->SetForegroundColour(wxColour(210, 255, 210));
    t->SetFont(MakeUiFont(11, false));
    if(align == wxALIGN_RIGHT)
        t->SetWindowStyleFlag(t->GetWindowStyleFlag() | wxALIGN_RIGHT);
    return t;
}

static void ApplyHeaderStyle(wxStaticText* t)
{
    if(!t) return;
    t->SetForegroundColour(wxColour(255, 230, 80));
    t->SetFont(MakeUiFont(11, true));
}

static void ApplySectionTitleStyle(wxStaticText* t)
{
    if(!t) return;
    t->SetForegroundColour(wxColour(255, 230, 80));
    t->SetFont(MakeUiFont(13, true));
}

StrategicInfoFrame::StrategicInfoFrame(wxWindow* parent, const LevelData& level)
    : wxFrame(parent, wxID_ANY, "Statistiky", wxDefaultPosition, wxSize(760, 620),
        wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
    m_level(level)
{
    SetBackgroundColour(wxColour(10, 40, 10));

    LoadRanksTable();
    LoadPlayerFromStrategicStateIfPresent();
    LoadMissionStatsIfPresent();
    RecomputePlayerRank();

    BuildUI();
    RefreshUI();
}

void StrategicInfoFrame::BuildUI()
{
    auto* root = new wxPanel(this);
    root->SetBackgroundColour(wxColour(10, 40, 10));

    auto* main = new wxBoxSizer(wxVERTICAL);

    // ---- Statistika cel� hry ----
    {
        auto* title = new wxStaticText(root, wxID_ANY, "Statistika cel� hry");
        ApplySectionTitleStyle(title);
        main->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 14);

        auto* box = new wxPanel(root);
        box->SetBackgroundColour(wxColour(15, 55, 15));
        box->SetForegroundColour(*wxWHITE);

        auto* grid = new wxFlexGridSizer(5, 3, 6, 10);
        grid->AddGrowableCol(0, 1);
        grid->AddGrowableCol(1, 0);
        grid->AddGrowableCol(2, 0);

        auto* h0 = MakeCell(box, "");
        auto* h1 = MakeCell(box, "Aliance - ztr�ty", wxALIGN_RIGHT);
        auto* h2 = MakeCell(box, "Other Side - ztr�ty", wxALIGN_RIGHT);
        ApplyHeaderStyle(h1);
        ApplyHeaderStyle(h2);

        grid->Add(h0, 0, wxEXPAND);
        grid->Add(h1, 0, wxEXPAND);
        grid->Add(h2, 0, wxEXPAND);

        // rows
        grid->Add(MakeCell(box, "Lehk� j."), 0, wxEXPAND);
        m_lblAllLightA = MakeCell(box, "0", wxALIGN_RIGHT);
        m_lblAllLightE = MakeCell(box, "0", wxALIGN_RIGHT);
        grid->Add(m_lblAllLightA, 0, wxEXPAND);
        grid->Add(m_lblAllLightE, 0, wxEXPAND);

        grid->Add(MakeCell(box, "T�k� j."), 0, wxEXPAND);
        m_lblAllHeavyA = MakeCell(box, "0", wxALIGN_RIGHT);
        m_lblAllHeavyE = MakeCell(box, "0", wxALIGN_RIGHT);
        grid->Add(m_lblAllHeavyA, 0, wxEXPAND);
        grid->Add(m_lblAllHeavyE, 0, wxEXPAND);

        grid->Add(MakeCell(box, "Vzdu�n� j."), 0, wxEXPAND);
        m_lblAllAirA = MakeCell(box, "0", wxALIGN_RIGHT);
        m_lblAllAirE = MakeCell(box, "0", wxALIGN_RIGHT);
        grid->Add(m_lblAllAirA, 0, wxEXPAND);
        grid->Add(m_lblAllAirE, 0, wxEXPAND);

        grid->Add(MakeCell(box, "Velitel�"), 0, wxEXPAND);
        m_lblAllCmdA = MakeCell(box, "0", wxALIGN_RIGHT);
        m_lblAllCmdE = MakeCell(box, "0", wxALIGN_RIGHT);
        grid->Add(m_lblAllCmdA, 0, wxEXPAND);
        grid->Add(m_lblAllCmdE, 0, wxEXPAND);

        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(grid, 0, wxALL | wxEXPAND, 12);
        box->SetSizer(s);

        main->Add(box, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 14);
    }

    // ---- Statistika aktu�ln�ho levelu ----
    {
        auto* title = new wxStaticText(root, wxID_ANY, "Statistika aktu�ln�ho levelu");
        ApplySectionTitleStyle(title);
        main->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 14);

        auto* box = new wxPanel(root);
        box->SetBackgroundColour(wxColour(15, 55, 15));

        auto* grid = new wxFlexGridSizer(5, 3, 6, 10);
        grid->AddGrowableCol(0, 1);
        grid->AddGrowableCol(1, 0);
        grid->AddGrowableCol(2, 0);

        auto* h0 = MakeCell(box, "");
        auto* h1 = MakeCell(box, "Aliance - ztr�ty", wxALIGN_RIGHT);
        auto* h2 = MakeCell(box, "Other Side - ztr�ty", wxALIGN_RIGHT);
        ApplyHeaderStyle(h1);
        ApplyHeaderStyle(h2);

        grid->Add(h0, 0, wxEXPAND);
        grid->Add(h1, 0, wxEXPAND);
        grid->Add(h2, 0, wxEXPAND);

        grid->Add(MakeCell(box, "Lehk� j."), 0, wxEXPAND);
        m_lblLvlLightA = MakeCell(box, "0", wxALIGN_RIGHT);
        m_lblLvlLightE = MakeCell(box, "0", wxALIGN_RIGHT);
        grid->Add(m_lblLvlLightA, 0, wxEXPAND);
        grid->Add(m_lblLvlLightE, 0, wxEXPAND);

        grid->Add(MakeCell(box, "T�k� j."), 0, wxEXPAND);
        m_lblLvlHeavyA = MakeCell(box, "0", wxALIGN_RIGHT);
        m_lblLvlHeavyE = MakeCell(box, "0", wxALIGN_RIGHT);
        grid->Add(m_lblLvlHeavyA, 0, wxEXPAND);
        grid->Add(m_lblLvlHeavyE, 0, wxEXPAND);

        grid->Add(MakeCell(box, "Vzdu�n� j."), 0, wxEXPAND);
        m_lblLvlAirA = MakeCell(box, "0", wxALIGN_RIGHT);
        m_lblLvlAirE = MakeCell(box, "0", wxALIGN_RIGHT);
        grid->Add(m_lblLvlAirA, 0, wxEXPAND);
        grid->Add(m_lblLvlAirE, 0, wxEXPAND);

        grid->Add(MakeCell(box, "Velitel�"), 0, wxEXPAND);
        m_lblLvlCmdA = MakeCell(box, "0", wxALIGN_RIGHT);
        m_lblLvlCmdE = MakeCell(box, "0", wxALIGN_RIGHT);
        grid->Add(m_lblLvlCmdA, 0, wxEXPAND);
        grid->Add(m_lblLvlCmdE, 0, wxEXPAND);

        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(grid, 0, wxALL | wxEXPAND, 12);
        box->SetSizer(s);

        main->Add(box, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 14);
    }

    // ---- Player box ----
    {
        auto* box = new wxPanel(root);
        box->SetBackgroundColour(wxColour(15, 55, 15));

        auto* s = new wxBoxSizer(wxVERTICAL);

        m_lblPlayerName = new wxStaticText(box, wxID_ANY, "Hr�� - John Alexander");
        m_lblPlayerRank = new wxStaticText(box, wxID_ANY, "Hodnost: Poru��k");
        m_lblPlayerExp = new wxStaticText(box, wxID_ANY, "Zku�enost: 0 (0)");
        m_lblPlayerMaxUnits = new wxStaticText(box, wxID_ANY, "Max. po�et st�l�ch jednotek: 0");
        m_lblPlayerMaxCmds = new wxStaticText(box, wxID_ANY, "Max. po�et velitel�: 0");

        m_lblPlayerName->SetForegroundColour(wxColour(210, 255, 210));
        m_lblPlayerRank->SetForegroundColour(wxColour(210, 255, 210));
        m_lblPlayerExp->SetForegroundColour(wxColour(210, 255, 210));
        m_lblPlayerMaxUnits->SetForegroundColour(wxColour(210, 255, 210));
        m_lblPlayerMaxCmds->SetForegroundColour(wxColour(210, 255, 210));

        m_lblPlayerName->SetFont(MakeUiFont(12, true));
        m_lblPlayerRank->SetFont(MakeUiFont(11, false));
        m_lblPlayerExp->SetFont(MakeUiFont(11, false));
        m_lblPlayerMaxUnits->SetFont(MakeUiFont(11, false));
        m_lblPlayerMaxCmds->SetFont(MakeUiFont(11, false));

        s->Add(m_lblPlayerName, 0, wxALL, 10);
        s->Add(m_lblPlayerRank, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        s->Add(m_lblPlayerExp, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        s->Add(m_lblPlayerMaxUnits, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        s->Add(m_lblPlayerMaxCmds, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

        box->SetSizer(s);
        main->Add(box, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxEXPAND, 14);
    }

    root->SetSizer(main);
    Layout();
}

void StrategicInfoFrame::RefreshUI()
{
    // overall
    m_lblAllLightA->SetLabel(wxString::Format("%d", m_stats.alliance_all.light));
    m_lblAllLightE->SetLabel(wxString::Format("%d", m_stats.enemy_all.light));
    m_lblAllHeavyA->SetLabel(wxString::Format("%d", m_stats.alliance_all.heavy));
    m_lblAllHeavyE->SetLabel(wxString::Format("%d", m_stats.enemy_all.heavy));
    m_lblAllAirA->SetLabel(wxString::Format("%d", m_stats.alliance_all.air));
    m_lblAllAirE->SetLabel(wxString::Format("%d", m_stats.enemy_all.air));
    m_lblAllCmdA->SetLabel(wxString::Format("%d", m_stats.alliance_all.commanders));
    m_lblAllCmdE->SetLabel(wxString::Format("%d", m_stats.enemy_all.commanders));

    // level
    m_lblLvlLightA->SetLabel(wxString::Format("%d", m_stats.alliance_level.light));
    m_lblLvlLightE->SetLabel(wxString::Format("%d", m_stats.enemy_level.light));
    m_lblLvlHeavyA->SetLabel(wxString::Format("%d", m_stats.alliance_level.heavy));
    m_lblLvlHeavyE->SetLabel(wxString::Format("%d", m_stats.enemy_level.heavy));
    m_lblLvlAirA->SetLabel(wxString::Format("%d", m_stats.alliance_level.air));
    m_lblLvlAirE->SetLabel(wxString::Format("%d", m_stats.enemy_level.air));
    m_lblLvlCmdA->SetLabel(wxString::Format("%d", m_stats.alliance_level.commanders));
    m_lblLvlCmdE->SetLabel(wxString::Format("%d", m_stats.enemy_level.commanders));

    // player
    const CommanderRankRec* rec = FindRankRec(m_player.rank);
    const int maxUnits = rec ? rec->max_units : 0;
    const int maxCmds = rec ? rec->max_commanders : 0;
    const int nextExp = FindNextRankExp(m_player.rank);

    m_lblPlayerName->SetLabel(wxString::Format("Hr�� - %s", wxString::FromUTF8(m_player.name)));
    m_lblPlayerRank->SetLabel(wxString::Format("Hodnost: %s", GetRankNameCz(m_player.rank)));
    m_lblPlayerExp->SetLabel(wxString::Format("Zku�enost: %d (%d)", m_player.experience, nextExp));
    m_lblPlayerMaxUnits->SetLabel(wxString::Format("Max. po�et st�l�ch jednotek: %d", maxUnits));
    m_lblPlayerMaxCmds->SetLabel(wxString::Format("Max. po�et velitel�: %d", maxCmds));

    Layout();
}

// ---------------- Paths ----------------

wxString StrategicInfoFrame::FindStrategicStatePath() const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::path(m_level.source_path).parent_path();
    if (base.empty() || !fs::exists(base, ec))
        base = fs::current_path(ec);
    return wxString::FromUTF8((base / "strategic_state.json").string());
}

wxString StrategicInfoFrame::FindStrategicStatsPath() const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::path(m_level.source_path).parent_path();
    if (base.empty() || !fs::exists(base, ec))
        base = fs::current_path(ec);
    return wxString::FromUTF8((base / "strategic_stats.json").string());
}

wxString StrategicInfoFrame::FindHodnostiDefPath() const
{
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path base = fs::path(m_level.source_path).parent_path();
    if (base.empty() || !fs::exists(base, ec))
        base = fs::current_path(ec);

    const std::array<fs::path, 6> candidates = {
        base / "HODNOSTI.DEF",
        base / "hodnosti.def",
        base / "DATA" / "HODNOSTI.DEF",
        base / "data" / "HODNOSTI.DEF",
        fs::current_path(ec) / "data" / "HODNOSTI.DEF",
        fs::current_path(ec) / "HODNOSTI.DEF"
    };

    for (const auto& p : candidates)
    {
        if (fs::exists(p, ec) && fs::is_regular_file(p, ec))
            return wxString::FromUTF8(p.string());
    }
    return "";
}

// ---------------- Tiny JSON helpers ----------------

bool StrategicInfoFrame::ParseJsonIntField(const std::string& obj, const char* key, int& outValue)
{
    if (!key) return false;
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;

    ++pos;
    while (pos < obj.size() && std::isspace((unsigned char)obj[pos])) ++pos;

    const char* start = obj.c_str() + pos;
    char* end = nullptr;
    long v = std::strtol(start, &end, 10);
    if (end == start) return false;
    outValue = (int)v;
    return true;
}

bool StrategicInfoFrame::ParseJsonStringField(const std::string& obj, const char* key, std::string& outValue)
{
    if (!key) return false;
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;

    ++pos;
    while (pos < obj.size() && std::isspace((unsigned char)obj[pos])) ++pos;
    if (pos >= obj.size() || obj[pos] != '"') return false;
    ++pos;

    std::string s;
    bool esc = false;
    for (; pos < obj.size(); ++pos)
    {
        char c = obj[pos];
        if (esc)
        {
            switch (c)
            {
            case '"': case '\\': case '/': s.push_back(c); break;
            case 'n': s.push_back('\n'); break;
            case 'r': s.push_back('\r'); break;
            case 't': s.push_back('\t'); break;
            default: s.push_back(c); break;
            }
            esc = false;
            continue;
        }
        if (c == '\\') { esc = true; continue; }
        if (c == '"') break;
        s.push_back(c);
    }
    outValue = std::move(s);
    return true;
}

// ---------------- Data loading ----------------

void StrategicInfoFrame::LoadRanksTable()
{
    m_ranks.clear();

    wxString p = FindHodnostiDefPath();
    if (p.empty())
    {
        // fallback defaults (from your snippet)
        m_ranks = {
            {0,  2,  2,     0,  0},
            {1,  4,  6,     0,  0},
            {2,  9, 10,     0,  2},
            {3, 12, 18,   300,  4},
            {4, 14, 26,  2550,  6},
            {5, 18, 36,  5350,  8},
            {6, 22, 48, 10000, 10},
            {7, 26, 66, 16000, 12},
            {8, 32, 84, 26000, 14},
        };
        return;
    }

    std::ifstream f(p.ToStdString());
    if (!f) return;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) return;

    std::regex re(R"(DefineCommander\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*\))");
    for (auto it = std::sregex_iterator(data.begin(), data.end(), re); it != std::sregex_iterator(); ++it)
    {
        const auto& m = *it;
        if (m.size() < 6) continue;
        CommanderRankRec r;
        r.rank = std::stoi(m[1].str());
        r.max_units = std::stoi(m[2].str());
        r.actions_required = std::stoi(m[3].str());
        r.exp_required = std::stoi(m[4].str());
        r.max_commanders = std::stoi(m[5].str());
        m_ranks.push_back(r);
    }

    std::sort(m_ranks.begin(), m_ranks.end(), [](const CommanderRankRec& a, const CommanderRankRec& b) {
        return a.rank < b.rank;
        });
}

void StrategicInfoFrame::LoadPlayerFromStrategicStateIfPresent()
{
    // Backward compatible: reads optional "player" object if later added:
    // "player": {"name":"John Alexander","rank":0,"experience":0,"actions":0}
    wxString p = FindStrategicStatePath();
    std::ifstream f(p.ToStdString());
    if (!f) return;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) return;

    std::smatch m;
    std::regex player_re("\"player\"\\s*:\\s*\\{([^}]*)\\}");
    if (std::regex_search(data, m, player_re) && m.size() > 1)
    {
        std::string obj = m[1].str();
        (void)ParseJsonStringField(obj, "name", m_player.name);
        (void)ParseJsonIntField(obj, "rank", m_player.rank);
        (void)ParseJsonIntField(obj, "experience", m_player.experience);
        (void)ParseJsonIntField(obj, "actions", m_player.actions);
    }
    // else: default John Alexander, 0/0/0
}

static bool ReadLossBlockFromObj(const std::string& obj, StrategicInfoFrame::LossBlock& out)
{
    bool any = false;
    any |= StrategicInfoFrame::ParseJsonIntField(obj, "light", out.light);
    any |= StrategicInfoFrame::ParseJsonIntField(obj, "heavy", out.heavy);
    any |= StrategicInfoFrame::ParseJsonIntField(obj, "air", out.air);
    any |= StrategicInfoFrame::ParseJsonIntField(obj, "commanders", out.commanders);
    return any;
}

void StrategicInfoFrame::LoadMissionStatsIfPresent()
{
    // Optional file:
    // {
    //   "all":   {"alliance":{"light":..,"heavy":..,"air":..,"commanders":..}, "enemy":{...}},
    //   "level": {"alliance":{...}, "enemy":{...}}
    // }
    wxString p = FindStrategicStatsPath();
    std::ifstream f(p.ToStdString());
    if (!f) return;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) return;

    auto extractObj = [&](const char* section, const char* side, std::string& outObj) -> bool
        {
            std::regex re(std::string("\"") + section + "\"\\s*:\\s*\\{([^}]*)\\}");
            std::smatch m;
            if (!std::regex_search(data, m, re) || m.size() < 2) return false;
            std::string sec = m[1].str();

            std::regex re2(std::string("\"") + side + "\"\\s*:\\s*\\{([^}]*)\\}");
            if (!std::regex_search(sec, m, re2) || m.size() < 2) return false;
            outObj = m[1].str();
            return true;
        };

    std::string obj;

    if (extractObj("all", "alliance", obj)) ReadLossBlockFromObj(obj, m_stats.alliance_all);
    if (extractObj("all", "enemy", obj))    ReadLossBlockFromObj(obj, m_stats.enemy_all);

    if (extractObj("level", "alliance", obj)) ReadLossBlockFromObj(obj, m_stats.alliance_level);
    if (extractObj("level", "enemy", obj))    ReadLossBlockFromObj(obj, m_stats.enemy_level);
}

// ---------------- Rank helpers ----------------

void StrategicInfoFrame::RecomputePlayerRank()
{
    // compute best based on exp + actions
    int best = 0;
    for (const auto& r : m_ranks)
    {
        if (m_player.experience >= r.exp_required && m_player.actions >= r.actions_required)
            best = std::max(best, r.rank);
    }
    m_player.rank = best;
}

const StrategicInfoFrame::CommanderRankRec* StrategicInfoFrame::FindRankRec(int rank) const
{
    for (const auto& r : m_ranks)
        if (r.rank == rank) return &r;
    return nullptr;
}

int StrategicInfoFrame::FindNextRankExp(int current_rank) const
{
    // next higher rank exp_required
    int nextExp = 0;
    bool found = false;
    for (const auto& r : m_ranks)
    {
        if (r.rank == current_rank) { found = true; continue; }
        if (found && r.rank > current_rank) { nextExp = r.exp_required; break; }
    }
    if (nextExp <= 0)
    {
        for (const auto& r : m_ranks)
            nextExp = std::max(nextExp, r.exp_required);
    }
    return nextExp;
}

wxString StrategicInfoFrame::GetRankNameCz(int rank) const
{
    switch (rank)
    {
    case 0: return "Poru��k";
    case 1: return "Nadporu��k";
    case 2: return "Kapit�n";
    case 3: return "Major";
    case 4: return "Podplukovn�k";
    case 5: return "Plukovn�k";
    case 6: return "Gener�lmajor";
    case 7: return "Gener�lporu��k";
    case 8: return "Arm�dn� gener�l";
    default: return wxString::Format("Hodnost %d", rank);
    }
}
