#include "Kr/KrMedia.h"
#include "Kr/KrMath.h"
#include "Kr/KrArray.h"
#include "Kr/KrString.h"
#include "Kr/KrLog.h"
#include "Kr/KrMap.h"
#include "Kr/KrString.h"
#include "Kr/KrRandom.h"

#include "Render2d.h"
#include "RenderBackend.h"
#include "Render2dBackend.h"
#include "ResourceLoaders/Loaders.h"

//#include "KrPhysics.h"
//#include "KrCollision.h"

enum Resource_Kind {
	RESOURCE_FONT,
	RESOURCE_TEXTURE,
	RESOURCE_PIPELINE,
};

typedef uint32_t Resource_Index;

template <typename T>
struct Resource {
	T        data;
	uint32_t reference;
	Span     source;
};

template <typename T>
using Resource_Table = Array<Resource<T>, void>;

struct Resource_Id {
	Resource_Kind  kind;
	Resource_Index index;
};

struct Resource_Manager {
	Map<String, Resource_Id>     lookup;

	Resource_Table<R_Font *>     fonts;
	Resource_Table<R_Texture *>  textures;
	Resource_Table<R_Pipeline *> pipelines;

	R_Device *device;

	M_Arena *temp_arena;
	M_Allocator                  allocator;
};

static Resource_Manager Manager;

template <typename T>
struct Resource_Handle {
	uint32_t index = 0;

	Resource_Handle() {}
	Resource_Handle(uint32_t i) : index(i) {}
	Resource_Handle(nullptr_t) : index(0) {}
	operator bool() { return index != 0; }
};

template <typename T>
bool operator==(Resource_Handle<T> a, Resource_Handle<T> b) {
	return a.index == b.index;
}

template <typename T>
bool operator!=(Resource_Handle<T> a, Resource_Handle<T> b) {
	return a.index != b.index;
}

typedef Resource_Handle<R_Font *>     H_Font;
typedef Resource_Handle<R_Texture *>  H_Texture;
typedef Resource_Handle<R_Pipeline *> H_Pipeline;

static String TmpReadEntireFile(const String path) {
	String content = PL_ReadEntireFile(path, M_GetArenaAllocator(Manager.temp_arena));
	return content;
}

H_Font LoadFont(const String path, float height, Array_View<uint32_t> codepoint_ranges) {
	H_Font handle = nullptr;

	Resource_Id *id = Find(Manager.lookup, path);
	if (id) {
		if (id->kind == RESOURCE_FONT) {
			if (PL_InterlockedIncrement(&Manager.fonts[id->index].reference) > 1)
				return id->index;
			handle = id->index;
		}
	}

	String content = TmpReadEntireFile(path);

	R_Font_File file;
	file.path = path;
	file.data = content;
	file.index = 0;
	file.cp_ranges = codepoint_ranges;

	R_Font_Config config;
	config.files = Array_View<R_Font_File>(&file, 1);
	config.replacement = '?';
	config.texture = R_FONT_TEXTURE_RGBA;

	R_Font *font = LoadFont(Manager.temp_arena, config, height);
	if (!font) {
		return nullptr;
	}
	if (!UploadFontTexture(Manager.device, font)) {
		ReleaseFont(font);
		return nullptr;
	}

	if (!handle) {
		Span span = Span(Manager.lookup.strings.count, path.count);
		Append(&Manager.fonts, Manager.allocator, { font,1, span });
		handle = (uint32_t)Manager.fonts.count;

		Put(&Manager.lookup, path, Resource_Id{ RESOURCE_FONT, handle.index });

		LogInfo("[ResourceManager] Loaded Font: %", path);
	}

	return handle;
}

