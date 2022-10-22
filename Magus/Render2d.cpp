#include "Render2d.h"
#include "Kr/KrPrelude.h"
#include "RobotoMedium.h"

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

#include <malloc.h>

#if defined(RENDER_GROUP2D_ENABLE_DEBUG_INFO)
struct R_Memory_Mark {
	ptrdiff_t command;
	ptrdiff_t vertex;
	ptrdiff_t index;
	ptrdiff_t path;
};
#endif

struct R_Command2d {
	R_Pipeline *pipeline;
	R_Camera2d  camera;
	Mat4        transform;
	R_Rect      rect;
	R_Texture * texture;
	uint32_t    vertex_offset;
	uint32_t    index_offset;
	uint32_t    index_count;
};

struct R_Renderer2d {
	Array<R_Command2d>  command;
	Array<R_Vertex2d>   vertex;
	Array<R_Index2d>    index;
	Array<Mat4>         transform;

	R_Command2d *       write_command        = nullptr;
	R_Vertex2d *        write_vertex         = nullptr;
	R_Index2d *         write_index          = nullptr;

	R_Index2d           next_index           = 0;

	Array<R_Pipeline *> pipeline;
	Array<R_Texture *>  texture;
	Array<R_Rect>       rect;
	Array<Vec2>         path;

	R_Camera2d          camera;
	float               thickness            = 1.0f;

	R_Backend2d *       backend              = nullptr;

	R_Texture *         white_texture        = nullptr;
	R_Font *            default_font         = nullptr;

#if defined(RENDER_GROUP2D_ENABLE_DEBUG_INFO)
	R_Memory_Mark       mark                 = {};
#endif

	M_Allocator         allocator            = ThreadContext.allocator;
};

//
//
//

float  UnitCircleCosValues[MAX_CIRCLE_SEGMENTS];
float  UnitcircleSinValues[MAX_CIRCLE_SEGMENTS];

static R_Command2d       FallbackDrawCmd = {};
static const R_Font      FallbackFont    = {};
static const R_Backend2d FallbackBackend;

static void R_InitNextDrawCommand(R_Renderer2d *r2) {
	R_Command2d *command   = r2->write_command;
	command->camera        = r2->camera;
	command->pipeline      = r2->pipeline.Last();
	command->texture       = r2->texture.Last();
	command->rect          = r2->rect.Last();
	command->transform     = r2->transform.Last();
	command->vertex_offset = (uint32_t)r2->vertex.count;
	command->index_offset  = (uint32_t)r2->index.count;
	command->index_count   = 0;

	r2->next_index = 0;
}

static void R_PushDrawCommand(R_Renderer2d *r2) {
	if (r2->write_command != &FallbackDrawCmd) {
		r2->write_command = r2->command.Add();
		if (r2->write_command) {
#if defined(RENDER_GROUP2D_ENABLE_DEBUG_INFO)
			r2->mark.command = Max(r2->mark.command, r2->command.count);
#endif
		} else {
			r2->write_command = &FallbackDrawCmd;
			LogWarningEx("R_Renderer2d", "Command buffer overflow");
		}
		R_InitNextDrawCommand(r2);
	}
}

static R_Index2d R_EnsurePrimitive(R_Renderer2d *r2, uint32_t vertex, uint32_t index) {
	ptrdiff_t vertex_count = r2->vertex.count;
	ptrdiff_t index_count = r2->index.count;

	if (r2->vertex.Resize(vertex_count + vertex) && r2->index.Resize(index_count + index)) {
		r2->write_vertex = &r2->vertex[vertex_count];
		r2->write_index  = &r2->index[index_count];

		r2->write_command->index_count += index;

		R_Index2d next_index = r2->next_index;
		r2->next_index += vertex;

#if defined(RENDER_GROUP2D_ENABLE_DEBUG_INFO)
		r2->mark.vertex = Max(r2->mark.vertex, r2->vertex.count);
		r2->mark.index  = Max(r2->mark.index, r2->index.count);
#endif

		return next_index;
	}

	LogWarningEx("R_Renderer2d", "Primitive buffer overflow");

	R_PushDrawCommand(r2);

	r2->write_command = &FallbackDrawCmd;
	r2->write_vertex  = nullptr;
	r2->write_index   = nullptr;

	return -1;
}

static void R_LoadRendererResources(R_Renderer2d *r2) {
	M_Allocator backup = ThreadContext.allocator;

	ThreadContext.allocator = r2->allocator;

	uint8_t pixels[]  = { 0xff, 0xff, 0xff, 0xff };
	r2->white_texture = R_Backend_CreateTexture(r2, 1, 1, 4, pixels);

	r2->default_font  = R_CreateFont(r2, String(RobotoMediumFontBytes, sizeof(RobotoMediumFontBytes)), 25.0f);

	ThreadContext.allocator = backup;
}

static void R_ReleaseRendererResources(R_Renderer2d *r2) {
	M_Allocator backup = ThreadContext.allocator;

	ThreadContext.allocator = r2->allocator;

	if (r2->white_texture)
		R_Backend_DestroyTexture(r2, r2->white_texture);

	if (r2->default_font)
		R_DestroyFont(r2, r2->default_font);

	ThreadContext.allocator = backup;
}

//
//
//

R_Renderer2d *R_CreateRenderer2d(R_Backend2d *backend, const R_Specification2d &spec) {
	static bool Initialized = false;

	if (!Initialized) {
		for (int i = 0; i < MAX_CIRCLE_SEGMENTS; ++i) {
			float theta = ((float)i / (float)MAX_CIRCLE_SEGMENTS) * PI * 2;
			UnitCircleCosValues[i] = Cos(theta);
			UnitcircleSinValues[i] = Sin(theta);
		}

		UnitCircleCosValues[MAX_CIRCLE_SEGMENTS - 1] = 1;
		UnitcircleSinValues[MAX_CIRCLE_SEGMENTS - 1] = 0;
	}

	R_Renderer2d *r2 = new R_Renderer2d;

	if (!r2) return nullptr;

	if (!backend)
		backend = (R_Backend2d *)&FallbackBackend;

	r2->backend = backend;
	R_LoadRendererResources(r2);

	r2->command.Reserve(spec.command);
	r2->vertex.Reserve(spec.vertex);
	r2->index.Reserve(spec.index);

	r2->pipeline.Reserve(spec.pipeline);
	r2->texture.Reserve(spec.texture);
	r2->rect.Reserve(spec.rect);
	r2->transform.Reserve(spec.transform);
	r2->path.Reserve(spec.path);

	r2->thickness = spec.thickness;

	r2->pipeline.Add(nullptr);
	r2->texture.Add(r2->white_texture);
	r2->rect.Add(R_Rect(0.0f, 0.0f, 0.0f, 0.0f));
	r2->transform.Add(Identity());

	if (!r2->default_font)
		r2->default_font = (R_Font *)&FallbackFont;

	if (!r2->pipeline.count || !r2->texture.count || !r2->rect.count || !r2->transform.count) {
		R_DestroyRenderer2d(r2);
		return nullptr;
	}

	r2->camera = { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f };

	R_PushDrawCommand(r2);

	return r2;
}

void R_DestroyRenderer2d(R_Renderer2d *r2) {
	R_ReleaseRendererResources(r2);
	
	r2->backend->Release();

	Free(r2->command);
	Free(r2->vertex);
	Free(r2->index);
	Free(r2->pipeline);
	Free(r2->texture);
	Free(r2->rect);
	Free(r2->path);


	M_Free(r2, sizeof(*r2), r2->allocator);
}

R_Texture *R_Backend_CreateTexture(R_Renderer2d *r2, uint32_t w, uint32_t h, uint32_t n, const uint8_t *pixels) {
	return r2->backend->CreateTexture(w, h, n, pixels);
}

R_Texture *R_Backend_CreateTextureSRGBA(R_Renderer2d *r2, uint32_t w, uint32_t h, const uint8_t *pixels) {
	return r2->backend->CreateTextureSRGBA(w, h, pixels);
}

void R_Backend_DestroyTexture(R_Renderer2d *r2, R_Texture *texture) {
	r2->backend->DestroyTexture(texture);
}

