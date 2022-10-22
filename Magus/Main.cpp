#include "Kr/KrPrelude.h"
#include "Kr/KrMedia.h"
#include "Kr/KrMath.h"
#include "Kr/KrArray.h"

#include "Render2d.h"
#include "RenderBackend.h"

struct R_Backend2d_Data {
	R_Buffer *vertex;
	R_Buffer *index;
	R_Buffer *constant;

	uint32_t vertex_allocated;
	uint32_t index_allocated;
	uint32_t constant_allocated;
};

struct R_Backend2d_Impl : R_Backend2d {
	R_Device *       device;

	R_Backend2d_Data data;

	virtual R_Texture *CreateTexture(uint32_t w, uint32_t h, uint32_t n, const uint8_t *pixels) {
		R_Format format;
		if (n == 1) format = R_FORMAT_R8_UNORM;
		else if (n == 2) format = R_FORMAT_RG8_UNORM;
		else if (n == 4) format = R_FORMAT_RGBA8_UNORM;
		else Unreachable();

		return R_CreateTexture(device, format, w, h, pixels, 0);
	}

	virtual R_Texture *CreateTextureSRGBA(uint32_t w, uint32_t h, const uint8_t *pixels) {
		return R_CreateTexture(device, R_FORMAT_RGBA8_UNORM_SRGB, w, h, pixels, 0);
	}

	virtual void DestroyTexture(R_Texture *texture) {
		R_DestroyTexture(texture);
	}

	virtual bool UploadVertexData(void *context, void *ptr, uint32_t size) { 
		R_List *list = (R_List *)context;

		if (data.vertex_allocated < size) {
			if (data.vertex)
				R_DestroyBuffer(data.vertex);
			data.vertex = R_CreateVertexBuffer(device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_WRITE_ACCESS, size, nullptr);
			if (!data.vertex) {
				data.vertex_allocated = 0;
				return false;
			}

			data.vertex_allocated = size;
		}

		void *dst = R_MapBuffer(list, data.vertex);
		if (dst) {
			memcpy(dst, ptr, size);
			R_UnmapBuffer(list, data.vertex);
			uint32_t stride = sizeof(R_Vertex2d), offset = 0;
			R_SetVertexBuffers(list, &data.vertex, &stride, &offset, 0, 1);
			return true;
		}
		return false;
	}

	virtual bool UploadIndexData(void *context, void *ptr, uint32_t size) {
		R_List *list = (R_List *)context;

		if (data.index_allocated < size) {
			if (data.index)
				R_DestroyBuffer(data.index);
			data.index = R_CreateIndexBuffer(device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_READ_ACCESS, size, nullptr);
			if (!data.index) {
				data.index_allocated = 0;
				return false;
			}

			data.index_allocated = size;
		}

		void *dst = R_MapBuffer(list, data.index);
		if (dst) {
			memcpy(dst, ptr, size);
			R_UnmapBuffer(list, data.index);

			static_assert(sizeof(R_Index2d) == sizeof(uint32_t) || sizeof(R_Index2d) == sizeof(uint16_t));

			R_Format format = sizeof(R_Index2d) == sizeof(uint32_t) ? R_FORMAT_R32_UINT : R_FORMAT_R16_UINT;
			R_SetIndexBuffer(list, data.index, format, 0);
			return true;
		}
		return false;
	}

	virtual void SetCameraTransform(void *context, const R_Camera2d &camera, const Mat4 &transform) {
		R_List *list = (R_List *)context;

		if (data.constant_allocated < sizeof(Mat4)) {
			if (data.constant)
				R_DestroyBuffer(data.constant);
			data.constant = R_CreateConstantBuffer(device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_READ_ACCESS, sizeof(Mat4), nullptr);
			if (!data.constant) {
				data.constant_allocated = 0;
				return;
			}

			data.constant_allocated = sizeof(Mat4);
		}

		void *dst = R_MapBuffer(list, data.constant);
		if (dst) {
			Mat4 proj = OrthographicLH(camera.left, camera.right, camera.top, camera.bottom, camera.near, camera.far);
			Mat4 t = proj * transform;
			memcpy(dst, t.m, sizeof(Mat4));
			R_UnmapBuffer(list, data.constant);

			R_SetConstantBuffers(list, R_SHADER_VERTEX, &data.constant, 0, 1);
		}
	}

	virtual void SetPipeline(void *context, R_Pipeline *pipeline) {
		R_List *list = (R_List *)context;
		R_SetPipeline(list, pipeline);
	}

	virtual void SetScissor(void *context, R_Rect rect) {
		R_List *list = (R_List *)context;
		R_Scissor scissor;
		scissor.min_x = rect.min.x;
		scissor.min_y = rect.min.y;
		scissor.max_x = rect.max.x;
		scissor.max_y = rect.max.y;
		R_SetScissors(list, &scissor, 1);
	}

	virtual void SetTexture(void *context, R_Texture *texture) {
		R_List *list = (R_List *)context;
		R_SetTextures(list, &texture, 0, 1);
	}

