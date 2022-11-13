#pragma once
#include "Kr/KrMath.h"

struct R_Font_Glyph {
	uint32_t codepoint;
	float    advance;
	Vec2     offset;
	Vec2     dimension;
	Region   uv;
};

struct R_Texture;

struct R_Font {
	float                    height;
	Array_View<uint16_t>     index;
	Array_View<R_Font_Glyph> glyphs;
	R_Font_Glyph *           replacement;
	R_Texture *              texture;
	void *                   _internal;
};

struct R_Font_File {
	String               path;
	String               data;
	uint32_t             index;
	Array_View<uint32_t> cp_ranges;
};

enum R_Font_Texture_Kind {
	R_FONT_TEXTURE_GRAYSCALE,
	R_FONT_TEXTURE_RGBA,
	R_FONT_TEXTURE_RGBA_COLOR,
	R_FONT_TEXTURE_SIGNED_DISTANCE_FIELD
};

struct R_Font_Config {
	Array_View<R_Font_File> files;
	uint32_t                replacement = '?';
	R_Font_Texture_Kind     texture = R_FONT_TEXTURE_RGBA;
};
