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
#include <unordered_map>
#include <unordered_set>
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
EVT_BUTTON(StrategicLevelFrame::ID_BTN_INFO, StrategicLevelFrame::OnShowInfo)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_BUY, StrategicLevelFrame::OnBuyUnits)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_BUY_CMD, StrategicLevelFrame::OnBuyCommander)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_SELL, StrategicLevelFrame::OnSellUnits)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_BUY_SHOP, StrategicLevelFrame::OnBuyShop)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_BUY_ACTION, StrategicLevelFrame::OnBuyAction)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_UNITS, StrategicLevelFrame::OnUnitsShop)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_UNITS_ACTION, StrategicLevelFrame::OnUnitsAction)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_ENDTURN, StrategicLevelFrame::OnEndTurn)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_LAUNCH, StrategicLevelFrame::OnLaunch)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_STRATEGIC_MAP, StrategicLevelFrame::OnShowStrategicMap)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_HIERARCHY, StrategicLevelFrame::OnShowHierarchy)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_RESOURCES, StrategicLevelFrame::OnShowResources)
EVT_BUTTON(StrategicLevelFrame::ID_BTN_STATS, StrategicLevelFrame::OnShowStats)
wxEND_EVENT_TABLE()

static void MakeChildTransparent(wxWindow* w)
{
    if (!w) return;
    wxWindow* parent = w->GetParent();
    if (parent)
        w->SetBackgroundColour(parent->GetBackgroundColour());

    w->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
}

static void MakeChildrenTransparentRecursive(wxWindow* root)
{
    if (!root) return;

    // Použij i na root – např. panely vevnitř
    MakeChildTransparent(root);

    const wxWindowList& children = root->GetChildren();
    for (wxWindowList::const_iterator it = children.begin(); it != children.end(); ++it)
    {
        wxWindow* child = *it;
        MakeChildrenTransparentRecursive(child);
    }
}

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
    if (loaded)
        return;

    const auto fontPath = StrategicFontPath();
    if (std::filesystem::exists(fontPath))
        // wxFont::AddPrivateFont(wxString::FromUTF8(fontPath.string()));
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

    if (!font.IsOk())
    {
        font = wxFont(wxFontInfo(wxSize(0, pixelSize))
            .Family(wxFONTFAMILY_MODERN)
            .Style(wxFONTSTYLE_NORMAL)
            .Weight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL));
        font.SetPixelSize(wxSize(0, pixelSize));
    }

    return font;
}

// Přidejte tuto pomocnou funkci endsWith na začátek souboru (nebo do anonymního namespace, kde ji potřebujete)
static bool endsWith(const std::string& s, const std::string& suf)
{
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

namespace
{
    struct HierarchyDragData
    {
        bool valid = false;
        bool fromSlot = false;
        std::string slotId;
        std::string type;
        uint32_t commander_uid = 0; // for commanders
        int rank = -1; // for commanders
        wxString name;
    };

    HierarchyDragData ParseHierarchyDragData(const wxString& data)
    {
        HierarchyDragData parsed;
        wxArrayString tokens = wxSplit(data, ':', '\0');
        if (tokens.empty())
            return parsed;

        const wxString kind = tokens[0];

        // unit:<name>
        if (kind == "unit" && tokens.size() >= 2)
        {
            parsed.valid = true;
            parsed.type = "unit";
            parsed.name = tokens[1];
            return parsed;
        }

        // commander:<uid>:<rank>:<name>
        // commander:<rank>:<name>  (backward compatibility)
        // commander:<name>         (very old compatibility)
        if (kind == "commander" && tokens.size() >= 2)
        {
            parsed.valid = true;
            parsed.type = "commander";
            if (tokens.size() >= 4)
            {
                long uid = 0;
                long r = -1;
                if (tokens[1].ToLong(&uid) && uid > 0)
                    parsed.commander_uid = (uint32_t)uid;
                if (tokens[2].ToLong(&r))
                    parsed.rank = (int)r;
                parsed.name = tokens[3];
            }
            else if (tokens.size() >= 3)
            {
                long r = -1;
                if (tokens[1].ToLong(&r))
                    parsed.rank = (int)r;
                parsed.name = tokens[2];
            }
            else
            {
                parsed.name = tokens[1];
            }
            return parsed;
        }

        // slot:<slotId>:<type>:<name>
        // slot:<slotId>:commander:<uid>:<rank>:<name>
        // slot:<slotId>:commander:<rank>:<name>
        if (kind == "slot" && tokens.size() >= 4)
        {
            parsed.valid = true;
            parsed.fromSlot = true;
            parsed.slotId = tokens[1].ToStdString();
            parsed.type = tokens[2].ToStdString();

            if (parsed.type == "commander" && tokens.size() >= 6)
            {
                long uid = 0;
                long r = -1;
                if (tokens[3].ToLong(&uid) && uid > 0)
                    parsed.commander_uid = (uint32_t)uid;
                if (tokens[4].ToLong(&r))
                    parsed.rank = (int)r;
                parsed.name = tokens[5];
            }
            else if (parsed.type == "commander" && tokens.size() >= 5)
            {
                long r = -1;
                if (tokens[3].ToLong(&r))
                    parsed.rank = (int)r;
                parsed.name = tokens[4];
            }
            else
            {
                parsed.name = tokens[3];
            }
            return parsed;
        }

        return parsed;
    }

    class HierarchySlotDropTarget : public wxTextDropTarget
    {
    public:
        HierarchySlotDropTarget(StrategicLevelFrame* owner, std::string slotId)
            : m_owner(owner)
            , m_slotId(std::move(slotId))
        {
        }

        bool OnDropText(wxCoord, wxCoord, const wxString& data) override
        {
            if (!m_owner)
                return false;
            m_owner->ApplyHierarchyDrop(m_slotId, data);
            return true;
        }

    private:
        StrategicLevelFrame* m_owner = nullptr;
        std::string m_slotId;
    };

    class HierarchyPoolDropTarget : public wxTextDropTarget
    {
    public:
        HierarchyPoolDropTarget(StrategicLevelFrame* owner, std::string type)
            : m_owner(owner)
            , m_type(std::move(type))
        {
        }

        bool OnDropText(wxCoord, wxCoord, const wxString& data) override
        {
            if (!m_owner)
                return false;
            HierarchyDragData parsed = ParseHierarchyDragData(data);
            if (!parsed.valid || !parsed.fromSlot || parsed.type != m_type)
                return false;
            m_owner->ClearHierarchySlot(parsed.slotId);
            return true;
        }

    private:
        StrategicLevelFrame* m_owner = nullptr;
        std::string m_type;
    };

    // Parse SetResearchFlag(N) entries from a LEVEL_XX.DEF file
    std::set<int> ParseResearchFlagsFromDef(const std::filesystem::path& defPath)
    {
        std::set<int> flags;
        std::error_code ec;
        if (!std::filesystem::exists(defPath, ec))
            return flags;
        
        std::ifstream f(defPath);
        if (!f)
            return flags;
        
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        
        // Match SetResearchFlag(N) - the number N is the unit type_id
        std::regex re(R"(SetResearchFlag\s*\(\s*(\d+)\s*\))");
        for (auto it = std::sregex_iterator(content.begin(), content.end(), re);
             it != std::sregex_iterator(); ++it)
        {
            const auto& m = *it;
            if (m.size() >= 2)
            {
                int flag = std::stoi(m[1].str());
                flags.insert(flag);
            }
        }
        return flags;
    }

    // Get cumulative research flags from LEVEL_01.DEF up to current level
    std::set<int> GetCumulativeResearchFlags(const std::filesystem::path& currentLevelPath)
    {
        std::set<int> cumulative;
        namespace fs = std::filesystem;
        std::error_code ec;
        
        // Get directory and current level number
        fs::path dir = currentLevelPath.parent_path();
        std::string stem = currentLevelPath.stem().string();
        
        // Extract level number from filename like "LEVEL_05" or "level_05"
        int currentLevel = 0;
        std::regex levelRe(R"([Ll][Ee][Vv][Ee][Ll]_?(\d+))");
        std::smatch m;
        if (std::regex_search(stem, m, levelRe) && m.size() >= 2)
            currentLevel = std::stoi(m[1].str());
        
        if (currentLevel <= 0)
            return cumulative;
        
        // Load flags from LEVEL_01.DEF up to current level
        for (int lvl = 1; lvl <= currentLevel; ++lvl)
        {
            // Try various filename patterns
            std::vector<std::string> patterns = {
                std::string("LEVEL_") + (lvl < 10 ? "0" : "") + std::to_string(lvl) + ".DEF",
                std::string("level_") + (lvl < 10 ? "0" : "") + std::to_string(lvl) + ".def",
                std::string("LEVEL") + std::to_string(lvl) + ".DEF",
                std::string("level") + std::to_string(lvl) + ".def"
            };
            
            for (const auto& pattern : patterns)
            {
                fs::path candidate = dir / pattern;
                if (fs::exists(candidate, ec))
                {
                    auto flags = ParseResearchFlagsFromDef(candidate);
                    cumulative.insert(flags.begin(), flags.end());
                    break;
                }
            }
        }
        
        return cumulative;
    }
} // namespace

static wxBitmap RenderStrategicLabel(const std::vector<StrategicTextSpan>& spans, const wxFont& fallbackFont,
    const wxColour& shadow, const wxColour* background = nullptr)
{
    if (spans.empty())
        return wxBitmap(1, 1);

    //wxScreenDC measure;
    wxMemoryDC measure;
    wxBitmap tmp(1, 1);
    measure.SelectObject(tmp);
    int totalW = 0;
    int maxH = 0;
    std::vector<wxSize> extents;
    extents.reserve(spans.size());

    for (const auto& span : spans)
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

    if (background)
    {
        dc.SetPen(*background);
        dc.SetBrush(*background);
        dc.DrawRectangle(0, 0, bmpW, bmpH);
    }

    const int shadowOffset = 1;
    int x = paddingX;
    for (size_t i = 0; i < spans.size(); ++i)
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
    for (size_t i = 0; i < spans.size(); ++i)
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
    if (!target)
        return;

    if (auto bmp = RenderStrategicLabel(spans, fallbackFont, shadow, background); bmp.IsOk())
        target->SetBitmap(bmp);
}

static wxStaticBitmap* CreateStrategicLabel(wxWindow* parent, const std::vector<StrategicTextSpan>& spans,
    const wxFont& fallbackFont, const wxColour& shadow, const wxColour* background = nullptr)
{
    auto* b = new wxStaticBitmap(parent, wxID_ANY, wxBitmap(1, 1));

    // Helps with background blending on Windows
    b->SetBackgroundColour(parent ? parent->GetBackgroundColour() : *wxBLACK);
    b->SetBackgroundStyle(wxBG_STYLE_PAINT);

    UpdateStrategicLabel(b, spans, fallbackFont, shadow, background);
    return b;
}

static bool g_bakeStrategicBorders = true;

static wxStaticBitmap* CreateStrategicLabel(wxWindow* parent, const wxString& text, const wxFont& font,
    const wxColour& color, const wxColour& shadow)
{
    std::vector<StrategicTextSpan> spans = { { text, color, &font } };
    return CreateStrategicLabel(parent, spans, font, shadow);
}

//static wxBitmapButton* CreateStrategicButton(wxWindow* parent, int id, const wxString& text,
//                                             const wxFont& font, const wxColour& textColor,
//                                             const wxColour& shadow, const wxColour& background,
//                                             const wxSize& minSize = wxDefaultSize)
//{
//    std::vector<StrategicTextSpan> spans = { { text, textColor, &font } };
//    wxBitmap bmp = RenderStrategicLabel(spans, font, shadow, &background);
//    if(!bmp.IsOk())
//        bmp = wxBitmap(1, 1);
//
//    auto* b = new wxBitmapButton(parent, id, bmp);
//    if(minSize != wxDefaultSize)
//        b->SetMinSize(minSize);
//
//    b->SetBackgroundColour(background);
//    return b;
//}

static wxString WrapButtonLabel(const wxString& src, int maxLen)
{
    wxString out;
    out.reserve(src.length() + 8);

    int col = 0;
    int lastSpacePos = -1;
    int lastOutPos = 0;

    for (size_t i = 0; i < src.length(); ++i)
    {
        const wxChar ch = src[i];
        out.Append(ch);
        col++;

        if (ch == ' ')
        {
            lastSpacePos = (int)i;
            lastOutPos = (int)out.length();
        }

        if (col >= maxLen && lastSpacePos >= 0)
        {
            // nahradíme poslední mezeru za \n
            out[lastOutPos - 1] = '\n';
            col = (int)(out.length() - lastOutPos);
            lastSpacePos = -1;
        }
    }

    return out;
}

// Oprava: změňte návratový typ na wxButton* (nebo použijte správný typ v místě volání)
static wxButton* CreateStrategicButton(
    wxWindow* parent,
    int id,
    const wxString& text,
    const wxFont& font,
    const wxColour& textColor,
    const wxColour& background,
    const wxSize& minSize = wxDefaultSize)
{
    // např. wrap na ~12 znaků v řádku – můžeš doladit
    wxString wrapped = WrapButtonLabel(text, 12);

    wxButton* btn = new wxButton(parent, id, wrapped);

    btn->SetFont(font);
    btn->SetForegroundColour(textColor);
    btn->SetBackgroundColour(background);

    if (minSize != wxDefaultSize)
    {
        btn->SetMinSize(minSize);
    }

    // volitelně – trochu větší vnitřní okraje (padding), aby to vypadalo líp
    // btn->SetMargins(12, 8);   // funguje od wx 3.1+, pokud máš starší verzi → ignoruj

    return btn;
}

static std::filesystem::path GetUnitsJsonPath()
{
    return std::filesystem::current_path() / "data" / "units.json";
}

static bool ParseJsonIntField(const std::string& obj, const char* key, int& outValue)
{
    if (!key)
        return false;

    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos)
        return false;

    pos = obj.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return false;

    ++pos;
    while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos])))
        ++pos;

    const char* start = obj.c_str() + pos;
    char* end = nullptr;
    long value = std::strtol(start, &end, 10);
    if (end == start)
        return false;

    outValue = static_cast<int>(value);
    return true;
}


static bool ParseJsonStringField(const std::string& obj, const char* key, std::string& outValue)
{
    if (!key)
        return false;

    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos)
        return false;

    pos = obj.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return false;

    ++pos;
    while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos])))
        ++pos;

    if (pos >= obj.size() || obj[pos] != '"')
        return false;
    ++pos;

    std::string s;
    bool escaped = false;
    for (; pos < obj.size(); ++pos)
    {
        char c = obj[pos];
        if (escaped)
        {
            switch (c)
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
        if (c == '\\')
        {
            escaped = true;
            continue;
        }
        if (c == '"')
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
    for (char c : s)
    {
        switch (c)
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
    switch (rank)
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

static bool LoadUnitCostsFromJson(const std::filesystem::path& path,
    std::unordered_map<int, int>& outCosts,
    std::unordered_map<int, std::string>* outCategories = nullptr)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();
    if (content.empty())
        return false;

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    size_t objStart = std::string::npos;

    for (size_t i = 0; i < content.size(); ++i)
    {
        char c = content[i];
        if (inString)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (c == '\\')
            {
                escaped = true;
            }
            else if (c == '"')
            {
                inString = false;
            }
            continue;
        }

        if (c == '"')
        {
            inString = true;
            continue;
        }

        if (c == '{')
        {
            if (depth == 0)
                objStart = i;
            ++depth;
        }
        else if (c == '}')
        {
            if (depth > 0)
            {
                --depth;
                if (depth == 0 && objStart != std::string::npos)
                {
                    const std::string obj = content.substr(objStart, i - objStart + 1);
                    int index = -1;
                    int cost = -1;
                    if (ParseJsonIntField(obj, "index", index) && ParseJsonIntField(obj, "cost_buy", cost))
                    {
                        outCosts[index] = cost;
                        if (outCategories)
                        {
                            std::string cat;
                            if (ParseJsonStringField(obj, "category", cat) && !cat.empty())
                                (*outCategories)[index] = cat;
                        }
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
    auto notspace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
    return s;
}

static std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

static std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
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
    if (token.empty())
        return token;
    token = to_upper(token);
    if (token[0] == 'M')
        token[0] = 'T';
    return token;
}

static bool read_text_file(const std::filesystem::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

static void append_text_snippet(wxString& info, const std::string& label, const std::string& raw)
{
    if (raw.empty())
        return;

    // Quick cleanup: remove CR and typical in-game control marks.
    std::string s;
    s.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char c = (unsigned char)raw[i];
        if (c == '\r')
            continue;
        if (c == '~' || c == 0x1A)
            continue;
        s.push_back((char)c);
    }

    // Limit preview to keep messagebox readable.
    const size_t kMax = 600;
    if (s.size() > kMax)
        s = s.substr(0, kMax) + "...";

    info << "\n" << label << "\n";
    info << wxString(char2wstringCP895(s.c_str())) << "\n";
}

static void try_append_text_set(wxString& info, const std::filesystem::path& base_dir, std::string mission_token)
{
    if (mission_token.empty())
        return;

    std::string base = mission_to_text_base(mission_token);

    // Best-effort: if token ends with digit (M02_02), try A (T02_02A)
    if (!base.empty())
    {
        char last = base.back();
        if (last >= '0' && last <= '9')
            base.push_back('A');
    }

    auto load_and_append = [&](const std::string& suffix, const char* caption)
        {
            std::string raw;
            if (read_text_file(base_dir / (base + suffix), raw))
                append_text_snippet(info, wxString::Format("%s (%s%s)", caption, base, suffix).ToStdString(), raw);
        };

    load_and_append("", "Briefing");
    load_and_append(".OK", "Victory");
    load_and_append(".BAD", "Defeat");
    load_and_append(".S", "Counter-attack");
}

// ---------------- Campaign start territory helper ----------------
// In some levels the campaign starting territory is NOT T01.
// Heuristic: pick the first territory whose mission has no Briefing text file.
// (In the original game, those typically represent the already-secured / home region.)
static std::filesystem::path FindTextsDirForLevel(const LevelData& level)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path texts_dir;

    auto try_dir = [&](const fs::path& p)
    {
        if (texts_dir.empty() && !p.empty() && fs::exists(p, ec) && fs::is_directory(p, ec))
            texts_dir = p;
    };

    // Walk up from level DEF location and try common layouts
    fs::path base = fs::path(level.source_path).parent_path();
    for (int i = 0; i < 8 && !base.empty() && texts_dir.empty(); ++i)
    {
        try_dir(base / "DATA" / "TEXTS");
        try_dir(base / "DATA" / "texts");
        try_dir(base / "TEXTS");
        try_dir(base / "texts");
        base = base.parent_path();
    }

    // Fallback: current working directory
    if (texts_dir.empty())
    {
        const fs::path cwd = fs::current_path(ec);
        try_dir(cwd / "DATA" / "TEXTS");
        try_dir(cwd / "DATA" / "texts");
        try_dir(cwd / "TEXTS");
        try_dir(cwd / "texts");
    }

    return texts_dir;
}

static bool HasBriefingForMissionToken(const std::filesystem::path& texts_dir, const std::string& mission_token)
{
    if (texts_dir.empty() || mission_token.empty() || mission_token == "none")
        return false;

    namespace fs = std::filesystem;
    std::error_code ec;

    std::string base = mission_to_text_base(mission_token);
    if (base.empty())
        return false;

    // Briefing is stored as the A variant (Txx_yyA) when token ends with digit.
    char last = base.back();
    if (last >= '0' && last <= '9')
        base.push_back('A');

    const fs::path p1 = texts_dir / base;
    const fs::path p2 = texts_dir / to_lower(base);
    const fs::path p3 = texts_dir / to_upper(base);

    return fs::exists(p1, ec) || fs::exists(p2, ec) || fs::exists(p3, ec);
}

static int ChooseDefaultStartTerritoryId_NoBriefing(const LevelData& level)
{
    if (level.territories.empty())
        return 0;

    const auto texts_dir = FindTextsDirForLevel(level);

    // 1) Prefer a territory whose mission has NO briefing file.
    for (const auto& t : level.territories)
    {
        if (!HasBriefingForMissionToken(texts_dir, t.mission))
            return t.id;
    }

    // 2) Fallback: first territory.
    return level.territories.front().id;
}

static std::vector<int> ChooseStartTerritories_NoBriefing(const LevelData& level)
{
    std::vector<int> out;
    if (level.territories.empty())
        return out;

    const auto texts_dir = FindTextsDirForLevel(level);

    for (const auto& t : level.territories)
    {
        // "start territories" = those whose mission has NO briefing file
        if (!HasBriefingForMissionToken(texts_dir, t.mission))
            out.push_back(t.id);
    }

    // make deterministic & unique
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

StrategicLevelFrame::StrategicLevelFrame(MainFrame* parent, const LevelData& level)
    : wxFrame(parent, wxID_ANY, "Strategic Level", wxDefaultPosition, wxSize(1390, 1050),
        wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
    m_main(parent),
    m_spellData(parent ? parent->spell_data : nullptr),
    m_level(level)
{
    static bool seeded = false;
    if (!seeded) { std::srand((unsigned)std::time(nullptr)); seeded = true; }
    m_money = 0;
    m_research = 0;
    m_playerUnits = m_level.start_units;

    // init territory mission state from LevelData
    for (const auto& t : m_level.territories)
    {
        m_territoryCurrentMission[t.id] = t.mission;
        m_territoryLaunchCount[t.id] = 0;
    }

    // init default resources state (20/20) for all territories
    for (const auto& t : m_level.territories)
        m_territoryResources[t.id] = TerritoryResourceState{};

    // Load cumulative research flags from LEVEL_01..current for Game mode unit filtering
    {
        namespace fs = std::filesystem;
        fs::path defPath = fs::path(m_level.source_path);
        m_levelResearchFlags = GetCumulativeResearchFlags(defPath);
    }

    BuildMenu();

    BuildUI();
    TryLoadBackground();
    CenterOnParent();

    // Default: start NEW strategic state (do NOT auto-load autosave).
    // If autosave exists, ask user.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const auto autosave = GetStrategicStatePath(m_level);

        if (fs::exists(autosave, ec))
        {
            const int rc = wxMessageBox(
                "Autosave for this level exists.\n\n"
                "YES = Continue (load autosave)\n"
                "NO  = Start new (keep old autosave as .bak)\n"
                "CANCEL = Start new (do not touch autosave)",
                "Strategic",
                wxYES_NO | wxCANCEL | wxICON_QUESTION,
                this);

            if (rc == wxYES)
            {
                LoadStrategicState();
            }
            else if (rc == wxNO)
            {
                // Keep old file, start fresh; do NOT overwrite silently.
                fs::path bak = autosave;
                bak += ".bak";
                fs::rename(autosave, bak, ec);
                // Start new: keep ctor defaults. We won't create a new autosave until something changes.
            }
            else
            {
                // CANCEL: start new, leave autosave untouched.
            }
        }
    }

    RefreshUI();


    Bind(wxEVT_ACTIVATE, &StrategicLevelFrame::OnActivate, this);

}

static std::filesystem::path GetStableBaseDir() {
    return std::filesystem::current_path();
}

// Forward declarations (definitions are later in this file)

// ------------------------------------------------------------------
// Research persistence glue
// We MUST NOT change SaveStrategicStateFile / LoadStrategicStateFile signatures.
// So we pass research state via thread-local pointers set by StrategicLevelFrame::SaveStrategicState / LoadStrategicState.
// ------------------------------------------------------------------
struct ResearchPersistSaveView
{
    int activeId = -1;
    int activeIndex = -1;
    int allocPerTurn = 0;
    const std::unordered_map<int, int>* progressById = nullptr;
    const std::unordered_set<int>* completed = nullptr;
};

struct ResearchPersistLoadView
{
    int* activeId = nullptr;
    int* activeIndex = nullptr;
    int* allocPerTurn = nullptr;
    std::unordered_map<int, int>* progressById = nullptr;
    std::unordered_set<int>* completed = nullptr;
};

static thread_local const ResearchPersistSaveView* g_researchPersistSave = nullptr;
static thread_local ResearchPersistLoadView* g_researchPersistLoad = nullptr;

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
    std::vector<StrategicLevelFrame::CommanderRec>& playerCommanders,
    std::vector<StrategicLevelFrame::CommanderRec>& availableCommanders,
    int& cmdGenWindowStartTurn,
    int& cmdGenCountInWindow,
    bool& gameModeEnabled,
    std::vector<int>& ownedTerritories,
    std::unordered_map<int, StrategicLevelFrame::TerritoryResourceState>& territoryResources,
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
    const std::vector<StrategicLevelFrame::CommanderRec>& playerCommanders,
    const std::vector<StrategicLevelFrame::CommanderRec>& availableCommanders,
    int cmdGenWindowStartTurn,
    int cmdGenCountInWindow,
    bool gameModeEnabled,
    const std::vector<int>& ownedTerritories,
    const std::unordered_map<int, StrategicLevelFrame::TerritoryResourceState>& territoryResources,
    const std::string& timestamp);

void StrategicLevelFrame::BuildMenu()
{
    // Only build once
    if (GetMenuBar() != nullptr)
        return;

    auto* bar = new wxMenuBar();

    auto* file = new wxMenu();
    file->Append(ID_MENU_SAVE_GAME, (L"&Save game...\tCtrl+S"));
    file->Append(ID_MENU_LOAD_GAME, (L"&Load game...\tCtrl+L"));
    bar->Append(file, "&File");

    auto* options = new wxMenu();
    options->Append(ID_MENU_OPTIONS_AUDIO, (L"&Audio...\tCtrl+O"));
    bar->Append(options, "&Options");

    auto* game = new wxMenu();
    game->AppendCheckItem(ID_MENU_GAME_MODE_TOGGLE, L"&Enabled");
    bar->Append(game, "&Game mode");

    SetMenuBar(bar);

    Bind(wxEVT_MENU, &StrategicLevelFrame::OnSaveGame, this, ID_MENU_SAVE_GAME);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnLoadGame, this, ID_MENU_LOAD_GAME);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnOptionsAudio, this, ID_MENU_OPTIONS_AUDIO);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnToggleGameMode, this, ID_MENU_GAME_MODE_TOGGLE);

}

static std::string LevelKeyFromSourcePath(const std::string& src)
{
    // English identifiers/comments, Czech UI is fine elsewhere.
    // We want stable per-level key like "level_03" even if path differs.
    std::filesystem::path p(src);
    std::string stem = p.stem().string(); // e.g. "LEVEL_03"
    stem = to_lower(stem);
    if (stem.empty())
        stem = "unknown_level";
    return stem;
}

static std::filesystem::path GetStrategicSaveDir(const LevelData& level)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // One stable root for ALL strategic saves (autosave + slots)
    fs::path dir = fs::path(GetStableBaseDir()) / "save" / "strategic" / LevelKeyFromSourcePath(level.source_path);
    fs::create_directories(dir, ec);
    return dir;
}


static std::filesystem::path GetStrategicSaveSlotPath(const LevelData& level, int slot)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "slot_%02d.json", slot);
    return GetStrategicSaveDir(level) / buf;
}

