//=============================================================================
// Spellcross Map Editor
// ----------------------------------------------------------------------------
// Top level functions, wxWidgets GUI.
// 
// This code is part of Spellcross Map Editor project.
// (c) 2021-2025, Stanislav Maslan, s.maslan@seznam.cz
// url: https://github.com/smaslan/spellcross-map-edit
// Distributed under MIT license, https://opensource.org/licenses/MIT.
//=============================================================================
// 
// For compilers that support precompilation, includes "wx/wx.h".
//#define wxMSVC_VERSION_ABI_COMPAT
#include <wx/wxprec.h>
//#include <wx/msw/wx.rc>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/dcgraph.h>
#include <wx/dcbuffer.h>
#include <wx/rawbmp.h>
#include <wx/timer.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/slider.h>
#include <wx/stdpaths.h>
#include <wx/event.h>

#include <filesystem>
#include <algorithm>
#include <codecvt>
#include <tuple>
#include <string>
#include <chrono>
#include <cmath>
#include <cctype>
#include <future>
#include <thread>
#include <memory>
#include <map>

#include "resource.h"
#include "app_identity.h"
#include "main.h"
#include "other.h"
#include "simpleini.h"
#include "spellcross.h"
#include "map.h"

#include "level.h"
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/busyinfo.h>
#include "forms/form_level.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <iterator>

namespace
{

    struct TacticalCampaignContext
    {
        bool present = false;
        std::string level_def_path;
        std::string strategic_state_json;
        bool pending_valid = false;
        int territory_id = -1;
        std::string mission_token;
        std::vector<std::uint64_t> sent_unit_indices;
        std::vector<std::uint32_t> sent_unit_uids;
        SpellMap::MissionLossStats mission_losses;
    };

    static wxBitmap MakeSpellTextBitmap(SpellFont* font, const wxString& text,
        const wxColour& fg, int boxWidth, bool centered)
    {
        if (!font || text.empty() || boxWidth <= 0)
            return wxBitmap();
        const int h = std::max(1, font->GetHeight()) + 4;
        std::vector<uint8_t> mask(static_cast<size_t>(boxWidth) * static_cast<size_t>(h), 0);
        std::string encoded = wstring2stringCP895(text.ToStdWstring());
        const int tw = font->GetTextWidth(encoded);
        const int x = centered ? std::max(0, (boxWidth - tw) / 2) : 0;
        font->Render(mask.data(), mask.data() + mask.size(), boxWidth, x, 1,
            text.ToStdWstring(), 2, 1, SpellFont::RIGHT_DOWN, SpellFont::LEFT);

        wxImage img(boxWidth, h, true);
        img.InitAlpha();
        unsigned char* rgb = img.GetData();
        unsigned char* alpha = img.GetAlpha();
        if (!rgb || !alpha)
            return wxBitmap();
        for (size_t i = 0; i < mask.size(); ++i)
        {
            if (mask[i] == 0)
            {
                alpha[i] = 0;
                continue;
            }
            alpha[i] = 255;
            if (mask[i] == 1)
            {
                rgb[i * 3 + 0] = 0; rgb[i * 3 + 1] = 0; rgb[i * 3 + 2] = 0;
            }
            else
            {
                rgb[i * 3 + 0] = fg.Red();
                rgb[i * 3 + 1] = fg.Green();
                rgb[i * 3 + 2] = fg.Blue();
            }
        }
        return wxBitmap(img);
    }

    static wxBitmap ScaleNearest(const wxBitmap& bmp, double scale)
    {
        if (!bmp.IsOk() || scale <= 0.0 || std::abs(scale - 1.0) < 0.001)
            return bmp;
        wxImage img = bmp.ConvertToImage();
        return wxBitmap(img.Scale(std::max(1, (int)std::lround(img.GetWidth() * scale)),
            std::max(1, (int)std::lround(img.GetHeight() * scale)), wxIMAGE_QUALITY_NEAREST));
    }

    static constexpr char kTacticalContextMarker[] = "\n--SPELLCROSS-STRATEGIC-CONTEXT-V1--\n";
    static constexpr std::uint32_t kTacticalContextMagic = 0x31584353u; // SCX1
    static constexpr std::uint32_t kTacticalContextVersion = 3u;

    template <typename T>
    static bool WriteCtxPod(std::ostream& os, const T& v)
    {
        os.write(reinterpret_cast<const char*>(&v), sizeof(T));
        return static_cast<bool>(os);
    }

    template <typename T>
    static bool ReadCtxPod(std::istream& is, T& v)
    {
        is.read(reinterpret_cast<char*>(&v), sizeof(T));
        return static_cast<bool>(is);
    }

    static bool WriteCtxString(std::ostream& os, const std::string& v)
    {
        const std::uint64_t n = static_cast<std::uint64_t>(v.size());
        if (!WriteCtxPod(os, n)) return false;
        if (n) os.write(v.data(), static_cast<std::streamsize>(n));
        return static_cast<bool>(os);
    }

    static bool ReadCtxString(std::istream& is, std::string& v)
    {
        std::uint64_t n = 0;
        if (!ReadCtxPod(is, n)) return false;
        constexpr std::uint64_t kMaxBlob = 64ull * 1024ull * 1024ull;
        if (n > kMaxBlob) return false;
        v.resize(static_cast<size_t>(n));
        if (n) is.read(v.data(), static_cast<std::streamsize>(n));
        return static_cast<bool>(is);
    }

    static bool AppendTacticalCampaignContext(const std::wstring& path, const TacticalCampaignContext& ctx)
    {
        std::ofstream os(std::filesystem::path(path), std::ios::binary | std::ios::app);
        if (!os) return false;
        os.write(kTacticalContextMarker, sizeof(kTacticalContextMarker) - 1);
        if (!WriteCtxPod(os, kTacticalContextMagic) || !WriteCtxPod(os, kTacticalContextVersion)) return false;
        if (!WriteCtxString(os, ctx.level_def_path)) return false;
        if (!WriteCtxString(os, ctx.strategic_state_json)) return false;
        const std::uint8_t pv = ctx.pending_valid ? 1u : 0u;
        if (!WriteCtxPod(os, pv)) return false;
        const std::int32_t terr = static_cast<std::int32_t>(ctx.territory_id);
        if (!WriteCtxPod(os, terr)) return false;
        if (!WriteCtxString(os, ctx.mission_token)) return false;
        const std::uint64_t count = static_cast<std::uint64_t>(ctx.sent_unit_indices.size());
        if (!WriteCtxPod(os, count)) return false;
        for (std::uint64_t idx : ctx.sent_unit_indices)
            if (!WriteCtxPod(os, idx)) return false;
        const std::uint64_t uidCount = static_cast<std::uint64_t>(ctx.sent_unit_uids.size());
        if (!WriteCtxPod(os, uidCount)) return false;
        for (std::uint32_t uid : ctx.sent_unit_uids)
            if (!WriteCtxPod(os, uid)) return false;
        const int losses[8] = {
            ctx.mission_losses.alliance_light, ctx.mission_losses.alliance_heavy,
            ctx.mission_losses.alliance_air, ctx.mission_losses.alliance_commanders,
            ctx.mission_losses.enemy_light, ctx.mission_losses.enemy_heavy,
            ctx.mission_losses.enemy_air, ctx.mission_losses.enemy_commanders };
        for (int v : losses)
        {
            const std::int32_t x = static_cast<std::int32_t>(v);
            if (!WriteCtxPod(os, x)) return false;
        }
        return static_cast<bool>(os);
    }

    static bool ReadTacticalCampaignContext(const std::wstring& path, TacticalCampaignContext& ctx)
    {
        ctx = {};
        std::ifstream f(std::filesystem::path(path), std::ios::binary);
        if (!f) return false;
        std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const std::string marker(kTacticalContextMarker, sizeof(kTacticalContextMarker) - 1);
        const size_t pos = all.rfind(marker);
        if (pos == std::string::npos) return false;
        std::istringstream is(all.substr(pos + marker.size()), std::ios::binary);
        std::uint32_t magic = 0, ver = 0;
        if (!ReadCtxPod(is, magic) || !ReadCtxPod(is, ver) ||
            magic != kTacticalContextMagic || ver < 1u || ver > kTacticalContextVersion)
            return false;
        if (!ReadCtxString(is, ctx.level_def_path)) return false;
        if (!ReadCtxString(is, ctx.strategic_state_json)) return false;
        std::uint8_t pv = 0;
        std::int32_t terr = -1;
        if (!ReadCtxPod(is, pv) || !ReadCtxPod(is, terr)) return false;
        ctx.pending_valid = pv != 0;
        ctx.territory_id = static_cast<int>(terr);
        if (!ReadCtxString(is, ctx.mission_token)) return false;
        std::uint64_t count = 0;
        if (!ReadCtxPod(is, count) || count > 100000ull) return false;
        ctx.sent_unit_indices.resize(static_cast<size_t>(count));
        for (std::uint64_t& idx : ctx.sent_unit_indices)
            if (!ReadCtxPod(is, idx)) return false;
        if (ver >= 2u)
        {
            std::uint64_t uidCount = 0;
            if (!ReadCtxPod(is, uidCount) || uidCount > 100000ull) return false;
            ctx.sent_unit_uids.resize(static_cast<size_t>(uidCount));
            for (std::uint32_t& uid : ctx.sent_unit_uids)
                if (!ReadCtxPod(is, uid)) return false;
        }
        if (ver >= 3u)
        {
            std::int32_t losses[8]{};
            for (auto& v : losses)
                if (!ReadCtxPod(is, v)) return false;
            ctx.mission_losses.alliance_light = losses[0];
            ctx.mission_losses.alliance_heavy = losses[1];
            ctx.mission_losses.alliance_air = losses[2];
            ctx.mission_losses.alliance_commanders = losses[3];
            ctx.mission_losses.enemy_light = losses[4];
            ctx.mission_losses.enemy_heavy = losses[5];
            ctx.mission_losses.enemy_air = losses[6];
            ctx.mission_losses.enemy_commanders = losses[7];
        }
        ctx.present = true;
        return true;
    }

    static bool IsUsableConfig(const std::filesystem::path& path)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec || fs::file_size(path, ec) == 0 || ec)
            return false;

        CSimpleIniA probe;
        probe.SetUnicode();
        if (probe.LoadFile(path.wstring().c_str()) != SI_OK)
            return false;

        const char* spell_path = probe.GetValue("SPELCROS", "spell_path", "");
        return spell_path && *spell_path;
    }

    static std::filesystem::path FindRuntimeRoot(const std::filesystem::path& executable_dir,
        const std::filesystem::path& startup_dir)
    {
        namespace fs = std::filesystem;

        const auto find_from = [](fs::path dir) -> fs::path
        {
            std::error_code ec;
            for (;;)
            {
                if (fs::is_regular_file(dir / "data" / "units.fsa", ec) && !ec)
                    return dir;
                ec.clear();

                const fs::path parent = dir.parent_path();
                if (parent.empty() || parent == dir)
                    break;
                dir = parent;
            }
            return {};
        };

        if (auto root = find_from(executable_dir); !root.empty())
            return root;
        if (auto root = find_from(startup_dir); !root.empty())
            return root;
        return executable_dir;
    }

    static std::filesystem::path FindConfigPath(const std::filesystem::path& executable_dir,
        const std::filesystem::path& runtime_root,
        const std::filesystem::path& startup_dir)
    {
        namespace fs = std::filesystem;

        // Prefer a config placed next to the executable even when it is empty.
        // A fresh release is allowed to ship an empty config.ini; the startup
        // source wizard will populate it instead of rejecting it outright.
        std::vector<fs::path> candidates = {
            executable_dir / "config.ini",
            runtime_root / "config.ini",
            runtime_root / "source" / "config.ini",
            startup_dir / "config.ini",
            startup_dir / "source" / "config.ini"
        };

        fs::path dir = executable_dir;
        for (;;)
        {
            candidates.push_back(dir / "config.ini");
            candidates.push_back(dir / "source" / "config.ini");
            const fs::path parent = dir.parent_path();
            if (parent.empty() || parent == dir)
                break;
            dir = parent;
        }

        std::error_code ec;
        for (const auto& candidate : candidates)
        {
            if (fs::is_regular_file(candidate, ec) && !ec)
                return fs::absolute(candidate).lexically_normal();
            ec.clear();
        }

        // No config yet: create/use one beside the executable.
        return fs::absolute(executable_dir / "config.ini").lexically_normal();
    }

    static std::string to_lower(std::string s)
    {
        for (char& ch : s)
        {
            const unsigned char uch = static_cast<unsigned char>(ch);
            ch = static_cast<char>(std::tolower(uch));
        }
        return s;
    }

    static std::filesystem::path FindFileCaseInsensitive(const std::filesystem::path& dir,
        const std::string& wanted)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        if (!fs::exists(dir, ec) || ec)
            return {};
        if (!fs::is_directory(dir, ec) || ec)
            return {};

        const std::string w = to_lower(wanted);

        // skip_permission_denied = ať to nezdechne na právech
        for (const auto& de : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec)
                break;

            if (!de.is_regular_file(ec) || ec)
            {
                ec.clear();
                continue;
            }

            const std::string fn = to_lower(de.path().filename().string());
            if (fn == w)
                return de.path();
        }

        return {};
    }

    static std::filesystem::path FindSpellDataFile(const std::filesystem::path& root,
        const std::string& filename)
    {
        namespace fs = std::filesystem;
        fs::path temp_common = fs::current_path() / "temp" / "COMMON";

        const std::vector<std::filesystem::path> candidates = {
            root,
            root / "DATA",
            root / "COMMON",
            root / "DATA" / "COMMON",
            root / "CD",
            root / "DATA" / "CD",
            temp_common,
        };

        for (const auto& dir : candidates)
        {
            auto found = FindFileCaseInsensitive(dir, filename);
            if (!found.empty())
                return found;
        }
        return {};
    }


    static std::filesystem::path ResolveStartupConfigPath(const char* value,
        const std::filesystem::path& config_path,
        const std::filesystem::path& executable_dir)
    {
        namespace fs = std::filesystem;
        if (!value || !*value)
            return {};

        const fs::path configured = char2wstring(value);
        if (configured.is_absolute())
            return configured.lexically_normal();

        std::vector<fs::path> roots = {
            fs::current_path(), config_path.parent_path(), executable_dir
        };
        std::error_code ec;
        for (const auto& root : roots)
        {
            const fs::path candidate = (root / configured).lexically_normal();
            if (fs::exists(candidate, ec) && !ec)
                return candidate;
            ec.clear();
        }
        return (config_path.parent_path() / configured).lexically_normal();
    }

    static bool FileNameEquals(const std::filesystem::path& path, const std::string& expected)
    {
        return to_lower(path.filename().string()) == to_lower(expected);
    }

    static bool IsValidSourceFile(const std::filesystem::path& path, const std::string& expected)
    {
        std::error_code ec;
        return !path.empty() && FileNameEquals(path, expected) &&
            std::filesystem::is_regular_file(path, ec) && !ec &&
            std::filesystem::file_size(path, ec) > 0 && !ec;
    }

    static std::string ConfigPathValue(const std::filesystem::path& path,
        const std::filesystem::path& config_path,
        const std::filesystem::path& runtime_root)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path abs = fs::absolute(path, ec).lexically_normal();
        const std::vector<fs::path> bases = { config_path.parent_path(), runtime_root };
        for (const auto& base : bases)
        {
            if (base.empty()) continue;
            const fs::path rel = abs.lexically_relative(fs::absolute(base, ec).lexically_normal());
            if (!rel.empty())
            {
                const auto first = rel.begin();
                if (first != rel.end() && first->string() != "..")
                    return wstring2string(rel.wstring());
            }
        }
        return wstring2string(abs.wstring());
    }

    struct InstallDtaEntry
    {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t directory_index = 0;
    };

    static bool ReadInstallDtaU32(std::ifstream& f, std::uint32_t& value)
    {
        unsigned char b[4] = {};
        f.read(reinterpret_cast<char*>(b), 4);
        if (!f)
            return false;
        value = static_cast<std::uint32_t>(b[0]) |
            (static_cast<std::uint32_t>(b[1]) << 8) |
            (static_cast<std::uint32_t>(b[2]) << 16) |
            (static_cast<std::uint32_t>(b[3]) << 24);
        return true;
    }

    // INSTALL.DTA is the original DOS installer's payload container. Its layout is:
    //   u32 directory-block-size
    //   u32 directory-count + NUL-terminated directory names
    //   u32 file-count
    //   file-count * { char name[13], u32 absolute_offset, u32 size, u32 directory_index }
    // File payloads are stored raw at the recorded absolute offsets.
    static bool ReadInstallDtaIndex(const std::filesystem::path& dta_path,
        std::vector<std::string>& directories,
        std::vector<InstallDtaEntry>& entries,
        std::string& error)
    {
        namespace fs = std::filesystem;
        directories.clear();
        entries.clear();
        error.clear();

        std::ifstream f(dta_path, std::ios::binary);
        if (!f)
        {
            error = "Cannot open INSTALL.DTA.";
            return false;
        }

        std::error_code ec;
        const std::uintmax_t total_size = fs::file_size(dta_path, ec);
        if (ec || total_size < 16)
        {
            error = "INSTALL.DTA is empty or invalid.";
            return false;
        }

        std::uint32_t directory_block_size = 0;
        std::uint32_t directory_count = 0;
        if (!ReadInstallDtaU32(f, directory_block_size) ||
            !ReadInstallDtaU32(f, directory_count) ||
            directory_block_size < 4 || directory_block_size > 1024 * 1024 ||
            directory_count == 0 || directory_count > 1024)
        {
            error = "INSTALL.DTA has an invalid directory table.";
            return false;
        }

        directories.reserve(directory_count);
        for (std::uint32_t i = 0; i < directory_count; ++i)
        {
            std::string dir;
            for (size_t guard = 0; guard < 4096; ++guard)
            {
                char ch = 0;
                f.read(&ch, 1);
                if (!f)
                {
                    error = "INSTALL.DTA directory table is truncated.";
                    return false;
                }
                if (ch == '\0')
                    break;
                dir.push_back(ch);
                if (guard == 4095)
                {
                    error = "INSTALL.DTA contains an invalid directory name.";
                    return false;
                }
            }
            directories.push_back(dir);
        }

        // The first DWORD describes the whole directory block beginning at byte 4.
        // Seek to its end instead of relying on exact string packing/padding.
        const std::uint64_t file_count_pos = 4ull + static_cast<std::uint64_t>(directory_block_size);
        if (file_count_pos + 4ull > total_size)
        {
            error = "INSTALL.DTA directory table points outside the file.";
            return false;
        }
        f.clear();
        f.seekg(static_cast<std::streamoff>(file_count_pos), std::ios::beg);

        std::uint32_t file_count = 0;
        if (!ReadInstallDtaU32(f, file_count) || file_count == 0 || file_count > 100000)
        {
            error = "INSTALL.DTA has an invalid file table.";
            return false;
        }

        const std::uint64_t records_end = file_count_pos + 4ull + static_cast<std::uint64_t>(file_count) * 25ull;
        if (records_end > total_size)
        {
            error = "INSTALL.DTA file table is truncated.";
            return false;
        }

        entries.reserve(file_count);
        for (std::uint32_t i = 0; i < file_count; ++i)
        {
            char raw_name[13] = {};
            f.read(raw_name, sizeof(raw_name));
            if (!f)
            {
                error = "INSTALL.DTA file table is truncated.";
                return false;
            }

            InstallDtaEntry entry;
            size_t name_len = 0;
            while (name_len < sizeof(raw_name) && raw_name[name_len] != '\0')
                ++name_len;
            entry.name.assign(raw_name, raw_name + name_len);

            if (!ReadInstallDtaU32(f, entry.offset) ||
                !ReadInstallDtaU32(f, entry.size) ||
                !ReadInstallDtaU32(f, entry.directory_index))
            {
                error = "INSTALL.DTA file table is truncated.";
                return false;
            }

            if (entry.name.empty() || entry.directory_index >= directories.size() ||
                static_cast<std::uint64_t>(entry.offset) + static_cast<std::uint64_t>(entry.size) > total_size)
            {
                error = "INSTALL.DTA contains an invalid file entry.";
                return false;
            }
            entries.push_back(entry);
        }

        return true;
    }

    static bool InstallDtaSegmentMatchesFile(std::ifstream& dta,
        std::uint64_t offset,
        std::uint64_t size,
        const std::filesystem::path& destination)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_regular_file(destination, ec) || ec || fs::file_size(destination, ec) != size || ec)
            return false;

        std::ifstream out(destination, std::ios::binary);
        if (!out)
            return false;

        dta.clear();
        dta.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!dta)
            return false;

        std::vector<char> a(1024 * 1024);
        std::vector<char> b(a.size());
        std::uint64_t remaining = size;
        while (remaining)
        {
            const size_t chunk = static_cast<size_t>(std::min<std::uint64_t>(remaining, a.size()));
            dta.read(a.data(), static_cast<std::streamsize>(chunk));
            out.read(b.data(), static_cast<std::streamsize>(chunk));
            if (!dta || !out || !std::equal(a.begin(), a.begin() + chunk, b.begin()))
                return false;
            remaining -= chunk;
        }
        return true;
    }

    static bool ExtractInstallDtaDataFiles(const std::filesystem::path& dta_path,
        const std::filesystem::path& destination_data_dir,
        std::vector<std::filesystem::path>& extracted,
        std::string& error)
    {
        namespace fs = std::filesystem;
        extracted.clear();
        error.clear();

        std::vector<std::string> directories;
        std::vector<InstallDtaEntry> entries;
        if (!ReadInstallDtaIndex(dta_path, directories, entries, error))
            return false;

        std::error_code ec;
        fs::create_directories(destination_data_dir, ec);
        if (ec)
        {
            error = "Cannot create the local Spellcross game-data folder: " + ec.message();
            return false;
        }

        std::ifstream dta(dta_path, std::ios::binary);
        if (!dta)
        {
            error = "Cannot reopen INSTALL.DTA for extraction.";
            return false;
        }

        std::vector<char> buffer(1024 * 1024);
        for (const auto& entry : entries)
        {
            const std::string dir_name = to_lower(directories[entry.directory_index]);
            if (dir_name != "data")
                continue;

            // INSTALL.DTA uses DOS 8.3 names. Reject anything path-like even if a
            // damaged/custom image contains it; extraction must stay inside our folder.
            if (entry.name.find('/') != std::string::npos || entry.name.find('\\') != std::string::npos ||
                entry.name == "." || entry.name == "..")
                continue;

            const fs::path destination = destination_data_dir / fs::path(entry.name);
            if (InstallDtaSegmentMatchesFile(dta, entry.offset, entry.size, destination))
            {
                extracted.push_back(destination);
                continue;
            }

            dta.clear();
            dta.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
            if (!dta)
            {
                error = "Cannot seek to " + entry.name + " inside INSTALL.DTA.";
                return false;
            }

            fs::path partial = destination;
            partial += L".partial";
            std::ofstream out(partial, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                error = "Cannot create " + destination.string() + ".";
                return false;
            }

            std::uint64_t remaining = entry.size;
            while (remaining)
            {
                const size_t chunk = static_cast<size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                dta.read(buffer.data(), static_cast<std::streamsize>(chunk));
                if (!dta)
                {
                    out.close();
                    fs::remove(partial, ec);
                    error = "INSTALL.DTA is truncated while extracting " + entry.name + ".";
                    return false;
                }
                out.write(buffer.data(), static_cast<std::streamsize>(chunk));
                if (!out)
                {
                    out.close();
                    fs::remove(partial, ec);
                    error = "Writing " + destination.string() + " failed.";
                    return false;
                }
                remaining -= chunk;
            }
            out.close();

            fs::remove(destination, ec);
            ec.clear();
            fs::rename(partial, destination, ec);
            if (ec)
            {
                fs::remove(partial, ec);
                error = "Cannot finalize " + destination.string() + ".";
                return false;
            }
            extracted.push_back(destination);
        }

        if (extracted.empty())
        {
            error = "INSTALL.DTA was readable, but it did not contain a DATA payload.";
            return false;
        }
        return true;
    }

    static bool CopyCdArchiveIfPresent(const std::filesystem::path& cd_data_dir,
        const std::filesystem::path& destination_data_dir,
        const std::string& source_name,
        const std::string& destination_name,
        std::filesystem::path* copied_path = nullptr)
    {
        namespace fs = std::filesystem;
        const fs::path source = FindFileCaseInsensitive(cd_data_dir, source_name);
        if (source.empty())
            return false;

        std::error_code ec;
        fs::create_directories(destination_data_dir, ec);
        if (ec)
            return false;

        const fs::path destination = destination_data_dir / destination_name;
        // CD import is an explicit repair/import operation, so overwrite the
        // local sidecar. This also repairs a same-sized but corrupted old copy.
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
        if (ec)
            return false;
        if (copied_path)
            *copied_path = destination;
        return true;
    }

    static std::filesystem::path FindInstallDtaFromCdSelection(const std::filesystem::path& selected)
    {
        namespace fs = std::filesystem;
        if (selected.empty())
            return {};

        fs::path found = FindFileCaseInsensitive(selected, "INSTALL.DTA");
        if (!found.empty())
            return found;
        found = FindFileCaseInsensitive(selected / "DATA", "INSTALL.DTA");
        if (!found.empty())
            return found;
        return {};
    }

    struct StartupSourceRequirement
    {
        const char* key;
        const char* filename;
        const char* label;
        bool optional;
        const char* skip_key;
    };

    static bool EnsureSpellcrossSourceConfiguration(CSimpleIniA& ini,
        const std::filesystem::path& config_path,
        const std::filesystem::path& runtime_root,
        const std::filesystem::path& executable_dir)
    {
        namespace fs = std::filesystem;

        const StartupSourceRequirement reqs[] = {
            {"common_fs", "COMMON.FS", "core graphics and definitions", false, nullptr},
            {"t11_fs", "T11.FS", "T11 terrain graphics", false, nullptr},
            {"pust_fs", "PUST.FS", "PUST terrain graphics", false, nullptr},
            {"devast_fs", "DEVAST.FS", "DEVAST terrain graphics", false, nullptr},
            {"units_fsu", "UNITS.FSU", "unit graphics", false, nullptr},
            {"texts_fs", "TEXTS.FS", "game text tables", false, nullptr},
            {"info_fs", "INFO.FS", "unit information graphics", false, nullptr},
            {"samples_fs", "SAMPLES.FS", "sound effects", true, "samples_fs_skip"},
            {"music_fs", "MUSIC.FS", "music", true, "music_fs_skip"}
        };

        // Program-side helper data ships with the release; make clean configs useful.
        if (!*ini.GetValue("DATA", "spec_data_path", ""))
            ini.SetValue("DATA", "spec_data_path", "data");
        if (!*ini.GetValue("DATA", "units_aux_data_path", ""))
            ini.SetValue("DATA", "units_aux_data_path", "data\\units.fsa");

        auto saveNow = [&]() -> bool
        {
            std::error_code saveEc;
            fs::create_directories(config_path.parent_path(), saveEc);
            if(saveEc)
                return false;
            return ini.SaveFile(config_path.wstring().c_str()) == SI_OK;
        };

        // Legacy directory config is still accepted and migrated automatically.
        const fs::path legacyData = ResolveStartupConfigPath(ini.GetValue("SPELCROS", "spell_path", ""), config_path, executable_dir);
        const fs::path legacyCd = ResolveStartupConfigPath(ini.GetValue("SPELCROS", "spellcd_path", ""), config_path, executable_dir);

        auto configuredPath = [&](const StartupSourceRequirement& req) -> fs::path
        {
            const char* value = ini.GetValue("FILES", req.key, "");
            if (value && *value)
                return ResolveStartupConfigPath(value, config_path, executable_dir);
            return {};
        };

        auto storeSource = [&](const StartupSourceRequirement& req, const fs::path& selected)
        {
            ini.SetValue("FILES", req.key, ConfigPathValue(selected, config_path, runtime_root).c_str());
            if (req.skip_key)
                ini.SetBoolValue("FILES", req.skip_key, false, nullptr, true);
        };

        auto tryLegacy = [&](const StartupSourceRequirement& req) -> fs::path
        {
            if (std::string(req.key) == "info_fs")
            {
                fs::path p = FindFileCaseInsensitive(legacyCd, req.filename);
                if (!p.empty()) return p;
                p = FindFileCaseInsensitive(legacyData / "CD", req.filename);
                if (!p.empty()) return p;
            }
            return FindFileCaseInsensitive(legacyData, req.filename);
        };

        // Seed [FILES] from old spell_path/spellcd_path where possible.
        bool migrated = false;
        for (const auto& req : reqs)
        {
            if (IsValidSourceFile(configuredPath(req), req.filename))
                continue;
            fs::path p = tryLegacy(req);
            if (IsValidSourceFile(p, req.filename))
            {
                storeSource(req, p);
                migrated = true;
            }
        }
        if (migrated && !saveNow())
        {
            wxMessageBox("The configuration could not be saved.", "Startup configuration", wxOK | wxICON_ERROR);
            return false;
        }

        auto autoDiscoverFrom = [&](const fs::path& seedDir)
        {
            if (seedDir.empty()) return;
            std::vector<fs::path> dirs = { seedDir, seedDir / "CD", seedDir.parent_path(), seedDir.parent_path() / "CD" };
            for (const auto& req : reqs)
            {
                if (IsValidSourceFile(configuredPath(req), req.filename))
                    continue;
                for (const auto& dir : dirs)
                {
                    fs::path p = FindFileCaseInsensitive(dir, req.filename);
                    if (IsValidSourceFile(p, req.filename))
                    {
                        storeSource(req, p);
                        break;
                    }
                }
            }
        };

        // Existing configured sources and release folder are discovery hints.
        autoDiscoverFrom(runtime_root);
        for (const auto& req : reqs)
        {
            const fs::path p = configuredPath(req);
            if (IsValidSourceFile(p, req.filename))
                autoDiscoverFrom(p.parent_path());
        }
        saveNow();

        // Fresh original-CD path: the DOS installer keeps the installed DATA files
        // inside DATA\INSTALL.DTA rather than exposing COMMON.FS etc. directly on
        // the disc. Offer to import that payload once before asking for individual
        // files. A mounted ISO works exactly like a physical CD.
        bool missingRequired = false;
        for (const auto& req : reqs)
        {
            if (!req.optional && !IsValidSourceFile(configuredPath(req), req.filename))
            {
                missingRequired = true;
                break;
            }
        }

        if (missingRequired)
        {
            const int importCd = wxMessageBox(
                "Some required Spellcross game data is missing.\n\n"
                "If you have the original Spellcross CD, or a mounted ISO image, "
                "Spellcross can import the installed game archives directly from "
                "DATA\\INSTALL.DTA.\n\n"
                "Import from the original CD / mounted ISO now?\n\n"
                "Choose No to locate the individual files manually.",
                "Spellcross original CD", wxYES_NO | wxICON_QUESTION);

            if (importCd == wxYES)
            {
                wxDirDialog cdDialog(nullptr,
                    "Select the Spellcross CD root or its DATA folder",
                    wxString(), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

                if (cdDialog.ShowModal() == wxID_OK)
                {
                    const fs::path selected = cdDialog.GetPath().ToStdWstring();
                    const fs::path installDta = FindInstallDtaFromCdSelection(selected);
                    if (installDta.empty())
                    {
                        wxMessageBox(
                            "INSTALL.DTA was not found.\n\n"
                            "Select either the root of the original Spellcross CD / mounted ISO, "
                            "or its DATA folder. Manual file selection will continue.",
                            "Spellcross original CD", wxOK | wxICON_WARNING);
                    }
                    else
                    {
                        const fs::path localDataDir = runtime_root / "game_data" / "DATA";
                        std::vector<fs::path> extracted;
                        std::string importError;

                        bool extractedOk = false;
                        {
                            wxBusyCursor busy;
                            extractedOk = ExtractInstallDtaDataFiles(
                                installDta, localDataDir, extracted, importError);
                        }

                        if (!extractedOk)
                        {
                            wxMessageBox(
                                wxString("Reading INSTALL.DTA failed:\n\n") + wxString::FromUTF8(importError.c_str()),
                                "Spellcross original CD", wxOK | wxICON_ERROR);
                        }
                        else
                        {
                            // These archives are CD-side resources, not part of the
                            // installed INSTALL.DTA payload. Copy them locally when
                            // the particular CD edition contains them so the disc is
                            // not needed on subsequent runs.
                            const fs::path cdDataDir = installDta.parent_path();
                            CopyCdArchiveIfPresent(cdDataDir, localDataDir, "INFO.FS", "INFO.FS");
                            if (!CopyCdArchiveIfPresent(cdDataDir, localDataDir, "MOVIE.FS", "MOVIE.FS"))
                                CopyCdArchiveIfPresent(cdDataDir, localDataDir, "MOVIES.FS", "MOVIE.FS");
                            CopyCdArchiveIfPresent(cdDataDir, localDataDir, "SPEAKER.FS", "SPEAKER.FS");

                            // The imported folder now behaves like a normal installed
                            // Spellcross DATA directory. Re-use the normal discovery
                            // path so [FILES] and the legacy directory keys stay in one
                            // place and remain portable/relative where possible.
                            autoDiscoverFrom(localDataDir);
                            if (!saveNow())
                            {
                                wxMessageBox(
                                    "The imported paths could not be written to config.ini.",
                                    "Spellcross original CD", wxOK | wxICON_ERROR);
                                return false;
                            }

                            wxString importMessage("Imported ");
                            const std::string importedCount = std::to_string(extracted.size());
                            importMessage += wxString::FromUTF8(importedCount.c_str());
                            importMessage += " installed game-data files from INSTALL.DTA.\n\n";
                            importMessage += "Local data folder:\n";
                            importMessage += wxString(localDataDir.wstring());

                            if (!IsValidSourceFile(configuredPath(reqs[6]), reqs[6].filename))
                            {
                                importMessage +=
                                    "\n\nINFO.FS was not present in the selected CD DATA folder. "
                                    "You will be asked for it separately.";
                            }
                            wxMessageBox(importMessage, "Spellcross original CD", wxOK | wxICON_INFORMATION);
                        }
                    }
                }
            }
        }

        for (const auto& req : reqs)
        {
            fs::path path = configuredPath(req);
            if (IsValidSourceFile(path, req.filename))
                continue;

            const bool skipped = req.optional && req.skip_key && ini.GetBoolValue("FILES", req.skip_key, false);
            if (skipped)
                continue;

            for (;;)
            {
                const wxString fileName = wxString::FromUTF8(req.filename);
                const wxString label = wxString::FromUTF8(req.label);
                wxString message;
                if (req.optional)
                {
                    message = wxString("Optional Spellcross data file ") + fileName +
                        " is not configured or cannot be found.\n\nIt provides " + label +
                        ". Select the file now, or Cancel to continue without it.";
                }
                else
                {
                    message = wxString("Spellcross needs ") + fileName + " (" + label +
                        "), but the configured file is missing or invalid.\n\nSelect the original " +
                        fileName + " file.";
                }
                wxMessageBox(message, "Spellcross game data", wxOK | wxICON_INFORMATION);

                wxString initialDirectory;
                if (!path.empty())
                    initialDirectory = wxString(path.parent_path().wstring());

                wxFileDialog dlg(nullptr,
                    wxString("Locate ") + fileName,
                    initialDirectory,
                    fileName,
                    fileName + wxString("|") + fileName + "|All files (*.*)|*.*",
                    wxFD_OPEN | wxFD_FILE_MUST_EXIST);

                if (dlg.ShowModal() != wxID_OK)
                {
                    if (req.optional)
                    {
                        ini.SetValue("FILES", req.key, "");
                        ini.SetBoolValue("FILES", req.skip_key, true, nullptr, true);
                        if (!saveNow())
                            return false;
                        break;
                    }

                    const int retry = wxMessageBox(
                        wxString::FromUTF8(req.filename) + wxString(" is required to run Spellcross.\n\nTry again?"),
                        "Required game data", wxYES_NO | wxICON_WARNING);
                    if (retry == wxYES)
                        continue;
                    return false;
                }

                const fs::path selected = dlg.GetPath().ToStdWstring();
                if (!IsValidSourceFile(selected, req.filename))
                {
                    wxMessageBox(wxString("Please select the actual ") + wxString::FromUTF8(req.filename) + " file.",
                        "Wrong file", wxOK | wxICON_WARNING);
                    continue;
                }

                storeSource(req, selected);
                if (!saveNow())
                {
                    wxMessageBox("The selected path could not be written to config.ini.",
                        "Startup configuration", wxOK | wxICON_ERROR);
                    return false;
                }

                // One selection usually identifies the whole original DATA folder.
                // Fill any sibling archives automatically, then only ask for what is
                // genuinely still missing.
                autoDiscoverFrom(selected.parent_path());
                saveNow();
                break;
            }
        }

        // Maintain the historical directory keys for code paths that still search
        // dynamically for videos or optional assets. These are derived, not required.
        const fs::path commonPath = configuredPath(reqs[0]);
        const fs::path infoPath = configuredPath(reqs[6]);
        if (!commonPath.empty())
            ini.SetValue("SPELCROS", "spell_path", ConfigPathValue(commonPath.parent_path(), config_path, runtime_root).c_str());
        if (!infoPath.empty())
            ini.SetValue("SPELCROS", "spellcd_path", ConfigPathValue(infoPath.parent_path(), config_path, runtime_root).c_str());

        return saveNow();
    }

} // namespace

