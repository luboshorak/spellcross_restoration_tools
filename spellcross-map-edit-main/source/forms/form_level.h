#pragma once

#include <unordered_map>
#include <unordered_set>
#include <string>

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/statbmp.h>
#include <wx/simplebook.h>

#include "level.h"

class MainFrame;
class SpellData;

class StrategicLevelFrame : public wxFrame
{
public:
    StrategicLevelFrame(MainFrame* parent, const LevelData& level);

private:
    void BuildUI();
    void RefreshUI();

    // background (LEVEL_XX.LZ + LEVEL_XX.PAL) - best effort
    void TryLoadBackground();
    void OnMapPaint(wxPaintEvent& ev);
    void OnActivate(wxActivateEvent& ev);

    // actions
    void OnTerritory(wxCommandEvent& ev);
    void OnResearch(wxCommandEvent& ev);
    void OnBuyUnits(wxCommandEvent& ev);
    void OnEndTurn(wxCommandEvent& ev);
    void OnLaunch(wxCommandEvent& ev);
    void OnShowStrategicMap(wxCommandEvent& ev);
    void OnShowHierarchy(wxCommandEvent& ev);

    void LoadStrategicState();
    void SaveStrategicState() const;
    wxString GetUnitDisplayName(int unit_id) const;
    void LoadResearchData();
    void UpdateHierarchyList();
    bool IsResearchAvailableInLevel(int research_id) const;
    bool AreResearchPrerequisitesMet(const std::vector<int>& prereq) const;
    std::unordered_set<int> CollectUnlockedUnits() const;

    // mission selection / progression
    std::string ResolveMissionTokenForTerritory(int territory_id) const;
    std::wstring ResolveMapDefPathForMissionToken(const std::string& mission_token) const;
    const LevelMission* FindMissionByNameUpper(const std::string& name_upper) const;

private:
    MainFrame* m_main = nullptr;
    SpellData* m_spellData = nullptr;
    LevelData m_level;

    // simple strategic state (in-memory for now)
    int m_turn = 1;
    int m_money = 0;
    int m_research = 0;
    int m_selectedTerritory = -1;

    // per-territory state: current mission token + number of launches
    std::unordered_map<int, std::string> m_territoryCurrentMission;
    std::unordered_map<int, int> m_territoryLaunchCount;

    std::vector<LevelData::PlayerUnitAdd> m_playerUnits;

    struct ResearchItem {
        int id = -1;
        std::string name;
        int cost = 100;
        std::vector<int> prerequisites;
        std::vector<int> unlock_units;
    };
    std::vector<ResearchItem> m_researchItems;
    std::unordered_set<int> m_researchUnlocked;

    // background bitmap
    wxBitmap m_bgBitmap;
    bool m_hasBg = false;

    // widgets
    wxStaticBitmap* m_lblMoney = nullptr;
    wxStaticBitmap* m_lblResearch = nullptr;
    wxStaticBitmap* m_lblTurn = nullptr;

    wxPanel* m_mapPanel = nullptr;
    wxBoxSizer* m_mapSizer = nullptr;
    wxListCtrl* m_roster = nullptr;
    wxListCtrl* m_hierarchyList = nullptr;
    wxSimplebook* m_rightBook = nullptr;

    wxButton* m_btnResearch = nullptr;
    wxButton* m_btnBuy = nullptr;
    wxButton* m_btnEndTurn = nullptr;
    wxButton* m_btnLaunch = nullptr;
    wxButton* m_btnStrategicMap = nullptr;
    wxButton* m_btnHierarchy = nullptr;

    wxBitmap m_bgBitmapScaled;
    int m_bgScaledW = -1;
    int m_bgScaledH = -1;


    enum : int {
        ID_TERRITORY_BASE = 20000,
        ID_BTN_RESEARCH,
        ID_BTN_BUY,
        ID_BTN_ENDTURN,
        ID_BTN_LAUNCH,
        ID_BTN_STRATEGIC_MAP,
        ID_BTN_HIERARCHY
    };

    wxDECLARE_EVENT_TABLE();
};
