#include "form_mmenu.h"

#include <wx/dcbuffer.h>
#include <wx/frame.h>
#include <wx/log.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#include "LZ_spell.h"

namespace {

std::string to_lower(std::string s)
{
    for (char& ch : s)
        ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool LoadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& out)
{
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n <= 0) return false;
    out.resize(static_cast<size_t>(n));
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), n));
}

std::filesystem::path FindFileCaseInsensitive(const std::filesystem::path& dir, const std::string& wanted)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return {};
    const std::string w = to_lower(wanted);
    for (const auto& de : fs::directory_iterator(dir, ec))
    {
        if (ec) break;
        if (!de.is_regular_file(ec)) continue;
        const std::string fn = to_lower(de.path().filename().string());
        if (fn == w) return de.path();
    }
    return {};
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

    for (const auto& dir : candidates)
    {
        auto found = FindFileCaseInsensitive(dir, filename);
        if (!found.empty())
            return found;
    }
    return {};
}

bool ExpandPaletteTo256(const std::vector<unsigned char>& palBytes, std::array<unsigned char, 256 * 3>& pal256)
{
    pal256.fill(0);
    if (palBytes.size() < 3) return false;

    const size_t colors = palBytes.size() / 3;
    if (colors != 32 && colors != 64 && colors != 256) return false;

    unsigned char maxv = 0;
    for (size_t i = 0; i < colors * 3; ++i) maxv = std::max(maxv, palBytes[i]);
    const bool is_vga6 = (maxv <= 63);

    auto to8 = [&](unsigned char v) -> unsigned char {
        return is_vga6 ? (unsigned char)std::min(255, (int)v * 4) : v;
    };

    for (size_t i = 0; i < 256; ++i)
    {
        const size_t src = (i % colors) * 3;
        pal256[i * 3 + 0] = to8(palBytes[src + 0]);
        pal256[i * 3 + 1] = to8(palBytes[src + 1]);
        pal256[i * 3 + 2] = to8(palBytes[src + 2]);
    }
    return true;
}

struct DecodedIndexed
{
    int w = 0;
    int h = 0;
    std::vector<unsigned char> pixels;
};

bool DecodeIndexedMaybeHeader(const std::vector<unsigned char>& src, DecodedIndexed& out)
{
    out = {};
    if (src.size() < 4) return false;

    auto rd16 = [&](size_t off) -> unsigned {
        return (unsigned)src[off] | ((unsigned)src[off + 1] << 8);
    };

    unsigned w = rd16(0);
    unsigned h = rd16(2);
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;

    const size_t need = (size_t)w * (size_t)h;
    if (src.size() < 4 + need) return false;

    out.w = (int)w;
    out.h = (int)h;
    out.pixels.assign(src.begin() + 4, src.begin() + 4 + need);
    return true;
}

bool GuessDimsFromSize(const std::vector<unsigned char>& pixels, DecodedIndexed& out, std::initializer_list<int> widths)
{
    out = {};
    for (int w : widths)
    {
        if (w <= 0) continue;
        if (pixels.size() % (size_t)w != 0) continue;
        int h = (int)(pixels.size() / (size_t)w);
        if (h <= 0 || h > 480) continue;
        out.w = w; out.h = h; out.pixels = pixels;
        return true;
    }
    return false;
}

wxBitmap MakeBitmapFromIndexed(const DecodedIndexed& d, const std::array<unsigned char, 256 * 3>& pal256, bool idx0Transparent)
{
    if (d.w <= 0 || d.h <= 0 || d.pixels.size() < (size_t)d.w * (size_t)d.h)
        return wxBitmap();

    wxImage img(d.w, d.h, true);
    if (idx0Transparent)
        img.InitAlpha();

    for (int y = 0; y < d.h; ++y)
    {
        for (int x = 0; x < d.w; ++x)
        {
            const unsigned char idx = d.pixels[(size_t)y * d.w + x];
            img.SetRGB(x, y,
                pal256[(size_t)idx * 3 + 0],
                pal256[(size_t)idx * 3 + 1],
                pal256[(size_t)idx * 3 + 2]);

            if (idx0Transparent)
                img.SetAlpha(x, y, (idx == 0) ? 0 : 255);
        }
    }
    return wxBitmap(img);
}