static std::string ReadStrategicSaveJsonString(const std::filesystem::path& path, const char* key)
{
    if (!key || !*key)
        return {};

    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = data.find(needle);
    if (pos == std::string::npos)
        return {};
    pos = data.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return {};
    ++pos;
    while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos])))
        ++pos;
    if (pos >= data.size() || data[pos] != '"')
        return {};
    ++pos;

    std::string out;
    bool esc = false;
    for (; pos < data.size(); ++pos)
    {
        const char c = data[pos];
        if (esc)
        {
            switch (c)
            {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            default: out.push_back(c); break;
            }
            esc = false;
        }
        else if (c == '\\')
        {
            esc = true;
        }
        else if (c == '"')
        {
            break;
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

static std::filesystem::path ResolveStrategicLevelDefForSave(
    const std::filesystem::path& savePath, const SpellData* spellData)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const std::string savedDef = ReadStrategicSaveJsonString(savePath, "level_def");
    if (!savedDef.empty())
    {
        const fs::path exact(savedDef);
        if (fs::exists(exact, ec) && fs::is_regular_file(exact, ec))
            return exact;
        ec.clear();

        const std::string filename = exact.filename().string();
        if (!filename.empty())
        {
            const fs::path root = spellData ? fs::path(spellData->spell_data_root) : fs::path();
            const fs::path found = FindSpellDataFile(root, filename);
            if (!found.empty())
                return found;
        }
    }

    // Current strategic slots live in save/strategic/level_XX/slot_YY.json.
    // Older saves may not carry level_def, so the stable directory name itself
    // is enough to recover LEVEL_XX.DEF from the configured Spellcross data.
    std::string levelKey = to_lower(savePath.parent_path().filename().string());
    if (levelKey.rfind("level_", 0) == 0 || levelKey.rfind("level-", 0) == 0)
    {
        std::replace(levelKey.begin(), levelKey.end(), '-', '_');
        std::string candidate = levelKey + ".def";
        const fs::path root = spellData ? fs::path(spellData->spell_data_root) : fs::path();
        const fs::path found = FindSpellDataFile(root, candidate);
        if (!found.empty())
            return found;
    }

    // Compatibility with the old development layout where JSON and LEVEL DEF
    // were kept next to each other.
    const fs::path dir = savePath.parent_path();
    if (!dir.empty() && fs::exists(dir, ec) && fs::is_directory(dir, ec))
    {
        std::vector<fs::path> defs;
        for (const auto& de : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) { ec.clear(); break; }
            if (!de.is_regular_file(ec) || ec) { ec.clear(); continue; }
            if (to_lower(de.path().extension().string()) == ".def")
                defs.push_back(de.path());
        }
        if (!defs.empty())
        {
            std::sort(defs.begin(), defs.end());
            auto it = std::find_if(defs.begin(), defs.end(), [](const fs::path& pp)
            {
                return to_lower(pp.filename().string()).find("level") != std::string::npos;
            });
            return it != defs.end() ? *it : defs.front();
        }
    }

    return {};
}

static bool OpenStrategicSaveFromPath(MainFrame* main, const std::filesystem::path& savePath)
{
    if (!main)
        return false;

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(savePath, ec) || !fs::is_regular_file(savePath, ec))
    {
        wxMessageBox("The selected strategic save does not exist.", "Load game",
            wxOK | wxICON_ERROR, main);
        return false;
    }

    const bool originalBigMap =
        to_lower(savePath.extension().string()) == ".sav";

    fs::path defPath;
    if (originalBigMap)
    {
        int levelNumber = -1;
        std::string importError;
        if (!StrategicLevelFrame::PeekOriginalBigMapLevel(savePath, levelNumber, &importError))
        {
            wxMessageBox(
                "The selected file is not a valid original BIG_MAP.SAV.\n\n" +
                wxString::FromUTF8(importError),
                "Load original strategic game", wxOK | wxICON_ERROR, main);
            return false;
        }

        char levelDefName[32] = {};
        std::snprintf(levelDefName, sizeof(levelDefName), "LEVEL_%02d.DEF", levelNumber);
        const fs::path root = main->spell_data
            ? fs::path(main->spell_data->spell_data_root) : fs::path();
        defPath = FindSpellDataFile(root, levelDefName);
        if (defPath.empty())
        {
            wxMessageBox(
                wxString::Format(
                    "BIG_MAP.SAV is from strategic level %d, but %s could not be located in the configured Spellcross data.",
                    levelNumber, wxString::FromUTF8(levelDefName).c_str()),
                "Load original strategic game", wxOK | wxICON_ERROR, main);
            return false;
        }
    }
    else
    {
        defPath = ResolveStrategicLevelDefForSave(savePath, main->spell_data);
        if (defPath.empty())
        {
            wxMessageBox(
                "The strategic save was found, but its LEVEL_XX.DEF could not be located.\n\n"
                "The loader checked the level_def stored in the save, the configured Spellcross data, "
                "the save/strategic/level_XX directory name and the save folder itself.",
                "Load strategic game", wxOK | wxICON_ERROR, main);
            return false;
        }
    }

    LevelData lvl;
    std::string err;
    LevelLoader loader;
    if (!loader.LoadLevelDef(defPath.string(), lvl, &err))
    {
        wxMessageBox("Failed to load strategic LEVEL DEF:\n" + err,
            "Load strategic game", wxOK | wxICON_ERROR, main);
        return false;
    }

    // skipAutosave=true is essential: the explicitly selected save is the
    // authoritative state. Do not let the constructor silently substitute the
    // level autosave before we apply it.
    auto* win = new StrategicLevelFrame(main, lvl, /*skipAutosave=*/true);
    std::string importWarning;
    const bool loaded = originalBigMap
        ? win->LoadOriginalBigMapSaveFromPath(savePath, &importWarning)
        : win->LoadStrategicStateFromPath(savePath);
    if (!loaded)
    {
        win->Destroy();
        wxMessageBox(
            originalBigMap
                ? "The selected BIG_MAP.SAV could not be imported for the resolved LEVEL DEF."
                : "The selected file is not a valid strategic save for the resolved LEVEL DEF.",
            "Load strategic game", wxOK | wxICON_ERROR, main);
        return false;
    }

    win->TryLoadBackground();
    win->CheckTimeouts();
    win->RefreshUI();
    win->SetOriginalStrategicUi(true);

    StrategicLevelFrame* old = main->m_strategicLevel;
    main->m_strategicLevel = win;
    if (old && old != win)
        old->Destroy();

    main->HideTacticalWindow();
    win->Show();
    win->Raise();

    if (originalBigMap && !importWarning.empty())
    {
        wxMessageBox(wxString::FromUTF8(importWarning),
            "Original save imported", wxOK | wxICON_INFORMATION, win);
    }
    return true;
}


static wxString BuildSpellcrossWindowTitle(SpellMap* map);

bool MainFrame::LoadMapFromDefPath(const std::wstring& def_path, const std::vector<LevelData::PlayerUnitAdd>& player_units)
{
    if (!spell_map || !spell_data)
        return false;

    wstring path = def_path;
    if (spell_map->Load(path, spell_data))
    {
        wxMessageBox(string_format("Loading Spellcross map file failed with error:\n%s", spell_map->GetLastError().c_str()), "Error", wxOK | wxICON_ERROR, this);
        return false;
    }

    SetTitle(BuildSpellcrossWindowTitle(spell_map));
    spell_map->SetGamma(1.30);

    wxCommandEvent dummy;
    OnViewLayer(dummy);

    LoadToolsetRibbon();
    Refresh();

    if (player_units.empty())
        return true;

    if (spell_map->start.empty())
    {
        wxMessageBox("No start positions found (layer 6).", "Launch", wxOK | wxICON_WARNING, this);
        return true;
    }

    size_t start_idx = 0;
    MapUnit* first_deployed_player_unit = nullptr;
    for (const auto& entry : player_units)
    {
        int count = std::max(0, entry.count);
        for (int i = 0; i < count; ++i)
        {
            SpellUnitRec* unit_rec = spell_data->units ? spell_data->units->GetUnit(entry.unit_id) : nullptr;
            if (!unit_rec)
                continue;

            MapUnit* unit = new MapUnit(spell_map);
            unit->unit = unit_rec;
            unit->coor = spell_map->start[start_idx % spell_map->start.size()];
            unit->spec_type = MapUnitType::NormalUnit;
            unit->behave = MapUnitType::NormalUnit;
            unit->is_enemy = 0;
            unit->experience = std::max(0, entry.experience);
            unit->experience_level = std::clamp(entry.experience_level, 1, 12);
            unit->experience_init = unit->experience_level;

            // Preserve strategic hierarchy identity in the tactical battle.
            // The original HUD shrinks the unit status plate for formation
            // members, prints the one-digit formation number, marks the unit
            // carrying a commander, and shows WM_FORM0/1/2 in unit info.
            unit->strategic_uid = entry.strategic_uid;
            unit->formation_id = entry.formation_id;
            unit->formation_commander_mask = entry.formation_commander_mask;
            unit->commander_id = entry.formation_level > 0 ? entry.formation_id : 0;
            unit->is_commander = entry.formation_commander_mask != 0 ? 1 : (entry.carries_commander ? 1 : 0);
            unit->formation_level = entry.formation_level;
            unit->formation_attack_bonus = entry.formation_attack_bonus;
            unit->formation_defence_bonus = entry.formation_defence_bonus;
            unit->upgrade_move_bonus = entry.upgrade_move_bonus;
            unit->upgrade_defence_bonus = entry.upgrade_defence_bonus;
            unit->upgrade_attack_bonus = entry.upgrade_attack_bonus;
            unit->upgrade_attack_count_bonus = entry.upgrade_attack_count_bonus;
            unit->upgrade_range_bonus = entry.upgrade_range_bonus;
            unit->ResetAP();
            // entry.health is percentage (0-100), convert to actual man count based on unit_rec->cnt
            if (entry.health > 0 && entry.health <= 100)
                unit->man = std::max(1, (unit_rec->cnt * entry.health + 50) / 100);
            else
                unit->man = unit_rec->cnt;
            unit->wounded = 0;

            if (spell_map->PlaceUnit(unit))
            {
                delete unit;
                continue;
            }

            spell_map->AssignUnitID(unit);
            if (!first_deployed_player_unit)
                first_deployed_player_unit = unit;
            start_idx++;
        }
    }

    spell_map->SortUnits();
    spell_map->RecalculateTacticalFormations();
    spell_map->InvalidateUnitsView();

    // A strategic attack should open on the deployed Alliance force, not on
    // the map origin or an enemy that happened to be first in the DEF unit list.
    if (first_deployed_player_unit)
        spell_map->SelectUnit(first_deployed_player_unit, true);

    return true;
}


bool MainFrame::LoadGeneratedStrategicBattleFromDtaPath(const std::wstring& dta_path,
    const std::vector<LevelData::PlayerUnitAdd>& player_units,
    const std::vector<int>& enemy_unit_ids)
{
    if (!spell_map || !spell_data || dta_path.empty())
        return false;

    // Starting/recaptured territories in the original campaign intentionally
    // have only Mxx_yy.DTA.  Their defence/recapture battle was generated by
    // the DOS strategic layer, so looking for Mxx_yy.DEF is wrong.  Load the
    // base battlefield and reconstruct the small amount of runtime mission
    // data the tactical engine needs: deployment squares, occupiers and the
    // DestroyAllUnits objective.
    std::wstring path = dta_path;
    if (spell_map->Load(path, spell_data))
    {
        wxMessageBox(string_format("Loading Spellcross map DTA file failed with error:\n%s",
            spell_map->GetLastError().c_str()), "Launch", wxOK | wxICON_ERROR, this);
        return false;
    }

    if (spell_map->x_size <= 0 || spell_map->y_size <= 0)
        return false;

    SetTitle(BuildSpellcrossWindowTitle(spell_map));
    spell_map->SetGamma(1.30);

    wxCommandEvent dummy;
    OnViewLayer(dummy);
    LoadToolsetRibbon();
    Refresh();

    size_t playerCompanyCount = 0;
    for (const auto& entry : player_units)
        playerCompanyCount += static_cast<size_t>(std::max(0, entry.count));

    auto tile_ok = [&](int x, int y, bool strict) -> bool
    {
        if (x < 0 || y < 0 || x >= spell_map->x_size || y >= spell_map->y_size)
            return false;
        const uint8_t flags = spell_map->tiles[static_cast<size_t>(y * spell_map->x_size + x)].flags;
        if (strict)
            return flags == 0x00;
        // 0x60/0x90 are the two special terrain cases accepted by some land
        // movement classes. PlaceUnit() still performs the authoritative check
        // for the concrete unit and searches nearby when necessary.
        return flags == 0x00 || flags == 0x60 || flags == 0x90;
    };

    auto collect_band = [&](bool playerSide, bool strict) -> std::vector<MapXY>
    {
        std::vector<MapXY> out;
        const int yMargin = std::max(1, spell_map->y_size / 20);
        const int xMargin = std::max(1, spell_map->x_size / 12);
        const int band = std::max(4, spell_map->y_size / 4);
        const int yBegin = playerSide
            ? std::max(yMargin, spell_map->y_size - yMargin - band)
            : yMargin;
        const int yEnd = playerSide
            ? std::max(yBegin + 1, spell_map->y_size - yMargin)
            : std::min(spell_map->y_size - yMargin, yMargin + band);

        for (int y = yBegin; y < yEnd; ++y)
        {
            for (int x = xMargin; x < spell_map->x_size - xMargin; ++x)
            {
                if (tile_ok(x, y, strict))
                    out.emplace_back(x, y);
            }
        }
        return out;
    };

    auto spread_pick = [](const std::vector<MapXY>& candidates, size_t wanted) -> std::vector<MapXY>
    {
        std::vector<MapXY> out;
        if (candidates.empty() || wanted == 0)
            return out;
        wanted = std::min(wanted, candidates.size());
        out.reserve(wanted);
        for (size_t i = 0; i < wanted; ++i)
        {
            const size_t idx = ((i + 1) * candidates.size()) / (wanted + 1);
            out.push_back(candidates[std::min(idx, candidates.size() - 1)]);
        }
        return out;
    };

    std::vector<MapXY> playerCandidates = collect_band(true, true);
    if (playerCandidates.empty())
        playerCandidates = collect_band(true, false);
    std::vector<MapXY> enemyCandidates = collect_band(false, true);
    if (enemyCandidates.empty())
        enemyCandidates = collect_band(false, false);

    if (playerCandidates.empty() || enemyCandidates.empty())
    {
        wxMessageBox("The DTA battlefield has no usable deployment area.",
            "Launch", wxOK | wxICON_ERROR, this);
        return false;
    }

    const size_t wantedStarts = std::max<size_t>(12, playerCompanyCount);
    spell_map->start = spread_pick(playerCandidates, wantedStarts);
    const std::vector<MapXY> enemySeeds = spread_pick(enemyCandidates,
        std::max<size_t>(1, enemy_unit_ids.size()));

    // Recreate the random/counter-attack mission objective used by the DOS
    // generated battle: destroy every Other Side company on the map.
    if (spell_map->events)
    {
        auto* objective = new SpellMapEventRec(spell_map);
        objective->SetType(SpellMapEventRec::EvtTypes::EVT_DESTROY_ALL);
        objective->is_objective = true;
        objective->probability = 100;
        objective->label = L"Destroy all enemies";
        spell_map->events->AddEvent(objective);
    }

    // Place Other Side occupiers first, well away from the Alliance deployment
    // band. PlaceUnit() is deliberately used instead of trusting a raw tile: it
    // resolves local terrain restrictions for walkers, hover units and air units.
    size_t enemySeedIndex = 0;
    for (int unitId : enemy_unit_ids)
    {
        SpellUnitRec* unitRec = spell_data->units ? spell_data->units->GetUnit(unitId) : nullptr;
        if (!unitRec)
            continue;

        MapUnit* unit = new MapUnit(spell_map);
        unit->unit = unitRec;
        unit->coor = enemySeeds[enemySeedIndex % enemySeeds.size()];
        unit->spec_type = MapUnitType::EnemyUnit;
        unit->behave = MapUnitType::NormalUnit;
        unit->is_enemy = 1;
        unit->is_active = 1;
        unit->InitExperience(0);
        unit->man = std::max(1, unitRec->cnt);
        unit->wounded = 0;
        unit->morale = 100.0;
        unit->ResetAP();

        if (spell_map->PlaceUnit(unit))
        {
            delete unit;
            continue;
        }
        spell_map->AssignUnitID(unit);
        ++enemySeedIndex;
    }

    if (enemySeedIndex == 0)
    {
        wxMessageBox("No valid Other Side units could be created for this generated battle.",
            "Launch", wxOK | wxICON_ERROR, this);
        return false;
    }

    // Deploy the selected permanent Alliance companies exactly the same way as
    // normal DEF missions, including strategic UID / formation / upgrade data.
    size_t start_idx = 0;
    MapUnit* first_deployed_player_unit = nullptr;
    for (const auto& entry : player_units)
    {
        const int count = std::max(0, entry.count);
        for (int i = 0; i < count; ++i)
        {
            SpellUnitRec* unit_rec = spell_data->units ? spell_data->units->GetUnit(entry.unit_id) : nullptr;
            if (!unit_rec)
                continue;

            MapUnit* unit = new MapUnit(spell_map);
            unit->unit = unit_rec;
            unit->coor = spell_map->start[start_idx % spell_map->start.size()];
            unit->spec_type = MapUnitType::NormalUnit;
            unit->behave = MapUnitType::NormalUnit;
            unit->is_enemy = 0;
            unit->experience = std::max(0, entry.experience);
            unit->experience_level = std::clamp(entry.experience_level, 1, 12);
            unit->experience_init = unit->experience_level;
            unit->strategic_uid = entry.strategic_uid;
            unit->formation_id = entry.formation_id;
            unit->formation_commander_mask = entry.formation_commander_mask;
            unit->commander_id = entry.formation_level > 0 ? entry.formation_id : 0;
            unit->is_commander = entry.formation_commander_mask != 0 ? 1 : (entry.carries_commander ? 1 : 0);
            unit->formation_level = entry.formation_level;
            unit->formation_attack_bonus = entry.formation_attack_bonus;
            unit->formation_defence_bonus = entry.formation_defence_bonus;
            unit->upgrade_move_bonus = entry.upgrade_move_bonus;
            unit->upgrade_defence_bonus = entry.upgrade_defence_bonus;
            unit->upgrade_attack_bonus = entry.upgrade_attack_bonus;
            unit->upgrade_attack_count_bonus = entry.upgrade_attack_count_bonus;
            unit->upgrade_range_bonus = entry.upgrade_range_bonus;
            unit->ResetAP();
            if (entry.health > 0 && entry.health <= 100)
                unit->man = std::max(1, (unit_rec->cnt * entry.health + 50) / 100);
            else
                unit->man = unit_rec->cnt;
            unit->wounded = 0;

            if (spell_map->PlaceUnit(unit))
            {
                delete unit;
                continue;
            }

            spell_map->AssignUnitID(unit);
            if (!first_deployed_player_unit)
                first_deployed_player_unit = unit;
            ++start_idx;
        }
    }

    if (spell_map->events)
        spell_map->events->ResetEvents();
    spell_map->SortUnits();
    spell_map->RecalculateTacticalFormations();
    spell_map->InvalidateUnitsView();

    if (first_deployed_player_unit)
        spell_map->SelectUnit(first_deployed_player_unit, true);

    return first_deployed_player_unit != nullptr;
}

