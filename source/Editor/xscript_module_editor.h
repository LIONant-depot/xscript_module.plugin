#ifndef XSCRIPT_MODULE_EDITOR_H
#define XSCRIPT_MODULE_EDITOR_H
#pragma once

// The script module editor: the window of a ScriptModule resource. A module is source files; its descriptor (Descriptor.txt) lists them and the Scripting system turns the
// descriptors of the project's modules into the game's Visual Studio project, so the descriptor is what this editor edits:
//
//   Sources    the module's folders and files as a tree, like Solution Explorer: new file, new folder, rename (F2), delete (Del), drag to move, "not built" (exclude),
//              open (double click or Enter); each row shows its source control state and lock. Files that are in the folder and not in the module, and files that are listed
//              and gone, are shown as what they are.
//   (files)    one viewer window per open file - read-only C++ with the compiler's errors on their lines; the same file never opens twice. Code is written in Visual Studio.
//   Overview   what the module is made of, what is wrong with it, how the last build went.
//   Libraries  the third-party libraries and defines the module needs (the descriptor's own properties).
//
// Every change is a command (xscript_module_editor_commands.h), undoable, and the same one the tree, the keys and an AI run.
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "plugins/xscript_module.plugin/source/Editor/xscript_module_editor_commands.h"
#include "plugins/xscript_module.plugin/source/Module/xscript_module_export.h"
#include "plugins/xscript_module.plugin/source/Editor/xscript_module_cpp_view.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_commands_source_control.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_source_control_status.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_asset_browser.h"
#include "dependencies/xeditor/include/xeditor/host.h"
#include "dependencies/xeditor/include/xeditor/widgets.h"
#include "plugins/xlevel.plugin/source/Editor/game_module/LevelEditor_GamePlugin.h"

#include <array>
#include <map>

namespace xscript::module_editor
{
    using namespace xscript::module;

    // One row of the tree: a folder (Path is the folder's path, "" for the module itself) or a file.
    struct tree_node
    {
        std::string             m_Name, m_Path;
        bool                    m_bFolder = false, m_bListed = true, m_bMissing = false, m_bExcluded = false;
        std::vector<tree_node>  m_Children;
    };

    // What the source control layer knows about one path.
    struct sc_info
    {
        const char*                             m_pStatus = "unknown";      // clean, modified, staged, untracked, conflicted, unknown (not scanned yet)
        const char*                             m_pLock   = "none";         // none, mine, other
        xresource_editor::asset_status_badge    m_Badge     = xresource_editor::asset_status_badge::None;
        xresource_editor::asset_lock_badge      m_LockBadge = xresource_editor::asset_lock_badge::None;
    };

    struct session : xeditor::descriptor_editor, module_api
    {
        // ---- the module
        std::filesystem::path                       m_Folder;                   // the resource's .desc folder
        struct entry { std::string m_Path; bool m_bListed = true, m_bMissing = false, m_bExcluded = false; file_kind m_Kind = file_kind::Other; std::uintmax_t m_Bytes = 0; };
        std::vector<entry>                          m_Entries;                  // the listed files in the descriptor's order, then the ones only in the folder
        std::vector<std::string>                    m_Folders;                  // every folder of source_db
        tree_node                                   m_Tree;
        bool                                        m_bRebuild = true;
        double                                      m_NextCheck = 0.0, m_NextScan = 0.0;
        std::vector<std::string>                    m_UsedBy;                   // the Games of the project that list this module (read every half second, not every frame)
        std::filesystem::file_time_type             m_DescriptorStamp{};

        // ---- the viewers
        std::vector<std::unique_ptr<cpp_view>>      m_Views;                    // in the order they were opened
        std::string                                 m_Front;                    // the path of the viewer that was in front last
        ImGuiID                                     m_CenterDock = 0;           // the dock node of the middle: a new viewer opens there

        // ---- the tree's state
        std::string                                 m_Selected;                 // a file or folder path ("" = the module)
        bool                                        m_bSelectedFolder = true;
        std::string                                 m_Filter;
        bool                                        m_bChangesOnly = false;
        std::string                                 m_RenamePath;               // the row being renamed
        std::array<char, 256>                       m_RenameText{};
        bool                                        m_bRenameFocus = false;
        enum class dialog : std::uint8_t { None, NewFile, NewFolder, Delete, Revert };
        dialog                                      m_Dialog = dialog::None;
        bool                                        m_bDialogOpen = false;      // OpenPopup is called once, the frame the dialog is asked for
        std::string                                 m_DialogPath;               // the folder a new file goes in, or the row to delete or revert
        bool                                        m_bDialogFolder = false;
        std::array<char, 128>                       m_DialogText{};
        int                                         m_DialogTemplate = 0;       // 0 by extension, 1 header, 2 source, 3 empty
        std::string                                 m_Hover;
        std::uint64_t                               m_OpenerId = 0;             // the Logs' "Open source" and F8 land here for the files of this module

        // ---- the commands
        cmd::add_file       m_AddFile;
        cmd::remove_file    m_RemoveFile;
        cmd::rename_file    m_RenameFile;
        cmd::new_folder     m_NewFolder;
        cmd::rename_folder  m_RenameFolder;
        cmd::remove_folder  m_RemoveFolder;
        cmd::exclude_file   m_ExcludeFile;
        cmd::rescan         m_Rescan;
        cmd::list_files     m_ListFiles;
        cmd::open_file      m_OpenFile;
        cmd::close_file     m_CloseFile;
        cmd::zoom_file      m_ZoomFile;
        cmd::list_open      m_ListOpen;
        cmd::list_tree      m_ListTree;
        cmd::export_cmake   m_ExportCMake;
        cmd::select_node    m_SelectNode;

        struct drawn_row { std::string m_Path; bool m_bFolder = false, m_bExpanded = false, m_bSelected = false; int m_Depth = 0; float m_X = 0, m_Y = 0; };
        std::vector<drawn_row> m_Drawn;                                             // what the tree drew last frame
        int                    m_Depth = 0;

