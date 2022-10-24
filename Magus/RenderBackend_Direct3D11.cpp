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

static constexpr D3D11_USAGE BufferUsageMap[] = { D3D11_USAGE_DEFAULT, D3D11_USAGE_IMMUTABLE, D3D11_USAGE_DYNAMIC, D3D11_USAGE_STAGING };
static_assert(ArrayCount(BufferUsageMap) == _R_BUFFER_USAGE_COUNT, "");

static constexpr DXGI_FORMAT FormatMap[] = {
	DXGI_FORMAT_R32G32B32A32_FLOAT,
	DXGI_FORMAT_R32G32B32A32_SINT,
	DXGI_FORMAT_R32G32B32A32_UINT,
	DXGI_FORMAT_R16G16B16A16_FLOAT,
	DXGI_FORMAT_R8G8B8A8_UNORM,
	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
	DXGI_FORMAT_R32G32B32_FLOAT,
	DXGI_FORMAT_R32G32B32_SINT,
	DXGI_FORMAT_R32G32B32_UINT,
	DXGI_FORMAT_R32G32_FLOAT,
	DXGI_FORMAT_R32G32_SINT,
	DXGI_FORMAT_R32G32_UINT,
	DXGI_FORMAT_R8G8_UNORM,
	DXGI_FORMAT_R32_FLOAT,
	DXGI_FORMAT_R32_SINT,
	DXGI_FORMAT_R32_UINT,
	DXGI_FORMAT_R16_UINT,
	DXGI_FORMAT_R8_UNORM,
};
static_assert(ArrayCount(FormatMap) == _R_FORMAT_COUNT, "");

static constexpr D3D11_COMPARISON_FUNC ComparisonMap[] = {
	D3D11_COMPARISON_NEVER,
	D3D11_COMPARISON_LESS,
	D3D11_COMPARISON_EQUAL,
	D3D11_COMPARISON_LESS_EQUAL,
	D3D11_COMPARISON_GREATER,
	D3D11_COMPARISON_NOT_EQUAL,
	D3D11_COMPARISON_GREATER_EQUAL,
	D3D11_COMPARISON_ALWAYS,
};
static_assert(ArrayCount(ComparisonMap) == _R_COMPARISON_COUNT, "");

static constexpr D3D11_FILTER FilterMap[] = {
	D3D11_FILTER_MIN_MAG_MIP_POINT,
	D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR,
	D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR,
	D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,
	D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
	D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_MIN_MAG_MIP_LINEAR,
	D3D11_FILTER_ANISOTROPIC,
	D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
	D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR,
	D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR,
	D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT,
	D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
	D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
	D3D11_FILTER_COMPARISON_ANISOTROPIC,
	D3D11_FILTER_MINIMUM_MIN_MAG_MIP_POINT,
	D3D11_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR,
	D3D11_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR,
	D3D11_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT,
	D3D11_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
	D3D11_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR,
	D3D11_FILTER_MINIMUM_ANISOTROPIC,
	D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT,
	D3D11_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR,
	D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR,
	D3D11_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT,
	D3D11_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
	D3D11_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR,
	D3D11_FILTER_MAXIMUM_ANISOTROPIC,
};
static_assert(ArrayCount(FilterMap) == _R_FILTER_COUNT, "");

static constexpr D3D11_TEXTURE_ADDRESS_MODE TextureAddressModeMap[] = {
	D3D11_TEXTURE_ADDRESS_WRAP,
	D3D11_TEXTURE_ADDRESS_MIRROR,
	D3D11_TEXTURE_ADDRESS_CLAMP,
	D3D11_TEXTURE_ADDRESS_BORDER,
	D3D11_TEXTURE_ADDRESS_MIRROR_ONCE,
};
static_assert(ArrayCount(TextureAddressModeMap) == _R_TEXTURE_ADDRESS_MODE_COUNT, "");

static constexpr D3D11_INPUT_CLASSIFICATION ClassificationMap[] = { D3D11_INPUT_PER_VERTEX_DATA, D3D11_INPUT_PER_INSTANCE_DATA };
static_assert(ArrayCount(ClassificationMap) == _R_INPUT_CLASSIFICATION_COUNT, "");

