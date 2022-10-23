#pragma once
#include "Kr/KrPlatform.h"

struct PL_Window;

struct R_Device;
struct R_Queue;
struct R_List;
struct R_Swap_Chain;
struct R_Pipeline;
struct R_Texture;
struct R_Buffer;
struct R_Render_Target;

enum R_Device_Flags {
	R_DEVICE_DEBUG_ENABLE = 0x1
};

enum R_Texture_Flags {
	R_TEXTURE_GEN_MIPMAPS = 0x1
};

enum R_Buffer_Usage {
	R_BUFFER_USAGE_DEFAULT,
	R_BUFFER_USAGE_IMMUTABLE,
	R_BUFFER_USAGE_DYNAMIC,
	R_BUFFER_USAGE_STAGING,
	_R_BUFFER_USAGE_COUNT
};

enum R_Buffer_Flags {
	R_BUFFER_CPU_READ_ACCESS = 0x1,
	R_BUFFER_CPU_WRITE_ACCESS = 0x2
};

enum R_Shader {
	R_SHADER_VERTEX,
	R_SHADER_PIXEL,
	R_SHADER_COUNT
};

enum R_Format {
	R_FORMAT_RGBA32_FLOAT,
	R_FORMAT_RGBA16_FLOAT,
	R_FORMAT_RGBA8_UNORM,
	R_FORMAT_RGBA8_UNORM_SRGB,
	R_FORMAT_RGB32_FLOAT,
	R_FORMAT_RG32_FLOAT,
	R_FORMAT_RG8_UNORM,
	R_FORMAT_R32_FLOAT,
	R_FORMAT_R32_UINT,
	R_FORMAT_R16_UINT,
	R_FORMAT_R8_UNORM,

	_R_FORMAT_COUNT
};

enum R_Input_Classification {
	R_INPUT_CLASSIFICATION_PER_VERTEX,
	R_INPUT_CLASSIFICATION_PER_INSTANCE,
	_R_INPUT_CLASSIFICATION_COUNT
};

struct R_Input_Layout_Element {
	const char *            name;
	uint32_t                index;
	R_Format                format;
	uint32_t                input;
	uint32_t                offset;
	R_Input_Classification  classification;
	uint32_t                instance_data_step_rate;
};

typedef Array_View<R_Input_Layout_Element> R_Input_Layout;

enum R_Blend_Type {
	R_BLEND_ZERO,
	R_BLEND_ONE,
	R_BLEND_SRC_COLOR,
	R_BLEND_INV_SRC_COLOR,
	R_BLEND_SRC_ALPHA,
	R_BLEND_INV_SRC_ALPHA,
	R_BLEND_DEST_ALPHA,
	R_BLEND_INV_DEST_ALPHA,
	R_BLEND_DEST_COLOR,
	R_BLEND_INV_DEST_COLOR,
	R_BLEND_SRC_ALPHA_SAT,
	R_BLEND_BLEND_FACTOR,
	R_BLEND_INV_BLEND_FACTOR,
	R_BLEND_SRC1_COLOR,
	R_BLEND_INV_SRC1_COLOR,
	R_BLEND_SRC1_ALPHA,
	R_BLEND_INV_SRC1_ALPHA,
	_R_BLEND_TYPE_COUNT
};

enum R_Blend_Op {
	R_BLEND_OP_ADD,
	R_BLEND_OP_SUBTRACT,
	R_BLEND_OP_REV_SUBTRACT,
	R_BLEND_OP_MIN,
	R_BLEND_OP_MAX,
	_R_BLEND_OP_COUNT
};

enum R_Write_Mask {
	R_WRITE_MASK_RED   = 1,
	R_WRITE_MASK_GREEN = 2,
	R_WRITE_MASK_BLUE  = 4,
	R_WRITE_MASK_ALPHA = 8,
	R_WRITE_MASK_ALL   = 0xff
};

struct R_Blend_Desc {
	struct Desc {
		R_Blend_Type src;
		R_Blend_Type dst;
		R_Blend_Op   op;
	};
	Desc         color;
	Desc         alpha;
	R_Write_Mask write_mask;
	bool         enable;
};

struct R_Blend {
	R_Blend_Desc render_target[8];
};

