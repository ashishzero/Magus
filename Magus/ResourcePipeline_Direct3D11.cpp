#include "Kr/KrMedia.h"
#include "Kr/KrPrelude.h"
#include "Kr/KrString.h"
#include "Kr/KrArray.h"

#include "RenderBackend.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

static constexpr char LogSource[] = "Resource Pipeline - HLSL";

struct Shader_Header {
	R_Blend         blend;
	R_Depth_Stencil depth_stencil;
	R_Rasterizer    rasterizer;
	R_Sampler       sampler;
};

typedef void(*Parse_Shader_Header_Property_Value_Proc)(String property, String value, Shader_Header *header);

static bool ParseBoolean(String property, String value) {
	if (value == "true") return true;
	if (value == "false") return false;
	LogWarningEx(LogSource, "Expected boolean for " StrFmt ". Ignoring...", StrArg(property));
	return false;
}

static void ParseDepthValue(String property, String value, Shader_Header *header) {
	if (ParseBoolean(property, value)) {
		header->depth_stencil.depth.enable     = true;
		header->depth_stencil.depth.write_mask = R_DEPTH_WRITE_MASK_ALL;
		header->depth_stencil.depth.comparison = R_COMPARISON_LESS_EQUAL;
	}
}

static void ParseFillValue(String property, String value, Shader_Header *header) {
	if (value == "solid")
		header->rasterizer.fill_mode = R_FILL_SOLID;
	else if (value == "wireframe")
		header->rasterizer.fill_mode = R_FILL_WIREFRAME;
	else
		LogWarningEx(LogSource, "Expected \"solid\" or \"wireframe\" but got \"" StrFmt "\" for property" StrFmt 
			". Ignoring...", StrArg(value), StrArg(property));
}

static void ParseCullValue(String property, String value, Shader_Header *header) {
	if (value == "none")
		header->rasterizer.cull_mode = R_CULL_NONE;
	else if (value == "front")
		header->rasterizer.cull_mode = R_CULL_FRONT;
	else if (value == "back")
		header->rasterizer.cull_mode = R_CULL_BACK;
	else
		LogWarningEx(LogSource, "Expected \"none\" or \"front\" or \"back\" but got \"" StrFmt "\" for property" StrFmt
			". Ignoring...", StrArg(value), StrArg(property));
}

static void ParseScissorValue(String property, String value, Shader_Header *header) {
	if (ParseBoolean(property, value)) {
		header->rasterizer.scissor_enable = true;
	}
}

static void ParseFrontFaceValue(String property, String value, Shader_Header *header) {
	if (value == "cw")
		header->rasterizer.front_clockwise = true;
	else if (value == "ccw")
		header->rasterizer.front_clockwise = false;
	else
		LogWarningEx(LogSource, "Expected \"cw\" or \"ccw\" but got \"" StrFmt "\" for property" StrFmt
			". Ignoring...", StrArg(value), StrArg(property));
}

static void ParseFilterValue(String property, String value, Shader_Header *header) {
	if (value == "linear")
		header->sampler.filter = R_FILTER_MIN_MAG_MIP_LINEAR;
	else if (value == "point")
		header->sampler.filter = R_FILTER_MIN_MAG_MIP_POINT;
	else
		LogWarningEx(LogSource, "Expected \"linear\" or \"point\" but got \"" StrFmt "\" for property" StrFmt
			". Ignoring...", StrArg(value), StrArg(property));
}

static void ParseShaderHeaderBlendIndexed(String property, int index, String value, Shader_Header *header) {
	if (ParseBoolean(property, value)) {
		header->blend.render_target[index].enable     = true;
		header->blend.render_target[index].color      = { R_BLEND_SRC_ALPHA, R_BLEND_INV_SRC_ALPHA, R_BLEND_OP_ADD };
		header->blend.render_target[index].alpha      = { R_BLEND_SRC_ALPHA, R_BLEND_INV_SRC_ALPHA, R_BLEND_OP_ADD };
		header->blend.render_target[index].write_mask = R_WRITE_MASK_ALL;
	}
}