static constexpr D3D11_BLEND BlendTypeMap[] = {
	D3D11_BLEND_ZERO,
	D3D11_BLEND_ONE,
	D3D11_BLEND_SRC_COLOR,
	D3D11_BLEND_INV_SRC_COLOR,
	D3D11_BLEND_SRC_ALPHA,
	D3D11_BLEND_INV_SRC_ALPHA,
	D3D11_BLEND_DEST_ALPHA,
	D3D11_BLEND_INV_DEST_ALPHA,
	D3D11_BLEND_DEST_COLOR,
	D3D11_BLEND_INV_DEST_COLOR,
	D3D11_BLEND_SRC_ALPHA_SAT,
	D3D11_BLEND_BLEND_FACTOR,
	D3D11_BLEND_INV_BLEND_FACTOR,
	D3D11_BLEND_SRC1_COLOR,
	D3D11_BLEND_INV_SRC1_COLOR,
	D3D11_BLEND_SRC1_ALPHA,
	D3D11_BLEND_INV_SRC1_ALPHA
};
static_assert(ArrayCount(BlendTypeMap) == _R_BLEND_TYPE_COUNT, "");

static constexpr D3D11_BLEND_OP BlendOpMap[] = {
	D3D11_BLEND_OP_ADD,
	D3D11_BLEND_OP_SUBTRACT,
	D3D11_BLEND_OP_REV_SUBTRACT,
	D3D11_BLEND_OP_MIN,
	D3D11_BLEND_OP_MAX
};
static_assert(ArrayCount(BlendOpMap) == _R_BLEND_OP_COUNT, "");

static constexpr D3D11_STENCIL_OP StencilOpMap[] = {
	D3D11_STENCIL_OP_KEEP,
	D3D11_STENCIL_OP_ZERO,
	D3D11_STENCIL_OP_REPLACE,
	D3D11_STENCIL_OP_INCR_SAT,
	D3D11_STENCIL_OP_DECR_SAT,
	D3D11_STENCIL_OP_INVERT,
	D3D11_STENCIL_OP_INCR,
	D3D11_STENCIL_OP_DECR,
};
static_assert(ArrayCount(StencilOpMap) == _R_STENCIL_OP_COUNT, "");

static constexpr D3D11_DEPTH_WRITE_MASK DepthWriteMaskMap[] = {
	D3D11_DEPTH_WRITE_MASK_ZERO,
	D3D11_DEPTH_WRITE_MASK_ALL,
};
static_assert(ArrayCount(DepthWriteMaskMap) == _R_DEPTH_WRITE_MASK_COUNT, "");

static constexpr D3D11_FILL_MODE FillModeMap[] = {
	D3D11_FILL_SOLID,
	D3D11_FILL_WIREFRAME,
};
static_assert(ArrayCount(FillModeMap) == _R_FILL_MODE_COUNT, "");

static constexpr D3D11_CULL_MODE CullModeMap[] = {
	D3D11_CULL_NONE,
	D3D11_CULL_FRONT,
	D3D11_CULL_BACK,
};
static_assert(ArrayCount(CullModeMap) == _R_CULL_MODE_COUNT, "");

static constexpr D3D11_PRIMITIVE_TOPOLOGY PrimitiveTopologyMap[] = {
	D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,
	D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
};
static_assert(ArrayCount(PrimitiveTopologyMap) == _R_PRIMITIVE_TOPOLOGY_COUNT, "");

//
//
//

static UINT ConvertBufferFlags(uint32_t access_flags) {
	UINT flags = 0;
	if (access_flags & R_BUFFER_CPU_WRITE_ACCESS) flags |= D3D11_CPU_ACCESS_WRITE;
	if (access_flags & R_BUFFER_CPU_READ_ACCESS)  flags |= D3D11_CPU_ACCESS_WRITE;
	return flags;
}

