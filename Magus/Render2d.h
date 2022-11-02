#pragma once
#include "Kr/KrCommon.h"
#include "Kr/KrMath.h"
#include "Kr/KrArray.h"

#include "RenderFont.h"

#ifndef KR_RENDER2D_ENABLE_DEBUG_INFO
#if defined(BUILD_DEBUG) || defined(BUILD_DEVELOPER)
#define KR_RENDER2D_ENABLE_DEBUG_INFO
#endif
#endif

constexpr int DEFAULT_CIRCLE_SEGMENTS = 48;
constexpr int DEFAULT_BEZIER_SEGMENTS = 48;

static constexpr int MIN_CIRCLE_SEGMENTS = 12;
static constexpr int MAX_CIRCLE_SEGMENTS = 512; // MUST BE POWER OF 2

struct R_Vertex2d {
	Vec3 position;
	Vec2 tex_coord;
	Vec4 color;
};

typedef uint32_t R_Index2d;
typedef Rect     R_Rect;

struct R_Camera2d {
	float left;
	float right;
	float bottom;
	float top;
	float near;
	float far;
};

struct R_Pipeline;
struct R_Texture;

//
//
//

extern const String Renderer2dEmbeddedFont;

constexpr uint32_t  Renderer2dDefaultCodepointRange[] = { 0x20, 0xFF };

//
//
//

struct R_Font_Specification2d {
	R_Font_Config *config;
	float          height;
};

struct R_Specification2d {
	uint32_t               command;
	uint32_t               vertex;
	uint32_t               index;
	uint32_t               path;
	uint32_t               pipeline;
	uint32_t               texture;
	uint32_t               rect;
	uint32_t               transform;
	float                  thickness;
	R_Font_Specification2d font;
};

struct R_Memory2d {
	struct Information {
		size_t command;
		size_t vertex;
		size_t index;
		size_t path;
		size_t total;
	};

	Information allocated;
#if defined(KR_RENDER2D_ENABLE_DEBUG_INFO)
	Information used_mark;
#endif
};

constexpr R_Specification2d Renderer2dDefaultSpec = {
	64,
	1048576,
	1048576 * 6,
	64,
	64,
	255,
	255,
	255,
	1.0f,
	{ nullptr, 14.0f }
};

struct R_Renderer2d;

struct R_Backend2d_Draw_Data {
	R_Camera2d camera;
	Mat4       transform;
};

struct R_Backend2d {
	R_Texture *(*CreateTexture)(R_Backend2d *, uint32_t w, uint32_t h, uint32_t n, const uint8_t *pixels);
	R_Texture *(*CreateTextureSRGBA)(R_Backend2d *, uint32_t w, uint32_t h, const uint8_t *pixels);
	void       (*DestroyTexture)(R_Backend2d *, R_Texture *texture);
	R_Font *   (*CreateFont)(R_Backend2d *, const R_Font_Config &config, float height_in_pixels);
	void       (*DestroyFont)(R_Backend2d *, R_Font *font);

	bool (*UploadVertexData)(R_Backend2d *, void* context, void *ptr, uint32_t size);
	bool (*UploadIndexData)(R_Backend2d *, void *context, void *ptr, uint32_t size);
	void (*UploadDrawData)(R_Backend2d *, void *context, const R_Backend2d_Draw_Data &draw_data);
	void (*SetPipeline)(R_Backend2d *, void *context, R_Pipeline *pipeline);
	void (*SetScissor)(R_Backend2d *, void *context, R_Rect rect);
	void (*SetTexture)(R_Backend2d *, void *context, R_Texture *texture);
	void (*DrawTriangleList)(R_Backend2d *, void *context, uint32_t index_count, uint32_t index_offset, int32_t vertex_offset);

	void (*Release)(R_Backend2d *);
};

R_Renderer2d *R_CreateRenderer2d(R_Backend2d *backend, const R_Specification2d &spec = Renderer2dDefaultSpec);
void          R_DestroyRenderer2d(R_Renderer2d *r2);