bool DecodeMainMenuPixels(const std::vector<unsigned char>& src, std::vector<unsigned char>& out, int w, int h)
{
    const size_t need = (size_t)w * (size_t)h;

    if (src.size() == need)
    {
        out = src;
        return true;
    }

    if (src.size() >= 4)
    {
        auto rd16 = [&](size_t off) -> unsigned {
            return (unsigned)src[off] | ((unsigned)src[off + 1] << 8);
        };
        unsigned tw = rd16(0);
        unsigned th = rd16(2);
        if (tw == (unsigned)w && th == (unsigned)h && src.size() >= 4 + need)
        {
            out.assign(src.begin() + 4, src.begin() + 4 + need);
            return true;
        }
    }
    return false;
}

} // namespace

FormMainMenu::FormMainMenu(wxPanel* parent,
    wxWindowID win_id,
    SpellMap* spell_map,
    std::function<void(FormMainMenuAction)> action_cb)
{
    m_spell_map = spell_map;
    m_spelldata = spell_map ? spell_map->spelldata : nullptr;
    m_action_cb = std::move(action_cb);
    m_hover_index = -1;

    m_panel = wxBitmap();
    m_panel_pos = wxPoint(0, 0);
    m_panel_size = wxSize(0, 0);
    m_pal256_ok = false;
    m_pal256.fill(0);

    LoadBackground();
    LoadPanel();
    BuildMenuItems();

    const wxSize size = m_bg_size.IsFullySpecified() ? m_bg_size : wxSize(640, 480);
    wxPoint pos = { (parent->GetSize().x - size.x) / 2, (parent->GetSize().y - size.y) / 2 };

    long style = wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX);
    form = new wxFrame(parent, win_id, "Spellcross reloaded alpha", pos, size, style);
    form->SetBackgroundStyle(wxBG_STYLE_PAINT);
    form->SetDoubleBuffered(true);

    LayoutMenuItems(size);

    form->Bind(wxEVT_CLOSE_WINDOW, &FormMainMenu::OnClose, this);
    form->Bind(wxEVT_PAINT, &FormMainMenu::OnPaint, this);
    form->Bind(wxEVT_MOTION, &FormMainMenu::OnMouseMove, this);
    form->Bind(wxEVT_LEAVE_WINDOW, &FormMainMenu::OnMouseLeave, this);
    form->Bind(wxEVT_LEFT_UP, &FormMainMenu::OnMouseClick, this);
    form->Bind(wxEVT_KEY_DOWN, &FormMainMenu::OnKeyDown, this);

    form->Show();
}

FormMainMenu::~FormMainMenu()
{
    if (form)
    {
        // Unbind all handlers so no events fire on the dead object
        form->Unbind(wxEVT_CLOSE_WINDOW, &FormMainMenu::OnClose, this);
        form->Unbind(wxEVT_PAINT, &FormMainMenu::OnPaint, this);
        form->Unbind(wxEVT_MOTION, &FormMainMenu::OnMouseMove, this);
        form->Unbind(wxEVT_LEAVE_WINDOW, &FormMainMenu::OnMouseLeave, this);
        form->Unbind(wxEVT_LEFT_UP, &FormMainMenu::OnMouseClick, this);
        form->Unbind(wxEVT_KEY_DOWN, &FormMainMenu::OnKeyDown, this);
        form->Destroy();
        form = nullptr;
    }
}

