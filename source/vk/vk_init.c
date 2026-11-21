#include "vk_init.h"
#include "sz/log.h"
#include <errno.h>
#include <lib/volk.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vk_platform.h>
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

#define ARRAY_SIZE(arr)                                                                              \
	(sizeof(struct { int : -!!__builtin_types_compatible_p(typeof(arr), typeof(&(arr)[0])); }) + \
	 sizeof(arr) / sizeof((arr)[0]))

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
			.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH,
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
			if (strcmp(extension_props[i].extensionName, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) == 0 ||
			    strcmp(extension_props[i].extensionName, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME) == 0 ||
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

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_utils_callback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity,
		     [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT msg_type,
		     const VkDebugUtilsMessengerCallbackDataEXT *callback_data, [[maybe_unused]] void *user_data)
{
	printf("[VK] %s\n", callback_data->pMessage);
	return false;
}

[[clang::internal_linkage]] static inline VkDebugUtilsMessengerEXT setup_debug_messenger(VkInstance instance)
{
	VkDebugUtilsMessengerCreateInfoEXT create_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				   VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
			       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
		.pfnUserCallback = debug_utils_callback,
	};

	VkDebugUtilsMessengerEXT messenger = {};
	VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &create_info, nullptr, &messenger));
	return messenger;
}

struct vk_context *vk_context_init()
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

	const char *instance_extensions[] = {
		"VK_KHR_surface",
		"VK_KHR_wayland_surface",
#ifdef DEBUG_BUILD
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
	};

	const char *layer_names = "VK_LAYER_KHRONOS_validation";

	VkInstanceCreateInfo inst_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
		.enabledExtensionCount = (uint32_t)ARRAY_SIZE(instance_extensions),
		.ppEnabledExtensionNames = instance_extensions,
		.ppEnabledLayerNames = nullptr,
		.enabledLayerCount = 0,
	};

#ifdef DEBUG_BUILD
	inst_info.ppEnabledLayerNames = &layer_names;
	inst_info.enabledLayerCount = 1;
#else
	inst_info.ppEnabledLayerNames = NULL;
	inst_info.enabledLayerCount = 0;
#endif

	struct vk_context *self = malloc(sizeof(struct vk_context));

	if (self == nullptr)
		LOG_FATAL("allocation failed %s", strerror(-errno));

	VK_CHECK(vkCreateInstance(&inst_info, nullptr, &self->instance));

	VkInstance instance = self->instance;
	volkLoadInstance(instance);

#ifdef DEBUG_BUILD
	self->dbg_messenger = setup_debug_messenger(instance);
#elif
	self->dbg_messenger = VK_NULL_HANDLE;