void MainFrame::SetGameModeUI(bool enable_game_mode)
{
    if(!spell_map || !spell_map->IsLoaded())
        return;

    // Keep menu state in sync (if menu exists)
    if(GetMenuBar())
    {
        if(auto* item = GetMenuBar()->FindItem(ID_mmGameMode))
            item->Check(enable_game_mode);
    }

    spell_map->SetGameMode(enable_game_mode);
    if(canvas)
        canvas->Refresh();

    if(enable_game_mode)
    {
        // switch to game mode. The ribbon is an editor-only control; hiding
        // just its panels leaves an empty blue ribbon strip below the menu.
        // Hide the whole control so the tactical map starts directly under
        // the menu bar.
        if(ribbonBar)
        {
            ribbonBar->Hide();
            if (sizer) sizer->Layout();
        }

        if(menuView)
        {
            if(menuView->FindItem(ID_ViewSoundLoops)) menuView->FindItem(ID_ViewSoundLoops)->Check(false);
            if(menuView->FindItem(ID_ViewSounds))     menuView->FindItem(ID_ViewSounds)->Check(false);
            if(menuView->FindItem(ID_ViewEvents))     menuView->FindItem(ID_ViewEvents)->Check(false);
        }

        wxCommandEvent dummy;
        OnViewLayer(dummy);

        // reset map runtime state
        if(spell_map->saves)
            spell_map->saves->Clear();
        if(spell_map->events)
            spell_map->events->ResetEvents();
        if(spell_map->saves)
            spell_map->saves->SaveInitial();

        // exec initial events
        spell_map->MissionStartEvent();

        // reset units view/attack ranges
        if(spell_map->unit_view)
        {
            spell_map->unit_view->ClearEvents();
            spell_map->unit_view->ClearUnitsView(SpellMap::ViewRange::ClearMode::RESET);
            spell_map->unit_view->AddUnitsView();
        }
    }
    else
    {
        // switch to editor mode
        if(spell_map->saves)
            spell_map->saves->LoadInitial();
        spell_map->ResetUnitEvents();

        // Restore status bar when leaving game mode
        if (GetStatusBar() && !GetStatusBar()->IsShown())
            GetStatusBar()->Show();

        // Recreate the editor ribbon when returning to editor mode.
        LoadToolsetRibbon();
    }

    UpdateMenuForGameMode();
}

void MainFrame::UpdateMenuForGameMode()
{
    auto* bar = GetMenuBar();
    if (!bar)
        return;

    // Menu is restricted when editor is locked (default state).
    // Console command GAMEMODEOFF unlocks it.
    bool restricted = !m_editor_unlocked;

    // File menu = index 0: disable entirely
    bar->EnableTop(0, !restricted);

    // Game menu = index 1: disable individual items except Save/Load game state
    bar->Enable(ID_mmGameMode, !restricted);
    bar->Enable(ID_mmResetViewMap, !restricted);
    bar->Enable(ID_mmUnitViewMode, !restricted);
    // ID_mmSaveGameState and ID_mmLoadGameState stay enabled always

    // Edit menu = index 2: disable entirely
    bar->EnableTop(2, !restricted);

    // View menu = index 3: disable entirely
    bar->EnableTop(3, !restricted);

    // Tools menu = index 4: disable entirely
    bar->EnableTop(4, !restricted);

    // Options menu = index 5: always enabled (Audio/Screen accessible in game mode)

    // Help menu = index 6: always enabled
}


void MainFrame::OnOpenLevelDef(wxCommandEvent& ev)
{
    wstring temp_dir = (std::filesystem::current_path() / L"temp").wstring();
    wxFileDialog dlg(
        this,
        "Open Level DEF",
        temp_dir,
        "",
        "Level DEF (*.def)|*.def|All files|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );

    if (dlg.ShowModal() != wxID_OK)
        return;

    const std::string path = dlg.GetPath().ToStdString();

    LevelData lvl;
    std::string err;
    LevelLoader loader;
    if (!loader.LoadLevelDef(path, lvl, &err))
    {
        wxMessageBox("Failed to load level DEF:\n" + err, "Error", wxOK | wxICON_ERROR, this);
        return;
    }

    // otevøi strategické UI (window si žije samo, wxWidgets ho znièí po zavøení)
    auto* win = new StrategicLevelFrame(this, lvl);
    m_strategicLevel = win;  // Store reference for mission results
    HideTacticalWindow();
    win->Show();
    win->Raise();
}


wxIMPLEMENT_APP(MyApp);
bool MyApp::OnInit()
{
    // for saving PNG file (among other stuff)
    wxInitAllImageHandlers();

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path startup_dir = fs::current_path(ec);
    ec.clear();
    const fs::path executable_dir = fs::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring()).parent_path();
    const fs::path runtime_root = FindRuntimeRoot(executable_dir, startup_dir);

    // All legacy current_path() lookups now have one deterministic base.
    fs::current_path(runtime_root, ec);
    if (ec)
    {
        wxMessageBox(string_format("Cannot use the runtime data folder:\n%ls\n\n%s",
            runtime_root.wstring().c_str(), ec.message().c_str()), "Startup error", wxICON_ERROR);
        return false;
    }

    config_path = FindConfigPath(executable_dir, runtime_root, startup_dir).wstring();

    // Empty/missing config.ini is valid for a fresh release. Load it when it
    // contains data, otherwise start with an empty in-memory configuration and
    // let the source wizard populate it.
    ini.SetUnicode();
    ini.Reset();
    {
        std::error_code cfgEc;
        const fs::path cfg(config_path);
        if (fs::is_regular_file(cfg, cfgEc) && !cfgEc && fs::file_size(cfg, cfgEc) > 0 && !cfgEc)
        {
            if (ini.LoadFile(config_path.c_str()) != SI_OK)
            {
                wxMessageBox(string_format("Loading configuration failed:\n%ls", config_path.c_str()),
                    "Startup error", wxICON_ERROR);
                return false;
            }
        }
    }

    // Resolve/repair the external Spellcross data sources on the UI thread
    // before FormLoader starts. Required archives are requested individually;
    // optional audio sources can be skipped and that choice is remembered.
    if (!EnsureSpellcrossSourceConfiguration(ini, fs::path(config_path), runtime_root, executable_dir))
        return false;

    // --- try load Spellcross data
    FormLoader* form_loader = new FormLoader(NULL, spell_data, config_path);
    bool data_ok = form_loader->ShowModal();
    delete form_loader;
    if(!data_ok)
    {
        wxMessageBox("Loading Spellcross data failed!\nPossibly incorrect game paths in ''config.ini''?","Error",wxICON_ERROR);
        return(false);
    }

    // --- restore the last tactical map only when the saved path is still valid.
    // A fresh/portable config intentionally starts without one and should land
    // cleanly in the main menu instead of showing a bogus map-load error.
    wstring map_path = char2wstring(ini.GetValue("STATE","last_map",""));
    spell_map = new SpellMap();
    if(!map_path.empty())
    {
        std::error_code mapEc;
        if(std::filesystem::is_regular_file(std::filesystem::path(map_path), mapEc) && !mapEc)
        {
            if(spell_map->Load(map_path,spell_data))
                wxMessageBox(string_format("Loading Spellcross map file failed with error:\n%s",spell_map->GetLastError().c_str()),"Error",wxICON_ERROR);
        }
        else
        {
            ini.SetValue("STATE", "last_map", "");
        }
    }
    spell_map->SetGamma(1.3);

    // sound effects/midi volumes. Both subsystems remain valid in silent mode.
    if (spell_data->sounds && spell_data->sounds->channels)
        spell_data->sounds->channels->SetVolume(0.01*ini.GetLongValue("STATE","sound_volume",50));
    if (spell_data->midi)
        spell_data->midi->SetVolume(0.01*ini.GetLongValue("STATE","music_volume",100));

    // play default MIDI (a no-op when MUSIC.FS was intentionally skipped)
    string midi_name = ini.GetValue("STATE","default_midi","");
    if (spell_data->midi)
        spell_data->midi->Play(midi_name);

    // default window size
    int win_x_size = ini.GetLongValue("STATE","win_x_size",1600);
    int win_y_size = ini.GetLongValue("STATE","win_y_size",1000);
    bool win_maximize = ini.GetBoolValue("STATE","win_maximize",false);

    // limit to screen size
    int disp_x_size;
    int disp_y_size;
    wxDisplaySize(&disp_x_size,&disp_y_size);
    win_x_size = min(win_x_size,disp_x_size);
    win_y_size = min(win_y_size,disp_y_size);
                
    // --- run main form    
    // main window frame
    MainFrame* frame = new MainFrame(spell_map, spell_data);
    frame->SetSize(win_x_size,win_y_size);
    if(win_maximize)
        frame->Maximize();

    // set icon
    wxIcon appIcon;
    appIcon.LoadFile("IDI_ICON2",wxBITMAP_TYPE_ICO_RESOURCE);
    if(appIcon.IsOk())
        frame->SetIcon(appIcon);

    frame->Center();
    // Start in the real main menu, not with the tactical map visible behind it.
    // MainFrame stays alive as the application/campaign controller but is only
    // shown when a tactical mission is actually entered.
    frame->Show(false);
    return(true);
}
int MyApp::OnExit()
{
    // OnInit can now legitimately stop during the first-run source wizard, so
    // keep shutdown safe even when game objects have not been created yet.
    if (spell_map)
        ini.SetValue("STATE","last_map",wstring2string(spell_map->GetTopPath()).c_str());

    // store sound/midi volumes
    if (spell_data && spell_data->sounds && spell_data->sounds->channels)
        ini.SetLongValue("STATE", "sound_volume", 100.0*spell_data->sounds->channels->GetVolume());
    if (spell_data && spell_data->midi)
        ini.SetLongValue("STATE", "music_volume", 100.0*spell_data->midi->GetVolume());

    // save INI
    if (!config_path.empty())
        ini.SaveFile(config_path.c_str());

    // loose map/data (both may still be null after an aborted first run)
    delete spell_map;
    spell_map = nullptr;
    delete spell_data;
    spell_data = nullptr;

    return(0);
}

// Main tactical/game window title. Keep the application identity clean and,
// when a map is loaded, show the current mission/map name instead of the old
// editor-era title.
static wxString BuildSpellcrossWindowTitle(SpellMap* map)
{
    wxString title = "Spellcross";
    if (!map || !map->IsLoaded())
        return title;

    const std::filesystem::path topPath = map->GetTopPath();
    if (topPath.empty())
        return title;

    const std::wstring stem = topPath.stem().wstring();
    if (!stem.empty())
        title += " - " + wxString(stem.c_str());
    return title;
}

// Main panel init
MainFrame::MainFrame(SpellMap* map, SpellData* spelldata):wxFrame(NULL, wxID_ANY, "Spellcross", wxDefaultPosition, wxSize(1600,1000))
{
    spellcross_app::ApplyWindowIcon(this);
    // store local reference to initial map and data
    spell_map = map;
    spell_data = spelldata;
    SetTitle(BuildSpellcrossWindowTitle(spell_map));

    // subforms
    form_gamma = NULL;
    form_sprites = NULL;
    form_anms = NULL;
    form_objects = NULL;
    form_pal = NULL;
    form_gres = NULL;
    form_units = NULL;
    form_events = NULL;
    form_videos = NULL;
    form_unit_opts = NULL;
    form_message = NULL;
    form_video_box = NULL;
    form_map_options = NULL;
    form_midi = NULL;
    form_minimap = NULL;
    form_units_list = NULL;
    form_sounds = NULL;
    form_mmenu = NULL;
        
    // File menu
    wxMenu* menuFile = new wxMenu;
    menuFile->Append(ID_OpenMap, "&Open Map\tCtrl-O", "Open new Spellcross map file.");
    menuFile->Append(ID_SaveMap,"&Save Map\tCtrl-S","Save Spellcross map file(s).");
    menuFile->Append(ID_SaveDTA,"&Save DTA map file","Save Spellcross map DTA file.");
    menuFile->Append(ID_SaveDEF,"&Save DEF map file","Save Spellcross map DEF file.");
    menuFile->Append(ID_NewMap,"&Ceate new Map\tCtrl-N","Create new map.");
    menuFile->Append(ID_MainMenu,"Main &menu\tCtrl-M","Open main game menu.");
    menuFile->AppendSeparator();
    menuFile->Append(ID_OpenLevelDef, "Open &Level DEF...\tCtrl-L", "Open strategic level definition (.DEF).");

    // Strategic campaign loading (opens Strategic Level from saved state)
    wxMenuItem* miLoadStrategic = menuFile->Append(wxID_ANY, "Load &strategic game...	Ctrl+Alt+L", "Load a strategic campaign JSON save (autosave or slot).");
    menuFile->Append(wxID_EXIT);

    // Game menu
    wxMenu* menuGame = new wxMenu;
    menuGame->Append(ID_mmGameMode,"Game mode\tCtrl-G","Switch game mode",wxITEM_CHECK);
    menuGame->AppendSeparator();
    menuGame->Append(ID_mmSaveGameState, "Save game state...\tCtrl+Shift+S", "Save current game snapshot to file.");
	menuGame->Append(ID_mmLoadGameState, "Load game state...\tCtrl+Shift+L", "Load game snapshot from file.");
    menuGame->AppendSeparator();
    menuGame->Append(ID_mmResetViewMap,"Reset view map","");
    menuGame->Append(ID_mmUnitViewMode,"View unit move/attack range\tSpace","");
    
    
    // View menu
    menuView = new wxMenu;
    menuView->Append(ID_ViewTer,"Layer 1: Terrain\tF1","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewTer)->Check(true);
    menuView->Append(ID_ViewObj,"Layer 2: Objects\tF2","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewObj)->Check(true);
    menuView->Append(ID_ViewAnm,"Layer 3: Tile animations\tF3","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewAnm)->Check(true);
    menuView->Append(ID_ViewPnm,"Layer 4: Sprite animations\tF4","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewPnm)->Check(true);
    menuView->Append(ID_ViewUnt,"Layer 5: Units\tF5","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewUnt)->Check(true);
    menuView->Append(ID_ViewStTa,"Layer 6: Start/Target\tF6","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewStTa)->Check(true);
    menuView->Append(ID_ViewSoundLoops,"Layer 7: Sound loops\tF7","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewSoundLoops)->Check(false);
    menuView->Append(ID_ViewSounds,"Layer 8: Sounds\tF8","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewSounds)->Check(false);
    menuView->Append(ID_ViewEvents,"Show events\tF9","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewEvents)->Check(false);    
    menuView->Append(ID_HighlighObj,"Highlight objects\tF10","",wxITEM_CHECK);
    menuView->FindItem(ID_HighlighObj)->Check(false);
    menuView->Append(ID_ViewHUD,"Show mission HUD panel\tCtrl+H","",wxITEM_CHECK);
    menuView->FindItem(ID_ViewHUD)->Check(spell_map->GetHUDstate());
    menuView->Append(ID_UnitViewDbg,"Enable unit view debug mode\tCtrl+D","",wxITEM_CHECK);    
    menuView->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuView->Append(ID_SetGamma,"Set gamma","",wxITEM_NORMAL);    

    // Layer selection submenu for copy/paste editor
    wxMenu* menuLayer = new wxMenu;
    menuLayer->Append(ID_SelectLay1,"Layer 1 - Terrain\tCtrl+F1","",wxITEM_CHECK);
    menuLayer->FindItem(ID_SelectLay1)->Check(true);
    menuLayer->Append(ID_SelectLay2,"Layer 2 - Objects\tCtrl+F2","",wxITEM_CHECK);
    menuLayer->FindItem(ID_SelectLay2)->Check(true);
    menuLayer->Append(ID_SelectLayANM,"Layer 3 - ANM animations\tCtrl+F3","",wxITEM_CHECK);
    menuLayer->FindItem(ID_SelectLayANM)->Check(true);
    menuLayer->Append(ID_SelectLayPNM,"Layer 4 - PNM animations\tCtrl+F4","",wxITEM_CHECK);
    menuLayer->FindItem(ID_SelectLayPNM)->Check(true);
    // edit menu
    wxMenu* menuEdit = new wxMenu;
    menuEdit->Append(ID_EditMissionParams,"Edit mission parameters","",wxITEM_NORMAL);
    menuEdit->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuEdit->AppendSubMenu(menuLayer,"Select layer(s)","");
    menuEdit->Append(ID_SelectAll,"Select all tiles\tCtrl+A","",wxITEM_NORMAL);
    menuEdit->Append(ID_DeselectAll,"Deselect all tiles\tCtrl+Shift+A","",wxITEM_NORMAL);
    menuEdit->Append(ID_SelectDeselect,"Select/deselect tiles\tCtrl+Insert","",wxITEM_NORMAL);
    menuEdit->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuEdit->Append(ID_CopyBuf,"Copy selection to buffer\tCtrl+C","",wxITEM_NORMAL);
    menuEdit->Append(ID_CutBuf,"Cut selection to buffer\tCtrl+X","",wxITEM_NORMAL);
    menuEdit->Append(ID_PasteBuf,"Paste from buffer\tCtrl+V","",wxITEM_NORMAL);
    menuEdit->Append(ID_ClearBuf,"Clear buffer\tESC","",wxITEM_NORMAL);
    menuEdit->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuEdit->Append(ID_InvalidateSel,"Invalidate selection\tCtrl+I","",wxITEM_NORMAL);
    menuEdit->Append(ID_DeleteSel,"Delete stuff\tCtrl+Delete","",wxITEM_NORMAL);
    menuEdit->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuEdit->Append(ID_ElevUp,"Elevate terrain\tCtrl+PageUp","",wxITEM_NORMAL);
    menuEdit->Append(ID_ElevDown,"Lower terrain\tCtrl+PageDown","",wxITEM_NORMAL);
    menuEdit->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuEdit->Append(ID_CreateNewObject,"Create new object\tCtrl+Shift+O","",wxITEM_NORMAL);
    menuEdit->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuEdit->Append(ID_AddUnit,"Add unit\tCtrl+Shift+U","",wxITEM_NORMAL);


    
    // tools
    wxMenu* menuTools = new wxMenu;
    menuTools->Append(ID_ViewSprites,"Sprites viewer","",wxITEM_NORMAL);
    menuTools->Append(ID_ViewAnms,"Animations (ANM) viewer","",wxITEM_NORMAL);
    menuTools->Append(ID_ViewPnms,"Animations (PNM) viewer","",wxITEM_NORMAL);
    menuTools->Append(ID_SoundsViewer,"Sounds viewer","",wxITEM_NORMAL);
    menuTools->Append(ID_ViewPal,"Palette viewer","",wxITEM_NORMAL);
    menuTools->Append(ID_ViewGRes,"Graphics viewer","",wxITEM_NORMAL);
    menuTools->Append(ID_EditUnit,"Units viewer/editor\tCtrl+U","",wxITEM_NORMAL);
    menuTools->Append(ID_EditEvent,"Event viewer/editor\tCtrl+E","",wxITEM_NORMAL);
    menuTools->Append(ID_ViewVideo,"Video viewer","",wxITEM_NORMAL);
    menuTools->Append(ID_ViewMIDI,"MIDI player","",wxITEM_NORMAL);
    menuTools->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuTools->Append(ID_ViewObjects,"Objects editor","",wxITEM_NORMAL);
    menuTools->Append(ID_EditTileFlags,"Edit tile flags\tCtrl+F","",wxITEM_NORMAL);
    menuTools->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuTools->Append(ID_ViewMiniMap,"View mini-map","",wxITEM_NORMAL);
    menuTools->Append(ID_ViewVoxZ,"View Z-map","",wxITEM_NORMAL);
    menuTools->Append(ID_ExportVoxZ,"Export Z-map","",wxITEM_NORMAL);
    menuTools->Append(wxID_ANY,"","",wxITEM_SEPARATOR);
    menuTools->Append(ID_UpdateSprContext, "Update tile context from this map","",wxITEM_NORMAL);
    menuTools->Append(ID_UpdateSprContextMaps,"Update tile context from ALL maps","",wxITEM_NORMAL);
    menuTools->Append(ID_GenDMAobjects,"Generate DMAx_xxx objects from this map","",wxITEM_NORMAL);
    menuTools->Append(ID_GenDMAobjectsMaps,"Generate DMAx_xxx objects from ALL maps","",wxITEM_NORMAL);
    
        
    // Help menu
    wxMenu* menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);

    // Options menu (accessible in game mode too)
    wxMenu* menuOptions = new wxMenu;
    menuOptions->Append(ID_OptionsAudio, "&Audio...", "Audio volume settings");
    menuOptions->Append(ID_OptionsScreen, "&Screen...", "Brightness settings");

    // Main menu
    wxMenuBar* menuBar = new wxMenuBar;    
    menuBar->Append(menuFile, "&File");
    menuBar->Append(menuGame, "&Game");
    menuBar->Append(menuEdit, "&Edit");
    menuBar->Append(menuView, "&View");
    menuBar->Append(menuTools,"&Tools");
    menuBar->Append(menuOptions, "&Options");
    menuBar->Append(menuHelp, "&Help");
    SetMenuBar(menuBar);
    
    CreateStatusBar(8);
    const int ss_w[] = {45,45,45,60,100,100,100,-1};
    SetStatusWidths(8,ss_w);
    SetStatusText("");
      
    // tick timer
    m_timer.SetOwner(this);
    this->Connect(wxEVT_TIMER,wxTimerEventHandler(MainFrame::OnTimer),NULL,this);
    m_timer.Start(10);

    // main sizer 
    sizer = new wxBoxSizer(wxVERTICAL); 
    this->SetSizeHints(wxDefaultSize,wxDefaultSize);

    // toolset ribbon
    //ribbonBar = new wxRibbonBar(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxRIBBON_BAR_DEFAULT_STYLE);
    //ribbonBar->SetArtProvider(new wxRibbonDefaultArtProvider);
    ribbonBar = NULL;
    LoadToolsetRibbon();
    //sizer->Add(ribbonBar,0,wxALL|wxEXPAND,2);
    

    Bind(wxEVT_RIBBONBUTTONBAR_CLICKED,&MainFrame::OnToolBtnClick,this);
    Bind(wxEVT_RIBBONBAR_PAGE_CHANGED,&MainFrame::OnToolPageClick,this);

    // make and attach render canvas
    canvas = new wxPanel(this,ID_Canvas,wxDefaultPosition,wxDefaultSize,wxTAB_TRAVERSAL);
    sizer->Add(canvas,1,wxEXPAND|wxALL,1);
    canvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
    canvas->SetDoubleBuffered(true);
    

    canvas->Bind(wxEVT_CLOSE_WINDOW,&MainFrame::OnClose,this);
    canvas->Bind(wxEVT_PAINT,&MainFrame::OnPaintCanvas,this);
    canvas->Bind(wxEVT_RIGHT_DOWN,&MainFrame::OnCanvasRMouse,this);
    canvas->Bind(wxEVT_RIGHT_UP,&MainFrame::OnCanvasRMouse,this);
    
    

    canvas->Bind(wxEVT_MOTION,&MainFrame::OnCanvasMouseMove,this);
    canvas->Bind(wxEVT_LEAVE_WINDOW,&MainFrame::OnCanvasMouseLeave,this);
    canvas->Bind(wxEVT_ENTER_WINDOW,&MainFrame::OnCanvasMouseEnter,this);
    canvas->Bind(wxEVT_MOUSEWHEEL,&MainFrame::OnCanvasMouseWheel,this);
    canvas->Bind(wxEVT_KEY_DOWN,&MainFrame::OnCanvasKeyDown,this);
    canvas->Bind(wxEVT_LEFT_DOWN,&MainFrame::OnCanvasLMouseDown,this);
    //canvas->Bind(wxEVT_LEFT_DCLICK,&MainFrame::OnCanvasLMouseDown,this);
    canvas->Bind(wxEVT_THREAD,&MainFrame::OnThreadCanvas,this);
    

    this->SetSizer(sizer);    
    this->SetAutoLayout(true);
    this->Layout();

    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    
    Bind(wxEVT_MENU,&MainFrame::OnOpenMap,this,ID_OpenMap);
    Bind(wxEVT_MENU,&MainFrame::OnSaveMap,this,ID_SaveMap);
    Bind(wxEVT_MENU,&MainFrame::OnSaveDTA,this,ID_SaveDTA);
    Bind(wxEVT_MENU,&MainFrame::OnSaveDEF,this,ID_SaveDEF);
    Bind(wxEVT_MENU,&MainFrame::OnNewMap,this,ID_NewMap);
    Bind(wxEVT_MENU,&MainFrame::OnOpenMainMenu,this,ID_MainMenu);
	Bind(wxEVT_MENU, &MainFrame::OnOpenLevelDef, this, ID_OpenLevelDef);

// Load a remake strategic JSON save or an original BIG_MAP.SAV.
// JSON identifies its DEF; BIG_MAP.SAV carries the original level number.
Bind(wxEVT_MENU, [this](wxCommandEvent&)
{
    namespace fs = std::filesystem;
    fs::path startDir = fs::current_path() / "save" / "strategic";
    std::error_code ec;
    if (!fs::exists(startDir, ec))
        startDir = fs::current_path() / "temp";

    wxFileDialog dlg(
        this,
        "Load strategic game",
        wxString::FromUTF8(startDir.string()),
        "",
        "Strategic saves (*.json;*.sav)|*.json;*.sav|Remake strategic save (*.json)|*.json|Original Spellcross save (*.sav)|*.sav|All files|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );

    if (dlg.ShowModal() != wxID_OK)
        return;

    (void)OpenStrategicSaveFromPath(this, fs::path(dlg.GetPath().ToStdWstring()));
}, miLoadStrategic->GetId());

    Bind(wxEVT_MENU,&MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_MENU,&MainFrame::OnExit, this, wxID_EXIT);

    Bind(wxEVT_MENU,&MainFrame::OnSwitchGameMode,this,ID_mmGameMode);
    Bind(wxEVT_MENU,&MainFrame::OnSaveGameState,this,ID_mmSaveGameState);
	Bind(wxEVT_MENU,&MainFrame::OnLoadGameState,this,ID_mmLoadGameState);
    Bind(wxEVT_MENU,&MainFrame::OnResetUnitView,this,ID_mmResetViewMap);
    Bind(wxEVT_MENU,&MainFrame::OnSelectUnitView,this,ID_mmUnitViewMode);

    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewTer);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewObj);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewAnm);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewPnm);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewUnt);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewStTa);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewHUD);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewSounds);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewSoundLoops);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_ViewEvents);
    Bind(wxEVT_MENU,&MainFrame::OnViewLayer,this,ID_HighlighObj);

    Bind(wxEVT_MENU,&MainFrame::OnSetGamma,this,ID_SetGamma);
    Bind(wxEVT_MENU,&MainFrame::OnViewSprites,this,ID_ViewSprites);
    Bind(wxEVT_MENU,&MainFrame::OnViewAnms,this,ID_ViewAnms);
    Bind(wxEVT_MENU,&MainFrame::OnViewPnms,this,ID_ViewPnms);
    Bind(wxEVT_MENU,&MainFrame::OnViewSounds,this,ID_SoundsViewer);
    Bind(wxEVT_MENU,&MainFrame::OnViewObjects,this,ID_ViewObjects);
    Bind(wxEVT_MENU,&MainFrame::OnViewPal,this,ID_ViewPal);
    Bind(wxEVT_MENU,&MainFrame::OnViewGrRes,this,ID_ViewGRes);
    Bind(wxEVT_MENU,&MainFrame::OnEditUnit,this,ID_EditUnit);
    Bind(wxEVT_MENU,&MainFrame::OnEditEvent,this,ID_EditEvent);
    Bind(wxEVT_MENU,&MainFrame::OnViewVideo,this,ID_ViewVideo);
    Bind(wxEVT_MENU,&MainFrame::OnViewMidi,this,ID_ViewMIDI);
    Bind(wxEVT_MENU,&MainFrame::OnTileFlags,this,ID_EditTileFlags);
    Bind(wxEVT_MENU,&MainFrame::OnViewVoxZ,this,ID_ViewVoxZ);
    Bind(wxEVT_MENU,&MainFrame::OnViewVoxZ,this,ID_ExportVoxZ);
    Bind(wxEVT_MENU,&MainFrame::OnViewMiniMap,this,ID_ViewMiniMap);
    Bind(wxEVT_MENU,&MainFrame::OnUnitViewDebug,this,ID_UnitViewDbg);
    Bind(wxEVT_MENU,&MainFrame::OnUpdateTileContext,this,ID_UpdateSprContext);
    Bind(wxEVT_MENU,&MainFrame::OnUpdateTileContextMaps,this,ID_UpdateSprContextMaps);
    Bind(wxEVT_MENU,&MainFrame::OnGenDMAobjects,this,ID_GenDMAobjects);
    Bind(wxEVT_MENU,&MainFrame::OnGenDMAobjectsMaps,this,ID_GenDMAobjectsMaps);
    
    Bind(wxEVT_MENU,&MainFrame::OnEditMissionParams,this,ID_EditMissionParams);
    Bind(wxEVT_MENU,&MainFrame::OnCopyBuf,this,ID_CopyBuf);
    Bind(wxEVT_MENU,&MainFrame::OnCopyBuf,this,ID_CutBuf);
    Bind(wxEVT_MENU,&MainFrame::OnPasteBuf,this,ID_PasteBuf);
    Bind(wxEVT_MENU,&MainFrame::OnClearBuf,this,ID_ClearBuf);
    Bind(wxEVT_MENU,&MainFrame::OnChangeElevation,this,ID_ElevUp);
    Bind(wxEVT_MENU,&MainFrame::OnChangeElevation,this,ID_ElevDown);
    Bind(wxEVT_MENU,&MainFrame::OnSelectAll,this,ID_SelectAll);
    Bind(wxEVT_MENU,&MainFrame::OnDeselectAll,this,ID_DeselectAll);
    Bind(wxEVT_MENU,&MainFrame::OnSelectDeselect,this,ID_SelectDeselect);
    Bind(wxEVT_MENU,&MainFrame::OnInvalidateSelection,this,ID_InvalidateSel);
    Bind(wxEVT_MENU,&MainFrame::OnDeleteSel,this,ID_DeleteSel);
    Bind(wxEVT_MENU,&MainFrame::OnCreateNewObject,this,ID_CreateNewObject);
    Bind(wxEVT_MENU,&MainFrame::OnAddUnit,this,ID_AddUnit);
    Bind(wxEVT_MENU,&MainFrame::OnOptionsAudio,this,ID_OptionsAudio);
    Bind(wxEVT_MENU,&MainFrame::OnOptionsScreen,this,ID_OptionsScreen);

    spell_map->SetMessageInterface(bind(&MainFrame::ShowMessage,this,placeholders::_1,placeholders::_2,placeholders::_3), bind(&MainFrame::CheckMessageState,this));    
    
    // main sizer 
    /*auto sizer2 = new wxBoxSizer(wxVERTICAL);

    wxButton *btnOk = new wxButton(canvas,wxID_ANY,wxT("TEST"),wxDefaultPosition,wxDefaultSize,0);
    sizer2->Add(btnOk,0,wxALL,5);

    canvas->SetSizer(sizer2);
    canvas->SetAutoLayout(true);
    canvas->Layout();*/

    //SetCursor(spelldata->gres.GetResource("DOJAZD.CUR")->Render(true)->ConvertToImage());
    
    //SetCursor(*spelldata->gres.ico_attack_up_down);

    /*SpellTextRec text("Experimental text message", SpellLang::CZE);
    ShowMessage(&text,true);*/

    // Restrict menus at startup (editor locked by default, GAMEMODEOFF unlocks)
    UpdateMenuForGameMode();

    // ESC handler via CHAR_HOOK — fires BEFORE menu accelerators,
    // so ESC is not consumed by the disabled Edit > Clear buffer accelerator.
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE && !m_editor_unlocked && !form_mmenu)
        {
            // Tactical HUD overlays sit on/over the map. ESC must close the
            // top-level overlay first instead of falling through to main menu.
            auto queueOverlayClose = [this](int id)
            {
                if (canvas)
                    wxQueueEvent(canvas, new wxCloseEvent(wxEVT_CLOSE_WINDOW, id));
            };

            if (form_map_options) { queueOverlayClose(ID_MAP_OPT_WIN); return; }
            if (form_units_list)  { queueOverlayClose(ID_MAP_UNITS_WIN); return; }
            if (form_minimap)     { queueOverlayClose(ID_MINIMAP_WIN); return; }

            // These forms already own their keyboard/close flow; let them
            // consume ESC rather than opening the game menu underneath them.
            if (form_unit_opts || form_message || form_video_box)
            {
                event.Skip();
                return;
            }

            if (spell_map && spell_map->isGameMode())
                spell_map->SetActiveGroup(0);
            wxCommandEvent evt;
            OnOpenMainMenu(evt);
            return;
        }
        event.Skip();
    });

    // auto-open main menu at startup
    CallAfter([this]() {
        wxCommandEvent evt;
        OnOpenMainMenu(evt);
    });

}