        session(xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : descriptor_editor("ScriptModule", Guid, LibraryGuid, pDevice)
            , m_AddFile(m_Undo, *this), m_RemoveFile(m_Undo, *this), m_RenameFile(m_Undo, *this), m_NewFolder(m_Undo, *this), m_RenameFolder(m_Undo, *this), m_RemoveFolder(m_Undo, *this)
            , m_ExcludeFile(m_Undo, *this), m_Rescan(m_Undo, *this), m_ListFiles(m_Undo, *this), m_OpenFile(m_Undo, *this), m_CloseFile(m_Undo, *this), m_ZoomFile(m_Undo, *this), m_ListOpen(m_Undo, *this), m_ListTree(m_Undo, *this), m_SelectNode(m_Undo, *this), m_ExportCMake(m_Undo, *this)
        {
            m_Folder = std::filesystem::path(m_Document.m_DescriptorPath).parent_path();
            if (!m_Folder.empty() && !std::filesystem::exists(DescriptorFile(m_Folder)))          // a module from before the descriptors: written from its folder (the generator does the same)
            {
                LoadOrMigrate(m_Folder, /*bWriteMigration*/ true);
                m_Document.Load();
                BindDescriptorInspector();
            }
            std::error_code Ec;
            m_DescriptorStamp = std::filesystem::last_write_time(DescriptorFile(m_Folder), Ec);
            m_OpenerId = xeditor::AddFileOpener([this](const xlog::ref& R) noexcept { return OpenFileOfThisModule(R); });
            AddPanel("Sources",   dock::left,   [this] { RenderSources(); });
            AddPanel("Overview",  dock::center, [this] { RenderOverview(); });
            AddPanel("Libraries", dock::bottom, [this] { RenderLibraries(); });
        }

        ~session() noexcept override { xeditor::RemoveFileOpener(m_OpenerId); }

        // A file of the Logs ("Open source", F8, a build error) that is in this module's source_db is shown in its viewer, at the line.
        bool OpenFileOfThisModule(const xlog::ref& R) noexcept
        {
            if (R.m_Type != xlog::ref::type::File || R.m_Path.empty() || !m_Document.isLoaded()) return false;
            std::error_code Ec;
            const std::string Abs = std::filesystem::weakly_canonical(std::filesystem::path(R.m_Path), Ec).generic_string();
            const std::string Root = std::filesystem::weakly_canonical(SourceDb(m_Folder), Ec).generic_string() + "/";
            if (Ec || Abs.size() <= Root.size() || !SamePath(std::string_view(Abs).substr(0, Root.size()), Root)) return false;
            if (!OpenFile(Abs.substr(Root.size()), R.m_Line).empty()) return false;                // not a C++ file of the module: the system gets it
            Focus();                                                                                // the editor's tab to the front, the viewer in it
            return true;
        }

        // =============================================================================================================
        // module_api: what the commands use
        // =============================================================================================================
        descriptor& Descriptor() noexcept override
        {
            static descriptor Nothing;                                           // a module whose descriptor could not be read: the commands answer instead of crashing
            return m_Document.m_pDescriptor ? static_cast<descriptor&>(*m_Document.m_pDescriptor) : Nothing;
        }
        const std::filesystem::path& Folder() const noexcept override { return m_Folder; }
        xeditor::file_document& Document() noexcept override { return m_Document; }

        void Commit() noexcept override
        {
            if (!m_Document.isLoaded()) return;
            std::string Error;
            if (!Write(m_Folder, Descriptor(), &Error)) xeditor::NotifyToast("The module's descriptor could not be saved: " + Error);
            m_Document.m_bDirty = false;
            std::error_code Ec;
            m_DescriptorStamp = std::filesystem::last_write_time(DescriptorFile(m_Folder), Ec);
            m_bRebuild = true;
            m_NextScan = 0.0;                                                     // new files: ask source control about them soon
            // the resource pipeline sees the new Descriptor.txt and compiles the module (and then the Game that lists it)
        }

        std::string OpenFile(const std::string& PathIn, int Line) noexcept override
        {
            const std::string Path = NormalizeRelative(PathIn);
            std::error_code Ec;
            if (Path.empty() || KindOf(Path) == file_kind::Other || (!FindFile(static_cast<const descriptor&>(Descriptor()), Path) && !std::filesystem::is_regular_file(Absolute(m_Folder, Path), Ec)))
                return std::format("'{}' is not a C++ file of this module", PathIn);
            for (auto& V : m_Views)
                if (SamePath(V->m_Path, Path)) { V->GoTo(Line); return {}; }                // one viewer per file, ever
            m_Views.push_back(std::make_unique<cpp_view>(Path, Absolute(m_Folder, Path), m_IdSuffix));
            m_Views.back()->GoTo(Line);
            return {};
        }
        std::string ZoomFile(const std::string& PathIn, float Notches, float Size) noexcept override
        {
            const std::string Path = NormalizeRelative(PathIn);
            for (auto& V : m_Views)
                if (SamePath(V->m_Path, Path)) { if (Size >= 0.0f) V->SetFontSize(Size); else V->ZoomBy(Notches); return {}; }
            return std::format("'{}' is not open", PathIn);
        }
        bool CloseFile(const std::string& PathIn) noexcept override
        {
            const std::string Path = NormalizeRelative(PathIn);
            const auto It = std::find_if(m_Views.begin(), m_Views.end(), [&](const auto& V) { return SamePath(V->m_Path, Path); });
            if (It == m_Views.end()) return false;
            m_Views.erase(It);
            return true;
        }
        // A file (or a folder with files in it) was renamed or moved: its viewers follow.
        void RenameView(const std::string& From, const std::string& To) noexcept override
        {
            for (auto& V : m_Views)
            {
                if (SamePath(V->m_Path, From)) V->SetPath(To, Absolute(m_Folder, To));
                else if (V->m_Path.size() > From.size() && V->m_Path[From.size()] == '/' && SamePath(std::string_view(V->m_Path).substr(0, From.size()), From))
                { const std::string New = To + V->m_Path.substr(From.size()); V->SetPath(New, Absolute(m_Folder, New)); }
                if (SamePath(m_Front, From)) m_Front = To;
            }
            if (SamePath(m_Selected, From)) m_Selected = To;
        }

        // =============================================================================================================
        // The module as the window shows it
        // =============================================================================================================
        std::wstring LibraryRoot() const noexcept { return xresource_editor::commands::ResolveLibraryRootPath(m_Document.m_LibraryGuid); }
        std::wstring LibraryRelative(const std::filesystem::path& Absolute) const noexcept
        {
            std::wstring Path = Absolute.wstring();
            const std::wstring Root = LibraryRoot();
            if (!Root.empty() && Path.size() > Root.size() && Path.compare(0, Root.size(), Root) == 0) Path.erase(0, Root.size());
            while (!Path.empty() && (Path.front() == L'\\' || Path.front() == L'/')) Path.erase(Path.begin());
            return Path;
        }

        sc_info SourceControlOf(const std::string& Path, bool bFolder) const noexcept
        {
            sc_info Info;
            const std::wstring Root = LibraryRoot();
            if (Root.empty()) return Info;
            const std::wstring Relative = LibraryRelative(Path.empty() ? SourceDb(m_Folder) : Absolute(m_Folder, Path));
            using badge = xresource_editor::asset_status_badge;
            if (bFolder)
            {
                const bool bPending = !xresource_editor::source_control::GetPendingPathsUnderFolder(Root, Relative).empty();
                Info.m_pStatus = bPending ? "modified" : xresource_editor::source_control::GetLastRefreshTime(Root) ? "clean" : "unknown";
                Info.m_Badge = bPending ? badge::Modified : badge::None;
                return Info;
            }
            if (auto Status = xresource_editor::source_control::GetCachedFileStatus(Root, Relative))
            {
                Info.m_pStatus = Status->conflicted ? "conflicted" : Status->untracked ? "untracked" : Status->staged && !Status->modified ? "staged" : "modified";
                Info.m_Badge = Status->untracked ? badge::Untracked : badge::Modified;
            }
            else if (xresource_editor::source_control::GetLastRefreshTime(Root)) { Info.m_pStatus = "clean"; Info.m_Badge = badge::Clean; }
            if (auto Lock = xresource_editor::source_control::GetCachedLockStatus(Root, Relative))
            {
                const bool bMine = Lock->ownership == sc::LockOwnership::CurrentUser;
                Info.m_pLock = bMine ? "mine" : "other";
                Info.m_LockBadge = bMine ? xresource_editor::asset_lock_badge::LockedByMe : xresource_editor::asset_lock_badge::LockedByOther;
            }
            return Info;
        }

        // The files of the descriptor and of the folder, as rows; rebuilt when something changed (a command, a change on disk found by the poll).
        void Rebuild() noexcept
        {
            m_bRebuild = false;
            m_Entries.clear(); m_Folders.clear();
            const auto& D = static_cast<const descriptor&>(Descriptor());
            std::error_code Ec;
            for (const auto& F : D.m_Files)
            {
                const std::string Path = NormalizeRelative(F.m_Path);
                if (Path.empty()) continue;
                entry E; E.m_Path = Path; E.m_bExcluded = F.m_bExclude; E.m_Kind = KindOf(Path);
                const auto Abs = Absolute(m_Folder, Path);
                E.m_bMissing = !std::filesystem::is_regular_file(Abs, Ec);
                if (!E.m_bMissing) E.m_Bytes = std::filesystem::file_size(Abs, Ec);
                m_Entries.push_back(std::move(E));
            }
            for (const auto& Path : ScanSourceDb(m_Folder))
            {
                if (FindFile(D, Path)) continue;
                entry E; E.m_Path = Path; E.m_bListed = false; E.m_Kind = KindOf(Path);
                E.m_Bytes = std::filesystem::file_size(Absolute(m_Folder, Path), Ec);
                m_Entries.push_back(std::move(E));
            }
            const auto Root = SourceDb(m_Folder);
            if (std::filesystem::is_directory(Root, Ec))
                for (auto It = std::filesystem::recursive_directory_iterator(Root, std::filesystem::directory_options::skip_permission_denied, Ec); !Ec && It != std::filesystem::recursive_directory_iterator(); It.increment(Ec))
                    if (It->is_directory(Ec)) m_Folders.push_back(std::filesystem::relative(It->path(), Root, Ec).generic_string());

            m_Tree = tree_node{}; m_Tree.m_bFolder = true;
            m_Tree.m_Name = xeditor::ResolveResourceDisplayName(m_Document.m_LibraryGuid, m_Document.m_Guid, "Script module");
            auto Place = [&](const std::string& Path, bool bFolder, const entry* pEntry)
            {
                tree_node* pNode = &m_Tree;
                std::size_t At = 0;
                std::string So_Far;
                while (At <= Path.size())
                {
                    const auto Slash = Path.find('/', At);
                    const std::string Part = Path.substr(At, Slash == std::string::npos ? std::string::npos : Slash - At);
                    const bool bLast = Slash == std::string::npos;
                    So_Far += (So_Far.empty() ? "" : "/") + Part;
                    auto It = std::find_if(pNode->m_Children.begin(), pNode->m_Children.end(), [&](const tree_node& N) { return N.m_Name == Part && N.m_bFolder == (!bLast || bFolder); });
                    if (It == pNode->m_Children.end())
                    {
                        tree_node N; N.m_Name = Part; N.m_Path = So_Far; N.m_bFolder = !bLast || bFolder;
                        pNode->m_Children.push_back(std::move(N));
                        It = std::prev(pNode->m_Children.end());
                    }
                    pNode = &*It;
                    if (bLast) break;
                    At = Slash + 1;
                }
                if (pEntry) { pNode->m_bListed = pEntry->m_bListed; pNode->m_bMissing = pEntry->m_bMissing; pNode->m_bExcluded = pEntry->m_bExcluded; }
            };
            for (const auto& Path : m_Folders) Place(Path, true, nullptr);
            for (const auto& E : m_Entries) Place(E.m_Path, false, &E);
            SortTree(m_Tree);
        }
        static void SortTree(tree_node& Node) noexcept
        {
            std::stable_sort(Node.m_Children.begin(), Node.m_Children.end(), [](const tree_node& A, const tree_node& B)
            {
                if (A.m_bFolder != B.m_bFolder) return A.m_bFolder;                   // folders first, like Solution Explorer
                return Lower(A.m_Name) < Lower(B.m_Name);
            });
            for (auto& C : Node.m_Children) SortTree(C);
        }

        // Once a frame, whether or not this tab is the one showing: what changed outside is picked up, so commands see the module as it is now.
        void Render() noexcept override
        {
            Housekeeping();
            descriptor_editor::Render();
        }
        void Housekeeping() noexcept
        {
            if (!m_Document.isLoaded()) return;
            const double Now = ImGui::GetTime();
            if (Now < m_NextCheck) return;
            m_NextCheck = Now + 0.5;
            {
                m_UsedBy = xlevel::GamesListingModule(m_Document.m_Guid.m_Instance.m_Value);
            }
            std::error_code Ec;
            const auto Stamp = std::filesystem::last_write_time(DescriptorFile(m_Folder), Ec);
            if (!Ec && Stamp != m_DescriptorStamp)                                    // Save, a workspace command or a hand edit wrote Descriptor.txt
            {
                m_DescriptorStamp = Stamp;
                if (!m_Document.m_bDirty) { m_Document.ReplaceFromFile(m_Document.m_DescriptorPath); BindDescriptorInspector(); }
                m_bRebuild = true;
            }
            m_bRebuild = true;                                                        // a file added or removed in Visual Studio shows up within the second
            if (Now >= m_NextScan)
            {
                m_NextScan = Now + 15.0;
                if (const auto Root = LibraryRoot(); !Root.empty()) xresource_editor::source_control::RequestPriorityScan(Root, LibraryRelative(SourceDb(m_Folder)));
            }
        }

        std::string ListFiles() noexcept override
        {
            if (m_bRebuild) Rebuild();
            std::size_t nUnlisted = 0, nMissing = 0;
            for (const auto& E : m_Entries) { nUnlisted += !E.m_bListed; nMissing += E.m_bMissing; }
            std::string Out = std::format("ListFiles: ok\nFiles={}  Unlisted={}  Missing={}  Open={}\n\nPath\tKind\tExcluded\tState\tSourceControl\tLock\tOpen\tBytes\n", m_Entries.size(), nUnlisted, nMissing, m_Views.size());
            for (const auto& E : m_Entries)
            {
                const auto Sc = SourceControlOf(E.m_Path, false);
                const bool bOpen = std::any_of(m_Views.begin(), m_Views.end(), [&](const auto& V) { return SamePath(V->m_Path, E.m_Path); });
                Out += std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", E.m_Path, E.m_Kind == file_kind::Compiled ? "source" : "header", E.m_bExcluded
                    , !E.m_bListed ? "unlisted" : E.m_bMissing ? "missing" : "ok", Sc.m_pStatus, Sc.m_pLock, bOpen, E.m_Bytes);
            }
            return Out;
        }
        std::string ListOpen() noexcept override
        {
            std::string Out = std::format("ListOpenFiles: ok\nOpen={}  Front={}\n\nPath\tFront\tProblems\tFontSize\tX\tY\n", m_Views.size(), m_Front.empty() ? "none" : m_Front);
            for (const auto& V : m_Views) Out += std::format("{}\t{}\t{}\t{:.0f}\t{:.0f}\t{:.0f}\n", V->m_Path, SamePath(V->m_Path, m_Front), V->m_Problems, V->m_FontSize, V->m_X, V->m_Y);
            return Out;
        }

