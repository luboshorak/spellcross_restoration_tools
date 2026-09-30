#include "strategic_original_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "LZ_spell.h"

namespace
{
    std::string LevelAsset(const char* fmt, int level)
    {
        char name[64]{};
        std::snprintf(name, sizeof(name), fmt, level);
        return std::string(name);
    }

    std::uint16_t ReadLe16(const std::vector<std::uint8_t>& b, std::size_t off)
    {
        if(off + 1 >= b.size()) return 0;
        return static_cast<std::uint16_t>(b[off] | (static_cast<unsigned>(b[off + 1]) << 8));
    }

    void SetError(std::string* error, const std::string& text)
    {
        if(error) *error = text;
    }
}

bool StrategicOriginalRenderer::LoadExact(const AssetLoader& load,
                                           const std::string& name,
                                           std::size_t expected,
                                           std::vector<std::uint8_t>& out,
                                           std::string* error)
{
    out.clear();
    if(!load || !load(name, out))
    {
        SetError(error, "Cannot load asset: " + name);
        return false;
    }
    if(out.size() < expected)
    {
        // Some project/runtime configurations expose raw compressed *.LZ even
        // though SpellData normally opens COMMON.FS with DELZ_ALL. Be liberal
        // here: only attempt decompression when the buffer is smaller than the
        // known decoded size, so already-decoded assets are never touched.
        const std::string ext = std::filesystem::path(name).extension().string();
        if((ext == ".LZ" || ext == ".lz" || ext == ".LZ0" || ext == ".lz0") && !out.empty())
        {
            LZWexpand delz(static_cast<int>(std::max<std::size_t>(expected + 64u, 1024u * 1024u)));
            auto& dec = delz.Decode(out.data(), out.data() + out.size());
            if(!dec.empty())
                out.assign(dec.begin(), dec.end());
        }
    }
    if(out.size() < expected)
    {
        SetError(error, "Asset has unexpected size: " + name +
            " (got " + std::to_string(out.size()) + ", need " + std::to_string(expected) + ")");
        return false;
    }
    // LEVEL_06.LZ in known data has one harmless trailing byte, so permit > expected.
    if(out.size() > expected)
        out.resize(expected);
    return true;
}