R_Font_Glyph *R_FontFindGlyph(R_Font *font, uint32_t codepoint) {
	if (codepoint < font->index.count) {
		uint16_t index = font->index[codepoint];
		if (index != UINT16_MAX) {
			return &font->glyphs[index];
		}
	}
	return font->replacement;
}

R_Font *R_CreateFont(R_Renderer2d *r2, Array_View<R_Font_Config> configs, float height_in_pixels, const R_Font_Global_Config &global_config, M_Arena *tmp_arena) {
	M_Temporary temp = M_BeginTemporaryMemory(tmp_arena);
	Defer{ M_EndTemporaryMemory(&temp); };

	int padding        = global_config.padding;
	float oversample_h = global_config.oversample_h;
	float oversample_v = global_config.oversample_v;

	int rect_allocated = 0;

	for (const R_Font_Config &config : configs) {
		Assert((config.cp_ranges.count & 0x1) == 0x0);

		for (ptrdiff_t i = 0; i + 1 < config.cp_ranges.count; i += 2) {
			uint32_t first = config.cp_ranges[i];
			uint32_t last  = config.cp_ranges[i + 1];

			rect_allocated += (last - first + 1);
		}
	}

	stbrp_rect *rp_rects = M_PushArray(tmp_arena, stbrp_rect, rect_allocated);
	if (!rp_rects) return nullptr;

	memset(rp_rects, 0, sizeof(*rp_rects) * rect_allocated);

	stbtt_fontinfo *font_infos = M_PushArray(tmp_arena, stbtt_fontinfo, configs.count);
	if (!font_infos) return nullptr;

	uint32_t max_codepoint   = 0;
	float surface_area       = 0;
	int rect_count           = 0;
	bool replacement_present = false;

	for (ptrdiff_t config_index = 0; config_index < configs.count; ++config_index) {
		const R_Font_Config &config = configs[config_index];

		uint8_t *data = config.ttf.data;

		stbtt_fontinfo *font_info = &font_infos[config_index];
		int font_offset = stbtt_GetFontOffsetForIndex(data, config.index);
		if (!stbtt_InitFont(font_info, data, font_offset)) {
			continue;
		}

		float scale = stbtt_ScaleForPixelHeight(font_info, height_in_pixels);
		float scale_x = scale * oversample_h;
		float scale_y = scale * oversample_v;

		for (ptrdiff_t i = 0; i + 1 < config.cp_ranges.count; i += 2) {
			uint32_t first = config.cp_ranges[i];
			uint32_t last  = config.cp_ranges[i + 1];

			max_codepoint  = Max(max_codepoint, last);

			for (uint32_t codepoint = first; codepoint <= last; ++codepoint) {
				int glyph_index = stbtt_FindGlyphIndex(font_info, codepoint);

				if (glyph_index) {
					Assert(rect_count < rect_allocated);

					stbrp_rect *rect = rp_rects + rect_count;

					int x0, y0, x1, y1;
					stbtt_GetGlyphBitmapBoxSubpixel(font_info, glyph_index, scale_x, scale_y, 0, 0, &x0, &y0, &x1, &y1);

					rect->font = (uint32_t)config_index;
					rect->cp   = codepoint;
					rect->w    = (stbrp_coord)(x1 - x0 + padding + oversample_h - 1);
					rect->h    = (stbrp_coord)(y1 - y0 + padding + oversample_v - 1);

					surface_area += (float)rect->w * (float)rect->h;

					rect_count += 1;

					replacement_present = replacement_present || (global_config.replacement == codepoint);
				}
			}
		}
	}

	const int MAX_TEXTURE_DIM = 16384;

	int texture_width      = Min(NextPowerOf2((int)Ceil(SquareRoot(surface_area))), MAX_TEXTURE_DIM);

	int temp_node_count    = texture_width + 1;
	stbrp_node *temp_nodes = M_PushArray(tmp_arena, stbrp_node, temp_node_count);
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

	uint8_t *gray_pixels = M_PushArray(tmp_arena, uint8_t, texture_width * texture_height);
	uint8_t *rgba_pixels = M_PushArray(tmp_arena, uint8_t, texture_width * texture_height * 4);

	if (!gray_pixels || !rgba_pixels)
		return nullptr;

	memset(gray_pixels, 0, texture_width * texture_height);

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

	uint8_t *mem = (uint8_t *)MemAlloc(allocation_size);
	if (!mem) return nullptr;

	R_Font *font = (R_Font *)mem;
	mem += sizeof(R_Font);

	font->index  = Array_View<uint16_t>((uint16_t *)mem, max_codepoint);
	mem += ArrSizeInBytes(font->index);

	font->glyphs = Array_View<R_Font_Glyph>((R_Font_Glyph *)mem, glyph_count);
	memset(font->glyphs.data, 0, ArrSizeInBytes(font->glyphs));

	for (uint16_t &val : font->index)
		val = UINT16_MAX;

	uint16_t index = 0;
	for (int rect_index = 0; rect_index < rect_count; ++rect_index) {
		stbrp_rect *rect = rp_rects + rect_index;

		Assert(rect->was_packed);

		stbtt_fontinfo *font_info = &font_infos[rect->font];

		float scale   = stbtt_ScaleForPixelHeight(font_info, height_in_pixels);
		float scale_x = scale * oversample_h;
		float scale_y = scale * oversample_v;

		int glyph_index = stbtt_FindGlyphIndex(font_info, (int)rect->cp);

		int x0, y0, x1, y1;
		stbtt_GetGlyphBitmapBoxSubpixel(font_info, glyph_index, scale_x, scale_y, 0, 0, &x0, &y0, &x1, &y1);

		int advance, left_side_bearing;
		stbtt_GetGlyphHMetrics(font_info, glyph_index, &advance, &left_side_bearing);

		R_Rect uv;
		uv.min = Vec2((float)rect->x / (float)texture_width, (float)rect->y / (float)texture_height);
		uv.max = Vec2((float)(rect->x + rect->w) / (float)texture_width, (float)(rect->y + rect->h) / (float)texture_height);

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
		index = font->index[global_config.replacement];
		font->replacement = &font->glyphs[index];
	} else {
		gray_pixels[texture_width * texture_height - 1] = 0xff;

		float box_width  = height_in_pixels * 0.7f;
		float box_height = height_in_pixels * 0.9f;

		R_Rect uv;
		uv.min = Vec2(0.0f, (float)(texture_height - 2) / (float)texture_height);
		uv.max = Vec2(1.0f / (float)texture_width, 1.0f);

		font->replacement = &font->glyphs[font->glyphs.count - 1];
		font->replacement->codepoint = -1;
		font->replacement->advance   = box_width + 2;
		font->replacement->offset    = Vec2(1, 0);
		font->replacement->dimension = Vec2(box_width, box_height);
		font->replacement->uv        = uv;
	}

	uint8_t *dst_pixel = rgba_pixels;
	uint8_t *src_pixel = gray_pixels;
	for (int i = 0; i < texture_width * texture_height; ++i) {
		dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = 0xff;
		dst_pixel[3] = *src_pixel;
		dst_pixel += 4;
		src_pixel += 1;
	}

	font->texture = r2->backend->CreateTexture(texture_width, texture_height, 4, rgba_pixels);
	if (!font->texture) {
		R_DestroyFont(r2, font);
		return nullptr;
	}

	return font;
}

R_Font *R_CreateFont(R_Renderer2d *r2, String font_data, float height, Array_View<uint32_t> ranges, uint32_t index, const R_Font_Global_Config &global_config, M_Arena *tmp_arena) {
	R_Font_Config config;
	config.ttf       = font_data;
	config.index     = index;
	config.cp_ranges = ranges;
	return R_CreateFont(r2, Array_View<R_Font_Config>(&config, 1), height, global_config, tmp_arena);
}

void R_DestroyFont(R_Renderer2d *r2, R_Font *font) {
	if (font == &FallbackFont) return;

	if (font->texture)
		R_Backend_DestroyTexture(r2, font->texture);
	MemFree(font, sizeof(R_Font) + ArrSizeInBytes(font->index) + ArrSizeInBytes(font->glyphs));
}

