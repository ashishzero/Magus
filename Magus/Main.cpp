#include "Kr/KrPrelude.h"
#include "Kr/KrMedia.h"
#include "Kr/KrMath.h"
#include "Kr/KrArray.h"
#include "Kr/KrRender2d.h"

#include "RenderBackend.h"

#define STBRP_ASSERT Assert
#define STBRP_STATIC

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#define STBTT_malloc(x,u)  ((void)(u),MemAlloc(x))
#define STBTT_free(x,u)    ((void)(u),MemFree(x, 0))
#define STBTT_assert(x)    Assert(x)
#define STBTT_STATIC

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

struct R_Font_Internal {
	void (*release_texture)(R_Texture *texture);

	M_Allocator         allocator;
	uint32_t            allocated;

	R_Font_Texture_Kind kind;
	uint32_t            width;
	uint32_t            height;
	uint8_t *           pixels;
};

void Font_FreePixels(R_Font *font) {
	R_Font_Internal *_internal = (R_Font_Internal *)font->_internal;
	if (_internal->pixels) {
		uint32_t count = (_internal->kind == R_FONT_TEXTURE_RGBA_COLOR || _internal->kind == R_FONT_TEXTURE_RGBA) ? 4 : 1;
		uint32_t allocated = _internal->width * _internal->height * count;

		M_Free(_internal->pixels, allocated, _internal->allocator);
		_internal->pixels = nullptr;
	}
}

void Font_Destroy(R_Font *font) {
	Font_FreePixels(font);

	R_Font_Internal *_internal = (R_Font_Internal *)font->_internal;
	if (_internal->release_texture && font->texture) {
		_internal->release_texture(font->texture);
	}

	M_Free(font, _internal->allocated, _internal->allocator);
}