static void ParseShaderHeaderBlend(String property, String value, Shader_Header *header) {
	uint8_t last_char = property[property.length - 1];
	int index = last_char - '0';
	ParseShaderHeaderBlendIndexed(property, index, value, header);
}

static const String HeaderPropertyList[] = {
	"Depth", "Fill", "Cull", "Scissor", "FrontFace", "Filter",
	"Blend0", "Blend1", "Blend2", "Blend3", "Blend4", "Blend5", "Blend6", "Blend7",
};

static const Parse_Shader_Header_Property_Value_Proc HeaderPropertyParse[] = {
	ParseDepthValue, ParseFillValue, ParseCullValue, ParseScissorValue, ParseFrontFaceValue, ParseFilterValue,
	ParseShaderHeaderBlend, ParseShaderHeaderBlend, ParseShaderHeaderBlend, ParseShaderHeaderBlend,
	ParseShaderHeaderBlend, ParseShaderHeaderBlend, ParseShaderHeaderBlend, ParseShaderHeaderBlend,
};

static_assert(ArrayCount(HeaderPropertyList) == ArrayCount(HeaderPropertyParse), "");

static bool ParseShaderHeaderField(String field, Shader_Header *header) {
	String property, value;
	if (!SplitString(field, '=', &property, &value)) {
		LogErrorEx(LogSource, "Expected property=value in the header fields.");
		return false;
	}

	property = TrimString(property);
	value    = TrimString(value);

	for (int i = 0; i < ArrayCount(HeaderPropertyList); ++i) {
		if (property == HeaderPropertyList[i]) {
			HeaderPropertyParse[i](HeaderPropertyList[i], value, header);
			return true;
		}
	}

	LogErrorEx(LogSource, "Unknow property name \"" StrFmt "\"", StrArg(property));

	return false;
}

static bool ParseShaderHeader(String header_str, Shader_Header *header) {
	header_str = TrimString(header_str);

	if (!StringStartsWith(header_str, "[[")) {
		LogErrorEx(LogSource, "Expected [[ at the start of shader header");
		return false;
	}

	if (!StringEndsWith(header_str, "]]")) {
		LogErrorEx(LogSource, "Expected ]] at the start of shader header");
		return false;
	}

	header_str = RemovePrefix(RemoveSuffix(header_str, 2), 2); // removing [[ and ]]
	header_str = TrimString(header_str);

	String part = header_str;
	String field, remaining;
	for (; SplitString(part, ',', &field, &remaining); part = remaining) {
		if (!ParseShaderHeaderField(field, header))
			return false;
	}
	if (!ParseShaderHeaderField(part, header))
		return false;

	return true;
}

static String BlobToString(ID3DBlob *b) {
	String str;
	str.data   = (uint8_t *)b->GetBufferPointer();
	str.length = (ptrdiff_t)b->GetBufferSize();
	return str;
}