static D3D11_DEPTH_STENCIL_DESC ConvertDepthStencilDesc(const R_Depth_Stencil *src) {
	D3D11_DEPTH_STENCIL_DESC dst;
	dst.DepthEnable                  = src->depth.enable;
	dst.DepthWriteMask               = DepthWriteMaskMap[src->depth.write_mask];
	dst.DepthFunc                    = ComparisonMap[src->depth.comparison];
	dst.StencilEnable                = src->stencil.enable;
	dst.StencilReadMask              = src->stencil.read_mask;
	dst.StencilWriteMask             = src->stencil.write_mask;
	dst.FrontFace.StencilFailOp      = StencilOpMap[src->stencil.front_face.fail_op];
	dst.FrontFace.StencilDepthFailOp = StencilOpMap[src->stencil.front_face.depth_fail_op];
	dst.FrontFace.StencilPassOp      = StencilOpMap[src->stencil.front_face.pass_op];
	dst.FrontFace.StencilFunc        = ComparisonMap[src->stencil.front_face.comparison];
	dst.BackFace.StencilFailOp       = StencilOpMap[src->stencil.back_face.fail_op];
	dst.BackFace.StencilDepthFailOp  = StencilOpMap[src->stencil.back_face.depth_fail_op];
	dst.BackFace.StencilPassOp       = StencilOpMap[src->stencil.back_face.pass_op];
	dst.BackFace.StencilFunc         = ComparisonMap[src->stencil.back_face.comparison];
	return dst;
}

static uint8_t ConvertWriteMask(uint8_t mask) {
	uint8_t result = 0;
	if (mask & R_WRITE_MASK_RED) result |= D3D11_COLOR_WRITE_ENABLE_RED;
	if (mask & R_WRITE_MASK_GREEN) result |= D3D11_COLOR_WRITE_ENABLE_GREEN;
	if (mask & R_WRITE_MASK_BLUE) result |= D3D11_COLOR_WRITE_ENABLE_BLUE;
	if (mask & R_WRITE_MASK_ALPHA) result |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
	return result;
}


static D3D11_RASTERIZER_DESC ConvertRasterizerDesc(const R_Rasterizer *src) {
	D3D11_RASTERIZER_DESC dst;
	dst.FillMode              = FillModeMap[src->fill_mode];
	dst.CullMode              = CullModeMap[src->cull_mode];
	dst.FrontCounterClockwise = src->front_clockwise;
	dst.DepthBias             = src->depth_bias;
	dst.DepthBiasClamp        = src->depth_bias_clamp;
	dst.SlopeScaledDepthBias  = 0.0f;
	dst.DepthClipEnable       = src->depth_clip_enable;
	dst.ScissorEnable         = src->scissor_enable;
	dst.MultisampleEnable     = src->multisample_enable;
	dst.AntialiasedLineEnable = src->anti_aliased_line_enable;
	return dst;
}

static D3D11_SAMPLER_DESC ConvertSamplerDesc(const R_Sampler *src) {
	D3D11_SAMPLER_DESC dst;
	dst.Filter         = FilterMap[src->filter];
	dst.AddressU       = TextureAddressModeMap[src->address_u];
	dst.AddressV       = TextureAddressModeMap[src->address_v];
	dst.AddressW       = TextureAddressModeMap[src->address_w];
	dst.MipLODBias     = src->mip_lod_bias;
	dst.MaxAnisotropy  = src->max_anisotropy;
	dst.ComparisonFunc = ComparisonMap[src->comparison];
	dst.BorderColor[0] = src->border_color[0];
	dst.BorderColor[1] = src->border_color[1];
	dst.BorderColor[2] = src->border_color[2];
	dst.BorderColor[3] = src->border_color[3];
	dst.MinLOD         = src->min_lod;
	dst.MaxLOD         = src->max_lod;
	return dst;
}

static D3D11_BLEND_DESC ConvertBlendDesc(const R_Blend *src) {
	D3D11_BLEND_DESC dst;
	dst.AlphaToCoverageEnable  = FALSE;
	dst.IndependentBlendEnable = FALSE;

	static_assert(ArrayCount(src->render_target) == ArrayCount(dst.RenderTarget), "");

	for (uint32_t index = 0; index < ArrayCount(dst.RenderTarget); ++index) {
		auto& src_target = src->render_target[index];
		auto& dst_target = dst.RenderTarget[index];

		dst_target.BlendEnable           = src_target.enable;
		dst_target.SrcBlend              = BlendTypeMap[src_target.color.src];
		dst_target.DestBlend             = BlendTypeMap[src_target.color.dst];
		dst_target.BlendOp               = BlendOpMap[src_target.color.op];
		dst_target.SrcBlendAlpha         = BlendTypeMap[src_target.alpha.src];
		dst_target.DestBlendAlpha        = BlendTypeMap[src_target.alpha.dst];
		dst_target.BlendOpAlpha          = BlendOpMap[src_target.alpha.op];
		dst_target.RenderTargetWriteMask = ConvertWriteMask(src_target.write_mask);
	}

	return dst;
}

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

