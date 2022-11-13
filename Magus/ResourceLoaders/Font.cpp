#include "Kr/KrMemory.h"
#include "Kr/KrLog.h"

#include "RenderFont.h"
#include "TrueType.h"
#include "RectPack.h"
#include "RenderBackend.h"

#include <string.h>

struct R_Texture;

struct R_Font_Internal {
	void (*release_texture)(R_Texture *texture);

	M_Allocator         allocator;
	uint32_t            allocated;

	R_Font_Texture_Kind kind;
	uint32_t            width;
	uint32_t            height;
	uint8_t *           pixels;
};

R_Font *LoadFont(M_Arena *arena, const R_Font_Config &config, float height) {
	int padding        = 1;
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

		float scale = stbtt_ScaleForPixelHeight(font_info, height);
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

					replacement_present = replacement_present || (config.replacement == codepoint);
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

	uint8_t *mem = (uint8_t *)M_Alloc(allocation_size);
	if (!mem) {
		LogError("Font: Failed to allocate font");
		return nullptr;
	}

	uint8_t *gray_pixels = nullptr;
	uint8_t *rgba_pixels = nullptr;

	bool persistent_gray = (config.texture == R_FONT_TEXTURE_GRAYSCALE || config.texture == R_FONT_TEXTURE_SIGNED_DISTANCE_FIELD);

	if (persistent_gray) {
		gray_pixels = (uint8_t *)M_Alloc(texture_width * texture_height);
	} else {
		gray_pixels = M_PushArray(arena, uint8_t, texture_width * texture_height);
		rgba_pixels = (uint8_t *)M_Alloc(texture_width * texture_height * 4);

		if (!rgba_pixels) {
			LogError("Font: Failed to allocate RGBA pixels");
			M_Free(mem, allocation_size);
			return nullptr;
		}
	}

	if (!gray_pixels) {
		LogError("Font: Failed to allocate gray pixels");
		M_Free(mem, allocation_size);
		if (!persistent_gray) {
			M_Free(rgba_pixels, texture_width * texture_height * 4);
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
	font->height    = height;
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

		float scale = stbtt_ScaleForPixelHeight(font_info, height);
		float scale_x = scale * oversample_h;
		float scale_y = scale * oversample_v;

		int glyph_index = stbtt_FindGlyphIndex(font_info, (int)rect->cp);

		int x0, y0, x1, y1;
		stbtt_GetGlyphBitmapBoxSubpixel(font_info, glyph_index, scale_x, scale_y, 0, 0, &x0, &y0, &x1, &y1);

		int advance, left_side_bearing;
		stbtt_GetGlyphHMetrics(font_info, glyph_index, &advance, &left_side_bearing);

		Region uv;
		uv.min = Vec2((float)rect->x / (float)texture_width, (float)rect->y / (float)texture_height);
		uv.max = Vec2((float)(rect->x + rect->w - 1) / (float)texture_width, (float)(rect->y + rect->h - 1) / (float)texture_height);

		font->index[rect->cp] = index;

		R_Font_Glyph *glyph = &font->glyphs[index++];
		glyph->codepoint = rect->cp;
		glyph->advance = (float)advance * scale_x;
		glyph->offset = Vec2((float)x0, -(float)y1);
		glyph->dimension = Vec2((float)(x1 - x0), (float)(y1 - y0));
		glyph->uv = uv;

		Swap(&glyph->uv.min.y, &glyph->uv.max.y);

		stbtt_MakeGlyphBitmapSubpixel(font_info,
			&gray_pixels[rect->x + rect->y * texture_width], x1 - x0, y1 - y0,
			texture_width, scale_x, scale_y, 0, 0, glyph_index);
	}

	if (replacement_present) {
		index = font->index[config.replacement];
		font->replacement = &font->glyphs[index];
	} else {
		gray_pixels[texture_width * texture_height - 1] = 0xff;

		float box_width  = height * oversample_h * 0.5f;
		float box_height = height * oversample_v * 0.7f;

		Region uv = {};
		uv.min = Vec2((float)(texture_width - 1) / (float)(texture_width), (float)(texture_height - 1) / (float)(texture_height));
		uv.max = Vec2(1.0f, 1.0f);

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

void FreeFontTexturePixels(R_Font *font) {
	R_Font_Internal *_internal = (R_Font_Internal *)font->_internal;
	if (_internal->pixels) {
		uint32_t count = (_internal->kind == R_FONT_TEXTURE_RGBA_COLOR || _internal->kind == R_FONT_TEXTURE_RGBA) ? 4 : 1;
		uint32_t allocated = _internal->width * _internal->height * count;

		M_Free(_internal->pixels, allocated, _internal->allocator);
		_internal->pixels = nullptr;
	}
}

bool UploadFontTexture(R_Device *device, R_Font *font) {
	R_Font_Internal *_internal = (R_Font_Internal *)font->_internal;

	R_Format format;
	uint32_t pitch;

	if (_internal->kind == R_FONT_TEXTURE_GRAYSCALE || _internal->kind == R_FONT_TEXTURE_SIGNED_DISTANCE_FIELD) {
		format = R_FORMAT_R8_UNORM;
		pitch = _internal->width;
	} else {
		format = R_FORMAT_RGBA8_UNORM;
		pitch = _internal->width * 4;
	}

	font->texture = R_CreateTexture(device, format, _internal->width, _internal->height, pitch, _internal->pixels, 0);
	if (font->texture) {
		_internal->release_texture = R_DestroyTexture;
		FreeFontTexturePixels(font);
		return true;
	}

	return false;
}

void ReleaseFont(R_Font *font) {
	FreeFontTexturePixels(font);

	R_Font_Internal *_internal = (R_Font_Internal *)font->_internal;
	if (_internal->release_texture && font->texture) {
		_internal->release_texture(font->texture);
	}

	M_Free(font, _internal->allocated, _internal->allocator);
}