bool StrategicOriginalRenderer::BuildPalette(const AssetLoader& load,
                                              int level,
                                              Palette& pal,
                                              std::string* error)
{
    for(auto& c : pal) c = {0,0,0};

    std::vector<std::uint8_t> shared;
    std::vector<std::uint8_t> levelPal;
    std::vector<std::uint8_t> bigMapPal;
    if(!LoadExact(load, "_SHARED1.PAL", 128u * 3u, shared, error)) return false;
    if(!LoadExact(load, LevelAsset("LEVEL_%02d.PAL", level), 64u * 3u, levelPal, error)) return false;
    if(!LoadExact(load, "BIG_MAP.PAL", 64u * 3u, bigMapPal, error)) return false;

    // This is the actual strategic-screen palette split observed in original data:
    //   0..127   screen chrome / VMM assets
    //   128..191 level map
    //   192..255 common BIG_MAP chrome
    for(int i = 0; i < 128; ++i)
        pal[static_cast<std::size_t>(i)] = {shared[i*3+0], shared[i*3+1], shared[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(128+i)] = {levelPal[i*3+0], levelPal[i*3+1], levelPal[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(192+i)] = {bigMapPal[i*3+0], bigMapPal[i*3+1], bigMapPal[i*3+2]};
    return true;
}

bool StrategicOriginalRenderer::BuildStrategyPalette(const AssetLoader& load,
                                                      Palette& pal,
                                                      std::string* error)
{
    for(auto& c : pal) c = {0,0,0};

    std::vector<std::uint8_t> strategy;
    std::vector<std::uint8_t> shared;
    if(!LoadExact(load, "STRATEGY.PAL", 256u * 3u, strategy, error)) return false;
    if(!LoadExact(load, "_SHARED1.PAL", 128u * 3u, shared, error)) return false;

    for(int i = 0; i < 256; ++i)
        pal[static_cast<std::size_t>(i)] = {
            strategy[static_cast<std::size_t>(i) * 3u + 0u],
            strategy[static_cast<std::size_t>(i) * 3u + 1u],
            strategy[static_cast<std::size_t>(i) * 3u + 2u]
        };

    // The original strategic screens reuse the shared stone/UI palette in the
    // lower half, overriding STRATEGY.PAL's 0..127 bank.
    for(int i = 0; i < 128; ++i)
        pal[static_cast<std::size_t>(i)] = {
            shared[static_cast<std::size_t>(i) * 3u + 0u],
            shared[static_cast<std::size_t>(i) * 3u + 1u],
            shared[static_cast<std::size_t>(i) * 3u + 2u]
        };
    return true;
}

void StrategicOriginalRenderer::BlitOpaque(std::vector<std::uint8_t>& dst,
                                            int dstW,
                                            int dstH,
                                            int dx,
                                            int dy,
                                            const std::vector<std::uint8_t>& src,
                                            int srcW,
                                            int srcH)
{
    for(int y = 0; y < srcH; ++y)
    {
        const int yy = dy + y;
        if(yy < 0 || yy >= dstH) continue;
        for(int x = 0; x < srcW; ++x)
        {
            const int xx = dx + x;
            if(xx < 0 || xx >= dstW) continue;
            dst[static_cast<std::size_t>(yy) * dstW + xx] = src[static_cast<std::size_t>(y) * srcW + x];
        }
    }
}

bool StrategicOriginalRenderer::DecodeClk(const std::vector<std::uint8_t>& bytes,
                                           int& outW,
                                           int& outH,
                                           std::vector<std::uint8_t>& values)
{
    outW = 0;
    outH = 0;
    values.clear();
    if(bytes.size() < 4) return false;

    const unsigned H = ReadLe16(bytes, 0);
    const unsigned W = ReadLe16(bytes, 2);
    if(!W || !H) return false;

    const std::size_t offsetsOff = 4;
    const std::size_t offsetsSize = static_cast<std::size_t>(H) * 2;
    if(offsetsOff + offsetsSize > bytes.size()) return false;

    std::vector<unsigned> offsets(H);
    for(unsigned y = 0; y < H; ++y)
        offsets[y] = ReadLe16(bytes, offsetsOff + static_cast<std::size_t>(y) * 2);

    values.assign(static_cast<std::size_t>(W) * H, 0);
    for(unsigned y = 0; y < H; ++y)
    {
        const unsigned start = offsets[y];
        const unsigned end = (y + 1 < H) ? offsets[y + 1] : static_cast<unsigned>(bytes.size());
        if(start >= bytes.size() || end > bytes.size() || end <= start) continue;

        std::size_t x = 0;
        for(unsigned i = start; i + 1 < end && x < W; i += 2)
        {
            const unsigned run = bytes[i];
            const std::uint8_t val = bytes[i + 1];
            if(!run) continue;
            const std::size_t x2 = std::min<std::size_t>(W, x + run);
            std::fill(values.begin() + static_cast<std::size_t>(y) * W + x,
                      values.begin() + static_cast<std::size_t>(y) * W + x2,
                      val);
            x = x2;
        }
    }

    outW = static_cast<int>(W);
    outH = static_cast<int>(H);
    return true;
}

void StrategicOriginalRenderer::GenerateHatch(const std::vector<std::uint8_t>& territoryMask,
                                               int w,
                                               int h,
                                               int territoryCount,
                                               std::vector<std::uint8_t>& hatch)
{
    hatch.assign(static_cast<std::size_t>(w) * h, 0);

    auto plot = [&](int territory, int x1, int y1, int x2, int y2)
    {
        const int dx = std::abs(x2 - x1);
        const int sx = x1 < x2 ? 1 : -1;
        const int dy = -std::abs(y2 - y1);
        const int sy = y1 < y2 ? 1 : -1;
        int err = dx + dy;
        for(;;)
        {
            if(x1 >= 0 && x1 < w && y1 >= 0 && y1 < h)
            {
                const std::size_t p = static_cast<std::size_t>(y1) * w + x1;
                if(territoryMask[p] == territory)
                    hatch[p] = static_cast<std::uint8_t>(territory);
            }
            if(x1 == x2 && y1 == y2) break;
            const int e2 = 2 * err;
            if(e2 >= dy) { err += dy; x1 += sx; }
            if(e2 <= dx) { err += dx; y1 += sy; }
        }
    };

    // Same 2px / 7px descending diagonal pattern used by Maslan's save editor.
    // It matches the hatch visible in the original strategic-map screenshot.
    for(int territory = 1; territory <= territoryCount; ++territory)
    {
        for(int x = 0; x < 2*w; x += 7)
        {
            for(int s = 0; s < 2; ++s)
            {
                const int x1 = x + s;
                plot(territory, x1, 0, x1 - (h - 1), h - 1);
            }
        }

        // Add a 2px boundary around the territory, matching the original look.
        for(int y = 0; y < h; ++y)
        {
            for(int dir : {+1,-1})
            {
                int edge = 0;
                for(int x = 0; x < w; ++x)
                {
                    const int xx = (dir < 0 ? w - 1 : 0) + x * dir;
                    const std::size_t p = static_cast<std::size_t>(y) * w + xx;
                    if(edge || territoryMask[p] == territory) ++edge;
                    if(edge) hatch[p] = static_cast<std::uint8_t>(territory);
                    if(edge > 1) break;
                }
            }
        }
        for(int x = 0; x < w; ++x)
        {
            for(int dir : {+1,-1})
            {
                int edge = 0;
                for(int y = 0; y < h; ++y)
                {
                    const int yy = (dir < 0 ? h - 1 : 0) + y * dir;
                    const std::size_t p = static_cast<std::size_t>(yy) * w + x;
                    if(edge || territoryMask[p] == territory) ++edge;
                    if(edge) hatch[p] = static_cast<std::uint8_t>(territory);
                    if(edge > 1) break;
                }
            }
        }
    }
}

bool StrategicOriginalRenderer::RenderStrategicMap(const AssetLoader& load,
                                                     const MapState& state,
                                                     RgbImage& out,
                                                     std::string* error) const
{
    out = {};
    if(state.level < 2 || state.level > 99)
    {
        SetError(error, "Unsupported strategic level number");
        return false;
    }

    Palette pal{};
    if(!BuildPalette(load, state.level, pal, error)) return false;

    std::vector<std::uint8_t> bigMap, vmm, hmla, level, clk, lst1, lst2;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMM_FULL.LZ", kScreenSpecificW * kScreenH, vmm, error)) return false;
    if(!LoadExact(load, LevelAsset("HMLA__%02d.LZ", state.level), kMapW * kMapH, hmla, error)) return false;
    if(!LoadExact(load, LevelAsset("LEVEL_%02d.LZ", state.level), kMapW * kMapH, level, error)) return false;
    if(!load(LevelAsset("LEVEL_%02d.CLK", state.level), clk))
    {
        SetError(error, "Cannot load CLK territory mask");
        return false;
    }
    if(!LoadExact(load, "VMM_LST1.LZ", 163u * 41u, lst1, error)) return false;
    if(!LoadExact(load, "VMM_LST2.LZ", 406u * 174u, lst2, error)) return false;

    int clkW = 0, clkH = 0;
    std::vector<std::uint8_t> territoryMask;
    if(!DecodeClk(clk, clkW, clkH, territoryMask) || clkW != kMapW || clkH != kMapH)
    {
        SetError(error, "Invalid CLK territory mask dimensions");
        return false;
    }

    int territoryCount = 0;
    for(std::uint8_t v : territoryMask)
        if(v > 0 && v < 128) territoryCount = std::max(territoryCount, static_cast<int>(v));

    std::vector<std::uint8_t> hatch;
    GenerateHatch(territoryMask, kMapW, kMapH, territoryCount, hatch);

    // Indexed 640x480 canvas. BIG_MAP owns common right chrome; VMM owns left 575 px.
    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vmm, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, kMapX, kMapY, hmla, kMapW, kMapH);

    // Reveal only territory pixels whose game-state view says so. CLK stores edge
    // pixels as 0x80+territory, therefore normalize that high bit for state lookup.
    for(int y = 0; y < kMapH; ++y)
    {
        for(int x = 0; x < kMapW; ++x)
        {
            const std::size_t p = static_cast<std::size_t>(y) * kMapW + x;
            const std::uint8_t rawTerr = territoryMask[p];
            const int terr = rawTerr >= 128 ? rawTerr - 128 : rawTerr;
            if(terr <= 0) continue;

            TerritoryVisualState vis = TerritoryVisualState::Hidden;
            if(static_cast<std::size_t>(terr) < state.territories.size())
                vis = state.territories[static_cast<std::size_t>(terr)];

            if(vis != TerritoryVisualState::Hidden && level[p] != 0)
                canvas[static_cast<std::size_t>(kMapY + y) * kScreenW + (kMapX + x)] = level[p];
        }
    }

    // Exact original strategic-map subpanels found by matching against reference.
    BlitOpaque(canvas, kScreenW, kScreenH, 6, 298, lst2, 406, 174);
    BlitOpaque(canvas, kScreenW, kScreenH, 412, 434, lst1, 163, 41);

    // Convert the indexed framebuffer once. No layout manager touches coordinates.
    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3+0] = c[0];
        out.rgb[p*3+1] = c[1];
        out.rgb[p*3+2] = c[2];
    }

    // Hatch is deliberately drawn after indexed->RGB conversion because the
    // original red is a UI overlay, not part of the level's 64-color palette.
    for(int y = 0; y < kMapH; ++y)
    {
        for(int x = 0; x < kMapW; ++x)
        {
            const std::size_t p = static_cast<std::size_t>(y) * kMapW + x;
            const int terr = hatch[p];
            if(terr <= 0) continue;
            TerritoryVisualState vis = TerritoryVisualState::Hidden;
            if(static_cast<std::size_t>(terr) < state.territories.size())
                vis = state.territories[static_cast<std::size_t>(terr)];
            if(vis != TerritoryVisualState::EnemyHatched) continue;

            const std::size_t q = (static_cast<std::size_t>(kMapY + y) * kScreenW + (kMapX + x)) * 3;
            out.rgb[q+0] = 255;
            out.rgb[q+1] = 0;
            out.rgb[q+2] = 0;
        }
    }

    return true;
}


