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
    int GetUnitExperienceLevel(int unitId, int experience) const;
    void NormalizeStrategicUnitInstances();
    void RebuildRosterRowUidsFromUnitStates();
    bool IsTemporaryUnitIndex(size_t index) const;
    int FindUnitIndexByUid(uint32_t uid) const;
    void RemoveTemporaryUnitsForCampaignTransition();
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
    void SaveStrategicGameToSlot(int slot, bool notify = true);
    void LoadStrategicGameFromSlot(int slot, bool notify = true);
    bool PromptStrategicSaveSlot(int maxSlots, bool notify = true);

    void OnOptionsAudio(wxCommandEvent& ev);
    void OnOptionsScreen(wxCommandEvent& ev);

    // Experimental second strategic UI branch. This stays completely separate
    // from the existing wx strategic layout while the restored 640x480 UI is
    // developed and tested screen-by-screen.
    void OnStrategicUiCurrent(wxCommandEvent& ev);
    void OnStrategicUiOriginal(wxCommandEvent& ev);
    void SetOriginalStrategicUi(bool enabled);
    void RefreshOriginalStrategicView();
    void OnOriginalStrategicPaint(wxPaintEvent& ev);
    void OnOriginalStrategicLeftDown(wxMouseEvent& ev);
    void OnOriginalStrategicRightDown(wxMouseEvent& ev);
    void OnOriginalStrategicMouseWheel(wxMouseEvent& ev);
    void OnOriginalStrategicMouseMove(wxMouseEvent& ev);
    void OnOriginalStrategicMouseLeave(wxMouseEvent& ev);
    void OnOriginalStrategicAnimTimer(wxTimerEvent& ev);

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

    // Minimal serializable form of a strategic hierarchy slot.  UIDs refer to
    // the already-persisted unit/commander instances.
    struct HierarchyPersistRec
    {
        std::string slot_id;
        uint32_t commander_uid = 0;
        uint32_t unit_uid = 0;
        uint32_t assigned_unit_uid = 0;
    };


    void LoadStrategicState();
    // Load an exact strategic JSON save selected outside the Strategic Level window.
    // This applies the save to the already-correct LevelData and does not change the save format.
    bool LoadStrategicStateFromPath(const std::filesystem::path& path);

    // Import the original DOS strategic save (BIG_MAP.SAV). The file is
    // Spellcross-LZW compressed; level number and strategic state are read
    // directly from the original binary layout.
    static bool PeekOriginalBigMapLevel(const std::filesystem::path& path, int& levelNumber,
        std::string* error = nullptr);
    bool LoadOriginalBigMapSaveFromPath(const std::filesystem::path& path,
        std::string* warning = nullptr);

    void SaveStrategicState() const;
    void LoadPlayerStateFromPreviousLevel();

    // Start a fresh game mode for this level (no save loading, no debug dialog).
    // bonus_units: extra units the player earned from the previous mission (e.g. rescued commando).
    void StartFreshGameMode(const std::vector<LevelData::PlayerUnitAdd>& bonus_units = {});
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
        // Indices into m_playerUnits of units sent to this mission. Kept for
        // compatibility with older campaign saves; concrete roster UIDs are the
        // authoritative identity for hierarchy/commander loss tracking.
        std::vector<size_t> sent_unit_indices;
        std::vector<uint32_t> sent_unit_uids;
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
    
    // Tactical battle saves must carry the strategic campaign snapshot that
    // existed when the battle was launched.  These helpers make a .scsave
    // self-contained without conflating tactical and strategic save formats.
    bool ExportBattleSaveContext(std::string& level_def_path, std::string& strategic_state_json,
        PendingMissionResult& pending) const;
    bool ImportBattleSaveContext(const std::string& strategic_state_json,
        const PendingMissionResult& pending);
    bool HasStrategicAutosave() const;
    bool RecoverPendingMissionFromLoadedBattle(const std::string& mission_token);

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

    // Process Level Events from DEF (AbsTime/Time events with armies, texts, etc.)
    void ProcessLevelEvents();

    // Track which level events have been triggered (by event id)
    std::set<int> m_triggeredLevelEvents;
    // Events activated by RunEvents (id -> activation turn for Time-based events)
    std::unordered_map<int, int> m_activatedEvents;
    
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


    struct HierarchyBattleMeta
    {
        int formation_id = 0;
        int formation_level = 0;
        int attack_bonus = 0;
        int defence_bonus = 0;
        uint8_t commander_host_mask = 0; // bit0=battalion, bit1=regiment, bit2=brigade
        bool carries_commander = false;
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

    enum class OriginalBuyRowKind : int
    {
        Heading,
        Unit,
        Commander,
        Spacer
    };

    struct OriginalBuyRow
    {
        OriginalBuyRowKind kind = OriginalBuyRowKind::Spacer;
        wxString label;
        int id = -1;
        bool enabled = false;
    };

    std::vector<OriginalBuyRow> BuildOriginalBuyRows();
    void GetOriginalBuyLimits(int& maxUnits, int& maxCommanders);

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
    void ChooseCommanderForHierarchySlot(const std::string& commanderSlotId);
    void ChooseAssignedUnitForCommanderAssignmentSlot(const std::string& assignmentSlotId);
    bool AssignCommanderToHierarchySlot(const std::string& commanderSlotId,
        uint32_t commanderUid,
        int rank,
        const wxString& commanderName);
    bool AssignUnitToHierarchySlot(const std::string& unitSlotId,
        uint32_t unitUid,
        const wxString& unitDisplay);
    void ClearOriginalHierarchyPoolSelection();
    bool ApplyOriginalHierarchyPoolSelectionToSlot(const std::string& slotId);
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
    std::vector<HierarchyPersistRec> CaptureHierarchyAssignments() const;
    void RestoreHierarchyAssignments(const std::vector<HierarchyPersistRec>& records);

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
    std::unordered_map<int, int> m_unitCosts;         // unit_id -> cost_buy
    std::unordered_map<int, int> m_unitUpgradeCosts;  // unit_id -> cost_upgrade (re-arm cost)
    std::unordered_map<int, int> m_unitReplaceCosts;  // unit_id -> cost_replace from JEDNOTKY.DEF (reinforcement base cost)
    bool m_unitCostsLoaded = false;

    // Tech upgrades from UPGRADES.DEF (Engine/Weapon/Armour style)
    struct UpgradeDefRec
    {
        enum Kind : int { Unknown = 0, Engine, Weapon, Armor };
        int id = -1;
        int price = 0;
        int time = 1;
        Kind kind = Unknown;
        wxString title;
        std::set<int> suitableTypes; // unit type_ids this upgrade applies to
    };
    std::unordered_map<int, UpgradeDefRec> m_upgradeDefs;
    bool m_upgradeDefsLoaded = false;

    // Game mode (campaign progression)
    bool m_gameModeEnabled = false;
    std::vector<int> m_ownedTerritories;

    struct TerritoryResourceState
    {
        int total = 0;
        int remaining = 0;
        int incomePerTurn = 0; // DefineStrategicPoints(..., total, perTurn)
        // Legacy Stage-6 fields kept for backward-compatible save loading.
        int researchPercent = 0;
        int allocAccum = 0;
        // Territory id 0 uses this as the persisted global research allocation.
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
        bool researchable = true; // Time(0) entries are Info/base-knowledge only
        // parsed from RESEARCH.DEF
        wxString group;         // "Races" / "Technologies" / "Upgrades" / "Global"
        int level = 0;          // minimum campaign level to unlock
        std::vector<int> prerequisites; // OR-connected prerequisite ids
        wxString flags;         // "UnitType" / "NewUnit" / "Info" / "UpgradeItem" / "Special"
        int data = -1;          // Data(...) payload; for UpgradeItem this is UPGRADES.DEF id
    };

    void EnsureResearchLoaded();
    void EnterResearchMode();
    void LeaveResearchMode();
    void RefreshResearchUI();
    void ApplyResearchTickEndTurn();
    void SelectResearchIndex(int idx);
    bool IsResearchUnlocked(const ResearchItem& item) const;
    bool IsResearchAvailable(const ResearchItem& item) const;
    bool IsInfoItemVisible(const ResearchItem& item) const;
    bool IsCampaignUnitUnlocked(int unitType) const;
    bool StartResearchIndex(int idx);
    void NormalizeResearchSelection();

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

    void SetGlobalResearchAllocation(int value);
    int GetCurrentStrategicPointIncome() const;

    std::unordered_map<int, TerritoryResourceState> m_territoryResources;

    // Global allocation: research points produced this turn. Each research point costs 3 strategic points; remainder becomes money.
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

    // Original VMM_FULL strategic-map frame. The level bitmap is drawn into
    // its 379x259 viewport, preserving the original DOS layout and hit-test.
    wxBitmap m_mapChromeBitmap;
    wxBitmap m_mapChromeBitmapScaled;
    int m_mapChromeScaledW = -1;
    int m_mapChromeScaledH = -1;

    // statistics model (integrated from former form_strategic.*)
    std::vector<CommanderRankRec> m_ranks;
    LossStats m_lossStats;

    UiPalette m_palette;
    wxFont m_fontText;
    wxFont m_fontHeading;

    // stats page widgets (native wxStaticText controls)
    wxPanel* m_statsPanel = nullptr;

    wxStaticText* m_lblAllLightA = nullptr;
    wxStaticText* m_lblAllLightE = nullptr;
    wxStaticText* m_lblAllHeavyA = nullptr;
    wxStaticText* m_lblAllHeavyE = nullptr;
    wxStaticText* m_lblAllAirA = nullptr;
    wxStaticText* m_lblAllAirE = nullptr;
    wxStaticText* m_lblAllCmdA = nullptr;
    wxStaticText* m_lblAllCmdE = nullptr;

    wxStaticText* m_lblLvlLightA = nullptr;
    wxStaticText* m_lblLvlLightE = nullptr;
    wxStaticText* m_lblLvlHeavyA = nullptr;
    wxStaticText* m_lblLvlHeavyE = nullptr;
    wxStaticText* m_lblLvlAirA = nullptr;
    wxStaticText* m_lblLvlAirE = nullptr;
    wxStaticText* m_lblLvlCmdA = nullptr;
    wxStaticText* m_lblLvlCmdE = nullptr;

    wxStaticText* m_lblPlayerName = nullptr;
    wxStaticText* m_lblPlayerRank = nullptr;
    wxStaticText* m_lblPlayerExp = nullptr;
    wxStaticText* m_lblPlayerMaxUnits = nullptr;
    wxStaticText* m_lblPlayerMaxCmds = nullptr;

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
    // FACTORY/STATS artwork spans the complete 575 px strategic surface.
    // These companion pages keep its x=412..574 part visible instead of
    // leaving the map roster over the right-hand half of those screens.
    wxPanel* m_midResourcesPanel = nullptr;
    wxPanel* m_midStatsPanel = nullptr;
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
    HierarchyBattleMeta GetHierarchyBattleMeta(uint32_t unit_uid,
        const std::unordered_set<uint32_t>& participating_units) const;
    bool IsCommanderFormationActive(uint32_t commander_uid) const;
    std::vector<uint32_t> GetActiveFormationCommanderUids() const;
    void ToggleCommanderFormationForMission(uint32_t commander_uid);
    void RemoveCommandersHostedByDeadUnits(const std::unordered_set<uint32_t>& dead_uids);
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

    // Root + alternate restored UI canvas. The restored branch owns one
    // logical 640x480 framebuffer and does not participate in the legacy
    // strategic sizer hierarchy.
    wxPanel* m_rootPanel = nullptr;
    wxPanel* m_originalStrategicPanel = nullptr;
    enum class OriginalStrategicScreen : int
    {
        Map = 0,
        Hierarchy = 1,
        Units = 2,
        Buy = 3,
        Research = 4,
        Info = 5,
        Resources = 6,
        Stats = 7,
        Options = 8
    };
    enum class OriginalHierarchyPoolSelectionKind : int
    {
        None = 0,
        Unit = 1,
        Commander = 2
    };
    OriginalStrategicScreen m_originalStrategicScreen = OriginalStrategicScreen::Map;
    int m_originalHierarchyPage = 1;
    OriginalHierarchyPoolSelectionKind m_originalHierarchyPoolSelectionKind = OriginalHierarchyPoolSelectionKind::None;
    uint32_t m_originalHierarchySelectedUnitUid = 0;
    wxString m_originalHierarchySelectedUnitDisplay;
    uint32_t m_originalHierarchySelectedCommanderUid = 0;
    int m_originalHierarchySelectedCommanderRank = -1;
    wxString m_originalHierarchySelectedCommanderName;
    bool m_originalStrategicUi = false;
    bool m_originalStrategicDirty = true;
    wxBitmap m_originalStrategicBitmap;
    wxString m_originalStrategicError;
    wxString m_originalBriefingText;
    wxRect m_originalStrategicDrawRect;
    int m_originalUnitScroll = 0;
    int m_originalHierarchyUnitScroll = 0;
    int m_originalUnitsRosterScroll = 0;
    int m_originalUnitsOptionScroll = 0;
    int m_originalBuyListScroll = 0;
    int m_originalBuySelectedUnitId = -1;
    int m_originalBuySelectedCommander = -1;
    int m_originalResearchListScroll = 0;
    int m_originalResearchActiveTextScroll = 0;
    int m_originalResearchBrowseTextScroll = 0;
    int m_originalInfoListScroll = 0;
    int m_originalInfoTextScroll = 0;
    bool m_originalQuickHelp = true;

    // Native strategic-map interaction/animation state. These values are only
    // used by the reconstructed 640x480 branch and never affect legacy wx UI.
    wxTimer m_originalStrategicAnimTimer;
    int m_originalStrategicAnimPhase = 0;
    int m_originalStrategicAnimSubTick = 0;
    int m_originalToolbarHoverSlot = -1;
    int m_originalActionHover = -1; // 0=Attack, 1=Cancel
    bool m_originalEndTurnHover = false;
    int m_originalEndTurnReveal = 0; // 0..41 px, native ET_BTN0 left-to-right wipe

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
        bool temporary = false;        // support unit: valid only for the current strategic level
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
        ID_MENU_STRATEGIC_UI_CURRENT,
        ID_MENU_STRATEGIC_UI_ORIGINAL,
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
