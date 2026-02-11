#include "form_mmenu.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/log.h>
#include <wx/frame.h>
#include <wx/filefn.h>


#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>

#include "LZ_spell.h"
#include "other.h"

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
        if (!f)
            return false;

        f.seekg(0, std::ios::end);
        std::streamsize n = f.tellg();
        f.seekg(0, std::ios::beg);

        if (n <= 0)
            return false;

        out.resize(static_cast<size_t>(n));
        return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), n));
    }

    std::filesystem::path FindFileCaseInsensitive(const std::filesystem::path& dir, const std::string& wanted)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
            return {};

        const std::string w = to_lower(wanted);
        for (const auto& de : fs::directory_iterator(dir, ec))
        {
            if (ec)
                break;
            if (!de.is_regular_file(ec))
                continue;

            const std::string fn = to_lower(de.path().filename().string());
            if (fn == w)
                return de.path();
        }
        return {};
    }

    bool ExpandPaletteTo256(const std::vector<unsigned char>& palBytes, std::array<unsigned char, 256 * 3>& pal256)
    {
        pal256.fill(0);
        if (palBytes.size() < 3)
            return false;

        const size_t colors = palBytes.size() / 3;
        if (colors != 32 && colors != 64 && colors != 256)
            return false;

        unsigned char maxv = 0;
        for (size_t i = 0; i < colors * 3; ++i)
            maxv = std::max(maxv, palBytes[i]);

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

    bool DecodeMainMenuPixels(const std::vector<unsigned char>& src, std::vector<unsigned char>& out, int w, int h)
    {
        const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h);

        // raw indexed image without header
        if (src.size() == need)
        {
            out = src;
            return true;
        }

        // some dumps include 16-bit little-endian width/height header
        if (src.size() >= 4)
        {
            auto rd16 = [&](size_t off) -> unsigned {
                return static_cast<unsigned>(src[off]) | (static_cast<unsigned>(src[off + 1]) << 8);
                };
            unsigned tw = rd16(0);
            unsigned th = rd16(2);
            if (tw == static_cast<unsigned>(w) && th == static_cast<unsigned>(h) && src.size() >= 4 + need)
            {
                out.assign(src.begin() + 4, src.begin() + 4 + static_cast<ptrdiff_t>(need));
                return true;
            }
        }

        return false;
    }


    bool DecodeIndexedWithOptionalHeader(const std::vector<unsigned char>& src,
        std::vector<unsigned char>& out_pixels,
        int& out_w, int& out_h)
    {
        out_pixels.clear();
        out_w = 0; out_h = 0;

        // header w16,h16 + pixels
        if (src.size() >= 4)
        {
            auto rd16 = [&](size_t off) -> int {
                return (int)src[off] | ((int)src[off + 1] << 8);
            };
            const int w = rd16(0);
            const int h = rd16(2);
            if (w > 0 && h > 0)
            {
                const size_t need = (size_t)w * (size_t)h;
                if (src.size() >= 4 + need)
                {
                    out_pixels.assign(src.begin() + 4, src.begin() + 4 + (ptrdiff_t)need);
                    out_w = w; out_h = h;
                    return true;
                }
            }
        }

        // raw pixels (no header) - caller must provide dimensions externally
        return false;
    }

    wxBitmap MakeIndexedBitmap(const unsigned char* pixels, int w, int h,
        const std::array<unsigned char, 256 * 3>& pal256)
    {
        wxImage img(w, h, true);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const unsigned char idx = pixels[(size_t)y * (size_t)w + (size_t)x];
                img.SetRGB(x, y,
                    pal256[(size_t)idx * 3 + 0],
                    pal256[(size_t)idx * 3 + 1],
                    pal256[(size_t)idx * 3 + 2]);
            }
        }
        return wxBitmap(img);
    }

    bool DecodeMainMenuButtonFromLZ(const std::vector<unsigned char>& lzBytes,
        std::vector<unsigned char>& out_pixels,
        int& out_w, int& out_h)
    {
        // First try: maybe it is already raw pixels (15810 bytes) without header for MAINM buttons.
        if (lzBytes.size() == 510u * 31u)
        {
            out_pixels = lzBytes;
            out_w = 510;
            out_h = 31;
            return true;
        }

        // Second try: header + pixels directly
        if (DecodeIndexedWithOptionalHeader(lzBytes, out_pixels, out_w, out_h))
            return true;

        // Third try: LZW decode then retry the above patterns
        LZWexpand delz(512 * 1024);
        std::vector<uint8_t> decoded = delz.Decode((uint8_t*)lzBytes.data(), (uint8_t*)lzBytes.data() + lzBytes.size());
        if (decoded.empty())
            return false;

        std::vector<unsigned char> dec(decoded.begin(), decoded.end());

        if (dec.size() == 510u * 31u)
        {
            out_pixels = std::move(dec);
            out_w = 510;
            out_h = 31;
            return true;
        }

        if (DecodeIndexedWithOptionalHeader(dec, out_pixels, out_w, out_h))
            return true;

        return false;
    }

    bool Split510x31ToTwoStates(const std::vector<unsigned char>& pixels510x31,
        std::vector<unsigned char>& out_left255x31,
        std::vector<unsigned char>& out_right255x31)
    {
        if (pixels510x31.size() != 510u * 31u)
            return false;

        const int W = 510, H = 31, W1 = 255;
        out_left255x31.assign((size_t)W1 * H, 0);
        out_right255x31.assign((size_t)W1 * H, 0);

        for (int y = 0; y < H; ++y)
        {
            memcpy(&out_left255x31[(size_t)y * W1], &pixels510x31[(size_t)y * W], (size_t)W1);
            memcpy(&out_right255x31[(size_t)y * W1], &pixels510x31[(size_t)y * W + W1], (size_t)W1);
        }
        return true;
    }

    wxColour Mix(const wxColour& a, const wxColour& b, double t)
    {
        auto lerp = [&](int x, int y) { return (int)std::lround(x + (y - x) * t); };
        return wxColour(
            (unsigned char)lerp(a.Red(), b.Red()),
            (unsigned char)lerp(a.Green(), b.Green()),
            (unsigned char)lerp(a.Blue(), b.Blue()));
    }

    void DrawLed(wxGraphicsContext& gc, const wxPoint& center, int r, bool active)
    {
        const wxColour outer = wxColour(20, 10, 10);
        const wxColour inner_dark = wxColour(80, 10, 10);
        const wxColour inner_bright = wxColour(220, 40, 40);

        wxColour inner = active ? inner_bright : inner_dark;

        gc.SetPen(wxPen(outer, 1));
        gc.SetBrush(wxBrush(outer));
        gc.DrawEllipse(center.x - r, center.y - r, r * 2, r * 2);

        const int r2 = std::max(1, r - 2);
        wxGraphicsBrush b = gc.CreateRadialGradientBrush(
            center.x - r2 / 3, center.y - r2 / 3,
            center.x, center.y, r2,
            Mix(inner, wxColour(255, 180, 180), active ? 0.20 : 0.05),
            inner);

        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(b);
        gc.DrawEllipse(center.x - r2, center.y - r2, r2 * 2, r2 * 2);
    }

    void DrawMenuButton(wxGraphicsContext& gc, const wxRect& r, bool hover)
    {
        const int radius = 10;

        const wxColour frame_light = wxColour(175, 175, 175);
        const wxColour frame_dark = wxColour(70, 70, 70);

        wxColour top = wxColour(35, 90, 35);
        wxColour bot = wxColour(18, 50, 18);

        if (hover)
        {
            top = Mix(top, wxColour(80, 170, 80), 0.45);
            bot = Mix(bot, wxColour(40, 120, 40), 0.45);
        }

        // shadow
        wxRect shadow = r;
        shadow.Offset(1, 2);
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(wxColour(0, 0, 0, 90)));
        gc.DrawRoundedRectangle(shadow.x, shadow.y, shadow.width, shadow.height, radius);

        // outer metal frame
        gc.SetPen(wxPen(frame_dark, 1));
        gc.SetBrush(wxBrush(frame_light));
        gc.DrawRoundedRectangle(r.x, r.y, r.width, r.height, radius);

        // inner green fill
        wxRect inner = r;
        inner.Deflate(2, 2);
        wxGraphicsBrush fill = gc.CreateLinearGradientBrush(
            inner.x, inner.y,
            inner.x, inner.y + inner.height,
            top, bot);

        gc.SetPen(wxPen(wxColour(45, 45, 45), 1));
        gc.SetBrush(fill);
        gc.DrawRoundedRectangle(inner.x, inner.y, inner.width, inner.height, radius - 2);

        // top highlight
        wxRect hi = inner;
        hi.Deflate(3, 3);
        hi.height = std::max(1, hi.height / 3);
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(wxColour(255, 255, 255, hover ? 50 : 30)));
        gc.DrawRoundedRectangle(hi.x, hi.y, hi.width, hi.height, radius - 4);

        // bottom darkening
        wxRect lo = inner;
        lo.Deflate(3, 3);
        lo.y += lo.height * 2 / 3;
        lo.height = std::max(1, inner.height - (lo.y - inner.y));
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(wxColour(0, 0, 0, hover ? 45 : 30)));
        gc.DrawRoundedRectangle(lo.x, lo.y, lo.width, lo.height, radius - 4);
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

    LoadBackground();
    BuildMenuItems();
    LoadMenuPanel();const wxSize size = m_bg_size.IsFullySpecified() ? m_bg_size : wxSize(640, 480);
    wxPoint pos = { (parent->GetSize().x - size.x) / 2, (parent->GetSize().y - size.y) / 2 };

    form = new wxFrame(parent, win_id, "Spellcross reloaded alpha", pos, size, wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)| wxBG_STYLE_PAINT);
    form->SetBackgroundStyle(wxBG_STYLE_PAINT);
    form->SetDoubleBuffered(true);

    LayoutMenuItems(size);

    form->Bind(wxEVT_CLOSE_WINDOW, &FormMainMenu::OnClose, this);
    form->Bind(wxEVT_PAINT, &FormMainMenu::OnPaint, this);
    form->Bind(wxEVT_MOTION, &FormMainMenu::OnMouseMove, this);
    form->Bind(wxEVT_LEAVE_WINDOW, &FormMainMenu::OnMouseLeave, this);
    form->Bind(wxEVT_LEFT_UP, &FormMainMenu::OnMouseClick, this);
    form->Bind(wxEVT_KEY_DOWN, &FormMainMenu::OnKeyDown, this);

    form->SetFocus();
    form->Show();
}