R_Texture *R_DefaultTexture(R_Renderer2d *r2) {
	return r2->white_texture;
}

R_Font *R_DefaultFont(R_Renderer2d *r2) {
	return r2->default_font;
}

R_Backend2d *R_GetBackend(R_Renderer2d *r2) {
	return r2->backend;
}

R_Backend2d *R_SwapBackend(R_Renderer2d *r2, R_Backend2d *new_backend) {
	// Textures and Pipeline must be release before swapping the backend
	// This procedure MUST not be called in between R_NextFrame and R_FinishFrame
	Assert(r2->texture.count == 1 && r2->pipeline.count == 1);

	R_ReleaseRendererResources(r2);

	R_Backend2d *prev = r2->backend;
	r2->backend       = new_backend;

	R_LoadRendererResources(r2);

	// Replace the default texture in texture stack
	r2->texture[0] = r2->white_texture;

	return prev;
}

void R_SetBackend(R_Renderer2d *r2, R_Backend2d *backend) {
	R_Backend2d *old_backend = R_SwapBackend(r2, backend);
	old_backend->Release();
}

R_Memory2d R_GetMemoryInformation(R_Renderer2d *r2) {
	R_Memory2d info;

	info.allocated.command = r2->command.allocated * sizeof(R_Command2d);
	info.allocated.vertex  = r2->vertex.allocated * sizeof(R_Vertex2d);
	info.allocated.index   = r2->index.allocated * sizeof(R_Index2d);

	info.allocated.path = 0;
	info.allocated.path += r2->path.allocated * sizeof(Vec2);

	info.allocated.total = 0;
	info.allocated.total += info.allocated.command;
	info.allocated.total += info.allocated.vertex;
	info.allocated.total += info.allocated.index;
	info.allocated.total += info.allocated.path;

#if defined(RENDER_GROUP2D_ENABLE_DEBUG_INFO)
	info.used_mark.command = r2->mark.command * sizeof(R_Command2d);
	info.used_mark.vertex  = r2->mark.vertex * sizeof(R_Vertex2d);
	info.used_mark.index   = r2->mark.index * sizeof(R_Index2d);
	info.used_mark.path    = r2->mark.path * sizeof(Vec2) * 3;

	info.used_mark.total = 0;
	info.used_mark.total += info.used_mark.command;
	info.used_mark.total += info.used_mark.vertex;
	info.used_mark.total += info.used_mark.index;
	info.used_mark.total += info.used_mark.path;
#endif

	return info;
}

//
//
//

void R_NextFrame(R_Renderer2d *r2, R_Rect region) {
	Assert(r2->texture.count >= 1);
	Assert(r2->rect.count >= 1);

	r2->texture.count   = 1;
	r2->rect.count      = 1;
	r2->transform.count = 1;

	r2->command.Reset();
	r2->vertex.Reset();
	r2->index.Reset();
	r2->path.Reset();

	r2->next_index = 0;
	r2->rect[0]    = region;

	r2->write_command = nullptr;
	r2->write_vertex  = nullptr;
	r2->write_index   = nullptr;

	r2->camera = { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f };

	R_PushDrawCommand(r2);
}

void R_FinishFrame(R_Renderer2d *r2, void *context) {
	if (r2->command.count == 0)
		return;

	R_Backend2d *backend = r2->backend;

	if (!backend->UploadVertexData(context, r2->vertex.data, (uint32_t)ArrSizeInBytes(r2->vertex)))
		return;

	if (!backend->UploadIndexData(context, r2->index.data, (uint32_t)ArrSizeInBytes(r2->index)))
		return;

	for (const R_Command2d &cmd : r2->command) {
		if (cmd.index_count == 0)
			continue;

		backend->SetCameraTransform(context, cmd.camera, cmd.transform);
		backend->SetPipeline(context, cmd.pipeline);
		backend->SetScissor(context, cmd.rect);
		backend->SetTexture(context, cmd.texture);
		backend->DrawTriangleList(context, cmd.index_count, cmd.index_offset, cmd.vertex_offset);
	}
}

void R_NextDrawCommand(R_Renderer2d *r2) {
	if (r2->write_command->index_count)
		R_PushDrawCommand(r2);
}

//
//
//

void R_CameraView(R_Renderer2d *r2, float left, float right, float bottom, float top, float z_near, float z_far) {
	if (r2->write_command->index_count)
		R_PushDrawCommand(r2);

	r2->camera.left   = left;
	r2->camera.right  = right;
	r2->camera.bottom = bottom;
	r2->camera.top    = top;
	r2->camera.near   = z_near;
	r2->camera.far    = z_far;

	r2->write_command->camera = r2->camera;
}

void R_CameraView(R_Renderer2d *r2, float aspect_ratio, float height) {
	float width = aspect_ratio * height;

	float arx = aspect_ratio;
	float ary = 1;

	if (width < height) {
		ary = 1.0f / arx;
		arx = 1.0f;
	}

	float half_height = 0.5f * height;

	R_CameraView(r2, -half_height * arx, half_height * arx, -half_height * ary, half_height * ary, -1.0f, 1.0f);
}

void R_CameraDimension(R_Renderer2d *r2, float width, float height) {
	float half_width = 0.5f * width;
	float half_height = 0.5f * height;
	R_CameraView(r2, -half_width, half_width, -half_height, half_height, 0.0f, 1.0f);
}

void R_SetLineThickness(R_Renderer2d *r2, float thickness) {
	r2->thickness = thickness;
}

void R_SetPipeline(R_Renderer2d *r2, R_Pipeline *pipeline) {
	R_Pipeline *prev_pipeline = r2->pipeline[r2->pipeline.count - 1];
	if (prev_pipeline != pipeline && r2->write_command->index_count)
		R_PushDrawCommand(r2);
	r2->pipeline[r2->pipeline.count - 1] = pipeline;
	r2->write_command->pipeline          = pipeline;
}

void R_PushPipeline(R_Renderer2d *r2, R_Pipeline *pipeline) {
	r2->pipeline.Add(r2->pipeline.Last());
	R_SetPipeline(r2, pipeline);
}

void R_PopPipeline(R_Renderer2d *r2) {
	Assert(r2->pipeline.count > 1);
	R_SetPipeline(r2, r2->pipeline[r2->pipeline.count - 2]);
	r2->pipeline.RemoveLast();
}

void R_SetTexture(R_Renderer2d *r2, R_Texture *texture) {
	R_Texture *prev_texture = r2->texture[r2->texture.count - 1];
	if (prev_texture != texture && r2->write_command->index_count)
		R_PushDrawCommand(r2);
	r2->texture[r2->texture.count - 1] = texture;
	r2->write_command->texture         = texture;
}

void R_PushTexture(R_Renderer2d *r2, R_Texture *texture) {
	r2->texture.Add(r2->texture.Last());
	R_SetTexture(r2, texture);
}

void R_PopTexture(R_Renderer2d *r2) {
	Assert(r2->texture.count > 1);
	R_SetTexture(r2, r2->texture[r2->texture.count - 2]);
	r2->texture.RemoveLast();
}

void R_SetRect(R_Renderer2d *r2, R_Rect rect) {
	R_Rect prev_rect = r2->rect[r2->rect.count - 1];
	if (memcmp(&prev_rect, &rect, sizeof(rect)) == 0 && r2->write_command->index_count)
		R_PushDrawCommand(r2);
	r2->rect[r2->rect.count - 1] = rect;
	r2->write_command->rect      = rect;
}

void R_PushRect(R_Renderer2d *r2, R_Rect rect) {
	r2->rect.Add(r2->rect.Last());
	R_SetRect(r2, rect);
}

void R_PopRect(R_Renderer2d *r2) {
	Assert(r2->rect.count > 1);
	R_SetRect(r2, r2->rect[r2->rect.count - 2]);
	r2->rect.RemoveLast();
}

void R_SetTransform(R_Renderer2d *r2, const Mat4 &transform) {
	const Mat4 *prev = &r2->transform[r2->transform.count - 1];
	if (memcmp(prev, &transform, sizeof(transform)) == 0 && r2->write_command->index_count)
		R_PushDrawCommand(r2);
	r2->transform[r2->transform.count - 1] = transform;
	r2->write_command->transform           = transform;
}

