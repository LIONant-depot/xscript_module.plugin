#ifndef XSCRIPT_MODULE_OPS_H
#define XSCRIPT_MODULE_OPS_H
#pragma once

// What can be done to a script module's files, with the descriptor and the disk always changing together: add, remove, rename, move, new folder, remove folder,
// exclude, set content. Every operation works on the descriptor it is given (the caller loads it and writes it back) and on the module's folder, returns an
// error text ("" = it worked) and hands back what its inverse needs, so the commands that wrap these (the workspace's ones and the module editor's) are undoable
// without each of them re-inventing it. No UI, no editor.
#include "plugins/xscript_module.plugin/source/Module/xscript_module_files.h"

namespace xscript::module::ops
{
    // A path for a file of the module: normalized, and a C++ file.
    inline std::string CheckFilePath(std::string_view In, std::string& Normalized) noexcept
    {
        Normalized = NormalizeRelative(In);
        if (Normalized.empty()) return std::format("'{}' cannot be a path inside source_db (relative, no '..', no drive)", In);
        if (KindOf(Normalized) == file_kind::Other) return std::format("'{}' is not a C++ file (.cpp .cc .cxx .c .h .hpp .hxx .inl .ipp)", Normalized);
        return {};
    }

    // The template a name asks for ("header", "source", "empty"); an empty or unknown name means "by the extension".
    inline file_template TemplateFor(std::string_view Name, std::string_view Path) noexcept
    {
        if (Name == "empty")  return file_template::Empty;
        if (Name == "header") return file_template::Header;
        if (Name == "source") return file_template::Source;
        return IsPchHeader(Path) ? file_template::Header : KindOf(Path) == file_kind::Compiled ? file_template::Source : file_template::Empty;
    }

    //----------------------------------------------------------------------------------------------------------------------
    // Add: a new file (created from a template) or one that is already in the folder (it is just listed)
    //----------------------------------------------------------------------------------------------------------------------
    struct added { std::string m_Path; bool m_bCreated = false; };

    inline std::string AddFile(descriptor& D, const std::filesystem::path& Desc, std::string_view PathIn, file_template Template, added& Out) noexcept
    {
        std::string Path;
        if (auto Err = CheckFilePath(PathIn, Path); !Err.empty()) return Err;
        if (FindFile(D, Path)) return std::format("'{}' is already part of the module", Path);
        const auto Abs = Absolute(Desc, Path);
        std::error_code Ec;
        const bool bExists = std::filesystem::exists(Abs, Ec);
        if (bExists && !std::filesystem::is_regular_file(Abs, Ec)) return std::format("'{}' is a folder", Path);
        if (!bExists && !WriteAll(Abs, TemplateText(Template, Path, Desc))) return std::format("'{}' could not be created", Path);
        D.m_Files.push_back({ Path, false });
        Out = { Path, !bExists };
        return {};
    }
    inline std::string UndoAdd(descriptor& D, const std::filesystem::path& Desc, const added& A) noexcept
    {
        std::erase_if(D.m_Files, [&](const file& F) { return SamePath(F.m_Path, A.m_Path); });
        std::error_code Ec;
        if (A.m_bCreated)
        {
            std::filesystem::remove(Absolute(Desc, A.m_Path), Ec);
            // the folders the file was created in go too when they are empty now (a folder that was already there with something in it stays)
            for (std::string Folder = FolderOf(A.m_Path); !Folder.empty(); Folder = FolderOf(Folder))
            {
                if (!std::filesystem::is_directory(Absolute(Desc, Folder), Ec) || !std::filesystem::is_empty(Absolute(Desc, Folder), Ec)) break;
                std::filesystem::remove(Absolute(Desc, Folder), Ec);
            }
        }
        return {};
    }

    //----------------------------------------------------------------------------------------------------------------------
    // Remove: the file leaves the module and the disk; its bytes and its place are kept for the undo
    //----------------------------------------------------------------------------------------------------------------------
    struct removed
    {
        std::string m_Path, m_Content;
        std::size_t m_Index = 0;                    // its place in the descriptor's list
        bool        m_bExclude = false, m_bWasListed = true, m_bHadFile = true;
    };

