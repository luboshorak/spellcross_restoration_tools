#pragma once

#include <unordered_map>
#include <string>
#include <vector>

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

    void BuildUI();
    void RefreshUI();

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
    void OnSellUnits(wxCommandEvent& ev);
    void OnEndTurn(wxCommandEvent& ev);
    void OnLaunch(wxCommandEvent& ev);
    void OnShowStrategicMap(wxCommandEvent& ev);
    void OnShowHierarchy(wxCommandEvent& ev);


// menu (Strategic Level saves)
void BuildMenu();
void OnSaveGame(wxCommandEvent& ev);
void OnLoadGame(wxCommandEvent& ev);

struct PlayerProgress
{
    std::string name = "John Alexander";
    int rank = 0;
    int experience = 0;
    int actions = 0;
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
    std::unordered_map<int, int> m_unitCosts;
    bool m_unitCostsLoaded = false;

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

    // widgets
    wxStaticBitmap* m_lblMoney = nullptr;
    wxStaticBitmap* m_lblResearch = nullptr;
    wxStaticBitmap* m_lblTurn = nullptr;

    wxPanel* m_mapPanel = nullptr;
    wxPanel* m_territoryButtonsPanel = nullptr;
    // Dedicated paint surface for the strategic background (so it isn't fully covered by child controls).
    wxPanel* m_mapCanvas = nullptr;
    wxBoxSizer* m_mapSizer = nullptr;
    wxListCtrl* m_roster = nullptr;
    wxListCtrl* m_hierarchyList = nullptr;
    wxSimplebook* m_rightBook = nullptr;

    wxButton* m_btnResearch = nullptr;
    wxButton* m_btnBuy = nullptr;
    wxButton* m_btnSell = nullptr;
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
        ID_BTN_SELL,
        ID_BTN_ENDTURN,
        ID_BTN_LAUNCH,
        ID_BTN_STRATEGIC_MAP,
        ID_BTN_HIERARCHY,
        ID_MENU_SAVE_GAME,
        ID_MENU_LOAD_GAME
    };

    wxDECLARE_EVENT_TABLE();
};
// void StrategicLevelFrame::TryLoadBackground()
