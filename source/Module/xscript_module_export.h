#ifndef XSCRIPT_MODULE_EXPORT_H
#define XSCRIPT_MODULE_EXPORT_H
#pragma once

// A module as a CMake file of its own, for a project that builds the game without the editor. The editor generates its own game project from the descriptors of the modules
// the project's Scripting settings include (Cache/Script/CMakeLists.txt, never checked in); this is the same information for a project that wants one module:
//
//      include(<project>/Descriptors/ScriptModule/xx/yy/<guid>.desc/module.cmake)
//      soccergame_apply(Game)                      # sources, include folders, libraries, defines of the module
//
// It is made from Descriptor.txt (the files that are listed and not excluded, the libraries, the defines) and checked in with the module. Paths are written relative to the file
// itself and to the project root, which the file finds by the place it lives in, so it works wherever the project is.
#include "plugins/xscript_module.plugin/source/Module/xscript_module_files.h"

namespace xscript::module
{
    // "Soccer Game" -> "SOCCERGAME": the prefix of the variables and the function of the file.
    inline std::string CMakeName(std::string_view Name) noexcept
    {
        std::string Out;
        for (const unsigned char c : Name) if (std::isalnum(c)) Out += static_cast<char>(std::toupper(c));
        if (Out.empty() || std::isdigit(static_cast<unsigned char>(Out.front()))) Out.insert(Out.begin(), 'M');
        return Out;
    }

    // The folder of the module in Visual Studio's Solution Explorer: the module's name, without what a quoted CMake string cannot hold.
    inline std::string GroupName(std::string_view Name) noexcept
    {
        std::string Out;
        for (const unsigned char c : Name) Out += (std::isalnum(c) || c == ' ' || c == '_' || c == '-' || c == '.' || c >= 0x80) ? static_cast<char>(c) : '_';
        return Out.empty() ? std::string("Module") : Out;
    }

