#ifndef XSCRIPT_MODULE_EDITOR_COMMANDS_H
#define XSCRIPT_MODULE_EDITOR_COMMANDS_H
#pragma once

// The commands of the script module editor (run as "<module name>\Command ..."; `list` shows the names). Every change of the module is a command - the tree's
// menu, a drag, a key and an AI all run the same ones - and every one is undoable: the descriptor and the files on disk change together (xscript_module_ops.h) and
// the undo puts both back. The queries say what the window shows (the files with their source control state, the tabs that are open) and open or close a file's
// viewer, so a script can do anything a person does with the mouse.
#include "source/Tools/Editor/xeditor_document_editor.h"
#include "plugins/xscript_module.plugin/source/Module/xscript_module_ops.h"

namespace xscript::module_editor
{
    // What the commands need of the editor. The editor implements it; keeping it apart is what lets the commands be written before the window is.
    struct module_api
    {
        virtual ~module_api() = default;
        virtual xscript::module::descriptor&    Descriptor()         noexcept = 0;
        virtual const std::filesystem::path&    Folder()       const noexcept = 0;
        virtual xeditor::file_document&         Document()           noexcept = 0;
        virtual void                            Commit()             noexcept = 0;     // the descriptor changed: save it, regenerate the game project, refresh the tree
        virtual std::string                     OpenFile(const std::string& Path, int Line) noexcept = 0;     // "" or why not
        virtual bool                            CloseFile(const std::string& Path) noexcept = 0;
        virtual std::string                     ZoomFile(const std::string& Path, float Notches, float Size) noexcept = 0;     // "" or why not; Size > 0 sets the text's size, else Notches change it (what the wheel does)
        virtual void                            RenameView(const std::string& From, const std::string& To) noexcept = 0;
        virtual std::string                     ListFiles()          noexcept = 0;
        virtual std::string                     ListOpen()           noexcept = 0;
        virtual std::string                     ListTree()           noexcept = 0;
        virtual std::string                     ExportCMake(const std::string& File) noexcept = 0;     // "" File = module.cmake next to the descriptor; returns what happened
        virtual std::string                     SelectNode(const std::string& Path) noexcept = 0;     // "" or why not
    };

    namespace cmd
    {
        struct base_edit : xundo::command_base
        {
            module_api& m_Api;
            base_edit(xundo::system& System, module_api& Api, const char* pName) noexcept : xundo::command_base(System, pName, nullptr), m_Api(Api) {}
            std::string Text(xcmdline::parser::handle H) const noexcept
            {
                auto A = m_Parser.getOptionArgAs<std::string>(H, 0);
                return std::holds_alternative<xerr>(A) ? std::string() : std::get<std::string>(A);
            }
        };
        struct base_query : xundo::query_command_base
        {
            module_api& m_Api;
            base_query(xundo::system& System, module_api& Api, const char* pName) noexcept : xundo::query_command_base(System, pName, nullptr), m_Api(Api) {}
            std::string Text(xcmdline::parser::handle H) const noexcept
            {
                auto A = m_Parser.getOptionArgAs<std::string>(H, 0);
                return std::holds_alternative<xerr>(A) ? std::string() : std::get<std::string>(A);
            }
        };
        inline std::string Prefixed(const char* pName, const std::string& Err) { return Err.empty() ? Err : std::string(pName) + ": " + Err; }