H_Texture LoadTexture(const String path) {
	H_Texture handle = nullptr;

	Resource_Id *id = Find(Manager.lookup, path);
	if (id) {
		if (id->kind == RESOURCE_TEXTURE) {
			if (PL_InterlockedIncrement(&Manager.textures[id->index].reference) > 1)
				return id->index;
			handle = id->index;
		}
	}

	String content = TmpReadEntireFile(path);
	if (!content.data)
		return nullptr;

	R_Texture *texture = LoadTexture(Manager.temp_arena, Manager.device, content, path);
	if (!texture) {
		return nullptr;
	}

	if (!handle) {
		Span span = Span(Manager.lookup.strings.count, path.count);
		Append(&Manager.textures, Manager.allocator, { texture,1, span });
		handle = (uint32_t)Manager.textures.count;

		Put(&Manager.lookup, path, Resource_Id{ RESOURCE_TEXTURE, handle.index });

		LogInfo("[ResourceManager] Loaded Texture: %", path);
	}

	return handle;
}

H_Pipeline LoadPipeline(const String path) {
	H_Pipeline handle = nullptr;

	Resource_Id *id = Find(Manager.lookup, path);
	if (id) {
		if (id->kind == RESOURCE_PIPELINE) {
			if (PL_InterlockedIncrement(&Manager.pipelines[id->index].reference) > 1)
				return id->index;
			handle = id->index;
		}
	}

	String content = TmpReadEntireFile(path);
	if (!content.data)
		return nullptr;

	R_Pipeline *pipeline = LoadPipeline(Manager.temp_arena, Manager.device, content, path);
	if (!pipeline) {
		return nullptr;
	}

	if (!handle) {
		Span span = Span(Manager.lookup.strings.count, path.count);
		Append(&Manager.pipelines, Manager.allocator, { pipeline, 1, span });
		handle = (uint32_t)Manager.pipelines.count;

		Put(&Manager.lookup, path, Resource_Id{ RESOURCE_PIPELINE, handle.index });

		LogInfo("[ResourceManager] Loaded Pipeline: %", path);
	}

	return handle;
}

R_Font *GetResource(H_Font font) {
	return Manager.fonts[font.index - 1].data;
}

R_Texture *GetResource(H_Texture texture) {
	return Manager.textures[texture.index - 1].data;
}

R_Pipeline *GetResource(H_Pipeline pipeline) {
	return Manager.pipelines[pipeline.index - 1].data;
}

void ReleaseResource(H_Font font) {
	if (!font) return;

	uint32_t index = font.index - 1;
	Assert(Manager.fonts[index].reference);

	uint32_t val = PL_InterlockedDecrement(&Manager.fonts[index].reference);

	if (val == 0) {
		ReleaseFont(Manager.fonts[index].data);
		Manager.fonts[index].data = nullptr;

		Span span = Manager.fonts[index].source;
		String path = String(Manager.lookup.strings.data + span.index, span.count);
		LogInfo("[ResourceManager] Released Font: %", path);
	}
}

void ReleaseResource(H_Texture texture) {
	if (!texture) return;

	uint32_t index = texture.index - 1;
	Assert(Manager.textures[index].reference);

	uint32_t val = PL_InterlockedDecrement(&Manager.textures[index].reference);

	if (val == 0) {
		ReleaseTexture(Manager.textures[index].data);
		Manager.textures[index].data = nullptr;

		Span span = Manager.textures[index].source;
		String path = String(Manager.lookup.strings.data + span.index, span.count);
		LogInfo("[ResourceManager] Released Texture: %", path);
	}
}

void ReleaseResource(H_Pipeline pipeline) {
	if (!pipeline) return;

	uint32_t index = pipeline.index - 1;
	Assert(Manager.pipelines[index].reference);

	uint32_t val = PL_InterlockedDecrement(&Manager.pipelines[index].reference);

	if (val == 0) {
		ReleasePipeline(Manager.pipelines[index].data);
		Manager.pipelines[index].data = nullptr;

		Span span = Manager.pipelines[index].source;
		String path = String(Manager.lookup.strings.data + span.index, span.count);
		LogInfo("[ResourceManager] Released Pipeline: %", path);
	}
}

void ReleaseAll() {
	for (ptrdiff_t index = 0; index < Manager.fonts.count; ++index) {
		ReleaseResource(H_Font{ (uint32_t)index + 1 });
	}
	for (ptrdiff_t index = 0; index < Manager.textures.count; ++index) {
		ReleaseResource(H_Texture{ (uint32_t)index + 1 });
	}
	for (ptrdiff_t index = 0; index < Manager.pipelines.count; ++index) {
		ReleaseResource(H_Pipeline{ (uint32_t)index + 1 });
	}
}

