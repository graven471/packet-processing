#pragma once

#include <lib/volk.h>
#include <vulkan/vulkan_core.h>

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
	VkQueue graphics_queue;
	VkQueue decode_queue;
	VkVideoSessionKHR video_session;
	bool shared;
};

struct vk_context *vk_context_init();
void vk_context_destroy(struct vk_context *context);

#ifdef __cplusplus
}
#endif