        //----------------------------------------------------------------------------------------------------------------------
        struct add_file : base_edit
        {
            add_file(xundo::system& S, module_api& A) noexcept : base_edit(S, A, "AddFile") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Adds a file to the module (undoable): created from a template, or a file of the folder that was not listed. Usage: AddFile -Path \"Systems/Foo.h\" [-Template header|source|empty]"; }
            void RegisterArguments() noexcept override
            {
                m_hPath = m_Parser.addOption("Path", "Path inside source_db, e.g. \"Systems/Foo.h\"", true, 1);
                m_hTemplate = m_Parser.addOption("Template", "header (#pragma once), source (includes its own header) or empty; default by the extension", false, 1);
            }
            std::string Redo() noexcept override
            {
                const auto Path = Text(m_hPath);
                xscript::module::ops::added Added;
                const auto Err = xscript::module::ops::AddFile(m_Api.Descriptor(), m_Api.Folder(), Path, xscript::module::ops::TemplateFor(Text(m_hTemplate), xscript::module::NormalizeRelative(Path)), Added);
                if (Err.empty()) m_Api.Commit();
                return Prefixed("AddFile", Err);
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                const auto Path = xscript::module::NormalizeRelative(Text(m_hPath));
                std::error_code Ec;
                const std::uint8_t bExisted = !Path.empty() && std::filesystem::exists(xscript::module::Absolute(m_Api.Folder(), Path), Ec);
                File.Write(bExisted); xeditor::WriteString(File, Path);
            }
            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bExisted = 0; File.Read(bExisted); const auto Path = xeditor::ReadString(File);
                m_Api.CloseFile(Path);
                xscript::module::ops::UndoAdd(m_Api.Descriptor(), m_Api.Folder(), { Path, bExisted == 0 });
                m_Api.Commit();
            }
            xcmdline::parser::handle m_hPath, m_hTemplate;
        };

        //----------------------------------------------------------------------------------------------------------------------
        inline void WriteRemoved(xundo::undo_file& File, const xscript::module::ops::removed& R) noexcept
        {
            xeditor::WriteString(File, R.m_Path); xeditor::WriteString(File, R.m_Content);
            const std::uint64_t Index = R.m_Index; File.Write(Index);
            const std::uint8_t Flags = static_cast<std::uint8_t>((R.m_bExclude ? 1 : 0) | (R.m_bWasListed ? 2 : 0) | (R.m_bHadFile ? 4 : 0)); File.Write(Flags);
        }
        inline xscript::module::ops::removed ReadRemoved(xundo::undo_file& File) noexcept
        {
            xscript::module::ops::removed R;
            R.m_Path = xeditor::ReadString(File); R.m_Content = xeditor::ReadString(File);
            std::uint64_t Index = 0; File.Read(Index); R.m_Index = static_cast<std::size_t>(Index);
            std::uint8_t Flags = 0; File.Read(Flags); R.m_bExclude = Flags & 1; R.m_bWasListed = Flags & 2; R.m_bHadFile = Flags & 4;
            return R;
        }

        struct remove_file : base_edit
        {
            remove_file(xundo::system& S, module_api& A) noexcept : base_edit(S, A, "RemoveFile") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Removes a file from the module and deletes it (undoable - restores its exact content and place). Usage: RemoveFile -Path \"Systems/Foo.h\""; }
            void RegisterArguments() noexcept override { m_hPath = m_Parser.addOption("Path", "Path inside source_db", true, 1); }
            std::string Redo() noexcept override
            {
                const auto Path = xscript::module::NormalizeRelative(Text(m_hPath));
                m_Api.CloseFile(Path);
                xscript::module::ops::removed R;
                const auto Err = xscript::module::ops::RemoveFile(m_Api.Descriptor(), m_Api.Folder(), Text(m_hPath), R);
                if (Err.empty()) m_Api.Commit();
                return Prefixed("RemoveFile", Err);
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                xscript::module::ops::removed R;
                xscript::module::ops::CaptureFile(m_Api.Descriptor(), m_Api.Folder(), Text(m_hPath), R);
                WriteRemoved(File, R);
            }
            void Undo(xundo::undo_file& File) noexcept override
            {
                const auto R = ReadRemoved(File);
                xscript::module::ops::RestoreFile(m_Api.Descriptor(), m_Api.Folder(), R);
                m_Api.Commit();
            }
            xcmdline::parser::handle m_hPath;
        };

        //----------------------------------------------------------------------------------------------------------------------
        struct rename_file : base_edit
        {
            rename_file(xundo::system& S, module_api& A) noexcept : base_edit(S, A, "RenameFile") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Renames or moves a file of the module (undoable; an open viewer follows it). Usage: RenameFile -Path \"Foo.h\" -To \"Systems/Foo.h\""; }
            void RegisterArguments() noexcept override
            {
                m_hPath = m_Parser.addOption("Path", "Current path inside source_db", true, 1);
                m_hTo   = m_Parser.addOption("To",   "New path inside source_db",     true, 1);
            }
            std::string Redo() noexcept override
            {
                const auto From = xscript::module::NormalizeRelative(Text(m_hPath)), To = xscript::module::NormalizeRelative(Text(m_hTo));
                const auto Err = xscript::module::ops::RenameFile(m_Api.Descriptor(), m_Api.Folder(), From, To);
                if (Err.empty()) { m_Api.RenameView(From, To); m_Api.Commit(); }
                return Prefixed("RenameFile", Err);
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override { xeditor::WriteString(File, xscript::module::NormalizeRelative(Text(m_hPath))); xeditor::WriteString(File, xscript::module::NormalizeRelative(Text(m_hTo))); }
            void Undo(xundo::undo_file& File) noexcept override
            {
                const auto From = xeditor::ReadString(File), To = xeditor::ReadString(File);
                if (xscript::module::ops::RenameFile(m_Api.Descriptor(), m_Api.Folder(), To, From).empty()) { m_Api.RenameView(To, From); m_Api.Commit(); }
            }
            xcmdline::parser::handle m_hPath, m_hTo;
        };

        //----------------------------------------------------------------------------------------------------------------------
        struct new_folder : base_edit
        {
            new_folder(xundo::system& S, module_api& A) noexcept : base_edit(S, A, "NewFolder") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Creates a folder in the module's source_db (undoable). Usage: NewFolder -Path \"Systems\""; }
            void RegisterArguments() noexcept override { m_hPath = m_Parser.addOption("Path", "Folder path inside source_db", true, 1); }
            std::string Redo() noexcept override { const auto Err = xscript::module::ops::NewFolder(m_Api.Folder(), Text(m_hPath)); if (Err.empty()) m_Api.Commit(); return Prefixed("NewFolder", Err); }
            void BackupCurrenState(xundo::undo_file& File) noexcept override { xeditor::WriteString(File, xscript::module::NormalizeRelative(Text(m_hPath))); }
            void Undo(xundo::undo_file& File) noexcept override { xscript::module::ops::UndoNewFolder(m_Api.Folder(), xeditor::ReadString(File)); m_Api.Commit(); }
            xcmdline::parser::handle m_hPath;
        };

        struct rename_folder : base_edit
        {
            rename_folder(xundo::system& S, module_api& A) noexcept : base_edit(S, A, "RenameFolder") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Renames or moves a folder of the module with everything in it (undoable; open viewers follow). Usage: RenameFolder -Path \"Systems\" -To \"Game/Systems\""; }
            void RegisterArguments() noexcept override
            {
                m_hPath = m_Parser.addOption("Path", "Current folder path inside source_db", true, 1);
                m_hTo   = m_Parser.addOption("To",   "New folder path inside source_db",     true, 1);
            }
            std::string Redo() noexcept override
            {
                const auto From = xscript::module::NormalizeRelative(Text(m_hPath)), To = xscript::module::NormalizeRelative(Text(m_hTo));
                const auto Err = xscript::module::ops::RenameFolder(m_Api.Descriptor(), m_Api.Folder(), From, To);
                if (Err.empty()) { m_Api.RenameView(From, To); m_Api.Commit(); }
                return Prefixed("RenameFolder", Err);
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override { xeditor::WriteString(File, xscript::module::NormalizeRelative(Text(m_hPath))); xeditor::WriteString(File, xscript::module::NormalizeRelative(Text(m_hTo))); }
            void Undo(xundo::undo_file& File) noexcept override
            {
                const auto From = xeditor::ReadString(File), To = xeditor::ReadString(File);
                if (xscript::module::ops::RenameFolder(m_Api.Descriptor(), m_Api.Folder(), To, From).empty()) { m_Api.RenameView(To, From); m_Api.Commit(); }
            }
            xcmdline::parser::handle m_hPath, m_hTo;
        };

        struct remove_folder : base_edit
        {
            remove_folder(xundo::system& S, module_api& A) noexcept : base_edit(S, A, "RemoveFolder") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Removes a folder of the module with every file in it (undoable - restores them all). Usage: RemoveFolder -Path \"Systems\""; }
            void RegisterArguments() noexcept override { m_hPath = m_Parser.addOption("Path", "Folder path inside source_db", true, 1); }
            std::string Redo() noexcept override
            {
                xscript::module::ops::removed_folder R;
                if (auto Err = xscript::module::ops::CaptureFolder(m_Api.Descriptor(), m_Api.Folder(), Text(m_hPath), R); !Err.empty()) return Prefixed("RemoveFolder", Err);
                for (const auto& F : R.m_Files) m_Api.CloseFile(F.m_Path);
                const auto Err = xscript::module::ops::RemoveFolder(m_Api.Descriptor(), m_Api.Folder(), Text(m_hPath), R);
                if (Err.empty()) m_Api.Commit();
                return Prefixed("RemoveFolder", Err);
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                xscript::module::ops::removed_folder R;
                xscript::module::ops::CaptureFolder(m_Api.Descriptor(), m_Api.Folder(), Text(m_hPath), R);
                xeditor::WriteString(File, R.m_Path);
                const std::uint32_t nFolders = static_cast<std::uint32_t>(R.m_Folders.size()); File.Write(nFolders);
                for (const auto& F : R.m_Folders) xeditor::WriteString(File, F);
                const std::uint32_t nFiles = static_cast<std::uint32_t>(R.m_Files.size()); File.Write(nFiles);
                for (const auto& F : R.m_Files) WriteRemoved(File, F);
            }
            void Undo(xundo::undo_file& File) noexcept override
            {
                xscript::module::ops::removed_folder R;
                R.m_Path = xeditor::ReadString(File);
                std::uint32_t nFolders = 0; File.Read(nFolders);
                for (std::uint32_t i = 0; i < nFolders; ++i) R.m_Folders.push_back(xeditor::ReadString(File));
                std::uint32_t nFiles = 0; File.Read(nFiles);
                for (std::uint32_t i = 0; i < nFiles; ++i) R.m_Files.push_back(ReadRemoved(File));
                xscript::module::ops::RestoreFolder(m_Api.Descriptor(), m_Api.Folder(), R);
                m_Api.Commit();
            }
            xcmdline::parser::handle m_hPath;
        };

        //----------------------------------------------------------------------------------------------------------------------
        struct exclude_file : base_edit
        {
            exclude_file(xundo::system& S, module_api& A) noexcept : base_edit(S, A, "ExcludeFile") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Keeps a file in the module but leaves it out of the build, or puts it back (undoable). Usage: ExcludeFile -Path \"Foo.cpp\" [-Value true|false]"; }
            void RegisterArguments() noexcept override
            {
                m_hPath  = m_Parser.addOption("Path",  "Path inside source_db", true, 1);
                m_hValue = m_Parser.addOption("Value", "true (default) leaves the file out of the build; false builds it", false, 1);
            }
            std::string Redo() noexcept override
            {
                bool bPrevious = false;
                const auto Err = xscript::module::ops::SetExclude(m_Api.Descriptor(), Text(m_hPath), Text(m_hValue) != "false", bPrevious);
                if (Err.empty()) m_Api.Commit();
                return Prefixed("ExcludeFile", Err);
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                const auto Path = xscript::module::NormalizeRelative(Text(m_hPath));
                const auto* pFile = xscript::module::FindFile(static_cast<const xscript::module::descriptor&>(m_Api.Descriptor()), Path);
                const std::uint8_t bPrevious = pFile && pFile->m_bExclude;
                File.Write(bPrevious); xeditor::WriteString(File, Path);
            }
            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bPrevious = 0; File.Read(bPrevious); const auto Path = xeditor::ReadString(File);
                bool bIgnored = false;
                if (xscript::module::ops::SetExclude(m_Api.Descriptor(), Path, bPrevious != 0, bIgnored).empty()) m_Api.Commit();
            }
            xcmdline::parser::handle m_hPath, m_hValue;
        };

        //----------------------------------------------------------------------------------------------------------------------
        struct rescan : base_edit
        {
            rescan(xundo::system& S, module_api& A) noexcept : base_edit(S, A, "Rescan") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Adds the files that are in the folder and not in the module and, with -RemoveMissing, drops the listed files that are gone (undoable). Usage: Rescan [-RemoveMissing true]"; }
            void RegisterArguments() noexcept override { m_hRemove = m_Parser.addOption("RemoveMissing", "true: also drop the listed files that are not on disk", false, 1); }
            std::string Redo() noexcept override
            {
                const auto Result = xscript::module::Sync(m_Api.Descriptor(), m_Api.Folder(), Text(m_hRemove) == "true");
                for (const auto& Path : Result.m_Removed) m_Api.CloseFile(Path);
                m_Api.Commit();
                return {};
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override { xeditor::WriteString(File, m_Api.Document().Snapshot()); }
            void Undo(xundo::undo_file& File) noexcept override { m_Api.Document().Restore(xeditor::ReadString(File)); m_Api.Commit(); }
            xcmdline::parser::handle m_hRemove;
        };

        //----------------------------------------------------------------------------------------------------------------------
        // Queries: what the window shows, and the viewers
        //----------------------------------------------------------------------------------------------------------------------
        struct list_files : base_query
        {
            list_files(xundo::system& S, module_api& A) noexcept : base_query(S, A, "ListFiles") {}
            const char* getCommandHelp() const noexcept override { return "The module's files: path, kind, excluded, state (ok, missing, unlisted), source control (clean, modified, staged, untracked, conflicted, unknown), lock, whether a viewer is open, bytes. Usage: ListFiles"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override { return m_Api.ListFiles(); }
        };
        struct open_file : base_query
        {
            open_file(xundo::system& S, module_api& A) noexcept : base_query(S, A, "OpenFile") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Opens the viewer of a file in its own tab (one tab per file, ever: an open file is just brought to the front) and goes to a line. Usage: OpenFile -Path \"Foo.h\" [-Line n]"; }
            void RegisterArguments() noexcept override
            {
                m_hPath = m_Parser.addOption("Path", "Path inside source_db", true, 1);
                m_hLine = m_Parser.addOption("Line", "1-based line to go to", false, 1);
            }
            std::string Query() noexcept override
            {
                const auto LineText = Text(m_hLine);
                const int Line = LineText.empty() ? 0 : std::atoi(LineText.c_str());
                const auto Err = m_Api.OpenFile(Text(m_hPath), Line);
                return Err.empty() ? "OpenFile: " + xscript::module::NormalizeRelative(Text(m_hPath)) + " is open" : "OpenFile: " + Err;
            }
            xcmdline::parser::handle m_hPath, m_hLine;
        };
        struct zoom_file : base_query
        {
            zoom_file(xundo::system& S, module_api& A) noexcept : base_query(S, A, "ZoomFile") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Zooms the text of a file's viewer, as Ctrl + the mouse wheel does (the size is in ListOpenFiles). Usage: ZoomFile -Path \"Foo.h\" [-By notches (+ bigger, one pixel each)] [-Size pixels (0 = the default size)]"; }
            void RegisterArguments() noexcept override
            {
                m_hPath = m_Parser.addOption("Path", "Path inside source_db", true, 1);
                m_hBy   = m_Parser.addOption("By", "Wheel notches: positive is bigger", false, 1);
                m_hSize = m_Parser.addOption("Size", "The text's size in pixels; 0 is the default size", false, 1);
            }
            std::string Query() noexcept override
            {
                const auto By = Text(m_hBy), Size = Text(m_hSize);
                if (By.empty() && Size.empty()) return "ZoomFile: say -By (notches) or -Size (pixels)";
                const auto Err = m_Api.ZoomFile(Text(m_hPath), By.empty() ? 0.0f : static_cast<float>(std::atof(By.c_str())), Size.empty() ? -1.0f : static_cast<float>(std::atof(Size.c_str())));
                return Err.empty() ? "ZoomFile: ok" : "ZoomFile: " + Err;
            }
            xcmdline::parser::handle m_hPath, m_hBy, m_hSize;
        };
        struct close_file : base_query
        {
            close_file(xundo::system& S, module_api& A) noexcept : base_query(S, A, "CloseFile") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Closes the viewer of a file. Usage: CloseFile -Path \"Foo.h\""; }
            void RegisterArguments() noexcept override { m_hPath = m_Parser.addOption("Path", "Path inside source_db", true, 1); }
            std::string Query() noexcept override { return m_Api.CloseFile(Text(m_hPath)) ? "CloseFile: closed" : "CloseFile: that file has no viewer open"; }
            xcmdline::parser::handle m_hPath;
        };
        struct export_cmake : base_query
        {
            export_cmake(xundo::system& S, module_api& A) noexcept : base_query(S, A, "ExportCMake") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Writes the module as a CMake file for a project that builds the game without the editor: include it and call <module>_apply(<target>). Default file: module.cmake next to the descriptor. Usage: ExportCMake [-File path]"; }
            void RegisterArguments() noexcept override { m_hFile = m_Parser.addOption("File", "Where to write it (default: module.cmake in the module's folder)", false, 1); }
            std::string Query() noexcept override { return m_Api.ExportCMake(Text(m_hFile)); }
            xcmdline::parser::handle m_hFile;
        };
        struct list_tree : base_query
        {
            list_tree(xundo::system& S, module_api& A) noexcept : base_query(S, A, "ListTree") {}
            const char* getCommandHelp() const noexcept override { return "The rows the Sources tree drew last frame - path, kind, depth, whether the folder is expanded, whether it is selected and where it is on the screen - and what the tree is doing (a dialog open, a row being renamed). Usage: ListTree"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override { return m_Api.ListTree(); }
        };
        struct select_node : base_query
        {
            select_node(xundo::system& S, module_api& A) noexcept : base_query(S, A, "SelectFile") { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Selects a file or folder in the Sources tree, as a click does (the keys F2, Del and Enter act on it). Usage: SelectFile -Path \"Systems/Foo.h\""; }
            void RegisterArguments() noexcept override { m_hPath = m_Parser.addOption("Path", "Path inside source_db (\"\" is the module)", true, 1); }
            std::string Query() noexcept override { const auto Err = m_Api.SelectNode(Text(m_hPath)); return Err.empty() ? "SelectFile: selected" : "SelectFile: " + Err; }
            xcmdline::parser::handle m_hPath;
        };
        struct list_open : base_query
        {
            list_open(xundo::system& S, module_api& A) noexcept : base_query(S, A, "ListOpenFiles") {}
            const char* getCommandHelp() const noexcept override { return "The files whose viewer is open, in the order they were opened (the first column says which tab is in front). Usage: ListOpenFiles"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override { return m_Api.ListOpen(); }
        };
    }
}

#endif // XSCRIPT_MODULE_EDITOR_COMMANDS_H