R_Texture *   R_Backend_CreateTexture(R_Renderer2d *r2, uint32_t w, uint32_t h, uint32_t n, const uint8_t *pixels);
R_Texture *   R_Backend_CreateTextureSRGBA(R_Renderer2d *r2, uint32_t w, uint32_t h, const uint8_t *pixels);
void          R_Backend_DestroyTexture(R_Renderer2d *r2, R_Texture *texture);

R_Font *      R_Backend_CreateFont(R_Renderer2d *r2, const R_Font_Config &configs, float height_in_pixels);
R_Font *      R_Backend_CreateFont(R_Renderer2d *r2, String font_data, float height, Array_View<uint32_t> ranges = Renderer2dDefaultCodepointRange, uint32_t index = 0);
void          R_Backend_DestroyFont(R_Renderer2d *r2, R_Font *font);

R_Font_Glyph *R_FontFindGlyph(R_Font *font, uint32_t codepoint);

R_Texture *   R_DefaultTexture(R_Renderer2d *r2);
R_Font *      R_DefaultFont(R_Renderer2d *r2);

R_Backend2d * R_GetBackend(R_Renderer2d *r2);
R_Backend2d * R_SwapBackend(R_Renderer2d *r2, R_Backend2d *new_backend);
void          R_SetBackend(R_Renderer2d *r2, R_Backend2d *backend);
R_Memory2d    R_GetMemoryInformation(R_Renderer2d *r2);

//
//
//

void R_NextFrame(R_Renderer2d *r2, R_Rect region = R_Rect(0.0f, 0.0f, 1.0f, 1.0f));
void R_FinishFrame(R_Renderer2d *r2, void *context);
void R_NextDrawCommand(R_Renderer2d *r2);

void R_CameraView(R_Renderer2d *r2, float left, float right, float bottom, float top, float _near, float _far);
void R_CameraView(R_Renderer2d *r2, float aspect_ratio, float height);
void R_CameraDimension(R_Renderer2d *r2, float width, float height);

void R_SetLineThickness(R_Renderer2d *r2, float thickness);

void R_SetPipeline(R_Renderer2d *r2, R_Pipeline *pipeline);
void R_PushPipeline(R_Renderer2d *r2, R_Pipeline *pipeline);
void R_PopPipeline(R_Renderer2d *r2);

void R_SetTexture(R_Renderer2d *r2, R_Texture *texture);
void R_PushTexture(R_Renderer2d *r2, R_Texture *texture);
void R_PopTexture(R_Renderer2d *r2);

void R_SetRect(R_Renderer2d *r2, R_Rect rect);
void R_PushRect(R_Renderer2d *r2, R_Rect rect);
void R_PopRect(R_Renderer2d *r2);

void R_SetTransform(R_Renderer2d *r2, const Mat4 &transform);
void R_PushTransform(R_Renderer2d *r2, const Mat4 &transform);
void R_PopTransform(R_Renderer2d *r2);

R_Texture *R_CurrentTexture(R_Renderer2d *r2);
R_Rect     R_CurrentRect(R_Renderer2d *r2);
Mat4       R_CurrentTransform(R_Renderer2d *r2);

void R_DrawTriangle(R_Renderer2d *r2, Vec3 va, Vec3 vb, Vec3 vc, Vec2 ta, Vec2 tb, Vec2 tc, Vec4 ca, Vec4 cb, Vec4 cc);
void R_DrawTriangle(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec2 ta, Vec2 tb, Vec2 tc, Vec4 col);
void R_DrawTriangle(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 ta, Vec2 tb, Vec2 tc, Vec4 col);
void R_DrawTriangle(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec4 color);
void R_DrawTriangle(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec4 color);

