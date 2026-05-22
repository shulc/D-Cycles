/* MSVC compat injected via /FI (ForcedIncludeFiles). Lives in D-Cycles, not Blender.
 *
 * 1. uint — POSIX typedef absent on MSVC; Blender's mikktspace headers need it globally.
 *
 * 2. <xutility> — guarded_allocator.h has a #ifdef _MSC_VER block that specialises
 *    rebind<> for std::_Container_proxy. In MSVC 14.42 (VS2022 17.12), <new> no longer
 *    pulls in <xutility>, so _Container_proxy is undeclared at the point of use.
 *    Including <vector> here pulls <xutility> which defines std::_Container_proxy
 *    before the template is instantiated. */
#pragma once
#ifdef _MSC_VER
#  include <vector>
#  ifndef uint
typedef unsigned int uint;
#  endif
#endif
