#ifndef XSCRIPT_MODULE_FILES_H
#define XSCRIPT_MODULE_FILES_H
#pragma once

// A script module on disk: reading and writing its descriptor, finding its files, and the one question the generator of the game project asks of it ("what do
// I build?", Resolve). No UI, no editor: the generator, the module editor and the pipe commands share it.
//
// The descriptor lists the files; the folder is only where they live. A file that is in source_db but not in the descriptor is not built (Unlisted says which:
// the editor offers to add them), and a listed file that is not there is Missing (reported, never silently dropped).
#include "plugins/xscript_module.plugin/source/Module/xscript_module_descriptor.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace xscript::module
{
    inline std::filesystem::path DescriptorFile(const std::filesystem::path& DescFolder) noexcept { return DescFolder / L"Descriptor.txt"; }
    inline std::filesystem::path SourceDb(const std::filesystem::path& DescFolder) noexcept       { return DescFolder / L"source_db"; }

    // The path of a file inside source_db as the OS wants it.
    inline std::filesystem::path Absolute(const std::filesystem::path& DescFolder, std::string_view Relative) noexcept
    {
        return SourceDb(DescFolder) / std::filesystem::path(std::string(Relative)).make_preferred();
    }

    // Every C++ file under source_db (any depth), as normalized relative paths, in a stable order (case-insensitive, folders by name).
    inline std::vector<std::string> ScanSourceDb(const std::filesystem::path& DescFolder) noexcept
    {
        std::vector<std::string> Out;
        const auto Root = SourceDb(DescFolder);
        std::error_code Ec;
        if (!std::filesystem::is_directory(Root, Ec)) return Out;
        for (auto It = std::filesystem::recursive_directory_iterator(Root, std::filesystem::directory_options::skip_permission_denied, Ec); !Ec && It != std::filesystem::recursive_directory_iterator(); It.increment(Ec))
        {
            if (!It->is_regular_file(Ec)) continue;
            const std::string Rel = NormalizeRelative(std::filesystem::relative(It->path(), Root, Ec).generic_string());
            if (!Rel.empty() && KindOf(Rel) != file_kind::Other) Out.push_back(Rel);
        }
        std::sort(Out.begin(), Out.end(), [](const std::string& A, const std::string& B) { return Lower(A) < Lower(B); });
        return Out;
    }

    // The paths of a descriptor written the way the rules want them (a hand edit with backslashes is fixed; a path that cannot be one is left for Validate).
    inline void Normalize(descriptor& D) noexcept
    {
        for (auto& F : D.m_Files) if (const std::string N = NormalizeRelative(F.m_Path); !N.empty()) F.m_Path = N;
    }

    // false when there is no Descriptor.txt or it cannot be read; Out is untouched then.
    inline bool Read(const std::filesystem::path& DescFolder, descriptor& Out, std::string* pError = nullptr) noexcept
    {
        std::error_code Ec;
        const auto File = DescriptorFile(DescFolder);
        if (!std::filesystem::exists(File, Ec)) { if (pError) *pError = "no Descriptor.txt"; return false; }
        descriptor Fresh;
        xproperty::settings::context Context;
        if (auto Err = Fresh.Serialize(true, File.wstring(), Context); Err) { if (pError) *pError = std::string(Err.getMessage()); return false; }
        Normalize(Fresh);
        Out = std::move(Fresh);
        return true;
    }

    inline bool Write(const std::filesystem::path& DescFolder, descriptor& D, std::string* pError = nullptr) noexcept
    {
        std::error_code Ec;
        std::filesystem::create_directories(DescFolder, Ec);
        Normalize(D);
        xproperty::settings::context Context;
        if (auto Err = D.Serialize(false, DescriptorFile(DescFolder).wstring(), Context); Err) { if (pError) *pError = std::string(Err.getMessage()); return false; }
        return true;
    }

    // What the module was before it had a descriptor: every C++ file of its folder.
    inline descriptor FromFolder(const std::filesystem::path& DescFolder) noexcept
    {
        descriptor D;
        for (auto& Path : ScanSourceDb(DescFolder)) D.m_Files.push_back({ Path, false });
        return D;
    }

    // What the module's descriptor is. A module without one gets it written from its folder (once); one that cannot be read is an error and is never overwritten.
    struct loaded
    {
        descriptor  m_Descriptor;
        bool        m_bMigrated = false;        // there was no Descriptor.txt: it was made from the folder (and written, when asked to)
        std::string m_Error;                    // Descriptor.txt exists and cannot be read
    };
    inline loaded LoadOrMigrate(const std::filesystem::path& DescFolder, bool bWriteMigration) noexcept
    {
        loaded L;
        std::string Error;
        if (Read(DescFolder, L.m_Descriptor, &Error)) return L;
        std::error_code Ec;
        if (std::filesystem::exists(DescriptorFile(DescFolder), Ec)) { L.m_Error = Error; return L; }
        L.m_Descriptor = FromFolder(DescFolder);
        L.m_bMigrated = true;
        if (bWriteMigration) Write(DescFolder, L.m_Descriptor);
        return L;
    }

    // The module as the generator builds it.
    struct resolved
    {
        descriptor                              m_Descriptor;
        bool                                    m_bMigrated = false;
        std::string                             m_Error;
        std::vector<std::filesystem::path>      m_Compiled;         // the files to compile (absolute)
        std::vector<std::filesystem::path>      m_Headers;          // every header that is listed (absolute): for the project's file list
        std::vector<std::filesystem::path>      m_PchHeaders;       // the .h/.hpp ones, force-included into the precompiled header
        std::vector<std::string>                m_Missing;          // listed, not excluded, and not on disk
        std::vector<std::string>                m_Unlisted;         // on disk in source_db, not in the descriptor: not built
    };
    inline resolved Resolve(const std::filesystem::path& DescFolder, bool bWriteMigration) noexcept
    {
        resolved R;
        auto L = LoadOrMigrate(DescFolder, bWriteMigration);
        R.m_Descriptor = std::move(L.m_Descriptor); R.m_bMigrated = L.m_bMigrated; R.m_Error = std::move(L.m_Error);
        if (!R.m_Error.empty()) return R;
        std::error_code Ec;
        for (const auto& F : R.m_Descriptor.m_Files)
        {
            if (F.m_bExclude) continue;
            const std::string Path = NormalizeRelative(F.m_Path);
            const auto Kind = Path.empty() ? file_kind::Other : KindOf(Path);
            if (Kind == file_kind::Other) continue;                                 // Validate says why
            const auto Abs = Absolute(DescFolder, Path);
            if (!std::filesystem::is_regular_file(Abs, Ec)) { R.m_Missing.push_back(Path); continue; }
            if (Kind == file_kind::Compiled) R.m_Compiled.push_back(Abs);
            else { R.m_Headers.push_back(Abs); if (IsPchHeader(Path)) R.m_PchHeaders.push_back(Abs); }
        }
        for (const auto& Path : ScanSourceDb(DescFolder))
        {
            const bool bListed = std::any_of(R.m_Descriptor.m_Files.begin(), R.m_Descriptor.m_Files.end(), [&](const file& F) { return SamePath(F.m_Path, Path); });
            if (!bListed) R.m_Unlisted.push_back(Path);
        }
        return R;
    }

    // The newest time anything this module is built from changed: its descriptor and every file it builds.
    inline std::filesystem::file_time_type NewestWrite(const std::filesystem::path& DescFolder, const resolved& R) noexcept
    {
        std::error_code Ec;
        auto Latest = std::filesystem::file_time_type::min();
        auto Consider = [&](const std::filesystem::path& File) { if (const auto T = std::filesystem::last_write_time(File, Ec); !Ec && T > Latest) Latest = T; };
        Consider(DescriptorFile(DescFolder));
        for (const auto& F : R.m_Compiled) Consider(F);
        for (const auto& F : R.m_Headers)  Consider(F);
        return Latest;
    }

    //----------------------------------------------------------------------------------------------------------------------
    // Edits of a descriptor (the module editor's commands apply them; the descriptor is saved by the caller)
    //----------------------------------------------------------------------------------------------------------------------
    inline file* FindFile(descriptor& D, std::string_view Path) noexcept
    {
        for (auto& F : D.m_Files) if (SamePath(F.m_Path, Path)) return &F;
        return nullptr;
    }
    inline const file* FindFile(const descriptor& D, std::string_view Path) noexcept
    {
        for (const auto& F : D.m_Files) if (SamePath(F.m_Path, Path)) return &F;
        return nullptr;
    }

    // Lists the files that are in the folder and not in the descriptor (and with bRemoveMissing drops the listed ones that are gone): the "rescan" of the editor.
    struct sync_result { std::vector<std::string> m_Added, m_Removed; };
    inline sync_result Sync(descriptor& D, const std::filesystem::path& DescFolder, bool bRemoveMissing) noexcept
    {
        sync_result R;
        for (const auto& Path : ScanSourceDb(DescFolder))
            if (!FindFile(D, Path)) { D.m_Files.push_back({ Path, false }); R.m_Added.push_back(Path); }
        if (bRemoveMissing)
        {
            std::error_code Ec;
            for (auto It = D.m_Files.begin(); It != D.m_Files.end(); )
            {
                const std::string Path = NormalizeRelative(It->m_Path);
                if (!Path.empty() && !std::filesystem::is_regular_file(Absolute(DescFolder, Path), Ec)) { R.m_Removed.push_back(Path); It = D.m_Files.erase(It); }
                else ++It;
            }
        }
        return R;
    }

    // Reads a whole file as bytes (a file that is deleted is restored from this by Undo).
    inline bool ReadAll(const std::filesystem::path& Path, std::string& Out) noexcept
    {
        std::ifstream In(Path, std::ios::binary);
        if (!In) return false;
        std::stringstream Text; Text << In.rdbuf();
        Out = Text.str();
        return true;
    }
    inline bool WriteAll(const std::filesystem::path& Path, const std::string& Bytes) noexcept
    {
        std::error_code Ec;
        std::filesystem::create_directories(Path.parent_path(), Ec);
        std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
        if (!Out) return false;
        Out.write(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
        return Out.good();
    }

    // What a new file starts with: a header gets #pragma once, a source file includes its own header when there is one.
    enum class file_template : unsigned char { Empty, Header, Source };
    inline std::string TemplateText(file_template T, std::string_view Path, const std::filesystem::path& DescFolder) noexcept
    {
        if (T == file_template::Header) return "#pragma once\r\n\r\n";
        if (T != file_template::Source) return {};
        const std::string Name = NameOf(Path);
        const auto Dot = Name.find_last_of('.');
        const std::string Stem = Dot == std::string::npos ? Name : Name.substr(0, Dot);
        std::error_code Ec;
        for (const char* pExt : { ".h", ".hpp" })
        {
            const std::string Header = (FolderOf(Path).empty() ? std::string() : FolderOf(Path) + "/") + Stem + pExt;
            if (std::filesystem::is_regular_file(Absolute(DescFolder, Header), Ec)) return "#include \"" + Stem + pExt + "\"\r\n\r\n";
        }
        return {};
    }
}

#endif // XSCRIPT_MODULE_FILES_H
