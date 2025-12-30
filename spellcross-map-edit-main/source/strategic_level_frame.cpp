#include "strategic_level_frame.h"

#include "main.h"

#include <wx/dcbuffer.h>
#include <wx/choicdlg.h>
#include <wx/stdpaths.h>
#include <wx/graphics.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>

static wxString U8(const char* s){ return wxString::FromUTF8(s); }
static wxString S(const std::string& s){ return wxString::FromUTF8(s.c_str()); }

wxBEGIN_EVENT_TABLE(StrategicLevelFrame, wxFrame)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_RESEARCH, StrategicLevelFrame::OnResearch)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_BUY,      StrategicLevelFrame::OnBuyUnits)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_ENDTURN,  StrategicLevelFrame::OnEndTurn)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_LAUNCH,   StrategicLevelFrame::OnLaunch)
wxEND_EVENT_TABLE()

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
    : wxFrame(parent, wxID_ANY, U8(u8"Strategick\u00e1 mapa"), wxDefaultPosition, wxSize(1100, 700)),
      m_main(parent),
      m_level(level)
{
    // init territory mission state from LevelData
    for(const auto& t : m_level.territories)
    {
        m_territoryCurrentMission[t.id] = t.mission;
        m_territoryLaunchCount[t.id] = 0;
    }

    BuildUI();
    TryLoadBackground();
    RefreshUI();

    Bind(wxEVT_SIZE, &StrategicLevelFrame::OnSize, this);
    Bind(wxEVT_SHOW, [this](wxShowEvent& e){ if(e.IsShown()){ Raise(); SetFocus(); } e.Skip(); });
    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& e){ if(e.GetActive()){ Raise(); SetFocus(); } e.Skip(); });
}

void StrategicLevelFrame::BuildUI()
{
    // Force Windows "System" font (legacy), fallback to MS Shell Dlg 2
    wxFont f;
    if(!f.SetFaceName("System"))
        f = wxFontInfo(9).FaceName("MS Shell Dlg 2");
    f.SetPointSize(9);
    SetFont(f);

    // Main content panels (will be positioned by anchors)
    m_mapPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_bottomPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_sidePanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_toolbarPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

    m_mapPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_mapPanel->Bind(wxEVT_PAINT, &StrategicLevelFrame::OnMapPaint, this);

    // bottom
    m_bottomPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_bottomPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&){
        wxAutoBufferedPaintDC dc(m_bottomPanel);
        dc.Clear();
        dc.SetBrush(*wxBLACK_BRUSH);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(m_bottomPanel->GetClientRect());
    });

    m_log = new wxTextCtrl(m_bottomPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
    m_log->SetBackgroundColour(*wxBLACK);
    m_log->SetForegroundColour(wxColour(140,255,140));
    auto bs = new wxBoxSizer(wxVERTICAL);
    bs->Add(m_log, 1, wxEXPAND | wxALL, 8);
    m_bottomPanel->SetSizer(bs);

    // side: roster + buttons at bottom (styled)
    m_sidePanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_sidePanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&){
        wxAutoBufferedPaintDC dc(m_sidePanel);
        dc.Clear();
        dc.SetBrush(*wxBLACK_BRUSH);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(m_sidePanel->GetClientRect());
    });

    m_roster = new wxListCtrl(m_sidePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxBORDER_NONE);
    m_roster->SetBackgroundColour(*wxBLACK);
    m_roster->SetTextColour(wxColour(140,255,140));
    m_roster->InsertColumn(0, U8(u8"Jednotka"), wxLIST_FORMAT_LEFT, 180);
    m_roster->InsertColumn(1, U8(u8"Po\u010det"), wxLIST_FORMAT_LEFT, 70);
    m_roster->InsertColumn(2, U8(u8"HP"), wxLIST_FORMAT_LEFT, 60);

    m_btnResearch = new wxButton(m_sidePanel, ID_BTN_RESEARCH, U8(u8"V\u00fdzkum"));
    m_btnBuy      = new wxButton(m_sidePanel, ID_BTN_BUY,      U8(u8"N\u00e1kup"));
    m_btnLaunch   = new wxButton(m_sidePanel, ID_BTN_LAUNCH,   U8(u8"Spustit"));
    m_btnEndTurn  = new wxButton(m_sidePanel, ID_BTN_ENDTURN,  U8(u8"Kolo"));

    auto ss = new wxBoxSizer(wxVERTICAL);
    ss->Add(m_roster, 1, wxEXPAND | wxALL, 8);
    auto btnRow = new wxBoxSizer(wxHORIZONTAL);
    btnRow->Add(m_btnResearch, 1, wxRIGHT, 6);
    btnRow->Add(m_btnBuy, 1, wxRIGHT, 6);
    btnRow->Add(m_btnLaunch, 1, wxRIGHT, 6);
    btnRow->Add(m_btnEndTurn, 1);
    ss->Add(btnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    m_sidePanel->SetSizer(ss);

    // toolbar panel: black for now (icons can be owner-drawn later)
    m_toolbarPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_toolbarPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&){
        wxAutoBufferedPaintDC dc(m_toolbarPanel);
        dc.Clear();
        dc.SetBrush(*wxBLACK_BRUSH);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(m_toolbarPanel->GetClientRect());
    });

    // Overlay panel on top
    LoadUiOverlay();
    m_overlayPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxBORDER_NONE | wxTRANSPARENT_WINDOW);
    m_overlayPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_overlayPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxBORDER_NONE | wxTRANSPARENT_WINDOW);
    m_overlayPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&){
        wxAutoBufferedPaintDC dc(m_overlayPanel);
        dc.Clear();
        PaintUiOverlay(dc);
        PaintHud(dc);
    });
    m_overlayPanel->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&){});

    LayoutAnchors();
    m_overlayPanel->Raise();
}