FormMainMenu::~FormMainMenu()
{
    if (form)
        form->Destroy();
}

bool FormMainMenu::LoadBackground()
{
    m_background = wxBitmap();
    m_bg_size = wxSize(640, 480);

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

    std::vector<unsigned char> pixels;
    const int width = 640;
    const int height = 480;

    if (!DecodeMainMenuPixels(lzBytes, pixels, width, height))
    {
        LZWexpand delz(512 * 1024);
        std::vector<uint8_t> decoded = delz.Decode((uint8_t*)lzBytes.data(), (uint8_t*)lzBytes.data() + lzBytes.size());
        std::vector<unsigned char> decoded_uc(decoded.begin(), decoded.end());
        if (!DecodeMainMenuPixels(decoded_uc, pixels, width, height))
            return false;
    }

    if (!ExpandPaletteTo256(palBytes, m_pal256))
        return false;
    m_pal_ok = true;

    wxImage img(width, height, true);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const unsigned char idx = pixels[static_cast<size_t>(y) * width + x];
            img.SetRGB(x, y,
                m_pal256[(size_t)idx * 3 + 0],
                m_pal256[(size_t)idx * 3 + 1],
                m_pal256[(size_t)idx * 3 + 2]);
        }
    }

    m_background = wxBitmap(img);
    if (m_background.IsOk())
    {
        m_bg_size = m_background.GetSize();
        return true;
    }

    return false;
}

