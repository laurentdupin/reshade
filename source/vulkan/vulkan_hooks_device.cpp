/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "vulkan_hooks.hpp"
#include "vulkan_impl_device.hpp"
#include "vulkan_impl_command_queue.hpp"
#include "vulkan_impl_swapchain.hpp"
#include "vulkan_impl_type_convert.hpp"
#include "dll_log.hpp"
#include "hook_manager.hpp"
#include "runtime_manager.hpp"
#include "lockfree_linear_map.hpp"
#include <cstring> // std::strcmp, std::strncmp
#include <algorithm> // std::fill_n, std::find_if, std::min, std::sort, std::unique

// Set during Vulkan device creation and presentation, to avoid hooking internal D3D devices created e.g. by NVIDIA Ansel and Optimus
extern thread_local bool g_in_dxgi_runtime;

lockfree_linear_map<void *, reshade::vulkan::device_impl *, 8> g_vulkan_devices;
extern lockfree_linear_map<void *, instance_dispatch_table, 16> g_vulkan_instances;
extern lockfree_linear_map<VkSurfaceKHR, HWND, 16> g_vulkan_surface_windows;

#define GET_DISPATCH_PTR(name, object) \
	GET_DISPATCH_PTR_FROM(name, g_vulkan_devices.at(dispatch_key_from_handle(object)))
#define GET_DISPATCH_PTR_FROM(name, data) \
	assert((data) != nullptr); \
	PFN_vk##name trampoline = (data)->_dispatch_table.name; \
	assert(trampoline != nullptr)
#define INIT_DISPATCH_PTR(name) \
	dispatch_table.name = reinterpret_cast<PFN_vk##name>(get_device_proc(device, "vk" #name))
#define INIT_DISPATCH_PTR_ALTERNATIVE(name, suffix) \
	if (nullptr == dispatch_table.name) \
		dispatch_table.name = reinterpret_cast<PFN_vk##name##suffix>(get_device_proc(device, "vk" #name #suffix))


VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
	reshade::log::message(reshade::log::level::info, "Redirecting vkCreateDevice(physicalDevice = %p, pCreateInfo = %p, pAllocator = %p, pDevice = %p) ...", physicalDevice, pCreateInfo, pAllocator, pDevice);

	assert(pCreateInfo != nullptr && pDevice != nullptr);

	const instance_dispatch_table &instance_dispatch = g_vulkan_instances.at(dispatch_key_from_handle(physicalDevice));
	assert(instance_dispatch.instance != VK_NULL_HANDLE);

	// Look for layer link info if installed as a layer (provided by the Vulkan loader)
	VkLayerDeviceCreateInfo *const link_info = find_layer_info<VkLayerDeviceCreateInfo>(pCreateInfo->pNext, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, VK_LAYER_LINK_INFO);

	// Get trampoline function pointers
	PFN_vkCreateDevice trampoline = nullptr;
	PFN_vkGetDeviceProcAddr get_device_proc = nullptr;
	PFN_vkGetInstanceProcAddr get_instance_proc = nullptr;

	if (link_info != nullptr)
	{
		assert(link_info->u.pLayerInfo != nullptr);
		assert(link_info->u.pLayerInfo->pfnNextGetDeviceProcAddr != nullptr);
		assert(link_info->u.pLayerInfo->pfnNextGetInstanceProcAddr != nullptr);

		// Look up functions in layer info
		get_device_proc = link_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
		get_instance_proc = link_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
		trampoline = reinterpret_cast<PFN_vkCreateDevice>(get_instance_proc(instance_dispatch.instance, "vkCreateDevice"));

		// Advance the link info for the next element on the chain
		link_info->u.pLayerInfo = link_info->u.pLayerInfo->pNext;
	}
#ifdef RESHADE_TEST_APPLICATION
	else
	{
		trampoline = reshade::hooks::call(vkCreateDevice);
		get_device_proc = reshade::hooks::call(vkGetDeviceProcAddr);
		get_instance_proc = reshade::hooks::call(vkGetInstanceProcAddr);
	}
#endif

	if (trampoline == nullptr) // Unable to resolve next 'vkCreateDevice' function in the call chain
		return VK_ERROR_INITIALIZATION_FAILED;

	reshade::log::message(reshade::log::level::info, "> Dumping enabled device extensions:");
	for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i)
		reshade::log::message(reshade::log::level::info, "  %s", pCreateInfo->ppEnabledExtensionNames[i]);

	const auto enum_queue_families = instance_dispatch.GetPhysicalDeviceQueueFamilyProperties;
	assert(enum_queue_families != nullptr);
	const auto enum_device_extensions = instance_dispatch.EnumerateDeviceExtensionProperties;
	assert(enum_device_extensions != nullptr);

	uint32_t num_queue_families = 0;
	enum_queue_families(physicalDevice, &num_queue_families, nullptr);
	std::vector<VkQueueFamilyProperties> queue_families(num_queue_families);
	enum_queue_families(physicalDevice, &num_queue_families, queue_families.data());

	uint32_t graphics_queue_family_index = std::numeric_limits<uint32_t>::max();
	for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; ++i)
	{
		const uint32_t queue_family_index = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
		assert(queue_family_index < num_queue_families);

		// Find the first queue family which supports graphics and has at least one queue
		if (pCreateInfo->pQueueCreateInfos[i].queueCount > 0 && (queue_families[queue_family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			if (pCreateInfo->pQueueCreateInfos[i].pQueuePriorities[0] < 1.0f)
				reshade::log::message(reshade::log::level::warning, "Vulkan queue used for rendering has a low priority (%f).", pCreateInfo->pQueueCreateInfos[i].pQueuePriorities[0]);

			graphics_queue_family_index = queue_family_index;
			break;
		}
	}

	VkPhysicalDeviceFeatures enabled_features = {};
	const VkPhysicalDeviceFeatures2 *const features2 = find_in_structure_chain<VkPhysicalDeviceFeatures2>(
		pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
	if (features2 != nullptr) // The features from the structure chain take precedence
		enabled_features = features2->features;
	else if (pCreateInfo->pEnabledFeatures != nullptr)
		enabled_features = *pCreateInfo->pEnabledFeatures;

	std::vector<const char *> enabled_extensions;
	enabled_extensions.reserve(pCreateInfo->enabledExtensionCount);
	for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i)
		enabled_extensions.push_back(pCreateInfo->ppEnabledExtensionNames[i]);

	bool push_descriptor_ext = false;
	bool dynamic_rendering_ext = false;
	bool timeline_semaphore_ext = false;
	bool custom_border_color_ext = false;
	bool extended_dynamic_state_ext = false;
	bool conservative_rasterization_ext = false;
	bool ray_tracing_ext = false;

	// Check if the device is used for presenting
	if (std::find_if(enabled_extensions.cbegin(), enabled_extensions.cend(),
			[](const char *name) { return std::strcmp(name, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; }) == enabled_extensions.cend())
	{
		reshade::log::message(reshade::log::level::warning, "Skipping device because it is not created with the \"" VK_KHR_SWAPCHAIN_EXTENSION_NAME "\" extension.");

		graphics_queue_family_index = std::numeric_limits<uint32_t>::max();
	}
	// Only have to enable additional features if there is a graphics queue, since ReShade will not run otherwise
	else if (graphics_queue_family_index == std::numeric_limits<uint32_t>::max())
	{
		reshade::log::message(reshade::log::level::warning, "Skipping device because it is not created with a graphics queue.");
	}
	else
	{
		// No Man's Sky initializes OpenVR before loading Vulkan (and therefore before loading ReShade), so need to manually install OpenVR hooks now when used
		extern void check_and_init_openvr_hooks();
		check_and_init_openvr_hooks();

		uint32_t num_extensions = 0;
		enum_device_extensions(physicalDevice, nullptr, &num_extensions, nullptr);
		std::vector<VkExtensionProperties> extensions(num_extensions);
		enum_device_extensions(physicalDevice, nullptr, &num_extensions, extensions.data());

		// Make sure the driver actually supports the requested extensions
		const auto add_extension = [&extensions, &enabled_extensions, &graphics_queue_family_index](const char *name, bool required) {
			if (const auto it = std::find_if(extensions.cbegin(), extensions.cend(),
					[name](const auto &props) { return std::strncmp(props.extensionName, name, VK_MAX_EXTENSION_NAME_SIZE) == 0; });
				it != extensions.cend())
			{
				enabled_extensions.push_back(name);
				return true;
			}

			if (required)
			{
				reshade::log::message(reshade::log::level::error, "Required extension \"%s\" is not supported on this device. Initialization failed.", name);

				// Reset queue family index to prevent ReShade initialization
				graphics_queue_family_index = std::numeric_limits<uint32_t>::max();
			}
			else
			{
				reshade::log::message(reshade::log::level::warning, "Optional extension \"%s\" is not supported on this device.", name);
			}

			return false;
		};

		// Enable features that ReShade requires
		enabled_features.samplerAnisotropy = VK_TRUE;
		enabled_features.shaderImageGatherExtended = VK_TRUE;
		enabled_features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

		// Enable extensions that ReShade requires
		if (instance_dispatch.api_version < VK_API_VERSION_1_3 && !add_extension(VK_EXT_PRIVATE_DATA_EXTENSION_NAME, true))
			return VK_ERROR_EXTENSION_NOT_PRESENT;

		add_extension(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME, true);
		add_extension(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME, true);

		push_descriptor_ext = add_extension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, false);
		dynamic_rendering_ext = instance_dispatch.api_version >= VK_API_VERSION_1_3 || add_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, false);
		// Add extensions that are required by VK_KHR_dynamic_rendering when not using the core variant
		if (dynamic_rendering_ext && instance_dispatch.api_version < VK_API_VERSION_1_3)
		{
			add_extension(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, false);
			add_extension(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME, false);
		}
		timeline_semaphore_ext = instance_dispatch.api_version >= VK_API_VERSION_1_2 || add_extension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME, false);
		custom_border_color_ext = add_extension(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME, false);
		extended_dynamic_state_ext = instance_dispatch.api_version >= VK_API_VERSION_1_3 || add_extension(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME, false);
		conservative_rasterization_ext = add_extension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME, false);
		add_extension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, false);

