/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "d3d11_device.hpp"
#include "d3d11_device_context.hpp"
#include "d3d11on12_device.hpp"
#include "d3d11_resource.hpp"
#include "d3d11_impl_type_convert.hpp"
#include "dll_log.hpp" // Include late to get 'hr_to_string' helper function
#include "com_utils.hpp"
#include "hook_manager.hpp"

using reshade::d3d11::to_handle;

D3D11Device::D3D11Device(IDXGIDevice1 *original_dxgi_device, ID3D11Device *original) :
	DXGIDevice(original_dxgi_device), device_impl(original)
{
	assert(_orig != nullptr);

	// Add proxy object to the private data of the device, so that it can be retrieved again when only the original device is available (as is the case in the OpenVR hooks)
	D3D11Device *const device_proxy = this;
	_orig->SetPrivateData(__uuidof(D3D11Device), sizeof(device_proxy), &device_proxy);

}
D3D11Device::~D3D11Device()
{

	// Remove pointer to this proxy object from the private data of the device (in case the device unexpectedly survives)
	_orig->SetPrivateData(__uuidof(D3D11Device), 0, nullptr);
}

bool D3D11Device::check_and_upgrade_interface(REFIID riid)
{
	static constexpr IID iid_lookup[] = {
		__uuidof(ID3D11Device),
		__uuidof(ID3D11Device1),
		__uuidof(ID3D11Device2),
		__uuidof(ID3D11Device3),
		__uuidof(ID3D11Device4),
		__uuidof(ID3D11Device5),
	};

	for (unsigned short version = 0; version < ARRAYSIZE(iid_lookup); ++version)
	{
		if (riid != iid_lookup[version])
			continue;

		if (version > _interface_version)
		{
			IUnknown *new_interface = nullptr;
			if (FAILED(_orig->QueryInterface(riid, reinterpret_cast<void **>(&new_interface))))
				return false;
#if RESHADE_VERBOSE_LOG
			reshade::log::message(reshade::log::level::debug, "Upgrading ID3D11Device%hu object %p to ID3D11Device%hu.", _interface_version, static_cast<ID3D11Device *>(this), version);
#endif
			_orig->Release();
			_orig = static_cast<ID3D11Device *>(new_interface);
			_interface_version = version;
		}

		return true;
	}

	return false;
}