R_RENDER_API R_Queue *R_CreateRenderQueue(R_Device *device) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	ID3D11DeviceContext1 *imm;
	device1->GetImmediateContext1(&imm);

	return (R_Queue *)imm;
}

R_RENDER_API void R_DestroyRenderQueue(R_Queue *queue) {
	ID3D11DeviceContext1 *imm = (ID3D11DeviceContext1 *)queue;
	imm->Release();
}

R_RENDER_API void R_Submit(R_Queue *queue, R_List *list) {
	ID3D11DeviceContext1 *immediate_context = (ID3D11DeviceContext1 *)queue;
	ID3D11DeviceContext1 *deferred_context  = (ID3D11DeviceContext1 *)list;

	ID3D11CommandList *command_list = nullptr;
	deferred_context->FinishCommandList(false, &command_list);
	if (command_list) {
		immediate_context->ExecuteCommandList(command_list, false);
		command_list->Release();
	}
}

R_RENDER_API void R_Flush(R_Queue *queue) {
	ID3D11DeviceContext1 *immediate_context = (ID3D11DeviceContext1 *)queue;
	immediate_context->ClearState();
	immediate_context->Flush();
}

R_RENDER_API R_List *R_CreateRenderList(R_Device *device) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	ID3D11DeviceContext1 *deferred_context;
	device1->CreateDeferredContext1(0, &deferred_context);

	return (R_List *)deferred_context;
}

R_RENDER_API void R_DestroyRenderList(R_List *list) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	deferred_context->Release();
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
	swap_chain_desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
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

