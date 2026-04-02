#pragma once

#include <unordered_map>
#include <unordered_set>
#include <set>
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
#include <wx/gauge.h>
#include <wx/dnd.h>
#include <wx/scrolwin.h>

#include "level.h"

class MainFrame;
class SpellData;

class StrategicLevelFrame : public wxFrame
{
public:
    StrategicLevelFrame(MainFrame* parent, const LevelData& level, bool skipAutosave = false);

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
    void OnShowInfo(wxCommandEvent& ev);  // NEW: Info/encyclopedia handler
    void OnBuyUnits(wxCommandEvent& ev);
    void OnBuyCommander(wxCommandEvent& ev);
    void OnSellUnits(wxCommandEvent& ev);
    void BuildBuyPage();
    void RefreshBuyShopList();
    void RefreshBuyRosters();
    void RefreshBuyInfo(long data);
    void ShowBuyPanel(bool show);
    void PostFixBuyLayout();
    void EnterBuyMode();
    void LeaveBuyMode();
    void OnBuyShop(wxCommandEvent&);
    void OnBuyAction(wxCommandEvent&);
    void OnEndTurn(wxCommandEvent& ev);
    void OnLaunch(wxCommandEvent& ev);
    void OnShowStrategicMap(wxCommandEvent& ev);
    void OnShowHierarchy(wxCommandEvent& ev);
    void OnShowStats(wxCommandEvent& ev);
    void OnShowResources(wxCommandEvent& ev);

    // ============================================================
    // Units Management Page (Recruit / Disband / Upgrade / Info)
    // ============================================================
    void OnUnitsShop(wxCommandEvent& ev);
    void BuildUnitsPage();
    void ShowUnitsPanel(bool show);
    void PostFixUnitsLayout();
    void EnterUnitsMode();
    void LeaveUnitsMode();
    void RefreshUnitsRoster();
    void RefreshUnitsShopList();
    void RefreshUnitsInfo(int unitIndex);
    void RefreshUnitsActionButton();
    void OnUnitsAction(wxCommandEvent& ev);
    void OnUnitsDisband(wxCommandEvent& ev);
    void OnUnitsTabChange(int tab);
    void ApplyUnitsCooldownTick();  // Called at end of turn
    int GetRecruitCost(int unitIndex, int quality) const;
    int GetRecruitTime(int quality) const;
    int GetUpgradeCost(int unitId, int upgradeId) const;  // re-arm cost (uses cost_upgrade from units.json)
    int GetUpgradeTime(int upgradeId) const;              // re-arm time
    int GetTechUpgradeCost(int upgradeId) const;          // tech upgrade cost (from UPGRADES.DEF)
    int GetTechUpgradeTime(int upgradeId) const;          // tech upgrade time (from UPGRADES.DEF)
    bool EnsureUpgradeDefsLoaded();                       // load UPGRADES.DEF
    wxString GetUnitCategoryName(int unitId) const;
    bool CanUpgradeUnitTo(int fromUnitId, int toUnitId) const;
    std::vector<int> GetAvailableUpgradesForUnit(int unitId) const;
    std::vector<int> GetAvailableUnitTypesForUpgrade(int unitId) const;


    // menu (Strategic Level saves)
    void BuildMenu();
    void OnSaveGame(wxCommandEvent& ev);
    void OnLoadGame(wxCommandEvent& ev);

    void OnOptionsAudio(wxCommandEvent& ev);
    void OnOptionsScreen(wxCommandEvent& ev);

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
    void LoadPlayerStateFromPreviousLevel();
    wxString GetUnitDisplayName(int unit_id) const;
    bool EnsureUnitCostsLoaded();
    int GetUnitBuyCost(int unit_id) const;

    // mission selection / progression
    std::string ResolveMissionTokenForTerritory(int territory_id) const;
    std::wstring ResolveMapDefPathForMissionToken(const std::string& mission_token) const;
    const LevelMission* FindMissionByNameUpper(const std::string& name_upper) const;

    // ============================================================
    // Mission Result Handling & Campaign Progression
    // ============================================================
    
    // Mission statistics tracking
    struct MissionStats
    {
        int missions_completed = 0;
        int missions_failed = 0;
        int territories_conquered = 0;
        int territories_lost = 0;
        int turns_total = 0;
    };
    