R_Font *Font_Create(const R_Font_Config &config, float height_in_pixels) {
	M_Arena *arena = ThreadScratchpad();

	M_Temporary temp = M_BeginTemporaryMemory(arena);
	Defer{ M_EndTemporaryMemory(&temp); };

	uint32_t replacement_codepoint = '?';

	int padding = 1;
	float oversample_h = 2;
	float oversample_v = 2;

	int rect_allocated = 0;

	for (const R_Font_File &file : config.files) {
		Assert((file.cp_ranges.count & 0x1) == 0x0);

		for (ptrdiff_t i = 0; i + 1 < file.cp_ranges.count; i += 2) {
			uint32_t first = file.cp_ranges[i];
			uint32_t last = file.cp_ranges[i + 1];

			rect_allocated += (last - first + 1);
		}
	}

	stbrp_rect *rp_rects = M_PushArray(arena, stbrp_rect, rect_allocated);
	if (!rp_rects) return nullptr;

	memset(rp_rects, 0, sizeof(*rp_rects) * rect_allocated);

	stbtt_fontinfo *font_infos = M_PushArray(arena, stbtt_fontinfo, config.files.count);
	if (!font_infos) return nullptr;

	uint32_t max_codepoint = 0;
	float surface_area = 0;
	int rect_count = 0;
	bool replacement_present = false;

	for (ptrdiff_t file_index = 0; file_index < config.files.count; ++file_index) {
		const R_Font_File &file = config.files[file_index];

		uint8_t *data = file.data.data;

		stbtt_fontinfo *font_info = &font_infos[file_index];
		int font_offset = stbtt_GetFontOffsetForIndex(data, file.index);
		if (!stbtt_InitFont(font_info, data, font_offset)) {
			continue;
		}

		float scale = stbtt_ScaleForPixelHeight(font_info, height_in_pixels);
		float scale_x = scale * oversample_h;
		float scale_y = scale * oversample_v;

		for (ptrdiff_t i = 0; i + 1 < file.cp_ranges.count; i += 2) {
			uint32_t first = file.cp_ranges[i];
			uint32_t last = file.cp_ranges[i + 1];

			max_codepoint = Max(max_codepoint, last);

			for (uint32_t codepoint = first; codepoint <= last; ++codepoint) {
				int glyph_index = stbtt_FindGlyphIndex(font_info, codepoint);

				if (glyph_index) {
					Assert(rect_count < rect_allocated);

					stbrp_rect *rect = rp_rects + rect_count;

					int x0, y0, x1, y1;
					stbtt_GetGlyphBitmapBoxSubpixel(font_info, glyph_index, scale_x, scale_y, 0, 0, &x0, &y0, &x1, &y1);

					rect->font = (uint32_t)file_index;
					rect->cp = codepoint;
					rect->w = (stbrp_coord)(x1 - x0 + padding + oversample_h - 1);
					rect->h = (stbrp_coord)(y1 - y0 + padding + oversample_v - 1);

					surface_area += (float)rect->w * (float)rect->h;

					rect_count += 1;

					replacement_present = replacement_present || (replacement_codepoint == codepoint);
				}
			}
		}
	}

	const int MAX_TEXTURE_DIM = 16384;

	int texture_width = Min(NextPowerOf2((int)Ceil(SquareRoot(surface_area))), MAX_TEXTURE_DIM);

	int temp_node_count = texture_width + 1;
	stbrp_node *temp_nodes = M_PushArray(arena, stbrp_node, temp_node_count);
	if (!temp_nodes) return nullptr;

	stbrp_context rp_context;
	stbrp_init_target(&rp_context, texture_width, MAX_TEXTURE_DIM, temp_nodes, temp_node_count);
	stbrp_pack_rects(&rp_context, rp_rects, rect_count);

	int texture_height = 0;
	for (int rect_index = 0; rect_index < rect_count; ++rect_index) {
		stbrp_rect *rect = rp_rects + rect_index;
		texture_height = Max(texture_height, rect->y + rect->h);
	}

	texture_height = Min(NextPowerOf2(texture_height + 1), MAX_TEXTURE_DIM);

	//
	//
	//

	int glyph_count = rect_count;

	if (!replacement_present) {
		// if replacement glyph is not present, allocate extra glyph
		glyph_count += 1;
	}

	max_codepoint += 1;

	size_t allocation_size = 0;
	allocation_size += sizeof(R_Font);
	allocation_size += sizeof(uint16_t) * max_codepoint;
	allocation_size += sizeof(R_Font_Glyph) * glyph_count;
	allocation_size += sizeof(R_Font_Internal);

	uint8_t *mem = (uint8_t *)MemAlloc(allocation_size);
	if (!mem) {
		LogErrorEx("Font", "Failed to allocate font");
		return nullptr;
	}

	uint8_t *gray_pixels = nullptr;
	uint8_t *rgba_pixels = nullptr;

	bool persistent_gray = (config.texture == R_FONT_TEXTURE_GRAYSCALE || config.texture == R_FONT_TEXTURE_SIGNED_DISTANCE_FIELD);

	if (persistent_gray) {
		gray_pixels = (uint8_t *)MemAlloc(texture_width * texture_height);
	} else {
		gray_pixels = M_PushArray(arena, uint8_t, texture_width * texture_height);
		rgba_pixels = (uint8_t *)MemAlloc(texture_width * texture_height * 4);

		if (!rgba_pixels) {
			LogErrorEx("Font", "Failed to allocate RGBA pixels");
			MemFree(mem, allocation_size);
			return nullptr;
		}
	}

	if (!gray_pixels) {
		LogErrorEx("Font", "Failed to allocate gray pixels");
		MemFree(mem, allocation_size);
		if (!persistent_gray) {
			MemFree(rgba_pixels, texture_width * texture_height * 4);
		}
		return nullptr;
	}

	memset(gray_pixels, 0, texture_width * texture_height);

	if (rgba_pixels)
		memset(rgba_pixels, 0, texture_width * texture_height * 4);

	//
	//
	//

	R_Font *font = (R_Font *)mem;
	mem += sizeof(R_Font);

	font->index = Array_View<uint16_t>((uint16_t *)mem, max_codepoint);
	mem += ArrSizeInBytes(font->index);

	font->glyphs = Array_View<R_Font_Glyph>((R_Font_Glyph *)mem, glyph_count);
	memset(font->glyphs.data, 0, ArrSizeInBytes(font->glyphs));
	mem += ArrSizeInBytes(font->glyphs);

	font->_internal = mem;
	font->height    = height_in_pixels;
	font->texture   = nullptr;

	R_Font_Internal *_internal = (R_Font_Internal *)(font->_internal);
	_internal->release_texture = nullptr;
	_internal->allocator       = ThreadContext.allocator;
	_internal->allocated       = (uint32_t)allocation_size;
	_internal->kind            = config.texture;
	_internal->width           = texture_width;
	_internal->height          = texture_height;
	_internal->pixels          = rgba_pixels ? rgba_pixels : gray_pixels;

	for (uint16_t &val : font->index)
		val = UINT16_MAX;

	uint16_t index = 0;
	for (int rect_index = 0; rect_index < rect_count; ++rect_index) {
		stbrp_rect *rect = rp_rects + rect_index;

		Assert(rect->was_packed);

		stbtt_fontinfo *font_info = &font_infos[rect->font];

		float scale = stbtt_ScaleForPixelHeight(font_info, height_in_pixels);
		float scale_x = scale * oversample_h;
		float scale_y = scale * oversample_v;

		int glyph_index = stbtt_FindGlyphIndex(font_info, (int)rect->cp);

		int x0, y0, x1, y1;
		stbtt_GetGlyphBitmapBoxSubpixel(font_info, glyph_index, scale_x, scale_y, 0, 0, &x0, &y0, &x1, &y1);

		int advance, left_side_bearing;
		stbtt_GetGlyphHMetrics(font_info, glyph_index, &advance, &left_side_bearing);

		R_Rect uv;
		uv.min = Vec2((float)rect->x / (float)texture_width, (float)rect->y / (float)texture_height);
		uv.max = Vec2((float)(rect->x + rect->w - 1) / (float)texture_width, (float)(rect->y + rect->h - 1) / (float)texture_height);

		font->index[rect->cp] = index;

		R_Font_Glyph *glyph = &font->glyphs[index++];
		glyph->codepoint    = rect->cp;
		glyph->advance      = (float)advance * scale_x;
		glyph->offset       = Vec2((float)x0, -(float)y1);
		glyph->dimension    = Vec2((float)(x1 - x0), (float)(y1 - y0));
		glyph->uv           = uv;

		Swap(&glyph->uv.min.y, &glyph->uv.max.y);

		stbtt_MakeGlyphBitmapSubpixel(font_info,
			&gray_pixels[rect->x + rect->y * texture_width], x1 - x0, y1 - y0,
			texture_width, scale_x, scale_y, 0, 0, glyph_index);
	}

	if (replacement_present) {
		index = font->index[replacement_codepoint];
		font->replacement = &font->glyphs[index];
	} else {
		gray_pixels[texture_width * texture_height - 1] = 0xff;

		float box_width = height_in_pixels * 0.7f;
		float box_height = height_in_pixels * 0.9f;

		R_Rect uv;
		uv.min = Vec2(0.0f, (float)(texture_height - 2) / (float)texture_height);
		uv.max = Vec2(1.0f / (float)texture_width, 1.0f);

		font->replacement = &font->glyphs[font->glyphs.count - 1];
		font->replacement->codepoint = -1;
		font->replacement->advance = box_width + 2;
		font->replacement->offset = Vec2(1, 0);
		font->replacement->dimension = Vec2(box_width, box_height);
		font->replacement->uv = uv;
	}

	if (rgba_pixels) {
		uint8_t *dst_pixel = rgba_pixels;
		uint8_t *src_pixel = gray_pixels;
		for (int i = 0; i < texture_width * texture_height; ++i) {
			dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = 0xff;
			dst_pixel[3] = *src_pixel;
			dst_pixel += 4;
			src_pixel += 1;
		}
	}

	return font;
}

