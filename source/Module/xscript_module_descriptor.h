#ifndef XSCRIPT_MODULE_DESCRIPTOR_H
#define XSCRIPT_MODULE_DESCRIPTOR_H
#pragma once

// The descriptor of a script module (a ScriptModule resource): the single source of truth for what the module is made of. The Scripting system
// (the project's Script.config.txt lists the modules that are part of the game) turns these descriptors into the game's Visual Studio project; nothing else
// decides what is built. A module's folder is
//
//      <resource>.desc/info.txt         what every resource has
//      <resource>.desc/Descriptor.txt   this descriptor
//      <resource>.desc/source_db/       the files (subfolders are fine); only what the descriptor lists is built
//
// A module that has no Descriptor.txt yet (it predates the descriptor) gets one written from its source_db folder the first time anything needs it
// (see xscript_module_files.h, LoadOrMigrate): nothing about an existing module breaks.
//
// No UI and no editor in here: the generator of the game project, the module editor and the pipe commands all include this one header.
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "plugins/xscript_module.plugin/source/Module/xscript_module_paths.h"

#include <format>
#include <string>
#include <vector>

namespace xscript::module
{
    inline constexpr auto type_guid_v = xresource::type_guid(0x8D3968CB1287FA04ull);      // "ScriptModule": must match the TypeGUID of the plugin's resource_pipeline.config.txt
    using module_ref = xresource::def_guid<type_guid_v>;                                   // a reference to a script module (what the Game resource lists)

    // One file of the module. Path is relative to source_db, with forward slashes ("Systems/ball_system.h").
    struct file
    {
        std::string m_Path;
        bool        m_bExclude = false;         // kept in the module but not built (a file that is being rewritten, an experiment)

        XPROPERTY_DEF
        ( "ScriptFile", file
        , obj_member<"Path",    &file::m_Path>
        , obj_member<"Exclude", &file::m_bExclude>
        )
    };
    XPROPERTY_REG(file)

    // A third-party library the module needs. Every path is relative to the project (the folder that holds Descriptors and Project.config): a project that is
    // moved to another machine builds the same. RuntimeFiles (DLLs) are copied next to the game DLL that the editor loads.
    struct library
    {
        std::string                 m_Name;
        std::vector<std::string>    m_IncludeDirs;
        std::vector<std::string>    m_LibDirs;
        std::vector<std::string>    m_Libs;             // "foo.lib" in a LibDir, or a full relative path
        std::vector<std::string>    m_Defines;          // "NAME" or "NAME=value"
        std::vector<std::string>    m_RuntimeFiles;

        XPROPERTY_DEF
        ( "ScriptLibrary", library
        , obj_member<"Name",         &library::m_Name>
        , obj_member<"IncludeDirs",  &library::m_IncludeDirs>
        , obj_member<"LibDirs",      &library::m_LibDirs>
        , obj_member<"Libs",         &library::m_Libs>
        , obj_member<"Defines",      &library::m_Defines>
        , obj_member<"RuntimeFiles", &library::m_RuntimeFiles>
        )
    };
    XPROPERTY_REG(library)

    struct descriptor : xresource_pipeline::descriptor::base
    {
        void SetupFromSource(std::string_view) override {}
        void Validate(std::vector<std::string>& Errors) const noexcept override;

        std::vector<file>           m_Files;
        std::vector<library>        m_Libraries;
        std::vector<std::string>    m_Defines;          // for the whole module

        XPROPERTY_VDEF
        ( "ScriptModule", descriptor
        , obj_member<"Files",     &descriptor::m_Files, member_flags<flags::DONT_SHOW>>      // the tree of the module editor manages the files
        , obj_member<"Libraries", &descriptor::m_Libraries>
        , obj_member<"Defines",   &descriptor::m_Defines>
        )
    };
    XPROPERTY_VREG(descriptor)

    // What can be wrong with the descriptor on its own (whether the files are there is the folder's business: see xscript_module_files.h).
    inline void descriptor::Validate(std::vector<std::string>& Errors) const noexcept
    {
        std::vector<std::string> Seen;
        for (const auto& F : m_Files)
        {
            const std::string Path = NormalizeRelative(F.m_Path);
            if (Path.empty())                       { Errors.push_back(std::format("File '{}': the path must be relative to source_db, with no '..', no drive and no empty parts", F.m_Path)); continue; }
            if (KindOf(Path) == file_kind::Other)   Errors.push_back(std::format("File '{}': not a C++ file (.cpp .cc .cxx .c .h .hpp .hxx .inl .ipp)", F.m_Path));
            for (const auto& Other : Seen) if (SamePath(Other, Path)) { Errors.push_back(std::format("File '{}' is listed twice", Path)); break; }
            Seen.push_back(Path);
        }
        auto CheckDefines = [&](const std::vector<std::string>& Defines, const std::string& Owner)
        {
            for (const auto& D : Defines)
                if (D.empty() || D.find_first_of(" \t\"") != std::string::npos) Errors.push_back(std::format("{}: the define '{}' is empty or has a space or a quote", Owner, D));
        };
        CheckDefines(m_Defines, "Module");
        for (std::size_t i = 0; i < m_Libraries.size(); ++i)
        {
            const auto& L = m_Libraries[i];
            const std::string Owner = L.m_Name.empty() ? std::format("Library #{}", i + 1) : std::format("Library '{}'", L.m_Name);
            if (L.m_Name.empty()) Errors.push_back(std::format("{} has no name", Owner));
            for (const auto* pList : { &L.m_IncludeDirs, &L.m_LibDirs, &L.m_Libs, &L.m_RuntimeFiles })
                for (const auto& Path : *pList)
                {
                    if (Path.empty())        Errors.push_back(std::format("{}: an empty path", Owner));
                    else if (IsAbsolute(Path)) Errors.push_back(std::format("{}: '{}' is absolute - paths are relative to the project, so it builds on any machine", Owner, Path));
                }
            CheckDefines(L.m_Defines, Owner);
        }
    }

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;
        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor() const noexcept override { return std::make_unique<descriptor>(); }
        xresource::type_guid ResourceTypeGUID() const noexcept override { return type_guid_v; }
        const char* ResourceTypeName() const noexcept override { return "ScriptModule"; }
        const xproperty::type::object& ResourceXPropertyObject() const noexcept override { return *xproperty::getObjectByType<descriptor>(); }
    };
    namespace details { struct factory_holder { inline static factory s_Instance{}; }; }
}

#endif // XSCRIPT_MODULE_DESCRIPTOR_H