    // Pending mission result (set before launch, consumed after return)
    struct PendingMissionResult
    {
        bool valid = false;
        int territory_id = -1;
        std::string mission_token;
        // Indices into m_playerUnits of units sent to this mission
        std::vector<size_t> sent_unit_indices;
    };
    
    // Counter-attack state for owned territories
    struct CounterAttackState
    {
        int territory_id = 0;
        int conquest_turn = 0;
        int trigger_turn = 0;
        std::string counter_mission;
        bool triggered = false;
        bool completed = false;
    };
    
    // Handle mission completion (called from main.cpp after returning from tactical map)
    void HandleMissionResult(int territory_id, bool success, const std::string& mission_token);

    // Collect battle results from tactical map and apply to strategic state
    // Returns the per-mission enemy losses for XP calculation
    LossBlock CollectAndApplyBattleResults(bool success);

    // Save mission/loss stats to strategic_stats.json
    void SaveMissionStats() const;

    // Conquest a territory (add to owned, apply visibility, play video)
    void ConquestTerritory(int territory_id);
    
    // Check and trigger timeouts (called in OnEndTurn)
    void CheckTimeouts();
    
    // Check and trigger counter-attacks (called in OnEndTurn)
    void CheckCounterAttacks();
    
    // Show briefing for territory before mission launch
    void ShowBriefing(int territory_id);
    
    // Play video file (DPK)
    void PlayVideo(const std::string& video_file);
    
    // Draw territory marker on map (LASTTERT, timeout countdown, owned/enemy)
    void DrawTerritoryMarker(wxDC& dc, int territory_id, int x, int y, double scale);

    // Load strategic icons (VM_0..VM_9, LASTTERT) from SpellGraphics gres
    void EnsureStrategicIconsLoaded();
    
    // Check if all territories are conquered
    bool AreAllTerritoriesConquered() const;
    
    // Check if territory is the final one
    bool IsFinalTerritory(int territory_id) const;
    
    // Advance to next level (load next DEF)
    void AdvanceToNextLevel();
    
    // Get remaining turns until timeout for territory (-1 if no timeout)
    int GetTerritoryTimeoutRemaining(int territory_id) const;
    
    // Find briefing text file for mission
    std::wstring FindBriefingPath(const std::string& mission_token) const;
    
    // Statistics
    MissionStats m_stats;
    
    // Pending mission (for result tracking)
    PendingMissionResult m_pendingMission;
    
    // Counter-attack tracking
    std::vector<CounterAttackState> m_counterAttacks;
    
    // Territory timeouts (territory_id -> deadline turn)
    std::unordered_map<int, int> m_territoryTimeoutTurn;

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
    std::unordered_map<int, int> m_unitCosts;        // unit_id -> cost_buy
    std::unordered_map<int, int> m_unitUpgradeCosts; // unit_id -> cost_upgrade (re-arm cost)
    bool m_unitCostsLoaded = false;

    // Tech upgrades from UPGRADES.DEF (Engine/Weapon/Armour style)
    struct UpgradeDefRec
    {
        int id = -1;
        int price = 0;
        int time = 1;
        std::set<int> suitableTypes; // unit type_ids this upgrade applies to
    };
    std::unordered_map<int, UpgradeDefRec> m_upgradeDefs;
    bool m_upgradeDefsLoaded = false;

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

    // ============================================================
    // Research (Strategic level)
    // ============================================================
    struct ResearchItem
    {
        int id = -1;            // numeric id from R000..R999, or -1 for non-numeric entries
        wxString code;          // e.g. "R004" / "RACES"
        wxString title;         // shown in list (from RESEARCH.CZ / .ENG)
        wxString brief;         // short flavour text (BRF) – shown in top box when active
        wxString info;          // long detail text (INF) – shown in bottom box when browsing
        int cost = 20;          // research duration (Time() from RESEARCH.DEF)
        // parsed from RESEARCH.DEF
        wxString group;         // "Races" / "Technologies" / "Upgrades" / "Global"
        int level = 0;          // minimum campaign level to unlock
        std::vector<int> prerequisites; // OR-connected prerequisite ids
        wxString flags;         // "UnitType" / "NewUnit" / "Info" / "UpgradeItem" / "Special"
    };