    // What removing the file would take away, without removing anything (a command takes this before it runs: it is its undo).
    inline std::string CaptureFile(const descriptor& D, const std::filesystem::path& Desc, std::string_view PathIn, removed& Out) noexcept
    {
        const std::string Path = NormalizeRelative(PathIn);
        if (Path.empty()) return std::format("'{}' is not a path inside source_db", PathIn);
        const auto Abs = Absolute(Desc, Path);
        std::error_code Ec;
        const bool bHasFile = std::filesystem::is_regular_file(Abs, Ec);
        const auto It = std::find_if(D.m_Files.begin(), D.m_Files.end(), [&](const file& F) { return SamePath(F.m_Path, Path); });
        if (It == D.m_Files.end() && !bHasFile) return std::format("'{}' is not part of the module", Path);
        Out = {};
        Out.m_Path = Path; Out.m_bHadFile = bHasFile; Out.m_bWasListed = It != D.m_Files.end();
        if (bHasFile && !ReadAll(Abs, Out.m_Content)) return std::format("'{}' could not be read (it is not deleted)", Path);
        if (Out.m_bWasListed) { Out.m_Index = static_cast<std::size_t>(It - D.m_Files.begin()); Out.m_bExclude = It->m_bExclude; }
        return {};
    }

    inline std::string RemoveFile(descriptor& D, const std::filesystem::path& Desc, std::string_view PathIn, removed& Out) noexcept
    {
        if (auto Err = CaptureFile(D, Desc, PathIn, Out); !Err.empty()) return Err;
        if (Out.m_bWasListed) std::erase_if(D.m_Files, [&](const file& F) { return SamePath(F.m_Path, Out.m_Path); });
        std::error_code Ec;
        if (Out.m_bHadFile) { std::filesystem::remove(Absolute(Desc, Out.m_Path), Ec); if (Ec) return std::format("'{}' could not be deleted: {}", Out.m_Path, Ec.message()); }
        return {};
    }
    inline std::string RestoreFile(descriptor& D, const std::filesystem::path& Desc, const removed& R) noexcept
    {
        if (R.m_bHadFile && !WriteAll(Absolute(Desc, R.m_Path), R.m_Content)) return std::format("'{}' could not be restored", R.m_Path);
        if (R.m_bWasListed && !FindFile(D, R.m_Path))
            D.m_Files.insert(D.m_Files.begin() + static_cast<std::ptrdiff_t>(std::min(R.m_Index, D.m_Files.size())), file{ R.m_Path, R.m_bExclude });
        return {};
    }

    //----------------------------------------------------------------------------------------------------------------------
    // Rename / move (the same thing: a new path, which may be in another folder). Its inverse is the same call the other way.
    //----------------------------------------------------------------------------------------------------------------------
    inline std::string RenameFile(descriptor& D, const std::filesystem::path& Desc, std::string_view OldIn, std::string_view NewIn) noexcept
    {
        const std::string Old = NormalizeRelative(OldIn);
        std::string New;
        if (Old.empty()) return std::format("'{}' is not a path inside source_db", OldIn);
        if (auto Err = CheckFilePath(NewIn, New); !Err.empty()) return Err;
        auto* pEntry = FindFile(D, Old);
        const auto OldAbs = Absolute(Desc, Old), NewAbs = Absolute(Desc, New);
        std::error_code Ec;
        if (!pEntry && !std::filesystem::is_regular_file(OldAbs, Ec)) return std::format("'{}' is not part of the module", Old);
        if (!SamePath(Old, New) && (FindFile(D, New) || std::filesystem::exists(NewAbs, Ec))) return std::format("'{}' already exists", New);
        if (std::filesystem::is_regular_file(OldAbs, Ec))
        {
            std::filesystem::create_directories(NewAbs.parent_path(), Ec);
            // a rename that only changes the case of the name goes through a temporary name (the file system would take it as the same file)
            if (SamePath(Old, New) && Old != New)
            {
                const auto Temp = std::filesystem::path(OldAbs).concat(L".renaming");
                std::filesystem::rename(OldAbs, Temp, Ec);
                if (!Ec) std::filesystem::rename(Temp, NewAbs, Ec);
            }
            else std::filesystem::rename(OldAbs, NewAbs, Ec);
            if (Ec) return std::format("'{}' could not be renamed: {}", Old, Ec.message());
        }
        if (pEntry) pEntry->m_Path = New;
        return {};
    }

