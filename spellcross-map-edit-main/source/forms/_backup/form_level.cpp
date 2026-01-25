#include "form_level.h"

#include "main.h"
#include "other.h"

#include <wx/dcbuffer.h>
#include <wx/choicdlg.h>
#include <wx/spinctrl.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <regex>
#include <array>
#include <sstream>
#include "LZ_spell.h"

// Best-effort background decoding.
// Some LEVEL_XX.LZ files are *compressed* using Spellcross LZW variant.
// LZ_spell.cpp provides the implementation; we forward-declare the minimal API here
// to keep this file decoupled from headers.


wxBEGIN_EVENT_TABLE(StrategicLevelFrame, wxFrame)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_RESEARCH, StrategicLevelFrame::OnResearch)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_BUY,      StrategicLevelFrame::OnBuyUnits)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_ENDTURN,  StrategicLevelFrame::OnEndTurn)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_LAUNCH,   StrategicLevelFrame::OnLaunch)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_STRATEGIC_MAP, StrategicLevelFrame::OnShowStrategicMap)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_HIERARCHY, StrategicLevelFrame::OnShowHierarchy)
wxEND_EVENT_TABLE()

// UI-only: readonly text panel under the territory grid (instead of popups)
static const int ID_TERRITORY_TEXTBOX = wxID_HIGHEST + 2201;

static wxBitmap RenderSpellLabel(const SpellData* data, const std::string& text)
{
    if(!data || !data->font || text.empty())
        return wxBitmap(1, 1);

    SpellFont* font = data->font;

    std::string tmp = text;
    int w = font->GetTextWidth(tmp) + 8;
    int h = font->GetHeight() + 6;
    w = std::max(w, 8);
    h = std::max(h, 8);

    std::vector<uint8_t> buf((size_t)w * h, 0);
    font->Render(buf.data(), buf.data() + buf.size(), w, 3, 2, w - 6, h - 4, tmp, 229, 254, SpellFont::FontShadow::DIAG3);

    auto to8 = [](unsigned char v) -> unsigned char {
        return (unsigned char)std::min(255, (int)v * 4);
    };

    wxImage img(w, h);
    for(int y = 0; y < h; ++y)
    for(int x = 0; x < w; ++x)
    {
        uint8_t idx = buf[(size_t)y * w + x];
        unsigned char r = 0, g = 0, b = 0;
        if(idx < 256)
        {
            r = to8(data->map_pal[idx][0]);
            g = to8(data->map_pal[idx][1]);
            b = to8(data->map_pal[idx][2]);
        }
        img.SetRGB(x, y, r, g, b);
    }

    return wxBitmap(img);
}

static void UpdateSpellLabel(wxStaticBitmap* target, const SpellData* data, const std::string& text)
{
    if(!target)
        return;

    if(auto bmp = RenderSpellLabel(data, text); bmp.IsOk())
        target->SetBitmap(bmp);
}

static std::string trim(std::string s)
{
    auto notspace = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
    return s;
}

static std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::toupper(c); });
    return s;
}

static std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

// --- TEXTS helper (disk based) -------------------------------------------------
//
// Per-mission texts live in DATA/TEXTS as:
//   Txx_yyA            (briefing)
//   Txx_yyA.OK         (victory)
//   Txx_yyA.BAD        (defeat)
//   Txx_yyA.S          (counter-attack)
//
// You said all FS archives are unpacked on program start. That means the plain
// files should exist on disk, so we can load them directly here without needing
// extra FSarchive plumbing.
static std::string mission_to_text_base(std::string token)
{
    token = trim(token);
    if(token.empty())
        return token;
    token = to_upper(token);
    if(token[0] == 'M')
        token[0] = 'T';
    return token;
}