HRESULT STDMETHODCALLTYPE D3D11Device::QueryInterface(REFIID riid, void **ppvObj)
{
	if (ppvObj == nullptr)
		return E_POINTER;

	if (riid == __uuidof(this))
	{
		AddRef();
		*ppvObj = this;
		return S_OK;
	}

	if (check_and_upgrade_interface(riid))
	{
		// The Microsoft Media Foundation library unfortunately checks that the device pointers of the different D3D11 video interfaces it uses match
		// Since the D3D11 video interfaces ('ID3D11VideoContext' etc.) are not hooked, they return a pointer to the original device when queried via 'GetDevice', rather than this proxy one
		// To make things work, return a pointer to the original device here too, but only when video support is enabled and therefore it is possible this device was created by the Microsoft Media Foundation library
		if (_orig->GetCreationFlags() & D3D11_CREATE_DEVICE_VIDEO_SUPPORT)
		{
			_orig->AddRef();
			*ppvObj = _orig;
		}
		else
		{
			AddRef();
			*ppvObj = static_cast<ID3D11Device *>(this);
		}
		return S_OK;
	}

	// Note: Objects must have an identity, so use DXGIDevice for IID_IUnknown
	// See https://docs.microsoft.com/windows/desktop/com/rules-for-implementing-queryinterface
	if (DXGIDevice::check_and_upgrade_interface(riid))
	{
		AddRef();
		*ppvObj = static_cast<IDXGIDevice1 *>(this);
		return S_OK;
	}

	if (riid == __uuidof(ID3D11On12Device) ||
		riid == __uuidof(ID3D11On12Device1) ||
		riid == __uuidof(ID3D11On12Device2))
	{
		if (_d3d11on12_device != nullptr)
			return _d3d11on12_device->QueryInterface(riid, ppvObj);
	}

	// Unimplemented interfaces:
	//   ID3D11Debug        {79CF2233-7536-4948-9D36-1E4692DC5760}
	//   ID3D11InfoQueue    {6543DBB6-1B48-42F5-AB82-E97EC74326F6}
	//   ID3D11Multithread  {9B7E4E00-342C-4106-A19F-4F2704F689F0}
	//   ID3D11VideoDevice  {10EC4D5B-975A-4689-B9E4-D0AAC30FE333}
	//   ID3D11VideoDevice1 {29DA1D51-1321-4454-804B-F5FC9F861F0F}
	//   ID3D11VideoDevice2 {59C0CB01-35F0-4A70-8F67-87905C906A53}

	return _orig->QueryInterface(riid, ppvObj);
}
ULONG   STDMETHODCALLTYPE D3D11Device::AddRef()
{
	_orig->AddRef();
	return InterlockedIncrement(&_ref);
}
ULONG   STDMETHODCALLTYPE D3D11Device::Release()
{
	const ULONG ref = InterlockedDecrement(&_ref);
	if (ref != 0)
	{
		_orig->Release();
		return ref;
	}

	if (_d3d11on12_device != nullptr)
	{
		// Release the reference that was added when the D3D11on12 device was first queried in 'D3D11On12CreateDevice'
		_d3d11on12_device->_orig->Release();
		delete _d3d11on12_device;
	}

	// Release the reference that was added by 'GetImmediateContext' in 'D3D11CreateDeviceAndSwapChain'
	assert(_immediate_context != nullptr && _immediate_context->_ref == 1);
	_immediate_context->_orig->Release();
	delete _immediate_context;

	const auto orig = _orig;
	const auto interface_version = _interface_version;
#if RESHADE_VERBOSE_LOG
	reshade::log::message(
		reshade::log::level::debug,
		"Destroying ID3D11Device%hu object %p (%p) and IDXGIDevice%hu object %p (%p).",
		interface_version, static_cast<ID3D11Device *>(this), orig, DXGIDevice::_interface_version, static_cast<IDXGIDevice1 *>(this), DXGIDevice::_orig);
#endif
	delete this;

	// Note: At this point the immediate context should have been deleted by the release above (so do not access it)
	const ULONG ref_orig = orig->Release();
	if (ref_orig != 0) // Verify internal reference count
		reshade::log::message(reshade::log::level::warning, "Reference count for ID3D11Device%hu object %p (%p) is inconsistent (%lu).", interface_version, static_cast<ID3D11Device *>(this), orig, ref_orig);
	return 0;
}

