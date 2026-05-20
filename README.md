# D-Cycles

D bindings for [Cycles X](https://docs.blender.org/manual/en/latest/render/cycles/), Blender's production path tracer. Embedded library mode — links Cycles in-process (not via the `blender --background` CLI nor via the `cycles_standalone` executable) for zero-overhead IPR.

Architecture matches the neighbour `D-OpenSubdiv`: a thin C shim (`csrc/cyclesc.h`) wraps Cycles' C++ API; D side binds via `extern (C)`.

## Status — Phase 0c done (CPU)

| Component | Status |
|---|---|
| C shim header (`csrc/cyclesc.h`) — designed API surface | ✅ |
| Stub C shim (`csrc/cyclesc.cpp`) for `WITH_CYCLES=OFF` builds | ✅ |
| Real wrappers (`csrc/cyclesc_*.cpp`) for `WITH_CYCLES=ON` | ✅ |
| D bindings (`source/cycles/c.d`) | ✅ |
| Cycles libs build (`tools/build_cycles_libs.sh`) — CPU + Embree | ✅ |
| `WITH_CYCLES=ON` CMake links against real Cycles | ✅ |
| Triangle smoke test renders red-on-grey via real path tracer | ✅ |
| GPU backends (CUDA / OptiX / HIP / Metal) | ⏳ next |
| Full shader-graph API (Phase 2) | ⏳ |

The stub library still exists so the D bindings and downstream code can link without dragging in Cycles when the renderer isn't needed.

## Use

```d
import cycles.c;

cyc_session_params params = {
    device_type:  cyc_device_type.CPU,    // OPTIX / HIP / METAL after Phase 0c
    device_index: 0,
    samples:      64,
    interactive:  0,
};
cyc_session_t* session;
cyc_session_create(&params, &session);
// build scene through cyc_mesh_*, cyc_object_*, cyc_light_*, cyc_camera_*, cyc_shader_create_principled
cyc_session_reset(session, 512, 512);
cyc_session_start(session);
cyc_session_wait(session);
cyc_session_save_image(session, "out.png");
```

End-to-end example: [`examples/triangle/source/app.d`](examples/triangle/source/app.d).

## Build

### Stub mode (no Cycles dependency)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cyclesc
```

### Real Cycles backend

1. Pull the Blender source and its precompiled deps:

   ```bash
   git submodule update --init extern/blender
   git -C extern/blender config --local submodule.lib/linux_x64.update checkout
   git -C extern/blender submodule update --init --depth 1 lib/linux_x64
   ```

   The `lib/linux_x64` submodule is a Git LFS repo (~2.4 GB on disk) carrying precompiled OpenImageIO / OpenEXR / Embree / OpenImageDenoise / TBB / OpenSubdiv / etc, pinned to Blender 4.5 LTS. Requires `git-lfs` installed.

2. Build the Cycles static libs via Blender's CMake. We can't `add_subdirectory(extern/blender)` because Blender's top-level CMakeLists calls `project(Blender)` and assumes `CMAKE_SOURCE_DIR` is its own root. A helper script handles the chain:

   ```bash
   tools/build_cycles_libs.sh
   ```

   First run: ~15 min on a 16-core CPU. Produces `extern/blender/build_cycles/lib/libcycles_*.a`.

3. Configure & build our shim:

   ```bash
   cmake -S . -B build -DWITH_CYCLES=ON
   cmake --build build --target cyclesc
   ```

4. Build & run the triangle smoke test:

   ```bash
   tools/build_triangle.sh
   ./examples/triangle/triangle
   open examples/triangle/triangle.png
   ```

The first-iteration backend is CPU-only (with Embree for BVH). GPU backends are a follow-up task — see `doc/phase_0c_plan.md`.

## Layout

```
csrc/
├── cyclesc.h               # public C shim API surface
├── cyclesc.cpp             # stub for WITH_CYCLES=OFF
├── cyclesc_internal.h      # shared C++ helpers (real backend)
├── cyclesc_common.cpp      # path_init / log_init / mat4 helper
├── cyclesc_devices.cpp     # cyc_devices_query
├── cyclesc_session.cpp     # session lifecycle + capturing output driver
├── cyclesc_scene.cpp       # scene root ops
├── cyclesc_mesh.cpp        # vertex / triangle uploads
├── cyclesc_object.cpp      # transforms, shader binding
├── cyclesc_light.cpp       # point / sun / spot / area / bg lights
├── cyclesc_camera.cpp      # perspective / ortho, lookat, DoF
├── cyclesc_shader.cpp      # Principled BSDF convenience (Phase 2 = full graph)
└── cyclesc_render.cpp      # read_framebuffer / save_image (via OIIO)
source/cycles/
└── c.d                     # extern (C) D bindings
examples/triangle/          # smoke test
extern/blender/             # Blender submodule, pinned to blender-v4.5-release
tools/
├── build_cycles_libs.sh    # builds libcycles_*.a via Blender's CMake
└── build_triangle.sh       # compiles examples/triangle directly via ldc2
CMakeLists.txt
dub.json
```

## License

Bindings: Boost Software License 1.0.

Cycles itself: Apache License 2.0 (since Blender 2.81). Compatible with permissive licensing including MIT — vibe3d can ship a build that statically links Cycles without GPL contamination.