void StrategicLevelFrame::OnSize(wxSizeEvent&)
{
    LayoutAnchors();
    if(m_overlayPanel) m_overlayPanel->Refresh();
    if(m_mapPanel) m_mapPanel->Refresh();
}

// Normalized anchors from 2048x1365 transparent layout
static wxRect ScaleRect(const wxSize& sz, int x, int y, int w, int h)
{
    const double sx = sz.x / 2048.0;
    const double sy = sz.y / 1365.0;
    return wxRect((int)std::lround(x*sx), (int)std::lround(y*sy), (int)std::lround(w*sx), (int)std::lround(h*sy));
}

void StrategicLevelFrame::LayoutAnchors()
{
    wxSize sz = GetClientSize();
    if(sz.x <= 0 || sz.y <= 0) return;

    // These numbers match the holes you drew (adjust if needed)
    wxRect rcMap   = ScaleRect(sz,  84,  44, 1184, 756);
    wxRect rcBot   = ScaleRect(sz,  92, 948, 1176, 360);
    wxRect rcSide  = ScaleRect(sz, 1352,  76,  360, 1140);
    wxRect rcTool  = ScaleRect(sz, 1840, 364,  148,  824);

    if(m_mapPanel) m_mapPanel->SetSize(rcMap);
    if(m_bottomPanel) m_bottomPanel->SetSize(rcBot);
    if(m_sidePanel) m_sidePanel->SetSize(rcSide);
    if(m_toolbarPanel) m_toolbarPanel->SetSize(rcTool);
    if(m_overlayPanel) m_overlayPanel->SetSize(wxRect(0,0,sz.x,sz.y));

    // Layout child sizers
    if(m_bottomPanel) m_bottomPanel->Layout();
    if(m_sidePanel) m_sidePanel->Layout();
}

void StrategicLevelFrame::RefreshUI()
{
    if(!m_roster) return;

    m_roster->DeleteAllItems();
    for(size_t i = 0; i < m_level.start_units.size(); ++i)
    {
        const auto& u = m_level.start_units[i];
        long idx = m_roster->InsertItem((long)i, wxString::Format("%d", u.unit_id));
        m_roster->SetItem(idx, 1, wxString::Format("%d", u.count));
        m_roster->SetItem(idx, 2, wxString::Format("%d", u.health));
    }

    m_roster->SetColumnWidth(0, wxLIST_AUTOSIZE);
    m_roster->SetColumnWidth(1, wxLIST_AUTOSIZE);
    m_roster->SetColumnWidth(2, wxLIST_AUTOSIZE);

    if(m_btnLaunch)
        m_btnLaunch->Enable(m_selectedTerritory >= 0);

    if(m_overlayPanel)
        m_overlayPanel->Refresh();
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
    info << U8(u8"Mise: ") << S(t.mission) << "\n";
    info << U8(u8"Intro: ") << S(t.intro_mission) << "\n";
    info << U8(u8"Hudba: ") << S(t.music) << "\n";
    info << wxString::Format("Strategic point: %d,%d\n", t.strategic_x, t.strategic_y);

    wxMessageBox(info, U8(u8"\u00dazem\u00ed"), wxOK | wxICON_INFORMATION, this);
    RefreshUI();
}