void R_DrawQuad(R_Renderer2d *r2, Vec3 va, Vec3 vb, Vec3 vc, Vec3 vd, Vec2 ta, Vec2 tb, Vec2 tc, Vec2 td, Vec4 color);
void R_DrawQuad(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawQuad(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec4 color);
void R_DrawQuad(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec4 color);
void R_DrawQuad(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec3 d, R_Rect rect, Vec4 color);
void R_DrawQuad(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, R_Rect rect, Vec4 color);

void R_DrawRect(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawRect(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawRect(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color);
void R_DrawRect(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color);
void R_DrawRect(R_Renderer2d *r2, Vec3 pos, Vec2 dim, R_Rect rect, Vec4 color);
void R_DrawRect(R_Renderer2d *r2, Vec2 pos, Vec2 dim, R_Rect rect, Vec4 color);

void R_DrawRectRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawRectRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawRectRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, Vec4 color);
void R_DrawRectRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, Vec4 color);
void R_DrawRectRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, R_Rect rect, Vec4 color);
void R_DrawRectRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, R_Rect rect, Vec4 color);

void R_DrawRectCentered(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawRectCentered(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawRectCentered(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color);
void R_DrawRectCentered(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color);
void R_DrawRectCentered(R_Renderer2d *r2, Vec3 pos, Vec2 dim, R_Rect rect, Vec4 color);
void R_DrawRectCentered(R_Renderer2d *r2, Vec2 pos, Vec2 dim, R_Rect rect, Vec4 color);

void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, Vec2 uv_a, Vec2 uv_b, Vec2 uv_c, Vec2 uv_d, Vec4 color);
void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, Vec4 color);
void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, Vec4 color);
void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec3 pos, Vec2 dim, float angle, R_Rect rect, Vec4 color);
void R_DrawRectCenteredRotated(R_Renderer2d *r2, Vec2 pos, Vec2 dim, float angle, R_Rect rect, Vec4 color);