static bool PeekStrategicSaveSummary(const std::filesystem::path& path, int& outMoney, int& outRank, int& outExp, std::string& outTs)
{
    outMoney = 0; outRank = 0; outExp = 0; outTs.clear();

    std::ifstream f(path);
    if (!f)
        return false;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty())
        return false;

    std::smatch m;
    if (std::regex_search(data, m, std::regex("\"money\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        outMoney = std::stoi(m[1].str());

    // timestamp optional
    std::regex ts_re("\"timestamp\"\\s*:\\s*\"([^\"]*)\"");
    if (std::regex_search(data, m, ts_re) && m.size() > 1)
        outTs = m[1].str();

    // player object optional
    std::regex player_obj_re("\"player\"\\s*:\\s*\\{([^}]*)\\}");
    if (std::regex_search(data, m, player_obj_re) && m.size() > 1)
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

    for (int i = 1; i <= 10; ++i)
    {
        const auto p = GetStrategicSaveSlotPath(m_level, i);
        std::error_code ec;
        if (std::filesystem::exists(p, ec))
        {
            int money = 0, rank = 0, xp = 0;
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
    if (dlg.ShowModal() != wxID_OK)
        return;

    const int slot = dlg.GetSelection() + 1;
    const auto path = GetStrategicSaveSlotPath(m_level, slot);

    // Save full strategic state into slot file
    SaveStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
        m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
        m_playerCommanders, m_availableCommanders, m_cmdGenWindowStartTurn, m_cmdGenCountInWindow,
        m_gameModeEnabled, m_ownedTerritories, m_territoryResources,
        /*timestamp*/NowIsoLocal());

    wxMessageBox(wxString::Format("Saved to slot %02d.", slot), "Save game", wxOK | wxICON_INFORMATION, this);
}

void StrategicLevelFrame::OnLoadGame(wxCommandEvent&)
{
    wxArrayString choices;
    choices.reserve(10);

    std::vector<bool> exists(10, false);
    for (int i = 1; i <= 10; ++i)
    {
        const auto p = GetStrategicSaveSlotPath(m_level, i);
        std::error_code ec;
        exists[i - 1] = std::filesystem::exists(p, ec);
        if (exists[i - 1])
        {
            int money = 0, rank = 0, xp = 0;
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
    if (dlg.ShowModal() != wxID_OK)
        return;

    const int slot = dlg.GetSelection() + 1;
    if (!exists[slot - 1])
    {
        wxMessageBox("This slot is empty.", "Load game", wxOK | wxICON_WARNING, this);
        return;
    }

    const auto path = GetStrategicSaveSlotPath(m_level, slot);

    std::string loaded_level_def;
    std::string ts;
    if (!LoadStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
        m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
        m_playerCommanders, m_availableCommanders, m_cmdGenWindowStartTurn, m_cmdGenCountInWindow,
        m_gameModeEnabled, m_ownedTerritories, m_territoryResources,
        &loaded_level_def, &ts))
    {
        wxMessageBox("Failed to load the saved game.", "Load game", wxOK | wxICON_ERROR, this);
        return;
    }

    if (GetMenuBar())
    {
        auto* item = GetMenuBar()->FindItem(ID_MENU_GAME_MODE_TOGGLE);
        if (item) item->Check(m_gameModeEnabled);
    }

    // Save slot may belong to a different strategic LEVEL_XX.DEF.
    // In that case, automatically switch to the correct level and load there.
    if (!loaded_level_def.empty() && loaded_level_def != m_level.source_path)
    {
        if (!m_main)
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
        if (!loader.LoadLevelDef(loaded_level_def, lvl, &err))
        {
            wxMessageBox(L"Failed to load the level DEF from this save:\n" + wxString::FromUTF8(err),
                L"Load game", wxOK | wxICON_ERROR, this);
            return;
        }

        // Re-load the save file using the correct level, so territory defaults match.
        int turn = 1, money = 0, research = 0, selTerr = -1;
        PlayerProgress pl;
        std::unordered_map<int, std::string> terrMission;
        std::unordered_map<int, int> terrLaunch;
        std::vector<LevelData::PlayerUnitAdd> units;
        std::string def2, ts2;


        std::vector<CommanderRec> playerCmds2;
        std::vector<CommanderRec> availCmds2;
        int windowStart2 = 1;
        int genCount2 = 0;


        bool gm2 = false;
        std::vector<int> owned2;
                std::unordered_map<int, TerritoryResourceState> terrRes;

                if (!LoadStrategicStateFile(path, lvl, turn, money, research, selTerr, pl, terrMission, terrLaunch, units, playerCmds2, availCmds2, windowStart2, genCount2, gm2, owned2, terrRes, &def2, &ts2)) {
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
        win->m_playerCommanders = std::move(playerCmds2);
        win->m_availableCommanders = std::move(availCmds2);
        win->m_cmdGenWindowStartTurn = windowStart2;
        win->m_cmdGenCountInWindow = genCount2;

        win->m_gameModeEnabled = gm2;
        win->m_ownedTerritories = std::move(owned2);
        win->TryLoadBackground();
        win->RefreshUI();
        if (win->m_selectedTerritory >= 0)
            win->SelectTerritoryById(win->m_selectedTerritory);

        win->Show();
        win->Raise();

        // Close this (wrong-level) window.
        Close(true);
        return;
    }

    if (m_selectedTerritory >= 0)
        SelectTerritoryById(m_selectedTerritory);

    RefreshUI();
    wxMessageBox(wxString::Format("Loaded slot %02d.", slot), "Load game", wxOK | wxICON_INFORMATION, this);
}


void StrategicLevelFrame::MarkOverlayDirty()
{
    m_overlayDirty = true;
    if (m_leftBook && m_leftBook->GetCurrentPage() == m_resourcesPanel)
    {
        if (m_resourcesCanvas) m_resourcesCanvas->Refresh();
    }
    else
    {
        if (m_mapCanvas) m_mapCanvas->Refresh();
        else if (m_mapPanel) m_mapPanel->Refresh();
    }
}

static int PickStartTerritoryIdForGameMode(const LevelData& level)
{
    // 1) Some levels explicitly mark the "home" territory with an empty mission token.
    for (const auto& t : level.territories)
        if (trim(t.mission).empty())
            return t.id;

    // 2) Otherwise, pick the first territory that has NO briefing text file.
    // (This matches the original campaign behavior for e.g. LEVEL_07 where start != T01.)
    const int byNoBrief = ChooseDefaultStartTerritoryId_NoBriefing(level);
    if (byNoBrief > 0)
        return byNoBrief;

    // 3) Fallback: first territory in list.
    return !level.territories.empty() ? level.territories.front().id : 1;
}

void StrategicLevelFrame::ApplyTerritoryVisibility()
{
    // Determine max territory id
    int maxId = 0;
    for (const auto& t : m_level.territories)
        maxId = std::max(maxId, t.id);

    m_visibleTerritory.assign(std::max(maxId + 1, 1), 1);

    if (!m_gameModeEnabled)
        return; // debug mode: all visible

    // In game mode: visible = owned + neighbors(owned)
    std::fill(m_visibleTerritory.begin(), m_visibleTerritory.end(), 0);

    // Ensure we always have a start territory; otherwise everything becomes "fog".
    if (m_ownedTerritories.empty())
        m_ownedTerritories.push_back(PickStartTerritoryIdForGameMode(m_level));


    auto mark = [&](int tid)
    {
        if (tid > 0 && tid < (int)m_visibleTerritory.size())
            m_visibleTerritory[tid] = 1;
    };

    for (int tid : m_ownedTerritories)
        mark(tid);

    for (int tid : m_ownedTerritories)
    {
        if (tid <= 0 || tid >= (int)m_territoryAdjMask.size())
            continue;

        uint32_t mask = m_territoryAdjMask[tid];
        for (int nb = 1; nb < (int)m_visibleTerritory.size() && nb < 32; ++nb)
        {
            if (mask & (1u << nb))
                mark(nb);
        }
    }
}
void StrategicLevelFrame::OnToggleGameMode(wxCommandEvent& ev)
{
    m_gameModeEnabled = ev.IsChecked();

    if (m_gameModeEnabled)
    {
        // Ensure at least one owned territory (start territory).
        if (m_ownedTerritories.empty())
        {
            // Some levels start with MULTIPLE owned territories = those WITHOUT briefing
            m_ownedTerritories = ChooseStartTerritories_NoBriefing(m_level);

            // Fallback: at least one
            if (m_ownedTerritories.empty() && !m_level.territories.empty())
                m_ownedTerritories.push_back(m_level.territories.front().id);
        }

        m_hoverTerritory = 0;

    }

    SaveStrategicState(); // persist into autosave.json

    // Rebuild background, because baked borders must be ON in editor mode and OFF in game mode.
    TryLoadBackground();
    ApplyTerritoryVisibility();
    MarkOverlayDirty();
    RefreshUI();

    if (m_mapCanvas) m_mapCanvas->Refresh();
    else if (m_mapPanel) m_mapPanel->Refresh();

}


void StrategicLevelFrame::OnOptionsAudio(wxCommandEvent& ev)
{
    if (!m_spellData || !m_spellData->sounds || !m_spellData->sounds->channels || !m_spellData->midi)
    {
        wxMessageBox("Audio system is not initialized.", "Audio", wxOK | wxICON_WARNING, this);
        return;
    }

    // uložíme původní hodnoty kvůli Cancel
    const double oldSfx = m_spellData->sounds->channels->GetVolume(); // 0..1
    const double oldMusic = m_spellData->midi->GetVolume();            // 0..1

    wxDialog dlg(this, wxID_ANY, "Audio options", wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    auto* sizerTop = new wxBoxSizer(wxVERTICAL);

    auto* lblMusic = new wxStaticText(&dlg, wxID_ANY, "Music volume");
    auto* sldMusic = new wxSlider(&dlg, wxID_ANY,
        (int)std::lround(oldMusic * 100.0),
        0, 100,
        wxDefaultPosition, wxSize(300, -1),
        wxSL_HORIZONTAL | wxSL_VALUE_LABEL);

    auto* lblSfx = new wxStaticText(&dlg, wxID_ANY, "Sound volume");
    auto* sldSfx = new wxSlider(&dlg, wxID_ANY,
        (int)std::lround(oldSfx * 100.0),
        0, 100,
        wxDefaultPosition, wxSize(300, -1),
        wxSL_HORIZONTAL | wxSL_VALUE_LABEL);

    sizerTop->Add(lblMusic, 0, wxALL, 8);
    sizerTop->Add(sldMusic, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    sizerTop->Add(lblSfx, 0, wxALL, 8);
    sizerTop->Add(sldSfx, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
    sizerTop->Add(btns, 0, wxALL | wxEXPAND, 8);

    dlg.SetSizerAndFit(sizerTop);

    auto applyVolumes = [&]()
        {
            const double mv = sldMusic->GetValue() / 100.0;
            const double sv = sldSfx->GetValue() / 100.0;

            m_spellData->midi->SetVolume(mv);
            m_spellData->sounds->channels->SetVolume(sv);
        };

    // okamžitě aplikuj při posunu
    sldMusic->Bind(wxEVT_SLIDER, [&](wxCommandEvent&)
        {
            applyVolumes();
        });

    sldSfx->Bind(wxEVT_SLIDER, [&](wxCommandEvent&)
        {
            applyVolumes();
        });

    // otevření dialogu
    const int rc = dlg.ShowModal();

    if (rc == wxID_OK)
    {
        // necháme nastavené (persistenci řeší MyApp::OnExit -> ini)
        applyVolumes();
    }
    else
    {
        // Cancel -> vrať původní hodnoty
        m_spellData->midi->SetVolume(oldMusic);
        m_spellData->sounds->channels->SetVolume(oldSfx);
    }
}

bool StrategicLevelFrame::EnsureUnitCostsLoaded()
{
    if (m_unitCostsLoaded)
        return true;

    m_unitCosts.clear();
    const auto path = GetUnitsJsonPath();
    if (!LoadUnitCostsFromJson(path, m_unitCosts, &m_unitCategories))
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
    if (it == m_unitCosts.end())
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
    m_palette.buttonText = wxColour(0xFF, 0xFF, 0xFF);
    m_palette.buttonBackground = wxColour(0x84, 0x7C, 0x7C);
    m_palette.shadow = wxColour(0, 0, 0, 160);

    m_fontText = MakeStrategicFont(20, false);
    m_fontHeading = MakeStrategicFont(22, false);

    root->SetBackgroundColour(m_palette.background);

    // Normal layout container
    m_normalLayoutPanel = new wxPanel(root);
    m_normalLayoutPanel->SetBackgroundColour(m_palette.background);
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // ============================================================
    // LEFT: content book (Strategic map / Hierarchy)
    // ============================================================
    m_leftBook = new wxSimplebook(m_normalLayoutPanel, wxID_ANY);
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
    m_mapCanvas->Bind(wxEVT_MOTION, &StrategicLevelFrame::OnMapMouseMove, this);
    m_mapSizer->Add(m_mapCanvas, 3, wxALL | wxEXPAND, 8);

    // Under-map panel: (optional) territory grid fallback + briefing/info text.
    auto* under = new wxPanel(m_mapPanel);
    under->SetBackgroundColour(m_palette.background);
    auto* underSizer = new wxBoxSizer(wxVERTICAL);

    // Territory buttons (fallback UI). When CLK is available (click map regions), this stays hidden.
    m_territoryButtonsPanel = new wxPanel(under);
    m_territoryButtonsPanel->SetBackgroundColour(m_palette.background);
    auto* grid = new wxGridSizer(0, 4, 6, 6);
    for (size_t i = 0; i < m_level.territories.size(); ++i)
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
    BuildHierarchyPage(hierarchyPanel);

    // --- Page 2: Resources ---
    m_resourcesPanel = new wxPanel(m_leftBook);
    m_resourcesPanel->SetBackgroundColour(m_palette.background);
    BuildResourcesPage();

    // --- Page 3: Statistics (integrated into this frame) ---
    m_statsPanel = new wxPanel(m_leftBook);
    m_statsPanel->SetBackgroundColour(m_palette.background);
    BuildStatsPage();


    // --- Page 4: Research – left side (active research + browser detail) ---
    m_researchPanel = new wxPanel(m_leftBook);
    m_researchPanel->SetBackgroundColour(m_palette.background);
    // Must NOT contribute a large minimum size – wxSimplebook propagates minimums
    // from ALL pages, not just the visible one, which would push the right panel off screen.
    m_researchPanel->SetMinSize(wxSize(1, 1));
    {
        auto* rs = new wxBoxSizer(wxVERTICAL);

        // ── Top box: BRF text of the currently active research ──
        m_researchActiveText = new wxTextCtrl(
            m_researchPanel, wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        m_researchActiveText->SetFont(m_fontText);
        m_researchActiveText->SetBackgroundColour(m_palette.background);
        m_researchActiveText->SetForegroundColour(m_palette.text);
        m_researchActiveText->SetMinSize(wxSize(1, 1));
        rs->Add(m_researchActiveText, 2, wxALL | wxEXPAND, 8);

        // ── Progress bar + STOP/START button ──
        auto* gRow = new wxBoxSizer(wxHORIZONTAL);

        m_researchGauge = new wxGauge(m_researchPanel, wxID_ANY, 100,
            wxDefaultPosition, wxSize(-1, 18), wxGA_HORIZONTAL);
        m_researchGauge->SetValue(0);
        gRow->Add(m_researchGauge, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

        m_researchGaugeLabel = new wxStaticText(m_researchPanel, wxID_ANY, "0/0");
        m_researchGaugeLabel->SetFont(m_fontText);
        m_researchGaugeLabel->SetForegroundColour(m_palette.text);
        gRow->Add(m_researchGaugeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        m_btnResearchStart = new wxButton(m_researchPanel, wxID_ANY, "Start");
        m_btnResearchStart->SetFont(m_fontText);
        m_btnResearchStart->SetForegroundColour(m_palette.buttonText);
        m_btnResearchStart->SetBackgroundColour(m_palette.buttonBackground);
        m_btnResearchStart->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnResearchStartStop, this);
        gRow->Add(m_btnResearchStart, 0, wxALIGN_CENTER_VERTICAL);

        rs->Add(gRow, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

        // ── Bottom box: INF text of the selected/browsed research item ──
        m_researchText = new wxTextCtrl(
            m_researchPanel, wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        m_researchText->SetFont(m_fontText);
        m_researchText->SetBackgroundColour(m_palette.background);
        m_researchText->SetForegroundColour(m_palette.text);
        m_researchText->SetMinSize(wxSize(1, 1));
        rs->Add(m_researchText, 3, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

        m_researchPanel->SetSizer(rs);
    }

    // --- Page 5: Info / Encyclopedia – left side (browser detail only) ---
    m_infoPanel = new wxPanel(m_leftBook);
    m_infoPanel->SetBackgroundColour(m_palette.background);
    m_infoPanel->SetMinSize(wxSize(1, 1));
    {
        auto* is = new wxBoxSizer(wxVERTICAL);

        // Info text box (detail of selected item)
        m_infoText = new wxTextCtrl(
            m_infoPanel, wxID_ANY, "",
            wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        m_infoText->SetFont(m_fontText);
        m_infoText->SetBackgroundColour(m_palette.background);
        m_infoText->SetForegroundColour(m_palette.text);
        m_infoText->SetMinSize(wxSize(1, 1));
        is->Add(m_infoText, 1, wxALL | wxEXPAND, 8);

        m_infoPanel->SetSizer(is);
    }

    m_leftBook->AddPage(m_mapPanel, "Strategic map", true);
    m_leftBook->AddPage(hierarchyPanel, "Hierarchy", false);
    m_leftBook->AddPage(m_resourcesPanel, "Resources", false);
    m_leftBook->AddPage(m_statsPanel, "Statistics", false);
    m_leftBook->AddPage(m_researchPanel, "Research", false);
    m_leftBook->AddPage(m_infoPanel, "Info", false);

    mainSizer->Add(m_leftBook, 4, wxEXPAND);

    // ============================================================
    // MIDDLE: player units (always visible)
    // ============================================================
    // Book for middle area: roster vs research
    m_midBook = new wxSimplebook(m_normalLayoutPanel, wxID_ANY);
    m_midBook->SetBackgroundColour(m_palette.background);

    m_midRosterPanel = new wxPanel(m_midBook);
    auto* mid = m_midRosterPanel;
    mid->SetBackgroundColour(m_palette.background);
    auto* midSizer = new wxBoxSizer(wxVERTICAL);


    // Commanders (owned) - list (max 14 commanders)
    auto* cmdTitle = CreateStrategicLabel(
        mid,
        { { "Commanders", m_palette.heading, &m_fontHeading } },
        m_fontHeading,
        m_palette.shadow,
        &m_palette.background);

    midSizer->Add(cmdTitle, 0, wxALL, 8);

    m_cmdRoster = new wxListCtrl(mid, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_cmdRoster->SetFont(m_fontText);
    m_cmdRoster->SetBackgroundColour(m_palette.background);
    m_cmdRoster->SetForegroundColour(m_palette.text);
    m_cmdRoster->InsertColumn(0, "Commander");
    m_cmdRoster->InsertColumn(1, "Rank");
    m_cmdRoster->Bind(wxEVT_LIST_BEGIN_DRAG, &StrategicLevelFrame::OnCommanderBeginDrag, this);
    m_cmdRoster->SetDropTarget(new HierarchyPoolDropTarget(this, "commander"));
    // Keep the commanders list compact (14 rows max)
    // NOVĚ: výška podle fontu + menší počet řádků
    {
        const int rowsVisible = 8;                   // uprav dle potřeby (např. 6–8)
        const int ch = m_cmdRoster->GetCharHeight(); // výška znaku dle aktuálního fontu
        const int rowH = ch + 8;                     // odhad výšky řádku (font + padding)
        const int headerH = ch + 18;                 // odhad výšky headeru
        m_cmdRoster->SetMinSize(wxSize(-1, headerH + rowsVisible * rowH));
    };
    midSizer->Add(m_cmdRoster, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* midTitle = CreateStrategicLabel(
        mid,
        { { "Player units", m_palette.heading, &m_fontHeading } },
        m_fontHeading,
        m_palette.shadow,
        &m_palette.background);

    midSizer->Add(midTitle, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // v BuildUI(): úprava definice sloupců m_roster
    m_roster = new wxListCtrl(mid, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_roster->SetFont(m_fontText);
    m_roster->SetBackgroundColour(m_palette.background);
    m_roster->SetForegroundColour(m_palette.text);
    // PŮVODNĚ: InsertColumn(0, "Unit"); InsertColumn(1, "Count"); InsertColumn(2, "HP");
    // NOVĚ: jen dva sloupce: Unit a HP
    m_roster->InsertColumn(0, "Unit");
    m_roster->InsertColumn(1, "HP");
    // Units are no longer dragged into hierarchy slots; assignment is done by selecting a unit under a commander.
    // m_roster->Bind(wxEVT_LIST_BEGIN_DRAG, &StrategicLevelFrame::OnRosterBeginDrag, this);
    // m_roster->SetDropTarget(new HierarchyPoolDropTarget(this, "unit"));
    midSizer->Add(m_roster, 1, wxALL | wxEXPAND, 8);

    mid->SetSizer(midSizer);
    m_midBook->AddPage(m_midRosterPanel, "Roster", true);

    // --- Middle Research page – categorized list with yellow group headers ---
    m_midResearchPanel = new wxPanel(m_midBook);
    m_midResearchPanel->SetBackgroundColour(m_palette.background);
    m_midResearchPanel->SetMinSize(wxSize(1, 1));
    {
        auto* rs = new wxBoxSizer(wxVERTICAL);
        auto* rtitle = CreateStrategicLabel(
            m_midResearchPanel,
            { { "Research", m_palette.heading, &m_fontHeading } },
            m_fontHeading,
            m_palette.shadow,
            &m_palette.background);
        rs->Add(rtitle, 0, wxALL, 8);

        // wxListCtrl (single column, no header) – supports SetItemTextColour per row
        m_researchList = new wxListCtrl(
            m_midResearchPanel, wxID_ANY,
            wxDefaultPosition, wxDefaultSize,
            wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL);
        m_researchList->SetFont(m_fontText);
        m_researchList->SetBackgroundColour(m_palette.background);
        m_researchList->SetForegroundColour(m_palette.text);
        m_researchList->SetMinSize(wxSize(1, 1));
        m_researchList->InsertColumn(0, "", wxLIST_FORMAT_LEFT, -1);
        m_researchList->Bind(wxEVT_LIST_ITEM_SELECTED,
            [this](wxListEvent& ev) {
                if (m_researchRefreshing) return;  // ignore events during repopulation
                const long row = ev.GetIndex();
                if (!m_researchList || row < 0) return;
                // Check for group header row (sentinel = max wxUIntPtr value)
                const wxUIntPtr data = m_researchList->GetItemData(row);
                if (data == static_cast<wxUIntPtr>(-1))
                {
                    // Header clicked – deselect and do nothing
                    m_researchList->SetItemState(row, 0, wxLIST_STATE_SELECTED);
                    return;
                }
                SelectResearchIndex(static_cast<int>(data));
            });
        rs->Add(m_researchList, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

        m_midResearchPanel->SetSizer(rs);
    }
    m_midBook->AddPage(m_midResearchPanel, "Research", false);

    // --- Middle Info/Encyclopedia page – categorized list (read-only browsing) ---
    m_midInfoPanel = new wxPanel(m_midBook);
    m_midInfoPanel->SetBackgroundColour(m_palette.background);
    m_midInfoPanel->SetMinSize(wxSize(1, 1));
    {
        auto* is = new wxBoxSizer(wxVERTICAL);
        auto* ititle = CreateStrategicLabel(
            m_midInfoPanel,
            { { "Encyclopedia", m_palette.heading, &m_fontHeading } },
            m_fontHeading,
            m_palette.shadow,
            &m_palette.background);
        is->Add(ititle, 0, wxALL, 8);

        // wxListCtrl (single column, no header) – supports SetItemTextColour per row
        m_infoList = new wxListCtrl(
            m_midInfoPanel, wxID_ANY,
            wxDefaultPosition, wxDefaultSize,
            wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL);
        m_infoList->SetFont(m_fontText);
        m_infoList->SetBackgroundColour(m_palette.background);
        m_infoList->SetForegroundColour(m_palette.text);
        m_infoList->SetMinSize(wxSize(1, 1));
        m_infoList->InsertColumn(0, "", wxLIST_FORMAT_LEFT, -1);
        m_infoList->Bind(wxEVT_LIST_ITEM_SELECTED,
            [this](wxListEvent& ev) {
                if (m_infoRefreshing) return;
                const long row = ev.GetIndex();
                if (!m_infoList || row < 0) return;
                const wxUIntPtr data = m_infoList->GetItemData(row);
                // Skip header rows
                if (data == static_cast<wxUIntPtr>(-1))
                {
                    m_infoList->SetItemState(row, 0, wxLIST_STATE_SELECTED);
                    return;
                }
                SelectInfoIndex(static_cast<int>(data));
            });
        is->Add(m_infoList, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

        m_midInfoPanel->SetSizer(is);
    }
    m_midBook->AddPage(m_midInfoPanel, "Info", false);

    mainSizer->Add(m_midBook, 1, wxEXPAND);


    // ============================================================
    // RIGHT: status + actions (always visible, consistent layout)
    // ============================================================
    // Keep the right sidebar width consistent across all pages.
    // If added with a proportional grow factor, buttons become excessively wide on larger resolutions.
    const int kRightSidebarW = 240;
    auto* right = new wxPanel(m_normalLayoutPanel);
    right->SetBackgroundColour(m_palette.background);
    right->SetMinSize(wxSize(kRightSidebarW, -1));
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    // Status box (Money / Research / Turn)
    auto* status = new wxPanel(right);
    status->SetBackgroundColour(m_palette.background);
    auto* statusSizer = new wxBoxSizer(wxVERTICAL);

    auto makeStatusRow = [&](const wxString& caption,
        wxStaticText*& outCaption,
        wxStaticText*& outValue)
        {
            auto* row = new wxBoxSizer(wxHORIZONTAL);

            outCaption = new wxStaticText(status, wxID_ANY, caption);
            outValue = new wxStaticText(status, wxID_ANY, "0");

            outCaption->SetFont(m_fontHeading);
            outValue->SetFont(m_fontHeading);

            // caption – zeleně
            outCaption->SetForegroundColour(m_palette.statusHeading);
            // hodnota – šedě
            outValue->SetForegroundColour(m_palette.statusNumber);

            outCaption->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
            outValue->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);

            row->Add(outCaption, 0, wxRIGHT, 6);
            row->Add(outValue, 0);

            return row;
        };

    statusSizer->Add(makeStatusRow("Money:", m_lblMoneyCaption, m_lblMoneyValue),
        0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);
    statusSizer->Add(makeStatusRow("Research:", m_lblResearchCaption, m_lblResearchValue),
        0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);
    statusSizer->Add(makeStatusRow("Turn:", m_lblTurnCaption, m_lblTurnValue),
        0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);

    status->SetSizer(statusSizer);
    rightSizer->Add(status, 0, wxALL | wxEXPAND, 8);

    auto makeBtn = [&](int id, const wxString& label) -> wxButton*
        {
            //return CreateStrategicButton(right, id, label, m_fontText, m_palette.buttonText,
            //                             m_palette.shadow, m_palette.buttonBackground, wxSize(-1, 44));
            return CreateStrategicButton(right, id, label,
                m_fontText,
                m_palette.buttonText,
                m_palette.buttonBackground,
                wxSize(-1, 44));

        };

    // Buttons (order matches original-ish layout)

    m_btnStrategicMap = makeBtn(ID_BTN_STRATEGIC_MAP, "Strategic map");
    m_btnHierarchy = makeBtn(ID_BTN_HIERARCHY, "Hierarchy");
    m_btnResources = makeBtn(ID_BTN_RESOURCES, "Resources");
    m_btnResearch = makeBtn(ID_BTN_RESEARCH, "Research");
    m_btnInfo = makeBtn(ID_BTN_INFO, "Info");
    m_btnBuyShop = makeBtn(ID_BTN_BUY_SHOP, "Buy / Sell");
    m_btnUnitsShop = makeBtn(ID_BTN_UNITS, "Units");
    m_btnStats = makeBtn(ID_BTN_STATS, "Statistics");
    m_btnLaunch = makeBtn(ID_BTN_LAUNCH, "Launch mission");
    m_btnEndTurn = makeBtn(ID_BTN_ENDTURN, "End turn");

    auto* btnSizer = new wxBoxSizer(wxVERTICAL);
    btnSizer->Add(m_btnResearch, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnInfo, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnBuyShop, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnUnitsShop, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(m_btnStrategicMap, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnHierarchy, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnResources, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(m_btnStats, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(m_btnLaunch, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(m_btnEndTurn, 0, wxEXPAND);

    rightSizer->Add(btnSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    right->SetSizer(rightSizer);

    // Fixed-width sidebar (like original UI).
    mainSizer->Add(right, 0, wxEXPAND);
    m_normalLayoutPanel->SetSizer(mainSizer);

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(m_normalLayoutPanel, 1, wxEXPAND);
    
    // --- Buy/Sell root panel (hidden by default) ---
    m_buyMainPanel = new wxPanel(root);
    m_buyMainPanel->SetBackgroundColour(m_palette.background);
    m_buyMainPanel->Show(false);
    BuildBuyPage();
    rootSizer->Add(m_buyMainPanel, 1, wxEXPAND);

    // --- Units Management root panel (hidden by default) ---
    m_unitsMainPanel = new wxPanel(root);
    m_unitsMainPanel->SetBackgroundColour(m_palette.background);
    m_unitsMainPanel->Show(false);
    BuildUnitsPage();
    rootSizer->Add(m_unitsMainPanel, 1, wxEXPAND);
    
    root->SetSizer(rootSizer);
    //použije rekurzivně transparentní pozadí na všechny elementy wx - opatrně!
    //MakeChildrenTransparentRecursive(root);
}

// ============================================================
//  Buy / Sell page
// ============================================================

static constexpr wxUIntPtr kBuyHdrSentinel = static_cast<wxUIntPtr>(-1);
static constexpr wxUIntPtr kBuyCmdBase     = static_cast<wxUIntPtr>(0x40000000);

void StrategicLevelFrame::BuildBuyPage()
{
    if (!m_buyMainPanel) return;

    // 3-column layout: left rosters + middle shop/info + right sidebar (status + all buttons)
    // Keep the right sidebar width consistent with the rest of Strategic UI.
    // If the sidebar is proportional, buttons become excessively wide on larger resolutions.
    const int kSidebarW = 240;
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // ---------------------------------------------------------------------
    // LEFT: Rosters
    // ---------------------------------------------------------------------
    auto* leftPanel = new wxPanel(m_buyMainPanel);
    leftPanel->SetBackgroundColour(m_palette.background);
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);

    // Unit roster (top)
    {
        auto* unitLabel = new wxStaticText(leftPanel, wxID_ANY, "Player units:");
        unitLabel->SetFont(m_fontText);
        unitLabel->SetForegroundColour(m_palette.heading);
        leftSizer->Add(unitLabel, 0, wxALL, 4);

        auto* unitRoster = new wxListCtrl(leftPanel, wxID_ANY,
            wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        unitRoster->SetFont(m_fontText);
        unitRoster->SetBackgroundColour(m_palette.background);
        unitRoster->SetForegroundColour(m_palette.text);

        // Exactly the needed columns (no empty columns).
        unitRoster->InsertColumn(0, "Unit");
        unitRoster->InsertColumn(1, "HP");

        leftSizer->Add(unitRoster, 3, wxALL | wxEXPAND, 4);
        m_buyUnitRoster = unitRoster;
    }

    // Commander roster (bottom)
    {
        auto* cmdLabel = new wxStaticText(leftPanel, wxID_ANY, "Commanders:");
        cmdLabel->SetFont(m_fontText);
        cmdLabel->SetForegroundColour(m_palette.heading);
        leftSizer->Add(cmdLabel, 0, wxALL, 4);

        auto* cmdRoster = new wxListCtrl(leftPanel, wxID_ANY,
            wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        cmdRoster->SetFont(m_fontText);
        cmdRoster->SetBackgroundColour(m_palette.background);
        cmdRoster->SetForegroundColour(m_palette.text);

        // Exactly the needed columns (no empty columns).
        cmdRoster->InsertColumn(0, "Commander");
        cmdRoster->InsertColumn(1, "Rank");

        leftSizer->Add(cmdRoster, 1, wxALL | wxEXPAND, 4);
        m_buyCmdRoster = cmdRoster;
    }

    leftPanel->SetSizer(leftSizer);
    // Left / Middle should split 50/50 (like other pages).
    mainSizer->Add(leftPanel, 1, wxEXPAND);

    // ---------------------------------------------------------------------
    // MIDDLE: Shop + info + buy/sell action
    // ---------------------------------------------------------------------
    auto* midPanel = new wxPanel(m_buyMainPanel);
    midPanel->SetBackgroundColour(m_palette.background);
    auto* midSizer = new wxBoxSizer(wxVERTICAL);

    // Tab toggles (Buy / Sell)
    {
        auto* tabRow = new wxBoxSizer(wxHORIZONTAL);

        auto* btnTabBuy = new wxButton(midPanel, wxID_ANY, "Buy");
        btnTabBuy->SetFont(m_fontText);
        btnTabBuy->SetForegroundColour(m_palette.buttonText);
        btnTabBuy->SetBackgroundColour(m_palette.buttonBackground);
        btnTabBuy->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_buyTabSell = false;
            RefreshBuyShopList();
        });

        auto* btnTabSell = new wxButton(midPanel, wxID_ANY, "Sell");
        btnTabSell->SetFont(m_fontText);
        btnTabSell->SetForegroundColour(m_palette.buttonText);
        btnTabSell->SetBackgroundColour(m_palette.buttonBackground);
        btnTabSell->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_buyTabSell = true;
            RefreshBuyShopList();
        });

        tabRow->Add(btnTabBuy, 1, wxEXPAND | wxRIGHT, 4);
        tabRow->Add(btnTabSell, 1, wxEXPAND);
        midSizer->Add(tabRow, 0, wxALL | wxEXPAND, 8);
    }

    // Shop list (single column; width auto-resized)
    m_buyShopList = new wxListCtrl(midPanel, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxLC_NO_SORT_HEADER);
    m_buyShopList->SetFont(m_fontText);
    m_buyShopList->SetBackgroundColour(m_palette.background);
    m_buyShopList->SetForegroundColour(m_palette.text);
    m_buyShopList->InsertColumn(0, "", wxLIST_FORMAT_LEFT, -1);

    m_buyShopList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& ev) {
        const long row = ev.GetIndex();
        if (!m_buyShopList || row < 0) return;
        const wxUIntPtr data = m_buyShopList->GetItemData(row);
        if (data == kBuyHdrSentinel) {
            m_buyShopList->SetItemState(row, 0, wxLIST_STATE_SELECTED);
            return;
        }
        RefreshBuyInfo(static_cast<long>(data));
    });

    m_buyShopList->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        if (!m_buyShopList) return;
        const int w = m_buyShopList->GetClientSize().GetWidth();
        if (w > 0) m_buyShopList->SetColumnWidth(0, w);
    });

    midSizer->Add(m_buyShopList, 1, wxLEFT | wxRIGHT | wxEXPAND, 8);

    // Time + Cost row
    {
        auto* priceRow = new wxBoxSizer(wxHORIZONTAL);

        m_buyTimeLabel = new wxStaticText(midPanel, wxID_ANY, "Time: -");
        m_buyTimeLabel->SetFont(m_fontText);
        m_buyTimeLabel->SetForegroundColour(m_palette.text);

        m_buyCostLabel = new wxStaticText(midPanel, wxID_ANY, "Cost: -");
        m_buyCostLabel->SetFont(m_fontText);
        m_buyCostLabel->SetForegroundColour(m_palette.heading);

        priceRow->Add(m_buyTimeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);
        priceRow->Add(m_buyCostLabel, 0, wxALIGN_CENTER_VERTICAL);

        midSizer->Add(priceRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    }

    // Buy/Sell action button
    m_btnBuyAction = new wxButton(midPanel, ID_BTN_BUY_ACTION, "Buy");
    m_btnBuyAction->SetFont(m_fontText);
    m_btnBuyAction->SetForegroundColour(m_palette.buttonText);
    m_btnBuyAction->SetBackgroundColour(m_palette.buttonBackground);
    m_btnBuyAction->Enable(false);
    midSizer->Add(m_btnBuyAction, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    midPanel->SetSizer(midSizer);
    // Left / Middle 50/50
    mainSizer->Add(midPanel, 1, wxEXPAND);

    // ---------------------------------------------------------------------
    // RIGHT: Sidebar (status + all buttons) – no duplicates
    // ---------------------------------------------------------------------
    auto* sidePanel = new wxPanel(m_buyMainPanel);
    sidePanel->SetBackgroundColour(m_palette.background);
    sidePanel->SetMinSize(wxSize(kSidebarW, -1));
    auto* sideSizer = new wxBoxSizer(wxVERTICAL);

    // Status box (Money / Research / Turn)
    {
        auto* status = new wxPanel(sidePanel);
        status->SetBackgroundColour(m_palette.background);
        auto* statusSizer = new wxBoxSizer(wxVERTICAL);

        auto makeStatusRow = [&](const wxString& caption,
                                 wxStaticText*& outCaption,
                                 wxStaticText*& outValue)
        {
            auto* row = new wxBoxSizer(wxHORIZONTAL);

            outCaption = new wxStaticText(status, wxID_ANY, caption);
            outValue = new wxStaticText(status, wxID_ANY, "0");

            outCaption->SetFont(m_fontHeading);
            outValue->SetFont(m_fontHeading);

            outCaption->SetForegroundColour(m_palette.statusHeading);
            outValue->SetForegroundColour(m_palette.statusNumber);

            outCaption->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
            outValue->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);

            row->Add(outCaption, 0, wxRIGHT, 6);
            row->Add(outValue, 0);
            return row;
        };

        statusSizer->Add(makeStatusRow("Money:", m_buyLblMoneyCaption, m_buyLblMoneyValue),
                         0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);
        statusSizer->Add(makeStatusRow("Research:", m_buyLblResearchCaption, m_buyLblResearchValue),
                         0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);
        statusSizer->Add(makeStatusRow("Turn:", m_buyLblTurnCaption, m_buyLblTurnValue),
                         0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);

        status->SetSizer(statusSizer);
        sideSizer->Add(status, 0, wxALL | wxEXPAND, 8);
    }

    auto makeBtn = [&](const wxString& label) -> wxButton*
    {
        return CreateStrategicButton(sidePanel, wxID_ANY, label,
            m_fontText,
            m_palette.buttonText,
            m_palette.buttonBackground,
            wxSize(-1, 44));
    };

    auto* btnSizer = new wxBoxSizer(wxVERTICAL);

    auto* btnResearch = makeBtn("Research");
    btnResearch->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnResearch(ev); });

    auto* btnBuySell = makeBtn("Buy / Sell");
    btnBuySell->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { OnBuyShop(ev); });

    auto* btnUnits = makeBtn("Units");
    btnUnits->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); EnterUnitsMode(); });

    auto* btnStrategicMap = makeBtn("Strategic map");
    btnStrategicMap->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowStrategicMap(ev); });

    auto* btnHierarchy = makeBtn("Hierarchy");
    btnHierarchy->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowHierarchy(ev); });

    auto* btnResources = makeBtn("Resources");
    btnResources->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowResources(ev); });

    auto* btnStats = makeBtn("Statistics");
    btnStats->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowStats(ev); });

    auto* btnLaunch = makeBtn("Launch mission");
    btnLaunch->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnLaunch(ev); });
    // Launch mission must only be enabled on the Strategic map page.
    btnLaunch->Enable(false);

    auto* btnEndTurn = makeBtn("End turn");
    btnEndTurn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnEndTurn(ev); });

    btnSizer->Add(btnResearch, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnBuySell, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnUnits, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(btnStrategicMap, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnHierarchy, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnResources, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnStats, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(btnLaunch, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(btnEndTurn, 0, wxEXPAND);

    sideSizer->Add(btnSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    sidePanel->SetSizer(sideSizer);
    // Fixed-width sidebar (like the normal right panel).
    mainSizer->Add(sidePanel, 0, wxEXPAND);

    m_buyMainPanel->SetSizer(mainSizer);
}

void StrategicLevelFrame::RefreshBuyInfo(long data)
{
    if (!m_buyTimeLabel || !m_buyCostLabel || !m_btnBuyAction) return;
    m_btnBuyAction->Enable(false);

    if (data < 0 || data == (long)kBuyHdrSentinel){
        m_buyTimeLabel->SetLabel("Time: -");
        m_buyCostLabel->SetLabel("Cost: -");
        return;
    }

    if (!m_buyTabSell)
    {
        // Buy mode
        if ((wxUIntPtr)data >= kBuyCmdBase){
            // Commander
            m_buyTimeLabel->SetLabel("Time: 1");
            m_buyCostLabel->SetLabel("Cost: 0");
            m_btnBuyAction->SetLabel("Buy");
            m_btnBuyAction->Enable((int)m_playerCommanders.size() < 14);
        }
        else{
            // Unit
            const int tid  = (int)data;
            const int cost = GetUnitBuyCost(tid);
            m_buyTimeLabel->SetLabel("Time: 1");
            m_buyCostLabel->SetLabel(cost > 0 ? wxString::Format("Cost: %d", cost) : wxString("Cost: ?"));
            m_btnBuyAction->SetLabel("Buy");
            m_btnBuyAction->Enable(cost > 0 && m_money >= cost);
        }
    }
    else
    {
        // Sell mode
        const int idx = (int)data;
        if (idx >= 0 && idx < (int)m_playerUnits.size()){
            const auto& u = m_playerUnits[idx];
            const int cost   = GetUnitBuyCost(u.unit_id);
            const int refund = cost > 0 ? cost / 2 : 0;
            m_buyTimeLabel->SetLabel("Time: 1");
            m_buyCostLabel->SetLabel(refund > 0 ? wxString::Format("Cost: %d", refund) : wxString("Cost: ?"));
            m_btnBuyAction->SetLabel("Sell");
            m_btnBuyAction->Enable(refund > 0);
        }
    }
}

void StrategicLevelFrame::RefreshBuyRosters()
{
    if (!m_buyUnitRoster || !m_buyCmdRoster) return;

    // Refresh unit roster
    m_buyUnitRoster->DeleteAllItems();
    for (const auto& u : m_playerUnits){
        const long idx = m_buyUnitRoster->GetItemCount();
        m_buyUnitRoster->InsertItem(idx, GetUnitDisplayName(u.unit_id));
        m_buyUnitRoster->SetItem(idx, 1, wxString::Format("%d%%", u.health));
        if (u.health < 100)
            m_buyUnitRoster->SetItemTextColour(idx, wxColour(0xFF, 0xA0, 0x00));
    }

    // Refresh commander roster
    m_buyCmdRoster->DeleteAllItems();
    for (const auto& c : m_playerCommanders){
        const long idx = m_buyCmdRoster->GetItemCount();
        m_buyCmdRoster->InsertItem(idx, wxString::FromUTF8(c.name));
        m_buyCmdRoster->SetItem(idx, 1, GetRankAbbrev(c.rank));
    }

    // Column widths (avoid empty space / extra columns)
    {
        int w = 0, h = 0;
        if (m_buyUnitRoster)
        {
            m_buyUnitRoster->GetClientSize(&w, &h);
            const int hpW = 55;
            m_buyUnitRoster->SetColumnWidth(1, hpW);
            m_buyUnitRoster->SetColumnWidth(0, std::max(80, w - hpW - 6));
        }
        if (m_buyCmdRoster)
        {
            m_buyCmdRoster->GetClientSize(&w, &h);
            const int rankW = 70;
            m_buyCmdRoster->SetColumnWidth(1, rankW);
            m_buyCmdRoster->SetColumnWidth(0, std::max(90, w - rankW - 6));
        }
    }

}

void StrategicLevelFrame::RefreshBuyShopList()
{
    EnsureUnitCostsLoaded();
    if (!m_buyShopList) return;

    m_buyShopList->Freeze();
    m_buyShopList->DeleteAllItems();

    const wxColour clrHdr  = m_palette.heading;
    const wxColour clrNorm = m_palette.text;
    const wxColour clrGrey(0x60, 0x60, 0x60);
    long row = 0;

    auto addHdr = [&](const wxString& lbl){
        m_buyShopList->InsertItem(row, lbl);
        m_buyShopList->SetItemData(row, kBuyHdrSentinel);
        m_buyShopList->SetItemTextColour(row, clrHdr);
        ++row;
    };

    if (!m_buyTabSell)
    {
        // Buy mode - categorized shop list
        if (m_spellData && m_spellData->units)
        {
            struct UE { int type_id; wxString name; int cost; };
            const std::vector<std::string> catOrder = {"Infantry","Artillery","Transporters","Aerial guns","Other"};
            std::map<std::string, std::vector<UE>> byCat;

            for (const auto* unit : m_spellData->units->GetUnits())
            {
                if (!unit) continue;
                const int cost = GetUnitBuyCost(unit->type_id);
                if (cost <= 0) continue;

                // Game mode filter
                if (m_gameModeEnabled && !m_levelResearchFlags.empty()){
                    if (m_levelResearchFlags.count(unit->type_id) == 0)
                        continue;
                }

                std::string cat = "Other";
                auto it = m_unitCategories.find(unit->type_id);
                if (it != m_unitCategories.end() && !it->second.empty())
                    cat = it->second;
                byCat[cat].push_back({unit->type_id, wxString(char2wstringCP895(unit->name)), cost});
            }

            for (const auto& catName : catOrder){
                auto it = byCat.find(catName);
                if (it == byCat.end() || it->second.empty()) continue;
                addHdr(wxString::FromUTF8(catName));
                for (const auto& ue : it->second){
                    m_buyShopList->InsertItem(row, wxString("  ") + ue.name);
                    m_buyShopList->SetItemData(row, static_cast<wxUIntPtr>(ue.type_id));
                    m_buyShopList->SetItemTextColour(row, m_money >= ue.cost ? clrNorm : clrGrey);
                    ++row;
                }
            }
        }

        // Commanders
        if (!m_availableCommanders.empty()){
            addHdr("Commanders");
            for (int ci = 0; ci < (int)m_availableCommanders.size(); ++ci){
                const auto& c = m_availableCommanders[ci];
                m_buyShopList->InsertItem(row,
                    wxString("  ") + wxString::FromUTF8(c.name)
                    + " (" + GetRankAbbrev(c.rank) + ")");
                m_buyShopList->SetItemData(row, kBuyCmdBase + (wxUIntPtr)ci);
                m_buyShopList->SetItemTextColour(row,
                    (int)m_playerCommanders.size() < 14 ? clrNorm : clrGrey);
                ++row;
            }
        }
    }
    else
    {
        // Sell mode - owned units list
        if (!m_playerUnits.empty()){
            struct SE { int idx; wxString name; int refund; };
            const std::vector<std::string> catOrder = {"Infantry","Artillery","Transporters","Aerial guns","Other"};
            std::map<std::string, std::vector<SE>> byCat;

            for (int i = 0; i < (int)m_playerUnits.size(); ++i){
                const auto& u = m_playerUnits[i];
                const int cost = GetUnitBuyCost(u.unit_id);
                std::string cat = "Other";
                auto it = m_unitCategories.find(u.unit_id);
                if (it != m_unitCategories.end() && !it->second.empty())
                    cat = it->second;
                byCat[cat].push_back({i, GetUnitDisplayName(u.unit_id), cost > 0 ? cost/2 : 0});
            }

            for (const auto& catName : catOrder){
                auto it = byCat.find(catName);
                if (it == byCat.end() || it->second.empty()) continue;
                addHdr(wxString::FromUTF8(catName));
                for (const auto& se : it->second){
                    wxString lbl = wxString("  ") + se.name;
                    if (se.refund > 0) lbl += wxString::Format(" (%d)", se.refund);
                    m_buyShopList->InsertItem(row, lbl);
                    m_buyShopList->SetItemData(row, static_cast<wxUIntPtr>(se.idx));
                    m_buyShopList->SetItemTextColour(row, clrNorm);
                    ++row;
                }
            }
        }
        else{
            m_buyShopList->InsertItem(row, "  No units to sell.");
            m_buyShopList->SetItemData(row, kBuyHdrSentinel);
            m_buyShopList->SetItemTextColour(row, clrGrey);
        }
    }

    const int lw = m_buyShopList->GetClientSize().GetWidth();
    m_buyShopList->SetColumnWidth(0, lw > 0 ? lw : wxLIST_AUTOSIZE);
    m_buyShopList->Thaw();

    RefreshBuyInfo(-1);
}


void StrategicLevelFrame::ShowBuyPanel(bool show)
{
    // IMPORTANT: hide/show via the ROOT sizer, otherwise the hidden panel can still reserve space
    // and the visible one ends up with 0 height (symptom: Buy/Sell looks like it did not load).
    wxWindow* root = nullptr;
    if (m_normalLayoutPanel)
        root = m_normalLayoutPanel->GetParent();
    else if (m_buyMainPanel)
        root = m_buyMainPanel->GetParent();

    if (root && root->GetSizer())
    {
        wxSizer* sz = root->GetSizer();
        if (m_normalLayoutPanel) sz->Show(m_normalLayoutPanel, !show, true);
        if (m_buyMainPanel)     sz->Show(m_buyMainPanel,     show, true);
        if (m_unitsMainPanel)   sz->Show(m_unitsMainPanel,   false, true);
        root->Layout();
    }
    else
    {
        // Fallback: plain Show/Hide (works, but can leave stale layout on some platforms).
        if (m_normalLayoutPanel) m_normalLayoutPanel->Show(!show);
        if (m_buyMainPanel)      m_buyMainPanel->Show(show);
        if (m_unitsMainPanel)    m_unitsMainPanel->Show(false);
        Layout();
    }
}

void StrategicLevelFrame::PostFixBuyLayout()
{
    if (!m_buyModeActive || !m_buyMainPanel)
        return;

    // Now that the panel is truly visible and has a real size, rebuild lists + enforce column widths.
    m_buyMainPanel->Layout();
    Layout();

    RefreshBuyRosters();
    RefreshBuyShopList();

    if (m_buyShopList)
    {
        const int w = m_buyShopList->GetClientSize().GetWidth();
        if (w > 0)
            m_buyShopList->SetColumnWidth(0, w);
    }

    if (m_buyUnitRoster)
    {
        int cw = 0, ch = 0;
        m_buyUnitRoster->GetClientSize(&cw, &ch);
        const int hpW = 70;
        const int unitW = std::max(100, cw - hpW - 4);
        if (m_buyUnitRoster->GetColumnCount() >= 2)
        {
            m_buyUnitRoster->SetColumnWidth(1, hpW);
            m_buyUnitRoster->SetColumnWidth(0, unitW);
        }
    }

    if (m_buyCmdRoster)
    {
        int cw = 0, ch = 0;
        m_buyCmdRoster->GetClientSize(&cw, &ch);
        const int rankW = 70;
        const int nameW = std::max(90, cw - rankW - 4);
        if (m_buyCmdRoster->GetColumnCount() >= 2)
        {
            m_buyCmdRoster->SetColumnWidth(1, rankW);
            m_buyCmdRoster->SetColumnWidth(0, nameW);
        }
    }

    Refresh();
}

void StrategicLevelFrame::EnterBuyMode()
{
    m_buyModeActive = true;

    ShowBuyPanel(true);

    // Defer the expensive refresh until after wx has assigned a real size to the shown panel.
    // (Without this, list controls often report 0 width on first open.)
    CallAfter(&StrategicLevelFrame::PostFixBuyLayout);

    // Update status labels immediately
    if (m_buyLblMoneyValue) m_buyLblMoneyValue->SetLabel(wxString::Format("%d", m_money));
    if (m_buyLblResearchValue) m_buyLblResearchValue->SetLabel(wxString::Format("%d", m_research));
    if (m_buyLblTurnValue) m_buyLblTurnValue->SetLabel(wxString::Format("%d", m_turn));
}

void StrategicLevelFrame::LeaveBuyMode()
{
    m_buyModeActive = false;

    ShowBuyPanel(false);

    RefreshUI();
    Refresh();
}

void StrategicLevelFrame::OnBuyShop(wxCommandEvent&)
{
    if (m_buyModeActive)
        LeaveBuyMode();
    else{
        if (m_researchMode) LeaveResearchMode();
        if (m_unitsModeActive) LeaveUnitsMode();
        EnterBuyMode();
    }
}

void StrategicLevelFrame::OnBuyAction(wxCommandEvent&)
{
    if (!m_buyShopList) return;
    const long sel = m_buyShopList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0) return;
    const wxUIntPtr data = m_buyShopList->GetItemData(sel);
    if (data == kBuyHdrSentinel) return;

    EnsureUnitCostsLoaded();

    if (!m_buyTabSell)
    {
        // BUY
        if (data >= kBuyCmdBase){
            // Commander
            const int ci = (int)(data - kBuyCmdBase);
            if (ci >= (int)m_availableCommanders.size()) return;
            if ((int)m_playerCommanders.size() >= 14){
                wxMessageBox("Commander limit reached (14).", "Buy", wxOK | wxICON_INFORMATION, this);
                return;
            }
            m_playerCommanders.push_back(m_availableCommanders[(size_t)ci]);
            m_availableCommanders.clear();
        }
        else{
            // Unit
            const int tid  = (int)data;
            const int cost = GetUnitBuyCost(tid);
            if (cost <= 0){ wxMessageBox("No price defined.", "Buy", wxOK | wxICON_WARNING, this); return; }
            if (m_money < cost){
                wxMessageBox(wxString::Format("Not enough money. Need %d, have %d.", cost, m_money),
                    "Buy", wxOK | wxICON_WARNING, this);
                return;
            }
            LevelData::PlayerUnitAdd add;
            add.unit_id = tid; add.count = 1; add.health = 100; add.extra = "-";
            m_playerUnits.push_back(add);
            m_money -= cost;
        }
    }
    else
    {
        // SELL
        const int idx = (int)data;
        if (idx < 0 || idx >= (int)m_playerUnits.size()) return;
        const auto& u  = m_playerUnits[idx];
        const int cost = GetUnitBuyCost(u.unit_id);
        if (cost <= 0){ wxMessageBox("No price defined.", "Sell", wxOK | wxICON_WARNING, this); return; }
        m_money += cost / 2;
        m_playerUnits.erase(m_playerUnits.begin() + idx);
    }

    SaveStrategicState();
    RefreshUI();
    RefreshBuyRosters();
    RefreshBuyShopList();
}

// ============================================================
//  Units Management Page (Recruit / Disband / Upgrade / Info)
// ============================================================

static constexpr wxUIntPtr kUnitsHdrSentinel = static_cast<wxUIntPtr>(-1);

void StrategicLevelFrame::BuildUnitsPage()
{
    if (!m_unitsMainPanel) return;

    const int kSidebarW = 240;
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // ---------------------------------------------------------------------
    // LEFT: Player Units Roster
    // ---------------------------------------------------------------------
    auto* leftPanel = new wxPanel(m_unitsMainPanel);
    leftPanel->SetBackgroundColour(m_palette.background);
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);

    {
        auto* unitLabel = new wxStaticText(leftPanel, wxID_ANY, "Your Units:");
        unitLabel->SetFont(m_fontText);
        unitLabel->SetForegroundColour(m_palette.heading);
        leftSizer->Add(unitLabel, 0, wxALL, 4);

        m_unitsRoster = new wxListCtrl(leftPanel, wxID_ANY,
            wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        m_unitsRoster->SetFont(m_fontText);
        m_unitsRoster->SetBackgroundColour(m_palette.background);
        m_unitsRoster->SetForegroundColour(m_palette.text);

        m_unitsRoster->InsertColumn(0, "Unit");
        m_unitsRoster->InsertColumn(1, "HP");
        m_unitsRoster->InsertColumn(2, "XP");
        m_unitsRoster->InsertColumn(3, "Status");

        m_unitsRoster->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& ev) {
            m_unitsSelectedUnit = ev.GetIndex();
            RefreshUnitsInfo(m_unitsSelectedUnit);
            RefreshUnitsShopList();
            RefreshUnitsActionButton();
        });

        leftSizer->Add(m_unitsRoster, 1, wxALL | wxEXPAND, 4);

        // Unit icon canvas
        m_unitsIconCanvas = new wxPanel(leftPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 80));
        m_unitsIconCanvas->SetBackgroundColour(m_palette.background);
        m_unitsIconCanvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_unitsIconCanvas->Bind(wxEVT_PAINT, [this](wxPaintEvent& ev) {
            wxPaintDC dc(m_unitsIconCanvas);
            dc.SetBackground(wxBrush(m_palette.background));
            dc.Clear();

            if (m_unitsSelectedUnit < 0 || m_unitsSelectedUnit >= (int)m_playerUnits.size())
                return;

            const auto& u = m_playerUnits[m_unitsSelectedUnit];
            if (!m_spellData || !m_spellData->units) return;

            auto* unitRec = m_spellData->units->GetUnit(u.unit_id);
            if (!unitRec || !unitRec->icon_glyph) return;

            wxBitmap* bmp = unitRec->icon_glyph->Render(
                m_unitsIconCanvas->GetClientSize().GetWidth(),
                m_unitsIconCanvas->GetClientSize().GetHeight());
            if (bmp) {
                dc.DrawBitmap(*bmp, wxPoint(0, 0));
                delete bmp;
            }
        });
        leftSizer->Add(m_unitsIconCanvas, 0, wxALL | wxEXPAND, 4);
    }

    leftPanel->SetSizer(leftSizer);
    mainSizer->Add(leftPanel, 1, wxEXPAND);

    // ---------------------------------------------------------------------
    // MIDDLE: Tab buttons + options/shop list + info
    // ---------------------------------------------------------------------
    auto* midPanel = new wxPanel(m_unitsMainPanel);
    midPanel->SetBackgroundColour(m_palette.background);
    auto* midSizer = new wxBoxSizer(wxVERTICAL);

    // Tab row (Recruit / Disband / Upgrade / Info)
    {
        auto* tabRow = new wxBoxSizer(wxHORIZONTAL);

        auto makeTabBtn = [&](const wxString& label, int tabId) -> wxButton* {
            auto* btn = new wxButton(midPanel, tabId, label);
            btn->SetFont(m_fontText);
            btn->SetForegroundColour(m_palette.buttonText);
            btn->SetBackgroundColour(m_palette.buttonBackground);
            return btn;
        };

        m_btnUnitsTabRecruit = makeTabBtn("Recruit", ID_UNITS_TAB_RECRUIT);
        m_btnUnitsTabDisband = makeTabBtn("Disband", ID_UNITS_TAB_DISBAND);
        m_btnUnitsTabUpgrade = makeTabBtn("Upgrade", ID_UNITS_TAB_UPGRADE);
        m_btnUnitsTabInfo = makeTabBtn("Info", ID_UNITS_TAB_INFO);

        m_btnUnitsTabRecruit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnUnitsTabChange(UNITS_TAB_RECRUIT); });
        m_btnUnitsTabDisband->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnUnitsTabChange(UNITS_TAB_DISBAND); });
        m_btnUnitsTabUpgrade->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnUnitsTabChange(UNITS_TAB_UPGRADE); });
        m_btnUnitsTabInfo->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnUnitsTabChange(UNITS_TAB_INFO); });

        tabRow->Add(m_btnUnitsTabRecruit, 1, wxEXPAND | wxRIGHT, 2);
        tabRow->Add(m_btnUnitsTabDisband, 1, wxEXPAND | wxRIGHT, 2);
        tabRow->Add(m_btnUnitsTabUpgrade, 1, wxEXPAND | wxRIGHT, 2);
        tabRow->Add(m_btnUnitsTabInfo, 1, wxEXPAND);
        midSizer->Add(tabRow, 0, wxALL | wxEXPAND, 8);
    }

    // Quality selector (for Recruit mode)
    {
        auto* qualityRow = new wxBoxSizer(wxHORIZONTAL);
        auto* qualityLabel = new wxStaticText(midPanel, wxID_ANY, "Quality:");
        qualityLabel->SetFont(m_fontText);
        qualityLabel->SetForegroundColour(m_palette.text);

        wxArrayString choices;
        for (int i = 0; i < RECRUIT_QUALITY_COUNT; i++)
            choices.Add(RECRUIT_QUALITY_NAMES[i]);

        m_unitsQualityChoice = new wxChoice(midPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
        m_unitsQualityChoice->SetFont(m_fontText);
        m_unitsQualityChoice->SetSelection(1); // Default to Standard
        m_unitsQualityChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            RefreshUnitsInfo(m_unitsSelectedUnit);
            RefreshUnitsActionButton();
        });

        qualityRow->Add(qualityLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        qualityRow->Add(m_unitsQualityChoice, 1, wxEXPAND);
        midSizer->Add(qualityRow, 0, wxLEFT | wxRIGHT | wxEXPAND, 8);
    }

    // Upgrade type selector (for Upgrade mode)
    {
        auto* upgradeRow = new wxBoxSizer(wxHORIZONTAL);
        auto* upgradeLabel = new wxStaticText(midPanel, wxID_ANY, "Upgrade to:");
        upgradeLabel->SetFont(m_fontText);
        upgradeLabel->SetForegroundColour(m_palette.text);

        m_unitsUpgradeChoice = new wxChoice(midPanel, wxID_ANY);
        m_unitsUpgradeChoice->SetFont(m_fontText);
        m_unitsUpgradeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            RefreshUnitsInfo(m_unitsSelectedUnit);
            RefreshUnitsActionButton();
        });

        upgradeRow->Add(upgradeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        upgradeRow->Add(m_unitsUpgradeChoice, 1, wxEXPAND);
        midSizer->Add(upgradeRow, 0, wxALL | wxEXPAND, 8);
    }

    // Shop/options list
    m_unitsShopList = new wxListCtrl(midPanel, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL);
    m_unitsShopList->SetFont(m_fontText);
    m_unitsShopList->SetBackgroundColour(m_palette.background);
    m_unitsShopList->SetForegroundColour(m_palette.text);
    m_unitsShopList->InsertColumn(0, "", wxLIST_FORMAT_LEFT, -1);

    m_unitsShopList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& ev) {
        m_unitsSelectedUpgrade = ev.GetIndex();
        RefreshUnitsActionButton();
    });

    m_unitsShopList->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        if (!m_unitsShopList) return;
        const int w = m_unitsShopList->GetClientSize().GetWidth();
        if (w > 0) m_unitsShopList->SetColumnWidth(0, w);
    });

    midSizer->Add(m_unitsShopList, 1, wxLEFT | wxRIGHT | wxEXPAND, 8);

    // Time + Cost row
    {
        auto* priceRow = new wxBoxSizer(wxHORIZONTAL);

        m_unitsTimeLabel = new wxStaticText(midPanel, wxID_ANY, "Time: -");
        m_unitsTimeLabel->SetFont(m_fontText);
        m_unitsTimeLabel->SetForegroundColour(m_palette.text);

        m_unitsCostLabel = new wxStaticText(midPanel, wxID_ANY, "Cost: -");
        m_unitsCostLabel->SetFont(m_fontText);
        m_unitsCostLabel->SetForegroundColour(m_palette.heading);

        priceRow->Add(m_unitsTimeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);
        priceRow->Add(m_unitsCostLabel, 0, wxALIGN_CENTER_VERTICAL);

        midSizer->Add(priceRow, 0, wxALL, 8);
    }

    // Action button
    m_btnUnitsAction = new wxButton(midPanel, ID_BTN_UNITS_ACTION, "Action");
    m_btnUnitsAction->SetFont(m_fontText);
    m_btnUnitsAction->SetForegroundColour(m_palette.buttonText);
    m_btnUnitsAction->SetBackgroundColour(m_palette.buttonBackground);
    m_btnUnitsAction->Enable(false);
    m_btnUnitsAction->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnUnitsAction, this);
    midSizer->Add(m_btnUnitsAction, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    // Unit Art canvas (for Info mode)
    m_unitsArtCanvas = new wxPanel(midPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 200));
    m_unitsArtCanvas->SetBackgroundColour(m_palette.background);
    m_unitsArtCanvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_unitsArtCanvas->Bind(wxEVT_PAINT, [this](wxPaintEvent& ev) {
        wxPaintDC dc(m_unitsArtCanvas);
        dc.SetBackground(wxBrush(m_palette.background));
        dc.Clear();

        if (m_unitsCurrentTab != UNITS_TAB_INFO) return;
        if (m_unitsSelectedUnit < 0 || m_unitsSelectedUnit >= (int)m_playerUnits.size()) return;

        // Art painting logic would go here (similar to form_units.cpp)
        // For now, just display placeholder
        dc.SetTextForeground(m_palette.text);
        dc.DrawText("Unit Art", 10, 10);
    });
    midSizer->Add(m_unitsArtCanvas, 0, wxALL | wxEXPAND, 8);

    // Info text
    m_unitsInfoText = new wxTextCtrl(midPanel, wxID_ANY, "",
        wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
    m_unitsInfoText->SetFont(m_fontText);
    m_unitsInfoText->SetBackgroundColour(m_palette.background);
    m_unitsInfoText->SetForegroundColour(m_palette.text);
    midSizer->Add(m_unitsInfoText, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    midPanel->SetSizer(midSizer);
    mainSizer->Add(midPanel, 1, wxEXPAND);

    // ---------------------------------------------------------------------
    // RIGHT: Sidebar (status + all buttons)
    // ---------------------------------------------------------------------
    auto* sidePanel = new wxPanel(m_unitsMainPanel);
    sidePanel->SetBackgroundColour(m_palette.background);
    sidePanel->SetMinSize(wxSize(kSidebarW, -1));
    auto* sideSizer = new wxBoxSizer(wxVERTICAL);

    // Status box
    {
        auto* status = new wxPanel(sidePanel);
        status->SetBackgroundColour(m_palette.background);
        auto* statusSizer = new wxBoxSizer(wxVERTICAL);

        auto makeStatusRow = [&](const wxString& caption,
                                 wxStaticText*& outCaption,
                                 wxStaticText*& outValue)
        {
            auto* row = new wxBoxSizer(wxHORIZONTAL);

            outCaption = new wxStaticText(status, wxID_ANY, caption);
            outValue = new wxStaticText(status, wxID_ANY, "0");

            outCaption->SetFont(m_fontHeading);
            outValue->SetFont(m_fontHeading);

            outCaption->SetForegroundColour(m_palette.statusHeading);
            outValue->SetForegroundColour(m_palette.statusNumber);

            outCaption->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
            outValue->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);

            row->Add(outCaption, 0, wxRIGHT, 6);
            row->Add(outValue, 0);
            return row;
        };

        statusSizer->Add(makeStatusRow("Money:", m_unitsLblMoneyCaption, m_unitsLblMoneyValue),
                         0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);
        statusSizer->Add(makeStatusRow("Research:", m_unitsLblResearchCaption, m_unitsLblResearchValue),
                         0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);
        statusSizer->Add(makeStatusRow("Turn:", m_unitsLblTurnCaption, m_unitsLblTurnValue),
                         0, wxALL | wxALIGN_CENTER_HORIZONTAL, 4);

        status->SetSizer(statusSizer);
        sideSizer->Add(status, 0, wxALL | wxEXPAND, 8);
    }

    auto makeBtn = [&](const wxString& label) -> wxButton*
    {
        return CreateStrategicButton(sidePanel, wxID_ANY, label,
            m_fontText,
            m_palette.buttonText,
            m_palette.buttonBackground,
            wxSize(-1, 44));
    };

    auto* btnSizer = new wxBoxSizer(wxVERTICAL);

    auto* btnResearch = makeBtn("Research");
    btnResearch->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnResearch(ev); });

    auto* btnBuySell = makeBtn("Buy / Sell");
    btnBuySell->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); EnterBuyMode(); });

    auto* btnUnits = makeBtn("Units");
    btnUnits->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { OnUnitsShop(ev); });

    auto* btnStrategicMap = makeBtn("Strategic map");
    btnStrategicMap->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowStrategicMap(ev); });

    auto* btnHierarchy = makeBtn("Hierarchy");
    btnHierarchy->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowHierarchy(ev); });

    auto* btnResources = makeBtn("Resources");
    btnResources->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowResources(ev); });

    auto* btnStats = makeBtn("Statistics");
    btnStats->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowStats(ev); });

    auto* btnLaunch = makeBtn("Launch mission");
    btnLaunch->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnLaunch(ev); });
    btnLaunch->Enable(false);

    auto* btnEndTurn = makeBtn("End turn");
    btnEndTurn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnEndTurn(ev); });

    btnSizer->Add(btnResearch, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnBuySell, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnUnits, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(btnStrategicMap, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnHierarchy, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnResources, 0, wxEXPAND | wxBOTTOM, 6);
    btnSizer->Add(btnStats, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(btnLaunch, 0, wxEXPAND | wxBOTTOM, 10);
    btnSizer->Add(btnEndTurn, 0, wxEXPAND);

    sideSizer->Add(btnSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    sidePanel->SetSizer(sideSizer);
    mainSizer->Add(sidePanel, 0, wxEXPAND);

    m_unitsMainPanel->SetSizer(mainSizer);
}

void StrategicLevelFrame::ShowUnitsPanel(bool show)
{
    wxWindow* root = m_normalLayoutPanel ? m_normalLayoutPanel->GetParent() : nullptr;
    if (!root && m_unitsMainPanel)
        root = m_unitsMainPanel->GetParent();
    if (!root) return;

    wxSizer* sz = root->GetSizer();
    if (sz) {
        if (m_normalLayoutPanel) sz->Show(m_normalLayoutPanel, !show, true);
        if (m_buyMainPanel)      sz->Show(m_buyMainPanel,      false, true);
        if (m_unitsMainPanel)    sz->Show(m_unitsMainPanel,    show, true);
        root->Layout();
    } else {
        if (m_normalLayoutPanel) m_normalLayoutPanel->Show(!show);
        if (m_buyMainPanel)      m_buyMainPanel->Show(false);
        if (m_unitsMainPanel)    m_unitsMainPanel->Show(show);
    }
}

void StrategicLevelFrame::PostFixUnitsLayout()
{
    if (!m_unitsModeActive || !m_unitsMainPanel)
        return;

    m_unitsMainPanel->Layout();

    RefreshUnitsRoster();
    RefreshUnitsShopList();
    OnUnitsTabChange(m_unitsCurrentTab);
}

void StrategicLevelFrame::EnterUnitsMode()
{
    m_unitsModeActive = true;

    ShowUnitsPanel(true);

    CallAfter(&StrategicLevelFrame::PostFixUnitsLayout);

    // Update status labels
    if (m_unitsLblMoneyValue) m_unitsLblMoneyValue->SetLabel(wxString::Format("%d", m_money));
    if (m_unitsLblResearchValue) m_unitsLblResearchValue->SetLabel(wxString::Format("%d", m_research));
    if (m_unitsLblTurnValue) m_unitsLblTurnValue->SetLabel(wxString::Format("%d", m_turn));
}

void StrategicLevelFrame::LeaveUnitsMode()
{
    m_unitsModeActive = false;

    ShowUnitsPanel(false);

    RefreshUI();
    Refresh();
}

void StrategicLevelFrame::OnUnitsShop(wxCommandEvent&)
{
    if (m_unitsModeActive)
        LeaveUnitsMode();
    else {
        if (m_buyModeActive) LeaveBuyMode();
        if (m_researchMode) LeaveResearchMode();
        EnterUnitsMode();
    }
}

void StrategicLevelFrame::RefreshUnitsRoster()
{
    if (!m_unitsRoster) return;

    m_unitsRoster->Freeze();
    m_unitsRoster->DeleteAllItems();

    for (size_t i = 0; i < m_playerUnits.size(); i++)
    {
        const auto& u = m_playerUnits[i];
        wxString name = GetUnitDisplayName(u.unit_id);

        // Check for custom name in unit state
        if (i < m_unitStates.size() && !m_unitStates[i].custom_name.empty())
            name = wxString::FromUTF8(m_unitStates[i].custom_name);

        long idx = m_unitsRoster->InsertItem(i, name);
        m_unitsRoster->SetItem(idx, 1, wxString::Format("%d%%", u.health));

        // XP/Level
        int xp = (i < m_unitStates.size()) ? m_unitStates[i].experience : 0;
        m_unitsRoster->SetItem(idx, 2, wxString::Format("L%d", xp / 100));

        // Status (cooldown)
        int cooldown = (i < m_unitStates.size()) ? m_unitStates[i].cooldown_turns : 0;
        if (cooldown > 0)
            m_unitsRoster->SetItem(idx, 3, wxString::Format("-%dT", cooldown));
        else
            m_unitsRoster->SetItem(idx, 3, "Ready");

        m_unitsRoster->SetItemData(idx, i);
    }

    // Auto-size columns
    for (int c = 0; c < 4; c++)
        m_unitsRoster->SetColumnWidth(c, wxLIST_AUTOSIZE_USEHEADER);

    m_unitsRoster->Thaw();
}

void StrategicLevelFrame::RefreshUnitsShopList()
{
    if (!m_unitsShopList) return;

    m_unitsShopList->Freeze();
    m_unitsShopList->DeleteAllItems();

    // Show/hide controls based on current tab
    bool showQuality = (m_unitsCurrentTab == UNITS_TAB_RECRUIT);
    bool showUpgrade = (m_unitsCurrentTab == UNITS_TAB_UPGRADE);

    if (m_unitsQualityChoice) m_unitsQualityChoice->Show(showQuality);
    if (m_unitsUpgradeChoice) m_unitsUpgradeChoice->Show(showUpgrade);
    if (m_unitsArtCanvas) m_unitsArtCanvas->Show(m_unitsCurrentTab == UNITS_TAB_INFO);

    switch (m_unitsCurrentTab)
    {
    case UNITS_TAB_RECRUIT:
        // Show recruit quality options (already handled by choice control)
        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
        {
            const auto& u = m_playerUnits[m_unitsSelectedUnit];
            if (u.health < 100)
            {
                m_unitsShopList->InsertItem(0, wxString::Format("Recruit to %d%% health",
                    RECRUIT_QUALITY_PERCENT[m_unitsQualityChoice ? m_unitsQualityChoice->GetSelection() : 1]));
            }
            else
            {
                m_unitsShopList->InsertItem(0, "Unit is at full strength");
            }
        }
        break;

    case UNITS_TAB_DISBAND:
        // Show disband option
        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
        {
            const auto& u = m_playerUnits[m_unitsSelectedUnit];
            int cost = GetUnitBuyCost(u.unit_id);
            int refund = cost > 0 ? cost / 2 : 0;
            m_unitsShopList->InsertItem(0, wxString::Format("Disband unit (refund: %d)", refund));
        }
        break;

    case UNITS_TAB_UPGRADE:
        // Show available upgrades
        if (m_unitsUpgradeChoice)
        {
            m_unitsUpgradeChoice->Clear();
            if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
            {
                const auto& u = m_playerUnits[m_unitsSelectedUnit];
                auto upgrades = GetAvailableUnitTypesForUpgrade(u.unit_id);
                for (int upId : upgrades)
                {
                    wxString name = GetUnitDisplayName(upId);
                    m_unitsUpgradeChoice->Append(name, reinterpret_cast<void*>(static_cast<intptr_t>(upId)));
                }
                if (m_unitsUpgradeChoice->GetCount() > 0)
                    m_unitsUpgradeChoice->SetSelection(0);
            }
        }
        // Show item upgrades list
        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
        {
            const auto& u = m_playerUnits[m_unitsSelectedUnit];
            auto itemUpgrades = GetAvailableUpgradesForUnit(u.unit_id);
            for (size_t i = 0; i < itemUpgrades.size(); i++)
            {
                // Find upgrade name from research database
                wxString upgName = wxString::Format("Upgrade #%d", itemUpgrades[i]);
                for (const auto& r : m_researchDb) {
                    if (r.id == itemUpgrades[i]) {
                        upgName = r.title;
                        break;
                    }
                }
                long idx = m_unitsShopList->InsertItem(i, upgName);
                m_unitsShopList->SetItemData(idx, itemUpgrades[i]);
            }
        }
        break;

    case UNITS_TAB_INFO:
        // Info mode - show unit properties
        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
        {
            const auto& u = m_playerUnits[m_unitsSelectedUnit];
            if (m_spellData && m_spellData->units)
            {
                auto* unitRec = m_spellData->units->GetUnit(u.unit_id);
                if (unitRec)
                {
                    m_unitsShopList->InsertItem(0, wxString::Format("Sight: %d", unitRec->sdir));
                    m_unitsShopList->InsertItem(1, wxString::Format("Move: %d", unitRec->apw));
                    m_unitsShopList->InsertItem(2, wxString::Format("Defence: %d", unitRec->defence));
                    m_unitsShopList->InsertItem(3, wxString::Format("Light ATK: %d", unitRec->attack_light));
                    m_unitsShopList->InsertItem(4, wxString::Format("Armor ATK: %d", unitRec->attack_armored));
                    m_unitsShopList->InsertItem(5, wxString::Format("Air ATK: %d", unitRec->attack_air));
                    m_unitsShopList->InsertItem(6, wxString::Format("Man count: %d", unitRec->cnt));
                }
            }
        }
        break;
    }

    m_unitsShopList->Thaw();

    if (m_unitsMainPanel)
        m_unitsMainPanel->Layout();
}

void StrategicLevelFrame::RefreshUnitsInfo(int unitIndex)
{
    if (!m_unitsInfoText) return;

    m_unitsInfoText->Clear();

    if (unitIndex < 0 || unitIndex >= (int)m_playerUnits.size())
        return;

    const auto& u = m_playerUnits[unitIndex];

    wxString info;

    // Unit name
    info += "Unit: " + GetUnitDisplayName(u.unit_id) + "\n";

    // Custom name if set
    if (unitIndex < (int)m_unitStates.size() && !m_unitStates[unitIndex].custom_name.empty())
        info += "Name: " + wxString::FromUTF8(m_unitStates[unitIndex].custom_name) + "\n";

    // Health
    info += wxString::Format("Health: %d%%\n", u.health);

    // Experience/Level
    if (unitIndex < (int)m_unitStates.size())
    {
        int xp = m_unitStates[unitIndex].experience;
        int lvl = m_unitStates[unitIndex].level;
        info += wxString::Format("Experience: %d (Level %d)\n", xp, lvl);
    }

    // Cooldown
    if (unitIndex < (int)m_unitStates.size() && m_unitStates[unitIndex].cooldown_turns > 0)
    {
        info += wxString::Format("Status: Unavailable for %d turns\n",
            m_unitStates[unitIndex].cooldown_turns);
    }
    else
    {
        info += "Status: Ready for deployment\n";
    }

    // Category
    info += "Category: " + GetUnitCategoryName(u.unit_id) + "\n";

    // Tab-specific info
    switch (m_unitsCurrentTab)
    {
    case UNITS_TAB_RECRUIT:
        if (u.health < 100)
        {
            int quality = m_unitsQualityChoice ? m_unitsQualityChoice->GetSelection() : 1;
            int cost = GetRecruitCost(unitIndex, quality);
            int time = GetRecruitTime(quality);
            info += wxString::Format("\nRecruit cost: %d\nTime: %d turns\n", cost, time);
        }
        else
        {
            info += "\nUnit is at full strength.\n";
        }
        break;

    case UNITS_TAB_DISBAND:
        {
            int cost = GetUnitBuyCost(u.unit_id);
            int refund = cost > 0 ? cost / 2 : 0;
            info += wxString::Format("\nDisband refund: %d\n", refund);
        }
        break;

    case UNITS_TAB_UPGRADE:
        info += "\nSelect an upgrade from the list.\n";
        break;

    case UNITS_TAB_INFO:
        // Load unit description from INFO files
        if (m_spellData && m_spellData->units && m_spellData->info)
        {
            auto* unitRec = m_spellData->units->GetUnit(u.unit_id);
            if (unitRec)
            {
                auto artList = unitRec->GetArtList(m_spellData->info);
                if (!artList.empty())
                {
                    std::vector<std::string> langs = {"CZ", "ENG"};
                    for (const auto& lang : langs)
                    {
                        std::string artInfoName = artList[0] + "." + lang;
                        uint8_t* txtBuf;
                        int txtSize;
                        if (!m_spellData->info->GetFile(artInfoName.c_str(), &txtBuf, &txtSize))
                        {
                            std::string text(reinterpret_cast<char*>(txtBuf), txtSize);
                            info += "\n" + wxString::FromUTF8(text);
                            break;
                        }
                    }
                }
            }
        }
        break;
    }

    m_unitsInfoText->SetValue(info);

    // Refresh icon
    if (m_unitsIconCanvas)
        m_unitsIconCanvas->Refresh();
}

void StrategicLevelFrame::RefreshUnitsActionButton()
{
    if (!m_btnUnitsAction) return;

    m_btnUnitsAction->Enable(false);

    if (m_unitsSelectedUnit < 0 || m_unitsSelectedUnit >= (int)m_playerUnits.size())
        return;

    const auto& u = m_playerUnits[m_unitsSelectedUnit];

    // Check cooldown
    bool hasCooldown = (m_unitsSelectedUnit < (int)m_unitStates.size() &&
                        m_unitStates[m_unitsSelectedUnit].cooldown_turns > 0);

    switch (m_unitsCurrentTab)
    {
    case UNITS_TAB_RECRUIT:
        {
            m_btnUnitsAction->SetLabel("Recruit");
            if (u.health >= 100)
            {
                // Already at full strength
                m_btnUnitsAction->Enable(false);
            }
            else if (hasCooldown)
            {
                m_btnUnitsAction->Enable(false);
            }
            else
            {
                int quality = m_unitsQualityChoice ? m_unitsQualityChoice->GetSelection() : 1;
                int cost = GetRecruitCost(m_unitsSelectedUnit, quality);
                m_btnUnitsAction->Enable(m_money >= cost);

                if (m_unitsTimeLabel)
                    m_unitsTimeLabel->SetLabel(wxString::Format("Time: %d", GetRecruitTime(quality)));
                if (m_unitsCostLabel)
                    m_unitsCostLabel->SetLabel(wxString::Format("Cost: %d", cost));
            }
        }
        break;

    case UNITS_TAB_DISBAND:
        {
            m_btnUnitsAction->SetLabel("Disband");
            m_btnUnitsAction->Enable(!hasCooldown);

            int cost = GetUnitBuyCost(u.unit_id);
            int refund = cost > 0 ? cost / 2 : 0;
            if (m_unitsTimeLabel)
                m_unitsTimeLabel->SetLabel("Time: 0");
            if (m_unitsCostLabel)
                m_unitsCostLabel->SetLabel(wxString::Format("Refund: %d", refund));
        }
        break;

    case UNITS_TAB_UPGRADE:
        {
            m_btnUnitsAction->SetLabel("Upgrade");

            if (hasCooldown)
            {
                m_btnUnitsAction->Enable(false);
            }
            else if (m_unitsUpgradeChoice && m_unitsUpgradeChoice->GetCount() > 0)
            {
                int sel = m_unitsUpgradeChoice->GetSelection();
                if (sel >= 0)
                {
                    int toUnitId = static_cast<int>(reinterpret_cast<intptr_t>(
                        m_unitsUpgradeChoice->GetClientData(sel)));
                    int cost = GetUpgradeCost(u.unit_id, toUnitId);
                    int time = 2; // Default upgrade time
                    m_btnUnitsAction->Enable(m_money >= cost && cost > 0);

                    if (m_unitsTimeLabel)
                        m_unitsTimeLabel->SetLabel(wxString::Format("Time: %d", time));
                    if (m_unitsCostLabel)
                        m_unitsCostLabel->SetLabel(wxString::Format("Cost: %d", cost));
                }
            }
            else
            {
                m_btnUnitsAction->Enable(false);
            }
        }
        break;

    case UNITS_TAB_INFO:
        {
            m_btnUnitsAction->SetLabel("Close");
            m_btnUnitsAction->Enable(true);

            if (m_unitsTimeLabel)
                m_unitsTimeLabel->SetLabel("Time: -");
            if (m_unitsCostLabel)
                m_unitsCostLabel->SetLabel("Cost: -");
        }
        break;
    }
}

void StrategicLevelFrame::OnUnitsTabChange(int tab)
{
    m_unitsCurrentTab = static_cast<UnitsTab>(tab);
    m_unitsSelectedUpgrade = -1;

    // Update tab button highlighting
    auto highlightTab = [this](wxButton* btn, bool active) {
        if (!btn) return;
        if (active)
            btn->SetBackgroundColour(m_palette.heading);
        else
            btn->SetBackgroundColour(m_palette.buttonBackground);
        btn->Refresh();
    };

    highlightTab(m_btnUnitsTabRecruit, tab == UNITS_TAB_RECRUIT);
    highlightTab(m_btnUnitsTabDisband, tab == UNITS_TAB_DISBAND);
    highlightTab(m_btnUnitsTabUpgrade, tab == UNITS_TAB_UPGRADE);
    highlightTab(m_btnUnitsTabInfo, tab == UNITS_TAB_INFO);

    RefreshUnitsShopList();
    RefreshUnitsInfo(m_unitsSelectedUnit);
    RefreshUnitsActionButton();

    if (m_unitsArtCanvas)
        m_unitsArtCanvas->Refresh();
}

void StrategicLevelFrame::OnUnitsAction(wxCommandEvent&)
{
    if (m_unitsSelectedUnit < 0 || m_unitsSelectedUnit >= (int)m_playerUnits.size())
        return;

    auto& u = m_playerUnits[m_unitsSelectedUnit];

    // Ensure unit state exists
    while (m_unitStates.size() <= (size_t)m_unitsSelectedUnit)
    {
        UnitInstanceState state;
        state.uid = m_nextRosterUid++;
        m_unitStates.push_back(state);
    }

    switch (m_unitsCurrentTab)
    {
    case UNITS_TAB_RECRUIT:
        {
            if (u.health >= 100)
            {
                wxMessageBox("Unit is already at full strength.", "Recruit", wxOK | wxICON_INFORMATION, this);
                return;
            }

            int quality = m_unitsQualityChoice ? m_unitsQualityChoice->GetSelection() : 1;
            int cost = GetRecruitCost(m_unitsSelectedUnit, quality);
            int time = GetRecruitTime(quality);

            if (m_money < cost)
            {
                wxMessageBox(wxString::Format("Not enough money. Need %d, have %d.", cost, m_money),
                    "Recruit", wxOK | wxICON_WARNING, this);
                return;
            }

            // Apply recruitment
            m_money -= cost;
            u.health = RECRUIT_QUALITY_PERCENT[quality];
            m_unitStates[m_unitsSelectedUnit].cooldown_turns = time;

            wxMessageBox(wxString::Format("Unit recruited to %d%% strength.\nReady in %d turns.",
                u.health, time), "Recruit", wxOK | wxICON_INFORMATION, this);
        }
        break;

    case UNITS_TAB_DISBAND:
        {
            int cost = GetUnitBuyCost(u.unit_id);
            int refund = cost > 0 ? cost / 2 : 0;

            int result = wxMessageBox(
                wxString::Format("Disband this unit for %d credits?", refund),
                "Disband", wxYES_NO | wxICON_QUESTION, this);

            if (result != wxYES)
                return;

            m_money += refund;
            m_playerUnits.erase(m_playerUnits.begin() + m_unitsSelectedUnit);
            if ((size_t)m_unitsSelectedUnit < m_unitStates.size())
                m_unitStates.erase(m_unitStates.begin() + m_unitsSelectedUnit);

            m_unitsSelectedUnit = -1;
        }
        break;

    case UNITS_TAB_UPGRADE:
        {
            if (!m_unitsUpgradeChoice || m_unitsUpgradeChoice->GetCount() == 0)
                return;

            int sel = m_unitsUpgradeChoice->GetSelection();
            if (sel < 0)
                return;

            int toUnitId = static_cast<int>(reinterpret_cast<intptr_t>(
                m_unitsUpgradeChoice->GetClientData(sel)));
            int cost = GetUpgradeCost(u.unit_id, toUnitId);
            int time = 2;

            if (m_money < cost)
            {
                wxMessageBox(wxString::Format("Not enough money. Need %d, have %d.", cost, m_money),
                    "Upgrade", wxOK | wxICON_WARNING, this);
                return;
            }

            wxString fromName = GetUnitDisplayName(u.unit_id);
            wxString toName = GetUnitDisplayName(toUnitId);

            int result = wxMessageBox(
                wxString::Format("Upgrade %s to %s for %d credits?\nReady in %d turns.",
                    fromName, toName, cost, time),
                "Upgrade", wxYES_NO | wxICON_QUESTION, this);

            if (result != wxYES)
                return;

            m_money -= cost;
            u.unit_id = toUnitId;
            m_unitStates[m_unitsSelectedUnit].cooldown_turns = time;

            wxMessageBox(wxString::Format("Unit upgraded to %s.\nReady in %d turns.",
                toName, time), "Upgrade", wxOK | wxICON_INFORMATION, this);
        }
        break;

    case UNITS_TAB_INFO:
        // Close info mode
        LeaveUnitsMode();
        return;
    }

    SaveStrategicState();
    RefreshUI();
    RefreshUnitsRoster();
    RefreshUnitsShopList();
    RefreshUnitsInfo(m_unitsSelectedUnit);
    RefreshUnitsActionButton();

    // Update status labels
    if (m_unitsLblMoneyValue)
        m_unitsLblMoneyValue->SetLabel(wxString::Format("%d", m_money));
}

void StrategicLevelFrame::ApplyUnitsCooldownTick()
{
    for (auto& state : m_unitStates)
    {
        if (state.cooldown_turns > 0)
            state.cooldown_turns--;
    }
}

int StrategicLevelFrame::GetRecruitCost(int unitIndex, int quality) const
{
    if (unitIndex < 0 || unitIndex >= (int)m_playerUnits.size())
        return 0;

    const auto& u = m_playerUnits[unitIndex];
    int baseCost = GetUnitBuyCost(u.unit_id);
    if (baseCost <= 0)
        return 0;

    // Cost is proportional to damage and quality
    int damage = 100 - u.health;
    int costPercent = (damage * RECRUIT_QUALITY_COST_MULT[quality]) / 100;
    return (baseCost * costPercent) / 100;
}

int StrategicLevelFrame::GetRecruitTime(int quality) const
{
    if (quality < 0 || quality >= RECRUIT_QUALITY_COUNT)
        return 1;
    return RECRUIT_QUALITY_TIME[quality];
}

int StrategicLevelFrame::GetUpgradeCost(int unitId, int upgradeId) const
{
    int fromCost = GetUnitBuyCost(unitId);
    int toCost = GetUnitBuyCost(upgradeId);

    if (fromCost <= 0 || toCost <= 0)
        return 0;

    // Upgrade cost is the difference plus 20%
    int diff = toCost - fromCost;
    if (diff < 0) diff = 0;
    return diff + (diff / 5);
}

int StrategicLevelFrame::GetUpgradeTime(int upgradeId) const
{
    // Default upgrade time
    return 2;
}

wxString StrategicLevelFrame::GetUnitCategoryName(int unitId) const
{
    auto it = m_unitCategories.find(unitId);
    if (it != m_unitCategories.end())
        return wxString::FromUTF8(it->second);

    // Try to determine from unit type
    if (m_spellData && m_spellData->units)
    {
        auto* unitRec = m_spellData->units->GetUnit(unitId);
        if (unitRec)
        {
            // Simple category detection based on unit properties
            if (unitRec->attack_air > 0 && unitRec->attack_air > unitRec->attack_light)
                return "Air Defense";
            if (unitRec->attack_armored > unitRec->attack_light)
                return "Heavy";
            return "Light";
        }
    }
    return "Unknown";
}

bool StrategicLevelFrame::CanUpgradeUnitTo(int fromUnitId, int toUnitId) const
{
    // Check if units are in the same category
    wxString fromCat = GetUnitCategoryName(fromUnitId);
    wxString toCat = GetUnitCategoryName(toUnitId);

    if (fromCat != toCat)
        return false;

    // Check if upgrade is researched
    // For now, allow all same-category upgrades
    return true;
}

std::vector<int> StrategicLevelFrame::GetAvailableUpgradesForUnit(int unitId) const
{
    std::vector<int> result;

    // Find upgrades from research database that apply to this unit
    for (const auto& r : m_researchDb)
    {
        if (r.flags.Contains("UpgradeItem") && m_researchCompleted.count(r.id))
        {
            // Check if this upgrade applies to the unit type
            // For now, include all completed UpgradeItem research
            result.push_back(r.id);
        }
    }

    return result;
}

std::vector<int> StrategicLevelFrame::GetAvailableUnitTypesForUpgrade(int unitId) const
{
    std::vector<int> result;

    if (!m_spellData || !m_spellData->units)
        return result;

    wxString currentCat = GetUnitCategoryName(unitId);

    // Find all units in the same category that are different
    for (int i = 0; i < m_spellData->units->Count(); i++)
    {
        if (i == unitId)
            continue;

        wxString cat = GetUnitCategoryName(i);
        if (cat == currentCat)
        {
            // Check if upgrade to this unit is unlocked via research
            // For now, include all same-category units
            result.push_back(i);
        }
    }

    return result;
}

void StrategicLevelFrame::BuildHierarchyPage(wxPanel* parent)
{
    auto* hs = new wxBoxSizer(wxVERTICAL);

    auto* hTitle = CreateStrategicLabel(
        parent,
        { { "Units / Hierarchy", m_palette.heading, &m_fontHeading } },
        m_fontHeading,
        m_palette.shadow,
        &m_palette.background);

    hs->Add(hTitle, 0, wxALL, 8);

    m_hierarchyBook = new wxSimplebook(parent, wxID_ANY);
    m_hierarchyBook->SetBackgroundColour(m_palette.background);
    m_hierarchyBook->AddPage(BuildHierarchyBookPage(m_hierarchyBook, 1), "Page 1", true);
    m_hierarchyBook->AddPage(BuildHierarchyBookPage(m_hierarchyBook, 2), "Page 2", false);
    hs->Add(m_hierarchyBook, 1, wxALL | wxEXPAND, 8);

    m_btnHierarchyPageToggle = new wxButton(parent, wxID_ANY, "Go to Page 2");
    m_btnHierarchyPageToggle->SetFont(m_fontText);
    m_btnHierarchyPageToggle->SetBackgroundColour(m_palette.buttonBackground);
    m_btnHierarchyPageToggle->SetForegroundColour(m_palette.buttonText);
    m_btnHierarchyPageToggle->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnHierarchyTogglePage, this);
    hs->Add(m_btnHierarchyPageToggle, 0, wxALL | wxALIGN_RIGHT, 8);

    parent->SetSizer(hs);
}

wxPanel* StrategicLevelFrame::BuildHierarchyFormation(wxWindow* parent,
    const wxString& label,
    const wxColour& color,
    wxSizer* contents)
{
    auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
    panel->SetBackgroundColour(m_palette.background);

    auto* borderSizer = new wxBoxSizer(wxVERTICAL);
    auto* title = CreateStrategicLabel(panel, label, m_fontText, color, m_palette.shadow);
    borderSizer->Add(title, 0, wxALL, 6);
    borderSizer->Add(contents, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
    panel->SetSizer(borderSizer);
    return panel;
}

wxPanel* StrategicLevelFrame::BuildHierarchySlot(wxWindow* parent,
    const wxString& placeholder,
    const std::string& slotId,
    const std::string& type)
{
    // Spellcross-like slot: compact, left aligned, custom green border (not system wxBORDER_SIMPLE).
    const wxSize slotSize(180, 24);

    auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, slotSize, wxBORDER_NONE);
    panel->SetBackgroundColour(m_palette.background);
    panel->SetBackgroundStyle(wxBG_STYLE_PAINT);

    auto* label = new wxStaticText(panel, wxID_ANY, placeholder);
    label->SetFont(m_fontText);
    label->SetForegroundColour(m_palette.text);

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6);
    panel->SetSizer(sizer);

    // Draw custom border
    panel->Bind(wxEVT_PAINT, [this, panel](wxPaintEvent&) {
        wxPaintDC dc(panel);
        dc.SetBackground(wxBrush(panel->GetBackgroundColour()));
        dc.Clear();
        const wxSize sz = panel->GetClientSize();
        dc.SetPen(wxPen(wxColour(70, 110, 70), 1));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(0, 0, sz.x - 1, sz.y - 1);
        });

    RegisterHierarchySlot(slotId, type, label, placeholder);

    if (type == "commander")
    {
        // Commander slots: drag & drop from the commanders roster.
        panel->SetDropTarget(new HierarchySlotDropTarget(this, slotId));

        // Drag from label OR panel (move commander between slots)
        auto bindDrag = [this, slotId](wxWindow* w) {
            w->Bind(wxEVT_LEFT_DOWN, [this, slotId](wxMouseEvent& ev) {
                BeginHierarchySlotDrag(slotId, static_cast<wxWindow*>(ev.GetEventObject()));
                ev.Skip();
                });
            };
        bindDrag(label);
        bindDrag(panel);
    }
    else if (type == "unit")
    {
        // Unit slots:
        // - left click on empty slot => choose a unit instance from roster
        // - left click on filled slot => mark it as the commander's assigned unit
        // - right click => always open chooser (change/clear)
        auto bindHandlers = [this, slotId](wxWindow* w) {
            w->Bind(wxEVT_LEFT_UP, [this, slotId](wxMouseEvent& ev) {
                auto it = m_hierarchySlotIndex.find(slotId);
                if (it != m_hierarchySlotIndex.end())
                {
                    HierarchySlot& s = m_hierarchySlots[it->second];
                    if (s.unit_uid == 0)
                        ChooseUnitForHierarchySlot(slotId);
                    else
                        TryAssignCommanderToUnitSlot(slotId);
                }
                ev.Skip();
                });
            w->Bind(wxEVT_RIGHT_UP, [this, slotId](wxMouseEvent& ev) {
                ChooseUnitForHierarchySlot(slotId);
                ev.Skip();
                });
            };
        bindHandlers(label);
        bindHandlers(panel);
    }

    return panel;
}


