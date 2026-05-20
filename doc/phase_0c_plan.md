# Phase 0c — Cycles real integration plan

Реализация Phase 0c: заменить stub-имплементацию `csrc/cyclesc.cpp` на реальный wrapper над Cycles X из Blender source tree. По завершении: triangle.png рендерится через настоящий Cycles, биндинг готов к использованию в vibe3d's `render/cycles_backend.d`.

**Реалистичный бюджет**: 40–70 часов от cold start до зелёного triangle smoke test'а. Большая часть времени уходит **не на C++ код**, а на возню с Blender build system'ом.

## Цели и критерии успеха

- [ ] `extern/blender/` — pinned submodule на стабильный Blender release branch
- [ ] `cmake -DWITH_CYCLES=ON --target cyclesc` собирает `libcyclesc.a` со статически слинкованным Cycles
- [ ] `examples/triangle/` рендерит ту же сцену что `D-RadeonProRender/examples/triangle/` (треугольник + point light + diffuse material → PNG) через Cycles + OpenImageIO
- [ ] Работает как минимум один device backend (CPU обязательно; CUDA/OptiX/HIP/Metal — по доступности)
- [ ] D extern(C) биндинг компилируется без изменений (API surface уже зафиксирован в `csrc/cyclesc.h`)

Чего **не** делаем в Phase 0c:
- ShaderGraph API (Phase 2 — нужен для Shader Tree compiler'а)
- AOV / Render Outputs (Phase 4)
- Image texture loading (Phase 2)
- Volume / Hair (Phase 5+)
- Multi-device session (опционально, не блокирует)

## Prerequisites

### Дисковое пространство

- ~5 GB — Blender source (shallow clone branch)
- ~30 GB — precompiled deps tree (lib/, в зависимости от платформы)
- ~5 GB — Cycles build artifacts (out-of-tree build dir)

**Итого ~40 GB** на платформу. На текущем `/dev/sdb3` свободно 169 GB — норм.

### Системные библиотеки

Fedora 43 (текущая dev-машина):
```bash
sudo dnf install -y \
    gcc gcc-c++ cmake ninja-build git python3 \
    libX11-devel libXi-devel libXxf86vm-devel libXcursor-devel libXfixes-devel \
    libXrandr-devel libXinerama-devel mesa-libGL-devel mesa-libGLU-devel \
    openssl-devel zlib-devel libpng-devel libjpeg-turbo-devel \
    libtiff-devel libwebp-devel xz-devel
```

### Build tools

- `cmake` ≥ 3.20 (есть)
- `ninja` (рекомендуется — параллельная сборка)
- `gcc` / `clang` C++17
- `python3` для Blender build scripts
- `git` (есть)
- 16+ ядер CPU желательно — иначе `make deps` будет идти полдня

### GPU device toolkits (опционально)

Для активации соответствующего device backend'а в Cycles:

| Backend | Нужен | Размер | Где брать |
|---|---|---|---|
| CPU | ничего | — | стандарт, всегда работает |
| CUDA | CUDA Toolkit ≥ 11.4 | ~3 GB | developer.nvidia.com |
| OptiX | CUDA + OptiX SDK ≥ 7.3 | ~200 MB сверху | developer.nvidia.com/optix |
| HIP | ROCm ≥ 5.0 | — | в Fedora из коробки (`libamdhip64.so.6`) |
| Metal | Xcode | — | только macOS |
| oneAPI | Intel oneAPI Base Toolkit | ~5 GB | Intel сайт |

На Linux+NVIDIA приоритет — **OptiX**, потому что использует RT-ядра и даёт 5-15× speedup на RTX-картах.

## Phase 0c-1: Blender source

### Цель
Подтянуть Blender source как submodule на pinned release branch.

### Шаги

```bash
cd ~/Code/D-Cycles
git submodule add -b blender-v4.4-release --depth 1 \
    https://projects.blender.org/blender/blender.git extern/blender
git submodule update --init --depth 1 extern/blender
```

Pin на `blender-v4.4-release` (или последний LTS на момент работы — проверь сам). Branch `main` категорически не использовать — Cycles API меняется без предупреждений.

### Размер и время
- Shallow clone (`--depth 1`) — ~800 MB, 5-10 минут
- Full clone — ~2.5 GB, 30+ минут (не нужно)

### Что проверить
- `extern/blender/intern/cycles/CMakeLists.txt` — на месте
- `extern/blender/intern/cycles/scene/scene.h` — public C++ API
- `extern/blender/lib/` — **пуст** (тут будут precompiled deps после следующего шага)

### Риски
- **ABI break при bump**. При апдейте на новую Blender версию ждать поломок в C++ API. Тестим только при сознательной миграции.
- **Submodule depth=1 ломает git workflow** — не получится `git log` посмотреть. Если нужна история — depth=100 или unlimited.

## Phase 0c-2: Precompiled deps

### Цель
Собрать `extern/blender/lib/linux_x86_64/` — дерево предкомпилированных зависимостей: OpenImageIO, OSL, OpenSubdiv, Embree, OpenColorIO, Alembic, USD, OpenVDB, TBB, Boost, и далее по списку.

### Шаги

```bash
cd ~/Code/D-Cycles/extern/blender
make deps
```

**Это самый долгий шаг во всём Phase 0c — 2–6 часов** в зависимости от железа. Запускать на ночь.

### Что делает `make deps` под капотом
Blender's `make deps` это alias для:
```
cd build_files/build_environment && cmake -B ../../deps_build && cmake --build ../../deps_build
```
Скрипт клонирует исходники каждой зависимости с фиксированной версии, собирает их в правильном порядке (учитывая взаимозависимости), результат складывает в `lib/<platform>/`.

### Что проверить после завершения
```bash
ls extern/blender/lib/linux_x86_64/
# Ожидаются директории:
#   openimageio/  openshadinglanguage/  opensubdiv/
#   embree/  openvdb/  ocio/  alembic/  usd/
#   boost/  tbb/  zlib/  png/  jpeg/  tiff/
```

### Риски

1. **Один из deps не собрался** — обычно из-за gcc-13/14 несовместимости со старыми версиями OSL или USD. Митигация:
   - Прочитать ошибку, найти патч на blender.org dev forum
   - Или использовать Docker с подходящим базовым образом (см. `extern/blender/build_files/buildbot/`)

2. **Сборка не помещается в RAM** — особенно USD и LLVM (входит в OSL). Митигация:
   - `-j 4` вместо `-j $(nproc)` чтобы не запускать слишком много clang параллельно
   - Swap файл если RAM < 32 GB

3. **Время** — может занять ночь. Запускать в `screen`/`tmux`.

### Шортката для Linux: распакованный дистрибутив
Blender Foundation выкладывает готовые `lib/<platform>/` архивы на `https://svn.blender.org/svnroot/bf-blender/trunk/lib/`. Можно **скачать готовые** вместо локальной сборки:

```bash
# Внутри extern/blender:
svn checkout https://svn.blender.org/svnroot/bf-blender/trunk/lib/linux_x86_64 lib/linux_x86_64
```

Это **сильно быстрее** (минут 10-30 на скачивание ~3-5 GB), но требует `subversion` клиент и привязывает к конкретной версии lib/ от Blender Foundation.

Рекомендация: **попробовать SVN-путь первым**, fallback на `make deps` если не сработает.

## Phase 0c-3: Cycles isolated build

### Цель
Собрать Cycles как набор статических библиотек, не собирая весь Blender (Blender headless build тянет Python, mesh editing, animation, и ещё кучу всего что нам не нужно).

### Шаги

```bash
cd ~/Code/D-Cycles
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DWITH_CYCLES=ON \
    -DCYCLES_INSTALL_PATH="${PWD}/build/cycles_install" \
    -DBLENDER_SRC="${PWD}/extern/blender"
cmake --build build --target cyclesc -j
```

### CMakeLists.txt дополнения (то, что сейчас в TODO-блоке)

```cmake
if(WITH_CYCLES)
    set(WITH_CYCLES_STANDALONE      OFF CACHE BOOL "")
    set(WITH_CYCLES_STANDALONE_GUI  OFF CACHE BOOL "")
    set(WITH_CYCLES_DEVICE_CUDA     ON  CACHE BOOL "")
    set(WITH_CYCLES_DEVICE_OPTIX    ON  CACHE BOOL "")
    set(WITH_CYCLES_DEVICE_HIP      OFF CACHE BOOL "")  # AMD only
    set(WITH_CYCLES_DEVICE_METAL    OFF CACHE BOOL "")  # macOS only
    set(WITH_CYCLES_DEVICE_ONEAPI   OFF CACHE BOOL "")  # Intel Arc
    set(WITH_CYCLES_OSL             ON  CACHE BOOL "")
    set(WITH_CYCLES_EMBREE          ON  CACHE BOOL "")  # CPU BVH
    set(WITH_CYCLES_OPENIMAGEDENOISE ON CACHE BOOL "")
    set(WITH_CYCLES_OPENSUBDIV      ON  CACHE BOOL "")

    # Tell Cycles where its precompiled deps are.
    set(LIBDIR "${BLENDER_SRC}/lib/linux_x86_64" CACHE PATH "")
    add_subdirectory("${BLENDER_SRC}/intern/cycles" cycles)

    target_compile_definitions(cyclesc PUBLIC CYCLES_AVAILABLE=1)
    target_link_libraries(cyclesc PRIVATE
        cycles_session
        cycles_scene
        cycles_kernel
        cycles_device
        cycles_bvh
        cycles_subd
        cycles_graph
        cycles_util
        # plus the deps Cycles links against:
        # OIIO, OSL, OpenColorIO, OpenSubdiv, Embree, OIDN, TBB, etc.
    )
endif()
```

### Артефакты после успешной сборки

- `build/libcyclesc.a` — наш wrapper (раньше был stub, теперь со включённым Cycles)
- `build/cycles/lib/*.a` — Cycles internal libs (~10 штук)
- `build/cycles/lib/precompiled_kernels/` — для CUDA/OptiX/HIP: SASS/PTX/CSO бинари рендер-кернелов

### Риски

1. **Cycles X build не задизайнен как standalone library outside Blender** — это самый главный риск. Возможно потребуется патчинг Cycles `CMakeLists.txt` чтобы:
   - Подавить попытки слинковаться с `bf_blenkernel` (Blender internal API)
   - Заменить Blender-specific image loading на собственное (или через OIIO)
   - Отключить Cycles "session manager" frontend если он зависит от Blender RNA

   Митигация: посмотреть на `intern/cycles/app/` — это Blender's `cycles_standalone` build target, он не зависит от Blender's RNA и показывает minimum-viable embedded use. Использовать его как шаблон.

2. **OptiX requires CUDA toolkit at build time** — для шейдер-компиляции CUDA→PTX→OptiX. Cycles ожидает что `nvcc` доступен. На машинах без CUDA — отключить OptiX.

3. **Symbol conflicts** — если vibe3d уже линкует TBB или другую общую библиотеку, статический Cycles потянет свои версии и будут duplicate symbols. Митигация: сборка Cycles как `.so` (но это разрушает «zero-overhead embedded» идею), либо общее управление version'ом deps.

## Phase 0c-4: C shim implementation

### Цель
Заменить stub-функции в `csrc/cyclesc.cpp` на реальные wrapper'ы.

### Стратегия

Раздробить `cyclesc.cpp` на тематические файлы:

```
csrc/
├── cyclesc.h                # uniform API surface
├── cyclesc_common.cpp       # status codes, util
├── cyclesc_devices.cpp      # cyc_devices_query
├── cyclesc_session.cpp      # cyc_session_*
├── cyclesc_scene.cpp        # cyc_scene_*
├── cyclesc_mesh.cpp         # cyc_mesh_*
├── cyclesc_object.cpp       # cyc_object_*
├── cyclesc_light.cpp        # cyc_light_*
├── cyclesc_camera.cpp       # cyc_camera_*
├── cyclesc_shader.cpp       # cyc_shader_*  (минимум — Principled convenience; full graph — позже)
└── cyclesc_render.cpp       # cyc_session_read_framebuffer / save_image
```

Каждый файл ~50-200 строк wrapper-кода. Total оценка: ~1500-2000 строк C++.

### Per-функция template

Каждая функция следует этому паттерну:

```cpp
// csrc/cyclesc_mesh.cpp
#include "cyclesc.h"
#include "scene/mesh.h"     // ccl::Mesh
#include "scene/scene.h"    // ccl::Scene

extern "C" cyc_status cyc_mesh_create(cyc_scene_t* scene_h, cyc_mesh_t** out_mesh) {
    if (!scene_h || !out_mesh) return CYC_ERR_INVALID_ARGUMENT;
    auto* scene = reinterpret_cast<ccl::Scene*>(scene_h);
    try {
        auto* mesh = scene->create_node<ccl::Mesh>();   // Cycles owns it
        *out_mesh = reinterpret_cast<cyc_mesh_t*>(mesh);
        return CYC_OK;
    } catch (const std::exception&) {
        return CYC_ERR_INTERNAL;
    }
}

extern "C" cyc_status cyc_mesh_set_verts(cyc_mesh_t* mesh_h, const float* verts, int num_verts) {
    if (!mesh_h || !verts || num_verts <= 0) return CYC_ERR_INVALID_ARGUMENT;
    auto* mesh = reinterpret_cast<ccl::Mesh*>(mesh_h);
    mesh->reserve_mesh(num_verts, /*num_triangles*/0);
    auto* P = mesh->verts.resize(num_verts);
    for (int i = 0; i < num_verts; ++i) {
        P[i] = ccl::make_float3(verts[i*3+0], verts[i*3+1], verts[i*3+2]);
    }
    mesh->tag_verts_modified();
    return CYC_OK;
}
```

### Ключевые Cycles X классы и где они живут

| Концепция | Класс | Header |
|---|---|---|
| Render session | `ccl::Session` | `session/session.h` |
| Session config | `ccl::SessionParams` | `session/session.h` |
| Scene graph | `ccl::Scene` | `scene/scene.h` |
| Scene config | `ccl::SceneParams` | `scene/scene.h` |
| Triangle mesh | `ccl::Mesh` | `scene/mesh.h` |
| Instance | `ccl::Object` | `scene/object.h` |
| Light | `ccl::Light` | `scene/light.h` |
| Camera | `ccl::Camera` | `scene/camera.h` |
| Material | `ccl::Shader` | `scene/shader.h` |
| Shader graph | `ccl::ShaderGraph` | `scene/shader_graph.h` |
| Shader nodes | `ccl::ShaderNode` subclasses | `scene/shader_nodes.h` |
| Device manager | `ccl::DeviceInfo` + `ccl::Device` | `device/device.h` |
| Display driver (IPR) | `ccl::DisplayDriver` | `session/display_driver.h` |
| Output driver (final) | `ccl::OutputDriver` | `session/output_driver.h` |

### Reference implementation
`extern/blender/intern/cycles/app/cycles_standalone.cpp` — это **готовый embedded example от Blender Foundation**. Он строит сцену из XML, не из BPY/RNA. Использовать как шпаргалку — там видно правильный порядок вызовов:
1. `Device::available_devices()` → выбрать
2. `SessionParams` → `new Session(params)`
3. `session->scene_load()` → получить Scene*
4. строим scene (mesh, object, light, camera)
5. `session->start()` → асинк рендер
6. `session->wait()` → блок до готовности
7. `OutputDriver` → save_image

### Что НЕ имплементируем в Phase 0c

- `cyc_shader_add_node` / `cyc_shader_connect` / `cyc_shader_node_set_input_*` — graph API. Возвращают `CYC_ERR_UNSUPPORTED` до Phase 2.
- `cyc_shader_create_principled` — обязательно, минимум для smoke test'а.

## Phase 0c-5: Triangle smoke test

### Цель
Заменить `examples/triangle/source/app.d` на полноценный сценарий, аналогичный `D-RadeonProRender/examples/triangle/source/app.d`.

### Ожидаемый код

```d
import std.stdio;
import std.string : toStringz;
import cycles.c;

void check(cyc_status s, string where) { /* abort on err */ }

int main(string[] args) {
    // Device pick
    cyc_device_info[8] devs;
    int count;
    check(cyc_devices_query(devs.ptr, 8, &count), "devices_query");
    writefln("Cycles devices: %d", count);

    // Session params (CPU default; CUDA/OPTIX/HIP/METAL по env override)
    cyc_session_params sp = {
        device_type: cyc_device_type.CPU,
        samples: 64,
        interactive: 0,
    };
    cyc_session_t* session;
    check(cyc_session_create(&sp, &session), "session_create");
    scope(exit) cyc_session_destroy(session);

    auto* scene = cyc_session_scene(session);

    // Mesh: 1 triangle
    cyc_mesh_t* mesh;
    check(cyc_mesh_create(scene, &mesh), "mesh_create");
    static immutable float[9] verts = [
         0.0f,  0.8f, 0.0f,
        -0.8f, -0.6f, 0.0f,
         0.8f, -0.6f, 0.0f,
    ];
    static immutable int[3] tri = [0, 1, 2];
    check(cyc_mesh_set_verts(mesh, verts.ptr, 3), "verts");
    check(cyc_mesh_set_triangles(mesh, tri.ptr, 1, null), "tris");

    // Object + Material
    cyc_object_t* obj;
    check(cyc_object_create(scene, &obj), "obj_create");
    cyc_object_set_mesh(obj, mesh);
    cyc_shader_t* shader;
    check(cyc_shader_create_principled(scene, &shader), "shader");
    cyc_shader_set_principled_base_color(shader, 0.85f, 0.25f, 0.15f);
    cyc_object_set_shader(obj, shader);

    // Light
    cyc_light_t* light;
    check(cyc_light_create(scene, cyc_light_type.POINT, &light), "light");
    static immutable float[16] lightXform = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,2,2,1];
    cyc_light_set_transform(light, lightXform.ptr);
    cyc_light_set_intensity(light, 60.0f);

    // Camera
    cyc_camera_t* cam;
    check(cyc_camera_create_perspective(scene, &cam), "cam");
    cyc_camera_lookat(cam, 0,0,3,  0,0,0,  0,1,0);
    cyc_scene_set_active_camera(scene, cam);

    // Render
    check(cyc_session_reset(session, 512, 512), "reset");
    check(cyc_session_start(session), "start");
    check(cyc_session_wait(session), "wait");
    check(cyc_session_save_image(session, "triangle.png".toStringz), "save");
    writeln("Wrote triangle.png");
    return 0;
}
```

### Критерий успеха
Файл `examples/triangle/triangle.png` появляется, содержит распознаваемый оранжевый треугольник с diffuse-затенением от point light'а — визуально сопоставимо с тем, что выдал RPR.

## Phase 0c-6: Multi-platform validation

### Платформы для верификации

| Платформа | Backend | Когда запускать |
|---|---|---|
| Linux x86_64 + CPU | CPU | первый success в Phase 0c-5 |
| Linux x86_64 + NVIDIA OptiX | OPTIX | следующий шаг — это **ради чего** мы вообще делаем Cycles backend |
| Windows x86_64 + CPU | CPU | в Windows VM или dual-boot |
| Windows x86_64 + NVIDIA OptiX | OPTIX | подтвердить главный target |
| macOS arm64 + Metal | METAL | на Mac, отдельная сессия |

Каждая комбинация = пересборка deps + Cycles. **Не пытаться кросс-компилировать** — Cycles это не дружелюбно к этому.

## Risk register

| Риск | Вероятность | Влияние | Митигация |
|---|---|---|---|
| Cycles build не работает out-of-Blender | **высокая** | блокер | использовать `cycles_standalone` как референс, патчить CMake |
| `make deps` падает на текущем gcc | средняя | задержка дней | использовать SVN-precompiled lib/ от Blender Foundation |
| OptiX SDK не находится | низкая | -20% perf | сначала запустить CPU, потом OptiX отдельно |
| ABI break при следующем Blender release | средняя | поломка раз в 6-12 месяцев | pin tightly, тестить bump осознанно |
| Symbol conflicts с vibe3d deps | средняя | блокер для интеграции | смотреть `nm libcyclesc.a`, vendoring если нужно |
| Cycles GPU device init segfault (как у RPR на этой машине) | низкая для CPU, средняя для GPU | блокер только GPU части | CPU-first development; GPU validation отдельной задачей |

## Cycles X internals reference

Полезные точки входа в `extern/blender/intern/cycles/`:

```
intern/cycles/
├── app/
│   ├── cycles_standalone.cpp         # пример embedded use без Blender — это ШПАРГАЛКА
│   ├── cycles_xml.cpp                # XML scene parser — образец сценарного API
│   └── opengl/                       # OpenGL display driver — образец IPR
├── session/
│   ├── session.h / session.cpp       # ccl::Session — главная точка входа
│   ├── display_driver.h              # for IPR — нужен в Phase 1
│   ├── output_driver.h               # for final — нужен в Phase 0c
│   └── tile.h                        # progressive sampling
├── scene/
│   ├── scene.h / scene.cpp           # ccl::Scene
│   ├── mesh.h / mesh.cpp             # ccl::Mesh — vertex/triangle arrays
│   ├── object.h / object.cpp         # ccl::Object
│   ├── light.h / light.cpp           # ccl::Light
│   ├── camera.h / camera.cpp         # ccl::Camera
│   ├── shader.h / shader.cpp         # ccl::Shader + ccl::ShaderGraph
│   └── shader_nodes.h                # все BSDF / texture nodes
├── device/
│   ├── device.h                      # DeviceInfo + Device::available_devices()
│   ├── cuda/                         # CUDA device backend
│   ├── optix/                        # OptiX device backend (RT cores)
│   ├── hip/                          # HIP device backend (AMD)
│   └── metal/                        # Metal device backend
└── kernel/                           # GPU shading + integrator kernels (мы это не трогаем)
```

**Что читать первым делом**:
1. `app/cycles_standalone.cpp` — целиком, ~600 строк, показывает minimum-viable Cycles embedded
2. `session/session.h` — публичный API Session
3. `scene/scene.h` — публичный API Scene
4. `app/cycles_xml.cpp` — как добавлять mesh/light/camera/shader без BPY

## Per-file implementation map

Распределение работы по C++ файлам с примерной нагрузкой:

| Файл | Цель | Кол-во функций | Сложность | Оценка часов |
|---|---|---|---|---|
| `cyclesc_common.cpp` | error mapping, helpers | — | low | 1 |
| `cyclesc_devices.cpp` | enumerate devices | 1 | low | 1 |
| `cyclesc_session.cpp` | session lifecycle | 7 | **high** (асинхронность, потоки) | 6 |
| `cyclesc_scene.cpp` | scene root, active camera | 2 | low | 1 |
| `cyclesc_mesh.cpp` | vertex/tri/normal/uv | 6 | medium | 3 |
| `cyclesc_object.cpp` | transform, shader bind | 5 | low | 2 |
| `cyclesc_light.cpp` | 6 light types | 8 | medium | 3 |
| `cyclesc_camera.cpp` | perspective/ortho, lookat | 8 | medium | 2 |
| `cyclesc_shader.cpp` | Principled convenience only | 8 | medium | 4 |
| `cyclesc_render.cpp` | framebuffer readback, save | 2 | medium (нужен OutputDriver) | 3 |
| Build system / CMake | wire deps, debug | — | **high** | **10** |
| First-time debugging Cycles startup | — | — | **very high** (новый код) | **8** |
| Triangle smoke test + iteration | — | — | medium | 3 |
| **Total** | | ~50 | | **~47 ч** |

47 часов — base case. Realistic с учётом неожиданных проблем: **60–80 часов**, можно растянуть на 2 недели по вечерам.

## Open questions

1. **Какая версия Blender pinned?** План написан под Blender 4.4. На момент Phase 0c старта проверить актуальный LTS / последний stable release. Cycles X не сломался с 3.0 принципиально, но API мелочи дрейфуют.

2. **Где брать lib/ — SVN или make deps?** SVN быстрее но привязывает к Blender Foundation версии. `make deps` гибче но дольше. Попробовать SVN первым.

3. **Static vs shared linking Cycles?** Static проще для дистрибуции, но раздувает `.a` до ~500 MB и может конфликтовать с TBB/OIIO в vibe3d. Shared (`libcycles.so`) аккуратнее но требует ship runtime отдельно. Решить **после** первого работающего build'а — измерить размер и symbol conflicts.

4. **OutputDriver vs прямое чтение из buffer?** `OutputDriver` это callback-based — Cycles вызывает наш `write_render_tile()` когда тайл готов. `Session::get_framebuffer()` (если такой есть) — pull-based. Для IPR удобнее display driver. Для batch save — OutputDriver. Решить при имплементации `cyc_session_read_framebuffer`.

5. **Какие deps реально нужны?** Cycles' CMake тянет всё подряд. Для minimum-viable можно отключить: USD, Alembic, OSL (если только Principled BSDF), OpenSubdiv (если только flat meshes). Это сократит build time и `lib/` размер. Решить через CMake опции.

## Чеклист завершения Phase 0c

- [ ] `extern/blender` submodule pinned, есть в `.gitmodules`
- [ ] `make deps` или SVN lib/ — успешно завершён, артефакты в `extern/blender/lib/<platform>/`
- [ ] `cmake -DWITH_CYCLES=ON` без ошибок
- [ ] `cmake --build build --target cyclesc` собирает `libcyclesc.a` без unresolved symbols
- [ ] `examples/triangle/` собирается через `dub build`
- [ ] `./triangle` создаёт `triangle.png` с видимым треугольником
- [ ] README обновлён: «Phase 0c: done» + раздел "Build instructions" с реальными командами
- [ ] Phase 0c документ закрыт; обновлён `vibe3d/doc/renderer_choice_plan.md`: статус Cycles binding'а
- [ ] Создан issue/задача на следующий шаг (Phase 1b — Cycles backend в vibe3d)

## Что после Phase 0c

Когда triangle.png работает через Cycles — этап завершён. Дальше:
- **Phase 1b** vibe3d: написать `source/render/cycles_backend.d`, реализующий тот же `RenderBackend` interface что и `rpr_backend.d`. Параллельная имплементация, разница только в backend-specific вызовах.
- **Phase 2**: расширить `csrc/cyclesc_shader.cpp` под полный graph API (`cyc_shader_add_node` / `cyc_shader_connect`). Это нужно для Shader Tree compiler'а в vibe3d.
- **Платформо-специфичная GPU валидация** — отдельная задача после CPU-first работающей картинки.
