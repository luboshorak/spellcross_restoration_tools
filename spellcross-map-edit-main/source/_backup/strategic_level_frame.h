#pragma once

#include <unordered_map>
#include <string>

#include <wx/wx.h>
#include <wx/listctrl.h>

#include "level.h"

class MainFrame;

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

    // actions
    void OnTerritory(wxCommandEvent& ev);
    void OnResearch(wxCommandEvent& ev);
    void OnBuyUnits(wxCommandEvent& ev);
    void OnEndTurn(wxCommandEvent& ev);
    void OnLaunch(wxCommandEvent& ev);

    // mission selection / progression
    std::string ResolveMissionTokenForTerritory(int territory_id) const;
    std::wstring ResolveMapDefPathForMissionToken(const std::string& mission_token) const;
    const LevelMission* FindMissionByNameUpper(const std::string& name_upper) const;

private:
    MainFrame* m_main = nullptr;
    LevelData m_level;

    // simple strategic state (in-memory for now)
    int m_turn = 1;
    int m_money = 0;
    int m_research = 0;
    int m_selectedTerritory = -1;

    // per-territory state: current mission token + number of launches
    std::unordered_map<int, std::string> m_territoryCurrentMission;
    std::unordered_map<int, int> m_territoryLaunchCount;

    // background bitmap
    wxBitmap m_bgBitmap;
    bool m_hasBg = false;

    // widgets
    wxStaticText* m_lblMoney = nullptr;
    wxStaticText* m_lblResearch = nullptr;
    wxStaticText* m_lblTurn = nullptr;

    wxPanel* m_mapPanel = nullptr;
    wxBoxSizer* m_mapSizer = nullptr;
    wxListCtrl* m_roster = nullptr;

    wxButton* m_btnResearch = nullptr;
    wxButton* m_btnBuy = nullptr;
    wxButton* m_btnEndTurn = nullptr;
    wxButton* m_btnLaunch = nullptr;

    enum : int {
        ID_TERRITORY_BASE = 20000,
        ID_BTN_RESEARCH,
        ID_BTN_BUY,
        ID_BTN_ENDTURN,
        ID_BTN_LAUNCH
    };

    wxDECLARE_EVENT_TABLE();
};