wxWindow* StrategicLevelFrame::BuildHierarchyBookPage(wxWindow* parent, int brigadeIndex)
{
    // NOTE: We intentionally do NOT use nested sizers here.
    // The original Spellcross hierarchy screen is a hand-placed tree.
    // We mimic that by using a fixed canvas with absolute positions + painted connector lines.

    class HierarchyCanvas : public wxPanel
    {
    public:
        HierarchyCanvas(StrategicLevelFrame* owner, wxWindow* parent,
            wxColour frameCol, wxColour lineCol)
            : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
            , m_owner(owner)
            , m_frameCol(frameCol)
            , m_lineCol(lineCol)
        {
            SetBackgroundColour(owner->m_palette.background);
            SetBackgroundStyle(wxBG_STYLE_PAINT);
            Bind(wxEVT_PAINT, &HierarchyCanvas::OnPaint, this);
        }

        void AddLine(wxPoint a, wxPoint b) { m_lines.push_back({ a, b }); }

    private:
        void OnPaint(wxPaintEvent&)
        {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetBackgroundColour()));
            dc.Clear();

            //// Subtle frame around whole tree area
            //const wxSize sz = GetClientSize();
            //dc.SetPen(wxPen(m_frameCol, 1));
            //dc.SetBrush(*wxTRANSPARENT_BRUSH);
            //dc.DrawRectangle(0, 0, sz.x - 1, sz.y - 1);

            // Tree connector lines
            dc.SetPen(wxPen(m_lineCol, 1));
            for (const auto& ln : m_lines)
                dc.DrawLine(ln.first, ln.second);
        }

        StrategicLevelFrame* m_owner = nullptr;
        wxColour m_frameCol;
        wxColour m_lineCol;
        std::vector<std::pair<wxPoint, wxPoint>> m_lines;
    };

    // -------------------------------------------------------------------------
    // TUNING PARAMETERS (edit these only)
    // -------------------------------------------------------------------------
    struct Layout
    {
        // Scroller / canvas
        int scrollStepY = 12;
        int canvasW = 800;
        int canvasH = 660;
        int canvasMargin = 8;

        int rightPadding = 20;     // padding from right edge for top commander column
        int minCanvasW = 760;      // base (your current)
        int minCanvasH = 720;

        // Columns (left to right)
        int x_units = 20;     // unit slots (left stack)
        int x_bcmd = 220;    // battalion commander column
        int x_rcmd = 440;    // regiment commander column
        int x_brig = 660;    // brigade commander column

        // Slot geometry
        int slotW = 160;
        int slotH = 26;       // you said you need 26
        int gapY = 6;

        // Regiment blocks vertical placement
        int regTopY = 40;             // top Y of first regiment block
        int regBlockHeight = 280;     // distance between regiments (was 320)
        int regCommanderOffsetY = 40; // commander Y inside regiment block

        // Battalion placement inside regiment block
        int battalionPairGapY = 140;  // distance between 2 battalion groups within a regiment (tune)
        int unitsStackOffsetY = 0;    // allows nudging unit stack down/up inside battalion group

        // Optional: brigade node Y
        int brigadeCommanderY = 120;

        // Connector line colors
        wxColour frameCol = wxColour(50, 80, 50);
        wxColour lineCol = wxColour(70, 110, 70);

        // Connector geometry tweaks (helps if you want junction-style later)
        int lineInset = 0; // e.g. 2..6 if you want lines not touching borders
    } L;

    // -------------------------------------------------------------------------
    // UI setup
    // -------------------------------------------------------------------------
    auto* page = new wxWindow(parent, wxID_ANY);
    page->SetBackgroundColour(m_palette.background);
    //page->SetScrollRate(0, L.scrollStepY);

    auto* canvas = new HierarchyCanvas(this, page, L.frameCol, L.lineCol);
    canvas->SetMinSize(wxSize(L.canvasW, L.canvasH));

    auto computeCanvasWidth = [&]() -> int
        {
            // viewport width inside scroller (how much we can show without horizontal scroll)
            int viewportW = page->GetClientSize().x - 2 * L.canvasMargin;
            if (viewportW < 0) viewportW = 0;

            // minimum width needed so right-most column is never clipped
            const int needW = L.x_brig + L.slotW + L.rightPadding;

            // final width = at least: base, viewport, and needed for right column
            int w = std::max({ L.minCanvasW, viewportW, needW });
            return w;
        };

    auto applyCanvasSize = [&]()
        {
            const int w = computeCanvasWidth();
            canvas->SetMinSize(wxSize(w, L.minCanvasH));
            canvas->SetSize(wxSize(w, L.minCanvasH));
        };

    // Keep top-level commander column always fully visible (stick to right edge)
    L.x_brig = canvas->GetClientSize().x - L.slotW - L.rightPadding;
    if (L.x_brig < L.x_rcmd + L.slotW + 40) // safety so it doesn't collide
        L.x_brig = L.x_rcmd + L.slotW + 40;

    // Helper to place slot widgets at exact coordinates.
    auto place = [&](int x, int y, const wxString& ph, const std::string& id, const std::string& type) {
        wxPanel* p = BuildHierarchySlot(canvas, ph, id, type);
        p->SetSize(wxRect(x, y, L.slotW, L.slotH));
        return p;
        };

    // Derived helpers
    auto slotMidY = [&](int yTop) { return yTop + L.slotH / 2; };

    // Battalion blocks (4 per brigade page)
    const int battalionBase = (brigadeIndex - 1) * 4;

    // Place battalions stacked vertically (2 regiments, each has 2 battalions).
    auto battalionTopY = [&](int bLocal) {
        const int regLocal = bLocal / 2;  // 0 or 1
        const int inReg = bLocal % 2;  // 0 or 1
        const int regY = L.regTopY + regLocal * L.regBlockHeight;
        return regY + inReg * L.battalionPairGapY;
        };

    // -------------------------------------------------------------------------
    // Battalions: units + battalion commander + assigned unit
    // -------------------------------------------------------------------------
    for (int bLocal = 0; bLocal < 4; ++bLocal)
    {
        const int bIndex = battalionBase + bLocal + 1;
        const int y0 = battalionTopY(bLocal);

        // 4 unit slots
        for (int u = 0; u < 4; ++u)
        {
            const std::string id = "battalion_" + std::to_string(bIndex) + "_unit_" + std::to_string(u + 1);
            const int y = y0 + L.unitsStackOffsetY + u * (L.slotH + L.gapY);
            place(L.x_units, y, "unit", id, "unit");
        }

        // battalion commander + assigned unit
        place(L.x_bcmd, y0 + 0 * (L.slotH + L.gapY), "commander",
            "battalion_" + std::to_string(bIndex) + "_commander", "commander");
        place(L.x_bcmd, y0 + 1 * (L.slotH + L.gapY), "?",
            "battalion_" + std::to_string(bIndex) + "_commander_unit", "unit");

        // connector: units stack -> battalion commander
        const int yMidUnits = y0 + L.unitsStackOffsetY + 1 * (L.slotH + L.gapY) + L.slotH / 2;
        canvas->AddLine(wxPoint(L.x_units + L.slotW - L.lineInset, yMidUnits),
            wxPoint(L.x_bcmd + L.lineInset, yMidUnits));
    }

    // -------------------------------------------------------------------------
    // Regiments: one commander per 2 battalions
    // -------------------------------------------------------------------------
    for (int rLocal = 0; rLocal < 2; ++rLocal)
    {
        const int regimentIndex = (brigadeIndex - 1) * 2 + rLocal + 1;
        const int yReg = L.regTopY + rLocal * L.regBlockHeight + L.regCommanderOffsetY;

        place(L.x_rcmd, yReg, "commander",
            "regiment_" + std::to_string(regimentIndex) + "_commander", "commander");
        place(L.x_rcmd, yReg + (L.slotH + L.gapY), "?",
            "regiment_" + std::to_string(regimentIndex) + "_unit", "unit");

        // connectors: both battalion commander nodes -> regiment commander
        const int b0 = rLocal * 2;
        const int b1 = rLocal * 2 + 1;
        const int y0 = battalionTopY(b0) + L.slotH / 2;
        const int y1 = battalionTopY(b1) + L.slotH / 2;

        const int xFrom = L.x_bcmd + L.slotW - L.lineInset;
        const int xTo = L.x_rcmd + L.lineInset;
        const int yTo = yReg + L.slotH / 2;

        canvas->AddLine(wxPoint(xFrom, y0), wxPoint(xTo, yTo));
        canvas->AddLine(wxPoint(xFrom, y1), wxPoint(xTo, yTo));
    }

    // -------------------------------------------------------------------------
    // Brigade: commander per brigade page
    // -------------------------------------------------------------------------
    {
        const int yBrig = L.brigadeCommanderY;

        place(L.x_brig, yBrig, "commander",
            "brigade_" + std::to_string(brigadeIndex) + "_commander", "commander");
        place(L.x_brig, yBrig + (L.slotH + L.gapY), "?",
            "brigade_" + std::to_string(brigadeIndex) + "_unit", "unit");

        // connectors: both regiment nodes -> brigade node
        const int xFrom = L.x_rcmd + L.slotW - L.lineInset;
        const int xTo = L.x_brig + L.lineInset;

        const int yR0 = L.regTopY + 0 * L.regBlockHeight + L.regCommanderOffsetY + L.slotH / 2;
        const int yR1 = L.regTopY + 1 * L.regBlockHeight + L.regCommanderOffsetY + L.slotH / 2;

        canvas->AddLine(wxPoint(xFrom, yR0), wxPoint(xTo, yBrig + L.slotH / 2));
        canvas->AddLine(wxPoint(xFrom, yR1), wxPoint(xTo, yBrig + L.slotH / 2));
    }

    // Put canvas into scroller
    auto* s = new wxBoxSizer(wxVERTICAL);
    s->Add(canvas, 1, wxEXPAND | wxALL, L.canvasMargin);
    page->SetSizer(s);
    page->FitInside();
    return page;
}



