#include <Windows.h>
#include <VersionHelpers.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>

#include "Kr/KrPrelude.h"
#include "Kr/KrMediaNative.h"
#include "RenderBackend.h"

#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "DXGI.lib")

static const char *R_DXGIErrorString(HRESULT hr) {
	switch (hr) {
	case DXGI_ERROR_ACCESS_DENIED: return "You tried to use a resource to which you did not have the required access privileges. This error is most typically caused when you write to a shared resource with read-only access.";
	case DXGI_ERROR_ACCESS_LOST: return "The desktop duplication interface is invalid. The desktop duplication interface typically becomes invalid when a different type of image is displayed on the desktop.";
	case DXGI_ERROR_ALREADY_EXISTS: return "The desired element already exists. This is returned by DXGIDeclareAdapterRemovalSupport if it is not the first time that the function is called.";
	case DXGI_ERROR_CANNOT_PROTECT_CONTENT: return "DXGI can't provide content protection on the swap chain. This error is typically caused by an older driver, or when you use a swap chain that is incompatible with content protection.";
	case DXGI_ERROR_DEVICE_HUNG: return "The application's device failed due to badly formed commands sent by the application. This is an design-time issue that should be investigated and fixed.";
	case DXGI_ERROR_DEVICE_REMOVED: return "The video card has been physically removed from the system, or a driver upgrade for the video card has occurred. The application should destroy and recreate the device. For help debugging the problem, call ID3D10Device::GetDeviceRemovedReason.";
	case DXGI_ERROR_DEVICE_RESET: return "The device failed due to a badly formed command. This is a run-time issue; The application should destroy and recreate the device.";
	case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "The driver encountered a problem and was put into the device removed state.";
	case DXGI_ERROR_FRAME_STATISTICS_DISJOINT: return "An event (for example, a power cycle) interrupted the gathering of presentation statistics.";
	case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE: return "The application attempted to acquire exclusive ownership of an output, but failed because some other application (or device within the application) already acquired ownership.";
	case DXGI_ERROR_INVALID_CALL: return "The application provided invalid parameter data; this must be debugged and fixed before the application is released.";
	case DXGI_ERROR_MORE_DATA: return "The buffer supplied by the application is not big enough to hold the requested data.";
	case DXGI_ERROR_NAME_ALREADY_EXISTS: return "The supplied name of a resource in a call to IDXGIResource1::CreateSharedHandle is already associated with some other resource.";
	case DXGI_ERROR_NONEXCLUSIVE: return "A global counter resource is in use, and the Direct3D device can't currently use the counter resource.";
	case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: return "The resource or request is not currently available, but it might become available later.";
	case DXGI_ERROR_NOT_FOUND: return "When calling IDXGIObject::GetPrivateData, the GUID passed in is not recognized as one previously passed to IDXGIObject::SetPrivateData or IDXGIObject::SetPrivateDataInterface. When calling IDXGIFactory::EnumAdapters or IDXGIAdapter::EnumOutputs, the enumerated ordinal is out of range.";
	case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE: return "The DXGI output (monitor) to which the swap chain content was restricted is now disconnected or changed.";
	case DXGI_ERROR_SDK_COMPONENT_MISSING: return "The operation depends on an SDK component that is missing or mismatched.";
	case DXGI_ERROR_SESSION_DISCONNECTED: return "The Remote Desktop Services session is currently disconnected.";
	case DXGI_ERROR_UNSUPPORTED: return "The requested functionality is not supported by the device or the driver.";
	case DXGI_ERROR_WAIT_TIMEOUT: return "The time-out interval elapsed before the next desktop frame was available.";
	case DXGI_ERROR_WAS_STILL_DRAWING: return "The GPU was busy at the moment when a call was made to perform an operation, and did not execute or schedule the operation.";
	}
	return "Unknown error";
}

static void R_ReportDXGIError_(HRESULT hr, const char *file, int line, const char *proc) {
	const char *msg = R_DXGIErrorString(hr);
	LogErrorEx("DXGI", "%s(%d) in procedure \"%s\": %s", file, line, proc, msg);
}