        std::string ExportCMake(const std::string& FileText) noexcept override
        {
            if (!m_Document.isLoaded()) return "ExportCMake: the module could not be loaded";
            const std::filesystem::path File = FileText.empty() ? m_Folder / L"module.cmake" : std::filesystem::path(xstrtool::To(FileText));
            const std::string Name = xeditor::ResolveResourceDisplayName(m_Document.m_LibraryGuid, m_Document.m_Guid, "Module");
            bool bChanged = false;
            if (const auto Err = WriteExportCMake(Descriptor(), m_Folder, Name, File, &bChanged); !Err.empty()) return "ExportCMake: " + Err;
            return std::format("ExportCMake: {} {}  (include it and call {}_apply(<target>))", bChanged ? "wrote" : "unchanged:", File.string(), Lower(CMakeName(Name)));
        }
        std::string ListTree() noexcept override
        {
            const char* pDialog = m_Dialog == dialog::NewFile ? "NewFile" : m_Dialog == dialog::NewFolder ? "NewFolder" : m_Dialog == dialog::Delete ? "Delete" : m_Dialog == dialog::Revert ? "Revert" : "none";
            std::string Out = std::format("ListTree: ok\nSelected={}  Dialog={}  Renaming={}  Filter={}  ChangesOnly={}\n\nPath\tKind\tDepth\tExpanded\tSelected\tX\tY\n"
                , m_Selected.empty() && m_bSelectedFolder ? "(module)" : m_Selected, pDialog, m_RenamePath.empty() ? "none" : m_RenamePath, m_Filter.empty() ? "none" : m_Filter, m_bChangesOnly);
            for (const auto& R : m_Drawn) Out += std::format("{}\t{}\t{}\t{}\t{}\t{:.0f}\t{:.0f}\n", R.m_Path.empty() ? "(module)" : R.m_Path, R.m_bFolder ? "folder" : "file", R.m_Depth, R.m_bExpanded, R.m_bSelected, R.m_X, R.m_Y);
            return Out;
        }
        std::string SelectNode(const std::string& PathIn) noexcept override
        {
            if (m_bRebuild) Rebuild();
            const std::string Path = PathIn.empty() ? std::string() : NormalizeRelative(PathIn);
            if (!PathIn.empty() && Path.empty()) return std::format("'{}' is not a path inside source_db", PathIn);
            bool bFolder = Path.empty();
            if (!Path.empty())
            {
                if (FindNode(m_Tree, Path, false)) bFolder = false;
                else if (FindNode(m_Tree, Path, true)) bFolder = true;
                else return std::format("'{}' is not in the tree", Path);
            }
            m_Selected = Path; m_bSelectedFolder = bFolder;
            return {};
        }