void StrategicLevelFrame::RegisterHierarchySlot(const std::string& slotId,
    const std::string& type,
    wxStaticText* label,
    const wxString& placeholder)
{
    HierarchySlot slot;
    slot.id = slotId;
    slot.type = type;
    slot.label = label;
    slot.placeholder = placeholder;
    slot.rank = -1;
    m_hierarchySlotIndex[slotId] = m_hierarchySlots.size();
    m_hierarchySlots.push_back(std::move(slot));
}

void StrategicLevelFrame::ApplyHierarchyDrop(const std::string& slotId, const wxString& data)
{
    auto it = m_hierarchySlotIndex.find(slotId);
    if (it == m_hierarchySlotIndex.end())
        return;

    HierarchySlot& slot = m_hierarchySlots[it->second];
    HierarchyDragData parsed = ParseHierarchyDragData(data);
    if (!parsed.valid)
        return;
    if (parsed.type != slot.type)
        return;

    // Rank gating for commander slots:
    // - battalion: any commander
    // - regiment: rank >= Major (3)
    // - brigade:  rank >= Major General (6)
    if (slot.type == "commander")
    {
        // Resolve commander UID (required to ensure a commander cannot occupy multiple slots).
        uint32_t cmdUid = parsed.commander_uid;
        if (cmdUid == 0)
        {
            // Backward compatibility payloads don't carry UID. Best-effort lookup by name + rank.
            const std::string name = parsed.name.ToStdString();
            for (const auto& c : m_playerCommanders)
            {
                if (c.name == name && (parsed.rank < 0 || c.rank == parsed.rank))
                {
                    cmdUid = c.uid;
                    break;
                }
            }
        }

        int required = 0;
        if (slotId.find("regiment_") != std::string::npos)
            required = 3;
        else if (slotId.find("brigade_") != std::string::npos)
            required = 6;

        // If we didn't receive rank, attempt a best-effort lookup by name.
        int rank = parsed.rank;
        if (rank < 0)
        {
            const std::string name = parsed.name.ToStdString();
            for (const auto& c : m_playerCommanders)
            {
                if (c.name == name)
                {
                    rank = c.rank;
                    break;
                }
            }
        }

        if (rank < required)
        {
            wxMessageBox(
                wxString::Format("This commander needs at least %s for this slot.",
                    required == 6 ? "MajGen." : required == 3 ? "Maj." : "any rank"),
                "Hierarchy",
                wxOK | wxICON_INFORMATION,
                this);
            return;
        }

        // Enforce uniqueness: move the commander if they're already placed elsewhere.
        if (cmdUid != 0)
        {
            for (auto& s : m_hierarchySlots)
            {
                if (s.type == "commander" && s.commander_uid == cmdUid && s.id != slotId)
                {
                    ClearHierarchySlot(s.id);
                    break;
                }
            }
        }

        slot.rank = rank;
        slot.commander_uid = cmdUid;
        wxString baseName = parsed.name;
        const wxString abbr = GetRankAbbrev(rank);
        if (baseName.StartsWith(abbr + " "))
            baseName = baseName.Mid(abbr.length() + 1);
        slot.commander_name = baseName.ToUTF8().data();
        // Switching commander resets its assigned unit.
        slot.assigned_unit_uid = 0;
        slot.assigned_unit_display.clear();
        slot.label->SetLabel(wxString::Format("%s %s", abbr, baseName));
        // Ensure consistent formatting (and clear any stale "assigned" marker).
        UpdateCommanderHierarchyLabel(slotId);
    }
    else
    {
        // unit slot: just store the chosen unit name
        slot.label->SetLabel(parsed.name);
    }

    slot.label->GetParent()->Layout();

    if (parsed.fromSlot && parsed.slotId != slotId)
        ClearHierarchySlot(parsed.slotId);
}

void StrategicLevelFrame::ClearHierarchySlot(const std::string& slotId)
{
    auto it = m_hierarchySlotIndex.find(slotId);
    if (it == m_hierarchySlotIndex.end())
        return;
    HierarchySlot& slot = m_hierarchySlots[it->second];

    if (slot.type == "unit")
    {
        // Clearing a unit slot should NOT wipe commander state.
        const uint32_t oldUid = slot.unit_uid;
        slot.unit_uid = 0;
        slot.unit_display.clear();
        slot.label->SetLabel(slot.placeholder);
        slot.label->GetParent()->Layout();

        // If this unit was the assigned unit for the commander above, unassign it.
        if (oldUid != 0)
        {
            const std::string commanderId = GetCommanderSlotForUnitSlot(slotId);
            auto itc = m_hierarchySlotIndex.find(commanderId);
            if (itc != m_hierarchySlotIndex.end())
            {
                HierarchySlot& cs = m_hierarchySlots[itc->second];
                if (cs.type == "commander" && cs.assigned_unit_uid == oldUid)
                {
                    cs.assigned_unit_uid = 0;
                    cs.assigned_unit_display.clear();
                    UpdateCommanderHierarchyLabel(commanderId);
                }
            }
        }
        return;
    }

    // Commander slot: clear commander + its assignment.
    slot.label->SetLabel(slot.placeholder);
    slot.rank = -1;
    slot.commander_uid = 0;
    slot.commander_name.clear();
    slot.assigned_unit_uid = 0;
    slot.assigned_unit_display.clear();

    // Also clear the paired "assignment unit" slot (the "?" slot under commander nodes), if present.
    {
        std::string assignmentId;
        if (slotId.find("battalion_") == 0 && endsWith(slotId, "_commander"))
            assignmentId = slotId + "_unit"; // battalion_X_commander -> battalion_X_commander_unit
        else if (slotId.find("regiment_") == 0 && endsWith(slotId, "_commander"))
            assignmentId = slotId.substr(0, slotId.size() - std::string("_commander").size()) + "_unit";
        else if (slotId.find("brigade_") == 0 && endsWith(slotId, "_commander"))
            assignmentId = slotId.substr(0, slotId.size() - std::string("_commander").size()) + "_unit";

        if (!assignmentId.empty())
            ClearHierarchySlot(assignmentId);
    }
    slot.label->GetParent()->Layout();
}