void MainFrame::OnSaveGameState(wxCommandEvent& event)
{
    (void)ShowUnifiedSaveGameDialog(this);
}

void MainFrame::OnOptionsAudio(wxCommandEvent& event)
{
    if (!spell_data || !spell_data->sounds || !spell_data->sounds->channels || !spell_data->midi)
        return;

    const double oldSfx = spell_data->sounds->channels->GetVolume();
    const double oldMusic = spell_data->midi->GetVolume();

    wxDialog dlg(this, wxID_ANY, "Audio", wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE);

    auto* sizerTop = new wxBoxSizer(wxVERTICAL);

    auto* lblMusic = new wxStaticText(&dlg, wxID_ANY, "Music volume");
    auto* sldMusic = new wxSlider(&dlg, wxID_ANY,
        (int)std::lround(oldMusic * 100.0), 0, 100,
        wxDefaultPosition, wxSize(300, -1),
        wxSL_HORIZONTAL | wxSL_VALUE_LABEL);

    auto* lblSfx = new wxStaticText(&dlg, wxID_ANY, "Sound volume");
    auto* sldSfx = new wxSlider(&dlg, wxID_ANY,
        (int)std::lround(oldSfx * 100.0), 0, 100,
        wxDefaultPosition, wxSize(300, -1),
        wxSL_HORIZONTAL | wxSL_VALUE_LABEL);

    sizerTop->Add(lblMusic, 0, wxALL, 8);
    sizerTop->Add(sldMusic, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    sizerTop->Add(lblSfx, 0, wxALL, 8);
    sizerTop->Add(sldSfx, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
    sizerTop->Add(btns, 0, wxALL | wxEXPAND, 8);
    dlg.SetSizerAndFit(sizerTop);

    auto applyAudio = [&]() {
        spell_data->midi->SetVolume(sldMusic->GetValue() / 100.0);
        spell_data->sounds->channels->SetVolume(sldSfx->GetValue() / 100.0);
    };

    sldMusic->Bind(wxEVT_SLIDER, [&](wxCommandEvent&) { applyAudio(); });
    sldSfx->Bind(wxEVT_SLIDER, [&](wxCommandEvent&) { applyAudio(); });

    if (dlg.ShowModal() == wxID_OK)
        applyAudio();
    else
    {
        spell_data->midi->SetVolume(oldMusic);
        spell_data->sounds->channels->SetVolume(oldSfx);
    }
}

void MainFrame::OnOptionsScreen(wxCommandEvent& event)
{
    const double oldGamma = spell_map ? spell_map->GetGamma() : 1.3;

    wxDialog dlg(this, wxID_ANY, "Screen", wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE);

    auto* sizerTop = new wxBoxSizer(wxVERTICAL);

    auto* lblBrightness = new wxStaticText(&dlg, wxID_ANY, "Brightness");
    auto* sldBrightness = new wxSlider(&dlg, wxID_ANY,
        (int)std::lround(oldGamma * 1000.0), 500, 2000,
        wxDefaultPosition, wxSize(300, -1),
        wxSL_HORIZONTAL | wxSL_VALUE_LABEL);

    sizerTop->Add(lblBrightness, 0, wxALL, 8);
    sizerTop->Add(sldBrightness, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
    sizerTop->Add(btns, 0, wxALL | wxEXPAND, 8);
    dlg.SetSizerAndFit(sizerTop);

    auto applyScreen = [&]() {
        if (spell_map)
            spell_map->SetGamma(sldBrightness->GetValue() * 0.001);
    };

    sldBrightness->Bind(wxEVT_SLIDER, [&](wxCommandEvent&) { applyScreen(); });

    if (dlg.ShowModal() == wxID_OK)
        applyScreen();
    else
    {
        if (spell_map)
            spell_map->SetGamma(oldGamma);
    }
}


// on form close
void MainFrame::RequestApplicationExit()
{
    if (m_applicationExitInProgress)
        return;

    m_applicationExitInProgress = true;
    wxLogDebug("[SHUTDOWN] beginning application shutdown");

    // The restored main menu and strategic screen intentionally live as
    // independent top-level frames (their wx parent is nullptr).  Destroying
    // only MainFrame therefore does NOT guarantee that wxWidgets leaves the
    // event loop.  Tear the independent layers down explicitly first.
    if (form_mmenu)
    {
        delete form_mmenu;
        form_mmenu = nullptr;
    }

    if (m_strategicLevel)
    {
        StrategicLevelFrame* strategic = m_strategicLevel;
        m_strategicLevel = nullptr;
        strategic->Hide();
        strategic->Destroy();
    }

    // A video wrapper owns Media Foundation / audio playback objects which are
    // not wx children themselves.  Release those before the canvas disappears.
    if (form_video_box)
    {
        delete form_video_box;
        form_video_box = nullptr;
    }

    if (spell_data && spell_data->midi)
        spell_data->midi->Stop();

    if (spell_map)
    {
        wxLogDebug("[SHUTDOWN] stopping tactical map/workers");
        spell_map->Close();
        wxLogDebug("[SHUTDOWN] tactical map/workers stopped");
    }

    // Safety net: should another independent top-level helper ever be added,
    // do not leave it keeping the process alive after every visible window has
    // gone.  Copy the list first because Destroy() mutates wxTopLevelWindows.
    std::vector<wxWindow*> strayTopLevels;
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst();
         node; node = node->GetNext())
    {
        wxWindow* win = node->GetData();
        if (win && win != this)
            strayTopLevels.push_back(win);
    }
    for (wxWindow* win : strayTopLevels)
    {
        if (!win || win->IsBeingDeleted())
            continue;
        wxLogDebug("[SHUTDOWN] destroying stray top-level window: %s", win->GetName().c_str());
        win->Hide();
        win->Destroy();
    }

    // Destroy rather than Close(): this is the terminal path and must not
    // re-enter OnClose or reopen the main menu from a child close handler.
    Hide();
    Destroy();

    // Do not rely solely on wxWidgets' "last top-level window" heuristic.
    // The restoration uses several independently-owned top-level frames and a
    // deferred Destroy() can otherwise leave the GUI loop alive with no visible
    // window.  Explicitly end the loop after all teardown requests are queued.
    if (wxTheApp)
        wxTheApp->ExitMainLoop();
}

void MainFrame::OnExit(wxCommandEvent& event)
{
    RequestApplicationExit();
}
// about message
void MainFrame::OnAbout(wxCommandEvent& event)
{
    auto form = new FormAbout(this);
    if(form->ShowModal() == wxID_OK)
    {
        // --- confirmed
    }
    delete form;
}
// callback function to write status messages from within the spellcross routines:
// usage: make and pass callback pointer using: bind(&MainFrame::StatusStringCallback,this,placeholders::_1)
// spellcross function example:
// void whatever_function(std::function<void(std::string)> status_cb)
// {
//   status_cb("Some message");
// }
void MainFrame::StatusStringCallback(std::string info)
{
    SetStatusText(info,7);
}

void MainFrame::OnClose(wxCloseEvent& ev)
{
    if(ev.GetId() == ID_GAMMA_WIN)
    {
        form_gamma->Destroy();
    }
    else if (ev.GetId() == ID_OBJECTS_WIN)
    {
        form_objects->Destroy();
        LoadToolsetRibbon();
    }
    else if (ev.GetId() == ID_SPRITES_WIN)
    {
        // on close sprite editor
        Terrain *terr = form_sprites->GetSelectedTerrain();
        Sprite *spr = form_sprites->GetSelectedSprite();        
        bool was_edit = form_sprites->wasSet();
        form_sprites->Destroy();
        LoadToolsetRibbon();

        if(spell_map && spell_map->IsLoaded() && spell_map->terrain == terr && spr)
        {
            if(was_edit)
            {
                // edit existing sprite
                spell_map->EditTileSprite(spr,&spell_pos);
            }
            else
            {
                // some sprite selected - place to clipboard
                spell_map->SetBuffer(spr);
            }
        }
    }
    else if(ev.GetId() == ID_ANM_WIN)
    {
        // on close ANM viewer
        Terrain* terr = form_anms->GetSelectedTerrain();
        AnimL1* anm = form_anms->GetSelectedAnim();
        AnimPNM* pnm = form_anms->GetSelectedPNM();
        bool was_edit = form_anms->WasAnmSet();
        bool was_pnm =  form_anms->wasPNM();
        auto [x_ofs,y_ofs] = form_anms->GetPNMoffset();
        form_anms->Destroy();

        if(spell_map && spell_map->IsLoaded() && spell_map->terrain == terr)
        {
            if(anm)
            {
                if(was_edit)
                {
                    // edit existing map anim
                    spell_map->PlaceANM(&spell_pos,anm);
                }
                else
                {
                    // some anim selected - place to clipboard
                    spell_map->SetBuffer(anm);
                }
            }
            if(pnm)
            {
                if(was_edit)
                {
                    // edit existing map anim
                    spell_map->PlacePNM(&spell_pos,pnm,x_ofs,y_ofs);
                }
                else
                {
                    // some anim selected - place to clipboard
                    spell_map->SetBuffer(pnm,x_ofs,y_ofs);
                }
            }

        }
    }
    else if(ev.GetId() == ID_SOUNDS_WIN)
    {
        // on close sounds viewer        
        SpellSample *snd = form_sounds->GetSelectedSound();
        auto snd_type = form_sounds->GetMapSoundType();
        auto was_edit = form_sounds->WasSoundSet();
        form_sounds->Destroy();

        if(spell_map && spell_map->IsLoaded() && snd)
        {
            if(was_edit)
            {
                // edit existing map anim
                spell_map->SoundEdit(snd,snd_type,&spell_pos);
            }
            else
            {
                // add new sound
                auto map_sound = spell_map->SoundAdd(snd, snd_type, &spell_pos);
                if(map_sound)
                {
                    spell_map->SoundSelect(map_sound);
                    map_sound->in_placement = true;                    
                }
            }
        }                
    }
    else if(ev.GetId() == ID_PAL_WIN)
    {
        form_pal->Destroy();
    }
    else if(ev.GetId() == ID_GRES_WIN)
    {
        form_gres->Destroy();
    }
    else if(ev.GetId() == ID_MINIMAP_WIN)
    {
        delete form_minimap;
        form_minimap = NULL;
    }
    else if(ev.GetId() == ID_UNITS_WIN)
    {
        // unit editor closed
        auto new_unit = form_units->DoAddUnit();
        if(new_unit)
        {
            // add new unit to map
            new_unit->in_placement = true;
            new_unit->is_active = true;
            new_unit->ResetAP();
            auto pos = spell_map->GetSelection();
            if(pos.IsSelected())
                new_unit->coor = pos;
            else
                new_unit->coor = MapXY(0,0);
            if(new_unit->is_event)
            {
                // event unit - place to MissionStart
                spell_map->events->AddMissionStartUnit(new_unit);
            }
            else
            {
                // normal unit - place to map
                spell_map->AddUnit(new_unit);
            }
            spell_map->SelectUnit(new_unit);
        }
        if(form_units->DoUpdateUnit())
        {
            // update current unit:
            spell_map->SortUnits();
            canvas->Refresh();
        }        
        form_units->Destroy();
    }
    else if(ev.GetId() == ID_EVENT_WIN)
    {
        // event editor closed        
        spell_map->SortUnits();
        canvas->Refresh();
        form_events->Destroy();
    }
    else if(ev.GetId() == ID_VIDEO_WIN)
    {
        // video viwer closed        
        form_videos->Destroy();
    }
    else if(ev.GetId() == ID_MIDI_WIN)
    {
        // midi player closed        
        form_midi->Destroy();
    }
    else if(ev.GetId() == ID_UNIT_MODE_WIN)
    {
        // unit multi-action menu
        form_unit_opts->ResultCallback(); // exec result callback (calling it from here to have in this thread)
        delete form_unit_opts;
        form_unit_opts = NULL;
    }
    else if(ev.GetId() == ID_MSG_WIN && form_message)
    {
        // unit multi-action menu
        form_message->ResultCallback(); // exec result callback (calling it from here to have in this thread)
        delete form_message;
        form_message = NULL;

        // If the mission ended while this message was visible, reveal the
        // mission-result page only after wxWidgets has finished closing the
        // child window.  This guarantees a repaint instead of waiting for a
        // seemingly unrelated click/move on the battlefield.
        if (m_mission_result_pending_show)
        {
            CallAfter([this]() {
                if (m_mission_result_pending_show && !CheckMessageState())
                    ShowMissionResultOverlay();
            });
        }
    }
    else if(ev.GetId() == ID_VIDEO_BOX_WIN && form_video_box)
    {
        delete form_video_box;
        form_video_box = NULL;
        if (m_reopen_mmenu_after_video)
        {
            m_reopen_mmenu_after_video = false;
            CallAfter([this]() {
                wxCommandEvent evt;
                OnOpenMainMenu(evt);
            });
        }
    }
    else if(ev.GetId() == ID_MAP_OPT_WIN && form_map_options)
    {
        // unit multi-action menu
        //form_map_options->ResultCallback(); // exec result callback (calling it from here to have in this thread)
        delete form_map_options;
        form_map_options = NULL;
    }
    else if(ev.GetId() == ID_MAP_UNITS_WIN && form_units_list)
    {
        // map unit list selection        
        auto *unit = form_units_list->GetSelectedUnit();
        if(unit)
            spell_map->SelectUnit(unit,true);
        
        delete form_units_list;
        form_units_list = NULL;
    }
    else if(ev.GetId() == ID_MMENU_WIN)
    {
        // Menu may already be deleted by OnMainMenuAction CallAfter;
        // guard against double-free.
        if(form_mmenu)
        {
            delete form_mmenu;
            form_mmenu = NULL;
        }

        if (m_mainMenuReturnToTactical)
        {
            m_mainMenuReturnToTactical = false;
            ShowTacticalWindow();
        }
        else
        {
            // Closing the startup/strategic-return menu must never expose the
            // stale tactical map behind it. Treat the window close like Exit.
            RequestApplicationExit();
        }
    }
    else
    {
        // This handler is bound both to MainFrame and to the tactical canvas,
        // because several overlay wrappers forward close notifications through
        // the canvas.  Only an actual MainFrame close means application exit.
        const bool mainFrameClose = ev.GetEventObject() == this || ev.GetId() == GetId();
        if (!mainFrameClose)
        {
            ev.Skip();
            return;
        }

        // Native close of MainFrame is an application exit.  Route it through
        // the same idempotent cleanup as menu/strategic Exit so independent
        // top-level frames cannot survive invisibly in the background.
        if (m_applicationExitInProgress)
        {
            ev.Skip();
        }
        else
        {
            if (ev.CanVeto())
                ev.Veto();
            CallAfter([this]() { RequestApplicationExit(); });
        }
    }
}


// on switch game mode
void MainFrame::OnSwitchGameMode(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;

    auto is_game = GetMenuBar()->FindItem(ID_mmGameMode)->IsChecked();
    SetGameModeUI(is_game);
}

void MainFrame::OnLoadGameState(wxCommandEvent& event)
{
    (void)ShowUnifiedLoadGameDialog(this);
}

bool MainFrame::LoadGameStateFromDialog()
{
    wstring temp_dir = (std::filesystem::current_path() / L"temp").wstring();
    wxFileDialog openFileDialog(
        this,
        "Load game state",
        temp_dir,
        "",
        "Spellcross save (*.scsave)|*.scsave",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );

    if (openFileDialog.ShowModal() == wxID_CANCEL)
        return false;

    wxString path = openFileDialog.GetPath();
    std::wstring wpath = path.ToStdWstring();

    if (!LoadTacticalGameWithCampaignContext(wpath))
    {
        wxMessageBox(wxString::Format("Load failed: %s", spell_map->GetLastError()),
            "Load error", wxICON_ERROR | wxOK, this);
        return false;
    }

    return true;
}


bool MainFrame::ShowUnifiedSaveGameDialog(wxWindow* owner)
{
    // If the strategic layer is the active game screen, save the campaign
    // state rather than the hidden/stale tactical map behind it.  This is the
    // exact same entry point used by Strategic -> File -> Save game.
    if (m_strategicLevel && m_strategicLevel->IsShown())
        return m_strategicLevel->PromptStrategicSaveFile(false);

    if (!spell_map || !spell_map->IsLoaded())
        return false;

    const std::filesystem::path startDir = std::filesystem::current_path() / "temp";
    wxWindow* dialogOwner = owner ? owner : static_cast<wxWindow*>(this);
    wxFileDialog dlg(
        dialogOwner,
        "Save game",
        wxString::FromUTF8(startDir.string()),
        "",
        "Spellcross save (*.scsave)|*.scsave|All files (*.*)|*.*",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT
    );

    if (dlg.ShowModal() != wxID_OK)
        return false;

    std::filesystem::path savePath(dlg.GetPath().ToStdWstring());
    if (savePath.extension().empty())
        savePath.replace_extension(".scsave");

    if (!SaveTacticalGameWithCampaignContext(savePath.wstring()))
    {
        wxMessageBox("Save game state failed!", "Save error", wxICON_ERROR | wxOK, dialogOwner);
        return false;
    }

    return true;
}

bool MainFrame::ShowUnifiedLoadGameDialog(wxWindow* owner)
{
    namespace fs = std::filesystem;

    fs::path loadStart = fs::current_path() / "save" / "strategic";
    std::error_code ec;
    if (!fs::exists(loadStart, ec))
        loadStart = fs::current_path() / "temp";

    wxWindow* dialogOwner = owner ? owner : static_cast<wxWindow*>(this);
    wxFileDialog dlg(
        dialogOwner,
        "Load game",
        wxString::FromUTF8(loadStart.string()),
        "",
        "All saves (*.scsave;*.json;*.sav)|*.scsave;*.json;*.sav|Tactical save (*.scsave)|*.scsave|Remake strategic state (*.json)|*.json|Original BIG_MAP.SAV (*.sav)|*.sav",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );

    if (dlg.ShowModal() != wxID_OK)
        return false;

    const fs::path loadPath(dlg.GetPath().ToStdWstring());
    const std::string extLower = to_lower(loadPath.extension().string());

    if (extLower == ".json" || extLower == ".sav")
        return OpenStrategicSaveFromPath(this, loadPath);

    if (!LoadTacticalGameWithCampaignContext(loadPath.wstring()))
    {
        wxMessageBox(wxString::Format("Load failed: %s", spell_map ? spell_map->GetLastError() : "unknown error"),
            "Load error", wxICON_ERROR | wxOK, dialogOwner);
        return false;
    }

    // Loading an old/standalone tactical save may not contain campaign context,
    // in which case LoadTacticalGameWithCampaignContext intentionally cannot
    // know that a currently visible strategic window must go away.
    if (m_strategicLevel && m_strategicLevel->IsShown())
    {
        StrategicLevelFrame* old = m_strategicLevel;
        m_strategicLevel = nullptr;
        old->Destroy();
    }

    ShowTacticalWindow();
    return true;
}

void MainFrame::ShowTacticalWindow()
{
    Show(true);
    Raise();
    if (canvas)
        canvas->SetFocus();
}

void MainFrame::HideTacticalWindow()
{
    if (IsShown())
        Hide();
}

void MainFrame::ShowMainMenuWindow()
{
    wxCommandEvent evt;
    OnOpenMainMenu(evt);
}

void MainFrame::OnOpenMainMenu(wxCommandEvent& event)
{
    if(form_mmenu)
        return;

    if(!canvas)
        return;

    // A main menu opened from an active battle acts like a pause/menu layer and
    // may return to that battle when closed. At startup/after strategic there
    // is deliberately no tactical surface to fall back to.
    m_mainMenuReturnToTactical = IsShown() && spell_map && spell_map->IsLoaded() && spell_map->isGameMode();

    form_mmenu = new FormMainMenu(canvas, ID_MMENU_WIN, spell_map, spell_data,
        [this](FormMainMenuAction action) { OnMainMenuAction(action); });

    HideTacticalWindow();
}

static std::string FindLevelDefContainingMission(const std::string& missionStem,
    const SpellData* spellData);

// Tactical DEF names may carry an A/B/C mission-stage suffix while the loaded
// battlefield itself is Mxx_yy.DTA.  Reduce both forms to the same mission
// family before deciding whether a hidden strategic frame belongs to the
// battle that is currently running.
static std::string MissionFamilyToken(const std::string& raw)
{
    namespace fs = std::filesystem;
    std::string token = fs::path(raw).stem().string();
    for (char& c : token)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    // Original campaign mission families are MNN_NN with an optional stage
    // letter (M05_06A/M05_06B).  The DTA is simply M05_06.
    if (token.size() == 7 && token[0] == 'M' &&
        std::isdigit(static_cast<unsigned char>(token[1])) &&
        std::isdigit(static_cast<unsigned char>(token[2])) &&
        token[3] == '_' &&
        std::isdigit(static_cast<unsigned char>(token[4])) &&
        std::isdigit(static_cast<unsigned char>(token[5])) &&
        std::isalpha(static_cast<unsigned char>(token[6])))
    {
        token.pop_back();
    }
    return token;
}

static bool StrategicContextMatchesLoadedMission(const StrategicLevelFrame* strategic,
    SpellMap* map)
{
    if (!strategic || !map || !map->IsLoaded() || !strategic->m_pendingMission.valid)
        return false;

    const std::string pending = MissionFamilyToken(strategic->m_pendingMission.mission_token);
    const std::string loaded = MissionFamilyToken(
        std::filesystem::path(map->map_path).stem().string());
    return !pending.empty() && !loaded.empty() && pending == loaded;
}

void MainFrame::SyncUiAfterTacticalLoad()
{
    if (!spell_map || !spell_map->isGameMode())
        return;

    SetTitle(BuildSpellcrossWindowTitle(spell_map));

    if (GetMenuBar())
    {
        if (auto* item = GetMenuBar()->FindItem(ID_mmGameMode))
            item->Check(true);
    }
    if (ribbonBar)
    {
        ribbonBar->Hide();
        if (sizer) sizer->Layout();
    }

    if (menuView)
    {
        if (menuView->FindItem(ID_ViewSoundLoops)) menuView->FindItem(ID_ViewSoundLoops)->Check(false);
        if (menuView->FindItem(ID_ViewSounds))     menuView->FindItem(ID_ViewSounds)->Check(false);
        if (menuView->FindItem(ID_ViewEvents))     menuView->FindItem(ID_ViewEvents)->Check(false);
    }
    wxCommandEvent dummy;
    OnViewLayer(dummy);
    UpdateMenuForGameMode();
    if (canvas)
        canvas->Refresh();
}

bool MainFrame::SaveTacticalGameWithCampaignContext(const std::wstring& path)
{
    if (!spell_map || !spell_map->IsLoaded())
        return false;

    const int rc = spell_map->SaveGameStateToFile(path);
    if (rc != 0)
        return false;

    // Persist the current mission casualty ledger even for the opening
    // tactical mission, which legitimately has no strategic frame yet. Dead
    // units have already been removed from the map and cannot be reconstructed
    // from the tactical unit list after loading.
    TacticalCampaignContext ctx;
    ctx.present = true;
    // Serialize only casualties already removed from the tactical unit list.
    // A zero-man unit still present in the base .scsave will be counted by
    // GetMissionLossStats() after load; storing that transient corpse here as
    // well would count it twice.
    ctx.mission_losses = spell_map->GetRecordedMissionLossStats();

    if (m_strategicLevel)
    {
        StrategicLevelFrame::PendingMissionResult pending;
        if (!m_strategicLevel->ExportBattleSaveContext(
                ctx.level_def_path, ctx.strategic_state_json, pending))
            return false;
        ctx.pending_valid = pending.valid;
        ctx.territory_id = pending.territory_id;
        ctx.mission_token = pending.mission_token;
        ctx.sent_unit_indices.reserve(pending.sent_unit_indices.size());
        for (size_t idx : pending.sent_unit_indices)
            ctx.sent_unit_indices.push_back(static_cast<std::uint64_t>(idx));
        ctx.sent_unit_uids = pending.sent_unit_uids;
    }

    return AppendTacticalCampaignContext(path, ctx);
}

bool MainFrame::LoadTacticalGameWithCampaignContext(const std::wstring& path)
{
    if (!spell_map)
        return false;

    TacticalCampaignContext ctx;
    const bool hasEmbeddedContext = ReadTacticalCampaignContext(path, ctx);

    const int rc = spell_map->LoadGameStateFromFile(path);
    if (rc != 0)
        return false;
    if (hasEmbeddedContext && ctx.present)
        spell_map->SetMissionLossStats(ctx.mission_losses);

    auto destroyOldStrategic = [this]()
    {
        if (m_strategicLevel)
        {
            StrategicLevelFrame* old = m_strategicLevel;
            m_strategicLevel = nullptr;
            old->Hide();
            old->Destroy();
        }
    };
    bool restoredStrategicContext = false;

    if (hasEmbeddedContext && ctx.present && !ctx.strategic_state_json.empty())
    {
        namespace fs = std::filesystem;
        fs::path levelDef(ctx.level_def_path);
        std::error_code ec;

        if (!fs::exists(levelDef, ec))
        {
            // Saves may move between installs/machines. Recover the canonical
            // LEVEL_XX.DEF from the mission token rather than trusting an old
            // absolute path.
            std::string mission = ctx.mission_token;
            if (mission.empty() && spell_map->IsLoaded())
                mission = fs::path(spell_map->map_path).stem().string();
            const std::string found = FindLevelDefContainingMission(mission, spell_data);
            if (!found.empty())
                levelDef = fs::path(found);
        }

        LevelData lvl;
        std::string err;
        LevelLoader loader;
        if (!levelDef.empty() && loader.LoadLevelDef(levelDef.string(), lvl, &err))
        {
            destroyOldStrategic();
            auto* win = new StrategicLevelFrame(this, lvl, true);
            m_strategicLevel = win;

            StrategicLevelFrame::PendingMissionResult pending;
            pending.valid = ctx.pending_valid;
            pending.territory_id = ctx.territory_id;
            pending.mission_token = ctx.mission_token;
            pending.sent_unit_indices.reserve(ctx.sent_unit_indices.size());
            for (std::uint64_t idx : ctx.sent_unit_indices)
                pending.sent_unit_indices.push_back(static_cast<size_t>(idx));
            pending.sent_unit_uids = ctx.sent_unit_uids;

            if (!win->ImportBattleSaveContext(ctx.strategic_state_json, pending))
            {
                m_strategicLevel = nullptr;
                win->Destroy();
                return false;
            }
            win->Hide();
            restoredStrategicContext = true;
        }
        else
        {
            wxLogWarning("Battle save contains strategic context, but its LEVEL DEF could not be restored: %s",
                err.c_str());
            return false;
        }
    }
    else if (!hasEmbeddedContext || !ctx.present)
    {
        // Compatibility for old Stage/build saves: they did not embed strategic
        // context at all. If the matching strategic autosave still exists on the
        // same installation, reconnect it and reconstruct the pending mission.
        // A v3 context with an intentionally empty strategic JSON is the opening
        // standalone mission and must NOT be rebound to an unrelated autosave.
        // This is deliberately best-effort; new campaign saves are self-contained.
        namespace fs = std::filesystem;
        std::string missionStem;
        if (spell_map->IsLoaded())
            missionStem = fs::path(spell_map->map_path).stem().string();

        if (!missionStem.empty())
        {
            const std::string levelDefPath = FindLevelDefContainingMission(missionStem, spell_data);
            if (!levelDefPath.empty())
            {
                LevelData lvl;
                std::string err;
                LevelLoader loader;
                if (loader.LoadLevelDef(levelDefPath, lvl, &err))
                {
                    auto* candidate = new StrategicLevelFrame(this, lvl, true);
                    if (candidate->HasStrategicAutosave())
                    {
                        destroyOldStrategic();
                        m_strategicLevel = candidate;
                        candidate->LoadStrategicState();
                        candidate->RecoverPendingMissionFromLoadedBattle(missionStem);
                        candidate->Hide();
                        restoredStrategicContext = true;
                    }
                    else
                    {
                        candidate->Destroy();
                    }
                }
            }
        }
    }

    // A tactical save can intentionally have no strategic snapshot (the
    // campaign-opening mission is the important example).  Never leave an
    // unrelated hidden StrategicLevelFrame from an earlier load attached to
    // such a battle: mission completion would otherwise return to that stale
    // level instead of following the loaded mission's own campaign data.
    if (!restoredStrategicContext && m_strategicLevel)
        destroyOldStrategic();

    SyncUiAfterTacticalLoad();
    return true;
}

void MainFrame::OnMainMenuAction(FormMainMenuAction action)
{
    // Defer the actual work so that the mouse-click event handler in
    // FormMainMenu finishes before we destroy the menu (avoids use-after-free).
    CallAfter([this, action]() {

        // Close the menu first (triggers ID_MMENU_WIN close handler which does delete).
        if(form_mmenu)
        {
            delete form_mmenu;
            form_mmenu = NULL;
        }

        switch(action)
        {
            case FormMainMenuAction::NewGame:
            {
                if(!spell_data)
                    break;

                // New Game starts with the standalone M01_01A escape mission.
                // A previously loaded strategic campaign can still exist as a
                // hidden frame behind the main menu; keeping it would make the
                // opening mission return to that old level when it ends.
                if (m_strategicLevel)
                {
                    StrategicLevelFrame* old = m_strategicLevel;
                    m_strategicLevel = nullptr;
                    old->Hide();
                    old->Destroy();
                }
                m_mission_end_flow = false;
                m_mission_result_pending_show = false;
                m_mission_result_visible = false;
                m_mission_result_stage = MissionResultStage::None;
                m_mission_end_req = SpellMap::MissionEndRequest();

                std::filesystem::path root = std::filesystem::path(spell_data->spell_data_root);
                auto def_path = FindSpellDataFile(root, "M01_01A.DEF");
                if(def_path.empty())
                {
                    wxMessageBox("Mission M01_01A.DEF not found.", "Main menu", wxOK | wxICON_WARNING, this);
                    ShowMainMenuWindow();
                    break;
                }

                // Play level intro video (L_01.CAN or similar) before the first mission
                {
                    std::string levelDefPath = FindLevelDefContainingMission("M01_01A", spell_data);
                    if (!levelDefPath.empty())
                    {
                        LevelData lvlIntro;
                        std::string err;
                        LevelLoader loader;
                        if (loader.LoadLevelDef(levelDefPath, lvlIntro, &err) &&
                            !lvlIntro.intro_video.empty() && lvlIntro.intro_video != "none")
                        {
                            if (!FindWindowById(ID_VIDEO_BOX_WIN) && spell_data)
                            {
                                ShowTacticalWindow();
                                try {
                                    form_video_box = new FormVideoBox(
                                        canvas, ID_VIDEO_BOX_WIN, spell_data,
                                        lvlIntro.intro_video, 2);
                                } catch (const std::exception&) {
                                    // intro video not found — continue without it
                                }
                            }
                        }
                    }
                }

                if(LoadMapFromDefPath(def_path.wstring(), {}))
                {
                    SetGameModeUI(true);
                    ShowTacticalWindow();
                }
                else
                {
                    ShowMainMenuWindow();
                }
                break;
            }
            case FormMainMenuAction::Continue:
            {
                // If the main menu was opened from an already running tactical
                // battle (ESC), Continue must resume that exact in-memory state.
                // Reloading GetTopPath()/DEF here restarts the mission and loses
                // the current turn/unit/event state.
                if (m_mainMenuReturnToTactical && spell_map && spell_map->IsLoaded() && spell_map->isGameMode())
                {
                    m_mainMenuReturnToTactical = false;
                    ShowTacticalWindow();
                    break;
                }

                // Outside an ESC/pause flow there is no live tactical session to
                // resume. Keep the menu visible rather than silently reloading a
                // map definition and pretending that it is a continuation.
                wxMessageBox("No paused tactical game to continue.", "Main menu",
                    wxOK | wxICON_INFORMATION, this);
                ShowMainMenuWindow();
                break;
            }
            case FormMainMenuAction::LoadGame:
            {
                // Use the exact same picker/dispatch path as Strategic -> File
                // -> Load game, so tactical, remake strategic and original DOS
                // saves behave identically from either screen.
                if (!ShowUnifiedLoadGameDialog(this))
                    ShowMainMenuWindow();
                break;
            }
            case FormMainMenuAction::Credits:
            {
                // Legacy video window is owned by the tactical canvas. Show its
                // host only for the cutscene; the menu reopens afterwards.
                ShowTacticalWindow();
                if (!FindWindowById(ID_VIDEO_BOX_WIN) && spell_data)
                {
                    try {
                        form_video_box = new FormVideoBox(canvas, ID_VIDEO_BOX_WIN, spell_data, "CREDITS.DPK", 2);
                        m_reopen_mmenu_after_video = true;
                    } catch (const std::exception&) {
                        wxMessageBox("Cannot play CREDITS.DPK (video not found).", "Credits", wxOK | wxICON_WARNING, this);
                        HideTacticalWindow();
                        ShowMainMenuWindow();
                    }
                }
                break;
            }
            case FormMainMenuAction::Intro:
            {
                ShowTacticalWindow();
                if (!FindWindowById(ID_VIDEO_BOX_WIN) && spell_data)
                {
                    try {
                        form_video_box = new FormVideoBox(canvas, ID_VIDEO_BOX_WIN, spell_data, "INTRO.CAN", 2);
                        m_reopen_mmenu_after_video = true;
                    } catch (const std::exception&) {
                        wxMessageBox("Cannot play INTRO.CAN (video not found).", "Intro", wxOK | wxICON_WARNING, this);
                        HideTacticalWindow();
                        ShowMainMenuWindow();
                    }
                }
                break;
            }
            case FormMainMenuAction::Exit:
                RequestApplicationExit();
                return;
            case FormMainMenuAction::GameModeOff:
                m_editor_unlocked = true;
                if(spell_map && spell_map->IsLoaded() && spell_map->isGameMode())
                    SetGameModeUI(false);
                else
                    UpdateMenuForGameMode();
                ShowTacticalWindow();
                break;
            default:
                break;
        }

        // Return focus to canvas so ESC and other keys work
        if(canvas)
            canvas->SetFocus();
    });
}

// on reset view range in game mode
void MainFrame::OnResetUnitView(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;

    spell_map->unit_view->ClearUnitsView(SpellMap::ViewRange::ClearMode::RESET);
    spell_map->InvalidateUnitsView();
    canvas->Refresh();
}

// cycle unit range view modes
void MainFrame::OnSelectUnitView(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;

    if(spell_map->isGameMode())
    {
        // game mode unti range view selection
        spell_map->SetUnitRangeViewMode(SpellMap::UNIT_RANGE_INCREMENT);
        canvas->Refresh();
    }
}

static bool g_cutscene_handled = false;

static bool IsIntroEscapeMission(const SpellMap* map)
{
    if (!map) return false;
    std::wstring stem = std::filesystem::path(map->map_path).stem().wstring();
    for (auto& ch : stem) ch = (wchar_t)towupper(ch);
    return stem == L"M01_01" || stem == L"M01_01A" || stem == L"LEVEL_01" ||
        stem == L"LEVEL1_1" || stem == L"LEVEL_01_01";
}


void MainFrame::ShowMissionResultOverlay()
{
    m_mission_result_pending_show = false;
    m_mission_result_stats = spell_map ? spell_map->GetMissionLossStats() : SpellMap::MissionLossStats{};
    m_mission_result_visible = true;
    m_mission_result_stage = MissionResultStage::Debrief;
    // Two timer ticks are enough to get past the input event that triggered the
    // final objective/death.  This makes the result page a real modal step
    // instead of allowing a click-through straight into Strategic Level.
    m_mission_result_input_guard = 2;

    // Mission-end presentation is deliberately drawn directly into the tactical
    // canvas.  Do not use FormMsgBox here: creating a child window from the same
    // mouse event that completes a mission can immediately close that child and
    // advance the flow before it is ever painted (the bug seen in v20-v22).
    if (canvas)
    {
        canvas->SetFocus();
        canvas->Refresh();
        canvas->Update();
    }
}

void MainFrame::DismissMissionResultOverlay()
{
    if (!m_mission_result_visible || m_mission_result_input_guard > 0)
        return;

    if (m_mission_result_stage == MissionResultStage::Debrief)
    {
        // The campaign-opening Escape mission goes from the debrief directly to
        // Alexander's video, matching the DOS flow.  Normal campaign missions
        // show the losses table as a second page.
        if (IsIntroEscapeMission(spell_map) && !m_strategicLevel)
        {
            m_mission_result_visible = false;
            m_mission_result_stage = MissionResultStage::None;
            if (canvas) canvas->Refresh();
            StartMissionEndFlow();
            return;
        }

        m_mission_result_stage = MissionResultStage::Statistics;
        m_mission_result_input_guard = 2;
        if (canvas)
        {
            canvas->Refresh();
            canvas->Update();
        }
        return;
    }

    m_mission_result_visible = false;
    m_mission_result_stage = MissionResultStage::None;
    if (canvas) canvas->Refresh();
    StartMissionEndFlow();
}

void MainFrame::DrawMissionResultOverlay(wxDC& dc)
{
    if (!m_mission_result_visible || !spell_data || !spell_data->font || !canvas)
        return;

    SpellGraphicItem* titleItem = spell_data->gres.GetResource(
        m_mission_end_req.success ? "M_ACCOMP" : "M_FAILED");

    const wxSize cs = canvas->GetClientSize();
    const double scale = std::min(cs.GetWidth() / 640.0, cs.GetHeight() / 480.0);
    if (scale <= 0.0) return;
    const int ox = (cs.GetWidth() - (int)std::lround(640.0 * scale)) / 2;
    const int oy = (cs.GetHeight() - (int)std::lround(480.0 * scale)) / 2;
    auto X = [&](int v) { return ox + (int)std::lround(v * scale); };
    auto Y = [&](int v) { return oy + (int)std::lround(v * scale); };

    const wxColour text(218, 222, 211);

    // The original FS filename is M_ACCOMP.LZ/M_FAILED.LZ, but AddRaw stores
    // these as extensionless resources.  Draw the original asset whenever it
    // is available.  If an installation is genuinely missing it, keep the
    // result page visible with a text heading instead of creating an invisible
    // modal screen.
    bool titleDrawn = false;
    if (titleItem)
    {
        std::unique_ptr<wxBitmap> titleNative(titleItem->Render(true));
        if (titleNative && titleNative->IsOk())
        {
            wxBitmap title = ScaleNearest(*titleNative, scale);
            dc.DrawBitmap(title, X(150), Y(10), true);
            titleDrawn = true;
        }
    }

    auto drawText = [&](const wxString& str, int x, int y, int w, bool centered)
    {
        wxBitmap b = MakeSpellTextBitmap(spell_data->font, str, text, w, centered);
        if (!b.IsOk()) return;
        b = ScaleNearest(b, scale);
        dc.DrawBitmap(b, X(x), Y(y), true);
    };

    if (!titleDrawn)
    {
        wxLogWarning("Mission result title resource is missing; using text fallback.");
        drawText(m_mission_end_req.success ? L"MISSION ACCOMPLISHED" : L"MISSION FAILED",
            150, 35, 340, true);
    }

    if (m_mission_result_stage == MissionResultStage::Debrief)
    {
        // Render the debrief in the same original battlefield message-frame
        // graphics used by Spellcross (RAM2ROH/RAM2HORZ/RAM2VERT).  Keeping it
        // inside the canvas makes this page deterministic after save/load and
        // prevents the old child-window click-through race.
        wxString debrief;
        if (m_mission_end_req.text)
            debrief = wxString(m_mission_end_req.text->text.c_str());
        if (debrief.empty())
            debrief = m_mission_end_req.success ? L"Mission accomplished." : L"Mission failed.";

        // Match the original post-mission debrief placement: a fixed framed
        // message box directly under the banner.  The earlier restoration sized
        // and positioned the box dynamically near the bottom of the screen,
        // which made the frame drift down/right compared to DOS and changed
        // from mission to mission depending on wrapped line count.
        const int panelW = 408;
        const int innerW = 344;
        const int panelX = 116;
        const int panelY = 92;

        const auto chunks = m_mission_end_req.text
            ? m_mission_end_req.text->WordWrap(spell_data->font, innerW)
            : SpellTextChunks{};
        std::vector<wxString> lines;
        if (!chunks.empty())
        {
            lines.reserve(chunks.size());
            for (const auto& c : chunks)
                if (!c.text.empty()) lines.emplace_back(c.text);
        }
        else
        {
            // Fallback is short; keep it as one centered line.
            lines.push_back(debrief);
        }

        const int lineH = std::max(1, spell_data->font->GetHeight()) + 1;
        const int panelH = std::max(74, 32 + (int)lines.size() * lineH);

        // Recreate the original message-box background from the already
        // rendered battlefield and run it through the same dark palette filter
        // as FormMsgBox.  This keeps the terrain visible underneath instead of
        // replacing it with an opaque green rectangle.
        const int bgX = X(panelX);
        const int bgY = Y(panelY);
        const int bgW = std::max(1, (int)std::lround(panelW * scale));
        const int bgH = std::max(1, (int)std::lround(panelH * scale));
        if (spell_map && spell_map->terrain && bgW > 0 && bgH > 0)
        {
            std::vector<uint8_t> bg(static_cast<size_t>(bgW) * static_cast<size_t>(bgH));
            spell_map->GetRender(bg.data(), bgW, bgH, bgX, bgY);
            for (uint8_t& px : bg)
                px = spell_map->terrain->filter.darkpal[px];

            uint8_t* pal = reinterpret_cast<uint8_t*>(spell_map->terrain->pal);
            wxBitmap darkBg(bgW, bgH, 24);
            wxNativePixelData pdata(darkBg);
            wxNativePixelData::Iterator it(pdata);
            size_t src = 0;
            for (int y = 0; y < bgH; ++y)
            {
                uint8_t* scan = it.m_ptr;
                for (int x = 0; x < bgW; ++x, ++src)
                {
                    const uint8_t idx = bg[src];
                    *scan++ = pal[idx * 3 + 2];
                    *scan++ = pal[idx * 3 + 1];
                    *scan++ = pal[idx * 3 + 0];
                }
                it.OffsetY(pdata, 1);
            }
            dc.DrawBitmap(darkBg, bgX, bgY, false);
        }

        // Compose only the metallic frame and text at native 640x480 size,
        // then scale that finished overlay as one bitmap.  A colour key keeps
        // the centre transparent so the darkened battlefield remains visible.
        const wxColour maskColour(255, 0, 255);
        wxBitmap panel(panelW, panelH, 24);
        wxMemoryDC mem;
        mem.SelectObject(panel);
        mem.SetBackground(wxBrush(maskColour));
        mem.Clear();

        auto drawNativePanel = [&](SpellGraphicItem* item, int x, int y)
        {
            if (!item) return;
            std::unique_ptr<wxBitmap> native(item->Render(true));
            if (!native || !native->IsOk()) return;
            mem.DrawBitmap(*native, x, y, true);
        };

        auto* corn = spell_data->gres.wm_frame_corner;
        auto* horz = spell_data->gres.wm_frame_horz;
        auto* vert = spell_data->gres.wm_frame_vert;
        if (corn && horz && vert)
        {
            const int cw = std::max(1, corn->x_size);
            const int ch = std::max(1, corn->y_size);
            for (int x = cw; x < panelW - cw; x += std::max(1, horz->x_size))
            {
                drawNativePanel(horz, x, 0);
                drawNativePanel(horz, x, panelH - horz->y_size);
            }
            for (int y = ch; y < panelH - ch; y += std::max(1, vert->y_size))
            {
                drawNativePanel(vert, 0, y);
                drawNativePanel(vert, panelW - cw, y);
            }
            drawNativePanel(corn, 0, 0);
            drawNativePanel(corn, panelW - cw, 0);
            drawNativePanel(corn, 0, panelH - ch);
            drawNativePanel(corn, panelW - cw, panelH - ch);
        }

        const int textY = 18;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            wxBitmap lineBmp = MakeSpellTextBitmap(spell_data->font, lines[i], text, innerW, true);
            if (lineBmp.IsOk())
                mem.DrawBitmap(lineBmp, 32, textY + (int)i * lineH, true);
        }
        mem.SelectObject(wxNullBitmap);
        panel.SetMask(new wxMask(panel, maskColour));

        panel = ScaleNearest(panel, scale);
        dc.DrawBitmap(panel, X(panelX), Y(panelY), true);
        return;
    }

    SpellGraphicItem* statItem = spell_data->gres.GetResource("WM_STAT");
    bool statDrawn = false;
    if (statItem)
    {
        std::unique_ptr<wxBitmap> statNative(statItem->Render(true));
        if (statNative && statNative->IsOk())
        {
            wxBitmap stats = ScaleNearest(*statNative, scale);
            dc.DrawBitmap(stats, X(116), Y(103), true);
            statDrawn = true;
        }
    }
    if (!statDrawn)
    {
        wxLogWarning("Mission result statistics resource WM_STAT is missing; using visible fallback panel.");
        dc.SetPen(wxPen(wxColour(135, 135, 135)));
        dc.SetBrush(wxBrush(wxColour(12, 18, 12)));
        dc.DrawRectangle(X(116), Y(103),
            (int)std::lround(408 * scale), (int)std::lround(175 * scale));
    }

    // WM_STAT.LZ native grid: x=116..523, y=103..277 in 640x480 space.
    drawText(L"Alliance - losses", 116 + 113, 103 + 25, 132, true);
    drawText(L"Dark Side - losses", 116 + 246, 103 + 25, 144, true);

    const wxString labels[4] = { L"Light units", L"Heavy units", L"Air units", L"Commanders" };
    const int alliance[4] = { m_mission_result_stats.alliance_light, m_mission_result_stats.alliance_heavy,
        m_mission_result_stats.alliance_air, m_mission_result_stats.alliance_commanders };
    const int enemy[4] = { m_mission_result_stats.enemy_light, m_mission_result_stats.enemy_heavy,
        m_mission_result_stats.enemy_air, m_mission_result_stats.enemy_commanders };
    const int rowY[4] = { 53, 79, 106, 133 };
    for (int i = 0; i < 4; ++i)
    {
        drawText(labels[i], 116 + 18, 103 + rowY[i], 90, false);
        drawText(wxString::Format("%d", alliance[i]), 116 + 113, 103 + rowY[i], 132, true);
        drawText(wxString::Format("%d", enemy[i]), 116 + 246, 103 + rowY[i], 144, true);
    }
}

void MainFrame::StartMissionEndFlow()
{
    // 1) Pokud je video, přehraj ho a až pak pokračuj
    if (!m_mission_end_req.movie_path.empty())
    {

        // zavři případný seznam videí
        if (form_videos)
        {
            form_videos->Close();
            form_videos = NULL;
        }

        // přehraj cutscénu
        bool video_ok = false;
        if (!FindWindowById(ID_VIDEO_BOX_WIN))
        {
            try
            {
                form_video_box = new FormVideoBox(
                    canvas,
                    ID_VIDEO_BOX_WIN,
                    spell_data,
                    std::string(m_mission_end_req.movie_path.begin(), m_mission_end_req.movie_path.end()),
                    2
                );
                video_ok = true;
            }
            catch (const std::exception& ex)
            {
                wxLogMessage("StartMissionEndFlow: video failed: %s", ex.what());
                form_video_box = nullptr;
            }
        }

        if (video_ok)
        {
            g_cutscene_handled = false;
            canvas->Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnCutsceneClosed, this, ID_VIDEO_BOX_WIN);
            return;
        }

        // Video failed to load – fall through to strategic level
    }

    // 2) Bez videa – rovnou přejdi na strategickou
    OpenStrategicAndLoadNext();
}

