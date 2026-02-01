#include "form_level.h"

#include "main.h"
#include "other.h"

#include <wx/dcbuffer.h>
#include <wx/choicdlg.h>
#include <wx/spinctrl.h>
#include <wx/dcscreen.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstdio>
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
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_SELL,     StrategicLevelFrame::OnSellUnits)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_ENDTURN,  StrategicLevelFrame::OnEndTurn)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_LAUNCH,   StrategicLevelFrame::OnLaunch)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_STRATEGIC_MAP, StrategicLevelFrame::OnShowStrategicMap)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_HIERARCHY, StrategicLevelFrame::OnShowHierarchy)
    EVT_BUTTON(StrategicLevelFrame::ID_BTN_STATS, StrategicLevelFrame::OnShowStats)
wxEND_EVENT_TABLE()

// UI-only: readonly text panel under the territory grid (instead of popups)
static const int ID_TERRITORY_TEXTBOX = wxID_HIGHEST + 2201;

struct StrategicTextSpan
{
    wxString text;
    wxColour color;
    const wxFont* font = nullptr;
};

static wxString StrategicFontFaceName()
{
    return wxString::FromUTF8("Fixedsys Excelsior 3.01");
}

static std::filesystem::path StrategicFontPath()
{
    return std::filesystem::current_path() / "data" / "font.ttf";
}

static void EnsureStrategicFontLoaded()
{
    static bool loaded = false;
    if(loaded)
        return;

    const auto fontPath = StrategicFontPath();
    if(std::filesystem::exists(fontPath))
        wxFont::AddPrivateFont(wxString::FromUTF8(fontPath.string()));
    loaded = true;
}

static wxFont MakeStrategicFont(int pixelSize, bool bold)
{
    EnsureStrategicFontLoaded();

    wxFont font(wxFontInfo(wxSize(0, pixelSize))
        .Family(wxFONTFAMILY_MODERN)
        .Style(wxFONTSTYLE_NORMAL)
        .Weight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL));
    font.SetFaceName(StrategicFontFaceName());
    font.SetPixelSize(wxSize(0, pixelSize));

    if(!font.IsOk())
    {
        font = wxFont(wxFontInfo(wxSize(0, pixelSize))
            .Family(wxFONTFAMILY_MODERN)
            .Style(wxFONTSTYLE_NORMAL)
            .Weight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL));
        font.SetPixelSize(wxSize(0, pixelSize));
    }

    return font;
}

static wxBitmap RenderStrategicLabel(const std::vector<StrategicTextSpan>& spans, const wxFont& fallbackFont,
                                     const wxColour& shadow, const wxColour* background = nullptr)
{
    if(spans.empty())
        return wxBitmap(1, 1);

    wxScreenDC measure;
    int totalW = 0;
    int maxH = 0;
    std::vector<wxSize> extents;
    extents.reserve(spans.size());

    for(const auto& span : spans)
    {
        const wxFont& font = span.font ? *span.font : fallbackFont;
        measure.SetFont(font);
        int w = 0;
        int h = 0;
        measure.GetTextExtent(span.text, &w, &h);
        extents.emplace_back(w, h);
        totalW += w;
        maxH = std::max(maxH, h);
    }

    const int paddingX = 6;
    const int paddingY = 4;
    int bmpW = std::max(1, totalW + paddingX * 2);
    int bmpH = std::max(1, maxH + paddingY * 2);

    wxBitmap bmp(bmpW, bmpH, 32);
    bmp.UseAlpha();
    wxMemoryDC dc(bmp);
    dc.SetBackground(wxBrush(wxColour(0, 0, 0, 0)));
    dc.Clear();

    if(background)
    {
        dc.SetPen(*background);
        dc.SetBrush(*background);
        dc.DrawRectangle(0, 0, bmpW, bmpH);
    }

    const int shadowOffset = 1;
    int x = paddingX;
    for(size_t i = 0; i < spans.size(); ++i)
    {
        const auto& span = spans[i];
        const wxFont& font = span.font ? *span.font : fallbackFont;
        dc.SetFont(font);
        const int y = paddingY + (maxH - extents[i].GetHeight()) / 2;
        dc.SetTextForeground(shadow);
        dc.DrawText(span.text, x + shadowOffset, y + shadowOffset);
        x += extents[i].GetWidth();
    }

    x = paddingX;
    for(size_t i = 0; i < spans.size(); ++i)
    {
        const auto& span = spans[i];
        const wxFont& font = span.font ? *span.font : fallbackFont;
        dc.SetFont(font);
        const int y = paddingY + (maxH - extents[i].GetHeight()) / 2;
        dc.SetTextForeground(span.color);
        dc.DrawText(span.text, x, y);
        x += extents[i].GetWidth();
    }

    dc.SelectObject(wxNullBitmap);
    return bmp;
}

static void UpdateStrategicLabel(wxStaticBitmap* target, const std::vector<StrategicTextSpan>& spans,
                                 const wxFont& fallbackFont, const wxColour& shadow, const wxColour* background = nullptr)
{
    if(!target)
        return;

    if(auto bmp = RenderStrategicLabel(spans, fallbackFont, shadow, background); bmp.IsOk())
        target->SetBitmap(bmp);
}

static wxStaticBitmap* CreateStrategicLabel(wxWindow* parent, const std::vector<StrategicTextSpan>& spans,
                                            const wxFont& fallbackFont, const wxColour& shadow, const wxColour* background = nullptr)
{
    auto* b = new wxStaticBitmap(parent, wxID_ANY, wxBitmap(1, 1));
    UpdateStrategicLabel(b, spans, fallbackFont, shadow, background);
    return b;
}

static wxStaticBitmap* CreateStrategicLabel(wxWindow* parent, const wxString& text, const wxFont& font,
                                            const wxColour& color, const wxColour& shadow)
{
    std::vector<StrategicTextSpan> spans = { { text, color, &font } };
    return CreateStrategicLabel(parent, spans, font, shadow);
}

static wxBitmapButton* CreateStrategicButton(wxWindow* parent, int id, const wxString& text,
                                             const wxFont& font, const wxColour& textColor,
                                             const wxColour& shadow, const wxColour& background,
                                             const wxSize& minSize = wxDefaultSize)
{
    std::vector<StrategicTextSpan> spans = { { text, textColor, &font } };
    wxBitmap bmp = RenderStrategicLabel(spans, font, shadow, &background);
    if(!bmp.IsOk())
        bmp = wxBitmap(1, 1);

    auto* b = new wxBitmapButton(parent, id, bmp);
    if(minSize != wxDefaultSize)
        b->SetMinSize(minSize);

    b->SetBackgroundColour(background);
    return b;
}



static std::filesystem::path GetUnitsJsonPath()
{
    return std::filesystem::current_path() / "data" / "units.json";
}

static bool ParseJsonIntField(const std::string& obj, const char* key, int& outValue)
{
    if(!key)
        return false;

    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if(pos == std::string::npos)
        return false;

    pos = obj.find(':', pos + needle.size());
    if(pos == std::string::npos)
        return false;

    ++pos;
    while(pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos])))
        ++pos;

    const char* start = obj.c_str() + pos;
    char* end = nullptr;
    long value = std::strtol(start, &end, 10);
    if(end == start)
        return false;

    outValue = static_cast<int>(value);
    return true;
}


static bool ParseJsonStringField(const std::string& obj, const char* key, std::string& outValue)
{
    if(!key)
        return false;

    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if(pos == std::string::npos)
        return false;

    pos = obj.find(':', pos + needle.size());
    if(pos == std::string::npos)
        return false;

    ++pos;
    while(pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos])))
        ++pos;

    if(pos >= obj.size() || obj[pos] != '"')
        return false;
    ++pos;

    std::string s;
    bool escaped = false;
    for(; pos < obj.size(); ++pos)
    {
        char c = obj[pos];
        if(escaped)
        {
            switch(c)
            {
                case '"': case '\\': case '/': s.push_back(c); break;
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                default: s.push_back(c); break;
            }
            escaped = false;
            continue;
        }
        if(c == '\\')
        {
            escaped = true;
            continue;
        }
        if(c == '"')
            break;
        s.push_back(c);
    }

    outValue = std::move(s);
    return true;
}