R_RENDER_API void R_DestroySwapChain(R_Device *device, R_Swap_Chain *swap_chain) {
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

R_RENDER_API void R_SetSyncInterval(R_Swap_Chain *swap_chain, uint32_t interval) {
	swap_chain->sync_interval = interval;
}

R_RENDER_API void R_ResizeRenderTargets(R_Device *device, R_Swap_Chain *swap_chain, uint32_t w, uint32_t h) {
	if (w && h) {
		swap_chain->render_target->Release();
		swap_chain->render_target = nullptr;
		swap_chain->render_target_w = 0;
		swap_chain->render_target_h = 0;

		swap_chain->native->ResizeBuffers(BufferCount, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

		ID3D11Texture2D *back_buffer = nullptr;
		HRESULT hr = swap_chain->native->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
		if (FAILED(hr)) {
			R_ReportDXGIError(hr);
			return;
		}

		Defer{ back_buffer->Release(); };

		ID3D11Device1 *device1 = (ID3D11Device1 *)device;

		hr = device1->CreateRenderTargetView(back_buffer, NULL, &swap_chain->render_target);
		if (FAILED(hr)) {
			R_ReportDXGIError(hr);
			return;
		}

		D3D11_TEXTURE2D_DESC render_target_desc;
		back_buffer->GetDesc(&render_target_desc);
		swap_chain->render_target_w = render_target_desc.Width;
		swap_chain->render_target_h = render_target_desc.Height;
	}
}

R_RENDER_API R_Render_Target *R_GetRenderTarget(R_Swap_Chain *swap_chain) {
	return (R_Render_Target *)swap_chain->render_target;
}

R_RENDER_API void R_GetRenderTargetSize(R_Swap_Chain *swap_chain, float *w, float *h) {
	*w = (float)swap_chain->render_target_w;
	*h = (float)swap_chain->render_target_h;
}

R_RENDER_API void R_Present(R_Swap_Chain *swap_chain) {
	swap_chain->native->Present(swap_chain->sync_interval, 0);
}

R_RENDER_API void R_RenderTargetSize(R_Render_Target *render_target, float *w, float *h) {
	ID3D11RenderTargetView *view = (ID3D11RenderTargetView *)render_target;

	ID3D11Texture2D *texture;
	view->GetResource((ID3D11Resource **)&texture);

	D3D11_TEXTURE2D_DESC render_target_desc;
	texture->GetDesc(&render_target_desc);
	*w = (float)render_target_desc.Width;
	*h = (float)render_target_desc.Height;

	texture->Release();
}

struct R_Pipeline {
	ID3D11VertexShader *     vertex_shader;
	ID3D11PixelShader *      pixel_shader;
	ID3D11InputLayout *      input_layout;
	ID3D11DepthStencilState *depth_stencil;
	ID3D11RasterizerState *  rasterizer;
	ID3D11SamplerState *     sampler;
	ID3D11BlendState *       blend;
};

R_RENDER_API R_Pipeline *R_CreatePipeline(R_Device *device, const R_Pipeline_Config &config) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	HRESULT hresult;

	R_Pipeline *p = (R_Pipeline *)MemAlloc(sizeof(*p));

	if (!p) {
		LogErrorEx("D3D11 Backend", "Pipeline creation failed. Reason: Out of memory");
		return nullptr;
	}

	Array_View<uint8_t> vertex = config.shaders[R_SHADER_VERTEX];
	hresult = device1->CreateVertexShader(config.shaders[R_SHADER_VERTEX].data, config.shaders[R_SHADER_VERTEX].count, nullptr, &p->vertex_shader);
	if (FAILED(hresult)) {
		R_DestroyPipeline(p);
		LogErrorEx("D3D11 Backend", "Pipeline creation failed. Reason: Failed to compile vertex shader");
		return nullptr;
	}

	Array_View<uint8_t> pixel = config.shaders[R_SHADER_PIXEL];
	hresult = device1->CreatePixelShader(config.shaders[R_SHADER_PIXEL].data, config.shaders[R_SHADER_PIXEL].count, nullptr, &p->pixel_shader);
	if (FAILED(hresult)) {
		R_DestroyPipeline(p);
		LogErrorEx("D3D11 Backend", "Pipeline creation failed. Reason: Failed to compile pixel shader");
		return nullptr;
	}

	D3D11_INPUT_ELEMENT_DESC input_elements[15];

	Assert(config.input_layout->count < ArrayCount(input_elements));

	for (ptrdiff_t index = 0; index < config.input_layout->count; ++index) {
		const R_Input_Layout_Element *src = &config.input_layout->data[index];
		D3D11_INPUT_ELEMENT_DESC *dst     = &input_elements[index];
		dst->SemanticName                 = src->name;
		dst->SemanticIndex                = src->index;
		dst->Format                       = FormatMap[src->format];
		dst->InputSlot                    = src->input;
		dst->AlignedByteOffset            = src->offset;
		dst->InputSlotClass               = ClassificationMap[src->classification];
		dst->InstanceDataStepRate         = src->instance_data_step_rate;
	}

	hresult = device1->CreateInputLayout(input_elements, (uint32_t)config.input_layout->count, vertex.data, vertex.count, &p->input_layout);
	if (FAILED(hresult)) {
		R_DestroyPipeline(p);
		LogErrorEx("D3D11 Backend", "Pipeline creation failed. Reason: Failed to create input layout");
		return nullptr;
	}

	D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = ConvertDepthStencilDesc(config.depth_stencil);
	hresult = device1->CreateDepthStencilState(&depth_stencil_desc, &p->depth_stencil);
	if (FAILED(hresult)) {
		R_DestroyPipeline(p);
		LogErrorEx("D3D11 Backend", "Pipeline creation failed. Reason: Failed to create depth stencil state");
		return nullptr;
	}

	D3D11_RASTERIZER_DESC rasterizer_desc = ConvertRasterizerDesc(config.rasterizer);
	hresult = device1->CreateRasterizerState(&rasterizer_desc, &p->rasterizer);
	if (FAILED(hresult)) {
		R_DestroyPipeline(p);
		LogErrorEx("D3D11 Backend", "Pipeline creation failed. Reason: Failed to create rasterizer");
		return nullptr;
	}

	D3D11_SAMPLER_DESC sampler_desc = ConvertSamplerDesc(config.sampler);
	hresult = device1->CreateSamplerState(&sampler_desc, &p->sampler);
	if (FAILED(hresult)) {
		R_DestroyPipeline(p);
		LogErrorEx("D3D11 Backend", "Pipeline creation failed. Reason: Failed to create sampler");
		return nullptr;
	}

	D3D11_BLEND_DESC blend_desc = ConvertBlendDesc(config.blend);
	hresult = device1->CreateBlendState(&blend_desc, &p->blend);
	if (FAILED(hresult)) {
		R_DestroyPipeline(p);
		LogErrorEx("D3D11 Backend", "Pipeline creation failed. Reason: Failed to create blend state");
		return nullptr;
	}

	return p;
}

R_RENDER_API void R_DestroyPipeline(R_Pipeline *pipeline) {
	if (pipeline->vertex_shader) pipeline->vertex_shader->Release();
	if (pipeline->pixel_shader)  pipeline->pixel_shader->Release();
	if (pipeline->input_layout)  pipeline->input_layout->Release();
	if (pipeline->depth_stencil) pipeline->depth_stencil->Release();
	if (pipeline->rasterizer)    pipeline->rasterizer->Release();
	if (pipeline->sampler)       pipeline->sampler->Release();
	if (pipeline->blend)         pipeline->blend->Release();
	MemFree(pipeline, sizeof(*pipeline));
}

R_RENDER_API R_Buffer *R_CreateVertexBuffer(R_Device *device, R_Buffer_Usage usage, uint32_t flags, uint32_t size, void *data) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	D3D11_BUFFER_DESC buffer_desc;
	buffer_desc.ByteWidth           = size;
	buffer_desc.Usage               = BufferUsageMap[usage];
	buffer_desc.BindFlags           = D3D11_BIND_VERTEX_BUFFER;
	buffer_desc.CPUAccessFlags      = ConvertBufferFlags(flags);
	buffer_desc.MiscFlags           = 0;
	buffer_desc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA initial_data;
	initial_data.pSysMem          = data;
	initial_data.SysMemPitch      = size;
	initial_data.SysMemSlicePitch = 0;

	ID3D11Buffer *buffer = nullptr;
	device1->CreateBuffer(&buffer_desc, data ? &initial_data : nullptr, &buffer);

	return (R_Buffer *)buffer;
}