    void EnsureResearchLoaded();
    void EnterResearchMode();
    void LeaveResearchMode();
    void RefreshResearchUI();
    void ApplyResearchTickEndTurn();
    void SelectResearchIndex(int idx);

    void OnResearchList(wxCommandEvent& ev);
    void OnResearchStartStop(wxCommandEvent& ev);
    void OnResearchAlloc(wxCommandEvent& ev);

    // ============================================================
    // Info / Encyclopedia (Strategic level) - NEW
    // ============================================================
    void EnterInfoMode();
    void LeaveInfoMode();
    void RefreshInfoUI();
    void SelectInfoIndex(int idx);


    std::unordered_map<int, TerritoryResourceState> m_territoryResources;

    // Global allocation for all territories: research points per territory (0..5). Money per territory = 20 - 4*R.
    int m_resourcesGlobalResearch = 0;


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

    // Strategic map icons (cached wxBitmaps from SpellGraphics ICO resources)
    bool m_strategicIconsLoaded = false;
    wxBitmap m_icoVM[10];        // VM_0 .. VM_9 (timeout countdown digits)
    wxBitmap m_icoLastTert;      // LASTTERT (crossed swords for final territory)

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
    wxListCtrl* m_resourcesTable = nullptr;

    wxBoxSizer* m_mapSizer = nullptr;
    wxListCtrl* m_cmdRoster = nullptr;
    wxListCtrl* m_roster = nullptr;
    wxSimplebook* m_leftBook = nullptr;
    wxSimplebook* m_hierarchyBook = nullptr;

    // Research UI books/panels
    wxSimplebook* m_midBook = nullptr;
    wxPanel* m_midRosterPanel = nullptr;
    wxPanel* m_midResearchPanel = nullptr;
    wxPanel* m_researchPanel = nullptr; // left-side page (details + progress)

    wxListCtrl* m_researchList = nullptr;
    wxTextCtrl* m_researchActiveText = nullptr; // top box: BRF of currently active research
    wxTextCtrl* m_researchText = nullptr;
    wxGauge* m_researchGauge = nullptr;
    wxStaticText* m_researchGaugeLabel = nullptr;
    wxSlider* m_researchAllocSlider = nullptr;
    wxStaticText* m_researchAllocLabel = nullptr;
    wxButton* m_btnResearchStart = nullptr;

    // Info / Encyclopedia UI panels - NEW
    wxPanel* m_infoPanel = nullptr;      // left-side page (details)
    wxPanel* m_midInfoPanel = nullptr;   // middle page (list)
    wxListCtrl* m_infoList = nullptr;    // list of discovered items
    wxTextCtrl* m_infoText = nullptr;    // detail text box

    // Info state - NEW
    bool m_infoMode = false;
    bool m_infoRefreshing = false;       // re-entrancy guard
    int m_infoBrowseIndex = -1;          // index in m_researchDb of selected item

    // Research state
    bool m_researchMode = false;
    bool m_researchRefreshing = false;  // re-entrancy guard for RefreshResearchUI
    std::vector<ResearchItem> m_researchDb;
    int m_researchActiveId = -1;      // id of active research (matches ResearchItem.id for numeric, or -1 otherwise)
    int m_researchActiveIndex = -1;   // index in m_researchDb of the item currently being researched
    int m_researchBrowseIndex = -1;   // index in m_researchDb of the item selected in list (bottom box)
    int m_researchAllocPerTurn = 0;   // how many points to spend per turn from m_research pool
    std::unordered_map<int, int> m_researchProgressById; // id -> points invested
    std::unordered_set<int> m_researchCompleted;         // completed ids
    wxButton* m_btnHierarchyPageToggle = nullptr;
    std::vector<HierarchySlot> m_hierarchySlots;
    std::unordered_map<std::string, size_t> m_hierarchySlotIndex;

    // ── Mission unit selection ──
    // Selected unit UIDs (roster row UIDs) for the next mission launch
    std::unordered_set<uint32_t> m_selectedUnitsForMission;
    // Selected commander UIDs for mission (their units are automatically included)
    std::unordered_set<uint32_t> m_selectedCommandersForMission;

