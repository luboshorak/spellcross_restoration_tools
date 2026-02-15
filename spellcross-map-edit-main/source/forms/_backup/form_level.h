#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/statbmp.h>
#include <wx/bmpbuttn.h>
#include <wx/simplebook.h>
#include <wx/slider.h>
#include <wx/dnd.h>
#include <wx/scrolwin.h>

#include "level.h"

class MainFrame;
class SpellData;

class StrategicLevelFrame : public wxFrame
{
public:
    StrategicLevelFrame(MainFrame* parent, const LevelData& level);

    void BuildUI();
    void RefreshUI();

    void BuildResourcesPage();
    void RefreshResourcesPage();
    void ApplyResourceTickEndTurn();

    // background (LEVEL_XX.LZ + LEVEL_XX.PAL) - best effort
    void TryLoadBackground();
    void OnMapPaint(wxPaintEvent& ev);
    void OnMapLeftDown(wxMouseEvent& ev);
    void OnActivate(wxActivateEvent& ev);

    void RebuildTerritoryCentroids();

    // actions
    void OnTerritory(wxCommandEvent& ev);
    void SelectTerritoryById(int territory_id);
    void OnResearch(wxCommandEvent& ev);
    void OnBuyUnits(wxCommandEvent& ev);
    void OnBuyCommander(wxCommandEvent& ev);
    void OnSellUnits(wxCommandEvent& ev);
    void OnEndTurn(wxCommandEvent& ev);
    void OnLaunch(wxCommandEvent& ev);
    void OnShowStrategicMap(wxCommandEvent& ev);
    void OnShowHierarchy(wxCommandEvent& ev);
    void OnShowStats(wxCommandEvent& ev);
    void OnShowResources(wxCommandEvent& ev);


// menu (Strategic Level saves)
void BuildMenu();
void OnSaveGame(wxCommandEvent& ev);
void OnLoadGame(wxCommandEvent& ev);

void OnOptionsAudio(wxCommandEvent& ev);

struct PlayerProgress
{
    std::string name = "John Alexander";
    int rank = 0;
    int experience = 0;
    int actions = 0;
};

    struct CommanderRankRec
    {
        int rank = 0;
        int max_units = 0;
        int actions_required = 0;
        int exp_required = 0;
        int max_commanders = 0;
    };

    struct LossBlock
    {
        int light = 0;
        int heavy = 0;
        int air = 0;
        int commanders = 0;
    };

    struct LossStats
    {
        LossBlock alliance_all;
        LossBlock enemy_all;
        LossBlock alliance_level;
        LossBlock enemy_level;
    };


    void LoadStrategicState();
    void SaveStrategicState() const;
    wxString GetUnitDisplayName(int unit_id) const;
    bool EnsureUnitCostsLoaded();
    int GetUnitBuyCost(int unit_id) const;

    // mission selection / progression
    std::string ResolveMissionTokenForTerritory(int territory_id) const;
    std::wstring ResolveMapDefPathForMissionToken(const std::string& mission_token) const;
    const LevelMission* FindMissionByNameUpper(const std::string& name_upper) const;

private:
    struct HierarchySlot
    {
        std::string id;
        std::string type;
        int rank = -1; // for commander slots
        uint32_t commander_uid = 0; // for commander slots
        // commander slots: store commander name so label can be rebuilt with rank / assigned unit
        std::string commander_name;

        // unit slots: unique unit instance id (0 = empty)
        uint32_t unit_uid = 0;
        wxString unit_display;

        // commander slots: which unit (uid) is the commander's assigned unit
        uint32_t assigned_unit_uid = 0;
        wxString assigned_unit_display;

        wxStaticText* label = nullptr;
        wxString placeholder;
    };

    struct UiPalette
    {
        wxColour text;
        wxColour heading;
        wxColour background;
        wxColour inactive;
        wxColour statusHeading;
        wxColour statusNumber;
        wxColour buttonText;
        wxColour buttonBackground;
        wxColour shadow;
    };

    // stats page helpers (integrated from former form_strategic.*)
    void BuildStatsPage();
    void RefreshStatsPage();
    void LoadRanksTable();
    void LoadMissionStatsIfPresent();
    void RecomputePlayerRank();
    const CommanderRankRec* FindRankRec(int rank) const;
    int FindNextRankExp(int current_rank) const;
    wxString GetRankNameCz(int rank) const;

public:

    void BuildHierarchyPage(wxPanel* parent);
    wxWindow* BuildHierarchyBookPage(wxWindow* parent, int brigadeIndex);
    wxPanel* BuildHierarchyFormation(wxWindow* parent,
                                     const wxString& label,
                                     const wxColour& color,
                                     wxSizer* contents);
    wxPanel* BuildHierarchySlot(wxWindow* parent,
                                const wxString& placeholder,
                                const std::string& slotId,
                                const std::string& type);
    void RegisterHierarchySlot(const std::string& slotId,
                               const std::string& type,
                               wxStaticText* label,
                               const wxString& placeholder);
    void ApplyHierarchyDrop(const std::string& slotId, const wxString& data);
    void ChooseUnitForHierarchySlot(const std::string& unitSlotId);
    void ChooseAssignedUnitForCommanderAssignmentSlot(const std::string& assignmentSlotId);
    void TryAssignCommanderToUnitSlot(const std::string& unitSlotId);
    std::string GetCommanderSlotForUnitSlot(const std::string& unitSlotId) const;
    struct RosterPickItem { uint32_t uid; wxString display; wxString label; };
    std::vector<RosterPickItem> GetRosterPickItems() const;
    void ClearHierarchySlot(const std::string& slotId);
    void BeginHierarchySlotDrag(const std::string& slotId, wxWindow* source);
    void OnHierarchyTogglePage(wxCommandEvent& ev);
    void OnRosterBeginDrag(wxListEvent& event);
    void OnCommanderBeginDrag(wxListEvent& event);

    void UpdateCommanderHierarchyLabel(const std::string& commanderSlotId);

public:

    // commanders
    wxString GetRankAbbrev(int rank) const;
    void MaybeGenerateCommanderOffer();
    bool EnsureCommanderNamesLoaded();

    wxString FindHodnostiDefPath() const;
    wxString FindStrategicStatsPath() const;

    MainFrame* m_main = nullptr;
    SpellData* m_spellData = nullptr;
    LevelData m_level;

    // simple strategic state (in-memory for now)
    int m_turn = 1;
    int m_money = 0;
    int m_research = 0;
    int m_selectedTerritory = -1;

    PlayerProgress m_player;

    // per-territory state: current mission token + number of launches
    std::unordered_map<int, std::string> m_territoryCurrentMission;
    std::unordered_map<int, int> m_territoryLaunchCount;

    std::vector<LevelData::PlayerUnitAdd> m_playerUnits;

    // Per-roster-row unique IDs (session-stable). Used for hierarchy assignment.
    mutable std::vector<uint32_t> m_rosterRowUids;
    mutable uint32_t m_nextRosterUid = 1;
    std::unordered_map<int, int> m_unitCosts;
    bool m_unitCostsLoaded = false;

    // Game mode (campaign progression)
    bool m_gameModeEnabled = false;
    std::vector<int> m_ownedTerritories;

    struct TerritoryResourceState
    {
        int total = 20;
        int remaining = 20;
        // 0..100: percent of extracted resource "ticks" routed toward research.
        int researchPercent = 0;
        // 0..99 accumulator for deterministic ratio routing.
        int allocAccum = 0;
        // 0..3 carry for converting 4 resource ticks -> 1 research point.
        int researchCarry = 0;
    };

    std::unordered_map<int, TerritoryResourceState> m_territoryResources;

    // Territory visibility / overlay for Game mode
    std::vector<uint32_t> m_territoryAdjMask; // indexed by territory id (1..N)
    std::vector<uint8_t>  m_visibleTerritory; // 0/1 per territory id
    int m_hoverTerritory = 0;

    // Overlay cache
    bool m_overlayDirty = true;
    wxBitmap m_overlayBitmap;
    wxBitmap m_overlayBitmapScaled;
    int m_overlayScaledW = -1;
    int m_overlayScaledH = -1;

    // Last draw transform (map panel -> background bitmap)
    double m_lastMapScale = 1.0;
    int m_lastMapOffX = 0;
    int m_lastMapOffY = 0;
    int m_lastBgW = 0;
    int m_lastBgH = 0;

    std::string m_compositeFolder; // where LEVEL_XX.* were found (for SSD)

    void OnToggleGameMode(wxCommandEvent& ev);
    void OnMapMouseMove(wxMouseEvent& ev);
    void ApplyTerritoryVisibility();
    void MarkOverlayDirty();

struct CommanderRec
{
    // Unique commander instance id (session-stable). Used to prevent the same commander
    // being assigned into multiple hierarchy slots.
    uint32_t uid = 0;
    std::string name;
    int rank = 0;
};

// owned commanders (max 14)
std::vector<CommanderRec> m_playerCommanders;

// session-stable commander UID generator (used when uid==0)
mutable uint32_t m_nextCommanderUid = 1;

// runtime helpers (uid -> rank) for drag payload construction
mutable std::unordered_map<uint32_t, int> m_commanderRankByUid;

// available commanders to buy in current turn (usually 0 or 1)
std::vector<CommanderRec> m_availableCommanders;

// generation limits: max 2 commanders per 25 turns window
int m_cmdGenWindowStartTurn = 1;
int m_cmdGenCountInWindow = 0;

// commander names source
std::vector<std::string> m_commanderNames;
bool m_commanderNamesLoaded = false;

    // decoded CLK territory map for click-detection
    std::vector<unsigned char> m_clkValues;
    int m_clkW = 0;
    int m_clkH = 0;
    bool m_hasClk = false;


    // territory id -> centroid (pixel coords in background bitmap space)
    std::unordered_map<int, wxPoint> m_territoryCentroids;

