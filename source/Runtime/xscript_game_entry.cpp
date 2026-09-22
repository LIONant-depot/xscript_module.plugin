// The entry points of the game DLL the editor generates from a project's script modules. The editor loads the DLL and calls
// them in the order xecs_plugin_api.h requires: every RegisterComponents before any RegisterSystems. They only walk the
// self-registration lists (xscript_registration.h), so they never change when modules are added or removed.
#include "dependencies/xECSV2/src/xecs.h"
#include "dependencies/xECSV2/src/xecs_plugin_api.h"
#include "xscript_registration.h"

extern "C" __declspec(dllexport)
void XecsPlugin_RegisterComponents( xecs::game_mgr::instance& GameMgr, xecs::plugin::token Token ) noexcept
{
    for (auto* p = xscript::self_registration<xscript::component_entry>::s_pHead; p; p = p->m_pNext)
        p->m_Value.m_pRegisterFn(GameMgr, Token);
}

extern "C" __declspec(dllexport)
void XecsPlugin_RegisterSystems( xecs::game_mgr::instance& GameMgr ) noexcept
{
    for (auto* p = xscript::self_registration<xscript::system_entry>::s_pHead; p; p = p->m_pNext)
        p->m_Value.m_pRegisterFn(GameMgr);
}

extern "C" __declspec(dllexport)
void XecsPlugin_Unregister( xecs::plugin::token /*Token*/ ) noexcept
{
    // Nothing is allocated outside the ECS world, so there is nothing to release (see xecs_plugin_api.h).
}

extern "C" __declspec(dllexport)
void XScript_GetComponentDisplayInfo( xscript::pfn_component_display_visitor pVisitor, void* pUserData ) noexcept
{
    for (auto* p = xscript::self_registration<xscript::component_entry>::s_pHead; p; p = p->m_pNext)
        pVisitor(pUserData, p->m_Value.m_Guid, p->m_Value.m_pName, p->m_Value.m_pCategory, p->m_Value.m_Priority);
}
