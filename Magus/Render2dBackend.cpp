#include "Render2dBackend.h"

#include "Kr/KrMemory.h"

#include "RenderBackend.h"
#include "ResourceLoaders/Loaders.h"

#include <string.h>

struct R_Backend2d_Impl {
	R_Backend2d backend;

	R_Device *  device;
	R_Buffer *  vertex;
	R_Buffer *  index;
	R_Buffer *  constant;

	uint32_t    vertex_allocated;
	uint32_t    index_allocated;
	uint32_t    constant_allocated;
};

static R_Texture *CreateTextureImpl(R_Backend2d *backend, uint32_t w, uint32_t h, uint32_t n, const uint8_t *pixels) {
	R_Backend2d_Impl *impl = (R_Backend2d_Impl *)backend;

	R_Format format;
	if (n == 1) format = R_FORMAT_R8_UNORM;
	else if (n == 2) format = R_FORMAT_RG8_UNORM;
	else if (n == 4) format = R_FORMAT_RGBA8_UNORM;
	else Unreachable();

	return R_CreateTexture(impl->device, format, w, h, w * n, pixels, 0);
}

static R_Texture *CreateTextureSRGBAImpl(R_Backend2d *backend, uint32_t w, uint32_t h, const uint8_t *pixels) {
	R_Backend2d_Impl *impl = (R_Backend2d_Impl *)backend;
	return R_CreateTexture(impl->device, R_FORMAT_RGBA8_UNORM_SRGB, w, h, w * 4, pixels, 0);
}

static void DestroyTextureImpl(R_Backend2d *backend, R_Texture *texture) {
	R_DestroyTexture(texture);
}

static R_Font *CreateFontImpl(R_Backend2d *backend, const R_Font_Config &config, float height_in_pixels) {
	R_Backend2d_Impl *impl = (R_Backend2d_Impl *)backend;

	M_Arena *arena    = ThreadScratchpad();
	M_Temporary temp = M_BeginTemporaryMemory(arena);
	Defer{ M_EndTemporaryMemory(&temp); };

	R_Font *font = LoadFont(arena, config, height_in_pixels);
	if (UploadFontTexture(impl->device, font)) {
		return font;
	}

	ReleaseFont(font);
	return nullptr;
}

static void DestroyFontImpl(R_Backend2d *backend, R_Font *font) {
	ReleaseFont(font);
}

static bool UploadVertexDataImpl(R_Backend2d *backend, void *_list, void *ptr, uint32_t size) {
	R_Backend2d_Impl *impl = (R_Backend2d_Impl *)backend;
	R_List *list           = (R_List *)_list;

	if (impl->vertex_allocated < size) {
		if (impl->vertex)
			R_DestroyBuffer(impl->vertex);
		impl->vertex = R_CreateVertexBuffer(impl->device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_WRITE_ACCESS, size, nullptr);
		impl->vertex_allocated = size;
	}

	if (!impl->vertex) {
		impl->vertex_allocated = 0;
		return false;
	}

	void *dst = R_MapBuffer(list, impl->vertex);
	if (dst) {
		memcpy(dst, ptr, size);
		R_UnmapBuffer(list, impl->vertex);
		uint32_t stride = sizeof(R_Vertex2d), offset = 0;
		R_BindVertexBuffers(list, &impl->vertex, &stride, &offset, 0, 1);
		return true;
	}
	return false;
}

static bool UploadIndexDataImpl(R_Backend2d *backend, void *_list, void *ptr, uint32_t size) {
	R_Backend2d_Impl *impl = (R_Backend2d_Impl *)backend;
	R_List *list           = (R_List *)_list;

	if (impl->index_allocated < size) {
		if (impl->index)
			R_DestroyBuffer(impl->index);
		impl->index = R_CreateIndexBuffer(impl->device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_READ_ACCESS, size, nullptr);

		impl->index_allocated = size;
	}

	if (!impl->index) {
		impl->index_allocated = 0;
		return false;
	}

	void *dst = R_MapBuffer(list, impl->index);
	if (dst) {
		memcpy(dst, ptr, size);
		R_UnmapBuffer(list, impl->index);

		static_assert(sizeof(R_Index2d) == sizeof(uint32_t) || sizeof(R_Index2d) == sizeof(uint16_t), "");

		R_Format format = sizeof(R_Index2d) == sizeof(uint32_t) ? R_FORMAT_R32_UINT : R_FORMAT_R16_UINT;
		R_BindIndexBuffer(list, impl->index, format, 0);
		return true;
	}
	return false;
}