#if 0
		ray_tracing_ext =
			add_extension(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME, false) &&
			add_extension(VK_KHR_SPIRV_1_4_EXTENSION_NAME, false) &&
			add_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false) &&
			add_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, false) &&
			add_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, false) &&
			add_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, false) &&
			add_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false);
#endif
	}

	VkDeviceCreateInfo create_info = *pCreateInfo;
	create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
	create_info.ppEnabledExtensionNames = enabled_extensions.data();

	// Patch the enabled features
	if (features2 != nullptr)
		// This is evil, because overwriting application memory, but whatever (RenderDoc does this too)
		const_cast<VkPhysicalDeviceFeatures2 *>(features2)->features = enabled_features;
	else
		create_info.pEnabledFeatures = &enabled_features;

	// Enable private data feature
	VkDevicePrivateDataCreateInfo private_data_info { VK_STRUCTURE_TYPE_DEVICE_PRIVATE_DATA_CREATE_INFO };
	private_data_info.pNext = create_info.pNext;
	private_data_info.privateDataSlotRequestCount = 1;

	VkPhysicalDevicePrivateDataFeatures private_data_feature;
	VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_feature;
	VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_feature;

	if (const auto existing_vulkan_13_features = find_in_structure_chain<VkPhysicalDeviceVulkan13Features>(
			pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES))
	{
		assert(instance_dispatch.api_version >= VK_API_VERSION_1_3);

		create_info.pNext = &private_data_info;

		dynamic_rendering_ext = existing_vulkan_13_features->dynamicRendering;

		// Force enable private data in Vulkan 1.3, again, evil =)
		const_cast<VkPhysicalDeviceVulkan13Features *>(existing_vulkan_13_features)->privateData = VK_TRUE;
	}
	else
	{
		private_data_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES };
		private_data_feature.pNext = &private_data_info;
		private_data_feature.privateData = VK_TRUE;

		create_info.pNext = &private_data_feature;

		if (const auto existing_dynamic_rendering_features = find_in_structure_chain<VkPhysicalDeviceDynamicRenderingFeatures>(
				pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES))
		{
			dynamic_rendering_ext = existing_dynamic_rendering_features->dynamicRendering;
		}
		else if (dynamic_rendering_ext)
		{
			dynamic_rendering_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
			dynamic_rendering_feature.pNext = const_cast<void *>(create_info.pNext);
			dynamic_rendering_feature.dynamicRendering = VK_TRUE;

			create_info.pNext = &dynamic_rendering_feature;
		}
	}

	if (const auto existing_vulkan_12_features = find_in_structure_chain<VkPhysicalDeviceVulkan12Features>(
			pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES))
	{
		assert(instance_dispatch.api_version >= VK_API_VERSION_1_2);

		timeline_semaphore_ext = existing_vulkan_12_features->timelineSemaphore;
	}
	else
	{
		if (const auto existing_timeline_semaphore_features = find_in_structure_chain<VkPhysicalDeviceTimelineSemaphoreFeatures>(
				pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES))
		{
			timeline_semaphore_ext = existing_timeline_semaphore_features->timelineSemaphore;
		}
		else if (timeline_semaphore_ext)
		{
			timeline_semaphore_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
			timeline_semaphore_feature.pNext = const_cast<void *>(create_info.pNext);
			timeline_semaphore_feature.timelineSemaphore = VK_TRUE;

			create_info.pNext = &timeline_semaphore_feature;
		}
	}

	// Optionally enable custom border color feature
	VkPhysicalDeviceCustomBorderColorFeaturesEXT custom_border_feature;
	if (const auto existing_custom_border_features = find_in_structure_chain<VkPhysicalDeviceCustomBorderColorFeaturesEXT>(
			pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT))
	{
		custom_border_color_ext = existing_custom_border_features->customBorderColors;
	}
	else if (custom_border_color_ext)
	{
		custom_border_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT };
		custom_border_feature.pNext = const_cast<void *>(create_info.pNext);
		custom_border_feature.customBorderColors = VK_TRUE;
		custom_border_feature.customBorderColorWithoutFormat = VK_TRUE;

		create_info.pNext = &custom_border_feature;
	}

	// Optionally enable extended dynamic state feature
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state_feature;
	if (const auto existing_extended_dynamic_state_features = find_in_structure_chain<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>(
			pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT))
	{
		extended_dynamic_state_ext = existing_extended_dynamic_state_features->extendedDynamicState;
	}
	else if (extended_dynamic_state_ext)
	{
		extended_dynamic_state_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT };
		extended_dynamic_state_feature.pNext = const_cast<void *>(create_info.pNext);
		extended_dynamic_state_feature.extendedDynamicState = VK_TRUE;

		create_info.pNext = &extended_dynamic_state_feature;
	}

	// Optionally enable ray tracing feature
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_feature;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_feature;
	VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_feature;
	if (const auto existing_ray_tracing_features = find_in_structure_chain<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(
			pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR))
	{
		ray_tracing_ext = existing_ray_tracing_features->rayTracingPipeline;
	}
	else if (ray_tracing_ext)
	{
		ray_tracing_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
		ray_tracing_feature.pNext = const_cast<void *>(create_info.pNext);
		ray_tracing_feature.rayTracingPipeline = VK_TRUE;

		create_info.pNext = &ray_tracing_feature;

		acceleration_structure_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
		acceleration_structure_feature.pNext = const_cast<void *>(create_info.pNext);
		acceleration_structure_feature.accelerationStructure = VK_TRUE;

		create_info.pNext = &acceleration_structure_feature;

		buffer_device_address_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
		buffer_device_address_feature.pNext = const_cast<void *>(create_info.pNext);
		buffer_device_address_feature.bufferDeviceAddress = VK_TRUE;

		create_info.pNext = &buffer_device_address_feature;
	}

	// Continue calling down the chain
	assert(!g_in_dxgi_runtime);
	g_in_dxgi_runtime = true;
	const VkResult result = trampoline(physicalDevice, &create_info, pAllocator, pDevice);
	g_in_dxgi_runtime = false;
	if (result < VK_SUCCESS)
	{
		reshade::log::message(reshade::log::level::warning, "vkCreateDevice failed with error code %d.", static_cast<int>(result));
		return result;
	}

	VkDevice device = *pDevice;
	// Initialize the device dispatch table
	VkLayerDispatchTable dispatch_table = {};
	dispatch_table.GetDeviceProcAddr = get_device_proc;

	// Core 1_0
	INIT_DISPATCH_PTR(DestroyDevice);
	INIT_DISPATCH_PTR(GetDeviceQueue);
	INIT_DISPATCH_PTR(QueueSubmit);
	INIT_DISPATCH_PTR(QueueWaitIdle);
	INIT_DISPATCH_PTR(DeviceWaitIdle);
	INIT_DISPATCH_PTR(AllocateMemory);
	INIT_DISPATCH_PTR(FreeMemory);
	INIT_DISPATCH_PTR(MapMemory);
	INIT_DISPATCH_PTR(UnmapMemory);
	INIT_DISPATCH_PTR(FlushMappedMemoryRanges);
	INIT_DISPATCH_PTR(InvalidateMappedMemoryRanges);
	INIT_DISPATCH_PTR(BindBufferMemory);
	INIT_DISPATCH_PTR(BindImageMemory);
	INIT_DISPATCH_PTR(GetBufferMemoryRequirements);
	INIT_DISPATCH_PTR(GetImageMemoryRequirements);
	INIT_DISPATCH_PTR(CreateFence);
	INIT_DISPATCH_PTR(DestroyFence);
	INIT_DISPATCH_PTR(ResetFences);
	INIT_DISPATCH_PTR(GetFenceStatus);
	INIT_DISPATCH_PTR(WaitForFences);
	INIT_DISPATCH_PTR(CreateSemaphore);
	INIT_DISPATCH_PTR(DestroySemaphore);
	INIT_DISPATCH_PTR(CreateQueryPool);
	INIT_DISPATCH_PTR(DestroyQueryPool);
	INIT_DISPATCH_PTR(GetQueryPoolResults);
	INIT_DISPATCH_PTR(CreateBuffer);
	INIT_DISPATCH_PTR(DestroyBuffer);
	INIT_DISPATCH_PTR(CreateBufferView);
	INIT_DISPATCH_PTR(DestroyBufferView);
	INIT_DISPATCH_PTR(CreateImage);
	INIT_DISPATCH_PTR(DestroyImage);
	INIT_DISPATCH_PTR(GetImageSubresourceLayout);
	INIT_DISPATCH_PTR(CreateImageView);
	INIT_DISPATCH_PTR(DestroyImageView);
	INIT_DISPATCH_PTR(CreateShaderModule);
	INIT_DISPATCH_PTR(DestroyShaderModule);
	INIT_DISPATCH_PTR(CreateGraphicsPipelines);
	INIT_DISPATCH_PTR(CreateComputePipelines);
	INIT_DISPATCH_PTR(DestroyPipeline);
	INIT_DISPATCH_PTR(CreatePipelineLayout);
	INIT_DISPATCH_PTR(DestroyPipelineLayout);
	INIT_DISPATCH_PTR(CreateSampler);
	INIT_DISPATCH_PTR(DestroySampler);
	INIT_DISPATCH_PTR(CreateDescriptorSetLayout);
	INIT_DISPATCH_PTR(DestroyDescriptorSetLayout);
	INIT_DISPATCH_PTR(CreateDescriptorPool);
	INIT_DISPATCH_PTR(DestroyDescriptorPool);
	INIT_DISPATCH_PTR(ResetDescriptorPool);
	INIT_DISPATCH_PTR(AllocateDescriptorSets);
	INIT_DISPATCH_PTR(FreeDescriptorSets);
	INIT_DISPATCH_PTR(UpdateDescriptorSets);
	INIT_DISPATCH_PTR(CreateFramebuffer);
	INIT_DISPATCH_PTR(DestroyFramebuffer);
	INIT_DISPATCH_PTR(CreateRenderPass);
	INIT_DISPATCH_PTR(DestroyRenderPass);
	INIT_DISPATCH_PTR(CreateCommandPool);
	INIT_DISPATCH_PTR(DestroyCommandPool);
	INIT_DISPATCH_PTR(ResetCommandPool);
	INIT_DISPATCH_PTR(AllocateCommandBuffers);
	INIT_DISPATCH_PTR(FreeCommandBuffers);
	INIT_DISPATCH_PTR(BeginCommandBuffer);
	INIT_DISPATCH_PTR(EndCommandBuffer);
	INIT_DISPATCH_PTR(ResetCommandBuffer);
	INIT_DISPATCH_PTR(CmdBindPipeline);
	INIT_DISPATCH_PTR(CmdSetViewport);
	INIT_DISPATCH_PTR(CmdSetScissor);
	INIT_DISPATCH_PTR(CmdSetDepthBias);
	INIT_DISPATCH_PTR(CmdSetBlendConstants);
	INIT_DISPATCH_PTR(CmdSetStencilCompareMask);
	INIT_DISPATCH_PTR(CmdSetStencilWriteMask);
	INIT_DISPATCH_PTR(CmdSetStencilReference);
	INIT_DISPATCH_PTR(CmdBindDescriptorSets);
	INIT_DISPATCH_PTR(CmdBindIndexBuffer);
	INIT_DISPATCH_PTR(CmdBindVertexBuffers);
	INIT_DISPATCH_PTR(CmdDraw);
	INIT_DISPATCH_PTR(CmdDrawIndexed);
	INIT_DISPATCH_PTR(CmdDrawIndirect);
	INIT_DISPATCH_PTR(CmdDrawIndexedIndirect);
	INIT_DISPATCH_PTR(CmdDispatch);
	INIT_DISPATCH_PTR(CmdDispatchIndirect);
	INIT_DISPATCH_PTR(CmdCopyBuffer);
	INIT_DISPATCH_PTR(CmdCopyImage);
	INIT_DISPATCH_PTR(CmdBlitImage);
	INIT_DISPATCH_PTR(CmdCopyBufferToImage);
	INIT_DISPATCH_PTR(CmdCopyImageToBuffer);
	INIT_DISPATCH_PTR(CmdUpdateBuffer);
	INIT_DISPATCH_PTR(CmdClearColorImage);
	INIT_DISPATCH_PTR(CmdClearDepthStencilImage);
	INIT_DISPATCH_PTR(CmdClearAttachments);
	INIT_DISPATCH_PTR(CmdResolveImage);
	INIT_DISPATCH_PTR(CmdPipelineBarrier);
	INIT_DISPATCH_PTR(CmdBeginQuery);
	INIT_DISPATCH_PTR(CmdEndQuery);
	INIT_DISPATCH_PTR(CmdResetQueryPool);
	INIT_DISPATCH_PTR(CmdWriteTimestamp);
	INIT_DISPATCH_PTR(CmdCopyQueryPoolResults);
	INIT_DISPATCH_PTR(CmdPushConstants);
	INIT_DISPATCH_PTR(CmdBeginRenderPass);
	INIT_DISPATCH_PTR(CmdNextSubpass);
	INIT_DISPATCH_PTR(CmdEndRenderPass);
	INIT_DISPATCH_PTR(CmdExecuteCommands);

	// Core 1_1
	if (instance_dispatch.api_version >= VK_API_VERSION_1_1)
	{
		INIT_DISPATCH_PTR(BindBufferMemory2);
		INIT_DISPATCH_PTR(BindImageMemory2);
		INIT_DISPATCH_PTR(GetBufferMemoryRequirements2);
		INIT_DISPATCH_PTR(GetImageMemoryRequirements2);
		INIT_DISPATCH_PTR(GetDeviceQueue2);
	}

	// Core 1_2
	if (instance_dispatch.api_version >= VK_API_VERSION_1_2)
	{
		INIT_DISPATCH_PTR(CmdDrawIndirectCount);
		INIT_DISPATCH_PTR(CmdDrawIndexedIndirectCount);
		INIT_DISPATCH_PTR(CreateRenderPass2);
		INIT_DISPATCH_PTR(CmdBeginRenderPass2);
		INIT_DISPATCH_PTR(CmdNextSubpass2);
		INIT_DISPATCH_PTR(CmdEndRenderPass2);
		INIT_DISPATCH_PTR(GetSemaphoreCounterValue);
		INIT_DISPATCH_PTR(WaitSemaphores);
		INIT_DISPATCH_PTR(SignalSemaphore);
		INIT_DISPATCH_PTR(GetBufferDeviceAddress);
	}

	// Core 1_3
	if (instance_dispatch.api_version >= VK_API_VERSION_1_3)
	{
		INIT_DISPATCH_PTR(CreatePrivateDataSlot);
		INIT_DISPATCH_PTR(DestroyPrivateDataSlot);
		INIT_DISPATCH_PTR(GetPrivateData);
		INIT_DISPATCH_PTR(SetPrivateData);
		INIT_DISPATCH_PTR(CmdPipelineBarrier2);
		INIT_DISPATCH_PTR(CmdWriteTimestamp2);
		INIT_DISPATCH_PTR(QueueSubmit2);
		INIT_DISPATCH_PTR(CmdCopyBuffer2);
		INIT_DISPATCH_PTR(CmdCopyImage2);
		INIT_DISPATCH_PTR(CmdCopyBufferToImage2);
		INIT_DISPATCH_PTR(CmdCopyImageToBuffer2);
		INIT_DISPATCH_PTR(CmdBlitImage2);
		INIT_DISPATCH_PTR(CmdResolveImage2);
		INIT_DISPATCH_PTR(CmdBeginRendering);
		INIT_DISPATCH_PTR(CmdEndRendering);
		INIT_DISPATCH_PTR(CmdSetCullMode);
		INIT_DISPATCH_PTR(CmdSetFrontFace);
		INIT_DISPATCH_PTR(CmdSetPrimitiveTopology);
		INIT_DISPATCH_PTR(CmdSetViewportWithCount);
		INIT_DISPATCH_PTR(CmdSetScissorWithCount);
		INIT_DISPATCH_PTR(CmdBindVertexBuffers2);
		INIT_DISPATCH_PTR(CmdSetDepthTestEnable);
		INIT_DISPATCH_PTR(CmdSetDepthWriteEnable);
		INIT_DISPATCH_PTR(CmdSetDepthCompareOp);
		INIT_DISPATCH_PTR(CmdSetDepthBoundsTestEnable);
		INIT_DISPATCH_PTR(CmdSetStencilTestEnable);
		INIT_DISPATCH_PTR(CmdSetStencilOp);
		INIT_DISPATCH_PTR(CmdSetRasterizerDiscardEnable);
		INIT_DISPATCH_PTR(CmdSetDepthBiasEnable);
		INIT_DISPATCH_PTR(CmdSetPrimitiveRestartEnable);
		INIT_DISPATCH_PTR(GetDeviceBufferMemoryRequirements);
		INIT_DISPATCH_PTR(GetDeviceImageMemoryRequirements);
	}

	// VK_KHR_swapchain
	INIT_DISPATCH_PTR(CreateSwapchainKHR);
	INIT_DISPATCH_PTR(DestroySwapchainKHR);
	INIT_DISPATCH_PTR(GetSwapchainImagesKHR);
	INIT_DISPATCH_PTR(AcquireNextImageKHR);
	INIT_DISPATCH_PTR(QueuePresentKHR);
	INIT_DISPATCH_PTR(AcquireNextImage2KHR);

	// VK_KHR_dynamic_rendering
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdBeginRendering, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdEndRendering, KHR);

	// VK_KHR_push_descriptor
	INIT_DISPATCH_PTR(CmdPushDescriptorSetKHR);

	// VK_KHR_create_renderpass2 (try the KHR version if the core version does not exist)
	INIT_DISPATCH_PTR_ALTERNATIVE(CreateRenderPass2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdBeginRenderPass2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdNextSubpass2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdEndRenderPass2, KHR);

	// VK_KHR_bind_memory2
	INIT_DISPATCH_PTR_ALTERNATIVE(BindBufferMemory2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(BindImageMemory2, KHR);

	// VK_KHR_draw_indirect_count
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdDrawIndirectCount, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdDrawIndexedIndirectCount, KHR);

	// VK_KHR_timeline_semaphore
	INIT_DISPATCH_PTR_ALTERNATIVE(GetSemaphoreCounterValue, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(WaitSemaphores, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(SignalSemaphore, KHR);

	// VK_KHR_buffer_device_address
	INIT_DISPATCH_PTR_ALTERNATIVE(GetBufferDeviceAddress, KHR);

	// VK_KHR_synchronization2
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdPipelineBarrier2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdWriteTimestamp2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(QueueSubmit2, KHR);

	// VK_KHR_copy_commands2
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdCopyBuffer2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdCopyImage2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdCopyBufferToImage2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdCopyImageToBuffer2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdBlitImage2, KHR);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdResolveImage2, KHR);

	// VK_EXT_transform_feedback
	INIT_DISPATCH_PTR(CmdBindTransformFeedbackBuffersEXT);
	INIT_DISPATCH_PTR(CmdBeginQueryIndexedEXT);
	INIT_DISPATCH_PTR(CmdEndQueryIndexedEXT);

	// VK_EXT_debug_utils
	INIT_DISPATCH_PTR(SetDebugUtilsObjectNameEXT);
	INIT_DISPATCH_PTR(QueueBeginDebugUtilsLabelEXT);
	INIT_DISPATCH_PTR(QueueEndDebugUtilsLabelEXT);
	INIT_DISPATCH_PTR(QueueInsertDebugUtilsLabelEXT);
	INIT_DISPATCH_PTR(CmdBeginDebugUtilsLabelEXT);
	INIT_DISPATCH_PTR(CmdEndDebugUtilsLabelEXT);
	INIT_DISPATCH_PTR(CmdInsertDebugUtilsLabelEXT);

	// VK_EXT_extended_dynamic_state
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetCullMode, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetFrontFace, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetPrimitiveTopology, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetViewportWithCount, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetScissorWithCount, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdBindVertexBuffers2, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetDepthTestEnable, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetDepthWriteEnable, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetDepthCompareOp, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetDepthBoundsTestEnable, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetStencilTestEnable, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(CmdSetStencilOp, EXT);

	// VK_EXT_private_data (try the EXT version if the core version does not exist)
	INIT_DISPATCH_PTR_ALTERNATIVE(CreatePrivateDataSlot, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(DestroyPrivateDataSlot, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(GetPrivateData, EXT);
	INIT_DISPATCH_PTR_ALTERNATIVE(SetPrivateData, EXT);

	// VK_KHR_acceleration_structure
	INIT_DISPATCH_PTR(CreateAccelerationStructureKHR);
	INIT_DISPATCH_PTR(DestroyAccelerationStructureKHR);
	INIT_DISPATCH_PTR(CmdBuildAccelerationStructuresKHR);
	INIT_DISPATCH_PTR(CmdBuildAccelerationStructuresIndirectKHR);
	INIT_DISPATCH_PTR(CopyAccelerationStructureKHR);
	INIT_DISPATCH_PTR(GetAccelerationStructureDeviceAddressKHR);
	INIT_DISPATCH_PTR(GetAccelerationStructureBuildSizesKHR);

	// VK_KHR_ray_tracing_pipeline
	INIT_DISPATCH_PTR(CmdTraceRaysKHR);
	INIT_DISPATCH_PTR(CreateRayTracingPipelinesKHR);
	INIT_DISPATCH_PTR(GetRayTracingShaderGroupHandlesKHR);
	INIT_DISPATCH_PTR(CmdTraceRaysIndirectKHR);
	INIT_DISPATCH_PTR(CmdSetRayTracingPipelineStackSizeKHR);

	// VK_KHR_ray_tracing_maintenance1
	INIT_DISPATCH_PTR(CmdTraceRaysIndirect2KHR);

	// VK_EXT_mesh_shader
	INIT_DISPATCH_PTR(CmdDrawMeshTasksEXT);
	INIT_DISPATCH_PTR(CmdDrawMeshTasksIndirectEXT);
	INIT_DISPATCH_PTR(CmdDrawMeshTasksIndirectCountEXT);

	// VK_KHR_external_memory_win32
	INIT_DISPATCH_PTR(GetMemoryWin32HandleKHR);
	INIT_DISPATCH_PTR(GetMemoryWin32HandlePropertiesKHR);

	// VK_KHR_external_semaphore_win32
	INIT_DISPATCH_PTR(ImportSemaphoreWin32HandleKHR);
	INIT_DISPATCH_PTR(GetSemaphoreWin32HandleKHR);

	// Initialize per-device data
	const auto device_impl = new reshade::vulkan::device_impl(
		device,
		physicalDevice,
		instance_dispatch.instance,
		instance_dispatch.api_version,
		static_cast<const VkLayerInstanceDispatchTable &>(instance_dispatch),
		dispatch_table,
		enabled_features,
		push_descriptor_ext,
		dynamic_rendering_ext,
		timeline_semaphore_ext,
		custom_border_color_ext,
		extended_dynamic_state_ext,
		conservative_rasterization_ext,
		ray_tracing_ext);

	device_impl->_graphics_queue_family_index = graphics_queue_family_index;

	if (!g_vulkan_devices.emplace(dispatch_key_from_handle(device), device_impl))
	{
		reshade::log::message(reshade::log::level::warning, "Failed to register Vulkan device %p.", device);
	}


	// Initialize all queues associated with this device
	for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; ++i)
	{
		const VkDeviceQueueCreateInfo &queue_create_info = pCreateInfo->pQueueCreateInfos[i];

		for (uint32_t queue_index = 0; queue_index < queue_create_info.queueCount; ++queue_index)
		{
			VkDeviceQueueInfo2 queue_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2 };
			queue_info.flags = queue_create_info.flags;
			queue_info.queueFamilyIndex = queue_create_info.queueFamilyIndex;
			queue_info.queueIndex = queue_index;

			VkQueue queue = VK_NULL_HANDLE;
			// According to the spec, 'vkGetDeviceQueue' must only be used to get queues where 'VkDeviceQueueCreateInfo::flags' is set to zero, so use 'vkGetDeviceQueue2' instead
			dispatch_table.GetDeviceQueue2(device, &queue_info, &queue);
			assert(VK_NULL_HANDLE != queue);

			// Subsequent layers (like the validation layer or the Steam overlay) expect the loader to have set the dispatch pointer, but this does not happen when calling down the layer chain from here, so fix it
			// This applies to 'vkGetDeviceQueue', 'vkGetDeviceQueue2' and 'vkAllocateCommandBuffers' (functions that return dispatchable objects)
			*reinterpret_cast<void **>(queue) = *reinterpret_cast<void **>(device);

			const auto queue_impl = new reshade::vulkan::object_data<VK_OBJECT_TYPE_QUEUE>(
				device_impl,
				queue_create_info.queueFamilyIndex,
				queue_families[queue_create_info.queueFamilyIndex],
				queue);

			device_impl->register_object<VK_OBJECT_TYPE_QUEUE>(queue, queue_impl);

		}
	}