static std::string EscapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for(char c : s)
    {
        switch(c)
        {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

static std::string NowIsoLocal()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}



static wxString RankNameCz(int rank)
{
    switch(rank)
    {
        case 0: return wxString(L"Lieutenant");
        case 1: return wxString(L"First Lieutenant");
        case 2: return wxString(L"Captain");
        case 3: return wxString(L"Major");
        case 4: return wxString(L"Lieutenant Colonel");
        case 5: return wxString(L"Colonel");
        case 6: return wxString(L"Major General");
        case 7: return wxString(L"Lieutenant General");
        case 8: return wxString(L"General");
        default: return wxString::Format(L"Hodnost %d", rank);
    }
}

static bool LoadUnitCostsFromJson(const std::filesystem::path& path, std::unordered_map<int, int>& outCosts)
{
    std::ifstream file(path);
    if(!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();
    if(content.empty())
        return false;

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    size_t objStart = std::string::npos;

    for(size_t i = 0; i < content.size(); ++i)
    {
        char c = content[i];
        if(inString)
        {
            if(escaped)
            {
                escaped = false;
            }
            else if(c == '\\')
            {
                escaped = true;
            }
            else if(c == '"')
            {
                inString = false;
            }
            continue;
        }

        if(c == '"')
        {
            inString = true;
            continue;
        }

        if(c == '{')
        {
            if(depth == 0)
                objStart = i;
            ++depth;
        }
        else if(c == '}')
        {
            if(depth > 0)
            {
                --depth;
                if(depth == 0 && objStart != std::string::npos)
                {
                    const std::string obj = content.substr(objStart, i - objStart + 1);
                    int index = -1;
                    int cost = -1;
                    if(ParseJsonIntField(obj, "index", index) && ParseJsonIntField(obj, "cost_buy", cost))
                    {
                        outCosts[index] = cost;
                    }
                    objStart = std::string::npos;
                }
            }
        }
    }

    return !outCosts.empty();
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
    : wxFrame(parent, wxID_ANY, "Strategic Level", wxDefaultPosition, wxSize(1320, 820),
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

    BuildMenu();

        BuildUI();          // UI must exist before we start selecting territories.
    TryLoadBackground();

        LoadStrategicState();  // May call SelectTerritoryById -> OnTerritory.
    RefreshUI();

    Bind(wxEVT_ACTIVATE, &StrategicLevelFrame::OnActivate, this);

}



// Forward declarations (definitions are later in this file)
static bool LoadStrategicStateFile(
    const std::filesystem::path& path,
    const LevelData& level,
    int& turn,
    int& money,
    int& research,
    int& selected_territory,
    StrategicLevelFrame::PlayerProgress& player,
    std::unordered_map<int, std::string>& territoryMission,
    std::unordered_map<int, int>& territoryLaunchCount,
    std::vector<LevelData::PlayerUnitAdd>& units,
    std::string* out_level_def,
    std::string* out_timestamp);

static void SaveStrategicStateFile(
    const std::filesystem::path& path,
    const LevelData& level,
    int turn,
    int money,
    int research,
    int selected_territory,
    const StrategicLevelFrame::PlayerProgress& player,
    const std::unordered_map<int, std::string>& territoryMission,
    const std::unordered_map<int, int>& territoryLaunchCount,
    const std::vector<LevelData::PlayerUnitAdd>& units,
    const std::string& timestamp);

void StrategicLevelFrame::BuildMenu()
{
    // Only build once
    if(GetMenuBar() != nullptr)
        return;

    auto* bar = new wxMenuBar();
    auto* file = new wxMenu();

    file->Append(ID_MENU_SAVE_GAME, (L"&Save game...\tCtrl+S"));
    file->Append(ID_MENU_LOAD_GAME, (L"&Load game...\tCtrl+L"));

    bar->Append(file, "&File");
    SetMenuBar(bar);

    Bind(wxEVT_MENU, &StrategicLevelFrame::OnSaveGame, this, ID_MENU_SAVE_GAME);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnLoadGame, this, ID_MENU_LOAD_GAME);
}

static std::filesystem::path GetStrategicSaveSlotPath(const LevelData& level, int slot)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::path(level.source_path).parent_path();
    if(base.empty() || !fs::exists(base, ec))
        base = fs::current_path(ec);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "strategic_save_%02d.json", slot);
    return base / buf;
}

