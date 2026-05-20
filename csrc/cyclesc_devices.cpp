/*
 * cyclesc_devices.cpp — enumerate Cycles devices.
 */

#include "cyclesc_internal.h"

#include "device/device.h"

#include <cstring>

using cyc_internal::ensure_global_init;

static cyc_device_type map_device_type(ccl::DeviceType t)
{
    switch (t) {
        case ccl::DEVICE_CPU:    return CYC_DEVICE_CPU;
        case ccl::DEVICE_CUDA:   return CYC_DEVICE_CUDA;
        case ccl::DEVICE_OPTIX:  return CYC_DEVICE_OPTIX;
        case ccl::DEVICE_HIP:    return CYC_DEVICE_HIP;
        case ccl::DEVICE_METAL:  return CYC_DEVICE_METAL;
        case ccl::DEVICE_ONEAPI: return CYC_DEVICE_ONEAPI;
        default:                 return CYC_DEVICE_CPU;
    }
}

extern "C"
cyc_status cyc_devices_query(cyc_device_info *out_devices, int max_devices, int *out_count)
{
    if (!out_count) return CYC_ERR_INVALID_ARGUMENT;
    if (max_devices < 0 || (max_devices > 0 && !out_devices)) {
        return CYC_ERR_INVALID_ARGUMENT;
    }

    ensure_global_init();
    const ccl::vector<ccl::DeviceInfo> devices = ccl::Device::available_devices();

    int n = 0;
    for (const ccl::DeviceInfo &info : devices) {
        if (n >= max_devices) break;
        cyc_device_info &dst = out_devices[n];
        dst.type  = map_device_type(info.type);
        dst.index = info.num;
        std::strncpy(dst.name, info.description.c_str(), sizeof(dst.name) - 1);
        dst.name[sizeof(dst.name) - 1] = '\0';
        dst.supports_hw_rt = info.use_hardware_raytracing ? 1 : 0;
        ++n;
    }
    *out_count = static_cast<int>(devices.size());
    return CYC_OK;
}