#if RESHADE_VERBOSE_LOG
	reshade::log::message(reshade::log::level::debug, "Returning Vulkan device %p.", device);
#endif
	return result;
}
void     VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
{
	reshade::log::message(reshade::log::level::info, "Redirecting vkDestroyDevice(device = %p, pAllocator = %p) ...", device, pAllocator);

	if (device == VK_NULL_HANDLE)
		return;

	// Remove from device dispatch table since this device is being destroyed
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.erase(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyDevice, device_impl);

	// Destroy all queues associated with this device
	const std::vector<reshade::vulkan::command_queue_impl *> queues = device_impl->_queues;
	for (auto queue_it = queues.begin(); queue_it != queues.end(); ++queue_it)
	{
		const auto queue_impl = static_cast<reshade::vulkan::object_data<VK_OBJECT_TYPE_QUEUE> *>(*queue_it);


		device_impl->unregister_object<VK_OBJECT_TYPE_QUEUE, false>(queue_impl->_orig);

		delete queue_impl; // This will remove the queue from the queue list of the device too (see 'command_queue_impl' destructor)
	}


	// Finally destroy the device
	delete device_impl;

	trampoline(device, pAllocator);
}

VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
	reshade::log::message(reshade::log::level::info, "Redirecting vkCreateSwapchainKHR(device = %p, pCreateInfo = %p, pAllocator = %p, pSwapchain = %p) ...", device, pCreateInfo, pAllocator, pSwapchain);

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateSwapchainKHR, device_impl);

	assert(pCreateInfo != nullptr && pSwapchain != nullptr);

	std::vector<VkFormat> format_list;
	std::vector<uint32_t> queue_family_list;
	VkSwapchainCreateInfoKHR create_info = *pCreateInfo;
	VkImageFormatListCreateInfoKHR format_list_info;

	// Only have to enable additional features if there is a graphics queue, since ReShade will not run otherwise
	if (device_impl->_graphics_queue_family_index != std::numeric_limits<uint32_t>::max())
	{
		// Add required usage flags to create info
		create_info.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		// Add required format variants, so e.g. both linear and sRGB views can be created for the swap chain images
		format_list.push_back(reshade::vulkan::convert_format(
			reshade::api::format_to_default_typed(reshade::vulkan::convert_format(create_info.imageFormat), 0)));
		format_list.push_back(reshade::vulkan::convert_format(
			reshade::api::format_to_default_typed(reshade::vulkan::convert_format(create_info.imageFormat), 1)));

		// Only have to make format mutable if they are actually different
		if (format_list[0] != format_list[1])
			create_info.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;

		// Patch the format list in the create info of the application
		if (const auto format_list_info2 = find_in_structure_chain<VkImageFormatListCreateInfoKHR>(
				pCreateInfo->pNext, VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR))
		{
			format_list.insert(format_list.end(),
				format_list_info2->pViewFormats, format_list_info2->pViewFormats + format_list_info2->viewFormatCount);

			// Remove duplicates from the list (since the new formats may have already been added by the application)
			std::sort(format_list.begin(), format_list.end());
			format_list.erase(std::unique(format_list.begin(), format_list.end()), format_list.end());

			// This is evil, because writing into application memory, but eh =)
			const_cast<VkImageFormatListCreateInfoKHR *>(format_list_info2)->viewFormatCount = static_cast<uint32_t>(format_list.size());
			const_cast<VkImageFormatListCreateInfoKHR *>(format_list_info2)->pViewFormats = format_list.data();
		}
		else if (format_list[0] != format_list[1])
		{
			format_list_info = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR };
			format_list_info.pNext = create_info.pNext;
			format_list_info.viewFormatCount = static_cast<uint32_t>(format_list.size());
			format_list_info.pViewFormats = format_list.data();

			create_info.pNext = &format_list_info;
		}

		// Add required queue family indices, so images can be used on the graphics queue
		if (create_info.imageSharingMode == VK_SHARING_MODE_CONCURRENT)
		{
			queue_family_list.reserve(create_info.queueFamilyIndexCount + 1);
			queue_family_list.push_back(device_impl->_graphics_queue_family_index);

			for (uint32_t i = 0; i < create_info.queueFamilyIndexCount; ++i)
				if (create_info.pQueueFamilyIndices[i] != device_impl->_graphics_queue_family_index)
					queue_family_list.push_back(create_info.pQueueFamilyIndices[i]);

			create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_list.size());
			create_info.pQueueFamilyIndices = queue_family_list.data();
		}
	}

	// Dump swap chain description
	{
		const char *format_string = nullptr;
		switch (create_info.imageFormat)
		{
		case VK_FORMAT_UNDEFINED:
			format_string = "VK_FORMAT_UNDEFINED";
			break;
		case VK_FORMAT_R8G8B8A8_UNORM:
			format_string = "VK_FORMAT_R8G8B8A8_UNORM";
			break;
		case VK_FORMAT_R8G8B8A8_SRGB:
			format_string = "VK_FORMAT_R8G8B8A8_SRGB";
			break;
		case VK_FORMAT_B8G8R8A8_UNORM:
			format_string = "VK_FORMAT_B8G8R8A8_UNORM";
			break;
		case VK_FORMAT_B8G8R8A8_SRGB:
			format_string = "VK_FORMAT_B8G8R8A8_SRGB";
			break;
		case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
			format_string = "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
			break;
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
			format_string = "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
			break;
		case VK_FORMAT_R16G16B16A16_UNORM:
			format_string = "VK_FORMAT_R16G16B16A16_UNORM";
			break;
		case VK_FORMAT_R16G16B16A16_SFLOAT:
			format_string = "VK_FORMAT_R16G16B16A16_SFLOAT";
			break;
		}

		const char *color_space_string = nullptr;
		switch (create_info.imageColorSpace)
		{
		case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
			color_space_string = "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
			break;
		case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
			color_space_string = "VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT";
			break;
		case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
			color_space_string = "VK_COLOR_SPACE_BT2020_LINEAR_EXT";
			break;
		case VK_COLOR_SPACE_HDR10_ST2084_EXT:
			color_space_string = "VK_COLOR_SPACE_HDR10_ST2084_EXT";
			break;
		case VK_COLOR_SPACE_HDR10_HLG_EXT:
			color_space_string = "VK_COLOR_SPACE_HDR10_HLG_EXT";
			break;
		}

		reshade::log::message(reshade::log::level::info, "> Dumping swap chain description:");
		reshade::log::message(reshade::log::level::info, "  +-----------------------------------------+-----------------------------------------+");
		reshade::log::message(reshade::log::level::info, "  | Parameter                               | Value                                   |");
		reshade::log::message(reshade::log::level::info, "  +-----------------------------------------+-----------------------------------------+");
		reshade::log::message(reshade::log::level::info, "  | flags                                   |"                               " %-#39x |", static_cast<unsigned int>(create_info.flags));
		reshade::log::message(reshade::log::level::info, "  | surface                                 |"                                " %-39p |", create_info.surface);
		reshade::log::message(reshade::log::level::info, "  | minImageCount                           |"                                " %-39u |", create_info.minImageCount);
		if (format_string != nullptr)
		reshade::log::message(reshade::log::level::info, "  | imageFormat                             |"                                " %-39s |", format_string);
		else
		reshade::log::message(reshade::log::level::info, "  | imageFormat                             |"                                " %-39d |", static_cast<int>(create_info.imageFormat));
		if (color_space_string != nullptr)
		reshade::log::message(reshade::log::level::info, "  | imageColorSpace                         |"                                " %-39s |", color_space_string);
		else
		reshade::log::message(reshade::log::level::info, "  | imageColorSpace                         |"                                " %-39d |", static_cast<int>(create_info.imageColorSpace));
		reshade::log::message(reshade::log::level::info, "  | imageExtent                             |"            " %-19u"            " %-19u |", create_info.imageExtent.width, create_info.imageExtent.height);
		reshade::log::message(reshade::log::level::info, "  | imageArrayLayers                        |"                                " %-39u |", create_info.imageArrayLayers);
		reshade::log::message(reshade::log::level::info, "  | imageUsage                              |"                               " %-#39x |", static_cast<unsigned int>(create_info.imageUsage));
		reshade::log::message(reshade::log::level::info, "  | imageSharingMode                        |"                                " %-39d |", static_cast<int>(create_info.imageSharingMode));
		reshade::log::message(reshade::log::level::info, "  | queueFamilyIndexCount                   |"                                " %-39u |", create_info.queueFamilyIndexCount);
		reshade::log::message(reshade::log::level::info, "  | preTransform                            |"                               " %-#39x |", static_cast<unsigned int>(create_info.preTransform));
		reshade::log::message(reshade::log::level::info, "  | compositeAlpha                          |"                               " %-#39x |", static_cast<unsigned int>(create_info.compositeAlpha));
		reshade::log::message(reshade::log::level::info, "  | presentMode                             |"                                " %-39d |", static_cast<int>(create_info.presentMode));
		reshade::log::message(reshade::log::level::info, "  | clipped                                 |"                                " %-39s |", create_info.clipped ? "true" : "false");
		reshade::log::message(reshade::log::level::info, "  | oldSwapchain                            |"                                " %-39p |", create_info.oldSwapchain);
		reshade::log::message(reshade::log::level::info, "  +-----------------------------------------+-----------------------------------------+");
	}

	// Look up window handle from surface
	const HWND hwnd = g_vulkan_surface_windows.at(create_info.surface);


	// Unregister object from old swap chain so that a call to 'vkDestroySwapchainKHR' won't reset the effect runtime again
	reshade::vulkan::object_data<VK_OBJECT_TYPE_SWAPCHAIN_KHR> *swapchain_impl = nullptr;
	if (create_info.oldSwapchain != VK_NULL_HANDLE)
		swapchain_impl = device_impl->get_private_data_for_object<VK_OBJECT_TYPE_SWAPCHAIN_KHR>(create_info.oldSwapchain);

	if (nullptr != swapchain_impl)
	{
		// Reuse the existing effect runtime if this swap chain was not created from scratch, but reset it before initializing again below
		reshade::reset_effect_runtime(swapchain_impl);

		// Get back buffer images of old swap chain
		uint32_t num_images = 0;
		device_impl->_dispatch_table.GetSwapchainImagesKHR(device, swapchain_impl->_orig, &num_images, nullptr);
		temp_mem<VkImage, 3> swapchain_images(num_images);
		device_impl->_dispatch_table.GetSwapchainImagesKHR(device, swapchain_impl->_orig, &num_images, swapchain_images.p);


		for (uint32_t i = 0; i < num_images; ++i)
		{

			device_impl->unregister_object<VK_OBJECT_TYPE_IMAGE>(swapchain_images[i]);
		}

		device_impl->unregister_object<VK_OBJECT_TYPE_SWAPCHAIN_KHR, false>(swapchain_impl->_orig);
	}

	assert(!g_in_dxgi_runtime);
	g_in_dxgi_runtime = true;
	const VkResult result = trampoline(device, &create_info, pAllocator, pSwapchain);
	g_in_dxgi_runtime = false;
	if (result < VK_SUCCESS)
	{
		reshade::log::message(reshade::log::level::warning, "vkCreateSwapchainKHR failed with error code %d.", static_cast<int>(result));
		return result;
	}

	reshade::vulkan::command_queue_impl *queue_impl = nullptr;
	if (device_impl->_graphics_queue_family_index != std::numeric_limits<uint32_t>::max())
	{
		// Get the main graphics queue for command submission
		// There has to be at least one queue, or else this effect runtime would not have been created with this queue family index, so it is safe to get the first one here
		VkQueue graphics_queue = VK_NULL_HANDLE;
		device_impl->_dispatch_table.GetDeviceQueue(device, device_impl->_graphics_queue_family_index, 0, &graphics_queue);

		queue_impl = device_impl->get_private_data_for_object<VK_OBJECT_TYPE_QUEUE>(graphics_queue);
	}

	if (nullptr == swapchain_impl)
	{
		swapchain_impl = new reshade::vulkan::object_data<VK_OBJECT_TYPE_SWAPCHAIN_KHR>(device_impl, *pSwapchain, create_info, hwnd);

		reshade::create_effect_runtime(swapchain_impl, queue_impl);
	}
	else
	{
		swapchain_impl->_orig = *pSwapchain;
		swapchain_impl->_create_info = create_info;
		swapchain_impl->_hwnd = hwnd;
	}

	device_impl->register_object<VK_OBJECT_TYPE_SWAPCHAIN_KHR>(swapchain_impl->_orig, swapchain_impl);

	// Get back buffer images of new swap chain
	uint32_t num_images = 0;
	device_impl->_dispatch_table.GetSwapchainImagesKHR(device, swapchain_impl->_orig, &num_images, nullptr);
	temp_mem<VkImage, 3> swapchain_images(num_images);
	device_impl->_dispatch_table.GetSwapchainImagesKHR(device, swapchain_impl->_orig, &num_images, swapchain_images.p);

	// Add swap chain images to the image list
	for (uint32_t i = 0; i < num_images; ++i)
	{
		reshade::vulkan::object_data<VK_OBJECT_TYPE_IMAGE> &image_data = *device_impl->register_object<VK_OBJECT_TYPE_IMAGE>(swapchain_images[i]);
		image_data.allocation = VK_NULL_HANDLE;
		image_data.create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		image_data.create_info.imageType = VK_IMAGE_TYPE_2D;
		image_data.create_info.format = create_info.imageFormat;
		image_data.create_info.extent = { create_info.imageExtent.width, create_info.imageExtent.height, 1 };
		image_data.create_info.mipLevels = 1;
		image_data.create_info.arrayLayers = create_info.imageArrayLayers;
		image_data.create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_data.create_info.usage = create_info.imageUsage;
		image_data.create_info.sharingMode = create_info.imageSharingMode;
		image_data.create_info.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}


	reshade::init_effect_runtime(swapchain_impl);