void StrategicLevelFrame::BeginHierarchySlotDrag(const std::string& slotId, wxWindow* source)
{
    auto it = m_hierarchySlotIndex.find(slotId);
    if (it == m_hierarchySlotIndex.end())
        return;
    HierarchySlot& slot = m_hierarchySlots[it->second];
    if (slot.label->GetLabel() == slot.placeholder)
        return;
    wxTextDataObject dataObject(
        slot.type == "commander"
        ? wxString::Format("slot:%s:%s:%u:%d:%s", slot.id.c_str(), slot.type.c_str(),
            (unsigned)slot.commander_uid, slot.rank,
            slot.commander_name.empty() ? slot.label->GetLabel() : wxString::FromUTF8(slot.commander_name))
        : wxString::Format("slot:%s:%s:%s", slot.id.c_str(), slot.type.c_str(), slot.label->GetLabel()));
    wxDropSource dropSource(dataObject, source);
    dropSource.DoDragDrop(wxDrag_CopyOnly);
}


std::vector<StrategicLevelFrame::RosterPickItem> StrategicLevelFrame::GetRosterPickItems() const
{
    std::vector<RosterPickItem> out;
    if (!m_roster)
        return out;

    const long count = m_roster->GetItemCount();
    out.reserve((size_t)count);

    // Build stable-ish uids per roster row. We store uid in ItemData when inserting.
    for (long i = 0; i < count; ++i)
    {
        const wxString display = m_roster->GetItemText(i);
        const long data = m_roster->GetItemData(i);
        const uint32_t uid = (data >= 0) ? (uint32_t)data : 0;
        if (display.empty() || uid == 0)
            continue;

        RosterPickItem it;
        it.uid = uid;
        it.display = display;
        // Label shown in picker: include uid to disambiguate duplicates.
        it.label = wxString::Format("%s  [#%u]", display, (unsigned)uid);
        out.push_back(it);
    }
    return out;
}

std::string StrategicLevelFrame::GetCommanderSlotForUnitSlot(const std::string& unitSlotId) const
{
    // battalion_X_commander_unit -> battalion_X_commander
    // regiment_X_unit           -> regiment_X_commander
    // brigade_X_unit            -> brigade_X_commander
    auto endsWith = [](const std::string& s, const std::string& suf) {
        return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
        };

    // battalion_X_unit_N -> battalion_X_commander (unit slots are numbered, so they don't end with "_unit")
    {
        const std::string pre = "battalion_";
        const std::string mid = "_unit_";
        if (unitSlotId.rfind(pre, 0) == 0)
        {
            const size_t p = unitSlotId.find(mid);
            if (p != std::string::npos)
            {
                const std::string idx = unitSlotId.substr(pre.size(), p - pre.size());
                // idx should be numeric, but even if not, keep best-effort.
                return pre + idx + "_commander";
            }
        }
    }

    if (endsWith(unitSlotId, "_commander_unit"))
        return unitSlotId.substr(0, unitSlotId.size() - std::string("_unit").size());

    if (endsWith(unitSlotId, "_unit"))
    {
        std::string base = unitSlotId.substr(0, unitSlotId.size() - std::string("_unit").size());
        if (base.find("_commander") == std::string::npos)
            base += "_commander";
        return base;
    }

    return std::string();
}

void StrategicLevelFrame::ChooseUnitForHierarchySlot(const std::string& unitSlotId)
{
    auto it = m_hierarchySlotIndex.find(unitSlotId);
    if (it == m_hierarchySlotIndex.end())
        return;

    HierarchySlot& slot = m_hierarchySlots[it->second];
    if (slot.type != "unit")
        return;

    // Special case: assignment slot (the "?" slot under commander nodes). This slot must NOT
    // allow picking any roster unit. Instead, it selects ONE of the units already present in
    // the commander's subtree (4/8/16).
    {
        auto endsWith = [](const std::string& s, const std::string& suf) {
            return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
            };
        const bool isBattalionAssign = endsWith(unitSlotId, "_commander_unit");
        const bool isRegimentAssign = (unitSlotId.rfind("regiment_", 0) == 0) && endsWith(unitSlotId, "_unit");
        const bool isBrigadeAssign = (unitSlotId.rfind("brigade_", 0) == 0) && endsWith(unitSlotId, "_unit");
        if (isBattalionAssign || isRegimentAssign || isBrigadeAssign)
        {
            ChooseAssignedUnitForCommanderAssignmentSlot(unitSlotId);
            return;
        }
    }

    // If commander above is missing, still allow CLEARING a filled slot, but block ASSIGNING a new unit.
    const std::string commanderId = GetCommanderSlotForUnitSlot(unitSlotId);
    bool commanderPresent = true;
    if (!commanderId.empty())
    {
        auto itc = m_hierarchySlotIndex.find(commanderId);
        if (itc != m_hierarchySlotIndex.end())
        {
            const HierarchySlot& cslot = m_hierarchySlots[itc->second];
            if (cslot.label && cslot.label->GetLabel() == cslot.placeholder)
                commanderPresent = false;
        }
    }

    const auto items = GetRosterPickItems();
    wxArrayString choices;
    choices.Add("<none>");
    for (const auto& it : items)
        choices.Add(it.label);

    int sel = 0;
    if (slot.unit_uid != 0)
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].uid == slot.unit_uid)
            {
                sel = (int)i + 1; // +1 due to <none>
                break;
            }
        }
    }

    wxSingleChoiceDialog dlg(
        this,
        "Choose a unit to assign under this commander:",
        "Assign Unit",
        choices);
    dlg.SetSelection(sel);

    if (dlg.ShowModal() != wxID_OK)
        return;

    const wxString picked = dlg.GetStringSelection();
    if (picked == "<none>")
    {
        ClearHierarchySlot(unitSlotId);
        return;
    }

    if (!commanderPresent)
    {
        wxMessageBox(
            "Assign a commander first, then choose a unit for this commander.\n\n"
            "(You can still remove an already assigned unit via <none>.)",
            "Hierarchy",
            wxOK | wxICON_INFORMATION,
            this);
        return;
    }

    // Resolve picked item -> uid
    uint32_t uid = 0;
    wxString display;
    for (const auto& it : items)
    {
        if (it.label == picked)
        {
            uid = it.uid;
            display = it.display;
            break;
        }
    }
    if (uid == 0 || display.empty())
        return;

    // Enforce uniqueness by UID across hierarchy (but do NOT remove from roster).
    for (const auto& other : m_hierarchySlots)
    {
        if (other.id == unitSlotId)
            continue;
        if (other.type != "unit")
            continue;
        if (other.unit_uid != 0 && other.unit_uid == uid)
        {
            wxMessageBox(
                "This exact unit instance is already assigned elsewhere in the hierarchy.\n\n"
                "Pick a different unit (note the [#id] in the list).",
                "Hierarchy",
                wxOK | wxICON_INFORMATION,
                this);
            return;
        }
    }

    // Set this slot (store uid, show only display name)
    slot.unit_uid = uid;
    slot.unit_display = display;
    slot.label->SetLabel(display);
    slot.label->GetParent()->Layout();

    // Optional convenience: if a commander exists above, set this as their assigned unit immediately.
    TryAssignCommanderToUnitSlot(unitSlotId);
}

void StrategicLevelFrame::ChooseAssignedUnitForCommanderAssignmentSlot(const std::string& assignmentSlotId)
{
    auto it = m_hierarchySlotIndex.find(assignmentSlotId);
    if (it == m_hierarchySlotIndex.end())
        return;

    HierarchySlot& aslot = m_hierarchySlots[it->second];
    if (aslot.type != "unit")
        return;

    auto endsWith = [](const std::string& s, const std::string& suf) {
        return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
        };

    const bool isBattalionAssign = endsWith(assignmentSlotId, "_commander_unit");
    const bool isRegimentAssign = (assignmentSlotId.rfind("regiment_", 0) == 0) && endsWith(assignmentSlotId, "_unit");
    const bool isBrigadeAssign = (assignmentSlotId.rfind("brigade_", 0) == 0) && endsWith(assignmentSlotId, "_unit");

    // Identify owning commander slot
    const std::string commanderId = GetCommanderSlotForUnitSlot(assignmentSlotId);
    auto itc = m_hierarchySlotIndex.find(commanderId);
    if (itc == m_hierarchySlotIndex.end())
        return;
    HierarchySlot& cslot = m_hierarchySlots[itc->second];
    if (cslot.type != "commander" || !cslot.label)
        return;

    if (cslot.label->GetLabel() == cslot.placeholder)
    {
        wxMessageBox(
            "Assign a commander first, then choose which of their units they are part of.",
            "Hierarchy",
            wxOK | wxICON_INFORMATION,
            this);
        return;
    }

    // Gather candidate unit slots from the commander's subtree
    std::vector<std::string> candidateSlotIds;
    candidateSlotIds.reserve(16);

    auto parseNumberBetween = [](const std::string& s, const std::string& pre, const std::string& suf, int& out) {
        if (s.rfind(pre, 0) != 0)
            return false;
        const size_t p = s.find(suf, pre.size());
        if (p == std::string::npos)
            return false;
        const std::string num = s.substr(pre.size(), p - pre.size());
        if (num.empty())
            return false;
        try { out = std::stoi(num); return true; }
        catch (...) { return false; }
        };

    if (isBattalionAssign)
    {
        int bIndex = 0;
        if (parseNumberBetween(assignmentSlotId, "battalion_", "_commander_unit", bIndex))
        {
            for (int u = 1; u <= 4; ++u)
                candidateSlotIds.push_back("battalion_" + std::to_string(bIndex) + "_unit_" + std::to_string(u));
        }
    }
    else if (isRegimentAssign)
    {
        int rIndex = 0;
        if (parseNumberBetween(assignmentSlotId, "regiment_", "_unit", rIndex))
        {
            const int brigadeIndex = (rIndex - 1) / 2 + 1;
            const int rLocal = (rIndex - 1) % 2; // 0/1 within brigade
            const int battalionBase = (brigadeIndex - 1) * 4;
            const int b0 = battalionBase + rLocal * 2 + 1;
            const int b1 = battalionBase + rLocal * 2 + 2;
            for (int u = 1; u <= 4; ++u)
            {
                candidateSlotIds.push_back("battalion_" + std::to_string(b0) + "_unit_" + std::to_string(u));
                candidateSlotIds.push_back("battalion_" + std::to_string(b1) + "_unit_" + std::to_string(u));
            }
        }
    }
    else if (isBrigadeAssign)
    {
        int brigIndex = 0;
        if (parseNumberBetween(assignmentSlotId, "brigade_", "_unit", brigIndex))
        {
            const int battalionBase = (brigIndex - 1) * 4;
            for (int b = 1; b <= 4; ++b)
            {
                const int bIndex = battalionBase + b;
                for (int u = 1; u <= 4; ++u)
                    candidateSlotIds.push_back("battalion_" + std::to_string(bIndex) + "_unit_" + std::to_string(u));
            }
        }
    }

    struct Choice { uint32_t uid; wxString display; wxString label; };
    std::vector<Choice> choices;
    choices.reserve(candidateSlotIds.size());

    for (const auto& sid : candidateSlotIds)
    {
        auto itu = m_hierarchySlotIndex.find(sid);
        if (itu == m_hierarchySlotIndex.end())
            continue;
        const HierarchySlot& us = m_hierarchySlots[itu->second];
        if (us.type != "unit" || us.unit_uid == 0 || us.unit_display.empty())
            continue;
        Choice c;
        c.uid = us.unit_uid;
        c.display = us.unit_display;
        c.label = wxString::Format("%s  [#%u]", us.unit_display, (unsigned)us.unit_uid);
        choices.push_back(c);
    }

    wxArrayString pick;
    pick.Add("<none>");
    for (const auto& c : choices)
        pick.Add(c.label);

    int sel = 0;
    const uint32_t current = (cslot.assigned_unit_uid != 0) ? cslot.assigned_unit_uid : aslot.unit_uid;
    if (current != 0)
    {
        for (size_t i = 0; i < choices.size(); ++i)
        {
            if (choices[i].uid == current)
            {
                sel = (int)i + 1;
                break;
            }
        }
    }

    wxSingleChoiceDialog dlg(
        this,
        "Choose which unit this commander is part of (must be one of the units directly under them):",
        "Assign Commander",
        pick);
    dlg.SetSelection(sel);
    if (dlg.ShowModal() != wxID_OK)
        return;

    const wxString picked = dlg.GetStringSelection();
    if (picked == "<none>")
    {
        // Clear assignment only
        aslot.unit_uid = 0;
        aslot.unit_display.clear();
        aslot.label->SetLabel(aslot.placeholder);
        cslot.assigned_unit_uid = 0;
        cslot.assigned_unit_display.clear();
        UpdateCommanderHierarchyLabel(commanderId);
        aslot.label->GetParent()->Layout();
        return;
    }

    uint32_t uid = 0;
    wxString display;
    for (const auto& c : choices)
    {
        if (c.label == picked)
        {
            uid = c.uid;
            display = c.display;
            break;
        }
    }
    if (uid == 0 || display.empty())
        return;

    // Set assignment slot + commander display
    aslot.unit_uid = uid;
    aslot.unit_display = display;
    aslot.label->SetLabel(display);
    aslot.label->GetParent()->Layout();

    cslot.assigned_unit_uid = uid;
    cslot.assigned_unit_display = display;
    UpdateCommanderHierarchyLabel(commanderId);
}


void StrategicLevelFrame::TryAssignCommanderToUnitSlot(const std::string& unitSlotId)
{
    auto it = m_hierarchySlotIndex.find(unitSlotId);
    if (it == m_hierarchySlotIndex.end())
        return;

    HierarchySlot& us = m_hierarchySlots[it->second];
    if (us.type != "unit" || us.unit_uid == 0)
        return;

    const std::string commanderId = GetCommanderSlotForUnitSlot(unitSlotId);
    if (commanderId.empty())
        return;

    auto itc = m_hierarchySlotIndex.find(commanderId);
    if (itc == m_hierarchySlotIndex.end())
        return;

    HierarchySlot& cs = m_hierarchySlots[itc->second];
    if (cs.type != "commander" || !cs.label)
        return;

    if (cs.label->GetLabel() == cs.placeholder)
    {
        wxMessageBox(
            "Assign a commander first, then choose which unit under them is the assigned unit.",
            "Hierarchy",
            wxOK | wxICON_INFORMATION,
            this);
        return;
    }

    // Commander is assigned to ONE of its units (4/8/16 under them). This does NOT move/remove the unit anywhere.
    cs.assigned_unit_uid = us.unit_uid;
    cs.assigned_unit_display = us.unit_display;
    UpdateCommanderHierarchyLabel(commanderId);
}

void StrategicLevelFrame::UpdateCommanderHierarchyLabel(const std::string& commanderSlotId)
{
    auto it = m_hierarchySlotIndex.find(commanderSlotId);
    if (it == m_hierarchySlotIndex.end())
        return;

    HierarchySlot& cs = m_hierarchySlots[it->second];
    if (cs.type != "commander" || !cs.label)
        return;

    if (cs.commander_name.empty())
    {
        // Best effort fallback: try to parse label (may already include rank)
        cs.commander_name = cs.label->GetLabel().ToStdString();
    }

    wxString base = wxString::Format("%s %s", GetRankAbbrev(cs.rank), wxString::FromUTF8(cs.commander_name));
    if (cs.assigned_unit_uid != 0 && !cs.assigned_unit_display.empty())
        base += wxString::Format(" → %s", cs.assigned_unit_display);

    cs.label->SetLabel(base);
    cs.label->GetParent()->Layout();
}

void StrategicLevelFrame::OnHierarchyTogglePage(wxCommandEvent&)
{
    if (!m_hierarchyBook || !m_btnHierarchyPageToggle)
        return;

    size_t current = m_hierarchyBook->GetSelection();
    size_t next = current == 0 ? 1 : 0;
    m_hierarchyBook->SetSelection(next);
    m_btnHierarchyPageToggle->SetLabel(next == 0 ? "Go to Page 2" : "Back to Page 1");
}

void StrategicLevelFrame::OnRosterBeginDrag(wxListEvent& event)
{
    const long item = event.GetIndex();
    if (item < 0)
        return;
    wxString name = m_roster->GetItemText(item);
    if (name.empty())
        return;
    wxTextDataObject dataObject("unit:" + name);
    wxDropSource dropSource(dataObject, m_roster);
    dropSource.DoDragDrop(wxDrag_CopyOnly);
}

void StrategicLevelFrame::OnCommanderBeginDrag(wxListEvent& event)
{
    const long item = event.GetIndex();
    if (item < 0)
        return;

    wxString name = m_cmdRoster->GetItemText(item);
    if (name.empty())
        return;

    const uint32_t uid = (uint32_t)m_cmdRoster->GetItemData(item);
    int rank = 0;
    auto it = m_commanderRankByUid.find(uid);
    if (it != m_commanderRankByUid.end())
        rank = it->second;
    wxTextDataObject dataObject(wxString::Format("commander:%u:%d:%s", (unsigned)uid, rank, name));
    wxDropSource dropSource(dataObject, m_cmdRoster);
    dropSource.DoDragDrop(wxDrag_CopyOnly);
}

void StrategicLevelFrame::RefreshUI()
{
    if (m_lblMoneyValue)
        m_lblMoneyValue->SetLabel(wxString::Format("%d", m_money));
    if (m_lblResearchValue)
        m_lblResearchValue->SetLabel(wxString::Format("%d", m_research));
    if (m_lblTurnValue)
        m_lblTurnValue->SetLabel(wxString::Format("%d", m_turn));
    // Buy/Sell sidebar status labels (separate widgets)
    if (m_buyLblMoneyValue)
        m_buyLblMoneyValue->SetLabel(wxString::Format("%d", m_money));
    if (m_buyLblResearchValue)
        m_buyLblResearchValue->SetLabel(wxString::Format("%d", m_research));
    if (m_buyLblTurnValue)
        m_buyLblTurnValue->SetLabel(wxString::Format("%d", m_turn));


    // Commanders list
    if (m_cmdRoster)
    {
        while (m_cmdRoster->GetColumnCount() > 0)
            m_cmdRoster->DeleteColumn(0);
        m_cmdRoster->InsertColumn(0, "Commander");
        m_cmdRoster->InsertColumn(1, "Rank");

        m_cmdRoster->DeleteAllItems();

        // Ensure commander UIDs exist and rebuild uid->rank helper map
        m_commanderRankByUid.clear();

        long crow = 0;
        for (auto& c : m_playerCommanders)
        {
            if (crow >= 14) break;
            if (c.uid == 0)
                c.uid = m_nextCommanderUid++;

            long cidx = m_cmdRoster->InsertItem(crow++, wxString::FromUTF8(c.name));
            m_cmdRoster->SetItem(cidx, 1, GetRankAbbrev(c.rank));
            m_cmdRoster->SetItemData(cidx, (long)c.uid);
            m_commanderRankByUid[c.uid] = c.rank;
        }

        int cW = 0, cH = 0;
        m_cmdRoster->GetClientSize(&cW, &cH);
        const int rankW = 70;
        const int nameW = std::max(90, cW - rankW - 4);
        m_cmdRoster->SetColumnWidth(1, rankW);
        m_cmdRoster->SetColumnWidth(0, nameW);
    }

    // Reset sloupců: vynutit přesně 2 sloupce (Unit, HP)
        // Smaž existující sloupce bez ohledu na stav
    while (m_roster->GetColumnCount() > 0)
        m_roster->DeleteColumn(0);

    m_roster->InsertColumn(0, "Unit");
    m_roster->InsertColumn(1, "HP");

    // v RefreshUI(): rozbalení jednotek podle count a odstranění sloupce Count
    m_roster->DeleteAllItems();

    // Expand units into roster rows. Each row gets a stable-ish UID so hierarchy
    // can reference a specific instance even if there are duplicates by name.
    int totalRows = 0;
    for (const auto& u : m_playerUnits)
        totalRows += std::max(0, u.count);

    if ((int)m_rosterRowUids.size() != totalRows)
    {
        m_rosterRowUids.clear();
        m_rosterRowUids.reserve((size_t)totalRows);
        for (int i = 0; i < totalRows; ++i)
            m_rosterRowUids.push_back(m_nextRosterUid++);
    }

    long row = 0;
    int uidIndex = 0;
    for (const auto& u : m_playerUnits)
    {
        for (int i = 0; i < u.count; ++i)
        {
            const uint32_t uid = (uidIndex < (int)m_rosterRowUids.size()) ? m_rosterRowUids[(size_t)uidIndex++] : (uint32_t)m_nextRosterUid++;
            long idx = m_roster->InsertItem(row++, GetUnitDisplayName(u.unit_id));
            m_roster->SetItem(idx, 1, wxString::Format("%d", u.health));
            // Store UID in item data for picking.
            m_roster->SetItemData(idx, (long)uid);
        }
    }

    //    // Automatická šířka sloupců (jen 2 sloupce)
    //    m_roster->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);
    //    m_roster->SetColumnWidth(1, wxLIST_AUTOSIZE_USEHEADER);
    //
    //    m_btnLaunch->Enable(m_selectedTerritory >= 0);
    //    if(m_btnSell)
    //        m_btnSell->Enable(!m_playerUnits.empty());
    //}

        // Pevná šířka pro HP, zbytek pro název jednotky
    int clientW = 0, clientH = 0;
    m_roster->GetClientSize(&clientW, &clientH);

    const int hpWidth = 70;                // upravte dle potřeby (např. 70–100)
    const int unitWidth = std::max(100, clientW - hpWidth - 4); // rezerva na okraj/scrollbar

    m_roster->SetColumnWidth(1, hpWidth);
    m_roster->SetColumnWidth(0, unitWidth);

    // Launch mission is only available on the Strategic map page (left book page 0)
    // and never while Buy/Sell overlay is active.
    const bool onStrategicMap = (m_leftBook && m_leftBook->GetSelection() == 0 && !m_buyModeActive);
    m_btnLaunch->Enable(onStrategicMap && m_selectedTerritory >= 0);
    if (m_btnBuyShop)
        m_btnBuyShop->Enable(true);

    if (m_researchMode)
        RefreshResearchUI();
}




// Territory id 0 is reserved for global resources settings (meta).
static const int kResourcesMetaTerritoryId = 0;

static int ClampGlobalResearch(int r) { return std::clamp(r, 0, 5); }

// ============================================================
// Resources page + mechanics
// ============================================================

void StrategicLevelFrame::BuildResourcesPage()
{
    if (!m_resourcesPanel)
        return;

    m_resourcesPanel->SetMinSize(wxSize(1, 1));
    auto* s = new wxBoxSizer(wxVERTICAL);

    // ── Map canvas (same paint handler as strategic map) ──
    m_resourcesCanvas = new wxPanel(m_resourcesPanel);
    m_resourcesCanvas->SetBackgroundColour(m_palette.background);
    m_resourcesCanvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_resourcesCanvas->SetMinSize(wxSize(1, 1));
    m_resourcesCanvas->Bind(wxEVT_PAINT,     &StrategicLevelFrame::OnMapPaint,     this);
    m_resourcesCanvas->Bind(wxEVT_LEFT_DOWN, &StrategicLevelFrame::OnMapLeftDown,  this);
    m_resourcesCanvas->Bind(wxEVT_MOTION,    &StrategicLevelFrame::OnMapMouseMove, this);
    // When canvas first gets a real size, mark overlay dirty and repaint
    m_resourcesCanvas->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev)
    {
        ev.Skip();
        m_overlayDirty = true;
        if (m_resourcesCanvas) m_resourcesCanvas->Refresh();
    });
    s->Add(m_resourcesCanvas, 3, wxALL | wxEXPAND, 8);

    // ── Bottom controls ──
    auto* under = new wxPanel(m_resourcesPanel);
    under->SetBackgroundColour(m_palette.background);
    auto* us = new wxBoxSizer(wxVERTICAL);

    // Header label (shows selected territory or global summary)
    m_resourcesSelectedLabel = new wxStaticText(under, wxID_ANY, "Resources");
    m_resourcesSelectedLabel->SetFont(m_fontHeading);
    m_resourcesSelectedLabel->SetForegroundColour(m_palette.heading);
    us->Add(m_resourcesSelectedLabel, 0, wxLEFT | wxRIGHT | wxTOP, 8);

    // Global allocation row
    auto* allocRow = new wxBoxSizer(wxHORIZONTAL);

    auto* allocCaption = new wxStaticText(under, wxID_ANY, "Research allocation:");
    allocCaption->SetFont(m_fontText);
    allocCaption->SetForegroundColour(m_palette.text);
    allocRow->Add(allocCaption, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_resourcesSlider = new wxSlider(under, wxID_ANY, 0, 0, 5,
        wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
    m_resourcesSlider->SetMinSize(wxSize(-1, 40));
    allocRow->Add(m_resourcesSlider, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND);

    m_resourcesRatioLabel = new wxStaticText(under, wxID_ANY, "Money: 20  Research: 0");
    m_resourcesRatioLabel->SetFont(m_fontText);
    m_resourcesRatioLabel->SetForegroundColour(m_palette.statusHeading);
    allocRow->Add(m_resourcesRatioLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    us->Add(allocRow, 0, wxALL | wxEXPAND, 8);

    // ── Per-territory resource table ──
    m_resourcesTable = new wxListCtrl(under, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_NO_SORT_HEADER);
    m_resourcesTable->SetFont(m_fontText);
    m_resourcesTable->SetBackgroundColour(m_palette.background);
    m_resourcesTable->SetForegroundColour(m_palette.text);
    m_resourcesTable->SetMinSize(wxSize(1, 1));
    m_resourcesTable->InsertColumn(0, "Territory",  wxLIST_FORMAT_LEFT,   -1);
    m_resourcesTable->InsertColumn(1, "Resources",  wxLIST_FORMAT_CENTER, -1);
    m_resourcesTable->InsertColumn(2, "Money/turn", wxLIST_FORMAT_CENTER, -1);
    m_resourcesTable->InsertColumn(3, "Res./turn",  wxLIST_FORMAT_CENTER, -1);
    // Stretch columns after first layout
    m_resourcesTable->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev)
    {
        ev.Skip();
        if (!m_resourcesTable) return;
        const int w = m_resourcesTable->GetClientSize().GetWidth();
        if (w <= 0) return;
        m_resourcesTable->SetColumnWidth(0, w * 25 / 100);
        m_resourcesTable->SetColumnWidth(1, w * 30 / 100);
        m_resourcesTable->SetColumnWidth(2, w * 25 / 100);
        m_resourcesTable->SetColumnWidth(3, w * 20 / 100);
    });
    // Click in table also selects territory
    m_resourcesTable->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& ev)
    {
        const long row = ev.GetIndex();
        if (!m_resourcesTable || row < 0) return;
        const long tid = m_resourcesTable->GetItemData(row);
        if (tid <= 0) return;
        m_selectedTerritory = (int)tid;
        RefreshResourcesPage();
    });
    us->Add(m_resourcesTable, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    m_resourcesSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&)
    {
        if (!m_resourcesSlider) return;
        m_resourcesGlobalResearch = ClampGlobalResearch(m_resourcesSlider->GetValue());
        m_territoryResources[kResourcesMetaTerritoryId].researchCarry = m_resourcesGlobalResearch;
        SaveStrategicState();
        RefreshResourcesPage();
    });

    under->SetSizer(us);
    s->Add(under, 2, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    m_resourcesPanel->SetSizer(s);
}


void StrategicLevelFrame::RefreshResourcesPage()
{
    // Ensure all territories have a state entry
    for (const auto& t : m_level.territories)
    {
        if (m_territoryResources.find(t.id) == m_territoryResources.end())
            m_territoryResources[t.id] = TerritoryResourceState{};
    }

    // Load global allocation from meta entry
    auto itMeta = m_territoryResources.find(kResourcesMetaTerritoryId);
    if (itMeta != m_territoryResources.end())
        m_resourcesGlobalResearch = ClampGlobalResearch(itMeta->second.researchCarry);
    else
        m_territoryResources[kResourcesMetaTerritoryId].researchCarry = m_resourcesGlobalResearch;

    const int R = ClampGlobalResearch(m_resourcesGlobalResearch);
    const int M = 20 - 4 * R;

    // ── Header label ──
    if (m_resourcesSelectedLabel)
    {
        const bool ownedSel = (m_selectedTerritory > 0 &&
            std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(),
                      m_selectedTerritory) != m_ownedTerritories.end());
        if (ownedSel)
        {
            const auto& st = m_territoryResources[m_selectedTerritory];
            m_resourcesSelectedLabel->SetLabel(
                wxString::Format("Territory T%02d  -  Resources: %d / %d",
                    m_selectedTerritory, st.remaining, st.total));
        }
        else
        {
            m_resourcesSelectedLabel->SetLabel(
                wxString::Format("Owned territories: %d", (int)m_ownedTerritories.size()));
        }
    }

    // ── Slider + ratio label ──
    if (m_resourcesSlider)
        m_resourcesSlider->SetValue(R);
    if (m_resourcesRatioLabel)
        m_resourcesRatioLabel->SetLabel(
            wxString::Format("Money: %d  Research: %d", M, R));

    // ── Per-territory table ──
    if (m_resourcesTable)
    {
        m_resourcesTable->Freeze();
        m_resourcesTable->DeleteAllItems();

        const wxColour clrOwned   = m_palette.text;
        const wxColour clrDepleted(0x88, 0x44, 0x44);
        const wxColour clrSelected = m_palette.heading;

        for (int tid : m_ownedTerritories)
        {
            if (tid <= 0) continue;
            const auto& st = m_territoryResources.count(tid)
                             ? m_territoryResources.at(tid) : TerritoryResourceState{};

            const wxString resStr  = wxString::Format("%d / %d", st.remaining, st.total);
            const wxString monStr  = wxString::Format("%.1f", (double)M / 20.0);
            const wxString resRStr = (R > 0)
                ? wxString::Format("1/%d", 20 / R)
                : wxString("-");

            long row = m_resourcesTable->InsertItem(
                m_resourcesTable->GetItemCount(), wxString::Format("T%02d", tid));
            m_resourcesTable->SetItem(row, 1, resStr);
            m_resourcesTable->SetItem(row, 2, monStr);
            m_resourcesTable->SetItem(row, 3, resRStr);
            m_resourcesTable->SetItemData(row, (long)tid);  // store tid for click handler

            const bool depleted = (st.remaining <= 0);
            const bool selected = (tid == m_selectedTerritory);
            m_resourcesTable->SetItemTextColour(row,
                selected ? clrSelected : (depleted ? clrDepleted : clrOwned));
        }
        m_resourcesTable->Thaw();
    }

    // ── Map overlay + canvas refresh ──
    // Always rebuild overlay - selected territory highlight may have changed
    MarkOverlayDirty();
    if (m_resourcesCanvas)
        m_resourcesCanvas->Refresh();
}

void StrategicLevelFrame::ApplyResourceTickEndTurn()
{
    // One resource per owned territory per turn, until exhausted.
    for (int tid : m_ownedTerritories)
    {
        if (tid <= 0)
            continue;

        auto& st = m_territoryResources[tid];
        if (st.total <= 0) st.total = 20;
        if (st.remaining <= 0)
            continue;

        // spend 1 resource
        st.remaining -= 1;
        if (st.remaining < 0) st.remaining = 0;

        // route this tick either to money or to research using a deterministic 20-step distribution:
        // Research points per territory: R (0..5) => 4*R ticks go to research, the rest to money.
        const int R = ClampGlobalResearch(m_resourcesGlobalResearch);
        const int researchTicksPer20 = 4 * R; // 0..20
        st.allocAccum += researchTicksPer20;
        bool toResearch = false;
        if (st.allocAccum >= 20)
        {
            toResearch = true;
            st.allocAccum -= 20;
        }

        if (toResearch)
        {
            st.researchCarry += 1;
            if (st.researchCarry >= 4)
            {
                m_research += 1;
                st.researchCarry -= 4;
            }
        }
        else
        {
            m_money += 1;
        }
    }

    // If resources page is open, update it.
    RefreshResourcesPage();
}

// ============================================================
// Research system (Strategic level)
// ============================================================

// CP895 (Kamenický / Czech DOS encoding) → Unicode codepoint table.
// Verified against test data from RESEARCH.CZ and sample BRF/INF files.
static const uint16_t kCp895ToUnicode[256] = {
    // 0x00-0x7F: identical to ASCII
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
    // 0x80-0xFF: CP895 Kamenický Czech/Slovak characters + box drawing + symbols
    0x010C, 0x00FC, 0x00E9, 0x010F, 0x00E4, 0x010E, 0x0164, 0x010D,  // 80-87: Č ü é ď ä Ď Ť č
    0x011B, 0x011A, 0x0139, 0x00CD, 0x00EE, 0x013D, 0x00C4, 0x00C1,  // 88-8F: ě Ě Ĺ Í î Ľ Ä Á
    0x00C9, 0x017E, 0x017D, 0x00F4, 0x00F6, 0x00D3, 0x016F, 0x00DA,  // 90-97: É ž Ž ô ö Ó ů Ú
    0x00FD, 0x00D6, 0x00DC, 0x0160, 0x013A, 0x0165, 0x0159, 0x0158,  // 98-9F: ý Ö Ü Š ĺ ť ř Ř
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x0148, 0x0147, 0x016E, 0x00D4,  // A0-A7: á í ó ú ň Ň Ů Ô
    0x0161, 0x0159, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,  // A8-AF: š ř ¬ ½ ¼ ¡ « »
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,  // B0-B7: box drawing
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,  // B8-BF
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,  // C0-C7
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,  // C8-CF
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,  // D0-D7
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,  // D8-DF
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,  // E0-E7: α ß Γ π Σ σ μ τ
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,  // E8-EF
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,  // F0-F7
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,  // F8-FF
};

static wxString DecodeCp895Text(const std::string& bytes)
{
    wxString ws;
    ws.reserve(bytes.size());
    for (unsigned char b : bytes)
        ws += static_cast<wxChar>(kCp895ToUnicode[b]);

    ws.Replace("\r\n", "\n");
    ws.Replace("\r", "\n");

    // Strip trailing ~ sentinel used in some INF/BRF files
    int tilde = ws.Find('~');
    if (tilde != wxNOT_FOUND)
        ws = ws.Left(tilde);

    ws.Trim(true).Trim(false);
    return ws;
}

// Keep old name as alias for any remaining call sites
static wxString DecodeCp852Text(const std::string& bytes)
{
    return DecodeCp895Text(bytes);
}