static bool PeekStrategicSaveSummary(const std::filesystem::path& path, int& outMoney, int& outRank, int& outExp, std::string& outTs)
{
    outMoney = 0; outRank = 0; outExp = 0; outTs.clear();

    std::ifstream f(path);
    if(!f)
        return false;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if(data.empty())
        return false;

    std::smatch m;
    if(std::regex_search(data, m, std::regex("\"money\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        outMoney = std::stoi(m[1].str());

    // timestamp optional
    std::regex ts_re("\"timestamp\"\\s*:\\s*\"([^\"]*)\"");
    if(std::regex_search(data, m, ts_re) && m.size() > 1)
        outTs = m[1].str();

    // player object optional
    std::regex player_obj_re("\"player\"\\s*:\\s*\\{([^}]*)\\}");
    if(std::regex_search(data, m, player_obj_re) && m.size() > 1)
    {
        const std::string pobj = m[1].str();
        (void)ParseJsonIntField(pobj, "rank", outRank);
        (void)ParseJsonIntField(pobj, "experience", outExp);
    }

    return true;
}

void StrategicLevelFrame::OnSaveGame(wxCommandEvent&)
{
    wxArrayString choices;
    choices.reserve(10);

    for(int i = 1; i <= 10; ++i)
    {
        const auto p = GetStrategicSaveSlotPath(m_level, i);
        std::error_code ec;
        if(std::filesystem::exists(p, ec))
        {
            int money=0, rank=0, xp=0;
            std::string ts;
            PeekStrategicSaveSummary(p, money, rank, xp, ts);
            wxString line = wxString::Format("Slot %02d  |  %s  |  $%d  |  XP %d  |  %s",
                i,
                ts.empty() ? wxString(L"(no time)") : wxString::FromUTF8(ts),
                money,
                xp,
                RankNameCz(rank));
            choices.Add(line);
        }
        else
        {
            choices.Add(wxString::Format("Slot %02d  |  (empty)", i));
        }
    }

    wxSingleChoiceDialog dlg(this, "Choose a slot to save:", "Save game", choices);
    dlg.SetSelection(0);
    if(dlg.ShowModal() != wxID_OK)
        return;

    const int slot = dlg.GetSelection() + 1;
    const auto path = GetStrategicSaveSlotPath(m_level, slot);

    // Save full strategic state into slot file
    SaveStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
                          m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits, /*timestamp*/NowIsoLocal());

    wxMessageBox(wxString::Format("Saved to slot %02d.", slot), "Save game", wxOK | wxICON_INFORMATION, this);
}

void StrategicLevelFrame::OnLoadGame(wxCommandEvent&)
{
    wxArrayString choices;
    choices.reserve(10);

    std::vector<bool> exists(10, false);
    for(int i = 1; i <= 10; ++i)
    {
        const auto p = GetStrategicSaveSlotPath(m_level, i);
        std::error_code ec;
        exists[i-1] = std::filesystem::exists(p, ec);
        if(exists[i-1])
        {
            int money=0, rank=0, xp=0;
            std::string ts;
            PeekStrategicSaveSummary(p, money, rank, xp, ts);
            wxString line = wxString::Format("Slot %02d  |  %s  |  $%d  |  XP %d  |  %s",
                i,
                ts.empty() ? wxString(L"(no time)") : wxString::FromUTF8(ts),
                money,
                xp,
                RankNameCz(rank));
            choices.Add(line);
        }
        else
        {
            choices.Add(wxString::Format("Slot %02d  |  (empty)", i));
        }
    }

    wxSingleChoiceDialog dlg(this, "Choose a slot to load:", "Load game", choices);
    dlg.SetSelection(0);
    if(dlg.ShowModal() != wxID_OK)
        return;

    const int slot = dlg.GetSelection() + 1;
    if(!exists[slot-1])
    {
        wxMessageBox("This slot is empty.", "Load game", wxOK | wxICON_WARNING, this);
        return;
    }

    const auto path = GetStrategicSaveSlotPath(m_level, slot);

    std::string loaded_level_def;
    std::string ts;
    if(!LoadStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
                              m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
                              &loaded_level_def, &ts))
    {
        wxMessageBox("Failed to load the saved game.", "Load game", wxOK | wxICON_ERROR, this);
        return;
    }

    
// Save slot may belong to a different strategic LEVEL_XX.DEF.
// In that case, automatically switch to the correct level and load there.
if(!loaded_level_def.empty() && loaded_level_def != m_level.source_path)
{
    if(!m_main)
    {
        wxString msg;
        msg << L"This save belongs to a different level/DEF:\n\n";
        msg << wxString::FromUTF8(loaded_level_def) << L"\n\n";
        msg << L"Current level is:\n\n";
        msg << wxString::FromUTF8(m_level.source_path) << L"\n";
        wxMessageBox(msg, L"Load game", wxOK | wxICON_WARNING, this);
        return;
    }

    LevelData lvl;
    std::string err;
    LevelLoader loader;
    if(!loader.LoadLevelDef(loaded_level_def, lvl, &err))
    {
        wxMessageBox(L"Failed to load the level DEF from this save:\n" + wxString::FromUTF8(err),
                     L"Load game", wxOK | wxICON_ERROR, this);
        return;
    }

    // Re-load the save file using the correct level, so territory defaults match.
    int turn=1, money=0, research=0, selTerr=-1;
    PlayerProgress pl;
    std::unordered_map<int, std::string> terrMission;
    std::unordered_map<int, int> terrLaunch;
    std::vector<LevelData::PlayerUnitAdd> units;
    std::string def2, ts2;

    if(!LoadStrategicStateFile(path, lvl, turn, money, research, selTerr, pl,
                              terrMission, terrLaunch, units, &def2, &ts2))
    {
        wxMessageBox(L"Failed to load the saved game.", L"Load game", wxOK | wxICON_ERROR, this);
        return;
    }

    // Open new Strategic Level window for that DEF and apply loaded state.
    auto* win = new StrategicLevelFrame(m_main, lvl);

    win->m_turn = turn;
    win->m_money = money;
    win->m_research = research;
    win->m_selectedTerritory = selTerr;
    win->m_player = pl;
    win->m_territoryCurrentMission = std::move(terrMission);
    win->m_territoryLaunchCount = std::move(terrLaunch);
    win->m_playerUnits = std::move(units);

    win->TryLoadBackground();
    win->RefreshUI();
    if(win->m_selectedTerritory >= 0)
        win->SelectTerritoryById(win->m_selectedTerritory);

    win->Show();
    win->Raise();

    // Close this (wrong-level) window.
    Close(true);
    return;
}

    if(m_selectedTerritory >= 0)
        SelectTerritoryById(m_selectedTerritory);

    RefreshUI();
    wxMessageBox(wxString::Format("Loaded slot %02d.", slot), "Load game", wxOK | wxICON_INFORMATION, this);
}


bool StrategicLevelFrame::EnsureUnitCostsLoaded()
{
    if(m_unitCostsLoaded)
        return true;

    m_unitCosts.clear();
    const auto path = GetUnitsJsonPath();
    if(!LoadUnitCostsFromJson(path, m_unitCosts))
    {
        wxMessageBox(wxString::Format("Units pricing file not found or invalid.\nExpected: %s", path.string().c_str()),
                     "Units pricing", wxOK | wxICON_WARNING, this);
        return false;
    }

    m_unitCostsLoaded = true;
    return true;
}

int StrategicLevelFrame::GetUnitBuyCost(int unit_id) const
{
    auto it = m_unitCosts.find(unit_id);
    if(it == m_unitCosts.end())
        return -1;
    return it->second;
}


void StrategicLevelFrame::BuildUI()
{
    auto* root = new wxPanel(this);
    m_palette.text = wxColour(0x82, 0xA7, 0x82);
    m_palette.heading = wxColour(0xFF, 0xF6, 0x04);
    m_palette.background = wxColour(0x11, 0x30, 0x09);
    m_palette.inactive = wxColour(0xA4, 0x9D, 0x9D);
    m_palette.statusHeading = wxColour(0x04, 0xDD, 0x04);
    m_palette.statusNumber = wxColour(0xA4, 0x9D, 0x9D);
    m_palette.buttonText = wxColour(0xE8, 0xE0, 0xE0);
    m_palette.buttonBackground = wxColour(0x84, 0x7C, 0x7C);
    m_palette.shadow = wxColour(0, 0, 0, 160);

    m_fontText = MakeStrategicFont(12, false);
    m_fontHeading = MakeStrategicFont(14, false);

    root->SetBackgroundColour(m_palette.background);

    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // ============================================================
    // LEFT: content book (Strategic map / Hierarchy)
    // ============================================================
    m_leftBook = new wxSimplebook(root, wxID_ANY);
    m_leftBook->SetBackgroundColour(m_palette.background);

    // --- Page 0: Strategic map ---
    m_mapPanel = new wxPanel(m_leftBook);
    m_mapPanel->SetBackgroundColour(m_palette.background);

    m_mapSizer = new wxBoxSizer(wxVERTICAL);

    // Paint surface for the strategic background (map) - fills the top area.
    m_mapCanvas = new wxPanel(m_mapPanel);
    m_mapCanvas->SetBackgroundColour(m_palette.background);
    m_mapCanvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_mapCanvas->Bind(wxEVT_PAINT, &StrategicLevelFrame::OnMapPaint, this);
    m_mapCanvas->Bind(wxEVT_LEFT_DOWN, &StrategicLevelFrame::OnMapLeftDown, this);
    m_mapSizer->Add(m_mapCanvas, 3, wxALL | wxEXPAND, 8);

    // Under-map panel: (optional) territory grid fallback + briefing/info text.
    auto* under = new wxPanel(m_mapPanel);
    under->SetBackgroundColour(m_palette.background);
    auto* underSizer = new wxBoxSizer(wxVERTICAL);

    // Territory buttons (fallback UI). When CLK is available (click map regions), this stays hidden.
    m_territoryButtonsPanel = new wxPanel(under);
    m_territoryButtonsPanel->SetBackgroundColour(m_palette.background);
    auto* grid = new wxGridSizer(0, 4, 6, 6);
    for(size_t i = 0; i < m_level.territories.size(); ++i)
    {
        const auto& t = m_level.territories[i];
        const auto id = ID_TERRITORY_BASE + (int)i;

        wxString label = wxString::Format("T%02d\n%s", t.id, t.mission);
        auto* btn = new wxButton(m_territoryButtonsPanel, id, label, wxDefaultPosition, wxSize(140, 60));
        btn->SetFont(m_fontText);
        btn->SetForegroundColour(m_palette.buttonText);
        btn->SetBackgroundColour(m_palette.buttonBackground);
        btn->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnTerritory, this);
        grid->Add(btn, 0, wxEXPAND);
    }
    m_territoryButtonsPanel->SetSizer(grid);
    // Hidden by default; TryLoadBackground() will show it only if CLK is missing.
    m_territoryButtonsPanel->Hide();
    underSizer->Add(m_territoryButtonsPanel, 0, wxALL | wxEXPAND, 6);

    // Briefing / mission info (read-only)
    auto* info = new wxTextCtrl(
        under,
        ID_TERRITORY_TEXTBOX,
        "",
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    info->SetFont(m_fontText);
    info->SetBackgroundColour(m_palette.background);
    info->SetForegroundColour(m_palette.text);
    info->SetMinSize(wxSize(-1, 240));
    underSizer->Add(info, 1, wxALL | wxEXPAND, 6);

    under->SetSizer(underSizer);
    m_mapSizer->Add(under, 2, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    m_mapPanel->SetSizer(m_mapSizer);

    // --- Page 1: Hierarchy ---
    auto* hierarchyPanel = new wxPanel(m_leftBook);
    hierarchyPanel->SetBackgroundColour(m_palette.background);
    auto* hs = new wxBoxSizer(wxVERTICAL);

    auto* hTitle = CreateStrategicLabel(hierarchyPanel, "Units / Hierarchy", m_fontHeading, m_palette.heading, m_palette.shadow);
    hs->Add(hTitle, 0, wxALL, 8);

    auto* hIntro = new wxTextCtrl(
        hierarchyPanel,
        wxID_ANY,
        "Hierarchy overview"
        "- Basic formation is a battalion."
        "- Two battalions form a regiment."
        "- Two regiments (four battalions) form a brigade."
        "Commanders are special units assigned to formations."
        "Higher ranks allow higher formations."
        "If a unit with a commander is destroyed, the commander is lost.",
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
    hIntro->SetFont(m_fontText);
    hIntro->SetBackgroundColour(m_palette.background);
    hIntro->SetForegroundColour(m_palette.text);
    hIntro->SetMinSize(wxSize(-1, 160));
    hs->Add(hIntro, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    m_hierarchyList = new wxListCtrl(hierarchyPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_hierarchyList->SetFont(m_fontText);
    m_hierarchyList->SetBackgroundColour(m_palette.background);
    m_hierarchyList->SetForegroundColour(m_palette.text);
    m_hierarchyList->InsertColumn(0, "Formation");
    m_hierarchyList->InsertColumn(1, "Commander");
    m_hierarchyList->InsertColumn(2, "Units");
    hs->Add(m_hierarchyList, 1, wxALL | wxEXPAND, 8);

    hierarchyPanel->SetSizer(hs);

    // --- Page 2: Statistics (integrated into this frame) ---
    m_statsPanel = new wxPanel(m_leftBook);
    m_statsPanel->SetBackgroundColour(m_palette.background);
    BuildStatsPage();

    m_leftBook->AddPage(m_mapPanel, "Strategic map", true);
    m_leftBook->AddPage(hierarchyPanel, "Hierarchy", false);
    m_leftBook->AddPage(m_statsPanel, "Statistics", false);

    mainSizer->Add(m_leftBook, 4, wxEXPAND);

    // ============================================================
    // MIDDLE: player units (always visible)
    // ============================================================
    auto* mid = new wxPanel(root);
    mid->SetBackgroundColour(m_palette.background);
    auto* midSizer = new wxBoxSizer(wxVERTICAL);

    auto* midTitle = CreateStrategicLabel(mid, "Player units", m_fontHeading, m_palette.heading, m_palette.shadow);
    midSizer->Add(midTitle, 0, wxALL, 8);

    m_roster = new wxListCtrl(mid, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_roster->SetFont(m_fontText);
    m_roster->SetBackgroundColour(m_palette.background);
    m_roster->SetForegroundColour(m_palette.text);
    m_roster->InsertColumn(0, "Unit");
    m_roster->InsertColumn(1, "Count");
    m_roster->InsertColumn(2, "HP");
    midSizer->Add(m_roster, 1, wxALL | wxEXPAND, 8);

    mid->SetSizer(midSizer);
    mainSizer->Add(mid, 2, wxEXPAND);

    // ============================================================
    // RIGHT: status + actions (always visible, consistent layout)
    // ============================================================
    auto* right = new wxPanel(root);
    right->SetBackgroundColour(m_palette.background);
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    // Status box (Money / Research / Turn)
    auto* status = new wxPanel(right);
    status->SetBackgroundColour(m_palette.background);
    auto* statusSizer = new wxBoxSizer(wxVERTICAL);

    wxBitmap placeholder(1, 1);
    m_lblMoney = new wxStaticBitmap(status, wxID_ANY, placeholder);
    m_lblResearch = new wxStaticBitmap(status, wxID_ANY, placeholder);
    m_lblTurn = new wxStaticBitmap(status, wxID_ANY, placeholder);

    statusSizer->Add(m_lblMoney, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 6);
    statusSizer->Add(m_lblResearch, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 6);
    statusSizer->Add(m_lblTurn, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 6);

    status->SetSizer(statusSizer);
    rightSizer->Add(status, 0, wxALL | wxEXPAND, 8);

    auto makeBtn = [&](int id, const wxString& label) -> wxBitmapButton*
    {
        return CreateStrategicButton(right, id, label, m_fontText, m_palette.buttonText,
                                     m_palette.shadow, m_palette.buttonBackground, wxSize(-1, 44));
    };

    // Buttons (order matches original-ish layout)
    m_btnResearch      = makeBtn(ID_BTN_RESEARCH, "Research");
    m_btnBuy           = makeBtn(ID_BTN_BUY, "Buy units");
    m_btnSell          = makeBtn(ID_BTN_SELL, "Sell units");
    m_btnStrategicMap  = makeBtn(ID_BTN_STRATEGIC_MAP, "Strategic map");
    m_btnHierarchy     = makeBtn(ID_BTN_HIERARCHY, "Hierarchy");
    m_btnStats         = makeBtn(ID_BTN_STATS, "Statistics");
    m_btnLaunch        = makeBtn(ID_BTN_LAUNCH, "Launch mission");
    m_btnEndTurn       = makeBtn(ID_BTN_ENDTURN, "End turn");

    auto* btnSizer = new wxBoxSizer(wxVERTICAL);
    btnSizer->Add(m_btnResearch,     0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnBuy,          0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnSell,         0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(m_btnStrategicMap, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnHierarchy,    0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnStats,        0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(m_btnLaunch,       0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(m_btnEndTurn,      0, wxEXPAND);

    rightSizer->Add(btnSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    right->SetSizer(rightSizer);

    mainSizer->Add(right, 1, wxEXPAND);

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(mainSizer, 1, wxEXPAND);
    root->SetSizer(rootSizer);
}

void StrategicLevelFrame::RefreshUI()
{
    UpdateStrategicLabel(
        m_lblMoney,
        { { "Money ", m_palette.statusHeading, &m_fontHeading },
          { wxString::Format("%d", m_money), m_palette.statusNumber, &m_fontText } },
        m_fontText,
        m_palette.shadow);
    UpdateStrategicLabel(
        m_lblResearch,
        { { "Research ", m_palette.statusHeading, &m_fontHeading },
          { wxString::Format("%d", m_research), m_palette.statusNumber, &m_fontText } },
        m_fontText,
        m_palette.shadow);
    UpdateStrategicLabel(
        m_lblTurn,
        { { "Turn ", m_palette.statusHeading, &m_fontHeading },
          { wxString::Format("%d", m_turn), m_palette.statusNumber, &m_fontText } },
        m_fontText,
        m_palette.shadow);

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
    if(m_btnSell)
        m_btnSell->Enable(!m_playerUnits.empty());
}


void StrategicLevelFrame::OnShowStrategicMap(wxCommandEvent&)
{
    if(m_leftBook)
        m_leftBook->SetSelection(0);
}

void StrategicLevelFrame::OnShowHierarchy(wxCommandEvent&)
{
    if(m_leftBook)
        m_leftBook->SetSelection(1);
}

void StrategicLevelFrame::OnShowStats(wxCommandEvent&)
{
    // Ensure the stats page sees the latest state.
    SaveStrategicState();
    LoadRanksTable();
    LoadMissionStatsIfPresent();
    RecomputePlayerRank();
    RefreshStatsPage();

    if(m_leftBook)
        m_leftBook->SetSelection(2);
}
void StrategicLevelFrame::SelectTerritoryById(int territory_id)
{
    // Find index in LevelData by id.
    int idx = -1;
    for(size_t i = 0; i < m_level.territories.size(); ++i)
    {
        if(m_level.territories[i].id == territory_id)
        {
            idx = (int)i;
            break;
        }
    }
    if(idx < 0)
        return;

    m_selectedTerritory = territory_id;

    // Reuse existing logic by faking a button event id.
    wxCommandEvent ev(wxEVT_BUTTON, ID_TERRITORY_BASE + idx);
    OnTerritory(ev);
}

void StrategicLevelFrame::OnMapLeftDown(wxMouseEvent& ev)
{
    if(!m_hasBg || !m_bgBitmap.IsOk() || !m_hasClk || m_clkValues.empty())
    {
        ev.Skip();
        return;
    }

    wxWindow* target = m_mapCanvas ? (wxWindow*)m_mapCanvas : (wxWindow*)m_mapPanel;
    if(!target)
    {
        ev.Skip();
        return;
    }

    int pw, ph;
    target->GetClientSize(&pw, &ph);
    const int bw = m_bgBitmap.GetWidth();
    const int bh = m_bgBitmap.GetHeight();
    if(pw <= 0 || ph <= 0 || bw <= 0 || bh <= 0)
    {
        ev.Skip();
        return;
    }

    const double sx = (double)pw / (double)bw;
    const double sy = (double)ph / (double)bh;
    const double s  = std::min(sx, sy);
    const int dw = std::max(1, (int)std::lround((double)bw * s));
    const int dh = std::max(1, (int)std::lround((double)bh * s));
    const int ox = (pw - dw) / 2;
    const int oy = (ph - dh) / 2;

    const wxPoint p = ev.GetPosition();
    if(p.x < ox || p.y < oy || p.x >= ox + dw || p.y >= oy + dh)
        return;

    // Map click from scaled bitmap to original pixel coords.
    const int mx = (int)std::floor(((double)(p.x - ox) * (double)bw) / (double)dw);
    const int my = (int)std::floor(((double)(p.y - oy) * (double)bh) / (double)dh);
    if(mx < 0 || my < 0 || mx >= bw || my >= bh)
        return;

    // CLK map must match bitmap dimensions.
    if(m_clkW != bw || m_clkH != bh || (size_t)m_clkW * (size_t)m_clkH != m_clkValues.size())
        return;

    const unsigned char tid = m_clkValues[(size_t)my * (size_t)m_clkW + (size_t)mx];
    if(tid == 0)
        return;

    SelectTerritoryById((int)tid);
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
    if (!m_mapPanel)
    {
        RefreshUI();
        return;
    }

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
    if(!EnsureUnitCostsLoaded())
        return;

    wxDialog dlg(this, wxID_ANY, "Buy units", wxDefaultPosition, wxSize(420, 480));
    dlg.SetBackgroundColour(m_palette.background);
    dlg.SetForegroundColour(m_palette.text);
    dlg.SetFont(m_fontText);
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl = new wxStaticText(&dlg, wxID_ANY, "Select unit:");
    lbl->SetFont(m_fontText);
    lbl->SetForegroundColour(m_palette.text);
    rootSizer->Add(lbl, 0, wxLEFT | wxRIGHT | wxTOP, 10);

    auto* list = new wxListBox(&dlg, wxID_ANY);
    list->SetFont(m_fontText);
    list->SetBackgroundColour(m_palette.background);
    list->SetForegroundColour(m_palette.text);
    std::vector<int> unit_ids;
    std::vector<int> unit_costs;
    unit_ids.reserve(m_spellData->units->GetUnits().size());
    unit_costs.reserve(m_spellData->units->GetUnits().size());
    for(const auto* unit : m_spellData->units->GetUnits())
    {
        if(!unit)
            continue;
        unit_ids.push_back(unit->type_id);
        int cost = GetUnitBuyCost(unit->type_id);
        unit_costs.push_back(cost);
        wxString label = wxString::Format("#%02d: %s", unit->type_id, wxString(char2wstringCP895(unit->name)));
        if(cost > 0)
            label += wxString::Format(" (%d)", cost);
        list->Append(label);
    }
    if(!unit_ids.empty())
        list->SetSelection(0);
    rootSizer->Add(list, 1, wxALL | wxEXPAND, 10);

    auto* countSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* countLabel = new wxStaticText(&dlg, wxID_ANY, "Count:");
    countLabel->SetFont(m_fontText);
    countLabel->SetForegroundColour(m_palette.text);
    countSizer->Add(countLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* spinCount = new wxSpinCtrl(&dlg, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 99, 1);
    spinCount->SetFont(m_fontText);
    spinCount->SetBackgroundColour(m_palette.background);
    spinCount->SetForegroundColour(m_palette.text);
    countSizer->Add(spinCount, 0);
    rootSizer->Add(countSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* btnBuy = new wxButton(&dlg, wxID_OK, "Buy");
    auto* btnCancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
    btnBuy->SetFont(m_fontText);
    btnBuy->SetBackgroundColour(m_palette.buttonBackground);
    btnBuy->SetForegroundColour(m_palette.buttonText);
    btnCancel->SetFont(m_fontText);
    btnCancel->SetBackgroundColour(m_palette.buttonBackground);
    btnCancel->SetForegroundColour(m_palette.buttonText);
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
    if(sel >= (int)unit_costs.size() || unit_costs[sel] <= 0)
    {
        wxMessageBox("Selected unit has no price defined.", "Buy units", wxOK | wxICON_WARNING, this);
        return;
    }

    const int count = spinCount->GetValue();
    const int unitCost = unit_costs[sel];
    const int totalCost = unitCost * count;
    if(m_money < totalCost)
    {
        wxMessageBox(wxString::Format("Not enough money. Need %d, you have %d.", totalCost, m_money),
                     "Buy units", wxOK | wxICON_WARNING, this);
        return;
    }

    LevelData::PlayerUnitAdd add;
    add.unit_id = unit_ids[sel];
    add.count = count;
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

    m_money -= totalCost;
    SaveStrategicState();
    RefreshUI();
}

void StrategicLevelFrame::OnSellUnits(wxCommandEvent&)
{
    if(m_playerUnits.empty())
    {
        wxMessageBox("No units to sell.", "Sell units", wxOK | wxICON_INFORMATION, this);
        return;
    }
    if(!EnsureUnitCostsLoaded())
        return;

    struct SellEntry
    {
        int index = -1;
        int unit_id = -1;
        int count = 0;
        int health = 0;
        int cost = -1;
    };

    std::vector<SellEntry> entries;
    entries.reserve(m_playerUnits.size());

    wxDialog dlg(this, wxID_ANY, "Sell units", wxDefaultPosition, wxSize(420, 480));
    dlg.SetBackgroundColour(m_palette.background);
    dlg.SetForegroundColour(m_palette.text);
    dlg.SetFont(m_fontText);
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl = new wxStaticText(&dlg, wxID_ANY, "Select unit:");
    lbl->SetFont(m_fontText);
    lbl->SetForegroundColour(m_palette.text);
    rootSizer->Add(lbl, 0, wxLEFT | wxRIGHT | wxTOP, 10);

    auto* list = new wxListBox(&dlg, wxID_ANY);
    list->SetFont(m_fontText);
    list->SetBackgroundColour(m_palette.background);
    list->SetForegroundColour(m_palette.text);
    for(size_t i = 0; i < m_playerUnits.size(); ++i)
    {
        const auto& u = m_playerUnits[i];
        SellEntry entry;
        entry.index = static_cast<int>(i);
        entry.unit_id = u.unit_id;
        entry.count = u.count;
        entry.health = u.health;
        entry.cost = GetUnitBuyCost(u.unit_id);
        entries.push_back(entry);

        wxString label = wxString::Format("%s x%d", GetUnitDisplayName(u.unit_id), u.count);
        if(entry.cost > 0)
            label += wxString::Format(" (sell %d)", entry.cost / 2);
        else
            label += " (no price)";
        list->Append(label);
    }
    if(!entries.empty())
        list->SetSelection(0);
    rootSizer->Add(list, 1, wxALL | wxEXPAND, 10);

    auto* countSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* countLabel = new wxStaticText(&dlg, wxID_ANY, "Count:");
    countLabel->SetFont(m_fontText);
    countLabel->SetForegroundColour(m_palette.text);
    countSizer->Add(countLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* spinCount = new wxSpinCtrl(&dlg, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 99, 1);
    spinCount->SetFont(m_fontText);
    spinCount->SetBackgroundColour(m_palette.background);
    spinCount->SetForegroundColour(m_palette.text);
    countSizer->Add(spinCount, 0);
    rootSizer->Add(countSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto updateSpinRange = [&]()
    {
        int sel = list->GetSelection();
        if(sel == wxNOT_FOUND || sel >= (int)entries.size())
            return;
        int maxCount = std::max(1, entries[sel].count);
        spinCount->SetRange(1, maxCount);
        if(spinCount->GetValue() > maxCount)
            spinCount->SetValue(maxCount);
    };
    updateSpinRange();
    list->Bind(wxEVT_LISTBOX, [&](wxCommandEvent&){ updateSpinRange(); });

    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* btnSell = new wxButton(&dlg, wxID_OK, "Sell");
    auto* btnCancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
    btnSell->SetFont(m_fontText);
    btnSell->SetBackgroundColour(m_palette.buttonBackground);
    btnSell->SetForegroundColour(m_palette.buttonText);
    btnCancel->SetFont(m_fontText);
    btnCancel->SetBackgroundColour(m_palette.buttonBackground);
    btnCancel->SetForegroundColour(m_palette.buttonText);
    btnSizer->AddStretchSpacer(1);
    btnSizer->Add(btnSell, 0, wxRIGHT, 8);
    btnSizer->Add(btnCancel, 0);
    rootSizer->Add(btnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    dlg.SetSizerAndFit(rootSizer);

    if(dlg.ShowModal() != wxID_OK)
        return;

    int sel = list->GetSelection();
    if(sel == wxNOT_FOUND || sel >= (int)entries.size())
    {
        wxMessageBox("No unit selected.", "Sell units", wxOK | wxICON_WARNING, this);
        return;
    }

    const auto& entry = entries[sel];
    if(entry.cost <= 0)
    {
        wxMessageBox("Selected unit has no price defined.", "Sell units", wxOK | wxICON_WARNING, this);
        return;
    }

    int sellCount = spinCount->GetValue();
    if(sellCount <= 0)
        return;
    if(sellCount > entry.count)
        sellCount = entry.count;

    int refund = (entry.cost * sellCount) / 2;
    m_money += refund;

    if(entry.index >= 0 && entry.index < (int)m_playerUnits.size())
    {
        auto& unit = m_playerUnits[entry.index];
        unit.count -= sellCount;
        if(unit.count <= 0)
            m_playerUnits.erase(m_playerUnits.begin() + entry.index);
    }

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

    // persist progression before leaving the strategic screen
    SaveStrategicState();

    // jump directly into game mode and close the strategic-level window
    m_main->SetGameModeUI(true);
    m_main->Raise();

    Close(true);
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


static bool LoadStrategicStateFile(
    const std::filesystem::path& path,
    const LevelData& level,
    int& turn,
    int& money,
    int& research,
    int& selected_territory,
    StrategicLevelFrame::PlayerProgress& player,
    std::unordered_map<int, std::string>& territoryMission,
    std::unordered_map<int, int>& territoryLaunchCount,
    std::vector<LevelData::PlayerUnitAdd>& units,
    std::string* out_level_def = nullptr,
    std::string* out_timestamp = nullptr)
{
    units.clear();

    // defaults
    turn = 1;
    money = 0;
    research = 0;
    selected_territory = -1;
    player = StrategicLevelFrame::PlayerProgress{};

    territoryMission.clear();
    territoryLaunchCount.clear();
    for(const auto& t : level.territories)
    {
        territoryMission[t.id] = t.mission;
        territoryLaunchCount[t.id] = 0;
    }

    if(out_level_def) out_level_def->clear();
    if(out_timestamp) out_timestamp->clear();

    std::ifstream f(path);
    if(!f)
        return false;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if(data.empty())
        return false;

    std::smatch m;

    // version/level_def/timestamp are optional but recommended
    std::regex leveldef_re("\"level_def\"\\s*:\\s*\"([^\"]*)\"");
    if(out_level_def && std::regex_search(data, m, leveldef_re) && m.size() > 1)
        *out_level_def = m[1].str();

    std::regex ts_re("\"timestamp\"\\s*:\\s*\"([^\"]*)\"");
    if(out_timestamp && std::regex_search(data, m, ts_re) && m.size() > 1)
        *out_timestamp = m[1].str();

    if(std::regex_search(data, m, std::regex("\"turn\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        turn = std::stoi(m[1].str());

    if(std::regex_search(data, m, std::regex("\"money\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        money = std::stoi(m[1].str());

    if(std::regex_search(data, m, std::regex("\"research\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        research = std::stoi(m[1].str());

    if(std::regex_search(data, m, std::regex("\"selected_territory\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        selected_territory = std::stoi(m[1].str());

    // player object optional (backward compatible)
    std::regex player_obj_re("\"player\"\\s*:\\s*\\{([^}]*)\\}");
    if(std::regex_search(data, m, player_obj_re) && m.size() > 1)
    {
        const std::string pobj = m[1].str();
        (void)ParseJsonStringField(pobj, "name", player.name);
        (void)ParseJsonIntField(pobj, "rank", player.rank);
        (void)ParseJsonIntField(pobj, "experience", player.experience);
        (void)ParseJsonIntField(pobj, "actions", player.actions);
    }

    // territory list optional (backward compatible)
    // {"id":7,"mission":"M02_01","launches":2}
    std::regex terr_re("\\{\\s*\"id\"\\s*:\\s*(\\d+)\\s*,\\s*\"mission\"\\s*:\\s*\"([^\"]*)\"\\s*,\\s*\"launches\"\\s*:\\s*(\\d+)\\s*\\}");
    for(auto it = std::sregex_iterator(data.begin(), data.end(), terr_re); it != std::sregex_iterator(); ++it)
    {
        const auto& mm = *it;
        if(mm.size() < 4)
            continue;
        const int id = std::stoi(mm[1].str());
        const std::string mission = mm[2].str();
        const int launches = std::stoi(mm[3].str());
        territoryMission[id] = mission;
        territoryLaunchCount[id] = launches;
    }

    // units list (as before)
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

static void SaveStrategicStateFile(
    const std::filesystem::path& path,
    const LevelData& level,
    int turn,
    int money,
    int research,
    int selected_territory,
    const StrategicLevelFrame::PlayerProgress& player,
    const std::unordered_map<int, std::string>& territoryMission,
    const std::unordered_map<int, int>& territoryLaunchCount,
    const std::vector<LevelData::PlayerUnitAdd>& units,
    const std::string& timestamp)
{
    std::ofstream f(path);
    if(!f)
        return;

    f << "{\n";
    f << "  \"version\": 1,\n";
    f << "  \"timestamp\": \"" << EscapeJson(timestamp) << "\",\n";
    f << "  \"level_def\": \"" << EscapeJson(level.source_path) << "\",\n";
    f << "  \"turn\": " << turn << ",\n";
    f << "  \"money\": " << money << ",\n";
    f << "  \"research\": " << research << ",\n";
    f << "  \"selected_territory\": " << selected_territory << ",\n";
    f << "  \"player\": {"
      << "\"name\": \"" << EscapeJson(player.name) << "\", "
      << "\"rank\": " << player.rank << ", "
      << "\"experience\": " << player.experience << ", "
      << "\"actions\": " << player.actions
      << "},\n";

    // territories
    f << "  \"territories\": [\n";
    for(size_t i = 0; i < level.territories.size(); ++i)
    {
        const auto& t = level.territories[i];
        auto itM = territoryMission.find(t.id);
        auto itL = territoryLaunchCount.find(t.id);
        const std::string mission = (itM != territoryMission.end()) ? itM->second : t.mission;
        const int launches = (itL != territoryLaunchCount.end()) ? itL->second : 0;

        f << "    {\"id\": " << t.id
          << ", \"mission\": \"" << EscapeJson(mission)
          << "\", \"launches\": " << launches << "}";
        if(i + 1 < level.territories.size())
            f << ",";
        f << "\n";
    }
    f << "  ],\n";

    // units
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
    const auto path = GetStrategicStatePath(m_level);

    int turn = 1, money = 0, research = 0, selected = -1;
    PlayerProgress player{};
    std::unordered_map<int, std::string> terrM;
    std::unordered_map<int, int> terrL;
    std::vector<LevelData::PlayerUnitAdd> units;
    std::string level_def, ts;

    if(LoadStrategicStateFile(path, m_level, turn, money, research, selected, player, terrM, terrL, units, &level_def, &ts))
    {
        m_turn = turn;
        m_money = money;
        m_research = research;
        m_selectedTerritory = selected;
        m_player = player;
        m_territoryCurrentMission = std::move(terrM);
        m_territoryLaunchCount = std::move(terrL);
        m_playerUnits = std::move(units);

        if(m_selectedTerritory >= 0)
            SelectTerritoryById(m_selectedTerritory);
    }
}

void StrategicLevelFrame::SaveStrategicState() const
{
    const auto path = GetStrategicStatePath(m_level);
    SaveStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
                          m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits, NowIsoLocal());
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

static bool NormalizeIndexedBuffer(const std::vector<unsigned char>& src, size_t need,
                                   const std::vector<unsigned char>& clkValues,
                                   std::vector<unsigned char>& out)
{
    out.clear();
    if(src.size() == need)
    {
        out = src;
        return true;
    }
    if(src.size() == need + 1)
    {
        // Choose whether to drop first or last byte by comparing how well the outside area
        // compresses to a single key color (matches python tool behavior).
        auto score_drop = [&](bool drop_first) -> size_t
        {
            const unsigned char* p = src.data() + (drop_first ? 1 : 0);
            // count most frequent color on outside (clk==0)
            std::array<size_t, 256> counts{};
            for(size_t i = 0; i < need; ++i)
            {
                if(i < clkValues.size() && clkValues[i] == 0)
                    counts[p[i]]++;
            }
            return *std::max_element(counts.begin(), counts.end());
        };

        size_t s1 = score_drop(true);
        size_t s2 = score_drop(false); // dropping last means using first need bytes
        bool drop_first = (s1 >= s2);

        out.assign(src.begin() + (drop_first ? 1 : 0), src.begin() + (drop_first ? 1 : 0) + (ptrdiff_t)need);
        return true;
    }

    // Larger buffers: take the last 'need' bytes as a best-effort (some assets contain a small header).
    if(src.size() > need)
    {
        out.assign(src.end() - (ptrdiff_t)need, src.end());
        return true;
    }
    return false;
}

static bool BuildStrategicCompositeFromFolder(const std::filesystem::path& folder, int levelNum, wxBitmap& outBmp,
                                           int* outW = nullptr, int* outH = nullptr, std::vector<unsigned char>* outClk = nullptr)
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
    std::vector<unsigned char> levelPix, fogPix;
    if(!NormalizeIndexedBuffer(levelBytes, need, clkValues, levelPix))
        return false;
    if(!NormalizeIndexedBuffer(fogBytes, need, clkValues, fogPix))
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
        const bool inside = (clkValues[i] != 0);
        const unsigned char idx = inside ? levelPix[i] : fogPix[i];

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
        if(clkValues[i] == 0)
            continue;

        bool edge = false;
        if(x > 0 && clkValues[i - 1] != 0 && clkValues[i] != clkValues[i - 1]) edge = true;
        if(y > 0 && clkValues[i - (size_t)W] != 0 && clkValues[i] != clkValues[i - (size_t)W]) edge = true;
        if(edge)
        {
            img.SetRGB(x, y, 20, 20, 20);
            img.SetAlpha(x, y, 255);
        }
    }

    outBmp = wxBitmap(img);
    if(outW) *outW = W;
    if(outH) *outH = H;
    if(outClk) *outClk = std::move(clkValues);
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

    m_hasClk = false;
    m_clkValues.clear();
    m_clkW = m_clkH = 0;

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

    // If we can build the composite (LEVEL + HMLA + PAL + CLK), keep CLK for click-detection.
    bool composite_ok = false;
    int cw = 0, ch = 0;
    std::vector<unsigned char> cclk;

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
            if(BuildStrategicCompositeFromFolder(folder, levelNum, bmp, &cw, &ch, &cclk))
            {
                composite_ok = true;
                break;
            }
        }
    }

    // Fallback: older simple LZ background (no CLK).
    if(!bmp.IsOk())
        BuildLegacyLZBackgroundFromDef(defPath, bmp);

    if(bmp.IsOk())
    {
        m_bgBitmap = bmp;
        m_hasBg = true;

        if(composite_ok && !cclk.empty() && cw > 0 && ch > 0)
        {
            m_clkValues = std::move(cclk);
            m_clkW = cw;
            m_clkH = ch;
            m_hasClk = ((size_t)m_clkW * (size_t)m_clkH == m_clkValues.size());

            // Hide the territory button grid when region click-detection is available.
            if(m_territoryButtonsPanel)
            {
                m_territoryButtonsPanel->Show(!m_hasClk);
                if(m_mapPanel) m_mapPanel->Layout();
            }


            // Precompute centroid positions for labels / selection marker.
            RebuildTerritoryCentroids();
        }
    }

    if(m_mapCanvas)
        m_mapCanvas->Refresh();
    else if(m_mapPanel)
        m_mapPanel->Refresh();
}


void StrategicLevelFrame::RebuildTerritoryCentroids()
{
    m_territoryCentroids.clear();

    if(!m_hasClk || m_clkValues.empty() || m_clkW <= 0 || m_clkH <= 0)
        return;

    // Accumulate pixel sums per territory id.
    struct Acc { long long sx = 0; long long sy = 0; long long n = 0; };
    std::unordered_map<int, Acc> acc;
    acc.reserve(std::max<size_t>(16, m_level.territories.size() * 2));

    for(int y = 0; y < m_clkH; ++y)
    {
        const unsigned char* row = &m_clkValues[(size_t)y * (size_t)m_clkW];
        for(int x = 0; x < m_clkW; ++x)
        {
            const int tid = (int)row[x];
            if(tid == 0)
                continue;

            auto& a = acc[tid];
            a.sx += x;
            a.sy += y;
            a.n  += 1;
        }
    }

    for(const auto& kv : acc)
    {
        if(kv.second.n <= 0)
            continue;

        const int cx = (int)std::lround((double)kv.second.sx / (double)kv.second.n);
        const int cy = (int)std::lround((double)kv.second.sy / (double)kv.second.n);
        m_territoryCentroids[kv.first] = wxPoint(cx, cy);
    }
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

// Territory labels directly on the map (replacement for the temporary button grid).
// Prefer centroids computed from CLK (exact), fallback to LEVEL_XX.DEF "strategic_x/y".
        {
            dc.SetFont(m_fontText);

            // Heuristic: many DEFs store strategic_x/y in a 0..255 logical space (not pixel coords).
            int maxSX = 0, maxSY = 0;
            for (const auto& t : m_level.territories)
            {
                maxSX = std::max(maxSX, t.strategic_x);
                maxSY = std::max(maxSY, t.strategic_y);
            }
            const bool defLooksLike256 =
                (maxSX > 0 && maxSY > 0 && maxSX <= 255 && maxSY <= 255 && (bw > 255 || bh > 255));

            auto getPx = [&](const LevelTerritory& t, int& px, int& py) -> bool
                {
                    // 1) Exact centroid from CLK
                    auto it = m_territoryCentroids.find(t.id);
                    if (it != m_territoryCentroids.end())
                    {
                        px = it->second.x;
                        py = it->second.y;
                        return true;
                    }

                    // 2) Fallback: DEF point
                    if (t.strategic_x <= 0 || t.strategic_y <= 0)
                        return false;

                    if (defLooksLike256)
                    {
                        px = (int)std::lround(((double)t.strategic_x * (double)bw) / 256.0);
                        py = (int)std::lround(((double)t.strategic_y * (double)bh) / 256.0);
                    }
                    else
                    {
                        px = t.strategic_x;
                        py = t.strategic_y;
                    }
                    return true;
                };

            // Draw all territory IDs.
            for (const auto& t : m_level.territories)
            {
                int px = 0, py = 0;
                if (!getPx(t, px, py))
                    continue;

                const int tx = x + (int)std::lround((double)px * s);
                const int ty = y + (int)std::lround((double)py * s);

                wxString label = wxString::Format("T%02d", t.id);

                // Tiny shadow for readability.
                dc.SetTextForeground(m_palette.shadow);
                dc.DrawText(label, tx + 1, ty + 1);
                dc.SetTextForeground(m_palette.text);
                dc.DrawText(label, tx, ty);
            }

            // Simple selection marker at the selected territory point.
            if (m_selectedTerritory > 0)
            {
                const LevelTerritory* sel = nullptr;
                for (const auto& t : m_level.territories)
                {
                    if (t.id == m_selectedTerritory) { sel = &t; break; }
                }

                int px = 0, py = 0;
                if (sel && getPx(*sel, px, py))
                {
                    const int tx = x + (int)std::lround((double)px * s);
                    const int ty = y + (int)std::lround((double)py * s);
                    const int r = std::max(6, (int)std::lround(6.0 * s));

                    dc.SetPen(wxPen(m_palette.heading, 2));
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);
                    dc.DrawCircle(tx, ty, r);
                }
            }
        };
    }
}

void StrategicLevelFrame::OnActivate(wxActivateEvent& ev)
{
    if(ev.GetActive())
        Raise();
    ev.Skip();
}


// ============================================================
// Statistics page (integrated from former form_strategic.*)
// ============================================================

void StrategicLevelFrame::BuildStatsPage()
{
    if(!m_statsPanel)
        return;

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    // ---- Overall stats ----
    rootSizer->Add(CreateStrategicLabel(m_statsPanel, "Overall statistics", m_fontHeading, m_palette.heading, m_palette.shadow), 0, wxALL, 10);

    auto* overallBox = new wxPanel(m_statsPanel);
    overallBox->SetBackgroundColour(m_palette.background);
    auto* overallSizer = new wxBoxSizer(wxVERTICAL);

    auto addHeader = [&](wxWindow* parent) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(CreateStrategicLabel(parent, "", m_fontHeading, m_palette.heading, m_palette.shadow), 1, wxRIGHT, 8);
        row->Add(CreateStrategicLabel(parent, "Alliance", m_fontHeading, m_palette.heading, m_palette.shadow), 0, wxRIGHT, 12);
        row->Add(CreateStrategicLabel(parent, "Enemy", m_fontHeading, m_palette.heading, m_palette.shadow), 0);
        return row;
    };

    overallSizer->Add(addHeader(overallBox), 0, wxALL | wxEXPAND, 8);

    auto addRow = [&](wxWindow* parent, const char* caption, wxStaticBitmap*& outA, wxStaticBitmap*& outE)
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(CreateStrategicLabel(parent, wxString::FromUTF8(caption), m_fontText, m_palette.text, m_palette.shadow), 1, wxRIGHT, 8);

        outA = CreateStrategicLabel(parent, "0", m_fontText, m_palette.text, m_palette.shadow);
        outE = CreateStrategicLabel(parent, "0", m_fontText, m_palette.text, m_palette.shadow);
        row->Add(outA, 0, wxRIGHT, 12);
        row->Add(outE, 0);
        return row;
    };

    overallSizer->Add(addRow(overallBox, "Light units", m_lblAllLightA, m_lblAllLightE), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    overallSizer->Add(addRow(overallBox, "Heavy units", m_lblAllHeavyA, m_lblAllHeavyE), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    overallSizer->Add(addRow(overallBox, "Air units",   m_lblAllAirA,  m_lblAllAirE),  0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    overallSizer->Add(addRow(overallBox, "Commanders",  m_lblAllCmdA,  m_lblAllCmdE),  0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    overallBox->SetSizer(overallSizer);
    rootSizer->Add(overallBox, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    // ---- Level stats ----
    rootSizer->Add(CreateStrategicLabel(m_statsPanel, "Current level statistics", m_fontHeading, m_palette.heading, m_palette.shadow), 0, wxALL, 10);

    auto* levelBox = new wxPanel(m_statsPanel);
    levelBox->SetBackgroundColour(m_palette.background);
    auto* levelSizer = new wxBoxSizer(wxVERTICAL);

    levelSizer->Add(addHeader(levelBox), 0, wxALL | wxEXPAND, 8);

    levelSizer->Add(addRow(levelBox, "Light units", m_lblLvlLightA, m_lblLvlLightE), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    levelSizer->Add(addRow(levelBox, "Heavy units", m_lblLvlHeavyA, m_lblLvlHeavyE), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    levelSizer->Add(addRow(levelBox, "Air units",   m_lblLvlAirA,  m_lblLvlAirE),  0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    levelSizer->Add(addRow(levelBox, "Commanders",  m_lblLvlCmdA,  m_lblLvlCmdE),  0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    levelBox->SetSizer(levelSizer);
    rootSizer->Add(levelBox, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    // ---- Player box ----
    rootSizer->Add(CreateStrategicLabel(m_statsPanel, "Player", m_fontHeading, m_palette.heading, m_palette.shadow), 0, wxALL, 10);

    auto* playerBox = new wxPanel(m_statsPanel);
    playerBox->SetBackgroundColour(m_palette.background);
    auto* playerSizer = new wxBoxSizer(wxVERTICAL);

    m_lblPlayerName = CreateStrategicLabel(playerBox, "Player - John Alexander", m_fontText, m_palette.text, m_palette.shadow);
    m_lblPlayerRank = CreateStrategicLabel(playerBox, "Rank: 0", m_fontText, m_palette.text, m_palette.shadow);
    m_lblPlayerExp  = CreateStrategicLabel(playerBox, "Experience: 0", m_fontText, m_palette.text, m_palette.shadow);
    m_lblPlayerMaxUnits = CreateStrategicLabel(playerBox, "Max units: 0", m_fontText, m_palette.text, m_palette.shadow);
    m_lblPlayerMaxCmds  = CreateStrategicLabel(playerBox, "Max commanders: 0", m_fontText, m_palette.text, m_palette.shadow);

    playerSizer->Add(m_lblPlayerName, 0, wxALL, 8);
    playerSizer->Add(m_lblPlayerRank, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    playerSizer->Add(m_lblPlayerExp,  0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    playerSizer->Add(m_lblPlayerMaxUnits, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    playerSizer->Add(m_lblPlayerMaxCmds,  0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    playerBox->SetSizer(playerSizer);
    rootSizer->Add(playerBox, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    m_statsPanel->SetSizer(rootSizer);
}

void StrategicLevelFrame::RefreshStatsPage()
{
    if(!m_statsPanel)
        return;

    UpdateStrategicLabel(m_lblAllLightA, { { wxString::Format("%d", m_lossStats.alliance_all.light), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblAllLightE, { { wxString::Format("%d", m_lossStats.enemy_all.light), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblAllHeavyA, { { wxString::Format("%d", m_lossStats.alliance_all.heavy), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblAllHeavyE, { { wxString::Format("%d", m_lossStats.enemy_all.heavy), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblAllAirA, { { wxString::Format("%d", m_lossStats.alliance_all.air), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblAllAirE, { { wxString::Format("%d", m_lossStats.enemy_all.air), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblAllCmdA, { { wxString::Format("%d", m_lossStats.alliance_all.commanders), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblAllCmdE, { { wxString::Format("%d", m_lossStats.enemy_all.commanders), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);

    UpdateStrategicLabel(m_lblLvlLightA, { { wxString::Format("%d", m_lossStats.alliance_level.light), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblLvlLightE, { { wxString::Format("%d", m_lossStats.enemy_level.light), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblLvlHeavyA, { { wxString::Format("%d", m_lossStats.alliance_level.heavy), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblLvlHeavyE, { { wxString::Format("%d", m_lossStats.enemy_level.heavy), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblLvlAirA, { { wxString::Format("%d", m_lossStats.alliance_level.air), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblLvlAirE, { { wxString::Format("%d", m_lossStats.enemy_level.air), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblLvlCmdA, { { wxString::Format("%d", m_lossStats.alliance_level.commanders), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblLvlCmdE, { { wxString::Format("%d", m_lossStats.enemy_level.commanders), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);

    const CommanderRankRec* rec = FindRankRec(m_player.rank);
    const int maxUnits = rec ? rec->max_units : 0;
    const int maxCmds  = rec ? rec->max_commanders : 0;
    const int nextExp  = FindNextRankExp(m_player.rank);

    UpdateStrategicLabel(m_lblPlayerName, { { wxString::Format("Player - %s", wxString::FromUTF8(m_player.name)), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblPlayerRank, { { wxString::Format("Rank: %s", GetRankNameCz(m_player.rank)), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblPlayerExp, { { wxString::Format("Experience: %d (%d)", m_player.experience, nextExp), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblPlayerMaxUnits, { { wxString::Format("Max units: %d", maxUnits), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);
    UpdateStrategicLabel(m_lblPlayerMaxCmds, { { wxString::Format("Max commanders: %d", maxCmds), m_palette.text, &m_fontText } },
                         m_fontText, m_palette.shadow);

    m_statsPanel->Layout();
}

// ---------------- Data loading ----------------

wxString StrategicLevelFrame::FindStrategicStatsPath() const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::path(m_level.source_path).parent_path();
    if(base.empty() || !fs::exists(base, ec))
        base = fs::current_path(ec);
    return wxString::FromUTF8((base / "strategic_stats.json").string());
}

wxString StrategicLevelFrame::FindHodnostiDefPath() const
{
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path base = fs::path(m_level.source_path).parent_path();
    if(base.empty() || !fs::exists(base, ec))
        base = fs::current_path(ec);

    const std::array<fs::path, 6> candidates = {
        base / "HODNOSTI.DEF",
        base / "hodnosti.def",
        base / "DATA" / "HODNOSTI.DEF",
        base / "data" / "HODNOSTI.DEF",
        fs::current_path(ec) / "data" / "HODNOSTI.DEF",
        fs::current_path(ec) / "HODNOSTI.DEF"
    };

    for(const auto& p : candidates)
    {
        if(fs::exists(p, ec) && fs::is_regular_file(p, ec))
            return wxString::FromUTF8(p.string());
    }
    return "";
}

void StrategicLevelFrame::LoadRanksTable()
{
    if(!m_ranks.empty())
        return;

    wxString p = FindHodnostiDefPath();
    if(p.empty())
    {
        // Fallback defaults (kept identical to former form_strategic.*)
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
    if(!f)
        return;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if(data.empty())
        return;

    std::regex re(R"(DefineCommander\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*\))");
    for(auto it = std::sregex_iterator(data.begin(), data.end(), re); it != std::sregex_iterator(); ++it)
    {
        const auto& m = *it;
        if(m.size() < 6)
            continue;

        CommanderRankRec r;
        r.rank = std::stoi(m[1].str());
        r.max_units = std::stoi(m[2].str());
        r.actions_required = std::stoi(m[3].str());
        r.exp_required = std::stoi(m[4].str());
        r.max_commanders = std::stoi(m[5].str());
        m_ranks.push_back(r);
    }

    std::sort(m_ranks.begin(), m_ranks.end(),
              [](const CommanderRankRec& a, const CommanderRankRec& b) { return a.rank < b.rank; });
}

static bool ReadLossBlockFromObj_StrategicLevel(const std::string& obj, StrategicLevelFrame::LossBlock& out)
{
    bool any = false;
    any |= ParseJsonIntField(obj, "light", out.light);
    any |= ParseJsonIntField(obj, "heavy", out.heavy);
    any |= ParseJsonIntField(obj, "air", out.air);
    any |= ParseJsonIntField(obj, "commanders", out.commanders);
    return any;
}

void StrategicLevelFrame::LoadMissionStatsIfPresent()
{
    // Optional file:
    // {
    //   "all":   {"alliance":{"light":..,"heavy":..,"air":..,"commanders":..}, "enemy":{...}},
    //   "level": {"alliance":{...}, "enemy":{...}}
    // }
    wxString p = FindStrategicStatsPath();
    std::ifstream f(p.ToStdString());
    if(!f)
        return;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if(data.empty())
        return;

    auto extractObj = [&](const char* section, const char* side, std::string& outObj) -> bool
    {
        std::regex re(std::string("\"") + section + "\"\\s*:\\s*\\{([^}]*)\\}");
        std::smatch m;
        if(!std::regex_search(data, m, re) || m.size() < 2)
            return false;
        std::string sec = m[1].str();

        std::regex re2(std::string("\"") + side + "\"\\s*:\\s*\\{([^}]*)\\}");
        if(!std::regex_search(sec, m, re2) || m.size() < 2)
            return false;
        outObj = m[1].str();
        return true;
    };

    std::string obj;
    if(extractObj("all", "alliance", obj)) ReadLossBlockFromObj_StrategicLevel(obj, m_lossStats.alliance_all);
    if(extractObj("all", "enemy", obj))    ReadLossBlockFromObj_StrategicLevel(obj, m_lossStats.enemy_all);

    if(extractObj("level", "alliance", obj)) ReadLossBlockFromObj_StrategicLevel(obj, m_lossStats.alliance_level);
    if(extractObj("level", "enemy", obj))    ReadLossBlockFromObj_StrategicLevel(obj, m_lossStats.enemy_level);
}

// ---------------- Rank helpers ----------------

void StrategicLevelFrame::RecomputePlayerRank()
{
    int best = 0;
    for(const auto& r : m_ranks)
    {
        if(m_player.experience >= r.exp_required && m_player.actions >= r.actions_required)
            best = std::max(best, r.rank);
    }
    m_player.rank = best;
}

const StrategicLevelFrame::CommanderRankRec* StrategicLevelFrame::FindRankRec(int rank) const
{
    for(const auto& r : m_ranks)
        if(r.rank == rank)
            return &r;
    return nullptr;
}

int StrategicLevelFrame::FindNextRankExp(int current_rank) const
{
    int nextExp = 0;
    bool found = false;
    for(const auto& r : m_ranks)
    {
        if(r.rank == current_rank) { found = true; continue; }
        if(found && r.rank > current_rank) { nextExp = r.exp_required; break; }
    }
    if(nextExp <= 0)
    {
        for(const auto& r : m_ranks)
            nextExp = std::max(nextExp, r.exp_required);
    }
    return nextExp;
}

wxString StrategicLevelFrame::GetRankNameCz(int rank) const
{
    switch(rank)
    {
    case 0: return "Poručík";
    case 1: return "Nadporučík";
    case 2: return "Kapitán";
    case 3: return "Major";
    case 4: return "Podplukovník";
    case 5: return "Plukovník";
    case 6: return "Generálmajor";
    case 7: return "Generálporučík";
    case 8: return "Armádní generál";
    default: return wxString::Format("Rank %d", rank);
    }
}
