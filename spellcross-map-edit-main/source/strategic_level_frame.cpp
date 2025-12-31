#include "strategic_level_frame.h"

#include "main.h"

#include <wx/dcbuffer.h>
#include <wx/choicdlg.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
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
wxEND_EVENT_TABLE()

static wxString fmt_int(const wxString& label, int v)
{
    return wxString::Format("%s %d", label, v);
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

StrategicLevelFrame::StrategicLevelFrame(MainFrame* parent, const LevelData& level)
    : wxFrame(parent, wxID_ANY, "Strategic Level", wxDefaultPosition, wxSize(1100, 700)),
      m_main(parent),
      m_level(level)
{
    m_money = 0;
    m_research = 0;

    // init territory mission state from LevelData
    for(const auto& t : m_level.territories)
    {
        m_territoryCurrentMission[t.id] = t.mission;
        m_territoryLaunchCount[t.id] = 0;
    }

    BuildUI();
    TryLoadBackground();
    RefreshUI();
}

void StrategicLevelFrame::BuildUI()
{
    auto root = new wxPanel(this);

    // --- Top bar (Money / Research / Turn) ---
    auto topBar = new wxPanel(root);
    topBar->SetBackgroundColour(wxColour(20, 70, 20));
    auto topSizer = new wxBoxSizer(wxHORIZONTAL);

    m_lblMoney = new wxStaticText(topBar, wxID_ANY, "Money 0");
    m_lblResearch = new wxStaticText(topBar, wxID_ANY, "Research 0");
    m_lblTurn = new wxStaticText(topBar, wxID_ANY, "Turn 1");

    auto font = m_lblMoney->GetFont();
    font.SetPointSize(font.GetPointSize() + 3);
    font.SetWeight(wxFONTWEIGHT_BOLD);
    m_lblMoney->SetFont(font);
    m_lblResearch->SetFont(font);
    m_lblTurn->SetFont(font);

    m_lblMoney->SetForegroundColour(*wxWHITE);
    m_lblResearch->SetForegroundColour(*wxWHITE);
    m_lblTurn->SetForegroundColour(*wxWHITE);

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
    m_mapPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_mapPanel->Bind(wxEVT_PAINT, &StrategicLevelFrame::OnMapPaint, this);

    m_mapSizer = new wxBoxSizer(wxVERTICAL);

    auto mapTitle = new wxStaticText(m_mapPanel, wxID_ANY, "Strategic map");
    mapTitle->SetForegroundColour(*wxLIGHT_GREY);
    m_mapSizer->Add(mapTitle, 0, wxALL, 8);

    // Grid pro territory buttony (zatim overlay nad mapou)
    auto grid = new wxGridSizer(0, 4, 6, 6);
    for(size_t i = 0; i < m_level.territories.size(); ++i)
    {
        const auto& t = m_level.territories[i];
        auto id = ID_TERRITORY_BASE + (int)i;

        wxString label = wxString::Format("T%02d\n%s", t.id, t.mission);
        auto btn = new wxButton(m_mapPanel, id, label, wxDefaultPosition, wxSize(140, 60));
        btn->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnTerritory, this);
        grid->Add(btn, 0, wxEXPAND);
    }

    m_mapSizer->Add(grid, 1, wxALL | wxEXPAND, 8);
    m_mapPanel->SetSizer(m_mapSizer);

    // Right panel: roster + actions
    auto right = new wxPanel(root);
    right->SetBackgroundColour(wxColour(10, 50, 10));
    auto rightSizer = new wxBoxSizer(wxVERTICAL);

    auto rosterTitle = new wxStaticText(right, wxID_ANY, "Forces / Hierarchy");
    rosterTitle->SetForegroundColour(*wxWHITE);
    rosterTitle->SetFont(font);
    rightSizer->Add(rosterTitle, 0, wxALL, 10);

    m_roster = new wxListCtrl(right, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_roster->InsertColumn(0, "Unit");
    m_roster->InsertColumn(1, "Count");
    m_roster->InsertColumn(2, "HP");
    rightSizer->Add(m_roster, 1, wxALL | wxEXPAND, 10);

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
    m_lblMoney->SetLabel(fmt_int("Money", m_money));
    m_lblResearch->SetLabel(fmt_int("Research", m_research));
    m_lblTurn->SetLabel(fmt_int("Turn", m_turn));

    m_roster->DeleteAllItems();
    for(size_t i = 0; i < m_level.start_units.size(); ++i)
    {
        const auto& u = m_level.start_units[i];
        long idx = m_roster->InsertItem((long)i, wxString::Format("%d", u.unit_id));
        m_roster->SetItem(idx, 1, wxString::Format("%d", u.count));
        m_roster->SetItem(idx, 2, wxString::Format("%d", u.health));
    }

    m_roster->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);
    m_roster->SetColumnWidth(1, wxLIST_AUTOSIZE_USEHEADER);
    m_roster->SetColumnWidth(2, wxLIST_AUTOSIZE_USEHEADER);

    m_btnLaunch->Enable(m_selectedTerritory >= 0);
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

    wxMessageBox(info, "Territory", wxOK | wxICON_INFORMATION, this);
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
    RefreshUI();
}