bool FormMainMenu::LoadBackground()
{
    m_background = wxBitmap();
    m_bg_size = wxSize(640, 480);
    m_pal256_ok = false;

    if (!m_spelldata)
        return false;

    namespace fs = std::filesystem;
    fs::path root = fs::path(m_spelldata->spell_data_root);

    std::vector<unsigned char> lzBytes;
    std::vector<unsigned char> palBytes;

    fs::path lzPath = FindMenuFile(root, "MAINMENU.LZ");
    fs::path rawPath = FindMenuFile(root, "MAINMENU.BIN");
    fs::path palPath = FindMenuFile(root, "MAINMENU.PAL");

    if (palPath.empty())
        return false;
    if (lzPath.empty() && rawPath.empty())
        return false;

    if (!lzPath.empty())
    {
        if (!LoadFileBytes(lzPath, lzBytes))
            return false;
    }
    else if (!LoadFileBytes(rawPath, lzBytes))
        return false;

    if (!LoadFileBytes(palPath, palBytes))
        return false;

    std::array<unsigned char, 256 * 3> pal256;
    if (!ExpandPaletteTo256(palBytes, pal256))
        return false;

    // store palette for other layers
    m_pal256 = pal256;
    m_pal256_ok = true;

    std::vector<unsigned char> pixels;
    const int width = 640, height = 480;

    if (!DecodeMainMenuPixels(lzBytes, pixels, width, height))
    {
        LZWexpand delz(1024 * 1024);
        std::vector<uint8_t> decoded = delz.Decode((uint8_t*)lzBytes.data(), (uint8_t*)lzBytes.data() + lzBytes.size());
        std::vector<unsigned char> decoded_uc(decoded.begin(), decoded.end());
        if (!DecodeMainMenuPixels(decoded_uc, pixels, width, height))
            return false;
    }

    DecodedIndexed d;
    d.w = width; d.h = height; d.pixels = std::move(pixels);
    m_background = MakeBitmapFromIndexed(d, m_pal256, false);

    if (m_background.IsOk())
    {
        m_bg_size = m_background.GetSize();
        return true;
    }
    return false;
}

bool FormMainMenu::LoadPanel()
{
    m_panel = wxBitmap();
    m_panel_size = wxSize(0, 0);

    if (!m_spelldata || !m_pal256_ok)
        return false;

    namespace fs = std::filesystem;
    fs::path root = fs::path(m_spelldata->spell_data_root);

    fs::path lzPath = FindMenuFile(root, "MAINM_BG.LZ");
    fs::path rawPath = FindMenuFile(root, "MAINM_BG.BIN");
    if (lzPath.empty() && rawPath.empty())
        return false;

    std::vector<unsigned char> bytes;
    if (!lzPath.empty())
    {
        if (!LoadFileBytes(lzPath, bytes))
            return false;
        // try decode
        LZWexpand delz(512 * 1024);
        std::vector<uint8_t> decoded = delz.Decode((uint8_t*)bytes.data(), (uint8_t*)bytes.data() + bytes.size());
        bytes.assign(decoded.begin(), decoded.end());
    }
    else
    {
        if (!LoadFileBytes(rawPath, bytes))
            return false;
    }

    DecodedIndexed d;
    if (!DecodeIndexedMaybeHeader(bytes, d))
    {
        // Known Spellcross MAINM_BG
        if (bytes.size() == (size_t)255 * 237)
        {
            d.w = 255; d.h = 237; d.pixels = bytes;
        }
        else
        {
            // try common widths, including 255
            if (!GuessDimsFromSize(bytes, d, {255, 510, 640, 512, 480, 400, 360, 320}))
                return false;
        }
    }

    m_panel = MakeBitmapFromIndexed(d, m_pal256, true);
    if (!m_panel.IsOk())
        return false;

    m_panel_size = m_panel.GetSize();

    // Position: shifted down and left from center
    const int bgw = m_bg_size.x > 0 ? m_bg_size.x : 640;
    const int bgh = m_bg_size.y > 0 ? m_bg_size.y : 480;

    int x = (bgw - m_panel_size.x) / 2;
    int y = (bgh - m_panel_size.y) / 2;
    
    // Shift left and down
    x -= 20;  // posun doleva (snížení x)
    y += 79;  // posun dolů (zvýšení y)
    
    x = std::max(0, x);
    y = std::max(0, y);

    m_panel_pos = wxPoint(x, y);
    return true;
}

