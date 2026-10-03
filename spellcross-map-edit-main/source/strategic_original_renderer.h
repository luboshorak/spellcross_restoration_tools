#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Independent renderer for the original Spellcross strategic-map screen.
// It deliberately owns no game logic and no wxWidgets controls. The caller
// supplies decoded asset bytes + territory states and receives a 640x480 RGB
// framebuffer. This makes it suitable as the second ("Original") UI branch.
class StrategicOriginalRenderer
{
public:
    static constexpr int kScreenW = 640;
    static constexpr int kScreenH = 480;
    static constexpr int kScreenSpecificW = 575;
    static constexpr int kMapX = 18;
    static constexpr int kMapY = 18;
    static constexpr int kMapW = 379;
    static constexpr int kMapH = 259;

    enum class TerritoryVisualState : std::uint8_t
    {
        Hidden = 0,       // HMLA (fog) only
        Revealed = 1,     // LEVEL pixels
        EnemyHatched = 2 // LEVEL pixels + original-style red hatch
    };

    struct RgbImage
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgb; // width*height*3, RGB order
    };

    // Asset loader contract:
    // - .PAL/.CLK: raw file bytes
    // - .LZ: *already decompressed* payload bytes (as exposed in the remake's
    //   extracted COMMON temp folder). Runtime integration can simply wrap the
    //   project's existing LZ/FS loader behind this callback.
    using AssetLoader = std::function<bool(const std::string& name,
                                           std::vector<std::uint8_t>& out)>;

    struct MapState
    {
        int level = 2;
        // Index 1 == territory #1. Index 0 is ignored. Missing entries are Hidden.
        std::vector<TerritoryVisualState> territories;

        // Native strategic-map hover effect: enemy hatching scrolls while the
        // pointer is over a revealed, unconquered territory. animationPhase is
        // deliberately small (0..6) because the DOS hatch has a 7 px period.
        int hoverTerritory = 0;
        int animationPhase = 0;
    };

    bool RenderStrategicMap(const AssetLoader& load,
                            const MapState& state,
                            RgbImage& out,
                            std::string* error = nullptr) const;

    // Restored hierarchy screen. This is a static original Spellcross skin
    // (VMH_FULL + HIERARCH + BIG_MAP chrome); the caller overlays live
    // hierarchy/unit/commander data and hit-testing on top.
    bool RenderHierarchy(const AssetLoader& load,
                         RgbImage& out,
                         std::string* error = nullptr) const;

    // Restored unit-management screen. UNITS.LZ supplies the original roster
    // grid and inner chrome; VMU_FULL/BIG_MAP provide the common screen shell.
    // The caller overlays live roster/status/action data.
    bool RenderUnits(const AssetLoader& load,
                     RgbImage& out,
                     std::string* error = nullptr) const;

    struct BuyState
    {
        int maxPermanentUnits = 32;
        int maxCommanders = 14;
    };

    // Restored new-unit purchase screen. BUY.LZ and VMB_* provide the native
    // DOS layout. VMB_DIS/VMB_DIS2 are composited over slots which are locked
    // by John Alexander's current rank; all live text/data stays in the caller.
    bool RenderBuy(const AssetLoader& load,
                   const BuyState& state,
                   RgbImage& out,
                   std::string* error = nullptr) const;

    // Restored research screen: VMR_FULL + RSRCH_BG + native lower action strip.
    // Live research names, progress, descriptions and list state are drawn by
    // the caller so both UI branches share one research model.
    bool RenderResearch(const AssetLoader& load,
                        RgbImage& out,
                        std::string* error = nullptr) const;

    // Restored encyclopedia/info screen: VMI_FULL + INFO.
    bool RenderInfo(const AssetLoader& load,
                    RgbImage& out,
                    std::string* error = nullptr) const;

    // Restored territory/resource allocation screen: VMF_FULL + FACTORY.
    bool RenderResources(const AssetLoader& load,
                         RgbImage& out,
                         std::string* error = nullptr) const;

    // Restored statistics screen: VMS_FULL + STATS.
    bool RenderStats(const AssetLoader& load,
                     RgbImage& out,
                     std::string* error = nullptr) const;

    // Restored save/options screen: VMO_FULL + OPTIONS. VMO_BAR is the
    // native 10x12 slider thumb, positioned from live gamma/audio values.
    struct OptionsState
    {
        int gammaPercent = 50;
        int musicPercent = 100;
        int soundPercent = 50;
    };
    bool RenderOptions(const AssetLoader& load,
                       const OptionsState& state,
                       RgbImage& out,
                       std::string* error = nullptr) const;

    // Public so the controller/view can reuse the decoded CLK mask for hit tests.
    static bool DecodeClk(const std::vector<std::uint8_t>& bytes,
                          int& outW,
                          int& outH,
                          std::vector<std::uint8_t>& values);

private:
    using Palette = std::array<std::array<std::uint8_t,3>,256>;

    static bool LoadExact(const AssetLoader& load,
                          const std::string& name,
                          std::size_t expected,
                          std::vector<std::uint8_t>& out,
                          std::string* error);
    static bool BuildPalette(const AssetLoader& load,
                             int level,
                             Palette& pal,
                             std::string* error);
    static bool BuildStrategyPalette(const AssetLoader& load,
                                     Palette& pal,
                                     std::string* error);
    static void BlitOpaque(std::vector<std::uint8_t>& dst,
                           int dstW,
                           int dstH,
                           int dx,
                           int dy,
                           const std::vector<std::uint8_t>& src,
                           int srcW,
                           int srcH);
    // Composite the original DOS strategic list chrome from COMMON.FS.
    // SB_BG* contains the CRT surface, metal surround and normal arrow buttons;
    // SB_BAR* is the original metal scrollbar track. No procedural substitute.
    static bool BlitListChrome(const AssetLoader& load,
                               std::vector<std::uint8_t>& dst,
                               const char* bgName,
                               int bgH,
                               const char* barName,
                               int barH,
                               std::string* error);
    static void GenerateHatch(const std::vector<std::uint8_t>& territoryMask,
                              int w,
                              int h,
                              int territoryCount,
                              std::vector<std::uint8_t>& hatch,
                              int phase = 0);
};