void StrategicLevelFrame::OnResearch(wxCommandEvent&)
{
    if(m_money >= 100) {
        m_money -= 100;
        m_research += 1;
    } else {
        wxMessageBox(U8(u8"Nedostatek pen\u011bz (demo cena 100)."), U8(u8"V\u00fdzkum"), wxOK | wxICON_WARNING, this);
    }
    RefreshUI();
}

void StrategicLevelFrame::OnBuyUnits(wxCommandEvent&)
{
    wxMessageBox(U8(u8"N\u00e1kup jednotek (stub)."), U8(u8"N\u00e1kup"), wxOK | wxICON_INFORMATION, this);
}

const LevelMission* StrategicLevelFrame::FindMissionByNameUpper(const std::string& name_upper) const
{
    for(const auto& m : m_level.missions)
        if(to_upper(m.name) == name_upper)
            return &m;
    return nullptr;
}

std::string StrategicLevelFrame::ResolveMissionTokenForTerritory(int territory_id) const
{
    int launches = 0;
    auto itL = m_territoryLaunchCount.find(territory_id);
    if(itL != m_territoryLaunchCount.end()) launches = itL->second;

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

    fs::path pExact1 = base / (to_upper(mission_token) + ".DEF");
    fs::path pExact2 = base / (mission_token + ".DEF");
    fs::path pExact3 = base / (mission_token + ".def");

    if(fs::exists(pExact1)) return pExact1.wstring();
    if(fs::exists(pExact2)) return pExact2.wstring();
    if(fs::exists(pExact3)) return pExact3.wstring();

    std::string up = to_upper(mission_token);

    std::vector<fs::path> candidates;
    for(const auto& ent : fs::directory_iterator(base))
    {
        if(!ent.is_regular_file()) continue;
        auto ext = to_upper(ent.path().extension().string());
        if(ext != ".DEF") continue;

        std::string stem = to_upper(ent.path().stem().string());
        if(stem.rfind(up, 0) == 0)
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

    wxSingleChoiceDialog dlg(const_cast<StrategicLevelFrame*>(this),
        U8(u8"Nalezeno v\u00edce variant mise. Kterou na\u010d\u00edst?"),
        U8(u8"Vybrat misi"), choices);

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
        wxMessageBox(U8(u8"Nenalezen map DEF pro misi: ") + S(token), U8(u8"Spustit"), wxOK | wxICON_WARNING, this);
        return;
    }

    if(!m_main->LoadMapFromDefPath(defPath))
        return;

    m_territoryLaunchCount[terr_id] += 1;

    const std::string upperName = to_upper(token);
    if(const LevelMission* m = FindMissionByNameUpper(upperName))
        if(!m->end_ok_mission.empty() && m->end_ok_mission != "none")
            m_territoryCurrentMission[terr_id] = to_lower(m->end_ok_mission);

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

    namespace fs = std::filesystem;

    fs::path base = fs::path(m_level.source_path);
    base.replace_extension();

    fs::path lz = base;  lz.replace_extension(".LZ");
    fs::path pal = base; pal.replace_extension(".PAL");

    std::vector<unsigned char> lzBytes, palBytes;
    if(!LoadFileBytes(lz, lzBytes) || !LoadFileBytes(pal, palBytes))
        return;

    if(palBytes.size() != 192 || lzBytes.size() < 4)
        return;

    auto rd16 = [&](size_t off) -> unsigned {
        return (unsigned)lzBytes[off] | ((unsigned)lzBytes[off + 1] << 8);
    };

    unsigned w = rd16(0);
    unsigned h = rd16(2);
    const size_t need = 4ull + (size_t)w * (size_t)h;
    if(w == 0 || h == 0 || need > lzBytes.size())
        return;

    wxImage img((int)w, (int)h);
    unsigned char* rgb = img.GetData();
    const unsigned char* pix = lzBytes.data() + 4;

    for(unsigned y = 0; y < h; ++y)
    for(unsigned x = 0; x < w; ++x)
    {
        unsigned idx = pix[y * w + x] & 0x3F;
        unsigned char r = palBytes[idx * 3 + 0];
        unsigned char g = palBytes[idx * 3 + 1];
        unsigned char b = palBytes[idx * 3 + 2];

        size_t o = ((size_t)y * w + x) * 3;
        rgb[o + 0] = r;
        rgb[o + 1] = g;
        rgb[o + 2] = b;
    }

    img.Rescale(300, 150, wxIMAGE_QUALITY_BICUBIC);
    m_bgBitmap = wxBitmap(img);
    m_hasBg = m_bgBitmap.IsOk();
}

