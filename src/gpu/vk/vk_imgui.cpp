/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */

#include "vk_imgui.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

extern "C" {
#include "log.h"
}

/* ImGui resolves its own Vulkan entry points through this, because the process
 * links no libvulkan — everything goes through the loader SDL handed us. */
static PFN_vkVoidFunction imgui_loader(const char* name, void* user_data) {
    VkInstance inst = (VkInstance)user_data;
    PFN_vkGetInstanceProcAddr gipa = vk_loader_get_instance_proc_addr();
    return gipa ? gipa(inst, name) : nullptr;
}

static void check_vk(VkResult err) {
    if (err != VK_SUCCESS)
        LOG_RENDERER_ERROR("[VK] ImGui backend: %s", vk_result_str(err));
}

bool vk_imgui_init(VkInstance instance, VkPhysicalDevice phys, VkDevice device,
                   uint32_t queue_family, VkQueue queue, VkDescriptorPool pool,
                   uint32_t min_image_count, uint32_t image_count,
                   VkFormat colour_format, uint32_t api_version) {
    if (!ImGui_ImplVulkan_LoadFunctions(api_version, imgui_loader, (void*)instance)) {
        LOG_RENDERER_ERROR("[VK] ImGui_ImplVulkan_LoadFunctions failed");
        return false;
    }

    static VkFormat s_colour_format;          /* the init info keeps a pointer to this */
    s_colour_format = colour_format;

    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion     = api_version;
    info.Instance       = instance;
    info.PhysicalDevice = phys;
    info.Device         = device;
    info.QueueFamily    = queue_family;
    info.Queue          = queue;
    info.DescriptorPool = pool;
    info.MinImageCount  = min_image_count;
    info.ImageCount     = image_count;
    /* Dynamic rendering, matching the rest of the backend: no VkRenderPass
     * exists anywhere in this emulator, so ImGui must not expect one. */
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &s_colour_format;
    info.CheckVkResultFn = check_vk;

    if (!ImGui_ImplVulkan_Init(&info)) {
        LOG_RENDERER_ERROR("[VK] ImGui_ImplVulkan_Init failed");
        return false;
    }
    LOG_RENDERER_INFO("[VK] ImGui Vulkan backend up (%u swapchain images)", image_count);
    return true;
}

void vk_imgui_shutdown(void) { ImGui_ImplVulkan_Shutdown(); }
void vk_imgui_new_frame(void) { ImGui_ImplVulkan_NewFrame(); }

void vk_imgui_render(void* imgui_draw_data, VkCommandBuffer cmd) {
    if (!imgui_draw_data) return;
    ImGui_ImplVulkan_RenderDrawData((ImDrawData*)imgui_draw_data, cmd);
}

VkDescriptorSet vk_imgui_add_texture(VkSampler s, VkImageView v, VkImageLayout layout) {
    return ImGui_ImplVulkan_AddTexture(s, v, layout);
}