    // background bitmap
    wxBitmap m_bgBitmap;
    bool m_hasBg = false;

    // statistics model (integrated from former form_strategic.*)
    std::vector<CommanderRankRec> m_ranks;
    LossStats m_lossStats;

    UiPalette m_palette;
    wxFont m_fontText;
    wxFont m_fontHeading;

    // stats page widgets (TTF-rendered bitmaps)
    wxPanel* m_statsPanel = nullptr;

    wxStaticBitmap* m_lblAllLightA = nullptr;
    wxStaticBitmap* m_lblAllLightE = nullptr;
    wxStaticBitmap* m_lblAllHeavyA = nullptr;
    wxStaticBitmap* m_lblAllHeavyE = nullptr;
    wxStaticBitmap* m_lblAllAirA = nullptr;
    wxStaticBitmap* m_lblAllAirE = nullptr;
    wxStaticBitmap* m_lblAllCmdA = nullptr;
    wxStaticBitmap* m_lblAllCmdE = nullptr;

    wxStaticBitmap* m_lblLvlLightA = nullptr;
    wxStaticBitmap* m_lblLvlLightE = nullptr;
    wxStaticBitmap* m_lblLvlHeavyA = nullptr;
    wxStaticBitmap* m_lblLvlHeavyE = nullptr;
    wxStaticBitmap* m_lblLvlAirA = nullptr;
    wxStaticBitmap* m_lblLvlAirE = nullptr;
    wxStaticBitmap* m_lblLvlCmdA = nullptr;
    wxStaticBitmap* m_lblLvlCmdE = nullptr;

    wxStaticBitmap* m_lblPlayerName = nullptr;
    wxStaticBitmap* m_lblPlayerRank = nullptr;
    wxStaticBitmap* m_lblPlayerExp = nullptr;
    wxStaticBitmap* m_lblPlayerMaxUnits = nullptr;
    wxStaticBitmap* m_lblPlayerMaxCmds = nullptr;

    // widgets
    wxStaticText* m_lblMoneyCaption = nullptr;
    wxStaticText* m_lblMoneyValue = nullptr;
    wxStaticText* m_lblResearchCaption = nullptr;
    wxStaticText* m_lblResearchValue = nullptr;
    wxStaticText* m_lblTurnCaption = nullptr;
    wxStaticText* m_lblTurnValue = nullptr;

    wxPanel* m_mapPanel = nullptr;
    wxPanel* m_territoryButtonsPanel = nullptr;
    // Dedicated paint surface for the strategic background (so it isn't fully covered by child controls).
    wxPanel* m_mapCanvas = nullptr;
    // --- Resources page (strategic resource allocation)
    wxPanel* m_resourcesPanel = nullptr;
    wxPanel* m_resourcesCanvas = nullptr;
    wxStaticText* m_resourcesSelectedLabel = nullptr;
    wxSlider* m_resourcesSlider = nullptr;
    wxStaticText* m_resourcesRatioLabel = nullptr;

    wxBoxSizer* m_mapSizer = nullptr;
    wxListCtrl* m_cmdRoster = nullptr;
    wxListCtrl* m_roster = nullptr;
    wxSimplebook* m_leftBook = nullptr;
    wxSimplebook* m_hierarchyBook = nullptr;
    wxButton* m_btnHierarchyPageToggle = nullptr;
    std::vector<HierarchySlot> m_hierarchySlots;
    std::unordered_map<std::string, size_t> m_hierarchySlotIndex;

    wxButton* m_btnResearch = nullptr;
    wxButton* m_btnBuy = nullptr;
    wxButton* m_btnBuyCmd = nullptr;
    wxButton* m_btnSell = nullptr;
    wxButton* m_btnEndTurn = nullptr;
    wxButton* m_btnLaunch = nullptr;
    wxButton* m_btnStrategicMap = nullptr;
    wxButton* m_btnHierarchy = nullptr;
    wxButton* m_btnResources = nullptr;
    wxButton* m_btnStats = nullptr;

    wxBitmap m_bgBitmapScaled;
    int m_bgScaledW = -1;
    int m_bgScaledH = -1;


    enum : int {
        ID_TERRITORY_BASE = 20000,
        ID_BTN_RESEARCH,
        ID_BTN_BUY,
        ID_BTN_BUY_CMD,
        ID_BTN_SELL,
        ID_BTN_ENDTURN,
        ID_BTN_LAUNCH,
        ID_BTN_STRATEGIC_MAP,
        ID_BTN_HIERARCHY,
        ID_BTN_RESOURCES,
        ID_BTN_STATS,
        ID_MENU_SAVE_GAME,
        ID_MENU_LOAD_GAME,
        ID_MENU_OPTIONS_AUDIO,
        ID_MENU_GAME_MODE_TOGGLE

    };

    wxDECLARE_EVENT_TABLE();
};
// void StrategicLevelFrame::TryLoadBackground()

static std::filesystem::path GetStrategicStatePath(const LevelData& level);