void MainFrame::PlayCutsceneFromStrategic(const std::string& video_entry_name)
{
    if (video_entry_name.empty() || !spell_data)
        return;

    // Close existing video if any
    if (form_video_box)
    {
        delete form_video_box;
        form_video_box = nullptr;
    }

    try
    {
        form_video_box = new FormVideoBox(
            canvas,
            ID_VIDEO_BOX_WIN,
            spell_data,
            video_entry_name,
            2  // zoom
        );
    }
    catch (const std::exception& ex)
    {
        // Video not found or failed to load - log and continue
        wxLogMessage("PlayCutsceneFromStrategic: %s", ex.what());
    }
}


// Forward declarations for helpers defined later in this file
static std::string FindLevelDefContainingMission(const std::string& missionStem,
    const SpellData* spellData);

// map animation periodic refresh tick
void MainFrame::OnTimer(wxTimerEvent& event)
{
    if (!spell_map->IsLoaded())
        return;

    // Mission-end presentation must never be painted underneath a tactical
    // FormMsgBox/video.  Once that window is gone, reveal the native result
    // page on the very next timer tick and force an immediate repaint.
    if (m_mission_result_pending_show && !CheckMessageState())
        ShowMissionResultOverlay();

    // The original result table is modal over the finished battlefield. Freeze
    // tactical simulation until the player dismisses it, otherwise AI/cleanup
    // ticks could keep changing the just-reported mission state underneath it.
    if (m_mission_result_visible)
    {
        if (m_mission_result_input_guard > 0)
            --m_mission_result_input_guard;
        return;
    }

    if (spell_map->Tick())
        canvas->Refresh();

    // Mission-end flow (po ticku)
    // Note: do NOT require isGameMode() here — game mode may already be off
    // after all player units died (SetGameMode(0) runs after CheckAndTriggerMissionEnd).
    if (!m_mission_end_flow)
    {
        SpellMap::MissionEndRequest req;
        if (spell_map->ConsumeMissionEndRequest(req))
        {
            m_mission_end_flow = true;
            m_mission_end_req = req;

            // A strategic frame is usable only when it actually launched this
            // tactical mission.  Direct mission loads and New Game can otherwise
            // inherit an unrelated hidden level from a previous campaign load.
            if (m_strategicLevel && !StrategicContextMatchesLoadedMission(m_strategicLevel, spell_map))
            {
                wxLogDebug("[MISSION] Detaching stale strategic context before mission-end flow.");
                StrategicLevelFrame* stale = m_strategicLevel;
                m_strategicLevel = nullptr;
                stale->Hide();
                stale->Destroy();
            }

            // If no strategic level is active (e.g. first mission from main menu),
            // try to fill in movie_path and next_level_def from the parent LEVEL_XX.DEF.
            // This ensures the video plays before the strategic level opens.
            if (!m_strategicLevel && m_mission_end_req.movie_path.empty())
            {
                namespace fs = std::filesystem;
                std::string missionStem = fs::path(spell_map->map_path).stem().string();
                if (!missionStem.empty())
                {
                    std::string levelDefPath = FindLevelDefContainingMission(missionStem, spell_data);
                    if (!levelDefPath.empty())
                    {
                        LevelData currentLvl;
                        std::string err;
                        LevelLoader loader;
                        if (loader.LoadLevelDef(levelDefPath, currentLvl, &err))
                        {
                            // Set outro video as movie_path so StartMissionEndFlow() plays it
                            if (m_mission_end_req.success)
                            {
                                const std::string& preferredOutro =
                                    (!currentLvl.outro_can_video.empty() && currentLvl.outro_can_video != "none")
                                    ? currentLvl.outro_can_video : currentLvl.outro_video;
                                if (!preferredOutro.empty() && preferredOutro != "none")
                                    m_mission_end_req.movie_path = std::wstring(preferredOutro.begin(), preferredOutro.end());
                            }

                            // Set next_level_def so OpenStrategicAndLoadNext() can create the strategic level
                            if (m_mission_end_req.next_level_def.empty())
                            {
                                // Check mission-specific next_level_def first
                                std::string missionUpper = missionStem;
                                for (char& c : missionUpper) c = (char)std::toupper((unsigned char)c);
                                for (const auto& m : currentLvl.missions)
                                {
                                    std::string nameUp = m.name;
                                    for (char& c : nameUp) c = (char)std::toupper((unsigned char)c);
                                    if (nameUp == missionUpper && !m.next_level_def.empty() && m.next_level_def != "none")
                                    {
                                        m_mission_end_req.next_level_def = std::wstring(m.next_level_def.begin(), m.next_level_def.end());
                                        break;
                                    }
                                }

                                // Fall back to level-wide next_level_def
                                if (m_mission_end_req.next_level_def.empty() && !currentLvl.next_level_def.empty() && currentLvl.next_level_def != "none")
                                {
                                    m_mission_end_req.next_level_def = std::wstring(currentLvl.next_level_def.begin(), currentLvl.next_level_def.end());
                                }
                            }
                        }
                    }
                }
            }

            m_mission_result_pending_show = true;
            if (!CheckMessageState())
                ShowMissionResultOverlay();
        }
    }
}

//// on cutscene closed (after mission end)
//void MainFrame::OnCutsceneClosed(wxCloseEvent& ev)
//{
//    ev.Skip(); // neblokuj zavření
//
//    // pokračuj na strategickou
//    OpenStrategicAndLoadNext();
//}

void MainFrame::OnCutsceneClosed(wxCloseEvent& ev)
{
    if (g_cutscene_handled)
    {
        ev.Skip(false);
        return;
    }
    g_cutscene_handled = true;

    ev.Skip(false);

    // Odpoj handler – ať se po otevření LEVEL_02 nechytá další close eventy
    canvas->Unbind(wxEVT_CLOSE_WINDOW, &MainFrame::OnCutsceneClosed, this, ID_VIDEO_BOX_WIN);

    // Teprve teď smaž video box (destruktor udělá form->Destroy())
    if (form_video_box)
    {
        delete form_video_box;
        form_video_box = NULL;
    }

    OpenStrategicAndLoadNext();
}

static std::string find_def_candidate(const std::string& name, const SpellData* spellData)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path requested(name);
    if (fs::exists(requested, ec) && fs::is_regular_file(requested, ec))
        return fs::absolute(requested, ec).lexically_normal().string();

    const fs::path root = spellData ? fs::path(spellData->spell_data_root) : fs::path();
    const fs::path found = FindSpellDataFile(root, requested.filename().string());
    return found.empty() ? std::string() : found.string();
}