    //----------------------------------------------------------------------------------------------------------------------
    // Set the content of a file that is in the module (the descriptor does not change)
    //----------------------------------------------------------------------------------------------------------------------
    inline std::string SetContent(const std::filesystem::path& Desc, std::string_view PathIn, const std::string& Content, std::string& Previous) noexcept
    {
        const std::string Path = NormalizeRelative(PathIn);
        if (Path.empty()) return std::format("'{}' is not a path inside source_db", PathIn);
        const auto Abs = Absolute(Desc, Path);
        std::error_code Ec;
        if (!std::filesystem::is_regular_file(Abs, Ec)) return std::format("'{}' does not exist - add the file first", Path);
        if (!ReadAll(Abs, Previous)) return std::format("'{}' could not be read", Path);
        if (!WriteAll(Abs, Content)) return std::format("'{}' could not be written", Path);
        return {};
    }

    //----------------------------------------------------------------------------------------------------------------------
    // Folders. They are real folders of source_db; the descriptor lists files, so an empty folder is only on disk.
    //----------------------------------------------------------------------------------------------------------------------
    inline std::string NewFolder(const std::filesystem::path& Desc, std::string_view PathIn) noexcept
    {
        const std::string Path = NormalizeRelative(PathIn);
        if (Path.empty()) return std::format("'{}' cannot be a folder inside source_db", PathIn);
        std::error_code Ec;
        const auto Abs = Absolute(Desc, Path);
        if (std::filesystem::exists(Abs, Ec)) return std::format("'{}' already exists", Path);
        std::filesystem::create_directories(Abs, Ec);
        return Ec ? std::format("'{}' could not be created: {}", Path, Ec.message()) : std::string();
    }
    inline std::string UndoNewFolder(const std::filesystem::path& Desc, std::string_view Path) noexcept
    {
        std::error_code Ec;
        std::filesystem::remove(Absolute(Desc, Path), Ec);              // only if it is still empty
        return {};
    }

    // Every file under a folder (any kind, listed or not) goes with it; its undo puts them all back. The files are captured as they are BEFORE anything is removed
    // (their places in the list are the original ones), so the undo restores them in the order of those places.
    struct removed_folder { std::string m_Path; std::vector<removed> m_Files; std::vector<std::string> m_Folders; };