        // =============================================================================================================
        // The windows
        // =============================================================================================================
        void RenderExtraWindows(const ImGuiWindowClass& WindowClass) noexcept override
        {
            const double Now = ImGui::GetTime();
            const auto* pHost = xeditor::host::current();
            static const xlog::hub NoLogs;
            const xlog::hub& Hub = pHost ? pHost->m_Logs : NoLogs;
            for (auto& V : m_Views)
            {
                ImGui::SetNextWindowClass(&WindowClass);
                if (m_CenterDock) ImGui::SetNextWindowDockID(m_CenterDock, ImGuiCond_Appearing);
                ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
                if (V->m_bFocus) { ImGui::SetNextWindowFocus(); V->m_bFocus = false; m_Front = V->m_Path; }
                if (BeginEditorWindow(V->m_Title.c_str(), &V->m_bOpen))
                {
                    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) m_Front = V->m_Path;
                    V->RenderBody(Hub, Now);
                }
                ImGui::End();
            }
            std::erase_if(m_Views, [](const auto& V) { return !V->m_bOpen; });
        }

        void Run(const std::string& Command) noexcept { xeditor::Run(m_Undo, Command); }
        static void RunWorkspace(const std::string& Command) noexcept { if (auto* pHost = xeditor::host::current()) xeditor::RunQuery(pHost->workspace(), Command); }
        std::string SourceControlTarget(const std::string& Path) const noexcept
        {
            return std::format("-Library {} -Path {}", xresource_editor::commands::FormatLibraryGuid(m_Document.m_LibraryGuid), xresource_editor::commands::EncodeAssetPath(LibraryRelative(Absolute(m_Folder, Path))));
        }

