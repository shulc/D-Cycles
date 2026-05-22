/// Phase 0c smoke test: render a single triangle with a Principled
/// shader, lit by a point light, to triangle.png — through the real
/// Cycles backend.
///
/// Equivalent to D-RadeonProRender/examples/triangle/source/app.d in
/// spirit. Both demonstrate the smallest scene that exercises the
/// renderer's geometry, light, shader, and image-output paths.

import std.stdio;
import std.string : toStringz;
import core.stdc.stdlib : exit;

import cycles.c;

void check(cyc_status s, string where)
{
    if (s == CYC_OK) return;
    stderr.writefln("cyclesc %s failed: status=%d", where, s);
    exit(1);
}

int main(string[] args)
{
    // 1. Enumerate devices --------------------------------------------
    int count;
    cyc_device_info[16] devs;
    check(cyc_devices_query(devs.ptr, 16, &count), "devices_query");
    writefln("Cycles devices visible: %d", count);
    foreach (i; 0 .. count) {
        import std.conv : to;
        const name = to!string(devs[i].name.ptr);
        writefln("  [%d] type=%s  name=%s  hw_rt=%d",
                 i, devs[i].type, name, devs[i].supports_hw_rt);
    }

    // 2. Session ------------------------------------------------------
    // Pick the best device that's actually visible in the enumeration
    // above. CYC_DEVICE env override wins; otherwise the platform-
    // appropriate accelerator (OPTIX on NVIDIA Linux/Windows, METAL on
    // Apple Silicon) is preferred, falling back to CPU which always
    // works. Picking a device that the build wasn't compiled with —
    // e.g. OPTIX on macOS where the Cycles libs ship without it —
    // returns CYC_ERR_UNSUPPORTED (-4) at session_create; this loop
    // sidesteps that by only choosing from devs[].
    auto pick_device = () {
        import std.process : environment;
        cyc_device_type[] preferred;
        if (auto env = environment.get("CYC_DEVICE")) {
            switch (env) {
                case "CPU":   preferred = [cyc_device_type.CPU];   break;
                case "CUDA":  preferred = [cyc_device_type.CUDA];  break;
                case "OPTIX": preferred = [cyc_device_type.OPTIX]; break;
                case "HIP":   preferred = [cyc_device_type.HIP];   break;
                case "METAL": preferred = [cyc_device_type.METAL]; break;
                default:      preferred = [cyc_device_type.CPU];   break;
            }
        } else {
            version (OSX)          preferred = [cyc_device_type.METAL, cyc_device_type.CPU];
            else version (Windows) preferred = [cyc_device_type.OPTIX, cyc_device_type.CUDA, cyc_device_type.CPU];
            else                   preferred = [cyc_device_type.OPTIX, cyc_device_type.CUDA, cyc_device_type.CPU];
        }
        foreach (want; preferred)
            foreach (i; 0 .. count)
                if (devs[i].type == want)
                    return want;
        return cyc_device_type.CPU;
    };
    cyc_session_params sp = {
        device_type:  pick_device(),
        device_index: 0,
        samples:      64,
        threads:      0,
        tile_size:    0,
        use_denoiser: 0,
        interactive:  0,
    };
    writefln("Trying device: %s", sp.device_type);
    cyc_session_t* session;
    check(cyc_session_create(&sp, &session), "session_create");
    scope(exit) cyc_session_destroy(session);

    cyc_scene_t* scene = cyc_session_scene(session);

    // 3. Geometry: one triangle in the XY plane -----------------------
    cyc_mesh_t* mesh;
    check(cyc_mesh_create(scene, &mesh), "mesh_create");
    static immutable float[9] verts = [
         0.0f,  0.8f, 0.0f,
        -0.8f, -0.6f, 0.0f,
         0.8f, -0.6f, 0.0f,
    ];
    static immutable int[3] tri = [0, 1, 2];
    check(cyc_mesh_set_verts(mesh, verts.ptr, 3), "mesh_set_verts");
    check(cyc_mesh_set_triangles(mesh, tri.ptr, 1, null), "mesh_set_triangles");

    cyc_object_t* obj;
    check(cyc_object_create(scene, &obj), "object_create");
    check(cyc_object_set_mesh(obj, mesh), "object_set_mesh");

    cyc_shader_t* shader;
    check(cyc_shader_create_principled(scene, &shader), "shader_create_principled");
    check(cyc_shader_set_principled_base_color(shader, 0.85f, 0.25f, 0.15f),
          "shader_base_color");
    check(cyc_shader_set_principled_roughness(shader, 0.6f), "shader_roughness");
    check(cyc_object_set_shader(obj, shader), "object_set_shader");

    // 4. Point light, off-center to give visible shading --------------
    cyc_light_t* light;
    check(cyc_light_create(scene, cyc_light_type.POINT, &light), "light_create");
    static immutable float[16] lightXform = [
        1, 0, 0, 0,
        0, 1, 0, 2,
        0, 0, 1, 2,
        0, 0, 0, 1,
    ];
    check(cyc_light_set_transform(light, lightXform.ptr), "light_xform");
    check(cyc_light_set_color(light, 1.0f, 1.0f, 1.0f), "light_color");
    check(cyc_light_set_intensity(light, 100.0f), "light_intensity");

    // 5. Camera looking at origin from +Z -----------------------------
    cyc_camera_t* cam;
    check(cyc_camera_create_perspective(scene, &cam), "camera_create");
    check(cyc_camera_lookat(cam,
                            0.0f, 0.0f, 3.0f,    // eye
                            0.0f, 0.0f, 0.0f,    // target
                            0.0f, 1.0f, 0.0f),   // up
          "camera_lookat");
    check(cyc_camera_set_fov(cam, 0.785398f /* 45 deg */), "camera_fov");
    check(cyc_scene_set_active_camera(scene, cam), "set_active_camera");

    // 6. Render -------------------------------------------------------
    check(cyc_session_reset(session, 512, 512), "session_reset");
    writefln("Rendering 512x512 @ 64 samples on %s...", sp.device_type);
    import std.datetime.stopwatch : StopWatch, AutoStart;
    auto sw = StopWatch(AutoStart.yes);
    check(cyc_session_start(session), "session_start");
    check(cyc_session_wait(session), "session_wait");
    sw.stop();
    writefln("Render finished in %s ms", sw.peek.total!"msecs");

    const char* out_path = "triangle.png".toStringz;
    check(cyc_session_save_image(session, out_path), "session_save_image");
    writeln("Wrote triangle.png");
    return 0;
}