void StrategicLevelFrame::EnsureResearchLoaded()
{
    if (!m_researchDb.empty())
        return;

    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path base = GetStableBaseDir();
    const fs::path researchDir = base / "temp" / "RESEARCH";
    const fs::path commonDir   = base / "temp" / "COMMON";

    auto loadFileBin = [&](const fs::path& p) -> std::string
    {
        std::ifstream f(p, std::ios::binary);
        if (!f) return {};
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };

    // ----------------------------------------------------------------
    // 1.  Parse RESEARCH.DEF → group, level, cost (Time), flags, prereqs per item
    // ----------------------------------------------------------------
    struct DefRec
    {
        wxString group;
        int level = 0;
        int time  = 0;
        wxString flags;
        std::vector<int> prereqs;
    };
    std::unordered_map<int, DefRec> defById;

    {
        const std::string raw = loadFileBin(commonDir / "RESEARCH.DEF");
        if (!raw.empty())
        {
            DefRec cur;
            int curId = -1;
            bool inside = false;
            std::istringstream ss(raw);
            std::string line;
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                size_t s = line.find_first_not_of(" \t");
                if (s == std::string::npos) continue;
                line = line.substr(s);
                if (line[0] == ';') continue;

                // Item(N) {
                static const std::regex rxItem(R"(Item\((\d+)\)\s*\{)");
                std::smatch m;
                if (std::regex_search(line, m, rxItem))
                {
                    cur = DefRec{};
                    curId = std::stoi(m[1].str());
                    inside = true;
                    continue;
                }
                if (!inside) continue;
                if (line == "}") {
                    if (curId >= 0) defById[curId] = std::move(cur);
                    inside = false; curId = -1;
                    continue;
                }

                static const std::regex rxGroup(R"(Group\((\w+)\))");
                static const std::regex rxFlags(R"(Flags\((\w+)\))");
                static const std::regex rxLevel(R"(Level\((\d+)\))");
                static const std::regex rxTime (R"(Time\((\d+)\))");
                static const std::regex rxOr   (R"(ORconnections\(([^)]+)\))");

                if (std::regex_search(line, m, rxGroup)) cur.group = wxString::FromUTF8(m[1].str());
                if (std::regex_search(line, m, rxFlags)) cur.flags = wxString::FromUTF8(m[1].str());
                if (std::regex_search(line, m, rxLevel)) cur.level = std::stoi(m[1].str());
                if (std::regex_search(line, m, rxTime))  cur.time  = std::stoi(m[1].str());
                if (std::regex_search(line, m, rxOr))
                {
                    std::istringstream argss(m[1].str());
                    std::string tok;
                    while (std::getline(argss, tok, ','))
                    {
                        size_t ts = tok.find_first_not_of(" \t");
                        if (ts != std::string::npos)
                            cur.prereqs.push_back(std::stoi(tok.substr(ts)));
                    }
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // 2.  Title list from RESEARCH.CZ / fallback .ENG
    //     Line N (0-based) = title for Item(N)
    // ----------------------------------------------------------------
    std::vector<wxString> titleByIndex;
    {
        std::string raw;
        for (const char* name : {"RESEARCH.CZ", "RESEARCH.ENG"})
        {
            raw = loadFileBin(commonDir / name);
            if (!raw.empty()) break;
        }
        if (!raw.empty())
        {
            std::istringstream ss(raw);
            std::string line;
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                // Strip trailing control chars (0x1A etc.)
                while (!line.empty() && (unsigned char)line.back() < 0x20)
                    line.pop_back();
                titleByIndex.push_back(DecodeCp895Text(line));
            }
        }
    }

    // ----------------------------------------------------------------
    // 3.  BRF / INF texts from temp/RESEARCH/
    // ----------------------------------------------------------------
    struct TextRec { wxString inf, brf; };
    std::unordered_map<int, TextRec> textById;

    if (fs::exists(researchDir, ec) && fs::is_directory(researchDir, ec))
    {
        static const std::regex rxFile(R"(^(R(\d{3})|RACES)\.(INF|BRF)$)",
                                       std::regex_constants::icase);
        for (auto it = fs::directory_iterator(researchDir, ec);
             it != fs::directory_iterator(); ++it)
        {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string fn = it->path().filename().string();
            std::smatch m;
            if (!std::regex_match(fn, m, rxFile)) continue;

            std::string idStr = m[2].matched ? m[2].str() : "";
            std::string ext   = m[3].str();
            for (auto& ch : ext) ch = (char)std::toupper((unsigned char)ch);

            if (idStr.empty()) continue; // skip RACES.* for now

            const std::string raw = loadFileBin(it->path());
            if (raw.empty()) continue;
            wxString decoded = DecodeCp895Text(raw);

            int id = std::atoi(idStr.c_str());
            if (ext == "INF") textById[id].inf = decoded;
            else               textById[id].brf = decoded;
        }
    }

    // ----------------------------------------------------------------
    // 4.  Build m_researchDb
    // ----------------------------------------------------------------
    m_researchDb.clear();

    // All IDs from DEF (source of truth) + any with text but missing from DEF
    std::unordered_set<int> allIds;
    for (auto& kv : defById)  allIds.insert(kv.first);
    for (auto& kv : textById) allIds.insert(kv.first);

    for (int id : allIds)
    {
        const DefRec*  def  = defById.count(id)  ? &defById[id]  : nullptr;
        const TextRec* text = textById.count(id)  ? &textById[id] : nullptr;

        // Special items are internal game triggers, not player-researchable
        if (def && def->flags == "Special")
            continue;

        // Items with Time(0) are already available by default (base unit types etc.)
        // – they do not appear in the research screen.
        if (def && def->time == 0)
            continue;

        ResearchItem item;
        item.id    = id;
        item.code  = wxString::Format("R%03d", id);
        item.title = (id >= 0 && id < (int)titleByIndex.size() && !titleByIndex[id].empty())
                     ? titleByIndex[id] : item.code;

        if (text) { item.brief = text->brf; item.info = text->inf; }

        if (def)
        {
            item.group        = def->group;
            item.level        = def->level;
            item.cost         = std::max(1, def->time);
            item.flags        = def->flags;
            item.prerequisites = def->prereqs;
        }
        else
        {
            item.cost = 20;
        }

        m_researchDb.push_back(std::move(item));
    }

    // Sort: group order → level → id
    auto groupOrder = [](const wxString& g) -> int {
        if (g == "Global")       return 0;
        if (g == "Technologies") return 1;
        if (g == "Upgrades")     return 2;
        if (g == "Races")        return 3;
        return 4;
    };
    std::sort(m_researchDb.begin(), m_researchDb.end(),
        [&](const ResearchItem& a, const ResearchItem& b)
        {
            int ga = groupOrder(a.group), gb = groupOrder(b.group);
            if (ga != gb) return ga < gb;
            if (a.level != b.level) return a.level < b.level;
            return a.id < b.id;
        });

    // Preselect first item if nothing active yet
    if (!m_researchDb.empty() && m_researchActiveIndex < 0)
    {
        m_researchActiveIndex = 0;
        m_researchActiveId = m_researchDb[0].id;
    }
    if (!m_researchDb.empty() && m_researchBrowseIndex < 0)
        m_researchBrowseIndex = 0;
}

void StrategicLevelFrame::EnterResearchMode()
{
    EnsureResearchLoaded();
    m_researchMode = true;

    // Switch pages: left details + middle list
    if (m_leftBook) m_leftBook->SetSelection(4);
    if (m_midBook)  m_midBook->SetSelection(1);

    // Update button label
    if (m_btnResearch) m_btnResearch->SetLabel("Back");

    RefreshResearchUI();
}

void StrategicLevelFrame::LeaveResearchMode()
{
    m_researchMode = false;

    if (m_leftBook) m_leftBook->SetSelection(0);
    if (m_midBook)  m_midBook->SetSelection(0);

    if (m_btnResearch) m_btnResearch->SetLabel("Research");

    RefreshUI();
}

void StrategicLevelFrame::SelectResearchIndex(int idx)
{
    if (idx < 0 || idx >= (int)m_researchDb.size())
        return;

    // Clicking in the list only updates the browse selection (bottom info box).
    // The active research item (top box) only changes when Start is pressed.
    m_researchBrowseIndex = idx;

    RefreshResearchUI();
}

void StrategicLevelFrame::OnResearchList(wxCommandEvent& ev)
{
    // Selection handling is done via wxEVT_LIST_ITEM_SELECTED lambda bound in BuildUI.
    // This stub remains for EVT_TABLE compatibility if needed.
    (void)ev;
}

void StrategicLevelFrame::OnResearchAlloc(wxCommandEvent&)
{
    // Allocation slider removed – research uses all available points automatically.
}

void StrategicLevelFrame::OnResearchStartStop(wxCommandEvent&)
{
    if (m_researchAllocPerTurn > 0)
    {
        // Stop
        m_researchAllocPerTurn = 0;
    }
    else
    {
        // Start – commit browsed item as the active research target
        if (m_researchBrowseIndex >= 0 && m_researchBrowseIndex < (int)m_researchDb.size())
        {
            m_researchActiveIndex = m_researchBrowseIndex;
            m_researchActiveId    = m_researchDb[m_researchActiveIndex].id;
        }
        if (m_researchActiveIndex >= 0)
            m_researchAllocPerTurn = 1;
    }
    RefreshResearchUI();
    SaveStrategicState();
}

void StrategicLevelFrame::RefreshResearchUI()
{
    if (!m_researchList && !m_researchText && !m_researchActiveText)
        return;

    // Re-entrancy guard: SetItemState fires wxEVT_LIST_ITEM_SELECTED which
    // calls SelectResearchIndex -> RefreshResearchUI -> DeleteAllItems -> crash.
    if (m_researchRefreshing)
        return;
    m_researchRefreshing = true;
    struct RGuard { bool& f; ~RGuard(){ f = false; } } _rg{m_researchRefreshing};

    EnsureResearchLoaded();

    const int campaignLevel = m_gameModeEnabled
        ? std::max(1, (m_turn / 8) + 1) : 999;

    auto isUnlocked = [&](const ResearchItem& it) -> bool {
        if (it.prerequisites.empty()) return true;
        for (int pre : it.prerequisites)
            if (m_researchCompleted.count(pre)) return true;
        return false;
    };

    // ----------------------------------------------------------------
    // Categorized list (wxListCtrl)
    // ItemData: (wxUIntPtr)-1 = group header row (not selectable)
    //           other values  = index into m_researchDb
    // ----------------------------------------------------------------
    static constexpr wxUIntPtr kHdrSentinel = static_cast<wxUIntPtr>(-1);

    if (m_researchList)
    {
        m_researchList->Freeze();
        m_researchList->DeleteAllItems();

        const wxColour clrHeader = m_palette.heading;
        const wxColour clrNormal = m_palette.text;
        const wxColour clrDone  (0x4A, 0x7A, 0x4A);
        const wxColour clrLocked(0x60, 0x60, 0x60);

        wxString lastGroup;
        long row = 0, selRow = -1;

        for (int i = 0; i < (int)m_researchDb.size(); ++i)
        {
            const ResearchItem& it = m_researchDb[i];
            if (m_gameModeEnabled && it.level > campaignLevel)
                continue;

            // Group header
            if (it.group != lastGroup)
            {
                lastGroup = it.group;
                if (!lastGroup.empty())
                {
                    m_researchList->InsertItem(row, lastGroup);
                    m_researchList->SetItemData(row, kHdrSentinel);
                    m_researchList->SetItemTextColour(row, clrHeader);
                    ++row;
                }
            }

            const bool done   = (it.id >= 0 && m_researchCompleted.count(it.id) > 0);
            const bool locked = !isUnlocked(it);
            const bool active = (it.id == m_researchActiveId && m_researchAllocPerTurn > 0);

            wxString label = wxString("  ") + it.title;
            if (done)        label += " [+]";
            else if (locked) label += " [?]";
            else if (active) label += " >";

            m_researchList->InsertItem(row, label);
            m_researchList->SetItemData(row, static_cast<wxUIntPtr>(i));

            if (done)         m_researchList->SetItemTextColour(row, clrDone);
            else if (locked)  m_researchList->SetItemTextColour(row, clrLocked);
            else              m_researchList->SetItemTextColour(row, clrNormal);

            if (i == m_researchBrowseIndex)
                selRow = row;
            ++row;
        }

        // Auto-fit column
        if (row > 0)
        {
            m_researchList->SetColumnWidth(0, wxLIST_AUTOSIZE);
            const int lw = m_researchList->GetClientSize().GetWidth();
            if (m_researchList->GetColumnWidth(0) < lw)
                m_researchList->SetColumnWidth(0, lw);
        }

        // Select active item (guard prevents re-entrant call here)
        if (selRow >= 0)
        {
            m_researchList->SetItemState(selRow,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            m_researchList->EnsureVisible(selRow);
        }
        m_researchList->Thaw();
    }

    // Start/Stop button
    if (m_btnResearchStart)
        m_btnResearchStart->SetLabel(m_researchAllocPerTurn > 0 ? "Stop" : "Start");

    // ----------------------------------------------------------------
    // Top box: BRF of the item currently being researched
    // ----------------------------------------------------------------
    if (m_researchActiveText)
    {
        if (m_researchActiveIndex >= 0
            && m_researchActiveIndex < (int)m_researchDb.size()
            && m_researchAllocPerTurn > 0)
        {
            const ResearchItem& cur = m_researchDb[m_researchActiveIndex];
            m_researchActiveText->SetValue(cur.brief.empty() ? cur.info : cur.brief);
        }
        else
            m_researchActiveText->SetValue(wxEmptyString);
    }

    // ----------------------------------------------------------------
    // Progress gauge
    // ----------------------------------------------------------------
    if (m_researchGauge && m_researchGaugeLabel)
    {
        if (m_researchActiveIndex >= 0
            && m_researchActiveIndex < (int)m_researchDb.size()
            && m_researchAllocPerTurn > 0)
        {
            const ResearchItem& cur = m_researchDb[m_researchActiveIndex];
            const int cost = std::max(1, cur.cost);
            const int prog = m_researchProgressById.count(cur.id)
                             ? m_researchProgressById.at(cur.id) : 0;
            m_researchGauge->SetRange(cost);
            m_researchGauge->SetValue(std::min(prog, cost));
            m_researchGaugeLabel->SetLabel(
                wxString::Format("%d/%d", std::min(prog, cost), cost));
        }
        else
        {
            m_researchGauge->SetRange(100);
            m_researchGauge->SetValue(0);
            m_researchGaugeLabel->SetLabel("0/0");
        }
    }

    // ----------------------------------------------------------------
    // Bottom box: INF of the item selected for browsing
    // ----------------------------------------------------------------
    if (m_researchText)
    {
        const int bi = (m_researchBrowseIndex >= 0) ? m_researchBrowseIndex : m_researchActiveIndex;
        if (bi >= 0 && bi < (int)m_researchDb.size())
        {
            const ResearchItem& cur = m_researchDb[bi];
            wxString txt = cur.info.empty() ? cur.brief : cur.info;
            if (cur.id >= 0 && m_researchCompleted.count(cur.id))
                txt << "\n\n[COMPLETED]";
            else if (!isUnlocked(cur))
                txt << "\n\n[LOCKED - prerequisite required]";
            m_researchText->SetValue(txt);
        }
        else
            m_researchText->SetValue("Select an item from the list.");
    }
}


// ============================================================
// Info / Encyclopedia mode (read-only browsing of discovered items)
// ============================================================

void StrategicLevelFrame::EnterInfoMode()
{
    EnsureResearchLoaded();
    m_infoMode = true;

    // Switch pages: left info panel + middle info list
    // Page indices: 0=map, 1=hierarchy, 2=resources, 3=stats, 4=research, 5=info
    if (m_leftBook) m_leftBook->SetSelection(5);
    // Mid book: 0=roster, 1=research, 2=info
    if (m_midBook)  m_midBook->SetSelection(2);

    // Update button label
    if (m_btnInfo) m_btnInfo->SetLabel("Back");

    RefreshInfoUI();
}

void StrategicLevelFrame::LeaveInfoMode()
{
    m_infoMode = false;

    if (m_leftBook) m_leftBook->SetSelection(0);
    if (m_midBook)  m_midBook->SetSelection(0);

    if (m_btnInfo) m_btnInfo->SetLabel("Info");

    RefreshUI();
}

void StrategicLevelFrame::SelectInfoIndex(int idx)
{
    if (idx < 0 || idx >= (int)m_researchDb.size())
        return;

    m_infoBrowseIndex = idx;
    RefreshInfoUI();
}

void StrategicLevelFrame::RefreshInfoUI()
{
    if (!m_infoList && !m_infoText)
        return;

    // Re-entrancy guard
    if (m_infoRefreshing)
        return;
    m_infoRefreshing = true;
    struct RGuard { bool& f; ~RGuard(){ f = false; } } _rg{m_infoRefreshing};

    EnsureResearchLoaded();

    // In game mode: show only completed research
    // Without game mode: show everything
    auto isVisible = [&](const ResearchItem& it) -> bool {
        if (!m_gameModeEnabled)
            return true;  // Show all in sandbox mode
        // In game mode: only show completed items
        return (it.id >= 0 && m_researchCompleted.count(it.id) > 0);
    };

    // ----------------------------------------------------------------
    // Categorized list (wxListCtrl)
    // ----------------------------------------------------------------
    static constexpr wxUIntPtr kHdrSentinel = static_cast<wxUIntPtr>(-1);

    if (m_infoList)
    {
        m_infoList->Freeze();
        m_infoList->DeleteAllItems();

        const wxColour clrHeader = m_palette.heading;
        const wxColour clrNormal = m_palette.text;

        wxString lastGroup;
        long row = 0, selRow = -1;

        for (int i = 0; i < (int)m_researchDb.size(); ++i)
        {
            const ResearchItem& it = m_researchDb[i];
            
            // Filter: only show items that pass visibility check
            if (!isVisible(it))
                continue;

            // Group header
            if (it.group != lastGroup)
            {
                lastGroup = it.group;
                if (!lastGroup.empty())
                {
                    m_infoList->InsertItem(row, lastGroup);
                    m_infoList->SetItemData(row, kHdrSentinel);
                    m_infoList->SetItemTextColour(row, clrHeader);
                    ++row;
                }
            }

            wxString label = wxString("  ") + it.title;
            m_infoList->InsertItem(row, label);
            m_infoList->SetItemData(row, static_cast<wxUIntPtr>(i));
            m_infoList->SetItemTextColour(row, clrNormal);

            if (i == m_infoBrowseIndex)
                selRow = row;
            ++row;
        }

        // Auto-fit column
        if (row > 0)
        {
            m_infoList->SetColumnWidth(0, wxLIST_AUTOSIZE);
            const int lw = m_infoList->GetClientSize().GetWidth();
            if (m_infoList->GetColumnWidth(0) < lw)
                m_infoList->SetColumnWidth(0, lw);
        }

        // Select item
        if (selRow >= 0)
        {
            m_infoList->SetItemState(selRow,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            m_infoList->EnsureVisible(selRow);
        }
        m_infoList->Thaw();
    }

    // ----------------------------------------------------------------
    // Info text box: detail of the selected item
    // ----------------------------------------------------------------
    if (m_infoText)
    {
        if (m_infoBrowseIndex >= 0 && m_infoBrowseIndex < (int)m_researchDb.size())
        {
            const ResearchItem& cur = m_researchDb[m_infoBrowseIndex];
            wxString txt = cur.info.empty() ? cur.brief : cur.info;
            m_infoText->SetValue(txt);
        }
        else
        {
            if (m_gameModeEnabled)
                m_infoText->SetValue("Select a discovered item from the list.\n\nIn campaign mode, only researched items are shown.");
            else
                m_infoText->SetValue("Select an item from the list to view details.");
        }
    }
}

void StrategicLevelFrame::OnShowInfo(wxCommandEvent&)
{
    // Leave buy mode if active
    if (m_buyModeActive)
        LeaveBuyMode();

    // Leave research mode if active
    if (m_researchMode)
        LeaveResearchMode();

    // Toggle info mode
    if (m_infoMode)
        LeaveInfoMode();
    else
        EnterInfoMode();
}


void StrategicLevelFrame::ApplyResearchTickEndTurn()
{
    // Research allocation is automatic: when research is active (m_researchAllocPerTurn > 0),
    // ALL accumulated research points from resources are spent on the active item.
    if (m_researchAllocPerTurn <= 0)
        return;

    EnsureResearchLoaded();
    if (m_researchDb.empty() || m_researchActiveIndex < 0
        || m_researchActiveIndex >= (int)m_researchDb.size())
        return;

    const ResearchItem& cur = m_researchDb[m_researchActiveIndex];
    if (cur.id < 0 || m_researchCompleted.count(cur.id))
        return;

    if (m_research <= 0)
        return;

    const int cost = std::max(1, cur.cost);
    int& prog = m_researchProgressById[cur.id];

    // Spend all available points this turn
    const int spend = m_research;
    m_research = 0;
    prog += spend;

    if (prog >= cost)
    {
        prog = cost;
        m_researchCompleted.insert(cur.id);
        m_researchAllocPerTurn = 0; // auto-stop when done
        wxMessageBox(
            wxString::Format("Research complete: %s", cur.title),
            "Research", wxOK | wxICON_INFORMATION, this);
    }
}

void StrategicLevelFrame::OnShowStrategicMap(wxCommandEvent&)
{
    if (m_researchMode) LeaveResearchMode();
    if (m_infoMode) LeaveInfoMode();
    if (m_unitsModeActive) LeaveUnitsMode();
    if (m_leftBook)
        m_leftBook->SetSelection(0);
}

void StrategicLevelFrame::OnShowHierarchy(wxCommandEvent&)
{
    if (m_researchMode) LeaveResearchMode();
    if (m_infoMode) LeaveInfoMode();
    if (m_unitsModeActive) LeaveUnitsMode();
    if (m_leftBook)
        m_leftBook->SetSelection(1);
}


void StrategicLevelFrame::OnShowResources(wxCommandEvent&)
{
    if (m_researchMode) LeaveResearchMode();
    if (m_infoMode) LeaveInfoMode();
    if (m_unitsModeActive) LeaveUnitsMode();
    SaveStrategicState();

    // Switch page FIRST so canvas has correct size when Refresh triggers paint
    if (m_leftBook)
        m_leftBook->SetSelection(2);

    m_overlayDirty = true;
    RefreshResourcesPage();   // fills table + triggers canvas Refresh internally
}

void StrategicLevelFrame::OnShowStats(wxCommandEvent&)
{
    if (m_researchMode) LeaveResearchMode();
    if (m_infoMode) LeaveInfoMode();
    if (m_unitsModeActive) LeaveUnitsMode();
    // Ensure the stats page sees the latest state.
    SaveStrategicState();
    LoadRanksTable();
    LoadMissionStatsIfPresent();
    RecomputePlayerRank();
    RefreshStatsPage();

    if (m_leftBook)
        m_leftBook->SetSelection(3);
}
void StrategicLevelFrame::SelectTerritoryById(int territory_id)
{
    // Find index in LevelData by id.
    int idx = -1;
    for (size_t i = 0; i < m_level.territories.size(); ++i)
    {
        if (m_level.territories[i].id == territory_id)
        {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0)
        return;

    m_selectedTerritory = territory_id;

    // Reuse existing logic by faking a button event id.
    wxCommandEvent ev(wxEVT_BUTTON, ID_TERRITORY_BASE + idx);
    OnTerritory(ev);
}


void StrategicLevelFrame::OnMapMouseMove(wxMouseEvent& ev)
{
    if (!m_hasClk || !m_hasBg || !m_bgBitmap.IsOk())
    {
        ev.Skip();
        return;
    }

    const wxPoint p = ev.GetPosition();

    // Use last paint transform
    const double s = (m_lastMapScale <= 0.0) ? 1.0 : m_lastMapScale;
    const int bw = m_lastBgW;
    const int bh = m_lastBgH;

    if (bw <= 0 || bh <= 0)
    {
        ev.Skip();
        return;
    }

    // Screen -> background pixel
    const int px = (int)std::floor(((double)(p.x - m_lastMapOffX)) / s);
    const int py = (int)std::floor(((double)(p.y - m_lastMapOffY)) / s);

    int newHover = 0;
    if (px >= 0 && py >= 0 && px < bw && py < bh)
    {
        // Background pixel -> CLK pixel (scale if sizes differ)
        const int cx = (int)std::floor((double)px * (double)m_clkW / (double)bw);
        const int cy = (int)std::floor((double)py * (double)m_clkH / (double)bh);

        if (cx >= 0 && cy >= 0 && cx < m_clkW && cy < m_clkH)
        {
            const size_t idx = (size_t)cy * (size_t)m_clkW + (size_t)cx;
            const uint8_t v = m_clkValues[idx];

            const int maxId = (int)m_visibleTerritory.size() - 1;
            int tid = 0;
            if (v >= 1 && v <= (uint8_t)maxId) tid = (int)v;
            else if (v >= 129 && v <= (uint8_t)(128 + maxId)) tid = (int)v - 128;

            if (tid > 0)
            {
                // In game mode, only hover visible territories
                if (!m_gameModeEnabled || (tid < (int)m_visibleTerritory.size() && m_visibleTerritory[tid]))
                {
                    // In resources view, only highlight owned territories
                    const bool resourcesView = (m_leftBook && m_leftBook->GetCurrentPage() == m_resourcesPanel);
                    if (!resourcesView ||
                        std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid) != m_ownedTerritories.end())
                    {
                        newHover = tid;
                    }
                }
            }
        }
    }

    if (newHover != m_hoverTerritory)
    {
        m_hoverTerritory = newHover;
        MarkOverlayDirty();
    }

    ev.Skip();
}
void StrategicLevelFrame::OnMapLeftDown(wxMouseEvent& ev)
{
    if (!m_hasBg || !m_bgBitmap.IsOk() || !m_hasClk || m_clkValues.empty())
    {
        ev.Skip();
        return;
    }

    wxWindow* target = wxDynamicCast(ev.GetEventObject(), wxWindow);
    if (!target)
        target = m_mapCanvas ? (wxWindow*)m_mapCanvas : (wxWindow*)m_mapPanel;
    const bool resourcesView = (target == m_resourcesCanvas);
    if (!target)
    {
        ev.Skip();
        return;
    }

    int pw, ph;
    target->GetClientSize(&pw, &ph);
    const int bw = m_bgBitmap.GetWidth();
    const int bh = m_bgBitmap.GetHeight();
    if (pw <= 0 || ph <= 0 || bw <= 0 || bh <= 0)
    {
        ev.Skip();
        return;
    }

    const double sx = (double)pw / (double)bw;
    const double sy = (double)ph / (double)bh;
    const double s = std::min(sx, sy);
    const int dw = std::max(1, (int)std::lround((double)bw * s));
    const int dh = std::max(1, (int)std::lround((double)bh * s));
    const int ox = (pw - dw) / 2;
    const int oy = (ph - dh) / 2;

    const wxPoint p = ev.GetPosition();
    if (p.x < ox || p.y < oy || p.x >= ox + dw || p.y >= oy + dh)
        return;

    // Map click from scaled bitmap to original pixel coords.
    const int mx = (int)std::floor(((double)(p.x - ox) * (double)bw) / (double)dw);
    const int my = (int)std::floor(((double)(p.y - oy) * (double)bh) / (double)dh);
    if (mx < 0 || my < 0 || mx >= bw || my >= bh)
        return;

    // CLK map must match bitmap dimensions.
    if (m_clkW != bw || m_clkH != bh || (size_t)m_clkW * (size_t)m_clkH != m_clkValues.size())
        return;

    //const unsigned char tid = m_clkValues[(size_t)my * (size_t)m_clkW + (size_t)mx];
    //if(tid == 0)
    //    return;

    //SelectTerritoryById((int)tid);

    const unsigned char tid = m_clkValues[(size_t)my * (size_t)m_clkW + (size_t)mx];
    if (tid == 0)
        return;

    // CLK value can be either:
    // - territory id (matches LevelTerritory::id), or
    // - 1-based region index (1..N) into m_level.territories
    int chosenTerritoryId = (int)tid;

    auto isVisible = [&](int tid2) -> bool
    {
        if (!m_gameModeEnabled)
            return true;
        if (tid2 <= 0 || tid2 >= (int)m_visibleTerritory.size())
            return false;
        return m_visibleTerritory[tid2] != 0;
    };

    // 1) Try direct match by id
    bool idFound = false;
    for (const auto& t : m_level.territories)
    {
        if (t.id == chosenTerritoryId)
        {
            idFound = true;
            break;
        }
    }

    if (idFound)
    {
        if (!isVisible(chosenTerritoryId))
            return;
        // In resources view, only select owned territories
        if (resourcesView)
        {
            const bool owned = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), chosenTerritoryId) != m_ownedTerritories.end();
            if (!owned) return;
        }
        SelectTerritoryById(chosenTerritoryId);
        return;
    }

    // 2) Fallback: treat tid as 1-based index
    const size_t idx = (size_t)tid - 1;
    if (idx < m_level.territories.size())
    {
        SelectTerritoryById(m_level.territories[idx].id);
        return;
    }

    wxLogMessage("[CLK] tid=%u -> fallback idx=%zu -> territory_id=%d",
        (unsigned)tid, idx, m_level.territories[idx].id);

    // Out of range -> ignore click
    wxLogWarning("[CLK] tid=%u out of range (territories=%zu)", (unsigned)tid, m_level.territories.size());

}

void StrategicLevelFrame::OnTerritory(wxCommandEvent& ev)
{
    int idx = ev.GetId() - ID_TERRITORY_BASE;
    if (idx < 0 || idx >= (int)m_level.territories.size())
        return;

    m_selectedTerritory = m_level.territories[idx].id;

    const bool resourcesView = (m_leftBook && m_leftBook->GetCurrentPage() == m_resourcesPanel);

    if (resourcesView)
    {
        RefreshResourcesPage();
        if (m_resourcesCanvas) m_resourcesCanvas->Refresh();
        return;
    }

    const auto& t = m_level.territories[idx];
    wxString info;
    info << wxString::Format("Territory %d\n", t.id);
    info << "Mission: " << t.mission << "\n";
    info << "Intro: " << t.intro_mission << "\n";
    info << "Music: " << t.music << "\n";
    info << wxString::Format("Strategic point: %d,%d\n", t.strategic_x, t.strategic_y);

    auto itc = m_territoryCurrentMission.find(t.id);
    if (itc != m_territoryCurrentMission.end())
        info << "Current: " << itc->second << "\n";

    auto itn = m_territoryLaunchCount.find(t.id);
    if (itn != m_territoryLaunchCount.end())
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
                if (texts_dir.empty() && fs::exists(p, ec) && fs::is_directory(p, ec))
                    texts_dir = p;
            };

        // 1) Walk up from the level DEF location and try common layouts
        fs::path base = fs::path(m_level.source_path).parent_path();
        for (int i = 0; i < 8 && !base.empty() && texts_dir.empty(); ++i)
        {
            try_dir(base / "DATA" / "TEXTS");
            try_dir(base / "DATA" / "texts");
            try_dir(base / "TEXTS");
            try_dir(base / "texts");

            base = base.parent_path();
        }

        // 2) Fallback: current working directory
        if (texts_dir.empty())
        {
            const fs::path cwd = fs::current_path(ec);
            try_dir(cwd / "DATA" / "TEXTS");
            try_dir(cwd / "DATA" / "texts");
            try_dir(cwd / "TEXTS");
            try_dir(cwd / "texts");
        }
    }

    if (!texts_dir.empty())
    {
        // Use the *current* mission token (can change as you replay territories)
        std::string cur = t.mission;
        auto itc2 = m_territoryCurrentMission.find(t.id);
        if (itc2 != m_territoryCurrentMission.end() && !itc2->second.empty())
            cur = itc2->second;

        try_append_text_set(info, texts_dir, cur);

        // Also show intro (some territories use different intro token)
        if (!t.intro_mission.empty() && to_lower(t.intro_mission) != "none")
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

    if (auto* box = wxDynamicCast(m_mapPanel->FindWindow(ID_TERRITORY_TEXTBOX), wxTextCtrl))
    {
        box->SetValue(info);
        box->ShowPosition(0);
    }
    RefreshUI();
}

void StrategicLevelFrame::OnResearch(wxCommandEvent&)
{
    if (m_infoMode)
        LeaveInfoMode();
    if (m_unitsModeActive)
        LeaveUnitsMode();

    if (!m_researchMode)
        EnterResearchMode();
    else
        LeaveResearchMode();
}


static std::filesystem::path FindCommanderNamesDefPath()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path base = GetStableBaseDir();
    const std::vector<fs::path> candidates = {
        base / "data" / "C_NAMES.DEF",
        base / "C_NAMES.DEF",
        fs::current_path(ec) / "data" / "C_NAMES.DEF",
        fs::current_path(ec) / "C_NAMES.DEF",
        fs::path("C_NAMES.DEF"),
    };

    for (const auto& p : candidates)
    {
        if (!p.empty() && fs::exists(p, ec))
            return p;
    }
    return {};
}

bool StrategicLevelFrame::EnsureCommanderNamesLoaded()
{
    if (m_commanderNamesLoaded)
        return true;

    m_commanderNames.clear();
    const auto p = FindCommanderNamesDefPath();
    if (p.empty())
    {
        wxLogWarning("[COMMANDERS] C_NAMES.DEF not found.");
        m_commanderNamesLoaded = true; // avoid spamming warnings
        return false;
    }

    std::ifstream f(p);
    if (!f)
        return false;

    std::string line;
    while (std::getline(f, line))
    {
        line = trim(line);
        if (line.empty())
            continue;
        m_commanderNames.push_back(line);
    }

    m_commanderNamesLoaded = true;
    return !m_commanderNames.empty();
}

wxString StrategicLevelFrame::GetRankAbbrev(int rank) const
{
    // Abbreviations (Czech-ish). Keep stable for UI.
    static const char* kAbbr[] = {
        "2Lt.", "1Lt.", "Cpt.", "Maj.", "LtCol.", "Col.", "MajGen.", "LtGen.", "Gen."
    };
    if (rank < 0) rank = 0;
    if (rank >= (int)(sizeof(kAbbr) / sizeof(kAbbr[0])))
        return wxString::Format("R%d", rank);
    return wxString::FromUTF8(kAbbr[rank]);
}

