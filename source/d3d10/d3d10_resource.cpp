/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "d3d10_device.hpp"
#include "d3d10_resource.hpp"
#include "com_utils.hpp"
#include "hook_manager.hpp"

void STDMETHODCALLTYPE ID3D10Resource_GetDevice(ID3D10Resource *pResource, ID3D10Device **ppDevice)
{
	reshade::hooks::call(ID3D10Resource_GetDevice, reshade::hooks::vtable_from_instance(pResource) + 3)(pResource, ppDevice);

	const auto device = *ppDevice;
	assert(device != nullptr);

	const auto device_proxy = get_private_pointer_d3dx<D3D10Device>(device);
	if (device_proxy != nullptr)
	{
		assert(device != device_proxy);

		*ppDevice = device_proxy;
		InterlockedIncrement(&device_proxy->_ref);
	}
}