        static std::string Join(const std::string& Folder, const std::string& Name) { return Folder.empty() ? Name : Folder + "/" + Name; }

        // ---- the tree ---------------------------------------------------------------------------------------------
        bool Matches(const tree_node& Node) const noexcept
        {
            auto Own = [&](const tree_node& N)
            {
                if (!m_Filter.empty() && !details_contains(N.m_Path, m_Filter)) return false;
                if (m_bChangesOnly && !N.m_bFolder) { const auto Sc = SourceControlOf(N.m_Path, false); if (std::string_view(Sc.m_pStatus) == "clean" && N.m_bListed && !N.m_bMissing) return false; }
                return true;
            };
            if (!Node.m_bFolder) return Own(Node);
            for (const auto& C : Node.m_Children) if (Matches(C)) return true;
            return m_Filter.empty() && !m_bChangesOnly;                                // an empty folder shows only when nothing filters
        }
        static bool details_contains(std::string_view Hay, std::string_view Needle) noexcept
        {
            return Lower(Hay).find(Lower(Needle)) != std::string::npos;
        }

        void DrawBadge(const sc_info& Info) const noexcept
        {
            if (Info.m_Badge == xresource_editor::asset_status_badge::None && Info.m_LockBadge == xresource_editor::asset_lock_badge::None) return;
            const ImVec2 Min = ImGui::GetItemRectMin(), Max = ImGui::GetItemRectMax();
            const float X = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x + 7.0f;
            xresource_editor::DrawSourceControlBadge(ImGui::GetWindowDrawList(), ImVec2(X, (Min.y + Max.y) * 0.5f), 12.0f, Info.m_Badge, Info.m_LockBadge);
        }

        void BeginRename(const std::string& Path) noexcept
        {
            m_RenamePath = Path;
            const std::string Name = NameOf(Path);
            std::snprintf(m_RenameText.data(), m_RenameText.size(), "%s", Name.c_str());
            m_bRenameFocus = true;
        }
        void CommitRename(const std::string& Path, bool bFolder) noexcept
        {
            const std::string Text = m_RenameText.data();
            m_RenamePath.clear();
            if (Text.empty() || Text == NameOf(Path)) return;
            const std::string To = Text.find('/') != std::string::npos || Text.find('\\') != std::string::npos ? Text : Join(FolderOf(Path), Text);
            Run(std::format("{} -Path {} -To {}", bFolder ? "RenameFolder" : "RenameFile", xeditor::Quote(Path), xeditor::Quote(To)));
            m_Selected = NormalizeRelative(To);
        }
        void AskDialog(dialog Kind, const std::string& Path, bool bFolder) noexcept
        {
            m_Dialog = Kind; m_bDialogOpen = true; m_DialogPath = Path; m_bDialogFolder = bFolder; m_DialogTemplate = 0;
            const char* pDefault = Kind == dialog::NewFile ? "NewFile.h" : Kind == dialog::NewFolder ? "NewFolder" : "";
            std::snprintf(m_DialogText.data(), m_DialogText.size(), "%s", pDefault);
        }

