/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "vulkan_hooks.hpp"
#include "vulkan_impl_device.hpp"
#include "hook_manager.hpp"
#include "lockfree_linear_map.hpp"
#include <cstring> // std::strcmp

extern lockfree_linear_map<void *, instance_dispatch_table, 16> g_vulkan_instances;
extern lockfree_linear_map<void *, reshade::vulkan::device_impl *, 8> g_vulkan_devices;

#define HOOK_PROC(name) \
	if (0 == std::strcmp(pName, "vk" #name)) \
		return reinterpret_cast<PFN_vkVoidFunction>(vk##name)
#define HOOK_PROC_OPTIONAL(name, suffix) \
	if (0 == std::strcmp(pName, "vk" #name #suffix) && g_vulkan_devices.at(dispatch_key_from_handle(device))->_dispatch_table.name != nullptr) \
		return reinterpret_cast<PFN_vkVoidFunction>(vk##name);

PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
	// The Vulkan loader gets the 'vkDestroyDevice' function from the device dispatch table
	HOOK_PROC(DestroyDevice);

	// Core 1_0

	// Core 1_3

	// VK_KHR_swapchain
	HOOK_PROC(CreateSwapchainKHR);
	HOOK_PROC(DestroySwapchainKHR);
	HOOK_PROC(AcquireNextImageKHR);
	HOOK_PROC(QueuePresentKHR);
	HOOK_PROC(AcquireNextImage2KHR);







	// Need to self-intercept as well, since some layers rely on this (e.g. Steam overlay)
	// See https://github.com/KhronosGroup/Vulkan-Loader/blob/master/loader/LoaderAndLayerInterface.md#layer-conventions-and-rules
	HOOK_PROC(GetDeviceProcAddr);

#ifdef RESHADE_TEST_APPLICATION
	static const auto trampoline = reshade::hooks::call(vkGetDeviceProcAddr);
#else
	if (device == VK_NULL_HANDLE)
		return nullptr;

	const auto trampoline = g_vulkan_devices.at(dispatch_key_from_handle(device))->_dispatch_table.GetDeviceProcAddr;
#endif
	return trampoline(device, pName);
}

PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
	HOOK_PROC(CreateInstance);
	HOOK_PROC(DestroyInstance);
	HOOK_PROC(CreateDevice);
	HOOK_PROC(DestroyDevice);

	// VK_KHR_surface
	HOOK_PROC(CreateWin32SurfaceKHR);
	HOOK_PROC(DestroySurfaceKHR);

#ifdef VK_EXT_tooling_info
	// VK_EXT_tooling_info
	HOOK_PROC(GetPhysicalDeviceToolPropertiesEXT);
#endif

	// Self-intercept here as well to stay consistent with 'vkGetDeviceProcAddr' implementation
	HOOK_PROC(GetInstanceProcAddr);

#ifdef RESHADE_TEST_APPLICATION
	static const auto trampoline = reshade::hooks::call(vkGetInstanceProcAddr);
#else
	if (instance == VK_NULL_HANDLE)
		return nullptr;

	const auto trampoline = g_vulkan_instances.at(dispatch_key_from_handle(instance)).GetInstanceProcAddr;
#endif
	return trampoline(instance, pName);
}

VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct)
{
	if (pVersionStruct == nullptr ||
		pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
		return VK_ERROR_INITIALIZATION_FAILED;

	pVersionStruct->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
	pVersionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
	pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
	pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;

	return VK_SUCCESS;
}