bool StrategicOriginalRenderer::RenderHierarchy(const AssetLoader& load,
                                                  RgbImage& out,
                                                  std::string* error) const
{
    out = {};

    Palette pal{};
    for(auto& c : pal) c = {0,0,0};

    std::vector<std::uint8_t> shared, hierarchyPal, bigMapPal;
    if(!LoadExact(load, "_SHARED1.PAL", 128u * 3u, shared, error)) return false;
    if(!LoadExact(load, "_HIERAR.PAL", 64u * 3u, hierarchyPal, error)) return false;
    if(!LoadExact(load, "BIG_MAP.PAL", 64u * 3u, bigMapPal, error)) return false;

    for(int i = 0; i < 128; ++i)
        pal[static_cast<std::size_t>(i)] = {shared[i*3+0], shared[i*3+1], shared[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(128+i)] = {hierarchyPal[i*3+0], hierarchyPal[i*3+1], hierarchyPal[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(192+i)] = {bigMapPal[i*3+0], bigMapPal[i*3+1], bigMapPal[i*3+2]};

    std::vector<std::uint8_t> bigMap, vmh, hierarchy;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMH_FULL.LZ", kScreenSpecificW * kScreenH, vmh, error)) return false;
    if(!LoadExact(load, "HIERARCH.LZ", 406u * 464u, hierarchy, error)) return false;

    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vmh, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, 6, 8, hierarchy, 406, 464);

    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3+0] = c[0];
        out.rgb[p*3+1] = c[1];
        out.rgb[p*3+2] = c[2];
    }
    return true;
}