struct R_Backend2d_Data {
	R_Buffer *vertex;
	R_Buffer *index;
	R_Buffer *constant;

	uint32_t vertex_allocated;
	uint32_t index_allocated;
	uint32_t constant_allocated;
};

static R_Texture *R_Backend2d_CreateTexture(R_Device *device, uint32_t w, uint32_t h, uint32_t n, const uint8_t *pixels) {
	R_Format format;
	if (n == 1) format = R_FORMAT_R8_UNORM;
	else if (n == 2) format = R_FORMAT_RG8_UNORM;
	else if (n == 4) format = R_FORMAT_RGBA8_UNORM;
	else Unreachable();

	return R_CreateTexture(device, format, w, h, w * n, pixels, 0);
}

static R_Texture *R_Backend2d_CreateTextureSRGBA(R_Device *device, uint32_t w, uint32_t h, const uint8_t *pixels) {
	return R_CreateTexture(device, R_FORMAT_RGBA8_UNORM_SRGB, w, h, w * 4, pixels, 0);
}

static void R_Backend2d_DestroyTexture(R_Texture *texture) {
	R_DestroyTexture(texture);
}

static R_Font *R_Backend2d_CreateFont(R_Device *device, const R_Font_Config &config, float height_in_pixels) {
	R_Font *font = Font_Create(config, height_in_pixels);
	if (font) {
		R_Font_Internal *_internal = (R_Font_Internal *)font->_internal;

		R_Format format;
		uint32_t pitch;

		if (_internal->kind == R_FONT_TEXTURE_GRAYSCALE || _internal->kind == R_FONT_TEXTURE_SIGNED_DISTANCE_FIELD) {
			format = R_FORMAT_R8_UNORM;
			pitch  = _internal->width;
		} else {
			format = R_FORMAT_RGBA8_UNORM;
			pitch  = _internal->width * 4;
		}

		font->texture = R_CreateTexture(device, format, _internal->width, _internal->height, pitch, _internal->pixels, 0);
		if (font->texture) {
			_internal->release_texture = R_DestroyTexture;
			Font_FreePixels(font);
			return font;
		}
		Font_Destroy(font);
	}
	return nullptr;
}

