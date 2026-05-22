# Included via CMAKE_PROJECT_INCLUDE right after Blender's project() call.
# Injects MSVC Windows compatibility without touching Blender source.
#
# Two problems solved here:
#   1. _USE_MATH_DEFINES — MSVC hides M_PI from <cmath> without it.
#      OpenSubdiv precompiled headers (loopScheme.h) use M_PI directly.
#   2. uint — POSIX typedef absent on MSVC; Blender's mikktspace headers
#      use it in global scope. Force-include our compat header.
if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES)
    get_filename_component(_win_compat_h
        "${CMAKE_CURRENT_LIST_DIR}/win_msvc_types.h" ABSOLUTE)
    add_compile_options("/FI${_win_compat_h}")
endif()