enum R_Stencil_Op {
	R_STENCIL_OP_KEEP,
	R_STENCIL_OP_ZERO,
	R_STENCIL_OP_REPLACE,
	R_STENCIL_OP_INCR_SAT,
	R_STENCIL_OP_DECR_SAT,
	R_STENCIL_OP_INVERT,
	R_STENCIL_OP_INCR,
	R_STENCIL_OP_DECR,
	_R_STENCIL_OP_COUNT
};

enum R_Depth_Write_Mask {
	R_DEPTH_WRITE_MASK_ZERO,
	R_DEPTH_WRITE_MASK_ALL,
	_R_DEPTH_WRITE_MASK_COUNT
};

enum R_Comparison {
	R_COMPARISON_NEVER,
	R_COMPARISON_LESS,
	R_COMPARISON_EQUAL,
	R_COMPARISON_LESS_EQUAL,
	R_COMPARISON_GREATER,
	R_COMPARISON_NOT_EQUAL,
	R_COMPARISON_GREATER_EQUAL,
	R_COMPARISON_ALWAYS,
	_R_COMPARISON_COUNT
};

struct R_Depth_Stencil {
	struct Stencil_Desc {
		R_Stencil_Op fail_op;
		R_Stencil_Op depth_fail_op;
		R_Stencil_Op pass_op;
		R_Comparison comparison;
	};

	struct {
		R_Depth_Write_Mask write_mask;
		R_Comparison    comparison;
		bool             enable;
	} depth;

	struct {
		bool         enable;
		uint8_t      read_mask;
		uint8_t      write_mask;
		Stencil_Desc front_face;
		Stencil_Desc back_face;
	} stencil;
};


enum R_Fill_Mode {
	R_FILL_SOLID,
	R_FILL_WIREFRAME,
	_R_FILL_MODE_COUNT
};

enum R_Cull_Mode {
	R_CULL_NONE,
	R_CULL_FRONT,
	R_CULL_BACK,
	_R_CULL_MODE_COUNT
};

struct R_Rasterizer {
	R_Fill_Mode  fill_mode;
	R_Cull_Mode  cull_mode;
	bool         front_clockwise;
	int32_t      depth_bias;
	float        depth_bias_clamp;
	bool         depth_clip_enable;
	bool         scissor_enable;
	bool         multisample_enable;
	bool         anti_aliased_line_enable;
};

enum R_Filter {
	R_FILTER_MIN_MAG_MIP_POINT,
	R_FILTER_MIN_MAG_POINT_MIP_LINEAR,
	R_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
	R_FILTER_MIN_POINT_MAG_MIP_LINEAR,
	R_FILTER_MIN_LINEAR_MAG_MIP_POINT,
	R_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
	R_FILTER_MIN_MAG_LINEAR_MIP_POINT,
	R_FILTER_MIN_MAG_MIP_LINEAR,
	R_FILTER_ANISOTROPIC,
	R_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
	R_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR,
	R_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT,
	R_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR,
	R_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT,
	R_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
	R_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
	R_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
	R_FILTER_COMPARISON_ANISOTROPIC,
	R_FILTER_MINIMUM_MIN_MAG_MIP_POINT,
	R_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR,
	R_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT,
	R_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR,
	R_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT,
	R_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
	R_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT,
	R_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR,
	R_FILTER_MINIMUM_ANISOTROPIC,
	R_FILTER_MAXIMUM_MIN_MAG_MIP_POINT,
	R_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR,
	R_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT,
	R_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR,
	R_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT,
	R_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
	R_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT,
	R_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR,
	R_FILTER_MAXIMUM_ANISOTROPIC,
	_R_FILTER_COUNT
};

enum R_Texture_Address_Mode {
	R_TEXTURE_ADDRESS_WRAP,
	R_TEXTURE_ADDRESS_MIRROR,
	R_TEXTURE_ADDRESS_CLAMP,
	R_TEXTURE_ADDRESS_BORDER,
	R_TEXTURE_ADDRESS_MIRROR_ONCE,
	_R_TEXTURE_ADDRESS_MODE_COUNT
};

struct R_Sampler {
	R_Filter                filter;
	R_Texture_Address_Mode  address_u;
	R_Texture_Address_Mode  address_v;
	R_Texture_Address_Mode  address_w;
	float                   mip_lod_bias;
	uint32_t                max_anisotropy;
	R_Comparison            comparison;
	float                   border_color[4];
	float                   min_lod;
	float                   max_lod;
};

