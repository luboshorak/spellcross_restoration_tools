#include "form_level.h"

#include "main.h"
#include "other.h"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/choicdlg.h>
#include <wx/spinctrl.h>
#include <wx/dcscreen.h>

#include <filesystem>
#include <functional>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <regex>
#include <array>
#include <sstream>
#include "LZ_spell.h"
#include "../strategic_original_renderer.h"

namespace
{
    // Geometry of the map frame inside the original 575x480 VMM_FULL screen.
    constexpr int kStrategicScreenW = 575;
    constexpr int kStrategicScreenH = 480;
    constexpr int kMapChromeW = 412;
    constexpr int kMapChromeH = 299;
    constexpr int kMapViewportX = 17;
    constexpr int kMapViewportY = 19;
    constexpr int kMapViewportW = 379;
    constexpr int kMapViewportH = 259;

    // Restored strategic-map logical layout (original 640x480 screen coordinates).
    constexpr int kOriginalListX = 418;
    constexpr int kOriginalListY = 6;
    constexpr int kOriginalListW = 136;
    constexpr int kOriginalListH = 426;
    constexpr int kOriginalUnitRowsY = 83;
    constexpr int kOriginalUnitRowH = 14;
    constexpr int kOriginalVisibleRows = 24;
    // STRMAP.QH native hit rectangles. The BIGMB sprites themselves start one
    // pixel left and two pixels above those active rectangles (measured against
    // the original 640x480 frame).
    constexpr int kOriginalAttackX = 422;
    constexpr int kOriginalCancelX = 497;
    constexpr int kOriginalAttackY = 441;
    constexpr int kOriginalButtonW = 70;
    constexpr int kOriginalButtonH = 27;
    constexpr int kOriginalAttackDrawX = 421;
    constexpr int kOriginalCancelDrawX = 496;
    constexpr int kOriginalAttackDrawY = 439;

    // Exact DOS strategic-toolbar geometry.  The buttons have a 31 px pitch;
    // the active red wedge lives *inside* the toolbar at x=591, not on the
    // boundary between the main screen and the toolbar.
    constexpr int kOriginalToolbarX0 = 580;
    constexpr int kOriginalToolbarX1 = 640;
    constexpr int kOriginalToolbarY0 = 132;
    constexpr int kOriginalToolbarPitch = 31;
    constexpr int kOriginalToolbarCount = 9;

    // Territory id 0 is reserved for global resources settings (meta).
    // Keep this declaration near the other translation-unit constants because
    // the Original UI renderer uses it before the Resources-page functions.
    constexpr int kResourcesMetaTerritoryId = 0;

    using OriginalUiPalette = std::array<std::array<std::uint8_t, 3>, 256>;

    // Build the palette used by the common strategic chrome/buttons.  The DOS
    // game combines three palette banks: shared UI colours (0..127), STRATEGY
    // as a fallback, and BIG_MAP stone/button colours (192..255).
    static bool OriginalBuildUiPalette(const StrategicOriginalRenderer::AssetLoader& load,
        OriginalUiPalette& pal)
    {
        std::vector<std::uint8_t> strategy, shared, bigMap;
        if (!load || !load("STRATEGY.PAL", strategy) || strategy.size() < 256u * 3u)
            return false;
        if (!load("_SHARED1.PAL", shared) || shared.size() < 128u * 3u)
            return false;
        if (!load("BIG_MAP.PAL", bigMap) || bigMap.size() < 64u * 3u)
            return false;

        for (int i = 0; i < 256; ++i)
            pal[static_cast<size_t>(i)] = {
                strategy[static_cast<size_t>(i) * 3u + 0u],
                strategy[static_cast<size_t>(i) * 3u + 1u],
                strategy[static_cast<size_t>(i) * 3u + 2u]
            };
        for (int i = 0; i < 128; ++i)
            pal[static_cast<size_t>(i)] = {
                shared[static_cast<size_t>(i) * 3u + 0u],
                shared[static_cast<size_t>(i) * 3u + 1u],
                shared[static_cast<size_t>(i) * 3u + 2u]
            };
        for (int i = 0; i < 64; ++i)
            pal[static_cast<size_t>(192 + i)] = {
                bigMap[static_cast<size_t>(i) * 3u + 0u],
                bigMap[static_cast<size_t>(i) * 3u + 1u],
                bigMap[static_cast<size_t>(i) * 3u + 2u]
            };
        return true;
    }

    // Decode Spellcross ICO/BTN sparse scanlines into an RGBA wxImage.  Pixels
    // not present in the sparse stream remain transparent; an encoded colour 0
    // is the game's alternate solid black (palette entry 254), just like
    // SpellGraphics::AddICO().
    static wxImage OriginalDecodeIcoLike(const std::vector<std::uint8_t>& bytes,
        const OriginalUiPalette& pal)
    {
        if (bytes.size() < 7)
            return wxImage();

        const int xOfs = static_cast<int>(bytes[2] | (static_cast<unsigned>(bytes[3]) << 8));
        const int w = static_cast<int>(bytes[4] | (static_cast<unsigned>(bytes[5]) << 8));
        const int h = static_cast<int>(bytes[6]);
        if (w <= 0 || h <= 0)
            return wxImage();

        wxImage image(w, h, true);
        if (!image.IsOk() || !image.GetData())
            return wxImage();
        image.InitAlpha();
        std::memset(image.GetData(), 0, static_cast<size_t>(w) * h * 3u);
        std::memset(image.GetAlpha(), 0, static_cast<size_t>(w) * h);

        size_t pos = 7;
        for (int y = 0; y < h; ++y)
        {
            int lineX = -xOfs;
            while (pos < bytes.size() && bytes[pos] != 0xFF)
            {
                if (pos + 2 > bytes.size())
                    return wxImage();
                lineX += bytes[pos++];
                const int count = bytes[pos++];
                if (pos + static_cast<size_t>(count) > bytes.size())
                    return wxImage();
                for (int i = 0; i < count; ++i)
                {
                    const int x = lineX + i;
                    if (x < 0 || x >= w)
                        continue;
                    const std::uint8_t raw = bytes[pos + static_cast<size_t>(i)];
                    const std::uint8_t idx = raw == 0 ? 254 : raw;
                    const auto& c = pal[idx];
                    image.SetRGB(x, y, c[0], c[1], c[2]);
                    image.SetAlpha(x, y, 255);
                }
                pos += static_cast<size_t>(count);
                lineX += count;
            }
            if (pos >= bytes.size())
                return wxImage();
            ++pos; // 0xFF end-of-line
        }
        return image;
    }

    static bool OriginalBlitImage(wxImage& dst, const wxImage& src, int dx, int dy)
    {
        if (!dst.IsOk() || !src.IsOk() || !dst.GetData() || !src.GetData())
            return false;

        unsigned char* dd = dst.GetData();
        const unsigned char* sd = src.GetData();
        const unsigned char* sa = src.HasAlpha() ? src.GetAlpha() : nullptr;
        const int dw = dst.GetWidth(), dh = dst.GetHeight();
        const int sw = src.GetWidth(), sh = src.GetHeight();
        for (int y = 0; y < sh; ++y)
        {
            const int yy = dy + y;
            if (yy < 0 || yy >= dh) continue;
            for (int x = 0; x < sw; ++x)
            {
                const int xx = dx + x;
                if (xx < 0 || xx >= dw) continue;
                const size_t sp = static_cast<size_t>(y) * sw + x;
                if (sa && sa[sp] == 0) continue;
                const size_t si = sp * 3u;
                const size_t di = (static_cast<size_t>(yy) * dw + xx) * 3u;
                dd[di + 0] = sd[si + 0];
                dd[di + 1] = sd[si + 1];
                dd[di + 2] = sd[si + 2];
            }
        }
        return true;
    }

    static wxImage OriginalLoadIcoLike(const StrategicOriginalRenderer::AssetLoader& load,
        const std::string& name, const OriginalUiPalette& pal)
    {
        std::vector<std::uint8_t> bytes;
        if (!load || !load(name, bytes))
            return wxImage();
        return OriginalDecodeIcoLike(bytes, pal);
    }

    static wxImage OriginalLoadRawIndexed(const StrategicOriginalRenderer::AssetLoader& load,
        const std::string& name, int w, int h, const OriginalUiPalette& pal)
    {
        std::vector<std::uint8_t> bytes;
        if (!load || !load(name, bytes))
            return wxImage();
        const size_t expected = static_cast<size_t>(w) * h;
        if (bytes.size() < expected && !bytes.empty())
        {
            LZWexpand delz(static_cast<int>(std::max<size_t>(expected + 64u, 4096u)));
            auto& decoded = delz.Decode(bytes.data(), bytes.data() + bytes.size());
            if (!decoded.empty())
                bytes.assign(decoded.begin(), decoded.end());
        }
        if (bytes.size() < expected)
            return wxImage();

        wxImage image(w, h, true);
        if (!image.IsOk() || !image.GetData())
            return wxImage();
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const auto& c = pal[bytes[static_cast<size_t>(y) * w + x]];
                image.SetRGB(x, y, c[0], c[1], c[2]);
            }
        }
        return image;
    }

    struct OriginalHierarchyHitSlot
    {
        wxRect rect;
        std::string id;
        bool commander = false;
    };

    static std::vector<OriginalHierarchyHitSlot> OriginalHierarchySlotsForPage(int brigadeIndex)
    {
        brigadeIndex = std::clamp(brigadeIndex, 1, 2);
        constexpr int xUnits = 12;
        constexpr int xBattCmd = 165;
        constexpr int xRegCmd = 217;
        constexpr int xBrig = 261;
        constexpr int unitW = 146;
        constexpr int cmdW = 132;
        constexpr int brigW = 132;
        constexpr int slotH = 16;
        constexpr int gapY = 7;
        constexpr int pairGap = 1;
        constexpr int regTopY = 25;
        constexpr int regBlockH = 208;
        constexpr int regCmdOffY = 76;
        constexpr int battalionPairGapY = 104;
        constexpr int brigadeY = 207;

        std::vector<OriginalHierarchyHitSlot> out;
        out.reserve(4 * 6 + 2 * 2 + 2);
        const int battalionBase = (brigadeIndex - 1) * 4;
        auto battalionTopY = [=](int local)
        {
            const int regLocal = local / 2;
            const int inReg = local % 2;
            return regTopY + regLocal * regBlockH + inReg * battalionPairGapY;
        };
        for (int bLocal = 0; bLocal < 4; ++bLocal)
        {
            const int b = battalionBase + bLocal + 1;
            const int y0 = battalionTopY(bLocal);
            for (int u = 0; u < 4; ++u)
            {
                out.push_back({wxRect(xUnits, y0 + u * (slotH + gapY), unitW, slotH),
                    "battalion_" + std::to_string(b) + "_unit_" + std::to_string(u + 1), false});
            }
            const int yc = y0 + 26;
            out.push_back({wxRect(xBattCmd, yc, cmdW, slotH),
                "battalion_" + std::to_string(b) + "_commander", true});
            out.push_back({wxRect(xBattCmd, yc + slotH + pairGap, cmdW, slotH),
                "battalion_" + std::to_string(b) + "_commander_unit", false});
        }
        for (int rLocal = 0; rLocal < 2; ++rLocal)
        {
            const int r = (brigadeIndex - 1) * 2 + rLocal + 1;
            const int y = regTopY + rLocal * regBlockH + regCmdOffY;
            out.push_back({wxRect(xRegCmd, y, cmdW, slotH),
                "regiment_" + std::to_string(r) + "_commander", true});
            out.push_back({wxRect(xRegCmd, y + slotH + pairGap, cmdW, slotH),
                "regiment_" + std::to_string(r) + "_unit", false});
        }
        out.push_back({wxRect(xBrig, brigadeY, brigW, slotH),
            "brigade_" + std::to_string(brigadeIndex) + "_commander", true});
        out.push_back({wxRect(xBrig, brigadeY + slotH + pairGap, brigW, slotH),
            "brigade_" + std::to_string(brigadeIndex) + "_unit", false});
        return out;
    }

    static void OriginalSetPixel(wxImage& image, int x, int y, const wxColour& color)
    {
        if (!image.IsOk() || x < 0 || y < 0 || x >= image.GetWidth() || y >= image.GetHeight())
            return;
        unsigned char* data = image.GetData();
        if (!data)
            return;
        const size_t q = (static_cast<size_t>(y) * image.GetWidth() + x) * 3u;
        data[q + 0] = color.Red();
        data[q + 1] = color.Green();
        data[q + 2] = color.Blue();
    }

    static void OriginalFillRect(wxImage& image, int x, int y, int w, int h, const wxColour& color)
    {
        if (!image.IsOk() || w <= 0 || h <= 0)
            return;
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min(image.GetWidth(), x + w);
        const int y1 = std::min(image.GetHeight(), y + h);
        unsigned char* data = image.GetData();
        if (!data)
            return;
        for (int yy = y0; yy < y1; ++yy)
        {
            for (int xx = x0; xx < x1; ++xx)
            {
                const size_t q = (static_cast<size_t>(yy) * image.GetWidth() + xx) * 3u;
                data[q + 0] = color.Red();
                data[q + 1] = color.Green();
                data[q + 2] = color.Blue();
            }
        }
    }

    static void OriginalHLine(wxImage& image, int x0, int x1, int y, const wxColour& color)
    {
        if (x1 < x0) std::swap(x0, x1);
        for (int x = x0; x <= x1; ++x)
            OriginalSetPixel(image, x, y, color);
    }

    static void OriginalVLine(wxImage& image, int x, int y0, int y1, const wxColour& color)
    {
        if (y1 < y0) std::swap(y0, y1);
        for (int y = y0; y <= y1; ++y)
            OriginalSetPixel(image, x, y, color);
    }

    static int OriginalCenteredTextY(SpellFont* font, int y, int h)
    {
        const int fh = font ? std::max(1, font->GetHeight()) : 1;
        return y + std::max(0, (h - fh) / 2);
    }

    static void OriginalDrawScrollButton(wxImage& image, int x, int y, bool up, bool enabled = true)
    {
        // 22x28 native strategic scrollbar button. Drawn procedurally so the
        // restored UI does not depend on wx child controls or resource state.
        const wxColour outer(92, 88, 80);
        const wxColour inner(enabled ? 45 : 38, enabled ? 45 : 38, enabled ? 42 : 38);
        const wxColour hi(enabled ? 176 : 92, enabled ? 170 : 92, enabled ? 158 : 88);
        const wxColour lo(24, 23, 22);
        const wxColour arrow(enabled ? 188 : 92, enabled ? 184 : 92, enabled ? 174 : 88);

        OriginalFillRect(image, x, y, 22, 28, outer);
        OriginalFillRect(image, x + 2, y + 2, 18, 24, inner);
        OriginalHLine(image, x + 2, x + 19, y + 2, hi);
        OriginalVLine(image, x + 2, y + 2, y + 25, hi);
        OriginalHLine(image, x + 2, x + 19, y + 25, lo);
        OriginalVLine(image, x + 19, y + 2, y + 25, lo);

        const int cy = y + 13;
        for (int r = 0; r < 6; ++r)
        {
            const int yy = up ? (cy + 3 - r) : (cy - 3 + r);
            const int half = r;
            OriginalHLine(image, x + 10 - half, x + 10 + half, yy, arrow);
        }
    }

    static void OriginalDrawScrollTrack(wxImage& image, int x, int y, int h, int position, int maxPosition,
        int visibleRows, int totalRows)
    {
        if (h <= 0)
            return;
        const wxColour track(22, 35, 22);
        const wxColour edge(107, 104, 96);
        const wxColour thumb(112, 108, 100);
        const wxColour thumbHi(190, 184, 170);
        const wxColour thumbLo(42, 40, 37);
        OriginalFillRect(image, x, y, 22, h, track);
        OriginalVLine(image, x, y, y + h - 1, edge);
        OriginalVLine(image, x + 21, y, y + h - 1, edge);

        if (maxPosition <= 0 || totalRows <= 0)
            return;
        const int innerY = y + 2;
        const int innerH = std::max(1, h - 4);
        const int thumbH = std::max(16, innerH * std::max(1, visibleRows) / std::max(visibleRows, totalRows));
        const int travel = std::max(0, innerH - thumbH);
        const int thumbY = innerY + (travel * std::clamp(position, 0, maxPosition)) / maxPosition;
        OriginalFillRect(image, x + 3, thumbY, 16, thumbH, thumb);
        OriginalHLine(image, x + 3, x + 18, thumbY, thumbHi);
        OriginalVLine(image, x + 3, thumbY, thumbY + thumbH - 1, thumbHi);
        OriginalHLine(image, x + 3, x + 18, thumbY + thumbH - 1, thumbLo);
        OriginalVLine(image, x + 18, thumbY, thumbY + thumbH - 1, thumbLo);
    }

    static int OriginalTextWidth(SpellFont* font, const wxString& text)
    {
        if (!font || text.empty())
            return 0;
        std::string encoded = wstring2stringCP895(text.ToStdWstring());
        return font->GetTextWidth(encoded);
    }

    static void OriginalDrawSpellText(wxImage& image, SpellFont* font, const wxString& text,
        int x, int y, const wxColour& fg, int boxWidth = 0, bool centered = false,
        SpellFont::FontAlign align = SpellFont::LEFT)
    {
        if (!image.IsOk() || !font || text.empty())
            return;

        int tx = x;
        if (centered && boxWidth > 0)
            tx = x + std::max(0, (boxWidth - OriginalTextWidth(font, text)) / 2);

        std::vector<uint8_t> mask(static_cast<size_t>(image.GetWidth()) * image.GetHeight(), 0);
        font->Render(mask.data(), mask.data() + mask.size(), image.GetWidth(), tx, y,
            text.ToStdWstring(), 2, 1, SpellFont::RIGHT_DOWN, align);

        unsigned char* rgb = image.GetData();
        if (!rgb)
            return;
        const int fh = std::max(1, font->GetHeight());
        const int y0 = std::max(0, y - 1);
        const int y1 = std::min(image.GetHeight(), y + fh + 2);
        // boxWidth also acts as an explicit clip rectangle. This matters for
        // long unit names: they must never paint into the original scrollbar.
        const int x0 = std::max(0, boxWidth > 0 ? x : tx - 2);
        const int x1 = std::min(image.GetWidth(), boxWidth > 0
            ? x + boxWidth
            : tx + std::max(8, OriginalTextWidth(font, text)) + 4);
        for (int yy = y0; yy < y1; ++yy)
        {
            for (int xx = x0; xx < x1; ++xx)
            {
                const uint8_t m = mask[static_cast<size_t>(yy) * image.GetWidth() + xx];
                if (!m)
                    continue;
                const size_t q = (static_cast<size_t>(yy) * image.GetWidth() + xx) * 3u;
                if (m == 1)
                {
                    rgb[q + 0] = 0;
                    rgb[q + 1] = 0;
                    rgb[q + 2] = 0;
                }
                else
                {
                    rgb[q + 0] = fg.Red();
                    rgb[q + 1] = fg.Green();
                    rgb[q + 2] = fg.Blue();
                }
            }
        }
    }

    static wxString OriginalCleanBriefing(wxString text)
    {
        text.Replace("\r", "");
        wxArrayString lines = wxSplit(text, '\n', '\0');
        wxString out;
        for (const auto& raw : lines)
        {
            wxString line = raw;
            line.Trim(true).Trim(false);
            if (line.CmpNoCase("Briefing") == 0 ||
                line.CmpNoCase("Counter-Attack Briefing") == 0 ||
                line.StartsWith("---"))
                continue;
            if (!out.empty() && !line.empty())
                out += " ";
            if (!line.empty())
                out += line;
        }
        return out;
    }

    static std::vector<wxString> OriginalWrapText(SpellFont* font, const wxString& text, int maxWidth, int maxLines)
    {
        std::vector<wxString> lines;
        if (!font || maxWidth <= 0 || maxLines <= 0)
            return lines;

        std::wistringstream in(text.ToStdWstring());
        std::wstring word;
        wxString current;
        while (in >> word)
        {
            const wxString w(word.c_str());
            const wxString candidate = current.empty() ? w : current + " " + w;
            if (!current.empty() && OriginalTextWidth(font, candidate) > maxWidth)
            {
                lines.push_back(current);
                current = w;
                if (static_cast<int>(lines.size()) >= maxLines)
                    break;
            }
            else
            {
                current = candidate;
            }
        }
        if (static_cast<int>(lines.size()) < maxLines && !current.empty())
            lines.push_back(current);
        return lines;
    }

    static void OriginalDrawStrategicStatus(wxImage& image, SpellFont* font,
        int money, int research, int turn)
    {
        const wxColour text(218, 222, 211);
        const wxColour green(0, 242, 0);
        OriginalDrawSpellText(image, font, L"Peníze", 578, 17, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", money), 578, 31, text, 61, true);
        OriginalDrawSpellText(image, font, L"Výzkum", 578, 47, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", research), 578, 61, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 578, 77, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", turn), 578, 91, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 579, 436, text, 60, true);
        OriginalDrawSpellText(image, font, wxString::Format("%02d", turn), 579, 452, text, 60, true);
    }

    static void OriginalDrawActionButton(wxImage& image, SpellFont* font,
        int x, int y, int w, int h, const wxString& label, bool enabled)
    {
        const wxColour bg = enabled ? wxColour(18, 70, 22) : wxColour(22, 46, 22);
        const wxColour hi(78, 124, 78);
        const wxColour lo(10, 22, 10);
        const wxColour fg = enabled ? wxColour(0, 242, 0) : wxColour(90, 112, 90);
        OriginalFillRect(image, x, y, w, h, bg);
        OriginalHLine(image, x, x + w - 1, y, hi);
        OriginalVLine(image, x, y, y + h - 1, hi);
        OriginalHLine(image, x, x + w - 1, y + h - 1, lo);
        OriginalVLine(image, x + w - 1, y, y + h - 1, lo);
        // faint internal grid like the original green buttons
        const wxColour grid(26, 92, 30);
        for (int gx = x + 3; gx < x + w - 2; gx += 18)
            OriginalVLine(image, gx, y + 2, y + h - 3, grid);
        for (int gy = y + 3; gy < y + h - 2; gy += 18)
            OriginalHLine(image, x + 2, x + w - 3, gy, grid);
        if (font)
            OriginalDrawSpellText(image, font, label, x, y + std::max(0, (h - std::max(1, font->GetHeight())) / 2) - 1, fg, w, true);
    }

    static wxBitmap OriginalDesaturateBitmap(const wxBitmap& source)
    {
        if (!source.IsOk())
            return wxBitmap();
        wxImage img = source.ConvertToImage();
        if (!img.IsOk() || !img.GetData())
            return source;
        unsigned char* data = img.GetData();
        const size_t pixels = static_cast<size_t>(img.GetWidth()) * img.GetHeight();
        for (size_t i = 0; i < pixels; ++i)
        {
            const size_t q = i * 3u;
            const int gray = (static_cast<int>(data[q + 0]) + data[q + 1] + data[q + 2]) / 3;
            const unsigned char v = static_cast<unsigned char>(std::clamp(gray + 28, 0, 255));
            data[q + 0] = v;
            data[q + 1] = v;
            data[q + 2] = v;
        }
        return wxBitmap(img);
    }
}

static bool BuildStrategicScreenBitmap(SpellData* spellData, const char* resourceName, wxBitmap& outBmp);
static bool BindStrategicScreenSlice(wxPanel* panel, SpellData* spellData,
    const char* resourceName, const wxRect& sourceRect);
static std::filesystem::path GetStrategicSaveSlotPath(const LevelData& level, int slot);
static bool PeekStrategicSaveSummary(const std::filesystem::path& path, int& outMoney, int& outRank, int& outExp, std::string& outTs);

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

// Grid overlay for list controls - draws subtle grid lines on the background
// Note: On Windows, native ListView ignores EVT_ERASE_BACKGROUND, so we use
// a post-paint approach with wxClientDC + CallAfter for the overlay effect.
static void BindListGridOverlay(wxListCtrl* list, const wxColour& gridColor = wxColour(0x40, 0x60, 0x38))
{
    if (!list) return;

    // Force custom background style so we have more control
    list->SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Paint handler: let native control paint first, then overlay grid
    list->Bind(wxEVT_PAINT, [list, gridColor](wxPaintEvent& ev) {
        // MUST create wxPaintDC first, even if we don't use it directly
        wxPaintDC paintDC(list);

        // Let native control draw its content
        ev.Skip();

        // Schedule grid drawing AFTER native paint completes using CallAfter
        // This ensures grid is drawn on top of the native rendering
        list->CallAfter([list, gridColor]() {
            if (!list || !list->IsShownOnScreen()) return;

            // Use wxClientDC for post-paint drawing
            wxClientDC dc(list);
            if (!dc.IsOk()) return;

            int w, h;
            list->GetClientSize(&w, &h);
            if (w <= 0 || h <= 0) return;

            // Draw grid lines with higher visibility
            dc.SetPen(wxPen(gridColor, 1, wxPENSTYLE_SOLID));

            const int gridSize = 20;
            for (int gx = 0; gx < w; gx += gridSize)
                dc.DrawLine(gx, 0, gx, h);
            for (int gy = 0; gy < h; gy += gridSize)
                dc.DrawLine(0, gy, w, gy);
        });
    });
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

        // unit:<uid>:<name>
        // unit:<name>
        if (kind == "unit" && tokens.size() >= 2)
        {
            parsed.valid = true;
            parsed.type = "unit";
            if (tokens.size() >= 3)
            {
                long uid = 0;
                if (tokens[1].ToLong(&uid) && uid > 0)
                    parsed.commander_uid = static_cast<uint32_t>(uid); // reused as generic uid carrier for legacy drag payload parsing
                parsed.name = tokens[2];
            }
            else
            {
                parsed.name = tokens[1];
            }
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
        // slot:<slotId>:unit:<uid>:<name>
        // slot:<slotId>:commander:<uid>:<rank>:<name>
        // slot:<slotId>:commander:<rank>:<name>
        if (kind == "slot" && tokens.size() >= 4)
        {
            parsed.valid = true;
            parsed.fromSlot = true;
            parsed.slotId = tokens[1].ToStdString();
            parsed.type = tokens[2].ToStdString();

            if (parsed.type == "unit" && tokens.size() >= 5)
            {
                long uid = 0;
                if (tokens[3].ToLong(&uid) && uid > 0)
                    parsed.commander_uid = static_cast<uint32_t>(uid); // reused as generic uid carrier
                parsed.name = tokens[4];
            }
            else if (parsed.type == "commander" && tokens.size() >= 6)
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
        btn->SetMaxSize(minSize);  // Fixed size, no expansion
    }

    // volitelně – trochu větší vnitřní okraje (padding), aby to vypadalo líp
    // btn->SetMargins(12, 8);   // funguje od wx 3.1+, pokud máš starší verzi → ignoruj

    return btn;
}

// ============================================================
// Bitmap buttons for strategic menu (icon-based)
// ============================================================

static std::filesystem::path GetMenuIconPath()
{
    return std::filesystem::current_path() / "data" / "menu";
}

static const char* GetOriginalStrategicIconName(const wxString& name)
{
    if (name == "strategic_map") return "VM_MAP";
    if (name == "hierarchy")     return "VM_HIER";
    if (name == "units")         return "VM_UNIT";
    if (name == "buy_sell")      return "VM_BUY";
    if (name == "research")      return "VM_RESEA";
    if (name == "info")          return "VM_INFO";
    if (name == "resources")     return "VM_FACTO";
    if (name == "statistics")    return "VM_STATI";
    if (name == "options")       return "VM_OPTIO";
    return nullptr;
}

static wxBitmap ScaleMenuIcon(wxBitmap bitmap, const wxSize& targetSize, wxImageResizeQuality quality)
{
    if (!bitmap.IsOk() || targetSize == wxDefaultSize ||
        targetSize.GetWidth() <= 0 || targetSize.GetHeight() <= 0)
        return bitmap;

    wxImage img = bitmap.ConvertToImage();
    const int srcW = img.GetWidth();
    const int srcH = img.GetHeight();
    if (srcW <= 0 || srcH <= 0)
        return wxBitmap();

    const double scaleX = (double)targetSize.GetWidth() / (double)srcW;
    const double scaleY = (double)targetSize.GetHeight() / (double)srcH;
    const double scale = std::min(scaleX, scaleY);
    const int newW = std::max(1, (int)std::lround(srcW * scale));
    const int newH = std::max(1, (int)std::lround(srcH * scale));
    return wxBitmap(img.Scale(newW, newH, quality));
}

static wxBitmap LoadMenuIcon(SpellData* spellData, const wxString& name,
    const wxSize& targetSize = wxDefaultSize)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // Prefer the original Spellcross ICO resource already decoded from COMMON.FS.
    if (spellData)
    {
        const char* resourceName = GetOriginalStrategicIconName(name);
        SpellGraphicItem* item = resourceName ? spellData->gres.GetResource(resourceName) : nullptr;
        if (item)
        {
            std::unique_ptr<wxBitmap> original(item->Render(true));
            if (original && original->IsOk())
                return ScaleMenuIcon(*original, targetSize, wxIMAGE_QUALITY_NEAREST);
        }
    }

    // Development fallback for incomplete/foreign game-data installations.
    const fs::path dir = GetMenuIconPath();
    const fs::path pngPath = dir / (name.ToStdString() + ".png");

    if (!fs::exists(pngPath, ec))
    {
        return wxBitmap();
    }

    wxImage img;
    if (!img.LoadFile(wxString::FromUTF8(pngPath.string()), wxBITMAP_TYPE_PNG))
    {
        return wxBitmap();
    }

    return ScaleMenuIcon(wxBitmap(img), targetSize, wxIMAGE_QUALITY_HIGH);
}

static wxBitmapButton* CreateStrategicBitmapButton(
    wxWindow* parent,
    SpellData* spellData,
    int id,
    const wxString& iconName,
    const wxColour& background,
    const wxSize& minSize = wxDefaultSize)
{
    // Load icon (use minSize for scaling if specified)
    wxSize iconSize = minSize;
    if (iconSize == wxDefaultSize)
        iconSize = wxSize(200, 44);  // default button size

    wxBitmap bmp = LoadMenuIcon(spellData, iconName, iconSize);

    wxBitmapButton* btn;
    if (bmp.IsOk())
    {
        btn = new wxBitmapButton(parent, id, bmp, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    }
    else
    {
        // Fallback: create text button if icon missing
        btn = new wxBitmapButton(parent, id, wxBitmap(1, 1), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    }

    btn->SetBackgroundColour(background);

    if (minSize != wxDefaultSize)
        btn->SetMinSize(minSize);

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
    std::unordered_map<int, std::string>* outCategories = nullptr,
    std::unordered_map<int, int>* outUpgradeCosts = nullptr)
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
                        if (outUpgradeCosts)
                        {
                            int upgCost = 0;
                            if (ParseJsonIntField(obj, "cost_upgrade", upgCost))
                                (*outUpgradeCosts)[index] = upgCost;
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

    // Reflow hard line breaks from original game text files (designed for 320x200).
    // Replace single \n with space so text wraps to the full width of the text control.
    // Preserve paragraph breaks (double \n\n) and leading whitespace lines.
    {
        std::string reflowed;
        reflowed.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\n')
            {
                // Double newline (or more) = paragraph break: keep as-is
                if (i + 1 < s.size() && s[i + 1] == '\n')
                {
                    reflowed.push_back('\n');
                    reflowed.push_back('\n');
                    ++i; // skip second \n
                    // Skip any additional consecutive newlines
                    while (i + 1 < s.size() && s[i + 1] == '\n')
                        ++i;
                }
                else
                {
                    // Single newline: replace with space (reflow)
                    if (!reflowed.empty() && reflowed.back() != ' ' && reflowed.back() != '\n')
                        reflowed.push_back(' ');
                }
            }
            else
            {
                reflowed.push_back(s[i]);
            }
        }
        s = std::move(reflowed);
    }

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

// Load a single text variant for a mission token: "" = briefing, ".OK", ".BAD", ".S"
static void try_append_single_text(wxString& info, const std::filesystem::path& base_dir,
    std::string mission_token, const std::string& suffix, const char* caption)
{
    if (mission_token.empty())
        return;

    std::string base = mission_to_text_base(mission_token);
    if (!base.empty())
    {
        char last = base.back();
        if (last >= '0' && last <= '9')
            base.push_back('A');
    }

    std::string raw;
    if (read_text_file(base_dir / (base + suffix), raw))
        append_text_snippet(info, caption, raw);
}

// ---------------- Campaign start territory helper ----------------
// In some levels the campaign starting territory is NOT T01.
// Heuristic: pick the first territory whose mission has no Briefing text file.
// (In the original game, those typically represent the already-secured / home region.)
static std::filesystem::path FindTextsDirForLevel(const LevelData& level, const SpellData* spellData = nullptr)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path texts_dir;

    auto try_dir = [&](const fs::path& p)
        {
            if (texts_dir.empty() && !p.empty() && fs::exists(p, ec) && fs::is_directory(p, ec))
                texts_dir = p;
            ec.clear();
        };

    // The authoritative location is the game-data root resolved from config.ini.
    // LEVEL_XX.DEF is normally a cached COMMON.FS export, so walking from its
    // path cannot reach the installed DATA\Texts directory.
    if (spellData)
    {
        const fs::path dataRoot = spellData->data_path;
        const fs::path cdRoot = spellData->cd_data_path;
        try_dir(dataRoot / "TEXTS");
        try_dir(dataRoot / "Texts");
        try_dir(dataRoot / "texts");
        try_dir(cdRoot / "TEXTS");
        try_dir(cdRoot / "Texts");
        try_dir(cdRoot / "texts");
    }

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

static std::vector<int> ReadExplicitStartTerritories(const LevelData& level)
{
    std::vector<int> result;
    std::ifstream input(level.source_path, std::ios::binary);
    if (input)
    {
        // Original DEFs may contain several Start(n) directives, while the
        // legacy LevelData field retains only the last one.  Read all of them
        // from the authoritative source so levels 03/04/05 start correctly.
        const std::regex startRe(R"(\bStart\s*\(\s*(\d+)\s*\))",
            std::regex_constants::icase);
        std::string line;
        while (std::getline(input, line))
        {
            const size_t comment = line.find(';');
            if (comment != std::string::npos)
                line.resize(comment);

            std::smatch match;
            if (!std::regex_search(line, match, startRe) || match.size() < 2)
                continue;

            const int id = std::stoi(match[1].str());
            const bool known = std::any_of(level.territories.begin(), level.territories.end(),
                [id](const LevelTerritory& territory) { return territory.id == id; });
            if (known)
                result.push_back(id);
        }
    }

    // Custom/in-memory LevelData can have no readable source file.
    if (result.empty() && level.start_territory > 0)
        result.push_back(level.start_territory);

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

static int ChooseDefaultStartTerritoryId_NoBriefing(const LevelData& level, const SpellData* spellData = nullptr)
{
    if (level.territories.empty())
        return 0;

    const std::vector<int> explicitStarts = ReadExplicitStartTerritories(level);
    if (!explicitStarts.empty())
        return explicitStarts.front();

    // LEVEL_XX.DEF explicitly uses intro mission "none" for territories already
    // held at level start. Prefer that reliable metadata over a filesystem guess.
    for (const auto& t : level.territories)
    {
        const std::string intro = to_lower(trim(t.intro_mission));
        if (intro.empty() || intro == "none")
            return t.id;
    }

    const auto texts_dir = FindTextsDirForLevel(level, spellData);

    // Older/custom definitions may omit the explicit marker. Only use the
    // briefing heuristic when the TEXTS directory was actually found.
    if (!texts_dir.empty())
    {
        for (const auto& t : level.territories)
        {
            if (!HasBriefingForMissionToken(texts_dir, t.mission))
                return t.id;
        }
    }

    // Safe fallback: never mark every territory as owned because TEXTS is absent.
    return level.territories.front().id;
}

static std::vector<int> ChooseStartTerritories_NoBriefing(const LevelData& level, const SpellData* spellData = nullptr)
{
    std::vector<int> out = ReadExplicitStartTerritories(level);
    if (level.territories.empty())
        return out;

    // Compatibility for older/custom definitions without Start(n): their
    // already-held territories conventionally use intro mission "none".
    if (out.empty())
    {
        for (const auto& t : level.territories)
        {
            const std::string intro = to_lower(trim(t.intro_mission));
            if (intro.empty() || intro == "none")
                out.push_back(t.id);
        }
    }

    // Compatibility fallback for older/custom definitions.
    const auto texts_dir = FindTextsDirForLevel(level, spellData);
    if (out.empty() && !texts_dir.empty())
    {
        for (const auto& t : level.territories)
        {
            if (!HasBriefingForMissionToken(texts_dir, t.mission))
                out.push_back(t.id);
        }
    }

    if (out.empty())
        out.push_back(level.territories.front().id);

    // make deterministic & unique
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static std::filesystem::path GetStrategicStatePath(const LevelData& level);
static std::filesystem::path FindPreviousLevelSavePath(const LevelData& currentLevel);

StrategicLevelFrame::StrategicLevelFrame(MainFrame* parent, const LevelData& level, bool skipAutosave)
    : wxFrame(parent, wxID_ANY, "Strategic Level", wxDefaultPosition, wxSize(1390, 1050),
        wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
    m_main(parent),
    m_spellData(parent ? parent->spell_data : nullptr),
    m_level(level)
{
    static bool seeded = false;
    if (!seeded) { std::srand((unsigned)std::time(nullptr)); seeded = true; }

    // Small native-UI animation clock.  The original end-turn control wipes
    // a 41 px sprite in/out in roughly 0.6 s.  Territory hatch animation used
    // the old 120 ms cadence, so it advances only on every second 60 ms tick.
    m_originalStrategicAnimTimer.SetOwner(this);
    Bind(wxEVT_TIMER, &StrategicLevelFrame::OnOriginalStrategicAnimTimer, this);
    m_originalStrategicAnimTimer.Start(60);

    m_money = 0;
    m_research = 0;
    m_playerUnits = m_level.start_units;

    // init territory mission state from LevelData
    for (const auto& t : m_level.territories)
    {
        m_territoryCurrentMission[t.id] = t.mission;
        m_territoryLaunchCount[t.id] = 0;
    }

    // Initialize the finite strategic-point pools from DefineStrategicPoints().
    // total = total SB capacity, incomePerTurn = SB yielded per strategic turn.
    for (const auto& t : m_level.territories)
    {
        TerritoryResourceState st;
        st.total = std::max(0, t.strategic_points_total);
        st.remaining = st.total;
        st.incomePerTurn = std::max(0, t.strategic_points_per_turn);
        m_territoryResources[t.id] = st;
    }

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
    // If autosave exists or a previous level save is available, ask user.
    if (!skipAutosave)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const auto autosave = GetStrategicStatePath(m_level);
        const auto prevSave = FindPreviousLevelSavePath(m_level);
        const bool hasAutosave = fs::exists(autosave, ec);
        const bool hasPrevSave = !prevSave.empty();

        if (hasAutosave || hasPrevSave)
        {
            enum { ACT_LOAD_AUTOSAVE, ACT_NEW_BAK, ACT_NEW_KEEP, ACT_CONTINUE_CAMPAIGN };
            wxArrayString choices;
            std::vector<int> choiceActions;
            int defaultSelection = 0;

            // --- Standard campaign choices ---

            if (hasAutosave)
            {
                choices.Add("Continue (load autosave)");
                choiceActions.push_back(ACT_LOAD_AUTOSAVE);
            }

            if (hasPrevSave)
            {
                choices.Add("Continue the campaign (load progress from previous level)");
                choiceActions.push_back(ACT_CONTINUE_CAMPAIGN);
            }

            // --- Debug-only choices ---

            if (hasAutosave)
            {
                choices.Add("[DEBUG] Start new (keep old autosave as .bak)");
                choiceActions.push_back(ACT_NEW_BAK);
                choices.Add("[DEBUG] Start new (do not touch autosave)");
                choiceActions.push_back(ACT_NEW_KEEP);
            }
            else
            {
                choices.Add("[DEBUG] Start new (fresh state)");
                choiceActions.push_back(ACT_NEW_KEEP);
            }

            // --- Default selection logic ---
            // Autosave takes priority: it means the player was already playing this level.
            // prevSave only = player just arrived at this level from the previous one.
            // After the very first mission (New Game), neither exists and this
            // dialog is skipped entirely via skipAutosave=true.
            if (hasAutosave)
            {
                for (int i = 0; i < (int)choiceActions.size(); i++)
                {
                    if (choiceActions[i] == ACT_LOAD_AUTOSAVE) { defaultSelection = i; break; }
                }
            }
            else if (hasPrevSave)
            {
                for (int i = 0; i < (int)choiceActions.size(); i++)
                {
                    if (choiceActions[i] == ACT_CONTINUE_CAMPAIGN) { defaultSelection = i; break; }
                }
            }

            wxSingleChoiceDialog dlg(this,
                "Choose how to start this level:\n\n"
                "Options marked [DEBUG] are for testing only\n"
                "and are not part of normal campaign progression.",
                "Strategic Level", choices);
            dlg.SetSelection(defaultSelection);

            if (dlg.ShowModal() == wxID_OK)
            {
                const int sel = dlg.GetSelection();
                if (sel >= 0 && sel < (int)choiceActions.size())
                {
                    switch (choiceActions[sel])
                    {
                    case ACT_LOAD_AUTOSAVE:
                        LoadStrategicState();
                        break;
                    case ACT_NEW_BAK:
                    {
                        fs::path bak = autosave;
                        bak += ".bak";
                        fs::rename(autosave, bak, ec);
                        break;
                    }
                    case ACT_NEW_KEEP:
                        break;
                    case ACT_CONTINUE_CAMPAIGN:
                        LoadPlayerStateFromPreviousLevel();
                        break;
                    }
                }
            }
        }
    }

    // Rebuild background and visibility after state load (game mode may have changed,
    // affecting baked borders and territory fog). Must run AFTER LoadStrategicState().
    if (m_gameModeEnabled)
        TryLoadBackground();

    RefreshUI();

    // The reconstructed 640x480 strategic UI is now the default. The wx-based
    // implementation remains available from Strategic UI -> Current UI.
    SetOriginalStrategicUi(true);

    // Initialize timeout tracking so countdown markers are visible from turn 1
    CheckTimeouts();

    Bind(wxEVT_ACTIVATE, &StrategicLevelFrame::OnActivate, this);

    // Clear m_strategicLevel in parent when this window is closed/destroyed
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& ev) {
        if (m_main && m_main->m_strategicLevel == this)
            m_main->m_strategicLevel = nullptr;
        ev.Skip(); // proceed with default close/destroy
    });

}

void StrategicLevelFrame::StartFreshGameMode(const std::vector<LevelData::PlayerUnitAdd>& bonus_units)
{
    // Enable game mode
    m_gameModeEnabled = true;
    if (GetMenuBar())
    {
        auto* item = GetMenuBar()->FindItem(ID_MENU_GAME_MODE_TOGGLE);
        if (item) item->Check(m_gameModeEnabled);
    }

    // Set start territories as owned
    m_ownedTerritories = ChooseStartTerritories_NoBriefing(m_level, m_spellData);
    if (m_ownedTerritories.empty() && !m_level.territories.empty())
        m_ownedTerritories.push_back(m_level.territories.front().id);

    // Add bonus units from previous mission (e.g. rescued commando)
    for (const auto& bu : bonus_units)
        m_playerUnits.push_back(bu);

    while (m_unitStates.size() < m_playerUnits.size())
    {
        UnitInstanceState state;
        state.uid = m_nextRosterUid++;
        m_unitStates.push_back(state);
    }

    // Load cumulative research flags
    {
        namespace fs = std::filesystem;
        fs::path defPath = fs::path(m_level.source_path);
        m_levelResearchFlags = GetCumulativeResearchFlags(defPath);
    }

    // Apply territory visibility
    ApplyTerritoryVisibility();

    // Rebuild background with game mode borders
    TryLoadBackground();

    // Player rank is derived solely from the player-XP column in HODNOSTI.DEF.
    // At campaign start XP is legitimately zero, but that still means Captain
    // (rank 2), because the first three player XP thresholds are all zero.
    LoadRanksTable();
    RecomputePlayerRank();

    // Process immediate level events (AbsTime(-1) = fires right away, e.g. E02_0001 intro text)
    ProcessLevelEvents();

    // Save initial state
    SaveStrategicState();

    RefreshUI();
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

// ------------------------------------------------------------------
// Unit-state persistence glue (same pattern as research)
// ------------------------------------------------------------------
struct UnitStatePersistSaveView
{
    const std::vector<StrategicLevelFrame::UnitInstanceState>* states = nullptr;
};

struct UnitStatePersistLoadView
{
    std::vector<StrategicLevelFrame::UnitInstanceState>* states = nullptr;
};

static thread_local const UnitStatePersistSaveView* g_unitStatePersistSave = nullptr;
static thread_local UnitStatePersistLoadView* g_unitStatePersistLoad = nullptr;

// ------------------------------------------------------------------
// Mission-flow persistence: time limits, campaign events and counter-attacks.
// These are strategic state too; resetting them on load changes the campaign.
// ------------------------------------------------------------------
struct MissionFlowPersistSaveView
{
    const std::unordered_map<int, int>* timeoutTurn = nullptr;
    const std::set<int>* triggeredEvents = nullptr;
    const std::unordered_map<int, int>* activatedEvents = nullptr;
    const std::vector<StrategicLevelFrame::CounterAttackState>* counterAttacks = nullptr;
};

struct MissionFlowPersistLoadView
{
    std::unordered_map<int, int>* timeoutTurn = nullptr;
    std::set<int>* triggeredEvents = nullptr;
    std::unordered_map<int, int>* activatedEvents = nullptr;
    std::vector<StrategicLevelFrame::CounterAttackState>* counterAttacks = nullptr;
};

static thread_local const MissionFlowPersistSaveView* g_missionFlowPersistSave = nullptr;
static thread_local MissionFlowPersistLoadView* g_missionFlowPersistLoad = nullptr;

static std::filesystem::path FindPreviousLevelSavePath(const LevelData& currentLevel);

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
    options->Append(ID_MENU_OPTIONS_SCREEN, (L"&Screen...\tCtrl+B"));
    bar->Append(options, "&Options");

    auto* game = new wxMenu();
    game->AppendCheckItem(ID_MENU_GAME_MODE_TOGGLE, L"&Enabled");
    bar->Append(game, "&Game mode");

    // Parallel UI switch: the old implementation remains intact and usable.
    auto* strategicUi = new wxMenu();
    strategicUi->AppendRadioItem(ID_MENU_STRATEGIC_UI_CURRENT, L"&Current / wx UI");
    strategicUi->AppendRadioItem(ID_MENU_STRATEGIC_UI_ORIGINAL, L"&Reconstructed original UI");
    strategicUi->Check(ID_MENU_STRATEGIC_UI_ORIGINAL, true);
    bar->Append(strategicUi, "Strategic &UI");

    SetMenuBar(bar);

    Bind(wxEVT_MENU, &StrategicLevelFrame::OnSaveGame, this, ID_MENU_SAVE_GAME);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnLoadGame, this, ID_MENU_LOAD_GAME);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnOptionsAudio, this, ID_MENU_OPTIONS_AUDIO);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnOptionsScreen, this, ID_MENU_OPTIONS_SCREEN);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnToggleGameMode, this, ID_MENU_GAME_MODE_TOGGLE);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnStrategicUiCurrent, this, ID_MENU_STRATEGIC_UI_CURRENT);
    Bind(wxEVT_MENU, &StrategicLevelFrame::OnStrategicUiOriginal, this, ID_MENU_STRATEGIC_UI_ORIGINAL);

}


void StrategicLevelFrame::OnStrategicUiCurrent(wxCommandEvent&)
{
    SetOriginalStrategicUi(false);
}

void StrategicLevelFrame::OnStrategicUiOriginal(wxCommandEvent&)
{
    SetOriginalStrategicUi(true);
}

void StrategicLevelFrame::SetOriginalStrategicUi(bool enabled)
{
    m_originalStrategicUi = enabled;

    if (auto* bar = GetMenuBar())
    {
        bar->Check(ID_MENU_STRATEGIC_UI_CURRENT, !enabled);
        bar->Check(ID_MENU_STRATEGIC_UI_ORIGINAL, enabled);
    }

    if (!m_rootPanel)
        return;

    if (m_originalStrategicPanel)
        m_originalStrategicPanel->Show(enabled);

    if (enabled)
    {
        // No legacy wx controls are layered over the restored framebuffer.
        if (m_normalLayoutPanel) m_normalLayoutPanel->Hide();
        if (m_buyMainPanel) m_buyMainPanel->Hide();
        if (m_unitsMainPanel) m_unitsMainPanel->Hide();
        m_originalStrategicDirty = true;
        RefreshOriginalStrategicView();
    }
    else
    {
        // Return exactly to the old root mode that was active before switching.
        if (m_normalLayoutPanel) m_normalLayoutPanel->Show(!m_buyModeActive && !m_unitsModeActive);
        if (m_buyMainPanel) m_buyMainPanel->Show(m_buyModeActive);
        if (m_unitsMainPanel) m_unitsMainPanel->Show(m_unitsModeActive);
        if (m_originalStrategicPanel) m_originalStrategicPanel->Hide();
    }

    m_rootPanel->Layout();
    Layout();
}

static int StrategicLevelNumberFromPath(const std::string& sourcePath)
{
    const std::string name = std::filesystem::path(sourcePath).filename().string();
    std::smatch match;
    std::regex re("LEVEL[_-]?(\\d{1,2})", std::regex_constants::icase);
    if (std::regex_search(name, match, re) && match.size() >= 2)
    {
        try { return std::stoi(match[1].str()); }
        catch (...) {}
    }
    return -1;
}

void StrategicLevelFrame::RefreshOriginalStrategicView()
{
    if (!m_originalStrategicPanel)
        return;
    if (!m_originalStrategicDirty && m_originalStrategicBitmap.IsOk())
        return;

    m_originalStrategicError.clear();

    if (!m_spellData)
    {
        m_originalStrategicBitmap = wxBitmap();
        m_originalStrategicError = "Original UI: SpellData is not available.";
        m_originalStrategicDirty = false;
        m_originalStrategicPanel->Refresh();
        return;
    }

    const int levelNum = StrategicLevelNumberFromPath(m_level.source_path);
    if (levelNum < 0)
    {
        m_originalStrategicBitmap = wxBitmap();
        m_originalStrategicError = "Original UI: cannot determine LEVEL_XX from: " +
            wxString::FromUTF8(m_level.source_path.c_str());
        m_originalStrategicDirty = false;
        m_originalStrategicPanel->Refresh();
        return;
    }
    if (levelNum < 2)
    {
        m_originalStrategicBitmap = wxBitmap();
        m_originalStrategicError = wxString::Format(
            "Original UI: LEVEL_%02d is the intro/tactical level and has no original strategic map.",
            levelNum);
        m_originalStrategicDirty = false;
        m_originalStrategicPanel->Refresh();
        return;
    }

    if (m_visibleTerritory.empty())
        ApplyTerritoryVisibility();

    StrategicOriginalRenderer::MapState state;
    state.level = levelNum;
    state.hoverTerritory = (m_originalStrategicScreen == OriginalStrategicScreen::Map)
        ? m_hoverTerritory : 0;
    state.animationPhase = m_originalStrategicAnimPhase;
    int maxId = 0;
    for (const auto& t : m_level.territories)
        maxId = std::max(maxId, t.id);
    state.territories.assign(static_cast<std::size_t>(std::max(1, maxId + 1)),
        StrategicOriginalRenderer::TerritoryVisualState::Hidden);

    for (const auto& t : m_level.territories)
    {
        const int tid = t.id;
        if (tid <= 0 || tid >= static_cast<int>(state.territories.size()))
            continue;

        const bool owned = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid)
            != m_ownedTerritories.end();
        const bool visible = !m_gameModeEnabled ||
            (tid < static_cast<int>(m_visibleTerritory.size()) && m_visibleTerritory[tid] != 0);

        state.territories[static_cast<std::size_t>(tid)] = owned
            ? StrategicOriginalRenderer::TerritoryVisualState::Revealed
            : (visible ? StrategicOriginalRenderer::TerritoryVisualState::EnemyHatched
                       : StrategicOriginalRenderer::TerritoryVisualState::Hidden);
    }

    // Asset loader is deliberately redundant. Normally COMMON.FS is opened with
    // DELZ_ALL and returns decoded LZ resources. In real user builds, however,
    // strategic DEF files may be opened from an extracted temp tree, COMMON.FS
    // may come from another installation, or an older cache may be in use.
    // Try the archive first, then the known decoded/exported folders.
    std::vector<std::filesystem::path> originalAssetDirs;
    auto addAssetDir = [&](const std::filesystem::path& d)
    {
        if (d.empty()) return;
        std::error_code ec;
        if (!std::filesystem::exists(d, ec) || !std::filesystem::is_directory(d, ec))
            return;
        for (const auto& e : originalAssetDirs)
            if (e == d) return;
        originalAssetDirs.push_back(d);
    };

    addAssetDir(std::filesystem::path(m_level.source_path).parent_path());
    addAssetDir(std::filesystem::current_path() / "temp" / "COMMON");
    addAssetDir(std::filesystem::current_path() / "temp" / "common");
    addAssetDir(std::filesystem::path(m_spellData->data_path));
    addAssetDir(std::filesystem::path(m_spellData->data_path) / "COMMON");
    addAssetDir(std::filesystem::path(m_spellData->data_path) / "common");
    addAssetDir(std::filesystem::path(m_spellData->cd_data_path));
    addAssetDir(std::filesystem::path(m_spellData->cd_data_path) / "COMMON");
    addAssetDir(std::filesystem::path(m_spellData->cd_data_path) / "common");

    auto loader = [this, originalAssetDirs](const std::string& name, std::vector<std::uint8_t>& out) -> bool
    {
        out.clear();

        // 1) Preferred source: live COMMON.FS archive.
        if (m_spellData && m_spellData->GetCommonFS())
        {
            std::uint8_t* data = nullptr;
            int size = 0;
            if (m_spellData->GetCommonFS()->GetFile(name.c_str(), &data, &size) == 0 && data && size > 0)
            {
                out.assign(data, data + size);
                return true;
            }
        }

        // 2) Fallback: decoded/exported files (notably Release/temp/COMMON).
        for (const auto& dir : originalAssetDirs)
        {
            const std::filesystem::path p = dir / std::filesystem::path(name);
            std::ifstream f(p, std::ios::binary);
            if (!f)
                continue;
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            if (!out.empty())
                return true;
        }
        return false;
    };

    StrategicOriginalRenderer renderer;
    StrategicOriginalRenderer::RgbImage rgb;
    std::string error;
    bool rendered = false;
    switch (m_originalStrategicScreen)
    {
    case OriginalStrategicScreen::Hierarchy:
        rendered = renderer.RenderHierarchy(loader, rgb, &error);
        break;
    case OriginalStrategicScreen::Units:
        rendered = renderer.RenderUnits(loader, rgb, &error);
        break;
    case OriginalStrategicScreen::Buy:
    {
        StrategicOriginalRenderer::BuyState buyState;
        GetOriginalBuyLimits(buyState.maxPermanentUnits, buyState.maxCommanders);
        rendered = renderer.RenderBuy(loader, buyState, rgb, &error);
        break;
    }
    case OriginalStrategicScreen::Research:
        EnsureResearchLoaded();
        rendered = renderer.RenderResearch(loader, rgb, &error);
        break;
    case OriginalStrategicScreen::Info:
        EnsureResearchLoaded();
        rendered = renderer.RenderInfo(loader, rgb, &error);
        break;
    case OriginalStrategicScreen::Resources:
        RefreshResourcesPage();
        rendered = renderer.RenderResources(loader, rgb, &error);
        break;
    case OriginalStrategicScreen::Stats:
        LoadRanksTable();
        LoadMissionStatsIfPresent();
        RecomputePlayerRank();
        rendered = renderer.RenderStats(loader, rgb, &error);
        break;
    case OriginalStrategicScreen::Options:
    {
        StrategicOriginalRenderer::OptionsState optionsState;
        SpellMap* spellMap = m_main ? m_main->GetSpellMap() : nullptr;
        const double gamma = spellMap ? spellMap->GetGamma() : 1.3;
        optionsState.gammaPercent = std::clamp(
            static_cast<int>(std::lround((gamma - 0.5) * (100.0 / 1.5))), 0, 100);
        if (m_spellData && m_spellData->midi)
            optionsState.musicPercent = std::clamp(
                static_cast<int>(std::lround(m_spellData->midi->GetVolume() * 100.0)), 0, 100);
        if (m_spellData && m_spellData->sounds && m_spellData->sounds->channels)
            optionsState.soundPercent = std::clamp(
                static_cast<int>(std::lround(m_spellData->sounds->channels->GetVolume() * 100.0)), 0, 100);
        rendered = renderer.RenderOptions(loader, optionsState, rgb, &error);
        break;
    }
    case OriginalStrategicScreen::Map:
    default:
        rendered = renderer.RenderStrategicMap(loader, state, rgb, &error);
        break;
    }
    if (!rendered || rgb.rgb.empty())
    {
        m_originalStrategicBitmap = wxBitmap();
        m_originalStrategicError = "Original UI renderer: " + wxString::FromUTF8(error.c_str());
        m_originalStrategicDirty = false;
        m_originalStrategicPanel->SetToolTip(m_originalStrategicError);
        m_originalStrategicPanel->Refresh();
        return;
    }

    OriginalUiPalette nativeUiPal{};
    const bool haveNativeUiPal = OriginalBuildUiPalette(loader, nativeUiPal);

    wxImage image(rgb.width, rgb.height, true);
    if (image.IsOk() && image.GetData())
        std::memcpy(image.GetData(), rgb.rgb.data(), rgb.rgb.size());

    // --------------------------------------------------------------------
    // Dynamic original-map layer. Everything below is drawn into the same
    // logical 640x480 framebuffer; no legacy wx controls overlap this view.
    // --------------------------------------------------------------------
    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Map)
    {
        SpellFont* font = m_spellData->font;
        const wxColour listBg(24, 61, 26);
        const wxColour grid(31, 76, 32);
        const wxColour frame(135, 132, 120);
        const wxColour text(218, 222, 211);
        const wxColour green(0, 242, 0);
        const wxColour dim(126, 132, 118);
        const wxColour cooldown(132, 72, 72);

        // Central mission-unit panel (original game builds this dynamically).
        OriginalFillRect(image, kOriginalListX, kOriginalListY,
            kOriginalListW, kOriginalListH, listBg);
        OriginalVLine(image, kOriginalListX - 2, kOriginalListY,
            kOriginalListY + kOriginalListH - 1, frame);
        OriginalVLine(image, kOriginalListX + kOriginalListW,
            kOriginalListY, kOriginalListY + kOriginalListH - 1, frame);

        // Subtle grid under the rows, matching the DOS list surface.
        for (int x = kOriginalListX + 2; x < kOriginalListX + kOriginalListW; x += 18)
            OriginalVLine(image, x, 56, 431, grid);
        for (int y = 56; y < 432; y += 18)
            OriginalHLine(image, kOriginalListX + 1, kOriginalListX + kOriginalListW - 1, y, grid);

        // Scrollbar and thumb. Mouse wheel drives this in the restored view.
        OriginalFillRect(image, 556, 6, 18, 426, wxColour(74, 72, 66));
        OriginalFillRect(image, 558, 23, 14, 391, wxColour(26, 39, 25));
        OriginalHLine(image, 556, 573, 21, wxColour(182, 178, 166));
        OriginalHLine(image, 556, 573, 415, wxColour(182, 178, 166));
        OriginalDrawSpellText(image, font, "^", 557, 7, text, 16, true);
        OriginalDrawSpellText(image, font, "v", 557, 416, text, 16, true);

        // Ensure one UID per rendered unit instance, shared with the current UI.
        int totalRows = 0;
        for (const auto& u : m_playerUnits)
            totalRows += std::max(0, u.count);
        if (static_cast<int>(m_rosterRowUids.size()) != totalRows)
        {
            m_rosterRowUids.clear();
            m_rosterRowUids.reserve(static_cast<size_t>(totalRows));
            for (int i = 0; i < totalRows; ++i)
                m_rosterRowUids.push_back(m_nextRosterUid++);
        }
        const int maxScroll = std::max(0, totalRows - kOriginalVisibleRows);
        m_originalUnitScroll = std::clamp(m_originalUnitScroll, 0, maxScroll);
        if (maxScroll > 0)
        {
            const int trackY = 25;
            const int trackH = 387;
            const int thumbH = std::max(18, trackH * kOriginalVisibleRows / std::max(kOriginalVisibleRows, totalRows));
            const int thumbY = trackY + (trackH - thumbH) * m_originalUnitScroll / maxScroll;
            OriginalFillRect(image, 559, thumbY, 12, thumbH, wxColour(116, 114, 104));
            OriginalHLine(image, 559, 570, thumbY, wxColour(206, 202, 190));
            OriginalHLine(image, 559, 570, thumbY + thumbH - 1, wxColour(40, 39, 36));
        }

        OriginalDrawSpellText(image, font, L"Speci\u00E1ln\u00ED", 420, 8, text, 132, true);
        OriginalDrawSpellText(image, font, L"Vyber v\u0161echny", 423, 27, text);
        OriginalDrawSpellText(image, font, L"Odzna\u010D v\u0161echny", 423, 42, text);
        OriginalDrawSpellText(image, font, L"Jednotky", 420, 66, dim, 132, true);

        int flatRow = 0;
        int drawn = 0;
        for (size_t pIdx = 0; pIdx < m_playerUnits.size() && drawn < kOriginalVisibleRows; ++pIdx)
        {
            const auto& unit = m_playerUnits[pIdx];
            const bool onCooldown = pIdx < m_unitStates.size() && m_unitStates[pIdx].cooldown_turns > 0;
            for (int inst = 0; inst < unit.count && drawn < kOriginalVisibleRows; ++inst, ++flatRow)
            {
                if (flatRow < m_originalUnitScroll)
                    continue;
                const uint32_t uid = flatRow < static_cast<int>(m_rosterRowUids.size())
                    ? m_rosterRowUids[static_cast<size_t>(flatRow)] : 0;
                const bool selected = uid && m_selectedUnitsForMission.count(uid) > 0;
                wxString label = GetUnitDisplayName(unit.unit_id);
                if (onCooldown && pIdx < m_unitStates.size())
                    label += wxString::Format("  -%dT", m_unitStates[pIdx].cooldown_turns);
                OriginalDrawSpellText(image, font, label, 423,
                    kOriginalUnitRowsY + drawn * kOriginalUnitRowH,
                    onCooldown ? cooldown : (selected ? green : text), 130);
                ++drawn;
            }
        }

        // If there are no player units, keep the panel self-explanatory.
        if (totalRows == 0)
            OriginalDrawSpellText(image, font, L"-- bez jednotek --", 421, 84, dim, 130, true);

        // Bottom action buttons use the actual BIGMB DOS button sprites.  The
        // previous hand-drawn rectangles were the source of the conspicuous
        // black boxes around Attack/Cancel in the reconstructed screen.
        auto drawActionButton = [&](int x, int hoverIndex, const wxString& caption, bool enabled)
        {
            bool nativeDrawn = false;
            if (haveNativeUiPal)
            {
                const char* asset = !enabled ? "BIGMB__D.BTN"
                    : (m_originalActionHover == hoverIndex ? "BIGMB__A.BTN" : "BIGMB__N.BTN");
                wxImage plate = OriginalLoadIcoLike(loader, asset, nativeUiPal);
                nativeDrawn = OriginalBlitImage(image, plate, x, kOriginalAttackDrawY);
            }
            if (!nativeDrawn)
            {
                OriginalFillRect(image, x, kOriginalAttackDrawY, kOriginalButtonW, 28,
                    enabled ? wxColour(13, 51, 10) : wxColour(22, 34, 20));
            }
            OriginalDrawSpellText(image, font, caption, x, kOriginalAttackDrawY + 7,
                enabled ? text : dim, kOriginalButtonW, true);
        };
        const bool canAttack = m_selectedTerritory > 0 && !m_selectedUnitsForMission.empty();
        drawActionButton(kOriginalAttackDrawX, 0, L"\u00DAtok", canAttack);
        drawActionButton(kOriginalCancelDrawX, 1, L"Zru\u0161it",
            !m_selectedUnitsForMission.empty() || !m_selectedCommandersForMission.empty());

        // Original status panel values.
        OriginalDrawSpellText(image, font, L"Pen\u00EDze", 578, 17, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_money), 578, 31, text, 61, true);
        OriginalDrawSpellText(image, font, L"V\u00FDzkum", 578, 47, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_research), 578, 61, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 578, 77, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_turn), 578, 91, text, 61, true);

        // Idle lower-right turn label.  The common post-pass below overlays
        // the native ET_BTN0 sprite as a left-to-right wipe on mouse hover.
        OriginalDrawSpellText(image, font, L"Kolo", 579, 436, text, 60, true);
        OriginalDrawSpellText(image, font, wxString::Format("%02d", m_turn), 579, 452, text, 60, true);

        // Territory briefing text in the original lower frame.
        const wxString briefing = OriginalCleanBriefing(m_originalBriefingText);
        if (!briefing.empty())
        {
            const auto lines = OriginalWrapText(font, briefing, 356, 8);
            const int fh = std::max(1, font->GetHeight());
            const int blockH = static_cast<int>(lines.size()) * fh;
            int y = 323 + std::max(0, (130 - blockH) / 2);
            for (const auto& line : lines)
            {
                OriginalDrawSpellText(image, font, line, 22, y, text, 374, true);
                y += fh;
            }
        }
        else
        {
            OriginalDrawSpellText(image, font, L"Vyber \u00FAzem\u00ED na map\u011B.", 22, 366, dim, 374, true);
        }
    }

    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Hierarchy)
    {
        SpellFont* font = m_spellData->font;
        const wxColour listBg(24, 61, 26);
        const wxColour grid(31, 76, 32);
        const wxColour frame(135, 132, 120);
        const wxColour text(218, 222, 211);
        const wxColour green(0, 242, 0);
        const wxColour heading(232, 232, 0);
        const wxColour dim(126, 132, 118);

        // The hierarchy artwork contains the complete left-hand tree, but the
        // right roster is populated dynamically by the original game.
        OriginalFillRect(image, kOriginalListX, kOriginalListY,
            kOriginalListW, kOriginalListH, listBg);
        OriginalVLine(image, kOriginalListX - 2, kOriginalListY,
            kOriginalListY + kOriginalListH - 1, frame);
        OriginalVLine(image, kOriginalListX - 1, kOriginalListY,
            kOriginalListY + kOriginalListH - 1, wxColour(40, 40, 34));
        OriginalVLine(image, kOriginalListX + kOriginalListW,
            kOriginalListY, kOriginalListY + kOriginalListH - 1, frame);
        OriginalHLine(image, kOriginalListX - 2, 574, kOriginalListY, frame);
        OriginalHLine(image, kOriginalListX - 2, 574, kOriginalListY + kOriginalListH - 1, frame);
        for (int x = kOriginalListX + 2; x < kOriginalListX + kOriginalListW; x += 18)
            OriginalVLine(image, x, 8, 431, grid);
        for (int y = 8; y < 432; y += 18)
            OriginalHLine(image, kOriginalListX + 1, kOriginalListX + kOriginalListW - 1, y, grid);

        // Native list scrollbar. The old branch had only the list contents; the
        // original game also has dedicated up/down buttons and a narrow track.
        struct HierarchyPoolUnitRow
        {
            int playerIndex = -1;
            uint32_t uid = 0;
        };
        int hierarchyTotalRows = 0;
        for (const auto& u : m_playerUnits)
            hierarchyTotalRows += std::max(0, u.count);
        if (static_cast<int>(m_rosterRowUids.size()) != hierarchyTotalRows)
        {
            m_rosterRowUids.clear();
            m_rosterRowUids.reserve(static_cast<size_t>(hierarchyTotalRows));
            for (int i = 0; i < hierarchyTotalRows; ++i)
                m_rosterRowUids.push_back(m_nextRosterUid++);
        }

        std::vector<HierarchyPoolUnitRow> hierarchyFlatUnits;
        hierarchyFlatUnits.reserve(static_cast<size_t>(hierarchyTotalRows));
        int hierarchyUidIndex = 0;
        for (size_t pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
        {
            for (int inst = 0; inst < std::max(0, m_playerUnits[pIdx].count); ++inst)
            {
                HierarchyPoolUnitRow row;
                row.playerIndex = static_cast<int>(pIdx);
                if (hierarchyUidIndex < static_cast<int>(m_rosterRowUids.size()))
                    row.uid = m_rosterRowUids[static_cast<size_t>(hierarchyUidIndex)];
                hierarchyFlatUnits.push_back(row);
                ++hierarchyUidIndex;
            }
        }
        constexpr int maxUnitRows = 18;
        const int hierarchyMaxScroll = std::max(0, static_cast<int>(hierarchyFlatUnits.size()) - maxUnitRows);
        m_originalHierarchyUnitScroll = std::clamp(m_originalHierarchyUnitScroll, 0, hierarchyMaxScroll);

        OriginalDrawScrollButton(image, 553, 6, true, hierarchyMaxScroll > 0);
        OriginalDrawScrollButton(image, 553, 404, false, hierarchyMaxScroll > 0);
        OriginalDrawScrollTrack(image, 553, 34, 370, m_originalHierarchyUnitScroll, hierarchyMaxScroll,
            maxUnitRows, static_cast<int>(hierarchyFlatUnits.size()));

        OriginalDrawSpellText(image, font, L"Jednotky", 420, 12, heading, 132, true);
        const int unitTextH = std::max(1, font->GetHeight());
        int unitRows = 0;
        for (int flat = m_originalHierarchyUnitScroll;
             flat < static_cast<int>(hierarchyFlatUnits.size()) && unitRows < maxUnitRows; ++flat)
        {
            const auto& poolRow = hierarchyFlatUnits[static_cast<size_t>(flat)];
            const int pIdx = poolRow.playerIndex;
            if (pIdx < 0 || pIdx >= static_cast<int>(m_playerUnits.size()))
                continue;
            const int rowY = 27 + unitRows * 14;
            const bool selectedPoolUnit =
                m_originalHierarchyPoolSelectionKind == OriginalHierarchyPoolSelectionKind::Unit &&
                poolRow.uid != 0 && poolRow.uid == m_originalHierarchySelectedUnitUid;
            if (selectedPoolUnit)
                OriginalFillRect(image, 421, rowY, 129, 13, wxColour(35, 84, 29));
            OriginalDrawSpellText(image, font, GetUnitDisplayName(m_playerUnits[static_cast<size_t>(pIdx)].unit_id),
                424, rowY + std::max(0, (14 - unitTextH) / 2), selectedPoolUnit ? heading : text, 126);
            ++unitRows;
        }
        if (hierarchyFlatUnits.empty())
            OriginalDrawSpellText(image, font, L"-- bez jednotek --", 421, 30, dim, 130, true);

        OriginalDrawSpellText(image, font, L"Velitelé", 420, 292, heading, 132, true);
        int cy = 309;
        int cmdRows = 0;
        for (const auto& commander : m_playerCommanders)
        {
            if (cmdRows >= 8)
                break;
            const bool selectedPoolCommander =
                m_originalHierarchyPoolSelectionKind == OriginalHierarchyPoolSelectionKind::Commander &&
                commander.uid != 0 && commander.uid == m_originalHierarchySelectedCommanderUid;
            if (selectedPoolCommander)
                OriginalFillRect(image, 421, cy, 129, 13, wxColour(35, 84, 29));
            const wxString label = GetRankAbbrev(commander.rank) + " " + wxString::FromUTF8(commander.name);
            OriginalDrawSpellText(image, font, label, 424, cy, selectedPoolCommander ? heading : text, 126);
            cy += 14;
            ++cmdRows;
        }
        if (cmdRows == 0)
            OriginalDrawSpellText(image, font, L"-- bez velitelů --", 421, 310, dim, 130, true);

        // Live hierarchy contents. Coordinates exactly match the openings in
        // HIERARCH.LZ. Empty normal slots remain empty; commander's assignment
        // slot keeps the original green question mark until a unit is chosen.
        const auto hitSlots = OriginalHierarchySlotsForPage(m_originalHierarchyPage);
        for (const auto& hs : hitSlots)
        {
            auto it = m_hierarchySlotIndex.find(hs.id);
            if (it == m_hierarchySlotIndex.end())
                continue;
            const HierarchySlot& slot = m_hierarchySlots[it->second];
            wxString label;
            wxColour colour = text;
            if (hs.commander)
            {
                if (slot.commander_uid != 0 || !slot.commander_name.empty())
                {
                    label = GetRankAbbrev(slot.rank);
                    if (!slot.commander_name.empty())
                        label += " " + wxString::FromUTF8(slot.commander_name);
                }
            }
            else if (slot.unit_uid != 0 && !slot.unit_display.empty())
            {
                label = slot.unit_display;
            }
            else
            {
                const bool assignmentSlot = hs.id.find("_commander_unit") != std::string::npos ||
                    (hs.id.rfind("regiment_", 0) == 0 && hs.id.size() >= 5 &&
                        hs.id.compare(hs.id.size() - 5, 5, "_unit") == 0) ||
                    (hs.id.rfind("brigade_", 0) == 0 && hs.id.size() >= 5 &&
                        hs.id.compare(hs.id.size() - 5, 5, "_unit") == 0);
                if (assignmentSlot)
                {
                    label = L"?";
                    colour = green;
                }
            }

            if (!label.empty())
            {
                const int textY = OriginalCenteredTextY(font, hs.rect.y, hs.rect.height);
                const bool centerPlaceholder = (label == L"?");
                // Exact original geometry: permanent-unit rows have a small
                // status cell at their left edge, while commander/assignment
                // boxes start text almost immediately after the frame.
                const int labelPad = (hs.rect.x == 12) ? 16 : 2;
                OriginalDrawSpellText(image, font, label,
                    centerPlaceholder ? hs.rect.x : hs.rect.x + labelPad, textY, colour,
                    std::max(1, centerPlaceholder ? hs.rect.width : hs.rect.width - labelPad - 1),
                    centerPlaceholder);
            }
        }

        // Native lower-right page selector.
        const wxRect partButton(323, 439, 72, 28);
        OriginalFillRect(image, partButton.x, partButton.y, partButton.width, partButton.height,
            wxColour(13, 51, 10));
        OriginalHLine(image, partButton.x, partButton.GetRight(), partButton.y, wxColour(38, 94, 33));
        OriginalVLine(image, partButton.x, partButton.y, partButton.GetBottom(), wxColour(38, 94, 33));
        OriginalHLine(image, partButton.x, partButton.GetRight(), partButton.GetBottom(), wxColour(3, 17, 3));
        OriginalVLine(image, partButton.GetRight(), partButton.y, partButton.GetBottom(), wxColour(3, 17, 3));
        OriginalDrawSpellText(image, font,
            wxString::Format(L"\u010C\u00E1st %d", m_originalHierarchyPage),
            partButton.x, partButton.y + 7, green, partButton.width, true);

        // Common original status panel values.
        OriginalDrawSpellText(image, font, L"Pen\u00EDze", 578, 17, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_money), 578, 31, text, 61, true);
        OriginalDrawSpellText(image, font, L"V\u00FDzkum", 578, 47, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_research), 578, 61, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 578, 77, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_turn), 578, 91, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 579, 436, text, 60, true);
        OriginalDrawSpellText(image, font, wxString::Format("%02d", m_turn), 579, 452, text, 60, true);
    }


    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Units)
    {
        SpellFont* font = m_spellData->font;
        // These are sampled directly from the original 640x480 unit-management
        // screenshot.  The previous pass used the brighter map-screen colours,
        // which made this page look much newer than the DOS original.
        const wxColour text(150, 150, 150);
        const wxColour green(4, 219, 4);
        const wxColour yellow(231, 227, 5);
        const wxColour dim(77, 77, 77);
        const wxColour warn(232, 150, 72);
        const wxColour listBg(32, 60, 20);
        const wxColour listGrid(40, 77, 32);
        const wxColour listFrame(77, 77, 77);
        const wxColour selectedBand(77, 109, 69);
        const wxColour buttonBg(13, 51, 10);
        const wxColour buttonOff(22, 34, 20);
        const wxColour edgeHi(38, 94, 33);
        const wxColour edgeLo(3, 17, 3);

        EnsureUnitCostsLoaded();
        EnsureUpgradeDefsLoaded();
        EnsureResearchLoaded();

        if (m_unitsSelectedUnit < 0 && !m_playerUnits.empty())
            m_unitsSelectedUnit = 0;
        if (m_unitsSelectedUnit >= static_cast<int>(m_playerUnits.size()))
            m_unitsSelectedUnit = m_playerUnits.empty() ? -1 : static_cast<int>(m_playerUnits.size()) - 1;

        // -----------------------------------------------------------------
        // LEFT ROSTER
        // Original geometry is 16 permanent rows at y=8 + N*19.  The left
        // half is the company/custom name, the right half is its unit type;
        // it is NOT a textual "OK" status column.
        // -----------------------------------------------------------------
        std::vector<int> flatUnits;
        for (size_t pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
            for (int inst = 0; inst < std::max(0, m_playerUnits[pIdx].count); ++inst)
                flatUnits.push_back(static_cast<int>(pIdx));

        // Measured against a native 640x480 DOS screenshot.  UNITS.LZ starts
        // at (6,8), but the first live row is two pixels lower than the raw
        // layer origin.  Keeping the data geometry separate from the layer
        // geometry avoids the systematic up/left drift seen in Stage 4.4.
        constexpr int rosterY = 10;
        constexpr int rosterRowH = 19;
        constexpr int visibleRosterRows = 16;
        const int maxRosterScroll = std::max(0, static_cast<int>(flatUnits.size()) - visibleRosterRows);
        m_originalUnitsRosterScroll = std::clamp(m_originalUnitsRosterScroll, 0, maxRosterScroll);

        for (int row = 0; row < visibleRosterRows; ++row)
        {
            const int flat = m_originalUnitsRosterScroll + row;
            if (flat >= static_cast<int>(flatUnits.size()))
                break;
            const int pIdx = flatUnits[static_cast<size_t>(flat)];
            if (pIdx < 0 || pIdx >= static_cast<int>(m_playerUnits.size()))
                continue;

            const auto& u = m_playerUnits[static_cast<size_t>(pIdx)];
            wxString typeName = GetUnitDisplayName(u.unit_id);
            wxString companyName = typeName;
            if (pIdx < static_cast<int>(m_unitStates.size()) && !m_unitStates[pIdx].custom_name.empty())
                companyName = wxString::FromUTF8(m_unitStates[pIdx].custom_name);

            const int y = rosterY + row * rosterRowH;
            // SpellFont's visible glyph body sits a pixel above the nominal
            // line box; the DOS screen places it one pixel lower.
            const int ty = OriginalCenteredTextY(font, y, 17) + 1;
            const bool selected = pIdx == m_unitsSelectedUnit;

            // The original marks the active row by its green frame/status bar,
            // while the text itself stays the normal grey.
            if (selected)
            {
                // VMU_SLCT.LZ is 146x17: x=16..161 in the native screen.
                OriginalHLine(image, 16, 161, y, green);
                OriginalHLine(image, 16, 161, y + 16, green);
                OriginalVLine(image, 16, y, y + 16, green);
                OriginalVLine(image, 161, y, y + 16, green);
            }

            // Small original status bars sit in the top edge of both cells.
            const int hpBar = std::clamp((std::max(0, std::min(100, u.health)) * 26) / 100, 0, 26);
            if (hpBar > 0)
            {
                // Exact native bar origins sampled from the original screen.
                OriginalHLine(image, 38, 38 + hpBar, y, green);
                OriginalHLine(image, 190, 190 + hpBar, y, green);
            }

            // Native text origins are (31,...) and (183,...).  The previous
            // 28/180 anchors were the main reason the page looked left-shifted.
            OriginalDrawSpellText(image, font, companyName, 31, ty, text, 129);
            OriginalDrawSpellText(image, font, typeName, 183, ty, text, 129);

            if (pIdx < static_cast<int>(m_unitStates.size()) && m_unitStates[pIdx].cooldown_turns > 0)
                OriginalDrawSpellText(image, font,
                    wxString::Format(L"%d", m_unitStates[pIdx].cooldown_turns), 152, ty, green, 9, true);
        }
        // Rows 17+ in UNITS.LZ are the original helper/temporary-unit area.
        // Keep them genuinely empty when the remake has no separate helper
        // collection instead of painting a modern heading over the grid.

        // -----------------------------------------------------------------
        // MODE PANEL
        // -----------------------------------------------------------------
        OriginalDrawSpellText(image, font, L"M\u00F3d", 336, 214, green, 77, true);
        const struct { UnitsTab tab; const wchar_t* label; int y; } modes[] = {
            { UNITS_TAB_UPGRADE, L"\u00DApravy", 232 },
            { UNITS_TAB_RECRUIT, L"N\u00E1bor", 247 },
            { UNITS_TAB_INFO,    L"Info", 262 }
        };
        for (const auto& m : modes)
        {
            const bool active = m_unitsCurrentTab == m.tab;
            OriginalDrawSpellText(image, font, m.label, 338, m.y,
                active ? yellow : green, 73, true);
            if (active)
            {
                // Green bracket/arrows are part of the selected mode in the
                // original game and make the yellow text much easier to read.
                const int cy = m.y + 6;
                OriginalVLine(image, 337, m.y - 2, m.y + 13, green);
                OriginalHLine(image, 337, 344, m.y - 2, green);
                OriginalHLine(image, 337, 344, m.y + 13, green);
                OriginalHLine(image, 407, 414, m.y - 2, green);
                OriginalHLine(image, 407, 414, m.y + 13, green);
                OriginalVLine(image, 414, m.y - 2, m.y + 13, green);
                OriginalHLine(image, 341, 346, cy, green);
                OriginalHLine(image, 405, 410, cy, green);
            }
        }

        int actionCost = -1;
        int actionTime = -1;
        bool actionEnabled = false;
        bool hasCooldown = false;

        // -----------------------------------------------------------------
        // UPPER-RIGHT ORIGINAL LIST WINDOW
        // This always shows researched/suitable unit improvements, grouped as
        // Motory / Zbraně / Obrana.  Recruitment choices belong in the large
        // lower info panel; putting them here was the largest visual mismatch
        // in Stage 4.x.
        // -----------------------------------------------------------------
        OriginalFillRect(image, 418, 6, 135, 281, listBg);
        OriginalVLine(image, 416, 6, 286, listFrame);
        OriginalVLine(image, 417, 6, 286, wxColour(40, 40, 34));
        OriginalVLine(image, 553, 6, 286, listFrame);
        OriginalHLine(image, 416, 574, 6, listFrame);
        OriginalHLine(image, 416, 574, 286, listFrame);
        for (int x = 420; x < 553; x += 18)
            OriginalVLine(image, x, 8, 286, listGrid);
        for (int y = 8; y < 287; y += 18)
            OriginalHLine(image, 419, 552, y, listGrid);

        struct UnitOptionRow
        {
            wxString label;
            int upgradeId = -1;
            int rearmUnitId = -1;
            bool heading = false;
        };
        std::vector<UnitOptionRow> optionRows;

        auto addUpgradeGroup = [&](UpgradeDefRec::Kind kind, const wxString& heading,
            const std::vector<int>& upgrades)
        {
            std::vector<int> ids;
            for (int id : upgrades)
            {
                const auto it = m_upgradeDefs.find(id);
                if (it != m_upgradeDefs.end() && it->second.kind == kind)
                    ids.push_back(id);
            }
            // Keep the three original group headings visible even when the
            // player has not researched a member of that group yet.
            optionRows.push_back({ heading, -1, -1, true });
            for (int id : ids)
            {
                const auto it = m_upgradeDefs.find(id);
                wxString label = (it != m_upgradeDefs.end() && !it->second.title.empty())
                    ? it->second.title : wxString();
                if (label.empty())
                    for (const auto& r : m_researchDb)
                        if (r.data == id && !r.title.empty()) { label = r.title; break; }
                if (label.empty()) label = wxString::Format(L"#%d", id);
                optionRows.push_back({ label, id, -1, false });
            }
        };

        std::vector<int> availableUpgrades;
        std::vector<int> rearmChoices;
        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < static_cast<int>(m_playerUnits.size()))
        {
            const auto& u = m_playerUnits[static_cast<size_t>(m_unitsSelectedUnit)];
            availableUpgrades = GetAvailableUpgradesForUnit(u.unit_id);
            rearmChoices = GetAvailableUnitTypesForUpgrade(u.unit_id);
            addUpgradeGroup(UpgradeDefRec::Engine, L"Motory", availableUpgrades);
            addUpgradeGroup(UpgradeDefRec::Weapon, L"Zbran\u011B", availableUpgrades);
            addUpgradeGroup(UpgradeDefRec::Armor, L"Obrana", availableUpgrades);
            if (m_unitsCurrentTab == UNITS_TAB_UPGRADE && !rearmChoices.empty())
            {
                optionRows.push_back({ L"Nov\u00FD typ", -1, -1, true });
                for (int id : rearmChoices)
                    optionRows.push_back({ GetUnitDisplayName(id), -1, id, false });
            }
        }

        constexpr int optionRowH = 14;
        constexpr int optionY = 8;
        constexpr int optionBottom = 258;
        const int visibleOptionRows = std::max(1, (optionBottom - optionY) / optionRowH);
        const int maxOptionScroll = std::max(0, static_cast<int>(optionRows.size()) - visibleOptionRows);
        m_originalUnitsOptionScroll = std::clamp(m_originalUnitsOptionScroll, 0, maxOptionScroll);

        for (int vr = 0; vr < visibleOptionRows; ++vr)
        {
            const int idx = m_originalUnitsOptionScroll + vr;
            if (idx >= static_cast<int>(optionRows.size()))
                break;
            const auto& r = optionRows[static_cast<size_t>(idx)];
            const int y = optionY + vr * optionRowH;
            if (r.heading)
            {
                OriginalDrawSpellText(image, font, r.label, 423, y + 1, text, 129, true);
                continue;
            }

            bool installed = false;
            if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < static_cast<int>(m_unitStates.size()) && r.upgradeId >= 0)
            {
                const auto& owned = m_unitStates[m_unitsSelectedUnit].upgrades;
                installed = std::find(owned.begin(), owned.end(), r.upgradeId) != owned.end();
            }
            const bool chosen = (r.upgradeId >= 0 && m_unitsSelectedUpgrade == r.upgradeId &&
                m_unitsSelectedRearmUnitId <= 0) ||
                (r.rearmUnitId >= 0 && m_unitsSelectedRearmUnitId == r.rearmUnitId);
            OriginalDrawSpellText(image, font, r.label, 426, y + 1,
                chosen ? yellow : (installed ? green : text), 123);
        }

        OriginalDrawScrollButton(image, 553, 6, true, maxOptionScroll > 0);
        OriginalDrawScrollButton(image, 553, 259, false, maxOptionScroll > 0);
        OriginalDrawScrollTrack(image, 553, 34, 225, m_originalUnitsOptionScroll,
            maxOptionScroll, visibleOptionRows, static_cast<int>(optionRows.size()));

        // -----------------------------------------------------------------
        // LARGE LOWER INFO/ACTION PANEL
        // -----------------------------------------------------------------
        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < static_cast<int>(m_playerUnits.size()))
        {
            const auto& u = m_playerUnits[static_cast<size_t>(m_unitsSelectedUnit)];
            hasCooldown = m_unitsSelectedUnit < static_cast<int>(m_unitStates.size()) &&
                m_unitStates[m_unitsSelectedUnit].cooldown_turns > 0;

            wxString unitName = GetUnitDisplayName(u.unit_id);
            if (m_unitsSelectedUnit < static_cast<int>(m_unitStates.size()) &&
                !m_unitStates[m_unitsSelectedUnit].custom_name.empty())
                unitName = wxString::FromUTF8(m_unitStates[m_unitsSelectedUnit].custom_name);

            OriginalDrawSpellText(image, font, unitName, 346, 296, text, 217);
            OriginalDrawSpellText(image, font, wxString(L"Typ: ") + GetUnitDisplayName(u.unit_id), 362, 312, text, 137);

            int maxHp = 100;
            if (m_spellData && m_spellData->units)
            {
                if (auto* rec = m_spellData->units->GetUnit(u.unit_id))
                    maxHp = std::max(1, rec->cnt);
            }
            const int curHp = std::clamp((maxHp * std::max(0, std::min(100, u.health)) + 50) / 100, 0, maxHp);
            OriginalDrawSpellText(image, font, wxString::Format(L"Stav: %d (%d)", curHp, maxHp), 362, 327, text, 137);
            const int lvl = m_unitsSelectedUnit < static_cast<int>(m_unitStates.size()) ?
                m_unitStates[m_unitsSelectedUnit].level : 0;
            const int xp = m_unitsSelectedUnit < static_cast<int>(m_unitStates.size()) ?
                m_unitStates[m_unitsSelectedUnit].experience : 0;
            OriginalDrawSpellText(image, font, wxString::Format(L"\u00DArove\u0148: %d", lvl), 362, 342, text, 137);
            OriginalDrawSpellText(image, font, wxString::Format(L"Zku\u0161enost: %d", xp), 362, 357, text, 137);

            // Original unit portrait/icon in the small gridded square.
            if (m_spellData && m_spellData->units)
            {
                auto* rec = m_spellData->units->GetUnit(u.unit_id);
                if (rec && rec->icon_glyph)
                {
                    wxBitmap* iconBmp = rec->icon_glyph->Render(56, 48);
                    if (iconBmp && iconBmp->IsOk())
                    {
                        wxImage src = iconBmp->ConvertToImage();
                        if (src.IsOk() && src.GetData())
                        {
                            unsigned char* dst = image.GetData();
                            const unsigned char* sd = src.GetData();
                            const unsigned char* sa = src.HasAlpha() ? src.GetAlpha() : nullptr;
                            const int sw = src.GetWidth();
                            const int sh = src.GetHeight();
                            for (int sy = 0; sy < sh; ++sy)
                                for (int sx = 0; sx < sw; ++sx)
                                {
                                    const size_t sp = static_cast<size_t>(sy) * sw + sx;
                                    const unsigned char rr = sd[sp * 3u + 0];
                                    const unsigned char gg = sd[sp * 3u + 1];
                                    const unsigned char bb = sd[sp * 3u + 2];
                                    if ((sa && sa[sp] == 0) || (!sa && rr < 12 && gg < 12 && bb < 12))
                                        continue;
                                    const int dx = 505 + sx;
                                    const int dy = 299 + sy;
                                    if (dx < 0 || dx >= image.GetWidth() || dy < 0 || dy >= image.GetHeight())
                                        continue;
                                    const size_t dp = (static_cast<size_t>(dy) * image.GetWidth() + dx) * 3u;
                                    dst[dp + 0] = rr; dst[dp + 1] = gg; dst[dp + 2] = bb;
                                }
                        }
                    }
                    delete iconBmp;
                }
            }

            if (m_unitsCurrentTab == UNITS_TAB_RECRUIT)
            {
                int q = (m_unitsSelectedUpgrade >= 0 && m_unitsSelectedUpgrade < RECRUIT_QUALITY_COUNT)
                    ? m_unitsSelectedUpgrade : 1;
                m_unitsSelectedUpgrade = q;
                const wchar_t* labels[RECRUIT_QUALITY_COUNT] = {
                    L"N\u00E1bor nov\u00E1\u010Dk\u016F", L"N\u00E1bor veter\u00E1n\u016F", L"N\u00E1bor elity"
                };
                for (int i = 0; i < RECRUIT_QUALITY_COUNT; ++i)
                {
                    const int y = 373 + i * 15;
                    if (i == q)
                        OriginalFillRect(image, 335, y - 2, 234, 15, selectedBand);
                    OriginalDrawSpellText(image, font, labels[i], 336, y,
                        i == q ? wxColour(35, 35, 35) : text, 232, true);
                }
                actionCost = GetRecruitCost(m_unitsSelectedUnit, q);
                actionTime = GetRecruitTime(q);
                actionEnabled = !hasCooldown && u.health < 100 && actionCost > 0 && m_money >= actionCost;
            }
            else if (m_unitsCurrentTab == UNITS_TAB_UPGRADE)
            {
                const bool hasUpgradeChoice = m_unitsSelectedUpgrade >= 0 && m_unitsSelectedRearmUnitId <= 0;
                const bool hasRearmChoice = m_unitsSelectedRearmUnitId > 0;
                if (hasUpgradeChoice)
                {
                    const auto it = m_upgradeDefs.find(m_unitsSelectedUpgrade);
                    wxString label = (it != m_upgradeDefs.end() && !it->second.title.empty())
                        ? it->second.title : wxString();
                    if (label.empty())
                        for (const auto& r : m_researchDb)
                            if (r.data == m_unitsSelectedUpgrade && !r.title.empty()) { label = r.title; break; }
                    if (label.empty()) label = wxString::Format(L"Vylep\u0161en\u00ED #%d", m_unitsSelectedUpgrade);
                    OriginalFillRect(image, 335, 377, 234, 15, selectedBand);
                    OriginalDrawSpellText(image, font, label, 336, 379, wxColour(35,35,35), 232, true);
                    actionCost = GetTechUpgradeCost(m_unitsSelectedUpgrade);
                    actionTime = GetTechUpgradeTime(m_unitsSelectedUpgrade);
                    actionEnabled = !hasCooldown && m_money >= actionCost;
                }
                else if (hasRearmChoice)
                {
                    OriginalFillRect(image, 335, 377, 234, 15, selectedBand);
                    OriginalDrawSpellText(image, font, wxString(L"Nov\u00FD typ: ") + GetUnitDisplayName(m_unitsSelectedRearmUnitId),
                        336, 379, wxColour(35,35,35), 232, true);
                    actionCost = GetUpgradeCost(u.unit_id, m_unitsSelectedRearmUnitId);
                    actionTime = GetUpgradeTime(m_unitsSelectedRearmUnitId);
                    actionEnabled = !hasCooldown && actionCost >= 0 && m_money >= actionCost;
                }
                else
                {
                    OriginalDrawSpellText(image, font, L"Vyber vylep\u0161en\u00ED nebo nov\u00FD typ", 336, 379, dim, 232, true);
                }
            }
            else
            {
                // Info mode is deliberately quiet here; the upper list and the
                // core stats above remain visible exactly like the original.
                actionEnabled = false;
            }

            if (hasCooldown)
                OriginalDrawSpellText(image, font,
                    wxString::Format(L"Nedostupn\u00E1: %d kol", m_unitStates[m_unitsSelectedUnit].cooldown_turns),
                    336, 416, warn, 232, true);
        }
        else
        {
            OriginalDrawSpellText(image, font, L"Vyber jednotku.", 336, 334, dim, 232, true);
        }

        auto drawUnitButton = [&](int x, int y, int w, int h, const wxString& caption, bool enabled)
        {
            OriginalFillRect(image, x, y, w, h, enabled ? buttonBg : buttonOff);
            OriginalHLine(image, x, x + w - 1, y, enabled ? edgeHi : wxColour(57,61,54));
            OriginalVLine(image, x, y, y + h - 1, enabled ? edgeHi : wxColour(57,61,54));
            OriginalHLine(image, x, x + w - 1, y + h - 1, edgeLo);
            OriginalVLine(image, x + w - 1, y, y + h - 1, edgeLo);
            OriginalDrawSpellText(image, font, caption, x, OriginalCenteredTextY(font, y, h),
                enabled ? green : dim, w, true);
        };

        const bool canDisband = m_unitsSelectedUnit >= 0 && !hasCooldown;
        // Measured from the original 640x480 screenshot: the buttons are small
        // plates INSIDE VMU_LST1, not 39-pixel-tall modern controls.
        // Native lower controls.  The plate geometry is intentionally not
        // centered using generic wx-like metrics: the DOS font sits high in
        // these buttons.
        drawUnitButton(342, 444, 73, 28, L"", canDisband);
        OriginalDrawSpellText(image, font, L"Propustit", 351, 447,
            canDisband ? green : dim, 63, false);
        drawUnitButton(505, 444, 60, 28, L"", actionEnabled);
        OriginalDrawSpellText(image, font, L"OK", 505, 448,
            actionEnabled ? green : dim, 60, true);
        OriginalDrawSpellText(image, font, L"\u010Cas:", 423, 438, text, 38);
        const wxString actionTimeText = actionTime >= 0 ? wxString::Format(L"%d", actionTime) : wxString(L"-");
        OriginalDrawSpellText(image, font, actionTimeText, 469, 438, text, 25);
        OriginalDrawSpellText(image, font, L"Cena:", 423, 454, text, 38);
        const wxString actionCostText = actionCost >= 0 ? wxString::Format(L"%d", actionCost) : wxString(L"-");
        OriginalDrawSpellText(image, font, actionCostText, 469, 454, text, 25);

        // Common status area.  The turn appears here AND on the end-turn plate,
        // matching the original game.
        OriginalDrawSpellText(image, font, L"Pen\u00EDze", 578, 17, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_money), 578, 31, text, 61, true);
        OriginalDrawSpellText(image, font, L"V\u00FDzkum", 578, 47, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_research), 578, 61, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 578, 77, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_turn), 578, 91, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 579, 436, text, 60, true);
        OriginalDrawSpellText(image, font, wxString::Format("%02d", m_turn), 579, 452, text, 60, true);
    }


    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Buy)
    {
        SpellFont* font = m_spellData->font;
        const wxColour text(150, 150, 150);
        const wxColour green(4, 219, 4);
        const wxColour yellow(231, 227, 5);
        const wxColour selected(232, 48, 40);
        const wxColour dim(77, 77, 77);
        const wxColour listBg(32, 60, 20);
        const wxColour listGrid(40, 77, 32);
        const wxColour listFrame(77, 77, 77);
        const wxColour buttonBg(13, 51, 10);
        const wxColour buttonOff(22, 34, 20);
        const wxColour edgeHi(38, 94, 33);
        const wxColour edgeLo(3, 17, 3);

        EnsureUnitCostsLoaded();

        int maxUnits = 32;
        int maxCommanders = 14;
        GetOriginalBuyLimits(maxUnits, maxCommanders);

        // -------------------------------------------------------------
        // LEFT: permanent units. The native BUY screen has two columns of
        // 16 slots, all visible at once, with a 19-pixel row pitch.
        // -------------------------------------------------------------
        std::vector<int> flatBuyUnits;
        for (size_t pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
            for (int inst = 0; inst < std::max(0, m_playerUnits[pIdx].count); ++inst)
                flatBuyUnits.push_back(static_cast<int>(pIdx));

        constexpr int buyRosterY = 10;
        constexpr int buyRosterRowH = 19;
        for (int slot = 0; slot < static_cast<int>(flatBuyUnits.size()) && slot < 32; ++slot)
        {
            const int pIdx = flatBuyUnits[static_cast<size_t>(slot)];
            if (pIdx < 0 || pIdx >= static_cast<int>(m_playerUnits.size()))
                continue;
            const auto& u = m_playerUnits[static_cast<size_t>(pIdx)];
            const int col = slot / 16;
            const int row = slot % 16;
            const int x = 31 + col * 152;
            const int y = buyRosterY + row * buyRosterRowH;
            const int ty = OriginalCenteredTextY(font, y, 17) + 1;
            OriginalDrawSpellText(image, font, GetUnitDisplayName(u.unit_id), x, ty, text, 129);

            const int hpBar = std::clamp((std::max(0, std::min(100, u.health)) * 26) / 100, 0, 26);
            if (hpBar > 0)
                OriginalHLine(image, 38 + col * 152, 38 + col * 152 + hpBar, y, green);

            if (pIdx < static_cast<int>(m_unitStates.size()) && m_unitStates[pIdx].cooldown_turns > 0)
                OriginalDrawSpellText(image, font,
                    wxString::Format(L"%d", m_unitStates[pIdx].cooldown_turns),
                    152 + col * 152, ty, green, 9, true);
        }

        // -------------------------------------------------------------
        // LEFT BOTTOM: owned commanders, seven slots per column.
        // -------------------------------------------------------------
        auto originalRankAbbrev = [](int rank) -> wxString
        {
            static const wchar_t* names[] = {
                L"por.", L"npor.", L"kpt.", L"mjr.", L"pplk.",
                L"plk.", L"genmjr.", L"genpor.", L"armgen."
            };
            if (rank < 0 || rank >= static_cast<int>(sizeof(names) / sizeof(names[0])))
                return wxString::Format(L"R%d", rank);
            return wxString(names[rank]);
        };

        for (int slot = 0; slot < static_cast<int>(m_playerCommanders.size()) && slot < 14; ++slot)
        {
            const auto& commander = m_playerCommanders[static_cast<size_t>(slot)];
            const int col = slot / 7;
            const int row = slot % 7;
            const int y = 331 + row * 19;
            const int ty = OriginalCenteredTextY(font, y, 16) + 1;
            const wxString label = originalRankAbbrev(commander.rank) + L" " + wxString::FromUTF8(commander.name);
            OriginalDrawSpellText(image, font, label, 31 + col * 153, ty, text, 122);
        }

        // -------------------------------------------------------------
        // UPPER RIGHT: original categorized purchase list.
        // -------------------------------------------------------------
        OriginalFillRect(image, 418, 6, 135, 281, listBg);
        OriginalVLine(image, 416, 6, 286, listFrame);
        OriginalVLine(image, 417, 6, 286, wxColour(40, 40, 34));
        OriginalVLine(image, 553, 6, 286, listFrame);
        OriginalHLine(image, 416, 574, 6, listFrame);
        OriginalHLine(image, 416, 574, 286, listFrame);
        for (int x = 420; x < 553; x += 18)
            OriginalVLine(image, x, 8, 286, listGrid);
        for (int y = 8; y < 287; y += 18)
            OriginalHLine(image, 419, 552, y, listGrid);

        const auto buyRows = BuildOriginalBuyRows();
        bool selectedUnitPresent = false;
        bool selectedCommanderPresent = false;
        for (const auto& row : buyRows)
        {
            if (row.kind == OriginalBuyRowKind::Unit && row.id == m_originalBuySelectedUnitId)
                selectedUnitPresent = true;
            if (row.kind == OriginalBuyRowKind::Commander && row.id == m_originalBuySelectedCommander)
                selectedCommanderPresent = true;
        }
        if (!selectedUnitPresent)
            m_originalBuySelectedUnitId = -1;
        if (!selectedCommanderPresent)
            m_originalBuySelectedCommander = -1;

        constexpr int buyListY = 10;
        constexpr int buyListRowH = 14;
        constexpr int buyVisibleRows = 20;
        const int buyMaxScroll = std::max(0, static_cast<int>(buyRows.size()) - buyVisibleRows);
        m_originalBuyListScroll = std::clamp(m_originalBuyListScroll, 0, buyMaxScroll);

        for (int visible = 0; visible < buyVisibleRows; ++visible)
        {
            const int idx = m_originalBuyListScroll + visible;
            if (idx >= static_cast<int>(buyRows.size()))
                break;
            const auto& row = buyRows[static_cast<size_t>(idx)];
            if (row.kind == OriginalBuyRowKind::Spacer)
                continue;
            const int y = buyListY + visible * buyListRowH;
            if (row.kind == OriginalBuyRowKind::Heading)
            {
                OriginalDrawSpellText(image, font, row.label, 420, y, yellow, 132, true);
                continue;
            }
            const bool isSelected =
                (row.kind == OriginalBuyRowKind::Unit && row.id == m_originalBuySelectedUnitId) ||
                (row.kind == OriginalBuyRowKind::Commander && row.id == m_originalBuySelectedCommander);
            OriginalDrawSpellText(image, font, row.label, 424, y,
                isSelected ? selected : (row.enabled ? text : dim), 126);
        }

        OriginalDrawScrollButton(image, 553, 6, true, buyMaxScroll > 0);
        OriginalDrawScrollButton(image, 553, 259, false, buyMaxScroll > 0);
        OriginalDrawScrollTrack(image, 553, 34, 225, m_originalBuyListScroll,
            buyMaxScroll, buyVisibleRows, static_cast<int>(buyRows.size()));

        // -------------------------------------------------------------
        // LOWER RIGHT: selected unit/commander information and purchase.
        // -------------------------------------------------------------
        int actionCost = -1;
        int actionTime = -1;
        bool actionEnabled = false;

        if (m_originalBuySelectedUnitId >= 0 && m_spellData && m_spellData->units)
        {
            const int tid = m_originalBuySelectedUnitId;
            if (auto* rec = m_spellData->units->GetUnit(tid))
            {
                OriginalDrawSpellText(image, font, GetUnitDisplayName(tid), 346, 296, text, 217);
                OriginalDrawSpellText(image, font, L"\u00DAtok:", 369, 312, text, 126);
                OriginalDrawSpellText(image, font, wxString::Format(L"Lehk\u00E9: %d", rec->attack_light), 385, 327, text, 112);
                OriginalDrawSpellText(image, font, wxString::Format(L"T\u011B\u017Ek\u00E9: %d", rec->attack_armored), 385, 342, text, 112);
                OriginalDrawSpellText(image, font, wxString::Format(L"Vzdu\u0161n\u00E9: %d", rec->attack_air), 385, 357, text, 112);
                OriginalDrawSpellText(image, font, wxString::Format(L"Objekty: %d", rec->attack_objects), 385, 372, text, 112);
                OriginalDrawSpellText(image, font, wxString::Format(L"Obrana: %d", rec->defence), 369, 387, text, 128);
                OriginalDrawSpellText(image, font, wxString::Format(L"Dost\u0159el: %d", rec->fire_range), 369, 402, text, 128);
                OriginalDrawSpellText(image, font, wxString::Format(L"Dohled: %d", rec->sdir), 369, 417, text, 128);

                if (rec->icon_glyph)
                {
                    wxBitmap* iconBmp = rec->icon_glyph->Render(56, 48);
                    if (iconBmp && iconBmp->IsOk())
                    {
                        wxImage src = iconBmp->ConvertToImage();
                        if (src.IsOk() && src.GetData())
                        {
                            unsigned char* dst = image.GetData();
                            const unsigned char* sd = src.GetData();
                            const unsigned char* sa = src.HasAlpha() ? src.GetAlpha() : nullptr;
                            const int sw = src.GetWidth();
                            const int sh = src.GetHeight();
                            for (int sy = 0; sy < sh; ++sy)
                                for (int sx = 0; sx < sw; ++sx)
                                {
                                    const size_t sp = static_cast<size_t>(sy) * sw + sx;
                                    const unsigned char rr = sd[sp * 3u + 0];
                                    const unsigned char gg = sd[sp * 3u + 1];
                                    const unsigned char bb = sd[sp * 3u + 2];
                                    if ((sa && sa[sp] == 0) || (!sa && rr < 12 && gg < 12 && bb < 12))
                                        continue;
                                    const int dx = 505 + sx;
                                    const int dy = 299 + sy;
                                    if (dx < 0 || dx >= image.GetWidth() || dy < 0 || dy >= image.GetHeight())
                                        continue;
                                    const size_t dp = (static_cast<size_t>(dy) * image.GetWidth() + dx) * 3u;
                                    dst[dp + 0] = rr; dst[dp + 1] = gg; dst[dp + 2] = bb;
                                }
                        }
                    }
                    delete iconBmp;
                }
            }

            actionCost = GetUnitBuyCost(tid);
            actionTime = 2;
            int ownedUnits = 0;
            for (const auto& u : m_playerUnits)
                ownedUnits += std::max(0, u.count);
            actionEnabled = actionCost > 0 && m_money >= actionCost && ownedUnits < maxUnits;
        }
        else if (m_originalBuySelectedCommander >= 0 &&
                 m_originalBuySelectedCommander < static_cast<int>(m_availableCommanders.size()))
        {
            const auto& commander = m_availableCommanders[static_cast<size_t>(m_originalBuySelectedCommander)];
            const wxString label = originalRankAbbrev(commander.rank) + L" " + wxString::FromUTF8(commander.name);
            OriginalDrawSpellText(image, font, label, 346, 296, text, 217);
            OriginalDrawSpellText(image, font, L"Velitel Aliance", 369, 322, text, 128);
            actionCost = 0;
            actionTime = 1;
            actionEnabled = static_cast<int>(m_playerCommanders.size()) < maxCommanders;
        }

        auto drawBuyButton = [&](bool enabled)
        {
            constexpr int x = 496, y = 442, w = 69, h = 26;
            OriginalFillRect(image, x, y, w, h, enabled ? buttonBg : buttonOff);
            OriginalHLine(image, x, x + w - 1, y, enabled ? edgeHi : wxColour(57,61,54));
            OriginalVLine(image, x, y, y + h - 1, enabled ? edgeHi : wxColour(57,61,54));
            OriginalHLine(image, x, x + w - 1, y + h - 1, edgeLo);
            OriginalVLine(image, x + w - 1, y, y + h - 1, edgeLo);
            OriginalDrawSpellText(image, font, L"Koupit", x, 448,
                enabled ? green : dim, w, true);
        };
        drawBuyButton(actionEnabled);

        OriginalDrawSpellText(image, font, L"\u010Cas:", 423, 438, text, 38);
        OriginalDrawSpellText(image, font,
            actionTime >= 0 ? wxString::Format(L"%d", actionTime) : wxString(L"-"),
            469, 438, text, 25);
        OriginalDrawSpellText(image, font, L"Cena:", 423, 454, text, 38);
        OriginalDrawSpellText(image, font,
            actionCost >= 0 ? wxString::Format(L"%d", actionCost) : wxString(L"-"),
            469, 454, text, 25);

        // Common right-hand status and end-turn plate.
        OriginalDrawSpellText(image, font, L"Pen\u00EDze", 578, 17, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_money), 578, 31, text, 61, true);
        OriginalDrawSpellText(image, font, L"V\u00FDzkum", 578, 47, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_research), 578, 61, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 578, 77, green, 61, true);
        OriginalDrawSpellText(image, font, wxString::Format("%d", m_turn), 578, 91, text, 61, true);
        OriginalDrawSpellText(image, font, L"Kolo", 579, 436, text, 60, true);
        OriginalDrawSpellText(image, font, wxString::Format("%02d", m_turn), 579, 452, text, 60, true);
    }

    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Research)
    {
        SpellFont* font = m_spellData->font;
        const wxColour text(218, 222, 211);
        const wxColour green(0, 242, 0);
        const wxColour yellow(232, 232, 0);
        const wxColour red(242, 48, 40);
        const wxColour dim(126, 132, 118);
        const wxColour done(76, 132, 76);
        const wxColour progress(232, 74, 67);
        EnsureResearchLoaded();

        // The DOS VMR_FULL resource is only the metallic silhouette. The tall
        // research browser on the right is a live CRT panel drawn/populated by
        // the game. Stage 6 left that area black, which is why the restored
        // research screen looked visibly broken. Recreate the native green
        // grid/list frame before painting the dynamic rows.
        const wxColour listBg(32, 74, 28);
        const wxColour grid(42, 90, 38);
        const wxColour frame(135, 132, 120);
        constexpr int researchListX = 418;
        constexpr int researchListY = 6;
        constexpr int researchListW = 136;
        constexpr int researchListH = 460;
        OriginalFillRect(image, researchListX, researchListY, researchListW, researchListH, listBg);
        OriginalVLine(image, researchListX - 2, researchListY, researchListY + researchListH - 1, frame);
        OriginalVLine(image, researchListX - 1, researchListY, researchListY + researchListH - 1, wxColour(40, 40, 34));
        OriginalVLine(image, researchListX + researchListW, researchListY, researchListY + researchListH - 1, frame);
        OriginalHLine(image, researchListX - 2, 574, researchListY, frame);
        OriginalHLine(image, researchListX - 2, 574, researchListY + researchListH - 1, frame);
        for (int x = researchListX + 2; x < researchListX + researchListW; x += 18)
            OriginalVLine(image, x, researchListY + 1, researchListY + researchListH - 2, grid);
        for (int y = 8; y < researchListY + researchListH - 1; y += 18)
            OriginalHLine(image, researchListX + 1, researchListX + researchListW - 1, y, grid);

        auto groupLabel = [](const wxString& g) -> wxString {
            if (g == "Global") return L"Globální";
            if (g == "Technologies") return L"Technologie";
            if (g == "Upgrades") return L"Vylepšení";
            if (g == "Races") return L"Rasy a jednotky";
            return g;
        };
        struct RRow { bool heading=false; int index=-1; wxString label; };
        std::vector<RRow> rows;
        wxString lastGroup;
        for (int i = 0; i < static_cast<int>(m_researchDb.size()); ++i)
        {
            const ResearchItem& it = m_researchDb[static_cast<size_t>(i)];
            if (!IsResearchAvailable(it)) continue;
            if (it.group != lastGroup)
            {
                lastGroup = it.group;
                if (!lastGroup.empty()) rows.push_back({true, -1, groupLabel(lastGroup)});
            }
            rows.push_back({false, i, it.title});
        }

        constexpr int listX = 421, listY = 12, rowH = 14, visibleRows = 31;
        const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visibleRows);
        int selectedRow = -1;
        for (int ri = 0; ri < static_cast<int>(rows.size()); ++ri)
        {
            if (!rows[static_cast<size_t>(ri)].heading && rows[static_cast<size_t>(ri)].index == m_researchBrowseIndex)
            {
                selectedRow = ri;
                break;
            }
        }
        m_originalResearchListScroll = std::clamp(m_originalResearchListScroll, 0, maxScroll);
        if (selectedRow >= 0)
        {
            if (selectedRow < m_originalResearchListScroll)
                m_originalResearchListScroll = selectedRow;
            else if (selectedRow >= m_originalResearchListScroll + visibleRows)
                m_originalResearchListScroll = selectedRow - visibleRows + 1;
            m_originalResearchListScroll = std::clamp(m_originalResearchListScroll, 0, maxScroll);
        }
        for (int vr = 0; vr < visibleRows; ++vr)
        {
            const int ri = m_originalResearchListScroll + vr;
            if (ri >= static_cast<int>(rows.size())) break;
            const RRow& row = rows[static_cast<size_t>(ri)];
            const int y = listY + vr * rowH;
            if (row.heading)
                OriginalDrawSpellText(image, font, row.label, listX, y, yellow, 130, true);
            else
            {
                const bool selected = row.index == m_researchBrowseIndex;
                const ResearchItem& rowItem = m_researchDb[static_cast<size_t>(row.index)];
                const bool paused = (rowItem.id >= 0 &&
                    m_researchProgressById.count(rowItem.id) > 0 &&
                    m_researchProgressById.at(rowItem.id) > 0 &&
                    row.index != m_researchActiveIndex);
                // Original DOS UI marks a stopped project with a green bulb.
                // We draw the small luminous marker separately so the item text
                // keeps the original white/red selection colour.
                if (paused)
                {
                    OriginalFillRect(image, listX + 1, y + 5, 4, 4, green);
                    OriginalFillRect(image, listX + 2, y + 4, 2, 6, green);
                }
                OriginalDrawSpellText(image, font, row.label, listX + 7, y, selected ? red : text, 122);
            }
        }

        if (m_researchActiveIndex >= 0 && m_researchActiveIndex < static_cast<int>(m_researchDb.size()))
        {
            const ResearchItem& active = m_researchDb[static_cast<size_t>(m_researchActiveIndex)];
            OriginalDrawSpellText(image, font, active.title, 31, 8, text, 228, true);
            const int cost = std::max(1, active.cost);
            const int prog = m_researchProgressById.count(active.id) ? m_researchProgressById.at(active.id) : 0;
            const int fill = std::clamp((prog * 266) / cost, 0, 266);
            if (fill > 0) OriginalFillRect(image, 18, 42, fill, 13, progress);
            // Keep the active project visible even while paused.  The current wx UI
            // uses the same active target/progress state; pausing only stops spending
            // research points and must not make the project disappear.
            const wxString txt = active.brief.empty() ? active.info : active.brief;
            const auto lines = OriginalWrapText(font, txt, 356, 9);
            int y = 73;
            for (const auto& line : lines) { OriginalDrawSpellText(image, font, line, 20, y, green, 365, true); y += 14; }
        }

        if (m_researchBrowseIndex >= 0 && m_researchBrowseIndex < static_cast<int>(m_researchDb.size()))
        {
            const ResearchItem& browse = m_researchDb[static_cast<size_t>(m_researchBrowseIndex)];
            OriginalDrawSpellText(image, font, browse.title, 31, 236, text, 228, true);
            const wxString txt = browse.info.empty() ? browse.brief : browse.info;
            const auto lines = OriginalWrapText(font, txt, 356, 9);
            int y = 286;
            for (const auto& line : lines) { OriginalDrawSpellText(image, font, line, 20, y, green, 365, true); y += 14; }
        }

        // In the DOS screen the middle button is a STOP control for the currently
        // running project; choosing/starting a project is confirmed with OK below.
        const bool researchRunning = (m_researchAllocPerTurn > 0 &&
            m_researchActiveIndex >= 0 && m_researchActiveIndex < static_cast<int>(m_researchDb.size()));
        bool canConfirm = false;
        if (!researchRunning && m_researchBrowseIndex >= 0 &&
            m_researchBrowseIndex < static_cast<int>(m_researchDb.size()))
        {
            const ResearchItem& cand = m_researchDb[static_cast<size_t>(m_researchBrowseIndex)];
            canConfirm = IsResearchAvailable(cand);
        }
        // COMMON.FS tooltip geometry: STOP 305,227,69x28; OK 305,438,69x28.
        OriginalDrawActionButton(image, font, 305, 227, 69, 28, L"STOP", researchRunning);
        OriginalDrawActionButton(image, font, 305, 438, 69, 28, L"OK", canConfirm);

        OriginalDrawScrollButton(image, 553, 6, true, maxScroll > 0);
        OriginalDrawScrollButton(image, 553, 426, false, maxScroll > 0);
        OriginalDrawScrollTrack(image, 553, 34, 392, m_originalResearchListScroll, maxScroll,
            visibleRows, static_cast<int>(rows.size()));
        OriginalDrawStrategicStatus(image, font, m_money, m_research, m_turn);
    }

    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Info)
    {
        SpellFont* font = m_spellData->font;
        const wxColour text(218, 222, 211);
        const wxColour green(0, 242, 0);
        const wxColour yellow(232, 232, 0);
        const wxColour red(242, 48, 40);
        const wxColour dim(92, 100, 88);
        EnsureResearchLoaded();

        auto groupLabel = [](const wxString& g) -> wxString {
            if (g == "Global") return L"Globální";
            if (g == "Technologies") return L"Technologie";
            if (g == "Upgrades") return L"Vylepšení";
            if (g == "Races") return L"Rasy a jednotky";
            return g;
        };
        struct IRow { bool heading=false; int index=-1; wxString label; };
        std::vector<IRow> rows;
        wxString lastGroup;
        for (int i = 0; i < static_cast<int>(m_researchDb.size()); ++i)
        {
            const ResearchItem& it = m_researchDb[static_cast<size_t>(i)];
            if (!IsInfoItemVisible(it)) continue;
            if (it.group != lastGroup)
            {
                lastGroup = it.group;
                if (!lastGroup.empty()) rows.push_back({true, -1, groupLabel(lastGroup)});
            }
            rows.push_back({false, i, it.title});
        }
        if (m_infoBrowseIndex < 0)
            for (const auto& row : rows) if (!row.heading) { m_infoBrowseIndex = row.index; break; }

        constexpr int listY = 12, rowH = 14, visibleRows = 31;
        const int maxListScroll = std::max(0, static_cast<int>(rows.size()) - visibleRows);
        m_originalInfoListScroll = std::clamp(m_originalInfoListScroll, 0, maxListScroll);
        for (int vr = 0; vr < visibleRows; ++vr)
        {
            const int ri = m_originalInfoListScroll + vr;
            if (ri >= static_cast<int>(rows.size())) break;
            const auto& row = rows[static_cast<size_t>(ri)];
            const int y = listY + vr * rowH;
            if (row.heading)
                OriginalDrawSpellText(image, font, row.label, 421, y, yellow, 132, true);
            else
                OriginalDrawSpellText(image, font, row.label, 424, y,
                    row.index == m_infoBrowseIndex ? red : text, 128);
        }

        wxString body;
        if (m_infoBrowseIndex >= 0 && m_infoBrowseIndex < static_cast<int>(m_researchDb.size()))
        {
            const ResearchItem& cur = m_researchDb[static_cast<size_t>(m_infoBrowseIndex)];
            body = cur.info.empty() ? cur.brief : cur.info;
        }
        auto allLines = OriginalWrapText(font, body, 374, 200);
        constexpr int visibleTextLines = 29;
        const int maxTextScroll = std::max(0, static_cast<int>(allLines.size()) - visibleTextLines);
        m_originalInfoTextScroll = std::clamp(m_originalInfoTextScroll, 0, maxTextScroll);
        int y = 17;
        for (int i = 0; i < visibleTextLines; ++i)
        {
            const int li = m_originalInfoTextScroll + i;
            if (li >= static_cast<int>(allLines.size())) break;
            OriginalDrawSpellText(image, font, allLines[static_cast<size_t>(li)], 20, y, green, 378);
            y += 14;
        }
        OriginalDrawSpellText(image, font, L"Dolů", 196, 444,
            m_originalInfoTextScroll < maxTextScroll ? green : dim, 72, true);
        OriginalDrawSpellText(image, font, L"Nahoru", 286, 444,
            m_originalInfoTextScroll > 0 ? green : dim, 78, true);
        OriginalDrawStrategicStatus(image, font, m_money, m_research, m_turn);
    }

    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Resources)
    {
        SpellFont* font = m_spellData->font;
        const wxColour text(218, 222, 211);
        const wxColour green(0, 242, 0);
        const wxColour dark(8, 35, 9);
        const wxColour border(0, 215, 0);

        // Keep the restored screen and the working wx page on exactly the same
        // strategic-point state.  This also migrates old Stage-6 saves on sight.
        for (const auto& t : m_level.territories)
        {
            auto it = m_territoryResources.find(t.id);
            if (it == m_territoryResources.end())
            {
                TerritoryResourceState st;
                st.total = std::max(0, t.strategic_points_total);
                st.remaining = st.total;
                st.incomePerTurn = std::max(0, t.strategic_points_per_turn);
                m_territoryResources[t.id] = st;
            }
            else
            {
                if (it->second.incomePerTurn <= 0 && t.strategic_points_per_turn > 0)
                    it->second.incomePerTurn = t.strategic_points_per_turn;
                if (it->second.total <= 0 && t.strategic_points_total > 0)
                {
                    it->second.total = t.strategic_points_total;
                    it->second.remaining = it->second.total;
                }
                it->second.remaining = std::clamp(it->second.remaining, 0, std::max(0, it->second.total));
            }
        }
        SetGlobalResearchAllocation(m_territoryResources[kResourcesMetaTerritoryId].researchCarry);

        // FACTORY.LZ is composed at (3,8).  Its native strategic monitor starts
        // at screen (96,18), exactly 379x259: the same dimensions as LEVEL_XX.CLK.
        constexpr int mapX = 96, mapY = 18, mapW = 379, mapH = 259;
        if (m_hasClk && m_clkW == mapW && m_clkH == mapH &&
            m_clkValues.size() == static_cast<size_t>(mapW * mapH))
        {
            unsigned char* dst = image.GetData();
            wxImage sourceMap;
            if (m_bgBitmap.IsOk())
            {
                sourceMap = m_bgBitmap.ConvertToImage();
                if (sourceMap.IsOk() && (sourceMap.GetWidth() != mapW || sourceMap.GetHeight() != mapH))
                    sourceMap = sourceMap.Scale(mapW, mapH, wxIMAGE_QUALITY_NEAREST);
            }
            const unsigned char* srcMap = sourceMap.IsOk() ? sourceMap.GetData() : nullptr;
            auto isOwned = [this](int tid) {
                return std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid) != m_ownedTerritories.end();
            };

            for (int py = 0; py < mapH; ++py)
            {
                for (int px = 0; px < mapW; ++px)
                {
                    const uint8_t raw = m_clkValues[static_cast<size_t>(py) * mapW + px];
                    if (!raw) continue;
                    const bool edge = raw >= 129;
                    const int tid = edge ? static_cast<int>(raw) - 128 : static_cast<int>(raw);
                    if (tid <= 0 || !isOwned(tid)) continue;

                    const auto rs = m_territoryResources.find(tid);
                    const bool depleted = rs != m_territoryResources.end() && rs->second.remaining <= 0;
                    const bool selected = tid == m_selectedTerritory;
                    unsigned char rr = selected ? 35 : (depleted ? 70 : 5);
                    unsigned char gg = selected ? 245 : (depleted ? 86 : 180);
                    unsigned char bb = selected ? 20 : (depleted ? 70 : 7);
                    if (!edge && srcMap)
                    {
                        const size_t sp = (static_cast<size_t>(py) * mapW + px) * 3u;
                        const int lum = (static_cast<int>(srcMap[sp+0]) + srcMap[sp+1] + srcMap[sp+2]) / 3;
                        if (!depleted)
                            gg = static_cast<unsigned char>(std::clamp(95 + lum, 105, selected ? 255 : 235));
                    }
                    if (edge) { rr = 1; gg = 22; bb = 1; }
                    const int dx = mapX + px, dy = mapY + py;
                    if (dx < 0 || dy < 0 || dx >= image.GetWidth() || dy >= image.GetHeight()) continue;
                    const size_t dp = (static_cast<size_t>(dy) * image.GetWidth() + dx) * 3u;
                    dst[dp+0] = rr; dst[dp+1] = gg; dst[dp+2] = bb;
                }
            }

            // Original labels mean "SB per turn (turns remaining)".
            for (int tid : m_ownedTerritories)
            {
                const auto c = m_territoryCentroids.find(tid);
                const auto rs = m_territoryResources.find(tid);
                if (tid <= 0 || c == m_territoryCentroids.end() || rs == m_territoryResources.end()) continue;
                const TerritoryResourceState& st = rs->second;
                if (st.incomePerTurn <= 0 || st.remaining <= 0) continue;
                const int rounds = (st.remaining + st.incomePerTurn - 1) / st.incomePerTurn;
                const wxString label = wxString::Format(L"%d (%d)", st.incomePerTurn, rounds);
                const int tw = OriginalTextWidth(font, label) + 8;
                const int bx = mapX + c->second.x - tw / 2;
                const int by = mapY + c->second.y - 8;
                OriginalFillRect(image, bx, by, tw, 17, dark);
                OriginalHLine(image, bx, bx + tw - 1, by, border);
                OriginalHLine(image, bx, bx + tw - 1, by + 16, border);
                OriginalVLine(image, bx, by, by + 16, border);
                OriginalVLine(image, bx + tw - 1, by, by + 16, border);
                OriginalDrawSpellText(image, font, label, bx + 4, by + 3, text, tw - 8, true);
            }
        }

        // The fourteen top cells show each owned territory's current SB yield;
        // the centre box is the sum available for this strategic turn.
        int slot = 0;
        for (int tid : m_ownedTerritories)
        {
            const auto it = m_territoryResources.find(tid);
            if (it == m_territoryResources.end() || slot >= 14) continue;
            const int yield = std::min(std::max(0, it->second.incomePerTurn), std::max(0, it->second.remaining));
            if (yield <= 0) continue;
            OriginalDrawSpellText(image, font, wxString::Format(L"%d", yield),
                83 + slot * 24, 302, text, 23, true);
            ++slot;
        }
        const int totalIncome = GetCurrentStrategicPointIncome();
        OriginalDrawSpellText(image, font, wxString::Format(L"%d", totalIncome), 278, 342, green, 55, true);

        const int maxResearch = totalIncome / 3;
        const int R = std::clamp(m_resourcesGlobalResearch, 0, maxResearch);
        const int M = std::max(0, totalIncome - 3 * R);
        OriginalDrawSpellText(image, font, L"Výzkum", 166, 397, green, 65, true);
        OriginalDrawSpellText(image, font, wxString::Format(L"%d", R), 166, 421, green, 65, true);
        OriginalDrawSpellText(image, font, L"Peníze", 348, 397, green, 65, true);
        OriginalDrawSpellText(image, font, wxString::Format(L"%d", M), 348, 421, green, 65, true);

        // FACTORY.LZ already contains the native arrow chrome.  Only fill the
        // 60px centre allocation bar; Stage 6 painted a 92x23 rectangle over
        // the arrows, which made the restored control look broken.
        constexpr int meterX = 256, meterY = 419, meterW = 60, meterH = 14;
        OriginalFillRect(image, meterX, meterY, meterW, meterH, wxColour(9, 65, 8));
        if (maxResearch > 0 && R > 0)
        {
            const int fill = std::clamp((R * meterW) / maxResearch, 1, meterW);
            OriginalFillRect(image, meterX, meterY, fill, meterH, wxColour(0, 200, 0));
        }
        OriginalDrawStrategicStatus(image, font, m_money, m_research, m_turn);
    }

    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Stats)
    {
        SpellFont* font = m_spellData->font;
        const wxColour text(218, 222, 211);
        const wxColour green(0, 242, 0);
        const wxColour dim(150, 150, 145);
        auto value = [&](int v, int x, int y, int w) {
            OriginalDrawSpellText(image, font, wxString::Format(L"%d", v), x, y, text, w, true);
        };
        OriginalDrawSpellText(image, font, L"Statistika celé hry", 145, 39, text, 325, true);
        OriginalDrawSpellText(image, font, L"Aliance - ztráty", 217, 62, text, 133, true);
        OriginalDrawSpellText(image, font, L"Other Side - ztráty", 351, 62, text, 138, true);
        const std::array<wxString,4> labels = {L"Lehké j.", L"Těžké j.", L"Vzdušné j.", L"Velitelé"};
        const std::array<int,4> ay = {m_lossStats.alliance_all.light, m_lossStats.alliance_all.heavy, m_lossStats.alliance_all.air, m_lossStats.alliance_all.commanders};
        const std::array<int,4> ey = {m_lossStats.enemy_all.light, m_lossStats.enemy_all.heavy, m_lossStats.enemy_all.air, m_lossStats.enemy_all.commanders};
        for (int r = 0; r < 4; ++r)
        {
            const int y = 85 + r * 25;
            OriginalDrawSpellText(image, font, labels[static_cast<size_t>(r)], 138, y, text, 80);
            value(ay[static_cast<size_t>(r)], 217, y, 133);
            value(ey[static_cast<size_t>(r)], 351, y, 138);
        }
        OriginalDrawSpellText(image, font, L"Statistika aktuálního levelu", 145, 190, text, 325, true);
        OriginalDrawSpellText(image, font, L"Aliance - ztráty", 217, 213, text, 133, true);
        OriginalDrawSpellText(image, font, L"Other Side - ztráty", 351, 213, text, 138, true);
        const std::array<int,4> al = {m_lossStats.alliance_level.light, m_lossStats.alliance_level.heavy, m_lossStats.alliance_level.air, m_lossStats.alliance_level.commanders};
        const std::array<int,4> el = {m_lossStats.enemy_level.light, m_lossStats.enemy_level.heavy, m_lossStats.enemy_level.air, m_lossStats.enemy_level.commanders};
        for (int r = 0; r < 4; ++r)
        {
            const int y = 235 + r * 25;
            OriginalDrawSpellText(image, font, labels[static_cast<size_t>(r)], 138, y, text, 80);
            value(al[static_cast<size_t>(r)], 217, y, 133);
            value(el[static_cast<size_t>(r)], 351, y, 138);
        }
        auto rankCz = [](int rank) -> wxString {
            switch (rank) {
            case 0: return L"Poručík"; case 1: return L"Nadporučík"; case 2: return L"Kapitán";
            case 3: return L"Major"; case 4: return L"Podplukovník"; case 5: return L"Plukovník";
            case 6: return L"Generálmajor"; case 7: return L"Generálporučík"; case 8: return L"Armádní Generál";
            default: return wxString::Format(L"Hodnost %d", rank);
            }
        };
        int maxUnits = 0, maxCommanders = 0;
        if (const CommanderRankRec* rr = FindRankRec(m_player.rank))
        {
            // HODNOSTI.DEF contains values above the engine's 32-unit roster
            // ceiling for the two highest ranks; the original UI caps at 32.
            maxUnits = std::clamp(rr->max_units, 0, 32);
            maxCommanders = std::clamp(rr->max_commanders, 0, 14);
        }
        const int nextExp = FindNextRankExp(m_player.rank);
        OriginalDrawSpellText(image, font, wxString(L"Hráč - ") + wxString::FromUTF8(m_player.name), 142, 361, text, 250);
        OriginalDrawSpellText(image, font, L"Hodnost:", 157, 382, green, 77);
        OriginalDrawSpellText(image, font, rankCz(m_player.rank), 220, 382, dim, 175);
        OriginalDrawSpellText(image, font, L"Zkušenost:", 157, 400, green, 87);
        OriginalDrawSpellText(image, font,
            nextExp > m_player.experience ? wxString::Format(L"%d (%d)", m_player.experience, nextExp) : wxString::Format(L"%d (-)", m_player.experience),
            226, 400, dim, 170);
        OriginalDrawSpellText(image, font, L"Max. počet stálých jednotek:", 157, 418, green, 205);
        OriginalDrawSpellText(image, font, wxString::Format(L"%d", maxUnits), 354, 418, dim, 35);
        OriginalDrawSpellText(image, font, L"Max. počet velitelů:", 157, 436, green, 170);
        OriginalDrawSpellText(image, font, wxString::Format(L"%d", maxCommanders), 326, 436, dim, 35);
        OriginalDrawStrategicStatus(image, font, m_money, m_research, m_turn);
    }

    if (image.IsOk() && m_spellData->font &&
        m_originalStrategicScreen == OriginalStrategicScreen::Options)
    {
        SpellFont* font = m_spellData->font;
        const wxColour green(0, 242, 0);
        const wxColour dim(90, 112, 90);
        const wxColour selected(255, 224, 24);

        // OPTIONS.LZ contains the exact nine slot wells but leaves their
        // labels/buttons dynamic. Keep the native 31px row pitch.
        for (int i = 0; i < 9; ++i)
        {
            const int slot = i + 1;
            const int y = 22 + i * 31;
            const auto path = GetStrategicSaveSlotPath(m_level, slot);
            std::error_code ec;
            const bool exists = std::filesystem::exists(path, ec);
            OriginalDrawActionButton(image, font, 20, y, 75, 27, L"Load", exists);
            OriginalDrawActionButton(image, font, 490, y, 75, 27, L"Save", true);

            wxString slotText = L"-= EMPTY =-";
            if (exists)
            {
                int money = 0, rank = 0, xp = 0;
                std::string ts;
                if (PeekStrategicSaveSummary(path, money, rank, xp, ts))
                {
                    // ISO local timestamp -> compact DOS-style slot caption.
                    // Example: 2026-09-29T21:48:10 -> 29092026 21:48
                    if (ts.size() >= 16 && ts[4] == '-' && ts[7] == '-')
                    {
                        slotText = wxString::FromUTF8(
                            (ts.substr(8,2) + ts.substr(5,2) + ts.substr(0,4) +
                             " " + ts.substr(11,5)).c_str());
                    }
                    else if (!ts.empty())
                        slotText = wxString::FromUTF8(ts);
                    else
                        slotText = wxString::Format(L"SLOT %02d", slot);
                }
            }
            OriginalDrawSpellText(image, font, slotText, 115, y + 6,
                exists ? green : dim, 355, true);
        }

        OriginalDrawSpellText(image, font, L"Gamma Correction", 28, 337, green, 178, true);
        OriginalDrawSpellText(image, font, L"Music Volume", 28, 384, green, 178, true);
        OriginalDrawSpellText(image, font, L"Sound Volume", 28, 429, green, 178, true);

        // OPTIONS.LZ carries the metal plates but not their live +/- glyphs.
        // Draw them at native pixels so they stay inside the recessed buttons.
        const wxColour controlGlyph(174, 178, 168);
        for (int cy : { 361, 408, 455 })
        {
            OriginalHLine(image, 32, 40, cy, controlGlyph);
            OriginalHLine(image, 189, 197, cy, controlGlyph);
            OriginalVLine(image, 193, cy - 4, cy + 4, controlGlyph);
        }

        OriginalDrawSpellText(image, font, L"War map resolution", 234, 345, green, 166, true);
        const std::array<wxString,3> resolutions = { L"640x480", L"800x600", L"1024x768" };
        for (int i = 0; i < 3; ++i)
            OriginalDrawSpellText(image, font, resolutions[static_cast<size_t>(i)],
                250, 363 + i * 15,
                i == m_originalBattleResolution ? selected : green, 134, true);

        // Native selection brackets visible in the reference OPTIONS screen.
        const int resolutionY = 366 + std::clamp(m_originalBattleResolution, 0, 2) * 15;
        OriginalVLine(image, 269, 355, resolutionY, green);
        OriginalVLine(image, 359, 355, resolutionY, green);
        OriginalHLine(image, 269, 282, resolutionY, green);
        OriginalHLine(image, 346, 359, resolutionY, green);
        OriginalSetPixel(image, 281, resolutionY - 1, green);
        OriginalSetPixel(image, 281, resolutionY + 1, green);
        OriginalSetPixel(image, 347, resolutionY - 1, green);
        OriginalSetPixel(image, 347, resolutionY + 1, green);

        OriginalDrawSpellText(image, font, L"Quick Help", 434, 345, green, 74, true);
        OriginalDrawSpellText(image, font, L"On", 434, 367, m_originalQuickHelp ? selected : green, 74, true);
        OriginalDrawSpellText(image, font, L"Off", 434, 383, !m_originalQuickHelp ? selected : green, 74, true);
        const int helpY = m_originalQuickHelp ? 370 : 386;
        OriginalVLine(image, 434, 356, helpY, green);
        OriginalVLine(image, 507, 356, helpY, green);
        OriginalHLine(image, 434, 455, helpY, green);
        OriginalHLine(image, 486, 507, helpY, green);
        OriginalSetPixel(image, 454, helpY - 1, green);
        OriginalSetPixel(image, 454, helpY + 1, green);
        OriginalSetPixel(image, 487, helpY - 1, green);
        OriginalSetPixel(image, 487, helpY + 1, green);

        OriginalDrawSpellText(image, font, L"Exit", 327, 447, green, 113, true);

        OriginalDrawStrategicStatus(image, font, m_money, m_research, m_turn);
    }

    // Native end-turn animation, shared by every reconstructed strategic page.
    // ET_BTN0/1 are 41x36 (not 36x41): the original DOS UI reveals ET_BTN0
    // from left to right over the idle "Kolo NN" plate, then wipes it back out
    // when the pointer leaves.  This behavior was measured frame-by-frame from
    // the original game capture (2026-09-30_20h35_27.mp4).
    if (image.IsOk() && haveNativeUiPal && m_originalEndTurnReveal > 0)
    {
        wxImage et = OriginalLoadRawIndexed(loader, "ET_BTN0.LZ", 41, 36, nativeUiPal);
        if (et.IsOk())
        {
            const int reveal = std::clamp(m_originalEndTurnReveal, 0, 41);
            if (reveal >= 41)
            {
                OriginalBlitImage(image, et, 588, 432);
            }
            else if (reveal > 0)
            {
                wxImage partial = et.GetSubImage(wxRect(0, 0, reveal, 36));
                OriginalBlitImage(image, partial, 588, 432);
            }
        }
    }

    m_originalStrategicBitmap = image.IsOk() ? wxBitmap(image) : wxBitmap();
    if (!m_originalStrategicBitmap.IsOk())
        m_originalStrategicError = "Original UI: wxImage/wxBitmap creation failed after successful render.";
    else
        m_originalStrategicError.clear();

    // Dynamic map markers (timeouts, final territory, counter-attacks). Reuse the
    // already-tested game-state marker logic, but place it in original 640x480
    // map coordinates. CLK centroids are native 379x259 map coordinates.
    if (m_originalStrategicBitmap.IsOk() && m_gameModeEnabled &&
        m_originalStrategicScreen == OriginalStrategicScreen::Map)
    {
        wxMemoryDC markerDc(m_originalStrategicBitmap);
        for (const auto& t : m_level.territories)
        {
            if (t.id <= 0 || t.id >= static_cast<int>(m_visibleTerritory.size()) ||
                m_visibleTerritory[t.id] == 0)
                continue;

            auto it = m_territoryCentroids.find(t.id);
            if (it == m_territoryCentroids.end())
                continue;

            DrawTerritoryMarker(markerDc, t.id,
                StrategicOriginalRenderer::kMapX + it->second.x,
                StrategicOriginalRenderer::kMapY + it->second.y, 1.0);
        }
        markerDc.SelectObject(wxNullBitmap);
    }

    // Overlay the original strategic navigation glyphs. These are real VM_*.ICO
    // resources; desaturation compensates for the editor's legacy strategy-pal
    // loader and matches the grey DOS toolbar more closely.
    if (m_originalStrategicBitmap.IsOk())
    {
        wxMemoryDC dc(m_originalStrategicBitmap);
        const std::array<wxString, 9> icons = {
            "strategic_map", "hierarchy", "units", "buy_sell", "research",
            "info", "resources", "statistics", "options"
        };
        for (size_t i = 0; i < icons.size(); ++i)
        {
            // STRBAR.QH: native plate is exactly 37x24 at x=590 and a 31px
            // vertical pitch.  BMPAN__N/A are the real normal/hover states.
            const int slotX = 590;
            const int slotY = 131 + static_cast<int>(i) * kOriginalToolbarPitch;
            if (haveNativeUiPal)
            {
                const char* plateName = (m_originalToolbarHoverSlot == static_cast<int>(i))
                    ? "BMPAN__A.BTN" : "BMPAN__N.BTN";
                wxImage plateImage = OriginalLoadIcoLike(loader, plateName, nativeUiPal);
                if (plateImage.IsOk())
                    dc.DrawBitmap(wxBitmap(plateImage), slotX, slotY, true);
            }

            wxBitmap icon = OriginalDesaturateBitmap(LoadMenuIcon(m_spellData, icons[i]));
            if (!icon.IsOk())
                continue;
            const int x = slotX + (37 - icon.GetWidth()) / 2;
            const int y = slotY + (24 - icon.GetHeight()) / 2;
            dc.DrawBitmap(icon, x, y, true);
        }

        // Exact active-screen wedge measured from the native 640x480 DOS
        // screenshots: x=591..596 and y=132 + slot*31 .. +5.  The old
        // x=581 marker visibly leaked ten pixels into the screen content.
        const int activeSlot = static_cast<int>(m_originalStrategicScreen);
        if (activeSlot >= 0 && activeSlot < 9)
        {
            const int y = kOriginalToolbarY0 + activeSlot * kOriginalToolbarPitch;
            wxPoint tri[3] = { wxPoint(591, y), wxPoint(597, y), wxPoint(591, y + 6) };
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(245, 24, 16)));
            dc.DrawPolygon(3, tri);
        }
        dc.SelectObject(wxNullBitmap);
    }

    m_originalStrategicDirty = false;
    m_originalStrategicPanel->SetToolTip(m_originalStrategicError);
    m_originalStrategicPanel->Refresh();
}

void StrategicLevelFrame::OnOriginalStrategicPaint(wxPaintEvent&)
{
    if (!m_originalStrategicPanel)
        return;

    if (m_originalStrategicDirty || !m_originalStrategicBitmap.IsOk())
        RefreshOriginalStrategicView();

    wxAutoBufferedPaintDC dc(m_originalStrategicPanel);
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();
    m_originalStrategicDrawRect = wxRect();

    if (!m_originalStrategicBitmap.IsOk())
    {
        dc.SetTextForeground(*wxLIGHT_GREY);
        dc.DrawText("Original strategic renderer could not build this screen.", 12, 12);
        if (!m_originalStrategicError.empty())
        {
            dc.SetTextForeground(wxColour(255, 180, 90));
            dc.DrawText(m_originalStrategicError, 12, 34);
        }
        dc.SetTextForeground(wxColour(150, 150, 150));
        dc.DrawText("The Current UI remains available from Strategic UI -> Current UI.", 12, 58);
        return;
    }

    const wxSize client = m_originalStrategicPanel->GetClientSize();
    if (client.x <= 0 || client.y <= 0)
        return;

    // Prefer exact integer scaling (e.g. 1280x960 == 2x) so the DOS pixels
    // remain crisp. Fractional scaling is used only when the window is smaller.
    double scale = std::min(
        static_cast<double>(client.x) / StrategicOriginalRenderer::kScreenW,
        static_cast<double>(client.y) / StrategicOriginalRenderer::kScreenH);
    if (scale >= 1.0)
        scale = std::max(1.0, std::floor(scale));

    const int drawW = std::max(1, static_cast<int>(std::lround(StrategicOriginalRenderer::kScreenW * scale)));
    const int drawH = std::max(1, static_cast<int>(std::lround(StrategicOriginalRenderer::kScreenH * scale)));
    const int drawX = (client.x - drawW) / 2;
    const int drawY = (client.y - drawH) / 2;
    m_originalStrategicDrawRect = wxRect(drawX, drawY, drawW, drawH);

    wxBitmap drawBmp = m_originalStrategicBitmap;
    if (drawW != StrategicOriginalRenderer::kScreenW || drawH != StrategicOriginalRenderer::kScreenH)
        drawBmp = wxBitmap(m_originalStrategicBitmap.ConvertToImage().Scale(drawW, drawH, wxIMAGE_QUALITY_NEAREST));

    if (drawBmp.IsOk())
        dc.DrawBitmap(drawBmp, drawX, drawY, false);
}

void StrategicLevelFrame::OnOriginalStrategicLeftDown(wxMouseEvent& ev)
{
    if (!m_originalStrategicPanel || m_originalStrategicDrawRect.width <= 0 ||
        m_originalStrategicDrawRect.height <= 0)
    {
        ev.Skip();
        return;
    }

    const wxPoint p = ev.GetPosition();
    if (!m_originalStrategicDrawRect.Contains(p))
        return;

    const int lx = static_cast<int>(
        (static_cast<long long>(p.x - m_originalStrategicDrawRect.x) * StrategicOriginalRenderer::kScreenW) /
        m_originalStrategicDrawRect.width);
    const int ly = static_cast<int>(
        (static_cast<long long>(p.y - m_originalStrategicDrawRect.y) * StrategicOriginalRenderer::kScreenH) /
        m_originalStrategicDrawRect.height);

    auto refreshRestored = [this]()
    {
        m_originalStrategicDirty = true;
        if (m_originalStrategicPanel)
            m_originalStrategicPanel->Refresh();
    };

    if (m_originalStrategicScreen == OriginalStrategicScreen::Hierarchy)
    {
        // Lower-right turn panel remains live on every restored strategic page.
        if (lx >= 578 && ly >= 420)
        {
            wxCommandEvent dummy(wxEVT_BUTTON, ID_BTN_ENDTURN);
            OnEndTurn(dummy);
            refreshRestored();
            return;
        }

        // Restored toolbar: map, hierarchy and unit management stay in the new
        // renderer. Pages not yet restored still fall back to current UI.
        if (lx >= kOriginalToolbarX0 && lx < kOriginalToolbarX1 &&
            ly >= kOriginalToolbarY0 && ly < kOriginalToolbarY0 + kOriginalToolbarCount * kOriginalToolbarPitch)
        {
            const int slot = (ly - kOriginalToolbarY0) / kOriginalToolbarPitch;
            if (slot >= 0 && slot < 9)
            {
                wxCommandEvent dummy(wxEVT_BUTTON, wxID_ANY);
                switch (slot)
                {
                case 0:
                    m_originalStrategicScreen = OriginalStrategicScreen::Map;
                    refreshRestored();
                    return;
                case 1:
                    return;
                case 2:
                    m_originalStrategicScreen = OriginalStrategicScreen::Units;
                    if (m_unitsSelectedUnit < 0 && !m_playerUnits.empty())
                        m_unitsSelectedUnit = 0;
                    if (m_unitsCurrentTab == UNITS_TAB_RECRUIT &&
                        (m_unitsSelectedUpgrade < 0 || m_unitsSelectedUpgrade >= RECRUIT_QUALITY_COUNT))
                        m_unitsSelectedUpgrade = 1;
                    refreshRestored();
                    return;
                case 3:
                    m_originalStrategicScreen = OriginalStrategicScreen::Buy;
                    refreshRestored();
                    return;
                case 4:
                    m_originalStrategicScreen = OriginalStrategicScreen::Research;
                    EnsureResearchLoaded();
                    refreshRestored();
                    return;
                case 5:
                    m_originalStrategicScreen = OriginalStrategicScreen::Info;
                    EnsureResearchLoaded();
                    refreshRestored();
                    return;
                case 6:
                    m_originalStrategicScreen = OriginalStrategicScreen::Resources;
                    refreshRestored();
                    return;
                case 7:
                    m_originalStrategicScreen = OriginalStrategicScreen::Stats;
                    refreshRestored();
                    return;
                case 8:
                    m_originalStrategicScreen = OriginalStrategicScreen::Options;
                    refreshRestored();
                    return;
                default:
                    break;
                }
            }
        }

        // Native hierarchy roster scrollbar. It scrolls the permanent-unit
        // pool while commanders remain in their dedicated lower section.
        if (lx >= 553 && lx < 575 && ((ly >= 6 && ly < 34) || (ly >= 404 && ly < 432)))
        {
            int totalUnitRows = 0;
            for (const auto& u : m_playerUnits)
                totalUnitRows += std::max(0, u.count);
            const int maxScroll = std::max(0, totalUnitRows - 18);
            if (maxScroll > 0)
            {
                const int delta = (ly < 34) ? -1 : 1;
                m_originalHierarchyUnitScroll = std::clamp(
                    m_originalHierarchyUnitScroll + delta, 0, maxScroll);
                refreshRestored();
            }
            return;
        }

        // Clicking the native right-hand pools now selects a concrete unit or
        // commander first; the next click on the tree places that selection.
        // This matches the intended two-step workflow much better than opening
        // a chooser dialog for every single slot.
        {
            struct HierarchyPoolUnitRow
            {
                int playerIndex = -1;
                uint32_t uid = 0;
            };
            int totalUnitRows = 0;
            for (const auto& u : m_playerUnits)
                totalUnitRows += std::max(0, u.count);
            if (static_cast<int>(m_rosterRowUids.size()) != totalUnitRows)
            {
                m_rosterRowUids.clear();
                m_rosterRowUids.reserve(static_cast<size_t>(totalUnitRows));
                for (int i = 0; i < totalUnitRows; ++i)
                    m_rosterRowUids.push_back(m_nextRosterUid++);
            }
            std::vector<HierarchyPoolUnitRow> flatUnits;
            flatUnits.reserve(static_cast<size_t>(totalUnitRows));
            int uidIndex = 0;
            for (size_t pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
            {
                for (int inst = 0; inst < std::max(0, m_playerUnits[pIdx].count); ++inst)
                {
                    HierarchyPoolUnitRow row;
                    row.playerIndex = static_cast<int>(pIdx);
                    if (uidIndex < static_cast<int>(m_rosterRowUids.size()))
                        row.uid = m_rosterRowUids[static_cast<size_t>(uidIndex)];
                    flatUnits.push_back(row);
                    ++uidIndex;
                }
            }

            if (lx >= 420 && lx < 552 && ly >= 27 && ly < 27 + 18 * 14)
            {
                const int row = (ly - 27) / 14;
                const int flat = m_originalHierarchyUnitScroll + row;
                if (flat >= 0 && flat < static_cast<int>(flatUnits.size()))
                {
                    const auto& picked = flatUnits[static_cast<size_t>(flat)];
                    if (picked.playerIndex >= 0 && picked.playerIndex < static_cast<int>(m_playerUnits.size()))
                    {
                        const wxString display = GetUnitDisplayName(
                            m_playerUnits[static_cast<size_t>(picked.playerIndex)].unit_id);
                        const bool same =
                            m_originalHierarchyPoolSelectionKind == OriginalHierarchyPoolSelectionKind::Unit &&
                            picked.uid != 0 && picked.uid == m_originalHierarchySelectedUnitUid;
                        if (same)
                            ClearOriginalHierarchyPoolSelection();
                        else
                        {
                            ClearOriginalHierarchyPoolSelection();
                            m_originalHierarchyPoolSelectionKind = OriginalHierarchyPoolSelectionKind::Unit;
                            m_originalHierarchySelectedUnitUid = picked.uid;
                            m_originalHierarchySelectedUnitDisplay = display;
                        }
                        refreshRestored();
                    }
                }
                return;
            }

            if (lx >= 420 && lx < 552 && ly >= 309 && ly < 309 + 8 * 14)
            {
                const int row = (ly - 309) / 14;
                if (row >= 0 && row < static_cast<int>(m_playerCommanders.size()))
                {
                    const auto& commander = m_playerCommanders[static_cast<size_t>(row)];
                    const bool same =
                        m_originalHierarchyPoolSelectionKind == OriginalHierarchyPoolSelectionKind::Commander &&
                        commander.uid != 0 && commander.uid == m_originalHierarchySelectedCommanderUid;
                    if (same)
                        ClearOriginalHierarchyPoolSelection();
                    else
                    {
                        ClearOriginalHierarchyPoolSelection();
                        m_originalHierarchyPoolSelectionKind = OriginalHierarchyPoolSelectionKind::Commander;
                        m_originalHierarchySelectedCommanderUid = commander.uid;
                        m_originalHierarchySelectedCommanderRank = commander.rank;
                        m_originalHierarchySelectedCommanderName = wxString::FromUTF8(commander.name);
                    }
                    refreshRestored();
                }
                return;
            }
        }

        // Original "Část" switch at the bottom of the hierarchy tree.
        if (wxRect(323, 439, 72, 28).Contains(lx, ly))
        {
            m_originalHierarchyPage = (m_originalHierarchyPage == 1) ? 2 : 1;
            refreshRestored();
            return;
        }

        for (const auto& hs : OriginalHierarchySlotsForPage(m_originalHierarchyPage))
        {
            if (!hs.rect.Contains(lx, ly))
                continue;

            bool handled = false;
            if (m_originalHierarchyPoolSelectionKind == OriginalHierarchyPoolSelectionKind::Commander)
            {
                if (hs.commander)
                    handled = AssignCommanderToHierarchySlot(hs.id,
                        m_originalHierarchySelectedCommanderUid,
                        m_originalHierarchySelectedCommanderRank,
                        m_originalHierarchySelectedCommanderName);
                else
                    wxMessageBox("A commander is selected in the right-hand pool.\n\n"
                        "Click a commander slot in the hierarchy tree, or click the same pool row again to deselect it.",
                        "Hierarchy", wxOK | wxICON_INFORMATION, this);
            }
            else if (m_originalHierarchyPoolSelectionKind == OriginalHierarchyPoolSelectionKind::Unit)
            {
                if (!hs.commander)
                    handled = AssignUnitToHierarchySlot(hs.id,
                        m_originalHierarchySelectedUnitUid,
                        m_originalHierarchySelectedUnitDisplay);
                else
                    wxMessageBox("A unit is selected in the right-hand pool.\n\n"
                        "Click a unit slot or assignment slot in the hierarchy tree, or click the same pool row again to deselect it.",
                        "Hierarchy", wxOK | wxICON_INFORMATION, this);
            }
            else
            {
                if (hs.commander)
                    ChooseCommanderForHierarchySlot(hs.id);
                else
                    ChooseUnitForHierarchySlot(hs.id);
                handled = true;
            }

            if (handled)
                refreshRestored();
            return;
        }
        return;
    }

    if (m_originalStrategicScreen == OriginalStrategicScreen::Units)
    {
        // End turn stays live on every restored strategic page.
        if (lx >= 578 && ly >= 420)
        {
            wxCommandEvent dummy(wxEVT_BUTTON, ID_BTN_ENDTURN);
            OnEndTurn(dummy);
            refreshRestored();
            return;
        }

        // Common original toolbar. Map/Hierarchy/Units remain inside the restored
        // renderer; later pages still use the current implementation until restored.
        if (lx >= kOriginalToolbarX0 && lx < kOriginalToolbarX1 &&
            ly >= kOriginalToolbarY0 && ly < kOriginalToolbarY0 + kOriginalToolbarCount * kOriginalToolbarPitch)
        {
            const int slot = (ly - kOriginalToolbarY0) / kOriginalToolbarPitch;
            wxCommandEvent dummy(wxEVT_BUTTON, wxID_ANY);
            switch (slot)
            {
            case 0:
                m_originalStrategicScreen = OriginalStrategicScreen::Map;
                refreshRestored();
                return;
            case 1:
                m_originalStrategicScreen = OriginalStrategicScreen::Hierarchy;
                refreshRestored();
                return;
            case 2:
                return;
            case 3:
                m_originalStrategicScreen = OriginalStrategicScreen::Buy;
                refreshRestored();
                return;
            case 4:
                m_originalStrategicScreen = OriginalStrategicScreen::Research;
                EnsureResearchLoaded();
                refreshRestored();
                return;
            case 5:
                m_originalStrategicScreen = OriginalStrategicScreen::Info;
                EnsureResearchLoaded();
                refreshRestored();
                return;
            case 6:
                m_originalStrategicScreen = OriginalStrategicScreen::Resources;
                refreshRestored();
                return;
            case 7:
                m_originalStrategicScreen = OriginalStrategicScreen::Stats;
                refreshRestored();
                return;
            case 8:
                m_originalStrategicScreen = OriginalStrategicScreen::Options;
                refreshRestored();
                return;
            default:
                break;
            }
        }

        // Expanded permanent-unit roster (16 original 19px rows).
        if (lx >= 8 && lx <= 313 && ly >= 10 && ly < 10 + 16 * 19)
        {
            std::vector<int> flatUnits;
            for (size_t pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
                for (int inst = 0; inst < std::max(0, m_playerUnits[pIdx].count); ++inst)
                    flatUnits.push_back(static_cast<int>(pIdx));
            const int row = (ly - 10) / 19;
            const int flat = m_originalUnitsRosterScroll + row;
            if (flat >= 0 && flat < static_cast<int>(flatUnits.size()))
            {
                m_unitsSelectedUnit = flatUnits[static_cast<size_t>(flat)];
                if (m_unitsCurrentTab == UNITS_TAB_RECRUIT &&
                    (m_unitsSelectedUpgrade < 0 || m_unitsSelectedUpgrade >= RECRUIT_QUALITY_COUNT))
                    m_unitsSelectedUpgrade = 1;
                m_unitsSelectedRearmUnitId = -1;
                m_originalUnitsOptionScroll = 0;
                refreshRestored();
            }
            return;
        }

        // Original mode selector.
        if (lx >= 333 && lx <= 413 && ly >= 228 && ly < 277)
        {
            if (ly < 245)
            {
                m_unitsCurrentTab = UNITS_TAB_UPGRADE;
                m_unitsSelectedUpgrade = -1;
                m_unitsSelectedRearmUnitId = -1;
                m_originalUnitsOptionScroll = 0;
            }
            else if (ly < 260)
            {
                m_unitsCurrentTab = UNITS_TAB_RECRUIT;
                m_unitsSelectedUpgrade = 1; // original's useful middle default: veterans
                m_unitsSelectedRearmUnitId = -1;
                m_originalUnitsOptionScroll = 0;
            }
            else
            {
                m_unitsCurrentTab = UNITS_TAB_INFO;
                m_unitsSelectedUpgrade = -1;
                m_unitsSelectedRearmUnitId = -1;
                m_originalUnitsOptionScroll = 0;
            }
            refreshRestored();
            return;
        }

        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < static_cast<int>(m_playerUnits.size()))
        {
            const auto& unit = m_playerUnits[static_cast<size_t>(m_unitsSelectedUnit)];

            // Recruitment choices live in the lower info panel in the original.
            if (m_unitsCurrentTab == UNITS_TAB_RECRUIT && lx >= 335 && lx <= 569 && ly >= 371 && ly < 416)
            {
                const int q = std::clamp((ly - 371) / 15, 0, RECRUIT_QUALITY_COUNT - 1);
                m_unitsSelectedUpgrade = q;
                m_unitsSelectedRearmUnitId = -1;
                refreshRestored();
                return;
            }

            // In Upgrade mode the upper-right grouped list is interactive.
            if (m_unitsCurrentTab == UNITS_TAB_UPGRADE && lx >= 418 && lx < 553 && ly >= 8 && ly < 258)
            {
                EnsureUpgradeDefsLoaded();
                EnsureResearchLoaded();
                const auto upgrades = GetAvailableUpgradesForUnit(unit.unit_id);
                const auto rearm = GetAvailableUnitTypesForUpgrade(unit.unit_id);
                std::vector<std::pair<int,int>> targets; // upgradeId, rearmUnitId; {-1,-1}=heading
                auto appendKind = [&](UpgradeDefRec::Kind kind)
                {
                    targets.push_back({-1,-1});
                    for (int id : upgrades)
                    {
                        const auto it = m_upgradeDefs.find(id);
                        if (it != m_upgradeDefs.end() && it->second.kind == kind)
                            targets.push_back({id,-1});
                    }
                };
                appendKind(UpgradeDefRec::Engine);
                appendKind(UpgradeDefRec::Weapon);
                appendKind(UpgradeDefRec::Armor);
                if (!rearm.empty())
                {
                    targets.push_back({-1,-1});
                    for (int id : rearm) targets.push_back({-1,id});
                }
                const int visible = std::max(1, (258 - 8) / 14);
                const int maxScroll = std::max(0, static_cast<int>(targets.size()) - visible);
                m_originalUnitsOptionScroll = std::clamp(m_originalUnitsOptionScroll, 0, maxScroll);
                const int idx = m_originalUnitsOptionScroll + (ly - 8) / 14;
                if (idx >= 0 && idx < static_cast<int>(targets.size()))
                {
                    const auto t = targets[static_cast<size_t>(idx)];
                    if (t.first >= 0)
                    {
                        m_unitsSelectedUpgrade = t.first;
                        m_unitsSelectedRearmUnitId = -1;
                        refreshRestored();
                    }
                    else if (t.second >= 0)
                    {
                        m_unitsSelectedRearmUnitId = t.second;
                        m_unitsSelectedUpgrade = -1;
                        refreshRestored();
                    }
                }
                return;
            }
        }

        // Release unit.
        if (wxRect(342, 444, 73, 28).Contains(lx, ly))
        {
            wxCommandEvent dummy(wxEVT_BUTTON, ID_BTN_UNITS_ACTION);
            OnUnitsDisband(dummy);
            refreshRestored();
            return;
        }

        // Confirm recruit / upgrade. Info mode is informational and keeps the page open.
        if (wxRect(505, 444, 60, 28).Contains(lx, ly))
        {
            if (m_unitsCurrentTab != UNITS_TAB_INFO)
            {
                wxCommandEvent dummy(wxEVT_BUTTON, ID_BTN_UNITS_ACTION);
                OnUnitsAction(dummy);
            }
            refreshRestored();
            return;
        }
        return;
    }

    if (m_originalStrategicScreen == OriginalStrategicScreen::Buy)
    {
        // End turn remains active on the restored purchase page.
        if (lx >= 578 && ly >= 420)
        {
            wxCommandEvent dummy(wxEVT_BUTTON, ID_BTN_ENDTURN);
            OnEndTurn(dummy);
            refreshRestored();
            return;
        }

        // Common strategic toolbar. The first four pages now stay entirely in
        // the restored framebuffer; the remaining pages still use current UI.
        if (lx >= kOriginalToolbarX0 && lx < kOriginalToolbarX1 &&
            ly >= kOriginalToolbarY0 && ly < kOriginalToolbarY0 + kOriginalToolbarCount * kOriginalToolbarPitch)
        {
            const int slot = (ly - kOriginalToolbarY0) / kOriginalToolbarPitch;
            wxCommandEvent dummy(wxEVT_BUTTON, wxID_ANY);
            switch (slot)
            {
            case 0:
                m_originalStrategicScreen = OriginalStrategicScreen::Map;
                refreshRestored();
                return;
            case 1:
                m_originalStrategicScreen = OriginalStrategicScreen::Hierarchy;
                refreshRestored();
                return;
            case 2:
                m_originalStrategicScreen = OriginalStrategicScreen::Units;
                if (m_unitsSelectedUnit < 0 && !m_playerUnits.empty())
                    m_unitsSelectedUnit = 0;
                refreshRestored();
                return;
            case 3:
                return;
            case 4:
                m_originalStrategicScreen = OriginalStrategicScreen::Research;
                EnsureResearchLoaded();
                refreshRestored();
                return;
            case 5:
                m_originalStrategicScreen = OriginalStrategicScreen::Info;
                EnsureResearchLoaded();
                refreshRestored();
                return;
            case 6:
                m_originalStrategicScreen = OriginalStrategicScreen::Resources;
                refreshRestored();
                return;
            case 7:
                m_originalStrategicScreen = OriginalStrategicScreen::Stats;
                refreshRestored();
                return;
            case 8:
                m_originalStrategicScreen = OriginalStrategicScreen::Options;
                refreshRestored();
                return;
            default:
                break;
            }
        }

        const auto buyRows = BuildOriginalBuyRows();
        constexpr int buyListY = 10;
        constexpr int buyListRowH = 14;
        constexpr int buyVisibleRows = 20;
        const int buyMaxScroll = std::max(0, static_cast<int>(buyRows.size()) - buyVisibleRows);
        m_originalBuyListScroll = std::clamp(m_originalBuyListScroll, 0, buyMaxScroll);

        // Native upper-right list scrollbar buttons.
        if (lx >= 553 && lx < 575 &&
            ((ly >= 6 && ly < 34) || (ly >= 259 && ly < 287)))
        {
            if (buyMaxScroll > 0)
            {
                const int delta = (ly < 34) ? -1 : 1;
                m_originalBuyListScroll = std::clamp(
                    m_originalBuyListScroll + delta, 0, buyMaxScroll);
                refreshRestored();
            }
            return;
        }

        // Select an available unit/commander. Headings and blank separators are
        // deliberately inert just like the DOS list.
        if (lx >= 418 && lx < 553 && ly >= buyListY && ly < 286)
        {
            const int visible = (ly - buyListY) / buyListRowH;
            const int idx = m_originalBuyListScroll + visible;
            if (visible >= 0 && visible < buyVisibleRows &&
                idx >= 0 && idx < static_cast<int>(buyRows.size()))
            {
                const auto& row = buyRows[static_cast<size_t>(idx)];
                if (row.kind == OriginalBuyRowKind::Unit)
                {
                    m_originalBuySelectedUnitId = row.id;
                    m_originalBuySelectedCommander = -1;
                    refreshRestored();
                }
                else if (row.kind == OriginalBuyRowKind::Commander)
                {
                    m_originalBuySelectedCommander = row.id;
                    m_originalBuySelectedUnitId = -1;
                    refreshRestored();
                }
            }
            return;
        }

        // Native Koupit button.
        if (wxRect(496, 442, 69, 26).Contains(lx, ly))
        {
            EnsureUnitCostsLoaded();
            int maxUnits = 32;
            int maxCommanders = 14;
            GetOriginalBuyLimits(maxUnits, maxCommanders);

            if (m_originalBuySelectedUnitId >= 0)
            {
                int ownedUnits = 0;
                for (const auto& u : m_playerUnits)
                    ownedUnits += std::max(0, u.count);
                const int cost = GetUnitBuyCost(m_originalBuySelectedUnitId);
                if (ownedUnits >= maxUnits || cost <= 0 || m_money < cost)
                    return;

                // Keep the per-roster state vector aligned even when the player
                // reaches this page before ever opening Unit Management.
                while (m_unitStates.size() < m_playerUnits.size())
                {
                    UnitInstanceState existingState;
                    existingState.uid = m_nextRosterUid++;
                    m_unitStates.push_back(existingState);
                }

                LevelData::PlayerUnitAdd add;
                add.unit_id = m_originalBuySelectedUnitId;
                add.count = 1;
                add.health = 100;
                add.extra = "-";
                m_playerUnits.push_back(add);

                // The original manual and BUY screen both use a two-turn delay
                // for newly purchased permanent units.
                UnitInstanceState state;
                state.uid = m_nextRosterUid++;
                state.cooldown_turns = 2;
                m_unitStates.push_back(state);
                if (!m_rosterRowUids.empty())
                    m_rosterRowUids.push_back(state.uid);

                m_money -= cost;
            }
            else if (m_originalBuySelectedCommander >= 0 &&
                     m_originalBuySelectedCommander < static_cast<int>(m_availableCommanders.size()))
            {
                if (static_cast<int>(m_playerCommanders.size()) >= maxCommanders)
                    return;
                CommanderRec commander = m_availableCommanders[static_cast<size_t>(m_originalBuySelectedCommander)];
                if (commander.uid == 0)
                    commander.uid = m_nextCommanderUid++;
                m_playerCommanders.push_back(std::move(commander));
                m_availableCommanders.erase(
                    m_availableCommanders.begin() + m_originalBuySelectedCommander);
                m_originalBuySelectedCommander = -1;
            }
            else
            {
                return;
            }

            SaveStrategicState();
            RefreshUI();
            refreshRestored();
            return;
        }
        return;
    }

    if (m_originalStrategicScreen == OriginalStrategicScreen::Research ||
        m_originalStrategicScreen == OriginalStrategicScreen::Info ||
        m_originalStrategicScreen == OriginalStrategicScreen::Resources ||
        m_originalStrategicScreen == OriginalStrategicScreen::Stats ||
        m_originalStrategicScreen == OriginalStrategicScreen::Options)
    {
        // End-turn plate is shared by every strategic page.
        if (lx >= 578 && ly >= 420)
        {
            wxCommandEvent dummy(wxEVT_BUTTON, ID_BTN_ENDTURN);
            OnEndTurn(dummy);
            refreshRestored();
            return;
        }

        // Shared nine-button original toolbar. All nine strategic pages now
        // remain inside the restored 640x480 renderer.
        if (lx >= kOriginalToolbarX0 && lx < kOriginalToolbarX1 &&
            ly >= kOriginalToolbarY0 && ly < kOriginalToolbarY0 + kOriginalToolbarCount * kOriginalToolbarPitch)
        {
            const int slot = (ly - kOriginalToolbarY0) / kOriginalToolbarPitch;
            wxCommandEvent dummy(wxEVT_BUTTON, wxID_ANY);
            switch (slot)
            {
            case 0: m_originalStrategicScreen = OriginalStrategicScreen::Map; break;
            case 1: m_originalStrategicScreen = OriginalStrategicScreen::Hierarchy; break;
            case 2:
                m_originalStrategicScreen = OriginalStrategicScreen::Units;
                if (m_unitsSelectedUnit < 0 && !m_playerUnits.empty()) m_unitsSelectedUnit = 0;
                break;
            case 3: m_originalStrategicScreen = OriginalStrategicScreen::Buy; break;
            case 4: m_originalStrategicScreen = OriginalStrategicScreen::Research; EnsureResearchLoaded(); break;
            case 5: m_originalStrategicScreen = OriginalStrategicScreen::Info; EnsureResearchLoaded(); break;
            case 6: m_originalStrategicScreen = OriginalStrategicScreen::Resources; break;
            case 7: m_originalStrategicScreen = OriginalStrategicScreen::Stats; break;
            case 8: m_originalStrategicScreen = OriginalStrategicScreen::Options; break;
            default: return;
            }
            refreshRestored();
            return;
        }

        if (m_originalStrategicScreen == OriginalStrategicScreen::Research)
        {
            EnsureResearchLoaded();
            struct Row { bool heading=false; int index=-1; };
            std::vector<Row> rows;
            wxString lastGroup;
            for (int i = 0; i < static_cast<int>(m_researchDb.size()); ++i)
            {
                const auto& it = m_researchDb[static_cast<size_t>(i)];
                if (!IsResearchAvailable(it)) continue;
                if (it.group != lastGroup)
                {
                    lastGroup = it.group;
                    if (!lastGroup.empty()) rows.push_back({true, -1});
                }
                rows.push_back({false, i});
            }
            constexpr int rowY = 12, rowH = 14, visibleRows = 32;
            if (lx >= 418 && lx < 553 && ly >= rowY && ly < rowY + visibleRows * rowH)
            {
                const int ri = m_originalResearchListScroll + (ly - rowY) / rowH;
                if (ri >= 0 && ri < static_cast<int>(rows.size()) && !rows[static_cast<size_t>(ri)].heading)
                {
                    // Reuse the exact state transition used by the working wx UI.
                    SelectResearchIndex(rows[static_cast<size_t>(ri)].index);
                    refreshRestored();
                }
                return;
            }
            // Native upper-right list scrollbar buttons.
            const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visibleRows);
            if (lx >= 553 && lx < 575 && ((ly >= 6 && ly < 34) || (ly >= 426 && ly < 454)))
            {
                if (maxScroll > 0)
                {
                    const int delta = (ly < 34) ? -1 : 1;
                    m_originalResearchListScroll = std::clamp(m_originalResearchListScroll + delta, 0, maxScroll);
                    refreshRestored();
                }
                return;
            }
            if (wxRect(305, 227, 69, 28).Contains(lx, ly))
            {
                // Native STOP pauses the active project; OK below starts/resumes
                // the currently browsed project through the shared backend.
                if (m_researchAllocPerTurn > 0)
                {
                    m_researchAllocPerTurn = 0;
                    RefreshResearchUI();
                    SaveStrategicState();
                }
                refreshRestored();
                return;
            }
            if (wxRect(305, 438, 69, 28).Contains(lx, ly))
            {
                // The original explicitly requires stopping the current project
                // before another item can be started. While research is running
                // the OK button is disabled/inert (as in the reference screen).
                if (m_researchAllocPerTurn <= 0 && StartResearchIndex(m_researchBrowseIndex))
                {
                    RefreshResearchUI();
                    SaveStrategicState();
                }
                refreshRestored();
                return;
            }
            return;
        }

        if (m_originalStrategicScreen == OriginalStrategicScreen::Info)
        {
            EnsureResearchLoaded();
            struct Row { bool heading=false; int index=-1; };
            std::vector<Row> rows;
            wxString lastGroup;
            for (int i = 0; i < static_cast<int>(m_researchDb.size()); ++i)
            {
                const auto& it = m_researchDb[static_cast<size_t>(i)];
                if (!IsInfoItemVisible(it)) continue;
                if (it.group != lastGroup)
                {
                    lastGroup = it.group;
                    if (!lastGroup.empty()) rows.push_back({true, -1});
                }
                rows.push_back({false, i});
            }
            constexpr int rowY = 12, rowH = 14, visibleRows = 31;
            if (lx >= 418 && lx < 553 && ly >= rowY && ly < rowY + visibleRows * rowH)
            {
                const int ri = m_originalInfoListScroll + (ly - rowY) / rowH;
                if (ri >= 0 && ri < static_cast<int>(rows.size()) && !rows[static_cast<size_t>(ri)].heading)
                {
                    SelectInfoIndex(rows[static_cast<size_t>(ri)].index);
                    m_originalInfoTextScroll = 0;
                    refreshRestored();
                }
                return;
            }
            if (wxRect(190, 438, 82, 34).Contains(lx, ly))
            {
                m_originalInfoTextScroll += 3;
                refreshRestored();
                return;
            }
            if (wxRect(280, 438, 88, 34).Contains(lx, ly))
            {
                m_originalInfoTextScroll = std::max(0, m_originalInfoTextScroll - 3);
                refreshRestored();
                return;
            }
            return;
        }

        if (m_originalStrategicScreen == OriginalStrategicScreen::Resources)
        {
            // Native FACTORY.LZ controls.  The arrows occupy their original
            // screen-space rectangles; they modify the desired research output
            // by one point (i.e. three strategic points) and persist immediately.
            if (wxRect(222, 419, 34, 28).Contains(lx, ly))
            {
                SetGlobalResearchAllocation(m_resourcesGlobalResearch - 1);
                SaveStrategicState();
                RefreshResourcesPage();
                refreshRestored();
                return;
            }
            if (wxRect(316, 419, 34, 28).Contains(lx, ly))
            {
                SetGlobalResearchAllocation(m_resourcesGlobalResearch + 1);
                SaveStrategicState();
                RefreshResourcesPage();
                refreshRestored();
                return;
            }

            // Direct clicks on the centre bar are supported as a convenience,
            // but never steal clicks from the native arrows.
            constexpr int meterX = 256, meterW = 60;
            if (wxRect(meterX, 419, meterW, 28).Contains(lx, ly))
            {
                const int maxResearch = GetCurrentStrategicPointIncome() / 3;
                const int rel = std::clamp(lx - meterX, 0, meterW - 1);
                const int value = maxResearch > 0
                    ? std::clamp((rel * maxResearch + (meterW - 1) / 2) / (meterW - 1), 0, maxResearch)
                    : 0;
                SetGlobalResearchAllocation(value);
                SaveStrategicState();
                RefreshResourcesPage();
                refreshRestored();
                return;
            }

            constexpr int mapX = 96, mapY = 18, mapW = 379, mapH = 259;
            if (lx >= mapX && lx < mapX + mapW && ly >= mapY && ly < mapY + mapH &&
                m_hasClk && m_clkW == mapW && m_clkH == mapH &&
                m_clkValues.size() == static_cast<size_t>(mapW * mapH))
            {
                const int mx = lx - mapX, my = ly - mapY;
                const uint8_t raw = m_clkValues[static_cast<size_t>(my) * mapW + mx];
                const int tid = raw >= 129 ? static_cast<int>(raw) - 128 : static_cast<int>(raw);
                if (tid > 0 && std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid) != m_ownedTerritories.end())
                {
                    m_selectedTerritory = tid;
                    RefreshResourcesPage();
                    refreshRestored();
                }
            }
            return;
        }

        if (m_originalStrategicScreen == OriginalStrategicScreen::Options)
        {
            // Nine native save rows (STROPT.QH: load 20,22,75,280;
            // save 490,22,75,280). Gaps between the 27px buttons remain inert.
            for (int i = 0; i < 9; ++i)
            {
                const int y = 22 + i * 31;
                if (ly < y || ly >= y + 27)
                    continue;
                const int slot = i + 1;
                if (lx >= 20 && lx < 95)
                {
                    LoadStrategicGameFromSlot(slot, false);
                    refreshRestored();
                    return;
                }
                if (lx >= 490 && lx < 565)
                {
                    SaveStrategicGameToSlot(slot, false);
                    refreshRestored();
                    return;
                }
            }

            auto sliderPercentFromX = [](int x)
            {
                return std::clamp(((x - 47) * 100 + 65) / 130, 0, 100);
            };
            auto sliderHit = [&](int y0, int current, const std::function<void(int)>& apply) -> bool
            {
                if (ly < y0 || ly >= y0 + 41 || lx < 28 || lx >= 206)
                    return false;
                int value = current;
                if (lx < 47) value = std::max(0, current - 5);
                else if (lx >= 187) value = std::min(100, current + 5);
                else value = sliderPercentFromX(lx);
                apply(value);
                refreshRestored();
                return true;
            };

            SpellMap* spellMap = m_main ? m_main->GetSpellMap() : nullptr;
            const int gammaPct = std::clamp(static_cast<int>(std::lround(
                ((spellMap ? spellMap->GetGamma() : 1.3) - 0.5) * (100.0 / 1.5))), 0, 100);
            if (sliderHit(333, gammaPct, [&](int p) {
                if (spellMap) spellMap->SetGamma(0.5 + p * 0.015);
            })) return;

            const int musicPct = (m_spellData && m_spellData->midi)
                ? std::clamp(static_cast<int>(std::lround(m_spellData->midi->GetVolume() * 100.0)), 0, 100) : 0;
            if (sliderHit(380, musicPct, [&](int p) {
                if (m_spellData && m_spellData->midi) m_spellData->midi->SetVolume(p / 100.0);
            })) return;

            const int soundPct = (m_spellData && m_spellData->sounds && m_spellData->sounds->channels)
                ? std::clamp(static_cast<int>(std::lround(m_spellData->sounds->channels->GetVolume() * 100.0)), 0, 100) : 0;
            if (sliderHit(425, soundPct, [&](int p) {
                if (m_spellData && m_spellData->sounds && m_spellData->sounds->channels)
                    m_spellData->sounds->channels->SetVolume(p / 100.0);
            })) return;

            if (wxRect(234, 339, 166, 76).Contains(lx, ly))
            {
                if (ly >= 358 && ly < 375) m_originalBattleResolution = 0;
                else if (ly >= 375 && ly < 390) m_originalBattleResolution = 1;
                else if (ly >= 390) m_originalBattleResolution = 2;
                refreshRestored();
                return;
            }

            if (wxRect(434, 336, 74, 77).Contains(lx, ly))
            {
                if (ly >= 360 && ly < 382) m_originalQuickHelp = true;
                else if (ly >= 382) m_originalQuickHelp = false;
                refreshRestored();
                return;
            }

            if (wxRect(327, 435, 113, 41).Contains(lx, ly))
            {
                SaveStrategicState();
                Close(true);
                return;
            }
            return;
        }

        // Statistics is informational; toolbar and end-turn are its only controls.
        return;
    }

    // Mission-unit controls in the original centre list.
    if (lx >= 420 && lx <= 553 && ly >= 23 && ly < 39)
    {
        m_selectedUnitsForMission.clear();
        for (uint32_t uid : m_rosterRowUids)
            if (uid != 0 && !IsRosterUidOnCooldown(uid))
                m_selectedUnitsForMission.insert(uid);
        RefreshUI();
        refreshRestored();
        return;
    }
    if (lx >= 420 && lx <= 553 && ly >= 39 && ly < 56)
    {
        m_selectedUnitsForMission.clear();
        m_selectedCommandersForMission.clear();
        RefreshUI();
        refreshRestored();
        return;
    }

    // Click a visible unit row to toggle it for the next attack.
    if (lx >= 420 && lx <= 553 && ly >= kOriginalUnitRowsY &&
        ly < kOriginalUnitRowsY + kOriginalVisibleRows * kOriginalUnitRowH)
    {
        const int visibleRow = (ly - kOriginalUnitRowsY) / kOriginalUnitRowH;
        const int flatRow = m_originalUnitScroll + visibleRow;
        if (flatRow >= 0 && flatRow < static_cast<int>(m_rosterRowUids.size()))
        {
            const uint32_t uid = m_rosterRowUids[static_cast<size_t>(flatRow)];
            if (uid && !IsRosterUidOnCooldown(uid))
            {
                if (m_selectedUnitsForMission.count(uid))
                    m_selectedUnitsForMission.erase(uid);
                else
                    m_selectedUnitsForMission.insert(uid);
                RefreshUI();
                refreshRestored();
            }
        }
        return;
    }

    // Attack / cancel.
    if (ly >= kOriginalAttackY && ly < kOriginalAttackY + kOriginalButtonH)
    {
        if (lx >= kOriginalAttackX && lx < kOriginalAttackX + kOriginalButtonW)
        {
            wxCommandEvent dummy(wxEVT_BUTTON, ID_BTN_LAUNCH);
            OnLaunch(dummy);
            return;
        }
        if (lx >= kOriginalCancelX && lx < kOriginalCancelX + kOriginalButtonW)
        {
            m_selectedUnitsForMission.clear();
            m_selectedCommandersForMission.clear();
            RefreshUI();
            refreshRestored();
            return;
        }
    }

    // End turn panel in the lower-right corner.
    if (lx >= 578 && ly >= 420)
    {
        wxCommandEvent dummy(wxEVT_BUTTON, ID_BTN_ENDTURN);
        OnEndTurn(dummy);
        refreshRestored();
        return;
    }

    // Right-hand original toolbar. Map, hierarchy and unit management are now
    // restored; later pages deliberately fall back to the current UI.
    if (lx >= kOriginalToolbarX0 && lx < kOriginalToolbarX1 &&
            ly >= kOriginalToolbarY0 && ly < kOriginalToolbarY0 + kOriginalToolbarCount * kOriginalToolbarPitch)
    {
        const int slot = (ly - kOriginalToolbarY0) / kOriginalToolbarPitch;
        if (slot >= 0 && slot < 9)
        {
            wxCommandEvent dummy(wxEVT_BUTTON, wxID_ANY);
            switch (slot)
            {
            case 0: // strategic map - already here
                return;
            case 1:
                m_originalStrategicScreen = OriginalStrategicScreen::Hierarchy;
                refreshRestored();
                return;
            case 2:
                m_originalStrategicScreen = OriginalStrategicScreen::Units;
                if (m_unitsSelectedUnit < 0 && !m_playerUnits.empty())
                    m_unitsSelectedUnit = 0;
                if (m_unitsCurrentTab == UNITS_TAB_RECRUIT &&
                    (m_unitsSelectedUpgrade < 0 || m_unitsSelectedUpgrade >= RECRUIT_QUALITY_COUNT))
                    m_unitsSelectedUpgrade = 1;
                refreshRestored();
                return;
            case 3:
                m_originalStrategicScreen = OriginalStrategicScreen::Buy;
                refreshRestored();
                return;
            case 4:
                m_originalStrategicScreen = OriginalStrategicScreen::Research;
                EnsureResearchLoaded();
                refreshRestored();
                return;
            case 5:
                m_originalStrategicScreen = OriginalStrategicScreen::Info;
                EnsureResearchLoaded();
                refreshRestored();
                return;
            case 6:
                m_originalStrategicScreen = OriginalStrategicScreen::Resources;
                refreshRestored();
                return;
            case 7:
                m_originalStrategicScreen = OriginalStrategicScreen::Stats;
                refreshRestored();
                return;
            case 8:
                m_originalStrategicScreen = OriginalStrategicScreen::Options;
                refreshRestored();
                return;
            default:
                break;
            }
        }
    }

    // Map territory hit-test via the original LEVEL_XX.CLK mask.
    if (!m_hasClk || m_clkValues.empty())
        return;

    const int mx = lx - StrategicOriginalRenderer::kMapX;
    const int my = ly - StrategicOriginalRenderer::kMapY;
    if (mx < 0 || my < 0 || mx >= StrategicOriginalRenderer::kMapW || my >= StrategicOriginalRenderer::kMapH)
        return;
    if (m_clkW != StrategicOriginalRenderer::kMapW || m_clkH != StrategicOriginalRenderer::kMapH ||
        static_cast<std::size_t>(m_clkW) * m_clkH != m_clkValues.size())
        return;

    const std::uint8_t raw = m_clkValues[static_cast<std::size_t>(my) * m_clkW + mx];
    if (!raw)
        return;
    const int decoded = raw >= 129 ? static_cast<int>(raw) - 128 : static_cast<int>(raw);

    int chosen = decoded;
    bool direct = false;
    for (const auto& t : m_level.territories)
        if (t.id == chosen) { direct = true; break; }
    if (!direct)
    {
        const std::size_t idx = decoded > 0 ? static_cast<std::size_t>(decoded - 1) : m_level.territories.size();
        if (idx >= m_level.territories.size())
            return;
        chosen = m_level.territories[idx].id;
    }

    if (m_gameModeEnabled &&
        (chosen <= 0 || chosen >= static_cast<int>(m_visibleTerritory.size()) || !m_visibleTerritory[chosen]))
        return;

    SelectTerritoryById(chosen);
    refreshRestored();
}

void StrategicLevelFrame::OnOriginalStrategicRightDown(wxMouseEvent& ev)
{
    // The original Resources screen uses RMB on the allocation arrows for
    // 10x stepping.  Keep RMB otherwise untouched so existing context/info
    // behaviour elsewhere in the editor is not swallowed by the restored UI.
    if (m_originalStrategicScreen != OriginalStrategicScreen::Resources ||
        !m_originalStrategicPanel || m_originalStrategicDrawRect.width <= 0 ||
        m_originalStrategicDrawRect.height <= 0)
    {
        ev.Skip();
        return;
    }

    const wxPoint p = ev.GetPosition();
    if (!m_originalStrategicDrawRect.Contains(p))
    {
        ev.Skip();
        return;
    }

    const int lx = static_cast<int>(
        (static_cast<long long>(p.x - m_originalStrategicDrawRect.x) * StrategicOriginalRenderer::kScreenW) /
        m_originalStrategicDrawRect.width);
    const int ly = static_cast<int>(
        (static_cast<long long>(p.y - m_originalStrategicDrawRect.y) * StrategicOriginalRenderer::kScreenH) /
        m_originalStrategicDrawRect.height);

    int delta = 0;
    if (wxRect(222, 419, 34, 28).Contains(lx, ly)) delta = -10;
    if (wxRect(316, 419, 34, 28).Contains(lx, ly)) delta = +10;
    if (delta == 0)
    {
        ev.Skip();
        return;
    }

    SetGlobalResearchAllocation(m_resourcesGlobalResearch + delta);
    SaveStrategicState();
    RefreshResourcesPage();
    m_originalStrategicDirty = true;
    m_originalStrategicPanel->Refresh();
}

void StrategicLevelFrame::OnOriginalStrategicMouseWheel(wxMouseEvent& ev)
{
    if (!m_originalStrategicPanel || m_originalStrategicDrawRect.width <= 0 ||
        m_originalStrategicDrawRect.height <= 0)
    {
        ev.Skip();
        return;
    }

    const wxPoint p = ev.GetPosition();
    if (!m_originalStrategicDrawRect.Contains(p))
    {
        ev.Skip();
        return;
    }

    const int lx = static_cast<int>(
        (static_cast<long long>(p.x - m_originalStrategicDrawRect.x) * StrategicOriginalRenderer::kScreenW) /
        m_originalStrategicDrawRect.width);
    const int ly = static_cast<int>(
        (static_cast<long long>(p.y - m_originalStrategicDrawRect.y) * StrategicOriginalRenderer::kScreenH) /
        m_originalStrategicDrawRect.height);
    const int rotation = ev.GetWheelRotation();
    const int delta = ev.GetWheelDelta() ? ev.GetWheelDelta() : 120;
    int steps = rotation / delta;
    if (steps == 0)
        steps = rotation > 0 ? 1 : -1;

    if (m_originalStrategicScreen == OriginalStrategicScreen::Hierarchy)
    {
        if (lx < 418 || lx >= 575 || ly < 6 || ly >= 426)
        {
            ev.Skip();
            return;
        }
        int totalRows = 0;
        for (const auto& u : m_playerUnits)
            totalRows += std::max(0, u.count);
        const int maxScroll = std::max(0, totalRows - 18);
        if (maxScroll <= 0)
            return;
        m_originalHierarchyUnitScroll = std::clamp(
            m_originalHierarchyUnitScroll - steps * 3, 0, maxScroll);
        m_originalStrategicDirty = true;
        m_originalStrategicPanel->Refresh();
        return;
    }

    if (m_originalStrategicScreen == OriginalStrategicScreen::Units)
    {
        // Upper-right upgrade/re-arm list has its own original scrollbar.
        if (lx >= 418 && lx < 575 && ly >= 6 && ly < 287 &&
            m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < static_cast<int>(m_playerUnits.size()))
        {
            EnsureUpgradeDefsLoaded();
            EnsureResearchLoaded();
            const auto& u = m_playerUnits[static_cast<size_t>(m_unitsSelectedUnit)];
            const auto upgrades = GetAvailableUpgradesForUnit(u.unit_id);
            int rows = 3; // Motory, Zbrane, Obrana headings
            for (int id : upgrades)
            {
                const auto it = m_upgradeDefs.find(id);
                if (it != m_upgradeDefs.end() && it->second.kind != UpgradeDefRec::Unknown)
                    ++rows;
            }
            if (m_unitsCurrentTab == UNITS_TAB_UPGRADE)
            {
                const auto rearm = GetAvailableUnitTypesForUpgrade(u.unit_id);
                if (!rearm.empty()) rows += 1 + static_cast<int>(rearm.size());
            }
            const int visible = std::max(1, (258 - 8) / 14);
            const int maxScroll = std::max(0, rows - visible);
            if (maxScroll > 0)
            {
                m_originalUnitsOptionScroll = std::clamp(m_originalUnitsOptionScroll - steps * 3, 0, maxScroll);
                m_originalStrategicDirty = true;
                m_originalStrategicPanel->Refresh();
            }
            return;
        }

        if (lx < 6 || lx > 320 || ly < 8 || ly >= 312)
        {
            ev.Skip();
            return;
        }
        int totalRows = 0;
        for (const auto& u : m_playerUnits)
            totalRows += std::max(0, u.count);
        const int maxScroll = std::max(0, totalRows - 16);
        if (maxScroll <= 0)
            return;
        m_originalUnitsRosterScroll = std::clamp(m_originalUnitsRosterScroll - steps * 3, 0, maxScroll);
        m_originalStrategicDirty = true;
        m_originalStrategicPanel->Refresh();
        return;
    }

    if (m_originalStrategicScreen == OriginalStrategicScreen::Buy)
    {
        if (lx < 418 || lx >= 575 || ly < 6 || ly >= 287)
        {
            ev.Skip();
            return;
        }
        const auto rows = BuildOriginalBuyRows();
        constexpr int visibleRows = 20;
        const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visibleRows);
        if (maxScroll <= 0)
            return;
        m_originalBuyListScroll = std::clamp(
            m_originalBuyListScroll - steps * 3, 0, maxScroll);
        m_originalStrategicDirty = true;
        m_originalStrategicPanel->Refresh();
        return;
    }

    if (m_originalStrategicScreen == OriginalStrategicScreen::Research)
    {
        if (lx < 418 || lx >= 575 || ly < 6 || ly >= 426)
        {
            ev.Skip();
            return;
        }
        EnsureResearchLoaded();
        int rows = 0;
        wxString lastGroup;
        for (const auto& it : m_researchDb)
        {
            if (!IsResearchAvailable(it)) continue;
            if (it.group != lastGroup) { lastGroup = it.group; if (!lastGroup.empty()) ++rows; }
            ++rows;
        }
        const int maxScroll = std::max(0, rows - 32);
        if (maxScroll > 0)
        {
            m_originalResearchListScroll = std::clamp(m_originalResearchListScroll - steps * 3, 0, maxScroll);
            m_originalStrategicDirty = true;
            m_originalStrategicPanel->Refresh();
            return;
        }
        ev.Skip();
        return;
    }

    if (m_originalStrategicScreen == OriginalStrategicScreen::Info)
    {
        EnsureResearchLoaded();
        if (lx >= 418 && lx < 575 && ly >= 6 && ly < 446)
        {
            int rows = 0;
            wxString lastGroup;
            for (const auto& it : m_researchDb)
            {
                if (!IsInfoItemVisible(it)) continue;
                if (it.group != lastGroup) { lastGroup = it.group; if (!lastGroup.empty()) ++rows; }
                ++rows;
            }
            const int maxScroll = std::max(0, rows - 31);
            if (maxScroll > 0)
            {
                m_originalInfoListScroll = std::clamp(m_originalInfoListScroll - steps * 3, 0, maxScroll);
                m_originalStrategicDirty = true;
                m_originalStrategicPanel->Refresh();
            }
            return;
        }
        if (lx >= 6 && lx < 412 && ly >= 8 && ly < 438 && m_spellData && m_spellData->font)
        {
            wxString body;
            if (m_infoBrowseIndex >= 0 && m_infoBrowseIndex < static_cast<int>(m_researchDb.size()))
            {
                const auto& cur = m_researchDb[static_cast<size_t>(m_infoBrowseIndex)];
                body = cur.info.empty() ? cur.brief : cur.info;
            }
            const auto lines = OriginalWrapText(m_spellData->font, body, 374, 200);
            const int maxScroll = std::max(0, static_cast<int>(lines.size()) - 29);
            if (maxScroll > 0)
            {
                m_originalInfoTextScroll = std::clamp(m_originalInfoTextScroll - steps * 3, 0, maxScroll);
                m_originalStrategicDirty = true;
                m_originalStrategicPanel->Refresh();
            }
            return;
        }
        ev.Skip();
        return;
    }

    if (m_originalStrategicScreen == OriginalStrategicScreen::Resources ||
        m_originalStrategicScreen == OriginalStrategicScreen::Stats ||
        m_originalStrategicScreen == OriginalStrategicScreen::Options)
    {
        ev.Skip();
        return;
    }

    if (m_originalStrategicScreen != OriginalStrategicScreen::Map ||
        lx < 416 || lx > 574 || ly < 55 || ly > 432)
    {
        ev.Skip();
        return;
    }

    const int totalRows = static_cast<int>(m_rosterRowUids.size());
    const int maxScroll = std::max(0, totalRows - kOriginalVisibleRows);
    if (maxScroll <= 0)
        return;

    m_originalUnitScroll = std::clamp(m_originalUnitScroll - steps * 3, 0, maxScroll);
    m_originalStrategicDirty = true;
    m_originalStrategicPanel->Refresh();
}

void StrategicLevelFrame::OnOriginalStrategicMouseMove(wxMouseEvent& ev)
{
    if (!m_originalStrategicPanel || m_originalStrategicDrawRect.width <= 0 ||
        m_originalStrategicDrawRect.height <= 0)
    {
        ev.Skip();
        return;
    }

    const wxPoint p = ev.GetPosition();
    int newToolbar = -1;
    int newAction = -1;
    bool newEndTurn = false;
    int newTerritory = 0;

    if (m_originalStrategicDrawRect.Contains(p))
    {
        const int lx = static_cast<int>(
            (static_cast<long long>(p.x - m_originalStrategicDrawRect.x) * StrategicOriginalRenderer::kScreenW) /
            m_originalStrategicDrawRect.width);
        const int ly = static_cast<int>(
            (static_cast<long long>(p.y - m_originalStrategicDrawRect.y) * StrategicOriginalRenderer::kScreenH) /
            m_originalStrategicDrawRect.height);

        // STRBAR.QH uses 37x24 plates with a 31 px vertical pitch.  Keep the
        // seven-pixel gaps inert just like the original UI.
        if (lx >= 590 && lx < 627 && ly >= 131)
        {
            const int slot = (ly - 131) / kOriginalToolbarPitch;
            const int inSlotY = (ly - 131) % kOriginalToolbarPitch;
            if (slot >= 0 && slot < kOriginalToolbarCount && inSlotY < 24)
                newToolbar = slot;
        }

        newEndTurn = wxRect(587, 431, 47, 44).Contains(lx, ly);

        if (m_originalStrategicScreen == OriginalStrategicScreen::Map)
        {
            if (wxRect(kOriginalAttackX, kOriginalAttackY, kOriginalButtonW, kOriginalButtonH).Contains(lx, ly))
                newAction = 0;
            else if (wxRect(kOriginalCancelX, kOriginalAttackY, kOriginalButtonW, kOriginalButtonH).Contains(lx, ly))
                newAction = 1;

            const int mx = lx - StrategicOriginalRenderer::kMapX;
            const int my = ly - StrategicOriginalRenderer::kMapY;
            if (m_hasClk && mx >= 0 && my >= 0 && mx < m_clkW && my < m_clkH &&
                m_clkValues.size() >= static_cast<size_t>(m_clkW) * m_clkH)
            {
                const std::uint8_t raw = m_clkValues[static_cast<size_t>(my) * m_clkW + mx];
                int tid = raw >= 128 ? static_cast<int>(raw) - 128 : static_cast<int>(raw);
                if (tid > 0)
                {
                    const bool visible = !m_gameModeEnabled ||
                        (tid < static_cast<int>(m_visibleTerritory.size()) && m_visibleTerritory[tid] != 0);
                    const bool owned = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid)
                        != m_ownedTerritories.end();
                    // The native animated hatch belongs only to revealed enemy
                    // territory; owned and fogged regions stay still.
                    if (visible && !owned)
                        newTerritory = tid;
                }
            }
        }
    }

    if (newToolbar != m_originalToolbarHoverSlot ||
        newAction != m_originalActionHover ||
        newEndTurn != m_originalEndTurnHover ||
        newTerritory != m_hoverTerritory)
    {
        m_originalToolbarHoverSlot = newToolbar;
        m_originalActionHover = newAction;
        m_originalEndTurnHover = newEndTurn;
        m_hoverTerritory = newTerritory;
        m_originalStrategicDirty = true;
        m_originalStrategicPanel->Refresh(false);
    }

    ev.Skip();
}

void StrategicLevelFrame::OnOriginalStrategicMouseLeave(wxMouseEvent& ev)
{
    const bool changed = m_originalToolbarHoverSlot != -1 || m_originalActionHover != -1 ||
        m_originalEndTurnHover || m_hoverTerritory != 0;
    m_originalToolbarHoverSlot = -1;
    m_originalActionHover = -1;
    m_originalEndTurnHover = false;
    m_hoverTerritory = 0;
    if (changed && m_originalStrategicPanel)
    {
        m_originalStrategicDirty = true;
        m_originalStrategicPanel->Refresh(false);
    }
    ev.Skip();
}

void StrategicLevelFrame::OnOriginalStrategicAnimTimer(wxTimerEvent&)
{
    if (!m_originalStrategicUi || !m_originalStrategicPanel)
        return;

    bool repaint = false;

    // Original DOS end-turn hover is a horizontal wipe, not an alternating
    // two-frame blink.  At 60 ms and 4 px/tick the 41 px sprite takes about
    // 0.66 s to open/close, matching the supplied original-game video.
    const int endTurnTarget = m_originalEndTurnHover ? 41 : 0;
    if (m_originalEndTurnReveal != endTurnTarget)
    {
        if (m_originalEndTurnReveal < endTurnTarget)
            m_originalEndTurnReveal = std::min(endTurnTarget, m_originalEndTurnReveal + 4);
        else
            m_originalEndTurnReveal = std::max(endTurnTarget, m_originalEndTurnReveal - 4);
        repaint = true;
    }

    // Keep the already-approved territory hatch at its Stage 6.8 ~120 ms
    // cadence even though the common animation timer now runs at 60 ms.
    if (++m_originalStrategicAnimSubTick >= 2)
    {
        m_originalStrategicAnimSubTick = 0;
        if (m_originalStrategicScreen == OriginalStrategicScreen::Map && m_hoverTerritory > 0)
        {
            m_originalStrategicAnimPhase = (m_originalStrategicAnimPhase + 1) % 14;
            repaint = true;
        }
    }

    if (!repaint)
        return;

    m_originalStrategicDirty = true;
    m_originalStrategicPanel->Refresh(false);
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

void StrategicLevelFrame::SaveStrategicGameToSlot(int slot, bool notify)
{
    slot = std::clamp(slot, 1, 10);
    const auto path = GetStrategicSaveSlotPath(m_level, slot);

    ResearchPersistSaveView rsv;
    rsv.activeId = m_researchActiveId;
    rsv.activeIndex = m_researchActiveIndex;
    rsv.allocPerTurn = m_researchAllocPerTurn;
    rsv.progressById = &m_researchProgressById;
    rsv.completed = &m_researchCompleted;
    const ResearchPersistSaveView* prevR = g_researchPersistSave;
    g_researchPersistSave = &rsv;

    UnitStatePersistSaveView usv;
    usv.states = &m_unitStates;
    const UnitStatePersistSaveView* prevU = g_unitStatePersistSave;
    g_unitStatePersistSave = &usv;

    MissionFlowPersistSaveView mfsv;
    mfsv.timeoutTurn = &m_territoryTimeoutTurn;
    mfsv.triggeredEvents = &m_triggeredLevelEvents;
    mfsv.activatedEvents = &m_activatedEvents;
    mfsv.counterAttacks = &m_counterAttacks;
    const MissionFlowPersistSaveView* prevMF = g_missionFlowPersistSave;
    g_missionFlowPersistSave = &mfsv;

    SaveStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
        m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
        m_playerCommanders, m_availableCommanders, m_cmdGenWindowStartTurn, m_cmdGenCountInWindow,
        m_gameModeEnabled, m_ownedTerritories, m_territoryResources,
        /*timestamp*/NowIsoLocal());

    g_missionFlowPersistSave = prevMF;
    g_unitStatePersistSave = prevU;
    g_researchPersistSave = prevR;

    m_originalStrategicDirty = true;
    if (notify)
        wxMessageBox(wxString::Format("Saved to slot %02d.", slot), "Save game", wxOK | wxICON_INFORMATION, this);
}

void StrategicLevelFrame::LoadStrategicGameFromSlot(int slot, bool notify)
{
    slot = std::clamp(slot, 1, 10);
    const auto path = GetStrategicSaveSlotPath(m_level, slot);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        if (notify)
            wxMessageBox("This slot is empty.", "Load game", wxOK | wxICON_WARNING, this);
        return;
    }

    std::string loaded_level_def;
    std::string ts;

    std::vector<UnitInstanceState> loadedUnitStates;
    UnitStatePersistLoadView ulv;
    ulv.states = &loadedUnitStates;
    UnitStatePersistLoadView* prevUL = g_unitStatePersistLoad;
    g_unitStatePersistLoad = &ulv;

    MissionFlowPersistLoadView mflv;
    mflv.timeoutTurn = &m_territoryTimeoutTurn;
    mflv.triggeredEvents = &m_triggeredLevelEvents;
    mflv.activatedEvents = &m_activatedEvents;
    mflv.counterAttacks = &m_counterAttacks;
    MissionFlowPersistLoadView* prevMFL = g_missionFlowPersistLoad;
    g_missionFlowPersistLoad = &mflv;

    if (!LoadStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
        m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
        m_playerCommanders, m_availableCommanders, m_cmdGenWindowStartTurn, m_cmdGenCountInWindow,
        m_gameModeEnabled, m_ownedTerritories, m_territoryResources,
        &loaded_level_def, &ts))
    {
        g_missionFlowPersistLoad = prevMFL;
        g_unitStatePersistLoad = prevUL;
        if (notify)
            wxMessageBox("Failed to load the saved game.", "Load game", wxOK | wxICON_ERROR, this);
        return;
    }
    g_missionFlowPersistLoad = prevMFL;
    g_unitStatePersistLoad = prevUL;
    m_unitStates = std::move(loadedUnitStates);

    if (GetMenuBar())
    {
        auto* item = GetMenuBar()->FindItem(ID_MENU_GAME_MODE_TOGGLE);
        if (item) item->Check(m_gameModeEnabled);
    }

    if (!loaded_level_def.empty() && loaded_level_def != m_level.source_path)
    {
        if (!m_main)
        {
            if (notify)
            {
                wxString msg;
                msg << L"This save belongs to a different level/DEF:\n\n";
                msg << wxString::FromUTF8(loaded_level_def) << L"\n\n";
                msg << L"Current level is:\n\n";
                msg << wxString::FromUTF8(m_level.source_path) << L"\n";
                wxMessageBox(msg, L"Load game", wxOK | wxICON_WARNING, this);
            }
            return;
        }

        LevelData lvl;
        std::string err;
        LevelLoader loader;
        if (!loader.LoadLevelDef(loaded_level_def, lvl, &err))
        {
            if (notify)
                wxMessageBox(L"Failed to load the level DEF from this save:\n" + wxString::FromUTF8(err),
                    L"Load game", wxOK | wxICON_ERROR, this);
            return;
        }

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

        std::vector<UnitInstanceState> loadedUnitStates2;
        UnitStatePersistLoadView ulv2;
        ulv2.states = &loadedUnitStates2;
        UnitStatePersistLoadView* prevUL2 = g_unitStatePersistLoad;
        g_unitStatePersistLoad = &ulv2;

        if (!LoadStrategicStateFile(path, lvl, turn, money, research, selTerr, pl, terrMission, terrLaunch,
            units, playerCmds2, availCmds2, windowStart2, genCount2, gm2, owned2, terrRes, &def2, &ts2))
        {
            g_unitStatePersistLoad = prevUL2;
            if (notify)
                wxMessageBox(L"Failed to load the saved game.", L"Load game", wxOK | wxICON_ERROR, this);
            return;
        }
        g_unitStatePersistLoad = prevUL2;

        auto* win = new StrategicLevelFrame(m_main, lvl, /*skipAutosave=*/true);
        win->m_turn = turn;
        win->m_money = money;
        win->m_research = research;
        win->m_selectedTerritory = selTerr;
        win->m_player = pl;
        win->m_territoryCurrentMission = std::move(terrMission);
        win->m_territoryLaunchCount = std::move(terrLaunch);
        win->m_playerUnits = std::move(units);
        win->m_unitStates = std::move(loadedUnitStates2);
        win->m_playerCommanders = std::move(playerCmds2);
        win->m_availableCommanders = std::move(availCmds2);
        win->m_cmdGenWindowStartTurn = windowStart2;
        win->m_cmdGenCountInWindow = genCount2;
        win->m_gameModeEnabled = gm2;
        win->m_ownedTerritories = std::move(owned2);
        win->TryLoadBackground();
        win->RefreshUI();
        win->SetOriginalStrategicUi(true);
        if (win->m_selectedTerritory >= 0)
            win->SelectTerritoryById(win->m_selectedTerritory);
        win->Show();
        win->Raise();
        Close(true);
        return;
    }

    TryLoadBackground();
    CheckTimeouts();
    if (m_selectedTerritory >= 0)
        SelectTerritoryById(m_selectedTerritory);
    RefreshUI();
    m_originalStrategicDirty = true;
    if (notify)
        wxMessageBox(wxString::Format("Loaded slot %02d.", slot), "Load game", wxOK | wxICON_INFORMATION, this);
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

    // Save full strategic state into slot file (with research + unit state persistence)
    ResearchPersistSaveView rsv;
    rsv.activeId = m_researchActiveId;
    rsv.activeIndex = m_researchActiveIndex;
    rsv.allocPerTurn = m_researchAllocPerTurn;
    rsv.progressById = &m_researchProgressById;
    rsv.completed = &m_researchCompleted;
    const ResearchPersistSaveView* prevR = g_researchPersistSave;
    g_researchPersistSave = &rsv;

    UnitStatePersistSaveView usv;
    usv.states = &m_unitStates;
    const UnitStatePersistSaveView* prevU = g_unitStatePersistSave;
    g_unitStatePersistSave = &usv;

    MissionFlowPersistSaveView mfsv;
    mfsv.timeoutTurn = &m_territoryTimeoutTurn;
    mfsv.triggeredEvents = &m_triggeredLevelEvents;
    mfsv.activatedEvents = &m_activatedEvents;
    mfsv.counterAttacks = &m_counterAttacks;
    const MissionFlowPersistSaveView* prevMF = g_missionFlowPersistSave;
    g_missionFlowPersistSave = &mfsv;

    SaveStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
        m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
        m_playerCommanders, m_availableCommanders, m_cmdGenWindowStartTurn, m_cmdGenCountInWindow,
        m_gameModeEnabled, m_ownedTerritories, m_territoryResources,
        /*timestamp*/NowIsoLocal());

    g_missionFlowPersistSave = prevMF;
    g_unitStatePersistSave = prevU;
    g_researchPersistSave = prevR;

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

    // Hook unit state persistence for load
    std::vector<UnitInstanceState> loadedUnitStates;
    UnitStatePersistLoadView ulv;
    ulv.states = &loadedUnitStates;
    UnitStatePersistLoadView* prevUL = g_unitStatePersistLoad;
    g_unitStatePersistLoad = &ulv;

    MissionFlowPersistLoadView mflv;
    mflv.timeoutTurn = &m_territoryTimeoutTurn;
    mflv.triggeredEvents = &m_triggeredLevelEvents;
    mflv.activatedEvents = &m_activatedEvents;
    mflv.counterAttacks = &m_counterAttacks;
    MissionFlowPersistLoadView* prevMFL = g_missionFlowPersistLoad;
    g_missionFlowPersistLoad = &mflv;

    if (!LoadStrategicStateFile(path, m_level, m_turn, m_money, m_research, m_selectedTerritory, m_player,
        m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
        m_playerCommanders, m_availableCommanders, m_cmdGenWindowStartTurn, m_cmdGenCountInWindow,
        m_gameModeEnabled, m_ownedTerritories, m_territoryResources,
        &loaded_level_def, &ts))
    {
        g_missionFlowPersistLoad = prevMFL;
        g_unitStatePersistLoad = prevUL;
        wxMessageBox("Failed to load the saved game.", "Load game", wxOK | wxICON_ERROR, this);
        return;
    }
    g_missionFlowPersistLoad = prevMFL;
    g_unitStatePersistLoad = prevUL;
    m_unitStates = std::move(loadedUnitStates);

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

        // Hook unit state persistence for cross-level load
        std::vector<UnitInstanceState> loadedUnitStates2;
        UnitStatePersistLoadView ulv2;
        ulv2.states = &loadedUnitStates2;
        UnitStatePersistLoadView* prevUL2 = g_unitStatePersistLoad;
        g_unitStatePersistLoad = &ulv2;

        if (!LoadStrategicStateFile(path, lvl, turn, money, research, selTerr, pl, terrMission, terrLaunch, units, playerCmds2, availCmds2, windowStart2, genCount2, gm2, owned2, terrRes, &def2, &ts2)) {
            g_unitStatePersistLoad = prevUL2;
            wxMessageBox(L"Failed to load the saved game.", L"Load game", wxOK | wxICON_ERROR, this);
            return;
        }
        g_unitStatePersistLoad = prevUL2;

        // Open new Strategic Level window for that DEF and apply loaded state.
        auto* win = new StrategicLevelFrame(m_main, lvl, /*skipAutosave=*/true);

        win->m_turn = turn;
        win->m_money = money;
        win->m_research = research;
        win->m_selectedTerritory = selTerr;
        win->m_player = pl;
        win->m_territoryCurrentMission = std::move(terrMission);
        win->m_territoryLaunchCount = std::move(terrLaunch);
        win->m_playerUnits = std::move(units);
        win->m_unitStates = std::move(loadedUnitStates2);
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

    // Rebuild background, visibility and timeouts for loaded state
    TryLoadBackground();
    CheckTimeouts();

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

static int PickStartTerritoryIdForGameMode(const LevelData& level, const SpellData* spellData = nullptr)
{
    // 1) Some levels explicitly mark the "home" territory with an empty mission token.
    for (const auto& t : level.territories)
        if (trim(t.mission).empty())
            return t.id;

    // 2) Otherwise, pick the first territory that has NO briefing text file.
    // (This matches the original campaign behavior for e.g. LEVEL_07 where start != T01.)
    const int byNoBrief = ChooseDefaultStartTerritoryId_NoBriefing(level, spellData);
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
        m_ownedTerritories.push_back(PickStartTerritoryIdForGameMode(m_level, m_spellData));


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
            m_ownedTerritories = ChooseStartTerritories_NoBriefing(m_level, m_spellData);

            // Fallback: at least one
            if (m_ownedTerritories.empty() && !m_level.territories.empty())
                m_ownedTerritories.push_back(m_level.territories.front().id);
        }

        m_hoverTerritory = 0;

    }

    // Rebuild background, because baked borders must be ON in editor mode and OFF in game mode.
    TryLoadBackground();
    ApplyTerritoryVisibility();

    // Do not keep a hidden selection when campaign fog is enabled.  More
    // importantly, rebuild the lower information panel immediately when the
    // mode changes; otherwise it retained the previous mode's empty/brief
    // text until the player clicked a second territory.
    if (m_gameModeEnabled && m_selectedTerritory > 0 &&
        (m_selectedTerritory >= (int)m_visibleTerritory.size() ||
            m_visibleTerritory[m_selectedTerritory] == 0))
    {
        m_selectedTerritory = m_ownedTerritories.empty() ? -1 : m_ownedTerritories.front();
    }

    CheckTimeouts();
    MarkOverlayDirty();
    RefreshUI();

    if (m_selectedTerritory > 0)
        SelectTerritoryById(m_selectedTerritory);

    SaveStrategicState(); // persist the normalized mode/selection state

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

    const double oldSfx = m_spellData->sounds->channels->GetVolume();
    const double oldMusic = m_spellData->midi->GetVolume();

    wxDialog dlg(this, wxID_ANY, "Audio", wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE);

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

    auto applyAudio = [&]() {
        m_spellData->midi->SetVolume(sldMusic->GetValue() / 100.0);
        m_spellData->sounds->channels->SetVolume(sldSfx->GetValue() / 100.0);
    };

    sldMusic->Bind(wxEVT_SLIDER, [&](wxCommandEvent&) { applyAudio(); });
    sldSfx->Bind(wxEVT_SLIDER, [&](wxCommandEvent&) { applyAudio(); });

    if (dlg.ShowModal() == wxID_OK)
        applyAudio();
    else
    {
        m_spellData->midi->SetVolume(oldMusic);
        m_spellData->sounds->channels->SetVolume(oldSfx);
    }
}

void StrategicLevelFrame::OnOptionsScreen(wxCommandEvent& ev)
{
    SpellMap* spellMap = m_main ? m_main->GetSpellMap() : nullptr;
    const double oldGamma = spellMap ? spellMap->GetGamma() : 1.3;

    wxDialog dlg(this, wxID_ANY, "Screen", wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE);

    auto* sizerTop = new wxBoxSizer(wxVERTICAL);

    auto* lblBrightness = new wxStaticText(&dlg, wxID_ANY, "Brightness");
    auto* sldBrightness = new wxSlider(&dlg, wxID_ANY,
        (int)std::lround(oldGamma * 1000.0),
        500, 2000,
        wxDefaultPosition, wxSize(300, -1),
        wxSL_HORIZONTAL | wxSL_VALUE_LABEL);

    sizerTop->Add(lblBrightness, 0, wxALL, 8);
    sizerTop->Add(sldBrightness, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
    sizerTop->Add(btns, 0, wxALL | wxEXPAND, 8);
    dlg.SetSizerAndFit(sizerTop);

    auto applyScreen = [&]() {
        if (spellMap)
            spellMap->SetGamma(sldBrightness->GetValue() * 0.001);
    };

    sldBrightness->Bind(wxEVT_SLIDER, [&](wxCommandEvent&) { applyScreen(); });

    if (dlg.ShowModal() == wxID_OK)
        applyScreen();
    else
    {
        if (spellMap)
            spellMap->SetGamma(oldGamma);
    }
}

bool StrategicLevelFrame::EnsureUnitCostsLoaded()
{
    if (m_unitCostsLoaded)
        return true;

    m_unitCosts.clear();
    m_unitUpgradeCosts.clear();
    const auto path = GetUnitsJsonPath();
    if (!LoadUnitCostsFromJson(path, m_unitCosts, &m_unitCategories, &m_unitUpgradeCosts))
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

void StrategicLevelFrame::GetOriginalBuyLimits(int& maxUnits, int& maxCommanders)
{
    LoadRanksTable();

    // Rank is derived from the two authoritative progress counters.  Older
    // autosaves (and saves made by the pre-restoration wx buy page) can carry
    // a stale rank field, which made the restored BUY page believe that every
    // free slot was locked even though the campaign had already progressed.
    RecomputePlayerRank();

    maxUnits = 32;
    maxCommanders = 14;
    if (const CommanderRankRec* rank = FindRankRec(m_player.rank))
    {
        maxUnits = std::clamp(rank->max_units, 0, 32);
        maxCommanders = std::clamp(rank->max_commanders, 0, 14);
    }

    // Compatibility for saves produced before the Original UI enforced the
    // rank limits: never render already-owned permanent units/commanders as
    // being outside the available roster.  Advance only to the smallest
    // original rank capacity that can contain the existing roster; this does
    // not grant a free extra slot when the roster is exactly at its valid cap.
    int ownedUnits = 0;
    for (const auto& u : m_playerUnits)
        ownedUnits += std::max(0, u.count);
    if (ownedUnits > maxUnits)
    {
        int compatible = 32;
        for (const auto& r : m_ranks)
        {
            const int cap = std::clamp(r.max_units, 0, 32);
            if (cap >= ownedUnits)
                compatible = std::min(compatible, cap);
        }
        maxUnits = std::max(maxUnits, compatible);
    }

    const int ownedCommanders = static_cast<int>(m_playerCommanders.size());
    if (ownedCommanders > maxCommanders)
    {
        int compatible = 14;
        for (const auto& r : m_ranks)
        {
            const int cap = std::clamp(r.max_commanders, 0, 14);
            if (cap >= ownedCommanders)
                compatible = std::min(compatible, cap);
        }
        maxCommanders = std::max(maxCommanders, compatible);
    }
}

std::vector<StrategicLevelFrame::OriginalBuyRow> StrategicLevelFrame::BuildOriginalBuyRows()
{
    EnsureResearchLoaded();
    EnsureUnitCostsLoaded();

    int maxUnits = 32;
    int maxCommanders = 14;
    GetOriginalBuyLimits(maxUnits, maxCommanders);

    int ownedUnits = 0;
    for (const auto& u : m_playerUnits)
        ownedUnits += std::max(0, u.count);
    const bool unitSlotAvailable = ownedUnits < maxUnits;
    const bool commanderSlotAvailable = static_cast<int>(m_playerCommanders.size()) < maxCommanders;

    struct Group
    {
        const wchar_t* title;
        std::vector<OriginalBuyRow> rows;
    };
    std::array<Group, 7> groups = {{
        { L"P\u011Bchota", {} },
        { L"Tanky", {} },
        { L"D\u011Blost\u0159electvo", {} },
        { L"Transport\u00E9ry", {} },
        { L"Radary", {} },
        { L"Protivzdu\u0161n\u00E1", {} },
        { L"Ostatn\u00ED", {} }
    }};

    if (m_spellData && m_spellData->units)
    {
        for (const auto* unit : m_spellData->units->GetUnits())
        {
            if (!unit)
                continue;
            const int tid = unit->type_id;
            const int cost = GetUnitBuyCost(tid);
            if (cost <= 0)
                continue;

            // The original buy list only exposes technology which has become
            // available on this strategic level.
            if (m_gameModeEnabled && !IsCampaignUnitUnlocked(tid))
                continue;

            std::string category;
            if (auto it = m_unitCategories.find(tid); it != m_unitCategories.end())
                category = it->second;

            int group = -1;
            if (tid == 30 || tid == 31)
                group = 4; // the two Universal/Radar variants are a native group
            else if (category == "Infantry")
                group = 0;
            else if (category == "Tanks")
                group = 1;
            else if (category == "Artillery")
                group = 2;
            else if (category == "Transporters")
                group = 3;
            else if (category == "AA units" || category == "Aerial guns")
                group = 5;
            else if (category == "Other")
                group = 6;
            else
                continue; // Other Side and scenario-only units are not purchasable here.

            OriginalBuyRow row;
            row.kind = OriginalBuyRowKind::Unit;
            row.label = GetUnitDisplayName(tid);
            row.id = tid;
            row.enabled = unitSlotAvailable && m_money >= cost;
            groups[static_cast<size_t>(group)].rows.push_back(std::move(row));
        }
    }

    std::vector<OriginalBuyRow> result;
    for (const auto& group : groups)
    {
        if (group.rows.empty())
            continue;
        result.push_back({ OriginalBuyRowKind::Heading, wxString(group.title), -1, true });
        result.insert(result.end(), group.rows.begin(), group.rows.end());
        result.push_back({ OriginalBuyRowKind::Spacer, wxString(), -1, false });
    }

    if (!m_availableCommanders.empty())
    {
        result.push_back({ OriginalBuyRowKind::Heading, L"Velitel\u00E9", -1, true });
        auto rankAbbrev = [](int rank) -> wxString
        {
            static const wchar_t* names[] = {
                L"por.", L"npor.", L"kpt.", L"mjr.", L"pplk.",
                L"plk.", L"genmjr.", L"genpor.", L"armgen."
            };
            if (rank < 0 || rank >= static_cast<int>(sizeof(names) / sizeof(names[0])))
                return wxString::Format(L"R%d", rank);
            return wxString(names[rank]);
        };
        for (int i = 0; i < static_cast<int>(m_availableCommanders.size()); ++i)
        {
            const auto& commander = m_availableCommanders[static_cast<size_t>(i)];
            OriginalBuyRow row;
            row.kind = OriginalBuyRowKind::Commander;
            row.label = rankAbbrev(commander.rank) + L" " + wxString::FromUTF8(commander.name);
            row.id = i;
            row.enabled = commanderSlotAvailable;
            result.push_back(std::move(row));
        }
    }

    while (!result.empty() && result.back().kind == OriginalBuyRowKind::Spacer)
        result.pop_back();
    return result;
}

bool StrategicLevelFrame::EnsureUpgradeDefsLoaded()
{
    if (m_upgradeDefsLoaded)
        return true;

    m_upgradeDefs.clear();

    namespace fs = std::filesystem;
    std::error_code ec;

    // Search for UPGRADES.DEF in common locations
    const fs::path base = GetStableBaseDir();
    const std::vector<fs::path> candidates = {
        base / "temp" / "COMMON" / "UPGRADES.DEF",
        base / "builds" / "x64" / "Release" / "temp" / "COMMON" / "UPGRADES.DEF",
        base / "builds" / "x64" / "Debug" / "temp" / "COMMON" / "UPGRADES.DEF",
        base / "data" / "UPGRADES.DEF",
        fs::current_path(ec) / "temp" / "COMMON" / "UPGRADES.DEF",
    };

    fs::path defPath;
    for (const auto& p : candidates)
    {
        if (!p.empty() && fs::exists(p, ec))
        {
            defPath = p;
            break;
        }
    }

    if (defPath.empty())
    {
        m_upgradeDefsLoaded = true;
        return false;
    }

    std::ifstream f(defPath);
    if (!f)
    {
        m_upgradeDefsLoaded = true;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.empty())
    {
        m_upgradeDefsLoaded = true;
        return false;
    }

    // Parse Upgrade(N) { ... } blocks
    UpgradeDefRec cur;
    int curId = -1;
    bool inside = false;
    std::istringstream ss(content);
    std::string line;

    while (std::getline(ss, line))
    {
        // Strip CR
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // Skip leading whitespace
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos)
            continue;
        line = line.substr(s);

        // Skip comments
        if (!line.empty() && line[0] == ';')
            continue;

        // Match Upgrade(N) {
        static const std::regex rxUpgrade(R"(Upgrade\s*\(\s*(\d+)\s*\)\s*\{)");
        std::smatch m;
        if (std::regex_search(line, m, rxUpgrade))
        {
            cur = UpgradeDefRec{};
            curId = std::stoi(m[1].str());
            cur.id = curId;
            inside = true;
            continue;
        }

        if (!inside)
            continue;

        // End of block
        if (line[0] == '}')
        {
            if (curId >= 0)
                m_upgradeDefs[curId] = std::move(cur);
            inside = false;
            curId = -1;
            continue;
        }

        // Parse fields inside block
        static const std::regex rxPrice(R"(UpgradePrice\s*\(\s*(\d+)\s*\))");
        static const std::regex rxTime(R"(UpgradeTime\s*\(\s*(\d+)\s*\))");
        static const std::regex rxTypes(R"(SuitableTypes\s*\(\s*([^)]+)\s*\))");
        static const std::regex rxFlags(R"(Flags\s*\(\s*(\w+)\s*\))");

        if (std::regex_search(line, m, rxPrice))
            cur.price = std::stoi(m[1].str());

        if (std::regex_search(line, m, rxTime))
            cur.time = std::stoi(m[1].str());

        if (std::regex_search(line, m, rxFlags))
        {
            const std::string kind = m[1].str();
            if (kind == "Engine") cur.kind = UpgradeDefRec::Engine;
            else if (kind == "Weapon") cur.kind = UpgradeDefRec::Weapon;
            else if (kind == "Armor") cur.kind = UpgradeDefRec::Armor;
            else cur.kind = UpgradeDefRec::Unknown;
        }

        if (std::regex_search(line, m, rxTypes))
        {
            // Parse comma-separated list of type IDs
            std::string types = m[1].str();
            std::istringstream tss(types);
            std::string tok;
            while (std::getline(tss, tok, ','))
            {
                size_t ts = tok.find_first_not_of(" \t");
                if (ts != std::string::npos)
                {
                    try {
                        int tid = std::stoi(tok.substr(ts));
                        cur.suitableTypes.insert(tid);
                    }
                    catch (...) {}
                }
            }
        }
    }

    // Original upgrade display names are line-indexed by upgrade id.  Prefer
    // the Czech table because the restored UI uses the original CP895 font.
    for (const char* titleFile : { "UPGRADES.CZ", "UPGRADES.ENG" })
    {
        const fs::path tp = defPath.parent_path() / titleFile;
        std::ifstream tf(tp, std::ios::binary);
        if (!tf)
            continue;
        std::string line;
        int idx = 0;
        while (std::getline(tf, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            while (!line.empty() && static_cast<unsigned char>(line.back()) < 0x20) line.pop_back();
            auto it = m_upgradeDefs.find(idx);
            if (it != m_upgradeDefs.end() && !line.empty())
                it->second.title = wxString(char2wstringCP895(line.c_str()));
            ++idx;
        }
        break;
    }

    m_upgradeDefsLoaded = true;
    return !m_upgradeDefs.empty();
}


void StrategicLevelFrame::BuildUI()
{
    m_rootPanel = new wxPanel(this);
    auto* root = m_rootPanel;
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
    // VMM_FULL uses an exact 299:181 vertical split (map frame / briefing frame).
    m_mapSizer->Add(m_mapCanvas, kMapChromeH, wxEXPAND);

    // Under-map panel: (optional) territory grid fallback + briefing/info text.
    auto* under = new wxPanel(m_mapPanel);
    under->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(under, m_spellData, "VMM_FULL.LZ",
        wxRect(0, kMapChromeH, kMapChromeW, kStrategicScreenH - kMapChromeH));
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
    underSizer->Add(m_territoryButtonsPanel, 0, wxEXPAND);

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
    info->SetMinSize(wxSize(1, 1));

    // Inner opening of the original lower VMM frame: x=17..395, y=322..455.
    // Proportional spacers keep it aligned at every window size.
    underSizer->AddStretchSpacer(23);
    auto* briefingRow = new wxBoxSizer(wxHORIZONTAL);
    briefingRow->AddStretchSpacer(17);
    briefingRow->Add(info, 379, wxEXPAND);
    briefingRow->AddStretchSpacer(16);
    underSizer->Add(briefingRow, 134, wxEXPAND);
    underSizer->AddStretchSpacer(24);

    under->SetSizer(underSizer);
    m_mapSizer->Add(under, kStrategicScreenH - kMapChromeH, wxEXPAND);

    m_mapPanel->SetSizer(m_mapSizer);

    // --- Page 1: Hierarchy ---
    auto* hierarchyPanel = new wxPanel(m_leftBook);
    hierarchyPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(hierarchyPanel, m_spellData, "VMH_FULL.LZ",
        wxRect(0, 0, kMapChromeW, kStrategicScreenH));
    BuildHierarchyPage(hierarchyPanel);

    // --- Page 2: Resources ---
    m_resourcesPanel = new wxPanel(m_leftBook);
    m_resourcesPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(m_resourcesPanel, m_spellData, "VMF_FULL.LZ",
        wxRect(0, 0, kMapChromeW, kStrategicScreenH));
    BuildResourcesPage();

    // --- Page 3: Statistics (integrated into this frame) ---
    m_statsPanel = new wxPanel(m_leftBook);
    m_statsPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(m_statsPanel, m_spellData, "VMS_FULL.LZ",
        wxRect(0, 0, kMapChromeW, kStrategicScreenH));
    BuildStatsPage();


    // --- Page 4: Research – left side (active research + browser detail) ---
    m_researchPanel = new wxPanel(m_leftBook);
    m_researchPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(m_researchPanel, m_spellData, "VMR_FULL.LZ",
        wxRect(0, 0, kMapChromeW, kStrategicScreenH));
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
    BindStrategicScreenSlice(m_infoPanel, m_spellData, "VMI_FULL.LZ",
        wxRect(0, 0, kMapChromeW, kStrategicScreenH));
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

    mainSizer->Add(m_leftBook, kMapChromeW, wxEXPAND);

    // ============================================================
    // MIDDLE: player units (always visible)
    // ============================================================
    // Book for middle area: roster vs research
    m_midBook = new wxSimplebook(m_normalLayoutPanel, wxID_ANY);
    m_midBook->SetBackgroundColour(m_palette.background);

    m_midRosterPanel = new wxPanel(m_midBook);
    auto* mid = m_midRosterPanel;
    mid->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(mid, m_spellData, "VMM_FULL.LZ",
        wxRect(kMapChromeW, 0, kStrategicScreenW - kMapChromeW, kStrategicScreenH));
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
    m_cmdRoster->Bind(wxEVT_LIST_ITEM_SELECTED, &StrategicLevelFrame::OnCommanderSelectForMission, this);
    m_cmdRoster->SetDropTarget(new HierarchyPoolDropTarget(this, "commander"));
    BindListGridOverlay(m_cmdRoster);
    // Commanders list - compact height, units list gets more space
    {
        const int rowsVisible = 5;                   // fewer rows for commanders
        const int ch = m_cmdRoster->GetCharHeight();
        const int rowH = ch + 8;
        const int headerH = ch + 18;
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

    // v BuildUI(): úprava definice sloupců m_roster - selection managed manually via toggle
    m_roster = new wxListCtrl(mid, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_roster->SetFont(m_fontText);
    m_roster->SetBackgroundColour(m_palette.background);
    m_roster->SetForegroundColour(m_palette.text);
    // PŮVODNĚ: InsertColumn(0, "Unit"); InsertColumn(1, "Count"); InsertColumn(2, "HP");
    // NOVĚ: jen dva sloupce: Unit a HP
    m_roster->InsertColumn(0, "Unit");
    m_roster->InsertColumn(1, "HP");
    // Use SELECTED event for toggle behavior (click = toggle selection state)
    m_roster->Bind(wxEVT_LIST_ITEM_SELECTED, &StrategicLevelFrame::OnUnitSelectForMission, this);
    // Units are no longer dragged into hierarchy slots; assignment is done by selecting a unit under a commander.
    // m_roster->Bind(wxEVT_LIST_BEGIN_DRAG, &StrategicLevelFrame::OnRosterBeginDrag, this);
    // m_roster->SetDropTarget(new HierarchyPoolDropTarget(this, "unit"));
    BindListGridOverlay(m_roster);
    midSizer->Add(m_roster, 1, wxALL | wxEXPAND, 8);

    mid->SetSizer(midSizer);
    m_midBook->AddPage(m_midRosterPanel, "Roster", true);

    // --- Middle Research page – categorized list with yellow group headers ---
    m_midResearchPanel = new wxPanel(m_midBook);
    m_midResearchPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(m_midResearchPanel, m_spellData, "VMR_FULL.LZ",
        wxRect(kMapChromeW, 0, kStrategicScreenW - kMapChromeW, kStrategicScreenH));
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
        BindListGridOverlay(m_researchList);
        rs->Add(m_researchList, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

        m_midResearchPanel->SetSizer(rs);
    }
    m_midBook->AddPage(m_midResearchPanel, "Research", false);

    // --- Middle Info/Encyclopedia page – categorized list (read-only browsing) ---
    m_midInfoPanel = new wxPanel(m_midBook);
    m_midInfoPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(m_midInfoPanel, m_spellData, "VMI_FULL.LZ",
        wxRect(kMapChromeW, 0, kStrategicScreenW - kMapChromeW, kStrategicScreenH));
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
        BindListGridOverlay(m_infoList);
        is->Add(m_infoList, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

        m_midInfoPanel->SetSizer(is);
    }
    m_midBook->AddPage(m_midInfoPanel, "Info", false);

    // FACTORY.LZ and STATS.LZ are 569 px wide and deliberately cross the
    // left/middle split.  Give both screens a matching middle page so the
    // original artwork remains continuous across x=0..574.
    m_midResourcesPanel = new wxPanel(m_midBook);
    m_midResourcesPanel->SetBackgroundColour(m_palette.background);
    m_midResourcesPanel->SetMinSize(wxSize(1, 1));
    BindStrategicScreenSlice(m_midResourcesPanel, m_spellData, "VMF_FULL.LZ",
        wxRect(kMapChromeW, 0, kStrategicScreenW - kMapChromeW, kStrategicScreenH));
    m_midBook->AddPage(m_midResourcesPanel, "Resources", false);

    m_midStatsPanel = new wxPanel(m_midBook);
    m_midStatsPanel->SetBackgroundColour(m_palette.background);
    m_midStatsPanel->SetMinSize(wxSize(1, 1));
    BindStrategicScreenSlice(m_midStatsPanel, m_spellData, "VMS_FULL.LZ",
        wxRect(kMapChromeW, 0, kStrategicScreenW - kMapChromeW, kStrategicScreenH));
    m_midBook->AddPage(m_midStatsPanel, "Statistics", false);

    mainSizer->Add(m_midBook, kStrategicScreenW - kMapChromeW, wxEXPAND);


    // ============================================================
    // RIGHT: status + actions (always visible, consistent layout)
    // ============================================================
    // The DOS screen is 575 px of content plus a 65 px control rail.
    // Keep that exact 412:163:65 relationship while the window is resized.
    auto* right = new wxPanel(m_normalLayoutPanel);
    right->SetBackgroundColour(m_palette.background);
    right->SetMinSize(wxSize(1, 1));
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

    auto makeBtn = [&](int id, const wxString& label, const wxString& iconName = "") -> wxButton*
        {
            wxButton* btn = CreateStrategicButton(right, id, label,
                m_fontText,
                m_palette.buttonText,
                m_palette.buttonBackground,
                wxSize(110, 44));

            // Try to load icon - if found, hide text (text is fallback only)
            if (!iconName.empty())
            {
                wxBitmap bmp = LoadMenuIcon(m_spellData, iconName, wxSize(32, 32));
                if (bmp.IsOk())
                {
                    btn->SetBitmap(bmp);
                    btn->SetBitmapPosition(wxLEFT);
                    btn->SetLabel("");  // Hide text when icon is available
                }
            }

            return btn;
        };

    // Buttons (consistent order across all pages)

    m_btnStrategicMap = makeBtn(ID_BTN_STRATEGIC_MAP, "Strategic map", "strategic_map");
    m_btnHierarchy = makeBtn(ID_BTN_HIERARCHY, "Hierarchy", "hierarchy");
    m_btnUnitsShop = makeBtn(ID_BTN_UNITS, "Units", "units");
    m_btnBuyShop = makeBtn(ID_BTN_BUY_SHOP, "Buy / Sell", "buy_sell");
    m_btnResearch = makeBtn(ID_BTN_RESEARCH, "Research", "research");
    m_btnInfo = makeBtn(ID_BTN_INFO, "Info", "info");
    m_btnResources = makeBtn(ID_BTN_RESOURCES, "Resources", "resources");
    m_btnStats = makeBtn(ID_BTN_STATS, "Statistics", "statistics");
    m_btnLaunch = makeBtn(ID_BTN_LAUNCH, "Launch mission");  // No icon in original game

    // End Turn button with black background and turn number (no icon in original game)
    // Two-line format: "Turn" + number, hover shows "End" with gray background
    m_btnEndTurn = CreateStrategicButton(right, ID_BTN_ENDTURN,
        wxString::Format("Turn\n%02d", m_turn),
        m_fontText,
        m_palette.buttonText,
        wxColour(0, 0, 0),  // black background
        wxSize(110, 44));

    // Hover effect: gray background, show "End"
    m_btnEndTurn->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& ev) {
        if (m_btnEndTurn) {
            m_btnEndTurn->SetLabel("End");
            m_btnEndTurn->SetBackgroundColour(m_palette.buttonBackground);
            m_btnEndTurn->Refresh();
        }
        ev.Skip();
    });
    m_btnEndTurn->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& ev) {
        if (m_btnEndTurn) {
            m_btnEndTurn->SetLabel(wxString::Format("Turn\n%02d", m_turn));
            m_btnEndTurn->SetBackgroundColour(wxColour(0, 0, 0));
            m_btnEndTurn->Refresh();
        }
        ev.Skip();
    });

    auto* btnSizer = new wxBoxSizer(wxVERTICAL);
    btnSizer->Add(m_btnStrategicMap, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(m_btnHierarchy, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(m_btnUnitsShop, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(m_btnBuyShop, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(m_btnResearch, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(m_btnInfo, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(m_btnResources, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(m_btnStats, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(m_btnLaunch, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(m_btnEndTurn, 0, wxALIGN_CENTER_HORIZONTAL);

    rightSizer->Add(btnSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    right->SetSizer(rightSizer);

    mainSizer->Add(right, 65, wxEXPAND);
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

    // Alternate original/restored UI branch: one self-contained paint surface,
    // completely outside the old strategic page hierarchy.
    m_originalStrategicPanel = new wxPanel(root);
    m_originalStrategicPanel->SetBackgroundColour(*wxBLACK);
    m_originalStrategicPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_originalStrategicPanel->Bind(wxEVT_PAINT, &StrategicLevelFrame::OnOriginalStrategicPaint, this);
    m_originalStrategicPanel->Bind(wxEVT_LEFT_DOWN, &StrategicLevelFrame::OnOriginalStrategicLeftDown, this);
    m_originalStrategicPanel->Bind(wxEVT_RIGHT_DOWN, &StrategicLevelFrame::OnOriginalStrategicRightDown, this);
    m_originalStrategicPanel->Bind(wxEVT_MOUSEWHEEL, &StrategicLevelFrame::OnOriginalStrategicMouseWheel, this);
    m_originalStrategicPanel->Bind(wxEVT_MOTION, &StrategicLevelFrame::OnOriginalStrategicMouseMove, this);
    m_originalStrategicPanel->Bind(wxEVT_LEAVE_WINDOW, &StrategicLevelFrame::OnOriginalStrategicMouseLeave, this);
    m_originalStrategicPanel->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        if (m_originalStrategicPanel) m_originalStrategicPanel->Refresh();
    });
    m_originalStrategicPanel->Show(false);
    rootSizer->Add(m_originalStrategicPanel, 1, wxEXPAND);

    root->SetSizer(rootSizer);
    //použije rekurzivně transparentní pozadí na všechny elementy wx - opatrně!
    //MakeChildrenTransparentRecursive(root);
}

// ============================================================
//  Buy / Sell page
// ============================================================

static constexpr long kBuyHdrSentinel = -1L;
static constexpr wxUIntPtr kBuyCmdBase = static_cast<wxUIntPtr>(0x40000000);

void StrategicLevelFrame::BuildBuyPage()
{
    if (!m_buyMainPanel) return;

    // Original 640 px layout: 332 + 243 px content and a 65 px control rail.
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // ---------------------------------------------------------------------
    // LEFT: Rosters
    // ---------------------------------------------------------------------
    auto* leftPanel = new wxPanel(m_buyMainPanel);
    leftPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(leftPanel, m_spellData, "VMB_FULL.LZ",
        wxRect(0, 0, 332, kStrategicScreenH));
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

        BindListGridOverlay(unitRoster);
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

        BindListGridOverlay(cmdRoster);
        leftSizer->Add(cmdRoster, 1, wxALL | wxEXPAND, 4);
        m_buyCmdRoster = cmdRoster;
    }

    leftPanel->SetSizer(leftSizer);
    // Original VMB layout splits at x=332 inside the 575 px strategic surface.
    mainSizer->Add(leftPanel, 332, wxEXPAND);

    // ---------------------------------------------------------------------
    // MIDDLE: Shop + info + buy/sell action
    // ---------------------------------------------------------------------
    auto* midPanel = new wxPanel(m_buyMainPanel);
    midPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(midPanel, m_spellData, "VMB_FULL.LZ",
        wxRect(332, 0, kStrategicScreenW - 332, kStrategicScreenH));
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
        if (data == static_cast<wxUIntPtr>(kBuyHdrSentinel)) {
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

    BindListGridOverlay(m_buyShopList);
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
    mainSizer->Add(midPanel, kStrategicScreenW - 332, wxEXPAND);

    // ---------------------------------------------------------------------
    // RIGHT: Sidebar (status + all buttons) – no duplicates
    // ---------------------------------------------------------------------
    auto* sidePanel = new wxPanel(m_buyMainPanel);
    sidePanel->SetBackgroundColour(m_palette.background);
    sidePanel->SetMinSize(wxSize(1, 1));
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

    auto makeBtn = [&](const wxString& label, const wxString& iconName = "") -> wxButton*
        {
            wxButton* btn = CreateStrategicButton(sidePanel, wxID_ANY, label,
                m_fontText,
                m_palette.buttonText,
                m_palette.buttonBackground,
                wxSize(110, 44));

            // Try to load icon - if found, hide text (text is fallback only)
            if (!iconName.empty())
            {
                wxBitmap bmp = LoadMenuIcon(m_spellData, iconName, wxSize(32, 32));
                if (bmp.IsOk())
                {
                    btn->SetBitmap(bmp);
                    btn->SetBitmapPosition(wxLEFT);
                    btn->SetLabel("");
                }
            }

            return btn;
        };

    auto* btnSizer = new wxBoxSizer(wxVERTICAL);

    auto* btnStrategicMap = makeBtn("Strategic map", "strategic_map");
    btnStrategicMap->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowStrategicMap(ev); });

    auto* btnHierarchy = makeBtn("Hierarchy", "hierarchy");
    btnHierarchy->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowHierarchy(ev); });

    auto* btnUnits = makeBtn("Units", "units");
    btnUnits->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); EnterUnitsMode(); });

    auto* btnBuySell = makeBtn("Buy / Sell", "buy_sell");
    btnBuySell->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { LeaveBuyMode(); });

    auto* btnResearch = makeBtn("Research", "research");
    btnResearch->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnResearch(ev); });

    auto* btnInfo = makeBtn("Info", "info");
    btnInfo->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowInfo(ev); });

    auto* btnResources = makeBtn("Resources", "resources");
    btnResources->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowResources(ev); });

    auto* btnStats = makeBtn("Statistics", "statistics");
    btnStats->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnShowStats(ev); });

    auto* btnLaunch = makeBtn("Launch mission");
    btnLaunch->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnLaunch(ev); });
    // Launch mission must only be enabled on the Strategic map page.
    btnLaunch->Enable(false);

    // End Turn with black background and turn number (two-line format)
    auto* btnEndTurn = CreateStrategicButton(sidePanel, wxID_ANY,
        wxString::Format("Turn\n%02d", m_turn),
        m_fontText,
        m_palette.buttonText,
        wxColour(0, 0, 0),  // black background
        wxSize(110, 44));
    btnEndTurn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveBuyMode(); OnEndTurn(ev); });
    // Hover effect
    btnEndTurn->Bind(wxEVT_ENTER_WINDOW, [this, btnEndTurn](wxMouseEvent& ev) {
        btnEndTurn->SetLabel("End");
        btnEndTurn->SetBackgroundColour(m_palette.buttonBackground);
        btnEndTurn->Refresh();
        ev.Skip();
    });
    btnEndTurn->Bind(wxEVT_LEAVE_WINDOW, [this, btnEndTurn](wxMouseEvent& ev) {
        btnEndTurn->SetLabel(wxString::Format("Turn\n%02d", m_turn));
        btnEndTurn->SetBackgroundColour(wxColour(0, 0, 0));
        btnEndTurn->Refresh();
        ev.Skip();
    });

    btnSizer->Add(btnStrategicMap, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnHierarchy, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnUnits, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnBuySell, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(btnResearch, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnInfo, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnResources, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnStats, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(btnLaunch, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(btnEndTurn, 0, wxALIGN_CENTER_HORIZONTAL);

    sideSizer->Add(btnSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    sidePanel->SetSizer(sideSizer);
    mainSizer->Add(sidePanel, 65, wxEXPAND);

    m_buyMainPanel->SetSizer(mainSizer);
}

void StrategicLevelFrame::RefreshBuyInfo(long data)
{
    if (!m_buyTimeLabel || !m_buyCostLabel || !m_btnBuyAction) return;
    m_btnBuyAction->Enable(false);

    if (data < 0 || data == (long)kBuyHdrSentinel) {
        m_buyTimeLabel->SetLabel("Time: -");
        m_buyCostLabel->SetLabel("Cost: -");
        return;
    }

    if (!m_buyTabSell)
    {
        // Buy mode
        if ((wxUIntPtr)data >= kBuyCmdBase) {
            // Commander
            m_buyTimeLabel->SetLabel("Time: 1");
            m_buyCostLabel->SetLabel("Cost: 0");
            m_btnBuyAction->SetLabel("Buy");
            m_btnBuyAction->Enable((int)m_playerCommanders.size() < 14);
        }
        else {
            // Unit
            const int tid = (int)data;
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
        if (idx >= 0 && idx < (int)m_playerUnits.size()) {
            const auto& u = m_playerUnits[idx];
            const int cost = GetUnitBuyCost(u.unit_id);
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
    for (const auto& u : m_playerUnits) {
        const long idx = m_buyUnitRoster->GetItemCount();
        m_buyUnitRoster->InsertItem(idx, GetUnitDisplayName(u.unit_id));
        m_buyUnitRoster->SetItem(idx, 1, wxString::Format("%d%%", u.health));
        if (u.health < 100)
            m_buyUnitRoster->SetItemTextColour(idx, wxColour(0xFF, 0xA0, 0x00));
    }

    // Refresh commander roster
    m_buyCmdRoster->DeleteAllItems();
    for (const auto& c : m_playerCommanders) {
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
    EnsureResearchLoaded();
    EnsureUnitCostsLoaded();
    if (!m_buyShopList) return;

    m_buyShopList->Freeze();
    m_buyShopList->DeleteAllItems();

    const wxColour clrHdr = m_palette.heading;
    const wxColour clrNorm = m_palette.text;
    const wxColour clrGrey(0x60, 0x60, 0x60);
    long row = 0;

    auto addHdr = [&](const wxString& lbl) {
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
            const std::vector<std::string> catOrder = { "Infantry","Artillery","Transporters","Aerial guns","Other" };
            std::map<std::string, std::vector<UE>> byCat;

            for (const auto* unit : m_spellData->units->GetUnits())
            {
                if (!unit) continue;
                const int cost = GetUnitBuyCost(unit->type_id);
                if (cost <= 0) continue;

                // Game mode filter
                if (m_gameModeEnabled && !IsCampaignUnitUnlocked(unit->type_id))
                    continue;

                std::string cat = "Other";
                auto it = m_unitCategories.find(unit->type_id);
                if (it != m_unitCategories.end() && !it->second.empty())
                    cat = it->second;
                byCat[cat].push_back({ unit->type_id, wxString(char2wstringCP895(unit->name)), cost });
            }

            for (const auto& catName : catOrder) {
                auto it = byCat.find(catName);
                if (it == byCat.end() || it->second.empty()) continue;
                addHdr(wxString::FromUTF8(catName));
                for (const auto& ue : it->second) {
                    m_buyShopList->InsertItem(row, wxString("  ") + ue.name);
                    m_buyShopList->SetItemData(row, static_cast<wxUIntPtr>(ue.type_id));
                    m_buyShopList->SetItemTextColour(row, m_money >= ue.cost ? clrNorm : clrGrey);
                    ++row;
                }
            }
        }

        // Commanders
        if (!m_availableCommanders.empty()) {
            addHdr("Commanders");
            for (int ci = 0; ci < (int)m_availableCommanders.size(); ++ci) {
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
        if (!m_playerUnits.empty()) {
            struct SE { int idx; wxString name; int refund; };
            const std::vector<std::string> catOrder = { "Infantry","Artillery","Transporters","Aerial guns","Other" };
            std::map<std::string, std::vector<SE>> byCat;

            for (int i = 0; i < (int)m_playerUnits.size(); ++i) {
                const auto& u = m_playerUnits[i];
                const int cost = GetUnitBuyCost(u.unit_id);
                std::string cat = "Other";
                auto it = m_unitCategories.find(u.unit_id);
                if (it != m_unitCategories.end() && !it->second.empty())
                    cat = it->second;
                byCat[cat].push_back({ i, GetUnitDisplayName(u.unit_id), cost > 0 ? cost / 2 : 0 });
            }

            for (const auto& catName : catOrder) {
                auto it = byCat.find(catName);
                if (it == byCat.end() || it->second.empty()) continue;
                addHdr(wxString::FromUTF8(catName));
                for (const auto& se : it->second) {
                    wxString lbl = wxString("  ") + se.name;
                    if (se.refund > 0) lbl += wxString::Format(" (%d)", se.refund);
                    m_buyShopList->InsertItem(row, lbl);
                    m_buyShopList->SetItemData(row, static_cast<wxUIntPtr>(se.idx));
                    m_buyShopList->SetItemTextColour(row, clrNorm);
                    ++row;
                }
            }
        }
        else {
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
        if (m_buyMainPanel)     sz->Show(m_buyMainPanel, show, true);
        if (m_unitsMainPanel)   sz->Show(m_unitsMainPanel, false, true);
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
    else {
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
    if (data == static_cast<wxUIntPtr>(kBuyHdrSentinel)) return;

    EnsureUnitCostsLoaded();

    if (!m_buyTabSell)
    {
        // BUY
        if (data >= kBuyCmdBase) {
            // Commander
            const int ci = (int)(data - kBuyCmdBase);
            if (ci >= (int)m_availableCommanders.size()) return;
            if ((int)m_playerCommanders.size() >= 14) {
                wxMessageBox("Commander limit reached (14).", "Buy", wxOK | wxICON_INFORMATION, this);
                return;
            }
            m_playerCommanders.push_back(m_availableCommanders[(size_t)ci]);
            m_availableCommanders.clear();
        }
        else {
            // Unit
            const int tid = (int)data;
            const int cost = GetUnitBuyCost(tid);
            if (cost <= 0) { wxMessageBox("No price defined.", "Buy", wxOK | wxICON_WARNING, this); return; }
            if (m_money < cost) {
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
        const auto& u = m_playerUnits[idx];
        const int cost = GetUnitBuyCost(u.unit_id);
        if (cost <= 0) { wxMessageBox("No price defined.", "Sell", wxOK | wxICON_WARNING, this); return; }
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

    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // ---------------------------------------------------------------------
    // LEFT: Player units (permanent + temporary lists)
    // ---------------------------------------------------------------------
    auto* leftPanel = new wxPanel(m_unitsMainPanel);
    leftPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(leftPanel, m_spellData, "VMU_FULL.LZ",
        wxRect(0, 0, 332, kStrategicScreenH));
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);

    m_unitsRoster = new wxListCtrl(leftPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL);
    m_unitsRoster->SetFont(m_fontText);
    m_unitsRoster->SetBackgroundColour(m_palette.background);
    m_unitsRoster->SetForegroundColour(m_palette.text);

    // Spellcross-like roster: Unit + Level + Status (NO HP column here; details are in the info panel).
    m_unitsRoster->InsertColumn(0, "Unit");
    m_unitsRoster->InsertColumn(1, "Lvl");
    m_unitsRoster->InsertColumn(2, "Status");

    m_unitsRoster->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& ev) {
        // ItemData stores the original m_playerUnits index (not the expanded row index)
        m_unitsSelectedUnit = (int)m_unitsRoster->GetItemData(ev.GetIndex());
        m_unitsSelectedRearmUnitId = -1;

        // Default selection for recruit quality.
        if (m_unitsCurrentTab == UNITS_TAB_RECRUIT) m_unitsSelectedUpgrade = 1;
        else m_unitsSelectedUpgrade = -1;

        RefreshUnitsShopList();
        RefreshUnitsInfo(m_unitsSelectedUnit);
        RefreshUnitsActionButton();
        });

    BindListGridOverlay(m_unitsRoster);
    leftSizer->Add(m_unitsRoster, 1, wxALL | wxEXPAND, 8);

    // Temporary units list (same width, slightly lower) - will be filled once temporary units exist in data.
    m_unitsTempRoster = new wxListCtrl(leftPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL);
    m_unitsTempRoster->SetFont(m_fontText);
    m_unitsTempRoster->SetBackgroundColour(m_palette.background);
    m_unitsTempRoster->SetForegroundColour(m_palette.text);

    m_unitsTempRoster->InsertColumn(0, "Temporary");
    m_unitsTempRoster->InsertColumn(1, "Lvl");
    m_unitsTempRoster->InsertColumn(2, "Status");

    // Keep selection logic simple for now (temporary units are not yet implemented).
    m_unitsTempRoster->Enable(false);

    BindListGridOverlay(m_unitsTempRoster);
    leftSizer->Add(m_unitsTempRoster, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    leftPanel->SetSizer(leftSizer);
    mainSizer->Add(leftPanel, 332, wxEXPAND);

    // ---------------------------------------------------------------------
    // MIDDLE: Mode selector (Upgrade / Recruit / Info)
    // ---------------------------------------------------------------------
    auto* modePanel = new wxPanel(m_unitsMainPanel);
    modePanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(modePanel, m_spellData, "VMU_FULL.LZ",
        wxRect(332, 0, 80, kStrategicScreenH));

    auto* modeSizer = new wxBoxSizer(wxVERTICAL);

    auto* modeTitle = new wxStaticText(modePanel, wxID_ANY, "Mode");
    modeTitle->SetFont(m_fontHeading);
    modeTitle->SetForegroundColour(m_palette.heading);
    modeSizer->Add(modeTitle, 0, wxTOP | wxLEFT | wxRIGHT, 14);

    auto makeModeBtn = [&](const wxString& label, UnitsTab tab) -> wxButton*
        {
            auto* b = new wxButton(modePanel, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            b->SetFont(m_fontText);
            b->SetForegroundColour(m_palette.buttonText);
            b->SetBackgroundColour(m_palette.buttonBackground);
            b->Bind(wxEVT_BUTTON, [this, tab](wxCommandEvent&) { OnUnitsTabChange((int)tab); });
            return b;
        };

    m_btnUnitsTabUpgrade = makeModeBtn("Upgrade", UNITS_TAB_UPGRADE);
    m_btnUnitsTabRecruit = makeModeBtn("Recruit", UNITS_TAB_RECRUIT);
    m_btnUnitsTabInfo = makeModeBtn("Info", UNITS_TAB_INFO);
    m_btnUnitsTabDisband = nullptr; // Disband is a dedicated button in the bottom bar.

    modeSizer->Add(m_btnUnitsTabUpgrade, 0, wxALL | wxEXPAND, 10);
    modeSizer->Add(m_btnUnitsTabRecruit, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
    modeSizer->Add(m_btnUnitsTabInfo, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    modeSizer->AddStretchSpacer(1);
    modePanel->SetSizer(modeSizer);
    mainSizer->Add(modePanel, 80, wxEXPAND);

    // ---------------------------------------------------------------------
    // CENTER: Upgrade panel (always visible) + Unit info panel + bottom controls
    // ---------------------------------------------------------------------
    auto* centerPanel = new wxPanel(m_unitsMainPanel);
    centerPanel->SetBackgroundColour(m_palette.background);
    BindStrategicScreenSlice(centerPanel, m_spellData, "VMU_FULL.LZ",
        wxRect(412, 0, kStrategicScreenW - 412, kStrategicScreenH));
    auto* centerSizer = new wxBoxSizer(wxVERTICAL);

    // ── Upgrade panel (top) ───────────────────────────────────────────────
    auto* upgradePanel = new wxPanel(centerPanel);
    upgradePanel->SetBackgroundColour(m_palette.background);
    auto* upgradeSizer = new wxBoxSizer(wxVERTICAL);

    // Top: upgrade list with title/value (Armour/Weapon/Engine style)
    auto* upgTop = new wxBoxSizer(wxVERTICAL);
    m_unitsUpgradeTitle = new wxStaticText(upgradePanel, wxID_ANY, "Upgrade");
    m_unitsUpgradeTitle->SetFont(m_fontHeading);
    m_unitsUpgradeTitle->SetForegroundColour(m_palette.heading);

    m_unitsUpgradeValue = new wxStaticText(upgradePanel, wxID_ANY, "--default--");
    m_unitsUpgradeValue->SetFont(m_fontText);
    m_unitsUpgradeValue->SetForegroundColour(m_palette.text);

    upgTop->Add(m_unitsUpgradeTitle, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    upgTop->Add(m_unitsUpgradeValue, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_unitsShopList = new wxListCtrl(upgradePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL);
    m_unitsShopList->SetFont(m_fontText);
    m_unitsShopList->SetBackgroundColour(m_palette.background);
    m_unitsShopList->SetForegroundColour(m_palette.text);
    m_unitsShopList->InsertColumn(0, "", wxLIST_FORMAT_LEFT, -1);

    m_unitsShopList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& ev) {
        if (m_unitsCurrentTab == UNITS_TAB_RECRUIT)
        {
            m_unitsSelectedUpgrade = (int)ev.GetIndex(); // 0..2 quality
        }
        else if (m_unitsCurrentTab == UNITS_TAB_UPGRADE)
        {
            long idx = ev.GetIndex();
            m_unitsSelectedUpgrade = (int)m_unitsShopList->GetItemData(idx); // upgradeId
        }

        // Selecting an upgrade clears re-arm selection.
        m_unitsSelectedRearmUnitId = -1;
        if (m_unitsRearmList) m_unitsRearmList->DeselectAll();

        RefreshUnitsInfo(m_unitsSelectedUnit);
        RefreshUnitsActionButton();
        });

    m_unitsShopList->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        if (!m_unitsShopList) return;
        const int w = m_unitsShopList->GetClientSize().GetWidth();
        if (w > 0) m_unitsShopList->SetColumnWidth(0, w);
        });

    BindListGridOverlay(m_unitsShopList);
    upgTop->Add(m_unitsShopList, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    // Bottom: unit types in same category (re-arm)
    auto* upgBottom = new wxBoxSizer(wxVERTICAL);
    m_unitsRearmTitle = new wxStaticText(upgradePanel, wxID_ANY, "Category");
    m_unitsRearmTitle->SetFont(m_fontHeading);
    m_unitsRearmTitle->SetForegroundColour(m_palette.heading);
    upgBottom->Add(m_unitsRearmTitle, 0, wxLEFT | wxRIGHT | wxTOP, 8);

    m_unitsRearmList = new wxListBox(upgradePanel, wxID_ANY);
    m_unitsRearmList->SetFont(m_fontText);
    m_unitsRearmList->SetBackgroundColour(m_palette.background);
    m_unitsRearmList->SetForegroundColour(m_palette.text);
    m_unitsRearmList->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& ev) {
        int sel = ev.GetSelection();
        if (sel >= 0)
        {
            m_unitsSelectedRearmUnitId = (int)reinterpret_cast<intptr_t>(m_unitsRearmList->GetClientData(sel));
            // Selecting re-arm clears tech upgrade selection.
            m_unitsSelectedUpgrade = -1;
        }
        RefreshUnitsInfo(m_unitsSelectedUnit);
        RefreshUnitsActionButton();
        });
    upgBottom->Add(m_unitsRearmList, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    upgradeSizer->Add(upgTop, 3, wxEXPAND);
    upgradeSizer->Add(upgBottom, 2, wxEXPAND);

    upgradePanel->SetSizer(upgradeSizer);
    centerSizer->Add(upgradePanel, 3, wxALL | wxEXPAND, 0);

    // ── Unit info panel (bottom) ─────────────────────────────────────────
    auto* infoPanel = new wxPanel(centerPanel);
    infoPanel->SetBackgroundColour(m_palette.background);
    auto* infoSizer = new wxBoxSizer(wxHORIZONTAL);

    m_unitsInfoText = new wxTextCtrl(infoPanel, wxID_ANY, "",
        wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
    m_unitsInfoText->SetFont(m_fontText);
    m_unitsInfoText->SetBackgroundColour(m_palette.background);
    m_unitsInfoText->SetForegroundColour(m_palette.text);

    infoSizer->Add(m_unitsInfoText, 1, wxALL | wxEXPAND, 8);

    // Unit icon on the right (Spellcross-style)
    m_unitsIconCanvas = new wxPanel(infoPanel, wxID_ANY, wxDefaultPosition, wxSize(140, -1));
    m_unitsIconCanvas->SetBackgroundColour(m_palette.background);
    m_unitsIconCanvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_unitsIconCanvas->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(m_unitsIconCanvas);
        dc.SetBackground(wxBrush(m_palette.background));
        dc.Clear();

        if (m_unitsSelectedUnit < 0 || m_unitsSelectedUnit >= (int)m_playerUnits.size())
            return;
        if (!m_spellData || !m_spellData->units) return;

        const auto& u = m_playerUnits[m_unitsSelectedUnit];
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

    infoSizer->Add(m_unitsIconCanvas, 0, wxTOP | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    infoPanel->SetSizer(infoSizer);
    centerSizer->Add(infoPanel, 1, wxEXPAND);

    // ── Bottom controls: Disband / Time+Cost / OK ────────────────────────
    auto* bottomRow = new wxBoxSizer(wxHORIZONTAL);

    m_btnUnitsDisband = new wxButton(centerPanel, wxID_ANY, "Disband");
    m_btnUnitsDisband->SetFont(m_fontText);
    m_btnUnitsDisband->SetForegroundColour(m_palette.buttonText);
    m_btnUnitsDisband->SetBackgroundColour(m_palette.buttonBackground);
    m_btnUnitsDisband->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnUnitsDisband, this);

    bottomRow->Add(m_btnUnitsDisband, 0, wxALL, 8);

    auto* timeCostCol = new wxBoxSizer(wxVERTICAL);
    m_unitsTimeLabel = new wxStaticText(centerPanel, wxID_ANY, "Time: -");
    m_unitsTimeLabel->SetFont(m_fontText);
    m_unitsTimeLabel->SetForegroundColour(m_palette.text);
    m_unitsCostLabel = new wxStaticText(centerPanel, wxID_ANY, "Cost: -");
    m_unitsCostLabel->SetFont(m_fontText);
    m_unitsCostLabel->SetForegroundColour(m_palette.heading);

    timeCostCol->Add(m_unitsTimeLabel, 0, wxBOTTOM, 2);
    timeCostCol->Add(m_unitsCostLabel, 0);

    bottomRow->Add(timeCostCol, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 8);

    m_btnUnitsAction = new wxButton(centerPanel, ID_BTN_UNITS_ACTION, "OK");
    m_btnUnitsAction->SetFont(m_fontText);
    m_btnUnitsAction->SetForegroundColour(m_palette.buttonText);
    m_btnUnitsAction->SetBackgroundColour(m_palette.buttonBackground);
    m_btnUnitsAction->Enable(false);
    m_btnUnitsAction->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnUnitsAction, this);

    bottomRow->Add(m_btnUnitsAction, 0, wxALL, 8);

    centerSizer->Add(bottomRow, 0, wxEXPAND);

    centerPanel->SetSizer(centerSizer);
    mainSizer->Add(centerPanel, kStrategicScreenW - 412, wxEXPAND);

    // ---------------------------------------------------------------------
    // FAR RIGHT: Sidebar (status + navigation buttons)
    // ---------------------------------------------------------------------
    auto* sidePanel = new wxPanel(m_unitsMainPanel);
    sidePanel->SetBackgroundColour(m_palette.background);
    sidePanel->SetMinSize(wxSize(1, 1));
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

    auto makeBtn = [&](const wxString& label, const wxString& iconName = "") -> wxButton*
        {
            wxButton* btn = CreateStrategicButton(sidePanel, wxID_ANY, label,
                m_fontText,
                m_palette.buttonText,
                m_palette.buttonBackground,
                wxSize(110, 44));

            // Try to load icon - if found, hide text (text is fallback only)
            if (!iconName.empty())
            {
                wxBitmap bmp = LoadMenuIcon(m_spellData, iconName, wxSize(32, 32));
                if (bmp.IsOk())
                {
                    btn->SetBitmap(bmp);
                    btn->SetBitmapPosition(wxLEFT);
                    btn->SetLabel("");
                }
            }

            return btn;
        };

    auto* btnSizer = new wxBoxSizer(wxVERTICAL);

    auto* btnStrategicMap = makeBtn("Strategic map", "strategic_map");
    btnStrategicMap->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowStrategicMap(ev); });

    auto* btnHierarchy = makeBtn("Hierarchy", "hierarchy");
    btnHierarchy->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowHierarchy(ev); });

    auto* btnUnits = makeBtn("Units", "units");
    btnUnits->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { LeaveUnitsMode(); });

    auto* btnBuySell = makeBtn("Buy / Sell", "buy_sell");
    btnBuySell->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { LeaveUnitsMode(); EnterBuyMode(); });

    auto* btnResearch = makeBtn("Research", "research");
    btnResearch->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnResearch(ev); });

    auto* btnInfo = makeBtn("Info", "info");
    btnInfo->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowInfo(ev); });

    auto* btnResources = makeBtn("Resources", "resources");
    btnResources->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowResources(ev); });

    auto* btnStats = makeBtn("Statistics", "statistics");
    btnStats->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnShowStats(ev); });

    auto* btnLaunch = makeBtn("Launch mission");
    btnLaunch->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnLaunch(ev); });
    btnLaunch->Enable(false);

    // End Turn with black background and turn number (two-line format)
    auto* btnEndTurn = CreateStrategicButton(sidePanel, wxID_ANY,
        wxString::Format("Turn\n%02d", m_turn),
        m_fontText,
        m_palette.buttonText,
        wxColour(0, 0, 0),  // black background
        wxSize(110, 44));
    btnEndTurn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) { LeaveUnitsMode(); OnEndTurn(ev); });
    // Hover effect
    btnEndTurn->Bind(wxEVT_ENTER_WINDOW, [this, btnEndTurn](wxMouseEvent& ev) {
        btnEndTurn->SetLabel("End");
        btnEndTurn->SetBackgroundColour(m_palette.buttonBackground);
        btnEndTurn->Refresh();
        ev.Skip();
    });
    btnEndTurn->Bind(wxEVT_LEAVE_WINDOW, [this, btnEndTurn](wxMouseEvent& ev) {
        btnEndTurn->SetLabel(wxString::Format("Turn\n%02d", m_turn));
        btnEndTurn->SetBackgroundColour(wxColour(0, 0, 0));
        btnEndTurn->Refresh();
        ev.Skip();
    });

    btnSizer->Add(btnStrategicMap, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnHierarchy, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnUnits, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnBuySell, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(btnResearch, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnInfo, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnResources, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 4);
    btnSizer->Add(btnStats, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(btnLaunch, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);
    btnSizer->Add(btnEndTurn, 0, wxALIGN_CENTER_HORIZONTAL);

    sideSizer->Add(btnSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    sidePanel->SetSizer(sideSizer);
    mainSizer->Add(sidePanel, 65, wxEXPAND);

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
        if (m_buyMainPanel)      sz->Show(m_buyMainPanel, false, true);
        if (m_unitsMainPanel)    sz->Show(m_unitsMainPanel, show, true);
        root->Layout();
    }
    else {
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

    // Expand units into roster rows by count (same logic as RefreshUI() for the main roster).
    // Each row stores the original m_playerUnits index in ItemData for selection handling.
    long row = 0;
    for (size_t pIdx = 0; pIdx < m_playerUnits.size(); pIdx++)
    {
        const auto& u = m_playerUnits[pIdx];

        for (int inst = 0; inst < u.count; ++inst)
        {
            wxString name = GetUnitDisplayName(u.unit_id);

            // Custom name if set (shared across instances of the same stack)
            if (pIdx < m_unitStates.size() && !m_unitStates[pIdx].custom_name.empty())
                name = wxString::FromUTF8(m_unitStates[pIdx].custom_name);

            long idx = m_unitsRoster->InsertItem(row, name);

            int lvl = (pIdx < m_unitStates.size()) ? m_unitStates[pIdx].level : 0;
            if (lvl == 0 && pIdx < m_unitStates.size())
                lvl = m_unitStates[pIdx].experience / 100;
            m_unitsRoster->SetItem(idx, 1, wxString::Format("%d", lvl));

            int cooldown = (pIdx < m_unitStates.size()) ? m_unitStates[pIdx].cooldown_turns : 0;
            wxString status;
            if (cooldown > 0)
                status = wxString::Format("-%dT", cooldown);
            else
                status = "Ready";
            if (u.health < 100)
                status += wxString::Format(" %d%%", u.health);
            m_unitsRoster->SetItem(idx, 2, status);

            // Color damaged units so they are easy to spot
            if (u.health < 100)
                m_unitsRoster->SetItemTextColour(idx, wxColour(0xFF, 0xA0, 0x00));

            // Store the original playerUnits index for selection handling
            m_unitsRoster->SetItemData(idx, (long)pIdx);
            ++row;
        }
    }

    for (int c = 0; c < 3; c++)
        m_unitsRoster->SetColumnWidth(c, wxLIST_AUTOSIZE_USEHEADER);

    m_unitsRoster->Thaw();

    // Temporary units list: keep empty until temporary units exist in data.
    if (m_unitsTempRoster)
    {
        m_unitsTempRoster->Freeze();
        m_unitsTempRoster->DeleteAllItems();
        for (int c = 0; c < 3; c++)
            m_unitsTempRoster->SetColumnWidth(c, wxLIST_AUTOSIZE_USEHEADER);
        m_unitsTempRoster->Thaw();
    }
}


void StrategicLevelFrame::RefreshUnitsShopList()
{
    if (!m_unitsShopList) return;

    // Ensure research database is loaded before checking for upgrades
    EnsureResearchLoaded();

    m_unitsShopList->Freeze();
    m_unitsShopList->DeleteAllItems();

    // Upgrade panel widgets are always present, but re-arm list is only used in Upgrade mode.
    if (m_unitsRearmList)
        m_unitsRearmList->Show(m_unitsCurrentTab == UNITS_TAB_UPGRADE);

    if (m_unitsRearmTitle)
        m_unitsRearmTitle->Show(m_unitsCurrentTab == UNITS_TAB_UPGRADE);

    // Default selection (used for Recruit mode).
    if (m_unitsCurrentTab == UNITS_TAB_RECRUIT && m_unitsSelectedUpgrade < 0)
        m_unitsSelectedUpgrade = 1;

    // Update right-side title (category) for Upgrade mode.
    if (m_unitsRearmTitle && m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
        m_unitsRearmTitle->SetLabel(GetUnitCategoryName(m_playerUnits[m_unitsSelectedUnit].unit_id));

    // Clear re-arm list by default.
    if (m_unitsRearmList)
    {
        m_unitsRearmList->Clear();
        m_unitsSelectedRearmUnitId = -1;
    }

    switch (m_unitsCurrentTab)
    {
    case UNITS_TAB_RECRUIT:
        if (m_unitsUpgradeTitle) m_unitsUpgradeTitle->SetLabel("Recruit");
        if (m_unitsUpgradeValue) m_unitsUpgradeValue->SetLabel("");

        for (int q = 0; q < RECRUIT_QUALITY_COUNT; q++)
        {
            long idx = m_unitsShopList->InsertItem(q, RECRUIT_QUALITY_NAMES[q]);
            m_unitsShopList->SetItemData(idx, q);
        }

        if (m_unitsSelectedUpgrade >= 0 && m_unitsSelectedUpgrade < RECRUIT_QUALITY_COUNT)
        {
            m_unitsShopList->SetItemState(m_unitsSelectedUpgrade,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            m_unitsShopList->EnsureVisible(m_unitsSelectedUpgrade);
        }
        break;

    case UNITS_TAB_UPGRADE:
    {
        if (m_unitsUpgradeTitle) m_unitsUpgradeTitle->SetLabel("Upgrade");
        if (m_unitsUpgradeValue) m_unitsUpgradeValue->SetLabel("--default--");

        // Right list: re-arm targets
        if (m_unitsRearmList && m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
        {
            const auto& u = m_playerUnits[m_unitsSelectedUnit];
            auto targets = GetAvailableUnitTypesForUpgrade(u.unit_id);
            for (int toId : targets)
            {
                m_unitsRearmList->Append(GetUnitDisplayName(toId), reinterpret_cast<void*>(static_cast<intptr_t>(toId)));
            }
            if (m_unitsRearmList->GetCount() > 0)
                m_unitsRearmList->SetSelection(0);
            if (m_unitsRearmList->GetSelection() >= 0)
                m_unitsSelectedRearmUnitId = (int)reinterpret_cast<intptr_t>(m_unitsRearmList->GetClientData(m_unitsRearmList->GetSelection()));
        }

        // Left list: tech upgrades (research-based)
        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
        {
            const auto& u = m_playerUnits[m_unitsSelectedUnit];
            auto itemUpgrades = GetAvailableUpgradesForUnit(u.unit_id);

            if (itemUpgrades.empty())
            {
                m_unitsShopList->InsertItem(0, "No researched upgrades available");
                m_unitsSelectedUpgrade = -1;
            }
            else
            {
                for (size_t i = 0; i < itemUpgrades.size(); i++)
                {
                    int upgId = itemUpgrades[i];
                    wxString upgName = wxString::Format("Upgrade #%d", upgId);
                    const auto defIt = m_upgradeDefs.find(upgId);
                    if (defIt != m_upgradeDefs.end() && !defIt->second.title.empty())
                        upgName = defIt->second.title;
                    else
                        for (const auto& r : m_researchDb)
                            if (r.data == upgId) { upgName = r.title; break; }
                    long idx = m_unitsShopList->InsertItem((long)i, upgName);
                    m_unitsShopList->SetItemData(idx, (long)upgId);
                }

                // If a tech upgrade is selected, reflect it in the header.
                if (m_unitsSelectedUpgrade >= 0 && m_unitsUpgradeValue)
                {
                    const auto defIt = m_upgradeDefs.find(m_unitsSelectedUpgrade);
                    if (defIt != m_upgradeDefs.end() && !defIt->second.title.empty())
                        m_unitsUpgradeValue->SetLabel(defIt->second.title);
                    else
                        for (const auto& r : m_researchDb)
                            if (r.data == m_unitsSelectedUpgrade) { m_unitsUpgradeValue->SetLabel(r.title); break; }
                }
            }
        }
    }
    break;

    case UNITS_TAB_INFO:
        if (m_unitsUpgradeTitle) m_unitsUpgradeTitle->SetLabel("Info");
        if (m_unitsUpgradeValue) m_unitsUpgradeValue->SetLabel("");

        if (m_unitsSelectedUnit >= 0 && m_unitsSelectedUnit < (int)m_playerUnits.size())
        {
            const auto& u = m_playerUnits[m_unitsSelectedUnit];
            if (m_spellData && m_spellData->units)
            {
                auto* unitRec = m_spellData->units->GetUnit(u.unit_id);
                if (unitRec)
                {
                    m_unitsShopList->InsertItem(0, wxString::Format("Strength: %d%%", u.health));
                    m_unitsShopList->InsertItem(1, wxString::Format("Sight: %d", unitRec->sdir));
                    m_unitsShopList->InsertItem(2, wxString::Format("To move: %d", unitRec->apw));
                    m_unitsShopList->InsertItem(3, wxString::Format("Defence: %d", unitRec->defence));
                    m_unitsShopList->InsertItem(4, wxString::Format("ATK light: %d", unitRec->attack_light));
                    m_unitsShopList->InsertItem(5, wxString::Format("ATK heavy: %d", unitRec->attack_armored));
                    m_unitsShopList->InsertItem(6, wxString::Format("ATK air: %d", unitRec->attack_air));
                }
            }
        }
        break;

    default:
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
            int q = (m_unitsSelectedUpgrade >= 0 && m_unitsSelectedUpgrade < RECRUIT_QUALITY_COUNT) ? m_unitsSelectedUpgrade : 1;
            int cost = GetRecruitCost(unitIndex, q);
            int time = GetRecruitTime(q);
            info += wxString::Format("\n%s\nCost: %d\nTime: %d turns\n", RECRUIT_QUALITY_NAMES[q], cost, time);
        }
        else
        {
            info += "\nUnit is at full strength.\n";
        }
        break;

    case UNITS_TAB_UPGRADE:
        info += "\nUpgrade:\n";
        info += " - Select a researched upgrade in the list (Engine / Weapon / Armour style)\n";
        info += " - Or select a unit type in the category list to re-arm.\n";

        // Reflect current selections into the upgrade header.
        if (m_unitsUpgradeValue)
        {
            wxString upgLabel = "--default--";
            const bool restoredUnits = m_originalStrategicUi &&
                m_originalStrategicScreen == OriginalStrategicScreen::Units;
            if (m_unitsSelectedUpgrade > 0 &&
                ((m_unitsShopList && m_unitsShopList->GetSelectedItemCount() > 0) || restoredUnits))
            {
                for (const auto& r : m_researchDb) {
                    if (r.id == m_unitsSelectedUpgrade) { upgLabel = r.title; break; }
                }
            }
            m_unitsUpgradeValue->SetLabel(upgLabel);
        }

        if (m_unitsSelectedRearmUnitId > 0)
        {
            int cost = GetUpgradeCost(u.unit_id, m_unitsSelectedRearmUnitId);
            int time = GetUpgradeTime(m_unitsSelectedRearmUnitId);
            info += wxString::Format("\nRe-arm to: %s\nCost: %d\nTime: %d turns\n",
                GetUnitDisplayName(m_unitsSelectedRearmUnitId), cost, time);
        }
        break;

    case UNITS_TAB_INFO:
        // Load unit description from INFO files (if available)
        if (m_spellData && m_spellData->units && m_spellData->info)
        {
            auto* unitRec = m_spellData->units->GetUnit(u.unit_id);
            if (unitRec)
            {
                auto artList = unitRec->GetArtList(m_spellData->info);
                if (!artList.empty())
                {
                    const std::vector<std::string> langs = { "CZ", "ENG" };
                    for (const auto& lang : langs)
                    {
                        std::string artInfoName = artList[0] + "." + lang;
                        uint8_t* txtBuf = nullptr;
                        int txtSize = 0;
                        if (!m_spellData->info->GetFile(artInfoName.c_str(), &txtBuf, &txtSize) && txtBuf && txtSize > 0)
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

    default:
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

    // Ensure unit costs and upgrade defs are loaded before calculating prices
    EnsureUnitCostsLoaded();
    EnsureUpgradeDefsLoaded();

    m_btnUnitsAction->Enable(false);
    m_btnUnitsAction->SetLabel("OK");

    if (m_unitsSelectedUnit < 0 || m_unitsSelectedUnit >= (int)m_playerUnits.size())
    {
        if (m_unitsTimeLabel) m_unitsTimeLabel->SetLabel("Time: -");
        if (m_unitsCostLabel) m_unitsCostLabel->SetLabel("Cost: -");
        if (m_btnUnitsDisband) m_btnUnitsDisband->Enable(false);
        return;
    }

    const auto& u = m_playerUnits[m_unitsSelectedUnit];

    // cooldown
    const bool hasCooldown = (m_unitsSelectedUnit < (int)m_unitStates.size() &&
        m_unitStates[m_unitsSelectedUnit].cooldown_turns > 0);

    if (m_btnUnitsDisband)
        m_btnUnitsDisband->Enable(!hasCooldown);

    switch (m_unitsCurrentTab)
    {
    case UNITS_TAB_RECRUIT:
        if (u.health >= 100 || hasCooldown)
        {
            m_btnUnitsAction->Enable(false);
            if (m_unitsTimeLabel) m_unitsTimeLabel->SetLabel("Time: -");
            if (m_unitsCostLabel) m_unitsCostLabel->SetLabel("Cost: -");
        }
        else
        {
            int q = (m_unitsSelectedUpgrade >= 0 && m_unitsSelectedUpgrade < RECRUIT_QUALITY_COUNT) ? m_unitsSelectedUpgrade : 1;
            int cost = GetRecruitCost(m_unitsSelectedUnit, q);
            int time = GetRecruitTime(q);

            m_btnUnitsAction->Enable(cost > 0 && m_money >= cost);
            if (m_unitsTimeLabel) m_unitsTimeLabel->SetLabel(wxString::Format("Time: %d", time));
            if (m_unitsCostLabel) m_unitsCostLabel->SetLabel(wxString::Format("Cost: %d", cost));
        }
        break;

    case UNITS_TAB_UPGRADE:
        if (hasCooldown)
        {
            m_btnUnitsAction->Enable(false);
            if (m_unitsTimeLabel) m_unitsTimeLabel->SetLabel("Time: -");
            if (m_unitsCostLabel) m_unitsCostLabel->SetLabel("Cost: -");
        }
        else
        {
            // Two possible actions:
            // 1) apply selected tech upgrade (m_unitsSelectedUpgrade holds upgradeId)
            // 2) re-arm to selected unit type from m_unitsUpgradeChoice
            bool can = false;
            int cost = 0;
            int time = 0;

            // Tech upgrade takes precedence if user selected one.
            const bool restoredUnits = m_originalStrategicUi &&
                m_originalStrategicScreen == OriginalStrategicScreen::Units;
            if (m_unitsSelectedUpgrade > 0 &&
                ((m_unitsShopList && m_unitsShopList->GetSelectedItemCount() > 0) || restoredUnits))
            {
                int upgId = m_unitsSelectedUpgrade;
                // Tech upgrades use price/time from UPGRADES.DEF
                cost = GetTechUpgradeCost(upgId);
                time = GetTechUpgradeTime(upgId);
                can = (m_money >= cost);
            }
            else if (m_unitsSelectedRearmUnitId > 0)
            {
                int toUnitId = m_unitsSelectedRearmUnitId;
                // Re-arm uses cost_upgrade from units.json
                cost = GetUpgradeCost(u.unit_id, toUnitId);
                time = GetUpgradeTime(toUnitId);
                can = (cost >= 0 && m_money >= cost);  // allow cost 0 for same-tier re-arm
            }

            m_btnUnitsAction->Enable(can);
            m_unitsTimeLabel->SetLabel(time > 0 ? wxString::Format(wxS("Time: %d"), time) : wxString(wxS("Time: -")));
            m_unitsCostLabel->SetLabel(cost >= 0 ? wxString::Format(wxS("Cost: %d"), cost) : wxString(wxS("Cost: -")));
        }
        break;

    case UNITS_TAB_INFO:
        // OK acts as "close"
        m_btnUnitsAction->Enable(true);
        if (m_unitsTimeLabel) m_unitsTimeLabel->SetLabel("Time: -");
        if (m_unitsCostLabel) m_unitsCostLabel->SetLabel("Cost: -");
        break;

    default:
        break;
    }
}


void StrategicLevelFrame::OnUnitsTabChange(int tab)
{
    m_unitsCurrentTab = static_cast<UnitsTab>(tab);
    m_unitsSelectedUpgrade = (m_unitsCurrentTab == UNITS_TAB_RECRUIT) ? 1 : -1;
    m_unitsSelectedRearmUnitId = -1;

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

    // Block actions while on cooldown
    if (m_unitStates[m_unitsSelectedUnit].cooldown_turns > 0 && m_unitsCurrentTab != UNITS_TAB_INFO)
        return;

    switch (m_unitsCurrentTab)
    {
    case UNITS_TAB_RECRUIT:
    {
        if (u.health >= 100)
            return;

        int q = (m_unitsSelectedUpgrade >= 0 && m_unitsSelectedUpgrade < RECRUIT_QUALITY_COUNT) ? m_unitsSelectedUpgrade : 1;

        const int cost = GetRecruitCost(m_unitsSelectedUnit, q);
        const int time = GetRecruitTime(q);

        if (m_money < cost)
        {
            wxMessageBox(wxString::Format("Not enough money. Need %d, have %d.", cost, m_money),
                "Recruit", wxOK | wxICON_WARNING, this);
            return;
        }

        // Experience impact: Rookie reduces XP most, Veteran moderately, Elite keeps XP.
        const int missing = 100 - u.health;
        const double kLossFactor[RECRUIT_QUALITY_COUNT] = { 1.0, 0.5, 0.0 };

        int& xp = m_unitStates[m_unitsSelectedUnit].experience;
        int xpLoss = (int)std::lround((double)xp * (double)missing / 100.0 * kLossFactor[q]);
        if (xpLoss < 0) xpLoss = 0;
        if (xpLoss > xp) xpLoss = xp;

        xp -= xpLoss;
        m_unitStates[m_unitsSelectedUnit].level = xp / 100;

        // Restore to full strength, set cooldown.
        u.health = 100;
        m_unitStates[m_unitsSelectedUnit].cooldown_turns = time;

        m_money -= cost;
    }
    break;

    case UNITS_TAB_UPGRADE:
    {
        // Two options:
        // 1) Tech upgrade from the list (stored in m_unitsSelectedUpgrade when list item selected)
        // 2) Re-arm to another unit type from the drop-down

        // Option 1: tech upgrade selected
        const bool restoredUnits = m_originalStrategicUi &&
            m_originalStrategicScreen == OriginalStrategicScreen::Units;
        if (m_unitsSelectedUpgrade > 0 &&
            ((m_unitsShopList && m_unitsShopList->GetSelectedItemCount() > 0) || restoredUnits))
        {
            const int upgId = m_unitsSelectedUpgrade;

            // Use price/time from UPGRADES.DEF
            const int cost = GetTechUpgradeCost(upgId);
            const int time = GetTechUpgradeTime(upgId);

            if (m_money < cost)
            {
                wxMessageBox(wxString::Format("Not enough money. Need %d, have %d.", cost, m_money),
                    "Upgrade", wxOK | wxICON_WARNING, this);
                return;
            }

            // Prevent duplicates
            auto& ups = m_unitStates[m_unitsSelectedUnit].upgrades;
            if (std::find(ups.begin(), ups.end(), upgId) != ups.end())
            {
                wxMessageBox("This upgrade is already installed on this unit.", "Upgrade", wxOK | wxICON_INFORMATION, this);
                return;
            }

            m_money -= cost;
            ups.push_back(upgId);
            m_unitStates[m_unitsSelectedUnit].cooldown_turns = time;
            break;
        }

        // Option 2: re-arm to a different unit type in the same category
        if (m_unitsSelectedRearmUnitId <= 0)
            return;

        int toUnitId = m_unitsSelectedRearmUnitId;

        // Re-arm uses cost_upgrade from units.json
        int cost = GetUpgradeCost(u.unit_id, toUnitId);
        int time = GetUpgradeTime(toUnitId);

        if (m_money < cost)
        {
            wxMessageBox(wxString::Format("Not enough money. Need %d, have %d.", cost, m_money),
                "Upgrade", wxOK | wxICON_WARNING, this);
            return;
        }

        wxString fromName = GetUnitDisplayName(u.unit_id);
        wxString toName = GetUnitDisplayName(toUnitId);

        int result = wxMessageBox(
            wxString::Format("Re-arm %s to %s for %d credits?\nAll tech upgrades will be lost.\nReady in %d turns.",
                fromName, toName, cost, time),
            "Re-arm", wxYES_NO | wxICON_QUESTION, this);

        if (result != wxYES)
            return;

        m_money -= cost;

        // Re-arm: change unit type, drop all installed upgrades, reduce experience moderately.
        u.unit_id = toUnitId;
        m_unitStates[m_unitsSelectedUnit].upgrades.clear();

        int& xp = m_unitStates[m_unitsSelectedUnit].experience;
        int xpLoss = xp / 5; // -20%
        xp -= xpLoss;
        if (xp < 0) xp = 0;
        m_unitStates[m_unitsSelectedUnit].level = xp / 100;

        m_unitStates[m_unitsSelectedUnit].cooldown_turns = time;
    }
    break;

    case UNITS_TAB_INFO:
        LeaveUnitsMode();
        return;

    default:
        break;
    }

    SaveStrategicState();
    RefreshUI();
    RefreshUnitsRoster();
    RefreshUnitsShopList();
    RefreshUnitsInfo(m_unitsSelectedUnit);
    RefreshUnitsActionButton();

    if (m_unitsLblMoneyValue)
        m_unitsLblMoneyValue->SetLabel(wxString::Format("%d", m_money));
}


void StrategicLevelFrame::OnUnitsDisband(wxCommandEvent&)
{
    if (m_unitsSelectedUnit < 0 || m_unitsSelectedUnit >= (int)m_playerUnits.size())
        return;

    // Ensure state exists (for cooldown check)
    if ((size_t)m_unitsSelectedUnit < m_unitStates.size() && m_unitStates[m_unitsSelectedUnit].cooldown_turns > 0)
        return;

    const auto& u = m_playerUnits[m_unitsSelectedUnit];

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
    m_unitsSelectedUpgrade = (m_unitsCurrentTab == UNITS_TAB_RECRUIT) ? 1 : -1;
    m_unitsSelectedRearmUnitId = -1;

    SaveStrategicState();
    RefreshUI();
    RefreshUnitsRoster();
    RefreshUnitsShopList();
    RefreshUnitsInfo(m_unitsSelectedUnit);
    RefreshUnitsActionButton();

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

    if (quality < 0 || quality >= RECRUIT_QUALITY_COUNT)
        quality = 1;

    const auto& u = m_playerUnits[unitIndex];
    int baseCost = GetUnitBuyCost(u.unit_id);
    if (baseCost <= 0)
        baseCost = 10; // fallback for units missing from units.json

    int damage = 100 - u.health;
    if (damage <= 0)
        return 0; // full health, nothing to recruit

    // Single-step multiplication avoids double integer truncation.
    // Old formula: (baseCost * ((damage * mult) / 100)) / 100 — truncates to 0 for small damage.
    // New formula: baseCost * damage * mult / 10000  (mathematically identical, less precision loss).
    int cost = (baseCost * damage * RECRUIT_QUALITY_COST_MULT[quality]) / 10000;

    // Always at least 1 credit for any damaged unit
    return std::max(1, cost);
}

int StrategicLevelFrame::GetRecruitTime(int quality) const
{
    if (quality < 0 || quality >= RECRUIT_QUALITY_COUNT)
        return 1;
    return RECRUIT_QUALITY_TIME[quality];
}

int StrategicLevelFrame::GetUpgradeCost(int unitId, int toUnitId) const
{
    // Re-arm cost: use cost_upgrade from units.json for the TARGET unit type
    // This is the cost to change unit type within same category
    auto it = m_unitUpgradeCosts.find(toUnitId);
    if (it != m_unitUpgradeCosts.end() && it->second > 0)
        return it->second;

    // Fallback: if cost_upgrade not defined, use difference in buy costs
    int fromCost = GetUnitBuyCost(unitId);
    int toCost = GetUnitBuyCost(toUnitId);

    if (fromCost <= 0 || toCost <= 0)
        return 0;

    int diff = toCost - fromCost;
    if (diff < 0) diff = 0;
    return diff;
}

int StrategicLevelFrame::GetUpgradeTime(int toUnitId) const
{
    // Re-arm time: default is 1 turn for unit type change
    (void)toUnitId;
    return 1;
}

int StrategicLevelFrame::GetTechUpgradeCost(int upgradeId) const
{
    // Tech upgrade cost from UPGRADES.DEF (Engine/Weapon/Armour style)
    auto it = m_upgradeDefs.find(upgradeId);
    if (it != m_upgradeDefs.end())
        return it->second.price;

    // Fallback: default cost
    return 10;
}

int StrategicLevelFrame::GetTechUpgradeTime(int upgradeId) const
{
    // Tech upgrade time from UPGRADES.DEF
    auto it = m_upgradeDefs.find(upgradeId);
    if (it != m_upgradeDefs.end())
        return std::max(1, it->second.time);

    // Fallback: default time
    return 1;
}

// Helper: get unit class category from JEDNOTKY.DEF (utype field)
// Returns: 0=air, 1=light, 2=armored, -1=unknown
// Uses the built-in SpellUnitRec methods for correct interpretation.
static int GetUnitTypeClassFromDef(SpellUnitRec* unitRec)
{
    if (!unitRec)
        return -1;
    // Use the canonical methods from SpellUnitRec which correctly interpret utype
    if (unitRec->isAir())
        return 0;
    if (unitRec->isLight())
        return 1;
    if (unitRec->isArmored())
        return 2;
    return -1;
}

wxString StrategicLevelFrame::GetUnitCategoryName(int unitId) const
{
    // PRIORITY 1: Use category from units.json (most accurate, user-defined)
    // This is the primary source for both Game mode and Editor mode.
    auto it = m_unitCategories.find(unitId);
    if (it != m_unitCategories.end() && !it->second.empty())
        return wxString::FromUTF8(it->second);

    // PRIORITY 2: Fallback to JEDNOTKY.DEF utype field
    if (m_spellData && m_spellData->units)
    {
        auto* unitRec = m_spellData->units->GetUnit(unitId);
        if (unitRec)
        {
            const int cls = GetUnitTypeClassFromDef(unitRec);
            switch (cls)
            {
            case 0: return "Aerial guns";  // Air units
            case 1: return "Infantry";     // Light units
            case 2: return "Artillery";    // Armored units
            default: break;
            }
        }
    }

    return "Other";
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

    // RESEARCH.DEF UpgradeItem::Data() points at the real UPGRADES.DEF id.
    // Earlier restored builds accidentally used the research-item id itself,
    // which also made the original grouped upgrade list impossible to match.
    for (const auto& r : m_researchDb)
    {
        if (!r.flags.Contains("UpgradeItem"))
            continue;
        if (m_gameModeEnabled && !m_researchCompleted.count(r.id))
            continue;

        const int upgradeId = (r.data >= 0) ? r.data : r.id;
        const auto def = m_upgradeDefs.find(upgradeId);
        if (def != m_upgradeDefs.end() && !def->second.suitableTypes.empty() &&
            !def->second.suitableTypes.count(unitId))
            continue;
        if (std::find(result.begin(), result.end(), upgradeId) == result.end())
            result.push_back(upgradeId);
    }

    std::sort(result.begin(), result.end());
    return result;
}

std::vector<int> StrategicLevelFrame::GetAvailableUnitTypesForUpgrade(int unitId) const
{
    std::vector<int> result;

    if (!m_spellData || !m_spellData->units)
        return result;

    // Use GetUnitCategoryName() which now prefers units.json categories
    // This works correctly in both Game mode and Editor mode.
    wxString currentCat = GetUnitCategoryName(unitId);
    if (currentCat.empty() || currentCat == "Other" || currentCat == "Unknown")
        return result;

    // Find all units in the same category that are different
    for (int i = 0; i < m_spellData->units->Count(); i++)
    {
        if (i == unitId)
            continue;

        wxString cat = GetUnitCategoryName(i);
        if (cat != currentCat)
            continue;

        // In Game mode: also check if unit is unlocked via research flags
        if (m_gameModeEnabled && !IsCampaignUnitUnlocked(i))
            continue;

        result.push_back(i);
    }

    return result;
}

void StrategicLevelFrame::BuildHierarchyPage(wxPanel* parent)
{
    auto* hs = new wxBoxSizer(wxVERTICAL);

    m_hierarchyBook = new wxSimplebook(parent, wxID_ANY);
    m_hierarchyBook->SetBackgroundColour(m_palette.background);
    m_hierarchyBook->AddPage(BuildHierarchyBookPage(m_hierarchyBook, 1), "Page 1", true);
    m_hierarchyBook->AddPage(BuildHierarchyBookPage(m_hierarchyBook, 2), "Page 2", false);
    hs->Add(m_hierarchyBook, 1, wxEXPAND);
    parent->SetSizer(hs);

    // HIERARCH.LZ reserves this button-sized opening in the lower-right
    // decoration.  Keep a single overlay above both book pages and place it
    // proportionally in the original 412x480 coordinate system.
    m_btnHierarchyPageToggle = new wxButton(parent, wxID_ANY, "Part 2");
    m_btnHierarchyPageToggle->SetFont(m_fontText);
    m_btnHierarchyPageToggle->SetBackgroundColour(m_palette.buttonBackground);
    m_btnHierarchyPageToggle->SetForegroundColour(m_palette.buttonText);
    m_btnHierarchyPageToggle->Bind(wxEVT_BUTTON, &StrategicLevelFrame::OnHierarchyTogglePage, this);

    auto placeToggle = [parent, button = m_btnHierarchyPageToggle]()
        {
            if (!parent || !button)
                return;
            const wxSize size = parent->GetClientSize();
            if (size.x <= 0 || size.y <= 0)
                return;
            const auto sx = [size](int x) { return x * size.x / kMapChromeW; };
            const auto sy = [size](int y) { return y * size.y / kStrategicScreenH; };
            button->SetSize(wxRect(sx(323), sy(439),
                std::max(1, sx(72)), std::max(1, sy(28))));
            button->Raise();
        };
    parent->Bind(wxEVT_SIZE,
        [placeToggle](wxSizeEvent& ev)
        {
            ev.Skip();
            placeToggle();
        });
    parent->CallAfter(placeToggle);
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
            wxColour frameCol, wxColour lineCol, const wxBitmap& background)
            : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
            , m_owner(owner)
            , m_frameCol(frameCol)
            , m_lineCol(lineCol)
            , m_background(background)
        {
            SetBackgroundColour(owner->m_palette.background);
            SetBackgroundStyle(wxBG_STYLE_PAINT);
            Bind(wxEVT_PAINT, &HierarchyCanvas::OnPaint, this);
            Bind(wxEVT_SIZE, &HierarchyCanvas::OnSize, this);
        }

        void AddLine(wxPoint a, wxPoint b) { m_lines.push_back({ a, b }); }

        void AddPlacedWindow(wxWindow* window, const wxRect& logicalRect)
        {
            if (!window)
                return;
            m_placements.push_back({ window, logicalRect });
            ApplyLayout();
        }

        void ApplyLayout()
        {
            const wxSize size = GetClientSize();
            if (size.x <= 0 || size.y <= 0)
                return;

            for (const auto& placement : m_placements)
            {
                if (!placement.window)
                    continue;
                const wxRect& r = placement.logicalRect;
                placement.window->SetSize(wxRect(
                    r.x * size.x / kMapChromeW,
                    r.y * size.y / kStrategicScreenH,
                    std::max(1, r.width * size.x / kMapChromeW),
                    std::max(1, r.height * size.y / kStrategicScreenH)));
            }
        }

    private:
        struct Placement
        {
            wxWindow* window = nullptr;
            wxRect logicalRect;
        };

        void OnSize(wxSizeEvent& ev)
        {
            ApplyLayout();
            m_scaledBackground = wxBitmap();
            ev.Skip();
            Refresh(false);
        }

        void OnPaint(wxPaintEvent&)
        {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetBackgroundColour()));
            dc.Clear();

            const wxSize size = GetClientSize();
            if (size.x <= 0 || size.y <= 0)
                return;

            if (m_background.IsOk())
            {
                if (!m_scaledBackground.IsOk() ||
                    m_scaledBackground.GetWidth() != size.x ||
                    m_scaledBackground.GetHeight() != size.y)
                {
                    m_scaledBackground = wxBitmap(m_background.ConvertToImage().Scale(
                        size.x, size.y, wxIMAGE_QUALITY_NEAREST));
                }
                if (m_scaledBackground.IsOk())
                    dc.DrawBitmap(m_scaledBackground, 0, 0, false);
                return; // the original asset already contains all tree lines
            }

            // Fallback for installations missing the original asset.
            dc.SetPen(wxPen(m_lineCol, 1));
            for (const auto& ln : m_lines)
            {
                dc.DrawLine(
                    ln.first.x * size.x / kMapChromeW,
                    ln.first.y * size.y / kStrategicScreenH,
                    ln.second.x * size.x / kMapChromeW,
                    ln.second.y * size.y / kStrategicScreenH);
            }
        }

        StrategicLevelFrame* m_owner = nullptr;
        wxColour m_frameCol;
        wxColour m_lineCol;
        wxBitmap m_background;
        wxBitmap m_scaledBackground;
        std::vector<std::pair<wxPoint, wxPoint>> m_lines;
        std::vector<Placement> m_placements;
    };

    // -------------------------------------------------------------------------
    // TUNING PARAMETERS (edit these only)
    // -------------------------------------------------------------------------
    struct Layout
    {
        // Native coordinates measured from HIERARCH.LZ after it is inserted at
        // (6,8) in VMH_FULL.  The four columns and seven command groups now sit
        // directly inside the rectangles painted by the DOS artwork.
        int canvasW = kMapChromeW;
        int canvasH = kStrategicScreenH;
        int canvasMargin = 0;

        int x_units = 12;
        int x_bcmd = 164;
        int x_rcmd = 217;
        int x_brig = 260;

        int unitSlotW = 146;
        int commandSlotW = 134;
        int brigadeSlotW = 136;
        int slotH = 18;
        int gapY = 5;
        int commandPairGapY = 2;

        int regTopY = 25;
        int regBlockHeight = 208;
        int regCommanderOffsetY = 75;
        int battalionPairGapY = 104;
        int unitsStackOffsetY = 0;
        int brigadeCommanderY = 207;

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

    wxBitmap hierarchyBackground;
    BuildStrategicScreenBitmap(m_spellData, "VMH_FULL.LZ", hierarchyBackground);
    auto* canvas = new HierarchyCanvas(
        this, page, L.frameCol, L.lineCol, hierarchyBackground);
    canvas->SetMinSize(wxSize(1, 1));

    // Helper to place slot widgets at exact coordinates.
    auto place = [&](int x, int y, const wxString& ph, const std::string& id, const std::string& type) {
        wxPanel* p = BuildHierarchySlot(canvas, ph, id, type);
        const int width = (x == L.x_units) ? L.unitSlotW
            : ((x == L.x_brig) ? L.brigadeSlotW : L.commandSlotW);
        const wxRect rect(x, y, width, L.slotH);
        p->SetSize(rect);
        canvas->AddPlacedWindow(p, rect);
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

        // Battalion pair is centred beside its four-unit stack in the
        // pre-painted HIERARCH.LZ openings.
        const int yCommander = y0 + 23;
        place(L.x_bcmd, yCommander, "commander",
            "battalion_" + std::to_string(bIndex) + "_commander", "commander");
        place(L.x_bcmd, yCommander + L.slotH + L.commandPairGapY, "?",
            "battalion_" + std::to_string(bIndex) + "_commander_unit", "unit");

        // connector: units stack -> battalion commander
        const int yMidUnits = y0 + L.unitsStackOffsetY + 1 * (L.slotH + L.gapY) + L.slotH / 2;
        canvas->AddLine(wxPoint(L.x_units + L.unitSlotW - L.lineInset, yMidUnits),
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
        place(L.x_rcmd, yReg + L.slotH + L.commandPairGapY, "?",
            "regiment_" + std::to_string(regimentIndex) + "_unit", "unit");

        // connectors: both battalion commander nodes -> regiment commander
        const int b0 = rLocal * 2;
        const int b1 = rLocal * 2 + 1;
        const int y0 = battalionTopY(b0) + 23 + L.slotH / 2;
        const int y1 = battalionTopY(b1) + 23 + L.slotH / 2;

        const int xFrom = L.x_bcmd + L.commandSlotW - L.lineInset;
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
        place(L.x_brig, yBrig + L.slotH + L.commandPairGapY, "?",
            "brigade_" + std::to_string(brigadeIndex) + "_unit", "unit");

        // connectors: both regiment nodes -> brigade node
        const int xFrom = L.x_rcmd + L.commandSlotW - L.lineInset;
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
    canvas->ApplyLayout();
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

    const HierarchySlot& slot = m_hierarchySlots[it->second];
    HierarchyDragData parsed = ParseHierarchyDragData(data);
    if (!parsed.valid)
        return;
    if (parsed.type != slot.type)
        return;

    bool applied = false;
    if (slot.type == "commander")
    {
        uint32_t cmdUid = parsed.commander_uid;
        int rank = parsed.rank;
        wxString name = parsed.name;

        if ((cmdUid == 0 || rank < 0 || name.empty()) && parsed.fromSlot)
        {
            auto its = m_hierarchySlotIndex.find(parsed.slotId);
            if (its != m_hierarchySlotIndex.end())
            {
                const HierarchySlot& src = m_hierarchySlots[its->second];
                if (src.type == "commander")
                {
                    if (cmdUid == 0)
                        cmdUid = src.commander_uid;
                    if (rank < 0)
                        rank = src.rank;
                    if (name.empty())
                        name = wxString::FromUTF8(src.commander_name);
                }
            }
        }

        if (cmdUid == 0 || rank < 0 || name.empty())
        {
            const std::string needle = name.ToStdString();
            for (const auto& c : m_playerCommanders)
            {
                if ((!needle.empty() && c.name == needle) && (rank < 0 || c.rank == rank))
                {
                    cmdUid = c.uid;
                    if (rank < 0)
                        rank = c.rank;
                    if (name.empty())
                        name = wxString::FromUTF8(c.name);
                    break;
                }
            }
        }

        applied = AssignCommanderToHierarchySlot(slotId, cmdUid, rank, name);
    }
    else
    {
        uint32_t uid = parsed.commander_uid;
        wxString display = parsed.name;

        if (parsed.fromSlot)
        {
            auto its = m_hierarchySlotIndex.find(parsed.slotId);
            if (its != m_hierarchySlotIndex.end())
            {
                const HierarchySlot& src = m_hierarchySlots[its->second];
                if (src.type == "unit")
                {
                    uid = src.unit_uid;
                    if (display.empty())
                        display = src.unit_display;
                }
            }
        }

        if ((uid == 0 || display.empty()) && !display.empty())
        {
            const auto items = GetRosterPickItems();
            for (const auto& rosterItem : items)
            {
                if (rosterItem.display == display || rosterItem.label == display)
                {
                    uid = rosterItem.uid;
                    display = rosterItem.display;
                    break;
                }
            }
        }

        applied = AssignUnitToHierarchySlot(slotId, uid, display);
    }

    if (applied && parsed.fromSlot && parsed.slotId != slotId)
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

void StrategicLevelFrame::ClearOriginalHierarchyPoolSelection()
{
    m_originalHierarchyPoolSelectionKind = OriginalHierarchyPoolSelectionKind::None;
    m_originalHierarchySelectedUnitUid = 0;
    m_originalHierarchySelectedUnitDisplay.clear();
    m_originalHierarchySelectedCommanderUid = 0;
    m_originalHierarchySelectedCommanderRank = -1;
    m_originalHierarchySelectedCommanderName.clear();
}

bool StrategicLevelFrame::AssignCommanderToHierarchySlot(const std::string& commanderSlotId,
    uint32_t commanderUid,
    int rank,
    const wxString& commanderName)
{
    auto it = m_hierarchySlotIndex.find(commanderSlotId);
    if (it == m_hierarchySlotIndex.end())
        return false;

    HierarchySlot& slot = m_hierarchySlots[it->second];
    if (slot.type != "commander" || commanderUid == 0 || rank < 0 || commanderName.empty())
        return false;

    int requiredRank = 0;
    if (commanderSlotId.rfind("regiment_", 0) == 0)
        requiredRank = 3;
    else if (commanderSlotId.rfind("brigade_", 0) == 0)
        requiredRank = 6;

    if (rank < requiredRank)
    {
        wxMessageBox(
            wxString::Format("This commander needs at least %s for this slot.",
                requiredRank == 6 ? "MajGen." : requiredRank == 3 ? "Maj." : "any rank"),
            "Hierarchy", wxOK | wxICON_INFORMATION, this);
        return false;
    }

    for (auto& other : m_hierarchySlots)
    {
        if (other.id == commanderSlotId || other.type != "commander")
            continue;
        if (other.commander_uid == commanderUid)
        {
            ClearHierarchySlot(other.id);
            break;
        }
    }

    slot.rank = rank;
    slot.commander_uid = commanderUid;
    wxString baseName = commanderName;
    const wxString abbr = GetRankAbbrev(rank);
    if (baseName.StartsWith(abbr + " "))
        baseName = baseName.Mid(abbr.length() + 1);
    slot.commander_name = baseName.ToUTF8().data();
    slot.assigned_unit_uid = 0;
    slot.assigned_unit_display.clear();
    UpdateCommanderHierarchyLabel(commanderSlotId);
    return true;
}

bool StrategicLevelFrame::AssignUnitToHierarchySlot(const std::string& unitSlotId,
    uint32_t unitUid,
    const wxString& unitDisplay)
{
    auto it = m_hierarchySlotIndex.find(unitSlotId);
    if (it == m_hierarchySlotIndex.end())
        return false;

    HierarchySlot& slot = m_hierarchySlots[it->second];
    if (slot.type != "unit" || unitUid == 0 || unitDisplay.empty())
        return false;

    auto endsWithLocal = [](const std::string& s, const std::string& suf) {
        return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    };
    const bool isBattalionAssign = endsWithLocal(unitSlotId, "_commander_unit");
    const bool isRegimentAssign = (unitSlotId.rfind("regiment_", 0) == 0) && endsWithLocal(unitSlotId, "_unit");
    const bool isBrigadeAssign = (unitSlotId.rfind("brigade_", 0) == 0) && endsWithLocal(unitSlotId, "_unit");

    if (isBattalionAssign || isRegimentAssign || isBrigadeAssign)
    {
        const std::string commanderId = GetCommanderSlotForUnitSlot(unitSlotId);
        auto itc = m_hierarchySlotIndex.find(commanderId);
        if (itc == m_hierarchySlotIndex.end())
            return false;
        HierarchySlot& cslot = m_hierarchySlots[itc->second];
        if (cslot.type != "commander" || !cslot.label)
            return false;
        if (cslot.label->GetLabel() == cslot.placeholder)
        {
            wxMessageBox(
                "Assign a commander first, then choose which of their units they are part of.",
                "Hierarchy", wxOK | wxICON_INFORMATION, this);
            return false;
        }

        std::vector<std::string> candidateSlotIds;
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
            if (parseNumberBetween(unitSlotId, "battalion_", "_commander_unit", bIndex))
                for (int u = 1; u <= 4; ++u)
                    candidateSlotIds.push_back("battalion_" + std::to_string(bIndex) + "_unit_" + std::to_string(u));
        }
        else if (isRegimentAssign)
        {
            int rIndex = 0;
            if (parseNumberBetween(unitSlotId, "regiment_", "_unit", rIndex))
            {
                const int brigadeIndex = (rIndex - 1) / 2 + 1;
                const int rLocal = (rIndex - 1) % 2;
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
            if (parseNumberBetween(unitSlotId, "brigade_", "_unit", brigIndex))
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

        bool found = false;
        for (const auto& sid : candidateSlotIds)
        {
            auto itu = m_hierarchySlotIndex.find(sid);
            if (itu == m_hierarchySlotIndex.end())
                continue;
            const HierarchySlot& us = m_hierarchySlots[itu->second];
            if (us.type == "unit" && us.unit_uid == unitUid)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            wxMessageBox(
                "The chosen unit is not present in this commander's subtree.\n\n"
                "Pick one of the units already placed directly under this commander.",
                "Hierarchy", wxOK | wxICON_INFORMATION, this);
            return false;
        }

        slot.unit_uid = unitUid;
        slot.unit_display = unitDisplay;
        slot.label->SetLabel(unitDisplay);
        slot.label->GetParent()->Layout();

        cslot.assigned_unit_uid = unitUid;
        cslot.assigned_unit_display = unitDisplay;
        UpdateCommanderHierarchyLabel(commanderId);
        return true;
    }

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
    if (!commanderPresent)
    {
        wxMessageBox(
            "Assign a commander first, then choose a unit for this commander.",
            "Hierarchy", wxOK | wxICON_INFORMATION, this);
        return false;
    }

    for (const auto& other : m_hierarchySlots)
    {
        if (other.id == unitSlotId || other.type != "unit")
            continue;
        if (other.unit_uid != 0 && other.unit_uid == unitUid)
        {
            wxMessageBox(
                "This exact unit instance is already assigned elsewhere in the hierarchy.\n\n"
                "Pick a different unit (note the [#id] in the list).",
                "Hierarchy", wxOK | wxICON_INFORMATION, this);
            return false;
        }
    }

    const uint32_t oldUid = slot.unit_uid;
    slot.unit_uid = unitUid;
    slot.unit_display = unitDisplay;
    slot.label->SetLabel(unitDisplay);
    slot.label->GetParent()->Layout();

    if (oldUid != 0 && oldUid != unitUid)
    {
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

    TryAssignCommanderToUnitSlot(unitSlotId);
    return true;
}

bool StrategicLevelFrame::ApplyOriginalHierarchyPoolSelectionToSlot(const std::string& slotId)
{
    if (m_originalHierarchyPoolSelectionKind == OriginalHierarchyPoolSelectionKind::Commander)
        return AssignCommanderToHierarchySlot(slotId,
            m_originalHierarchySelectedCommanderUid,
            m_originalHierarchySelectedCommanderRank,
            m_originalHierarchySelectedCommanderName);
    if (m_originalHierarchyPoolSelectionKind == OriginalHierarchyPoolSelectionKind::Unit)
        return AssignUnitToHierarchySlot(slotId,
            m_originalHierarchySelectedUnitUid,
            m_originalHierarchySelectedUnitDisplay);
    return false;
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
        : wxString::Format("slot:%s:%s:%u:%s", slot.id.c_str(), slot.type.c_str(),
            (unsigned)slot.unit_uid,
            slot.unit_display.empty() ? slot.label->GetLabel() : slot.unit_display));
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

void StrategicLevelFrame::ChooseCommanderForHierarchySlot(const std::string& commanderSlotId)
{
    auto it = m_hierarchySlotIndex.find(commanderSlotId);
    if (it == m_hierarchySlotIndex.end())
        return;

    HierarchySlot& slot = m_hierarchySlots[it->second];
    if (slot.type != "commander")
        return;

    int requiredRank = 0;
    if (commanderSlotId.rfind("regiment_", 0) == 0)
        requiredRank = 3;
    else if (commanderSlotId.rfind("brigade_", 0) == 0)
        requiredRank = 6;

    wxArrayString choices;
    choices.Add("<none>");
    std::vector<const CommanderRec*> commanders;
    commanders.reserve(m_playerCommanders.size());
    for (const auto& c : m_playerCommanders)
    {
        if (c.rank < requiredRank)
            continue;
        commanders.push_back(&c);
        choices.Add(wxString::Format("%s %s  [#%u]",
            GetRankAbbrev(c.rank), wxString::FromUTF8(c.name), (unsigned)c.uid));
    }

    int selection = 0;
    if (slot.commander_uid != 0)
    {
        for (size_t i = 0; i < commanders.size(); ++i)
        {
            if (commanders[i]->uid == slot.commander_uid)
            {
                selection = static_cast<int>(i) + 1;
                break;
            }
        }
    }

    wxSingleChoiceDialog dlg(this,
        "Choose a commander for this formation:",
        "Assign Commander", choices);
    dlg.SetSelection(selection);
    if (dlg.ShowModal() != wxID_OK)
        return;

    const int picked = dlg.GetSelection();
    if (picked <= 0)
    {
        ClearHierarchySlot(commanderSlotId);
        return;
    }
    const size_t index = static_cast<size_t>(picked - 1);
    if (index >= commanders.size() || !commanders[index])
        return;

    const CommanderRec& commander = *commanders[index];
    AssignCommanderToHierarchySlot(commanderSlotId,
        commander.uid,
        commander.rank,
        wxString::FromUTF8(commander.name));
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

    AssignUnitToHierarchySlot(unitSlotId, uid, display);
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
    m_btnHierarchyPageToggle->SetLabel(next == 0 ? "Part 2" : "Part 1");
}

void StrategicLevelFrame::OnRosterBeginDrag(wxListEvent& event)
{
    const long item = event.GetIndex();
    if (item < 0)
        return;
    wxString name = m_roster->GetItemText(item);
    if (name.empty())
        return;
    const uint32_t uid = static_cast<uint32_t>(m_roster->GetItemData(item));
    wxTextDataObject dataObject(wxString::Format("unit:%u:%s", (unsigned)uid, name));
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

// ============================================================
// Mission unit selection
// ============================================================

std::vector<uint32_t> StrategicLevelFrame::GetUnitsUnderCommander(uint32_t commander_uid) const
{
    std::vector<uint32_t> result;
    if (commander_uid == 0)
        return result;

    // Find which hierarchy slot this commander is in
    std::string commanderSlotId;
    for (const auto& slot : m_hierarchySlots)
    {
        if (slot.type == "commander" && slot.commander_uid == commander_uid)
        {
            commanderSlotId = slot.id;
            break;
        }
    }
    if (commanderSlotId.empty())
        return result;

    // Determine the scope based on commander slot type (battalion/regiment/brigade)
    // battalion_X_commander -> 4 units (battalion_X_unit_1..4)
    // regiment_X_commander -> 8 units (2 battalions)
    // brigade_X_commander -> 16 units (4 battalions)

    std::vector<std::string> unitSlotIds;

    auto parseNumber = [](const std::string& s, const std::string& pre, const std::string& suf) -> int
        {
            if (s.rfind(pre, 0) != 0) return -1;
            size_t p = s.find(suf, pre.size());
            if (p == std::string::npos) return -1;
            std::string num = s.substr(pre.size(), p - pre.size());
            try { return std::stoi(num); }
            catch (...) { return -1; }
        };

    if (commanderSlotId.find("battalion_") == 0 && endsWith(commanderSlotId, "_commander"))
    {
        int bIndex = parseNumber(commanderSlotId, "battalion_", "_commander");
        if (bIndex > 0)
        {
            for (int u = 1; u <= 4; ++u)
                unitSlotIds.push_back("battalion_" + std::to_string(bIndex) + "_unit_" + std::to_string(u));
        }
    }
    else if (commanderSlotId.find("regiment_") == 0 && endsWith(commanderSlotId, "_commander"))
    {
        int rIndex = parseNumber(commanderSlotId, "regiment_", "_commander");
        if (rIndex > 0)
        {
            int brigadeIndex = (rIndex - 1) / 2 + 1;
            int rLocal = (rIndex - 1) % 2;
            int battalionBase = (brigadeIndex - 1) * 4;
            int b0 = battalionBase + rLocal * 2 + 1;
            int b1 = battalionBase + rLocal * 2 + 2;
            for (int b : { b0, b1 })
            {
                for (int u = 1; u <= 4; ++u)
                    unitSlotIds.push_back("battalion_" + std::to_string(b) + "_unit_" + std::to_string(u));
            }
        }
    }
    else if (commanderSlotId.find("brigade_") == 0 && endsWith(commanderSlotId, "_commander"))
    {
        int brigIndex = parseNumber(commanderSlotId, "brigade_", "_commander");
        if (brigIndex > 0)
        {
            int battalionBase = (brigIndex - 1) * 4;
            for (int b = 1; b <= 4; ++b)
            {
                int bIndex = battalionBase + b;
                for (int u = 1; u <= 4; ++u)
                    unitSlotIds.push_back("battalion_" + std::to_string(bIndex) + "_unit_" + std::to_string(u));
            }
        }
    }

    // Collect unit UIDs from the slots
    for (const auto& slotId : unitSlotIds)
    {
        auto it = m_hierarchySlotIndex.find(slotId);
        if (it == m_hierarchySlotIndex.end())
            continue;
        const HierarchySlot& slot = m_hierarchySlots[it->second];
        if (slot.type == "unit" && slot.unit_uid != 0)
            result.push_back(slot.unit_uid);
    }

    return result;
}

StrategicLevelFrame::HierarchyBattleMeta StrategicLevelFrame::GetHierarchyBattleMeta(
    uint32_t unit_uid,
    const std::unordered_set<uint32_t>& participating_units) const
{
    HierarchyBattleMeta meta;
    if (unit_uid == 0 || participating_units.empty())
        return meta;

    auto parseNumberBetween = [](const std::string& s, const std::string& pre,
        const std::string& marker) -> int
    {
        if (s.rfind(pre, 0) != 0)
            return -1;
        const size_t p = s.find(marker, pre.size());
        if (p == std::string::npos)
            return -1;
        try { return std::stoi(s.substr(pre.size(), p - pre.size())); }
        catch (...) { return -1; }
    };

    // Every permanent company occupies one of battalion_X_unit_N.  The
    // one-digit number shown beside the tactical mini status bar is therefore
    // the battalion/formation number (1..8), exactly matching the original HUD
    // capacity.
    int battalion = -1;
    for (const auto& slot : m_hierarchySlots)
    {
        if (slot.type != "unit" || slot.unit_uid != unit_uid)
            continue;
        if (slot.id.rfind("battalion_", 0) != 0 || slot.id.find("_unit_") == std::string::npos)
            continue;
        battalion = parseNumberBetween(slot.id, "battalion_", "_unit_");
        if (battalion > 0)
            break;
    }
    if (battalion <= 0)
        return meta;

    const int regiment = (battalion - 1) / 2 + 1;
    const int brigade = (battalion - 1) / 4 + 1;

    auto appendBattalionUnits = [&](int b, std::vector<uint32_t>& out)
    {
        for (int u = 1; u <= 4; ++u)
        {
            const std::string id = "battalion_" + std::to_string(b) + "_unit_" + std::to_string(u);
            auto it = m_hierarchySlotIndex.find(id);
            if (it == m_hierarchySlotIndex.end())
                continue;
            const HierarchySlot& s = m_hierarchySlots[it->second];
            if (s.unit_uid != 0)
                out.push_back(s.unit_uid);
        }
    };

    auto commanderHostReady = [&](const std::string& commanderId,
        const std::vector<uint32_t>& units,
        uint32_t& host_uid) -> bool
    {
        auto itc = m_hierarchySlotIndex.find(commanderId);
        if (itc == m_hierarchySlotIndex.end())
            return false;
        const HierarchySlot& commander = m_hierarchySlots[itc->second];
        if (commander.commander_uid == 0 || commander.assigned_unit_uid == 0)
            return false;

        host_uid = commander.assigned_unit_uid;
        bool hostInScope = false;
        for (uint32_t uid : units)
            if (uid != 0 && uid == host_uid) { hostInScope = true; break; }
        return hostInScope && participating_units.count(host_uid) != 0;
    };

    auto battalionActive = [&](int b, uint32_t& host_uid) -> bool
    {
        std::vector<uint32_t> units;
        appendBattalionUnits(b, units);
        if (!commanderHostReady("battalion_" + std::to_string(b) + "_commander", units, host_uid))
            return false;

        std::unordered_set<uint32_t> present;
        for (uint32_t uid : units)
            if (uid != 0 && participating_units.count(uid) != 0)
                present.insert(uid);
        // Manual: an active battalion (prapor) consists of at least 3 companies.
        return present.size() >= 3;
    };

    auto regimentActive = [&](int r, uint32_t& host_uid) -> bool
    {
        const int brigadeIndex = (r - 1) / 2 + 1;
        const int local = (r - 1) % 2;
        const int b0 = (brigadeIndex - 1) * 4 + local * 2 + 1;
        const int b1 = b0 + 1;

        uint32_t b0Host = 0, b1Host = 0;
        if (!battalionActive(b0, b0Host) || !battalionActive(b1, b1Host))
            return false;

        std::vector<uint32_t> units;
        appendBattalionUnits(b0, units);
        appendBattalionUnits(b1, units);
        // A regiment is the next level made from its two subordinate battalions.
        return commanderHostReady("regiment_" + std::to_string(r) + "_commander", units, host_uid);
    };

    auto brigadeActive = [&](int br, uint32_t& host_uid) -> bool
    {
        const int r0 = (br - 1) * 2 + 1;
        const int r1 = r0 + 1;
        uint32_t r0Host = 0, r1Host = 0;
        if (!regimentActive(r0, r0Host) || !regimentActive(r1, r1Host))
            return false;

        std::vector<uint32_t> units;
        const int base = (br - 1) * 4 + 1;
        for (int b = 0; b < 4; ++b)
            appendBattalionUnits(base + b, units);
        return commanderHostReady("brigade_" + std::to_string(br) + "_commander", units, host_uid);
    };

    uint32_t battalionHost = 0, regimentHost = 0, brigadeHost = 0;
    const bool battalionIsActive = battalionActive(battalion, battalionHost);
    const bool regimentIsActive = regimentActive(regiment, regimentHost);
    const bool brigadeIsActive = brigadeActive(brigade, brigadeHost);

    if (battalionIsActive)
    {
        meta.formation_id = battalion;
        meta.formation_level = 1;
        meta.attack_bonus = 1;
        meta.defence_bonus = 1;
    }
    if (regimentIsActive)
    {
        meta.formation_id = battalion;
        meta.formation_level = 2;
        meta.attack_bonus = 2;
        meta.defence_bonus = 1;
    }
    if (brigadeIsActive)
    {
        meta.formation_id = battalion;
        meta.formation_level = 3;
        meta.attack_bonus = 4;
        meta.defence_bonus = 3;
    }

    meta.carries_commander =
        (battalionIsActive && battalionHost == unit_uid) ||
        (regimentIsActive && regimentHost == unit_uid) ||
        (brigadeIsActive && brigadeHost == unit_uid);
    return meta;
}

bool StrategicLevelFrame::IsCommanderFormationActive(uint32_t commander_uid) const
{
    if (commander_uid == 0)
        return false;

    const HierarchySlot* commanderSlot = nullptr;
    for (const auto& slot : m_hierarchySlots)
    {
        if (slot.type == "commander" && slot.commander_uid == commander_uid)
        {
            commanderSlot = &slot;
            break;
        }
    }
    if (!commanderSlot || commanderSlot->assigned_unit_uid == 0)
        return false;

    std::unordered_set<uint32_t> participating;
    for (uint32_t uid : GetUnitsUnderCommander(commander_uid))
        if (uid != 0)
            participating.insert(uid);
    if (participating.empty())
        return false;

    const HierarchyBattleMeta meta = GetHierarchyBattleMeta(
        commanderSlot->assigned_unit_uid, participating);

    int requiredLevel = 1;
    if (commanderSlot->id.rfind("regiment_", 0) == 0)
        requiredLevel = 2;
    else if (commanderSlot->id.rfind("brigade_", 0) == 0)
        requiredLevel = 3;
    return meta.formation_level >= requiredLevel;
}

void StrategicLevelFrame::OnCommanderSelectForMission(wxListEvent& ev)
{
    const long item = ev.GetIndex();
    if (item < 0 || !m_cmdRoster)
        return;

    const uint32_t cmdUid = (uint32_t)m_cmdRoster->GetItemData(item);
    if (cmdUid == 0)
        return;

    // Toggle commander selection
    const bool wasSelected = (m_selectedCommandersForMission.count(cmdUid) > 0);

    // Get all units under this commander. The original strategic map treats
    // the commander shortcut as an *active formation* selector: the formation
    // needs at least three companies and the commander must sit in one of them.
    std::vector<uint32_t> units = GetUnitsUnderCommander(cmdUid);

    if (!wasSelected && !IsCommanderFormationActive(cmdUid))
    {
        wxMessageBox("This formation is not active yet.\n\n"
            "It needs at least three units and the commander must be assigned to one of them in Hierarchy.",
            "Select Formation", wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (wasSelected)
    {
        // Deselect commander and all their units
        m_selectedCommandersForMission.erase(cmdUid);
        for (uint32_t uid : units)
            m_selectedUnitsForMission.erase(uid);
    }
    else
    {
        // Select commander and all their units (skip units on cooldown)
        m_selectedCommandersForMission.insert(cmdUid);
        for (uint32_t uid : units)
        {
            if (!IsRosterUidOnCooldown(uid))
                m_selectedUnitsForMission.insert(uid);
        }
    }

    UpdateCommanderRosterSelectionVisuals();
    UpdateRosterSelectionVisuals();
}

void StrategicLevelFrame::OnUnitSelectForMission(wxListEvent& ev)
{
    if (!m_roster)
        return;

    const long item = ev.GetIndex();
    if (item < 0)
        return;

    const uint32_t uid = (uint32_t)m_roster->GetItemData(item);
    if (uid == 0)
        return;

    // Units on cooldown cannot be selected for mission
    if (IsRosterUidOnCooldown(uid))
    {
        m_selectedUnitsForMission.erase(uid);
        UpdateRosterSelectionVisuals();
        return;
    }

    // Toggle selection: if already selected, remove; otherwise add
    if (m_selectedUnitsForMission.count(uid) > 0)
        m_selectedUnitsForMission.erase(uid);
    else
        m_selectedUnitsForMission.insert(uid);

    // Update visuals (colors)
    UpdateRosterSelectionVisuals();
}

void StrategicLevelFrame::UpdateRosterSelectionVisuals()
{
    if (!m_roster)
        return;

    const wxColour clrSelected(0x00, 0xFF, 0x00);  // bright green for selected
    const wxColour clrUnselected(0x80, 0x80, 0x80); // gray for unselected
    const wxColour clrCooldown(0x66, 0x33, 0x33);   // dark red-brown for cooldown (not selectable)

    // Update text color in the list control to match m_selectedUnitsForMission
    // We don't modify the native selection state - we only use colors for visual feedback
    const long count = m_roster->GetItemCount();
    for (long i = 0; i < count; ++i)
    {
        const uint32_t uid = (uint32_t)m_roster->GetItemData(i);
        const bool onCooldown = (uid != 0 && IsRosterUidOnCooldown(uid));
        const bool isSelectedForMission = (uid != 0 && m_selectedUnitsForMission.count(uid) > 0);

        // Cooldown units are always shown in cooldown color and auto-deselected
        if (onCooldown)
        {
            m_selectedUnitsForMission.erase(uid);
            m_roster->SetItemTextColour(i, clrCooldown);
        }
        else
        {
            m_roster->SetItemTextColour(i, isSelectedForMission ? clrSelected : clrUnselected);
        }
    }

    // Force refresh to show color changes
    m_roster->Refresh();

    // Update launch button enable state
    const bool onStrategicMap = (m_leftBook && m_leftBook->GetSelection() == 0 && !m_buyModeActive);
    if (m_btnLaunch)
        m_btnLaunch->Enable(onStrategicMap && m_selectedTerritory >= 0 && !m_selectedUnitsForMission.empty());
}

void StrategicLevelFrame::UpdateCommanderRosterSelectionVisuals()
{
    if (!m_cmdRoster)
        return;

    const wxColour clrSelected(0x00, 0xFF, 0x00);  // bright green for selected
    const wxColour clrUnselected(0x80, 0x80, 0x80); // gray for unselected

    // Update text color in the commander list to match m_selectedCommandersForMission
    const long count = m_cmdRoster->GetItemCount();
    for (long i = 0; i < count; ++i)
    {
        const uint32_t uid = (uint32_t)m_cmdRoster->GetItemData(i);
        const bool isSelectedForMission = (uid != 0 && m_selectedCommandersForMission.count(uid) > 0);

        // Update text color based on mission selection
        m_cmdRoster->SetItemTextColour(i, isSelectedForMission ? clrSelected : clrUnselected);
    }

    // Force refresh to show color changes
    m_cmdRoster->Refresh();
}

std::vector<LevelData::PlayerUnitAdd> StrategicLevelFrame::GetSelectedUnitsForLaunch() const
{
    std::vector<LevelData::PlayerUnitAdd> result;

    if (m_selectedUnitsForMission.empty())
        return result;

    // Only units that will really enter the battle count towards formation
    // activation. A company on cooldown can remain highlighted from an older
    // selection, but it must not keep a formation bonus alive off-map.
    std::unordered_set<uint32_t> participatingUnits;
    for (uint32_t uid : m_selectedUnitsForMission)
        if (uid != 0 && !IsRosterUidOnCooldown(uid))
            participatingUnits.insert(uid);

    // Map roster row UID -> playerUnits index
    // We need to find which m_playerUnits entries correspond to selected UIDs
    int uidIndex = 0;
    for (size_t pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
    {
        const auto& u = m_playerUnits[pIdx];
        for (int inst = 0; inst < u.count; ++inst)
        {
            if (uidIndex < (int)m_rosterRowUids.size())
            {
                const uint32_t uid = m_rosterRowUids[uidIndex];
                if (m_selectedUnitsForMission.count(uid) > 0)
                {
                    // Safety: skip units on cooldown (should not be in selection, but guard anyway)
                    if (pIdx < m_unitStates.size() && m_unitStates[pIdx].cooldown_turns > 0)
                    {
                        ++uidIndex;
                        continue;
                    }
                    // Add this concrete unit instance to the result and carry
                    // the active hierarchy metadata into the tactical map.
                    LevelData::PlayerUnitAdd add;
                    add.unit_id = u.unit_id;
                    add.count = 1;
                    add.health = u.health;
                    add.extra = u.extra;
                    add.strategic_uid = uid;
                    const HierarchyBattleMeta formation = GetHierarchyBattleMeta(uid, participatingUnits);
                    add.formation_id = formation.formation_id;
                    add.formation_level = formation.formation_level;
                    add.formation_attack_bonus = formation.attack_bonus;
                    add.formation_defence_bonus = formation.defence_bonus;
                    add.carries_commander = formation.carries_commander;
                    result.push_back(add);
                }
            }
            ++uidIndex;
        }
    }

    return result;
}

bool StrategicLevelFrame::IsRosterUidOnCooldown(uint32_t uid) const
{
    if (uid == 0)
        return false;

    int uidIndex = 0;
    for (size_t pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
    {
        const auto& u = m_playerUnits[pIdx];
        for (int inst = 0; inst < u.count; ++inst)
        {
            if (uidIndex < (int)m_rosterRowUids.size())
            {
                if (m_rosterRowUids[uidIndex] == uid)
                    return (pIdx < m_unitStates.size() && m_unitStates[pIdx].cooldown_turns > 0);
            }
            ++uidIndex;
        }
    }
    return false;
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

    // Update End Turn button label with current turn number (two-line format)
    if (m_btnEndTurn)
        m_btnEndTurn->SetLabel(wxString::Format("Turn\n%02d", m_turn));


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

            const wxColour clrSelected(0x00, 0xFF, 0x00);  // bright green for selected
            const wxColour clrUnselected(0x80, 0x80, 0x80); // gray for unselected
            const bool isSelectedForMission = (m_selectedCommandersForMission.count(c.uid) > 0);

            long cidx = m_cmdRoster->InsertItem(crow++, wxString::FromUTF8(c.name));
            m_cmdRoster->SetItem(cidx, 1, GetRankAbbrev(c.rank));
            m_cmdRoster->SetItemData(cidx, (long)c.uid);
            m_cmdRoster->SetItemTextColour(cidx, isSelectedForMission ? clrSelected : clrUnselected);
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
    size_t pIdx = 0;
    const wxColour clrSelected(0x00, 0xFF, 0x00);  // bright green for selected
    const wxColour clrUnselected(0x80, 0x80, 0x80); // gray for unselected
    const wxColour clrCooldown(0x66, 0x33, 0x33);   // dark red-brown for cooldown
    for (pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
    {
        const auto& u = m_playerUnits[pIdx];
        const bool onCooldown = (pIdx < m_unitStates.size() && m_unitStates[pIdx].cooldown_turns > 0);
        for (int i = 0; i < u.count; ++i)
        {
            const uint32_t uid = (uidIndex < (int)m_rosterRowUids.size()) ? m_rosterRowUids[(size_t)uidIndex++] : (uint32_t)m_nextRosterUid++;
            long idx = m_roster->InsertItem(row++, GetUnitDisplayName(u.unit_id));
            // Show cooldown in HP column
            if (onCooldown)
                m_roster->SetItem(idx, 1, wxString::Format("%d%% -%dT", u.health, m_unitStates[pIdx].cooldown_turns));
            else
                m_roster->SetItem(idx, 1, wxString::Format("%d", u.health));
            // Store UID in item data for picking.
            m_roster->SetItemData(idx, (long)uid);
            // Set text color: cooldown > selection > default
            if (onCooldown)
            {
                m_selectedUnitsForMission.erase(uid);
                m_roster->SetItemTextColour(idx, clrCooldown);
            }
            else
            {
                const bool isSelected = (m_selectedUnitsForMission.count(uid) > 0);
                m_roster->SetItemTextColour(idx, isSelected ? clrSelected : clrUnselected);
            }
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

            // Restore selection state for units and commanders (after roster rebuild)
            UpdateRosterSelectionVisuals();
            UpdateCommanderRosterSelectionVisuals();

            // Launch mission is only available on the Strategic map page (left book page 0)
            // and never while Buy/Sell overlay is active.
            // Also requires at least one unit selected.
            const bool onStrategicMap = (m_leftBook && m_leftBook->GetSelection() == 0 && !m_buyModeActive);
            const bool hasSelection = !m_selectedUnitsForMission.empty();
            m_btnLaunch->Enable(onStrategicMap && m_selectedTerritory >= 0 && hasSelection);
            if (m_btnBuyShop)
                m_btnBuyShop->Enable(true);

            if (m_researchMode)
                RefreshResearchUI();

            m_originalStrategicDirty = true;
            if (m_originalStrategicUi && m_originalStrategicPanel)
                m_originalStrategicPanel->Refresh();
        }




// ============================================================
// Resources page + mechanics
// ============================================================

int StrategicLevelFrame::GetCurrentStrategicPointIncome() const
{
    int totalIncome = 0;
    for (int tid : m_ownedTerritories)
    {
        if (tid <= 0) continue;
        auto it = m_territoryResources.find(tid);
        if (it == m_territoryResources.end()) continue;
        const TerritoryResourceState& st = it->second;
        totalIncome += std::min(std::max(0, st.incomePerTurn), std::max(0, st.remaining));
    }
    return totalIncome;
}

void StrategicLevelFrame::SetGlobalResearchAllocation(int value)
{
    const int maxResearch = GetCurrentStrategicPointIncome() / 3;
    m_resourcesGlobalResearch = std::clamp(value, 0, maxResearch);
    // The status-panel "Výzkum" value in the original UI is the current
    // per-turn research allocation, not a banked currency.
    m_research = m_resourcesGlobalResearch;
    m_territoryResources[kResourcesMetaTerritoryId].researchCarry = m_resourcesGlobalResearch;
    if (m_resourcesSlider)
    {
        m_resourcesSlider->SetRange(0, std::max(1, maxResearch));
        m_resourcesSlider->Enable(maxResearch > 0);
        m_resourcesSlider->SetValue(std::clamp(m_resourcesGlobalResearch, 0, std::max(1, maxResearch)));
    }
}

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
    m_resourcesCanvas->Bind(wxEVT_PAINT, &StrategicLevelFrame::OnMapPaint, this);
    m_resourcesCanvas->Bind(wxEVT_LEFT_DOWN, &StrategicLevelFrame::OnMapLeftDown, this);
    m_resourcesCanvas->Bind(wxEVT_MOTION, &StrategicLevelFrame::OnMapMouseMove, this);
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

    m_resourcesSlider = new wxSlider(under, wxID_ANY, 0, 0, 100,
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
    m_resourcesTable->InsertColumn(0, "Territory", wxLIST_FORMAT_LEFT, -1);
    m_resourcesTable->InsertColumn(1, "Strategic points", wxLIST_FORMAT_CENTER, -1);
    m_resourcesTable->InsertColumn(2, "SB / turn", wxLIST_FORMAT_CENTER, -1);
    m_resourcesTable->InsertColumn(3, "Turns left", wxLIST_FORMAT_CENTER, -1);
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
    BindListGridOverlay(m_resourcesTable);
    us->Add(m_resourcesTable, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    m_resourcesSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&)
        {
            if (!m_resourcesSlider) return;
            SetGlobalResearchAllocation(m_resourcesSlider->GetValue());
            SaveStrategicState();
            RefreshResourcesPage();
        });

    under->SetSizer(us);
    s->Add(under, 2, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    m_resourcesPanel->SetSizer(s);
}


void StrategicLevelFrame::RefreshResourcesPage()
{
    // Ensure every territory mirrors its DefineStrategicPoints() definition.
    for (const auto& t : m_level.territories)
    {
        auto it = m_territoryResources.find(t.id);
        if (it == m_territoryResources.end())
        {
            TerritoryResourceState st;
            st.total = std::max(0, t.strategic_points_total);
            st.remaining = st.total;
            st.incomePerTurn = std::max(0, t.strategic_points_per_turn);
            m_territoryResources[t.id] = st;
        }
        else
        {
            // Old saves did not persist incomePerTurn; recover it from LEVEL_XX.DEF.
            if (it->second.total <= 0 && t.strategic_points_total > 0)
            {
                it->second.total = t.strategic_points_total;
                if (it->second.remaining <= 0) it->second.remaining = it->second.total;
            }
            if (it->second.incomePerTurn <= 0 && t.strategic_points_per_turn > 0)
                it->second.incomePerTurn = t.strategic_points_per_turn;
            it->second.remaining = std::clamp(it->second.remaining, 0, std::max(0, it->second.total));
        }
    }

    // Current strategic-point yield from owned, non-depleted territories.
    const int totalIncome = GetCurrentStrategicPointIncome();
    const int maxResearch = totalIncome / 3; // 3 SB -> 1 research point (manual/original UI)

    // Load/persist the global desired research output in the id=0 meta record.
    auto itMeta = m_territoryResources.find(kResourcesMetaTerritoryId);
    if (itMeta != m_territoryResources.end())
        m_resourcesGlobalResearch = std::clamp(itMeta->second.researchCarry, 0, maxResearch);
    else
    {
        m_resourcesGlobalResearch = std::clamp(m_resourcesGlobalResearch, 0, maxResearch);
        m_territoryResources[kResourcesMetaTerritoryId].researchCarry = m_resourcesGlobalResearch;
    }
    m_territoryResources[kResourcesMetaTerritoryId].researchCarry = m_resourcesGlobalResearch;

    const int R = m_resourcesGlobalResearch;
    const int M = std::max(0, totalIncome - 3 * R);

    if (m_resourcesSelectedLabel)
    {
        const bool ownedSel = (m_selectedTerritory > 0 &&
            std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(),
                m_selectedTerritory) != m_ownedTerritories.end());
        if (ownedSel)
        {
            const auto& st = m_territoryResources[m_selectedTerritory];
            const int rounds = st.incomePerTurn > 0
                ? (st.remaining + st.incomePerTurn - 1) / st.incomePerTurn : 0;
            m_resourcesSelectedLabel->SetLabel(
                wxString::Format("Territory T%02d  -  %d SB/turn, %d turns (%d/%d SB)",
                    m_selectedTerritory, st.incomePerTurn, rounds, st.remaining, st.total));
        }
        else
        {
            m_resourcesSelectedLabel->SetLabel(
                wxString::Format("Owned territories: %d  -  %d SB/turn",
                    (int)m_ownedTerritories.size(), totalIncome));
        }
    }

    if (m_resourcesSlider)
    {
        m_resourcesSlider->SetRange(0, std::max(1, maxResearch));
        m_resourcesSlider->Enable(maxResearch > 0);
        m_resourcesSlider->SetValue(std::clamp(R, 0, std::max(1, maxResearch)));
    }
    if (m_resourcesRatioLabel)
        m_resourcesRatioLabel->SetLabel(
            wxString::Format("Money: %d  Research: %d  (total SB: %d)", M, R, totalIncome));

    if (m_resourcesTable)
    {
        m_resourcesTable->Freeze();
        m_resourcesTable->DeleteAllItems();

        const wxColour clrOwned = m_palette.text;
        const wxColour clrDepleted(0x88, 0x44, 0x44);
        const wxColour clrSelected = m_palette.heading;

        for (int tid : m_ownedTerritories)
        {
            if (tid <= 0) continue;
            const auto& st = m_territoryResources.count(tid)
                ? m_territoryResources.at(tid) : TerritoryResourceState{};
            const int yield = std::min(std::max(0, st.incomePerTurn), std::max(0, st.remaining));
            const int rounds = st.incomePerTurn > 0
                ? (st.remaining + st.incomePerTurn - 1) / st.incomePerTurn : 0;

            const wxString resStr = wxString::Format("%d / %d", st.remaining, st.total);
            const wxString yieldStr = wxString::Format("%d", yield);
            const wxString turnsStr = st.incomePerTurn > 0 ? wxString::Format("%d", rounds) : wxString("-");

            long row = m_resourcesTable->InsertItem(
                m_resourcesTable->GetItemCount(), wxString::Format("T%02d", tid));
            m_resourcesTable->SetItem(row, 1, resStr);
            m_resourcesTable->SetItem(row, 2, yieldStr);
            m_resourcesTable->SetItem(row, 3, turnsStr);
            m_resourcesTable->SetItemData(row, (long)tid);

            const bool depleted = (st.remaining <= 0);
            const bool selected = (tid == m_selectedTerritory);
            m_resourcesTable->SetItemTextColour(row,
                selected ? clrSelected : (depleted ? clrDepleted : clrOwned));
        }
        m_resourcesTable->Thaw();
    }

    MarkOverlayDirty();
    if (m_resourcesCanvas)
        m_resourcesCanvas->Refresh();
}

void StrategicLevelFrame::ApplyResourceTickEndTurn()
{
    // Spellcross strategic points: each owned territory yields up to its
    // DefineStrategicPoints(..., total, perTurn) rate until the finite pool is empty.
    int generated = 0;
    for (int tid : m_ownedTerritories)
    {
        if (tid <= 0) continue;
        auto it = m_territoryResources.find(tid);
        if (it == m_territoryResources.end()) continue;
        auto& st = it->second;
        if (st.remaining <= 0 || st.incomePerTurn <= 0) continue;

        const int take = std::min(st.remaining, st.incomePerTurn);
        st.remaining -= take;
        generated += take;
    }

    // Original conversion ratios documented by the game:
    //   1 SB -> 1 money
    //   3 SB -> 1 research point
    // m_resourcesGlobalResearch stores the desired research points per turn.
    const int researchPoints = std::min(std::max(0, m_resourcesGlobalResearch), generated / 3);
    const int moneyPoints = std::max(0, generated - researchPoints * 3);
    m_money += moneyPoints;
    m_research = researchPoints;

    // Do not clamp the next-turn allocation here: ApplyResearchTickEndTurn()
    // still needs this turn's generated research value. The allocation is
    // clamped immediately afterwards in OnEndTurn(), once research progressed.
    m_territoryResources[kResourcesMetaTerritoryId].researchCarry = m_resourcesGlobalResearch;

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

    // Reflow hard line breaks from original game text files (designed for 320x200).
    // Replace single \n with space so text wraps to the full width of the text control.
    // Preserve paragraph breaks (double \n\n).
    {
        wxString reflowed;
        reflowed.reserve(ws.length());
        for (size_t i = 0; i < ws.length(); ++i)
        {
            if (ws[i] == '\n')
            {
                if (i + 1 < ws.length() && ws[i + 1] == '\n')
                {
                    reflowed += '\n';
                    reflowed += '\n';
                    ++i;
                    while (i + 1 < ws.length() && ws[i + 1] == '\n')
                        ++i;
                }
                else
                {
                    if (!reflowed.empty() && reflowed.Last() != ' ' && reflowed.Last() != '\n')
                        reflowed += ' ';
                }
            }
            else
            {
                reflowed += ws[i];
            }
        }
        ws = std::move(reflowed);
    }

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
    {
        NormalizeResearchSelection();
        return;
    }

    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path base = GetStableBaseDir();
    const std::vector<fs::path> commonDirs = {
        base / "temp" / "COMMON",
        fs::current_path(ec) / "temp" / "COMMON",
        base / "builds" / "x64" / "Release" / "temp" / "COMMON",
        base / "builds" / "x64" / "Debug" / "temp" / "COMMON",
    };
    const std::vector<fs::path> researchDirs = {
        base / "temp" / "RESEARCH",
        fs::current_path(ec) / "temp" / "RESEARCH",
        base / "builds" / "x64" / "Release" / "temp" / "RESEARCH",
        base / "builds" / "x64" / "Debug" / "temp" / "RESEARCH",
    };

    auto loadFileBin = [&](const fs::path& p) -> std::string
        {
            std::ifstream f(p, std::ios::binary);
            if (!f) return {};
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };

    auto loadExtracted = [&](const std::vector<fs::path>& dirs, const char* name) -> std::string
        {
            for (const auto& dir : dirs)
            {
                if (dir.empty()) continue;
                const fs::path pth = dir / name;
                if (!fs::exists(pth, ec)) continue;
                std::string raw = loadFileBin(pth);
                if (!raw.empty()) return raw;
            }
            return {};
        };

    auto archiveCandidates = [&](const wchar_t* archiveName) -> std::vector<fs::path>
        {
            std::vector<fs::path> out;
            if (m_spellData)
            {
                if (!m_spellData->data_path.empty())
                    out.emplace_back(fs::path(m_spellData->data_path) / archiveName);
                if (!m_spellData->cd_data_path.empty())
                    out.emplace_back(fs::path(m_spellData->cd_data_path) / archiveName);
            }
            out.emplace_back(base / archiveName);
            out.emplace_back(fs::current_path(ec) / archiveName);
            return out;
        };

    auto loadArchiveMember = [&](const wchar_t* archiveName, const char* member) -> std::string
        {
            for (const auto& pth : archiveCandidates(archiveName))
            {
                if (pth.empty() || !fs::exists(pth, ec) || fs::is_directory(pth, ec)) continue;
                try
                {
                    FSarchive arc(pth.wstring(), FSarchive::Options::NONE);
                    uint8_t* data = nullptr;
                    int size = 0;
                    if (arc.GetFile(member, &data, &size) == 0 && data && size > 0)
                        return std::string(reinterpret_cast<const char*>(data), static_cast<size_t>(size));
                }
                catch (...) {}
            }
            return {};
        };

    // ----------------------------------------------------------------
    // 1. Parse RESEARCH.DEF -> group, level, Time(), flags and OR prerequisites.
    //    Prefer the exported COMMON directory but fall back directly to COMMON.FS;
    //    this keeps Research/Info functional in a clean runtime with no pre-extract step.
    // ----------------------------------------------------------------
    struct DefRec
    {
        wxString group;
        int level = 0;
        int time = 0;
        int data = -1;
        wxString flags;
        std::vector<int> prereqs;
    };
    std::unordered_map<int, DefRec> defById;

    {
        std::string raw = loadExtracted(commonDirs, "RESEARCH.DEF");
        if (raw.empty()) raw = loadArchiveMember(L"COMMON.FS", "RESEARCH.DEF");
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
                size_t first = line.find_first_not_of(" \t");
                if (first == std::string::npos) continue;
                line = line.substr(first);
                if (line.empty() || line[0] == ';') continue;

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
                if (line == "}")
                {
                    if (curId >= 0) defById[curId] = std::move(cur);
                    inside = false;
                    curId = -1;
                    continue;
                }

                static const std::regex rxGroup(R"(Group\((\w+)\))");
                static const std::regex rxFlags(R"(Flags\((\w+)\))");
                static const std::regex rxLevel(R"(Level\((\d+)\))");
                static const std::regex rxTime(R"(Time\((\d+)\))");
                static const std::regex rxData(R"(Data\(([-]?\d+)\))");
                static const std::regex rxOr(R"(ORconnections\(([^)]+)\))");

                if (std::regex_search(line, m, rxGroup)) cur.group = wxString::FromUTF8(m[1].str());
                if (std::regex_search(line, m, rxFlags)) cur.flags = wxString::FromUTF8(m[1].str());
                if (std::regex_search(line, m, rxLevel)) cur.level = std::stoi(m[1].str());
                if (std::regex_search(line, m, rxTime)) cur.time = std::stoi(m[1].str());
                if (std::regex_search(line, m, rxData)) cur.data = std::stoi(m[1].str());
                if (std::regex_search(line, m, rxOr))
                {
                    std::istringstream argss(m[1].str());
                    std::string tok;
                    while (std::getline(argss, tok, ','))
                    {
                        size_t ts = tok.find_first_not_of(" \t");
                        if (ts == std::string::npos) continue;
                        try { cur.prereqs.push_back(std::stoi(tok.substr(ts))); }
                        catch (...) {}
                    }
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // 2. Item titles from RESEARCH.CZ / fallback RESEARCH.ENG.
    // ----------------------------------------------------------------
    std::vector<wxString> titleByIndex;
    {
        std::string raw;
        for (const char* name : { "RESEARCH.CZ", "RESEARCH.ENG" })
        {
            raw = loadExtracted(commonDirs, name);
            if (raw.empty()) raw = loadArchiveMember(L"COMMON.FS", name);
            if (!raw.empty()) break;
        }
        if (!raw.empty())
        {
            std::istringstream ss(raw);
            std::string line;
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                while (!line.empty() && static_cast<unsigned char>(line.back()) < 0x20)
                    line.pop_back();
                titleByIndex.push_back(DecodeCp895Text(line));
            }
        }
    }

    // ----------------------------------------------------------------
    // 3. BRF / INF texts. Read extracted temp/RESEARCH first, then fill gaps
    //    directly from RESEARCH.FS.  Stage 6 previously depended on a manually
    //    extracted directory, which is why this page could be visually present
    //    yet empty/non-functional in a normal build.
    // ----------------------------------------------------------------
    struct TextRec { wxString inf, brf; };
    std::unordered_map<int, TextRec> textById;
    static const std::regex rxTextFile(R"(^(R(\d{3})|RACES)\.(INF|BRF)$)",
        std::regex_constants::icase);

    auto absorbText = [&](const std::string& fn, const std::string& raw)
        {
            if (raw.empty()) return;
            std::smatch m;
            if (!std::regex_match(fn, m, rxTextFile) || !m[2].matched) return;
            std::string ext = m[3].str();
            for (auto& ch : ext) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const int id = std::atoi(m[2].str().c_str());
            const wxString decoded = DecodeCp895Text(raw);
            if (ext == "INF") textById[id].inf = decoded;
            else              textById[id].brf = decoded;
        };

    for (const auto& researchDir : researchDirs)
    {
        if (!fs::exists(researchDir, ec) || !fs::is_directory(researchDir, ec)) continue;
        for (auto it = fs::directory_iterator(researchDir, ec); it != fs::directory_iterator(); ++it)
        {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string fn = it->path().filename().string();
            if (!std::regex_match(fn, rxTextFile)) continue;
            absorbText(fn, loadFileBin(it->path()));
        }
        if (!textById.empty()) break;
    }

    for (const auto& pth : archiveCandidates(L"RESEARCH.FS"))
    {
        if (pth.empty() || !fs::exists(pth, ec) || fs::is_directory(pth, ec)) continue;
        try
        {
            FSarchive arc(pth.wstring(), FSarchive::Options::NONE);
            for (auto* file : arc.GetFiles())
            {
                if (!file) continue;
                if (!std::regex_match(file->name, rxTextFile)) continue;
                if (file->data.empty()) continue;
                const std::string raw(reinterpret_cast<const char*>(file->data.data()), file->data.size());
                absorbText(file->name, raw);
            }
            break;
        }
        catch (...) {}
    }

    // ----------------------------------------------------------------
    // 4. Build database.  Keep Time(0) entries: they are not research choices,
    //    but they are essential encyclopedia/base-unit records for the Info page.
    // ----------------------------------------------------------------
    m_researchDb.clear();
    std::unordered_set<int> allIds;
    for (const auto& kv : defById) allIds.insert(kv.first);
    for (const auto& kv : textById) allIds.insert(kv.first);

    for (int id : allIds)
    {
        const DefRec* def = defById.count(id) ? &defById[id] : nullptr;
        const TextRec* textRec = textById.count(id) ? &textById[id] : nullptr;
        if (def && def->flags == "Special") continue;

        ResearchItem item;
        item.id = id;
        item.code = wxString::Format("R%03d", id);
        item.title = (id >= 0 && id < static_cast<int>(titleByIndex.size()) && !titleByIndex[id].empty())
            ? titleByIndex[id] : item.code;
        if (textRec) { item.brief = textRec->brf; item.info = textRec->inf; }

        if (def)
        {
            item.group = def->group;
            item.level = def->level;
            item.researchable = def->time > 0;
            item.cost = item.researchable ? std::max(1, def->time) : 0;
            item.flags = def->flags;
            item.data = def->data;
            item.prerequisites = def->prereqs;
        }
        else
        {
            item.researchable = true;
            item.cost = 20;
        }
        m_researchDb.push_back(std::move(item));
    }

    auto groupOrder = [](const wxString& g) -> int {
        if (g == "Global") return 0;
        if (g == "Technologies") return 1;
        if (g == "Upgrades") return 2;
        if (g == "Races") return 3;
        return 4;
    };
    std::sort(m_researchDb.begin(), m_researchDb.end(),
        [&](const ResearchItem& a, const ResearchItem& b)
        {
            const int ga = groupOrder(a.group), gb = groupOrder(b.group);
            if (ga != gb) return ga < gb;
            if (a.level != b.level) return a.level < b.level;
            return a.id < b.id;
        });

    NormalizeResearchSelection();
}

bool StrategicLevelFrame::IsResearchUnlocked(const ResearchItem& item) const
{
    if (item.prerequisites.empty()) return true;
    for (int pre : item.prerequisites)
        if (m_researchCompleted.count(pre) > 0) return true;
    return false;
}

bool StrategicLevelFrame::IsResearchAvailable(const ResearchItem& item) const
{
    if (!item.researchable || item.id < 0) return false;
    if (m_researchCompleted.count(item.id) > 0) return false;
    if (m_gameModeEnabled)
    {
        const int levelNum = StrategicLevelNumberFromPath(m_level.source_path);
        if (levelNum > 0 && item.level > levelNum) return false;
    }
    return IsResearchUnlocked(item);
}

bool StrategicLevelFrame::IsInfoItemVisible(const ResearchItem& item) const
{
    if (!m_gameModeEnabled) return true;
    if (item.id < 0) return false;

    // Player-researchable knowledge moves to Info only after completion.
    if (item.researchable)
        return m_researchCompleted.count(item.id) > 0;

    // Time(0) UnitType/NewUnit records are known through the campaign's
    // SetResearchFlag(unitType) state, rather than through research completion.
    if ((item.flags == "UnitType" || item.flags == "NewUnit") && item.data >= 0)
    {
        if (item.flags == "NewUnit")
            return IsCampaignUnitUnlocked(item.data);
        if (!m_levelResearchFlags.empty())
            return m_levelResearchFlags.count(item.data) > 0;
        if (!item.prerequisites.empty())
            return IsResearchUnlocked(item);
        return true;
    }

    return true;
}

bool StrategicLevelFrame::IsCampaignUnitUnlocked(int unitType) const
{
    if (!m_gameModeEnabled) return true;
    if (unitType < 0) return false;
    if (m_levelResearchFlags.count(unitType) > 0) return true;
    for (const auto& item : m_researchDb)
    {
        if (item.flags != "NewUnit" || item.data != unitType) continue;
        if (item.researchable)
        {
            if (item.id >= 0 && m_researchCompleted.count(item.id) > 0) return true;
        }
        else if (IsResearchUnlocked(item))
            return true;
    }
    return false;
}

void StrategicLevelFrame::NormalizeResearchSelection()
{
    if (m_researchDb.empty())
    {
        m_researchActiveId = -1;
        m_researchActiveIndex = -1;
        m_researchBrowseIndex = -1;
        m_infoBrowseIndex = -1;
        return;
    }

    int resolvedActive = -1;
    if (m_researchActiveId >= 0)
    {
        for (int i = 0; i < static_cast<int>(m_researchDb.size()); ++i)
            if (m_researchDb[static_cast<size_t>(i)].id == m_researchActiveId)
            { resolvedActive = i; break; }
    }
    else if (m_researchActiveIndex >= 0 && m_researchActiveIndex < static_cast<int>(m_researchDb.size()))
    {
        const ResearchItem& old = m_researchDb[static_cast<size_t>(m_researchActiveIndex)];
        if (old.researchable && old.id >= 0)
        {
            resolvedActive = m_researchActiveIndex;
            m_researchActiveId = old.id;
        }
    }

    if (resolvedActive >= 0)
    {
        const ResearchItem& active = m_researchDb[static_cast<size_t>(resolvedActive)];
        if (!active.researchable || m_researchCompleted.count(active.id) > 0)
            resolvedActive = -1;
    }
    m_researchActiveIndex = resolvedActive;
    if (resolvedActive < 0)
    {
        m_researchActiveId = -1;
        m_researchAllocPerTurn = 0;
    }

    if (m_researchBrowseIndex < 0 || m_researchBrowseIndex >= static_cast<int>(m_researchDb.size()) ||
        !IsResearchAvailable(m_researchDb[static_cast<size_t>(m_researchBrowseIndex)]))
    {
        // Native behaviour is friendlier when the browser follows the active
        // project first; only if there is no valid active project do we fall
        // back to the first researchable item in the list.
        m_researchBrowseIndex = -1;
        if (resolvedActive >= 0 && resolvedActive < static_cast<int>(m_researchDb.size()) &&
            IsResearchAvailable(m_researchDb[static_cast<size_t>(resolvedActive)]))
        {
            m_researchBrowseIndex = resolvedActive;
        }
        else
        {
            for (int i = 0; i < static_cast<int>(m_researchDb.size()); ++i)
                if (IsResearchAvailable(m_researchDb[static_cast<size_t>(i)]))
                { m_researchBrowseIndex = i; break; }
        }
    }

    if (m_infoBrowseIndex < 0 || m_infoBrowseIndex >= static_cast<int>(m_researchDb.size()) ||
        !IsInfoItemVisible(m_researchDb[static_cast<size_t>(m_infoBrowseIndex)]))
    {
        m_infoBrowseIndex = -1;
        for (int i = 0; i < static_cast<int>(m_researchDb.size()); ++i)
            if (IsInfoItemVisible(m_researchDb[static_cast<size_t>(i)]))
            { m_infoBrowseIndex = i; break; }
    }
}

bool StrategicLevelFrame::StartResearchIndex(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_researchDb.size())) return false;
    const ResearchItem& item = m_researchDb[static_cast<size_t>(idx)];
    if (!IsResearchAvailable(item)) return false;
    m_researchBrowseIndex = idx;
    m_researchActiveIndex = idx;
    m_researchActiveId = item.id;
    m_researchAllocPerTurn = 1;
    return true;
}

void StrategicLevelFrame::EnterResearchMode()
{
    EnsureResearchLoaded();
    m_researchMode = true;

    // Switch pages: left details + middle list
    if (m_leftBook) m_leftBook->SetSelection(4);
    if (m_midBook)  m_midBook->SetSelection(1);

    // Button keeps its icon - clicking it again will leave research mode

    RefreshResearchUI();
}

void StrategicLevelFrame::LeaveResearchMode()
{
    m_researchMode = false;

    if (m_leftBook) m_leftBook->SetSelection(0);
    if (m_midBook)  m_midBook->SetSelection(0);

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
    EnsureResearchLoaded();
    if (m_researchAllocPerTurn > 0)
        m_researchAllocPerTurn = 0;
    else if (m_researchActiveIndex >= 0 &&
             m_researchActiveIndex < static_cast<int>(m_researchDb.size()) &&
             IsResearchAvailable(m_researchDb[static_cast<size_t>(m_researchActiveIndex)]))
        m_researchAllocPerTurn = 1;
    else
        StartResearchIndex(m_researchBrowseIndex);

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
    struct RGuard { bool& f; ~RGuard() { f = false; } } _rg{ m_researchRefreshing };

    EnsureResearchLoaded();

    NormalizeResearchSelection();

    // ----------------------------------------------------------------
    // Categorized list (wxListCtrl)
    // ItemData: (wxUIntPtr)-1 = group header row (not selectable)
    //           other values  = index into m_researchDb
    // ----------------------------------------------------------------
    static constexpr long kHdrSentinel = -1L;

    if (m_researchList)
    {
        m_researchList->Freeze();
        m_researchList->DeleteAllItems();

        const wxColour clrHeader = m_palette.heading;
        const wxColour clrNormal = m_palette.text;

        wxString lastGroup;
        long row = 0, selRow = -1;

        for (int i = 0; i < (int)m_researchDb.size(); ++i)
        {
            const ResearchItem& it = m_researchDb[i];
            if (!IsResearchAvailable(it))
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

            const bool active = (it.id == m_researchActiveId && m_researchAllocPerTurn > 0);
            wxString label = wxString("  ") + it.title;
            if (active) label += " >";
            m_researchList->InsertItem(row, label);
            m_researchList->SetItemData(row, static_cast<wxUIntPtr>(i));
            m_researchList->SetItemTextColour(row, clrNormal);

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
            && m_researchActiveIndex < (int)m_researchDb.size())
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
            && m_researchActiveIndex < (int)m_researchDb.size())
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

    // Button keeps its icon - clicking it again will leave info mode

    RefreshInfoUI();
}

void StrategicLevelFrame::LeaveInfoMode()
{
    m_infoMode = false;

    if (m_leftBook) m_leftBook->SetSelection(0);
    if (m_midBook)  m_midBook->SetSelection(0);

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
    struct RGuard { bool& f; ~RGuard() { f = false; } } _rg{ m_infoRefreshing };

    EnsureResearchLoaded();

    NormalizeResearchSelection();

    // ----------------------------------------------------------------
    // Categorized list (wxListCtrl)
    // ----------------------------------------------------------------
    static constexpr long kHdrSentinel = -1L;

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
            if (!IsInfoItemVisible(it))
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
    // Research allocation is automatic: when research is active, the current
    // per-turn research allocation advances the active project.  The original
    // UI displays this allocation continuously in the top-right status panel;
    // it is not a bank that gets zeroed after every turn.
    if (m_researchAllocPerTurn <= 0)
        return;

    EnsureResearchLoaded();
    if (m_researchDb.empty() || m_researchActiveIndex < 0
        || m_researchActiveIndex >= (int)m_researchDb.size())
        return;

    const ResearchItem& cur = m_researchDb[m_researchActiveIndex];
    if (!cur.researchable || cur.id < 0 || m_researchCompleted.count(cur.id) || !IsResearchUnlocked(cur))
        return;

    const int spend = std::max(0, m_research);
    if (spend <= 0)
        return;

    const int cost = std::max(1, cur.cost);
    int& prog = m_researchProgressById[cur.id];

    // 3 strategic points produce one research point. Apply that point once;
    // Stage 6 both accumulated it as currency and multiplied it again by two.
    prog += spend;

    if (prog >= cost)
    {
        prog = cost;
        m_researchCompleted.insert(cur.id);
        m_researchAllocPerTurn = 0;
        m_researchActiveId = -1;
        m_researchActiveIndex = -1;
        NormalizeResearchSelection();
    }
}

void StrategicLevelFrame::OnShowStrategicMap(wxCommandEvent&)
{
    if (m_researchMode) LeaveResearchMode();
    if (m_infoMode) LeaveInfoMode();
    if (m_unitsModeActive) LeaveUnitsMode();
    if (m_leftBook)
        m_leftBook->SetSelection(0);
    if (m_midBook)
        m_midBook->SetSelection(0);
}

void StrategicLevelFrame::OnShowHierarchy(wxCommandEvent&)
{
    if (m_researchMode) LeaveResearchMode();
    if (m_infoMode) LeaveInfoMode();
    if (m_unitsModeActive) LeaveUnitsMode();
    if (m_leftBook)
        m_leftBook->SetSelection(1);
    if (m_midBook)
        m_midBook->SetSelection(0);
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
    // Mid-book indices: 0 roster, 1 research, 2 info, 3 resources, 4 stats.
    if (m_midBook)
        m_midBook->SetSelection(3);

    m_overlayDirty = true;
    RefreshResourcesPage();   // fills table + triggers canvas Refresh internally
}

void StrategicLevelFrame::OnShowStats(wxCommandEvent&)
{
    if (m_researchMode) LeaveResearchMode();
    if (m_infoMode) LeaveInfoMode();
    if (m_unitsModeActive) LeaveUnitsMode();
    if (m_leftBook)
        m_leftBook->SetSelection(3);
    if (m_midBook)
        m_midBook->SetSelection(4);

    // Ensure the stats page sees the latest state.
    SaveStrategicState();
    LoadRanksTable();
    LoadMissionStatsIfPresent();
    RecomputePlayerRank();
    RefreshStatsPage();

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

    int dw = 0;
    int dh = 0;
    int ox = 0;
    int oy = 0;
    if (!resourcesView && m_mapChromeBitmap.IsOk())
    {
        const double chromeScale = std::min(
            (double)pw / (double)kMapChromeW,
            (double)ph / (double)kMapChromeH);
        const int chromeW = std::max(1, (int)std::lround(kMapChromeW * chromeScale));
        const int chromeH = std::max(1, (int)std::lround(kMapChromeH * chromeScale));
        const int chromeX = (pw - chromeW) / 2;
        const int chromeY = (ph - chromeH) / 2;
        ox = chromeX + (int)std::lround(kMapViewportX * chromeScale);
        oy = chromeY + (int)std::lround(kMapViewportY * chromeScale);
        dw = std::max(1, (int)std::lround(kMapViewportW * chromeScale));
        dh = std::max(1, (int)std::lround(kMapViewportH * chromeScale));
    }
    else
    {
        const double sx = (double)pw / (double)bw;
        const double sy = (double)ph / (double)bh;
        const double mapScale = std::min(sx, sy);
        dw = std::max(1, (int)std::lround((double)bw * mapScale));
        dh = std::max(1, (int)std::lround((double)bh * mapScale));
        ox = (pw - dw) / 2;
        oy = (ph - dh) / 2;
    }

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

    const unsigned char rawClk = m_clkValues[(size_t)my * (size_t)m_clkW + (size_t)mx];
    if (rawClk == 0)
        return;

    // CLK value can be either:
    // - territory id (matches LevelTerritory::id),
    // - a border pixel encoded as 128 + territory id, or
    // - 1-based region index (1..N) into m_level.territories
    const int decodedClk = rawClk >= 129 ? (int)rawClk - 128 : (int)rawClk;
    int chosenTerritoryId = decodedClk;

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

    // 2) Fallback: treat the decoded value as a 1-based region index.
    const size_t idx = (size_t)decodedClk - 1;
    if (idx < m_level.territories.size())
    {
        const int fallbackId = m_level.territories[idx].id;
        if (!isVisible(fallbackId))
            return;
        if (resourcesView &&
            std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), fallbackId) == m_ownedTerritories.end())
            return;
        SelectTerritoryById(fallbackId);
        return;
    }
    // Out of range -> ignore click
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
    const bool isOwned = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), t.id)
                         != m_ownedTerritories.end();

    // Resolve current mission token for this territory
    std::string cur = t.mission;
    auto itc = m_territoryCurrentMission.find(t.id);
    if (itc != m_territoryCurrentMission.end() && !itc->second.empty())
        cur = itc->second;

    wxString info;

    // --- Game mode: show only the relevant briefing based on territory state ---
    if (m_gameModeEnabled)
    {
        if (t.is_final)
            info << "[FINAL TERRITORY]\n\n";

        // Check if this territory has an active counter-attack
        bool hasCounterAttack = false;
        int counterTurnsLeft = -1;
        for (const auto& ca : m_counterAttacks)
        {
            if (ca.territory_id == t.id && ca.triggered && !ca.completed)
            {
                hasCounterAttack = true;
                break;
            }
            if (ca.territory_id == t.id && !ca.triggered && !ca.completed)
            {
                counterTurnsLeft = ca.trigger_turn - m_turn;
                break;
            }
        }

        // Check timeout
        int timeoutRemaining = GetTerritoryTimeoutRemaining(t.id);

        // Resolve TEXTS dir
        std::filesystem::path texts_dir = FindTextsDirForLevel(m_level, m_spellData);

        if (hasCounterAttack)
        {
            // Counter-attack active: show .S text
            info << "*** COUNTER-ATTACK ***\n\n";
            if (!texts_dir.empty())
                try_append_single_text(info, texts_dir, cur, ".S", "Counter-Attack Briefing");
        }
        else if (!isOwned)
        {
            // Not owned: show briefing only
            if (timeoutRemaining > 0)
                info << wxString::Format("Time remaining: %d turns\n\n", timeoutRemaining);

            if (!texts_dir.empty())
                try_append_single_text(info, texts_dir, cur, "", "Briefing");
        }
        else
        {
            // Owned territory
            info << "Territory secured.\n";

            if (counterTurnsLeft > 0)
                info << wxString::Format("\nCounter-attack expected in %d turns.\n", counterTurnsLeft);
        }
    }
    else
    {
        // --- Editor mode: show all debug info and all text variants ---
        info << wxString::Format("Territory %d\n", t.id);
        info << "Mission: " << t.mission << "\n";
        info << "Intro: " << t.intro_mission << "\n";
        info << "Music: " << t.music << "\n";
        info << wxString::Format("Strategic point: %d,%d\n", t.strategic_x, t.strategic_y);

        if (itc != m_territoryCurrentMission.end())
            info << "Current: " << itc->second << "\n";

        auto itn = m_territoryLaunchCount.find(t.id);
        if (itn != m_territoryLaunchCount.end())
            info << wxString::Format("Played: %d\n", itn->second);

        std::filesystem::path texts_dir = FindTextsDirForLevel(m_level, m_spellData);

        if (!texts_dir.empty())
        {
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
    m_originalBriefingText = info;
    m_originalStrategicDirty = true;
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
    // Exact order/abbreviations from the original HODNOSTI.ENG.
    static const char* kAbbr[] = {
        "2nd Lt.", "1st Lt.", "Cpt.", "Maj.", "Lt. Col.", "Col.", "Bgd. Gen.", "Maj. Gen.", "Gen."
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

    // Original manual/data: newly offered commanders always arrive as
    // nadporucik / 1st Lieutenant. Their later promotions use the
    // actions_required column from HODNOSTI.DEF.
    CommanderRec rec;
    rec.name = name;
    rec.rank = 1;

    m_availableCommanders.push_back(rec);
    m_cmdGenCountInWindow += 1;
}

void StrategicLevelFrame::OnBuyCommander(wxCommandEvent&)
{
    LoadRanksTable();
    RecomputePlayerRank();
    int maxCommanders = 0;
    if (const CommanderRankRec* rank = FindRankRec(m_player.rank))
        maxCommanders = std::clamp(rank->max_commanders, 0, 14);

    if ((int)m_playerCommanders.size() >= maxCommanders)
    {
        wxMessageBox(wxString::Format("Commander limit reached (%d).", maxCommanders),
            "Buy commander", wxOK | wxICON_INFORMATION, this);
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

    // Get selected units for this mission
    std::vector<LevelData::PlayerUnitAdd> unitsForMission = GetSelectedUnitsForLaunch();

    if (unitsForMission.empty())
    {
        wxMessageBox("No units selected for mission.\n\n"
            "Select units in the Units list (click to select, Ctrl+click for multiple)\n"
            "or click a Commander to select all units under them.",
            "Launch", wxOK | wxICON_INFORMATION, this);
        return;
    }

    const int terr_id = m_selectedTerritory;
    std::string token = ResolveMissionTokenForTerritory(terr_id);
    if (token.empty() || token == "none")
        return;

    std::wstring defPath = ResolveMapDefPathForMissionToken(token);

    if (defPath.empty())
    {
        wxMessageBox("Map DEF not found for mission: " + wxString(token), "Launch", wxOK | wxICON_WARNING, this);
        return;
    }
    // Store pending mission for result handling
    m_pendingMission.valid = true;
    m_pendingMission.territory_id = terr_id;
    m_pendingMission.mission_token = token;
    // Record which roster indices are being sent to the mission
    m_pendingMission.sent_unit_indices.clear();
    {
        int uidIndex = 0;
        for (size_t pIdx = 0; pIdx < m_playerUnits.size(); ++pIdx)
        {
            const auto& u = m_playerUnits[pIdx];
            for (int inst = 0; inst < u.count; ++inst)
            {
                if (uidIndex < (int)m_rosterRowUids.size())
                {
                    if (m_selectedUnitsForMission.count(m_rosterRowUids[uidIndex]) > 0)
                        m_pendingMission.sent_unit_indices.push_back(pIdx);
                }
                ++uidIndex;
            }
        }
    }
    
    // Show briefing in game mode
    if (m_gameModeEnabled)
    {
        ShowBriefing(terr_id);
    }
    for (size_t i = 0; i < unitsForMission.size(); ++i)
    {
        const auto& u = unitsForMission[i];
    }

    bool ok = m_main->LoadMapFromDefPath(defPath, unitsForMission);

    if (!ok && !unitsForMission.empty())
    {
        const std::vector<LevelData::PlayerUnitAdd> empty_units;
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

    // IMPORTANT: mission progression happens only after HandleMissionResult().
    // Advancing EndOKMission here used to corrupt the campaign when a mission
    // was failed/aborted and also skipped chained final missions.

    // Clear selection after launch
    m_selectedUnitsForMission.clear();
    m_selectedCommandersForMission.clear();

    // persist progression before leaving the strategic screen
    SaveStrategicState();

    // Apply the resolution selected on the reconstructed strategic Options screen.
    // Do not fight a maximized main window; in that case the platform owns its size.
    if (!m_main->IsMaximized())
    {
        static const std::array<wxSize, 3> kBattleClientSizes = {
            wxSize(640, 480), wxSize(800, 600), wxSize(1024, 768)
        };
        const int resolutionIndex = std::clamp(m_originalBattleResolution, 0, 2);
        m_main->SetClientSize(kBattleClientSizes[static_cast<size_t>(resolutionIndex)]);
    }

    // jump directly into game mode and hide the strategic-level window (keep alive for result callback)
    m_main->SetGameModeUI(true);
    m_main->Raise();

    Hide();
}


void StrategicLevelFrame::OnEndTurn(wxCommandEvent&)
{
    m_turn += 1;

    // Check timeouts (auto-BAD for territories)
    CheckTimeouts();

    // Process Level Events from DEF (counter-attacks, reinforcements, texts, etc.)
    ProcessLevelEvents();

    // Check counter-attacks (legacy territory-based)
    CheckCounterAttacks();
    
    // Update stats
    m_stats.turns_total++;

    ApplyResourceTickEndTurn();


    ApplyResearchTickEndTurn();

    // Pools may have depleted this turn. Clamp the next-turn research allocation
    // only after the current turn's research points have been applied.
    SetGlobalResearchAllocation(m_resourcesGlobalResearch);
    RefreshResourcesPage();

    // Apply unit cooldown ticks (recruit/upgrade completion)
    ApplyUnitsCooldownTick();

    // Commander offers are generated on end-turn (for the *new* turn).
    // Offers do not carry over between turns.
    // Store offer of commanders only for the current turn.
    // m_availableCommanders.clear();
    const std::size_t commanderOffersBefore = m_availableCommanders.size();
    MaybeGenerateCommanderOffer();
    if (m_availableCommanders.size() > commanderOffersBefore)
    {
        wxMessageBox(L"Nov\u00FD d\u016Fstojn\u00EDk p\u0159i\u0161el do gener\u00E1ln\u00EDho \u0161t\u00E1bu.",
            L"Gener\u00E1ln\u00ED \u0161t\u00E1b", wxOK | wxICON_INFORMATION, this);
    }

    SaveStrategicState();
    RefreshUI();
}

static std::filesystem::path GetStrategicStatePath(const LevelData& level)
{
    return GetStrategicSaveDir(level) / "autosave.json";
}

// Find autosave.json from the most recent previous level (LEVEL_N-1, N-2, ... 1)
static std::filesystem::path FindPreviousLevelSavePath(const LevelData& currentLevel)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string stem = fs::path(currentLevel.source_path).stem().string();
    std::regex re(R"([Ll][Ee][Vv][Ee][Ll]_?(\d+))");
    std::smatch m;
    int currentNum = -1;
    if (std::regex_search(stem, m, re) && m.size() >= 2)
        currentNum = std::stoi(m[1].str());
    if (currentNum <= 1)
        return {};
    for (int prev = currentNum - 1; prev >= 1; --prev)
    {
        std::string prevKey = "level_" + std::string(prev < 10 ? "0" : "") + std::to_string(prev);
        fs::path savePath = fs::path(GetStableBaseDir()) / "save" / "strategic" / prevKey / "autosave.json";
        if (fs::exists(savePath, ec))
            return savePath;
    }
    return {};
}

// Extract a JSON array or object value for a given key, properly handling
// nested brackets and quoted strings.  Returns the full block including
// the outer delimiters ("[...]" or "{...}"), or "null", or empty string.
static std::string ExtractJsonBlock(const std::string& data, const char* key)
{
    const std::string needle = std::string("\"" ) + key + "\"";
    size_t pos = data.find(needle);
    if (pos == std::string::npos)
        return {};
    pos = data.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return {};
    ++pos;
    while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos])))
        ++pos;
    if (pos >= data.size())
        return {};
    if (pos + 4 <= data.size() && data.compare(pos, 4, "null") == 0)
        return "null";
    const char open = data[pos];
    char close;
    if (open == '[') close = ']';
    else if (open == '{') close = '}';
    else return {};
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (size_t i = pos; i < data.size(); ++i)
    {
        const char c = data[i];
        if (inStr)
        {
            if (esc) { esc = false; continue; }
            if (c == '\\') { esc = true; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == open) ++depth;
        else if (c == close)
        {
            --depth;
            if (depth == 0)
                return data.substr(pos, i - pos + 1);
        }
    }
    return {};
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

    // Default finite strategic-point state from LEVEL_XX.DEF.
    for (const auto& t : level.territories)
    {
        StrategicLevelFrame::TerritoryResourceState st;
        st.total = std::max(0, t.strategic_points_total);
        st.remaining = st.total;
        st.incomePerTurn = std::max(0, t.strategic_points_per_turn);
        territoryResources[t.id] = st;
    }
    territoryResources[0] = StrategicLevelFrame::TerritoryResourceState{};

    if (out_level_def) out_level_def->clear();
    if (out_timestamp) out_timestamp->clear();

    std::ifstream f(path);
    if (!f)
        return false;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty())
        return false;

    // Normalize newlines to spaces so regex (.*?) can match across lines
    // (ECMAScript '.' does not match \n). JSON string values use escaped \\n,
    // so replacing literal newlines with spaces is safe.
    for (auto& ch : data)
    {
        if (ch == '\n' || ch == '\r')
            ch = ' ';
    }

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

        // Extract "research_state": {...} with proper bracket matching (handles nested {})
        const std::string rsVal = ExtractJsonBlock(data, "research_state");
        {
            if (!rsVal.empty() && rsVal != "null" && rsVal[0] == '{')
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
                    std::regex pair_re("\"(\\d+)\"\\s*:\\s*(-?\\d+)");
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
                    std::regex num_re("(-?\\d+)");
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
    {
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
                if (id < 0) continue;
                if (std::regex_search(obj, mo, std::regex("\\\"total\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                    st.total = std::max(0, std::stoi(mo[1].str()));
                if (std::regex_search(obj, mo, std::regex("\\\"remaining\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                    st.remaining = std::max(0, std::stoi(mo[1].str()));
                const bool hasIncomePerTurn =
                    std::regex_search(obj, mo, std::regex("\\\"income_per_turn\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1;
                if (hasIncomePerTurn)
                    st.incomePerTurn = std::max(0, std::stoi(mo[1].str()));
                if (std::regex_search(obj, mo, std::regex("\\\"research_percent\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                    st.researchPercent = std::clamp(std::stoi(mo[1].str()), 0, 100);
                if (std::regex_search(obj, mo, std::regex("\\\"alloc_accum\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                    st.allocAccum = std::clamp(std::stoi(mo[1].str()), 0, 99);
                if (std::regex_search(obj, mo, std::regex("\\\"research_carry\\\"\\s*:\\s*(-?\\d+)")) && mo.size() > 1)
                    st.researchCarry = id == 0 ? std::max(0, std::stoi(mo[1].str()))
                                               : std::clamp(std::stoi(mo[1].str()), 0, 3);
                if (id > 0)
                {
                    for (const auto& t : level.territories)
                    {
                        if (t.id != id) continue;
                        // Stage 6 originally saved a synthetic 20/20 pool and did not
                        // persist income_per_turn at all. Such saves cannot describe
                        // the real Spellcross economy, so rebuild those legacy pools
                        // from DefineStrategicPoints() instead of preserving bad data.
                        if (!hasIncomePerTurn)
                        {
                            st.total = std::max(0, t.strategic_points_total);
                            st.remaining = st.total;
                            st.incomePerTurn = std::max(0, t.strategic_points_per_turn);
                        }
                        else
                        {
                            if (st.total <= 0 && t.strategic_points_total > 0)
                                st.total = t.strategic_points_total;
                            if (st.incomePerTurn <= 0)
                                st.incomePerTurn = std::max(0, t.strategic_points_per_turn);
                            if (st.remaining > st.total && st.total > 0)
                                st.remaining = st.total;
                        }
                        break;
                    }
                }
                territoryResources[id] = st;
            }
        }
    }

    // mission-flow runtime state (optional/backward compatible)
    if (g_missionFlowPersistLoad)
    {
        if (g_missionFlowPersistLoad->timeoutTurn) g_missionFlowPersistLoad->timeoutTurn->clear();
        if (g_missionFlowPersistLoad->triggeredEvents) g_missionFlowPersistLoad->triggeredEvents->clear();
        if (g_missionFlowPersistLoad->activatedEvents) g_missionFlowPersistLoad->activatedEvents->clear();
        if (g_missionFlowPersistLoad->counterAttacks) g_missionFlowPersistLoad->counterAttacks->clear();

        const std::string mf = ExtractJsonBlock(data, "mission_flow");
        if (!mf.empty() && mf != "null")
        {
            if (g_missionFlowPersistLoad->timeoutTurn)
            {
                const std::string obj = ExtractJsonBlock(mf, "timeouts");
                std::regex pair_re("\"(\\d+)\"\\s*:\\s*(-?\\d+)");
                for (auto it = std::sregex_iterator(obj.begin(), obj.end(), pair_re); it != std::sregex_iterator(); ++it)
                    (*g_missionFlowPersistLoad->timeoutTurn)[std::stoi((*it)[1].str())] = std::stoi((*it)[2].str());
            }
            if (g_missionFlowPersistLoad->triggeredEvents)
            {
                const std::string arr = ExtractJsonBlock(mf, "triggered_events");
                std::regex num_re("(-?\\d+)");
                for (auto it = std::sregex_iterator(arr.begin(), arr.end(), num_re); it != std::sregex_iterator(); ++it)
                    g_missionFlowPersistLoad->triggeredEvents->insert(std::stoi((*it)[1].str()));
            }
            if (g_missionFlowPersistLoad->activatedEvents)
            {
                const std::string obj = ExtractJsonBlock(mf, "activated_events");
                std::regex pair_re("\"(\\d+)\"\\s*:\\s*(-?\\d+)");
                for (auto it = std::sregex_iterator(obj.begin(), obj.end(), pair_re); it != std::sregex_iterator(); ++it)
                    (*g_missionFlowPersistLoad->activatedEvents)[std::stoi((*it)[1].str())] = std::stoi((*it)[2].str());
            }
            if (g_missionFlowPersistLoad->counterAttacks)
            {
                const std::string arr = ExtractJsonBlock(mf, "counter_attacks");
                std::regex obj_re("\\{[^\\}]*\\}");
                for (auto it = std::sregex_iterator(arr.begin(), arr.end(), obj_re); it != std::sregex_iterator(); ++it)
                {
                    const std::string obj = (*it)[0].str();
                    StrategicLevelFrame::CounterAttackState ca;
                    (void)ParseJsonIntField(obj, "territory_id", ca.territory_id);
                    (void)ParseJsonIntField(obj, "conquest_turn", ca.conquest_turn);
                    (void)ParseJsonIntField(obj, "trigger_turn", ca.trigger_turn);
                    (void)ParseJsonStringField(obj, "counter_mission", ca.counter_mission);
                    ca.triggered = obj.find("\"triggered\":true") != std::string::npos || obj.find("\"triggered\": true") != std::string::npos;
                    ca.completed = obj.find("\"completed\":true") != std::string::npos || obj.find("\"completed\": true") != std::string::npos;
                    if (ca.territory_id > 0) g_missionFlowPersistLoad->counterAttacks->push_back(std::move(ca));
                }
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

    // units list – parse each unit object individually to also extract state fields
    {
        // Extract "units": [...] with proper bracket matching (handles nested [] in upgrades)
        std::string unitsBlock = ExtractJsonBlock(data, "units");
        std::string unitsArr;
        if (unitsBlock.size() >= 2 && unitsBlock.front() == '[' && unitsBlock.back() == ']')
            unitsArr = unitsBlock.substr(1, unitsBlock.size() - 2);

        if (!unitsArr.empty())
        {
            std::regex obj_re("\\{[^\\}]*\\}");
            for (auto it = std::sregex_iterator(unitsArr.begin(), unitsArr.end(), obj_re); it != std::sregex_iterator(); ++it)
            {
                const std::string obj = (*it)[0].str();
                int uid = -1, cnt = 0, hp = 100;
                if (!ParseJsonIntField(obj, "unit_id", uid) || uid < 0)
                    continue;
                (void)ParseJsonIntField(obj, "count", cnt);
                (void)ParseJsonIntField(obj, "health", hp);

                LevelData::PlayerUnitAdd entry;
                entry.unit_id = uid;
                entry.count = cnt;
                entry.health = hp;
                entry.extra = "-";
                units.push_back(entry);

                // Parse unit instance state (optional fields)
                if (g_unitStatePersistLoad && g_unitStatePersistLoad->states)
                {
                    StrategicLevelFrame::UnitInstanceState st;
                    (void)ParseJsonIntField(obj, "cooldown", st.cooldown_turns);
                    (void)ParseJsonIntField(obj, "experience", st.experience);
                    (void)ParseJsonIntField(obj, "level", st.level);
                    (void)ParseJsonStringField(obj, "custom_name", st.custom_name);

                    // Parse upgrades array: "upgrades": [1, 3, 5]
                    std::smatch mmUpg;
                    std::regex upg_re(R"("upgrades"\s*:\s*\[(.*?)\])");
                    if (std::regex_search(obj, mmUpg, upg_re) && mmUpg.size() > 1)
                    {
                        const std::string uArr = mmUpg[1].str();
                        std::regex num_re("(-?\\d+)");
                        for (auto uit = std::sregex_iterator(uArr.begin(), uArr.end(), num_re); uit != std::sregex_iterator(); ++uit)
                            st.upgrades.push_back(std::stoi((*uit)[1].str()));
                    }

                    g_unitStatePersistLoad->states->push_back(std::move(st));
                }
            }
        }
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
            f << "\"" << kv.first << "\": " << kv.second;
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

    // Resources state.  id=0 stores the global research allocation; positive
    // IDs store the finite strategic-point pools for each territory.
    f << "  \"resources\": [\n";
    {
        const auto mit = territoryResources.find(0);
        const auto meta = mit != territoryResources.end()
            ? mit->second : StrategicLevelFrame::TerritoryResourceState{};
        f << "    {\"id\": 0, \"total\": 0, \"remaining\": 0, \"income_per_turn\": 0"
          << ", \"research_percent\": 0, \"alloc_accum\": 0"
          << ", \"research_carry\": " << meta.researchCarry << "}";
        if (!level.territories.empty()) f << ",";
        f << "\n";
    }
    for (size_t i = 0; i < level.territories.size(); ++i)
    {
        const int tid = level.territories[i].id;
        auto it = territoryResources.find(tid);
        auto st = (it != territoryResources.end()) ? it->second : StrategicLevelFrame::TerritoryResourceState{};
        if (st.total <= 0 && level.territories[i].strategic_points_total > 0)
        {
            st.total = level.territories[i].strategic_points_total;
            if (st.remaining <= 0) st.remaining = st.total;
        }
        if (st.incomePerTurn <= 0)
            st.incomePerTurn = std::max(0, level.territories[i].strategic_points_per_turn);
        f << "    {\"id\": " << tid
            << ", \"total\": " << st.total
            << ", \"remaining\": " << st.remaining
            << ", \"income_per_turn\": " << st.incomePerTurn
            << ", \"research_percent\": " << st.researchPercent
            << ", \"alloc_accum\": " << st.allocAccum
            << ", \"research_carry\": " << st.researchCarry
            << "}";
        if (i + 1 < level.territories.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n";

    // Mission-flow runtime state. Deadlines are absolute strategic turns so
    // saving/loading cannot reset a timed mission.
    if (g_missionFlowPersistSave && g_missionFlowPersistSave->timeoutTurn &&
        g_missionFlowPersistSave->triggeredEvents && g_missionFlowPersistSave->activatedEvents &&
        g_missionFlowPersistSave->counterAttacks)
    {
        f << "  \"mission_flow\": {\n";
        f << "    \"timeouts\": {";
        bool firstT = true;
        for (const auto& kv : *g_missionFlowPersistSave->timeoutTurn)
        {
            if (!firstT) f << ", "; firstT = false;
            f << "\"" << kv.first << "\": " << kv.second;
        }
        f << "},\n";
        f << "    \"triggered_events\": [";
        bool firstE = true;
        for (int id : *g_missionFlowPersistSave->triggeredEvents)
        {
            if (!firstE) f << ", "; firstE = false; f << id;
        }
        f << "],\n";
        f << "    \"activated_events\": {";
        bool firstA = true;
        for (const auto& kv : *g_missionFlowPersistSave->activatedEvents)
        {
            if (!firstA) f << ", "; firstA = false;
            f << "\"" << kv.first << "\": " << kv.second;
        }
        f << "},\n";
        f << "    \"counter_attacks\": [";
        bool firstC = true;
        for (const auto& ca : *g_missionFlowPersistSave->counterAttacks)
        {
            if (!firstC) f << ", "; firstC = false;
            f << "{\"territory_id\":" << ca.territory_id
              << ",\"conquest_turn\":" << ca.conquest_turn
              << ",\"trigger_turn\":" << ca.trigger_turn
              << ",\"counter_mission\":\"" << EscapeJson(ca.counter_mission) << "\""
              << ",\"triggered\":" << (ca.triggered ? "true" : "false")
              << ",\"completed\":" << (ca.completed ? "true" : "false") << "}";
        }
        f << "]\n";
        f << "  },\n";
    }
    else
    {
        f << "  \"mission_flow\": null,\n";
    }

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

    // units (with per-unit instance state if available)
    f << "  \"units\": [\n";
    for (size_t i = 0; i < units.size(); ++i)
    {
        const auto& u = units[i];
        f << "    {\"unit_id\": " << u.unit_id << ", \"count\": " << u.count << ", \"health\": " << u.health;

        // Write unit instance state (cooldown, experience, level, upgrades, custom_name)
        if (g_unitStatePersistSave && g_unitStatePersistSave->states && i < g_unitStatePersistSave->states->size())
        {
            const auto& st = (*g_unitStatePersistSave->states)[i];
            f << ", \"cooldown\": " << st.cooldown_turns;
            f << ", \"experience\": " << st.experience;
            f << ", \"level\": " << st.level;
            if (!st.custom_name.empty())
                f << ", \"custom_name\": \"" << EscapeJson(st.custom_name) << "\"";
            if (!st.upgrades.empty())
            {
                f << ", \"upgrades\": [";
                for (size_t ui = 0; ui < st.upgrades.size(); ++ui)
                {
                    f << st.upgrades[ui];
                    if (ui + 1 < st.upgrades.size()) f << ", ";
                }
                f << "]";
            }
        }

        f << "}";
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

    // Hook unit state persistence
    std::vector<UnitInstanceState> loadedUnitStates;
    UnitStatePersistLoadView ulv;
    ulv.states = &loadedUnitStates;
    UnitStatePersistLoadView* prevU = g_unitStatePersistLoad;
    g_unitStatePersistLoad = &ulv;

    MissionFlowPersistLoadView mflv;
    mflv.timeoutTurn = &m_territoryTimeoutTurn;
    mflv.triggeredEvents = &m_triggeredLevelEvents;
    mflv.activatedEvents = &m_activatedEvents;
    mflv.counterAttacks = &m_counterAttacks;
    MissionFlowPersistLoadView* prevMF = g_missionFlowPersistLoad;
    g_missionFlowPersistLoad = &mflv;

    const bool ok = LoadStrategicStateFile(path, m_level, turn, money, research, selected, player, terrM, terrL, units,
        playerCmds, availCmds, windowStart, genCount,
        gm, owned, terrRes,
        &level_def, &ts);
    g_missionFlowPersistLoad = prevMF;
    g_unitStatePersistLoad = prevU;
    g_researchPersistLoad = prevR;

    if (ok)
    {
        // Validate that this save matches current level (compare stem)
        const std::string curStem = to_lower(std::filesystem::path(m_level.source_path).stem().string());
        const std::string saveStem = to_lower(std::filesystem::path(level_def).stem().string());

        if (!curStem.empty() && !saveStem.empty() && curStem != saveStem)
        {
            return; // keep defaults initialized in ctor
        }

        m_turn = turn;
        m_money = money;
        m_research = research;
        m_selectedTerritory = selected;
        m_player = player;
        // Rank is derived data.  Recompute it from the canonical player-XP
        // thresholds so old saves made by the broken actions+XP logic repair
        // themselves immediately after loading.
        RecomputePlayerRank();
        m_territoryCurrentMission = std::move(terrM);
        m_territoryLaunchCount = std::move(terrL);
        m_playerUnits = std::move(units);
        m_unitStates = std::move(loadedUnitStates);
        m_playerCommanders = std::move(playerCmds);
        m_availableCommanders = std::move(availCmds);
        m_cmdGenWindowStartTurn = windowStart;
        m_cmdGenCountInWindow = genCount;

        m_gameModeEnabled = gm;
        m_ownedTerritories = std::move(owned);

        // Repair saves produced by the old missing-TEXTS bug. It classified
        // every territory as a starting territory; such a fresh state has all
        // territories owned but no territory mission has ever been launched.
        bool noMissionWasLaunched = true;
        for (const auto& kv : m_territoryLaunchCount)
        {
            if (kv.second > 0)
            {
                noMissionWasLaunched = false;
                break;
            }
        }
        bool ownsEveryTerritory = !m_level.territories.empty();
        for (const auto& territory : m_level.territories)
        {
            if (std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), territory.id) == m_ownedTerritories.end())
            {
                ownsEveryTerritory = false;
                break;
            }
        }
        if (noMissionWasLaunched && ownsEveryTerritory)
        {
            const std::vector<int> expectedStart = ChooseStartTerritories_NoBriefing(m_level, m_spellData);
            if (!expectedStart.empty() && expectedStart.size() < m_ownedTerritories.size())
                m_ownedTerritories = expectedStart;
        }

        m_territoryResources = std::move(terrRes);
        // Backfill missing territories from the canonical LEVEL_XX.DEF economy.
        for (const auto& tt : m_level.territories)
        {
            if (m_territoryResources.find(tt.id) == m_territoryResources.end())
            {
                TerritoryResourceState st;
                st.total = std::max(0, tt.strategic_points_total);
                st.remaining = st.total;
                st.incomePerTurn = std::max(0, tt.strategic_points_per_turn);
                m_territoryResources[tt.id] = st;
            }
        }
        // Restore the global allocation immediately.  Previously it was only
        // copied out of the id=0 meta record after opening the Resources page,
        // so ending a turn straight after loading silently routed everything to money.
        const auto metaIt = m_territoryResources.find(kResourcesMetaTerritoryId);
        m_resourcesGlobalResearch = metaIt != m_territoryResources.end()
            ? std::max(0, metaIt->second.researchCarry) : 0;
        SetGlobalResearchAllocation(m_resourcesGlobalResearch);

        // Ensure start territory when loading older saves / empty campaign state.
        if (m_gameModeEnabled && m_ownedTerritories.empty())
            m_ownedTerritories.push_back(PickStartTerritoryIdForGameMode(m_level, m_spellData));

        if (GetMenuBar())
        {
            auto* item = GetMenuBar()->FindItem(ID_MENU_GAME_MODE_TOGGLE);
            if (item) item->Check(m_gameModeEnabled);
        }
        if (m_selectedTerritory >= 0)
            SelectTerritoryById(m_selectedTerritory);
    }

}

void StrategicLevelFrame::LoadPlayerStateFromPreviousLevel()
{
    const auto prevSave = FindPreviousLevelSavePath(m_level);
    if (prevSave.empty())
    {
        wxMessageBox("No previous level save found.", "Continue campaign", wxOK | wxICON_WARNING, this);
        return;
    }

    // Load previous level save into temporary variables
    int turn = 1, money = 0, research = 0, selected = -1;
    PlayerProgress player{};
    std::unordered_map<int, std::string> terrM;
    std::unordered_map<int, int> terrL;
    std::vector<LevelData::PlayerUnitAdd> units;
    std::vector<CommanderRec> playerCmds;
    std::vector<CommanderRec> availCmds;
    int windowStart = 1, genCount = 0;
    bool gm = false;
    std::vector<int> owned;
    std::unordered_map<int, TerritoryResourceState> terrRes;
    std::string level_def, ts;

    // Hook research persistence
    int resActiveId = -1, resActiveIndex = -1, resAllocPerTurn = 0;
    std::unordered_map<int, int> resProgressById;
    std::unordered_set<int> resCompleted;

    ResearchPersistLoadView rlv;
    rlv.activeId = &resActiveId;
    rlv.activeIndex = &resActiveIndex;
    rlv.allocPerTurn = &resAllocPerTurn;
    rlv.progressById = &resProgressById;
    rlv.completed = &resCompleted;
    ResearchPersistLoadView* prevR = g_researchPersistLoad;
    g_researchPersistLoad = &rlv;

    // Hook unit state persistence
    std::vector<UnitInstanceState> loadedUnitStates;
    UnitStatePersistLoadView ulv;
    ulv.states = &loadedUnitStates;
    UnitStatePersistLoadView* prevU = g_unitStatePersistLoad;
    g_unitStatePersistLoad = &ulv;

    const bool ok = LoadStrategicStateFile(prevSave, m_level, turn, money, research, selected, player,
        terrM, terrL, units, playerCmds, availCmds, windowStart, genCount,
        gm, owned, terrRes, &level_def, &ts);

    g_unitStatePersistLoad = prevU;
    g_researchPersistLoad = prevR;

    if (!ok)
    {
        wxMessageBox("Failed to load previous level save.", "Continue campaign", wxOK | wxICON_ERROR, this);
        return;
    }

    // --- Transfer player state (keep current level's territory/mission defaults) ---
    m_money = money;
    m_research = research;
    m_player = player;
    // Never carry a stale serialized rank into a new chapter.
    RecomputePlayerRank();
    m_playerUnits = std::move(units);
    m_unitStates = std::move(loadedUnitStates);
    m_playerCommanders = std::move(playerCmds);
    m_availableCommanders.clear(); // pending offers don't carry over

    // Transfer research state
    m_researchActiveId = resActiveId;
    m_researchActiveIndex = resActiveIndex;
    m_researchAllocPerTurn = resAllocPerTurn;
    m_researchProgressById = std::move(resProgressById);
    m_researchCompleted = std::move(resCompleted);

    // Enable game mode (campaign = game mode)
    m_gameModeEnabled = true;
    if (GetMenuBar())
    {
        auto* item = GetMenuBar()->FindItem(ID_MENU_GAME_MODE_TOGGLE);
        if (item) item->Check(m_gameModeEnabled);
    }

    // Ensure at least one owned territory (start territory for the new level)
    if (m_ownedTerritories.empty())
        m_ownedTerritories = ChooseStartTerritories_NoBriefing(m_level, m_spellData);
    if (m_ownedTerritories.empty() && !m_level.territories.empty())
        m_ownedTerritories.push_back(m_level.territories.front().id);

    // Add start_units from the new level (bonus units the player receives at level start)
    for (const auto& su : m_level.start_units)
        m_playerUnits.push_back(su);

    // Ensure unit states cover all roster entries (newly added start units need state)
    uint32_t maxUnitUid = 0;
    for (const auto& state : m_unitStates)
        maxUnitUid = std::max(maxUnitUid, state.uid);
    m_nextRosterUid = std::max<uint32_t>(1, maxUnitUid + 1);
    while (m_unitStates.size() < m_playerUnits.size())
    {
        UnitInstanceState state;
        state.uid = m_nextRosterUid++;
        m_unitStates.push_back(state);
    }

    uint32_t maxCommanderUid = 0;
    for (const auto& commander : m_playerCommanders)
        maxCommanderUid = std::max(maxCommanderUid, commander.uid);
    m_nextCommanderUid = std::max<uint32_t>(1, maxCommanderUid + 1);

    // Load cumulative research flags for the new level (game mode unit filtering)
    {
        namespace fs = std::filesystem;
        fs::path defPath = fs::path(m_level.source_path);
        m_levelResearchFlags = GetCumulativeResearchFlags(defPath);
    }

    // Load all-time loss stats from previous level's strategic_stats.json (if accessible)
    // (level-scope stats start fresh for the new level)
    {
        namespace fs = std::filesystem;
        std::error_code ec2;
        fs::path prevDir = prevSave.parent_path().parent_path().parent_path(); // up from save/strategic/level_XX/
        // Try common location for stats (same as FindStrategicStatsPath logic)
        fs::path prevLevelDef = fs::path(level_def);
        fs::path statsDir = prevLevelDef.parent_path();
        if (statsDir.empty() || !fs::exists(statsDir, ec2))
            statsDir = fs::current_path(ec2);
        fs::path statsPath = statsDir / "strategic_stats.json";
        if (fs::exists(statsPath, ec2))
        {
            std::ifstream sf(statsPath);
            if (sf)
            {
                std::string sdata((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
                auto extractBlock = [&](const char* key, LossBlock& out)
                {
                    std::regex re(std::string("\"") + key + "\"\\s*:\\s*\\{([^}]*)\\}");
                    std::smatch sm;
                    if (std::regex_search(sdata, sm, re) && sm.size() >= 2)
                    {
                        (void)ParseJsonIntField(sm[1].str(), "light", out.light);
                        (void)ParseJsonIntField(sm[1].str(), "heavy", out.heavy);
                        (void)ParseJsonIntField(sm[1].str(), "air", out.air);
                        (void)ParseJsonIntField(sm[1].str(), "commanders", out.commanders);
                    }
                };
                extractBlock("all_alliance", m_lossStats.alliance_all);
                extractBlock("all_enemy", m_lossStats.enemy_all);
                // Level-scope stats start fresh
                m_lossStats.alliance_level = {};
                m_lossStats.enemy_level = {};

                (void)ParseJsonIntField(sdata, "missions_completed", m_stats.missions_completed);
                (void)ParseJsonIntField(sdata, "missions_failed", m_stats.missions_failed);
                (void)ParseJsonIntField(sdata, "territories_conquered", m_stats.territories_conquered);
                (void)ParseJsonIntField(sdata, "territories_lost", m_stats.territories_lost);
                (void)ParseJsonIntField(sdata, "turns_total", m_stats.turns_total);
            }
        }
    }

    // Persist the merged state immediately
    SaveStrategicState();
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

    UnitStatePersistSaveView usv;
    usv.states = &m_unitStates;
    const UnitStatePersistSaveView* prevU = g_unitStatePersistSave;
    g_unitStatePersistSave = &usv;

    MissionFlowPersistSaveView mfsv;
    mfsv.timeoutTurn = &m_territoryTimeoutTurn;
    mfsv.triggeredEvents = &m_triggeredLevelEvents;
    mfsv.activatedEvents = &m_activatedEvents;
    mfsv.counterAttacks = &m_counterAttacks;
    const MissionFlowPersistSaveView* prevMF = g_missionFlowPersistSave;
    g_missionFlowPersistSave = &mfsv;

    SaveStrategicStateFile(
        path, m_level, m_turn, m_money, m_research, m_selectedTerritory,
        m_player, m_territoryCurrentMission, m_territoryLaunchCount, m_playerUnits,
        m_playerCommanders, m_availableCommanders, m_cmdGenWindowStartTurn, m_cmdGenCountInWindow,
        m_gameModeEnabled, m_ownedTerritories, m_territoryResources,
        NowIsoLocal());

    g_missionFlowPersistSave = prevMF;
    g_unitStatePersistSave = prevU;
    g_researchPersistSave = prev;
}

bool StrategicLevelFrame::ExportBattleSaveContext(std::string& level_def_path,
    std::string& strategic_state_json, PendingMissionResult& pending) const
{
    // A tactical save is a snapshot taken *inside* a mission.  Persist the
    // strategic side first, then embed those exact bytes into the battle save.
    // This is intentionally not just a filename link: loading an older battle
    // save must roll the strategic campaign back to the matching point in time.
    SaveStrategicState();

    level_def_path = m_level.source_path;
    pending = m_pendingMission;

    const auto path = GetStrategicStatePath(m_level);
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    strategic_state_json.assign(std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>());
    return !strategic_state_json.empty();
}

bool StrategicLevelFrame::ImportBattleSaveContext(const std::string& strategic_state_json,
    const PendingMissionResult& pending)
{
    if (strategic_state_json.empty())
        return false;

    const auto path = GetStrategicStatePath(m_level);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;

    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f.write(strategic_state_json.data(), static_cast<std::streamsize>(strategic_state_json.size()));
        if (!f)
            return false;
    }

    LoadStrategicState();
    m_pendingMission = pending;
    return true;
}

bool StrategicLevelFrame::HasStrategicAutosave() const
{
    std::error_code ec;
    const auto path = GetStrategicStatePath(m_level);
    return std::filesystem::exists(path, ec) && !ec && std::filesystem::is_regular_file(path, ec);
}

bool StrategicLevelFrame::RecoverPendingMissionFromLoadedBattle(const std::string& mission_token)
{
    if (mission_token.empty())
        return false;

    const std::string token_up = to_upper(mission_token);
    int territory_id = -1;

    auto matches = [&](const std::string& candidate) -> bool
    {
        if (candidate.empty() || to_lower(candidate) == "none")
            return false;
        const std::string c = to_upper(candidate);
        return token_up == c || token_up.rfind(c, 0) == 0 || c.rfind(token_up, 0) == 0;
    };

    // Prefer the live mission state from the strategic autosave; fall back to
    // canonical territory mission/intro tokens for legacy saves.
    for (const auto& t : m_level.territories)
    {
        auto it = m_territoryCurrentMission.find(t.id);
        if (it != m_territoryCurrentMission.end() && matches(it->second))
        {
            territory_id = t.id;
            break;
        }
        if (matches(t.mission) || matches(t.intro_mission))
        {
            territory_id = t.id;
            break;
        }
    }
    if (territory_id < 0)
        return false;

    PendingMissionResult recovered;
    recovered.valid = true;
    recovered.territory_id = territory_id;
    recovered.mission_token = mission_token;

    // Old .scsave files did not store the sent-roster mapping.  Reconstruct it
    // from tactical Alliance unit types and the strategic roster.  This is a
    // best-effort compatibility path only; new saves embed the exact mapping.
    SpellMap* tactical = m_main ? m_main->GetSpellMap() : nullptr;
    if (tactical && tactical->IsLoaded())
    {
        std::unordered_map<int, int> needed;
        for (auto* u : tactical->units)
        {
            if (!u || !u->unit || u->is_enemy)
                continue;
            needed[u->unit->type_id] += 1;
        }

        for (size_t i = 0; i < m_playerUnits.size(); ++i)
        {
            auto& n = needed[m_playerUnits[i].unit_id];
            const int take = std::min(std::max(0, m_playerUnits[i].count), n);
            for (int k = 0; k < take; ++k)
                recovered.sent_unit_indices.push_back(i);
            n -= take;
        }
    }

    m_pendingMission = std::move(recovered);
    return true;
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

static bool LoadSSDAdjacencyFromArchive(SpellData* spellData, int levelNum,
    int territoryMaxId, std::vector<uint32_t>& outAdj)
{
    outAdj.assign(std::max(territoryMaxId + 1, 1), 0u);
    if (!spellData || !spellData->GetCommonFS())
        return false;

    const std::string ssdName = wxString::Format("LEVEL_%02d.SSD", levelNum).ToStdString();
    uint8_t* raw = nullptr;
    int rawSize = 0;
    if (spellData->GetCommonFS()->GetFile(ssdName.c_str(), &raw, &rawSize) ||
        !raw || rawSize < 128)
        return false;

    constexpr int count = 32;
    for (int t = 1; t <= territoryMaxId && t < count; ++t)
    {
        const size_t o = (size_t)t * 4;
        outAdj[t] =
            (uint32_t)raw[o + 0] |
            ((uint32_t)raw[o + 1] << 8) |
            ((uint32_t)raw[o + 2] << 16) |
            ((uint32_t)raw[o + 3] << 24);
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

static bool BuildStrategicScreenBitmap(SpellData* spellData, const char* resourceName, wxBitmap& outBmp)
{
    outBmp = wxBitmap();
    if (!spellData || !spellData->GetCommonFS() || !resourceName || !*resourceName)
        return false;

    uint8_t* raw = nullptr;
    int rawSize = 0;
    if (spellData->GetCommonFS()->GetFile(resourceName, &raw, &rawSize) ||
        !raw || rawSize < kStrategicScreenW * kStrategicScreenH)
        return false;

    wxImage img(kStrategicScreenW, kStrategicScreenH, true);
    img.InitAlpha();
    for (int y = 0; y < kStrategicScreenH; ++y)
    {
        for (int x = 0; x < kStrategicScreenW; ++x)
        {
            const uint8_t idx = raw[(size_t)y * kStrategicScreenW + (size_t)x];
            img.SetRGB(x, y,
                spellData->strategy_pal[idx][0],
                spellData->strategy_pal[idx][1],
                spellData->strategy_pal[idx][2]);
            img.SetAlpha(x, y, 255);
        }
    }

    // VM*_FULL contains the outer 575x480 silhouette.  The original game then
    // blitted one or more screen-specific raw layers into that silhouette.  In
    // particular, these are the detailed green grids, pipework and lower
    // frames visible in the DOS version; treating *_FULL as a complete screen
    // leaves most of the interface black.
    auto blitLayer = [&](const char* layerName, int layerW, int layerH,
        int destX, int destY) -> bool
        {
            uint8_t* pixels = nullptr;
            int pixelCount = 0;
            if (spellData->GetCommonFS()->GetFile(layerName, &pixels, &pixelCount) ||
                !pixels || pixelCount < layerW * layerH)
                return false;

            for (int y = 0; y < layerH; ++y)
            {
                const int outY = destY + y;
                if (outY < 0 || outY >= kStrategicScreenH)
                    continue;
                for (int x = 0; x < layerW; ++x)
                {
                    const int outX = destX + x;
                    if (outX < 0 || outX >= kStrategicScreenW)
                        continue;

                    const uint8_t idx = pixels[(size_t)y * layerW + x];
                    img.SetRGB(outX, outY,
                        spellData->strategy_pal[idx][0],
                        spellData->strategy_pal[idx][1],
                        spellData->strategy_pal[idx][2]);
                    img.SetAlpha(outX, outY, 255);
                }
            }
            return true;
        };

    const std::string screenName(resourceName);
    if (screenName == "VMM_FULL.LZ")
    {
        // Exact matches in the original 640x480 screen: the briefing frame
        // starts at 6,298 and the bottom action strip at 412,434.
        blitLayer("VMM_LST2.LZ", 406, 174, 6, 298);
        blitLayer("VMM_LST1.LZ", 163, 41, 412, 434);
    }
    else if (screenName == "VMH_FULL.LZ")
    {
        blitLayer("HIERARCH.LZ", 406, 464, 6, 8);
    }
    else if (screenName == "VMU_FULL.LZ")
    {
        blitLayer("UNITS.LZ", 406, 464, 6, 8);
        // Positions measured by matching the original layers against a native
        // 640x480 screenshot (not guessed from neighbouring frames).
        blitLayer("VMU_LST2.LZ", 241, 141, 334, 291);
        blitLayer("VMU_LST1.LZ", 154, 41, 421, 434);
    }
    else if (screenName == "VMB_FULL.LZ")
    {
        blitLayer("BUY.LZ", 406, 464, 6, 8);
        blitLayer("VMB_LST2.LZ", 241, 141, 334, 292);
        blitLayer("VMB_LST1.LZ", 163, 41, 412, 434);
    }
    else if (screenName == "VMR_FULL.LZ")
    {
        blitLayer("RSRCH_BG.LZ", 406, 464, 6, 8);
        blitLayer("VMR_LST1.LZ", 120, 40, 277, 431);
    }
    else if (screenName == "VMI_FULL.LZ")
    {
        blitLayer("INFO.LZ", 412, 464, 0, 8);
    }
    else if (screenName == "VMF_FULL.LZ")
    {
        blitLayer("FACTORY.LZ", 569, 464, 3, 8);
    }
    else if (screenName == "VMS_FULL.LZ")
    {
        blitLayer("STATS.LZ", 569, 464, 3, 8);
    }
    else if (screenName == "VMO_FULL.LZ")
    {
        blitLayer("OPTIONS.LZ", 569, 464, 3, 8);
    }

    outBmp = wxBitmap(img);
    return outBmp.IsOk();
}

static bool BindStrategicScreenSlice(wxPanel* panel, SpellData* spellData,
    const char* resourceName, const wxRect& sourceRect)
{
    if (!panel || sourceRect.width <= 0 || sourceRect.height <= 0)
        return false;

    wxBitmap full;
    if (!BuildStrategicScreenBitmap(spellData, resourceName, full) || !full.IsOk())
        return false;

    const wxRect bounds(0, 0, full.GetWidth(), full.GetHeight());
    const wxRect clipped = sourceRect.Intersect(bounds);
    if (clipped.width <= 0 || clipped.height <= 0)
        return false;

    const wxBitmap slice(full.ConvertToImage().GetSubImage(clipped));
    if (!slice.IsOk())
        return false;

    panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    panel->Bind(wxEVT_PAINT,
        [panel, slice, scaled = wxBitmap(), scaledW = -1, scaledH = -1](wxPaintEvent&) mutable
        {
            wxAutoBufferedPaintDC dc(panel);
            dc.SetBackground(*wxBLACK_BRUSH);
            dc.Clear();

            const wxSize size = panel->GetClientSize();
            if (size.x <= 0 || size.y <= 0)
                return;

            if (!scaled.IsOk() || scaledW != size.x || scaledH != size.y)
            {
                scaled = wxBitmap(slice.ConvertToImage().Scale(
                    size.x, size.y, wxIMAGE_QUALITY_NEAREST));
                scaledW = size.x;
                scaledH = size.y;
            }

            if (scaled.IsOk())
                dc.DrawBitmap(scaled, 0, 0, false);
        });
    return true;
}

static bool BuildStrategicMapChrome(SpellData* spellData, wxBitmap& outBmp)
{
    wxBitmap full;
    if (!BuildStrategicScreenBitmap(spellData, "VMM_FULL.LZ", full))
    {
        outBmp = wxBitmap();
        return false;
    }

    outBmp = wxBitmap(full.ConvertToImage().GetSubImage(
        wxRect(0, 0, kMapChromeW, kMapChromeH)));
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

    m_mapChromeBitmap = wxBitmap();
    m_mapChromeBitmapScaled = wxBitmap();
    m_mapChromeScaledW = -1;
    m_mapChromeScaledH = -1;
    BuildStrategicMapChrome(m_spellData, m_mapChromeBitmap);

    m_hasClk = false;
    m_clkValues.clear();
    m_clkW = m_clkH = 0;
    m_compositeFolder.clear();

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

        // Prefer the configured game-data roots.  LEVEL_XX.DEF can be opened
        // from anywhere, while the original LEVEL/HMLA/PAL/CLK files normally
        // live in DATA\COMMON; do not depend on a project-local temp export.
        if (m_spellData)
        {
            const fs::path dataRoot = m_spellData->data_path;
            const fs::path cdRoot = m_spellData->cd_data_path;
            dirs.push_back(dataRoot / "COMMON");
            dirs.push_back(dataRoot / "common");
            dirs.push_back(dataRoot);
            dirs.push_back(cdRoot / "COMMON");
            dirs.push_back(cdRoot / "common");
            dirs.push_back(cdRoot);
        }

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
            bool adjacencyLoaded = false;
            if (!m_compositeFolder.empty())
                adjacencyLoaded = LoadSSDAdjacency(std::filesystem::path(m_compositeFolder),
                    levelNum, maxId, m_territoryAdjMask);
            if (!adjacencyLoaded)
                LoadSSDAdjacencyFromArchive(m_spellData, levelNum, maxId, m_territoryAdjMask);

            ApplyTerritoryVisibility();
            MarkOverlayDirty();
        }
    }

    // A failed/partial asset lookup must remain usable: restore the explicit
    // territory buttons whenever CLK hit-testing is unavailable.  Previously
    // a successful load followed by a failed reload left this fallback hidden.
    if (m_territoryButtonsPanel)
    {
        m_territoryButtonsPanel->Show(!m_hasClk);
        if (m_mapPanel)
            m_mapPanel->Layout();
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
            const int tid = row[x] >= 129 ? (int)row[x] - 128 : (int)row[x];
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

    // Draw grid on background (visible only in empty areas around/outside the map bitmap)
    {
        int pw, ph;
        target->GetClientSize(&pw, &ph);
        dc.SetPen(wxPen(wxColour(0x20, 0x40, 0x15), 1)); // darker green line
        const int gridSize = 32;
        for (int gx = 0; gx < pw; gx += gridSize)
            dc.DrawLine(gx, 0, gx, ph);
        for (int gy = 0; gy < ph; gy += gridSize)
            dc.DrawLine(0, gy, pw, gy);
    }

    if (m_hasBg && m_bgBitmap.IsOk())
    {
        int pw, ph;
        target->GetClientSize(&pw, &ph);

        const int bw = m_bgBitmap.GetWidth();
        const int bh = m_bgBitmap.GetHeight();
        if (pw <= 0 || ph <= 0 || bw <= 0 || bh <= 0)
            return;

        double s = 1.0;
        int dw = 0;
        int dh = 0;
        int x = 0;
        int y = 0;

        if (!resourcesView && m_mapChromeBitmap.IsOk())
        {
            // Fit the complete original frame, then place LEVEL_XX exactly in
            // the same viewport used by the DOS strategic screen.
            const double chromeScale = std::min(
                (double)pw / (double)kMapChromeW,
                (double)ph / (double)kMapChromeH);
            const int chromeW = std::max(1, (int)std::lround(kMapChromeW * chromeScale));
            const int chromeH = std::max(1, (int)std::lround(kMapChromeH * chromeScale));
            const int chromeX = (pw - chromeW) / 2;
            const int chromeY = (ph - chromeH) / 2;

            if (!m_mapChromeBitmapScaled.IsOk() ||
                m_mapChromeScaledW != chromeW || m_mapChromeScaledH != chromeH)
            {
                wxImage chrome = m_mapChromeBitmap.ConvertToImage();
                m_mapChromeBitmapScaled = wxBitmap(chrome.Scale(chromeW, chromeH, wxIMAGE_QUALITY_NEAREST));
                m_mapChromeScaledW = chromeW;
                m_mapChromeScaledH = chromeH;
            }
            dc.DrawBitmap(m_mapChromeBitmapScaled.IsOk() ? m_mapChromeBitmapScaled : m_mapChromeBitmap,
                chromeX, chromeY, false);

            x = chromeX + (int)std::lround(kMapViewportX * chromeScale);
            y = chromeY + (int)std::lround(kMapViewportY * chromeScale);
            dw = std::max(1, (int)std::lround(kMapViewportW * chromeScale));
            dh = std::max(1, (int)std::lround(kMapViewportH * chromeScale));
            s = (double)dw / (double)bw;
        }
        else
        {
            // Auxiliary resources view keeps the old map-only fit.
            const double sx = (double)pw / (double)bw;
            const double sy = (double)ph / (double)bh;
            s = std::min(sx, sy);
            dw = std::max(1, (int)std::lround((double)bw * s));
            dh = std::max(1, (int)std::lround((double)bh * s));
            x = (pw - dw) / 2;
            y = (ph - dh) / 2;
        }

        // Cache the scaled bitmap so we don't rescale on every paint.
        if (!m_bgBitmapScaled.IsOk() || m_bgScaledW != dw || m_bgScaledH != dh)
        {
            wxImage img = m_bgBitmap.ConvertToImage();
            m_bgBitmapScaled = wxBitmap(img.Scale(dw, dh, wxIMAGE_QUALITY_NEAREST));
            m_bgScaledW = dw;
            m_bgScaledH = dh;
        }

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
                std::memset(ovImg.GetData(), 0, (size_t)bw * (size_t)bh * 3);
                std::memset(ovImg.GetAlpha(), 255, (size_t)bw * (size_t)bh);

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
                            {
                                r = 0xFF; g = 0xF6; b = 0x04; a = 130;
                            }  // yellow highlight
                            else if (depleted)
                            {
                                r = 0x88; g = 0x44; b = 0x44; a = 160;
                            }  // red-grey
                            else
                            {
                                r = 0x10; g = 0xD0; b = 0x10; a = 120;
                            }  // green
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

                // In game mode: no text labels (T01, T02...), only markers at centroid
                if (m_gameModeEnabled && !resourcesView)
                {
                    DrawTerritoryMarker(dc, t.id, tx, ty, s);
                    continue;
                }

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
    const int maxUnits = rec ? std::clamp(rec->max_units, 0, 32) : 0;
    const int maxCmds = rec ? std::clamp(rec->max_commanders, 0, 14) : 0;
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
        // Exact values from the original COMMON.FS/HODNOSTI.DEF.
        // Parameter 3 is the number of battles a *subordinate commander* needs
        // for that commander rank; parameter 4 is John Alexander's strategic
        // experience threshold.  Do not conflate the two progression systems.
        m_ranks = {
            {0,  2,  2,     0,  0},
            {1,  4,  6,     0,  0},
            {2,  9, 10,     0,  2},
            {3, 14, 18,   300,  4},
            {4, 18, 26,  2550,  6},
            {5, 22, 36,  5350,  8},
            {6, 28, 48, 10000, 10},
            {7, 34, 66, 16000, 12},
            {8, 40, 84, 26000, 14},
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
    wxString p = FindStrategicStatsPath();
    std::ifstream f(p.ToStdString());
    if (!f)
        return;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty())
        return;

    // Flat format: "all_alliance": {...}, "all_enemy": {...}, "level_alliance": {...}, "level_enemy": {...}
    auto extractFlatBlock = [&](const char* key, LossBlock& out) -> bool
        {
            std::regex re(std::string("\"") + key + "\"\\s*:\\s*\\{([^}]*)\\}");
            std::smatch m;
            if (!std::regex_search(data, m, re) || m.size() < 2)
                return false;
            return ReadLossBlockFromObj_StrategicLevel(m[1].str(), out);
        };

    extractFlatBlock("all_alliance",   m_lossStats.alliance_all);
    extractFlatBlock("all_enemy",      m_lossStats.enemy_all);
    extractFlatBlock("level_alliance", m_lossStats.alliance_level);
    extractFlatBlock("level_enemy",    m_lossStats.enemy_level);

    // Also load mission stats counters if present
    ParseJsonIntField(data, "missions_completed",    m_stats.missions_completed);
    ParseJsonIntField(data, "missions_failed",       m_stats.missions_failed);
    ParseJsonIntField(data, "territories_conquered", m_stats.territories_conquered);
    ParseJsonIntField(data, "territories_lost",      m_stats.territories_lost);
    ParseJsonIntField(data, "turns_total",           m_stats.turns_total);
}

// ---------------- Rank helpers ----------------

void StrategicLevelFrame::RecomputePlayerRank()
{
    if (m_ranks.empty())
        LoadRanksTable();

    // HODNOSTI.DEF has two independent progression columns:
    //   actions_required = battles needed by a recruited subordinate commander
    //   exp_required     = strategic XP needed by the player/John Alexander
    // The old implementation incorrectly required BOTH values for John, which
    // left a fresh campaign at rank 0.  In the original data ranks 0..2 all
    // have player XP threshold 0, so John enters the strategic game as Captain
    // (rank 2) and immediately has two commander slots.
    int best = 0;
    for (const auto& r : m_ranks)
    {
        if (m_player.experience >= r.exp_required)
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

// ============================================================
// STRATEGIC LEVEL INTEGRATION - Mission Result Handling
// ============================================================

void StrategicLevelFrame::HandleMissionResult(int territory_id, bool success, const std::string& mission_token)
{
    // Collect battle results from the tactical map (losses, damage, unit experience).
    // John Alexander's *strategic* XP is not derived from kill counts; the
    // original campaign scripts provide the mission rewards explicitly via
    // EndOK(money, experience) in LEVEL_XX.DEF.
    CollectAndApplyBattleResults(success);

    // Resolve the exact mission record once. Territory tokens often omit the
    // trailing variant letter (m02_02 -> M02_02A).
    const LevelMission* mission = FindMissionByNameUpper(to_upper(mission_token));
    if (!mission)
    {
        std::string tokenUp = to_upper(mission_token);
        if (!tokenUp.empty() && std::isdigit((unsigned char)tokenUp.back()))
            mission = FindMissionByNameUpper(tokenUp + "A");
    }

    // Check if this was a counter-attack mission
    bool wasCounterAttack = false;
    for (auto& ca : m_counterAttacks)
    {
        if (ca.territory_id == territory_id && ca.triggered && !ca.completed)
        {
            wasCounterAttack = true;
            if (success)
            {
                ca.completed = true;
            }
            else
            {
                // Counter-attack defense failed — territory is lost
                ca.completed = true;
                auto it = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), territory_id);
                if (it != m_ownedTerritories.end())
                    m_ownedTerritories.erase(it);
                m_stats.territories_lost++;
            }
            break;
        }
    }

    if (success)
    {
        m_stats.missions_completed++;
        // Keep this legacy counter for save compatibility/debug statistics.
        // It is deliberately NOT used for John Alexander's rank.
        m_player.actions++;

        // Original strategic reward semantics from LEVEL_XX.DEF:
        //     EndOK(<money>, <player experience>)
        // Examples from the original data:
        //     M02_02A EndOK(50,40)
        //     M04_16A EndOK(780,2800)
        //     M09_13A EndOK(600,15000)
        // The previous restoration ignored these fields and invented XP from
        // enemy unit kills, which broke the intended promotion curve.
        const int money_award = mission ? std::max(0, mission->end_ok_x) : 0;
        const int xp_award = mission ? std::max(0, mission->end_ok_y) : 0;
        m_money += money_award;
        m_player.experience += xp_award;
        RecomputePlayerRank();

        // Play end_ok_video if defined
        if (mission && !mission->end_ok_video.empty() && mission->end_ok_video != "none")
        {
            PlayVideo(mission->end_ok_video);
        }

        const bool hasSuccessor = mission && !mission->end_ok_mission.empty() && mission->end_ok_mission != "none";

        if (hasSuccessor)
        {
            // Multi-stage territory: keep it hostile and move to the next mission
            // only after a successful result.  This is essential for e.g. the
            // final territories in LEVEL_07 and LEVEL_10 (A -> B).
            m_territoryCurrentMission[territory_id] = to_lower(mission->end_ok_mission);
            m_territoryTimeoutTurn.erase(territory_id);
        }
        else if (!wasCounterAttack)
        {
            // A territory is secured only when its successful mission chain ends.
            ConquestTerritory(territory_id);
        }

        // Campaign progression is driven by LevelInit::End(n).  Earlier builds
        // also treated "all territories conquered" (and a mission-level flag)
        // as an unconditional completion trigger.  That is NOT the original
        // strategic flow and could jump LEVEL_02 -> LEVEL_03 after the first
        // conquered territory when restored/save state got out of sync.
        //
        // In the original campaign the crossed-swords territory is exactly the
        // End(n) territory.  A level advances only after the terminal successful
        // mission on that territory.  If that mission has EndOKMission(next), the
        // territory remains hostile and the next stage must be completed first.
        bool isLevelComplete = false;
        if (!hasSuccessor)
        {
            if (m_level.end_territory > 0)
            {
                // Be deliberately strict: both the pending territory id and the
                // resolved mission token must belong to the End(n) territory.
                // This also protects against stale pending-mission state from an
                // older save/build.
                const LevelTerritory* finalTerr = nullptr;
                for (const auto& t : m_level.territories)
                    if (t.id == m_level.end_territory) { finalTerr = &t; break; }

                bool tokenMatchesFinal = (territory_id == m_level.end_territory);
                if (tokenMatchesFinal && finalTerr && !mission_token.empty())
                {
                    const std::string tokenUp = to_upper(mission_token);
                    const std::string baseUp = to_upper(finalTerr->mission);
                    const std::string introUp = to_upper(finalTerr->intro_mission);
                    tokenMatchesFinal =
                        (!baseUp.empty() && baseUp != "NONE" && tokenUp.rfind(baseUp, 0) == 0) ||
                        (!introUp.empty() && introUp != "NONE" && tokenUp.rfind(introUp, 0) == 0);
                }
                isLevelComplete = (territory_id == m_level.end_territory) && tokenMatchesFinal;
            }
            else if (mission && mission->is_level_final)
            {
                // Compatibility for custom definitions that do not use End(n).
                isLevelComplete = true;
            }
            else if (!m_gameModeEnabled)
            {
                // Editor/debug fallback only.  The campaign must never use this
                // shortcut because discovery order and End(n) are authoritative.
                isLevelComplete = AreAllTerritoriesConquered();
            }
        }

        wxLogMessage("[STRATEGIC FLOW] result terr=%d end=%d token='%s' successor=%s complete=%s",
            territory_id, m_level.end_territory, mission_token.c_str(),
            hasSuccessor ? "YES" : "NO", isLevelComplete ? "YES" : "NO");

        if (isLevelComplete)
        {
            SaveMissionStats();
            SaveStrategicState();
            AdvanceToNextLevel();
            return;
        }
    }
    else
    {
        m_stats.missions_failed++;

        // Play end_bad_video if defined
        if (mission && !mission->end_bad_video.empty() && mission->end_bad_video != "none")
        {
            PlayVideo(mission->end_bad_video);
        }

        // Check if mission has end_bad_mission (retry variant)
        if (mission && !mission->end_bad_mission.empty() && mission->end_bad_mission != "none")
        {
            m_territoryCurrentMission[territory_id] = to_lower(mission->end_bad_mission);
            // The replacement mission owns its own Time(...) rule.
            m_territoryTimeoutTurn.erase(territory_id);
        }
    }

    m_pendingMission.valid = false;

    ApplyTerritoryVisibility();
    CheckTimeouts();
    MarkOverlayDirty();

    SaveMissionStats();
    SaveStrategicState();
    RefreshUI();
}

StrategicLevelFrame::LossBlock StrategicLevelFrame::CollectAndApplyBattleResults(bool success)
{
    LossBlock empty_result{};

    if (!m_main || !m_main->spell_data)
        return empty_result;

    // Access the tactical map that just finished
    SpellMap* tactical_map = m_main->GetSpellMap();
    if (!tactical_map || !tactical_map->IsLoaded())
    {
        return empty_result;
    }

    // --- 1) Count losses by category ---
    LossBlock mission_alliance_losses{};
    LossBlock mission_enemy_losses{};

    for (auto* u : tactical_map->units)
    {
        if (!u || !u->unit)
            continue;

        const bool dead = u->isDead() != 0;
        const bool is_air = u->unit->isAir() != 0;
        const bool is_armored = u->unit->isArmored() != 0;
        const bool is_cmd = u->is_commander != 0;

        if (u->is_enemy)
        {
            if (dead)
            {
                if (is_cmd)       mission_enemy_losses.commanders++;
                else if (is_air)  mission_enemy_losses.air++;
                else if (is_armored) mission_enemy_losses.heavy++;
                else              mission_enemy_losses.light++;
            }
        }
        else
        {
            if (dead)
            {
                if (is_cmd)       mission_alliance_losses.commanders++;
                else if (is_air)  mission_alliance_losses.air++;
                else if (is_armored) mission_alliance_losses.heavy++;
                else              mission_alliance_losses.light++;
            }
        }
    }

    // Update loss statistics (both level and all-time)
    m_lossStats.alliance_level.light += mission_alliance_losses.light;
    m_lossStats.alliance_level.heavy += mission_alliance_losses.heavy;
    m_lossStats.alliance_level.air += mission_alliance_losses.air;
    m_lossStats.alliance_level.commanders += mission_alliance_losses.commanders;

    m_lossStats.alliance_all.light += mission_alliance_losses.light;
    m_lossStats.alliance_all.heavy += mission_alliance_losses.heavy;
    m_lossStats.alliance_all.air += mission_alliance_losses.air;
    m_lossStats.alliance_all.commanders += mission_alliance_losses.commanders;

    m_lossStats.enemy_level.light += mission_enemy_losses.light;
    m_lossStats.enemy_level.heavy += mission_enemy_losses.heavy;
    m_lossStats.enemy_level.air += mission_enemy_losses.air;
    m_lossStats.enemy_level.commanders += mission_enemy_losses.commanders;

    m_lossStats.enemy_all.light += mission_enemy_losses.light;
    m_lossStats.enemy_all.heavy += mission_enemy_losses.heavy;
    m_lossStats.enemy_all.air += mission_enemy_losses.air;
    m_lossStats.enemy_all.commanders += mission_enemy_losses.commanders;
    // --- 2) Sync unit health / damage / deaths back to player roster ---
    // Only touch roster entries that were actually sent to the mission (sent_unit_indices).
    const auto& sent = m_pendingMission.sent_unit_indices;
    if (sent.empty())
    {
        return mission_enemy_losses;
    }

    // Build ordered list of surviving alliance units from the tactical map
    std::vector<MapUnit*> survivors;
    for (auto* u : tactical_map->units)
    {
        if (!u || !u->unit) continue;
        if (u->is_enemy) continue;
        if (u->isDead()) continue;
        survivors.push_back(u);
    }

    // Match sent roster entries to survivors by type_id, in order
    std::unordered_map<int, std::vector<MapUnit*>> survivors_by_type;
    for (auto* s : survivors)
        survivors_by_type[s->unit->type_id].push_back(s);

    // Ensure m_unitStates covers all roster entries (it may be empty if user
    // never opened the Units page before the first battle).
    while (m_unitStates.size() < m_playerUnits.size())
    {
        UnitInstanceState state;
        state.uid = m_nextRosterUid++;
        m_unitStates.push_back(state);
    }

    std::unordered_map<int, size_t> consumed_idx; // type_id -> next index in survivors_by_type

    // Track which sent indices are dead (no matching survivor)
    std::vector<size_t> dead_indices;

    for (size_t si = 0; si < sent.size(); ++si)
    {
        const size_t roster_idx = sent[si];
        if (roster_idx >= m_playerUnits.size())
            continue;

        auto& pu = m_playerUnits[roster_idx];
        const int tid = pu.unit_id;

        auto it = survivors_by_type.find(tid);
        size_t& cidx = consumed_idx[tid];

        if (it != survivors_by_type.end() && cidx < it->second.size())
        {
            MapUnit* survivor = it->second[cidx];
            cidx++;

            // Update health: convert man count back to percentage
            int max_man = survivor->unit ? survivor->unit->cnt : 0;
            if (max_man > 0)
                pu.health = std::max(1, (survivor->man * 100 + max_man / 2) / max_man);
            else
                pu.health = 100;

            // Update unit instance experience and level from tactical map
            if (roster_idx < m_unitStates.size())
            {
                m_unitStates[roster_idx].experience += survivor->experience;
                m_unitStates[roster_idx].level = survivor->experience_level;
            }

            // Set post-battle rest cooldown (minimum 1 turn before next mission)
            if (roster_idx < m_unitStates.size())
                m_unitStates[roster_idx].cooldown_turns = std::max(m_unitStates[roster_idx].cooldown_turns, 1);
        }
        else
        {
            // No matching survivor -> this unit was killed in battle
            dead_indices.push_back(roster_idx);
        }
    }

    // Remove dead units from roster (walk backwards to keep indices valid)
    std::sort(dead_indices.begin(), dead_indices.end(), std::greater<size_t>());
    // Remove duplicates
    dead_indices.erase(std::unique(dead_indices.begin(), dead_indices.end()), dead_indices.end());

    for (size_t idx : dead_indices)
    {
        if (idx >= m_playerUnits.size())
            continue;
        m_playerUnits.erase(m_playerUnits.begin() + idx);
        if (idx < m_unitStates.size())
            m_unitStates.erase(m_unitStates.begin() + idx);
        if (idx < m_rosterRowUids.size())
            m_rosterRowUids.erase(m_rosterRowUids.begin() + idx);
    }

    // --- Rescued special units (e.g. Commando from first level) ---
    // Surviving SpecUnit units that were NOT in the sent roster are rescued units
    // from the tactical map (e.g. SaveUnit event). Add them to the player's strategic roster.
    {
        // build set of survivors already matched to sent roster entries
        std::unordered_set<MapUnit*> matched_survivors;
        {
            std::unordered_map<int, size_t> cidx2;
            for (size_t si = 0; si < sent.size(); ++si)
            {
                const size_t roster_idx = sent[si];
                if (roster_idx >= m_playerUnits.size()) continue;
                const int tid = m_playerUnits[roster_idx].unit_id;
                auto it = survivors_by_type.find(tid);
                size_t& ci = cidx2[tid];
                if (it != survivors_by_type.end() && ci < it->second.size())
                {
                    matched_survivors.insert(it->second[ci]);
                    ci++;
                }
            }
        }

        for (auto* s : survivors)
        {
            if (matched_survivors.count(s)) continue; // already matched to roster
            if (s->spec_type != MapUnitType::SpecUnit) continue; // only SpecUnit

            // this is a rescued special unit - add to player roster
            LevelData::PlayerUnitAdd pu;
            pu.unit_id = s->unit->type_id;
            pu.count = s->man;
            int max_man = s->unit ? s->unit->cnt : 0;
            if (max_man > 0)
                pu.health = std::max(1, (s->man * 100 + max_man / 2) / max_man);
            else
                pu.health = 100;
            pu.extra = "-";
            m_playerUnits.push_back(pu);

            // add matching unit state with experience from tactical map
            UnitInstanceState state;
            state.uid = m_nextRosterUid++;
            state.experience = s->experience;
            state.level = s->experience_level;
            state.cooldown_turns = 1;
            m_unitStates.push_back(state);

            if (!m_rosterRowUids.empty())
                m_rosterRowUids.push_back(state.uid);
        }
    }

    return mission_enemy_losses;
}

void StrategicLevelFrame::SaveMissionStats() const
{
    wxString p = FindStrategicStatsPath();
    if (p.empty())
        return;

    std::ofstream f(p.ToStdString());
    if (!f)
        return;

    auto writeLossBlock = [&](const char* prefix, const LossBlock& b)
    {
        f << "  \"" << prefix << "\": "
          << "{\"light\": " << b.light
          << ", \"heavy\": " << b.heavy
          << ", \"air\": " << b.air
          << ", \"commanders\": " << b.commanders << "}";
    };

    f << "{\n";
    writeLossBlock("all_alliance", m_lossStats.alliance_all);     f << ",\n";
    writeLossBlock("all_enemy",    m_lossStats.enemy_all);        f << ",\n";
    writeLossBlock("level_alliance", m_lossStats.alliance_level); f << ",\n";
    writeLossBlock("level_enemy",    m_lossStats.enemy_level);    f << ",\n";
    f << "  \"missions_completed\": " << m_stats.missions_completed << ",\n";
    f << "  \"missions_failed\": " << m_stats.missions_failed << ",\n";
    f << "  \"territories_conquered\": " << m_stats.territories_conquered << ",\n";
    f << "  \"territories_lost\": " << m_stats.territories_lost << ",\n";
    f << "  \"turns_total\": " << m_stats.turns_total << "\n";
    f << "}\n";
}

void StrategicLevelFrame::ConquestTerritory(int territory_id)
{
    auto it = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), territory_id);
    if (it != m_ownedTerritories.end())
    {
        return;
    }
    m_ownedTerritories.push_back(territory_id);
    m_stats.territories_conquered++;
    
    {
        TerritoryResourceState st;
        for (const auto& t : m_level.territories)
        {
            if (t.id != territory_id) continue;
            st.total = std::max(0, t.strategic_points_total);
            st.remaining = st.total;
            st.incomePerTurn = std::max(0, t.strategic_points_per_turn);
            break;
        }
        m_territoryResources[territory_id] = st;
    }
    
    const LevelTerritory* terr = nullptr;
    for (const auto& t : m_level.territories)
    {
        if (t.id == territory_id)
        {
            terr = &t;
            break;
        }
    }
    
    // Play conquest video if defined
    if (terr && !terr->conquest_video.empty() && terr->conquest_video != "none")
    {
        PlayVideo(terr->conquest_video);
    }
    
    // Schedule counter-attack if defined
    if (terr && terr->counter_attack_turn > 0 && !terr->counter_attack_mission.empty())
    {
        CounterAttackState ca;
        ca.territory_id = territory_id;
        ca.conquest_turn = m_turn;
        ca.trigger_turn = m_turn + terr->counter_attack_turn;
        ca.counter_mission = terr->counter_attack_mission;
        ca.triggered = false;
        ca.completed = false;
        m_counterAttacks.push_back(ca);
    }
    
    // Remove from timeout tracking if it was there
    m_territoryTimeoutTurn.erase(territory_id);
    
    ApplyTerritoryVisibility();
    MarkOverlayDirty();
}

void StrategicLevelFrame::CheckTimeouts()
{
    if (!m_gameModeEnabled)
        return;
    
    for (const auto& t : m_level.territories)
    {
        // Skip owned territories
        if (std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), t.id) != m_ownedTerritories.end())
            continue;
        
        // Time limit belongs to the CURRENT mission variant, not permanently
        // to the territory.  Some original chains introduce Time(...) only in
        // a later B/C mission (e.g. M03_05B).
        std::string curToken = ResolveMissionTokenForTerritory(t.id);
        std::string lookupToken = to_upper(curToken);
        const LevelMission* currentMission = FindMissionByNameUpper(lookupToken);
        if (!currentMission && !lookupToken.empty() && std::isdigit((unsigned char)lookupToken.back()))
            currentMission = FindMissionByNameUpper(lookupToken + "A");
        const int timeLimit = currentMission ? std::max(0, currentMission->time_limit) : 0;
        if (timeLimit <= 0)
        {
            // Do not carry a deadline from a previous mission stage.
            m_territoryTimeoutTurn.erase(t.id);
            continue;
        }

        // Check if territory is visible
        if (t.id < (int)m_visibleTerritory.size() && m_visibleTerritory[t.id] == 0)
            continue;
        
        auto it = m_territoryTimeoutTurn.find(t.id);
        if (it == m_territoryTimeoutTurn.end())
        {
            // Start countdown when this mission stage first becomes available.
            m_territoryTimeoutTurn[t.id] = m_turn + timeLimit;
        }
        else if (it->second <= 0)
        {
            // Already expired (sentinel), do not restart
            continue;
        }
        else
        {
            if (m_turn >= it->second)
            {
                // Resolve the current mission record already used for the timer.
                const LevelMission* mission = currentMission;

                if (mission && !mission->end_bad_mission.empty() && mission->end_bad_mission != "none")
                {
                    // Switch to the BAD mission variant. Its own Time(...) starts
                    // on the next CheckTimeouts pass; do not leave the old deadline.
                    m_territoryCurrentMission[t.id] = to_lower(mission->end_bad_mission);
                    const LevelMission* nextMission = FindMissionByNameUpper(to_upper(mission->end_bad_mission));
                    if (nextMission && nextMission->time_limit > 0)
                        m_territoryTimeoutTurn[t.id] = m_turn + nextMission->time_limit;
                    else
                        m_territoryTimeoutTurn.erase(t.id);
                }
                else
                {
                    // No replacement mission: keep an expired sentinel so this
                    // one-shot timeout cannot be restarted by visibility refresh.
                    m_stats.territories_lost++;
                    it->second = -1;
                }
            }
        }
    }

    MarkOverlayDirty();
}

void StrategicLevelFrame::CheckCounterAttacks()
{
    if (!m_gameModeEnabled)
        return;

    for (auto& ca : m_counterAttacks)
    {
        if (ca.triggered || ca.completed)
            continue;

        if (m_turn >= ca.trigger_turn)
        {
            ca.triggered = true;

            // Set the counter-attack mission on the territory
            if (!ca.counter_mission.empty())
                m_territoryCurrentMission[ca.territory_id] = to_lower(ca.counter_mission);

            // Select the territory to bring attention to it
            SelectTerritoryById(ca.territory_id);

            // Show counter-attack briefing in the textbox
            ShowBriefing(ca.territory_id);

            // Visual indicator update
            MarkOverlayDirty();

            int result = wxMessageBox(
                wxString::Format(
                    "COUNTER-ATTACK!\n\n"
                    "Enemy forces are attacking territory %d!\n\n"
                    "You must defend this territory or lose it.\n"
                    "Launch defense mission now?",
                    ca.territory_id
                ),
                "Counter-Attack Warning",
                wxYES_NO | wxICON_EXCLAMATION,
                this
            );

            if (result == wxYES)
            {
                // Player accepted — they will launch from the selected territory
                RefreshUI();
            }
            else
            {
                auto it = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), ca.territory_id);
                if (it != m_ownedTerritories.end())
                {
                    m_ownedTerritories.erase(it);
                }

                m_stats.territories_lost++;
                ca.completed = true;

                ApplyTerritoryVisibility();
                MarkOverlayDirty();
            }
        }
    }
}

void StrategicLevelFrame::ProcessLevelEvents()
{
    if (!m_gameModeEnabled)
        return;

    // Collect events to trigger this turn (may chain via RunEvents)
    std::vector<int> to_trigger;

    for (const auto& evt : m_level.events)
    {
        if (m_triggeredLevelEvents.count(evt.id))
            continue; // already triggered

        bool should_fire = false;

        if (evt.abs_time)
        {
            // AbsTime: fires at absolute turn number
            if (evt.time_value >= 0 && m_turn >= evt.time_value)
                should_fire = true;
        }
        else if (evt.time_value >= 0)
        {
            // Time(N): fires N turns after activation
            auto act_it = m_activatedEvents.find(evt.id);
            if (act_it != m_activatedEvents.end())
            {
                int target_turn = act_it->second + evt.time_value;
                if (m_turn >= target_turn)
                    should_fire = true;
            }
        }
        else
        {
            // time_value == -1: event is condition-only, check WaitForTerritories
            if (!evt.wait_for_territories.empty())
            {
                bool all_owned = true;
                for (int tid : evt.wait_for_territories)
                {
                    if (std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid) == m_ownedTerritories.end())
                    {
                        all_owned = false;
                        break;
                    }
                }
                if (all_owned)
                    should_fire = true;
            }
        }

        // WaitForTerritories gate (for AbsTime/Time events that also have this condition)
        if (should_fire && !evt.wait_for_territories.empty() && (evt.abs_time || evt.time_value >= 0))
        {
            bool all_owned = true;
            for (int tid : evt.wait_for_territories)
            {
                if (std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid) == m_ownedTerritories.end())
                {
                    all_owned = false;
                    break;
                }
            }
            if (!all_owned)
                should_fire = false;
        }

        if (should_fire)
            to_trigger.push_back(evt.id);
    }

    // Process triggered events (may chain via RunEvents)
    std::set<int> processed_this_turn;
    std::vector<int> queue = to_trigger;

    while (!queue.empty())
    {
        int eid = queue.back();
        queue.pop_back();

        if (m_triggeredLevelEvents.count(eid) || processed_this_turn.count(eid))
            continue;

        // Find event by id
        const LevelEvent* evt = nullptr;
        for (const auto& e : m_level.events)
        {
            if (e.id == eid) { evt = &e; break; }
        }
        if (!evt)
            continue;

        m_triggeredLevelEvents.insert(eid);
        processed_this_turn.insert(eid);

        // --- Execute event actions ---

        // Show EventText in strategic level window (not on the tactical map canvas)
        if (!evt->text_id.empty() && m_spellData && m_spellData->texts)
        {
            std::string text_name = evt->text_id;
            SpellTextRec* txt = m_spellData->texts->GetText(text_name);
            if (txt)
            {
                // Use the text content for a dialog in the strategic level window
                wxString msg = txt->text.empty()
                    ? wxString::FromUTF8(text_name)
                    : wxString(txt->text);
                wxMessageBox(msg, "Event", wxOK | wxICON_INFORMATION, this);
            }
        }

        // ChangeMission
        if (evt->change_mission_territory >= 0 && !evt->change_mission_name.empty())
        {
            m_territoryCurrentMission[evt->change_mission_territory] = to_lower(evt->change_mission_name);
        }

        // AddUnitToPlayer
        for (const auto& ua : evt->add_units)
        {
            LevelData::PlayerUnitAdd pu;
            pu.unit_id = ua.unit_id;
            pu.count = ua.count;
            pu.health = ua.health;
            pu.extra = ua.name;
            m_playerUnits.push_back(pu);
        }

        // SetResearchFlag
        for (int flag : evt->research_flags)
        {
            if (std::find(m_level.research_flags.begin(), m_level.research_flags.end(), flag) == m_level.research_flags.end())
                m_level.research_flags.push_back(flag);
        }

        // SetPlayersTerritory
        if (evt->set_player_territory >= 0)
        {
            int tid = evt->set_player_territory;
            if (std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), tid) == m_ownedTerritories.end())
            {
                m_ownedTerritories.push_back(tid);
                ApplyTerritoryVisibility();
            }
        }

        // RunEvents: activate referenced events
        for (int run_id : evt->run_events)
        {
            if (!m_triggeredLevelEvents.count(run_id) && !processed_this_turn.count(run_id))
            {
                // For Time-based events, record activation turn
                m_activatedEvents[run_id] = m_turn;
                queue.push_back(run_id);
            }
        }

        // Army: counter-attack on a player-owned territory
        if (!evt->armies.empty() && !m_ownedTerritories.empty())
        {
            // Pick a target territory for the counter-attack
            // Prefer the most recently conquered territory
            int target_tid = m_ownedTerritories.back();

            // Select the territory visually
            SelectTerritoryById(target_tid);
            MarkOverlayDirty();

            int result = wxMessageBox(
                wxString::Format(
                    "COUNTER-ATTACK!\n\n"
                    "Enemy forces are attacking territory %d!\n\n"
                    "Defend this territory?",
                    target_tid
                ),
                "Counter-Attack",
                wxYES_NO | wxICON_EXCLAMATION,
                this
            );

            if (result == wxYES)
            {
                // Player will defend — set territory for launch
                RefreshUI();
            }
            else
            {
                // Player refuses to defend — lose the territory
                auto it = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), target_tid);
                if (it != m_ownedTerritories.end())
                    m_ownedTerritories.erase(it);

                m_stats.territories_lost++;
                ApplyTerritoryVisibility();
                MarkOverlayDirty();
            }
        }
    }

    MarkOverlayDirty();
}

void StrategicLevelFrame::ShowBriefing(int territory_id)
{
    std::string token = ResolveMissionTokenForTerritory(territory_id);
    if (token.empty() || token == "none")
        return;

    // Check if this is a counter-attack briefing
    bool isCounterAttack = false;
    for (const auto& ca : m_counterAttacks)
    {
        if (ca.territory_id == territory_id && ca.triggered && !ca.completed)
        {
            isCounterAttack = true;
            break;
        }
    }

    // Resolve texts dir
    std::filesystem::path texts_dir = FindTextsDirForLevel(m_level, m_spellData);
    if (texts_dir.empty())
    {
        return;
    }

    wxString info;
    if (isCounterAttack)
    {
        info << "*** COUNTER-ATTACK ***\n\n";
        try_append_single_text(info, texts_dir, token, ".S", "Counter-Attack Briefing");
    }
    else
    {
        try_append_single_text(info, texts_dir, token, "", "Briefing");
    }

    if (info.IsEmpty())
    {
        return;
    }

    // Show in the textbox under the map
    if (m_mapPanel)
    {
        if (auto* box = wxDynamicCast(m_mapPanel->FindWindow(ID_TERRITORY_TEXTBOX), wxTextCtrl))
        {
            box->SetValue(info);
            box->ShowPosition(0);
        }
    }
}

std::wstring StrategicLevelFrame::FindBriefingPath(const std::string& mission_token) const
{
    namespace fs = std::filesystem;
    
    std::string briefName = mission_token;
    if (!briefName.empty() && (briefName[0] == 'M' || briefName[0] == 'm'))
    {
        briefName[0] = 'T';
    }
    
    if (!briefName.empty() && std::isdigit(static_cast<unsigned char>(briefName.back())))
    {
        briefName += 'A';
    }
    
    const fs::path textsDir = FindTextsDirForLevel(m_level, m_spellData);
    if (textsDir.empty())
        return L"";

    // Original Spellcross briefing files are usually extensionless, but keep
    // .TXT candidates for custom data packs.
    std::vector<fs::path> searchPaths = {
        textsDir / briefName,
        textsDir / to_upper(briefName),
        textsDir / to_lower(briefName),
        textsDir / (briefName + ".TXT"),
        textsDir / (to_upper(briefName) + ".TXT"),
        textsDir / (to_lower(briefName) + ".txt")
    };
    
    for (const auto& p : searchPaths)
    {
        std::error_code ec;
        if (fs::exists(p, ec))
        {
            return p.wstring();
        }
    }
    
    return L"";
}

void StrategicLevelFrame::PlayVideo(const std::string& video_file)
{
    if (video_file.empty() || !m_main || !m_spellData)
        return;

    // Convert video_file to entry name (uppercase, just filename)
    namespace fs = std::filesystem;
    std::string entry_name = fs::path(video_file).filename().string();
    for (auto& c : entry_name) c = (char)std::toupper((unsigned char)c);

    // Add extension if missing
    if (entry_name.find('.') == std::string::npos)
        entry_name += ".DPK";

    // Delegate to MainFrame for video playback
    // MainFrame handles FormVideoBox creation and synchronization
    m_main->PlayCutsceneFromStrategic(entry_name);
}

void StrategicLevelFrame::EnsureStrategicIconsLoaded()
{
    if (m_strategicIconsLoaded)
        return;
    m_strategicIconsLoaded = true;

    if (!m_spellData)
        return;

    auto& gres = m_spellData->gres;

    // Load VM_0 .. VM_9 (timeout countdown digits)
    for (int i = 0; i < 10; ++i)
    {
        char name[8];
        std::snprintf(name, sizeof(name), "VM_%d", i);
        SpellGraphicItem* item = gres.GetResource(name);
        if (item)
        {
            wxBitmap* bmp = item->Render(true); // transparent
            if (bmp && bmp->IsOk())
            {
                m_icoVM[i] = *bmp;
                delete bmp;
            }
        }
    }

    // Load LASTTERT (crossed swords for final territory)
    {
        SpellGraphicItem* item = gres.GetResource("LASTTERT");
        if (item)
        {
            wxBitmap* bmp = item->Render(true);
            if (bmp && bmp->IsOk())
            {
                m_icoLastTert = *bmp;
                delete bmp;
            }
        }
    }
}

// Helper: draw a wxBitmap centered at (cx, cy) with optional scaling
static void DrawIconCentered(wxDC& dc, const wxBitmap& bmp, int cx, int cy, double scale)
{
    if (!bmp.IsOk())
        return;

    const int srcW = bmp.GetWidth();
    const int srcH = bmp.GetHeight();
    if (srcW <= 0 || srcH <= 0)
        return;

    const int dstW = std::max(1, (int)std::lround(srcW * scale));
    const int dstH = std::max(1, (int)std::lround(srcH * scale));

    if (dstW == srcW && dstH == srcH)
    {
        // No scaling needed
        dc.DrawBitmap(bmp, cx - srcW / 2, cy - srcH / 2, true);
    }
    else
    {
        wxImage img = bmp.ConvertToImage();
        wxBitmap scaled(img.Scale(dstW, dstH, wxIMAGE_QUALITY_HIGH));
        dc.DrawBitmap(scaled, cx - dstW / 2, cy - dstH / 2, true);
    }
}

void StrategicLevelFrame::DrawTerritoryMarker(wxDC& dc, int territory_id, int x, int y, double scale)
{
    EnsureStrategicIconsLoaded();

    // Scale markers with the map but cap so they don't become huge.
    // VM icons are low-res sprites (designed for 320x200), so they need some
    // scaling on modern displays, but full map zoom (2-3x) is too much.
    const double markerScale = std::clamp(scale, 0.8, 1.6);
    const int baseRadius = 10;
    int radius = static_cast<int>(baseRadius * markerScale);
    if (radius < 6) radius = 6;

    bool isOwned = std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), territory_id) 
                   != m_ownedTerritories.end();

    bool isFinal = IsFinalTerritory(territory_id);
    int timeoutRemaining = GetTerritoryTimeoutRemaining(territory_id);

    // Check for counter-attack state
    bool counterActive = false;  // triggered, player needs to defend
    int counterTurnsLeft = -1;   // turns until counter-attack triggers
    for (const auto& ca : m_counterAttacks)
    {
        if (ca.territory_id != territory_id || ca.completed)
            continue;
        if (ca.triggered)
        {
            counterActive = true;
            break;
        }
        counterTurnsLeft = ca.trigger_turn - m_turn;
        break;
    }

    if (counterActive && isOwned)
    {
        // Counter-attack active — show LASTTERT icon (crossed swords) with orange tint
        if (m_icoLastTert.IsOk())
        {
            DrawIconCentered(dc, m_icoLastTert, x, y, markerScale);
        }
        else
        {
            // Fallback: primitive shape
            dc.SetPen(wxPen(wxColour(255, 100, 0), 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            int len = radius;
            dc.DrawLine(x - len, y - len, x + len, y + len);
            dc.DrawLine(x + len, y - len, x - len, y + len);
        }
        // Add "!" label for urgency
        dc.SetTextForeground(wxColour(255, 100, 0));
        wxFont font(wxFontInfo(std::max(8, radius)).Bold());
        dc.SetFont(font);
        dc.DrawText("!", x + radius + 2, y - radius);
    }
    else if (isFinal && !isOwned)
    {
        // LASTTERT - crossed swords icon for the final territory
        if (m_icoLastTert.IsOk())
        {
            DrawIconCentered(dc, m_icoLastTert, x, y, markerScale);
        }
        else
        {
            // Fallback: primitive crossed lines
            dc.SetPen(wxPen(wxColour(200, 50, 50), 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            int len = radius;
            dc.DrawLine(x - len, y - len, x + len, y + len);
            dc.DrawLine(x + len, y - len, x - len, y + len);
            int handleR = radius / 3;
            dc.DrawCircle(x - len, y - len, handleR);
            dc.DrawCircle(x + len, y - len, handleR);
        }
    }
    else if (timeoutRemaining > 0 && !isOwned)
    {
        // Timeout countdown: show VM_N icon (digit 0-9)
        int digit = std::clamp(timeoutRemaining, 0, 9);
        if (m_icoVM[digit].IsOk())
        {
            DrawIconCentered(dc, m_icoVM[digit], x, y, markerScale);
        }
        else
        {
            // Fallback: draw number as text with colored circle
            wxColour color;
            if (timeoutRemaining <= 2)
                color = wxColour(255, 50, 50);
            else if (timeoutRemaining <= 5)
                color = wxColour(255, 165, 0);
            else
                color = wxColour(255, 255, 100);

            dc.SetPen(wxPen(color, 2));
            dc.SetBrush(wxBrush(color, wxBRUSHSTYLE_TRANSPARENT));
            dc.DrawCircle(x, y, radius);

            dc.SetTextForeground(color);
            wxFont font(wxFontInfo(radius).Bold());
            dc.SetFont(font);

            wxString num = wxString::Format("%d", timeoutRemaining);
            wxSize textSize = dc.GetTextExtent(num);
            dc.DrawText(num, x - textSize.x / 2, y - textSize.y / 2);
        }
    }
    else if (isOwned)
    {
        // Show counter-attack countdown on owned territory using VM_N icons
        if (counterTurnsLeft > 0 && counterTurnsLeft <= 9)
        {
            int digit = std::clamp(counterTurnsLeft, 0, 9);
            if (m_icoVM[digit].IsOk())
            {
                // Draw the countdown icon slightly offset (bottom-right) to not cover the green marker
                DrawIconCentered(dc, m_icoVM[digit], x + radius, y - radius, markerScale * 0.7);
            }
            else
            {
                wxColour caColor = counterTurnsLeft <= 2 ? wxColour(255, 50, 50) : wxColour(255, 165, 0);
                dc.SetTextForeground(caColor);
                wxFont font(wxFontInfo(std::max(7, radius - 2)).Bold());
                dc.SetFont(font);
                wxString num = wxString::Format("%d", counterTurnsLeft);
                wxSize textSize = dc.GetTextExtent(num);
                dc.DrawText(num, x - textSize.x / 2, y - textSize.y / 2);
            }
        }
    }
    // else: visible but not owned, no special state — no marker drawn
}

bool StrategicLevelFrame::AreAllTerritoriesConquered() const
{
    // No hard-coded "territory 1" exception.  The starting territory is already
    // present in m_ownedTerritories, and some original levels do not start at 1.
    for (const auto& t : m_level.territories)
    {
        if (std::find(m_ownedTerritories.begin(), m_ownedTerritories.end(), t.id) == m_ownedTerritories.end())
            return false;
    }
    return true;
}

bool StrategicLevelFrame::IsFinalTerritory(int territory_id) const
{
    // End(n) is the canonical original-data definition of the crossed-swords
    // / final territory.  Prefer it over the cached convenience flag so campaign
    // completion cannot be affected by stale or incorrectly reconstructed flags.
    if (m_level.end_territory > 0)
        return territory_id == m_level.end_territory;

    // Compatibility for custom/legacy definitions without End(n).
    for (const auto& t : m_level.territories)
        if (t.id == territory_id)
            return t.is_final;
    return false;
}

int StrategicLevelFrame::GetTerritoryTimeoutRemaining(int territory_id) const
{
    auto it = m_territoryTimeoutTurn.find(territory_id);
    if (it == m_territoryTimeoutTurn.end())
        return -1;

    // Sentinel <= 0 means expired (timeout already fired)
    if (it->second <= 0)
        return 0;

    int remaining = it->second - m_turn;
    return remaining > 0 ? remaining : 0;
}

void StrategicLevelFrame::AdvanceToNextLevel()
{
    std::string nextDef;
    
    if (!m_level.next_level_def.empty() && m_level.next_level_def != "none")
    {
        nextDef = m_level.next_level_def;
    }
    
    if (m_pendingMission.valid)
    {
        const LevelMission* mission = FindMissionByNameUpper(to_upper(m_pendingMission.mission_token));
        if (mission && !mission->next_level_def.empty() && mission->next_level_def != "none")
        {
            nextDef = mission->next_level_def;
        }
    }
    
    // Play outro video
    if (!m_level.outro_video.empty() && m_level.outro_video != "none")
    {
        PlayVideo(m_level.outro_video);
    }
    
    if (nextDef.empty())
    {
        wxMessageBox(
            "Congratulations!\n\nYou have completed this level!\n\n"
            "No next level is defined.",
            "Level Complete",
            wxOK | wxICON_INFORMATION,
            this
        );
        return;
    }
    namespace fs = std::filesystem;
    fs::path base = fs::path(m_level.source_path).parent_path();
    
    std::vector<fs::path> searchPaths = {
        base / nextDef,
        base / to_upper(nextDef),
        base / ".." / nextDef,
        base / ".." / to_upper(nextDef)
    };
    
    std::string defPath;
    for (const auto& p : searchPaths)
    {
        std::error_code ec;
        if (fs::exists(p, ec))
        {
            defPath = p.string();
            break;
        }
    }
    
    if (defPath.empty())
    {
        auto levelNumberFromName = [](const std::string& name) -> int
            {
                std::smatch match;
                const std::regex pattern(R"(LEVEL[_-]?(\d{1,2}))",
                    std::regex_constants::icase);
                return std::regex_search(name, match, pattern) && match.size() >= 2
                    ? std::stoi(match[1].str()) : -1;
            };
        const int currentNumber = levelNumberFromName(
            fs::path(m_level.source_path).filename().string());
        const int requestedNumber = levelNumberFromName(nextDef);

        // LEVEL_10 is the last level of the original campaign.  LevelLoader
        // derives LEVEL_11 from the numeric filename for compatibility with
        // custom campaigns, so a missing LEVEL_11 here is a normal ending,
        // not a broken installation.
        if (currentNumber == 10 && requestedNumber == 11)
        {
            m_pendingMission.valid = false;
            SaveStrategicState();
            wxMessageBox(
                "Congratulations!\n\nYou have completed the Spellcross campaign.",
                "Campaign Complete",
                wxOK | wxICON_INFORMATION,
                this);
            return;
        }

        wxMessageBox(
            wxString::Format("Next level DEF not found: %s", nextDef.c_str()),
            "Error",
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }
    
    LevelData lvl;
    std::string err;
    LevelLoader loader;
    if (!loader.LoadLevelDef(defPath, lvl, &err))
    {
        wxMessageBox(
            wxString::Format("Failed to load next level:\n%s", err.c_str()),
            "Error",
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }
    
    auto* newWin = new StrategicLevelFrame(m_main, lvl, /*skipAutosave=*/true);

    // Transfer player progress to next level. Rank is derived from XP, so
    // normalize it on the destination frame as well.
    newWin->m_player = m_player;
    newWin->RecomputePlayerRank();
    newWin->m_money = m_money;
    newWin->m_research = m_research;
    newWin->m_playerUnits = m_playerUnits;
    newWin->m_playerCommanders = m_playerCommanders;
    newWin->m_gameModeEnabled = m_gameModeEnabled;
    newWin->m_unitStates = m_unitStates;
    newWin->m_researchActiveId = m_researchActiveId;
    newWin->m_researchActiveIndex = m_researchActiveIndex;
    newWin->m_researchAllocPerTurn = m_researchAllocPerTurn;
    newWin->m_researchCompleted = m_researchCompleted;
    newWin->m_researchProgressById = m_researchProgressById;

    // Each strategic DEF defines its own starting territories.  Do not carry
    // ownership from the old map and do not collapse multiple Start(n)
    // directives to one territory during a level transition.
    newWin->m_ownedTerritories = ChooseStartTerritories_NoBriefing(lvl, newWin->m_spellData);
    if (newWin->m_ownedTerritories.empty() && !lvl.territories.empty())
        newWin->m_ownedTerritories.push_back(lvl.territories.front().id);

    if (newWin->GetMenuBar())
    {
        auto* item = newWin->GetMenuBar()->FindItem(ID_MENU_GAME_MODE_TOGGLE);
        if (item) item->Check(newWin->m_gameModeEnabled);
    }

    // Merge start_units from the new level (bonus units the player receives at level start)
    for (const auto& su : lvl.start_units)
    {
        newWin->m_playerUnits.push_back(su);
    }

    // Preserve stable identities and allocate fresh IDs above every
    // transferred record.  Without this, the first unit/commander created in
    // a new strategic level could reuse UID 1 and corrupt assignments.
    uint32_t maxUnitUid = 0;
    for (const auto& state : newWin->m_unitStates)
        maxUnitUid = std::max(maxUnitUid, state.uid);
    newWin->m_nextRosterUid = std::max<uint32_t>(1, maxUnitUid + 1);
    while (newWin->m_unitStates.size() < newWin->m_playerUnits.size())
    {
        UnitInstanceState state;
        state.uid = newWin->m_nextRosterUid++;
        newWin->m_unitStates.push_back(state);
    }

    uint32_t maxCommanderUid = 0;
    for (const auto& commander : newWin->m_playerCommanders)
        maxCommanderUid = std::max(maxCommanderUid, commander.uid);
    newWin->m_nextCommanderUid = std::max<uint32_t>(1, maxCommanderUid + 1);

    // Always rebuild background, visibility and UI after transferring state
    newWin->TryLoadBackground();
    newWin->ApplyTerritoryVisibility();
    newWin->CheckTimeouts();
    newWin->ProcessLevelEvents();
    newWin->MarkOverlayDirty();
    newWin->RefreshUI();

    // Transfer all-time loss stats (level stats reset for new level)
    newWin->m_lossStats.alliance_all = m_lossStats.alliance_all;
    newWin->m_lossStats.enemy_all = m_lossStats.enemy_all;
    // Level-scope stats start fresh
    newWin->m_lossStats.alliance_level = {};
    newWin->m_lossStats.enemy_level = {};

    // Persist the transferred state immediately so it survives restarts
    newWin->SaveStrategicState();

    // Update parent reference
    if (m_main)
        m_main->m_strategicLevel = newWin;

    // Play intro video of the NEW level (after window is shown)
    if (!lvl.intro_video.empty() && lvl.intro_video != "none")
    {
        newWin->PlayVideo(lvl.intro_video);
    }

    newWin->Show();
    newWin->Raise();

    Destroy();
}