static void R_Backend2d_DestroyFont(R_Font *font) {
	Font_Destroy(font);
}

static bool R_Backend2d_UploadVertexData(R_Device *device, R_List *list, R_Backend2d_Data *data, void *ptr, uint32_t size) {
	if (data->vertex_allocated < size) {
		if (data->vertex)
			R_DestroyBuffer(data->vertex);
		data->vertex = R_CreateVertexBuffer(device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_WRITE_ACCESS, size, nullptr);
		if (!data->vertex) {
			data->vertex_allocated = 0;
			return false;
		}

		data->vertex_allocated = size;
	}

	void *dst = R_MapBuffer(list, data->vertex);
	if (dst) {
		memcpy(dst, ptr, size);
		R_UnmapBuffer(list, data->vertex);
		uint32_t stride = sizeof(R_Vertex2d), offset = 0;
		R_SetVertexBuffers(list, &data->vertex, &stride, &offset, 0, 1);
		return true;
	}
	return false;
}

static bool R_Backend2d_UploadIndexData(R_Device *device, R_List *list, R_Backend2d_Data *data, void *ptr, uint32_t size) {
	if (data->index_allocated < size) {
		if (data->index)
			R_DestroyBuffer(data->index);
		data->index = R_CreateIndexBuffer(device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_READ_ACCESS, size, nullptr);
		if (!data->index) {
			data->index_allocated = 0;
			return false;
		}

		data->index_allocated = size;
	}

	void *dst = R_MapBuffer(list, data->index);
	if (dst) {
		memcpy(dst, ptr, size);
		R_UnmapBuffer(list, data->index);

		static_assert(sizeof(R_Index2d) == sizeof(uint32_t) || sizeof(R_Index2d) == sizeof(uint16_t));

		R_Format format = sizeof(R_Index2d) == sizeof(uint32_t) ? R_FORMAT_R32_UINT : R_FORMAT_R16_UINT;
		R_SetIndexBuffer(list, data->index, format, 0);
		return true;
	}
	return false;
}

void R_Backend2d_UploadDrawData(R_Device *device, R_List *list, R_Backend2d_Data *data, const R_Backend2d_Draw_Data &draw_data) {
	uint32_t required_size = AlignPower2Up(sizeof(Mat4), 16);

	if (data->constant_allocated < required_size) {
		if (data->constant)
			R_DestroyBuffer(data->constant);
		data->constant = R_CreateConstantBuffer(device, R_BUFFER_USAGE_DYNAMIC, R_BUFFER_CPU_READ_ACCESS, required_size, nullptr);
		if (!data->constant) {
			data->constant_allocated = 0;
			return;
		}

		data->constant_allocated = required_size;
	}

	void *dst = R_MapBuffer(list, data->constant);
	if (dst) {
		const R_Camera2d &camera = draw_data.camera;
		Mat4 proj = OrthographicLH(camera.left, camera.right, camera.top, camera.bottom, camera.near, camera.far);
		Mat4 t = proj * draw_data.transform;
		memcpy(dst, t.m, sizeof(Mat4));
		R_UnmapBuffer(list, data->constant);

		R_SetConstantBuffers(list, R_SHADER_VERTEX, &data->constant, 0, 1);
	}
}

