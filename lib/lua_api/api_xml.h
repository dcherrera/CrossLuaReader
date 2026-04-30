/**
 * @file api_xml.h
 * @brief Lua xml.* module — opt-in capability bound by api_register_capability
 *        when a plugin manifest declares requires = {"xml"}.
 *
 * @status Phase 9.A.1
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register xml.* into the given Lua state. */
void api_xml_register(lua_State *L);
