#ifndef XSCRIPT_REGISTRATION_H
#define XSCRIPT_REGISTRATION_H
#pragma once

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
    };

    struct system_entry
    {
        void (*m_pRegisterFn)(xecs::game_mgr::instance&) = nullptr;
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
}

// One per component struct, where XPROPERTY_REG would otherwise go: it does both. CATEGORY and PRIORITY are required (a
// preprocessor macro cannot default an argument): a group name such as "Rendering" and an int that sorts ascending within it.
#define XSCRIPT_REGISTER_COMPONENT(TYPE, CATEGORY, PRIORITY) \
    XPROPERTY_REG(TYPE) \
    inline xscript::self_registration<xscript::component_entry> g_AutoReg_##TYPE \
    { xscript::component_entry \
        { [](xecs::game_mgr::instance& GameMgr, xecs::plugin::token Token) noexcept { GameMgr.RegisterComponents<TYPE>(Token); } \
        , TYPE::typedef_v.m_pName, CATEGORY, PRIORITY \
        , xecs::component::type::info_v<TYPE>.m_Guid.m_Value \
        } \
    };

// Systems are never XPROPERTY_REG'd, so this only does the self-registration half.
#define XSCRIPT_REGISTER_SYSTEM(TYPE) \
    inline xscript::self_registration<xscript::system_entry> g_AutoReg_##TYPE \
    { xscript::system_entry \
        { [](xecs::game_mgr::instance& GameMgr) noexcept { GameMgr.RegisterSystems<TYPE>(); } } \
    };

#endif // XSCRIPT_REGISTRATION_H
