/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */

#include "vk_loader.h"
#include "log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>

#define VK_GLOBAL(name)   PFN_##name name = NULL;
#define VK_INSTANCE(name) PFN_##name name = NULL;
#define VK_DEVICE(name)   PFN_##name name = NULL;
#include "vk_entry_points.inl"

static PFN_vkGetInstanceProcAddr s_get_instance_proc = NULL;
static const char*               s_failure = "not initialised";

const char* vk_loader_failure(void) { return s_failure; }

bool vk_loader_init(void) {
    if (s_get_instance_proc) { s_failure = NULL; return true; }

    if (!SDL_Vulkan_LoadLibrary(NULL)) {
        s_failure = "no Vulkan loader on this system";
        LOG_RENDERER_INFO("[VK] %s (%s)", s_failure, SDL_GetError());
        return false;
    }
    s_get_instance_proc = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (!s_get_instance_proc) {
        s_failure = "loader gave no vkGetInstanceProcAddr";
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    /* The global tier resolves from a NULL instance. Every one of these is
     * mandatory in any conformant loader, so a miss here means something is
     * badly wrong rather than merely old. */
    bool ok = true;
#define VK_GLOBAL(name) \
    name = (PFN_##name)s_get_instance_proc(NULL, #name); \
    if (!name) { LOG_RENDERER_ERROR("[VK] missing global entry point %s", #name); ok = false; }
#include "vk_entry_points.inl"

    if (!ok) {
        s_failure = "loader is missing mandatory global entry points";
        SDL_Vulkan_UnloadLibrary();
        s_get_instance_proc = NULL;
        return false;
    }

    uint32_t ver = 0;
    if (vkEnumerateInstanceVersion && vkEnumerateInstanceVersion(&ver) == VK_SUCCESS) {
        LOG_RENDERER_INFO("[VK] loader up, instance API %u.%u.%u",
                          VK_API_VERSION_MAJOR(ver), VK_API_VERSION_MINOR(ver),
                          VK_API_VERSION_PATCH(ver));
        if (VK_API_VERSION_MAJOR(ver) == 1 && VK_API_VERSION_MINOR(ver) < 3) {
            s_failure = "loader reports below Vulkan 1.3";
            SDL_Vulkan_UnloadLibrary();
            s_get_instance_proc = NULL;
            return false;
        }
    }
    s_failure = NULL;
    return true;
}

bool vk_loader_load_instance(VkInstance instance) {
    bool ok = true;
#define VK_INSTANCE(name) \
    name = (PFN_##name)s_get_instance_proc(instance, #name); \
    if (!name) { LOG_RENDERER_ERROR("[VK] missing instance entry point %s", #name); ok = false; }
#include "vk_entry_points.inl"
    return ok;
}

bool vk_loader_load_device(VkDevice device) {
    /* Through vkGetDeviceProcAddr, not the instance one: an instance-resolved
     * device function is a trampoline through the loader on every call. */
    bool ok = true;
#define VK_DEVICE(name) \
    name = (PFN_##name)vkGetDeviceProcAddr(device, #name); \
    if (!name) { LOG_RENDERER_ERROR("[VK] missing device entry point %s", #name); ok = false; }
#include "vk_entry_points.inl"
    return ok;
}

void vk_loader_shutdown(void) {
    if (!s_get_instance_proc) return;
#define VK_GLOBAL(name)   name = NULL;
#define VK_INSTANCE(name) name = NULL;
#define VK_DEVICE(name)   name = NULL;
#include "vk_entry_points.inl"
    s_get_instance_proc = NULL;
    s_failure = "shut down";
    SDL_Vulkan_UnloadLibrary();
}

PFN_vkGetInstanceProcAddr vk_loader_get_instance_proc_addr(void) {
    return s_get_instance_proc;
}

const char* vk_result_str(VkResult r) {
    switch (r) {
        case VK_SUCCESS:                       return "VK_SUCCESS";
        case VK_NOT_READY:                     return "VK_NOT_READY";
        case VK_TIMEOUT:                       return "VK_TIMEOUT";
        case VK_INCOMPLETE:                    return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR:                return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY:      return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:    return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:   return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:             return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:       return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:       return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:   return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:     return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:     return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:    return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR:        return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:         return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "VkResult %d", (int)r);
            return buf;
        }
    }
}
