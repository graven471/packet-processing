#pragma once

#include <lib/volk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_CHECK(x)                                         \
	do {                                                \
		VkResult err_ = (x);                        \
		if (err_ != VK_SUCCESS) {                   \
			LOG_FATAL("vulkan error %d", err_); \
		}                                           \
	} while (0)

struct vk_context {
	VkInstance instance;
	VkPhysicalDevice gpu;
	VkDevice device;
};

struct vk_context *vk_context_init(const char *const *instance_extensions);
void vk_context_destroy(struct vk_context *context);

#ifdef __cplusplus
}
#endif