static void UploadDrawDataImpl(R_Backend2d *backend, void *_list, const R_Backend2d_Draw_Data &draw_data) {
	R_Backend2d_Impl *impl = (R_Backend2d_Impl *)backend;
	R_List *list           = (R_List *)_list;

	uint32_t required_size = AlignPower2Up(sizeof(Mat4), 16);

	if (impl->constant_allocated < required_size) {
		if (impl->constant)
			R_DestroyBuffer(impl->constant);
		impl->constant = R_CreateConstantBuffer(impl->device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_READ_ACCESS, required_size, nullptr);
		if (!impl->constant) {
			impl->constant_allocated = 0;
			return;
		}

		impl->constant_allocated = required_size;
	}

	void *dst = R_MapBuffer(list, impl->constant);
	if (dst) {
		const R_Camera2d &camera = draw_data.camera;
		Mat4 proj = OrthographicLH(camera.left, camera.right, camera.top, camera.bottom, camera.near, camera.far);
		Mat4 t = proj * draw_data.transform;
		memcpy(dst, t.m, sizeof(Mat4));
		R_UnmapBuffer(list, impl->constant);

		R_BindConstantBuffers(list, R_SHADER_VERTEX, &impl->constant, 0, 1);
	}
}

void SetPipelineImpl(R_Backend2d *backend, void *_list, R_Pipeline *pipeline) {
	R_List *list = (R_List *)_list;
	R_BindPipeline(list, pipeline);
}

void SetScissorImpl(R_Backend2d *backend, void *_list, R_Rect rect) {
	R_List *list = (R_List *)_list;
	R_Scissor scissor;
	scissor.min_x = rect.min.x;
	scissor.min_y = rect.min.y;
	scissor.max_x = rect.max.x;
	scissor.max_y = rect.max.y;
	R_SetScissors(list, &scissor, 1);
}

void SetTextureImpl(R_Backend2d *backend, void *_list, R_Texture *texture) {
	R_List *list = (R_List *)_list;
	R_BindTextures(list, &texture, 0, 1);
}

void DrawTriangleListImpl(R_Backend2d *backend, void *_list, uint32_t index_count, uint32_t index_offset, int32_t vertex_offset) {
	R_List *list = (R_List *)_list;
	R_SetPrimitiveTopology(list, R_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	R_DrawIndexed(list, index_count, index_offset, vertex_offset);
}

void ReleaseImpl(R_Backend2d *backend) {
	R_Backend2d_Impl *impl = (R_Backend2d_Impl *)backend;

	R_DestroyBuffer(impl->vertex);
	R_DestroyBuffer(impl->index);
	R_DestroyBuffer(impl->constant);

	impl->vertex = impl->index = impl->constant = nullptr;
	impl->vertex_allocated = impl->index_allocated = impl->constant_allocated = 0;
}

R_Backend2d *R_CreateBackend2d(R_Device *device) {
	R_Backend2d_Impl *impl = (R_Backend2d_Impl *)M_Alloc(sizeof(R_Backend2d_Impl));
	if (!impl) return nullptr;

	memset(impl, 0, sizeof(*impl));

	impl->backend.CreateTexture      = CreateTextureImpl;
	impl->backend.CreateTextureSRGBA = CreateTextureSRGBAImpl;
	impl->backend.DestroyTexture     = DestroyTextureImpl;
	impl->backend.CreateFont         = CreateFontImpl;
	impl->backend.DestroyFont        = DestroyFontImpl;

	impl->backend.UploadVertexData   = UploadVertexDataImpl;
	impl->backend.UploadIndexData    = UploadIndexDataImpl;
	impl->backend.UploadDrawData     = UploadDrawDataImpl;
	impl->backend.SetPipeline        = SetPipelineImpl;
	impl->backend.SetScissor         = SetScissorImpl;
	impl->backend.SetTexture         = SetTextureImpl;
	impl->backend.DrawTriangleList   = DrawTriangleListImpl;

	impl->backend.Release            = ReleaseImpl;

	impl->device                     = device;

	return &impl->backend;
}

R_Renderer2d *R_CreateRenderer2dFromDevice(R_Device *device, const R_Specification2d &spec) {
	R_Backend2d *backend = R_CreateBackend2d(device);
	return R_CreateRenderer2d(backend, spec);
}