R_RENDER_API R_Buffer *R_CreateIndexBuffer(R_Device *device, R_Buffer_Usage usage, uint32_t flags, uint32_t size, void *data) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	D3D11_BUFFER_DESC buffer_desc;
	buffer_desc.ByteWidth           = size;
	buffer_desc.Usage               = BufferUsageMap[usage];
	buffer_desc.BindFlags           = D3D11_BIND_INDEX_BUFFER;
	buffer_desc.CPUAccessFlags      = ConvertBufferFlags(flags);
	buffer_desc.MiscFlags           = 0;
	buffer_desc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA initial_data;
	initial_data.pSysMem          = data;
	initial_data.SysMemPitch      = size;
	initial_data.SysMemSlicePitch = 0;

	ID3D11Buffer *buffer = nullptr;
	device1->CreateBuffer(&buffer_desc, data ? &initial_data : nullptr, &buffer);

	return (R_Buffer *)buffer;
}

R_RENDER_API R_Buffer *R_CreateConstantBuffer(R_Device *device, R_Buffer_Usage usage, uint32_t flags, uint32_t size, void *data) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	D3D11_BUFFER_DESC buffer_desc;
	buffer_desc.ByteWidth           = size;
	buffer_desc.Usage               = BufferUsageMap[usage];
	buffer_desc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
	buffer_desc.CPUAccessFlags      = ConvertBufferFlags(flags);
	buffer_desc.MiscFlags           = 0;
	buffer_desc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA initial_data;
	initial_data.pSysMem          = data;
	initial_data.SysMemPitch      = size;
	initial_data.SysMemSlicePitch = 0;

	ID3D11Buffer *buffer = nullptr;
	device1->CreateBuffer(&buffer_desc, data ? &initial_data : nullptr, &buffer);

	return (R_Buffer *)buffer;
}

R_RENDER_API void R_DestroyBuffer(R_Buffer *buffer) {
	ID3D11Buffer *d3d11buffer = (ID3D11Buffer *)buffer;
	d3d11buffer->Release();
}