void FormMainMenu::BuildMenuItems()
{
    m_items.clear();

    m_items.push_back({ "nová hra", FormMainMenuAction::NewGame, wxRect() });
    m_items.push_back({ "pokračovat", FormMainMenuAction::Continue, wxRect() });
    m_items.push_back({ "načíst hru", FormMainMenuAction::LoadGame, wxRect() });
    m_items.push_back({ "credits", FormMainMenuAction::Credits, wxRect() });
    m_items.push_back({ "intro", FormMainMenuAction::Intro, wxRect() });
    m_items.push_back({ "konec", FormMainMenuAction::Exit, wxRect() });
}

void FormMainMenu::LayoutMenuItems(const wxSize& clientSize)
{
    if (m_items.empty())
        return;

    if (m_panel.IsOk() && m_panel_size.x > 0 && m_panel_size.y > 0)
    {
        // Slots inside MAINM_BG (255x237). Offsets relative to panel top-left.
        const int panel_x = m_panel_pos.x;
        const int panel_y = m_panel_pos.y;

        const int btn_w = 210;
        const int btn_h = 26;
        const int btn_x = panel_x + 45;

        // Y-centers of the 6 button slots within the panel image
        const int centers_y[6] = { 30, 66, 100, 136, 170, 206 };

        for (size_t i = 0; i < m_items.size() && i < 6; ++i)
        {
            const int cy = panel_y + centers_y[i];
            m_items[i].rect = wxRect(btn_x, cy - btn_h / 2, btn_w, btn_h);
        }
        return;
    }

    // Fallback: center list in window
    const int w = clientSize.x;
    const int h = clientSize.y;

    const int btn_w = 235;
    const int btn_h = 30;
    const int gap = 10;

    const int x = (w - btn_w) / 2 + 4;

    const int area_top = 175;
    const int area_bottom = h - 25;
    const int area_h = std::max(1, area_bottom - area_top);

    const int total_h = (int)m_items.size() * btn_h + ((int)m_items.size() - 1) * gap;
    int y = area_top + std::max(0, (area_h - total_h) / 2);

    for (auto& it : m_items)
    {
        it.rect = wxRect(x, y, btn_w, btn_h);
        y += btn_h + gap;
    }
}

void FormMainMenu::TriggerAction(int index)
{
    if (index < 0 || index >= (int)m_items.size())
        return;

    if (m_action_cb)
        m_action_cb(m_items[(size_t)index].action);
}

void FormMainMenu::OnClose(wxCloseEvent& ev)
{
    form->DeletePendingEvents();
    wxPostEvent(form->GetParent(), ev);
}

void FormMainMenu::OnPaint(wxPaintEvent& event)
{
    wxAutoBufferedPaintDC dc(form);

    if (m_background.IsOk())
        dc.DrawBitmap(m_background, 0, 0, false);
    else
    {
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();
    }

    if (m_panel.IsOk())
        dc.DrawBitmap(m_panel, m_panel_pos.x, m_panel_pos.y, true);

    const wxSize cs = form->GetClientSize();
    LayoutMenuItems(cs);

    // (Optional) debug: draw hover rect highlight
    if (m_hover_index >= 0 && m_hover_index < (int)m_items.size())
    {
        dc.SetPen(wxPen(wxColour(255, 255, 0), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(m_items[(size_t)m_hover_index].rect);
    }
}

void FormMainMenu::OnMouseMove(wxMouseEvent& event)
{
    const wxPoint p = event.GetPosition();

    int hit = -1;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].rect.Contains(p))
        {
            hit = (int)i;
            break;
        }
    }

    if (hit != m_hover_index)
    {
        m_hover_index = hit;
        form->Refresh(false);
    }

    event.Skip();
}

void FormMainMenu::OnMouseLeave(wxMouseEvent& event)
{
    if (m_hover_index != -1)
    {
        m_hover_index = -1;
        form->Refresh(false);
    }
    event.Skip();
}

void FormMainMenu::OnMouseClick(wxMouseEvent& event)
{
    if (m_hover_index >= 0)
        TriggerAction(m_hover_index);
    event.Skip();
}

void FormMainMenu::OnKeyDown(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_ESCAPE)
    {
        if (form) form->Close();
        return;
    }
    event.Skip();
}