        void MenuFor(const tree_node& Node) noexcept
        {
            const bool bFolder = Node.m_bFolder;
            const std::string& Path = Node.m_Path;
            const std::string Where = bFolder ? Path : FolderOf(Path);
            if (ImGui::BeginMenu("Add"))
            {
                if (ImGui::MenuItem("New File...")) AskDialog(dialog::NewFile, Where, true);
                if (ImGui::MenuItem("New Folder...")) AskDialog(dialog::NewFolder, Where, true);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (!bFolder)
            {
                if (ImGui::MenuItem("Open", "Enter")) OpenFile(Path, 0);
                if (ImGui::MenuItem("Open in Visual Studio")) xeditor::OpenInShell({ xlog::ref::type::File, Absolute(m_Folder, Path).string(), 0, 0, 0, 0 });
                if (!Node.m_bListed) { if (ImGui::MenuItem("Include in Module")) Run(std::format("AddFile -Path {}", xeditor::Quote(Path))); }
                else if (ImGui::MenuItem(Node.m_bExcluded ? "Build This File" : "Exclude from Build")) Run(std::format("ExcludeFile -Path {} -Value {}", xeditor::Quote(Path), Node.m_bExcluded ? "false" : "true"));
                ImGui::Separator();
            }
            if (!Path.empty())
            {
                if (ImGui::MenuItem("Rename", "F2")) BeginRename(Path);
                if (ImGui::MenuItem("Delete", "Del")) AskDialog(dialog::Delete, Path, bFolder);
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Show in Explorer")) ShellExecuteW(nullptr, L"open", (bFolder ? (Path.empty() ? SourceDb(m_Folder) : Absolute(m_Folder, Path)) : Absolute(m_Folder, Path).parent_path()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText((Path.empty() ? SourceDb(m_Folder) : Absolute(m_Folder, Path)).string().c_str());
            if (!bFolder)
            {
                const auto Sc = SourceControlOf(Path, false);
                ImGui::Separator();
                if (ImGui::BeginMenu("Source Control"))
                {
                    const bool bChanged = std::string_view(Sc.m_pStatus) != "clean" && std::string_view(Sc.m_pStatus) != "unknown";
                    if (ImGui::MenuItem("Stage", nullptr, false, bChanged)) RunWorkspace("SourceControlStage " + SourceControlTarget(Path));
                    if (ImGui::MenuItem("Revert...", nullptr, false, bChanged && std::string_view(Sc.m_pStatus) != "untracked")) AskDialog(dialog::Revert, Path, false);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Lock", nullptr, false, std::string_view(Sc.m_pLock) == "none")) RunWorkspace("SourceControlLock " + SourceControlTarget(Path));
                    if (ImGui::MenuItem("Unlock", nullptr, false, std::string_view(Sc.m_pLock) == "mine")) RunWorkspace("SourceControlUnlock " + SourceControlTarget(Path));
                    ImGui::EndMenu();
                }
            }
            else if (ImGui::MenuItem("Refresh Source Control Status"))
                if (const auto Root = LibraryRoot(); !Root.empty()) xresource_editor::source_control::RequestPriorityScan(Root, LibraryRelative(Path.empty() ? SourceDb(m_Folder) : Absolute(m_Folder, Path)));
        }

        void MoveTo(const std::string& From, const std::string& ToFolder) noexcept
        {
            const std::string To = Join(ToFolder, NameOf(From));
            if (SamePath(From, To)) return;
            const bool bFolder = std::any_of(m_Folders.begin(), m_Folders.end(), [&](const std::string& F) { return SamePath(F, From); });
            Run(std::format("{} -Path {} -To {}", bFolder ? "RenameFolder" : "RenameFile", xeditor::Quote(From), xeditor::Quote(To)));
        }

        void RenderNode(const tree_node& Node, bool bRoot) noexcept
        {
            if (!Matches(Node)) return;
            ImGui::PushID(bRoot ? "##module" : Node.m_Path.c_str());
            const bool bSelected = m_Selected == Node.m_Path && m_bSelectedFolder == Node.m_bFolder;
            ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | (bSelected ? ImGuiTreeNodeFlags_Selected : 0);
            if (!Node.m_bFolder) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            else Flags |= ImGuiTreeNodeFlags_DefaultOpen;
            if (!m_Filter.empty() || m_bChangesOnly) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

            std::string Label = Node.m_Name;
            if (!Node.m_bFolder && !Node.m_bListed)       Label += "   (not in the module)";
            else if (!Node.m_bFolder && Node.m_bMissing)  Label += "   (missing)";
            else if (!Node.m_bFolder && Node.m_bExcluded) Label += "   (not built)";
            const bool bColor = !Node.m_bFolder && (!Node.m_bListed || Node.m_bMissing || Node.m_bExcluded);
            if (bColor) ImGui::PushStyleColor(ImGuiCol_Text, Node.m_bMissing ? ImVec4(0.90f, 0.45f, 0.42f, 1.0f) : !Node.m_bListed ? ImVec4(0.85f, 0.75f, 0.45f, 1.0f) : ImVec4(0.55f, 0.55f, 0.58f, 1.0f));

            bool bOpen = false;
            const bool bRenaming = !bRoot && m_RenamePath == Node.m_Path;
            if (bRenaming)
            {
                ImGui::TreeNodeEx("##renaming", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (m_bRenameFocus) { ImGui::SetKeyboardFocusHere(); m_bRenameFocus = false; }
                if (ImGui::InputText("##rename", m_RenameText.data(), m_RenameText.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) CommitRename(Node.m_Path, Node.m_bFolder);
                else if (ImGui::IsItemDeactivated()) m_RenamePath.clear();             // Esc, or a click somewhere else
            }
            else bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);
            if (bColor) ImGui::PopStyleColor();

            if (!bRenaming)
            {
                m_Drawn.push_back({ Node.m_Path, Node.m_bFolder, Node.m_bFolder && bOpen, bSelected, m_Depth, ImGui::GetItemRectMin().x + 24.0f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f });
                DrawBadge(SourceControlOf(Node.m_Path, Node.m_bFolder));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) { m_Selected = Node.m_Path; m_bSelectedFolder = Node.m_bFolder; }
                if (!Node.m_bFolder && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenFile(Node.m_Path, 0);          // a double click opens the file
                if (ImGui::IsItemHovered())
                {
                    const auto Sc = SourceControlOf(Node.m_Path, Node.m_bFolder);
                    ImGui::SetTooltip("%s%s\nSource control: %s%s", Node.m_Path.empty() ? "(the module's source_db)" : Node.m_Path.c_str(), !Node.m_bListed ? "\nNot part of the module: not built until it is included." : Node.m_bMissing ? "\nListed, but not on disk." : Node.m_bExcluded ? "\nKept in the module, left out of the build." : ""
                        , Sc.m_pStatus, std::string_view(Sc.m_pLock) == "none" ? "" : std::string_view(Sc.m_pLock) == "mine" ? "  (locked by you)" : "  (locked by someone else)");
                }
                if (ImGui::BeginPopupContextItem("##menu")) { m_Selected = Node.m_Path; m_bSelectedFolder = Node.m_bFolder; MenuFor(Node); ImGui::EndPopup(); }
                if (!bRoot && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("XSCRIPT_NODE", Node.m_Path.c_str(), Node.m_Path.size() + 1);
                    ImGui::TextUnformatted(Node.m_Path.c_str());
                    ImGui::EndDragDropSource();
                }
                if (Node.m_bFolder && ImGui::BeginDragDropTarget())
                {
                    if (const auto* pPayload = ImGui::AcceptDragDropPayload("XSCRIPT_NODE")) MoveTo(static_cast<const char*>(pPayload->Data), Node.m_Path);
                    ImGui::EndDragDropTarget();
                }
            }
            if (Node.m_bFolder && bOpen)
            {
                ++m_Depth;
                for (const auto& Child : Node.m_Children) RenderNode(Child, false);
                --m_Depth;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        const tree_node* FindNode(const tree_node& From, const std::string& Path, bool bFolder) const noexcept
        {
            if (From.m_Path == Path && From.m_bFolder == bFolder) return &From;
            for (const auto& C : From.m_Children) if (const auto* pFound = FindNode(C, Path, bFolder)) return pFound;
            return nullptr;
        }

        void RenderSources() noexcept
        {
            if (!m_Document.isLoaded()) return;
            if (m_bRebuild) Rebuild();
            m_Drawn.clear(); m_Depth = 0;
            // the keys of the tree, while it has the focus
            const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && m_RenamePath.empty() && m_Dialog == dialog::None;
            if (bFocused && !(m_Selected.empty() && m_bSelectedFolder))
            {
                if (ImGui::IsKeyPressed(ImGuiKey_F2)) BeginRename(m_Selected);
                if (ImGui::IsKeyPressed(ImGuiKey_Delete)) AskDialog(dialog::Delete, m_Selected, m_bSelectedFolder);
                if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !m_bSelectedFolder) OpenFile(m_Selected, 0);
            }
            if (ImGui::SmallButton("+ New File")) AskDialog(dialog::NewFile, m_bSelectedFolder ? m_Selected : FolderOf(m_Selected), true);
            ImGui::SameLine();
            ImGui::Checkbox("Changes only", &m_bChangesOnly);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only the files that source control says changed, and the ones that are not in step with the module.");
            xeditor::RenderTreeSearchBar(m_Filter, ImGui::GetContentRegionAvail().x);
            ImGui::Separator();
            if (ImGui::BeginChild("##sourcetree", ImVec2(0, 0), ImGuiChildFlags_None))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);               // folders nest by half the usual step
                ImGui::Indent(14.0f);                                                 // the gutter of the source control badges
                RenderNode(m_Tree, /*bRoot*/ true);
                ImGui::Unindent(14.0f);
                ImGui::PopStyleVar();
                if (ImGui::BeginPopupContextWindow("##treemenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) { MenuFor(m_Tree); ImGui::EndPopup(); }
            }
            ImGui::EndChild();
            RenderDialogs();
        }

        void RenderDialogs() noexcept
        {
            if (m_bDialogOpen) { ImGui::OpenPopup("##xscript_dialog"); m_bDialogOpen = false; }
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (!ImGui::BeginPopupModal("##xscript_dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) { if (m_Dialog != dialog::None && !ImGui::IsPopupOpen("##xscript_dialog")) m_Dialog = dialog::None; return; }
            bool bClose = false;
            switch (m_Dialog)
            {
            case dialog::NewFile:
            case dialog::NewFolder:
            {
                const bool bFile = m_Dialog == dialog::NewFile;
                ImGui::TextUnformatted(bFile ? "New file" : "New folder");
                ImGui::TextDisabled("in %s", m_DialogPath.empty() ? "the module's source_db" : m_DialogPath.c_str());
                ImGui::SetNextItemWidth(320.0f);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                const bool bEnter = ImGui::InputText("##name", m_DialogText.data(), m_DialogText.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (bFile)
                {
                    static const char* Templates[] = { "By the extension", "Header (#pragma once)", "Source (includes its header)", "Empty" };
                    ImGui::SetNextItemWidth(320.0f);
                    ImGui::Combo("##template", &m_DialogTemplate, Templates, 4);
                }
                std::string Name = m_DialogText.data();
                if (bFile && Name.find('.') == std::string::npos && !Name.empty()) Name += m_DialogTemplate == 2 ? ".cpp" : ".h";      // no extension: the type chosen decides
                const std::string Path = Join(m_DialogPath, Name);
                std::string Problem;
                if (Name.empty()) Problem = "Give it a name.";
                else if (bFile) { std::string Normalized; Problem = ops::CheckFilePath(Path, Normalized); }
                else if (NormalizeRelative(Path).empty()) Problem = "That cannot be a folder name.";
                if (Problem.empty()) { std::error_code Ec; if (std::filesystem::exists(Absolute(m_Folder, Path), Ec)) Problem = "It already exists."; }
                if (!Problem.empty()) ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.42f, 1.0f), "%s", Problem.c_str());
                ImGui::BeginDisabled(!Problem.empty());
                if ((ImGui::Button("Create", ImVec2(100, 0)) || bEnter) && Problem.empty())
                {
                    static const char* Names[] = { "", "header", "source", "empty" };
                    if (bFile) Run(std::format("AddFile -Path {}{}", xeditor::Quote(Path), m_DialogTemplate ? std::format(" -Template {}", Names[m_DialogTemplate]) : std::string()));
                    else       Run(std::format("NewFolder -Path {}", xeditor::Quote(Path)));
                    if (bFile) { m_Selected = NormalizeRelative(Path); m_bSelectedFolder = false; OpenFile(Path, 0); }
                    bClose = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0))) bClose = true;
                break;
            }
            case dialog::Delete:
            {
                std::size_t nFiles = 0;
                if (m_bDialogFolder) for (const auto& E : m_Entries) nFiles += E.m_Path.size() > m_DialogPath.size() && E.m_Path[m_DialogPath.size()] == '/' && SamePath(std::string_view(E.m_Path).substr(0, m_DialogPath.size()), m_DialogPath);
                ImGui::Text("Delete %s '%s'?", m_bDialogFolder ? "the folder" : "the file", m_DialogPath.c_str());
                ImGui::TextDisabled("%s", m_bDialogFolder ? std::format("It and its {} file{} leave the module and the disk.", nFiles, nFiles == 1 ? "" : "s").c_str() : "It leaves the module and the disk.");
                ImGui::TextDisabled("Undo brings it back exactly as it was.");
                if (ImGui::Button("Delete", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
                {
                    Run(std::format("{} -Path {}", m_bDialogFolder ? "RemoveFolder" : "RemoveFile", xeditor::Quote(m_DialogPath)));
                    m_Selected.clear(); m_bSelectedFolder = true;
                    bClose = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) bClose = true;
                break;
            }
            case dialog::Revert:
            {
                ImGui::Text("Discard ALL local changes to '%s'?", m_DialogPath.c_str());
                ImGui::TextDisabled("The file goes back to what source control has. This cannot be undone.");
                if (ImGui::Button("Revert", ImVec2(100, 0))) { RunWorkspace("SourceControlRevert " + SourceControlTarget(m_DialogPath)); bClose = true; }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) bClose = true;
                break;
            }
            case dialog::None: bClose = true; break;
            }
            if (bClose) { m_Dialog = dialog::None; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        // ---- the libraries and defines: the descriptor's own properties, with a way to add the first one (an empty list has no row of its own in the inspector)
        void RenderLibraries() noexcept
        {
            if (!m_Document.isLoaded()) return;
            const auto& D = static_cast<const descriptor&>(Descriptor());
            if (ImGui::SmallButton("+ Add library")) Run(std::format("ListOp -Path {} -Op Insert -Index {}", xeditor::Quote("ScriptModule/Libraries"), D.m_Libraries.size()));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A third-party library the module needs: include folders, library folders, libraries, defines and DLLs to copy. Paths are relative to the project.");
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Add define")) Run(std::format("ListOp -Path {} -Op Insert -Index {}", xeditor::Quote("ScriptModule/Defines"), D.m_Defines.size()));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A preprocessor define for the whole module: NAME or NAME=value.");
            if (D.m_Libraries.empty() && D.m_Defines.empty()) ImGui::TextDisabled("No third-party libraries or defines.");
            m_DescriptorInspector.Show();
        }

        // ---- the middle: what the module is made of and what is wrong with it ------------------------------------
        void RenderOverview() noexcept
        {
            m_CenterDock = ImGui::GetWindowDockID();                                 // the viewers of the files open next to this one
            if (!m_Document.isLoaded()) return;
            if (m_bRebuild) Rebuild();
            std::size_t nSources = 0, nHeaders = 0, nExcluded = 0, nMissing = 0, nUnlisted = 0;
            for (const auto& E : m_Entries)
            {
                if (!E.m_bListed) { ++nUnlisted; continue; }
                nSources += E.m_Kind == file_kind::Compiled; nHeaders += E.m_Kind == file_kind::Header; nExcluded += E.m_bExcluded; nMissing += E.m_bMissing;
            }
            const auto& D = static_cast<const descriptor&>(Descriptor());
            ImGui::TextUnformatted(m_Tree.m_Name.c_str());
            ImGui::TextDisabled("A script module: %zu source file%s, %zu header%s, %zu librar%s.", nSources, nSources == 1 ? "" : "s", nHeaders, nHeaders == 1 ? "" : "s", D.m_Libraries.size(), D.m_Libraries.size() == 1 ? "y" : "ies");
            ImGui::Spacing();
            if (!m_UsedBy.empty())
            {
                std::string Games;
                for (const auto& Name : m_UsedBy) Games += (Games.empty() ? "" : ", ") + Name;
                ImGui::Text("Part of: %s", Games.c_str());
                if (ImGui::IsItemHovered()) xeditor::hint::Draw({ .m_Topic = "Games", .m_Body = "The Games of the project whose script modules include this one. Each Game's project is made from the descriptors of its modules." });
            }
            else ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.45f, 1.0f), "Not part of any Game yet: add it to a Game resource (or run AddProjectModuleReference) for it to be built.");
            if (nExcluded) ImGui::TextDisabled("%zu file%s kept out of the build.", nExcluded, nExcluded == 1 ? " is" : "s are");
            ImGui::Separator();
            if (nMissing)
            {
                ImGui::TextColored(ImVec4(0.90f, 0.45f, 0.42f, 1.0f), "%zu listed file%s not on disk:", nMissing, nMissing == 1 ? " is" : "s are");
                for (const auto& E : m_Entries) if (E.m_bMissing) ImGui::BulletText("%s", E.m_Path.c_str());
                if (ImGui::Button("Remove them from the module")) Run("Rescan -RemoveMissing true");
            }
            if (nUnlisted)
            {
                ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.45f, 1.0f), "%zu file%s in the folder but not in the module (not built):", nUnlisted, nUnlisted == 1 ? " is" : "s are");
                for (const auto& E : m_Entries) if (!E.m_bListed) ImGui::BulletText("%s", E.m_Path.c_str());
                if (ImGui::Button("Add them to the module")) Run("Rescan");
            }
            if (!nMissing && !nUnlisted) ImGui::TextDisabled("The module and its folder agree.");
            for (const auto& Error : m_ValidationErrors) ImGui::TextColored(ImVec4(0.90f, 0.45f, 0.42f, 1.0f), "%s", Error.c_str());
            ImGui::Separator();
            RenderBuildState();
            ImGui::Spacing();
            if (ImGui::SmallButton("Export CMake")) xeditor::NotifyToast(ExportCMake({}));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes module.cmake next to Descriptor.txt: a project that builds the game without the editor includes it and calls %s_apply(target).", Lower(CMakeName(m_Tree.m_Name)).c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Double-click a file in the tree to open it here. The code is written in Visual Studio; these are views.");
        }

