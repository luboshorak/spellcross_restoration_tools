#include "form_mmenu.h"

#include <wx/dcbuffer.h>
#include <wx/settings.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "LZ_spell.h"
#include "other.h"

namespace {

std::string to_lower(std::string s)
{
    for(char& ch : s)
        ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool LoadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& out)
{
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if(!f)
        return false;
    f.seekg(0, std::ios::end);
    std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    if(n <= 0)
        return false;
    out.resize(static_cast<size_t>(n));
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), n));
}

std::filesystem::path FindFileCaseInsensitive(const std::filesystem::path& dir, const std::string& wanted)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if(!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return {};

    const std::string w = to_lower(wanted);
    for(const auto& de : fs::directory_iterator(dir, ec))
    {
        if(ec)
            break;
        if(!de.is_regular_file(ec))
            continue;
        const std::string fn = to_lower(de.path().filename().string());
        if(fn == w)
            return de.path();
    }
    return {};
}

bool ExpandPaletteTo256(const std::vector<unsigned char>& palBytes, std::array<unsigned char, 256 * 3>& pal256)
{
    pal256.fill(0);
    if(palBytes.size() < 3)
        return false;

    const size_t colors = palBytes.size() / 3;
    if(colors != 32 && colors != 64 && colors != 256)
        return false;

    unsigned char maxv = 0;
    for(size_t i = 0; i < colors * 3; ++i)
        maxv = std::max(maxv, palBytes[i]);

    const bool is_vga6 = (maxv <= 63);
    auto to8 = [&](unsigned char v) -> unsigned char {
        return is_vga6 ? (unsigned char)std::min(255, (int)v * 4) : v;
    };

    for(size_t i = 0; i < 256; ++i)
    {
        const size_t src = (i % colors) * 3;
        pal256[i * 3 + 0] = to8(palBytes[src + 0]);
        pal256[i * 3 + 1] = to8(palBytes[src + 1]);
        pal256[i * 3 + 2] = to8(palBytes[src + 2]);
    }
    return true;
}

std::filesystem::path FindMenuFile(const std::filesystem::path& root, const std::string& filename)
{
    const std::vector<std::filesystem::path> candidates = {
        root,
        root / "DATA",
        root / "COMMON",
        root / "DATA" / "COMMON",
        root / "CD",
        root / "DATA" / "CD",
    };

    for(const auto& dir : candidates)
    {
        auto found = FindFileCaseInsensitive(dir, filename);
        if(!found.empty())
            return found;
    }
    return {};
}

bool DecodeMainMenuPixels(const std::vector<unsigned char>& src, std::vector<unsigned char>& out, int w, int h)
{
    const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h);
    if(src.size() == need)
    {
        out = src;
        return true;
    }

    if(src.size() >= 4)
    {
        auto rd16 = [&](size_t off) -> unsigned {
            return static_cast<unsigned>(src[off]) | (static_cast<unsigned>(src[off + 1]) << 8);
        };
        unsigned tw = rd16(0);
        unsigned th = rd16(2);
        if(tw == static_cast<unsigned>(w) && th == static_cast<unsigned>(h) && src.size() >= 4 + need)
        {
            out.assign(src.begin() + 4, src.begin() + 4 + static_cast<ptrdiff_t>(need));
            return true;
        }
    }

    return false;
}

}

FormMainMenu::FormMainMenu(wxPanel* parent,
                           wxWindowID win_id,
                           SpellMap* spell_map,
                           std::function<void(FormMainMenuAction)> action_cb)
{
    m_spell_map = spell_map;
    m_spelldata = spell_map ? spell_map->spelldata : nullptr;
    m_action_cb = std::move(action_cb);
    m_hover_index = -1;

    LoadBackground();
    BuildMenuItems();

    const wxSize size = m_bg_size.IsFullySpecified() ? m_bg_size : wxSize(640, 480);
    wxPoint pos = {(parent->GetSize().x - size.x) / 2, (parent->GetSize().y - size.y) / 2};
    form = new wxPanel(parent, win_id, pos, size, wxBORDER_NONE | wxFRAME_FLOAT_ON_PARENT | wxBG_STYLE_PAINT);
    form->SetBackgroundStyle(wxBG_STYLE_PAINT);
    form->SetDoubleBuffered(true);
    form->SetFocus();

    form->Bind(wxEVT_CLOSE_WINDOW, &FormMainMenu::OnClose, this);
    form->Bind(wxEVT_PAINT, &FormMainMenu::OnPaint, this);
    form->Bind(wxEVT_MOTION, &FormMainMenu::OnMouseMove, this);
    form->Bind(wxEVT_LEAVE_WINDOW, &FormMainMenu::OnMouseLeave, this);
    form->Bind(wxEVT_LEFT_UP, &FormMainMenu::OnMouseClick, this);
    form->Bind(wxEVT_KEY_UP, &FormMainMenu::OnKeyDown, this);

    form->Show();
}

FormMainMenu::~FormMainMenu()
{
    form->Destroy();
}

