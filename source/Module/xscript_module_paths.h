#ifndef XSCRIPT_MODULE_PATHS_H
#define XSCRIPT_MODULE_PATHS_H
#pragma once

// The path rules of a script module, in one place (pure text, no files touched): what a module's file is called inside source_db, what kind of file it is,
// and when a path of a library entry is acceptable. The descriptor's validation, the module editor, the pipe commands and the generator of the game project
// all go through these, so a path means the same thing everywhere.
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

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
