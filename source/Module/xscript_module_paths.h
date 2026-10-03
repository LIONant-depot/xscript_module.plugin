#ifndef XSCRIPT_MODULE_PATHS_H
#define XSCRIPT_MODULE_PATHS_H
#pragma once

// The path rules of a script module, in one place (pure text, no files touched): what a module's file is called inside source_db, what kind of file it is,
// and when a path of a library entry is acceptable. The descriptor's validation, the module editor, the pipe commands and the generator of the game project
// all go through these, so a path means the same thing everywhere.
#include <algorithm>
#include <cctype>
#include <string>
#include <cstdint>
#include <string_view>
#include <vector>

namespace xscript::module
{
    enum class file_kind : unsigned char { Other, Compiled, Header };

    inline std::string Lower(std::string_view Text) noexcept
    {
        std::string Out(Text);
        std::transform(Out.begin(), Out.end(), Out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return Out;
    }

    // The file's kind by its extension: Compiled files are built, Headers are listed (and the .h/.hpp ones are force-included into the precompiled header, see
    // the generator), Other is not a source of a module.
    inline file_kind KindOf(std::string_view Path) noexcept
    {
        const auto Dot = Path.find_last_of('.');
        if (Dot == std::string_view::npos) return file_kind::Other;
        const std::string Ext = Lower(Path.substr(Dot));
        if (Ext == ".cpp" || Ext == ".cc" || Ext == ".cxx" || Ext == ".c") return file_kind::Compiled;
        if (Ext == ".h" || Ext == ".hpp" || Ext == ".hxx" || Ext == ".inl" || Ext == ".ipp") return file_kind::Header;
        return file_kind::Other;
    }

    // Only .h and .hpp go into the precompiled header: an .inl or .ipp is meant to be included by another file.
    inline bool IsPchHeader(std::string_view Path) noexcept
    {
        const auto Dot = Path.find_last_of('.');
        if (Dot == std::string_view::npos) return false;
        const std::string Ext = Lower(Path.substr(Dot));
        return Ext == ".h" || Ext == ".hpp";
    }

    // A path inside a module: forward slashes, no leading slash, no "." or ".." parts, no drive, no empty parts. Returns "" when the text cannot be one.
    inline std::string NormalizeRelative(std::string_view In) noexcept
    {
        std::string Text(In);
        std::replace(Text.begin(), Text.end(), '\\', '/');
        if (Text.empty() || Text.front() == '/' || (Text.size() > 1 && Text[1] == ':')) return {};
        std::string Out;
        std::size_t At = 0;
        while (At <= Text.size())
        {
            const auto Slash = Text.find('/', At);
            const std::string Part = Text.substr(At, Slash == std::string::npos ? std::string::npos : Slash - At);
            if (Part.empty() || Part == ".." || Part.find_first_of("<>:\"|?*") != std::string::npos || Part.back() == ' ' || Part.back() == '.') return {};
            if (Part != ".") { if (!Out.empty()) Out += '/'; Out += Part; }
            if (Slash == std::string::npos) break;
            At = Slash + 1;
        }
        return Out;
    }

    // The module a source file belongs to, from its path: the guid of the last "ScriptModule/<xx>/<yy>/<guid>.desc/source_db/" in it (0 when there is none). The compiler gives the
    // game's files to the compiler by absolute path, and an #include of another module's header keeps it, so a file's path says which module it is part of. "..", "." and backslashes are
    // folded first (a header included from another module's folder arrives as ".../a/source_db/../../b.desc/source_db/x.h"). xx and yy are the low bytes of the guid, the way
    // the resource pipeline names the folders; a path whose folders do not match its guid is not a module's.
    inline std::uint64_t ModuleGuidFromSourcePath(std::string_view File) noexcept
    {
        std::vector<std::string> Parts;
        std::size_t At = 0;
        while (At <= File.size())
        {
            auto Cut = File.find_first_of("/\\", At);
            const std::string Part(File.substr(At, Cut == std::string_view::npos ? std::string_view::npos : Cut - At));
            if (Part == "..") { if (!Parts.empty()) Parts.pop_back(); }
            else if (!Part.empty() && Part != ".") Parts.push_back(Part);
            if (Cut == std::string_view::npos) break;
            At = Cut + 1;
        }
        auto IsHex = [](const std::string& Text, std::size_t Min, std::size_t Max)
        {
            return Text.size() >= Min && Text.size() <= Max && std::all_of(Text.begin(), Text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
        };
        for (std::size_t i = Parts.size(); i-- > 0; )
        {
            // ... ScriptModule / xx / yy / <guid>.desc / source_db / ...
            if (Lower(Parts[i]) != "scriptmodule" || i + 4 >= Parts.size()) continue;
            const std::string& Low = Parts[i + 1]; const std::string& High = Parts[i + 2]; const std::string& Desc = Parts[i + 3];
            if (Lower(Parts[i + 4]) != "source_db" || Desc.size() < 6 || Lower(Desc.substr(Desc.size() - 5)) != ".desc") continue;
            const std::string Guid = Desc.substr(0, Desc.size() - 5);
            if (!IsHex(Low, 2, 2) || !IsHex(High, 2, 2) || !IsHex(Guid, 1, 16)) continue;
            const std::uint64_t Value = std::stoull(Guid, nullptr, 16);
            if (Value == 0 || std::stoul(Low, nullptr, 16) != (Value & 0xFF) || std::stoul(High, nullptr, 16) != ((Value >> 8) & 0xFF)) continue;
            return Value;
        }
        return 0;
    }

    // Case-insensitive equality of two normalized paths (Windows file names are).
    inline bool SamePath(std::string_view A, std::string_view B) noexcept
    {
        return A.size() == B.size() && std::equal(A.begin(), A.end(), B.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    }

    // What a library's paths and a file's name may not be: absolute (a project that moves must build the same).
    inline bool IsAbsolute(std::string_view Path) noexcept
    {
        return !Path.empty() && (Path.front() == '/' || Path.front() == '\\' || (Path.size() > 1 && Path[1] == ':'));
    }

    // "a/b/c.h" -> "a/b"; "c.h" -> ""
    inline std::string FolderOf(std::string_view Path) noexcept
    {
        const auto Slash = Path.find_last_of('/');
        return Slash == std::string_view::npos ? std::string() : std::string(Path.substr(0, Slash));
    }

    inline std::string NameOf(std::string_view Path) noexcept
    {
        const auto Slash = Path.find_last_of('/');
        return std::string(Slash == std::string_view::npos ? Path : Path.substr(Slash + 1));
    }
}

#endif // XSCRIPT_MODULE_PATHS_H