    inline std::string CaptureFolder(const descriptor& D, const std::filesystem::path& Desc, std::string_view PathIn, removed_folder& Out) noexcept
    {
        const std::string Path = NormalizeRelative(PathIn);
        if (Path.empty()) return std::format("'{}' is not a folder inside source_db", PathIn);
        const auto Abs = Absolute(Desc, Path);
        std::error_code Ec;
        if (!std::filesystem::is_directory(Abs, Ec)) return std::format("'{}' is not a folder of the module", Path);
        Out = {}; Out.m_Path = Path;
        const auto Root = SourceDb(Desc);
        std::vector<std::string> Paths;
        for (auto It = std::filesystem::recursive_directory_iterator(Abs, Ec); !Ec && It != std::filesystem::recursive_directory_iterator(); It.increment(Ec))
        {
            const std::string Rel = std::filesystem::relative(It->path(), Root, Ec).generic_string();
            if (It->is_directory(Ec)) Out.m_Folders.push_back(Rel); else Paths.push_back(Rel);
        }
        for (const auto& F : D.m_Files)                                    // listed files whose file is gone go too (nothing to restore but the entry)
            if (F.m_Path.size() > Path.size() && F.m_Path[Path.size()] == '/' && SamePath(std::string_view(F.m_Path).substr(0, Path.size()), Path) && std::find(Paths.begin(), Paths.end(), F.m_Path) == Paths.end())
                Paths.push_back(F.m_Path);
        for (const auto& Rel : Paths)
        {
            removed R;
            if (auto Err = CaptureFile(D, Desc, Rel, R); !Err.empty()) return Err;
            Out.m_Files.push_back(std::move(R));
        }
        return {};
    }
    inline std::string RemoveFolder(descriptor& D, const std::filesystem::path& Desc, std::string_view PathIn, removed_folder& Out) noexcept
    {
        if (auto Err = CaptureFolder(D, Desc, PathIn, Out); !Err.empty()) return Err;
        for (const auto& R : Out.m_Files) std::erase_if(D.m_Files, [&](const file& F) { return SamePath(F.m_Path, R.m_Path); });
        std::error_code Ec;
        std::filesystem::remove_all(Absolute(Desc, Out.m_Path), Ec);
        return Ec ? std::format("'{}' could not be deleted: {}", Out.m_Path, Ec.message()) : std::string();
    }
    inline std::string RestoreFolder(descriptor& D, const std::filesystem::path& Desc, const removed_folder& R) noexcept
    {
        std::error_code Ec;
        std::filesystem::create_directories(Absolute(Desc, R.m_Path), Ec);
        for (const auto& Folder : R.m_Folders) std::filesystem::create_directories(Absolute(Desc, Folder), Ec);
        std::vector<const removed*> Order;
        for (const auto& F : R.m_Files) Order.push_back(&F);
        std::stable_sort(Order.begin(), Order.end(), [](const removed* A, const removed* B) { return A->m_Index < B->m_Index; });
        for (const auto* pFile : Order)
            if (auto Err = RestoreFile(D, Desc, *pFile); !Err.empty()) return Err;
        return {};
    }

    // A folder renamed or moved with everything in it, listed or not: one rename on disk, and the listed paths under it follow.
    inline std::string RenameFolder(descriptor& D, const std::filesystem::path& Desc, std::string_view FromIn, std::string_view ToIn) noexcept
    {
        const std::string From = NormalizeRelative(FromIn), To = NormalizeRelative(ToIn);
        if (From.empty()) return std::format("'{}' is not a folder inside source_db", FromIn);
        if (To.empty()) return std::format("'{}' cannot be a folder inside source_db", ToIn);
        const auto AbsFrom = Absolute(Desc, From), AbsTo = Absolute(Desc, To);
        std::error_code Ec;
        if (!std::filesystem::is_directory(AbsFrom, Ec)) return std::format("'{}' is not a folder of the module", From);
        if (To.size() > From.size() && To[From.size()] == '/' && SamePath(std::string_view(To).substr(0, From.size()), From)) return std::format("a folder cannot be moved into itself ('{}')", To);
        if (!SamePath(From, To) && std::filesystem::exists(AbsTo, Ec)) return std::format("'{}' already exists", To);
        std::filesystem::create_directories(AbsTo.parent_path(), Ec);
        if (SamePath(From, To) && From != To)                     // only the case of the name changes
        {
            const auto Temp = std::filesystem::path(AbsFrom).concat(L".renaming");
            std::filesystem::rename(AbsFrom, Temp, Ec);
            if (!Ec) std::filesystem::rename(Temp, AbsTo, Ec);
        }
        else std::filesystem::rename(AbsFrom, AbsTo, Ec);
        if (Ec) return std::format("'{}' could not be renamed: {}", From, Ec.message());
        for (auto& F : D.m_Files)
            if (F.m_Path.size() > From.size() && F.m_Path[From.size()] == '/' && SamePath(std::string_view(F.m_Path).substr(0, From.size()), From)) F.m_Path = To + F.m_Path.substr(From.size());
        return {};
    }

    inline std::string SetExclude(descriptor& D, std::string_view PathIn, bool bExclude, bool& Previous) noexcept
    {
        auto* pFile = FindFile(D, NormalizeRelative(PathIn));
        if (!pFile) return std::format("'{}' is not listed in the module", PathIn);
        Previous = pFile->m_bExclude;
        pFile->m_bExclude = bExclude;
        return {};
    }
}

#endif // XSCRIPT_MODULE_OPS_H