R_Pipeline *Resource_LoadPipeline(M_Arena *arena, R_Device *device, String content, String path) {
	Shader_Header header;
	memset(&header, 0, sizeof(header));

	String code = content;
	String header_str, remaining;
	while (SplitString(code, '\n', &header_str, &remaining)) {
		if (!StringStartsWith(header_str, "[["))
			break;

		if (!ParseShaderHeader(header_str, &header))
			return nullptr;

		code = remaining;
	}

	HRESULT hresult;
	ID3DBlob *error = nullptr;

	ID3DBlob *vertex_binary = nullptr;
	hresult = D3DCompile(code.data, code.length, (char *)path.data, NULL, NULL, 
		"VertexMain", "vs_4_0", 0, 0, &vertex_binary, &error);
	if (FAILED(hresult)) {
		String message = String((uint8_t *)error->GetBufferPointer(), error->GetBufferSize());
		LogErrorEx(LogSource, "Failed to Vertex Shader compile: " StrFmt ". Reason " StrFmt, StrArg(path), StrArg(message));
		error->Release();
		return nullptr;
	}
	Defer{ vertex_binary->Release(); };

	ID3DBlob *pixel_binary = nullptr;
	hresult = D3DCompile(code.data, code.length, (char *)path.data, NULL, NULL, 
		"PixelMain", "ps_4_0", 0, 0, &pixel_binary, &error);
	if (FAILED(hresult)) {
		String message = String((uint8_t *)error->GetBufferPointer(), error->GetBufferSize());
		LogErrorEx(LogSource, "Failed to Pixel Shader compile: " StrFmt ". Reason " StrFmt, StrArg(path), StrArg(message));
		error->Release();
		return nullptr;
	}
	Defer{ pixel_binary->Release(); };

	ID3D11ShaderReflection *reflector = nullptr;
	hresult = D3DReflect(vertex_binary->GetBufferPointer(), vertex_binary->GetBufferSize(), 
		IID_ID3D11ShaderReflection, (void **)&reflector);
	if (FAILED(hresult)) {
		LogErrorEx(LogSource, "Failed to extract information from shader: " StrFmt, StrArg(path));
		return nullptr;
	}
	Defer{ reflector->Release(); };

	D3D11_SHADER_DESC shader_desc;
	reflector->GetDesc(&shader_desc);

	Array<R_Input_Layout_Element> input_elements(M_GetArenaAllocator(arena));

	uint32_t current_offset = 0;

	for (uint32_t i = 0; i < shader_desc.InputParameters; i++) {
		D3D11_SIGNATURE_PARAMETER_DESC param_desc;
		reflector->GetInputParameterDesc(i, &param_desc);

		R_Input_Layout_Element *elem = input_elements.Add();
		if (!elem) {
			LogErrorEx(LogSource, "Could not create pipeline: " StrFmt ". Reason: Out of temporary memory.", StrArg(path));
			return nullptr;
		}

		elem->name                    = param_desc.SemanticName;
		elem->index                   = param_desc.SemanticIndex;
		elem->input                   = 0;
		elem->offset                  = current_offset;
		elem->classification          = R_INPUT_CLASSIFICATION_PER_VERTEX;
		elem->instance_data_step_rate = 0;

		if (param_desc.Mask == 1) {
			if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elem->format = R_FORMAT_R32_UINT;
			else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elem->format = R_FORMAT_R32_SINT;
			else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elem->format = R_FORMAT_R32_FLOAT;
			else Unreachable();
			current_offset += sizeof(float) * 1;
		} else if (param_desc.Mask <= 3) {
			if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elem->format = R_FORMAT_RG32_UINT;
			else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elem->format = R_FORMAT_RG32_SINT;
			else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elem->format = R_FORMAT_RG32_FLOAT;
			else Unreachable();
			current_offset += sizeof(float) * 2;
		} else if (param_desc.Mask <= 7) {
			if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elem->format = R_FORMAT_RGB32_UINT;
			else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elem->format = R_FORMAT_RGB32_SINT;
			else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elem->format = R_FORMAT_RGB32_FLOAT;
			else Unreachable();
			current_offset += sizeof(float) * 3;
		} else if (param_desc.Mask <= 15) {
			if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) elem->format = R_FORMAT_RGBA32_UINT;
			else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) elem->format = R_FORMAT_RGBA32_SINT;
			else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elem->format = R_FORMAT_RGBA32_FLOAT;
			else Unreachable();
			current_offset += sizeof(float) * 4;
		} else {
			Unreachable();
		}

	}

	R_Input_Layout input_layout = input_elements;

	R_Pipeline_Config config = {};
	config.shaders[R_SHADER_VERTEX] = BlobToString(vertex_binary);
	config.shaders[R_SHADER_PIXEL]  = BlobToString(pixel_binary);
	config.input_layout             = &input_layout;
	config.blend                    = &header.blend;
	config.depth_stencil            = &header.depth_stencil;
	config.rasterizer               = &header.rasterizer;
	config.sampler                  = &header.sampler;

	R_Pipeline *pipeline = R_CreatePipeline(device, config);

	return pipeline;
}