#endif

	uint32_t physical_device_count = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));

	if (physical_device_count == 0)
		LOG_FATAL("no supported vulkan GPUs found terminating");

	VkPhysicalDevice physical_devices[physical_device_count];
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices));

	struct gpu_candidate selected_gpu = select_preferred_gpu(physical_devices, physical_device_count);
	printf("selected GPU = (%s)\n", selected_gpu.props.deviceName);
	self->gpu = selected_gpu.device;

	const float queue_priorities = 1.0f;

	struct VkDeviceQueueCreateInfo queue_infos[2];
	uint32_t queue_count = 0;

	queue_infos[queue_count++] = (VkDeviceQueueCreateInfo){ .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
								.queueCount = 1,
								.queueFamilyIndex = selected_gpu.decode_queue_idx,
								.pQueuePriorities = &queue_priorities };

	if (selected_gpu.b_has_graphics_support && selected_gpu.decode_queue_idx != selected_gpu.graphics_queue_idx)
		queue_infos[queue_count++] =
			(VkDeviceQueueCreateInfo){ .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
						   .queueCount = 1,
						   .queueFamilyIndex = selected_gpu.graphics_queue_idx,
						   .pQueuePriorities = &queue_priorities };

	const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME,
					    VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
					    VK_KHR_VIDEO_QUEUE_EXTENSION_NAME };

	VkPhysicalDeviceVulkan13Features features13 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.dynamicRendering = VK_TRUE,
		.synchronization2 = VK_TRUE,
	};

	VkPhysicalDeviceFeatures device_features = {
		.samplerAnisotropy = VK_TRUE,
	};

	VkDeviceCreateInfo device_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = queue_count,
		.pQueueCreateInfos = queue_infos,
		.enabledExtensionCount = (uint32_t)ARRAY_SIZE(device_extensions),
		.ppEnabledExtensionNames = device_extensions,
		.pEnabledFeatures = &device_features,
		.pNext = &features13,
	};

	VK_CHECK(vkCreateDevice(selected_gpu.device, &device_info, nullptr, &self->device));

	volkLoadDevice(self->device);

	vkGetDeviceQueue(self->device, selected_gpu.decode_queue_idx, 0, &self->decode_queue);

	if (selected_gpu.b_has_graphics_support && selected_gpu.decode_queue_idx != selected_gpu.graphics_queue_idx) {
		vkGetDeviceQueue(self->device, selected_gpu.graphics_queue_idx, 0, &self->graphics_queue);
		self->shared = false;
	} else {
		self->graphics_queue = self->decode_queue;
		self->shared = true;
	}

	// ======================== creating session for H264 =====================
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
		.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH,
	};

	VkVideoProfileInfoKHR profile = {
		.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
		.pNext = &h264_profile,
		.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
		.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
		.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
		.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
	};

	VK_CHECK(vkGetPhysicalDeviceVideoCapabilitiesKHR(self->gpu, &profile, &capabilities));

	VkVideoSessionCreateInfoKHR session_create_info = {
		.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR,
		.maxCodedExtent = (VkExtent2D){ .width = 1920, .height = 1080 },
		.maxDpbSlots = capabilities.maxDpbSlots,
		.maxActiveReferencePictures = capabilities.maxActiveReferencePictures,
		.queueFamilyIndex = selected_gpu.decode_queue_idx,
		/* image format of the decoded output */
		.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
		.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
		.pStdHeaderVersion = &capabilities.stdHeaderVersion,
		.pVideoProfile = &profile,
	};

	VK_CHECK(vkCreateVideoSessionKHR(self->device, &session_create_info, nullptr, &self->video_session));

	uint32_t video_session_mem_req_count = 0;
	VK_CHECK(vkGetVideoSessionMemoryRequirementsKHR(self->device, self->video_session, &video_session_mem_req_count,
							nullptr));

	VkVideoSessionMemoryRequirementsKHR mem_requirements[video_session_mem_req_count];
	memset(mem_requirements, 0, sizeof(mem_requirements));

	for (uint32_t i = 0; i < video_session_mem_req_count; ++i)
		mem_requirements[i].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;

	VK_CHECK(vkGetVideoSessionMemoryRequirementsKHR(self->device, self->video_session, &video_session_mem_req_count,
							mem_requirements));

	VkBindVideoSessionMemoryInfoKHR decode_session_bind_memory[MAX_BOUND_MEMORY];
	for (uint32_t mem_idx = 0; mem_idx < video_session_mem_req_count; ++mem_idx) {
		uint32_t memory_type_index = 0;
		uint32_t mem_type_bits = mem_requirements[mem_idx].memoryRequirements.memoryTypeBits;

		for (; !(mem_type_bits & 1); memory_type_index++)
			mem_type_bits >>= 1;

		VkMemoryAllocateInfo mem_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = mem_requirements[mem_idx].memoryRequirements.size,
			.memoryTypeIndex = memory_type_index,
		};

		VK_CHECK(vkAllocateMemory(self->device, &mem_info, nullptr, &self->memory_bound[mem_idx]));

		decode_session_bind_memory[mem_idx].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
		decode_session_bind_memory[mem_idx].memory = self->memory_bound[mem_idx];
		decode_session_bind_memory[mem_idx].memorySize = mem_requirements[mem_idx].memoryRequirements.size;
		decode_session_bind_memory[mem_idx].memoryOffset = 0;
		decode_session_bind_memory[mem_idx].memoryBindIndex = mem_requirements[mem_idx].memoryBindIndex;
		decode_session_bind_memory[mem_idx].pNext = NULL;
	}

	VK_CHECK(vkBindVideoSessionMemoryKHR(self->device, self->video_session, video_session_mem_req_count,
					     decode_session_bind_memory));

	self->memory_bound_size = video_session_mem_req_count;

	return self;
}

void vk_context_destroy(struct vk_context *context)
{
	if (context->video_session != VK_NULL_HANDLE) {
		vkDestroyVideoSessionKHR(context->device, context->video_session, nullptr);
		context->video_session = VK_NULL_HANDLE;
	}

	for (uint32_t i = 0; i < context->memory_bound_size; ++i) {
		vkFreeMemory(context->device, context->memory_bound[i], nullptr);
		context->memory_bound[i] = VK_NULL_HANDLE;
	}

	if (context->device != VK_NULL_HANDLE) {
		vkDestroyDevice(context->device, nullptr);
		context->device = VK_NULL_HANDLE;
	}

#ifdef DEBUG_BUILD
	vkDestroyDebugUtilsMessengerEXT(context->instance, context->dbg_messenger, nullptr);
	context->dbg_messenger = VK_NULL_HANDLE;
#endif

	if (context->instance != VK_NULL_HANDLE) {
		vkDestroyInstance(context->instance, nullptr);
		context->instance = VK_NULL_HANDLE;
	}

	volkFinalize();
}