// Find the LEVEL_XX.DEF that contains a given mission name (e.g. "M01_01A").
// Scans LEVEL_01..LEVEL_99 in common locations.
// Returns the full path to the LEVEL DEF, or empty if not found.
static std::string FindLevelDefContainingMission(const std::string& missionStem,
    const SpellData* spellData)
{
    if (missionStem.empty())
        return {};

    namespace fs = std::filesystem;

    const std::string missionUpper = [&]() {
        std::string s = missionStem;
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }();

    // Search the configured installation first; FindSpellDataFile also keeps
    // the project-local temp/COMMON export as a development fallback.
    const fs::path root = spellData ? fs::path(spellData->spell_data_root) : fs::path();

    for (int lvl = 1; lvl <= 99; ++lvl)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "LEVEL_%02d.DEF", lvl);
        const std::string levelName(buf);

        const fs::path candidate = FindSpellDataFile(root, levelName);
        if (candidate.empty())
            continue;

        // Quick scan: load the LEVEL DEF and check if it references our mission
        LevelData lvlData;
        std::string err;
        LevelLoader loader;
        if (!loader.LoadLevelDef(candidate.string(), lvlData, &err))
            continue;

        for (const auto& m : lvlData.missions)
        {
            std::string nameUp = m.name;
            for (char& c : nameUp) c = (char)std::toupper((unsigned char)c);
            if (nameUp == missionUpper)
                return candidate.string();
        }

        // Also check territory mission tokens (they may reference the mission
        // without the trailing variant letter, e.g. "m01_01" -> "M01_01A")
        for (const auto& t : lvlData.territories)
        {
            std::string tokUp = t.mission;
            for (char& c : tokUp) c = (char)std::toupper((unsigned char)c);
            // Mission stem without variant letter
            if (!tokUp.empty() && missionUpper.rfind(tokUp, 0) == 0)
                return candidate.string();
        }
    }

    return {};
}

void MainFrame::OpenStrategicAndLoadNext()
{
    const std::wstring nextW = m_mission_end_req.next_level_def;

    // Return to an existing strategic level only when it is the frame that
    // launched the battle being resolved.  A hidden frame from a previously
    // loaded campaign must never hijack a standalone/direct-loaded mission.
    if (m_strategicLevel && !StrategicContextMatchesLoadedMission(m_strategicLevel, spell_map))
    {
        wxLogDebug("[MISSION] Ignoring stale strategic context while resolving mission result.");
        StrategicLevelFrame* stale = m_strategicLevel;
        m_strategicLevel = nullptr;
        stale->Hide();
        stale->Destroy();
    }

    // Return to existing strategic level with mission result
    if (m_strategicLevel)
    {
        // Pass mission result if we have pending mission info
        if (m_strategicLevel->m_pendingMission.valid)
        {
            m_strategicLevel->HandleMissionResult(
                m_strategicLevel->m_pendingMission.territory_id,
                m_mission_end_req.success,
                m_strategicLevel->m_pendingMission.mission_token
            );
        }
        // else: no pending mission to process

        // HandleMissionResult may have called AdvanceToNextLevel() which
        // replaced m_strategicLevel with a new window (already shown).
        // Show/Raise the current strategic level (whichever it is now).
        if (m_strategicLevel)
        {
            HideTacticalWindow();
            m_strategicLevel->Show();
            m_strategicLevel->Raise();
        }

        // Reset flow flag
        m_mission_end_flow = false;

        // Exit game mode UI (return to editor-like state for strategic view)
        // Note: strategic level handles its own game mode state
        return;
    }

    // Mission failed without strategic level (e.g. first mission from main menu)
    // → offer retry or return to main menu
    if (!m_mission_end_req.success)
    {
        m_mission_end_flow = false;
        m_mission_end_req = SpellMap::MissionEndRequest();

        int result = wxMessageBox(
            L"Mission failed!\nDo you want to retry the mission?",
            L"Mission Failed",
            wxYES_NO | wxICON_QUESTION,
            this
        );

        if (result == wxYES)
        {
            // Retry = same as NewGame from main menu
            wxCommandEvent dummy;
            OnMainMenuAction(FormMainMenuAction::NewGame);
        }
        else
        {
            // Show main menu over current state
            wxCommandEvent dummy;
            OnOpenMainMenu(dummy);
        }

        return;
    }

    // Fallback: no existing strategic level. Create one from next_level_def.
    // This path is used when a mission was launched outside the strategic UI
    // (e.g., direct map open, or first mission from main menu).
    // Note: movie_path and next_level_def may have been enriched by OnTimer()
    // from the parent LEVEL_XX.DEF before StartMissionEndFlow() was called.
    std::string nextDef = nextW.empty()
        ? std::string()
        : find_def_candidate(std::string(nextW.begin(), nextW.end()), spell_data);

    // If next_level_def is still empty, try to infer it from the current map's mission name.
    // This is a safety net in case the OnTimer enrichment didn't find it.
    if (nextDef.empty() && spell_map && spell_map->IsLoaded())
    {
        namespace fs = std::filesystem;
        std::string missionStem = fs::path(spell_map->map_path).stem().string();
        if (!missionStem.empty())
        {
            std::string levelDefPath = FindLevelDefContainingMission(missionStem, spell_data);
            if (!levelDefPath.empty())
            {
                LevelData currentLvl;
                std::string err;
                LevelLoader loader;
                if (loader.LoadLevelDef(levelDefPath, currentLvl, &err))
                {
                    // Determine next level DEF
                    std::string missionUpper = missionStem;
                    for (char& c : missionUpper) c = (char)std::toupper((unsigned char)c);
                    for (const auto& m : currentLvl.missions)
                    {
                        std::string nameUp = m.name;
                        for (char& c : nameUp) c = (char)std::toupper((unsigned char)c);
                        if (nameUp == missionUpper && !m.next_level_def.empty() && m.next_level_def != "none")
                        {
                            nextDef = find_def_candidate(m.next_level_def, spell_data);
                            break;
                        }
                    }

                    if (nextDef.empty() && !currentLvl.next_level_def.empty() && currentLvl.next_level_def != "none")
                    {
                        nextDef = find_def_candidate(currentLvl.next_level_def, spell_data);
                    }

                    // If still empty but success, open the current level's strategic view
                    if (nextDef.empty() && m_mission_end_req.success)
                    {
                        nextDef = levelDefPath;
                    }
                }
            }
        }
    }

    if (!nextDef.empty())
    {
        LevelData lvl;
        std::string err;
        LevelLoader loader;
        if (!loader.LoadLevelDef(nextDef, lvl, &err))
        {
            wxMessageBox("Failed to load next level DEF:\n" + err, "Error", wxOK | wxICON_ERROR, this);
        }
        else
        {
            // Carry mission-earned permanent companies into the first strategic
            // screen.  Do NOT infer this from unit type: the mission DEF itself
            // tells us the lifetime.  In M01_01A the rescued infantry are
            // MissionUnit (blue stripes, tactical-only), while the commando in
            // the palisades is ArmyUnit and therefore joins the permanent army.
            std::vector<LevelData::PlayerUnitAdd> bonus_units;
            if (spell_map && spell_map->IsLoaded())
            {
                for (auto* u : spell_map->units)
                {
                    if (!u || !u->unit || u->is_enemy || u->isDead())
                        continue;
                    if (u->strategic_uid != 0 || u->spec_type != MapUnitType::ArmyUnit)
                        continue;

                    LevelData::PlayerUnitAdd bonus;
                    bonus.unit_id = u->unit->type_id;
                    bonus.count = 1;
                    const int maxMan = u->unit->cnt;
                    bonus.health = maxMan > 0
                        ? std::max(1, (u->man * 100 + maxMan / 2) / maxMan)
                        : 100;
                    bonus.extra = u->name.empty() ? "-" : u->name;
                    bonus.experience = std::max(0, u->experience);
                    bonus.experience_level = std::clamp(u->experience_level > 0
                        ? u->experience_level : u->experience_init, 1, 12);
                    bonus_units.push_back(std::move(bonus));
                }
            }

            // Create strategic level with skipAutosave=true (no debug dialog)
            auto* win = new StrategicLevelFrame(this, lvl, true);
            m_strategicLevel = win;

            // Start fresh game mode directly (campaign progression)
            win->StartFreshGameMode(bonus_units);

            // The campaign-opening tactical mission can run before a strategic
            // frame exists. Preserve its casualties in the same global/level
            // statistics model once the first strategic frame is created.
            StrategicLevelFrame::LossBlock allianceLosses;
            allianceLosses.light = m_mission_result_stats.alliance_light;
            allianceLosses.heavy = m_mission_result_stats.alliance_heavy;
            allianceLosses.air = m_mission_result_stats.alliance_air;
            allianceLosses.commanders = m_mission_result_stats.alliance_commanders;
            StrategicLevelFrame::LossBlock enemyLosses;
            enemyLosses.light = m_mission_result_stats.enemy_light;
            enemyLosses.heavy = m_mission_result_stats.enemy_heavy;
            enemyLosses.air = m_mission_result_stats.enemy_air;
            enemyLosses.commanders = m_mission_result_stats.enemy_commanders;
            win->RecordStandaloneMissionStatistics(allianceLosses, enemyLosses, m_mission_end_req.success);

            HideTacticalWindow();
            win->Show();
            win->Raise();
        }
    }
    else
    {
        // No next_level_def and no strategic level
    }

    // reset flow flags
    m_mission_result_pending_show = false;
    m_mission_end_flow = false;
}


// on main panel resizing
void MainFrame::OnResize(wxSizeEvent& event)
{
    if(spell_map)
        spell_map->scroller.SetSurface(canvas->GetClientSize().GetWidth(),canvas->GetClientSize().GetHeight());
    Refresh();
}

// render canvas repaint event
void MainFrame::OnThreadCanvas(wxThreadEvent& event)
{
    canvas->Refresh();
}
void MainFrame::OnPaintCanvas(wxPaintEvent& event)
{       
    // make buffer
    if(!m_buffer.IsOk() || m_buffer.GetSize() != canvas->GetClientSize())
        m_buffer = wxBitmap(canvas->GetClientSize(),24);
    
    // render map    
    if(!spell_map->IsLoaded())
        canvas->ClearBackground();
    else
    {
        wxPaintDC pdc(canvas);
        /*int frames = 100;
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();        
        for(int k = 0; k < frames; k++)*/
            spell_map->Render(m_buffer,NULL,&spell_tool,bind(&MainFrame::CreateHUDbuttons,this));
        /*std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        float time = 1e-6*std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
        SetStatusText(wxString::Format(wxT("%.0f fps"),frames/time),7);*/
        pdc.DrawBitmap(m_buffer,wxPoint(0,0));
        if (m_mission_result_visible)
            DrawMissionResultOverlay(pdc);
    }
       

    event.Skip();
}


void MainFrame::CreateHUDbuttons()
{
    // mark all buttons as unused
    for(auto & pan : hud_buttons)
    {
        pan->SetClientData(0);
    }

    // create new ones
    int button_id = ID_HUD_BASE;
    for(auto & btn : *spell_map->GetHUDbuttons())
    {        
        if(!btn->IsValid())
            continue;

        // try to find existing button
        int skip = false;
        for(auto& pan : hud_buttons)
        {
            if(pan->GetPosition().x == btn->x_pos && pan->GetPosition().y == btn->y_pos)
            {
                btn->wx_id = button_id++;
                pan->SetId(btn->wx_id);
                pan->SetClientData((void*)1);
                pan->Refresh();
                skip = true;
                break;
            }
        }
        if(skip)
            continue;
        
        btn->wx_id = button_id++;
        wxPanel *wx_btn = new wxPanel(canvas,btn->wx_id,wxPoint(btn->x_pos, btn->y_pos),wxSize(btn->x_size, btn->y_size));
        wx_btn->SetClientData((void*)1);
        wx_btn->SetWindowStyle(wxTRANSPARENT_WINDOW);       
        wx_btn->SetBackgroundStyle(wxBG_STYLE_PAINT);
        wx_btn->SetDoubleBuffered(true);        
        wx_btn->Bind(wxEVT_PAINT,&MainFrame::OnPaintHUDbutton,this);
        wx_btn->Bind(wxEVT_LEAVE_WINDOW,&MainFrame::OnHUDbuttonsLeave,this);
        wx_btn->Bind(wxEVT_ENTER_WINDOW,&MainFrame::OnHUDbuttonsMouseEnter,this);
        wx_btn->Bind(wxEVT_LEFT_DOWN,&MainFrame::OnHUDbuttonsClick,this);
        wx_btn->Bind(wxEVT_LEFT_UP,&MainFrame::OnHUDbuttonsClick,this);
        hud_buttons.push_back(wx_btn);
    }

    // loose old unused buttons   
    for(int pid = hud_buttons.size()-1; pid >= 0; pid--)
    {
        if(!hud_buttons[pid]->GetClientData())
        {
            hud_buttons[pid]->Destroy();
            hud_buttons.erase(hud_buttons.begin() + pid);
        }
    }
}
void MainFrame::OnPaintHUDbutton(wxPaintEvent& event)
{
    wxPanel* pan = (wxPanel*)event.GetEventObject();
    auto* btn = spell_map->GetHUDbutton(pan->GetId());
    if(btn)
    {                
        wxPaintDC pdc(pan);        
        if(btn->is_press && !btn->is_disabled)
            pdc.DrawBitmap(*btn->bmp_press,wxPoint(0,0));
        else if(btn->is_hover && !btn->is_disabled)
            pdc.DrawBitmap(*btn->bmp_hover,wxPoint(0,0));
        else
            pdc.DrawBitmap(*btn->bmp_idle,wxPoint(0,0));
    }
    event.Skip();
}
void MainFrame::OnHUDbuttonsMouseEnter(wxMouseEvent& event)
{
    wxPanel* pan = (wxPanel*)event.GetEventObject();
    auto* btn = spell_map->GetHUDbutton(pan->GetId());
    if(btn)
    {
        btn->is_hover = true;
        pan->Refresh();
        // click event callback?
        if(btn->is_hover && btn->cb_hover)
            btn->cb_hover();
        if(!btn->is_disabled)
        {
            // play hover sound when optional SAMPLES.FS is available
            if (spell_data && spell_data->sounds && spell_data->sounds->aux_samples.btn_hover)
            {
                auto *hover_sound = new SpellSound(*spell_data->sounds->aux_samples.btn_hover);
                hover_sound->Play(true);
            }

            /*std::thread snd(&SpellSound::PlayAsync,hover_sound);
            snd.detach();*/

            //std::async(std::launch::async,&SpellSound::PlayAsync,hover_sound);
            
            /*chrono::steady_clock::time_point ref_time = std::chrono::high_resolution_clock::now();            
            hover_sound->Play(true);
            auto now_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now_time - ref_time).count();
            StatusStringCallback(string_format("%d",duration));*/
            
        }
    }

    // default game cursor
    SetCursor(*spell_data->gres.cur_pointer);
}
void MainFrame::OnHUDbuttonsLeave(wxMouseEvent& event)
{
    wxPanel* pan = (wxPanel*)event.GetEventObject();
    auto* btn = spell_map->GetHUDbutton(pan->GetId());
    if(btn)
    {
        btn->is_hover = false;
        pan->Refresh();
    }
}
void MainFrame::OnHUDbuttonsClick(wxMouseEvent& event)
{
    wxPanel* pan = (wxPanel*)event.GetEventObject();
    auto* btn = spell_map->GetHUDbutton(pan->GetId());
    if(btn && !btn->is_disabled)
    {
        btn->is_press = (event.GetEventType() == wxEVT_LEFT_DOWN);        
        pan->Refresh();
        // play click sound        
        if(!btn->is_press && !btn->is_disabled)
        {
            if (spell_data && spell_data->sounds && spell_data->sounds->aux_samples.btn_end_turn)
            {
                auto* click_sound = new SpellSound(*spell_data->sounds->aux_samples.btn_end_turn);
                click_sound->Play(true);
            }
        }
        // click event callback?
        if(!btn->is_press && btn->cb_press)
            btn->cb_press();
        if(btn->is_press)
            return;
        if(btn->action_id == SpellMap::HUD_ACTION_MINIMAP)
        {
            // show minimap
            wxCommandEvent cmd(wxEVT_MENU);
            OnViewMiniMap(cmd);
        }
        if(btn->action_id == SpellMap::HUD_ACTION_UNITS && !form_units_list)
        {
            // show units list            
            form_units_list = new FormMapUnits(canvas,ID_MAP_UNITS_WIN, spell_data, spell_map);
        }
        if(btn->action_id == SpellMap::HUD_ACTION_MAP_OPTIONS && !form_map_options)
        {
            // show map options
            form_map_options = new FormMapOptions(canvas,ID_MAP_OPT_WIN,spell_map);
        }
        if(btn->action_id == SpellMap::HUD_ACTION_RETREAT)
        {
            OnTacticalRetreat();
        }
    }
}

