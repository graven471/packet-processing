#include "vk_init.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

struct vk_context *vk_context_init(const char *const *const *instance_extensions)
{
	uint32_t required_api_version = VK_VERSION_1_3;

	VkResult result = vkEnumerateInstanceVersion(&required_api_version);

	if (result != VK_SUCCESS) {
		fprintf(stderr, "vulkan 1.3 not found atleast vulkan 1.3 is required to run this application\n");
		return nullptr;
	}

	VkApplicationInfo app_info = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
				       .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
				       .pApplicationName = "packet-processor",
				       .engineVersion = VK_MAKE_VERSION(0, 0, 0),
				       .pEngineName = "NoEngine",
				       .apiVersion = required_api_version };

	VkInstanceCreateInfo inst_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
		.enabledExtensionCount = 0,
		.ppEnabledExtensionNames = nullptr,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = nullptr,
	};

	struct vk_context *self = malloc(sizeof(struct vk_context));

	if (self == nullptr) {
		fprintf(stderr, "allocation failed %s\n", strerror(-errno));
		return nullptr;
	}

	result = vkCreateInstance(&inst_info, nullptr, &self->instance);

	if (result != VK_SUCCESS) {
		fprintf(stderr, "instance creation failed in %s func: %s at %d\n", __FILE__, __FUNCTION__, __LINE__);
		goto error;
	}

	return self;

error:
	free(self);
	return nullptr;
}
