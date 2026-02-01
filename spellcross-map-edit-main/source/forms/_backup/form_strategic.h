#pragma once

#include <wx/wx.h>
#include <string>
#include <vector>

#include "level.h"

// New form: shows Strategic statistics similar to the original game screen.
// Reads:
//  - HODNOSTI.DEF for rank thresholds/limits
//  - strategic_state.json optionally for player fields (if present later)
//  - strategic_stats.json for mission loss tables (if present later)

class StrategicInfoFrame : public wxFrame
{
public:
    StrategicInfoFrame(wxWindow* parent, const LevelData& level);

    struct CommanderRankRec
    {
        int rank = 0;
        int max_units = 0;
        int actions_required = 0;
        int exp_required = 0;
        int max_commanders = 0;
    };

    struct PlayerProgress
    {
        std::string name = "John Alexander";
        int rank = 0;
        int experience = 0;
        int actions = 0;
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

    void BuildUI();
    void RefreshUI();

    // data loading
    void LoadRanksTable();
    void LoadPlayerFromStrategicStateIfPresent();
    void LoadMissionStatsIfPresent();

    // rank helpers
    void RecomputePlayerRank();
    const CommanderRankRec* FindRankRec(int rank) const;
    int FindNextRankExp(int current_rank) const;
    wxString GetRankNameCz(int rank) const;

    // paths
    wxString FindHodnostiDefPath() const;
    wxString FindStrategicStatePath() const;
    wxString FindStrategicStatsPath() const;

    // parsing helpers
    static bool ParseJsonIntField(const std::string& obj, const char* key, int& outValue);
    static bool ParseJsonStringField(const std::string& obj, const char* key, std::string& outValue);

private:
    LevelData m_level;

    std::vector<CommanderRankRec> m_ranks;
    PlayerProgress m_player;
    LossStats m_stats;

    // UI labels we update
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
};
