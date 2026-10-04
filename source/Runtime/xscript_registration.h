#ifndef XSCRIPT_REGISTRATION_H
#define XSCRIPT_REGISTRATION_H

// What a script module includes to announce its components and systems. Each announcement is an entry in an intrusive
// linked list whose constructor runs at static-init time, so a module is registered just by being compiled into the game
// DLL: the DLL's entry points (xscript_game_entry.cpp) walk the list, and nothing is edited when a module is added or removed.
// This is safe because every module compiles into the same DLL as the reader; xproperty's own reflection registry, by
// contrast, cannot cross a DLL boundary.
//
// Category and priority decide where a component appears in the editor's component lists. They are separate from the
// registration order, which is unspecified across translation units and does not matter. A component that never goes through
// XSCRIPT_REGISTER_COMPONENT (the engine's own) sorts before every categorized one.
#include "dependencies/xECSV2/src/xecs.h"

#include <cstdint>
#include <cstring>

namespace xscript
{
    struct component_entry
    {
        void (*m_pRegisterFn)(xecs::game_mgr::instance&, xecs::plugin::token) = nullptr;
        const char*   m_pName     = "";
        const char*   m_pCategory = "";
        int           m_Priority  = 0;
        // The type's own compile-time guid: identical in every binary the type is compiled into and available as soon as the DLL
        // is loaded, before XecsPlugin_RegisterComponents runs. It lets a candidate DLL's manifest be compared with a scene's
        // component dependencies by stable identity instead of by display name.
        std::uint64_t m_Guid      = 0;
        // The file the component is DEFINED in (__FILE__ where XSCRIPT_REGISTER_COMPONENT stands, so it is the header that holds the struct no matter which
        // module includes it). The editor finds the module from it: the path runs through <...>/ScriptModule/xx/yy/<guid>.desc/source_db/ (xscript_module_paths.h).
        const char*   m_pFile     = "";
    };

    struct system_entry
    {
        void (*m_pRegisterFn)(xecs::game_mgr::instance&) = nullptr;
        std::uint64_t m_Guid      = 0;
        const char*   m_pName     = "";
        const char*   m_pFile     = "";                 // where the system is defined, like component_entry::m_pFile
    };

    // A component that ANOTHER binary registered (the engine's Transform, say) and this module queries. Component type information is
    // kept per binary, so before the module's systems are registered this DLL's own copy of such a type needs the number the registry gave it
    // (SyncLocalBitIDs). Without it the first system that queries the type reads garbage and takes the whole editor down.
    struct use_entry
    {
        void (*m_pSyncFn)() = nullptr;
    };

    template<typename T>
    struct self_registration
    {
        inline static self_registration<T>* s_pHead = nullptr;

        self_registration<T>* m_pNext;
        T                     m_Value;

        explicit self_registration(T Value) noexcept : m_pNext(s_pHead), m_Value(Value) { s_pHead = this; }
    };

    // How the editor learns each component's guid, name, category and priority from the DLL. Not part of xecs_plugin_api.h:
    // GetProcAddress needs only the name and a function-pointer type, and a callback with user data keeps the exported signature
    // ABI-stable (no std::vector or std::function crosses the DLL boundary). Optional: a DLL without it just has no display info.
    inline constexpr const char* kGetComponentDisplayInfoName = "XScript_GetComponentDisplayInfo";
    using pfn_component_display_visitor  = void(__cdecl*)(void* pUserData, std::uint64_t Guid, const char* pName, const char* pCategory, int Priority);
    using pfn_get_component_display_info = void(__cdecl*)(pfn_component_display_visitor pVisitor, void* pUserData);