//
//
//

struct Animation_Frame {
	Region     rect;
	uint32_t time;
};

struct Animation_Properties {
	bool repeat;
};

struct Animation {
	H_Texture            texture;

	Animation_Properties properties;

	uint32_t             time;
	uint32_t             current;
	uint32_t             count;
	Animation_Frame *    frames;
};

Animation LoadDanceAnimation() {
	Animation animation;

	animation.texture = LoadTexture("Dance.bmp");
	animation.time    = 0;
	animation.current = 0;

	animation.properties.repeat = true;

	animation.count   = 80;
	animation.frames  = new Animation_Frame[animation.count];

	for (uint32_t i = 0; i < animation.count; ++i) {
		auto frame  = &animation.frames[i];

		float x = (float)(i % 8);
		float y = (float)(i / 8);

		Vec2 frame_pos = Vec2(x, y);

		R_Rect rect;
		rect.min = Vec2(0.125f, 0.1f) * frame_pos;
		rect.max = Vec2(0.125f, 0.1f) + rect.min;

		frame->rect = rect;
		frame->time = 4;
	}

	return animation;
}

Animation LoadRunAnimation() {
	Animation animation;

	animation.texture = LoadTexture("Run.png");
	animation.time    = 0;
	animation.current = 0;

	animation.properties.repeat = true;

	animation.count  = 10;
	animation.frames = new Animation_Frame[animation.count];

	for (uint32_t i = 0; i < animation.count; ++i) {
		auto frame = &animation.frames[i];

		float x = (float)(i % 10);
		float y = (float)(i / 10);

		Vec2 frame_pos = Vec2(x, y);

		R_Rect rect;
		rect.min = Vec2(0.2f, 0.5f) * frame_pos;
		rect.max = Vec2(0.2f, 0.5f) + rect.min;

		frame->rect = rect;
		frame->time = 4;
	}

	return animation;
}

void StepAnimation(Animation *animation) {
	Animation_Frame *frame = &animation->frames[animation->current];

	if (animation->time == frame->time) {
		if (animation->current + 1 == animation->count) {
			if (!animation->properties.repeat)
				return;
			animation->current = 0;
		} else {
			animation->current += 1;
		}
		animation->time = 0;
	}

	animation->time += 1;
}

void ResetAnimation(Animation *animation) {
	animation->current = 0;
	animation->time    = 0;
}

void DrawAnimation(R_Renderer2d *renderer, Vec2 pos, Vec2 dim, const Animation &animation, Vec2 dir = Vec2(0)) {
	R_Texture *texture     = GetResource(animation.texture);
	Animation_Frame &frame = animation.frames[animation.current];

	R_Rect rect = frame.rect;
	if (dir.x < 0) {
		rect.min.x  = 1.0f - rect.min.x;
		rect.max.x  = 1.0f - rect.max.x;
	}
	if (dir.y < 0) {
		rect.min.y = 1.0f - rect.min.y;
		rect.max.y = 1.0f - rect.max.y;
	}

	R_DrawTextureCentered(renderer, texture, pos, dim, rect, Vec4(1));
}

//
//
//