void R_PushTransform(R_Renderer2d *r2, const Mat4 &transform) {
	r2->transform.Add(r2->transform.Last());
	Mat4 t = r2->transform.Last() * transform;
	R_SetTransform(r2, t);
}

void R_PopTransform(R_Renderer2d *r2) {
	Assert(r2->transform.count > 1);
	R_SetTransform(r2, r2->transform[r2->transform.count - 2]);
	r2->transform.RemoveLast();
}

R_Texture *R_CurrentTexture(R_Renderer2d *r2) {
	return r2->texture[r2->texture.count];
}

R_Rect R_CurrentRect(R_Renderer2d *r2) {
	return r2->rect[r2->rect.count];
}

Mat4 R_CurrentTransform(R_Renderer2d *r2) {
	return r2->transform[r2->transform.count];
}

void R_DrawTriangle(R_Renderer2d *r2, Vec3 va, Vec3 vb, Vec3 vc, Vec2 ta, Vec2 tb, Vec2 tc, Vec4 ca, Vec4 cb, Vec4 cc) {
	R_Index2d index = R_EnsurePrimitive(r2, 3, 3);

	if (index != -1) {
		R_Vertex2d *vtx = r2->write_vertex;
		R_Index2d * idx = r2->write_index;

		vtx[0].position = va; vtx[0].tex_coord = ta; vtx[0].color = ca;
		vtx[1].position = vb; vtx[1].tex_coord = tb; vtx[1].color = cb;
		vtx[2].position = vc; vtx[2].tex_coord = tc; vtx[2].color = cc;

		idx[0] = index + 0;
		idx[1] = index + 1;
		idx[2] = index + 2;
	}
}

void R_DrawTriangle(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec2 ta, Vec2 tb, Vec2 tc, Vec4 col) {
	R_DrawTriangle(r2, a, b, c, ta, tb, tc, col, col, col);
}

void R_DrawTriangle(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 ta, Vec2 tb, Vec2 tc, Vec4 col) {
	R_DrawTriangle(r2, Vec3(a, 0), Vec3(b, 0), Vec3(c, 0), ta, tb, tc, col, col, col);
}

void R_DrawTriangle(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec4 color) {
	R_DrawTriangle(r2, a, b, c, Vec2(0), Vec2(0), Vec2(0), color, color, color);
}

void R_DrawTriangle(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec4 color) {
	R_DrawTriangle(r2, Vec3(a, 0), Vec3(b, 0), Vec3(c, 0), Vec2(0), Vec2(0), Vec2(0), color, color, color);
}

void R_DrawQuad(R_Renderer2d *r2, Vec3 va, Vec3 vb, Vec3 vc, Vec3 vd, Vec2 ta, Vec2 tb, Vec2 tc, Vec2 td, Vec4 color) {
	R_Index2d index = R_EnsurePrimitive(r2, 4, 6);

	if (index != -1) {
		R_Vertex2d *vtx = r2->write_vertex;
		R_Index2d * idx = r2->write_index;

		vtx[0].position = va; vtx[0].tex_coord = ta; vtx[0].color = color;
		vtx[1].position = vb; vtx[1].tex_coord = tb; vtx[1].color = color;
		vtx[2].position = vc; vtx[2].tex_coord = tc; vtx[2].color = color;
		vtx[3].position = vd; vtx[3].tex_coord = td; vtx[3].color = color;

		idx[0] = index + 0;
		idx[1] = index + 1;
		idx[2] = index + 2;
		idx[3] = index + 0;
		idx[4] = index + 2;
		idx[5] = index + 3;
	}
}