void MainFrame::OnTacticalRetreat()
{
    if (!spell_map || !spell_map->IsLoaded() || !spell_map->isGameMode())
        return;

    if (spell_map->start.empty())
    {
        wxMessageBox(
            "This mission has no starting cross squares, so a safe retreat cannot be resolved.",
            "Retreat", wxOK | wxICON_ERROR, this);
        return;
    }

    auto isStartSquare = [this](const MapXY& pos) -> bool
    {
        for (const auto& start : spell_map->start)
            if (start.x == pos.x && start.y == pos.y)
                return true;
        return false;
    };

    std::vector<MapUnit*> retreatLosses;
    std::map<std::string, int> lossNames;
    for (auto* unit : spell_map->units)
    {
        if (!unit || !unit->unit || unit->is_enemy || unit->isDead())
            continue;
        if (isStartSquare(unit->coor))
            continue;

        retreatLosses.push_back(unit);
        std::string name = !unit->name.empty() ? unit->name : std::string(unit->unit->name);
        if (name.empty())
            name = string_format("Unit #%d", unit->id);
        lossNames[name]++;
    }

    wxString message =
        "Do you really want to retreat?\n\n"
        "Only units standing on the starting cross squares will survive.";

    if (!lossNames.empty())
    {
        message += "\n\nUnits that will be lost:";
        for (const auto& entry : lossNames)
        {
            message += "\n  - " + wxString::FromUTF8(entry.first.c_str());
            if (entry.second > 1)
                message += wxString::Format(" x%d", entry.second);
        }
    }
    else
    {
        message += "\n\nAll surviving Alliance units are already on starting cross squares; "
                   "no additional units will be lost.";
    }

    wxMessageDialog confirm(this, message, "Retreat",
        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
    if (confirm.ShowModal() != wxID_YES)
        return;

    // Keep retreat casualties in the tactical unit list as dead units until
    // strategic result collection runs.  That preserves normal loss statistics
    // and lets the existing strategic UID synchronisation remove exactly the
    // companies that did not reach a starting cross square.
    for (auto* unit : retreatLosses)
    {
        unit->man = 0;
        unit->wounded = 0;
        unit->action_points = 0;
        unit->was_moved = true;
    }

    spell_map->SelectUnit(nullptr);
    spell_map->InvalidateUnitsView();

    // Use the existing mission-end/strategic return pipeline, but without the
    // generic tactical MISSION_FAILED message.
    if (!spell_map->RequestRetreatMissionEnd())
        return;

    if (canvas)
        canvas->Refresh();
}


// on change of map layer view
void MainFrame::OnViewLayer(wxCommandEvent& event)
{
    bool wL1 = GetMenuBar()->FindItem(ID_ViewTer)->IsChecked();
    bool wL2 = GetMenuBar()->FindItem(ID_ViewObj)->IsChecked();
    bool wL3 = GetMenuBar()->FindItem(ID_ViewAnm)->IsChecked();
    bool wL4 = GetMenuBar()->FindItem(ID_ViewPnm)->IsChecked();
    bool wL5 = GetMenuBar()->FindItem(ID_ViewUnt)->IsChecked();
    bool wSS = GetMenuBar()->FindItem(ID_ViewStTa)->IsChecked();
    bool wSound = GetMenuBar()->FindItem(ID_ViewSounds)->IsChecked();
    bool wSoundLoop = GetMenuBar()->FindItem(ID_ViewSoundLoops)->IsChecked();
    bool wEvents = GetMenuBar()->FindItem(ID_ViewEvents)->IsChecked();
    bool wHobj = GetMenuBar()->FindItem(ID_HighlighObj)->IsChecked();
    spell_map->SetRender(wL1,wL2,wL3,wL4,wSS,wL5,wSound,wSoundLoop,wEvents,wHobj);
    bool hud = GetMenuBar()->FindItem(ID_ViewHUD)->IsChecked();
    spell_map->SetHUDstate(hud);
    Refresh();
}

// enable disable unit view debug mode
void MainFrame::OnUnitViewDebug(wxCommandEvent& event)
{    
    if(!spell_map->IsLoaded())
        return;

    spell_map->SetUnitsViewDebugMode(GetMenuBar()->FindItem(ID_UnitViewDbg)->IsChecked());
    auto unit = spell_map->GetSelectedUnit();
    spell_map->unit_view->AddUnitView(unit,
        spell_map->isUnitsViewDebugMode()?(SpellMap::ViewRange::ClearMode::HIDE):(SpellMap::ViewRange::ClearMode::NONE));
}

// open new map
void MainFrame::OnOpenMap(wxCommandEvent& event)
{
    // split path to folder and file
    std::filesystem::path last_path = spell_map->GetTopPath();
    wstring dir;
    wstring name;
    if(!last_path.empty() && std::filesystem::exists(last_path.parent_path()))
    {
        dir = last_path.parent_path(); dir += wstring(L"\\");
        name = last_path.filename();
    }
    else
    {
        dir = (std::filesystem::current_path() / L"temp").wstring();
    }

    // show open dialog
    wxFileDialog openFileDialog(this,_("Open Spellcross Map File"),dir,name,"Map script file (*.def)|*.def|Map data file (*.dta)|*.dta",
        wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if(openFileDialog.ShowModal() == wxID_CANCEL)
        return;
    wstring path = wstring(openFileDialog.GetPath().ToStdWstring());

    // load new one
    if(spell_map->Load(path, spell_data))
        wxMessageBox(string_format("Loading Spellcross map file failed with error:\n%s",spell_map->GetLastError().c_str()),"Error",wxICON_ERROR);
    else
        SetTitle(BuildSpellcrossWindowTitle(spell_map));
    // reset layers visibility
    spell_map->SetGamma(1.30);
    OnViewLayer(event);
    // reload toolset ribbon
    LoadToolsetRibbon();
    // repaint
    Refresh();
}

// save map data files
void MainFrame::OnSaveMap(wxCommandEvent& event)
{
    if(!spell_map || !spell_map->IsLoaded())
        return;

    OnSaveDTA(event);
    OnSaveDEF(event);    
}

// save map DTA file
void MainFrame::OnSaveDTA(wxCommandEvent& event)
{
    if(!spell_map || !spell_map->IsLoaded())
        return;

    // split path to folder and file    
    std::filesystem::path last_path = spell_map->map_path;
    wstring dir;
    wstring name;
    if(!last_path.empty() && std::filesystem::exists(last_path.parent_path()))
    {
        dir = last_path.parent_path(); dir += wstring(L"\\");
        name = last_path.filename();
    }
    else
    {
        dir = (std::filesystem::current_path() / L"temp").wstring();
    }

    // show open dialog
    wxFileDialog saveFileDialog(this,_("Save Spellcross Map DTA file"),dir,name,"Map data file (*.dta)|*.dta",
        wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if(saveFileDialog.ShowModal() == wxID_CANCEL)
        return;
    wstring path = wstring(saveFileDialog.GetPath().ToStdWstring());

    if(spell_map->SaveDTA(path))
        wxMessageBox(string_format("Saving Spellcross map DTA file failed with error:\n%s",spell_map->GetLastError().c_str()),"Error",wxICON_ERROR);
}

// save map DEF file
void MainFrame::OnSaveDEF(wxCommandEvent& event)
{
    if(!spell_map || !spell_map->IsLoaded())
        return;

    // split path to folder and file    
    std::filesystem::path last_path = spell_map->def_path;
    wstring dir;
    wstring name;
    if(!last_path.empty() && std::filesystem::exists(last_path.parent_path()))
    {
        dir = last_path.parent_path(); dir += wstring(L"\\");
        name = last_path.filename();
    }
    else
    {
        dir = (std::filesystem::current_path() / L"temp").wstring();
    }

    // show open dialog
    wxFileDialog saveFileDialog(this,_("Save Spellcross Map DEF file"),dir,name,"Map script file (*.def)|*.def",
        wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if(saveFileDialog.ShowModal() == wxID_CANCEL)
        return;
    wstring path = wstring(saveFileDialog.GetPath().ToStdWstring());

    if(spell_map->SaveDEF(path))
        wxMessageBox(string_format("Saving Spellcross map DEF file failed with error:\n%s",spell_map->GetLastError().c_str()),"Error",wxICON_ERROR);
}

// create new map
void MainFrame::OnNewMap(wxCommandEvent& event)
{
    // create some map (###todo: set parameters by some menu)
    spell_map->Create(spell_data, "T11", 20,50);
    SetTitle(BuildSpellcrossWindowTitle(spell_map));
    // reset layers visibility
    spell_map->SetGamma(1.30);
    OnViewLayer(event);
}

// set gamma correction
void MainFrame::OnSetGamma(wxCommandEvent& event)
{
    if(!FindWindowById(ID_GAMMA_WIN))
    {
        form_gamma = new FormGamma(this,spell_map,ID_GAMMA_WIN);
        form_gamma->Show();
    } 
}

// open sprite viewer
void MainFrame::OnViewSprites(wxCommandEvent& event)
{    
    if(!FindWindowById(ID_SPRITES_WIN))
    {
        form_sprites = new FormSprite(this, spell_data, ID_SPRITES_WIN);
        if(spell_map)
            form_sprites->SetSprite(spell_map->terrain);
        form_sprites->Show();
    }
}

// open ANM viewer
void MainFrame::OnViewAnms(wxCommandEvent& event)
{
    if(!FindWindowById(ID_ANM_WIN))
    {
        form_anms = new FormANM(this,spell_data,false,ID_ANM_WIN);
        form_anms->Show();
    }
}

// open PNM viewer
void MainFrame::OnViewPnms(wxCommandEvent& event)
{
    if(!FindWindowById(ID_ANM_WIN))
    {
        form_anms = new FormANM(this,spell_data,true,ID_ANM_WIN);
        form_anms->Show();
    }
}

// open sounds viewer
void MainFrame::OnViewSounds(wxCommandEvent& event)
{
    if(!FindWindowById(ID_SOUNDS_WIN))
    {
        form_sounds = new FormSound(this,spell_data,ID_SOUNDS_WIN);
        form_sounds->Show();
    }
}

// update tiles context from map selection
void MainFrame::OnUpdateTileContext(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;

    spell_map->BuildSpriteContext();
}

// generate DMAx_xxx tiles objects from this map
void MainFrame::OnGenDMAobjects(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    spell_map->BuildHouseObjects();
    LoadToolsetRibbon();
}

// generate DMAx_xxx tiles objects from all maps
void MainFrame::OnGenDMAobjectsMaps(wxCommandEvent& event)
{
    FormTerrain form(this,spell_data,ID_TARRAIN_WIN);
    if(form.ShowModal() != wxID_OK)
        return;
    // --- confirmed
    auto terr_name = form.GetTerrain();
        
    // split path to folder and file
    wstring dir = spell_data->spell_data_root + L"\\DATA\\COMMON\\";

    // show open dialog
    wxDirDialog fd = wxDirDialog(this,_("Select Spellcross COMMON folder"),dir,wxDD_DIR_MUST_EXIST);
    if(fd.ShowModal() == wxID_CANCEL)
        return;
    wstring path = wstring(fd.GetPath().ToStdWstring());

    // load map context
    spell_data->BuildHouseObjectsOfMaps(path,terr_name,bind(&MainFrame::StatusStringCallback,this,placeholders::_1));

    LoadToolsetRibbon();
}

// update tiles context from all maps
void MainFrame::OnUpdateTileContextMaps(wxCommandEvent& event)
{   
    FormTerrain form(this,spell_data,ID_TARRAIN_WIN);
    if(form.ShowModal() != wxID_OK)
        return;
    // --- confirmed
    auto terr_name = form.GetTerrain();

    // split path to folder and file
    wstring dir = spell_data->spell_data_root + L"\\DATA\\COMMON\\";

    // show open dialog
    wxDirDialog fd = wxDirDialog(this, _("Select Spellcross COMMON folder"), dir, wxDD_DIR_MUST_EXIST);
    if(fd.ShowModal() == wxID_CANCEL)
        return;
    wstring path = wstring(fd.GetPath().ToStdWstring());
    
    // load map context
    spell_data->BuildSpriteContextOfMaps(path,terr_name, bind(&MainFrame::StatusStringCallback,this,placeholders::_1));
}


// open objects viewer
void MainFrame::OnViewObjects(wxCommandEvent& event)
{
    if(!FindWindowById(ID_OBJECTS_WIN))
    {
        form_objects = new FormObjects(this,spell_data,ID_OBJECTS_WIN);
        //form_objects->Connect(wxID_ANY,wxEVT_DESTROY,(wxObjectEventFunction)&MainFrame::OnViewObjectsClose);
        form_objects->SetMap(spell_map);
        form_objects->Show();
    }
}

// open palette viewer
void MainFrame::OnViewPal(wxCommandEvent& event)
{
    if(!FindWindowById(ID_PAL_WIN))
    {
        form_pal = new FormPalView(this,spell_data,ID_PAL_WIN);
        form_pal->SetMap(spell_map);
        form_pal->Show();
    }
}

// open graphics viewer
void MainFrame::OnViewGrRes(wxCommandEvent& event)
{
    if(!FindWindowById(ID_GRES_WIN))
    {
        form_gres = new FormGResView(this,spell_data,ID_GRES_WIN);
        form_gres->Show();
    }
}

// open units viewer/editor
void MainFrame::OnEditUnit(wxCommandEvent& event)
{
    if(!FindWindowById(ID_UNITS_WIN))
    {
        form_units = new FormUnits(this,ID_UNITS_WIN);
        form_units->SetSpellData(spell_data);
        form_units->SetMapUnit(spell_map->GetSelectedUnit());
        form_units->Show();
    }
}

// open events viewer/editor
void MainFrame::OnEditEvent(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    if(!FindWindowById(ID_EVENT_WIN))
    {
        form_events = new FormEvent(this,spell_data,ID_EVENT_WIN);
        form_events->SetMap(spell_map);
        form_events->Show();
    }
}

// open video viewer
void MainFrame::OnViewVideo(wxCommandEvent& event)
{
    /*if(!FindWindowById(ID_VIDEO_BOX_WIN))
    {
        form_video_box = new FormVideoBox(canvas,ID_VIDEO_BOX_WIN,spell_data,"LEVEL10.DPK");
    }*/
    if(!FindWindowById(ID_VIDEO_WIN))
    {
        form_videos = new FormVideo(this,spell_data,ID_VIDEO_WIN);
        form_videos->Show();
    }
}

// open MIDI player
void MainFrame::OnViewMidi(wxCommandEvent& event)
{
    if(!FindWindowById(ID_MIDI_WIN))
    {
        form_midi = new FormMIDI(this,spell_data,ID_MIDI_WIN);
        form_midi->Show();
    }
}

// tile flags editor
void MainFrame::OnTileFlags(wxCommandEvent& event)
{
    if(!spell_map || !spell_map->IsLoaded())
        return;
    auto flags = spell_map->GetFlags(spell_map->GetSelection());
    auto form = new FormFlags(this,spell_map->terrain,flags);
    if(form->ShowModal() == wxID_OK)
    {
        // --- confirmed
        flags = form->GetSelectedFlags();
        
        // set to all selected tiles
        auto posz = spell_map->GetSelections();
        spell_map->SetFlags(posz, flags);
    }
    delete form;
}





// export voxel map elevation raster
void MainFrame::OnViewVoxZ(wxCommandEvent& event)
{    
    if(!spell_map->IsLoaded())
        return;

    wxBitmap* bmp = spell_map->unit_view->ExportUnitsViewZmap();
    if(event.GetId() == ID_ViewVoxZ)
    {
        // view only:
        auto [map_x,map_y] = spell_map->GetMapSurfaceSize();
        
        // ceate panel
        TMiniMap minimap ={bmp, &spell_map->scroller, spell_map, 0, 0, map_x, map_y};
        form_minimap = new FormMiniMap(canvas,ID_MINIMAP_WIN,spell_data,minimap);
    }
    else if(event.GetId() == ID_ExportVoxZ)
    {
        // export to file:
        // 
        // split path to folder and file
        std::filesystem::path last_path = wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
        wstring dir = last_path.parent_path(); dir += wstring(L"\\");
        wstring name = last_path.filename();

        // show open dialog
        wxFileDialog saveFileDialog(this,_("Save voxel map elevation"),dir,name,"PNG file (*.png)|*.png",wxFD_SAVE);
        if(saveFileDialog.ShowModal() == wxID_CANCEL)
            return;
        wstring path = wstring(saveFileDialog.GetPath().ToStdWstring());

        // expor as PNG
        bmp->SaveFile(path,wxBITMAP_TYPE_PNG);
        delete bmp;
    }
    
}

// view minimap
void MainFrame::OnViewMiniMap(wxCommandEvent& event)
{
    if (!spell_map || !spell_map->IsLoaded())
        return;

    // TOGGLE: pokud minimapa existuje, zavøi ji stejným zpùsobem jako jinde v kódu
    // (tj. pøes wxCloseEvent s ID_MINIMAP_WIN, aby se spustil správný cleanup:
    //  delete form_minimap; form_minimap = NULL;)
    if (form_minimap)
    {
        if (form_minimap->form)
        {
            wxCloseEvent evt(wxEVT_CLOSE_WINDOW, ID_MINIMAP_WIN);
            wxQueueEvent(form_minimap->form, new wxCloseEvent(evt));
        }
        else
        {
            // fallback: kdyby wrapper existoval, ale okno už ne
            delete form_minimap;
            form_minimap = NULL;
        }
        return;
    }

    // make local scroll object with zero scroll state
    TScroll scrl;
    scrl.Reset();

    // obtain render surface range
    auto [pic_x, pic_y] = spell_map->GetMapSurfaceSize();
    int hud_state = spell_map->SetHUDstate(false);

    // NOTE: tenhle 1x1 bitmap "buf" se tu jen vytváøí a hned maže,
    // ale render prepare ho reálnì nepoužívá (RenderPrepare bere scroll, ne bitmap).
    // Nechávám to tak, aby se nerozbilo nic navázaného.
    wxBitmap* buf = new wxBitmap(1, 1, 24);

    scrl.SetPos(0, 0);
    scrl.SetSurface(1, 1);
    spell_map->RenderPrepare(&scrl);
    auto [x1, y1] = scrl.GetScroll();

    scrl.SetPos(pic_x, pic_y);
    spell_map->RenderPrepare(&scrl);
    auto [x2, y2] = scrl.GetScroll();

    delete buf;

    // make local render buffer for entire map size
    buf = new wxBitmap(x2 - x1, y2 - y1, 24);
    scrl.SetSurface(x2 - x1, y2 - y1);
    scrl.SetPos(0, 0);

    spell_map->Render(*buf, &scrl);
    spell_map->SetHUDstate(hud_state);

    // restore scroll surface to canvas size
    scrl.SetSurface(canvas->GetClientSize().GetWidth(), canvas->GetClientSize().GetHeight());

    // create panel
    TMiniMap minimap = { buf, &spell_map->scroller, spell_map, x1, y1, x2 - x1, y2 - y1 };
    form_minimap = new FormMiniMap(canvas, ID_MINIMAP_WIN, spell_data, minimap);
}


// edit mission parameters
void MainFrame::OnEditMissionParams(wxCommandEvent& event)
{
    if(!spell_data || !spell_map)
        return;
    FormMissionParams* form = new FormMissionParams(this,spell_data,spell_map);
    if(form->ShowModal() == wxID_OK)
    {
        // --- confirmed
    }

    // destroy form
    delete form;
}


// create new object
void MainFrame::OnCreateNewObject(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;

    FormNewObject form(this,spell_map->terrain);
    if(form.ShowModal() == wxID_OK)
    {
        // --- confirmed

        // get object descriptions
        std::string description = form.GetDescription();
        int class_id = form.GetClass();

        // get layers mask
        SpellMap::Layers lay;
        lay.lay1 = GetMenuBar()->FindItem(ID_SelectLay1)->IsChecked();
        lay.lay2 = GetMenuBar()->FindItem(ID_SelectLay2)->IsChecked();
        lay.anm = GetMenuBar()->FindItem(ID_SelectLayANM)->IsChecked();
        lay.pnm = GetMenuBar()->FindItem(ID_SelectLayPNM)->IsChecked();

        auto posxy = spell_map->GetPersistSelections();
        auto L1_list = spell_map->GetL1sprites(posxy);
        auto L2_list = spell_map->GetL2sprites(posxy);
        auto flag_list = spell_map->GetFlags(posxy);
        auto pnm_list = spell_map->GetPNMs(posxy);
        if(!lay.lay1)
            for(auto& spr: L1_list)
                spr = NULL;
        if(!lay.lay2)
            for(auto& spr: L2_list)
                spr = NULL;
        if(!lay.pnm)
            pnm_list.clear();
        
        // add object to list
        auto obj =spell_map->terrain->AddObject(posxy,L1_list,L2_list,flag_list,pnm_list,(uint8_t*)spell_map->terrain->pal,description);
        obj->SetToolClass(class_id);
                
        // clear selection
        spell_map->SelectTiles(SpellMap::SELECT_CLEAR);

        // refresh tools list
        LoadToolsetRibbon(spell_map->terrain);
    }
    
    // destroy form
    //delete form;    
}

// add new unit
void MainFrame::OnAddUnit(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;

    if(!FindWindowById(ID_UNITS_WIN))
    {
        // make new unit        
        /*MapUnit *new_unit = spell_map->CreateUnit();
        spell_map->SelectUnit(new_unit);
        new_unit->in_placement = true;
        new_unit->is_active = true;*/               

        form_units = new FormUnits(this,ID_UNITS_WIN);
        form_units->SetSpellData(spell_data);
        form_units->SetMapUnit(NULL, spell_map);
        form_units->Show();
    }


    /*auto *unit = spell_map->GetSelectedUnit();
    if(unit)
    {
        // start unit movement
        unit->in_placement = true;
    }*/
    
}



// unit popup menu
void MainFrame::OnCanvasPopupSelect(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;

    auto menu_id = event.GetId();
    auto menu = (wxMenu*)event.GetEventObject();
    auto cur_unit = (MapUnit*)menu->GetClientData();
    if(menu_id >= ID_POP_ADD_MISSIONSTART && menu_id <= ID_POP_ADD_MISSIONSTART_MAX)
    {
        // try add unit to MissionStart() event
        int probab = menu_id - ID_POP_ADD_MISSIONSTART;
        spell_map->ExtractUnit(cur_unit);
        spell_map->events->AddMissionStartUnit(cur_unit,probab);
        spell_map->SortUnits();
    }
    else if(menu_id == ID_POP_REM_MISSIONSTART)
    {
        // try remove the unit from MissionStart() event
        cur_unit->creator_event->ExtractUnit(cur_unit);
        spell_map->AddUnit(cur_unit);
        spell_map->SortUnits();
    }
    else if(menu_id == ID_POP_ADD_SEEUNIT)
    {
        // try add SeeUnit() event
        spell_map->events->AddSeeUnitEvent(cur_unit);
        spell_map->SortUnits();
    }
    else if(menu_id == ID_POP_ADD_DESTROY_UNIT)
    {
        // try add DestroyUnit() objective
        spell_map->events->AddUnitObjective(cur_unit,SpellMapEventRec::EvtTypes::EVT_DESTROY_UNIT);
        spell_map->SortUnits();
    }
    else if(menu_id == ID_POP_ADD_SAVE_UNIT)
    {
        // try add SaveUnit() objective
        spell_map->events->AddUnitObjective(cur_unit,SpellMapEventRec::EvtTypes::EVT_SAVE_UNIT);
        spell_map->SortUnits();
    }
    else if(menu_id == ID_POP_ADD_TRANSPORT_UNIT)
    {
        // try add TransportUnit() objective
        spell_map->events->AddUnitObjective(cur_unit,SpellMapEventRec::EvtTypes::EVT_TRANSPORT_UNIT);
        spell_map->SortUnits();
    }
    else if(menu_id == ID_POP_REM_SEEUNIT || menu_id == ID_POP_REM_DESTROY_UNIT || menu_id == ID_POP_REM_SAVE_UNIT || menu_id == ID_POP_REM_TRANSPORT_UNIT)
    {
        // try remove xxxUnit() events
        std::map<int,SpellMapEventRec::EvtTypes> types = {{ID_POP_REM_SEEUNIT,SpellMapEventRec::EvtTypes::EVT_SEE_UNIT},{ID_POP_REM_DESTROY_UNIT,SpellMapEventRec::EvtTypes::EVT_DESTROY_UNIT},{ID_POP_REM_SAVE_UNIT,SpellMapEventRec::EvtTypes::EVT_SAVE_UNIT},{ID_POP_REM_TRANSPORT_UNIT,SpellMapEventRec::EvtTypes::EVT_TRANSPORT_UNIT}};
        auto evt = cur_unit->GetTrigEvent(types.at(menu_id));
        spell_map->events->EraseEvent(evt);
    }
    else if(menu_id == ID_POP_ADD_SEE_PLACE)
    {
        // try create SeePlace event        
        spell_map->events->AddSeePlaceEvent(spell_pos);
    }
    else if(menu_id == ID_POP_REM_SEE_PLACE)
    {
        // try remove SeePlace event        
        auto evt = spell_map->events->CheckEvent(SpellMapEventRec::EvtTypes::EVT_SEE_PLACE,&spell_pos);
        spell_map->events->EraseEvent(evt);
    }
    else if(menu_id == ID_POP_ADD_SPAWN_UNIT)
    {
        // try add/remove unit to event
        auto cur_evt = spell_map->GetSelectEvent();
        spell_map->UpdateEventUnit(cur_evt,cur_unit);
    }
    else if(menu_id == ID_POP_EDIT_EVENT)
    {
        // edit event        
        auto cur_evt = spell_map->GetSelectEvent();
        //spell_map->SelectEvent(cur_evt);
        OnEditEvent(event);
    }
    else if(menu_id == ID_POP_ANOTHER_EVENT)
    {
        // switch to another event at position
        auto cur_evt = spell_map->GetSelectEvent();
        spell_map->SelectEvent(spell_map->events->GetAnotherEvent(cur_evt));        
    }
    else if(menu_id == ID_POP_EDIT_UNIT)
    {
        // edit unit
        spell_map->SelectUnit(cur_unit);
        OnEditUnit(event);
    }
    else if(menu_id == ID_POP_REM_UNIT)
    {
        // remove unit
        spell_map->RemoveUnit(cur_unit,true);
    }
    else if (menu_id == ID_POP_ADD_UNIT)
    {
        // stejné chování jako Shift+Left Click: otevøe editor a po zavøení se jednotka pøidá na selection
        wxCommandEvent cmd;
        OnAddUnit(cmd);
    }
    else if(menu_id == ID_POP_REM_OBJ)
    {
        // remove object tile        
        spell_map->RemoveObj();
        Refresh();
    }
    else if(menu_id == ID_POP_EDIT_TERR)
    {
        // edit terrain tile
        if(!FindWindowById(ID_SPRITES_WIN))
        {
            spell_pos = spell_map->GetSelection();
            auto tile = spell_map->GetTile();
            if(tile)
            {
                form_sprites = new FormSprite(this,spell_data,ID_SPRITES_WIN);
                form_sprites->SetSprite(spell_map->terrain,tile->L1);
                form_sprites->Show();
            }
        }
    }
    else if(menu_id == ID_POP_EDIT_OBJ)
    {
        // edit obj tile
        if(!FindWindowById(ID_SPRITES_WIN))
        {
            spell_pos = spell_map->GetSelection();
            form_sprites = new FormSprite(this,spell_data,ID_SPRITES_WIN);
            auto spr = spell_map->CheckObj();
            form_sprites->SetSprite(spell_map->terrain,spr->L2);
            form_sprites->Show();
        }
    }
    else if(menu_id == ID_POP_REM_ANM)
    {
        // remove ANM tile
        spell_map->RemoveANM();
    }
    else if(menu_id == ID_POP_EDIT_ANM)
    {
        // edit ANM tile
        if(!FindWindowById(ID_ANM_WIN))
        {
            spell_pos = spell_map->GetSelection();
            form_anms = new FormANM(this,spell_data,false,ID_ANM_WIN);
            form_anms->SetANM(spell_map->terrain, spell_map->CheckANM()->anim);
            form_anms->Show();
        }
    }
    else if(menu_id == ID_POP_REM_PNM)
    {
        // remove PNM animation
        spell_map->RemovePNM();
    }
    else if(menu_id == ID_POP_EDIT_PNM)
    {
        // edit PNM animation
        if(!FindWindowById(ID_ANM_WIN))
        {
            spell_pos = spell_map->GetSelection();
            auto pnm = spell_map->CheckPNM();
            if(pnm)
            {
                form_anms = new FormANM(this,spell_data,true,ID_ANM_WIN);
                form_anms->SetPNM(spell_map->terrain,pnm->anim,pnm->x_ofs,pnm->y_ofs);
                form_anms->Show();
            }
        }
    }
    else if(menu_id == ID_POP_REM_SOUND)
    {
        // remove sound
        spell_map->SoundRemove();        
    }
    else if(menu_id == ID_POP_EDIT_SOUND)
    {
        // edit sound
        if(!FindWindowById(ID_SOUNDS_WIN))
        {            
            spell_pos = spell_map->GetSelection();
            form_sounds = new FormSound(this,spell_data,ID_SOUNDS_WIN);
            auto snd = spell_map->CheckSound();
            if(snd)
                form_sounds->SetSound(snd->GetName(), snd->GetType());
            form_sounds->Show();
        }
    }
}



// --- scrolling control ---
void MainFrame::OnCanvasRMouse(wxMouseEvent& event)
{    
    if(!spell_map->IsLoaded())
        return;
    if(inUnitOptions())
        return;
    
    if(event.RightDown())
        spell_map->scroller.SetRef(event.GetX(), event.GetY());
    else if(event.RightUp())
    {
        int was_moved = spell_map->scroller.Idle();
        if(!was_moved && !spell_map->isGameMode())
        {
            // --- editor mode popup menu stuff:
            auto cur_unit = spell_map->GetCursorUnit();
            auto cur_evt = spell_map->GetCursorEvent();
            auto sel_evt = spell_map->GetSelectEvent();
            auto cur_pos = spell_map->GetSelection();
            spell_pos = cur_pos;

            int wSounds = GetMenuBar()->FindItem(ID_ViewSounds)->IsChecked(); // ###todo: optimize?
            int wSoundLoops = GetMenuBar()->FindItem(ID_ViewSoundLoops)->IsChecked(); // ###todo: optimize?
            bool wSound = wSounds || wSoundLoops;
            MapSound::SoundType snd_type = MapSound::SoundType::BOTH;
            if(wSounds && wSoundLoops)
                snd_type = MapSound::SoundType::BOTH;
            else if(wSounds)
                snd_type = MapSound::SoundType::RANDOM;
            else if(wSoundLoops)
                snd_type = MapSound::SoundType::LOOP;

            wxMenu menu;
            menu.SetClientData(cur_unit);
            // Add unit on empty tile
            if (!cur_unit)
            {
                menu.Append(ID_POP_ADD_UNIT, "Add unit...\tShift+Left Click");
            }

            if(sel_evt && sel_evt->GetPosition() == cur_pos && spell_map->events->GetEventsCount(cur_pos) > 1)
            {
                menu.Append(ID_POP_ANOTHER_EVENT,"Switch to other event");
            }
            if((cur_unit && cur_unit->creator_event && cur_unit->creator_event->isMissionStart()) || cur_evt)
            {
                menu.Append(ID_POP_EDIT_EVENT,"Edit event");
            }
            if(cur_unit && cur_unit->creator_event && cur_unit->creator_event->isMissionStart())
            {                
                menu.Append(ID_POP_REM_MISSIONSTART,"Remove unit from MissionStart event");
            }
            if(cur_unit && (!cur_unit->creator_event || !cur_unit->creator_event->isMissionStart()))
            {
                auto list = spell_map->events->GetMissionStartEvent();
                if(list.size() > 1)
                {
                    wxMenu *sub_menu = new wxMenu();
                    sub_menu->SetClientData(cur_unit);
                    for(auto &evt: list)
                        sub_menu->Append(ID_POP_ADD_MISSIONSTART + evt->probability,string_format("MissionStart (p=%d%%)",evt->probability));
                    menu.AppendSubMenu(sub_menu,"Add unit to MissionStart event");
                }
                else
                {
                    menu.Append(ID_POP_ADD_MISSIONSTART+100,"Add unit to MissionStart event");
                }
            }
            if(cur_unit && !cur_unit->GetTrigEvent({SpellMapEventRec::EvtTypes::EVT_SAVE_UNIT,SpellMapEventRec::EvtTypes::EVT_TRANSPORT_UNIT,SpellMapEventRec::EvtTypes::EVT_DESTROY_UNIT}))
            {
                menu.Append(ID_POP_ADD_SAVE_UNIT,"Create SaveUnit objective");
                menu.Append(ID_POP_ADD_TRANSPORT_UNIT,"Create TransportUnit objective");
                menu.Append(ID_POP_ADD_DESTROY_UNIT,"Create DestroyUnit objective");
            }
            if(cur_unit && cur_unit->GetTrigEvent(SpellMapEventRec::EvtTypes::EVT_SAVE_UNIT))
            {
                menu.Append(ID_POP_REM_SAVE_UNIT,"Remove SaveUnit objective");
            }
            if(cur_unit && cur_unit->GetTrigEvent(SpellMapEventRec::EvtTypes::EVT_TRANSPORT_UNIT))
            {
                menu.Append(ID_POP_REM_TRANSPORT_UNIT,"Remove TransportUnit objective");
            }
            if(cur_unit && cur_unit->GetTrigEvent(SpellMapEventRec::EvtTypes::EVT_DESTROY_UNIT))
            {
                menu.Append(ID_POP_REM_DESTROY_UNIT,"Remove DestroyUnit objective");
            }
            if(cur_unit && !cur_unit->GetTrigEvent(SpellMapEventRec::EvtTypes::EVT_SEE_UNIT))
            {
                menu.Append(ID_POP_ADD_SEEUNIT,"Create SeeUnit event");
            }
            if(cur_unit && cur_unit->GetTrigEvent(SpellMapEventRec::EvtTypes::EVT_SEE_UNIT))
            {
                menu.Append(ID_POP_REM_SEEUNIT,"Remove SeeUnit event");
            }
            if(!spell_map->events->CheckEvent(SpellMapEventRec::EvtTypes::EVT_SEE_PLACE,&cur_pos))
            {
                menu.Append(ID_POP_ADD_SEE_PLACE,"Create SeePlace event");
            }
            if(spell_map->events->CheckEvent(SpellMapEventRec::EvtTypes::EVT_SEE_PLACE,&cur_pos))
            {
                menu.Append(ID_POP_REM_SEE_PLACE,"Remove SeePlace event");
            }
            if(cur_unit && sel_evt)
            {
                menu.Append(ID_POP_ADD_SPAWN_UNIT,"Add/remove unit to/from event spawn units list\tCtrl+Left Click");
            }
            if(cur_unit)
            {
                if(menu.GetMenuItemCount())
                    menu.AppendSeparator();
                menu.Append(ID_POP_EDIT_UNIT,"Edit unit");
                menu.Append(ID_POP_REM_UNIT,"Remove unit");
            }            
            if(GetMenuBar()->FindItem(ID_ViewTer)->IsChecked())
            {
                if(menu.GetMenuItemCount())
                    menu.AppendSeparator();
                menu.Append(ID_POP_EDIT_TERR,"Edit terrain sprite");
            }
            if(spell_map->CheckObj() && GetMenuBar()->FindItem(ID_ViewObj)->IsChecked())
            {
                if(menu.GetMenuItemCount())
                    menu.AppendSeparator();
                menu.Append(ID_POP_EDIT_OBJ,"Edit object");
                menu.Append(ID_POP_REM_OBJ,"Remove object");
            }
            if(spell_map->CheckANM() && GetMenuBar()->FindItem(ID_ViewAnm)->IsChecked())
            {
                if(menu.GetMenuItemCount())
                    menu.AppendSeparator();
                menu.Append(ID_POP_EDIT_ANM,"Edit ANM tile");
                menu.Append(ID_POP_REM_ANM,"Remove ANM tile");
            }
            if(spell_map->CheckPNM() && GetMenuBar()->FindItem(ID_ViewPnm)->IsChecked())
            {
                if(menu.GetMenuItemCount())
                    menu.AppendSeparator();
                menu.Append(ID_POP_EDIT_PNM,"Edit PNM tile");
                menu.Append(ID_POP_REM_PNM,"Remove PNM tile");
            }
            if(spell_map->CheckSound(NULL,snd_type) && wSound)
            {
                if(menu.GetMenuItemCount())
                    menu.AppendSeparator();
                menu.Append(ID_POP_EDIT_SOUND,"Edit sound");
                menu.Append(ID_POP_REM_SOUND,"Remove sound");
            }
                        
            
            if(menu.GetMenuItemCount())
            {
                menu.Connect(wxEVT_COMMAND_MENU_SELECTED,wxCommandEventHandler(MainFrame::OnCanvasPopupSelect),NULL,this);
                PopupMenu(&menu);
            }
        }
    }
    
    // unit view mode:
    int is_down = event.RightIsDown();
    if(is_down)
        spell_map->SetUnitRangeViewMode(SpellMap::UNIT_RANGE_MOVE);
    else
        spell_map->SetUnitRangeViewMode(SpellMap::UNIT_RANGE_NONE);
    canvas->Refresh();

}


// select all tiles
void MainFrame::OnSelectAll(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    spell_map->SelectTiles(SpellMap::SELECT_ADD);
}
// deselect all tiles
void MainFrame::OnDeselectAll(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    spell_map->SelectTiles(SpellMap::SELECT_CLEAR);
}
// select or deselect tiles
void MainFrame::OnSelectDeselect(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    // add/remove selection
    auto list = spell_map->GetSelections();
    spell_map->SelectTiles(list,SpellMap::SELECT_XOR);
}

// copy map selection to copy buffer
void MainFrame::OnCopyBuf(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    
    // get layers mask
    SpellMap::Layers lay;
    lay.lay1 = GetMenuBar()->FindItem(ID_SelectLay1)->IsChecked();
    lay.lay2 = GetMenuBar()->FindItem(ID_SelectLay2)->IsChecked();
    lay.anm = GetMenuBar()->FindItem(ID_SelectLayANM)->IsChecked();
    lay.pnm = GetMenuBar()->FindItem(ID_SelectLayPNM)->IsChecked();
    
    // get selected area (preference of persistent selection over cursor)
    std::vector<MapXY> list;
    list = spell_map->GetPersistSelections();
    if(list.empty())
        list = spell_map->GetSelections();
    
    if(event.GetId() == ID_CutBuf)
        spell_map->CutBuffer(list, lay);
    else
        spell_map->CopyBuffer(list, lay);
    
}
// clear map copy buffer
void MainFrame::OnClearBuf(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    // clear copy buffer and also clear tool
    spell_map->ClearBuffer();    
    wxRibbonBarEvent rev;
    OnToolPageClick(rev);        
    Refresh();
}
// try place copy buffer to map
void MainFrame::OnPasteBuf(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;

    auto pos = spell_map->GetSelection();
    spell_map->PasteBuffer(spell_map->tiles,spell_map->L3,spell_map->L4,spell_map->start,spell_map->escape,spell_map->target,pos);

    // optional cycling of tool items
    /*if(spell_tool.isTool())
        spell_map->SetBuffer(spell_tool,+1);*/

    Refresh();
}

// try place copy buffer to map
void MainFrame::OnChangeElevation(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    int step = 0;
    if(event.GetId() == ID_ElevUp)
        step++;
    else if(event.GetId() == ID_ElevDown)
        step--;
    if(step != 0)
    {
        spell_map->LockMap();
        spell_map->EditElev(step);
        spell_map->ReleaseMap();
        Refresh();
    }
}

// invalidate map region (retexturing)
void MainFrame::OnInvalidateSelection(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    // get selected area (preference of persistent selection over cursor)
    std::vector<MapXY> list;    
    list = spell_map->GetPersistSelections();
    if(list.empty())
        list = spell_map->GetSelections();

    // invalidate region    
    spell_map->IvalidateTiles(list, bind(&MainFrame::StatusStringCallback,this,placeholders::_1));
}

// delete object or stuff
void MainFrame::OnDeleteSel(wxCommandEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    // get selected area (preference of persistent selection over cursor)
    std::vector<MapXY> list;
    list = spell_map->GetPersistSelections();
    if(list.empty())
        list = spell_map->GetSelections();

    SpellMap::Layers layers;
    layers.lay1 = false;
    layers.lay2 = GetMenuBar()->FindItem(ID_ViewObj)->IsChecked();
    layers.anm = GetMenuBar()->FindItem(ID_ViewAnm)->IsChecked();
    layers.pnm = GetMenuBar()->FindItem(ID_ViewPnm)->IsChecked();
    spell_map->LockMap();
    int rem_count = spell_map->DeleteSelObjects(list,layers);
    if(!rem_count)
    {
        // nothing removed, maybe remove unit?        
        spell_map->RemoveUnit(spell_map->GetCursorUnit(),true);
    }
    spell_map->ReleaseMap();
    Refresh();
}



// canvas left click
void MainFrame::OnCanvasLMouseDown(wxMouseEvent& event)
{
    if (m_mission_end_flow)
    {
        if (m_mission_result_visible && event.LeftDown())
            DismissMissionResultOverlay();
        // A completed mission is modal. Never allow a click intended for the
        // result flow to select/move/attack a tactical unit underneath it.
        return;
    }
    if(!spell_map->IsLoaded())
        return;
    if(inUnitOptions())
        return;

    // close minimap when clicked outside
    if(form_minimap && form_minimap->form)
    {
        wxCloseEvent evt(wxEVT_CLOSE_WINDOW,ID_MINIMAP_WIN);
        wxQueueEvent(form_minimap->form,new wxCloseEvent(evt));
    }
       
    // get selection
    auto xy_list = spell_map->GetSelections();
    
    if(event.LeftDown())
    {
        // LEFT DOWN event:

        if(spell_map->isCopyBufferFull())
        {
            // something in copy buffer
            auto pos = spell_map->GetSelection();
            spell_map->PasteBuffer(spell_map->tiles,spell_map->L3,spell_map->L4,spell_map->start,spell_map->escape,spell_map->target,pos);
            // optional cycling of tool items
            if(event.ControlDown() && spell_tool.isTool())
                spell_map->SetBuffer(spell_tool,+1);
            Refresh();
        }
        else if(spell_tool.isActive() && xy_list.size() && xy_list[0].IsSelected())
        {
            // some tool selected: edit map class
            spell_map->EditClass(xy_list, &spell_tool, bind(&MainFrame::StatusStringCallback,this,placeholders::_1));
        }
        else
        {
            // try select/move stuff:
            int wPnms = GetMenuBar()->FindItem(ID_ViewPnm)->IsChecked(); // ###todo: optimize?
            int wAnms = GetMenuBar()->FindItem(ID_ViewAnm)->IsChecked(); // ###todo: optimize?
            int wEvents = GetMenuBar()->FindItem(ID_ViewEvents)->IsChecked(); // ###todo: optimize?
            int wSounds = GetMenuBar()->FindItem(ID_ViewSounds)->IsChecked(); // ###todo: optimize?
            int wSoundLoops = GetMenuBar()->FindItem(ID_ViewSoundLoops)->IsChecked(); // ###todo: optimize?
            bool wSound = wSounds || wSoundLoops;
            MapSound::SoundType snd_type = MapSound::SoundType::BOTH;
            if(wSounds && wSoundLoops)
                snd_type = MapSound::SoundType::BOTH;
            else if(wSounds)
                snd_type = MapSound::SoundType::RANDOM;
            else if(wSoundLoops)
                snd_type = MapSound::SoundType::LOOP;

            select_pos = spell_map->GetSelection(NULL);
            sel_unit = spell_map->GetSelectedUnit();
            cur_unit = spell_map->GetCursorUnit();
            auto cur_evt = spell_map->GetCursorEvent();
            auto sel_evt = spell_map->GetSelectEvent();            
            auto cur_sound = spell_map->CheckSound(NULL,snd_type);
            auto sel_sound = spell_map->SoundSelected();
            auto cur_pnm = spell_map->CheckPNM();
            auto sel_pnm = spell_map->SelectedPNM();
            auto cur_anm = spell_map->CheckANM();
            auto sel_anm = spell_map->SelectedANM();
            // QUICK ADD UNIT:
            // Shift + click na prázdné políèko otevøe Units editor pro pøidání jednotky
            // (po zavøení okna ID_UNITS_WIN se jednotka sama pøidá na aktuální selection)
            if (event.ShiftDown() && !spell_map->isGameMode())
            {
                // pøidávej jen když klik není na žádném "objektu" (aby se to netlouklo se select/move)
                if (!cur_unit && !cur_evt && !cur_sound && !cur_pnm && !cur_anm)
                {
                    wxCommandEvent cmd;
                    OnAddUnit(cmd);
                    return;
                }
            }

            if(!spell_map->isGameMode())
            {
                if(wAnms && cur_anm && cur_anm == sel_anm)
                {
                    // move ANM
                    sel_anm->in_placement = !sel_anm->in_placement;
                }
                else if(wAnms && cur_anm)
                {
                    // select ANM
                    spell_map->SelectANM(cur_anm);
                }
                else if(wPnms && cur_pnm && cur_pnm == sel_pnm)
                {
                    // move PNM
                    sel_pnm->in_placement = !sel_pnm->in_placement;
                }
                else if(wPnms && cur_pnm)
                {
                    // select PNM
                    spell_map->SelectPNM(cur_pnm);
                }
                else if(wSound && cur_sound && cur_sound == sel_sound)
                {
                    // move sound
                    if(sel_sound->in_placement)
                    {
                        // remap sound positions when mover released
                        spell_map->sounds->InitSounds();
                        spell_map->sounds->UpdateMaps();
                    }
                    sel_sound->in_placement = !sel_sound->in_placement;
                }
                else if(wSound && cur_sound)
                {
                    // select sound
                    spell_map->SoundSelect(cur_sound);
                }
                else if(wEvents && sel_evt && cur_unit && event.ControlDown())
                {
                    // try add/remove unit to event
                    spell_map->UpdateEventUnit(sel_evt, cur_unit);
                }
                else if(wEvents && sel_evt && sel_evt->position == select_pos)
                {
                    // move/place event
                    if(sel_unit)
                        sel_unit->in_placement = false;
                    sel_evt->in_placement = !sel_evt->in_placement;
                }
                else if(wEvents && cur_evt && !cur_evt->isMissionStart())
                {
                    // select event
                    spell_map->SelectEvent(cur_evt);
                }
                else if(wEvents && cur_unit && cur_unit->GetTrigEvent(SpellMapEventRec::EvtTypes::EVT_SEE_UNIT))
                {
                    // select SeeUnit() event
                    spell_map->SelectEvent(cur_unit->GetTrigEvent(SpellMapEventRec::EvtTypes::EVT_SEE_UNIT));
                }
                else if(cur_unit && cur_unit == sel_unit)
                {
                    // move/place unit
                    if(sel_evt)
                        sel_evt->in_placement = false;
                    sel_unit->in_placement = !sel_unit->in_placement;
                }
                else if(cur_unit)
                {
                    // try select unit (if on cursor)
                    spell_map->SelectUnit(cur_unit);
                }
            }
            else
            {
                // game mode:
                if (spell_map->IsGroupMode())
                {
                    // group mode = no unit options menu: toggle units / move immediately (like original)
                    if (cur_unit && !cur_unit->is_enemy)
                        spell_map->SelectUnit(cur_unit);
                    else
                        spell_map->MoveUnit(select_pos);
                }
                else
                {
int options = spell_map->GetUnitOptions();
                
                // reduce attack options if only one target is possible
                if(!!(options & SpellMap::UNIT_OPT_LOWER) != !!(options & SpellMap::UNIT_OPT_UPPER))
                    options = (options & ~(SpellMap::UNIT_OPT_LOWER | SpellMap::UNIT_OPT_LOWER)) | SpellMap::UNIT_OPT_ATTACK;

                if(options)
                {
                    // show optional menu (or directly call callback) to resolve options
                    wxPoint pos = event.GetPosition();
                    pos.x -= 15;
                    pos.y -= 15;
                    form_unit_opts = new FormUnitOpts(canvas,ID_UNIT_MODE_WIN,pos,spell_data,options,bind(&MainFrame::OnUnitClick_cb,this,placeholders::_1));
                }
                }
            }
        }
    }

    canvas->Refresh();
}

// on canvas mouse enter
void MainFrame::OnCanvasMouseEnter(wxMouseEvent& event)
{
    if(inSubForm())
        return;

    canvas->SetFocus();
}
// on canvas mouse leave
void MainFrame::OnCanvasMouseLeave(wxMouseEvent& event)
{
    SetCursor(*wxSTANDARD_CURSOR);

    if(!spell_map->IsLoaded())
        return;
    if(inUnitOptions())
        return;

    spell_map->SetUnitRangeViewMode(SpellMap::UNIT_RANGE_NONE);
    spell_map->scroller.Idle();
}
// on canvas mouse move
void MainFrame::OnCanvasMouseMove(wxMouseEvent& event)
{
    if(!spell_map->IsLoaded())
        return;
    if(inUnitOptions())
        return;

    static int last_in_hud = false;

    int hud_top = spell_map->GetHUDtop(event.GetX());
    if(event.GetY() >= hud_top)
    {
        // mouse in HUD area - kill scroll
        spell_map->SetUnitRangeViewMode(SpellMap::UNIT_RANGE_NONE);
        if(!last_in_hud)
            spell_map->InvalidateHUDbuttons();
        spell_map->scroller.Idle();
        last_in_hud = true;

        // invalidate cursor
        spell_map->ClearSelections();

        // default game cursor
        SetCursor(*spell_data->gres.cur_pointer);
    }
    else
    {
        spell_map->scroller.Move(event.GetX(),event.GetY());
        last_in_hud = false;

        // resolve cursor
        auto options = spell_map->GetUnitOptions();
        wxCursor* cur = spell_data->gres.cur_pointer;
        if(!spell_map->isGameMode())
            cur = spell_data->gres.cur_pointer;
        else if(!options)
            cur = spell_data->gres.cur_pointer;
        else if(options == SpellMap::UNIT_OPT_MOVE)
            cur = spell_data->gres.cur_move;
        else if(options == SpellMap::UNIT_OPT_SELECT)
            cur = spell_data->gres.cur_select;
        else if(options == SpellMap::UNIT_OPT_LOWER)
            cur = spell_data->gres.cur_attack_down;
        else if(options == SpellMap::UNIT_OPT_UPPER)
            cur = spell_data->gres.cur_attack_up;
        else if(options == (SpellMap::UNIT_OPT_UPPER | SpellMap::UNIT_OPT_LOWER))
            cur = spell_data->gres.cur_attack_up_down;
        else
            cur = spell_data->gres.cur_question;
        SetCursor(*cur);
    }


    // update map selection
    MapXY mxy = spell_map->GetSelection();
    int elev = spell_map->GetElevation();

    // Hide debug status bar info in game mode
    if (!spell_map->isGameMode())
    {
        SetStatusText(wxString::Format(wxT("x=%d"),mxy.x),0);
        SetStatusText(wxString::Format(wxT("y=%d"),mxy.y),1);
        SetStatusText(wxString::Format(wxT("z=%d"),elev),2);
        SetStatusText(wxString::Format(wxT("xy=%d"),spell_map->ConvXY(mxy)),3);
        SetStatusText(wxString::Format(wxT("L1: %s"),spell_map->GetL1tileName()),4);
        SetStatusText(wxString::Format(wxT("L2: %s"),spell_map->GetL2tileName()),5);
        auto [flags,height,code] = spell_map->GetTileFlags();
        SetStatusText(wxString::Format(wxT("(0x%02X)"),code),6);
    }
    else if (GetStatusBar() && GetStatusBar()->IsShown())
    {
        GetStatusBar()->Hide();
    }

    auto sel_evt = spell_map->GetSelectEvent();
    auto* unit = spell_map->GetSelectedUnit();
    auto sel_sound = spell_map->SoundSelected();
    auto* sel_pnm = spell_map->SelectedPNM();
    auto* sel_anm = spell_map->SelectedANM();
    if(sel_anm && sel_anm->in_placement && mxy.IsSelected())
    {
        // change ANM position
        spell_map->MoveANM(sel_anm,mxy);
    }
    else if(sel_pnm && sel_pnm->in_placement && mxy.IsSelected())
    {
        // change PNM position
        spell_map->MovePNM(sel_pnm,mxy);
    }
    else if(sel_sound && sel_sound->in_placement && mxy.IsSelected())
    {
        // change sound position
        spell_map->SoundMove(sel_sound,mxy,false);
    }
    else if(sel_evt && sel_evt->in_placement && mxy.IsSelected())
    {
        // change event position
        sel_evt->position = mxy;
        spell_map->events->ResetEvents();
    }
    else if(unit && unit->in_placement && mxy.IsSelected())
    {
        // change unit position
        if(unit->coor != mxy)
            unit->was_moved = true;
        unit->coor = mxy;

        if(unit->was_moved)
            spell_map->unit_view->AddUnitView(unit,
                spell_map->isUnitsViewDebugMode()?(SpellMap::ViewRange::ClearMode::HIDE):(SpellMap::ViewRange::ClearMode::NONE));
    }

    canvas->Refresh();
    //event.Skip();
}
// on canvas wheel
void MainFrame::OnCanvasMouseWheel(wxMouseEvent& event)
{
    int delta = event.GetWheelRotation()/event.GetWheelDelta();
    if(event.ControlDown())
    {
        // with CTRL: cycle objects within tool
        if(spell_tool.isTool())
        {
            spell_map->SetBuffer(spell_tool,delta);
            canvas->Refresh();
        }
    }
    else
    {
        // no key: change selection size
        spell_map->scroller.ResizeSelection(delta);
        if(spell_tool.isTool())
            spell_map->SetBuffer(spell_tool,delta);
        canvas->Refresh();
    }
}
// on canvas key down
void MainFrame::OnCanvasKeyDown(wxKeyEvent& event)
{
    if(!spell_map)
    {
        event.Skip();
        return;
    }

    int key = event.GetKeyCode();

    // ~ key (backtick/tilde) opens in-game console
    if (key == '`' || key == '~' || event.GetRawKeyCode() == 0xC0)
    {
        OnMapConsoleCommand();
        return;
    }

    // ESC opens the main menu only when no tactical overlay is active.
    // Minimap / unit list / map options own ESC themselves and close first.
    if(key == WXK_ESCAPE && !m_editor_unlocked)
    {
        if (form_unit_opts || form_message || form_video_box ||
            form_map_options || form_minimap || form_units_list)
        {
            event.Skip();
            return;
        }

        if(spell_map->isGameMode())
            spell_map->SetActiveGroup(0);
        wxCommandEvent evt;
        OnOpenMainMenu(evt);
        return;
    }

    // Group move hotkeys (game mode only)
    if(spell_map->isGameMode())
    {
        if(key >= '1' && key <= '8')
        {
            spell_map->SetActiveGroup(key - '0');
            if(canvas) { canvas->SetFocus(); canvas->Refresh(); }
            return;
        }

        if(key == '0' || key == WXK_ESCAPE)
        {
            spell_map->SetActiveGroup(0);
            if(canvas) { canvas->SetFocus(); canvas->Refresh(); }
            return;
        }
    }

    // keep existing controls working
    event.Skip();
}

void MainFrame::OnMapConsoleCommand()
{
    wxTextEntryDialog dlg(this, "Enter command:", "Console", "");
    if (dlg.ShowModal() != wxID_OK)
        return;

    std::string cmd = dlg.GetValue().ToStdString();
    while (!cmd.empty() && cmd.front() == ' ') cmd.erase(cmd.begin());
    while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();
    for (char& c : cmd) c = (char)std::toupper((unsigned char)c);

    if (cmd == "ALLDONE")
    {
        if (spell_map && spell_map->IsLoaded() && spell_map->isGameMode() && spell_map->events)
        {
            for (auto* evt : spell_map->events->GetEvents())
            {
                if (!evt) continue;
                if (evt->is_objective && !evt->is_done)
                    evt->is_done = true;
            }
            spell_map->CheckAndTriggerMissionEnd();
            if (canvas) canvas->Refresh();
        }
    }
    else if (cmd == "GAMEMODEOFF")
    {
        m_editor_unlocked = true;
        if (spell_map && spell_map->IsLoaded() && spell_map->isGameMode())
            SetGameModeUI(false);
        else
            UpdateMenuForGameMode();
    }
    else if (!cmd.empty())
    {
        wxMessageBox("Unknown command: " + dlg.GetValue(), "Console", wxOK | wxICON_WARNING, this);
    }
}


void MainFrame::OnUnitClick_cb(int option)
{
    if(option & SpellMap::UNIT_OPT_SELECT)
    {
        // select unit
        auto unit = spell_map->CanSelectUnit(select_pos);
        spell_map->SelectUnit(unit);
    }
    else if(option & SpellMap::UNIT_OPT_MOVE)
    {
        // move
        spell_map->MoveUnit(select_pos);
    }
    else if(option & SpellMap::UNIT_OPT_ATTACK || option & SpellMap::UNIT_OPT_LOWER || option & SpellMap::UNIT_OPT_UPPER)
    {
        // attack (unit or object)
        int is_upper = option & SpellMap::UNIT_OPT_UPPER;
        spell_map->Attack(select_pos, is_upper);
    }
}


//--------------------------------------------------------------------------------------------------------------------
// Message display stuff
//--------------------------------------------------------------------------------------------------------------------
// show message function wrapper
void MainFrame::ShowMessage(SpellTextRec *message,bool is_yesno,std::function<void(bool)> exit_cb)
{
    //auto text = spell_data->texts->GetText("u0101_07");
    form_message = new FormMsgBox(canvas, ID_MSG_WIN, spell_data, spell_map, message, (is_yesno)?(FormMsgBox::SpellMsgOptions::YESNO):(FormMsgBox::SpellMsgOptions::NONE), exit_cb);
}
// return true if some message still exist or a video is playing
bool MainFrame::CheckMessageState()
{
    return(form_message != NULL || form_video_box != NULL);    
}


//--------------------------------------------------------------------------------------------------------------------
// Tool bar stuff
//--------------------------------------------------------------------------------------------------------------------
// tool selected
void MainFrame::OnToolBtnClick(wxRibbonButtonBarEvent& event)
{
    // get button id
    int id = event.GetId();

    // no tool selection
    spell_tool.Set();

    if(!spell_map->IsLoaded())
        return;
    if(!ribbonBar)
        return;

    int tool_id = (id - ID_TOOL_BASE)/ID_TOOL_CLASS_STEP;
    int item_id = (id - ID_TOOL_BASE)%ID_TOOL_CLASS_STEP;

    // very schmutzig way to deselect all other tool buttons
    for(int tid = 0; tid < ribbonBar->GetPageCount(); tid++)
    {        
        // get button bar
        wxRibbonPage* page = ribbonBar->GetPage(tid);
        auto wlist = page->GetChildren();
        if(!wlist.size())
            continue;
        wxRibbonPanel* panel = (wxRibbonPanel*)wlist[0];
        auto clist = panel->GetChildren();
        if(!clist.size())
            continue;
        wxRibbonButtonBar* btns = (wxRibbonButtonBar*)clist[0];

        // for each button:
        for(int iid = 0; iid < btns->GetButtonCount(); iid++)
        {
            int btn_id = ID_TOOL_BASE + tid*ID_TOOL_CLASS_STEP + iid;            
            
            if(id != btn_id)
                btns->ToggleButton(btn_id, false);
            else
            {            
                // this button (event caller):
                if(event.IsChecked())
                {
                    // some tool selected: setup tool pointer
                    SpellObject *obj = (SpellObject*)btns->GetItemClientData(btns->GetItemById(btn_id));
                    if(obj)
                    {
                        // tool is object
                        // unset tool (depreceted method)
                        spell_tool.Set();
                        // place tool to clipboard (new method)
                        spell_map->SetBuffer(obj);
                    }
                    else
                    {
                        spell_tool.Set(tid, iid); // tool is class
                        if(spell_tool.isTool())
                            spell_map->SetBuffer(spell_tool,0);
                    }
                }
            }
        }
    }
}
// tool page selected
void MainFrame::OnToolPageClick(wxRibbonBarEvent& event)
{
    // no tool selection
    spell_tool.Set();

    if(!ribbonBar)
        return;

    // very schmutzig way to deselect all other tool buttons
    for(int tid = 0; tid < ribbonBar->GetPageCount(); tid++)
    {
        wxRibbonPage* page = ribbonBar->GetPage(tid);
        auto wlist = page->GetChildren();
        if(!wlist.size())
            continue;
        wxRibbonPanel* panel = (wxRibbonPanel*)wlist[0];
        auto clist = panel->GetChildren();
        if(!clist.size())
            continue;
        wxRibbonButtonBar* btns = (wxRibbonButtonBar*)clist[0];
        // for each button:
        for(int iid = 0; iid < btns->GetButtonCount(); iid++)
        {
            int btn_id = ID_TOOL_BASE + tid*ID_TOOL_CLASS_STEP + iid;
            btns->ToggleButton(btn_id,false);
        }
    }
}
// fill toolset ribbon
void MainFrame::LoadToolsetRibbon(Terrain *terr)
{    
    // clear old ribbon
    if (ribbonBar)
        ribbonBar->Destroy();
    ribbonBar = new wxRibbonBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxRIBBON_BAR_DEFAULT_STYLE);
    ribbonBar->SetArtProvider(new wxRibbonDefaultArtProvider);
    
    if (!terr && spell_map)
        terr = spell_map->terrain;
    if (terr)
    {
        // for each toolset:
        for (int tool_id = 0; tool_id < terr->GetToolsCount(); tool_id++)
        {
            string toolset_name = terr->GetToolSetName(tool_id);
            string toolset_title = terr->GetToolSetTitle(tool_id);

            // for each tool in toolset:
            wxRibbonPage* ribPage = new wxRibbonPage(ribbonBar, wxID_ANY, toolset_name, wxNullBitmap, 0);
            wxRibbonPanel* ribPanel = new wxRibbonPanel(ribPage, wxID_ANY, toolset_title, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxRIBBON_PANEL_DEFAULT_STYLE | wxRIBBON_PANEL_NO_AUTO_MINIMISE | wxRIBBON_PANEL_FLEXIBLE);
            wxRibbonButtonBar* ribBtns = new wxRibbonButtonBar(ribPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
            
            // obtain all tools (glyphs) parameters
            vector<std::string> titles;
            vector<int> index;
            vector<std::tuple<int, int>> size;
            vector<SpellObject*> objects;
            int item_id = 0;
            for (; item_id < terr->GetToolSetItemsCount(tool_id); item_id++)
            {
                SpellTool spell_tool;
                spell_tool.Set(tool_id,item_id);
                int has_multi = terr->GetToolSprites(spell_tool).size() + terr->GetToolObjects(spell_tool).size();
                titles.push_back(terr->GetToolSetItem(tool_id, item_id) + ((has_multi > 1)?" [+]":""));
                index.push_back(ID_TOOL_BASE + tool_id * ID_TOOL_CLASS_STEP + item_id);
                size.push_back(terr->GetToolSetItemImageSize(tool_id, item_id));
                objects.push_back(NULL);
            }
            for (auto const& obj : terr->GetObjects())
            {
                if (obj->GetToolClass() != tool_id + 1)
                    continue;
                if (obj->GetToolClassGroup() != 0)
                    continue;
                titles.push_back(obj->GetDescription());
                index.push_back(ID_TOOL_BASE + tool_id * ID_TOOL_CLASS_STEP + item_id);
                size.push_back(obj->GetGlyphSize());
                objects.push_back(obj);
                item_id++;
            }
            // get mean aspect ratio
            double aspect = 0;
            int x_max = 0;
            int y_max = 0;
            for (int k = 0; k < size.size(); k++)
            {
                auto [x, y] = size[k];
                if (!objects[k])
                {
                    x_max = max(x_max, x);
                    y_max = max(y_max, y);
                }
                aspect += (double)x / (double)y;
            }
            aspect *= (1.0 / size.size());
            
            // glyph size
            int x_size = -1;
            int y_size = -1;
            if (terr->GetToolSetGlyphScalingMode(tool_id) == SpellToolsGroup::SCALE_MEAN)
            {
                auto [x,y] = terr->GetToolSetGlyphScaling(tool_id);
                x_size = x;
                y_size = (int)((double)x_size / aspect);
            }
            else
            {
                x_size = x_max;
                y_size = y_max;
            }
            // build buttons:
            for (int k = 0; k < size.size(); k++)
            {
                // render glyph
                wxBitmap *bmp=NULL;                
                if (objects[k])
                    bmp = objects[k]->RenderPreview(1.30, x_size, y_size);
                else
                    bmp = terr->RenderToolSetItemImage(tool_id, k, 1.30, x_size, y_size);
                // make button
                auto btn = ribBtns->AddButton(index[k], titles[k], *bmp, wxEmptyString, wxRIBBON_BUTTON_TOGGLE);
                // include object pointer if it's object
                ribBtns->SetItemClientData(btn,objects[k]);
                delete bmp;
            }

            ribBtns->Realize();
        }
    }

    // update ribbon with new stuff
    ribbonBar->Realize();
    sizer->Insert(0,ribbonBar, 0, wxALL | wxEXPAND, 2);

    // Ribbon belongs to the editor toolset only. Tactical game mode must not
    // reserve a blank ribbon row under the menu.
    if (spell_map && spell_map->isGameMode())
        ribbonBar->Hide();

    sizer->Layout();
}


//--------------------------------------------------------------------------------------------------------------------
// Set Gamma Dialog
//--------------------------------------------------------------------------------------------------------------------
FormGamma::FormGamma(wxFrame* parent,SpellMap* map,wxWindowID id) :wxDialog(parent,wxID_ANY,"Gamma correction",wxDefaultPosition,wxSize(400,80),wxDEFAULT_FRAME_STYLE|wxSTAY_ON_TOP)
{
    spellcross_app::ApplyWindowIcon(this);
    // store local reference to initial map and data
    spell_map = map;

    // make slider
    slider = new wxSlider(this,wxID_ANY,1300,500,2000);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(slider,1,wxEXPAND|wxALL);
    this->SetSizer(sizer);
    this->SetAutoLayout(true); 
    this->Center();
    this->Bind(wxEVT_CHAR_HOOK,&FormGamma::OnExit,this);
    SetMinSize(wxSize(300,-1));
    this->Fit();
    
    Bind(wxEVT_COMMAND_SLIDER_UPDATED,&FormGamma::OnChangeGamma, this);
    Bind(wxEVT_CLOSE_WINDOW,&FormGamma::OnClose,this,id);

}
void FormGamma::OnChangeGamma(wxCommandEvent& event)
{
    double gamma = 0.001*(double)slider->GetValue();
    SetTitle(wxString::Format(wxT("Gamma correction = %#0.2f"),gamma));
    spell_map->SetGamma(gamma);
}
void FormGamma::OnClose(wxCloseEvent& ev)
{
    wxPostEvent(GetParent(),ev);
    ev.Skip();
    Destroy();
}
void FormGamma::OnExit(wxKeyEvent& event)
{
    if(event.GetKeyCode()==WXK_ESCAPE)
        this->Close();
    else
        event.Skip();
}


//--------------------------------------------------------------------------------------------------------------------
// Select terrain Dialog
//--------------------------------------------------------------------------------------------------------------------
FormTerrain::FormTerrain(wxFrame* parent,SpellData* data,wxWindowID id) :wxDialog(parent,wxID_ANY,"Terrain selection",wxDefaultPosition,wxSize(300,150),wxDEFAULT_FRAME_STYLE|wxSTAY_ON_TOP)
{
    spellcross_app::ApplyWindowIcon(this);
    // store local reference to initial map and data
    m_spell_data = data;

    // make slider
    wxStaticText *txt = new wxStaticText(this, wxID_ANY, "Select terrain type:");    
    terr_choice = new wxChoice(this, wxID_TERR_CHB);
    btn_ok = new wxButton(this, wxID_OK_BTN,"OK");
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(txt,0,wxEXPAND|wxTOP|wxLEFT|wxRIGHT,5);
    sizer->Add(terr_choice,0,wxEXPAND|wxBOTTOM|wxLEFT|wxRIGHT,5);
    sizer->Add(btn_ok,0,wxEXPAND|wxALL,5);
    this->SetSizer(sizer);
    this->SetAutoLayout(true);
    this->Center();
    SetMinSize(wxSize(300,-1));
    this->Fit();

    // assign button shortcuts
    std::vector<wxAcceleratorEntry> entries;
    entries.emplace_back(wxACCEL_NORMAL,WXK_RETURN,wxID_OK_BTN);
    entries.emplace_back(wxACCEL_NORMAL,WXK_NUMPAD_ENTER,wxID_OK_BTN);
    wxAcceleratorTable accel(entries.size(),entries.data());
    this->SetAcceleratorTable(accel);

    this->Bind(wxEVT_CHAR_HOOK,&FormTerrain::OnExit,this);

    Bind(wxEVT_CLOSE_WINDOW,&FormTerrain::OnClose,this,id);
    Bind(wxEVT_COMMAND_BUTTON_CLICKED,&FormTerrain::OnOK,this,wxID_OK_BTN);

    for(int k = 0; k < m_spell_data->GetTerrainCount(); k++)
        terr_choice->Append(m_spell_data->GetTerrain(k)->name);
    terr_choice->Select(0);
}

void FormTerrain::OnClose(wxCloseEvent& ev)
{
}
void FormTerrain::OnOK(wxCommandEvent& ev)
{
    EndModal(wxID_OK);
}
std::string FormTerrain::GetTerrain()
{
    return(terr_choice->GetStringSelection().ToStdString());
}
void FormTerrain::OnExit(wxKeyEvent& event)
{
    if(event.GetKeyCode()==WXK_RETURN)
        EndModal(wxID_OK);
    else if(event.GetKeyCode()==WXK_ESCAPE)
        EndModal(wxID_CANCEL);
}