int Main(int argc, char **argv) {
	PL_ThreadCharacteristics(PL_THREAD_GAMES);

	PL_Window *window = PL_CreateWindow("Magus", 0, 0, false);
	if (!window)
		FatalError("Failed to create windows");

	R_Device *device = R_CreateDevice(R_DEVICE_DEBUG_ENABLE);
	R_Queue *queue = R_CreateRenderQueue(device);

	R_Swap_Chain *swap_chain = R_CreateSwapChain(device, window);

	R_List *render_list = R_CreateRenderList(device);
	R_Renderer2d *renderer = R_CreateRenderer2dFromDevice(device);

	Manager.device     = device;
	Manager.temp_arena = M_ArenaAllocate(GigaBytes(1));
	Manager.allocator  = M_GetDefaultHeapAllocator();

	H_Pipeline pipeline  = LoadPipeline("Shaders/HLSL/Quad.shader");

	float view_height = 10.0f;

	float width, height;
	R_GetRenderTargetSize(swap_chain, &width, &height);

	float aspect_ratio  = width / height;

	Vec2 cursor = Vec2(0);

	uint64_t counter  = PL_GetPerformanceCounter();
	float frequency   = (float)PL_GetPerformanceFrequency();

	float t = 0.0f;
	const float dt      = 1.0f / 60.0f;
	float accumulator   = dt;
	float frame_time_ms = 0.0f;

	bool running = true;

	while (running) {
		Array_View<PL_Event> events = PL_PollEvents();

		for (const PL_Event &e : events) {
			if (e.kind == PL_EVENT_QUIT || e.kind == PL_EVENT_CLOSE) {
				PL_DestroyWindow(window);
				running = false;
				break;
			}

			if (e.kind == PL_EVENT_CURSOR) {
				Vec2 view_half_size = 0.5f * Vec2(aspect_ratio * view_height, view_height);
				cursor = Vec2((float)e.cursor.x, (float)e.cursor.y);
				cursor = MapRange(Vec2(0), Vec2(width, height), -view_half_size, view_half_size, cursor);
			}

			if (e.kind == PL_EVENT_RESIZE) {
				R_Flush(queue);
				R_ResizeRenderTargets(device, swap_chain, e.resize.w, e.resize.h);
			}
		}

		while (accumulator >= dt) {
			t += dt;
			accumulator -= dt;
		}

		R_GetRenderTargetSize(swap_chain, &width, &height);

		aspect_ratio  = width / height;

		R_NextFrame(renderer, R_Rect(0.0f, 0.0f, width, height));
		R_SetPipeline(renderer, GetResource(pipeline));

		R_CameraView(renderer, aspect_ratio, view_height);

		R_SetLineThickness(renderer, 2.0f * view_height / height);

		R_DrawCircle(renderer, cursor, 0.1f, Vec4(1));

		//DrawSpring(renderer, state.x[0], state.x[1], rope.length, 20);
		//DrawSpring(renderer, state.x[1], Vec2(0), mass_spring.length, 20);


		//R_DrawCircleOutline(renderer, state.x, 0.5f, Vec4(1, 1, 0, 1));
		//R_DrawLine(renderer, state.x, Vec2(0), Vec4(1, 1, 0, 1));

		/////////////////////////////////////////////////////////////////////////////

		R_CameraView(renderer, 0.0f, width, 0.0f, height, -1.0f, 1.0f);
		String text = TmpFormat("%ms % FPS", frame_time_ms, (int)(1000.0f / frame_time_ms));

		R_DrawText(renderer, Vec2(0.0f, height - 25.0f), Vec4(1, 1, 0, 1), text);
		//R_DrawText(renderer, Vec2(0.0f, height - 75.0f), Vec4(1), TmpFormat("P: %, V: %", state.x[1], state.v[1]));

		R_Viewport viewport;

		viewport.y = viewport.x = 0;
		viewport.width = width;
		viewport.height = height;
		viewport.min_depth = 0;
		viewport.max_depth = 1;

		R_Render_Target *render_target = R_GetRenderTarget(swap_chain);

		float clear_color[] = { .12f, .12f, .12f, 1.0f };
		R_ClearRenderTarget(render_list, render_target, clear_color);

		R_BindRenderTargets(render_list, 1, &render_target, nullptr);
		R_SetViewports(render_list, &viewport, 1);
		R_FinishFrame(renderer, render_list);

		R_Submit(queue, render_list);

		R_Present(swap_chain);

		ResetThreadScratchpad();

		uint64_t current = PL_GetPerformanceCounter();
		uint64_t counts  = current - counter;
		counter = current;

		frame_time_ms = ((1000000.0f * (float)counts) / frequency) / 1000.0f;
		accumulator += (frame_time_ms / 1000.0f);
	}

	R_Flush(queue);
	ReleaseAll();

	R_DestroyRenderList(render_list);
	R_DestroySwapChain(device, swap_chain);
	R_DestroyRenderQueue(queue);
	R_DestroyDevice(device);

	return 0;
}
