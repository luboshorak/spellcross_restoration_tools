#include "level.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// --------- helpers ---------

static inline std::string trim(std::string s) {
    auto notspace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
    return s;
}

static inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

static inline std::string strip_comment_semicolon(std::string s) {
    // Spellcross DEF má komentáøe od ';' do konce øádku
    auto pos = s.find(';');
    if (pos != std::string::npos) s = s.substr(0, pos);
    return s;
}

static bool parse_int(const std::string& s, int& out) {
    try {
        size_t idx = 0;
        int v = std::stoi(s, &idx, 10);
        if (idx != s.size()) return false;
        out = v;
        return true;
    }
    catch (...) {
        return false;
    }
}

// Rozparsuje "Name(a,b,c)" -> name="Name", args=["a","b","c"] (trimnuté).
// Vrací false, když to není call.
static bool parse_call(const std::string& line, std::string& name, std::vector<std::string>& args) {
    auto l = trim(line);
    if (l.empty()) return false;

    auto lp = l.find('(');
    auto rp = l.rfind(')');
    if (lp == std::string::npos || rp == std::string::npos || rp < lp) return false;

    name = trim(l.substr(0, lp));
    auto inside = l.substr(lp + 1, rp - lp - 1);

    args.clear();
    std::string cur;
    for (char ch : inside) {
        if (ch == ',') {
            args.push_back(trim(cur));
            cur.clear();
        }
        else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty() || !inside.empty())
        args.push_back(trim(cur));

    return !name.empty();
}

// header typu: "Mission(M02_02A) {" nebo "Event(2) {"
static bool parse_block_header(const std::string& line, std::string& blockName, std::vector<std::string>& headerArgs) {
    auto l = trim(line);
    if (l.empty()) return false;
    if (l.find('{') == std::string::npos) return false;

    // LevelInit {  (bez závorek)
    if (starts_with(l, "LevelInit")) {
        blockName = "LevelInit";
        headerArgs.clear();
        return true;
    }

    // Mission(...) { / Event(...) {
    std::string callName;
    std::vector<std::string> callArgs;
    // vezmeme jen èást do '{', aby parse_call neshodil závorku
    auto beforeBrace = trim(l.substr(0, l.find('{')));
    if (!parse_call(beforeBrace, callName, callArgs)) return false;

    blockName = callName;
    headerArgs = callArgs;
    return true;
}

// --------- loader ---------