bool StrategicOriginalRenderer::RenderUnits(const AssetLoader& load,
                                              RgbImage& out,
                                              std::string* error) const
{
    out = {};

    Palette pal{};
    for(auto& c : pal) c = {0,0,0};

    std::vector<std::uint8_t> shared, unitsPal, bigMapPal;
    if(!LoadExact(load, "_SHARED1.PAL", 128u * 3u, shared, error)) return false;
    if(!LoadExact(load, "_UNITS.PAL", 64u * 3u, unitsPal, error)) return false;
    if(!LoadExact(load, "BIG_MAP.PAL", 64u * 3u, bigMapPal, error)) return false;

    for(int i = 0; i < 128; ++i)
        pal[static_cast<std::size_t>(i)] = {shared[i*3+0], shared[i*3+1], shared[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(128+i)] = {unitsPal[i*3+0], unitsPal[i*3+1], unitsPal[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(192+i)] = {bigMapPal[i*3+0], bigMapPal[i*3+1], bigMapPal[i*3+2]};

    std::vector<std::uint8_t> bigMap, vmu, units, infoPanel, actionStrip;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMU_FULL.LZ", kScreenSpecificW * kScreenH, vmu, error)) return false;
    if(!LoadExact(load, "UNITS.LZ", 406u * 464u, units, error)) return false;
    if(!LoadExact(load, "VMU_LST2.LZ", 241u * 141u, infoPanel, error)) return false;
    if(!LoadExact(load, "VMU_LST1.LZ", 154u * 41u, actionStrip, error)) return false;

    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vmu, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, 6, 8, units, 406, 464);
    // These two resources are the original dynamic lower-right unit panel.
    BlitOpaque(canvas, kScreenW, kScreenH, 334, 291, infoPanel, 241, 141);
    BlitOpaque(canvas, kScreenW, kScreenH, 421, 434, actionStrip, 154, 41);

    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3+0] = c[0];
        out.rgb[p*3+1] = c[1];
        out.rgb[p*3+2] = c[2];
    }
    return true;
}