    // Get all unit UIDs assigned under a commander in hierarchy
    std::vector<uint32_t> GetUnitsUnderCommander(uint32_t commander_uid) const;
    // Handler for commander selection in roster (selects all units under them)
    void OnCommanderSelectForMission(wxListEvent& ev);
    // Handler for unit selection in roster
    void OnUnitSelectForMission(wxListEvent& ev);
    // Update visual selection state in roster (units)
    void UpdateRosterSelectionVisuals();
    // Update visual selection state in commander roster
    void UpdateCommanderRosterSelectionVisuals();
    // Get selected units as PlayerUnitAdd vector for mission launch
    std::vector<LevelData::PlayerUnitAdd> GetSelectedUnitsForLaunch() const;
    // Check if a roster UID corresponds to a unit currently on cooldown
    bool IsRosterUidOnCooldown(uint32_t uid) const;

    wxButton* m_btnResearch = nullptr;
    wxButton* m_btnInfo = nullptr;       // NEW: Info/encyclopedia button
    wxButton* m_btnBuyShop = nullptr;   // single "Buy / Sell" toggle
    wxButton* m_btnEndTurn = nullptr;
    wxButton* m_btnLaunch = nullptr;
    wxButton* m_btnStrategicMap = nullptr;
    wxButton* m_btnHierarchy = nullptr;
    wxButton* m_btnResources = nullptr;
    wxButton* m_btnStats = nullptr;

    // ── Buy / Sell page (root-level panel, replaces entire layout) ──
    // Buy/Sell page status widgets (separate from normal sidebar)
    wxStaticText* m_buyLblMoneyCaption = nullptr;
    wxStaticText* m_buyLblMoneyValue = nullptr;
    wxStaticText* m_buyLblResearchCaption = nullptr;
    wxStaticText* m_buyLblResearchValue = nullptr;
    wxStaticText* m_buyLblTurnCaption = nullptr;
    wxStaticText* m_buyLblTurnValue = nullptr;

    wxPanel* m_normalLayoutPanel = nullptr;  // container for left+mid+right
    wxPanel* m_buyMainPanel = nullptr;  // root buy panel
    wxListCtrl* m_buyShopList = nullptr;  // shop list (right top)
    wxListCtrl* m_buyUnitRoster = nullptr;  // left top (cloned roster)
    wxListCtrl* m_buyCmdRoster = nullptr;  // left bottom (cloned cmd roster)
    wxTextCtrl* m_buyInfoText = nullptr;  // selected item info (right bottom)
    wxStaticText* m_buyTimeLabel = nullptr;  // "Time: N"
    wxStaticText* m_buyCostLabel = nullptr;  // "Cost: N"
    wxButton* m_btnBuyAction = nullptr;  // Buy/Sell button
    bool          m_buyModeActive = false;
    bool          m_buyTabSell = false;    // true = sell mode

    std::set<int> m_levelResearchFlags;  // from LEVEL_XX.DEF SetResearchFlag(N)
    std::unordered_map<int, std::string> m_unitCategories;

    wxBitmap m_bgBitmapScaled;
    int m_bgScaledW = -1;
    int m_bgScaledH = -1;

    // ── Units Management page (Recruit / Disband / Upgrade / Info) ──
    // Units page status widgets
    wxStaticText* m_unitsLblMoneyCaption = nullptr;
    wxStaticText* m_unitsLblMoneyValue = nullptr;
    wxStaticText* m_unitsLblResearchCaption = nullptr;
    wxStaticText* m_unitsLblResearchValue = nullptr;
    wxStaticText* m_unitsLblTurnCaption = nullptr;
    wxStaticText* m_unitsLblTurnValue = nullptr;