struct DecodedIndexed
{
    int w = 0;
    int h = 0;
    std::vector<unsigned char> pixels;
};

static bool DecodeIndexedMaybeHeader(const std::vector<unsigned char>& src, DecodedIndexed& out)
{
    out = {};
    if (src.size() < 4)
        return false;

    auto rd16 = [&](size_t off) -> unsigned {
        return static_cast<unsigned>(src[off]) | (static_cast<unsigned>(src[off + 1]) << 8);
    };

    unsigned w = rd16(0);
    unsigned h = rd16(2);
    if (w == 0 || h == 0 || w > 4096 || h > 4096)
        return false;

    const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (src.size() < 4 + need)
        return false;

    out.w = static_cast<int>(w);
    out.h = static_cast<int>(h);
    out.pixels.assign(src.begin() + 4, src.begin() + 4 + static_cast<ptrdiff_t>(need));
    return true;
}

static wxBitmap MakeBitmapFromIndexed(const DecodedIndexed& d, const std::array<unsigned char, 256 * 3>& pal256, bool treatIndex0AsTransparent)
{
    if (d.w <= 0 || d.h <= 0 || d.pixels.size() != (size_t)d.w * (size_t)d.h)
        return wxBitmap();

    wxImage img(d.w, d.h, true);
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

            img.SetAlpha(x, y, (treatIndex0AsTransparent && idx == 0) ? 0 : 255);
        }
    }
    return wxBitmap(img);
}

