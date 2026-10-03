#ifndef XSCRIPT_MODULE_CPP_VIEW_H
#define XSCRIPT_MODULE_CPP_VIEW_H
#pragma once

// One window that shows one C++ file of a script module: highlighted, with line numbers, read-only. It is a viewer, not an editor - code is written in Visual Studio -
// and each file has an instance of its own (the module editor keeps one per file and never opens a file twice). An instance
//   - watches its file and shows the new text when the file changes on disk (Visual Studio saved it),
//   - marks the lines the compiler complained about (the problems of the Logs whose site is this file), the message being the line's tooltip,
//   - jumps to a line when asked (the Logs' "Open source", F8, the OpenFile command),
//   - offers "Open in Visual Studio" (the file opens with the system's handler for it),
//   - zooms its text with Ctrl + the mouse wheel over the code (or the ZoomFile command): each file's viewer keeps its own size.
#include "source/Tools/Editor/xeditor_text_widget.h"
#include "dependencies/xeditor/include/xeditor/open_ref.h"
#include "dependencies/xlog/source/xlog_hub.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace xscript::module_editor
{
    // The viewer's background: a dark gray (Visual Studio's dark theme uses the same one), for the window it draws in and for the text widget's own palette.
    inline constexpr ImU32 background_v = IM_COL32(30, 30, 30, 255);

    struct cpp_view
    {
        std::string                         m_Path;                 // relative to source_db, normalized: what identifies the window
        std::filesystem::path               m_Absolute;
        std::string                         m_Title;                // the ImGui window's name: the file's name, then an id that never changes (a renamed file keeps its window)
        std::string                         m_Key;                  // that id: the path the window was opened with + the module's id
        TextEditor                          m_Text;
        std::filesystem::file_time_type     m_Stamp{};
        double                              m_NextPoll = 0.0;
        bool                                m_bOpen  = true;        // the tab's close button clears it
        bool                                m_bFocus = false;       // bring it to the front the next frame
        bool                                m_bMissing = false;
        int                                 m_GoToLine = 0;         // 1-based, 0 = none
        std::uint64_t                       m_MarkersRevision = ~0ull;
        std::size_t                         m_Problems = 0;
        float                               m_X = 0.0f, m_Y = 0.0f; // where the code is on the screen (its middle), as of the last frame it was drawn: where a mouse would go
        float                               m_FontSize = 0.0f;      // the text's size in pixels; 0 until the first frame says what the default one is

        static constexpr float              min_font_size_v = 6.0f;
        static constexpr float              max_font_size_v = 64.0f;

        cpp_view(std::string Path, std::filesystem::path Absolute, const std::string& IdSuffix) noexcept
            : m_Path(std::move(Path)), m_Absolute(std::move(Absolute))
        {
            m_Key = m_Path + IdSuffix;
            SetPath(m_Path, m_Absolute);
            m_Text.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
            auto Palette = TextEditor::GetDarkPalette();
            Palette[static_cast<int>(TextEditor::PaletteIndex::Background)] = background_v;
            m_Text.SetPalette(Palette);
            m_Text.SetReadOnly(true);
            m_Text.SetShowWhitespaces(false);
            m_Text.SetImGuiChildIgnored(true);                // the widget draws into the child the window gives it (it would begin a window of its own otherwise)
            Reload();
        }

        // The file was renamed or moved: the title shows the new name, the window stays the same one.
        void SetPath(std::string Path, std::filesystem::path Absolute) noexcept
        {
            m_Path = std::move(Path); m_Absolute = std::move(Absolute);
            const auto Slash = m_Path.find_last_of('/');
            m_Title = (Slash == std::string::npos ? m_Path : m_Path.substr(Slash + 1)) + "###" + m_Key;
        }

        // The file as it is now. The cursor stays where it was when only the text changed.
        void Reload() noexcept
        {
            std::error_code Ec;
            const auto Time = std::filesystem::last_write_time(m_Absolute, Ec);
            m_bMissing = static_cast<bool>(Ec);
            if (m_bMissing) { m_Text.SetText("// this file is not on disk (it was moved, renamed or deleted)\n"); return; }
            std::ifstream In(m_Absolute, std::ios::binary);
            std::stringstream Text; Text << In.rdbuf();
            std::string Content = Text.str();
            std::erase(Content, '\r');
            const auto Cursor = m_Text.GetCursorPosition();
            m_Text.SetText(Content);
            if (m_Stamp != std::filesystem::file_time_type{}) m_Text.SetCursorPosition(Cursor);
            m_Stamp = Time;
        }

        // Called every frame: looks at the file's time twice a second.
        void Poll(double Now) noexcept
        {
            if (Now < m_NextPoll) return;
            m_NextPoll = Now + 0.5;
            std::error_code Ec;
            const auto Time = std::filesystem::last_write_time(m_Absolute, Ec);
            if (static_cast<bool>(Ec) != m_bMissing || (!Ec && Time != m_Stamp)) Reload();
        }

        void GoTo(int Line) noexcept { m_GoToLine = Line; m_bFocus = true; }

        // The text's size: what the wheel does (one pixel for each notch, up is bigger) and what ZoomFile does. 0 is the default size.
        void SetFontSize(float Size) noexcept { m_FontSize = Size <= 0.0f ? ImGui::GetStyle().FontSizeBase : std::clamp(Size, min_font_size_v, max_font_size_v); }
        void ZoomBy(float Notches) noexcept { m_FontSize = std::clamp((m_FontSize > 0.0f ? m_FontSize : ImGui::GetStyle().FontSizeBase) + Notches, min_font_size_v, max_font_size_v); }

        // The compiler's complaints about this file: the Logs' problems from warning up whose site is the file.
        void UpdateMarkers(const xlog::hub& Hub) noexcept
        {
            if (Hub.Revision() == m_MarkersRevision) return;
            m_MarkersRevision = Hub.Revision();
            TextEditor::ErrorMarkers Markers;
            m_Problems = 0;
            const std::string Mine = Canonical(m_Absolute.string());
            for (const auto Id : Hub.ProblemOrder())
            {
                const auto* P = Hub.FindProblem(Id);
                if (!P || P->m_Severity < xlog::severity::Warning || P->m_Site.m_Type != xlog::ref::type::File || P->m_Site.m_Line <= 0) continue;
                if (Canonical(P->m_Site.m_Path) != Mine) continue;
                auto& Text = Markers[P->m_Site.m_Line];
                Text += (Text.empty() ? "" : "\n") + P->m_Title;
                ++m_Problems;
            }
            m_Text.SetErrorMarkers(Markers);
        }

        // The window's content (the caller has begun the window).
        void RenderBody(const xlog::hub& Hub, double Now) noexcept
        {
            Poll(Now);
            UpdateMarkers(Hub);
            if (ImGui::SmallButton("Open in Visual Studio")) xeditor::OpenInShell({ xlog::ref::type::File, m_Absolute.string(), 0, 0, 0, 0 });
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the file with the program Windows uses for it (Visual Studio): the code is written there, this is a view. Its path and line are on the clipboard.");
            ImGui::SameLine();
            ImGui::TextDisabled("%s  -  %d lines%s", m_Path.c_str(), m_Text.GetTotalLines(), m_Problems ? std::format("  -  {} problem{}", m_Problems, m_Problems == 1 ? "" : "s").c_str() : "");
            if (m_GoToLine > 0)
            {
                m_Text.SetCursorPosition(TextEditor::Coordinates(std::max(0, std::min(m_GoToLine, m_Text.GetTotalLines()) - 1), 0));
                m_GoToLine = 0;
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(background_v));
            if (ImGui::BeginChild(("##codechild" + m_Key).c_str(), ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove))
            {
                if (m_FontSize <= 0.0f) m_FontSize = ImGui::GetStyle().FontSizeBase;
                m_X = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f;
                m_Y = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y * 0.5f;
                const auto& IO = ImGui::GetIO();
                if (IO.KeyCtrl && IO.MouseWheel != 0.0f && ImGui::IsWindowHovered()) ZoomBy(IO.MouseWheel);      // ImGui does not scroll while Ctrl is down
                ImGui::PushFont(nullptr, m_FontSize);
                m_Text.Render("##code", ImVec2(0, 0), false, [] {});
                ImGui::PopFont();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        static std::string Canonical(std::string Path) noexcept
        {
            for (auto& c : Path) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return Path;
        }
    };
}

#endif // XSCRIPT_MODULE_CPP_VIEW_H
