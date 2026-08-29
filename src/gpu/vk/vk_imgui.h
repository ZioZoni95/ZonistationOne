/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef VK_IMGUI_H
#define VK_IMGUI_H

/* C entry points onto ImGui's Vulkan backend.
 *
 * Kept in its own translation unit rather than in debug_ui.cpp: the UI code has
 * no business knowing which graphics API is live, and this is the only place
 * where ImGui and Vulkan have to meet. It mirrors the pair of extern "C" hooks
 * the GL path already uses (imgui_opengl_new_frame / imgui_render_draw_data). */

#include "vk_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

bool vk_imgui_init(VkInstance instance, VkPhysicalDevice phys, VkDevice device,
                   uint32_t queue_family, VkQueue queue, VkDescriptorPool pool,
                   uint32_t min_image_count, uint32_t image_count,
                   VkFormat colour_format, uint32_t api_version);
void vk_imgui_shutdown(void);
void vk_imgui_new_frame(void);
void vk_imgui_render(void* imgui_draw_data, VkCommandBuffer cmd);

/** @brief A descriptor set ImGui can draw a texture with — this is what becomes
 *  the ImTextureID the UI hands to ImGui::Image(). */
VkDescriptorSet vk_imgui_add_texture(VkSampler s, VkImageView v, VkImageLayout layout);

#ifdef __cplusplus
}
#endif

#endif /* VK_IMGUI_H */