#if RESHADE_VERBOSE_LOG
	reshade::log::message(reshade::log::level::debug, "Returning Vulkan swap chain %p.", *pSwapchain);
#endif
	return result;
}
void     VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator)
{
	reshade::log::message(reshade::log::level::info, "Redirecting vkDestroySwapchainKHR(device = %p, swapchain = %p, pAllocator = %p) ...", device, swapchain, pAllocator);

	if (swapchain == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroySwapchainKHR, device_impl);

	// Remove swap chain from global list
	reshade::vulkan::object_data<VK_OBJECT_TYPE_SWAPCHAIN_KHR> *const swapchain_impl = device_impl->get_private_data_for_object<VK_OBJECT_TYPE_SWAPCHAIN_KHR, true>(swapchain);
	if (swapchain_impl != nullptr)
	{
		reshade::reset_effect_runtime(swapchain_impl);

		// Get back buffer images of old swap chain
		uint32_t num_images = 0;
		device_impl->_dispatch_table.GetSwapchainImagesKHR(device, swapchain, &num_images, nullptr);
		temp_mem<VkImage, 3> swapchain_images(num_images);
		device_impl->_dispatch_table.GetSwapchainImagesKHR(device, swapchain, &num_images, swapchain_images.p);


		for (uint32_t i = 0; i < num_images; ++i)
		{

			device_impl->unregister_object<VK_OBJECT_TYPE_IMAGE>(swapchain_images[i]);
		}

		reshade::destroy_effect_runtime(swapchain_impl);
	}

	device_impl->unregister_object<VK_OBJECT_TYPE_SWAPCHAIN_KHR, false>(swapchain);

	delete swapchain_impl;

	trampoline(device, swapchain, pAllocator);
}

VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
{
	assert(pImageIndex != nullptr);

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(AcquireNextImageKHR, device_impl);

	const VkResult result = trampoline(device, swapchain, timeout, semaphore, fence, pImageIndex);
	if (result == VK_SUCCESS)
	{
		if (reshade::vulkan::object_data<VK_OBJECT_TYPE_SWAPCHAIN_KHR> *const swapchain_impl = device_impl->get_private_data_for_object<VK_OBJECT_TYPE_SWAPCHAIN_KHR, true>(swapchain))
			swapchain_impl->_swap_index = *pImageIndex;
	}
#if RESHADE_VERBOSE_LOG
	else if (result < VK_SUCCESS)
	{
		reshade::log::message(reshade::log::level::warning, "vkAcquireNextImageKHR failed with error code %d.", static_cast<int>(result));
	}
#endif

	return result;
}
VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo, uint32_t *pImageIndex)
{
	assert(pAcquireInfo != nullptr && pImageIndex != nullptr);

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(AcquireNextImage2KHR, device_impl);

	const VkResult result = trampoline(device, pAcquireInfo, pImageIndex);
	if (result == VK_SUCCESS)
	{
		if (reshade::vulkan::object_data<VK_OBJECT_TYPE_SWAPCHAIN_KHR> *const swapchain_impl = device_impl->get_private_data_for_object<VK_OBJECT_TYPE_SWAPCHAIN_KHR, true>(pAcquireInfo->swapchain))
			swapchain_impl->_swap_index = *pImageIndex;
	}
#if RESHADE_VERBOSE_LOG
	else if (result < VK_SUCCESS)
	{
		reshade::log::message(reshade::log::level::warning, "vkAcquireNextImage2KHR failed with error code %d.", static_cast<int>(result));
	}
#endif

	return result;
}

VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence)
{
	assert(pSubmits != nullptr);

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(queue));

	GET_DISPATCH_PTR_FROM(QueueSubmit, device_impl);
	return trampoline(queue, submitCount, pSubmits, fence);
}
VkResult VKAPI_CALL vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence)
{
	assert(pSubmits != nullptr);

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(queue));

	GET_DISPATCH_PTR_FROM(QueueSubmit2, device_impl);
	return trampoline(queue, submitCount, pSubmits, fence);
}

VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
	assert(pPresentInfo != nullptr);

	VkPresentInfoKHR present_info = *pPresentInfo;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(queue));
	reshade::vulkan::command_queue_impl *const queue_impl = device_impl->get_private_data_for_object<VK_OBJECT_TYPE_QUEUE>(queue);

	for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
	{
		reshade::vulkan::swapchain_impl *const swapchain_impl = device_impl->get_private_data_for_object<VK_OBJECT_TYPE_SWAPCHAIN_KHR>(pPresentInfo->pSwapchains[i]);


		reshade::present_effect_runtime(swapchain_impl, queue_impl);
	}

	// Synchronize immediate command list flush
	{
		temp_mem<VkPipelineStageFlags> wait_stages(present_info.waitSemaphoreCount);
		std::fill_n(wait_stages.p, present_info.waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		VkSubmitInfo submit_info { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit_info.waitSemaphoreCount = present_info.waitSemaphoreCount;
		submit_info.pWaitSemaphores = present_info.pWaitSemaphores;
		submit_info.pWaitDstStageMask = wait_stages.p;

		queue_impl->flush_immediate_command_list(submit_info);

		// Override wait semaphores based on the last queue submit
		present_info.waitSemaphoreCount = submit_info.waitSemaphoreCount;
		present_info.pWaitSemaphores = submit_info.pWaitSemaphores;
	}

	device_impl->advance_transient_descriptor_pool();

	GET_DISPATCH_PTR_FROM(QueuePresentKHR, device_impl);
	assert(!g_in_dxgi_runtime);
	g_in_dxgi_runtime = true;
	const VkResult result = trampoline(queue, &present_info);
	g_in_dxgi_runtime = false;
	return result;
}

VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(BindBufferMemory, device_impl);

	const VkResult result = trampoline(device, buffer, memory, memoryOffset);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkBindBufferMemory failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo *pBindInfos)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(BindBufferMemory2, device_impl);

	const VkResult result = trampoline(device, bindInfoCount, pBindInfos);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkBindBufferMemory2 failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}

VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(BindImageMemory, device_impl);

	const VkResult result = trampoline(device, image, memory, memoryOffset);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkBindImageMemory failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
VkResult VKAPI_CALL vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(BindImageMemory2, device_impl);

	const VkResult result = trampoline(device, bindInfoCount, pBindInfos);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkBindImageMemory2 failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}

VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateQueryPool, device_impl);

	assert(pCreateInfo != nullptr && pQueryPool != nullptr);


	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pQueryPool);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateQueryPool failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator)
{
	if (queryPool == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyQueryPool, device_impl);


	trampoline(device, queryPool, pAllocator);
}

VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride, VkQueryResultFlags flags)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(GetQueryPoolResults, device_impl);


	return trampoline(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
}

VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateBuffer, device_impl);

	assert(pCreateInfo != nullptr && pBuffer != nullptr);


	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pBuffer);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateBuffer failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator)
{
	if (buffer == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyBuffer, device_impl);


	trampoline(device, buffer, pAllocator);
}

VkResult VKAPI_CALL vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBufferView *pView)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateBufferView, device_impl);

	assert(pCreateInfo != nullptr && pView != nullptr);


	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pView);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateBufferView failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks *pAllocator)
{
	if (bufferView == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyBufferView, device_impl);


	trampoline(device, bufferView, pAllocator);
}

VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateImage, device_impl);

	assert(pCreateInfo != nullptr && pImage != nullptr);


	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pImage);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateImage failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator)
{
	if (image == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyImage, device_impl);


	trampoline(device, image, pAllocator);
}

VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateImageView, device_impl);

	assert(pCreateInfo != nullptr && pView != nullptr);


	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pView);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateImageView failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator)
{
	if (imageView == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyImageView, device_impl);


	trampoline(device, imageView, pAllocator);
}

VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateShaderModule, device_impl);

	assert(pCreateInfo != nullptr && pShaderModule != nullptr);

	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pShaderModule);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateShaderModule failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator)
{
	if (shaderModule == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyShaderModule, device_impl);


	trampoline(device, shaderModule, pAllocator);
}

VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateGraphicsPipelines, device_impl);

	return trampoline(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateComputePipelines, device_impl);

	return trampoline(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateRayTracingPipelinesKHR, device_impl);

	return trampoline(device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
void     VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator)
{
	if (pipeline == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyPipeline, device_impl);


	trampoline(device, pipeline, pAllocator);
}

VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreatePipelineLayout, device_impl);

	assert(pCreateInfo != nullptr && pPipelineLayout != nullptr);

	VkResult result = VK_SUCCESS;
	{
		result = trampoline(device, pCreateInfo, pAllocator, pPipelineLayout);
	}

	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreatePipelineLayout failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator)
{
	if (pipelineLayout == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyPipelineLayout, device_impl);


	trampoline(device, pipelineLayout, pAllocator);
}

VkResult VKAPI_CALL vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateSampler, device_impl);

	assert(pCreateInfo != nullptr && pSampler != nullptr);


	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pSampler);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateSampler failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator)
{
	if (sampler == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroySampler, device_impl);


	trampoline(device, sampler, pAllocator);
}

VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateDescriptorSetLayout, device_impl);

	assert(pCreateInfo != nullptr && pSetLayout != nullptr);

	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pSetLayout);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateDescriptorSetLayout failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator)
{
	if (descriptorSetLayout == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyDescriptorSetLayout, device_impl);


	trampoline(device, descriptorSetLayout, pAllocator);
}

VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateDescriptorPool, device_impl);

	assert(pCreateInfo != nullptr && pDescriptorPool != nullptr);

	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pDescriptorPool);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateDescriptorPool failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator)
{
	if (descriptorPool == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyDescriptorPool, device_impl);


	trampoline(device, descriptorPool, pAllocator);
}

VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(ResetDescriptorPool, device_impl);


	return trampoline(device, descriptorPool, flags);
}
VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(AllocateDescriptorSets, device_impl);

	assert(pAllocateInfo != nullptr && pDescriptorSets != nullptr);

	const VkResult result = trampoline(device, pAllocateInfo, pDescriptorSets);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkAllocateDescriptorSets failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(FreeDescriptorSets, device_impl);

	assert(pDescriptorSets != nullptr);


	return trampoline(device, descriptorPool, descriptorSetCount, pDescriptorSets);
}

void     VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet *pDescriptorCopies)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(UpdateDescriptorSets, device_impl);


	trampoline(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}

VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateFramebuffer, device_impl);

	assert(pCreateInfo != nullptr && pFramebuffer != nullptr);

	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pFramebuffer);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateFramebuffer failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator)
{
	if (framebuffer == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyFramebuffer, device_impl);


	trampoline(device, framebuffer, pAllocator);
}

VkResult VKAPI_CALL vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateRenderPass, device_impl);

	assert(pCreateInfo != nullptr && pRenderPass != nullptr);

#if 0
	// Hack that prevents artifacts when trying to use depth buffer after render pass finished (see also comment in 'on_begin_render_pass_with_depth_stencil' in generic depth add-on)
	for (uint32_t a = 0; a < pCreateInfo->attachmentCount; ++a)
		if (pCreateInfo->pAttachments[a].storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE)
			const_cast<VkAttachmentDescription &>(pCreateInfo->pAttachments[a]).storeOp = VK_ATTACHMENT_STORE_OP_STORE;
#endif

	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pRenderPass);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateRenderPass failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateRenderPass2, device_impl);

	assert(pCreateInfo != nullptr && pRenderPass != nullptr);

	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pRenderPass);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateRenderPass2 failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator)
{
	if (renderPass == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyRenderPass, device_impl);


	trampoline(device, renderPass, pAllocator);
}

VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(AllocateCommandBuffers, device_impl);

	const VkResult result = trampoline(device, pAllocateInfo, pCommandBuffers);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkAllocateCommandBuffers failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(FreeCommandBuffers, device_impl);

	assert(pCommandBuffers != nullptr);


	trampoline(device, commandPool, commandBufferCount, pCommandBuffers);
}

VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(VkDevice device, const VkAccelerationStructureCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkAccelerationStructureKHR *pAccelerationStructure)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(CreateAccelerationStructureKHR, device_impl);

	assert(pCreateInfo != nullptr && pAccelerationStructure != nullptr);


	const VkResult result = trampoline(device, pCreateInfo, pAllocator, pAccelerationStructure);
	if (result < VK_SUCCESS)
	{
#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::warning, "vkCreateAccelerationStructureKHR failed with error code %d.", static_cast<int>(result));
#endif
		return result;
	}


	return result;
}
void     VKAPI_CALL vkDestroyAccelerationStructureKHR(VkDevice device, VkAccelerationStructureKHR accelerationStructure, const VkAllocationCallbacks *pAllocator)
{
	if (accelerationStructure == VK_NULL_HANDLE)
		return;

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(DestroyAccelerationStructureKHR, device_impl);


	trampoline(device, accelerationStructure, pAllocator);
}

VkResult VKAPI_CALL vkAcquireFullScreenExclusiveModeEXT(VkDevice device, VkSwapchainKHR swapchain)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(AcquireFullScreenExclusiveModeEXT, device_impl);


	return trampoline(device, swapchain);
}
VkResult VKAPI_CALL vkReleaseFullScreenExclusiveModeEXT(VkDevice device, VkSwapchainKHR swapchain)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(device));
	GET_DISPATCH_PTR_FROM(ReleaseFullScreenExclusiveModeEXT, device_impl);


	return trampoline(device, swapchain);
}