bool FormMainMenu::LoadMenuPanel()
{
    m_menuPanel = wxBitmap();
    m_menuPanelPos = wxPoint(0, 0);

    if (!m_spelldata || !m_pal_ok)
        return false;

    namespace fs = std::filesystem;
    fs::path root = fs::path(m_spelldata->spell_data_root);

    // Some distributions use slightly different names (with/without underscore).
    fs::path lzPath = FindMenuFile(root, "MAINM_BG.LZ");
    fs::path binPath = FindMenuFile(root, "MAINM_BG.BIN");

    if (lzPath.empty() && binPath.empty())
    {
        lzPath = FindMenuFile(root, "MAINMBG.LZ");
        binPath = FindMenuFile(root, "MAINMBG.BIN");
    }

    if (lzPath.empty() && binPath.empty())
    {
        lzPath = FindMenuFile(root, "MAINM-BG.LZ");
        binPath = FindMenuFile(root, "MAINM-BG.BIN");
    }


    std::vector<unsigned char> bytes;
    wxLogMessage("MainMenu: loading panel from %s", (!lzPath.empty() ? lzPath.string() : binPath.string()));
    if (!lzPath.empty())
    {
        if (!LoadFileBytes(lzPath, bytes))
            return false;

        LZWexpand delz(512 * 1024);
        std::vector<uint8_t> decoded = delz.Decode((uint8_t*)bytes.data(), (uint8_t*)bytes.data() + bytes.size());
        bytes.assign(decoded.begin(), decoded.end());
    }
    else if (!binPath.empty())
    {
        if (!LoadFileBytes(binPath, bytes))
            return false;
    }
    else
    {
        wxLogMessage("MainMenu: MAINM_BG not found (tried MAINM_BG/MAINMBG/MAINM-BG in root/DATA/COMMON). Panel overlay will be missing.");
        return false;
    }

    DecodedIndexed d;
	if (!DecodeIndexedMaybeHeader(bytes, d))
	{
		// MAINM_BG is typically 255x237 in Spellcross (same width as buttons),
		// so include 255 and a few other plausible widths.
		const std::array<int, 10> widths = { 255, 237, 640, 510, 512, 480, 400, 360, 320, 153 };

		for (int w : widths)
		{
			if (w <= 0) continue;
			if (bytes.size() % (size_t)w != 0) continue;

			int h = (int)(bytes.size() / (size_t)w);

			// sanity: panel is not huge and not tiny
			if (h >= 20 && h <= 480)
			{
				d.w = w;
				d.h = h;
				d.pixels = bytes;
				wxLogMessage("MainMenu: MAINM_BG decoded as %dx%d (bytes=%zu)", d.w, d.h, bytes.size());
				break;
			}
		}
	}

    if (d.w <= 0 || d.h <= 0)
        return false;

    m_menuPanel = MakeBitmapFromIndexed(d, m_pal256, true);
    if (!m_menuPanel.IsOk())
        return false;

    const int bgw = m_bg_size.GetWidth();
    const int bgh = m_bg_size.GetHeight();
    const wxSize ps = m_menuPanel.GetSize();

    int x = (bgw - ps.GetWidth()) / 2;
    int y = (bgh - ps.GetHeight()) / 2;
    y = std::max(0, y - 10);

    m_menuPanelPos = wxPoint(x, y);
    return true;
}