bool StrategicOriginalRenderer::RenderBuy(const AssetLoader& load,
                                            const BuyState& state,
                                            RgbImage& out,
                                            std::string* error) const
{
    out = {};

    Palette pal{};
    for(auto& c : pal) c = {0,0,0};

    // BUY/VMB resources use the shared 0..127 strategic palette. The original
    // _BUY.PAL is effectively empty, so unlike hierarchy/units there is no
    // meaningful 128..191 screen-specific bank to install here.
    std::vector<std::uint8_t> shared, bigMapPal;
    if(!LoadExact(load, "_SHARED1.PAL", 128u * 3u, shared, error)) return false;
    if(!LoadExact(load, "BIG_MAP.PAL", 64u * 3u, bigMapPal, error)) return false;

    for(int i = 0; i < 128; ++i)
        pal[static_cast<std::size_t>(i)] = {shared[i*3+0], shared[i*3+1], shared[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(192+i)] = {bigMapPal[i*3+0], bigMapPal[i*3+1], bigMapPal[i*3+2]};

    std::vector<std::uint8_t> bigMap, vmb, buy, infoPanel, actionStrip, disabledUnit, disabledCommander;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMB_FULL.LZ", kScreenSpecificW * kScreenH, vmb, error)) return false;
    if(!LoadExact(load, "BUY.LZ", 406u * 464u, buy, error)) return false;
    if(!LoadExact(load, "VMB_LST2.LZ", 241u * 141u, infoPanel, error)) return false;
    if(!LoadExact(load, "VMB_LST1.LZ", 163u * 41u, actionStrip, error)) return false;
    if(!LoadExact(load, "VMB_DIS.LZ", 146u * 17u, disabledUnit, error)) return false;
    if(!LoadExact(load, "VMB_DIS2.LZ", 132u * 16u, disabledCommander, error)) return false;

    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vmb, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, 6, 8, buy, 406, 464);
    // VMB_LST2 begins at y=292 in the native screen.  y=291 left row 432
    // exposed from VMB_FULL as a solid black seam above the action strip.
    BlitOpaque(canvas, kScreenW, kScreenH, 334, 292, infoPanel, 241, 141);
    BlitOpaque(canvas, kScreenW, kScreenH, 412, 434, actionStrip, 163, 41);

    // BUY.LZ contains all 32 permanent-unit slots and 14 commander slots in
    // their unlocked form. The original game darkens the part unavailable at
    // the current rank with two tiny overlays from COMMON.FS.
    const int maxUnits = std::clamp(state.maxPermanentUnits, 0, 32);
    for(int slot = maxUnits; slot < 32; ++slot)
    {
        const int col = slot / 16;
        const int row = slot % 16;
        BlitOpaque(canvas, kScreenW, kScreenH,
            16 + col * 152, 10 + row * 19, disabledUnit, 146, 17);
    }

    const int maxCommanders = std::clamp(state.maxCommanders, 0, 14);
    for(int slot = maxCommanders; slot < 14; ++slot)
    {
        const int col = slot / 7;
        const int row = slot % 7;
        BlitOpaque(canvas, kScreenW, kScreenH,
            21 + col * 153, 331 + row * 19, disabledCommander, 132, 16);
    }

    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3+0] = c[0];
        out.rgb[p*3+1] = c[1];
        out.rgb[p*3+2] = c[2];
    }
    return true;
}

