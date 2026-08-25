/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef VK_DEVICE_H
#define VK_DEVICE_H

#include "vk_loader.h"
#include "gpu_backend.h"
#include <SDL3/SDL.h>

#define VK_MAX_SWAP_IMAGES 8
#define VK_FRAMES_IN_FLIGHT 2

/* What the GPU can do, queried once and then branched on at pipeline build.
 *
 * These are the three that decide how the PS1's blending is emulated, and they
 * are read from the device rather than assumed: the same binary runs on the
 * discrete card and on the iGPU, and "works on NVIDIA" has already cost this
 * project one round of GL artifacts. On this machine all three devices report
 * pixel interlock and dual-source blend, but a machine is not the world. */
typedef struct {
    bool pixel_interlock;    /* VK_EXT_fragment_shader_interlock: the clean answer to
                              * sampling the colour target a draw is writing, which is
                              * what GL needed glTextureBarrier for */
    bool dual_src_blend;     /* collapses the two-pass semi-transparent textured draw */
    bool raster_order_access;/* VK_EXT_rasterization_order_attachment_access */
} VkCaps;

typedef struct {
    VkInstance       instance;
    VkSurfaceKHR     surface;
    VkPhysicalDevice phys;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queue_family;
    uint32_t         api_version;
    VkPhysicalDeviceMemoryProperties mem_props;
    char             device_name[256];
    char             driver_name[256];  /* VK_MAX_DRIVER_NAME_SIZE */
    VkCaps           caps;
    bool             validation;

    /* Swapchain */
    VkSwapchainKHR   swapchain;
    VkFormat         swap_format;
    VkColorSpaceKHR  swap_colorspace;
    VkExtent2D       swap_extent;
    uint32_t         swap_image_count;
    VkImage          swap_images[VK_MAX_SWAP_IMAGES];
    VkImageView      swap_views[VK_MAX_SWAP_IMAGES];
    VkPresentModeKHR present_mode;
} VkContext;

/** @brief Names every Vulkan GPU that can present to a window, without keeping a
 *  device alive. Creates and tears down a throwaway instance, so it is safe to
 *  call before anything else and from the UI. */
int  vk_device_enumerate(GfxDeviceInfo* out, int max);

/** @brief Instance, surface, physical device, logical device, queue, swapchain.
 *  @param device_index index into vk_device_enumerate()'s list, or -1 to choose. */
bool vk_context_create(VkContext* c, SDL_Window* window, int device_index);
void vk_context_destroy(VkContext* c);

/** @brief Rebuilds the swapchain against the window's current size. */
bool vk_swapchain_create(VkContext* c, SDL_Window* window);
void vk_swapchain_destroy(VkContext* c);

/** @brief Index of a memory type satisfying `type_bits` and `props`, or UINT32_MAX. */
uint32_t vk_find_memory_type(const VkContext* c, uint32_t type_bits, VkMemoryPropertyFlags props);

#endif /* VK_DEVICE_H */