void FormMainMenu::BuildMenuItems()
{
    m_items.clear();

    // Pokud chce CZ texty jako v originlu, sta pepsat labely:
    // "New game" -> "nov hra", "Continue" -> "pokraovat", "Load game" -> "nast hru", "End" -> "konec"
    m_items.push_back({ "New game", FormMainMenuAction::NewGame, wxRect() });
    m_items.push_back({ "Continue", FormMainMenuAction::Continue, wxRect() });
    m_items.push_back({ "Load game", FormMainMenuAction::LoadGame, wxRect() });
    m_items.push_back({ "Credits", FormMainMenuAction::Credits, wxRect() });
    m_items.push_back({ "Intro", FormMainMenuAction::Intro, wxRect() });
    m_items.push_back({ "End", FormMainMenuAction::Exit, wxRect() });
}


void FormMainMenu::LayoutMenuItems(const wxSize& clientSize)
{
    if (m_items.empty())
        return;

    if (m_menuPanel.IsOk())
    {
        const wxSize ps = m_menuPanel.GetSize();

        // Easy-to-tweak layout constants for click zones inside the panel.
        const int btn_w = 255;
        const int btn_h = 31;
        const int gap   = 7;
        const int pad_top = 18;
        const int pad_left = (ps.x - btn_w) / 2;

        int x = m_menuPanelPos.x + std::max(0, pad_left);
        int y = m_menuPanelPos.y + pad_top;

        for (auto& it : m_items)
        {
            it.rect = wxRect(x, y, btn_w, btn_h);
            y += btn_h + gap;
        }
        return;
    }

    // Fallback without panel
    const int w = clientSize.x;
    const int h = clientSize.y;

    const int btn_w = 255;
    const int btn_h = 31;
    const int gap   = 7;

    const int x = (w - btn_w) / 2;
    const int total_h = (int)m_items.size() * btn_h + ((int)m_items.size() - 1) * gap;
    int y = (h - total_h) / 2;

    for (auto& it : m_items)
    {
        it.rect = wxRect(x, y, btn_w, btn_h);
        y += btn_h + gap;
    }
}

void FormMainMenu::TriggerAction(int index)
{
    if (index < 0 || index >= static_cast<int>(m_items.size()))
        return;

    if (m_action_cb)
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

    if (m_background.IsOk())
        dc.DrawBitmap(m_background, 0, 0, false);
    else
    {
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();
    }

    if (m_menuPanel.IsOk())
        dc.DrawBitmap(m_menuPanel, m_menuPanelPos.x, m_menuPanelPos.y, true);
}

void FormMainMenu::OnMouseMove(wxMouseEvent& event)
{
    const wxPoint pos = event.GetPosition();
    int new_hover = -1;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].rect.Contains(pos))
        {
            new_hover = static_cast<int>(i);
            break;
        }
    }

    if (new_hover != m_hover_index)
    {
        m_hover_index = new_hover;
        form->Refresh(false);
    }

    form->SetCursor(m_hover_index >= 0 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
}

void FormMainMenu::OnMouseLeave(wxMouseEvent& event)
{
    if (m_hover_index != -1)
    {
        m_hover_index = -1;
        form->Refresh(false);
    }
    form->SetCursor(wxCursor(wxCURSOR_ARROW));
    event.Skip();
}

void FormMainMenu::OnMouseClick(wxMouseEvent& event)
{
    if (event.GetButton() != wxMOUSE_BTN_LEFT)
        return;

    if (m_hover_index < 0)
        return;

    TriggerAction(m_hover_index);
}

void FormMainMenu::OnKeyDown(wxKeyEvent& event)
{
    const int key = event.GetKeyCode();

    if (key == WXK_ESCAPE)
    {
        form->Close();
        return;
    }

    if (m_items.empty())
        return;

    if (key == WXK_UP || key == WXK_DOWN)
    {
        int idx = m_hover_index;
        if (idx < 0)
            idx = 0;
        else
            idx += (key == WXK_UP) ? -1 : 1;

        if (idx < 0)
            idx = (int)m_items.size() - 1;
        if (idx >= (int)m_items.size())
            idx = 0;

        if (idx != m_hover_index)
        {
            m_hover_index = idx;
            form->Refresh(false);
        }
        return;
    }

    if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER || key == WXK_SPACE)
    {
        if (m_hover_index < 0)
            m_hover_index = 0;
        TriggerAction(m_hover_index);
        return;
    }

    event.Skip();
}