struct R_Pipeline_Config {
	Array_View<uint8_t> shaders[R_SHADER_COUNT];
	R_Input_Layout      *input_layout;
	R_Blend             *blend;
	R_Depth_Stencil     *depth_stencil;
	R_Rasterizer        *rasterizer;
	R_Sampler           *sampler;
};

enum R_Primitive_Topology {
	R_PRIMITIVE_TOPOLOGY_UNDEFINED,
	R_PRIMITIVE_TOPOLOGY_POINTLIST,
	R_PRIMITIVE_TOPOLOGY_LINELIST,
	R_PRIMITIVE_TOPOLOGY_LINESTRIP,
	R_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	R_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	_R_PRIMITIVE_TOPOLOGY_COUNT
};

struct R_Viewport {
	float x, y;
	float width, height;
	float min_depth, max_depth;
};

struct R_Scissor {
	float min_x, min_y;
	float max_x, max_y;
};

R_Device *        R_CreateDevice(uint32_t device_flags);
void              R_DestroyDevice(R_Device *device);

R_Queue *         R_CreateRenderQueue(R_Device *device);
void              R_DestroyRenderQueue(R_Queue *queue);
void              R_Submit(R_Queue *queue, R_List *list);
void              R_Flush(R_Queue *queue);

R_List *          R_CreateRenderList(R_Device *device);
void              R_DestroyRenderList(R_List *list);

R_Swap_Chain *    R_CreateSwapChain(R_Device *device, PL_Window *window);
void              R_DestroySwapChain(R_Device *device, R_Swap_Chain *swap_chain);

void              R_SetSyncInterval(R_Swap_Chain *swap_chain, uint32_t interval);
void              R_ResizeRenderTargets(R_Device *device, R_Swap_Chain *swap_chain, uint32_t w, uint32_t h);
R_Render_Target * R_GetRenderTarget(R_Swap_Chain *swap_chain);
void              R_GetRenderTargetSize(R_Swap_Chain *swap_chain, float *w, float *h);
void              R_Present(R_Swap_Chain *swap_chain);

void              R_RenderTargetSize(R_Render_Target *render_target, float *w, float *h);

R_Pipeline *      R_CreatePipeline(R_Device *device, const R_Pipeline_Config &config);
void              R_DestroyPipeline(R_Pipeline *pipeline);

R_Buffer *        R_CreateVertexBuffer(R_Device *device, R_Buffer_Usage usage, uint32_t flags, uint32_t size, void *data);
R_Buffer *        R_CreateIndexBuffer(R_Device *device, R_Buffer_Usage usage, uint32_t flags, uint32_t size, void *data);
R_Buffer *        R_CreateConstantBuffer(R_Device *device, R_Buffer_Usage usage, uint32_t flags, uint32_t size, void *data);
void              R_DestroyBuffer(R_Buffer *buffer);

R_Texture *       R_CreateTexture(R_Device *device, R_Format format, uint32_t width, uint32_t height, uint32_t pitch, const uint8_t *pixels, uint32_t flags);
void              R_DestroyTexture(R_Texture *texture);

void *            R_MapBuffer(R_List *list, R_Buffer *buffer);
void              R_UnmapBuffer(R_List *list, R_Buffer *buffer);

void              R_ClearRenderTarget(R_List *list, R_Render_Target *render_target, const float color[4]);
void              R_SetPipeline(R_List *list, R_Pipeline *pipeline);
void              R_SetVertexBuffers(R_List *list, R_Buffer **buffer, uint32_t *stride, uint32_t *offset, uint32_t location, uint32_t count);
void              R_SetIndexBuffer(R_List *list, R_Buffer *buffer, R_Format format, uint32_t offset);
void              R_SetPrimitiveTopology(R_List *list, R_Primitive_Topology topology);
void              R_SetConstantBuffers(R_List *list, R_Shader shader, R_Buffer **buffer, uint32_t location, uint32_t count);
void              R_SetTextures(R_List *list, R_Texture **texture, uint32_t location, uint32_t count);
void              R_SetRenderTargets(R_List *list, uint32_t count, R_Render_Target *render_targets[], R_Depth_Stencil *depth_stencil);
void              R_SetViewports(R_List *list, R_Viewport *viewports, uint32_t count);
void              R_SetScissors(R_List *list, R_Scissor *scissors, uint32_t count);
void              R_Draw(R_List *list, uint32_t vertex_count, uint32_t start_vertex_location);
void              R_DrawIndexed(R_List *list, uint32_t index_count, uint32_t start_index_location, int32_t base_vertex_location);