    wxPanel* m_unitsMainPanel = nullptr;  // root units panel
    wxListCtrl* m_unitsRoster = nullptr;  // player units list (left)
    wxListCtrl* m_unitsTempRoster = nullptr;  // temporary units list (left bottom)
    wxListCtrl* m_unitsShopList = nullptr;  // shop/options list (middle top)
    wxTextCtrl* m_unitsInfoText = nullptr;  // unit info (middle bottom)
    wxPanel* m_unitsIconCanvas = nullptr;  // unit icon display
    wxPanel* m_unitsArtCanvas = nullptr;  // unit art display (for Info mode)
    wxStaticText* m_unitsTimeLabel = nullptr;  // "Time: N"
    wxStaticText* m_unitsCostLabel = nullptr;  // "Cost: N"
    wxButton* m_btnUnitsAction = nullptr;  // action button
    wxButton* m_btnUnitsDisband = nullptr;  // disband button (always visible)
    wxButton* m_btnUnitsShop = nullptr;  // Units button in sidebar
    wxButton* m_btnUnitsTabRecruit = nullptr;
    wxButton* m_btnUnitsTabDisband = nullptr;
    wxButton* m_btnUnitsTabUpgrade = nullptr;
    wxButton* m_btnUnitsTabInfo = nullptr;
    wxChoice* m_unitsQualityChoice = nullptr;  // recruit quality selector
    // Upgrade panel widgets (Upgrade tab)
    wxStaticText* m_unitsUpgradeTitle = nullptr;
    wxStaticText* m_unitsUpgradeValue = nullptr;
    wxStaticText* m_unitsRearmTitle = nullptr;
    wxListBox*    m_unitsRearmList = nullptr;  // unit types in same category (re-arm)

    bool          m_unitsModeActive = false;

    enum UnitsTab : int {
        UNITS_TAB_RECRUIT = 0,
        UNITS_TAB_DISBAND = 1,
        UNITS_TAB_UPGRADE = 2,
        UNITS_TAB_INFO = 3
    };
    UnitsTab      m_unitsCurrentTab = UNITS_TAB_RECRUIT;
    int           m_unitsSelectedUnit = -1;  // index in m_playerUnits
    int           m_unitsSelectedUpgrade = -1;  // selected upgrade item
    int           m_unitsSelectedRearmUnitId = -1;  // unit_id selected in re-arm list (Upgrade tab)

    // Per-unit instance state for cooldowns and upgrades
    struct UnitInstanceState
    {
        uint32_t uid = 0;              // matches roster uid
        int cooldown_turns = 0;        // turns until unit is ready (after recruit/upgrade)
        std::vector<int> upgrades;     // purchased upgrade IDs for this unit
        int experience = 0;            // unit experience (gained in combat)
        int level = 0;                 // unit level (derived from experience)
        std::string custom_name;       // player-assigned name
    };
    std::vector<UnitInstanceState> m_unitStates;

    // Recruit quality levels (Recruit mode)
    // 0 = Rookie (fast/cheap, reduces experience), 1 = Veteran, 2 = Elite (slow/expensive, preserves experience)
    static constexpr int RECRUIT_QUALITY_COUNT = 3;
    static constexpr const char* RECRUIT_QUALITY_NAMES[RECRUIT_QUALITY_COUNT] = {
        "Rookie recruitment",
        "Veteran recruitment",
        "Elite recruitment"
    };
    // Cost multiplier relative to base unit price and missing strength.
    static constexpr int RECRUIT_QUALITY_COST_MULT[RECRUIT_QUALITY_COUNT] = { 60, 110, 180 };
    // Turns the unit is unavailable after recruitment.
    static constexpr int RECRUIT_QUALITY_TIME[RECRUIT_QUALITY_COUNT] = { 1, 2, 3 };


    enum : int {
        ID_TERRITORY_BASE = 20000,
        ID_BTN_RESEARCH,
        ID_BTN_INFO,         // NEW: Info button ID
        ID_BTN_BUY,
        ID_BTN_BUY_CMD,
        ID_BTN_SELL,
        ID_BTN_BUY_SHOP,
        ID_BTN_BUY_ACTION,
        ID_BTN_ENDTURN,
        ID_BTN_LAUNCH,
        ID_BTN_STRATEGIC_MAP,
        ID_BTN_HIERARCHY,
        ID_BTN_RESOURCES,
        ID_BTN_STATS,
        ID_MENU_SAVE_GAME,
        ID_MENU_LOAD_GAME,
        ID_MENU_OPTIONS_AUDIO,
        ID_MENU_OPTIONS_SCREEN,
        ID_MENU_GAME_MODE_TOGGLE,
        // Units management page IDs
        ID_BTN_UNITS,
        ID_BTN_UNITS_ACTION,
        ID_UNITS_TAB_RECRUIT,
        ID_UNITS_TAB_DISBAND,
        ID_UNITS_TAB_UPGRADE,
        ID_UNITS_TAB_INFO
    };

    wxDECLARE_EVENT_TABLE();
};
// void StrategicLevelFrame::TryLoadBackground()

static std::filesystem::path GetStrategicStatePath(const LevelData& level);