    // The text of the file, for the file that will hold it: every path in it is written relative to that file's own folder, so it works wherever the project is. DescFolder is the
    // module's .desc folder, which is where the file goes by default.
    inline std::string ExportCMake(const descriptor& D, const std::filesystem::path& DescFolder, std::string_view ModuleName, const std::filesystem::path& File) noexcept
    {
        const auto Module = ResolveDescriptor(D, DescFolder);
        const std::string Prefix = CMakeName(ModuleName), Fn = Lower(Prefix);
        auto Quoted = [](const std::string& Text) { return "\"" + Text + "\""; };
        std::error_code Ec;
        const auto FileDir = std::filesystem::absolute(File, Ec).parent_path();
        const auto ProjectRoot = std::filesystem::weakly_canonical(DescFolder / ".." / ".." / ".." / ".." / "..", Ec);       // Descriptors/ScriptModule/xx/yy/<guid>.desc
        auto ToDir = [&](const std::filesystem::path& Target)           // "${CMAKE_CURRENT_LIST_DIR}/<way from the file's folder>", or the full path when there is no way (another drive)
        {
            const auto Way = std::filesystem::relative(std::filesystem::weakly_canonical(Target, Ec), std::filesystem::weakly_canonical(FileDir, Ec), Ec);
            std::string Text = (Ec || Way.empty()) ? Target.generic_string() : "${CMAKE_CURRENT_LIST_DIR}/" + Way.generic_string();
            return Text;
        };
        const std::string SourceBase = ToDir(SourceDb(DescFolder)), RootBase = ToDir(ProjectRoot);
        auto Here = [&](std::string_view Relative) { return "${" + Prefix + "_SOURCE_ROOT}/" + std::string(Relative); };
        auto Project = [&](const std::string& Relative) { return "${" + Prefix + "_PROJECT_ROOT}/" + NormalizeRelative(Relative); };
        auto Relative = [&](const std::filesystem::path& Absolute) { return std::filesystem::relative(Absolute, SourceDb(DescFolder)).generic_string(); };

        std::string C;
        C += "# Generated from Descriptor.txt (by the ScriptModule compiler, and by the module editor's Export CMake): do not edit, it is written again when the descriptor changes.\n";
        C += std::format("# Module: {}\n#   include(this file)  then  {}_apply(<target>)\n\n", ModuleName, Fn);
        // The project root is where the Descriptors and Project.config folders are.
        C += std::format("get_filename_component({0}_PROJECT_ROOT \"{1}\" ABSOLUTE)\n\n", Prefix, RootBase);
        C += std::format("list(APPEND XSCRIPT_MODULE_PREFIXES {})                 # the project that includes the files of several modules goes through this list\n", Prefix);
        C += std::format("get_filename_component({}_SOURCE_ROOT \"{}\" ABSOLUTE)       # clean: source_group(TREE) compares the paths as text\n", Prefix, SourceBase);
        C += std::format("set({}_SOURCES\n", Prefix);
        for (const auto& F : Module.m_Compiled) C += "  " + Quoted(Here(Relative(F))) + "\n";
        C += ")\n";
        C += std::format("set({}_HEADERS\n", Prefix);
        for (const auto& F : Module.m_Headers) C += "  " + Quoted(Here(Relative(F))) + "\n";
        C += ")\n";
        C += std::format("set({}_PCH_HEADERS\n", Prefix);
        for (const auto& F : Module.m_PchHeaders) C += "  " + Quoted(Here(Relative(F))) + "\n";
        C += ")\n";

        std::vector<std::string> Includes, LibDirs, Libs, Defines = Module.m_Descriptor.m_Defines, Runtime;
        for (const auto& L : Module.m_Descriptor.m_Libraries)
        {
            for (const auto& P : L.m_IncludeDirs)  Includes.push_back(Project(P));
            for (const auto& P : L.m_LibDirs)      LibDirs.push_back(Project(P));
            for (const auto& P : L.m_Libs)         Libs.push_back(P.find_first_of("/\\") == std::string::npos ? P : Project(P));
            for (const auto& D : L.m_Defines)      Defines.push_back(D);
            for (const auto& P : L.m_RuntimeFiles) Runtime.push_back(Project(P));
        }
        auto List = [&](const char* pName, const std::vector<std::string>& Items)
        {
            C += std::format("set({}_{}", Prefix, pName);
            for (const auto& Item : Items) C += "\n  " + Quoted(Item);
            C += Items.empty() ? ")\n" : "\n)\n";
        };
        List("INCLUDE_DIRS", Includes); List("LIB_DIRS", LibDirs); List("LIBS", Libs); List("DEFINES", Defines); List("RUNTIME_FILES", Runtime);

        C += std::format("\n# Adds the module to a target: its sources and headers (a folder of the module's name, with its own subfolders, in Visual Studio), its include folders, library folders, libraries and defines.\n"
                         "# The DLLs in {0}_RUNTIME_FILES have to be next to the program that loads the module: copy them as your project does.\n"
                         "function({1}_apply target)\n"
                         "  target_sources(${{target}} PRIVATE ${{{0}_SOURCES}} ${{{0}_HEADERS}})\n"
                         "  source_group(TREE \"${{{0}_SOURCE_ROOT}}\" PREFIX \"{2}\" FILES ${{{0}_SOURCES}} ${{{0}_HEADERS}})\n"
                         "  if({0}_INCLUDE_DIRS)\n    target_include_directories(${{target}} PRIVATE ${{{0}_INCLUDE_DIRS}})\n  endif()\n"
                         "  if({0}_LIB_DIRS)\n    target_link_directories(${{target}} PRIVATE ${{{0}_LIB_DIRS}})\n  endif()\n"
                         "  if({0}_LIBS)\n    target_link_libraries(${{target}} PRIVATE ${{{0}_LIBS}})\n  endif()\n"
                         "  if({0}_DEFINES)\n    target_compile_definitions(${{target}} PRIVATE ${{{0}_DEFINES}})\n  endif()\n"
                         "endfunction()\n", Prefix, Fn, GroupName(ModuleName));
        return C;
    }

    // Writes it (only when the text changed: the file is checked in, a rewrite of the same text must not look like an edit). Returns "" or why not.
    inline std::string WriteExportCMake(const descriptor& D, const std::filesystem::path& DescFolder, std::string_view ModuleName, const std::filesystem::path& File, bool* pChanged = nullptr) noexcept
    {
        const std::string Text = ExportCMake(D, DescFolder, ModuleName, File);
        std::string Old;
        const bool bSame = ReadAll(File, Old) && Old == Text;
        if (pChanged) *pChanged = !bSame;
        if (bSame) return {};
        return WriteAll(File, Text) ? std::string() : std::format("'{}' could not be written", File.string());
    }
}

#endif // XSCRIPT_MODULE_EXPORT_H