        // How the last build of the game went, from the Logs.
        void RenderBuildState() noexcept
        {
            auto* pHost = xeditor::host::current();
            if (!pHost) return;
            const xlog::hub& Hub = pHost->m_Logs;
            const xlog::operation* pLast = nullptr;
            for (auto It = Hub.OperationOrder().rbegin(); It != Hub.OperationOrder().rend(); ++It)
                if (const auto* pOp = Hub.FindOperation(*It); pOp && pOp->m_Kind == "game.build") { pLast = pOp; break; }
            if (!pLast) { ImGui::TextDisabled("The game has not been built in this session."); return; }
            ImGui::Text("Last build of the game: %s", xlog::OutcomeName(pLast->m_Outcome));
            if (pLast->m_Errors || pLast->m_Warnings) { ImGui::SameLine(); ImGui::TextDisabled("(%u error%s, %u warning%s)", pLast->m_Errors, pLast->m_Errors == 1 ? "" : "s", pLast->m_Warnings, pLast->m_Warnings == 1 ? "" : "s"); }
            if (ImGui::SmallButton("Show in the Logs")) pHost->show_logs(std::format("op:{}", pLast->m_Id));
        }
    };

    inline const xeditor::auto_register_resource_editor g_Registration
    { type_guid_v
    , [](xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) -> std::unique_ptr<xeditor::resource_editor>
      { return std::make_unique<session>(Guid, LibraryGuid, pDevice); }
    };
}

#endif // XSCRIPT_MODULE_EDITOR_H