R_RENDER_API R_Texture *R_CreateTexture(R_Device *device, R_Format format, uint32_t width, uint32_t height, uint32_t pitch, const uint8_t *pixels, uint32_t flags) {
	ID3D11Device1 *device1 = (ID3D11Device1 *)device;

	bool mipmaps = (flags & R_TEXTURE_GEN_MIPMAPS);

	D3D11_TEXTURE2D_DESC desc;
	desc.Width              = width;
	desc.Height             = height;
	desc.MipLevels          = mipmaps ? 0 : 1;
	desc.ArraySize          = 1;
	desc.Format             = FormatMap[format];
	desc.SampleDesc.Quality = 0;
	desc.SampleDesc.Count   = 1;
	desc.Usage              = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags     = 0;
	desc.MiscFlags          = 0;

	D3D11_SUBRESOURCE_DATA texture_data;
	texture_data.pSysMem          = pixels;
	texture_data.SysMemPitch      = pitch;
	texture_data.SysMemSlicePitch = 0;

	ID3D11Texture2D *texture2d = nullptr;

	HRESULT hresult = device1->CreateTexture2D(&desc, &texture_data, &texture2d);

	if (FAILED(hresult))
		return nullptr;

	Defer{ texture2d->Release(); };

	D3D11_SHADER_RESOURCE_VIEW_DESC view_desc;
	view_desc.Format                    = desc.Format;
	view_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
	view_desc.Texture2D.MipLevels       = -1;
	view_desc.Texture2D.MostDetailedMip = 0;

	ID3D11ShaderResourceView *shader_resource_view = nullptr;
	hresult = device1->CreateShaderResourceView(texture2d, &view_desc, &shader_resource_view);
	if (FAILED(hresult))
		return nullptr;

	return (R_Texture *)shader_resource_view;
}

R_RENDER_API void R_DestroyTexture(R_Texture *texture) {
	ID3D11ShaderResourceView *shader_resource_view = (ID3D11ShaderResourceView *)texture;
	shader_resource_view->Release();
}

R_RENDER_API void *R_MapBuffer(R_List *list, R_Buffer *buffer) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;

	ID3D11Resource *resource = (ID3D11Resource *)buffer;

	D3D11_MAPPED_SUBRESOURCE mapped;

	HRESULT hresult = deferred_context->Map(resource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hresult)) {
		return mapped.pData;
	}

	return nullptr;
}

R_RENDER_API void R_UnmapBuffer(R_List *list, R_Buffer *buffer) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;

	ID3D11Resource *resource = (ID3D11Resource *)buffer;
	deferred_context->Unmap(resource, 0);
}

R_RENDER_API void R_ClearRenderTarget(R_List *list, R_Render_Target *render_target, const float color[4]) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	ID3D11RenderTargetView *render_target_view = (ID3D11RenderTargetView *)render_target;
	deferred_context->ClearRenderTargetView(render_target_view, color);
}

R_RENDER_API void R_BindPipeline(R_List *list, R_Pipeline *pipeline) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	deferred_context->IASetInputLayout(pipeline->input_layout);
	deferred_context->VSSetShader(pipeline->vertex_shader, nullptr, 0);
	deferred_context->PSSetShader(pipeline->pixel_shader, nullptr, 0);
	deferred_context->PSSetSamplers(0, 1, &pipeline->sampler);
	deferred_context->OMSetDepthStencilState(pipeline->depth_stencil, 0);
	deferred_context->OMSetBlendState(pipeline->blend, nullptr, 0xffffffff);
	deferred_context->RSSetState(pipeline->rasterizer);
}

R_RENDER_API void R_BindVertexBuffers(R_List *list, R_Buffer **buffer, uint32_t *stride, uint32_t *offset, uint32_t location, uint32_t count) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;

	static_assert(sizeof(UINT) == sizeof(uint32_t), "");

	deferred_context->IASetVertexBuffers(location, count, (ID3D11Buffer **)buffer, stride, offset);
}

R_RENDER_API void R_BindIndexBuffer(R_List *list, R_Buffer *buffer, R_Format format, uint32_t offset) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	ID3D11Buffer *buffer_handle = (ID3D11Buffer *)buffer;
	DXGI_FORMAT index_fmt = FormatMap[format];
	deferred_context->IASetIndexBuffer(buffer_handle, index_fmt, (UINT)offset);
}