HRESULT STDMETHODCALLTYPE D3D11Device::CreateBuffer(const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer)
{
	if (pDesc == nullptr)
		return E_INVALIDARG;
	if (ppBuffer == nullptr) // This can happen when application only wants to validate input parameters
		return _orig->CreateBuffer(pDesc, pInitialData, ppBuffer);


	const HRESULT hr = _orig->CreateBuffer(pDesc, pInitialData, ppBuffer);
	if (SUCCEEDED(hr))
	{
		ID3D11Buffer *const resource = *ppBuffer;

		reshade::hooks::install("ID3D11Buffer::GetDevice", reshade::hooks::vtable_from_instance(resource), 3, reinterpret_cast<reshade::hook::address>(&ID3D11Resource_GetDevice));

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateBuffer failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture1D(const D3D11_TEXTURE1D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture1D **ppTexture1D)
{
	if (pDesc == nullptr)
		return E_INVALIDARG;
	if (ppTexture1D == nullptr) // This can happen when application only wants to validate input parameters
		return _orig->CreateTexture1D(pDesc, pInitialData, ppTexture1D);


	const HRESULT hr = _orig->CreateTexture1D(pDesc, pInitialData, ppTexture1D);
	if (SUCCEEDED(hr))
	{
		ID3D11Texture1D *const resource = *ppTexture1D;

		reshade::hooks::install("ID3D11Texture1D::GetDevice", reshade::hooks::vtable_from_instance(resource), 3, reinterpret_cast<reshade::hook::address>(&ID3D11Resource_GetDevice));

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateTexture1D failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D)
{
	if (pDesc == nullptr)
		return E_INVALIDARG;
	if (ppTexture2D == nullptr) // This can happen when application only wants to validate input parameters
		return _orig->CreateTexture2D(pDesc, pInitialData, ppTexture2D);


	const HRESULT hr = _orig->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
	if (SUCCEEDED(hr))
	{
		ID3D11Texture2D *const resource = *ppTexture2D;

		reshade::hooks::install("ID3D11Texture2D::GetDevice", reshade::hooks::vtable_from_instance(resource), 3, reinterpret_cast<reshade::hook::address>(&ID3D11Resource_GetDevice));

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateTexture2D failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D **ppTexture3D)
{
	if (pDesc == nullptr)
		return E_INVALIDARG;
	if (ppTexture3D == nullptr) // This can happen when application only wants to validate input parameters
		return _orig->CreateTexture3D(pDesc, pInitialData, ppTexture3D);


	const HRESULT hr = _orig->CreateTexture3D(pDesc, pInitialData, ppTexture3D);
	if (SUCCEEDED(hr))
	{
		ID3D11Texture3D *const resource = *ppTexture3D;

		reshade::hooks::install("ID3D11Texture3D::GetDevice", reshade::hooks::vtable_from_instance(resource), 3, reinterpret_cast<reshade::hook::address>(&ID3D11Resource_GetDevice));

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateTexture3D failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateShaderResourceView(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D11ShaderResourceView **ppShaderResourceView)
{

	const HRESULT hr = _orig->CreateShaderResourceView(pResource, pDesc, ppShaderResourceView);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateShaderResourceView failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateUnorderedAccessView(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc, ID3D11UnorderedAccessView **ppUnorderedAccessView)
{

	const HRESULT hr = _orig->CreateUnorderedAccessView(pResource, pDesc, ppUnorderedAccessView);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateUnorderedAccessView failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateRenderTargetView(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc, ID3D11RenderTargetView **ppRenderTargetView)
{

	const HRESULT hr = _orig->CreateRenderTargetView(pResource, pDesc, ppRenderTargetView);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateRenderTargetView failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateDepthStencilView(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView)
{

	const HRESULT hr = _orig->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateDepthStencilView failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout **ppInputLayout)
{

	const HRESULT hr = _orig->CreateInputLayout(pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateInputLayout failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11VertexShader **ppVertexShader)
{

	const HRESULT hr = _orig->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateVertexShader failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader)
{

	const HRESULT hr = _orig->CreateGeometryShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppGeometryShader);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateGeometryShader failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateGeometryShaderWithStreamOutput(const void *pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY *pSODeclaration, UINT NumEntries, const UINT *pBufferStrides, UINT NumStrides, UINT RasterizedStream, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader)
{

	const HRESULT hr = _orig->CreateGeometryShaderWithStreamOutput(pShaderBytecode, BytecodeLength, pSODeclaration, NumEntries, pBufferStrides, NumStrides, RasterizedStream, pClassLinkage, ppGeometryShader);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateGeometryShaderWithStreamOutput failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11PixelShader **ppPixelShader)
{

	const HRESULT hr = _orig->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreatePixelShader failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11HullShader **ppHullShader)
{

	const HRESULT hr = _orig->CreateHullShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppHullShader);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateHullShader failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11DomainShader **ppDomainShader)
{

	const HRESULT hr = _orig->CreateDomainShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppDomainShader);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateDomainShader failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11ComputeShader **ppComputeShader)
{

	const HRESULT hr = _orig->CreateComputeShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateComputeShader failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateClassLinkage(ID3D11ClassLinkage **ppLinkage)
{
	return _orig->CreateClassLinkage(ppLinkage);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateBlendState(const D3D11_BLEND_DESC *pBlendStateDesc, ID3D11BlendState **ppBlendState)
{

	const HRESULT hr = _orig->CreateBlendState(pBlendStateDesc, ppBlendState);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateBlendState failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D11DepthStencilState **ppDepthStencilState)
{

	const HRESULT hr = _orig->CreateDepthStencilState(pDepthStencilDesc, ppDepthStencilState);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateDepthStencilState failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateRasterizerState(const D3D11_RASTERIZER_DESC *pRasterizerDesc, ID3D11RasterizerState **ppRasterizerState)
{

	const HRESULT hr = _orig->CreateRasterizerState(pRasterizerDesc, ppRasterizerState);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateRasterizerState failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateSamplerState(const D3D11_SAMPLER_DESC *pSamplerDesc, ID3D11SamplerState **ppSamplerState)
{

	const HRESULT hr = _orig->CreateSamplerState(pSamplerDesc, ppSamplerState);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateSamplerState failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateQuery(const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery)
{
	return _orig->CreateQuery(pQueryDesc, ppQuery);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreatePredicate(const D3D11_QUERY_DESC *pPredicateDesc, ID3D11Predicate **ppPredicate)
{
	return _orig->CreatePredicate(pPredicateDesc, ppPredicate);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateCounter(const D3D11_COUNTER_DESC *pCounterDesc, ID3D11Counter **ppCounter)
{
	return _orig->CreateCounter(pCounterDesc, ppCounter);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext(UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext)
{
	reshade::log::message(reshade::log::level::info, "Redirecting ID3D11Device::CreateDeferredContext(this = %p, ContextFlags = %u, ppDeferredContext = %p) ...", this, ContextFlags, ppDeferredContext);

	if (ppDeferredContext == nullptr)
		return E_INVALIDARG;

	const HRESULT hr = _orig->CreateDeferredContext(ContextFlags, ppDeferredContext);
	if (SUCCEEDED(hr))
	{
		const auto device_context_proxy = new D3D11DeviceContext(this, *ppDeferredContext);
		*ppDeferredContext = device_context_proxy;

#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::debug, "Returning ID3D11DeviceContext object %p (%p).", device_context_proxy, device_context_proxy->_orig);
#endif
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::CreateDeferredContext failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void **ppResource)
{
	const HRESULT hr = _orig->OpenSharedResource(hResource, ReturnedInterface, ppResource);
	if (SUCCEEDED(hr))
	{
		assert(ppResource != nullptr);

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device::OpenSharedResource failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CheckFormatSupport(DXGI_FORMAT Format, UINT *pFormatSupport)
{
	return _orig->CheckFormatSupport(Format, pFormatSupport);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels)
{
	return _orig->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
}
void    STDMETHODCALLTYPE D3D11Device::CheckCounterInfo(D3D11_COUNTER_INFO *pCounterInfo)
{
	_orig->CheckCounterInfo(pCounterInfo);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CheckCounter(const D3D11_COUNTER_DESC *pDesc, D3D11_COUNTER_TYPE *pType, UINT *pActiveCounters, LPSTR szName, UINT *pNameLength, LPSTR szUnits, UINT *pUnitsLength, LPSTR szDescription, UINT *pDescriptionLength)
{
	return _orig->CheckCounter(pDesc, pType, pActiveCounters, szName, pNameLength, szUnits, pUnitsLength, szDescription, pDescriptionLength);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CheckFeatureSupport(D3D11_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize)
{
	return _orig->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}
HRESULT STDMETHODCALLTYPE D3D11Device::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
	return _orig->GetPrivateData(guid, pDataSize, pData);
}
HRESULT STDMETHODCALLTYPE D3D11Device::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
	return _orig->SetPrivateData(guid, DataSize, pData);
}
HRESULT STDMETHODCALLTYPE D3D11Device::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
	return _orig->SetPrivateDataInterface(guid, pData);
}
UINT    STDMETHODCALLTYPE D3D11Device::GetCreationFlags()
{
	return _orig->GetCreationFlags();
}
HRESULT STDMETHODCALLTYPE D3D11Device::GetDeviceRemovedReason()
{
	return _orig->GetDeviceRemovedReason();
}
void    STDMETHODCALLTYPE D3D11Device::GetImmediateContext(ID3D11DeviceContext **ppImmediateContext)
{
	assert(ppImmediateContext != nullptr);
	assert(_immediate_context != nullptr);

	_immediate_context->AddRef();
	*ppImmediateContext = _immediate_context;
}
HRESULT STDMETHODCALLTYPE D3D11Device::SetExceptionMode(UINT RaiseFlags)
{
	return _orig->SetExceptionMode(RaiseFlags);
}
UINT    STDMETHODCALLTYPE D3D11Device::GetExceptionMode()
{
	return _orig->GetExceptionMode();
}
D3D_FEATURE_LEVEL STDMETHODCALLTYPE D3D11Device::GetFeatureLevel()
{
	return _orig->GetFeatureLevel();
}

void    STDMETHODCALLTYPE D3D11Device::GetImmediateContext1(ID3D11DeviceContext1 **ppImmediateContext)
{
	assert(_interface_version >= 1);
	assert(ppImmediateContext != nullptr);
	assert(_immediate_context != nullptr);

	// Upgrade immediate context to interface version 1
	_immediate_context->check_and_upgrade_interface(__uuidof(**ppImmediateContext));

	_immediate_context->AddRef();
	*ppImmediateContext = _immediate_context;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext1(UINT ContextFlags, ID3D11DeviceContext1 **ppDeferredContext)
{
	assert(_interface_version >= 1);

	reshade::log::message(reshade::log::level::info, "Redirecting ID3D11Device1::CreateDeferredContext1(this = %p, ContextFlags = %u, ppDeferredContext = %p) ...", this, ContextFlags, ppDeferredContext);

	if (ppDeferredContext == nullptr)
		return E_INVALIDARG;

	const HRESULT hr = static_cast<ID3D11Device1 *>(_orig)->CreateDeferredContext1(ContextFlags, ppDeferredContext);
	if (SUCCEEDED(hr))
	{
		const auto device_context_proxy = new D3D11DeviceContext(this, *ppDeferredContext);
		*ppDeferredContext = device_context_proxy;

#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::debug, "Returning ID3D11DeviceContext1 object %p (%p).", device_context_proxy, device_context_proxy->_orig);
#endif
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device1::CreateDeferredContext1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateBlendState1(const D3D11_BLEND_DESC1 *pBlendStateDesc, ID3D11BlendState1 **ppBlendState)
{
	assert(_interface_version >= 1);


	const HRESULT hr = static_cast<ID3D11Device1 *>(_orig)->CreateBlendState1(pBlendStateDesc, ppBlendState);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device1::CreateBlendState1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateRasterizerState1(const D3D11_RASTERIZER_DESC1 *pRasterizerDesc, ID3D11RasterizerState1 **ppRasterizerState)
{
	assert(_interface_version >= 1);


	const HRESULT hr = static_cast<ID3D11Device1 *>(_orig)->CreateRasterizerState1(pRasterizerDesc, ppRasterizerState);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device1::CreateRasterizerState1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeviceContextState(UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, REFIID EmulatedInterface, D3D_FEATURE_LEVEL *pChosenFeatureLevel, ID3DDeviceContextState **ppContextState)
{
	assert(_interface_version >= 1);
	return static_cast<ID3D11Device1 *>(_orig)->CreateDeviceContextState(Flags, pFeatureLevels, FeatureLevels, SDKVersion, EmulatedInterface, pChosenFeatureLevel, ppContextState);
}
HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResource1(HANDLE hResource, REFIID returnedInterface, void **ppResource)
{
	assert(_interface_version >= 1);
	const HRESULT hr = static_cast<ID3D11Device1 *>(_orig)->OpenSharedResource1(hResource, returnedInterface, ppResource);
	if (SUCCEEDED(hr))
	{
		assert(ppResource != nullptr);

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device1::OpenSharedResource1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResourceByName(LPCWSTR lpName, DWORD dwDesiredAccess, REFIID returnedInterface, void **ppResource)
{
	assert(_interface_version >= 1);
	const HRESULT hr = static_cast<ID3D11Device1 *>(_orig)->OpenSharedResourceByName(lpName, dwDesiredAccess, returnedInterface, ppResource);
	if (SUCCEEDED(hr))
	{
		assert(ppResource != nullptr);

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device1::OpenSharedResourceByName failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}

void    STDMETHODCALLTYPE D3D11Device::GetImmediateContext2(ID3D11DeviceContext2 **ppImmediateContext)
{
	assert(_interface_version >= 2);
	assert(ppImmediateContext != nullptr);
	assert(_immediate_context != nullptr);

	// Upgrade immediate context to interface version 2
	_immediate_context->check_and_upgrade_interface(__uuidof(**ppImmediateContext));

	_immediate_context->AddRef();
	*ppImmediateContext = _immediate_context;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext2(UINT ContextFlags, ID3D11DeviceContext2 **ppDeferredContext)
{
	assert(_interface_version >= 2);

	reshade::log::message(reshade::log::level::info, "Redirecting ID3D11Device2::CreateDeferredContext2(this = %p, ContextFlags = %u, ppDeferredContext = %p) ...", this, ContextFlags, ppDeferredContext);

	if (ppDeferredContext == nullptr)
		return E_INVALIDARG;

	const HRESULT hr = static_cast<ID3D11Device2 *>(_orig)->CreateDeferredContext2(ContextFlags, ppDeferredContext);
	if (SUCCEEDED(hr))
	{
		const auto device_context_proxy = new D3D11DeviceContext(this, *ppDeferredContext);
		*ppDeferredContext = device_context_proxy;

#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::debug, "Returning ID3D11DeviceContext2 object %p (%p).", device_context_proxy, device_context_proxy->_orig);
#endif
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device2::CreateDeferredContext2 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
void    STDMETHODCALLTYPE D3D11Device::GetResourceTiling(ID3D11Resource *pTiledResource, UINT *pNumTilesForEntireResource, D3D11_PACKED_MIP_DESC *pPackedMipDesc, D3D11_TILE_SHAPE *pStandardTileShapeForNonPackedMips, UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D11_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips)
{
	assert(_interface_version >= 2);
	static_cast<ID3D11Device2 *>(_orig)->GetResourceTiling(pTiledResource, pNumTilesForEntireResource, pPackedMipDesc, pStandardTileShapeForNonPackedMips, pNumSubresourceTilings, FirstSubresourceTilingToGet, pSubresourceTilingsForNonPackedMips);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CheckMultisampleQualityLevels1(DXGI_FORMAT Format, UINT SampleCount, UINT Flags, UINT *pNumQualityLevels)
{
	assert(_interface_version >= 2);
	return static_cast<ID3D11Device2 *>(_orig)->CheckMultisampleQualityLevels1(Format, SampleCount, Flags, pNumQualityLevels);
}

HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture2D1(const D3D11_TEXTURE2D_DESC1 *pDesc1, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D1 **ppTexture2D)
{
	assert(_interface_version >= 3);

	if (pDesc1 == nullptr)
		return E_INVALIDARG;
	if (ppTexture2D == nullptr) // This can happen when application only wants to validate input parameters
		return static_cast<ID3D11Device3 *>(_orig)->CreateTexture2D1(pDesc1, pInitialData, ppTexture2D);


	const HRESULT hr = static_cast<ID3D11Device3 *>(_orig)->CreateTexture2D1(pDesc1, pInitialData, ppTexture2D);
	if (SUCCEEDED(hr))
	{
		ID3D11Texture2D1 *const resource = *ppTexture2D;

		reshade::hooks::install("ID3D11Texture2D1::GetDevice", reshade::hooks::vtable_from_instance(resource), 3, reinterpret_cast<reshade::hook::address>(&ID3D11Resource_GetDevice));

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device3::CreateTexture2D1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture3D1(const D3D11_TEXTURE3D_DESC1 *pDesc1, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D1 **ppTexture3D)
{
	assert(_interface_version >= 3);

	if (pDesc1 == nullptr)
		return E_INVALIDARG;
	if (ppTexture3D == nullptr) // This can happen when application only wants to validate input parameters
		return static_cast<ID3D11Device3 *>(_orig)->CreateTexture3D1(pDesc1, pInitialData, ppTexture3D);


	const HRESULT hr = static_cast<ID3D11Device3 *>(_orig)->CreateTexture3D1(pDesc1, pInitialData, ppTexture3D);
	if (SUCCEEDED(hr))
	{
		ID3D11Texture3D1 *const resource = *ppTexture3D;

		reshade::hooks::install("ID3D11Texture3D1::GetDevice", reshade::hooks::vtable_from_instance(resource), 3, reinterpret_cast<reshade::hook::address>(&ID3D11Resource_GetDevice));

	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device3::CreateTexture3D1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateRasterizerState2(const D3D11_RASTERIZER_DESC2 *pRasterizerDesc, ID3D11RasterizerState2 **ppRasterizerState)
{
	assert(_interface_version >= 3);


	const HRESULT hr = static_cast<ID3D11Device3 *>(_orig)->CreateRasterizerState2(pRasterizerDesc, ppRasterizerState);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device3::CreateRasterizerState2 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateShaderResourceView1(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc1, ID3D11ShaderResourceView1 **ppShaderResourceView1)
{
	assert(_interface_version >= 3);


	const HRESULT hr = static_cast<ID3D11Device3 *>(_orig)->CreateShaderResourceView1(pResource, pDesc1, ppShaderResourceView1);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device3::CreateShaderResourceView1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateUnorderedAccessView1(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc1, ID3D11UnorderedAccessView1 **ppUnorderedAccessView1)
{
	assert(_interface_version >= 3);


	const HRESULT hr = static_cast<ID3D11Device3 *>(_orig)->CreateUnorderedAccessView1(pResource, pDesc1, ppUnorderedAccessView1);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device3::CreateUnorderedAccessView1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateRenderTargetView1(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc1, ID3D11RenderTargetView1 **ppRenderTargetView1)
{
	assert(_interface_version >= 3);


	const HRESULT hr = static_cast<ID3D11Device3 *>(_orig)->CreateRenderTargetView1(pResource, pDesc1, ppRenderTargetView1);
	if (SUCCEEDED(hr))
	{
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device3::CreateRenderTargetView1 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateQuery1(const D3D11_QUERY_DESC1 *pQueryDesc1, ID3D11Query1 **ppQuery1)
{
	assert(_interface_version >= 3);
	return static_cast<ID3D11Device3 *>(_orig)->CreateQuery1(pQueryDesc1, ppQuery1);
}
void    STDMETHODCALLTYPE D3D11Device::GetImmediateContext3(ID3D11DeviceContext3 **ppImmediateContext)
{
	assert(_interface_version >= 3);
	assert(ppImmediateContext != nullptr);
	assert(_immediate_context != nullptr);

	// Upgrade immediate context to interface version 3
	_immediate_context->check_and_upgrade_interface(__uuidof(**ppImmediateContext));

	_immediate_context->AddRef();
	*ppImmediateContext = _immediate_context;
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext3(UINT ContextFlags, ID3D11DeviceContext3 **ppDeferredContext)
{
	assert(_interface_version >= 3);

	reshade::log::message(reshade::log::level::info, "Redirecting ID3D11Device3::CreateDeferredContext3(this = %p, ContextFlags = %u, ppDeferredContext = %p) ...", this, ContextFlags, ppDeferredContext);

	if (ppDeferredContext == nullptr)
		return E_INVALIDARG;

	const HRESULT hr = static_cast<ID3D11Device3 *>(_orig)->CreateDeferredContext3(ContextFlags, ppDeferredContext);
	if (SUCCEEDED(hr))
	{
		const auto device_context_proxy = new D3D11DeviceContext(this, *ppDeferredContext);
		*ppDeferredContext = device_context_proxy;

#if RESHADE_VERBOSE_LOG
		reshade::log::message(reshade::log::level::debug, "Returning ID3D11DeviceContext3 object %p (%p).", device_context_proxy, device_context_proxy->_orig);
#endif
	}
#if RESHADE_VERBOSE_LOG
	else
	{
		reshade::log::message(reshade::log::level::warning, "ID3D11Device3::CreateDeferredContext3 failed with error code %s.", reshade::log::hr_to_string(hr).c_str());
	}
#endif

	return hr;
}
void    STDMETHODCALLTYPE D3D11Device::WriteToSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
	assert(_interface_version >= 3);
	static_cast<ID3D11Device3 *>(_orig)->WriteToSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}
void    STDMETHODCALLTYPE D3D11Device::ReadFromSubresource(void *pDstData, UINT DstRowPitch, UINT DstDepthPitch, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
	assert(_interface_version >= 3);
	static_cast<ID3D11Device3 *>(_orig)->ReadFromSubresource(pDstData, DstRowPitch, DstDepthPitch, pSrcResource, SrcSubresource, pSrcBox);
}

HRESULT STDMETHODCALLTYPE D3D11Device::RegisterDeviceRemovedEvent(HANDLE hEvent, DWORD *pdwCookie)
{
	assert(_interface_version >= 4);
	return static_cast<ID3D11Device4 *>(_orig)->RegisterDeviceRemovedEvent(hEvent, pdwCookie);
}
void    STDMETHODCALLTYPE D3D11Device::UnregisterDeviceRemoved(DWORD dwCookie)
{
	assert(_interface_version >= 4);
	static_cast<ID3D11Device4 *>(_orig)->UnregisterDeviceRemoved(dwCookie);
}

HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedFence(HANDLE hFence, REFIID ReturnedInterface, void **ppFence)
{
	assert(_interface_version >= 5);
	return static_cast<ID3D11Device5 *>(_orig)->OpenSharedFence(hFence, ReturnedInterface, ppFence);
}
HRESULT STDMETHODCALLTYPE D3D11Device::CreateFence(UINT64 InitialValue, D3D11_FENCE_FLAG Flags, REFIID ReturnedInterface, void **ppFence)
{
	assert(_interface_version >= 5);
	return static_cast<ID3D11Device5 *>(_orig)->CreateFence(InitialValue, Flags, ReturnedInterface, ppFence);
}
