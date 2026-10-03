//=============================================================================
// Spellcross restoration
// ----------------------------------------------------------------------------
// Map and Levels handling functions, wxWidgets GUI.
// 
// This code is part of Spellcross – restoration tools project.
// (c) 2025-2026, Lubos Horak
// url: https://github.com/luboshorak/spellcross_restoration_tools
// Distributed under MIT license, https://opensource.org/licenses/MIT.
//=============================================================================

#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct LevelTerritory {
    int id = 0;
    std::string intro_mission;  // "none" nebo "m02_02a"
    std::string mission;        // "m02_01" ...
    std::string music;          // "mus05" / "none"
    // DefineStrategicPoints(territory, total_capacity, points_per_turn).
    // Older editor code misread these two values as map coordinates; keep the
    // legacy fields below for compatibility, but store the real semantics too.
    int strategic_points_total = 0;
    int strategic_points_per_turn = 0;
    int strategic_x = 0;
    int strategic_y = 0;
    // Finální území (LASTTERT) - ikona zkøížených meèù
    bool is_final = false;

    // Video pøi dobytí území
    std::string conquest_video;

    // Timeout v tazích (0 = bez timeoutu)
    int timeout_turns = 0;

    // Counter-attack: nepøítel útoèí po N tazích od dobytí
    int counter_attack_turn = 0;
    std::string counter_attack_mission;
};

struct LevelMission {
    std::string name;           // "M02_02A"
    std::string end_ok_mission; // "none" / "M02_02B"
    std::string end_bad_mission;// ...
    // EndOK(money, experience) from LEVEL_XX.DEF.
    // These are strategic rewards for a successful mission: one-time money
    // and John Alexander's strategic experience (not tactical unit XP).
    int end_ok_x = -1;
    int end_ok_y = -1;
    std::string music;          // "mus01" / ""
    int freq_random_a = -1;     // FrequencyOfRandomAttacks(a,b)
    int freq_random_b = -1;

    int end_bad_event = -1;     // EndBadEvent(n) (viz M02_05A)

    // Èasový limit mise v tazích (0 = bez limitu), z Time(X) v DEF
    int time_limit = 0;

    // Videa po dokonèení mise
    std::string end_ok_video;
    std::string end_bad_video;

    // Dokonèení této mise dokonèí level
    bool is_level_final = false;

    // Další level DEF
    std::string next_level_def;
};

struct LevelEventArmy {
    std::vector<int> units;     // Army(37,37,38,...) -> [37,37,38,...]
};

struct LevelEvent {
    int id = 0;
    bool abs_time = false;      // AbsTime(...) vs Time(...)
    int time_value = 0;         // mùže být -1
    std::string text_id;        // EventText(E02_0001)
    std::vector<LevelEventArmy> armies;

    // WaitForTerritories(11,12) - event only fires after these territories are conquered
    std::vector<int> wait_for_territories;

    // RunEvents(5,6,7,8) - trigger these events when this event fires
    std::vector<int> run_events;

    // ChangeMission(territory_id, mission_name)
    int change_mission_territory = -1;
    std::string change_mission_name;

    // AddUnitToPlayer(unit_id, experience_level, health, name)
    struct UnitAdd {
        int unit_id = 0;
        int experience_level = 1; // original 2nd AddUnitToPlayer parameter (1..12), not a count
        int health = 0;
        std::string name;
    };
    std::vector<UnitAdd> add_units;

    // SetResearchFlag(N)
    std::vector<int> research_flags;

    // SetPlayersTerritory(N)
    int set_player_territory = -1;
};

struct LevelData {
    std::string source_path;

    int start_territory = -1;   // Start(n)
    int end_territory = -1;     // End(n)

    std::string level_music;    // LevelMusic(mus00)

    // Attack... z LevelInit
    std::vector<int> attack_units;         // AttackUnits(...)
    std::vector<int> attack_special_units; // AttackSpecialUnits(...)
    std::vector<int> attack_flags;         // AttackFlags(...)

    std::vector<int> research_flags;       // SetResearchFlag(n)

    struct PlayerUnitAdd {
        int unit_id = 0;   // AddUnitToPlayer(0,1,100,-)
        int count = 1;     // runtime/tactical multiplicity; strategic AddUnitToPlayer always creates one company
        int health = 0;
        std::string extra; // campaign-specific extra field

        // Transient strategic -> tactical metadata. These fields are zero for
        // units created directly by a map DEF, but when a permanent campaign
        // unit is sent from the strategic screen they preserve its concrete
        // roster identity and active hierarchy/formation state.
        uint32_t strategic_uid = 0;
        int experience = 0;               // total strategic experience carried into the tactical battle
        int experience_level = 1;         // derived Spellcross experience level (1..12)
        int formation_id = 0;             // persistent battalion membership (1..8); HUD number only while active
        int formation_level = 0;          // 0=none, 1=battalion, 2=regiment, 3=brigade
        int formation_attack_bonus = 0;   // original FORMACIE.DEF bonus
        int formation_defence_bonus = 0;  // original FORMACIE.DEF bonus
        // Installed strategic technology modifiers from UPGRADES.DEF.
        int upgrade_move_bonus = 0;
        int upgrade_defence_bonus = 0;
        int upgrade_attack_bonus = 0;
        int upgrade_attack_count_bonus = 0;
        int upgrade_range_bonus = 0;
        uint8_t formation_commander_mask = 0; // bit0=battalion, bit1=regiment, bit2=brigade commander hosted here
        bool carries_commander = false;   // compatibility/convenience: formation_commander_mask != 0
    };    std::vector<PlayerUnitAdd> start_units;

    std::vector<LevelTerritory> territories;
    std::vector<LevelMission> missions;
    std::vector<LevelEvent> events;

    std::vector<std::string> unknown_lines; // pro debug

    // Intro/outro videa levelu
    std::string intro_video;
    std::string outro_video;

    // Výchozí další level
    std::string next_level_def;
};

class LevelLoader {
public:
    bool LoadLevelDef(const std::string& path, LevelData& out, std::string* err = nullptr);
};
