#include "Kr/KrMedia.h"
#include "Kr/KrMath.h"
#include "Kr/KrArray.h"
#include "Kr/KrString.h"
#include "Kr/KrLog.h"
#include "Kr/KrMap.h"
#include "Kr/KrMathFormat.h"
#include "Kr/KrString.h"
#include "Kr/KrRandom.h"

#include "Render2d.h"
#include "RenderBackend.h"
#include "Render2dBackend.h"
#include "ResourceLoaders/Loaders.h"

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

#if 0
int GridMap[100][100];

int GetGrid(int x, int y) {
	x = x + 50;
	y = y + 50;

	if (IsInRange(0, 99, x) && IsInRange(0, 99, y)) {
		return GridMap[y][x];
	}

	return 0;
}

void SetGrid(int x, int y) {
	x = x + 50;
	y = y + 50;

	if (IsInRange(0, 99, x) && IsInRange(0, 99, y)) {
		GridMap[y][x] = 1;
	}
}

void ClearGrid(int x, int y) {
	x = x + 50;
	y = y + 50;

	if (IsInRange(0, 99, x) && IsInRange(0, 99, y)) {
		GridMap[y][x] = 0;
	}
}
#endif

struct Animation_Frame {
	Rect     rect;
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

struct Input_Map {
	PL_Key left[2];
	PL_Key right[2];
	PL_Key up[2];
	PL_Key down[2];
};

struct Controller {
	Vec2 axis;
};

static inline float IntersectLineLine(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4) {
	Vec2 diff = p3 - p4;

	float d = (p1.x - p2.x) * diff.y - (p1.y - p2.y) * diff.x;

	if (d) {
		float n = (p1.x - p3.x) * diff.y - (p1.y - p3.y) * diff.x;
		float t = n / d;
		return t;
	}

	return INFINITY;
}

struct Rigid_Body {
	Vec2  position;
	Vec2  velocity;
	Vec2  acceleration;
	float drag;
	float imass;
	Vec2  force;
};

void ApplyForce(Rigid_Body *body, Vec2 force) {
	body->force += force;
}

void ApplyDrag(Rigid_Body *body, float k1, float k2) {
	Vec2 velocity = body->velocity;
	float speed   = Length(velocity);
	float drag    = k1 * speed + k2 * speed * speed;

	if (speed)
		velocity /= speed;

	Vec2 force = -velocity * drag;
	ApplyForce(body, force);
}

void ApplyGravity(Rigid_Body *body, Vec2 g) {
	if (body->imass == 0.0f) return;
	Vec2 force = g / body->imass;
	ApplyForce(body, force);
}

void ApplySpring(Rigid_Body *a, Rigid_Body *b, float k, float rest_length) {
	Vec2 dir     = a->position - b->position;
	float length = Length(dir);
	
	float magnitude = (rest_length - length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(a, force);
	ApplyForce(b, -force);
}

void ApplySpring(Rigid_Body *body, Vec2 anchor, float k, float rest_length) {
	Vec2 dir = body->position - anchor;
	float length = Length(dir);

	float magnitude = (rest_length - length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(body, force);
}

void ApplyBungee(Rigid_Body *a, Rigid_Body *b, float k, float rest_length) {
	Vec2 dir = a->position - b->position;
	float length = Length(dir);

	if (length <= rest_length) return;

	float magnitude = (rest_length - length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(a, force);
	ApplyForce(b, -force);
}

void ApplyBungee(Rigid_Body *body, Vec2 anchor, float k, float rest_length) {
	Vec2 dir = body->position - anchor;
	float length = Length(dir);

	if (length <= rest_length) return;

	float magnitude = (rest_length - length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(body, force);
}

void ApplyBouyancy(Rigid_Body *body, float max_depth, float volume, float liquid_height, float liquid_density) {
	float depth = body->position.y;

	// out of water
	if (depth >= liquid_height + max_depth)
		return;

	if (depth <= liquid_height - max_depth) {
		Vec2 force = Vec2(0, liquid_density * volume);
		ApplyForce(body, force);
		return;
	}

	// partly submerged
	Vec2 force;
	force.x = 0.0f;
	force.y = liquid_density * volume * (depth - max_depth - liquid_height) / (2 * max_depth);
	ApplyForce(body, force);
}

void ClearForces(Array_View<Rigid_Body> bodies) {
	for (Rigid_Body &body : bodies) {
		body.force = Vec2(0);
	}
}

void Integrate(Rigid_Body *body, float dt) {
	if (body->imass == 0.0f) return;

	Vec2 acceleration = body->acceleration;
	acceleration += body->force * body->imass;

	body->velocity += acceleration * dt;
	body->velocity *= Pow(1.0f - body->drag, dt);
	body->position += body->velocity * dt;
}

struct Contact {
	Rigid_Body *bodies[2];
	Vec2        normal;
	float       restitution;
	float       penetration;
};

float CalcSeparatingVelocity(Contact *contact) {
	Rigid_Body *a    = contact->bodies[0];
	Rigid_Body *b    = contact->bodies[1];
	Vec2 relative    = a->velocity - b->velocity;
	float separation = DotProduct(relative, contact->normal);
	return separation;
}

void ResolveVelocity(Contact *contact, float dt) {
	Rigid_Body *a = contact->bodies[0];
	Rigid_Body *b = contact->bodies[1];

	float separation = CalcSeparatingVelocity(contact);

	if (separation <= 0) {
		float bounce = separation * contact->restitution;

		Vec2 relative = (a->acceleration - b->acceleration) * dt;
		float acc_separation = DotProduct(relative, contact->normal);

		if (acc_separation < 0) {
			bounce -= acc_separation * contact->restitution;
			bounce = Max(0.0f, bounce);
		}

		float delta = -(bounce + separation);

		float imass = a->imass + b->imass;
		if (imass <= 0) return;

		float impulse = delta / imass;

		Vec2 impulse_per_imass = impulse * contact->normal;

		a->velocity += impulse_per_imass * a->imass;
		b->velocity -= impulse_per_imass * b->imass;
	}
}

void ResolvePosition(Contact *contact) {
	Rigid_Body *a = contact->bodies[0];
	Rigid_Body *b = contact->bodies[1];

	if (contact->penetration > 0) {
		float imass = a->imass + b->imass;
		if (imass <= 0) return;

		Vec2 move_per_imass = contact->penetration / imass * contact->normal;

		a->position += move_per_imass * a->imass;
		b->position -= move_per_imass * b->imass;

		contact->penetration = 0; // TODO: Is this required?
	}
}

int ResolveCollisions(Array_View<Contact> contacts, int max_iters, float dt) {
	// TODO: separate velocity and position resolution into different loop
	int iter = 0;
	for (; iter < max_iters; ++iter) {
		float max = FLT_MAX;
		int max_i = (int)contacts.count;

		for (int i = 0; i < (int)contacts.count; ++i) {
			float sep = CalcSeparatingVelocity(&contacts[i]);
			if (sep < max && (sep < 0 || contacts[i].penetration > 0)) {
				max = sep;
				max_i = i;
			}
		}

		if (max_i == (int)contacts.count) break;

		// TODO: Remove this collision??
		ResolveVelocity(&contacts[max_i], dt);
		ResolvePosition(&contacts[max_i]); // TODO: update penetrations??
	}
	return iter;
}

int ApplyCableConstraint(Rigid_Body *a, Rigid_Body *b, float max_length, float restitution, Contact *contact) {
	float length = Distance(a->position, b->position);

	if (length < max_length)
		return 0;

	contact->bodies[0]   = a;
	contact->bodies[1]   = b;
	contact->normal      = NormalizeZ(b->position - a->position);
	contact->penetration = length - max_length;
	contact->restitution = restitution;

	return 1;
}

int ApplyRodConstraint(Rigid_Body *a, Rigid_Body *b, float length, Contact *contact) {
	float curr_length = Distance(a->position, b->position);

	if (curr_length == length)
		return 0;

	contact->bodies[0]   = a;
	contact->bodies[1]   = b;
	contact->normal      = NormalizeZ(b->position - a->position) * Sgn(curr_length - length);
	contact->penetration = Absolute(curr_length - length);
	contact->restitution = 0;

	return 1;
}

int ApplyMagnetRepelConstraint(Rigid_Body *a, Rigid_Body *b, float max_length, float restitution, Contact *contact) {
	float length = Distance(a->position, b->position);

	if (length > max_length)
		return 0;

	contact->bodies[0]   = a;
	contact->bodies[1]   = b;
	contact->normal      = NormalizeZ(a->position - b->position);
	contact->penetration = max_length - length;
	contact->restitution = restitution;

	return 1;
}

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

	Manager.device = device;
	Manager.temp_arena = M_ArenaAllocate(GigaBytes(1));
	Manager.allocator = M_GetDefaultHeapAllocator();

	H_Pipeline pipeline  = LoadPipeline("Shaders/HLSL/Quad.shader");

	Input_Map input_map;
	input_map.left[0]  = PL_KEY_LEFT;
	input_map.left[1]  = PL_KEY_A;
	input_map.right[0] = PL_KEY_RIGHT;
	input_map.right[1] = PL_KEY_D;
	input_map.up[0]    = PL_KEY_UP;
	input_map.up[1]    = PL_KEY_W;
	input_map.down[0]  = PL_KEY_DOWN;
	input_map.down[1]  = PL_KEY_S;

	Animation dance = LoadDanceAnimation();
	Animation run   = LoadRunAnimation();

	Rigid_Body bodies[5];

	float ground_level = -2.0f;

	Rigid_Body ground;
	ground.position = Vec2(0, ground_level);
	ground.velocity = Vec2(0);
	ground.force    = Vec2(0);
	ground.drag     = 0;
	ground.imass    = 0;

	for (Rigid_Body &body : bodies) {
		body.position     = RandomVec2(Vec2(-5, 3), Vec2(5, 4));
		body.velocity     = Vec2(0);
		body.acceleration = Vec2(0, -10);
		body.force        = Vec2(0);
		body.drag         = 0.2f;
		body.imass        = 1.0f / RandomFloat(0.1f, 0.5f);
	}

	Array<Contact> contacts;
	int iterations = 0;

	Controller controller;
	controller.axis = Vec2(0);

	Vec2 camera_pos = Vec2(0);
	float camera_dist = 1.0f;

	float width, height;
	R_GetRenderTargetSize(swap_chain, &width, &height);

	Vec2 cursor_pos = Vec2(0);

	uint64_t counter  = PL_GetPerformanceCounter();
	float frequency   = (float)PL_GetPerformanceFrequency();

	const float dt    = 1.0f / 60.0f;
	float accumulator = dt;

	float frame_time_ms = 0.0f;

	bool follow = false;

	bool running = true;

	while (running) {
		Array_View<PL_Event> events = PL_PollEvents();

		for (const PL_Event &e : events) {
			if (e.kind == PL_EVENT_QUIT || e.kind == PL_EVENT_CLOSE) {
				PL_DestroyWindow(window);
				running = false;
				break;
			}

			if (e.kind == PL_EVENT_KEY_PRESSED || e.kind == PL_EVENT_KEY_RELEASED) {
				float value = (float)(e.kind == PL_EVENT_KEY_PRESSED);

				for (PL_Key left : input_map.left) {
					if (left == e.key.id) {
						controller.axis.x = -value;
						break;
					}
				}

				for (PL_Key right : input_map.right) {
					if (right == e.key.id) {
						controller.axis.x = value;
						break;
					}
				}

				for (PL_Key down : input_map.down) {
					if (down == e.key.id) {
						controller.axis.y = -value;
						break;
					}
				}

				for (PL_Key up : input_map.up) {
					if (up == e.key.id) {
						controller.axis.y = value;
						break;
					}
				}
			}

			if (e.kind == PL_EVENT_KEY_PRESSED && e.key.id == PL_KEY_SPACE) {
				if (!e.key.repeat) {
					follow = !follow;
				}
			}

			if (e.kind == PL_EVENT_KEY_PRESSED && e.key.id == PL_KEY_RETURN) {
				if (!e.key.repeat) {
					for (Rigid_Body &body : bodies) {
						body.position = RandomVec2(Vec2(-5, 3), Vec2(5, 4));
						body.velocity = Vec2(0);
						body.force    = Vec2(0);
						body.drag     = 0.2f;
						body.imass    = 1.0f/ 0.2f; // 1.0f / RandomFloat(0.1f, 0.5f);
					}
				}
			}

			if (e.kind == PL_EVENT_CONTROLLER_THUMB_LEFT) {
				controller.axis = Vec2(e.controller.thumb.x, e.controller.thumb.y);
			}

			if (e.kind == PL_EVENT_CURSOR) {
				cursor_pos.x = (float)e.cursor.x;
				cursor_pos.y = (float)e.cursor.y;
			}

			if (e.kind == PL_EVENT_RESIZE) {
				R_Flush(queue);
				R_ResizeRenderTargets(device, swap_chain, e.resize.w, e.resize.h);
			}
		}

		controller.axis = NormalizeZ(controller.axis);

		Vec2 anchor = Vec2(0, 3.5f);

		while (accumulator >= dt) {
			ClearForces(bodies);

			ApplyForce(&bodies[0], 10.0f * controller.axis);

			//ApplyBungee(&bodies[1], bodies[0].position, 10, 1);

#if 0
			for (Rigid_Body &a : bodies) {
				for (Rigid_Body &b : bodies) {
					if (&a == &b) continue;

					float min_dist = 0.5f;
					float max_dist = 2.0f;
					float distance = Distance(a.position, b.position);

					if (distance < min_dist) {
						ApplySpring(&a, &b, 2, min_dist);
					} else if (distance > max_dist) {
						ApplySpring(&a, &b, 2, max_dist);
					}
				}
			}
#endif

			for (Rigid_Body &body : bodies) {
				//ApplyBouyancy(&body, 0.0f, 1.0f, water_level, 1.0f);
				//ApplyBungee(&body, anchor, 2, 3);
				Integrate(&body, dt);
			}

			Reset(&contacts);

			Contact contact;
			for (Rigid_Body &body : bodies) {
				if (body.position.y <= ground.position.y) {
					contact.bodies[0]   = &body;
					contact.bodies[1]   = &ground;
					contact.normal      = Vec2(0, 1);
					contact.restitution = 0.5f;
					contact.penetration = ground.position.y - body.position.y;

					Append(&contacts, contact);
				}
			}

			if (ApplyRodConstraint(&bodies[0], &bodies[1], 2, &contact)) {
				Append(&contacts, contact);
			}

			iterations = ResolveCollisions(contacts, 2 * (int)contacts.count, dt);

			if (follow) {
				camera_pos = Lerp(camera_pos, bodies[0].position, 0.5f);
				camera_dist = Lerp(camera_dist, 0.5f, 0.5f);
			} else {
				camera_pos = Lerp(camera_pos, Vec2(0), 0.5f);
				camera_dist = Lerp(camera_dist, 1.0f, 0.5f);
			}

			accumulator -= dt;
		}

		StepAnimation(&dance);

		R_GetRenderTargetSize(swap_chain, &width, &height);

		float cam_height = 10.0f;
		float aspect_ratio = width / height;

		float cam_width = aspect_ratio * cam_height;

		Vec2 cursor_cam_pos = MapRange(Vec2(0), Vec2(width, height),
			-0.5f * Vec2(cam_width, cam_height), 0.5f * Vec2(cam_width, cam_height),
			cursor_pos);

		R_NextFrame(renderer, R_Rect(0.0f, 0.0f, width, height));
		R_SetPipeline(renderer, GetResource(pipeline));

		R_CameraView(renderer, 0.0f, width, 0.0f, height, -1.0f, 1.0f);

		String text = TmpFormat("%ms % FPS", frame_time_ms, (int)(1000.0f / frame_time_ms));
		R_DrawText(renderer, Vec2(0.0f, height - 25.0f), Vec4(1), text);
		R_DrawText(renderer, Vec2(0.0f, height - 50.0f), Vec4(1), TmpFormat("%", iterations));
		//R_DrawText(renderer, Vec2(0.0f, height - 75.0f), Vec4(1), TmpFormat("Position %", actor.position));
		//R_DrawText(renderer, Vec2(0.0f, height - 100.0f), Vec4(1), TmpFormat("Velocity %", actor.velocity));

		/////////////////////////////////////////////////////////////////////////////

		R_CameraView(renderer, aspect_ratio, cam_height);
		R_SetLineThickness(renderer, 2.0f * cam_height / height);

		Mat4 camera_transform = Translation(Vec3(camera_pos, 0)) * Scale(Vec3(camera_dist, camera_dist, 1.0f));
		Mat4 world_transform  = Inverse(camera_transform);

		R_PushTransform(renderer, world_transform);


		Vec2 render_offset = Vec2(0);

		//DrawAnimation(renderer, Vec2(0) + render_offset, Vec2(88, 128) / 88.0f, dance);

		R_DrawRectCentered(renderer, anchor, Vec2(0.1f), Vec4(1, 1, 0, 1));

		for (const Rigid_Body &body : bodies) {
			float radius = 1.0f / body.imass;
			R_DrawCircleOutline(renderer, body.position, radius, Vec4(1));
			R_DrawRectCentered(renderer, body.position, Vec2(0.1f), Vec4(1));
			//R_DrawLine(renderer, body.position, anchor, Vec4(1, 1, 0, 1));
		}

		float radius = 1.0f / bodies[0].imass;
		R_DrawCircleOutline(renderer, bodies[0].position, radius, Vec4(1, 0, 1, 1));
		R_DrawRectCentered(renderer, bodies[0].position, Vec2(0.1f), Vec4(1));

		R_DrawLine(renderer, Vec2(-10.0f, ground.position.y), Vec2(10.0f, ground.position.y), Vec4(0, 0, 1, 1));
		R_DrawLine(renderer, bodies[0].position, bodies[1].position, Vec4(1, 0, 0, 1));

		//R_DrawRectCentered(renderer, position, Vec2(0.1f), Vec4(1, 0, 0, 1));
		//R_DrawCircle(renderer, Vec2(0), 0.1f, Vec4(1, 0, 0, 1));

		R_PopTransform(renderer);

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