bool StrategicOriginalRenderer::RenderResearch(const AssetLoader& load,
                                                RgbImage& out,
                                                std::string* error) const
{
    out = {};

    Palette pal{};
    if(!BuildStrategyPalette(load, pal, error)) return false;

    std::vector<std::uint8_t> bigMap, vmr, researchBg, actionStrip;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMR_FULL.LZ", kScreenSpecificW * kScreenH, vmr, error)) return false;
    if(!LoadExact(load, "RSRCH_BG.LZ", 406u * 464u, researchBg, error)) return false;
    if(!LoadExact(load, "VMR_LST1.LZ", 120u * 40u, actionStrip, error)) return false;

    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vmr, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, 6, 8, researchBg, 406, 464);
    BlitOpaque(canvas, kScreenW, kScreenH, 277, 431, actionStrip, 120, 40);

    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3u);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3u+0u] = c[0];
        out.rgb[p*3u+1u] = c[1];
        out.rgb[p*3u+2u] = c[2];
    }
    return true;
}


bool StrategicOriginalRenderer::RenderInfo(const AssetLoader& load,
                                            RgbImage& out,
                                            std::string* error) const
{
    out = {};

    Palette pal{};
    if(!BuildStrategyPalette(load, pal, error)) return false;

    std::vector<std::uint8_t> bigMap, vmi, infoBg;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMI_FULL.LZ", kScreenSpecificW * kScreenH, vmi, error)) return false;
    if(!LoadExact(load, "INFO.LZ", 412u * 464u, infoBg, error)) return false;

    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vmi, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 8, infoBg, 412, 464);

    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3u);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3u+0u] = c[0];
        out.rgb[p*3u+1u] = c[1];
        out.rgb[p*3u+2u] = c[2];
    }
    return true;
}


bool StrategicOriginalRenderer::RenderResources(const AssetLoader& load,
                                                 RgbImage& out,
                                                 std::string* error) const
{
    out = {};

    Palette pal{};
    if(!BuildStrategyPalette(load, pal, error)) return false;

    std::vector<std::uint8_t> bigMap, vmf, factory;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMF_FULL.LZ", kScreenSpecificW * kScreenH, vmf, error)) return false;
    if(!LoadExact(load, "FACTORY.LZ", 569u * 464u, factory, error)) return false;

    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vmf, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, 3, 8, factory, 569, 464);

    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3u);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3u+0u] = c[0];
        out.rgb[p*3u+1u] = c[1];
        out.rgb[p*3u+2u] = c[2];
    }
    return true;
}