void R_DrawEllipse(R_Renderer2d *r2, Vec3 pos, float radius_a, float radius_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawEllipse(R_Renderer2d *r2, Vec2 pos, float radius_a, float radius_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);

void R_DrawCircle(R_Renderer2d *r2, Vec3 pos, float radius, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawCircle(R_Renderer2d *r2, Vec2 pos, float radius, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);

void R_DrawPie(R_Renderer2d *r2, Vec3 pos, float radius_a, float radius_b, float theta_a, float theta_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawPie(R_Renderer2d *r2, Vec2 pos, float radius_a, float radius_b, float theta_a, float theta_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawPie(R_Renderer2d *r2, Vec3 pos, float radius, float theta_a, float theta_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawPie(R_Renderer2d *r2, Vec2 pos, float radius, float theta_a, float theta_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);

void R_DrawPiePart(R_Renderer2d *r2, Vec3 pos, float radius_a_min, float radius_b_min, float radius_a_max, float radius_b_max, float theta_a, float theta_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawPiePart(R_Renderer2d *r2, Vec2 pos, float radius_a_min, float radius_b_min, float radius_a_max, float radius_b_max, float theta_a, float theta_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawPiePart(R_Renderer2d *r2, Vec3 pos, float radius_min, float radius_max, float theta_a, float theta_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawPiePart(R_Renderer2d *r2, Vec2 pos, float radius_min, float radius_max, float theta_a, float theta_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);

void R_DrawLine(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec4 color);
void R_DrawLine(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec4 color);

void R_PathTo(R_Renderer2d *r2, Vec2 a);
void R_ArcTo(R_Renderer2d *r2, Vec2 position, float radius_a, float radius_b, float theta_a, float theta_b, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_BezierQuadraticTo(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, int segments = DEFAULT_BEZIER_SEGMENTS);
void R_BezierCubicTo(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, int segments = DEFAULT_BEZIER_SEGMENTS);

void R_DrawPathStroked(R_Renderer2d *r2, Vec4 color, bool closed = false, float z = 1.0f);
void R_DrawPathFilled(R_Renderer2d *r2, Vec4 color, float z = 1.0f);

void R_DrawBezierQuadratic(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec4 color, float z = 0, int segments = DEFAULT_BEZIER_SEGMENTS);
void R_DrawBezierCubic(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec4 color, float z = 0, int segments = DEFAULT_BEZIER_SEGMENTS);

void R_DrawPolygon(R_Renderer2d *r2, const Vec2 *vertices, uint32_t count, float z, Vec4 color);
void R_DrawPolygon(R_Renderer2d *r2, const Vec2 *vertices, uint32_t count, Vec4 color);

void R_DrawTriangleOutline(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec4 color);
void R_DrawTriangleOutline(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec4 color);

void R_DrawQuadOutline(R_Renderer2d *r2, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec4 color);
void R_DrawQuadOutline(R_Renderer2d *r2, Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec4 color);

void R_DrawRectOutline(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color);
void R_DrawRectOutline(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color);

void R_DrawRectCenteredOutline(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color);
void R_DrawRectCenteredOutline(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color);

void R_DrawEllipseOutline(R_Renderer2d *r2, Vec3 position, float radius_a, float radius_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawEllipseOutline(R_Renderer2d *r2, Vec2 position, float radius_a, float radius_b, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);

void R_DrawCircleOutline(R_Renderer2d *r2, Vec3 position, float radius, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawCircleOutline(R_Renderer2d *r2, Vec2 position, float radius, Vec4 color, int segments = DEFAULT_CIRCLE_SEGMENTS);

void R_DrawArcOutline(R_Renderer2d *r2, Vec3 position, float radius_a, float radius_b, float theta_a, float theta_b, Vec4 color, bool closed, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawArcOutline(R_Renderer2d *r2, Vec2 position, float radius_a, float radius_b, float theta_a, float theta_b, Vec4 color, bool closed, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawArcOutline(R_Renderer2d *r2, Vec3 position, float radius, float theta_a, float theta_b, Vec4 color, bool closed, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawArcOutline(R_Renderer2d *r2, Vec2 position, float radius, float theta_a, float theta_b, Vec4 color, bool closed, int segments = DEFAULT_CIRCLE_SEGMENTS);

void R_DrawPolygonOutline(R_Renderer2d *r2, const Vec2 *vertices, uint32_t count, float z, Vec4 color);
void R_DrawPolygonOutline(R_Renderer2d *r2, const Vec2 *vertices, uint32_t count, Vec4 color);

void R_DrawTexture(R_Renderer2d *r2, R_Texture *texture, Vec3 pos, Vec2 dim, Vec4 color = Vec4(1));
void R_DrawTexture(R_Renderer2d *r2, R_Texture *texture, Vec2 pos, Vec2 dim, Vec4 color = Vec4(1));

void R_DrawRoundedRect(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color, float radius = 1.0f, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawRoundedRect(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color, float radius = 1.0f, int segments = DEFAULT_CIRCLE_SEGMENTS);

void R_DrawRoundedRectOutline(R_Renderer2d *r2, Vec3 pos, Vec2 dim, Vec4 color, float radius = 1.0f, int segments = DEFAULT_CIRCLE_SEGMENTS);
void R_DrawRoundedRectOutline(R_Renderer2d *r2, Vec2 pos, Vec2 dim, Vec4 color, float radius = 1.0f, int segments = DEFAULT_CIRCLE_SEGMENTS);

float R_GetFontHeight(R_Renderer2d *r2, R_Font *font, float factor = 1.0f);
float R_PrepareText(R_Renderer2d *r2, String text, R_Font *font, float factor = 1.0f);

void R_DrawText(R_Renderer2d *r2, Vec3 pos, Vec4 color, String text, R_Font *font, float factor = 1.0f);
void R_DrawText(R_Renderer2d *r2, Vec2 pos, Vec4 color, String text, R_Font *font, float factor = 1.0f);
void R_DrawText(R_Renderer2d *r2, Vec3 pos, Vec4 color, String text, float factor = 1.0f);
void R_DrawText(R_Renderer2d *r2, Vec2 pos, Vec4 color, String text, float factor = 1.0f);