void StrategicLevelFrame::OnBuyUnits(wxCommandEvent&)
{
    wxMessageBox("Unit shop stub.\n\nSem pozdeji napojime ceny a availability podle research flagu.",
                 "Buy units", wxOK | wxICON_INFORMATION, this);
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
    for(const auto& ent : fs::directory_iterator(base))
    {
        if(!ent.is_regular_file()) continue;
        auto ext = to_upper(ent.path().extension().string());
        if(ext != ".DEF") continue;

        std::string stem = to_upper(ent.path().stem().string());
        if(stem.rfind(up, 0) == 0) // starts with
            candidates.push_back(ent.path());
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

    if(!m_main->LoadMapFromDefPath(defPath))
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
    RefreshUI();
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

void StrategicLevelFrame::TryLoadBackground()
{
    m_hasBg = false;
    m_bgBitmap = wxBitmap();
    m_bgBitmapScaled = wxBitmap();
    m_bgScaledW = -1;
    m_bgScaledH = -1;

    namespace fs = std::filesystem;

    fs::path base = fs::path(m_level.source_path);
    base.replace_extension();

    fs::path lz = base;  lz.replace_extension(".LZ");
    fs::path pal = base; pal.replace_extension(".PAL");

    std::vector<unsigned char> lzBytes, palBytes;
    if(!LoadFileBytes(lz, lzBytes) || !LoadFileBytes(pal, palBytes))
        return;

    // Spellcross palettes are commonly 64*3 = 192 bytes
    if(palBytes.size() != 192 || lzBytes.size() < 4)
        return;

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

    // 1) try direct/raw first (keeps old behavior)
    if(!try_parse_raw(src, srcLen))
    {
        // 2) try Spellcross LZW decode (only if LZ_spell.cpp is linked in)
        LZWexpand delz(256 * 1024);
        raw = delz.Decode((uint8_t*)src, (uint8_t*)src + srcLen);
        if(raw.empty() || !try_parse_raw(raw.data(), raw.size()))
            return;
    }

    // Palette is typically 6-bit VGA (0..63). Scale up for proper colors.
    auto vga6_to_8 = [](unsigned char v) -> unsigned char {
        // 0..63 -> 0..252 (classic VGA6 scaling)
        return (unsigned char)std::min(255, (int)v * 4);
    };

    wxImage img((int)w, (int)h);
    unsigned char* rgb = img.GetData();

    for(unsigned y = 0; y < h; ++y)
    for(unsigned x = 0; x < w; ++x)
    {
        unsigned idx = pix[y * w + x] & 0x3F;
        unsigned char r = vga6_to_8(palBytes[idx * 3 + 0]);
        unsigned char g = vga6_to_8(palBytes[idx * 3 + 1]);
        unsigned char b = vga6_to_8(palBytes[idx * 3 + 2]);

        size_t o = ((size_t)y * w + x) * 3;
        rgb[o + 0] = r;
        rgb[o + 1] = g;
        rgb[o + 2] = b;
    }

    m_bgBitmap = wxBitmap(img);
    m_hasBg = m_bgBitmap.IsOk();
    if(m_mapPanel) m_mapPanel->Refresh();
}

void StrategicLevelFrame::OnMapPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(m_mapPanel);
    dc.Clear();

    if(m_hasBg && m_bgBitmap.IsOk())
    {
        int pw, ph;
        m_mapPanel->GetClientSize(&pw, &ph);

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
