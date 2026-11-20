#include "vk_init.h"
#include "sz/log.h"
#include <errno.h>
#include <lib/volk.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

struct gpu_candidate {
	VkPhysicalDevice device;
	VkPhysicalDeviceProperties props;
	uint32_t score;
	uint32_t decode_queue_idx;
	uint32_t graphics_queue_idx;
	bool b_has_dedicated_decode_queue;
	bool b_has_swapchain;
	bool b_has_surface_support;
	bool b_has_graphics_support;
};

#define MAX(a, b)                   \
	({                          \
		typeof(a) _a = (a); \
		typeof(b) _b = (b); \
		_a > _b ? _a : _b;  \
	})

/*
 * i have n gpu(s) and i am looping over each of them ok and finding its props extensions etc
 * now what i have to do is that prefer dedicated gpu and device which has more features
 * let gpu = gpus[idx], where idx = number of supported vulkan gpu(s) in machine
 * now check this gpu supported features, type and extensions increment score if features i need is present
 * set candiates = gpu,
 * then i have two scores, for examples let's say gpu[0].score = 500, and gpu[1].score = 800
 * now i select one of the GPU based on the highest so ofcourse it will be gpu[1]
 */

[[clang::internal_linkage]] [[gnu::nonnull]] static struct gpu_candidate select_preferred_gpu(VkPhysicalDevice *gpus,
											      uint32_t gpu_count)
{
	struct gpu_candidate candiates[gpu_count];
	memset(candiates, 0, sizeof(candiates));

	uint32_t best_score = 0;
	uint32_t best_idx = UINT32_MAX;
	constexpr uint32_t discrete_gpu_score = 200;

	for (uint32_t idx = 0; idx < gpu_count; ++idx) {
		uint32_t ext_count = 0;
		VkPhysicalDevice gpu = gpus[idx];
		VkPhysicalDeviceProperties props = { 0 };
		vkGetPhysicalDeviceProperties(gpu, &props);
		candiates[idx].score = 0;
		candiates[idx].decode_queue_idx = UINT32_MAX;
		candiates[idx].graphics_queue_idx = UINT32_MAX;

		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			candiates[idx].score += discrete_gpu_score;

		VK_CHECK(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr));

		if (ext_count == 0)
			LOG_FATAL("no device extensions found");

		VkExtensionProperties extension_props[ext_count];

		VK_CHECK(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, extension_props));

		VkVideoDecodeH264CapabilitiesKHR h264_caps = {
			.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR,
		};

		VkVideoDecodeCapabilitiesKHR decode_caps = {
			.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR,
			.pNext = &h264_caps,
		};

		VkVideoCapabilitiesKHR capabilities = {
			.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
			.pNext = &decode_caps,
		};

		VkVideoDecodeH264ProfileInfoKHR h264_profile = {
			.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR,
			.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_MAIN,
		};

		VkVideoProfileInfoKHR profile = {
			.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
			.pNext = &h264_profile,
			.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
			.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
			.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
			.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
		};

		VkResult result = vkGetPhysicalDeviceVideoCapabilitiesKHR(gpu, &profile, &capabilities);

		if (result == VK_SUCCESS)
			candiates[idx].score += 300;

		for (uint32_t i = 0; i < ext_count; ++i) {
			if (strcmp(extension_props[i].extensionName, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) ||
			    strcmp(extension_props[i].extensionName, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME) ||
			    strcmp(extension_props[i].extensionName, VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME) == 0)
				candiates[idx].score += 400;

			if (strcmp(extension_props[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
				candiates[idx].score += 100;
				candiates[idx].b_has_swapchain = true;
			}

			if (strcmp(extension_props[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0) {
				candiates[idx].score += 100;
				candiates[idx].b_has_surface_support = true;
			}
		}

		uint32_t queue_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, nullptr);

		if (queue_count == 0)
			continue;

		VkQueueFamilyProperties queue_props[queue_count];

		vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, queue_props);

		for (uint32_t q = 0; q < queue_count; ++q) {
			if ((queue_props[q].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) &&
			    !(queue_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				candiates[idx].score += 200;
				candiates[idx].decode_queue_idx = q;
				candiates[idx].b_has_dedicated_decode_queue = true;
			}

			if (queue_props[q].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {
				candiates[idx].score += 100;
				candiates[idx].decode_queue_idx = q;
				candiates[idx].b_has_dedicated_decode_queue = false;
			}

			if (!(queue_props[q].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) &&
			    (queue_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				candiates[idx].score += 200;
				candiates[idx].graphics_queue_idx = q;
				candiates[idx].b_has_graphics_support = true;
			}

			if (queue_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				candiates[idx].score += 100;
				candiates[idx].graphics_queue_idx = q;
				candiates[idx].b_has_graphics_support = false;
			}
		}

		if (candiates[idx].score > best_score && candiates[idx].decode_queue_idx != UINT32_MAX) {
			candiates[idx].props = props;
			candiates[idx].device = gpu;
			best_score = candiates[idx].score;
			best_idx = idx;
		}
	}

	if (best_idx == UINT32_MAX || best_score == 0)
		LOG_FATAL("no valid GPU found features that this app needed is not available in any of GPUs");

	struct gpu_candidate selected = candiates[best_idx];

	return selected;
}

struct vk_context *vk_context_init(const char *const *instance_extensions)
{
	VK_CHECK(volkInitialize());

	uint32_t required_api_version = VK_VERSION_1_3;

	VkResult result = vkEnumerateInstanceVersion(&required_api_version);

	if (result != VK_SUCCESS)
		LOG_FATAL("vulkan 1.3 not found atleast vulkan 1.3 is required to run this application");

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

	if (self == nullptr)
		LOG_FATAL("allocation failed %s", strerror(-errno));

	VK_CHECK(vkCreateInstance(&inst_info, nullptr, &self->instance));

	VkInstance instance = self->instance;
	volkLoadInstance(instance);

	uint32_t physical_device_count = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));

	if (physical_device_count == 0)
		LOG_FATAL("no supported vulkan GPUs found terminating");

	VkPhysicalDevice physical_devices[physical_device_count];
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices));

	struct gpu_candidate selected_gpu = select_preferred_gpu(physical_devices, physical_device_count);
	printf("selected GPU = (%s)\n", selected_gpu.props.deviceName);

	return self;
}