void StrategicLevelFrame::MaybeGenerateCommanderOffer()
{
    // Enforce windowed limit: max 2 per 25 turns.
    if (m_turn >= m_cmdGenWindowStartTurn + 25)
    {
        m_cmdGenWindowStartTurn = m_turn;
        m_cmdGenCountInWindow = 0;
    }
    if (m_cmdGenCountInWindow >= 2)
        return;

    if (!EnsureCommanderNamesLoaded())
        return;

    // Already have an offer in this turn (shouldn't happen if we clear on end-turn).
    if (!m_availableCommanders.empty())
        return;

    // Chance: tweak here if you want different pacing.
    const int chancePercent = 20; // 20% per turn => many "rolls" but capped to 2 per 25 turns.
    if ((std::rand() % 100) >= chancePercent)
        return;

    // Pick random unique name (avoid duplicates among owned + current offer).
    auto nameTaken = [&](const std::string& n) -> bool
        {
            for (const auto& c : m_playerCommanders) if (to_upper(c.name) == to_upper(n)) return true;
            for (const auto& c : m_availableCommanders) if (to_upper(c.name) == to_upper(n)) return true;
            return false;
        };

    std::string name;
    for (int tries = 0; tries < 32; ++tries)
    {
        const std::string& cand = m_commanderNames[(size_t)(std::rand() % (int)m_commanderNames.size())];
        if (!cand.empty() && !nameTaken(cand))
        {
            name = cand;
            break;
        }
    }
    if (name.empty())
        name = m_commanderNames[(size_t)(std::rand() % (int)m_commanderNames.size())];

    // Rank is never higher than player's rank.
    int rankMax = std::max(0, m_player.rank);
    int rank = 0;
    if (rankMax > 0)
        rank = std::rand() % (rankMax + 1);

    CommanderRec rec;
    rec.name = name;
    rec.rank = rank;

    m_availableCommanders.push_back(rec);
    m_cmdGenCountInWindow += 1;
}