void StrategicLevelFrame::OnMapPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(m_mapPanel);
    dc.Clear();

    dc.SetBrush(*wxBLACK_BRUSH);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(m_mapPanel->GetClientRect());

    if(m_hasBg)
    {
        wxSize sz = m_mapPanel->GetClientSize();
        wxSize bsz = m_bgBitmap.GetSize();
        int x = (sz.x - bsz.x) / 2;
        int y = (sz.y - bsz.y) / 2;
        dc.DrawBitmap(m_bgBitmap, x, y, false);
    }

    // Territory buttons: for now keep the old grid as an overlay, but style it minimal.
    // (We'll replace with clickable regions later.)
    // If you still want buttons, create them elsewhere; painting only here.
}

void StrategicLevelFrame::LoadUiOverlay()
{
    // resources/Spell_new_ui_vectorized_transparent.png (next to EXE)
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    wxString p = exe.GetPathWithSep() + "resources" + wxFILE_SEP_PATH + "Spell_new_ui_vectorized_transparent.png";

    wxImage img;
    if(!img.LoadFile(p))
    {
        m_overlayOk = false;
        wxMessageBox(U8(u8"Nelze na\u010d\u00edst UI overlay: ") + p,
                     U8(u8"Chyba"), wxOK | wxICON_WARNING, this);
        return;
    }

    m_overlaySrc = wxBitmap(img);
    m_overlayOk = m_overlaySrc.IsOk();
    m_overlayScaled = wxBitmap();
    m_overlayScaledSize = wxSize();
}

void StrategicLevelFrame::PaintUiOverlay(wxDC& dc)
{
    if(!m_overlayOk) return;

    wxSize sz = GetClientSize();
    if(sz.x <= 0 || sz.y <= 0) return;

    if(!m_overlayScaled.IsOk() || m_overlayScaledSize != sz)
    {
        wxImage img = m_overlaySrc.ConvertToImage();
        img.Rescale(sz.x, sz.y, wxIMAGE_QUALITY_BICUBIC);
        m_overlayScaled = wxBitmap(img);
        m_overlayScaledSize = sz;
    }

    dc.DrawBitmap(m_overlayScaled, 0, 0, true);
}

void StrategicLevelFrame::PaintHud(wxDC& dc)
{
    // HUD text goes into the top-right green window area (scaled)
    wxSize sz = GetClientSize();
    if(sz.x <= 0 || sz.y <= 0) return;

    wxRect rcHud = ScaleRect(sz, 1834, 82, 151, 268);

    auto gc = wxGraphicsContext::Create(&dc);
    if(!gc) return;

    wxFont f = GetFont();
    f.SetPointSize(11);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    gc->SetFont(f, wxColour(80, 255, 80));

    int x = rcHud.x + 18;
    int y = rcHud.y + 18;
    int lh = 26;

    gc->DrawText(wxString::Format(U8(u8"Pen\u00edze! %d"), m_money), x, y); y += lh;
    gc->DrawText(wxString::Format(U8(u8"V\u00fdzkum %d"), m_research), x, y); y += lh;
    gc->DrawText(wxString::Format(U8(u8"Kolo %d"), m_turn), x, y);

    delete gc;
}