static bool read_text_file(const std::filesystem::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if(!f)
        return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

static void append_text_snippet(wxString& info, const std::string& label, const std::string& raw)
{
    if(raw.empty())
        return;

    // Quick cleanup: remove CR and typical in-game control marks.
    std::string s;
    s.reserve(raw.size());
    for(size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char c = (unsigned char)raw[i];
        if(c == '\r')
            continue;
        if(c == '~' || c == 0x1A)
            continue;
        s.push_back((char)c);
    }

    // Limit preview to keep messagebox readable.
    const size_t kMax = 600;
    if(s.size() > kMax)
        s = s.substr(0, kMax) + "...";

    info << "\n" << label << "\n";
    info << wxString(char2wstringCP895(s.c_str())) << "\n";
}

static void try_append_text_set(wxString& info, const std::filesystem::path& base_dir, std::string mission_token)
{
    if(mission_token.empty())
        return;

    std::string base = mission_to_text_base(mission_token);

    // Best-effort: if token ends with digit (M02_02), try A (T02_02A)
    if(!base.empty())
    {
        char last = base.back();
        if(last >= '0' && last <= '9')
            base.push_back('A');
    }

    auto load_and_append = [&](const std::string& suffix, const char* caption)
    {
        std::string raw;
        if(read_text_file(base_dir / (base + suffix), raw))
            append_text_snippet(info, wxString::Format("%s (%s%s)", caption, base, suffix).ToStdString(), raw);
    };

    load_and_append("",     "Briefing");
    load_and_append(".OK",  "Victory");
    load_and_append(".BAD", "Defeat");
    load_and_append(".S",   "Counter-attack");
}

StrategicLevelFrame::StrategicLevelFrame(MainFrame* parent, const LevelData& level)
    : wxFrame(parent, wxID_ANY, "Strategic Level", wxDefaultPosition, wxSize(1100, 700),
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
      m_main(parent),
      m_spellData(parent ? parent->spell_data : nullptr),
      m_level(level)
{
    m_money = 0;
    m_research = 0;
    m_playerUnits = m_level.start_units;

    // init territory mission state from LevelData
    for(const auto& t : m_level.territories)
    {
        m_territoryCurrentMission[t.id] = t.mission;
        m_territoryLaunchCount[t.id] = 0;
    }

    LoadStrategicState();
    BuildUI();
    TryLoadBackground();
    RefreshUI();

    Bind(wxEVT_ACTIVATE, &StrategicLevelFrame::OnActivate, this);
}

void StrategicLevelFrame::BuildUI()
{
    auto root = new wxPanel(this);

    // --- Top bar (Money / Research / Turn) ---
    auto topBar = new wxPanel(root);
    topBar->SetBackgroundColour(wxColour(20, 70, 20));
    auto topSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmap placeholder(1, 1);
    m_lblMoney = new wxStaticBitmap(topBar, wxID_ANY, placeholder);
    m_lblResearch = new wxStaticBitmap(topBar, wxID_ANY, placeholder);
    m_lblTurn = new wxStaticBitmap(topBar, wxID_ANY, placeholder);

    topSizer->AddStretchSpacer(1);
    topSizer->Add(m_lblMoney, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
    topSizer->AddSpacer(20);
    topSizer->Add(m_lblResearch, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
    topSizer->AddSpacer(20);
    topSizer->Add(m_lblTurn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
    topSizer->AddSpacer(10);
    topBar->SetSizer(topSizer);

    // --- Main split: Map (left) + Right panel ---
    auto mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // Left "map" panel
    m_mapPanel = new wxPanel(root);
    m_mapPanel->SetBackgroundColour(wxColour(30, 30, 30));

    // IMPORTANT: separate paint surface so the background isn't completely covered by child controls.
    // The canvas shows the composed strategic map; controls live below it.
    m_mapSizer = new wxBoxSizer(wxVERTICAL);

    m_mapCanvas = new wxPanel(m_mapPanel);
    m_mapCanvas->SetBackgroundColour(wxColour(30, 30, 30));
    m_mapCanvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_mapCanvas->Bind(wxEVT_PAINT, &StrategicLevelFrame::OnMapPaint, this);
    m_mapCanvas->SetMinSize(wxSize(640, 360));
    m_mapSizer->Add(m_mapCanvas, 1, wxALL | wxEXPAND, 8);

    auto controls = new wxPanel(m_mapPanel);
    controls->SetBackgroundColour(wxColour(20, 20, 20));
    auto controlsSizer = new wxBoxSizer(wxVERTICAL);

    auto mapTitle = new wxStaticText(controls, wxID_ANY, "Strategic map (click territory)");
    mapTitle->SetForegroundColour(*wxLIGHT_GREY);
    controlsSizer->Add(mapTitle, 0, wxLEFT | wxRIGHT | wxTOP, 6);

    // Territory buttons (temporary UI)
    auto grid = new wxGridSizer(0, 4, 6, 6);
    for(size_t i = 0; i < m_level.territories.size(); ++i)
    {
        const auto& t = m_level.territories[i];
        auto id = ID_TERRITORY_BASE + (int)i;

        wxString label = wxString::Format("T%02d\n%s", t.id, t.mission);
        auto btn = new wxButton(controls, id, label, wxDefaultPosition, wxSize(140, 60));
        btn->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnTerritory, this);
        grid->Add(btn, 0, wxEXPAND);
    }
    controlsSizer->Add(grid, 0, wxALL | wxEXPAND, 6);

    // Scrollbox with territory + briefing texts (replaces wxMessageBox popup)
    auto txtTitle = new wxStaticText(controls, wxID_ANY, "Territory / briefing text");
    txtTitle->SetForegroundColour(*wxLIGHT_GREY);
    controlsSizer->Add(txtTitle, 0, wxLEFT | wxRIGHT | wxTOP, 6);

    auto txt = new wxTextCtrl(
        controls,
        ID_TERRITORY_TEXTBOX,
        "",
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP);
    txt->SetMinSize(wxSize(-1, 180));
    controlsSizer->Add(txt, 0, wxALL | wxEXPAND, 6);

    controls->SetSizer(controlsSizer);
    m_mapSizer->Add(controls, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    m_mapPanel->SetSizer(m_mapSizer);

    // Right panel: roster + actions
    auto right = new wxPanel(root);
    right->SetBackgroundColour(wxColour(10, 50, 10));
    auto rightSizer = new wxBoxSizer(wxVERTICAL);

    auto rightNavSizer = new wxBoxSizer(wxHORIZONTAL);
    m_btnStrategicMap = new wxButton(right, ID_BTN_STRATEGIC_MAP, "Strategick\u00e1 mapa");
    m_btnHierarchy = new wxButton(right, ID_BTN_HIERARCHY, "Hierarchie");
    rightNavSizer->Add(m_btnStrategicMap, 1, wxRIGHT, 6);
    rightNavSizer->Add(m_btnHierarchy, 1);
    rightSizer->Add(rightNavSizer, 0, wxALL | wxEXPAND, 10);

    auto rosterTitle = new wxStaticText(right, wxID_ANY, "Forces / Hierarchy");
    rosterTitle->SetForegroundColour(*wxWHITE);
    // Ped tmto dkem pidejte zskn fontu z m_spellData
    wxFont font = m_spellData && m_spellData->font ? wxFont(wxFontInfo(m_spellData->font->GetHeight())) : wxFont(wxFontInfo(12));
    rosterTitle->SetFont(font);
    rightSizer->Add(rosterTitle, 0, wxALL, 10);

    m_rightBook = new wxSimplebook(right, wxID_ANY);
    auto rosterPage = new wxPanel(m_rightBook);
    auto rosterSizer = new wxBoxSizer(wxVERTICAL);
    m_roster = new wxListCtrl(rosterPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_roster->InsertColumn(0, "Unit");
    m_roster->InsertColumn(1, "Count");
    m_roster->InsertColumn(2, "HP");
    rosterSizer->Add(m_roster, 1, wxEXPAND);
    rosterPage->SetSizer(rosterSizer);

    auto hierarchyPage = new wxPanel(m_rightBook);
    auto hierarchySizer = new wxBoxSizer(wxVERTICAL);
    auto hierarchyIntro = new wxStaticText(
        hierarchyPage,
        wxID_ANY,
        "P\u0159ehled hierarchie\n\n"
        "- Z\u00e1kladn\u00ed formac\u00ed je prapor.\n"
        "- 2 prapory tvo\u0159\u00ed pluk.\n"
        "- 2 pluky (4 prapory) tvo\u0159\u00ed brig\u00e1du.\n\n"
        "Velitel\u00e9 jsou vz\u00e1cn\u00e9 jednotky ur\u010den\u00e9 pro formace.\n"
        "Vy\u0161\u0161\u00ed hodnosti umo\u017e\u0148uj\u00ed vy\u0161\u0161\u00ed formace.\n"
        "Pokud je jednotka s velitelem zni\u010dena, velitel je ztracen.");
    hierarchyIntro->SetForegroundColour(*wxWHITE);
    hierarchySizer->Add(hierarchyIntro, 0, wxBOTTOM | wxEXPAND, 10);

    m_hierarchyList = new wxListCtrl(hierarchyPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_hierarchyList->InsertColumn(0, "Formation");
    m_hierarchyList->InsertColumn(1, "Commander");
    m_hierarchyList->InsertColumn(2, "Units");
    hierarchySizer->Add(m_hierarchyList, 1, wxEXPAND);
    hierarchyPage->SetSizer(hierarchySizer);

    m_rightBook->AddPage(rosterPage, "Strategick\u00e1 mapa", true);
    m_rightBook->AddPage(hierarchyPage, "Hierarchie", false);
    rightSizer->Add(m_rightBook, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    auto btnSizer = new wxBoxSizer(wxVERTICAL);
    m_btnResearch = new wxButton(right, ID_BTN_RESEARCH, "Research");
    m_btnBuy      = new wxButton(right, ID_BTN_BUY, "Buy units");
    m_btnLaunch   = new wxButton(right, ID_BTN_LAUNCH, "Launch mission");
    m_btnEndTurn  = new wxButton(right, ID_BTN_ENDTURN, "End turn");

    btnSizer->Add(m_btnResearch, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnBuy,      0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnLaunch,   0, wxEXPAND | wxBOTTOM, 12);
    btnSizer->Add(m_btnEndTurn,  0, wxEXPAND);

    rightSizer->Add(btnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
    right->SetSizer(rightSizer);

    mainSizer->Add(m_mapPanel, 2, wxEXPAND);
    mainSizer->Add(right, 1, wxEXPAND);

    auto rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(topBar, 0, wxEXPAND);
    rootSizer->Add(mainSizer, 1, wxEXPAND);
    root->SetSizer(rootSizer);
}

void StrategicLevelFrame::RefreshUI()
{
    UpdateSpellLabel(m_lblMoney, m_spellData, wxString::Format("Money %d", m_money).ToStdString());
    UpdateSpellLabel(m_lblResearch, m_spellData, wxString::Format("Research %d", m_research).ToStdString());
    UpdateSpellLabel(m_lblTurn, m_spellData, wxString::Format("Turn %d", m_turn).ToStdString());

    m_roster->DeleteAllItems();
    for(size_t i = 0; i < m_playerUnits.size(); ++i)
    {
        const auto& u = m_playerUnits[i];
        long idx = m_roster->InsertItem((long)i, GetUnitDisplayName(u.unit_id));
        m_roster->SetItem(idx, 1, wxString::Format("%d", u.count));
        m_roster->SetItem(idx, 2, wxString::Format("%d", u.health));
    }

    m_roster->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);
    m_roster->SetColumnWidth(1, wxLIST_AUTOSIZE_USEHEADER);
    m_roster->SetColumnWidth(2, wxLIST_AUTOSIZE_USEHEADER);

    m_btnLaunch->Enable(m_selectedTerritory >= 0);
}

void StrategicLevelFrame::OnShowStrategicMap(wxCommandEvent&)
{
    if(m_rightBook)
        m_rightBook->SetSelection(0);
}

void StrategicLevelFrame::OnShowHierarchy(wxCommandEvent&)
{
    if(m_rightBook)
        m_rightBook->SetSelection(1);
}

void StrategicLevelFrame::OnTerritory(wxCommandEvent& ev)
{
    int idx = ev.GetId() - ID_TERRITORY_BASE;
    if(idx < 0 || idx >= (int)m_level.territories.size())
        return;

    m_selectedTerritory = m_level.territories[idx].id;

    const auto& t = m_level.territories[idx];
    wxString info;
    info << wxString::Format("Territory %d\n", t.id);
    info << "Mission: " << t.mission << "\n";
    info << "Intro: " << t.intro_mission << "\n";
    info << "Music: " << t.music << "\n";
    info << wxString::Format("Strategic point: %d,%d\n", t.strategic_x, t.strategic_y);

    auto itc = m_territoryCurrentMission.find(t.id);
    if(itc != m_territoryCurrentMission.end())
        info << "Current: " << itc->second << "\n";

    auto itn = m_territoryLaunchCount.find(t.id);
    if(itn != m_territoryLaunchCount.end())
        info << wxString::Format("Played: %d\n", itn->second);

    // --- Show per-mission texts from DATA/TEXTS (briefing + OK/BAD/S) ---
    // You said all FS archives are unpacked on start, so these should exist as plain files.
    // The only reason you'd see "nothing" is usually that the *working directory* isn't the
    // game root. So we resolve DATA/TEXTS relative to the loaded LEVEL_XX.DEF path, with a
    // fallback to current working directory.
    std::filesystem::path texts_dir;
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        auto try_dir = [&](const fs::path& p)
        {
            if(texts_dir.empty() && fs::exists(p, ec) && fs::is_directory(p, ec))
                texts_dir = p;
        };

        // 1) Walk up from the level DEF location and try common layouts
        fs::path base = fs::path(m_level.source_path).parent_path();
        for(int i = 0; i < 8 && !base.empty() && texts_dir.empty(); ++i)
        {
            try_dir(base / "DATA" / "TEXTS");
            try_dir(base / "DATA" / "texts");
            try_dir(base / "TEXTS");
            try_dir(base / "texts");

            base = base.parent_path();
        }

        // 2) Fallback: current working directory
        if(texts_dir.empty())
        {
            const fs::path cwd = fs::current_path(ec);
            try_dir(cwd / "DATA" / "TEXTS");
            try_dir(cwd / "DATA" / "texts");
            try_dir(cwd / "TEXTS");
            try_dir(cwd / "texts");
        }
    }

    if(!texts_dir.empty())
    {
        // Use the *current* mission token (can change as you replay territories)
        std::string cur = t.mission;
        auto itc2 = m_territoryCurrentMission.find(t.id);
        if(itc2 != m_territoryCurrentMission.end() && !itc2->second.empty())
            cur = itc2->second;

        try_append_text_set(info, texts_dir, cur);

        // Also show intro (some territories use different intro token)
        if(!t.intro_mission.empty() && to_lower(t.intro_mission) != "none")
            try_append_text_set(info, texts_dir, t.intro_mission);
    }
    else
    {
        info << "\n(TEXTS) DATA/TEXTS not found.\n";
        info << "Level path: " << m_level.source_path << "\n";
        info << "Working dir: " << std::filesystem::current_path().string() << "\n";
    }

    // Show in the scrollbox under the map (no popup)
    if(auto* box = wxDynamicCast(m_mapPanel->FindWindow(ID_TERRITORY_TEXTBOX), wxTextCtrl))
    {
        box->SetValue(info);
        box->ShowPosition(0);
    }
    RefreshUI();
}

void StrategicLevelFrame::OnResearch(wxCommandEvent&)
{
    if(m_money >= 100) {
        m_money -= 100;
        m_research += 1;
    } else {
        wxMessageBox("Not enough money for research (demo cost 100).", "Research", wxOK | wxICON_WARNING, this);
    }
    SaveStrategicState();
    RefreshUI();
}

void StrategicLevelFrame::OnBuyUnits(wxCommandEvent&)
{
    if(!m_spellData || !m_spellData->units)
    {
        wxMessageBox("Units data not loaded.", "Buy units", wxOK | wxICON_WARNING, this);
        return;
    }

    wxDialog dlg(this, wxID_ANY, "Buy units", wxDefaultPosition, wxSize(420, 480));
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl = new wxStaticText(&dlg, wxID_ANY, "Select unit:");
    rootSizer->Add(lbl, 0, wxLEFT | wxRIGHT | wxTOP, 10);

    auto* list = new wxListBox(&dlg, wxID_ANY);
    std::vector<int> unit_ids;
    unit_ids.reserve(m_spellData->units->GetUnits().size());
    for(const auto* unit : m_spellData->units->GetUnits())
    {
        if(!unit)
            continue;
        unit_ids.push_back(unit->type_id);
        list->Append(wxString::Format("#%02d: %s", unit->type_id, wxString(char2wstringCP895(unit->name))));
    }
    if(!unit_ids.empty())
        list->SetSelection(0);
    rootSizer->Add(list, 1, wxALL | wxEXPAND, 10);

    auto* countSizer = new wxBoxSizer(wxHORIZONTAL);
    countSizer->Add(new wxStaticText(&dlg, wxID_ANY, "Count:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* spinCount = new wxSpinCtrl(&dlg, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 99, 1);
    countSizer->Add(spinCount, 0);
    rootSizer->Add(countSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* btnBuy = new wxButton(&dlg, wxID_OK, "Buy");
    auto* btnCancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
    btnSizer->AddStretchSpacer(1);
    btnSizer->Add(btnBuy, 0, wxRIGHT, 8);
    btnSizer->Add(btnCancel, 0);
    rootSizer->Add(btnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    dlg.SetSizerAndFit(rootSizer);

    if(dlg.ShowModal() != wxID_OK)
        return;

    int sel = list->GetSelection();
    if(sel == wxNOT_FOUND || sel >= (int)unit_ids.size())
    {
        wxMessageBox("No unit selected.", "Buy units", wxOK | wxICON_WARNING, this);
        return;
    }

    LevelData::PlayerUnitAdd add;
    add.unit_id = unit_ids[sel];
    add.count = spinCount->GetValue();
    add.health = 100;
    add.extra = "-";

    auto it = std::find_if(m_playerUnits.begin(), m_playerUnits.end(),
        [&](const LevelData::PlayerUnitAdd& u)
        {
            return u.unit_id == add.unit_id && u.health == add.health;
        });
    if(it != m_playerUnits.end())
        it->count += add.count;
    else
        m_playerUnits.push_back(add);

    SaveStrategicState();
    RefreshUI();
}

const LevelMission* StrategicLevelFrame::FindMissionByNameUpper(const std::string& name_upper) const
{
    for(const auto& m : m_level.missions)
    {
        if(to_upper(m.name) == name_upper)
            return &m;
    }
    return nullptr;
}

std::string StrategicLevelFrame::ResolveMissionTokenForTerritory(int territory_id) const
{
    // first play can use intro
    int launches = 0;
    auto itL = m_territoryLaunchCount.find(territory_id);
    if(itL != m_territoryLaunchCount.end()) launches = itL->second;

    // find territory record
    const LevelTerritory* terr = nullptr;
    for(const auto& t : m_level.territories)
        if(t.id == territory_id) { terr = &t; break; }

    if(!terr)
        return std::string();

    if(launches == 0 && !terr->intro_mission.empty() && terr->intro_mission != "none")
        return terr->intro_mission;

    auto it = m_territoryCurrentMission.find(territory_id);
    if(it != m_territoryCurrentMission.end() && !it->second.empty() && it->second != "none")
        return it->second;

    return terr->mission;
}

std::wstring StrategicLevelFrame::ResolveMapDefPathForMissionToken(const std::string& mission_token) const
{
    if(mission_token.empty() || mission_token == "none")
        return L"";

    namespace fs = std::filesystem;
    fs::path base = fs::path(m_level.source_path).parent_path();

    // 0) prefer "A" variant when base token ends with digit (m02_02 -> m02_02a.def)
    if(!mission_token.empty())
    {
        char last = mission_token.back();
        if(last >= '0' && last <= '9')
        {
            const std::string token_lower = to_lower(mission_token) + "a";
            const std::string token_upper = to_upper(mission_token) + "A";
            fs::path pVar1 = base / (token_lower + ".def");
            fs::path pVar2 = base / (token_lower + ".DEF");
            fs::path pVar3 = base / (token_upper + ".DEF");
            if(fs::exists(pVar1)) return pVar1.wstring();
            if(fs::exists(pVar2)) return pVar2.wstring();
            if(fs::exists(pVar3)) return pVar3.wstring();
        }
    }

    // 1) exact match (preferred)
    fs::path pExact1 = base / (to_upper(mission_token) + ".DEF");
    fs::path pExact2 = base / (mission_token + ".DEF");
    fs::path pExact3 = base / (mission_token + ".def");

    if(fs::exists(pExact1)) return pExact1.wstring();
    if(fs::exists(pExact2)) return pExact2.wstring();
    if(fs::exists(pExact3)) return pExact3.wstring();

    // 2) if token is base (e.g. m02_03) and multiple variants exist (m02_03a/b/c), offer choice
    std::string up = to_upper(mission_token);

    std::vector<fs::path> candidates;
    std::error_code ec;
    if(fs::exists(base, ec))
    {
        for(fs::directory_iterator it(base, ec); !ec && it != fs::directory_iterator(); ++it)
        {
            if(!it->is_regular_file()) continue;
            auto ext = to_upper(it->path().extension().string());
            if(ext != ".DEF") continue;

            std::string stem = to_upper(it->path().stem().string());
            if(stem.rfind(up, 0) == 0) // starts with
                candidates.push_back(it->path());
        }
    }

    if(candidates.empty())
        return L"";

    if(candidates.size() == 1)
        return candidates[0].wstring();

    std::sort(candidates.begin(), candidates.end());

    wxArrayString choices;
    for(const auto& c : candidates)
        choices.Add(wxString(c.filename().wstring()));

    wxSingleChoiceDialog dlg(const_cast<StrategicLevelFrame*>(this), "Multiple mission variants found. Which one to load?", "Select mission", choices);
    if(dlg.ShowModal() != wxID_OK)
        return L"";

    int sel = dlg.GetSelection();
    if(sel < 0 || sel >= (int)candidates.size())
        return L"";

    return candidates[sel].wstring();
}

void StrategicLevelFrame::OnLaunch(wxCommandEvent&)
{
    if(m_selectedTerritory < 0 || !m_main)
        return;

    const int terr_id = m_selectedTerritory;
    std::string token = ResolveMissionTokenForTerritory(terr_id);
    if(token.empty() || token == "none")
        return;

    std::wstring defPath = ResolveMapDefPathForMissionToken(token);
    if(defPath.empty())
    {
        wxMessageBox("Map DEF not found for mission: " + wxString(token), "Launch", wxOK | wxICON_WARNING, this);
        return;
    }

    if(!m_main->LoadMapFromDefPath(defPath, m_playerUnits))
        return;

    // update launch count
    m_territoryLaunchCount[terr_id] += 1;

    // very simple progression for multi-variant missions:
    // if Mission(MXX_YYA) has EndOKMission(MXX_YYB) -> advance.
    const std::string upperName = to_upper(token);
    if(const LevelMission* m = FindMissionByNameUpper(upperName))
    {
        if(!m->end_ok_mission.empty() && m->end_ok_mission != "none")
        {
            m_territoryCurrentMission[terr_id] = to_lower(m->end_ok_mission);
        }
    }

    RefreshUI();
}

void StrategicLevelFrame::OnEndTurn(wxCommandEvent&)
{
    m_turn += 1;
    m_money += 250;
    SaveStrategicState();
    RefreshUI();
}

static std::filesystem::path GetStrategicStatePath(const LevelData& level)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::path(level.source_path).parent_path();
    if(base.empty() || !fs::exists(base, ec))
        base = fs::current_path(ec);
    return base / "strategic_state.json";
}

static bool LoadStrategicStateFile(const std::filesystem::path& path, int& money, std::vector<LevelData::PlayerUnitAdd>& units)
{
    units.clear();
    money = 0;

    std::ifstream f(path);
    if(!f)
        return false;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if(data.empty())
        return false;

    std::regex money_re("\"money\"\\s*:\\s*(-?\\d+)");
    std::smatch m;
    if(std::regex_search(data, m, money_re) && m.size() > 1)
        money = std::stoi(m[1].str());

    std::regex unit_re("\\{\\s*\"unit_id\"\\s*:\\s*(\\d+)\\s*,\\s*\"count\"\\s*:\\s*(\\d+)\\s*,\\s*\"health\"\\s*:\\s*(\\d+)\\s*\\}");
    auto begin = std::sregex_iterator(data.begin(), data.end(), unit_re);
    auto end = std::sregex_iterator();
    for(auto it = begin; it != end; ++it)
    {
        const auto& match = *it;
        if(match.size() < 4)
            continue;
        LevelData::PlayerUnitAdd entry;
        entry.unit_id = std::stoi(match[1].str());
        entry.count = std::stoi(match[2].str());
        entry.health = std::stoi(match[3].str());
        entry.extra = "-";
        units.push_back(entry);
    }

    return true;
}

static void SaveStrategicStateFile(const std::filesystem::path& path, int money, const std::vector<LevelData::PlayerUnitAdd>& units)
{
    std::ofstream f(path);
    if(!f)
        return;

    f << "{\n";
    f << "  \"money\": " << money << ",\n";
    f << "  \"units\": [\n";
    for(size_t i = 0; i < units.size(); ++i)
    {
        const auto& u = units[i];
        f << "    {\"unit_id\": " << u.unit_id << ", \"count\": " << u.count << ", \"health\": " << u.health << "}";
        if(i + 1 < units.size())
            f << ",";
        f << "\n";
    }
    f << "  ]\n";
    f << "}\n";
}

void StrategicLevelFrame::LoadStrategicState()
{
    int money = 0;
    std::vector<LevelData::PlayerUnitAdd> units;
    const auto path = GetStrategicStatePath(m_level);
    if(LoadStrategicStateFile(path, money, units))
    {
        m_money = money;
        m_playerUnits = std::move(units);
    }
}

void StrategicLevelFrame::SaveStrategicState() const
{
    const auto path = GetStrategicStatePath(m_level);
    SaveStrategicStateFile(path, m_money, m_playerUnits);
}

wxString StrategicLevelFrame::GetUnitDisplayName(int unit_id) const
{
    if(m_spellData && m_spellData->units)
    {
        if(auto* unit = m_spellData->units->GetUnit(unit_id))
            return wxString(char2wstringCP895(unit->name));
    }
    return wxString::Format("%d", unit_id);
}

static bool LoadFileBytes(const std::filesystem::path& p, std::vector<unsigned char>& out)
{
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if(!f) return false;
    f.seekg(0, std::ios::end);
    std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    if(n <= 0) return false;
    out.resize((size_t)n);
    return (bool)f.read((char*)out.data(), n);
}

// --- Strategic background decoding (LEVEL_0X.bin + HMLA__0X.bin + LEVEL_0X.PAL + LEVEL_0X.CLK) ---
// Ported from spellcross_level_tool_v5.py (Pillow/Numpy) into C++/wxWidgets.

static std::filesystem::path FindFileCaseInsensitive(const std::filesystem::path& dir, const std::string& wanted)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if(!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return {};

    const std::string w = to_lower(wanted);
    for(const auto& de : fs::directory_iterator(dir, ec))
    {
        if(ec) break;
        if(!de.is_regular_file(ec))
            continue;
        const std::string fn = to_lower(de.path().filename().string());
        if(fn == w)
            return de.path();
    }
    return {};
}

static bool ExpandPaletteTo256(const std::vector<unsigned char>& palBytes, std::array<unsigned char, 256 * 3>& pal256)
{
    pal256.fill(0);
    if(palBytes.size() < 3)
        return false;

    const size_t colors = palBytes.size() / 3;
    if(colors != 32 && colors != 64 && colors != 256)
        return false;

    // Detect VGA 6-bit (0..63) values and scale to 0..255.
    unsigned char maxv = 0;
    for(size_t i = 0; i < colors * 3; ++i)
        maxv = std::max(maxv, palBytes[i]);

    const bool is_vga6 = (maxv <= 63);
    auto to8 = [&](unsigned char v) -> unsigned char {
        return is_vga6 ? (unsigned char)std::min(255, (int)v * 4) : v;
    };

    // Python tool repeats palette to fill 256 entries.
    for(size_t i = 0; i < 256; ++i)
    {
        const size_t src = (i % colors) * 3;
        pal256[i * 3 + 0] = to8(palBytes[src + 0]);
        pal256[i * 3 + 1] = to8(palBytes[src + 1]);
        pal256[i * 3 + 2] = to8(palBytes[src + 2]);
    }
    return true;
}

static bool DecodeCLK(const std::vector<unsigned char>& clkBytes, int& outW, int& outH, std::vector<unsigned char>& values)
{
    outW = 0;
    outH = 0;
    values.clear();

    if(clkBytes.size() < 4)
        return false;

    auto rd16 = [&](size_t off) -> unsigned {
        if(off + 1 >= clkBytes.size()) return 0;
        return (unsigned)clkBytes[off] | ((unsigned)clkBytes[off + 1] << 8);
    };

    // NOTE: format observed in python tool: uint16 H, uint16 W
    const unsigned H = rd16(0);
    const unsigned W = rd16(2);
    if(W == 0 || H == 0)
        return false;

    const size_t offsets_off = 4;
    const size_t offsets_size = (size_t)H * 2;
    if(offsets_off + offsets_size > clkBytes.size())
        return false;

    std::vector<unsigned> offsets;
    offsets.reserve(H);
    for(unsigned y = 0; y < H; ++y)
        offsets.push_back(rd16(offsets_off + (size_t)y * 2));

    values.assign((size_t)W * H, 0);

    for(unsigned y = 0; y < H; ++y)
    {
        const unsigned start = offsets[y];
        const unsigned end = (y + 1 < H) ? offsets[y + 1] : (unsigned)clkBytes.size();
        if(start >= clkBytes.size() || end > clkBytes.size() || end <= start)
            continue;

        size_t x = 0;
        for(unsigned i = start; i + 1 < end && x < W; i += 2)
        {
            const unsigned run_len = clkBytes[i];
            const unsigned val = clkBytes[i + 1];
            if(run_len == 0)
                continue;
            const size_t x2 = std::min((size_t)W, x + (size_t)run_len);
            std::fill(values.begin() + (size_t)y * W + x, values.begin() + (size_t)y * W + x2, (unsigned char)val);
            x = x2;
        }
    }

    outW = (int)W;
    outH = (int)H;
    return true;
}

static bool BuildStrategicCompositeFromFolder(const std::filesystem::path& folder, int levelNum, wxBitmap& outBmp)
{
    namespace fs = std::filesystem;
    outBmp = wxBitmap();
    if(levelNum < 0 || levelNum > 99)
        return false;

    const std::string lvl = wxString::Format("LEVEL_%02d.BIN", levelNum).ToStdString();
    const std::string fog = wxString::Format("HMLA__%02d.BIN", levelNum).ToStdString();
    const std::string pal = wxString::Format("LEVEL_%02d.PAL", levelNum).ToStdString();
    const std::string clk = wxString::Format("LEVEL_%02d.CLK", levelNum).ToStdString();

    fs::path pLevel = FindFileCaseInsensitive(folder, lvl);
    fs::path pFog   = FindFileCaseInsensitive(folder, fog);
    fs::path pPal   = FindFileCaseInsensitive(folder, pal);
    fs::path pClk   = FindFileCaseInsensitive(folder, clk);
    if(pLevel.empty() || pFog.empty() || pPal.empty() || pClk.empty())
        return false;

    std::vector<unsigned char> levelBytes, fogBytes, palBytes, clkBytes;
    if(!LoadFileBytes(pLevel, levelBytes) || !LoadFileBytes(pFog, fogBytes) || !LoadFileBytes(pPal, palBytes) || !LoadFileBytes(pClk, clkBytes))
        return false;

    int W = 0, H = 0;
    std::vector<unsigned char> clkValues;
    if(!DecodeCLK(clkBytes, W, H, clkValues))
        return false;

    const size_t need = (size_t)W * (size_t)H;
    if(levelBytes.size() < need || fogBytes.size() < need)
        return false;

    std::array<unsigned char, 256 * 3> pal256;
    if(!ExpandPaletteTo256(palBytes, pal256))
        return false;

    // Compose like python tool:
    //  out = fog (darkened), then LEVEL where (clk==0), plus optional region outline.
    const float fog_darken = 0.82f;

    wxImage img(W, H, true);
    img.InitAlpha();

    for(int y = 0; y < H; ++y)
    for(int x = 0; x < W; ++x)
    {
        const size_t i = (size_t)y * W + (size_t)x;
        const bool inside = (clkValues[i] == 0);
        const unsigned char idx = inside ? levelBytes[i] : fogBytes[i];

        unsigned char r = pal256[(size_t)idx * 3 + 0];
        unsigned char g = pal256[(size_t)idx * 3 + 1];
        unsigned char b = pal256[(size_t)idx * 3 + 2];

        if(!inside)
        {
            r = (unsigned char)std::clamp((int)std::lround((double)r * fog_darken), 0, 255);
            g = (unsigned char)std::clamp((int)std::lround((double)g * fog_darken), 0, 255);
            b = (unsigned char)std::clamp((int)std::lround((double)b * fog_darken), 0, 255);
        }

        img.SetRGB(x, y, r, g, b);
        img.SetAlpha(x, y, 255);
    }

    // Outline (white) where neighboring CLK values differ, limited to inside area.
    for(int y = 0; y < H; ++y)
    for(int x = 0; x < W; ++x)
    {
        const size_t i = (size_t)y * W + (size_t)x;
        if(clkValues[i] != 0)
            continue;

        bool edge = false;
        if(x > 0 && clkValues[i] != clkValues[i - 1]) edge = true;
        if(y > 0 && clkValues[i] != clkValues[i - (size_t)W]) edge = true;
        if(edge)
        {
            img.SetRGB(x, y, 255, 255, 255);
            img.SetAlpha(x, y, 255);
        }
    }

    outBmp = wxBitmap(img);
    return outBmp.IsOk();
}

static bool BuildLegacyLZBackgroundFromDef(const std::filesystem::path& defPath, wxBitmap& outBmp)
{
    namespace fs = std::filesystem;
    outBmp = wxBitmap();

    fs::path base = defPath;
    base.replace_extension();

    fs::path lz = base;  lz.replace_extension(".LZ");
    fs::path pal = base; pal.replace_extension(".PAL");

    std::vector<unsigned char> lzBytes, palBytes;
    if(!LoadFileBytes(lz, lzBytes) || !LoadFileBytes(pal, palBytes))
        return false;

    if(lzBytes.size() < 4)
        return false;

    // Best-effort:
    //  - Some .LZ are already raw (header w/h + pixels)
    //  - Some .LZ are compressed with Spellcross LZW; for those, first deLZ then read header
    const uint8_t* src = (const uint8_t*)lzBytes.data();
    size_t srcLen = lzBytes.size();

    auto rd16 = [&](const uint8_t* p, size_t off) -> unsigned {
        return (unsigned)p[off] | ((unsigned)p[off + 1] << 8);
    };

    unsigned w = 0, h = 0;
    const uint8_t* pix = nullptr;
    std::vector<uint8_t> raw;

    auto try_parse_raw = [&](const uint8_t* p, size_t len) -> bool {
        if(len < 4) return false;
        unsigned tw = rd16(p, 0);
        unsigned th = rd16(p, 2);
        if(tw == 0 || th == 0) return false;
        const size_t need = 4ull + (size_t)tw * (size_t)th;
        if(need > len) return false;
        w = tw; h = th;
        pix = p + 4;
        return true;
    };

    if(!try_parse_raw(src, srcLen))
    {
        LZWexpand delz(256 * 1024);
        raw = delz.Decode((uint8_t*)src, (uint8_t*)src + srcLen);
        if(raw.empty() || !try_parse_raw(raw.data(), raw.size()))
            return false;
    }

    std::array<unsigned char, 256 * 3> pal256;
    if(!ExpandPaletteTo256(palBytes, pal256))
        return false;

    wxImage img((int)w, (int)h, true);
    img.InitAlpha();
    for(unsigned y = 0; y < h; ++y)
    for(unsigned x = 0; x < w; ++x)
    {
        const unsigned char idx = pix[(size_t)y * w + x];
        img.SetRGB((int)x, (int)y,
            pal256[(size_t)idx * 3 + 0],
            pal256[(size_t)idx * 3 + 1],
            pal256[(size_t)idx * 3 + 2]);
        img.SetAlpha((int)x, (int)y, 255);
    }

    outBmp = wxBitmap(img);
    return outBmp.IsOk();
}

void StrategicLevelFrame::TryLoadBackground()
{
    m_hasBg = false;
    m_bgBitmap = wxBitmap();
    m_bgBitmapScaled = wxBitmap();
    m_bgScaledW = -1;
    m_bgScaledH = -1;

    namespace fs = std::filesystem;

    const fs::path defPath = fs::path(m_level.source_path);
    const std::string fnU = to_upper(defPath.filename().string());

    // Extract level number from "LEVEL_0X.DEF" (or similar) case-insensitively.
    int levelNum = -1;
    {
        std::smatch m;
        std::regex re("LEVEL[_-]?(\\d{1,2})", std::regex::icase);
        if(std::regex_search(fnU, m, re) && m.size() >= 2)
            levelNum = std::stoi(m[1].str());
    }

    wxBitmap bmp;
    if(levelNum >= 0)
    {
        // Search in reasonable places: folder of DEF, and a few parents with common subfolders.
        std::vector<fs::path> dirs;
        std::error_code ec;
        fs::path base = defPath.parent_path();
        for(int depth = 0; depth < 8 && !base.empty(); ++depth)
        {
            dirs.push_back(base);
            dirs.push_back(base / "DATA");
            dirs.push_back(base / "DATA" / "LEVEL");
            dirs.push_back(base / "DATA" / "LEVELS");
            dirs.push_back(base / "LEVEL");
            dirs.push_back(base / "LEVELS");
            dirs.push_back(base / "MAPS");
            dirs.push_back(base / "DATA" / "MAPS");
            base = base.parent_path();
        }

        // De-dup while preserving order.
        std::vector<fs::path> uniq;
        uniq.reserve(dirs.size());
        for(const auto& d : dirs)
        {
            if(d.empty()) continue;
            if(!fs::exists(d, ec) || !fs::is_directory(d, ec)) continue;
            bool seen = false;
            for(const auto& u : uniq)
                if(u == d) { seen = true; break; }
            if(!seen) uniq.push_back(d);
        }

        for(const auto& folder : uniq)
        {
            if(BuildStrategicCompositeFromFolder(folder, levelNum, bmp))
                break;
        }
    }

    if(!bmp.IsOk())
        BuildLegacyLZBackgroundFromDef(defPath, bmp);

    if(bmp.IsOk())
    {
        m_bgBitmap = bmp;
        m_hasBg = true;
    }

    if(m_mapCanvas)
        m_mapCanvas->Refresh();
    else if(m_mapPanel)
        m_mapPanel->Refresh();
}

void StrategicLevelFrame::OnMapPaint(wxPaintEvent&)
{
    wxWindow* target = m_mapCanvas ? (wxWindow*)m_mapCanvas : (wxWindow*)m_mapPanel;
    if(!target)
        return;

    wxAutoBufferedPaintDC dc(target);
    dc.Clear();

    if(m_hasBg && m_bgBitmap.IsOk())
    {
        int pw, ph;
        target->GetClientSize(&pw, &ph);

        const int bw = m_bgBitmap.GetWidth();
        const int bh = m_bgBitmap.GetHeight();
        if(pw <= 0 || ph <= 0 || bw <= 0 || bh <= 0)
            return;

        // Scale to fit panel while keeping aspect ratio.
        const double sx = (double)pw / (double)bw;
        const double sy = (double)ph / (double)bh;
        const double s  = std::min(sx, sy);
        const int dw = std::max(1, (int)std::lround((double)bw * s));
        const int dh = std::max(1, (int)std::lround((double)bh * s));

        // Cache the scaled bitmap so we don't rescale on every paint.
        if(!m_bgBitmapScaled.IsOk() || m_bgScaledW != dw || m_bgScaledH != dh)
        {
            wxImage img = m_bgBitmap.ConvertToImage();
            m_bgBitmapScaled = wxBitmap(img.Scale(dw, dh, wxIMAGE_QUALITY_NEAREST));
            m_bgScaledW = dw;
            m_bgScaledH = dh;
        }

        const int x = (pw - dw) / 2;
        const int y = (ph - dh) / 2;
        dc.DrawBitmap(m_bgBitmapScaled.IsOk() ? m_bgBitmapScaled : m_bgBitmap, x, y, false);
    }
}

void StrategicLevelFrame::OnActivate(wxActivateEvent& ev)
{
    if(ev.GetActive())
        Raise();
    ev.Skip();
}
