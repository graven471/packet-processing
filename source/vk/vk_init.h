#pragma once

#include <vulkan/vulkan_core.h>

struct vk_context {
	VkInstance instance;
	VkPhysicalDevice gpu;
	VkDevice device;
};

struct vk_context *vk_context_init(const char *const *const *instance_extensions);
void vk_context_destroy(struct vk_context *_Nonnull context);