R_RENDER_API void R_BindConstantBuffers(R_List *list, R_Shader shader, R_Buffer **buffer, uint32_t location, uint32_t count) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	if (shader == R_SHADER_VERTEX)
		deferred_context->VSSetConstantBuffers(location, count, (ID3D11Buffer **)buffer);
	else if (shader == R_SHADER_PIXEL)
		deferred_context->PSSetConstantBuffers(location, count, (ID3D11Buffer **)buffer);
	else
		Unreachable();
}

R_RENDER_API void R_BindTextures(R_List *list, R_Texture **texture, uint32_t location, uint32_t count) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	deferred_context->PSSetShaderResources(location, count, (ID3D11ShaderResourceView **)texture);
}

R_RENDER_API void R_BindRenderTargets(R_List *list, uint32_t count, R_Render_Target *render_targets[], R_Depth_Stencil *depth_stencil) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	deferred_context->OMSetRenderTargets(count, (ID3D11RenderTargetView **)render_targets, (ID3D11DepthStencilView *)depth_stencil);
}

R_RENDER_API void R_SetPrimitiveTopology(R_List *list, R_Primitive_Topology topology) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	D3D11_PRIMITIVE_TOPOLOGY prim_topology = PrimitiveTopologyMap[topology];
	deferred_context->IASetPrimitiveTopology(prim_topology);
}

R_RENDER_API void R_SetViewports(R_List *list, R_Viewport *viewports, uint32_t count) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;

	D3D11_VIEWPORT d3dviewports[8];
	ID3D11RenderTargetView *render_target_views[8] = {};

	Assert(count < ArrayCount(d3dviewports));

	deferred_context->OMGetRenderTargets(count, render_target_views, nullptr);

	for (uint32_t i = 0; i < count; ++i) {
		ID3D11RenderTargetView *render_target_view = render_target_views[i];
		Assert(render_target_view);

		float w, h;
		R_RenderTargetSize((R_Render_Target *)render_target_view, &w, &h);

		D3D11_VIEWPORT *viewport = &d3dviewports[i];
		viewport->TopLeftX       = viewports[i].x;
		viewport->TopLeftY       = h - viewports[i].y - viewports[i].height;
		viewport->Width          = viewports[i].width;
		viewport->Height         = viewports[i].height;
		viewport->MinDepth       = viewports[i].min_depth;
		viewport->MaxDepth       = viewports[i].max_depth;

		render_target_view->Release();
	}

	deferred_context->RSSetViewports(count, d3dviewports);
}

R_RENDER_API void R_SetScissors(R_List *list, R_Scissor *scissors, uint32_t count) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;

	D3D11_RECT rects[8];
	ID3D11RenderTargetView *render_target_views[8] = {};

	Assert(count < ArrayCount(rects));

	deferred_context->OMGetRenderTargets(count, render_target_views, nullptr);

	for (uint32_t i = 0; i < count; ++i) {
		ID3D11RenderTargetView *render_target_view = render_target_views[i];
		Assert(render_target_view);

		float w, h;
		R_RenderTargetSize((R_Render_Target *)render_target_view, &w, &h);

		D3D11_RECT *rect = &rects[i];
		rect->left       = (LONG)scissors[i].min_x;
		rect->right      = (LONG)scissors[i].max_x;
		rect->top        = (LONG)(h - scissors[i].max_y);
		rect->bottom     = (LONG)(h - scissors[i].min_y);

		render_target_view->Release();
	}

	deferred_context->RSSetScissorRects(count, rects);
}

R_RENDER_API void R_Draw(R_List *list, uint32_t vertex_count, uint32_t start_vertex_location) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	deferred_context->Draw(vertex_count, start_vertex_location);
}

R_RENDER_API void R_DrawIndexed(R_List *list, uint32_t index_count, uint32_t start_index_location, int32_t base_vertex_location) {
	ID3D11DeviceContext1 *deferred_context = (ID3D11DeviceContext1 *)list;
	deferred_context->DrawIndexed(index_count, start_index_location, (INT)base_vertex_location);
}