    // Which file each component and system of the DLL is defined in: the editor maps them to script modules (a component to the module that holds its header). A second export,
    // not a changed one: the editor may load a DLL built before this existed and must not call it through another signature. Optional: a DLL without it just has no mapping.
    // Kind: 0 = component, 1 = system.
    inline constexpr const char* kGetRegistrationsName = "XScript_GetRegistrations";
    using pfn_registration_visitor = void(__cdecl*)(void* pUserData, int Kind, std::uint64_t Guid, const char* pName, const char* pFile);
    using pfn_get_registrations    = void(__cdecl*)(pfn_registration_visitor pVisitor, void* pUserData);
}

// One per component struct, where XPROPERTY_REG would otherwise go: it does both. CATEGORY and PRIORITY are required (a
// preprocessor macro cannot default an argument): a group name such as "Rendering" and an int that sorts ascending within it.
//
// A module that wants the TYPES of an engine component (the physics ones, say) includes the engine's header between
//     #define XSCRIPT_IMPORT_ONLY
//     #include "dependencies/xLIONCore/src/physics/xlioncore_physics.h"
//     #undef  XSCRIPT_IMPORT_ONLY
// and then names each one it uses with XSCRIPT_USES_COMPONENT. The component stays registered once, by the binary that owns it; the
// module only gets its own reflection (XPROPERTY_REG) and its own copy of the type's information.
// (XSCRIPT_REGISTER_COMPONENT is defined at the end of this file, outside the include guard.)

// A module that queries a component it does not define itself (anything of the engine: xlioncore::transform, ...) says so once, anywhere in
// the module:   XSCRIPT_USES_COMPONENT(xlioncore::transform)
// The game DLL's entry syncs each of them before it registers the module's systems.
#define XSCRIPT_PASTE_(A, B) A##B
#define XSCRIPT_PASTE(A, B)  XSCRIPT_PASTE_(A, B)
#define XSCRIPT_USES_COMPONENT(TYPE) \
    inline xscript::self_registration<xscript::use_entry> XSCRIPT_PASTE(g_AutoUse_, __COUNTER__) \
    { xscript::use_entry{ []() noexcept { xecs::component::mgr::SyncLocalBitIDs<TYPE>(); } } };

// Systems are never XPROPERTY_REG'd, so this only does the self-registration half.
#define XSCRIPT_REGISTER_SYSTEM(TYPE) \
    inline xscript::self_registration<xscript::system_entry> g_AutoReg_##TYPE \
    { xscript::system_entry \
        { [](xecs::game_mgr::instance& GameMgr) noexcept { GameMgr.RegisterSystems<TYPE>(); } \
        , xecs::system::type::info_v<TYPE>.m_Guid.m_Value, xecs::system::type::info_v<TYPE>.m_pName, __FILE__ \
        } \
    };

#endif // XSCRIPT_REGISTRATION_H

// XSCRIPT_REGISTER_COMPONENT is chosen here, OUTSIDE the include guard, so that it is chosen again every time this header is included: an engine header (which includes this one
// itself) read between  #define XSCRIPT_IMPORT_ONLY  and  #undef XSCRIPT_IMPORT_ONLY  gets the import-only one, even when the module included this header before. A module
// that uses the macro for its own components after such a block includes this header once more, so it gets the registering one back.
#undef XSCRIPT_REGISTER_COMPONENT
#ifdef XSCRIPT_IMPORT_ONLY
    #define XSCRIPT_REGISTER_COMPONENT(TYPE, CATEGORY, PRIORITY) XPROPERTY_REG(TYPE)
#else
#define XSCRIPT_REGISTER_COMPONENT(TYPE, CATEGORY, PRIORITY) \
    XPROPERTY_REG(TYPE) \
    inline xscript::self_registration<xscript::component_entry> g_AutoReg_##TYPE \
    { xscript::component_entry \
        { [](xecs::game_mgr::instance& GameMgr, xecs::plugin::token Token) noexcept { GameMgr.RegisterComponents<TYPE>(Token); } \
        , TYPE::typedef_v.m_pName, CATEGORY, PRIORITY \
        , xecs::component::type::info_v<TYPE>.m_Guid.m_Value \
        , __FILE__ \
        } \
    };
#endif