	virtual void DrawTriangleList(void *context, uint32_t index_count, uint32_t index_offset, uint32_t vertex_offset) {
		R_List *list = (R_List *)context;
		R_SetPrimitiveTopology(list, R_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		R_DrawIndexed(list, index_count, index_offset, vertex_offset);
	}

	virtual void Release() {
		R_DestroyBuffer(data.vertex);
		R_DestroyBuffer(data.index);
		R_DestroyBuffer(data.constant);

		data.vertex = data.index = data.constant = nullptr;
		data.vertex_allocated = data.index_allocated = data.constant_allocated = 0;
	}
};

R_Backend2d_Impl CreateRenderer2dBackend(R_Device *device) {
	R_Backend2d_Impl backend;
	backend.device = device;
	memset(&backend.data, 0, sizeof(backend.data));
	return backend;
}

R_Pipeline *CreateRender2dPipeline(R_Device *device) {
	R_Input_Layout_Element elements[] = {
		{ "POSITION", 0, R_FORMAT_RGB32_FLOAT, 0, offsetof(R_Vertex2d, position), R_INPUT_CLASSIFICATION_PER_VERTEX, 0},
		{ "TEXCOORD", 0, R_FORMAT_RG32_FLOAT, 0, offsetof(R_Vertex2d, tex_coord), R_INPUT_CLASSIFICATION_PER_VERTEX, 0 },
		{ "COLOR", 0, R_FORMAT_RGBA32_FLOAT, 0, offsetof(R_Vertex2d, color), R_INPUT_CLASSIFICATION_PER_VERTEX, 0 }
	};

	R_Input_Layout input_layout = elements;

	R_Blend blend                     = {};
	blend.render_target[0].enable     = true;
	blend.render_target[0].color      = { R_BLEND_SRC_ALPHA, R_BLEND_INV_SRC_ALPHA, R_BLEND_OP_ADD };
	blend.render_target[0].alpha      = { R_BLEND_SRC_ALPHA, R_BLEND_INV_SRC_ALPHA, R_BLEND_OP_ADD };
	blend.render_target[0].write_mask = R_WRITE_MASK_ALL;

	R_Depth_Stencil depth_stencil  = {};
	depth_stencil.depth.enable     = true;
	depth_stencil.depth.comparison = R_COMPARISON_LESS_EQUAL;
	depth_stencil.depth.write_mask = R_DEPTH_WRITE_MASK_ALL;

	R_Rasterizer rasterizer    = {};
	rasterizer.fill_mode       = R_FILL_SOLID;
	rasterizer.cull_mode       = R_CULL_NONE;
	rasterizer.front_clockwise = true;
	rasterizer.scissor_enable  = true;

	R_Sampler sampler = {};
	sampler.filter    = R_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.address_u = R_TEXTURE_ADDRESS_WRAP;
	sampler.address_v = R_TEXTURE_ADDRESS_WRAP;
	sampler.address_w = R_TEXTURE_ADDRESS_WRAP;

	R_Pipeline_Config config        = {};
	config.shaders[R_SHADER_VERTEX] = PL_ReadEntireFile("Resources/Shaders/HLSL/QuadVertex.cso");
	config.shaders[R_SHADER_PIXEL]  = PL_ReadEntireFile("Resources/Shaders/HLSL/QuadPixel.cso");
	config.input_layout             = &input_layout;
	config.blend                    = &blend;
	config.depth_stencil            = &depth_stencil;
	config.rasterizer               = &rasterizer;
	config.sampler                  = &sampler;

	return R_CreatePipeline(device, config);
}

int Main(int argc, char **argv) {
	PL_Init();

	PL_ThreadCharacteristics(PL_THREAD_GAMES);
	
	PL_Window *window = PL_CreateWindow("Magus", 0, 0, false);
	if (!window)
		FatalError("Failed to create windows");

	R_Device *rdevice = R_CreateDevice(R_DEVICE_DEBUG_ENABLE);

	R_Queue *rqueue = R_CreateRenderQueue(rdevice);

	R_Swap_Chain *swap_chain = R_CreateSwapChain(rdevice, window);

	R_List *rlist = R_CreateRenderList(rdevice);

	R_Backend2d_Impl backend = CreateRenderer2dBackend(rdevice);

	R_Renderer2d *renderer = R_CreateRenderer2d(&backend);

	R_Pipeline *pipeline = CreateRender2dPipeline(rdevice);

	bool running = true;

	while (running) {
		Array_View<PL_Event> events = PL_PollEvents();

		for (const PL_Event &e : events) {
			if (e.kind == PL_EVENT_QUIT || e.kind == PL_EVENT_CLOSE) {
				PL_DestroyWindow(window);
				running = false;
				break;
			}

			if (e.kind == PL_EVENT_RESIZE) {
				R_Flush(rqueue);
				R_ResizeRenderTargets(rdevice, swap_chain, e.resize.w, e.resize.h);
			}
		}

		float width, height;
		R_GetRenderTargetSize(swap_chain, &width, &height);

		R_Render_Target *render_target = R_GetRenderTarget(swap_chain);

		R_NextFrame(renderer, R_Rect(0.0f, 0.0f, width, height));
		R_CameraView(renderer, 0.0f, width, 0.0f, height, -1.0f, 1.0f);

		R_SetPipeline(renderer, pipeline);

		R_DrawText(renderer, Vec2(10, height - 50), Vec4(1), u8"The cake is a lie.か");
		R_DrawRect(renderer, 0.5f * Vec2(width, height), Vec2(100), Vec4(1));

		R_Viewport viewport;
		viewport.y = viewport.x = 0;
		viewport.width = width;
		viewport.height = height;
		viewport.min_depth = 0;
		viewport.max_depth = 1;

		float clear_color[] = { .12f, .12f, .12f, 1.0f };
		R_ClearRenderTarget(rlist, render_target, clear_color);

		R_SetRenderTargets(rlist, 1, &render_target, nullptr);
		R_SetViewports(rlist, &viewport, 1);
		R_FinishFrame(renderer, rlist);

		R_Submit(rqueue, rlist);

		R_Present(swap_chain);

		ThreadResetScratchpad();
	}

	R_DestroyRenderList(rlist);
	R_DestroyRenderQueue(rqueue);

	R_DestroySwapChain(rdevice, swap_chain);
	R_DestroyDevice(rdevice);

	return 0;
}