static const char *R_D3D11ErrorString(HRESULT hr) {
	switch (hr) {
	case D3D11_ERROR_FILE_NOT_FOUND: return "The file was not found.";
	case D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS: return "There are too many unique instances of a particular type of state object.";
	case D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS: return "There are too many unique instances of a particular type of view object.";
	case D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD: return "The first call to ID3D11DeviceContext::Map after either ID3D11Device::CreateDeferredContext or ID3D11DeviceContext::FinishCommandList per Resource was not D3D11_MAP_WRITE_DISCARD.";
	case DXGI_ERROR_INVALID_CALL: return "The method call is invalid. For example, a method's parameter may not be a valid pointer.";
	case DXGI_ERROR_WAS_STILL_DRAWING: return "The previous blit operation that is transferring information to or from this surface is incomplete.";
	case E_FAIL: return "Attempted to create a device with the debug layer enabled and the layer is not installed.";
	case E_INVALIDARG: return "An invalid parameter was passed to the returning function.";
	case E_OUTOFMEMORY: return "Direct3D could not allocate sufficient memory to complete the call.";
	case E_NOTIMPL: return "The method call isn't implemented with the passed parameter combination.";
	}
	return "Source of error is unknown";
}

static void R_ReportD3D11Error_(HRESULT hr, const char *file, int line, const char *proc) {
	const char *msg = R_D3D11ErrorString(hr);
	LogErrorEx("D3D11", "%s(%d) in procedure \"%s\": %s", file, line, proc, msg);
}

#define R_ReportDXGIError(hr)  R_ReportDXGIError_(hr, __FILE__, __LINE__, __PROCEDURE__)
#define R_ReportD3D11Error(hr) R_ReportD3D11Error_(hr, __FILE__, __LINE__, __PROCEDURE__)

//
//
//

#define R_RENDER_API

static constexpr UINT BufferCount = 2;

static constexpr D3D_FEATURE_LEVEL FeatureLevels[] = {
	D3D_FEATURE_LEVEL_11_1,
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
	D3D_FEATURE_LEVEL_9_3,
	D3D_FEATURE_LEVEL_9_2,
	D3D_FEATURE_LEVEL_9_1,
};

static IDXGIFactory2 *Factory;

//
//
//

static IDXGIAdapter1 *R_FindAdapter(IDXGIFactory2 *factory, UINT flags) {
	IDXGIAdapter1 *adapter = nullptr;
	size_t         max_dedicated_memory = 0;
	IDXGIAdapter1 *adapter_it = 0;

	uint32_t it_index = 0;
	while (factory->EnumAdapters1(it_index, &adapter_it) != DXGI_ERROR_NOT_FOUND) {
		it_index += 1;

		DXGI_ADAPTER_DESC1 desc;
		adapter_it->GetDesc1(&desc);

		HRESULT hr = D3D11CreateDevice(adapter_it, D3D_DRIVER_TYPE_UNKNOWN, 0,
			flags, FeatureLevels, ArrayCount(FeatureLevels),
			D3D11_SDK_VERSION, NULL, NULL, NULL);

		if (SUCCEEDED(hr) && desc.DedicatedVideoMemory > max_dedicated_memory) {
			max_dedicated_memory = desc.DedicatedVideoMemory;
			adapter = adapter_it;
		} else {
			adapter_it->Release();
		}
	}

	return adapter;
}

//
//
//

R_RENDER_API R_Device *R_CreateDevice(uint32_t device_flags) {
	bool debug = (device_flags & R_DEVICE_DEBUG_ENABLE);

	UINT flags = 0;
#ifdef BUILD_DEBUG
	flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	if (!Factory) {
		HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&Factory));
		if (FAILED(hr)) {
			R_ReportDXGIError(hr);
			return nullptr;
		}
	}

	IDXGIAdapter1 *adapter = R_FindAdapter(Factory, flags);

	if (adapter == nullptr) {
		LogError("DirectX 11 supported Adapter not present!");
		return nullptr;
	}

	Defer{ if (adapter) adapter->Release(); };

	ID3D11Device1 *device1;

	{
		ID3D11Device *device = nullptr;
		HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, debug ? D3D11_CREATE_DEVICE_DEBUG : 0,
			FeatureLevels, ArrayCount(FeatureLevels), D3D11_SDK_VERSION, &device, nullptr, nullptr);
		Assert(hr != E_INVALIDARG);

		if (FAILED(hr)) {
			R_ReportD3D11Error(hr);
			return nullptr;
		}

		hr = device->QueryInterface(IID_PPV_ARGS(&device1));
		Assert(SUCCEEDED(hr));
		device->Release();
	}

	if (debug) {
		ID3D11Debug *debug = 0;

		if (SUCCEEDED(device1->QueryInterface(IID_PPV_ARGS(&debug)))) {
			ID3D11InfoQueue *info_queue = nullptr;
			if (SUCCEEDED(device1->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
				LogInfoEx("DirectX", "ID3D11Debug enabled.");

				info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
				info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);

				D3D11_MESSAGE_ID hide[] = {
					D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS };

				D3D11_INFO_QUEUE_FILTER filter = {};
				filter.DenyList.NumIDs = ArrayCount(hide);
				filter.DenyList.pIDList = hide;
				info_queue->AddStorageFilterEntries(&filter);

				info_queue->Release();
			}
		}
		debug->Release();
	}

	return (R_Device *)device1;
}