bool FormMainMenu::LoadBackground()
{
    m_background = wxBitmap();
    m_bg_size = wxSize(640, 480);

    if(!m_spelldata)
        return false;

    namespace fs = std::filesystem;
    fs::path root = fs::path(m_spelldata->spell_data_root);

    std::vector<unsigned char> lzBytes;
    std::vector<unsigned char> palBytes;

    fs::path lzPath = FindMenuFile(root, "MAINMENU.LZ");
    fs::path rawPath = FindMenuFile(root, "MAINMENU.BIN");
    fs::path palPath = FindMenuFile(root, "MAINMENU.PAL");

    if(palPath.empty())
        return false;

    if(lzPath.empty() && rawPath.empty())
        return false;

    if(!lzPath.empty())
    {
        if(!LoadFileBytes(lzPath, lzBytes))
            return false;
    }
    else if(!LoadFileBytes(rawPath, lzBytes))
        return false;

    if(!LoadFileBytes(palPath, palBytes))
        return false;

    if(lzBytes.empty() && !rawPath.empty())
    {
        if(!LoadFileBytes(rawPath, lzBytes))
            return false;
    }

    std::vector<unsigned char> pixels;
    const int width = 640;
    const int height = 480;

    if(!DecodeMainMenuPixels(lzBytes, pixels, width, height))
    {
        LZWexpand delz(512 * 1024);
        std::vector<uint8_t> decoded = delz.Decode((uint8_t*)lzBytes.data(), (uint8_t*)lzBytes.data() + lzBytes.size());
        std::vector<unsigned char> decoded_uc(decoded.begin(), decoded.end());
        if(!DecodeMainMenuPixels(decoded_uc, pixels, width, height))
            return false;
    }

    std::array<unsigned char, 256 * 3> pal256;
    if(!ExpandPaletteTo256(palBytes, pal256))
        return false;

    wxImage img(width, height, true);
    for(int y = 0; y < height; ++y)
    {
        for(int x = 0; x < width; ++x)
        {
            const unsigned char idx = pixels[static_cast<size_t>(y) * width + x];
            img.SetRGB(x, y,
                pal256[(size_t)idx * 3 + 0],
                pal256[(size_t)idx * 3 + 1],
                pal256[(size_t)idx * 3 + 2]);
        }
    }

    m_background = wxBitmap(img);
    if(m_background.IsOk())
    {
        m_bg_size = m_background.GetSize();
        return true;
    }

    return false;
}

void FormMainMenu::BuildMenuItems()
{
    m_items.clear();
    m_items.push_back({"New game", FormMainMenuAction::NewGame, wxRect()});
    m_items.push_back({"Continue", FormMainMenuAction::Continue, wxRect()});
    m_items.push_back({"Load game", FormMainMenuAction::LoadGame, wxRect()});
    m_items.push_back({"Credits", FormMainMenuAction::Credits, wxRect()});
    m_items.push_back({"Intro", FormMainMenuAction::Intro, wxRect()});
    m_items.push_back({"End", FormMainMenuAction::Exit, wxRect()});
}

void FormMainMenu::TriggerAction(int index)
{
    if(index < 0 || index >= static_cast<int>(m_items.size()))
        return;

    if(m_action_cb)
        m_action_cb(m_items[index].action);
}

void FormMainMenu::OnClose(wxCloseEvent& ev)
{
    form->DeletePendingEvents();
    wxPostEvent(form->GetParent(), ev);
}

void FormMainMenu::OnPaint(wxPaintEvent& event)
{
    wxAutoBufferedPaintDC dc(form);
    if(m_background.IsOk())
    {
        dc.Clear();
        dc.DrawBitmap(m_background, wxPoint(0, 0));
    }
    else
    {
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();
    }

    wxFont font(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    dc.SetFont(font);

    const int item_count = static_cast<int>(m_items.size());
    const int line_gap = 10;
    int total_h = 0;
    int max_w = 0;

    for(const auto& item : m_items)
    {
        int w = 0, h = 0;
        dc.GetTextExtent(item.label, &w, &h);
        total_h += h + line_gap;
        max_w = std::max(max_w, w);
    }
    if(item_count > 0)
        total_h -= line_gap;

    const int start_y = std::max(20, (form->GetSize().y - total_h) / 2);
    int y = start_y;

    for(size_t i = 0; i < m_items.size(); ++i)
    {
        const auto& item = m_items[i];
        int w = 0, h = 0;
        dc.GetTextExtent(item.label, &w, &h);
        int x = (form->GetSize().x - w) / 2;
        wxRect rect(x - 10, y - 4, w + 20, h + 8);
        m_items[i].rect = rect;

        if(static_cast<int>(i) == m_hover_index)
            dc.SetTextForeground(wxColour(255, 215, 0));
        else
            dc.SetTextForeground(wxColour(255, 255, 255));

        dc.DrawText(item.label, x, y);
        y += h + line_gap;
    }
}

void FormMainMenu::OnMouseMove(wxMouseEvent& event)
{
    const wxPoint pos = event.GetPosition();
    int new_hover = -1;
    for(size_t i = 0; i < m_items.size(); ++i)
    {
        if(m_items[i].rect.Contains(pos))
        {
            new_hover = static_cast<int>(i);
            break;
        }
    }

    if(new_hover != m_hover_index)
    {
        m_hover_index = new_hover;
        form->Refresh();
    }

    if(m_hover_index >= 0)
        form->SetCursor(wxCursor(wxCURSOR_HAND));
    else
        form->SetCursor(wxCursor(wxCURSOR_ARROW));
}

void FormMainMenu::OnMouseLeave(wxMouseEvent& event)
{
    if(m_hover_index != -1)
    {
        m_hover_index = -1;
        form->Refresh();
    }
    form->SetCursor(wxCursor(wxCURSOR_ARROW));
    event.Skip();
}

void FormMainMenu::OnMouseClick(wxMouseEvent& event)
{
    if(event.GetButton() != wxMOUSE_BTN_LEFT)
        return;

    if(m_hover_index < 0)
        return;

    TriggerAction(m_hover_index);
}

void FormMainMenu::OnKeyDown(wxKeyEvent& event)
{
    if(event.GetKeyCode() == WXK_ESCAPE)
        form->Close();
}
