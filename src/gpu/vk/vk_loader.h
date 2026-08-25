/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef VK_LOADER_H
#define VK_LOADER_H

/* Vulkan, resolved at runtime instead of linked.
 *
 * The binary must start on a machine with no Vulkan ICD at all — there the
 * Vulkan backend is simply absent from the picker, with the reason written out,
 * rather than the process failing to load. That rules out `-lvulkan`, so
 * VK_NO_PROTOTYPES is set and every entry point is a pointer resolved through
 * vkGetInstanceProcAddr, which SDL hands over after SDL_Vulkan_LoadLibrary().
 * SDL is already a hard dependency and already knows how to find the loader on
 * each platform, so there is no reason to carry volk as well.
 *
 * The list of entry points is vk_entry_points.inl and nowhere else.
 */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdbool.h>

/* vk_imgui.cpp is the one C++ consumer of this header, and it links against the
 * C definitions in vk_loader.c — without this the mangled names never resolve. */
#ifdef __cplusplus
extern "C" {
#endif

#define VK_GLOBAL(name)   extern PFN_##name name;
#define VK_INSTANCE(name) extern PFN_##name name;
#define VK_DEVICE(name)   extern PFN_##name name;
#include "vk_entry_points.inl"

/** @brief Loads the Vulkan library through SDL and resolves the global tier.
 *  @return false if no loader or no ICD is present — a normal outcome, not an error. */
bool vk_loader_init(void);

/** @brief Resolves the instance tier. Call once the VkInstance exists. */
bool vk_loader_load_instance(VkInstance instance);

/** @brief Resolves the device tier, bypassing the loader's dispatch trampoline. */
bool vk_loader_load_device(VkDevice device);

/** @brief Releases the library. No Vulkan object may be alive. */
void vk_loader_shutdown(void);

/** @brief Why the Vulkan backend is unavailable, or NULL if it is available. */
const char* vk_loader_failure(void);

/** @brief The loader's vkGetInstanceProcAddr, for anything that resolves its own
 *  entry points — ImGui's Vulkan backend does, since the process links no libvulkan. */
PFN_vkGetInstanceProcAddr vk_loader_get_instance_proc_addr(void);

/** @brief VkResult as a name, for logs. Unknown codes print as a number. */
const char* vk_result_str(VkResult r);

#ifdef __cplusplus
}
#endif

#endif /* VK_LOADER_H */