R_RENDER_API void R_DestroyDevice(R_Device *device) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;
	device1->Release();
}

struct R_Swap_Chain {
	IDXGISwapChain1 *       native;
	ID3D11RenderTargetView *render_target;
	uint32_t                sync_interval;
	uint32_t                render_target_w;
	uint32_t                render_target_h;
};

R_RENDER_API R_Swap_Chain *R_CreateSwapChain(R_Device *device, PL_Window *window) {
	HWND hwnd = PL_GetNativeHandle(window);

	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	R_Swap_Chain *r_swap_chain = (R_Swap_Chain *)MemAlloc(sizeof(R_Swap_Chain));
	if (!r_swap_chain) {
		LogErrorEx("DirectX11", "SwapChain creation failed. Reason: Out of memory.");
		return nullptr;
	}

	DXGI_SWAP_EFFECT swap_effect =
		IsWindows10OrGreater() ? DXGI_SWAP_EFFECT_FLIP_DISCARD :
		IsWindows8OrGreater() ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL :
		DXGI_SWAP_EFFECT_DISCARD;

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.Width              = 0;
	swap_chain_desc.Height             = 0;
	swap_chain_desc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
	swap_chain_desc.SampleDesc.Count   = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.BufferCount        = BufferCount;
	swap_chain_desc.SwapEffect         = swap_effect;

	HRESULT hr = Factory->CreateSwapChainForHwnd(device1, hwnd, &swap_chain_desc, nullptr, nullptr, &r_swap_chain->native);
	Assert(hr != DXGI_ERROR_INVALID_CALL);
	if (FAILED(hr)) {
		R_ReportDXGIError(hr);
		MemFree(r_swap_chain, sizeof(*r_swap_chain));
		return nullptr;
	}

	ID3D11Texture2D *back_buffer = nullptr;
	hr = r_swap_chain->native->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
	if (FAILED(hr)) {
		r_swap_chain->native->Release();
		R_ReportDXGIError(hr);
		MemFree(r_swap_chain, sizeof(*r_swap_chain));
		return nullptr;
	}

	Defer{ back_buffer->Release(); };

	hr = device1->CreateRenderTargetView(back_buffer, NULL, &r_swap_chain->render_target);
	if (FAILED(hr)) {
		r_swap_chain->native->Release();
		R_ReportDXGIError(hr);
		MemFree(r_swap_chain, sizeof(*r_swap_chain));
		return nullptr;
	}


	r_swap_chain->sync_interval = 1;

	D3D11_TEXTURE2D_DESC render_target_desc;
	back_buffer->GetDesc(&render_target_desc);
	r_swap_chain->render_target_w = render_target_desc.Width;
	r_swap_chain->render_target_h = render_target_desc.Height;

	return r_swap_chain;
}

void R_DestroySwapChain(R_Device *device, R_Swap_Chain *swap_chain) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	ID3D11DeviceContext1 *imm;
	device1->GetImmediateContext1(&imm);

	imm->Flush();
	imm->ClearState();

	swap_chain->native->Release();
	swap_chain->render_target->Release();

	imm->Release();

	MemFree(swap_chain, sizeof(*swap_chain));
}

void R_SetSyncInterval(R_Swap_Chain *swap_chain, uint32_t interval) {
	swap_chain->sync_interval = interval;
}

R_Render_Target *R_GetSwapChainRenderTarget(R_Swap_Chain *swap_chain) {
	return (R_Render_Target *)swap_chain->render_target;
}

void R_GetSwapChainRenderTargetSize(R_Swap_Chain *swap_chain, uint32_t *w, uint32_t *h) {
	*w = swap_chain->render_target_w;
	*h = swap_chain->render_target_h;
}

void R_Present(R_Swap_Chain *swap_chain) {
	swap_chain->native->Present(swap_chain->sync_interval, 0);
}

void R_GetRenderTargetSize(R_Render_Target *render_target, uint32_t *w, uint32_t *h) {
	ID3D11RenderTargetView *view = (ID3D11RenderTargetView *)render_target;

	ID3D11Texture2D *texture;
	view->GetResource((ID3D11Resource **)&texture);

	D3D11_TEXTURE2D_DESC render_target_desc;
	texture->GetDesc(&render_target_desc);
	*w = render_target_desc.Width;
	*h = render_target_desc.Height;
}