void StrategicLevelFrame::OnBuyCommander(wxCommandEvent&)
{
    if ((int)m_playerCommanders.size() >= 14)
    {
        wxMessageBox("Commander limit reached (14).", "Buy commander", wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (m_availableCommanders.empty())
    {
        wxMessageBox("No commanders available this turn.", "Buy commander", wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxDialog dlg(this, wxID_ANY, "Buy commander", wxDefaultPosition, wxSize(420, 360));
    dlg.SetBackgroundColour(m_palette.background);
    dlg.SetForegroundColour(m_palette.text);
    dlg.SetFont(m_fontText);
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl = new wxStaticText(&dlg, wxID_ANY, "Available commanders:");
    lbl->SetFont(m_fontText);
    lbl->SetForegroundColour(m_palette.text);
    rootSizer->Add(lbl, 0, wxLEFT | wxRIGHT | wxTOP, 10);

    auto* list = new wxListBox(&dlg, wxID_ANY);
    list->SetFont(m_fontText);
    list->SetBackgroundColour(m_palette.background);
    list->SetForegroundColour(m_palette.text);

    for (const auto& c : m_availableCommanders)
        list->Append(wxString::FromUTF8(c.name) + " (" + GetRankAbbrev(c.rank) + ")");

    if (list->GetCount() > 0)
        list->SetSelection(0);

    rootSizer->Add(list, 1, wxALL | wxEXPAND, 10);

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

    if (dlg.ShowModal() != wxID_OK)
        return;

    int sel = list->GetSelection();
    if (sel == wxNOT_FOUND || sel >= (int)m_availableCommanders.size())
        return;

    // Buy: move from offers to owned. (No selling.)
    m_playerCommanders.push_back(m_availableCommanders[(size_t)sel]);
    m_availableCommanders.clear();

    SaveStrategicState();
    RefreshUI();
}

void StrategicLevelFrame::OnBuyUnits(wxCommandEvent&)
{
    if (!m_spellData || !m_spellData->units)
    {
        wxMessageBox("Units data not loaded.", "Buy units", wxOK | wxICON_WARNING, this);
        return;
    }
    if (!EnsureUnitCostsLoaded())
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
    for (const auto* unit : m_spellData->units->GetUnits())
    {
        if (!unit)
            continue;
        unit_ids.push_back(unit->type_id);
        int cost = GetUnitBuyCost(unit->type_id);
        unit_costs.push_back(cost);
        wxString label = wxString::Format("#%02d: %s", unit->type_id, wxString(char2wstringCP895(unit->name)));
        if (cost > 0)
            label += wxString::Format(" (%d)", cost);
        list->Append(label);
    }
    if (!unit_ids.empty())
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

    if (dlg.ShowModal() != wxID_OK)
        return;

    int sel = list->GetSelection();
    if (sel == wxNOT_FOUND || sel >= (int)unit_ids.size())
    {
        wxMessageBox("No unit selected.", "Buy units", wxOK | wxICON_WARNING, this);
        return;
    }
    if (sel >= (int)unit_costs.size() || unit_costs[sel] <= 0)
    {
        wxMessageBox("Selected unit has no price defined.", "Buy units", wxOK | wxICON_WARNING, this);
        return;
    }

    const int count = spinCount->GetValue();
    const int unitCost = unit_costs[sel];
    const int totalCost = unitCost * count;
    if (m_money < totalCost)
    {
        wxMessageBox(wxString::Format("Not enough money. Need %d, you have %d.", totalCost, m_money),
            "Buy units", wxOK | wxICON_WARNING, this);
        return;
    }

    //LevelData::PlayerUnitAdd add;
    //add.unit_id = unit_ids[sel];
    //add.count = count;
    //add.health = 100;
    //add.extra = "-";

    //auto it = std::find_if(m_playerUnits.begin(), m_playerUnits.end(),
    //    [&](const LevelData::PlayerUnitAdd& u)
    //    {
    //        return u.unit_id == add.unit_id && u.health == add.health;
    //    });
    //if(it != m_playerUnits.end())
    //    it->count += add.count;
    //else
    //    m_playerUnits.push_back(add);

    // v OnBuyUnits(): místo agregace přidej 'count' kusů jako samostatné položky
    LevelData::PlayerUnitAdd addProto;
    addProto.unit_id = unit_ids[sel];
    addProto.health = 100;
    addProto.extra = "-";

    for (int i = 0; i < count; ++i)
    {
        LevelData::PlayerUnitAdd add = addProto;
        add.count = 1; // per-instance
        m_playerUnits.push_back(add);
    }

    m_money -= totalCost;
    SaveStrategicState();
    RefreshUI();
}

void StrategicLevelFrame::OnSellUnits(wxCommandEvent&)
{
    if (m_playerUnits.empty())
    {
        wxMessageBox("No units to sell.", "Sell units", wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (!EnsureUnitCostsLoaded())
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
    for (size_t i = 0; i < m_playerUnits.size(); ++i)
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
        if (entry.cost > 0)
            label += wxString::Format(" (sell %d)", entry.cost / 2);
        else
            label += " (no price)";
        list->Append(label);
    }
    if (!entries.empty())
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
            if (sel == wxNOT_FOUND || sel >= (int)entries.size())
                return;
            int maxCount = std::max(1, entries[sel].count);
            spinCount->SetRange(1, maxCount);
            if (spinCount->GetValue() > maxCount)
                spinCount->SetValue(maxCount);
        };
    updateSpinRange();
    list->Bind(wxEVT_LISTBOX, [&](wxCommandEvent&) { updateSpinRange(); });

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

    if (dlg.ShowModal() != wxID_OK)
        return;

    int sel = list->GetSelection();
    if (sel == wxNOT_FOUND || sel >= (int)entries.size())
    {
        wxMessageBox("No unit selected.", "Sell units", wxOK | wxICON_WARNING, this);
        return;
    }

    const auto& entry = entries[sel];
    if (entry.cost <= 0)
    {
        wxMessageBox("Selected unit has no price defined.", "Sell units", wxOK | wxICON_WARNING, this);
        return;
    }

    int sellCount = spinCount->GetValue();
    if (sellCount <= 0)
        return;
    if (sellCount > entry.count)
        sellCount = entry.count;

    int refund = (entry.cost * sellCount) / 2;
    m_money += refund;

    if (entry.index >= 0 && entry.index < (int)m_playerUnits.size())
    {
        auto& unit = m_playerUnits[entry.index];
        unit.count -= sellCount;
        if (unit.count <= 0)
            m_playerUnits.erase(m_playerUnits.begin() + entry.index);
    }

    SaveStrategicState();
    RefreshUI();
}

const LevelMission* StrategicLevelFrame::FindMissionByNameUpper(const std::string& name_upper) const
{
    for (const auto& m : m_level.missions)
    {
        if (to_upper(m.name) == name_upper)
            return &m;
    }
    return nullptr;
}

std::string StrategicLevelFrame::ResolveMissionTokenForTerritory(int territory_id) const
{
    // first play can use intro
    int launches = 0;
    auto itL = m_territoryLaunchCount.find(territory_id);
    if (itL != m_territoryLaunchCount.end()) launches = itL->second;

    // find territory record
    const LevelTerritory* terr = nullptr;
    for (const auto& t : m_level.territories)
        if (t.id == territory_id) { terr = &t; break; }

    if (!terr)
        return std::string();

    if (launches == 0 && !terr->intro_mission.empty() && terr->intro_mission != "none")
        return terr->intro_mission;

    auto it = m_territoryCurrentMission.find(territory_id);
    if (it != m_territoryCurrentMission.end() && !it->second.empty() && it->second != "none")
        return it->second;

    return terr->mission;
}

std::wstring StrategicLevelFrame::ResolveMapDefPathForMissionToken(const std::string& mission_token) const
{
    wxLogMessage(
        "[RESOLVE] token='%s'",
        mission_token.c_str()
    );

    if (mission_token.empty() || mission_token == "none")
        return L"";

    namespace fs = std::filesystem;
    std::error_code ec;

    std::vector<std::filesystem::path> bases;

    // 1) Prefer runtime extracted location
    bases.push_back(std::filesystem::path(GetStableBaseDir()) / "temp" / "COMMON");

    // 2) Fallback: where LEVEL DEF lives (often spell_extractfs/data_extracted)
    bases.push_back(std::filesystem::path(m_level.source_path).parent_path());

    // Fallback
    bases.push_back(fs::current_path(ec));

    auto try_in_base = [&](const fs::path& base) -> std::wstring
        {
            if (base.empty() || !fs::exists(base, ec))
                return L"";

            // 0) prefer A variant when token ends with digit
            if (!mission_token.empty())
            {
                char last = mission_token.back();
                if (last >= '0' && last <= '9')
                {
                    const std::string token_lower = to_lower(mission_token) + "a";
                    const std::string token_upper = to_upper(mission_token) + "A";
                    fs::path pVar1 = base / (token_lower + ".def");
                    fs::path pVar2 = base / (token_lower + ".DEF");
                    fs::path pVar3 = base / (token_upper + ".DEF");
                    if (fs::exists(pVar1)) return pVar1.wstring();
                    if (fs::exists(pVar2)) return pVar2.wstring();
                    if (fs::exists(pVar3)) return pVar3.wstring();
                }
            }

            // 1) exact match
            fs::path pExact1 = base / (to_upper(mission_token) + ".DEF");
            fs::path pExact2 = base / (mission_token + ".DEF");
            fs::path pExact3 = base / (mission_token + ".def");
            if (fs::exists(pExact1)) return pExact1.wstring();
            if (fs::exists(pExact2)) return pExact2.wstring();
            if (fs::exists(pExact3)) return pExact3.wstring();

            // 2) multi-variant choice (optional – nechal bych jen v prvním base,
            // ale klidně můžeš i tady – já bych to pro temp\COMMON nechal)
            // ... (tvůj stávající blok s candidates)

            return L"";
        };

    for (const auto& base : bases)
    {
        std::wstring p = try_in_base(base);
        if (!p.empty())
            return p;
    }

    return L"";
}

void StrategicLevelFrame::OnLaunch(wxCommandEvent&)
{
    if (m_selectedTerritory < 0 || !m_main)
        return;

    const int terr_id = m_selectedTerritory;
    std::string token = ResolveMissionTokenForTerritory(terr_id);
    wxLogMessage(
        "[LAUNCH] territory_id=%d token='%s'",
        terr_id,
        token.c_str()
    );
    if (token.empty() || token == "none")
        return;

    std::wstring defPath = ResolveMapDefPathForMissionToken(token);

    if (defPath.empty())
    {
        wxMessageBox("Map DEF not found for mission: " + wxString(token), "Launch", wxOK | wxICON_WARNING, this);
        return;
    }

    wxLogMessage("[LAUNCH] DEF: %ls", defPath.c_str());
    wxLogMessage("[LAUNCH] playerUnits.count=%zu", m_playerUnits.size());
    for (size_t i = 0; i < m_playerUnits.size(); ++i)
    {
        const auto& u = m_playerUnits[i];
        wxLogMessage("[LAUNCH] unit[%zu]: id=%d count=%d health=%d",
            i, u.unit_id, u.count, u.health);
    }

    bool ok = m_main->LoadMapFromDefPath(defPath, m_playerUnits);

    if (!ok && !m_playerUnits.empty())
    {
        wxLogWarning("[LAUNCH] Load failed WITH units, retrying WITHOUT units...");
        const decltype(m_playerUnits) empty_units;
        ok = m_main->LoadMapFromDefPath(defPath, empty_units);
    }

    if (!ok)
    {
        wxMessageBox("LoadMapFromDefPath FAILED (even without units)\nDEF:\n" + wxString(defPath),
            "Launch", wxOK | wxICON_ERROR, this);
        return;
    }

    // update launch count
    m_territoryLaunchCount[terr_id] += 1;

    // very simple progression for multi-variant missions:
    // if Mission(MXX_YYA) has EndOKMission(MXX_YYB) -> advance.
    const std::string upperName = to_upper(token);
    if (const LevelMission* m = FindMissionByNameUpper(upperName))
    {
        if (!m->end_ok_mission.empty() && m->end_ok_mission != "none")
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
    // TODO: Replace this placeholder income with Resources system once economy is balanced.
    m_money += 50;

    ApplyResourceTickEndTurn();


    ApplyResearchTickEndTurn();

    // Apply unit cooldown ticks (recruit/upgrade completion)
    ApplyUnitsCooldownTick();

    // Commander offers are generated on end-turn (for the *new* turn).
    // Offers do not carry over between turns.
    // Store offer of commanders only for the current turn.
    // m_availableCommanders.clear();
    MaybeGenerateCommanderOffer();

    SaveStrategicState();
    RefreshUI();
}

static std::filesystem::path GetStrategicStatePath(const LevelData& level)
{
    return GetStrategicSaveDir(level) / "autosave.json";
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
    std::vector<StrategicLevelFrame::CommanderRec>& playerCommanders,
    std::vector<StrategicLevelFrame::CommanderRec>& availableCommanders,
    int& cmdGenWindowStartTurn,
    int& cmdGenCountInWindow,
    bool& gameModeEnabled,
    std::vector<int>& ownedTerritories,
    std::unordered_map<int, StrategicLevelFrame::TerritoryResourceState>& territoryResources,
    std::string* out_level_def = nullptr,
    std::string* out_timestamp = nullptr)

{
    units.clear();
    playerCommanders.clear();
    availableCommanders.clear();
    gameModeEnabled = false;
    ownedTerritories.clear();
    territoryResources.clear();
    cmdGenWindowStartTurn = 1;
    cmdGenCountInWindow = 0;

    // defaults
    turn = 1;
    money = 0;
    research = 0;
    selected_territory = -1;
    player = StrategicLevelFrame::PlayerProgress{};
    gameModeEnabled = false;
    ownedTerritories.clear();

    territoryMission.clear();
    territoryLaunchCount.clear();
    for (const auto& t : level.territories)
    {
        territoryMission[t.id] = t.mission;
        territoryLaunchCount[t.id] = 0;
    }

    // default resources state
    for (const auto& t : level.territories)
    {
        territoryResources[t.id] = StrategicLevelFrame::TerritoryResourceState{};
    }

    if (out_level_def) out_level_def->clear();
    if (out_timestamp) out_timestamp->clear();

    std::ifstream f(path);
    if (!f)
        return false;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty())
        return false;

    std::smatch m;

    // version/level_def/timestamp are optional but recommended
    std::regex leveldef_re("\"level_def\"\\s*:\\s*\"([^\"]*)\"");
    if (out_level_def && std::regex_search(data, m, leveldef_re) && m.size() > 1)
        *out_level_def = m[1].str();

    std::regex ts_re("\"timestamp\"\\s*:\\s*\"([^\"]*)\"");
    if (out_timestamp && std::regex_search(data, m, ts_re) && m.size() > 1)
        *out_timestamp = m[1].str();

    if (std::regex_search(data, m, std::regex("\"turn\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        turn = std::stoi(m[1].str());

    if (std::regex_search(data, m, std::regex("\"money\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        money = std::stoi(m[1].str());

    if (std::regex_search(data, m, std::regex("\"research\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        research = std::stoi(m[1].str());

    if (std::regex_search(data, m, std::regex("\"selected_territory\"\\s*:\\s*(-?\\d+)")) && m.size() > 1)
        selected_territory = std::stoi(m[1].str());

    // game mode (optional)
    std::regex gm_re("\"game_mode\"\\s*:\\s*(true|false)");
    if (std::regex_search(data, m, gm_re) && m.size() > 1)
        gameModeEnabled = (m[1].str() == "true");

    // owned territories (optional)
    std::smatch mm2;
    std::regex owned_arr_re("\"owned_territories\"\\s*:\\s*\\[(.*?)\\]");
    if (std::regex_search(data, mm2, owned_arr_re) && mm2.size() > 1)
    {
        const std::string arr = mm2[1].str();
        std::regex int_re("(\\d+)");
        for (auto it = std::sregex_iterator(arr.begin(), arr.end(), int_re); it != std::sregex_iterator(); ++it)
        {
            int id = std::stoi((*it)[1].str());
            ownedTerritories.push_back(id);
        }

    
    // research_state (optional)
    if (g_researchPersistLoad && g_researchPersistLoad->progressById && g_researchPersistLoad->completed)
    {
        // defaults
        if (g_researchPersistLoad->activeId) *g_researchPersistLoad->activeId = -1;
        if (g_researchPersistLoad->activeIndex) *g_researchPersistLoad->activeIndex = -1;
        if (g_researchPersistLoad->allocPerTurn) *g_researchPersistLoad->allocPerTurn = 0;
        g_researchPersistLoad->progressById->clear();
        g_researchPersistLoad->completed->clear();

        // capture "research_state": { ... }  (or null)
        std::smatch mmRS;
                std::regex rs_re(R"("research_state"\s*:\s*(null|\{.*?\}))", std::regex_constants::ECMAScript | std::regex_constants::icase);
        if (std::regex_search(data, mmRS, rs_re) && mmRS.size() > 1)
        {
            const std::string rsVal = mmRS[1].str();
            if (!rsVal.empty() && rsVal[0] == '{')
            {
                (void)ParseJsonIntField(rsVal, "active_id", *g_researchPersistLoad->activeId);
                (void)ParseJsonIntField(rsVal, "active_index", *g_researchPersistLoad->activeIndex);
                (void)ParseJsonIntField(rsVal, "alloc_per_turn", *g_researchPersistLoad->allocPerTurn);

                // progress object
                std::smatch mmProg;
                                std::regex prog_re(R"("progress"\s*:\s*\{(.*?)\})", std::regex_constants::ECMAScript | std::regex_constants::icase);
                if (std::regex_search(rsVal, mmProg, prog_re) && mmProg.size() > 1)
                {
                    const std::string pobj = mmProg[1].str();
                    // match pairs like "12": 3
                    std::regex pair_re("\\\\\"(\\\\d+)\\\\\"\\\\s*:\\\\s*(-?\\\\d+)");
                    for (auto it = std::sregex_iterator(pobj.begin(), pobj.end(), pair_re); it != std::sregex_iterator(); ++it)
                    {
                        const int id = std::atoi((*it)[1].str().c_str());
                        const int val = std::atoi((*it)[2].str().c_str());
                        (*g_researchPersistLoad->progressById)[id] = val;
                    }
                }

                // completed array
                std::smatch mmComp;
                                std::regex comp_re(R"("completed"\s*:\s*\[(.*?)\])", std::regex_constants::ECMAScript | std::regex_constants::icase);
                if (std::regex_search(rsVal, mmComp, comp_re) && mmComp.size() > 1)
                {
                    const std::string carr = mmComp[1].str();
                    std::regex num_re("(-?\\\\d+)");
                    for (auto it = std::sregex_iterator(carr.begin(), carr.end(), num_re); it != std::sregex_iterator(); ++it)
                    {
                        const int id = std::atoi((*it)[1].str().c_str());
                        g_researchPersistLoad->completed->insert(id);
                    }
                }
            }
        }
    }

// resources (optional)
    std::smatch mmRes;
        std::regex res_arr_re(R"("resources"\s*:\s*\[(.*?)\])", std::regex_constants::ECMAScript | std::regex_constants::icase);
    if (std::regex_search(data, mmRes, res_arr_re) && mmRes.size() > 1)
    {
        const std::string arr = mmRes[1].str();
        // match objects like {"id": 1, "total": 20, ...}
        std::regex obj_re("\\{[^\\}]*\\}");
        for (auto it = std::sregex_iterator(arr.begin(), arr.end(), obj_re); it != std::sregex_iterator(); ++it)
        {
            const std::string obj = (*it)[0].str();
            std::smatch mo;
            int id = -1;
            StrategicLevelFrame::TerritoryResourceState st;
            if (std::regex_search(obj, mo, std::regex("\\\"id\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                id = std::stoi(mo[1].str());
            if (id <= 0) continue;
            if (std::regex_search(obj, mo, std::regex("\\\"total\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                st.total = std::max(0, std::stoi(mo[1].str()));
            if (std::regex_search(obj, mo, std::regex("\\\"remaining\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                st.remaining = std::max(0, std::stoi(mo[1].str()));
            if (std::regex_search(obj, mo, std::regex("\\\"research_percent\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                st.researchPercent = std::clamp(std::stoi(mo[1].str()), 0, 100);
            if (std::regex_search(obj, mo, std::regex("\\\"alloc_accum\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                st.allocAccum = std::clamp(std::stoi(mo[1].str()), 0, 99);
            if (std::regex_search(obj, mo, std::regex("\\\"research_carry\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                st.researchCarry = std::clamp(std::stoi(mo[1].str()), 0, 3);
            territoryResources[id] = st;
        }
    }
    }

    // player object optional (backward compatible)
    std::regex player_obj_re("\"player\"\\s*:\\s*\\{([^}]*)\\}");
    if (std::regex_search(data, m, player_obj_re) && m.size() > 1)
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
    for (auto it = std::sregex_iterator(data.begin(), data.end(), terr_re); it != std::sregex_iterator(); ++it)
    {
        const auto& mm = *it;
        if (mm.size() < 4)
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
    for (auto it = begin; it != end; ++it)
    {
        const auto& match = *it;
        if (match.size() < 4)
            continue;
        LevelData::PlayerUnitAdd entry;
        entry.unit_id = std::stoi(match[1].str());
        entry.count = std::stoi(match[2].str());
        entry.health = std::stoi(match[3].str());
        entry.extra = "-";
        units.push_back(entry);
    }


    // commander generation (optional)
    std::regex gen_re("\"commander_generation\"\\s*:\\s*\\{([^}]*)\\}");
    if (std::regex_search(data, m, gen_re) && m.size() > 1)
    {
        const std::string g = m[1].str();
        (void)ParseJsonIntField(g, "window_start_turn", cmdGenWindowStartTurn);
        (void)ParseJsonIntField(g, "generated_in_window", cmdGenCountInWindow);
    }

    auto parse_commander_array = [&](const char* key, std::vector<StrategicLevelFrame::CommanderRec>& out)
        {
            out.clear();
            std::smatch mm;
            std::regex arr_re(std::string("\"") + key + "\"\\s*:\\s*\\[(.*?)\\]", std::regex_constants::ECMAScript);
            if (!std::regex_search(data, mm, arr_re) || mm.size() < 2)
                return;

            const std::string arr = mm[1].str();
            std::regex item_re("\\{([^}]*)\\}");
            for (auto it = std::sregex_iterator(arr.begin(), arr.end(), item_re); it != std::sregex_iterator(); ++it)
            {
                const std::string obj = (*it)[1].str();
                StrategicLevelFrame::CommanderRec c;
                (void)ParseJsonStringField(obj, "name", c.name);
                (void)ParseJsonIntField(obj, "rank", c.rank);
                if (!c.name.empty())
                    out.push_back(c);
            }
        };

    parse_commander_array("player_commanders", playerCommanders);
    parse_commander_array("available_commanders", availableCommanders);

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
    const std::vector<StrategicLevelFrame::CommanderRec>& playerCommanders,
    const std::vector<StrategicLevelFrame::CommanderRec>& availableCommanders,
    int cmdGenWindowStartTurn,
    int cmdGenCountInWindow,
    bool gameModeEnabled,
    const std::vector<int>& ownedTerritories,
    const std::unordered_map<int, StrategicLevelFrame::TerritoryResourceState>& territoryResources,
    const std::string& timestamp)
{
    std::ofstream f(path);
    if (!f)
        return;

    f << "{\n";
    f << "  \"version\": 1,\n";
    f << "  \"timestamp\": \"" << EscapeJson(timestamp) << "\",\n";
    f << "  \"level_def\": \"" << EscapeJson(level.source_path) << "\",\n";
    f << "  \"turn\": " << turn << ",\n";
    f << "  \"money\": " << money << ",\n";
    f << "  \"research\": " << research << ",\n";
    f << "  \"selected_territory\": " << selected_territory << ",\n";
    f << "  \"game_mode\": " << (gameModeEnabled ? "true" : "false") << ",\n";

    f << "  \"owned_territories\": [";
    for (size_t i = 0; i < ownedTerritories.size(); ++i)
    {
        f << ownedTerritories[i];
        if (i + 1 < ownedTerritories.size())
            f << ", ";
    }
    f << "],\n";

    
    // Research (optional; driven by g_researchPersistSave)
    if (g_researchPersistSave && g_researchPersistSave->progressById && g_researchPersistSave->completed)
    {
        f << "  \"research_state\": {\n";
        f << "    \"active_id\": " << g_researchPersistSave->activeId << ",\n";
        f << "    \"active_index\": " << g_researchPersistSave->activeIndex << ",\n";
        f << "    \"alloc_per_turn\": " << g_researchPersistSave->allocPerTurn << ",\n";

        // progress object: { "0": 12, "1": 3, ... }
        f << "    \"progress\": {";
        bool first = true;
        for (const auto& kv : *g_researchPersistSave->progressById)
        {
            if (!first) f << ", ";
            first = false;
            f << "\\\"" << kv.first << "\\\": " << kv.second;
        }
        f << "},\n";

        // completed array
        f << "    \"completed\": [";
        bool firstC = true;
        for (const auto& id : *g_researchPersistSave->completed)
        {
            if (!firstC) f << ", ";
            firstC = false;
            f << id;
        }
        f << "]\n";
        f << "  },\n";
    }
    else
    {
        f << "  \"research_state\": null,\n";
    }

// Resources per-territory state (optional on load, defaults to 20/20)
    f << "  \"resources\": [\n";
    for (size_t i = 0; i < level.territories.size(); ++i)
    {
        const int tid = level.territories[i].id;
        auto it = territoryResources.find(tid);
        const auto st = (it != territoryResources.end()) ? it->second : StrategicLevelFrame::TerritoryResourceState{};
        f << "    {\"id\": " << tid
          << ", \"total\": " << st.total
          << ", \"remaining\": " << st.remaining
          << ", \"research_percent\": " << st.researchPercent
          << ", \"alloc_accum\": " << st.allocAccum
          << ", \"research_carry\": " << st.researchCarry
          << "}";
        if (i + 1 < level.territories.size())
            f << ",";
        f << "\n";
    }
    f << "  ],\n";

    f << "  \"player\": {"
        << "\"name\": \"" << EscapeJson(player.name) << "\", "
        << "\"rank\": " << player.rank << ", "
        << "\"experience\": " << player.experience << ", "
        << "\"actions\": " << player.actions
        << "},\n";

    // territories
    f << "  \"territories\": [\n";
    for (size_t i = 0; i < level.territories.size(); ++i)
    {
        const auto& t = level.territories[i];
        auto itM = territoryMission.find(t.id);
        auto itL = territoryLaunchCount.find(t.id);
        const std::string mission = (itM != territoryMission.end()) ? itM->second : t.mission;
        const int launches = (itL != territoryLaunchCount.end()) ? itL->second : 0;

        f << "    {\"id\": " << t.id
            << ", \"mission\": \"" << EscapeJson(mission)
            << "\", \"launches\": " << launches << "}";
        if (i + 1 < level.territories.size())
            f << ",";
        f << "\n";
    }
    f << "  ],\n";


    // commanders
    f << "  \"commander_generation\": {"
        << "\"window_start_turn\": " << cmdGenWindowStartTurn << ", "
        << "\"generated_in_window\": " << cmdGenCountInWindow
        << "},\n";

    f << "  \"player_commanders\": [\n";
    for (size_t i = 0; i < playerCommanders.size(); ++i)
    {
        const auto& c = playerCommanders[i];
        f << "    {\"name\": \"" << EscapeJson(c.name) << "\", \"rank\": " << c.rank << "}";
        if (i + 1 < playerCommanders.size())
            f << ",";
        f << "\n";
    }
    f << "  ],\n";

    f << "  \"available_commanders\": [\n";
    for (size_t i = 0; i < availableCommanders.size(); ++i)
    {
        const auto& c = availableCommanders[i];
        f << "    {\"name\": \"" << EscapeJson(c.name) << "\", \"rank\": " << c.rank << "}";
        if (i + 1 < availableCommanders.size())
            f << ",";
        f << "\n";
    }
    f << "  ],\n";

    // units
    f << "  \"units\": [\n";
    for (size_t i = 0; i < units.size(); ++i)
    {
        const auto& u = units[i];
        f << "    {\"unit_id\": " << u.unit_id << ", \"count\": " << u.count << ", \"health\": " << u.health << "}";
        if (i + 1 < units.size())
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
    std::vector<CommanderRec> playerCmds;
    std::vector<CommanderRec> availCmds;
    int windowStart = 1;
    int genCount = 0;
    std::string level_def, ts;
    bool gm = false;
    std::vector<int> owned;
    std::unordered_map<int, TerritoryResourceState> terrRes;

    // Hook research persistence
    ResearchPersistLoadView rlv;
    rlv.activeId = &m_researchActiveId;
    rlv.activeIndex = &m_researchActiveIndex;
    rlv.allocPerTurn = &m_researchAllocPerTurn;
    rlv.progressById = &m_researchProgressById;
    rlv.completed = &m_researchCompleted;
    ResearchPersistLoadView* prevR = g_researchPersistLoad;
    g_researchPersistLoad = &rlv;

    const bool ok = LoadStrategicStateFile(path, m_level, turn, money, research, selected, player, terrM, terrL, units,
        playerCmds, availCmds, windowStart, genCount,
        gm, owned, terrRes,
        &level_def, &ts);
    g_researchPersistLoad = prevR;

    if (ok)
    {
        // Validate that this save matches current level (compare stem)
        const std::string curStem = to_lower(std::filesystem::path(m_level.source_path).stem().string());
        const std::string saveStem = to_lower(std::filesystem::path(level_def).stem().string());

        if (!curStem.empty() && !saveStem.empty() && curStem != saveStem)
        {
            wxLogWarning("[STRATEGIC] Ignoring state file for different level: save='%s' cur='%s'",
                saveStem.c_str(), curStem.c_str());
            return; // keep defaults initialized in ctor
        }

        m_turn = turn;
        m_money = money;
        m_research = research;
        m_selectedTerritory = selected;
        m_player = player;
        m_territoryCurrentMission = std::move(terrM);
        m_territoryLaunchCount = std::move(terrL);
        m_playerUnits = std::move(units);
        m_playerCommanders = std::move(playerCmds);
        m_availableCommanders = std::move(availCmds);
        m_cmdGenWindowStartTurn = windowStart;
        m_cmdGenCountInWindow = genCount;

        m_gameModeEnabled = gm;
        m_ownedTerritories = std::move(owned);

        m_territoryResources = std::move(terrRes);
        // Backfill missing territories to defaults
        for (const auto& tt : m_level.territories)
        {
            if (m_territoryResources.find(tt.id) == m_territoryResources.end())
                m_territoryResources[tt.id] = TerritoryResourceState{};
        }

        // Ensure start territory when loading older saves / empty campaign state.
        if (m_gameModeEnabled && m_ownedTerritories.empty())
            m_ownedTerritories.push_back(PickStartTerritoryIdForGameMode(m_level));

        if (GetMenuBar())
        {
            auto* item = GetMenuBar()->FindItem(ID_MENU_GAME_MODE_TOGGLE);
            if (item) item->Check(m_gameModeEnabled);
        }
        if (m_selectedTerritory >= 0)
            SelectTerritoryById(m_selectedTerritory);
    }

}

void StrategicLevelFrame::SaveStrategicState() const
{
    const auto path = GetStrategicStatePath(m_level);
    ResearchPersistSaveView rsv;
    rsv.activeId = m_researchActiveId;
    rsv.activeIndex = m_researchActiveIndex;
    rsv.allocPerTurn = m_researchAllocPerTurn;
    rsv.progressById = &m_researchProgressById;
    rsv.completed = &m_researchCompleted;

    const ResearchPersistSaveView* prev = g_researchPersistSave;
    g_researchPersistSave = &rsv;

    SaveStrategicStateFile(
        path, m_level, m_turn, m_money, m_research, m_selectedTerritory,
        m_player, m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
        m_playerCommanders, m_availableCommanders, m_cmdGenWindowStartTurn, m_cmdGenCountInWindow,
        m_gameModeEnabled, m_ownedTerritories, m_territoryResources,
        NowIsoLocal());

    g_researchPersistSave = prev;
}

wxString StrategicLevelFrame::GetUnitDisplayName(int unit_id) const
{
    if (m_spellData && m_spellData->units)
    {
        if (auto* unit = m_spellData->units->GetUnit(unit_id))
            return wxString(char2wstringCP895(unit->name));
    }
    return wxString::Format("%d", unit_id);
}

static bool LoadFileBytes(const std::filesystem::path& p, std::vector<unsigned char>& out)
{
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n <= 0) return false;
    out.resize((size_t)n);
    return (bool)f.read((char*)out.data(), n);
}

// --- Strategic background decoding (LEVEL_0X.bin + HMLA__0X.bin + LEVEL_0X.PAL + LEVEL_0X.CLK) ---
// Ported from spellcross_level_tool_v5.py (Pillow/Numpy) into C++/wxWidgets.

static std::filesystem::path FindFileCaseInsensitive(const std::filesystem::path& dir, const std::string& wanted)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return {};

    const std::string w = to_lower(wanted);
    for (const auto& de : fs::directory_iterator(dir, ec))
    {
        if (ec) break;
        if (!de.is_regular_file(ec))
            continue;
        const std::string fn = to_lower(de.path().filename().string());
        if (fn == w)
            return de.path();
    }
    return {};
}

static bool LoadSSDAdjacency(const std::filesystem::path& folder, int levelNum, int territoryMaxId, std::vector<uint32_t>& outAdj)
{
    outAdj.assign(std::max(territoryMaxId + 1, 1), 0u);

    namespace fs = std::filesystem;
    const std::string ssdName = wxString::Format("LEVEL_%02d.SSD", levelNum).ToStdString();

    fs::path pSsd = FindFileCaseInsensitive(folder, ssdName);
    if (pSsd.empty())
        return false;

    std::vector<unsigned char> bytes;
    if (!LoadFileBytes(pSsd, bytes))
        return false;

    // SSD is typically 32 DWORDs (128 bytes). Be tolerant if bigger: read first 128.
    if (bytes.size() < 128)
        return false;

    const int count = 32;
    for (int t = 1; t <= territoryMaxId && t < count; ++t)
    {
        const size_t o = (size_t)t * 4;
        uint32_t v =
            (uint32_t)bytes[o + 0] |
            ((uint32_t)bytes[o + 1] << 8) |
            ((uint32_t)bytes[o + 2] << 16) |
            ((uint32_t)bytes[o + 3] << 24);

        outAdj[t] = v;
    }
    return true;
}


static bool ExpandPaletteTo256(const std::vector<unsigned char>& palBytes, std::array<unsigned char, 256 * 3>& pal256)
{
    pal256.fill(0);
    if (palBytes.size() < 3)
        return false;

    const size_t colors = palBytes.size() / 3;
    if (colors != 32 && colors != 64 && colors != 256)
        return false;

    // Detect VGA 6-bit (0..63) values and scale to 0..255.
    unsigned char maxv = 0;
    for (size_t i = 0; i < colors * 3; ++i)
        maxv = std::max(maxv, palBytes[i]);

    const bool is_vga6 = (maxv <= 63);
    auto to8 = [&](unsigned char v) -> unsigned char {
        return is_vga6 ? (unsigned char)std::min(255, (int)v * 4) : v;
        };

    // Python tool repeats palette to fill 256 entries.
    for (size_t i = 0; i < 256; ++i)
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

    if (clkBytes.size() < 4)
        return false;

    auto rd16 = [&](size_t off) -> unsigned {
        if (off + 1 >= clkBytes.size()) return 0;
        return (unsigned)clkBytes[off] | ((unsigned)clkBytes[off + 1] << 8);
        };

    // NOTE: format observed in python tool: uint16 H, uint16 W
    const unsigned H = rd16(0);
    const unsigned W = rd16(2);
    if (W == 0 || H == 0)
        return false;

    const size_t offsets_off = 4;
    const size_t offsets_size = (size_t)H * 2;
    if (offsets_off + offsets_size > clkBytes.size())
        return false;

    std::vector<unsigned> offsets;
    offsets.reserve(H);
    for (unsigned y = 0; y < H; ++y)
        offsets.push_back(rd16(offsets_off + (size_t)y * 2));

    values.assign((size_t)W * H, 0);

    for (unsigned y = 0; y < H; ++y)
    {
        const unsigned start = offsets[y];
        const unsigned end = (y + 1 < H) ? offsets[y + 1] : (unsigned)clkBytes.size();
        if (start >= clkBytes.size() || end > clkBytes.size() || end <= start)
            continue;

        size_t x = 0;
        for (unsigned i = start; i + 1 < end && x < W; i += 2)
        {
            const unsigned run_len = clkBytes[i];
            const unsigned val = clkBytes[i + 1];
            if (run_len == 0)
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
    if (src.size() == need)
    {
        out = src;
        return true;
    }
    if (src.size() == need + 1)
    {
        // Choose whether to drop first or last byte by comparing how well the outside area
        // compresses to a single key color (matches python tool behavior).
        auto score_drop = [&](bool drop_first) -> size_t
            {
                const unsigned char* p = src.data() + (drop_first ? 1 : 0);
                // count most frequent color on outside (clk==0)
                std::array<size_t, 256> counts{};
                for (size_t i = 0; i < need; ++i)
                {
                    if (i < clkValues.size() && clkValues[i] == 0)
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
    if (src.size() > need)
    {
        out.assign(src.end() - (ptrdiff_t)need, src.end());
        return true;
    }
    return false;
}

static bool MaybeDecompressSpellLZ(const std::vector<unsigned char>& in, std::vector<unsigned char>& out)
{
    out.clear();
    if (in.empty()) return false;

    // Zkus Spellcross LZW decode. Když to není LZ stream, většinou to vrátí prázdno nebo nesmyslnou délku.
    LZWexpand delz(1024 * 1024); // 1MB buffer, strategic mapy jsou typicky do ~300k
    std::vector<uint8_t>& dec = delz.Decode((uint8_t*)in.data(), (uint8_t*)in.data() + in.size());
    if (dec.empty())
        return false;

    out.assign(dec.begin(), dec.end());
    return true;
}

static void StripWHHeaderIfMatches(std::vector<unsigned char>& buf, int W, int H)
{
    if (buf.size() < 4) return;
    const unsigned w = (unsigned)buf[0] | ((unsigned)buf[1] << 8);
    const unsigned h = (unsigned)buf[2] | ((unsigned)buf[3] << 8);
    if ((int)w == W && (int)h == H)
        buf.erase(buf.begin(), buf.begin() + 4);
}

static bool LoadFileBytesMaybeExpandLZ(const std::filesystem::path& path,
    size_t need,
    std::vector<unsigned char>& out)
{
    out.clear();
    if (!LoadFileBytes(path, out))
        return false;

    // Když už to je dost velké, necháme být (raw .bin typicky need nebo need+1).
    if (out.size() >= need)
        return true;

    // Pokud je to menší než need, velmi pravděpodobně je to LZ stream -> zkus expand.
    LZWexpand delz((int)std::max<size_t>(1024 * 1024, need + 64));
    std::vector<uint8_t> decoded = delz.Decode((uint8_t*)out.data(), (uint8_t*)out.data() + out.size());

    if (decoded.empty())
        return false;

    // Po dekompresi čekáme aspoň need (nebo need+něco – header/extra byte).
    if (decoded.size() < need)
        return false;

    out.assign(decoded.begin(), decoded.end());
    return true;
}

static bool BuildStrategicCompositeFromFolder(const std::filesystem::path& folder, int levelNum, wxBitmap& outBmp,
    int* outW = nullptr, int* outH = nullptr, std::vector<unsigned char>* outClk = nullptr,
    bool bakeBorders = true)

{
    namespace fs = std::filesystem;
    outBmp = wxBitmap();
    if (levelNum < 0 || levelNum > 99)
        return false;

    const std::string lvlBIN = wxString::Format("LEVEL_%02d.BIN", levelNum).ToStdString();
    const std::string fogBIN = wxString::Format("HMLA__%02d.BIN", levelNum).ToStdString();

    const std::string lvlLZ = wxString::Format("LEVEL_%02d.LZ", levelNum).ToStdString();
    const std::string fogLZ = wxString::Format("HMLA__%02d.LZ", levelNum).ToStdString();

    const std::string lvlLZ0 = wxString::Format("LEVEL_%02d.LZ0", levelNum).ToStdString();
    const std::string fogLZ0 = wxString::Format("HMLA__%02d.LZ0", levelNum).ToStdString();

    const std::string pal = wxString::Format("LEVEL_%02d.PAL", levelNum).ToStdString();
    const std::string clk = wxString::Format("LEVEL_%02d.CLK", levelNum).ToStdString();

    fs::path pLevel = FindFileCaseInsensitive(folder, lvlBIN);
    if (pLevel.empty()) pLevel = FindFileCaseInsensitive(folder, lvlLZ);
    if (pLevel.empty()) pLevel = FindFileCaseInsensitive(folder, lvlLZ0);

    fs::path pFog = FindFileCaseInsensitive(folder, fogBIN);
    if (pFog.empty()) pFog = FindFileCaseInsensitive(folder, fogLZ);
    if (pFog.empty()) pFog = FindFileCaseInsensitive(folder, fogLZ0);

    fs::path pPal = FindFileCaseInsensitive(folder, pal);
    fs::path pClk = FindFileCaseInsensitive(folder, clk);

    if (pLevel.empty() || pFog.empty() || pPal.empty() || pClk.empty())
        return false;

    std::vector<unsigned char> palBytes, clkBytes;
    if (!LoadFileBytes(pPal, palBytes) || !LoadFileBytes(pClk, clkBytes))
        return false;

    int W = 0, H = 0;
    std::vector<unsigned char> clkValues;
    if (!DecodeCLK(clkBytes, W, H, clkValues))
        return false;

    const size_t need = (size_t)W * (size_t)H;

    // teď teprve načti LEVEL/HMLA – když budou LZ, expandnou se
    std::vector<unsigned char> levelBytes, fogBytes;
    if (!LoadFileBytesMaybeExpandLZ(pLevel, need, levelBytes))
        return false;
    if (!LoadFileBytesMaybeExpandLZ(pFog, need, fogBytes))
        return false;

    std::vector<unsigned char> levelPix, fogPix;
    if (!NormalizeIndexedBuffer(levelBytes, need, clkValues, levelPix))
        return false;
    if (!NormalizeIndexedBuffer(fogBytes, need, clkValues, fogPix))
        return false;

    // 1) LEVEL
    if (!NormalizeIndexedBuffer(levelBytes, need, clkValues, levelPix))
    {
        std::vector<unsigned char> dec;
        if (!MaybeDecompressSpellLZ(levelBytes, dec))
            return false;

        StripWHHeaderIfMatches(dec, W, H);

        if (!NormalizeIndexedBuffer(dec, need, clkValues, levelPix))
            return false;
    }

    // 2) HMLA
    if (!NormalizeIndexedBuffer(fogBytes, need, clkValues, fogPix))
    {
        std::vector<unsigned char> dec;
        if (!MaybeDecompressSpellLZ(fogBytes, dec))
            return false;

        StripWHHeaderIfMatches(dec, W, H);

        if (!NormalizeIndexedBuffer(dec, need, clkValues, fogPix))
            return false;
    }

    std::array<unsigned char, 256 * 3> pal256;
    if (!ExpandPaletteTo256(palBytes, pal256))
        return false;

    // Compose like python tool:
    //  out = fog (darkened), then LEVEL where (clk==0), plus optional region outline.
    const float fog_darken = 0.82f;

    wxImage img(W, H, true);
    img.InitAlpha();

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const size_t i = (size_t)y * W + (size_t)x;
            const bool inside = (clkValues[i] != 0);
            const unsigned char idx = inside ? levelPix[i] : fogPix[i];

            unsigned char r = pal256[(size_t)idx * 3 + 0];
            unsigned char g = pal256[(size_t)idx * 3 + 1];
            unsigned char b = pal256[(size_t)idx * 3 + 2];

            if (!inside)
            {
                r = (unsigned char)std::clamp((int)std::lround((double)r * fog_darken), 0, 255);
                g = (unsigned char)std::clamp((int)std::lround((double)g * fog_darken), 0, 255);
                b = (unsigned char)std::clamp((int)std::lround((double)b * fog_darken), 0, 255);
            }

            img.SetRGB(x, y, r, g, b);
            img.SetAlpha(x, y, 255);
        }

    // Outline (black) where neighboring CLK values differ, limited to inside area.
    // IMPORTANT: Do NOT bake borders into the composite in game mode,
    // because undiscovered territories must not reveal their shapes.
    if (bakeBorders)
    {
        // Outline (black) where neighboring CLK values differ, limited to inside area.
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                const size_t i = (size_t)y * W + (size_t)x;
                if (clkValues[i] == 0)
                    continue;

                bool edge = false;
                if (x > 0 && clkValues[i - 1] != 0 && clkValues[i] != clkValues[i - 1]) edge = true;
                if (y > 0 && clkValues[i - (size_t)W] != 0 && clkValues[i] != clkValues[i - (size_t)W]) edge = true;
                if (edge)
                {
                    img.SetRGB(x, y, 20, 20, 20);
                    img.SetAlpha(x, y, 255);
                }
            }
    }

    outBmp = wxBitmap(img);
    if (outW) *outW = W;
    if (outH) *outH = H;
    if (outClk) *outClk = std::move(clkValues);
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
    if (!LoadFileBytes(lz, lzBytes) || !LoadFileBytes(pal, palBytes))
        return false;

    if (lzBytes.size() < 4)
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
        if (len < 4) return false;
        unsigned tw = rd16(p, 0);
        unsigned th = rd16(p, 2);
        if (tw == 0 || th == 0) return false;
        const size_t need = 4ull + (size_t)tw * (size_t)th;
        if (need > len) return false;
        w = tw; h = th;
        pix = p + 4;
        return true;
        };

    if (!try_parse_raw(src, srcLen))
    {
        LZWexpand delz(256 * 1024);
        raw = delz.Decode((uint8_t*)src, (uint8_t*)src + srcLen);
        if (raw.empty() || !try_parse_raw(raw.data(), raw.size()))
            return false;
    }

    std::array<unsigned char, 256 * 3> pal256;
    if (!ExpandPaletteTo256(palBytes, pal256))
        return false;

    wxImage img((int)w, (int)h, true);
    img.InitAlpha();
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
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
    g_bakeStrategicBorders = !m_gameModeEnabled;

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
        std::regex re("LEVEL[_-]?(\\d{1,2})", std::regex_constants::icase);
        if (std::regex_search(fnU, m, re) && m.size() >= 2)
            levelNum = std::stoi(m[1].str());
    }

    wxBitmap bmp;

    // If we can build the composite (LEVEL + HMLA + PAL + CLK), keep CLK for click-detection.
    bool composite_ok = false;
    int cw = 0, ch = 0;
    std::vector<unsigned char> cclk;

    if (levelNum >= 0)
    {
        // Search in reasonable places: folder of DEF, and a few parents with common subfolders.
        std::vector<fs::path> dirs;
        std::error_code ec;
        fs::path base = defPath.parent_path();
        for (int depth = 0; depth < 8 && !base.empty(); ++depth)
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
        for (const auto& d : dirs)
        {
            if (d.empty()) continue;
            if (!fs::exists(d, ec) || !fs::is_directory(d, ec)) continue;
            bool seen = false;
            for (const auto& u : uniq)
                if (u == d) { seen = true; break; }
            if (!seen) uniq.push_back(d);
        }

        for (const auto& folder : uniq)
        {
            const bool bakeBorders = !m_gameModeEnabled; // debug/editor: true, game mode: false
            if (BuildStrategicCompositeFromFolder(folder, levelNum, bmp, &cw, &ch, &cclk, bakeBorders))
            {
                composite_ok = true;
                m_compositeFolder = folder.string();
                break;
            }
        }
    }

    // Fallback: older simple LZ background (no CLK).
    if (!bmp.IsOk())
        BuildLegacyLZBackgroundFromDef(defPath, bmp);

    if (bmp.IsOk())
    {
        m_bgBitmap = bmp;
        m_hasBg = true;

        if (composite_ok && !cclk.empty() && cw > 0 && ch > 0)
        {
            m_clkValues = std::move(cclk);
            m_clkW = cw;
            m_clkH = ch;
            m_hasClk = ((size_t)m_clkW * (size_t)m_clkH == m_clkValues.size());

            // Hide the territory button grid when region click-detection is available.
            if (m_territoryButtonsPanel)
            {
                m_territoryButtonsPanel->Show(!m_hasClk);
                if (m_mapPanel) m_mapPanel->Layout();
            }


            // Precompute centroid positions for labels / selection marker.
            RebuildTerritoryCentroids();

            // Load SSD adjacency (for game mode visible-neighbors logic)
            int maxId = 0;
            for (const auto& t : m_level.territories) maxId = std::max(maxId, t.id);
            if (!m_compositeFolder.empty())
                LoadSSDAdjacency(std::filesystem::path(m_compositeFolder), levelNum, maxId, m_territoryAdjMask);
            else
                m_territoryAdjMask.assign(std::max(maxId + 1, 1), 0u);

            ApplyTerritoryVisibility();
            MarkOverlayDirty();
        }
    }

    if (m_mapCanvas)
        m_mapCanvas->Refresh();
    else if (m_mapPanel)
        m_mapPanel->Refresh();
}


void StrategicLevelFrame::RebuildTerritoryCentroids()
{
    m_territoryCentroids.clear();

    if (!m_hasClk || m_clkValues.empty() || m_clkW <= 0 || m_clkH <= 0)
        return;

    // Accumulate pixel sums per territory id.
    struct Acc { long long sx = 0; long long sy = 0; long long n = 0; };
    std::unordered_map<int, Acc> acc;
    acc.reserve(std::max<size_t>(16, m_level.territories.size() * 2));

    for (int y = 0; y < m_clkH; ++y)
    {
        const unsigned char* row = &m_clkValues[(size_t)y * (size_t)m_clkW];
        for (int x = 0; x < m_clkW; ++x)
        {
            const int tid = (int)row[x];
            if (tid == 0)
                continue;

            auto& a = acc[tid];
            a.sx += x;
            a.sy += y;
            a.n += 1;
        }
    }

    for (const auto& kv : acc)
    {
        if (kv.second.n <= 0)
            continue;

        const int cx = (int)std::lround((double)kv.second.sx / (double)kv.second.n);
        const int cy = (int)std::lround((double)kv.second.sy / (double)kv.second.n);
        m_territoryCentroids[kv.first] = wxPoint(cx, cy);
    }
}

void StrategicLevelFrame::OnMapPaint(wxPaintEvent& ev)
{
    wxWindow* target = wxDynamicCast(ev.GetEventObject(), wxWindow);
    if (!target)
        target = m_mapCanvas ? (wxWindow*)m_mapCanvas : (wxWindow*)m_mapPanel;
    if (!target)
        return;

    const bool resourcesView = (target == m_resourcesCanvas);

    wxAutoBufferedPaintDC dc(target);
    dc.Clear();

    if (m_hasBg && m_bgBitmap.IsOk())
    {
        int pw, ph;
        target->GetClientSize(&pw, &ph);

        const int bw = m_bgBitmap.GetWidth();
        const int bh = m_bgBitmap.GetHeight();
        if (pw <= 0 || ph <= 0 || bw <= 0 || bh <= 0)
            return;

        // Scale to fit panel while keeping aspect ratio.
        const double sx = (double)pw / (double)bw;
        const double sy = (double)ph / (double)bh;
        const double s = std::min(sx, sy);
        const int dw = std::max(1, (int)std::lround((double)bw * s));
        const int dh = std::max(1, (int)std::lround((double)bh * s));

        // Cache the scaled bitmap so we don't rescale on every paint.
        if (!m_bgBitmapScaled.IsOk() || m_bgScaledW != dw || m_bgScaledH != dh)
        {
            wxImage img = m_bgBitmap.ConvertToImage();
            m_bgBitmapScaled = wxBitmap(img.Scale(dw, dh, wxIMAGE_QUALITY_NEAREST));
            m_bgScaledW = dw;
            m_bgScaledH = dh;
        }

        const int x = (pw - dw) / 2;
        const int y = (ph - dh) / 2;
        dc.DrawBitmap(m_bgBitmapScaled.IsOk() ? m_bgBitmapScaled : m_bgBitmap, x, y, false);

        // Store transform for hit-testing / hover.
        m_lastMapScale = s;
        m_lastMapOffX = x;
        m_lastMapOffY = y;
        m_lastBgW = bw;
        m_lastBgH = bh;

        // Resources view overlay: show only owned territories, green while remaining>0, gray when depleted.
        if (resourcesView && m_hasClk && m_clkW > 0 && m_clkH > 0 && !m_clkValues.empty())
        {
            if (m_overlayDirty || !m_overlayBitmap.IsOk() || m_overlayBitmap.GetWidth() != bw || m_overlayBitmap.GetHeight() != bh)
            {
                wxImage ovImg(bw, bh, true);
                ovImg.InitAlpha();
                // Start fully black – covers background AND areas outside all territories
                std::memset(ovImg.GetData(),  0,    (size_t)bw * (size_t)bh * 3);
                std::memset(ovImg.GetAlpha(), 255,  (size_t)bw * (size_t)bh);

                auto isOwned = [&](int tid) -> bool {
                    return std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid) != m_ownedTerritories.end();
                };

                auto decodeTid = [&](uint8_t v, bool& isBorder) -> int
                {
                    isBorder = false;
                    if (v == 0) return 0;
                    if (v >= 1 && v <= 128) return (int)v;
                    if (v >= 129) { isBorder = true; return (int)(v - 128); }
                    return 0;
                };

                for (int py = 0; py < bh; ++py)
                {
                    for (int px = 0; px < bw; ++px)
                    {
                        const uint8_t v = m_clkValues[(size_t)py * (size_t)bw + (size_t)px];
                        bool isBorder = false;
                        const int tid = decodeTid(v, isBorder);
                        if (tid <= 0) continue;

                        unsigned char r, g, b, a;
                        if (!isOwned(tid))
                        {
                            // Already black from initial fill – no change needed
                            continue;
                        }
                        else
                        {
                            auto it = m_territoryResources.find(tid);
                            TerritoryResourceState st = (it != m_territoryResources.end()) ? it->second : TerritoryResourceState{};
                            const bool depleted = (st.remaining <= 0);
                            const bool selected = (tid == m_selectedTerritory);

                            if (selected)
                            { r = 0xFF; g = 0xF6; b = 0x04; a = 130; }  // yellow highlight
                            else if (depleted)
                            { r = 0x88; g = 0x44; b = 0x44; a = 160; }  // red-grey
                            else
                            { r = 0x10; g = 0xD0; b = 0x10; a = 120; }  // green
                        }

                        ovImg.SetRGB(px, py, r, g, b);
                        ovImg.SetAlpha(px, py, a);
                    }
                }

                m_overlayBitmap = wxBitmap(ovImg);
                m_overlayBitmapScaled = wxBitmap();
                m_overlayScaledW = -1;
                m_overlayScaledH = -1;
                m_overlayDirty = false;
            }

            if (m_overlayBitmap.IsOk())
            {
                if (!m_overlayBitmapScaled.IsOk() || m_overlayScaledW != dw || m_overlayScaledH != dh)
                {
                    wxImage oi = m_overlayBitmap.ConvertToImage();
                    m_overlayBitmapScaled = wxBitmap(oi.Scale(dw, dh, wxIMAGE_QUALITY_NEAREST));
                    m_overlayScaledW = dw;
                    m_overlayScaledH = dh;
                }
                dc.DrawBitmap(m_overlayBitmapScaled.IsOk() ? m_overlayBitmapScaled : m_overlayBitmap, x, y, true);
            }
        }
        // Game mode overlay (fog + visible neighbors + hover highlight)
        else if (m_gameModeEnabled && m_hasClk && m_clkW > 0 && m_clkH > 0 && !m_clkValues.empty())
        {
            // Rebuild visibility if needed (e.g., after loading background)
            if (m_visibleTerritory.empty())
                ApplyTerritoryVisibility();

            // Build base overlay bitmap at background resolution (bw x bh)
            if (m_overlayDirty || !m_overlayBitmap.IsOk() || m_overlayBitmap.GetWidth() != bw || m_overlayBitmap.GetHeight() != bh)
            {
                wxImage ovImg(bw, bh, true);
                ovImg.InitAlpha();

                // InitAlpha() nastaví defaultně alpha=255 (neprůhledné). My chceme defaultně plně průhledné.
                std::memset(ovImg.GetAlpha(), 0, (size_t)bw * (size_t)bh);

                const int maxId = (int)m_visibleTerritory.size() - 1;

                // Helper: decode territory id from CLK byte (interior: 1..N, border: 129..128+N)
                auto decodeTid = [&](uint8_t vv) -> int
                    {
                        if (vv >= 1 && vv <= (uint8_t)maxId) return (int)vv;
                        if (vv >= 129 && vv <= (uint8_t)(128 + maxId)) return (int)vv - 128;
                        return 0;
                    };
                auto isVisibleTid = [&](int t) -> bool
                    {
                        return (t > 0 && t < (int)m_visibleTerritory.size() && m_visibleTerritory[t] != 0);
                    };

                for (int py = 0; py < bh; ++py)
                {
                    // Map bg y -> clk y
                    const int cy = (int)std::floor((double)py * (double)m_clkH / (double)bh);
                    if (cy < 0 || cy >= m_clkH) continue;

                    for (int px = 0; px < bw; ++px)
                    {
                        const int cx = (int)std::floor((double)px * (double)m_clkW / (double)bw);
                        if (cx < 0 || cx >= m_clkW) continue;

                        const size_t cidx = (size_t)cy * (size_t)m_clkW + (size_t)cx;
                        if (cidx >= m_clkValues.size()) continue;

                        const uint8_t v = m_clkValues[cidx];
                        const bool isBorder = (v >= 129);

                        const int tid = decodeTid(v);
                        if (tid <= 0) continue;

                        const bool isVis = isVisibleTid(tid);
                        const bool isOwned = (std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid) != m_ownedTerritories.end());
                        const bool isHover = (m_hoverTerritory == tid);

                        // If this is a border pixel and it borders any UNKNOWN (non-visible) territory,
                        // we must "block" the baked border even if the border pixel belongs to a visible territory.
                        bool borderToUnknown = false;
                        if (m_gameModeEnabled && isBorder && isVis)
                        {
                            auto nt = [&](int nx, int ny) -> int
                                {
                                    if (nx < 0 || ny < 0 || nx >= m_clkW || ny >= m_clkH) return 0;
                                    const size_t ni = (size_t)ny * (size_t)m_clkW + (size_t)nx;
                                    if (ni >= m_clkValues.size()) return 0;
                                    return decodeTid(m_clkValues[ni]);
                                };

                            const int tL = nt(cx - 1, cy);
                            const int tR = nt(cx + 1, cy);
                            const int tU = nt(cx, cy - 1);
                            const int tD = nt(cx, cy + 1);

                            auto unknownOther = [&](int t) -> bool
                                {
                                    if (t == 0) return true;      // outside any territory (background)
                                    if (t == tid) return false;   // same territory
                                    return !isVisibleTid(t);      // different and not visible => unknown
                                };

                            if (unknownOther(tL) || unknownOther(tR) || unknownOther(tU) || unknownOther(tD))
                                borderToUnknown = true;
                        }

                        unsigned char r = 0, g = 0, b = 0, a = 0;

                        // Fog tone: keeps terrain visible but hides borders
                        const unsigned char fogR = 10, fogG = 20, fogB = 10;
                        const unsigned char fogInteriorA = 180;
                        const unsigned char fogBorderA = 160;

                        // 1) Draw borders ONLY where both sides are visible (never towards unknown)
                        if (isBorder && isVis && !borderToUnknown)
                        {
                            r = 20; g = 20; b = 20;
                            a = 255;
                        }
                        // 2) Border that touches unknown -> hide it (do nothing, or gently fog it)
                        else if (borderToUnknown)
                        {
                            // Pokud už nemáš baked borders, můžeš klidně nechat a=0.
                            // Když chceš jemně "utopit" hranu do mlhy, nech fogBorderA:
                            r = fogR; g = fogG; b = fogB;
                            a = fogBorderA;
                        }
                        else if (!isVis)
                        {
                            // Unknown (not discovered): keep terrain visible
                            r = fogR; g = fogG; b = fogB;
                            a = fogInteriorA;
                        }
                        else if (!isOwned)
                        {
                            // visible but not owned: red tint + simple hatch (interior only)
                            r = 200; g = 40; b = 40;
                            a = 70;
                            if (((px + py) / 6) % 2 == 0)
                            {
                                r = 255; g = 80; b = 80;
                                a = 110;
                            }
                        }


                        if (isHover)
                        {
                            // hover highlight (red)
                            r = 178; g = 45; b = 35;
                            a = std::max<unsigned char>(a, 120);
                        }

                        if (a > 0)
                        {
                            ovImg.SetRGB(px, py, r, g, b);
                            ovImg.SetAlpha(px, py, a);
                        }
                    }
                }

                m_overlayBitmap = wxBitmap(ovImg);
                m_overlayBitmapScaled = wxBitmap();
                m_overlayScaledW = -1;
                m_overlayScaledH = -1;
                m_overlayDirty = false;
            }

            // Scale overlay to current draw size and draw it on top
            if (m_overlayBitmap.IsOk())
            {
                if (!m_overlayBitmapScaled.IsOk() || m_overlayScaledW != dw || m_overlayScaledH != dh)
                {
                    wxImage oi = m_overlayBitmap.ConvertToImage();
                    m_overlayBitmapScaled = wxBitmap(oi.Scale(dw, dh, wxIMAGE_QUALITY_NEAREST));
                    m_overlayScaledW = dw;
                    m_overlayScaledH = dh;
                }

                dc.DrawBitmap(m_overlayBitmapScaled.IsOk() ? m_overlayBitmapScaled : m_overlayBitmap, x, y, true);
            }
        }

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
                // In game mode: hide labels for undiscovered territories
                if (m_gameModeEnabled)
                {
                    if (t.id <= 0 || t.id >= (int)m_visibleTerritory.size() || m_visibleTerritory[t.id] == 0)
                        continue;
                }

                int px = 0, py = 0;
                if (!getPx(t, px, py))
                    continue;

                const int tx = x + (int)std::lround((double)px * s);
                const int ty = y + (int)std::lround((double)py * s);

                wxString label;
                if (resourcesView)
                {
                    // Only show owned territories in Resources view
                    if (std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), t.id) == m_ownedTerritories.end())
                        continue;
                    const auto itR = m_territoryResources.find(t.id);
                    const TerritoryResourceState st = (itR != m_territoryResources.end()) ? itR->second : TerritoryResourceState{};
                    label = wxString::Format("%d (%d)", st.total, st.remaining);
                }
                else
                {
                    label = wxString::Format("T%02d", t.id);
                }

                // Tiny shadow for readability.
                dc.SetTextForeground(m_palette.shadow);
                dc.DrawText(label, tx + 1, ty + 1);
                if (resourcesView)
                {
                    const auto itR2 = m_territoryResources.find(t.id);
                    const TerritoryResourceState st2 = (itR2 != m_territoryResources.end()) ? itR2->second : TerritoryResourceState{};
                    dc.SetTextForeground(st2.remaining <= 0 ? m_palette.inactive : m_palette.text);
                }
                else
                {
                    dc.SetTextForeground(m_palette.text);
                }
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
    if (ev.GetActive())
        Raise();
    ev.Skip();
}


// ============================================================
// Statistics page (integrated from former form_strategic.*)
// ============================================================

void StrategicLevelFrame::BuildStatsPage()
{
    if (!m_statsPanel)
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
    overallSizer->Add(addRow(overallBox, "Air units", m_lblAllAirA, m_lblAllAirE), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    overallSizer->Add(addRow(overallBox, "Commanders", m_lblAllCmdA, m_lblAllCmdE), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

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
    levelSizer->Add(addRow(levelBox, "Air units", m_lblLvlAirA, m_lblLvlAirE), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    levelSizer->Add(addRow(levelBox, "Commanders", m_lblLvlCmdA, m_lblLvlCmdE), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    levelBox->SetSizer(levelSizer);
    rootSizer->Add(levelBox, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    // ---- Player box ----
    rootSizer->Add(CreateStrategicLabel(m_statsPanel, "Player", m_fontHeading, m_palette.heading, m_palette.shadow), 0, wxALL, 10);

    auto* playerBox = new wxPanel(m_statsPanel);
    playerBox->SetBackgroundColour(m_palette.background);
    auto* playerSizer = new wxBoxSizer(wxVERTICAL);

    m_lblPlayerName = CreateStrategicLabel(playerBox, "Player - John Alexander", m_fontText, m_palette.text, m_palette.shadow);
    m_lblPlayerRank = CreateStrategicLabel(playerBox, "Rank: 0", m_fontText, m_palette.text, m_palette.shadow);
    m_lblPlayerExp = CreateStrategicLabel(playerBox, "Experience: 0", m_fontText, m_palette.text, m_palette.shadow);
    m_lblPlayerMaxUnits = CreateStrategicLabel(playerBox, "Max units: 0", m_fontText, m_palette.text, m_palette.shadow);
    m_lblPlayerMaxCmds = CreateStrategicLabel(playerBox, "Max commanders: 0", m_fontText, m_palette.text, m_palette.shadow);

    playerSizer->Add(m_lblPlayerName, 0, wxALL, 8);
    playerSizer->Add(m_lblPlayerRank, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    playerSizer->Add(m_lblPlayerExp, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    playerSizer->Add(m_lblPlayerMaxUnits, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    playerSizer->Add(m_lblPlayerMaxCmds, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    playerBox->SetSizer(playerSizer);
    rootSizer->Add(playerBox, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    m_statsPanel->SetSizer(rootSizer);
}

void StrategicLevelFrame::RefreshStatsPage()
{
    if (!m_statsPanel)
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
    const int maxCmds = rec ? rec->max_commanders : 0;
    const int nextExp = FindNextRankExp(m_player.rank);

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
    if (base.empty() || !fs::exists(base, ec))
        base = fs::current_path(ec);
    return wxString::FromUTF8((base / "strategic_stats.json").string());
}

wxString StrategicLevelFrame::FindHodnostiDefPath() const
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

void StrategicLevelFrame::LoadRanksTable()
{
    if (!m_ranks.empty())
        return;

    wxString p = FindHodnostiDefPath();
    if (p.empty())
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
    if (!f)
        return;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty())
        return;

    std::regex re(R"(DefineCommander\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*\))");
    for (auto it = std::sregex_iterator(data.begin(), data.end(), re); it != std::sregex_iterator(); ++it)
    {
        const auto& m = *it;
        if (m.size() < 6)
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
    if (!f)
        return;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty())
        return;

    auto extractObj = [&](const char* section, const char* side, std::string& outObj) -> bool
        {
            std::regex re(std::string("\"") + section + "\"\\s*:\\s*\\{([^}]*)\\}");
            std::smatch m;
            if (!std::regex_search(data, m, re) || m.size() < 2)
                return false;
            std::string sec = m[1].str();

            std::regex re2(std::string("\"") + side + "\"\\s*:\\s*\\{([^}]*)\\}");
            if (!std::regex_search(sec, m, re2) || m.size() < 2)
                return false;
            outObj = m[1].str();
            return true;
        };

    std::string obj;
    if (extractObj("all", "alliance", obj)) ReadLossBlockFromObj_StrategicLevel(obj, m_lossStats.alliance_all);
    if (extractObj("all", "enemy", obj))    ReadLossBlockFromObj_StrategicLevel(obj, m_lossStats.enemy_all);

    if (extractObj("level", "alliance", obj)) ReadLossBlockFromObj_StrategicLevel(obj, m_lossStats.alliance_level);
    if (extractObj("level", "enemy", obj))    ReadLossBlockFromObj_StrategicLevel(obj, m_lossStats.enemy_level);
}

// ---------------- Rank helpers ----------------

void StrategicLevelFrame::RecomputePlayerRank()
{
    int best = 0;
    for (const auto& r : m_ranks)
    {
        if (m_player.experience >= r.exp_required && m_player.actions >= r.actions_required)
            best = std::max(best, r.rank);
    }
    m_player.rank = best;
}

const StrategicLevelFrame::CommanderRankRec* StrategicLevelFrame::FindRankRec(int rank) const
{
    for (const auto& r : m_ranks)
        if (r.rank == rank)
            return &r;
    return nullptr;
}

int StrategicLevelFrame::FindNextRankExp(int current_rank) const
{
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

wxString StrategicLevelFrame::GetRankNameCz(int rank) const
{
    switch (rank)
    {
    case 0: return "Lieutenant";
    case 1: return "First Lieutenant";
    case 2: return "Captain";
    case 3: return "Major";
    case 4: return "Lieutenant Colonel";
    case 5: return "Colonel";
    case 6: return "Major General";
    case 7: return "Lieutenant General";
    case 8: return "General of the Army";
    default: return wxString::Format("Rank %d", rank);
    }
}
