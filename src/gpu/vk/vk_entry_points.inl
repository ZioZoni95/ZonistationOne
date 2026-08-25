/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 */

/* Every Vulkan entry point this emulator calls, in three tiers.
 *
 * The list is the single source of truth: vk_loader.h declares a pointer per
 * line, vk_loader.c defines and resolves one per line. Adding a call means
 * adding a line here and nowhere else.
 *
 * Tiers matter. A global function is resolved from a NULL instance, an instance
 * function from the VkInstance, and a device function from the VkDevice —
 * resolving a device function through the instance works but goes through the
 * loader's dispatch trampoline on every call, which is exactly the overhead
 * Vulkan is being used to avoid.
 */

#ifndef VK_GLOBAL
#define VK_GLOBAL(name)
#endif
#ifndef VK_INSTANCE
#define VK_INSTANCE(name)
#endif
#ifndef VK_DEVICE
#define VK_DEVICE(name)
#endif

VK_GLOBAL(vkCreateInstance)
VK_GLOBAL(vkEnumerateInstanceExtensionProperties)
VK_GLOBAL(vkEnumerateInstanceLayerProperties)
VK_GLOBAL(vkEnumerateInstanceVersion)

VK_INSTANCE(vkDestroyInstance)
VK_INSTANCE(vkEnumeratePhysicalDevices)
VK_INSTANCE(vkGetPhysicalDeviceProperties)
VK_INSTANCE(vkGetPhysicalDeviceProperties2)
VK_INSTANCE(vkGetPhysicalDeviceFeatures2)
VK_INSTANCE(vkGetPhysicalDeviceMemoryProperties)
VK_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties)
VK_INSTANCE(vkGetPhysicalDeviceFormatProperties)
VK_INSTANCE(vkEnumerateDeviceExtensionProperties)
VK_INSTANCE(vkCreateDevice)
VK_INSTANCE(vkGetDeviceProcAddr)
VK_INSTANCE(vkDestroySurfaceKHR)
VK_INSTANCE(vkGetPhysicalDeviceSurfaceSupportKHR)
VK_INSTANCE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
VK_INSTANCE(vkGetPhysicalDeviceSurfaceFormatsKHR)
VK_INSTANCE(vkGetPhysicalDeviceSurfacePresentModesKHR)

VK_DEVICE(vkDestroyDevice)
VK_DEVICE(vkGetDeviceQueue)
VK_DEVICE(vkDeviceWaitIdle)
VK_DEVICE(vkQueueSubmit)
VK_DEVICE(vkQueueWaitIdle)
VK_DEVICE(vkQueuePresentKHR)

VK_DEVICE(vkCreateSwapchainKHR)
VK_DEVICE(vkDestroySwapchainKHR)
VK_DEVICE(vkGetSwapchainImagesKHR)
VK_DEVICE(vkAcquireNextImageKHR)

VK_DEVICE(vkCreateCommandPool)
VK_DEVICE(vkDestroyCommandPool)
VK_DEVICE(vkResetCommandPool)
VK_DEVICE(vkAllocateCommandBuffers)
VK_DEVICE(vkFreeCommandBuffers)
VK_DEVICE(vkBeginCommandBuffer)
VK_DEVICE(vkEndCommandBuffer)
VK_DEVICE(vkResetCommandBuffer)

VK_DEVICE(vkCreateFence)
VK_DEVICE(vkDestroyFence)
VK_DEVICE(vkWaitForFences)
VK_DEVICE(vkResetFences)
VK_DEVICE(vkCreateSemaphore)
VK_DEVICE(vkDestroySemaphore)

VK_DEVICE(vkCreateImage)
VK_DEVICE(vkDestroyImage)
VK_DEVICE(vkCreateImageView)
VK_DEVICE(vkDestroyImageView)
VK_DEVICE(vkGetImageMemoryRequirements)
VK_DEVICE(vkBindImageMemory)

VK_DEVICE(vkCreateBuffer)
VK_DEVICE(vkDestroyBuffer)
VK_DEVICE(vkGetBufferMemoryRequirements)
VK_DEVICE(vkBindBufferMemory)
VK_DEVICE(vkAllocateMemory)
VK_DEVICE(vkFreeMemory)
VK_DEVICE(vkMapMemory)
VK_DEVICE(vkUnmapMemory)
VK_DEVICE(vkFlushMappedMemoryRanges)
VK_DEVICE(vkInvalidateMappedMemoryRanges)

VK_DEVICE(vkCreateSampler)
VK_DEVICE(vkDestroySampler)
VK_DEVICE(vkCreateShaderModule)
VK_DEVICE(vkDestroyShaderModule)

VK_DEVICE(vkCreateDescriptorSetLayout)
VK_DEVICE(vkDestroyDescriptorSetLayout)
VK_DEVICE(vkCreateDescriptorPool)
VK_DEVICE(vkDestroyDescriptorPool)
VK_DEVICE(vkAllocateDescriptorSets)
VK_DEVICE(vkFreeDescriptorSets)
VK_DEVICE(vkUpdateDescriptorSets)

VK_DEVICE(vkCreatePipelineLayout)
VK_DEVICE(vkDestroyPipelineLayout)
VK_DEVICE(vkCreateGraphicsPipelines)
VK_DEVICE(vkDestroyPipeline)
VK_DEVICE(vkCreatePipelineCache)
VK_DEVICE(vkDestroyPipelineCache)

VK_DEVICE(vkCmdBeginRendering)
VK_DEVICE(vkCmdEndRendering)
VK_DEVICE(vkCmdBindPipeline)
VK_DEVICE(vkCmdBindDescriptorSets)
VK_DEVICE(vkCmdBindVertexBuffers)
VK_DEVICE(vkCmdSetViewport)
VK_DEVICE(vkCmdSetScissor)
VK_DEVICE(vkCmdSetBlendConstants)
VK_DEVICE(vkCmdDraw)
VK_DEVICE(vkCmdPushConstants)
VK_DEVICE(vkCmdPipelineBarrier)
VK_DEVICE(vkCmdCopyBufferToImage)
VK_DEVICE(vkCmdCopyImageToBuffer)
VK_DEVICE(vkCmdBlitImage)
VK_DEVICE(vkCmdClearColorImage)

#undef VK_GLOBAL
#undef VK_INSTANCE
#undef VK_DEVICE
