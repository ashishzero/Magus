#pragma once
#include "Kr/KrMath.h"

struct R_Vertex2d {
	Vec3 position;
	Vec2 tex_coord;
	Vec4 color;
};

typedef uint32_t R_Index2d;

struct R_Camera2d {
	float left;
	float right;
	float bottom;
	float top;
	float near;
	float far;
};

struct R_Command2d {
	R_Camera2d camera;
	uint16_t   transform;
	uint16_t   rect;
	uint32_t   texture;
	uint32_t   vertex_offset;
	uint32_t   index_offset;
	uint32_t   index_count;
};

struct R_Description2d {
	Array_View<R_Command2d>  commands;
	Array_View<Mat4>         transforms;
	Array_View<R_Rect>       rects;
	Array_View<R_Texture *>  textures;

	Array_View<R_Vertex2d>   vertices;
	Array_View<R_Index2d>    indices;
};

struct R_Backend2d {
	virtual R_Texture *CreateTextureRGBA(uint32_t w, uint32_t h, const uint8_t *pixels) { return nullptr; }
	virtual void       DestroyTexture2d(R_Texture *) {}
	virtual void       DrawFrame(void *, const R_Description2d &) {}
};