void R_DrawQuad(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	R_DrawQuad(r2, Vec3(a, 0), Vec3(b, 0), Vec3(c, 0), Vec3(d, 0), uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawQuad(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec4 color) {
	R_DrawQuad(r2, a, b, c, d, Vec2(0, 0), Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), color);
}

void R_DrawQuad(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec4 color) {
	R_DrawQuad(r2, Vec3(a, 0), Vec3(b, 0), Vec3(c, 0), Vec3(d, 0), color);
}

void R_DrawQuad(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec3 d, R_Rect rect, Vec4 color) {
	auto uv_a = rect.min;
	auto uv_b = Vec2(rect.min.x, rect.max.y);
	auto uv_c = rect.max;
	auto uv_d = Vec2(rect.max.x, rect.min.y);
	R_DrawQuad(r2, a, b, c, d, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawQuad(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, R_Rect rect, Vec4 color) {
	R_DrawQuad(r2, Vec3(a, 0), Vec3(b, 0), Vec3(c, 0), Vec3(d, 0), rect, color);
}

void R_DrawRect(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	Vec3 a = pos;
	Vec3 b = Vec3(pos.x, pos.y + dim.y, pos.z);
	Vec3 c = Vec3(pos._0.xy + dim, pos.z);
	Vec3 d = Vec3(pos.x + dim.x, pos.y, pos.z);
	R_DrawQuad(r2, a, b, c, d, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRect(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	R_DrawRect(r2, Vec3(pos, 0), dim, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRect(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color) {
	R_DrawRect(r2, pos, dim, Vec2(0, 0), Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), color);
}

void R_DrawRect(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color) {
	R_DrawRect(r2, Vec3(pos, 0), dim, color);
}

void R_DrawRect(R_Renderer2d *r2, Vec3 pos, Vec2 dim, R_Rect rect, Vec4 color) {
	auto uv_a = rect.min;
	auto uv_b = Vec2(rect.min.x, rect.max.y);
	auto uv_c = rect.max;
	auto uv_d = Vec2(rect.max.x, rect.min.y);
	R_DrawRect(r2, pos, dim, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRect(R_Renderer2d *r2, Vec2 pos, Vec2 dim, R_Rect rect, Vec4 color) {
	R_DrawRect(r2, Vec3(pos, 0), dim, rect, color);
}

void R_DrawRectRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	Vec2  center = 0.5f * (2 * pos._0.xy + dim);

	Vec2  a = pos._0.xy;
	Vec2  b = Vec2(pos.x, pos.y + dim.y);
	Vec2  c = pos._0.xy + dim;
	Vec2  d = Vec2(pos.x + dim.x, pos.y);

	auto  t0 = a - center;
	auto  t1 = b - center;
	auto  t2 = c - center;
	auto  t3 = d - center;

	float cv = Cos(angle);
	float sv = Sin(angle);

	a.x = t0.x * cv - t0.y * sv;
	a.y = t0.x * sv + t0.y * cv;
	b.x = t1.x * cv - t1.y * sv;
	b.y = t1.x * sv + t1.y * cv;
	c.x = t2.x * cv - t2.y * sv;
	c.y = t2.x * sv + t2.y * cv;
	d.x = t3.x * cv - t3.y * sv;
	d.y = t3.x * sv + t3.y * cv;

	a += center;
	b += center;
	c += center;
	d += center;

	R_DrawQuad(r2, Vec3(a, pos.z), Vec3(b, pos.z), Vec3(c, pos.z), Vec3(d, pos.z), uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	R_DrawRectRotated(r2, Vec3(pos, 0), dim, angle, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, Vec4 color) {
	R_DrawRectRotated(r2, pos, dim, angle, Vec2(0, 0), Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), color);
}

void R_DrawRectRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, Vec4 color) {
	R_DrawRectRotated(r2, Vec3(pos, 0), dim, angle, color);
}

void R_DrawRectRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, R_Rect rect, Vec4 color) {
	auto uv_a = rect.min;
	auto uv_b = Vec2(rect.min.x, rect.max.y);
	auto uv_c = rect.max;
	auto uv_d = Vec2(rect.max.x, rect.min.y);
	R_DrawRectRotated(r2, pos, dim, angle, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, R_Rect rect, Vec4 color) {
	R_DrawRectRotated(r2, Vec3(pos, 0), dim, angle, rect, color);
}

void R_DrawRectCentered(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	Vec2 half_dim = 0.5f * dim;

	Vec3 a, b, c, d;
	a._0.xy = pos._0.xy - half_dim;
	b._0.xy = Vec2(pos.x - half_dim.x, pos.y + half_dim.y);
	c._0.xy = pos._0.xy + half_dim;
	d._0.xy = Vec2(pos.x + half_dim.x, pos.y - half_dim.y);

	a.z = pos.z;
	b.z = pos.z;
	c.z = pos.z;
	d.z = pos.z;

	R_DrawQuad(r2, a, b, c, d, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectCentered(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	R_DrawRectCentered(r2, Vec3(pos, 0), dim, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectCentered(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color) {
	R_DrawRectCentered(r2, pos, dim, Vec2(0, 0), Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), color);
}

void R_DrawRectCentered(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color) {
	R_DrawRectCentered(r2, Vec3(pos, 0), dim, Vec2(0, 0), Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), color);
}

void R_DrawRectCentered(R_Renderer2d *r2, Vec3 pos, Vec2 dim, R_Rect rect, Vec4 color) {
	auto uv_a = rect.min;
	auto uv_b = Vec2(rect.min.x, rect.max.y);
	auto uv_c = rect.max;
	auto uv_d = Vec2(rect.max.x, rect.min.y);
	R_DrawRectCentered(r2, pos, dim, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectCentered(R_Renderer2d *r2, Vec2 pos, Vec2 dim, R_Rect rect, Vec4 color) {
	R_DrawRectCentered(r2, Vec3(pos, 0), dim, rect, color);
}

void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	Vec2 center = pos._0.xy;

	Vec2 half_dim = 0.5f * dim;
	Vec2 a, b, c, d;
	a = pos._0.xy - half_dim;
	b = Vec2(pos.x - half_dim.x, pos.y + half_dim.y);
	c = pos._0.xy + half_dim;
	d = Vec2(pos.x + half_dim.x, pos.y - half_dim.y);

	auto  t0 = a - center;
	auto  t1 = b - center;
	auto  t2 = c - center;
	auto  t3 = d - center;

	float cv = Cos(angle);
	float sv = Sin(angle);

	a.x = t0.x * cv - t0.y * sv;
	a.y = t0.x * sv + t0.y * cv;
	b.x = t1.x * cv - t1.y * sv;
	b.y = t1.x * sv + t1.y * cv;
	c.x = t2.x * cv - t2.y * sv;
	c.y = t2.x * sv + t2.y * cv;
	d.x = t3.x * cv - t3.y * sv;
	d.y = t3.x * sv + t3.y * cv;

	a += center;
	b += center;
	c += center;
	d += center;

	R_DrawQuad(r2, Vec3(a, pos.z), Vec3(b, pos.z), Vec3(c, pos.z), Vec3(d, pos.z), uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color) {
	R_DrawRectCenteredRotated(r2, Vec3(pos, 0), dim, angle, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, Vec4 color) {
	R_DrawRectCenteredRotated(r2, pos, dim, angle, Vec2(0, 0), Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), color);
}

void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, Vec4 color) {
	R_DrawRectCenteredRotated(r2, Vec3(pos, 0), dim, angle, Vec2(0, 0), Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), color);
}

void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, R_Rect rect, Vec4 color) {
	auto uv_a = rect.min;
	auto uv_b = Vec2(rect.min.x, rect.max.y);
	auto uv_c = rect.max;
	auto uv_d = Vec2(rect.max.x, rect.min.y);
	R_DrawRectCenteredRotated(r2, pos, dim, angle, uv_a, uv_b, uv_c, uv_d, color);
}

void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, R_Rect rect, Vec4 color) {
	R_DrawRectCenteredRotated(r2, Vec3(pos, 0), dim, angle, rect, color);
}

void R_DrawEllipse(R_Renderer2d *r2, Vec3 pos, float radius_a, float radius_b, Vec4 color, int segments) {
	segments = Clamp(MIN_CIRCLE_SEGMENTS, MAX_CIRCLE_SEGMENTS - 1, segments);

	float px = UnitCircleCosValues[0] * radius_a;
	float py = UnitcircleSinValues[0] * radius_b;

	float npx, npy;
	for (int index = 1; index <= segments; ++index) {
		int lookup = (int)(((float)index / (float)segments) * (MAX_CIRCLE_SEGMENTS - 1) + 0.5f);

		npx = UnitCircleCosValues[lookup] * radius_a;
		npy = UnitcircleSinValues[lookup] * radius_b;

		R_DrawTriangle(r2, pos, pos + Vec3(npx, npy, 0), pos + Vec3(px, py, 0), color);

		px = npx;
		py = npy;
	}
}

void R_DrawEllipse(R_Renderer2d *r2, Vec2 pos, float radius_a, float radius_b, Vec4 color, int segments) {
	R_DrawEllipse(r2, Vec3(pos, 0), radius_a, radius_b, color, segments);
}

void R_DrawCircle(R_Renderer2d *r2, Vec3 pos, float radius, Vec4 color, int segments) {
	R_DrawEllipse(r2, pos, radius, radius, color, segments);
}

void R_DrawCircle(R_Renderer2d *r2, Vec2 pos, float radius, Vec4 color, int segments) {
	R_DrawEllipse(r2, Vec3(pos, 0), radius, radius, color, segments);
}

void R_DrawPie(R_Renderer2d *r2, Vec3 pos, float radius_a, float radius_b, float theta_a, float theta_b, Vec4 color, int segments) {
	Assert(theta_a >= 0 && theta_a <= PI * 2 && theta_b >= 0 && theta_b <= PI * 2);

	int first_index = (int)((0.5f * theta_a * PI_INVERSE) * (float)(MAX_CIRCLE_SEGMENTS)+0.5f);
	int last_index = (int)((0.5f * theta_b * PI_INVERSE) * (float)(MAX_CIRCLE_SEGMENTS)+0.5f);

	while (first_index >= last_index)
		last_index += MAX_CIRCLE_SEGMENTS;

	auto value_count = last_index - first_index;
	segments = Min(segments, value_count);

	float px = UnitCircleCosValues[first_index] * radius_a;
	float py = UnitcircleSinValues[first_index] * radius_b;

	float npx, npy;
	for (int index = 1; index <= segments; ++index) {
		auto lookup = first_index + (int)((float)index / (float)segments * (float)value_count + 0.5f);
		lookup = lookup & (MAX_CIRCLE_SEGMENTS - 1);

		npx = UnitCircleCosValues[lookup] * radius_a;
		npy = UnitcircleSinValues[lookup] * radius_b;

		R_DrawTriangle(r2, pos, pos + Vec3(npx, npy, 0), pos + Vec3(px, py, 0), color);

		px = npx;
		py = npy;
	}
}

void R_DrawPie(R_Renderer2d *r2, Vec2 pos, float radius_a, float radius_b, float theta_a, float theta_b, Vec4 color, int segments) {
	R_DrawPie(r2, Vec3(pos, 0), radius_a, radius_b, theta_a, theta_b, color, segments);
}

void R_DrawPie(R_Renderer2d *r2, Vec3 pos, float radius, float theta_a, float theta_b, Vec4 color, int segments) {
	R_DrawPie(r2, pos, radius, radius, theta_a, theta_b, color, segments);
}

void R_DrawPie(R_Renderer2d *r2, Vec2 pos, float radius, float theta_a, float theta_b, Vec4 color, int segments) {
	R_DrawPie(r2, Vec3(pos, 0), radius, radius, theta_a, theta_b, color, segments);
}

void R_DrawPiePart(R_Renderer2d *r2, Vec3 pos, float radius_a_min, float radius_b_min, float radius_a_max, float radius_b_max, float theta_a, float theta_b, Vec4 color, int segments) {
	Assert(theta_a >= 0 && theta_a <= PI * 2 && theta_b >= 0 && theta_b <= PI * 2);

	int first_index = (int)((0.5f * theta_a * PI_INVERSE) * (float)(MAX_CIRCLE_SEGMENTS)+0.5f);
	int last_index = (int)((0.5f * theta_b * PI_INVERSE) * (float)(MAX_CIRCLE_SEGMENTS)+0.5f);

	while (first_index >= last_index)
		last_index += MAX_CIRCLE_SEGMENTS;

	auto value_count = last_index - first_index;
	segments = Min(segments, value_count);

	float min_px = UnitCircleCosValues[first_index] * radius_a_min;
	float min_py = UnitcircleSinValues[first_index] * radius_b_min;
	float max_px = UnitCircleCosValues[first_index] * radius_a_max;
	float max_py = UnitcircleSinValues[first_index] * radius_b_max;

	float min_npx, min_npy;
	float max_npx, max_npy;
	for (int index = 1; index <= segments; ++index) {
		auto lookup = first_index + (int)((float)index / (float)segments * (float)value_count + 0.5f);
		lookup = lookup & (MAX_CIRCLE_SEGMENTS - 1);

		min_npx = UnitCircleCosValues[lookup] * radius_a_min;
		min_npy = UnitcircleSinValues[lookup] * radius_b_min;
		max_npx = UnitCircleCosValues[lookup] * radius_a_max;
		max_npy = UnitcircleSinValues[lookup] * radius_b_max;

		R_DrawQuad(r2, pos + Vec3(min_npx, min_npy, 0), pos + Vec3(max_npx, max_npy, 0), pos + Vec3(max_px, max_py, 0), pos + Vec3(min_px, min_py, 0), color);

		min_px = min_npx;
		min_py = min_npy;
		max_px = max_npx;
		max_py = max_npy;
	}
}

void R_DrawPiePart(R_Renderer2d *r2, Vec2 pos, float radius_a_min, float radius_b_min, float radius_a_max, float radius_b_max, float theta_a, float theta_b, Vec4 color, int segments) {
	R_DrawPiePart(r2, Vec3(pos, 0), radius_a_min, radius_b_min, radius_a_max, radius_b_max, theta_a, theta_b, color, segments);
}

void R_DrawPiePart(R_Renderer2d *r2, Vec3 pos, float radius_min, float radius_max, float theta_a, float theta_b, Vec4 color, int segments) {
	R_DrawPiePart(r2, pos, radius_min, radius_min, radius_max, radius_max, theta_a, theta_b, color, segments);
}

void R_DrawPiePart(R_Renderer2d *r2, Vec2 pos, float radius_min, float radius_max, float theta_a, float theta_b, Vec4 color, int segments) {
	R_DrawPiePart(r2, Vec3(pos, 0), radius_min, radius_min, radius_max, radius_max, theta_a, theta_b, color, segments);
}

void R_DrawLine(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec4 color) {
	if (IsNull(b - a))
		return;

	float thickness = r2->thickness * 0.5f;
	float dx = b.x - a.x;
	float dy = b.y - a.y;
	float ilen = 1.0f / SquareRoot(dx * dx + dy * dy);
	dx *= (thickness * ilen);
	dy *= (thickness * ilen);

	Vec3 c0 = Vec3(a.x - dy, a.y + dx, a.z);
	Vec3 c1 = Vec3(b.x - dy, b.y + dx, b.z);
	Vec3 c2 = Vec3(b.x + dy, b.y - dx, b.z);
	Vec3 c3 = Vec3(a.x + dy, a.y - dx, a.z);

	R_DrawQuad(r2, c0, c1, c2, c3, Vec2(0, 0), Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), color);
}

void R_DrawLine(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec4 color) {
	R_DrawLine(r2, Vec3(a, 0), Vec3(b, 0), color);
}

void R_PathTo(R_Renderer2d *r2, Vec2 a) {
	if (r2->path.count) {
		if (!IsNull(r2->path.Last() - a))
			r2->path.Add(a);
		return;
	}
	r2->path.Add(a);
}

void R_ArcTo(R_Renderer2d *r2, Vec2 position, float radius_a, float radius_b, float theta_a, float theta_b, int segments) {
	Assert(theta_a >= 0 && theta_a <= PI * 2 && theta_b >= 0 && theta_b <= PI * 2);

	int first_index = (int)((0.5f * theta_a * PI_INVERSE) * (float)(MAX_CIRCLE_SEGMENTS)+0.5f);
	int last_index = (int)((0.5f * theta_b * PI_INVERSE) * (float)(MAX_CIRCLE_SEGMENTS)+0.5f);

	while (first_index >= last_index)
		last_index += MAX_CIRCLE_SEGMENTS;

	auto value_count = last_index - first_index;
	segments = Min(segments, value_count);

	float npx, npy;
	for (int index = 0; index <= segments; ++index) {
		auto lookup = first_index + (int)((float)index / (float)segments * (float)value_count + 0.5f);
		lookup = lookup % MAX_CIRCLE_SEGMENTS;
		npx = UnitCircleCosValues[lookup] * radius_a;
		npy = UnitcircleSinValues[lookup] * radius_b;
		R_PathTo(r2, position + Vec2(npx, npy));
	}
}

void R_BezierQuadraticTo(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, int segments) {
	ptrdiff_t index = r2->path.count;
	if (r2->path.Resize(r2->path.count + segments + 1))
		BuildBezierQuadratic(a, b, c, &r2->path[index], segments);
}

void R_BezierCubicTo(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, int segments) {
	ptrdiff_t index = r2->path.count;
	if (r2->path.Resize(r2->path.count + segments + 1))
		BuildBezierCubic(a, b, c, d, &r2->path[index], segments);
}

static inline Vec2 R_IntersectRay(Vec2 p1, Vec2 q1, Vec2 p2, Vec2 q2) {
	Vec2 d1 = p1 - q1;
	Vec2 d2 = p2 - q2;

	float d = d1.x * d2.y - d1.y * d2.x;
	float n2 = -d1.x * (p1.y - p2.y) + d1.y * (p1.x - p2.x);

	Assert(d != 0);

	float u = n2 / d;
	return p2 - u * d2;
}

static inline void R_CalculateExtrudePoint(Vec2 point, Vec2 norm_a, Vec2 norm_b, float thickness, Vec2 *out, Vec2 *in) {
	Vec2 perp_a = Vec2(norm_a.y, -norm_a.x);
	Vec2 perp_b = Vec2(norm_b.y, -norm_b.x);

	Vec2 p1, q1, p2, q2;

	norm_a *= thickness;
	norm_b *= thickness;

	p1 = point + norm_a;
	q1 = p1 + perp_a;
	p2 = point + norm_b;
	q2 = p2 + perp_b;
	*out = R_IntersectRay(p1, q1, p2, q2);

	p1 = point - norm_a;
	q1 = p1 + perp_a;
	p2 = point - norm_b;
	q2 = p2 + perp_b;
	*in = R_IntersectRay(p1, q1, p2, q2);
}

void R_DrawPathStroked(R_Renderer2d *r2, Vec4 color, bool closed, float z) {
	if (r2->path.count < 2)
		return;

	Assert(r2->path.count == 2 ? !closed : true);

	int points_count = (int)r2->path.count;
	int vertex_count = points_count * 2;
	int index_count = points_count * 6;

	if (!closed) {
		index_count -= 6;
	}

	R_Index2d next_index = R_EnsurePrimitive(r2, vertex_count, index_count);

	if (next_index != -1) {
		R_Index2d first_index = next_index;

		R_Index2d *index = r2->write_index;

		for (int i = 0; i < points_count - 1; ++i) {
			index[0] = next_index + 0;
			index[1] = next_index + 1;
			index[2] = next_index + 3;
			index[3] = next_index + 3;
			index[4] = next_index + 2;
			index[5] = next_index + 0;

			index += 6;
			next_index += 2;
		}

		if (closed) {
			index[0] = next_index + 0;
			index[1] = next_index + 1;
			index[2] = first_index + 1;
			index[3] = first_index + 1;
			index[4] = first_index + 0;
			index[5] = next_index + 0;
		}

		Vec2 *points = r2->path.data;

		// Normal Calculation for closed polygon
		Vec2 *normals = (Vec2 *)alloca(sizeof(Vec2) * points_count);
		{
			int i = 0;
			for (; i < points_count - 1; ++i) {
				Vec2 norm = NormalizeZ(points[i + 1] - points[i]);
				normals[i].x = -norm.y;
				normals[i].y = norm.x;
			}
			Vec2 norm = NormalizeZ(points[0] - points[i]);
			normals[i].x = -norm.y;
			normals[i].y = norm.x;
		}

		float thickness = r2->thickness * 0.5f;

		R_Vertex2d *vertex = r2->write_vertex;

		Vec2 out_point;
		Vec2 in_point;

		if (closed) {
			Vec2 norm_a = normals[points_count - 1];
			Vec2 norm_b = normals[0];
			R_CalculateExtrudePoint(points[0], norm_a, norm_b, thickness, &out_point, &in_point);
		} else {
			Vec2 ext = normals[0] * thickness;
			out_point = points[0] + ext;
			in_point = points[0] - ext;
		}

		vertex[0].position = Vec3(out_point, z);
		vertex[0].color = color;
		vertex[0].tex_coord = Vec2(0);

		vertex[1].position = Vec3(in_point, z);
		vertex[1].color = color;
		vertex[1].tex_coord = Vec2(0);
		vertex += 2;

		for (int i = 1; i < points_count - 1; ++i) {
			Vec2 norm_a = normals[i - 1];
			Vec2 norm_b = normals[i];
			R_CalculateExtrudePoint(points[i], norm_a, norm_b, thickness, &out_point, &in_point);

			vertex[0].position = Vec3(out_point, z);
			vertex[0].color = color;
			vertex[0].tex_coord = Vec2(0);

			vertex[1].position = Vec3(in_point, z);
			vertex[1].color = color;
			vertex[1].tex_coord = Vec2(0);
			vertex += 2;
		}

		if (closed) {
			Vec2 norm_a = normals[points_count - 2];
			Vec2 norm_b = normals[points_count - 1];
			R_CalculateExtrudePoint(points[points_count - 1], norm_a, norm_b, thickness, &out_point, &in_point);
		} else {
			Vec2 ext = normals[points_count - 2] * thickness;
			out_point = points[points_count - 1] + ext;
			in_point = points[points_count - 1] - ext;
		}

		vertex[0].position = Vec3(out_point, z);
		vertex[0].color = color;
		vertex[0].tex_coord = Vec2(0);

		vertex[1].position = Vec3(in_point, z);
		vertex[1].color = color;
		vertex[1].tex_coord = Vec2(0);

	#if defined(RENDER_GROUP2D_ENABLE_DEBUG_INFO)
		r2->mark.path = Max(r2->mark.path, r2->path.count);
	#endif

		r2->path.Reset();
	}
}

void R_DrawPathFilled(R_Renderer2d *r2, Vec4 color, float z) {
	if (r2->path.count < 3)
		return;

	Vec2 *path = r2->path.data;
	int triangle_count = (int)r2->path.count - 2;
	for (int ti = 0; ti < triangle_count; ++ti) {
		R_DrawTriangle(r2, Vec3(path[0], z), Vec3(path[ti + 1], z), Vec3(path[ti + 2], z), color);
	}

#if defined(RENDER_GROUP2D_ENABLE_DEBUG_INFO)
	r2->mark.path = Max(r2->mark.path, r2->path.count);
#endif

	r2->path.Reset();
}

void R_DrawBezierQuadratic(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec4 color, float z, int segments) {
	R_BezierQuadraticTo(r2, a, b, c, segments);
	R_DrawPathStroked(r2, color, false, z);
}

void R_DrawBezierCubic(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec4 color, float z, int segments) {
	R_BezierCubicTo(r2, a, b, c, d, segments);
	R_DrawPathStroked(r2, color, false, z);
}

void R_DrawPolygon(R_Renderer2d *r2, const Vec2 *vertices, uint32_t count, float z, Vec4 color) {
	Assert(count >= 3);
	uint32_t triangle_count = count - 2;
	for (uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
		R_DrawTriangle(r2, Vec3(vertices[0], z), Vec3(vertices[triangle_index + 1], z), Vec3(vertices[triangle_index + 2], z), color);
	}
}

void R_DrawPolygon(R_Renderer2d *r2, const Vec2 *vertices, uint32_t count, Vec4 color) {
	R_DrawPolygon(r2, vertices, count, 0, color);
}

void R_DrawTriangleOutline(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec4 color) {
	R_PathTo(r2, a._0.xy);
	R_PathTo(r2, b._0.xy);
	R_PathTo(r2, c._0.xy);
	R_DrawPathStroked(r2, color, true, a.z);
}

void R_DrawTriangleOutline(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec4 color) {
	R_PathTo(r2, a);
	R_PathTo(r2, b);
	R_PathTo(r2, c);
	R_DrawPathStroked(r2, color, true, 0.0f);
}

void R_DrawQuadOutline(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec4 color) {
	R_PathTo(r2, a._0.xy);
	R_PathTo(r2, b._0.xy);
	R_PathTo(r2, c._0.xy);
	R_PathTo(r2, d._0.xy);
	R_DrawPathStroked(r2, color, true, a.z);
}

void R_DrawQuadOutline(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec4 color) {
	R_PathTo(r2, a);
	R_PathTo(r2, b);
	R_PathTo(r2, c);
	R_PathTo(r2, d);
	R_DrawPathStroked(r2, color, true, 0.0f);
}

void R_DrawRectOutline(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color) {
	Vec3 a = pos;
	Vec3 b = pos + Vec3(0, dim.y, 0);
	Vec3 c = pos + Vec3(dim, 0);
	Vec3 d = pos + Vec3(dim.x, 0, 0);
	R_DrawQuadOutline(r2, a, b, c, d, color);
}

void R_DrawRectOutline(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color) {
	R_DrawRectOutline(r2, Vec3(pos, 0), dim, color);
}

void R_DrawRectCenteredOutline(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color) {
	Vec2 half_dim = 0.5f * dim;

	Vec3 a, b, c, d;
	a._0.xy = pos._0.xy - half_dim;
	b._0.xy = Vec2(pos.x - half_dim.x, pos.y + half_dim.y);
	c._0.xy = pos._0.xy + half_dim;
	d._0.xy = Vec2(pos.x + half_dim.x, pos.y - half_dim.y);

	a.z = pos.z;
	b.z = pos.z;
	c.z = pos.z;
	d.z = pos.z;
	R_DrawQuadOutline(r2, a, b, c, d, color);
}

void R_DrawRectCenteredOutline(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color) {
	R_DrawRectCenteredOutline(r2, Vec3(pos, 0), dim, color);
}

void R_DrawEllipseOutline(R_Renderer2d *r2, Vec3 position, float radius_a, float radius_b, Vec4 color, int segments) {
	segments = Clamp(MIN_CIRCLE_SEGMENTS, MAX_CIRCLE_SEGMENTS - 1, segments);
	float npx, npy;
	for (int index = 0; index < segments; ++index) {
		int lookup = (int)(((float)index / (float)segments) * (MAX_CIRCLE_SEGMENTS - 1) + 0.5f);
		npx = UnitCircleCosValues[lookup] * radius_a;
		npy = UnitcircleSinValues[lookup] * radius_b;
		R_PathTo(r2, position._0.xy + Vec2(npx, npy));
	}
	R_DrawPathStroked(r2, color, true, position.z);
}

void R_DrawEllipseOutline(R_Renderer2d *r2, Vec2 position, float radius_a, float radius_b, Vec4 color, int segments) {
	R_DrawEllipseOutline(r2, Vec3(position, 0), radius_a, radius_b, color, segments);
}

void R_DrawCircleOutline(R_Renderer2d *r2, Vec3 position, float radius, Vec4 color, int segments) {
	R_DrawEllipseOutline(r2, position, radius, radius, color, segments);
}

void R_DrawCircleOutline(R_Renderer2d *r2, Vec2 position, float radius, Vec4 color, int segments) {
	R_DrawEllipseOutline(r2, Vec3(position, 0), radius, radius, color, segments);
}

void R_DrawArcOutline(R_Renderer2d *r2, Vec3 position, float radius_a, float radius_b, float theta_a, float theta_b, Vec4 color, bool closed, int segments) {
	R_ArcTo(r2, position._0.xy, radius_a, radius_b, theta_a, theta_b, segments);
	if (closed) {
		R_PathTo(r2, position._0.xy);
	}
	R_DrawPathStroked(r2, color, closed, position.z);
}

void R_DrawArcOutline(R_Renderer2d *r2, Vec2 position, float radius_a, float radius_b, float theta_a, float theta_b, Vec4 color, bool closed, int segments) {
	R_DrawArcOutline(r2, Vec3(position, 0), radius_a, radius_b, theta_a, theta_b, color, closed, segments);
}

void R_DrawArcOutline(R_Renderer2d *r2, Vec3 position, float radius, float theta_a, float theta_b, Vec4 color, bool closed, int segments) {
	R_DrawArcOutline(r2, position, radius, radius, theta_a, theta_b, color, closed, segments);
}

void R_DrawArcOutline(R_Renderer2d *r2, Vec2 position, float radius, float theta_a, float theta_b, Vec4 color, bool closed, int segments) {
	R_DrawArcOutline(r2, Vec3(position, 0), radius, radius, theta_a, theta_b, color, closed, segments);
}

void R_DrawPolygonOutline(R_Renderer2d *r2, const Vec2 *vertices, uint32_t count, float z, Vec4 color) {
	for (uint32_t index = 0; index < count; ++index) {
		R_PathTo(r2, vertices[index]);
	}
	R_DrawPathStroked(r2, color, true, z);
}

void R_DrawPolygonOutline(R_Renderer2d *r2, const Vec2 *vertices, uint32_t count, Vec4 color) {
	R_DrawPolygonOutline(r2, vertices, count, 0, color);
}

void R_DrawTexture(R_Renderer2d *r2, R_Texture *texture, Vec3 pos, Vec2 dim, Vec4 color) {
	R_PushTexture(r2, texture);
	R_DrawRect(r2, pos, dim, color);
	R_PopTexture(r2);
}

void R_DrawTexture(R_Renderer2d *r2, R_Texture *texture, Vec2 pos, Vec2 dim, Vec4 color) {
	R_PushTexture(r2, texture);
	R_DrawRect(r2, pos, dim, color);
	R_PopTexture(r2);
}

void R_DrawRoundedRect(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color, float radius, int segments) {
	if (radius) {
		float rad_x = Min(radius, 0.5f * dim.x);
		float rad_y = Min(radius, 0.5f * dim.y);

		Vec2 pos2d = pos._0.xy;

		Vec2 p0, p1, p2, p3;

		p0 = pos2d + Vec2(rad_x, rad_y);
		p1 = pos2d + Vec2(dim.x - rad_x, rad_y);
		p2 = pos2d + dim - Vec2(rad_x, rad_y);
		p3 = pos2d + Vec2(rad_x, dim.y - rad_y);

		R_ArcTo(r2, p0, rad_x, rad_y, DegToRad(180), DegToRad(270), segments);
		R_ArcTo(r2, p1, rad_x, rad_y, DegToRad(270), DegToRad(360), segments);
		R_ArcTo(r2, p2, rad_x, rad_y, DegToRad(0),   DegToRad(90), segments);
		R_ArcTo(r2, p3, rad_x, rad_y, DegToRad(90),  DegToRad(180), segments);

		R_DrawPathFilled(r2, color, pos.z);
	} else {
		R_DrawRect(r2, pos, dim, color);
	}
}

void R_DrawRoundedRect(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color, float radius, int segments) {
	R_DrawRoundedRect(r2, Vec3(pos, 0), dim, color, radius, segments);
}

void R_DrawRoundedRectOutline(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color, float radius, int segments) {
	if (radius) {
		float rad_x = Min(radius, 0.5f * dim.x);
		float rad_y = Min(radius, 0.5f * dim.y);

		Vec2 pos2d = pos._0.xy;

		Vec2 p0, p1, p2, p3;

		p0 = pos2d + Vec2(rad_x, rad_y);
		p1 = pos2d + Vec2(dim.x - rad_x, rad_y);
		p2 = pos2d + dim - Vec2(rad_x, rad_y);
		p3 = pos2d + Vec2(rad_x, dim.y - rad_y);

		R_ArcTo(r2, p0, rad_x, rad_y, DegToRad(180), DegToRad(270), segments);
		R_ArcTo(r2, p1, rad_x, rad_y, DegToRad(270), DegToRad(360), segments);
		R_ArcTo(r2, p2, rad_x, rad_y, DegToRad(0),   DegToRad(90), segments);
		R_ArcTo(r2, p3, rad_x, rad_y, DegToRad(90),  DegToRad(180), segments);

		R_DrawPathStroked(r2, color, true, pos.z);
	} else {
		R_DrawRect(r2, pos, dim, color);
	}
}

void R_DrawRoundedRectOutline(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color, float radius, int segments) {
	R_DrawRoundedRectOutline(r2, Vec3(pos, 0), dim, color, radius, segments);
}

static int R_UTF8ToCodepoint(const uint8_t *start, const uint8_t *end, uint32_t *codepoint) {
	uint32_t first = *start;

	if (first <= 0x7f) {
		*codepoint = first;
		return 1;
	}

	if ((first & 0xe0) == 0xc0) {
		if (start + 1 < end) {
			*codepoint = ((int)(start[0] & 0x1f) << 6);
			*codepoint |= (int)(start[1] & 0x3f);
			return 2;
		} else {
			*codepoint = 0xfffd;
			return (int)(end - start);
		}
	}

	if ((first & 0xf0) == 0xe0) {
		if (start + 2 <= end) {
			*codepoint = ((int)(start[0] & 0x0f) << 12);
			*codepoint |= ((int)(start[1] & 0x3f) << 6);
			*codepoint |= (int)(start[2] & 0x3f);
			return 3;
		} else {
			*codepoint = 0xfffd;
			return (int)(end - start);
		}
	}
	
	if ((first & 0xf8) == 0xf0) {
		if (start + 3 < end) {
			*codepoint = ((int)(start[0] & 0x07) << 18);
			*codepoint |= ((int)(start[1] & 0x3f) << 12);
			*codepoint |= ((int)(start[2] & 0x3f) << 6);
			*codepoint |= (int)(start[3] & 0x3f);
			return 4;
		} else {
			*codepoint = 0xfffd;
			return (int)(end - start);
		}
	}

	*codepoint = 0xfffd;

	return 1;
}

float R_PrepareText(R_Renderer2d *r2, String text, R_Font *font, float factor) {
	float width = 0;

	uint8_t *start = text.data;
	uint8_t *end = text.data + text.length;

	uint32_t codepoint;

	for (; start < end; ) {
		start += R_UTF8ToCodepoint(start, end, &codepoint);

		R_Font_Glyph *glyph = R_FontFindGlyph(font, codepoint);
		width += glyph->advance * factor;
	}

	return width;
}

void R_DrawText(R_Renderer2d *r2, Vec3 pos, Vec4 color, String text, R_Font *font, float factor) {
	R_PushTexture(r2, font->texture);

	Vec3 render_pos;
	Vec2 render_dim;

	render_pos.z = pos.z;

	uint8_t *start = text.data;
	uint8_t *end   = text.data + text.length;

	uint32_t codepoint;

	for (; start < end; ) {
		start += R_UTF8ToCodepoint(start, end, &codepoint);

		R_Font_Glyph *glyph = R_FontFindGlyph(font, codepoint);

		render_pos._0.xy = pos._0.xy + glyph->offset * factor;
		render_dim = glyph->dimension * factor;

		R_DrawRect(r2, render_pos, render_dim, glyph->uv, color);

		pos.x += glyph->advance * factor;
	}

	R_PopTexture(r2);
}

void R_DrawText(R_Renderer2d *r2, Vec2 pos, Vec4 color, String text, R_Font *font, float factor) {
	R_DrawText(r2, Vec3(pos, 0.0f), color, text, font, factor);
}

void R_DrawText(R_Renderer2d *r2, Vec3 pos, Vec4 color, String text, float factor) {
	R_DrawText(r2, pos, color, text, r2->default_font, factor);
}

void R_DrawText(R_Renderer2d *r2, Vec2 pos, Vec4 color, String text, float factor) {
	R_DrawText(r2, Vec3(pos, 0.0f), color, text, factor);
}