void R_Backend2d_SetPipeline(R_List *list, R_Pipeline *pipeline) {
	R_SetPipeline(list, pipeline);
}

void R_Backend2d_SetScissor(R_List *list, R_Rect rect) {
	R_Scissor scissor;
	scissor.min_x = rect.min.x;
	scissor.min_y = rect.min.y;
	scissor.max_x = rect.max.x;
	scissor.max_y = rect.max.y;
	R_SetScissors(list, &scissor, 1);
}

void R_Backend2d_SetTexture(R_List *list, R_Texture *texture) {
	R_SetTextures(list, &texture, 0, 1);
}

void R_Backend2d_DrawTriangleList(R_List *list, uint32_t index_count, uint32_t index_offset, int32_t vertex_offset) {
	R_SetPrimitiveTopology(list, R_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	R_DrawIndexed(list, index_count, index_offset, vertex_offset);
}

void R_Backend2d_Release(struct R_Backend2d_Impl *impl, R_Backend2d_Data *data) {
	R_DestroyBuffer(data->vertex);
	R_DestroyBuffer(data->index);
	R_DestroyBuffer(data->constant);

	data->vertex = data->index = data->constant = nullptr;
	data->vertex_allocated = data->index_allocated = data->constant_allocated = 0;
}

struct R_Backend2d_Impl : R_Backend2d {
	R_Device *       device;

	R_Backend2d_Data data;

	virtual R_Texture *CreateTexture(uint32_t w, uint32_t h, uint32_t n, const uint8_t *pixels) override {
		return R_Backend2d_CreateTexture(device, w, h, n, pixels);
	}

	virtual R_Texture *CreateTextureSRGBA(uint32_t w, uint32_t h, const uint8_t *pixels) override {
		return R_Backend2d_CreateTextureSRGBA(device, w, h, pixels);
	}

	virtual void DestroyTexture(R_Texture *texture) override {
		R_Backend2d_DestroyTexture(texture);
	}

	virtual R_Font *CreateFont(const R_Font_Config &config, float height_in_pixels) override {
		return R_Backend2d_CreateFont(device, config, height_in_pixels);
	}

	virtual void DestroyFont(R_Font *font) override {
		R_Backend2d_DestroyFont(font);
	}

	virtual bool UploadVertexData(void *context, void *ptr, uint32_t size) override {
		return R_Backend2d_UploadVertexData(device, (R_List *)context, &data, ptr, size);
	}

	virtual bool UploadIndexData(void *context, void *ptr, uint32_t size) override {
		return R_Backend2d_UploadIndexData(device, (R_List *)context, &data, ptr, size);
	}

	virtual void UploadDrawData(void *context, const R_Backend2d_Draw_Data &draw_data) override {
		R_Backend2d_UploadDrawData(device, (R_List *)context, &data, draw_data);
	}

	virtual void SetPipeline(void *context, R_Pipeline *pipeline) override {
		R_Backend2d_SetPipeline((R_List *)context, pipeline);
	}

	virtual void SetScissor(void *context, R_Rect rect) override {
		R_Backend2d_SetScissor((R_List *)context, rect);
	}

	virtual void SetTexture(void *context, R_Texture *texture) override {
		R_Backend2d_SetTexture((R_List *)context, texture);
	}

	virtual void DrawTriangleList(void *context, uint32_t index_count, uint32_t index_offset, int32_t vertex_offset) override {
		R_Backend2d_DrawTriangleList((R_List *)context, index_count, index_offset, vertex_offset);
	}

	virtual void Release() override {
		R_Backend2d_Release(this, &data);
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

		R_DrawText(renderer, Vec2(10, height - 50), Vec4(1), u8"The cake is a lie.");
		R_DrawRect(renderer, 0.5f * Vec2(width, height), Vec2(100), Vec4(1));

		//R_Font *font = R_DefaultFont(renderer);
		//R_Font_Internal *_internal = (R_Font_Internal *)font->_internal;
		//R_DrawTexture(renderer, font->texture, Vec2(50), Vec2(_internal->width, _internal->height));

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