bool StrategicOriginalRenderer::RenderStats(const AssetLoader& load,
                                             RgbImage& out,
                                             std::string* error) const
{
    out = {};

    Palette pal{};
    if(!BuildStrategyPalette(load, pal, error)) return false;

    std::vector<std::uint8_t> bigMap, vms, stats;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMS_FULL.LZ", kScreenSpecificW * kScreenH, vms, error)) return false;
    if(!LoadExact(load, "STATS.LZ", 569u * 464u, stats, error)) return false;

    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vms, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, 3, 8, stats, 569, 464);

    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3u);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3u+0u] = c[0];
        out.rgb[p*3u+1u] = c[1];
        out.rgb[p*3u+2u] = c[2];
    }
    return true;
}


bool StrategicOriginalRenderer::RenderOptions(const AssetLoader& load,
                                               const OptionsState& state,
                                               RgbImage& out,
                                               std::string* error) const
{
    out = {};

    Palette pal{};
    for(auto& c : pal) c = {0,0,0};

    std::vector<std::uint8_t> shared, optionsPal, bigMapPal;
    if(!LoadExact(load, "_SHARED1.PAL", 128u * 3u, shared, error)) return false;
    if(!LoadExact(load, "_OPTIONS.PAL", 64u * 3u, optionsPal, error)) return false;
    if(!LoadExact(load, "BIG_MAP.PAL", 64u * 3u, bigMapPal, error)) return false;

    for(int i = 0; i < 128; ++i)
        pal[static_cast<std::size_t>(i)] = {shared[i*3+0], shared[i*3+1], shared[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(128+i)] = {optionsPal[i*3+0], optionsPal[i*3+1], optionsPal[i*3+2]};
    for(int i = 0; i < 64; ++i)
        pal[static_cast<std::size_t>(192+i)] = {bigMapPal[i*3+0], bigMapPal[i*3+1], bigMapPal[i*3+2]};

    std::vector<std::uint8_t> bigMap, vmo, options, slider;
    if(!LoadExact(load, "BIG_MAP.LZ", kScreenW * kScreenH, bigMap, error)) return false;
    if(!LoadExact(load, "VMO_FULL.LZ", kScreenSpecificW * kScreenH, vmo, error)) return false;
    if(!LoadExact(load, "OPTIONS.LZ", 569u * 464u, options, error)) return false;
    if(!LoadExact(load, "VMO_BAR.LZ", 10u * 12u, slider, error)) return false;

    std::vector<std::uint8_t> canvas = bigMap;
    BlitOpaque(canvas, kScreenW, kScreenH, 0, 0, vmo, kScreenSpecificW, kScreenH);
    BlitOpaque(canvas, kScreenW, kScreenH, 3, 8, options, 569, 464);

    // Native slider travel: black groove x=47..186, 10 px thumb.
    // Keep the whole thumb inside that groove, matching the DOS OPTIONS screen.
    auto sliderX = [](int percent)
    {
        return 47 + (std::clamp(percent, 0, 100) * 130 + 50) / 100;
    };
    BlitOpaque(canvas, kScreenW, kScreenH, sliderX(state.gammaPercent), 356, slider, 10, 12);
    BlitOpaque(canvas, kScreenW, kScreenH, sliderX(state.musicPercent), 403, slider, 10, 12);
    BlitOpaque(canvas, kScreenW, kScreenH, sliderX(state.soundPercent), 448, slider, 10, 12);

    out.width = kScreenW;
    out.height = kScreenH;
    out.rgb.resize(static_cast<std::size_t>(kScreenW) * kScreenH * 3u);
    for(std::size_t p = 0; p < canvas.size(); ++p)
    {
        const auto& c = pal[canvas[p]];
        out.rgb[p*3u+0u] = c[0];
        out.rgb[p*3u+1u] = c[1];
        out.rgb[p*3u+2u] = c[2];
    }
    return true;
}