bool LevelLoader::LoadLevelDef(const std::string& path, LevelData& out, std::string* err) {
    out = LevelData{};
    out.source_path = path;

    std::ifstream f(path);
    if (!f) {
        if (err) *err = "Cannot open file: " + path;
        return false;
    }

    enum class Ctx { None, LevelInit, Mission, Event };
    Ctx ctx = Ctx::None;

    LevelMission curMission;
    bool hasMission = false;

    LevelEvent curEvent;
    bool hasEvent = false;

    auto flush_mission = [&]() {
        if (hasMission) {
            out.missions.push_back(curMission);
            curMission = LevelMission{};
            hasMission = false;
        }
        };

    auto flush_event = [&]() {
        if (hasEvent) {
            out.events.push_back(curEvent);
            curEvent = LevelEvent{};
            hasEvent = false;
        }
        };

    std::string raw;
    int lineNo = 0;

    while (std::getline(f, raw)) {
        lineNo++;

        std::string line = strip_comment_semicolon(raw);
        line = trim(line);
        if (line.empty()) continue;

        // konec bloku
        if (line == "}") {
            if (ctx == Ctx::Mission) flush_mission();
            if (ctx == Ctx::Event) flush_event();
            ctx = Ctx::None;
            continue;
        }

        // start bloku?
        if (line.find('{') != std::string::npos) {
            std::string bname;
            std::vector<std::string> hargs;
            if (parse_block_header(line, bname, hargs)) {
                if (bname == "LevelInit") {
                    ctx = Ctx::LevelInit;
                    continue;
                }
                if (bname == "Mission") {
                    flush_mission();
                    ctx = Ctx::Mission;
                    hasMission = true;
                    curMission = LevelMission{};
                    if (hargs.size() >= 1) curMission.name = hargs[0];
                    continue;
                }
                if (bname == "Event") {
                    flush_event();
                    ctx = Ctx::Event;
                    hasEvent = true;
                    curEvent = LevelEvent{};
                    if (hargs.size() >= 1) {
                        int id = 0;
                        if (parse_int(hargs[0], id)) curEvent.id = id;
                    }
                    continue;
                }
            }
        }

        // uvnitø bloku: øádkové calls
        std::string cmd;
        std::vector<std::string> args;
        if (!parse_call(line, cmd, args)) {
            // neznámý formát øádku
            out.unknown_lines.push_back("L" + std::to_string(lineNo) + ": " + line);
            continue;
        }

        auto unknown_here = [&]() {
            out.unknown_lines.push_back("L" + std::to_string(lineNo) + " [" +
                (ctx == Ctx::LevelInit ? "LevelInit" : ctx == Ctx::Mission ? "Mission" : ctx == Ctx::Event ? "Event" : "None")
                + "]: " + cmd + "(" + (args.empty() ? "" : args[0]) + (args.size() > 1 ? ",..." : "") + ")");
            };

        // --- LevelInit parsing ---
        if (ctx == Ctx::LevelInit) {
            if (cmd == "Start" && args.size() >= 1) {
                int v = 0; if (parse_int(args[0], v)) out.start_territory = v;
                continue;
            }
            if (cmd == "End" && args.size() >= 1) {
                int v = 0; if (parse_int(args[0], v)) out.end_territory = v;
                continue;
            }
            if (cmd == "Territory" && args.size() >= 4) {
                LevelTerritory t;
                parse_int(args[0], t.id);
                t.intro_mission = args[1];
                t.mission = args[2];
                t.music = args[3];
                out.territories.push_back(t);
                continue;
            }
            if (cmd == "DefineStrategicPoints" && args.size() >= 3) {
                int tid = 0, x = 0, y = 0;
                if (parse_int(args[0], tid) && parse_int(args[1], x) && parse_int(args[2], y)) {
                    // pøiøaï k poslednímu territory se stejným id
                    for (auto& t : out.territories) {
                        if (t.id == tid) { t.strategic_x = x; t.strategic_y = y; break; }
                    }
                }
                continue;
            }
            if (cmd == "LevelMusic" && args.size() >= 1) {
                out.level_music = args[0];
                continue;
            }
            if (cmd == "AttackUnits") {
                out.attack_units.clear();
                for (auto& a : args) { int v = 0; if (parse_int(a, v)) out.attack_units.push_back(v); }
                continue;
            }
            if (cmd == "AttackSpecialUnits") {
                out.attack_special_units.clear();
                for (auto& a : args) { int v = 0; if (parse_int(a, v)) out.attack_special_units.push_back(v); }
                continue;
            }
            if (cmd == "AttackFlags") {
                out.attack_flags.clear();
                for (auto& a : args) { int v = 0; if (parse_int(a, v)) out.attack_flags.push_back(v); }
                continue;
            }
            if (cmd == "SetResearchFlag" && args.size() >= 1) {
                int v = 0; if (parse_int(args[0], v)) out.research_flags.push_back(v);
                continue;
            }
            if (cmd == "AddUnitToPlayer" && args.size() >= 4) {
                LevelData::PlayerUnitAdd pu;
                parse_int(args[0], pu.unit_id);
                parse_int(args[1], pu.count);
                parse_int(args[2], pu.health);
                pu.extra = args[3];
                out.start_units.push_back(pu);
                continue;
            }

            // jinak ignor/tolerant
            unknown_here();
            continue;
        }

        // --- Mission parsing ---
        if (ctx == Ctx::Mission && hasMission) {
            if (cmd == "EndOKMission" && args.size() >= 1) { curMission.end_ok_mission = args[0]; continue; }
            if (cmd == "EndBadMission" && args.size() >= 1) { curMission.end_bad_mission = args[0]; continue; }
            if (cmd == "EndBadEvent" && args.size() >= 1) { int v = 0; if (parse_int(args[0], v)) curMission.end_bad_event = v; continue; }
            if (cmd == "EndOK" && args.size() >= 2) { parse_int(args[0], curMission.end_ok_x); parse_int(args[1], curMission.end_ok_y); continue; }
            if (cmd == "MissionMusic" && args.size() >= 1) { curMission.music = args[0]; continue; }
            if (cmd == "FrequencyOfRandomAttacks" && args.size() >= 2) {
                parse_int(args[0], curMission.freq_random_a);
                parse_int(args[1], curMission.freq_random_b);
                continue;
            }

            unknown_here();
            continue;
        }

        // --- Event parsing ---
        if (ctx == Ctx::Event && hasEvent) {
            if (cmd == "AbsTime" && args.size() >= 1) {
                curEvent.abs_time = true;
                parse_int(args[0], curEvent.time_value);
                continue;
            }
            if (cmd == "Time" && args.size() >= 1) {
                curEvent.abs_time = false;
                parse_int(args[0], curEvent.time_value);
                continue;
            }
            if (cmd == "EventText" && args.size() >= 1) {
                curEvent.text_id = args[0];
                continue;
            }
            if (cmd == "Army" && !args.empty()) {
                LevelEventArmy a;
                for (auto& s : args) {
                    int v = 0; if (parse_int(s, v)) a.units.push_back(v);
                }
                curEvent.armies.push_back(std::move(a));
                continue;
            }

            unknown_here();
            continue;
        }

        // mimo bloky: tolerujeme
        unknown_here();
    }

    // kdyby soubor skonèil bez '}'
    if (ctx == Ctx::Mission) flush_mission();
    if (ctx == Ctx::Event) flush_event();

    return true;
}
