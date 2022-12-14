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

constexpr int MAX_STATE = 50;

struct State {
	Vec2 x[MAX_STATE];
	Vec2 v[MAX_STATE];
};

struct Derivative {
	Vec2 dx[MAX_STATE];
	Vec2 dv[MAX_STATE];
};

Derivative operator+(const Derivative &a, const Derivative &b) {
	Derivative d;
	for (int i = 0; i < MAX_STATE; ++i) {
		d.dx[i] = a.dx[i] + b.dx[i];
		d.dv[i] = a.dv[i] + b.dv[i];
	}
	return d;
}

Derivative operator-(const Derivative &a, const Derivative &b) {
	Derivative d;
	for (int i = 0; i < MAX_STATE; ++i) {
		d.dx[i] = a.dx[i] - b.dx[i];
		d.dv[i] = a.dv[i] - b.dv[i];
	}
	return d;
}

Derivative operator*(const Derivative &a, float v) {
	Derivative d;
	for (int i = 0; i < MAX_STATE; ++i) {
		d.dx[i] = a.dx[i] * v;
		d.dv[i] = a.dv[i] * v;
	}
	return d;
}

Derivative operator*(float v, const Derivative &a) {
	return a * v;
}

Derivative operator-(const Derivative &a) {
	Derivative d;
	for (int i = 0; i < MAX_STATE; ++i) {
		d.dx[i] = -a.dx[i];
		d.dv[i] = -a.dv[i];
	}
	return d;
}

struct System {
	virtual Derivative Evaluate(const State &state, float t) const = 0;
};

Derivative Evaluate(const System &f, const State &initial, float t) {
	return f.Evaluate(initial, t);
}

State NextState(const State &state, const Derivative &d, float dt) {
	State output;
	for (int i = 0; i < MAX_STATE; ++i) {
		output.x[i] = state.x[i] + d.dx[i] * dt;
		output.v[i] = state.v[i] + d.dv[i] * dt;
	}
	return output;
}

Derivative Evaluate(const System &f, const State &initial, float t, float dt, const Derivative &d) {
	State state = NextState(initial, d, dt);
	return Evaluate(f, state, t + dt);
}

State IntegrateEuler(const System &f, const State &state, float t, float dt) {
	Derivative d = Evaluate(f, state, t);
	return NextState(state, d, dt);
}

State IntegrateModifiedEuler(const System &f, const State &state, float t, float dt) {
	Derivative k1, k2;

	k1 = Evaluate(f, state, t);
	k2 = Evaluate(f, state, t, dt, k1);

	Derivative d = 0.5f * (k1 + k2);

	return NextState(state, d, dt);
}

State IntegrateRK4(const System &f, const State &state, float t, float dt) {
	Derivative k1, k2, k3, k4;

	k1 = Evaluate(f, state, t);
	k2 = Evaluate(f, state, t, dt * 0.5f, k1);
	k3 = Evaluate(f, state, t, dt * 0.5f, k2);
	k4 = Evaluate(f, state, t, dt, k3);

	Derivative d = 1.0f / 6.0f * (k1 + 2.0f * (k2 + k3) + k4);

	return NextState(state, d, dt);
}

Vec2 ComputeSpringForce(Vec2 a, Vec2 b, Vec2 v, float k1, float k2, float length) {
	float dist = Distance(a, b);
	float x = dist - length;
	return (-k1 * x * NormalizeZ(a - b) - k2 * v);
}

Vec2 ComputeBungeeForce(Vec2 a, Vec2 b, Vec2 v, float k1, float k2, float length) {
	float dist = Distance(a, b);

	if (dist <= length) return Vec2(0);

	float x = dist - length;
	return (-k1 * x * NormalizeZ(a - b) - k2 * v);
}

struct Rigid_Body {
	float imass;
	Vec2  acceleration;
	Vec2  force;
};

Rigid_Body bodies[MAX_STATE];

struct Force_Generator {
	virtual void Generate(Rigid_Body *bodies, const State &state, float t) = 0;
};

struct Rope_Force_Generator : Force_Generator {
	float k1     = 0.5f;
	float k2     = 0.1f;
	float length = 0.1f;

	uint indices[2] = { 0,1 };

	Rope_Force_Generator() = default;
	Rope_Force_Generator(uint i, uint j, float rest) {
		indices[0] = i;
		indices[1] = j;
		length = rest;
	}

	virtual void Generate(Rigid_Body *bodies, const State &state, float t) {
		uint i = indices[0], j = indices[1];
		Vec2 v = state.v[i] - state.v[j];
		Vec2 f = ComputeBungeeForce(state.x[i], state.x[j], v, k1, k2, length);

		bodies[i].force += f;
		bodies[j].force -= f;
	}
};

void ClearForces() {
	for (int i = 0; i < MAX_STATE; ++i) {
		bodies[i].force = Vec2(0);
	}
}

static Array<Rope_Force_Generator *> Forces;

static Force_Generator *Dragging;

void ComputeForces(const State &state, float t) {
	for (Force_Generator *force : Forces) {
		force->Generate(bodies, state, t);
	}
	if (Dragging) {
		Dragging->Generate(bodies, state, t);
	}
}

struct Rigid_Body_System : System {
	virtual Derivative Evaluate(const State &state, float t) const {
		ClearForces();
		ComputeForces(state, t);

		Derivative d;
		for (int i = 0; i < MAX_STATE; ++i) {
			d.dx[i] = state.v[i];
			d.dv[i] = bodies[i].force * bodies[i].imass + bodies[i].acceleration;
		}
		return d;
	}
};

void DrawSpring(R_Renderer2d *renderer, Vec2 a, Vec2 b, float relaxed_length, int turns) {
	Vec2 normal = PerpendicularVector(a, b);

	int sections = (int)relaxed_length * turns - 1;
	for (int i = 0; i <= sections; ++i) {
		float t = (float)i / (float)sections;
		Vec2 p = Lerp(a, b, t);
		float sign = (float)(i & 1) ? -1 : 1;

		if (IsInRange(1, sections - 1, i))
			p += sign * normal * 0.25f;

		R_PathTo(renderer, p);
	}
	R_DrawPathStroked(renderer, Vec4(1, 1, 1, 1));
}

struct Dragging_Force_Generator : Force_Generator {
	uint i = 0;
	Vec2 pos = Vec2(0);

	float k = 1;

	virtual void Generate(Rigid_Body *bodies, const State &state, float t) {
		Vec2 x = state.x[i];
		float dist = Distance(x, pos);

		bodies[i].force += dist * k * NormalizeZ(pos - x);
	}
};

uint FindNearestBody(const State &state, Vec2 p, float *dist) {
	float dist2  = LengthSq(p - state.x[0]);
	uint nearest = 0;

	for (uint i = 1; i < (uint)MAX_STATE; ++i) {
		float next2 = LengthSq(p - state.x[i]);
		if (next2 < dist2) {
			dist2  = next2;
			nearest = i;
		}
	}

	*dist = SquareRoot(dist2);

	return nearest;
}

enum Collision_Kind {
	COLLISION_CLEAR,
	COLLISION_COLLIDING,
	COLLISION_PENETRATING
};

struct Constraint {
	float distance;
	uint i, j;
};

Array<Constraint> Constraints;

struct Contact {
	//Vec2 point; // TODO: support for contact points
	float restitution;
	float penetration;
	Vec2 normal;
	uint i, j;
};

struct Collision {
	Collision_Kind kind;

	//float t;

	Array_View<Contact> contacts;
};

Collision DetectCollisions(const State &state, float epsilon) {
	Collision result = { COLLISION_CLEAR };

	// TODO: Collision detection

	Array<Contact> contacts;
	contacts.allocator = M_GetArenaAllocator(ThreadScratchpad());

	for (const auto &constraint : Constraints) {
		Vec2 a = state.x[constraint.i];
		Vec2 b = state.x[constraint.j];

		float dist2 = LengthSq(b - a);
		float max2  = constraint.distance * constraint.distance;

		if (dist2 <= max2) continue;

		float dist = SquareRoot(dist2);
		float penetration = dist - constraint.distance;

		if (penetration > epsilon) {
			//result.t = constraint.distance / dist;

			// TODO: Find time of collision
			result.kind = COLLISION_PENETRATING;
		}

		Vec2 normal   = NormalizeZ(b - a);
		Vec2 velocity = state.v[constraint.j] - state.v[constraint.i];

		if (DotProduct(normal, velocity) > 0.0f) {
			if (result.kind != COLLISION_PENETRATING)
				result.kind = COLLISION_COLLIDING;
			Append(&contacts, Contact{ 0.1f, penetration, normal, constraint.i, constraint.j });
		}
	}

	result.contacts = contacts;

	return result;
}

void ResolveCollisions(State *state, const Collision &collision, float dt) {
	//Assert(collision.kind != COLLISION_PENETRATING);

	if (collision.kind == COLLISION_CLEAR)
		return;

	// TODO: Solve all contacts at once

	for (const Contact &contact : collision.contacts) {
		Vec2 rvel = state->v[contact.j] - state->v[contact.i];
		float separation = DotProduct(rvel, contact.normal);

		float impulse_denominator = bodies[contact.i].imass + bodies[contact.j].imass;
		if (impulse_denominator == 0.0f) continue;

		float bounce = separation;

		Vec2 relative = (bodies[contact.j].acceleration - bodies[contact.i].acceleration) * dt;
		float acc_separation = DotProduct(relative, contact.normal);

		if (acc_separation > 0) {
			bounce -= acc_separation;
			bounce = Max(0.0f, bounce);
		}

		float impulse_numerator = -separation - bounce * contact.restitution;

		float impulse = impulse_numerator / impulse_denominator;

		state->v[contact.i] -= impulse * bodies[contact.i].imass * contact.normal;
		state->v[contact.j] += impulse * bodies[contact.j].imass * contact.normal;

		float penetration = contact.penetration / impulse_denominator;
		state->x[contact.i] += penetration * bodies[contact.i].imass * contact.normal;
		state->x[contact.j] -= penetration * bodies[contact.j].imass * contact.normal;
	}
}

State Integrate(const System &f, const State &state, float t, float dt) {
	return IntegrateModifiedEuler(f, state, t, dt);
}

State Step(const System &f, const State &state, float t, float dt) {
	float current = 0.0f;
	float target  = dt;

	State initial = state;
	State next;

	for (; current < dt;) {
		next = Integrate(f, initial, t + current, target - current);

		constexpr float STEP_EPSILON = 0.00001f;
		constexpr float EPSILON = 0.5f; // todo: this should depend on the size/scale

		Collision collision = DetectCollisions(next, EPSILON);

		if (collision.kind == COLLISION_PENETRATING) {
			LogWarning("Penetrating");

			// Simulation gone too far, subdivide the step
			target = (current + target) * 0.5f; // TODO: Get target value from "DetectCollisions" function when possible
			//target = Lerp(current, target, collision.t);


			// Ensure that we are moving forward
			// Interpenetrations at the start of the frame may cause this
			if ((target - current) >= STEP_EPSILON) {
				ResolveCollisions(&next, collision, target - current);

				current = target;
				target = dt;

				initial = next;
			}
		} else {
			ResolveCollisions(&next, collision, target - current);

			current = target;
			target  = dt;

			initial = next;
		}
	}

	return next;
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

	Manager.device     = device;
	Manager.temp_arena = M_ArenaAllocate(GigaBytes(1));
	Manager.allocator  = M_GetDefaultHeapAllocator();

	H_Pipeline pipeline  = LoadPipeline("Shaders/HLSL/Quad.shader");

	float view_height = 10.0f;

	float width, height;
	R_GetRenderTargetSize(swap_chain, &width, &height);

	float aspect_ratio  = width / height;

	Dragging_Force_Generator dragging;

	Vec2 cursor = Vec2(0);

	uint64_t counter  = PL_GetPerformanceCounter();
	float frequency   = (float)PL_GetPerformanceFrequency();

	const float dt    = 1.0f / 60.0f;
	float accumulator = dt;
	float t = 0.0f;

	State state;
	memset(&state, 0, sizeof(state));
	memset(bodies, 0, sizeof(bodies));

	const float x_start_pos = -2.0f;
	const float y_start_pos = 4.0f;

#if 0
	const float x_sep_dist = 0.5f;
	const float y_sep_dist = 0.25f;

	float x_pos = x_start_pos;

	for (int x = 0; x < 10; ++x) {
		float y_pos = y_start_pos;

		uint i = x * 5;

		state.x[i] = Vec2(x_pos, y_pos);
		y_pos -= y_sep_dist;

		if (i + 5 < MAX_STATE)
			Append(&Forces, new Rope_Force_Generator(i, i + 5, x_sep_dist));

		i += 1;
		for (int y = 1; y < 5; ++y, ++i) {
			state.x[i] = Vec2(x_pos, y_pos);
			bodies[i].imass = 1.0f / 0.1f;
			bodies[i].acceleration = Vec2(0, -1);

			y_pos -= y_sep_dist;

			Append(&Forces, new Rope_Force_Generator(i - 1, i, y_sep_dist));

			if (i + 5 < MAX_STATE)
				Append(&Forces, new Rope_Force_Generator(i, i + 5, x_sep_dist));
		}

		x_pos += x_sep_dist;
	}
#elif 1
	const float x_sep_dist = 0.5f;
	const float y_sep_dist = 1.f;

	float x_pos = x_start_pos;

	for (int x = 0; x < 10; ++x) {
		float y_pos = y_start_pos;

		uint i = x * 5;

		state.x[i] = Vec2(x_pos, y_pos);
		y_pos -= y_sep_dist;

		if (i + 5 < MAX_STATE) {
			Append(&Constraints, Constraint{ x_sep_dist, i, i + 5 });
		}

		i += 1;
		for (int y = 1; y < 5; ++y, ++i) {
			state.x[i] = Vec2(x_pos, y_pos);
			bodies[i].imass = 1.0f / 0.1f;
			bodies[i].acceleration = Vec2(0, -1);

			y_pos -= y_sep_dist;

			Append(&Constraints, Constraint{ y_sep_dist, i - 1, i });

			if (i + 5 < MAX_STATE)
				Append(&Constraints, Constraint{ x_sep_dist, i, i + 5 });
		}

		x_pos += x_sep_dist;
	}
#else
	float x_pos = x_start_pos;

	for (int x = 0; x < 10; ++x) {
		float y_pos = y_start_pos;

		uint i = x * 5;

		state.x[i] = Vec2(x_pos, y_pos);
		y_pos -= y_sep_dist;

		i += 1;
		for (int y = 1; y < 5; ++y, ++i) {
			state.x[i] = Vec2(x_pos, y_pos);
			bodies[i].imass = 1.0f / 0.1f;
			bodies[i].acceleration = Vec2(0, -1);

			y_pos -= y_sep_dist;
		}

		x_pos += x_sep_dist;
	}

	Append(&Constraints, Constraint{ 2.0f, 0, 1 });
#endif

	Rigid_Body_System system;


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

			if (e.kind == PL_EVENT_BUTTON_PRESSED) {
				if (e.button.id == PL_BUTTON_LEFT) {
					float dist;
					uint i = FindNearestBody(state, cursor, &dist);
					
					if (dist < 2.0f) {
						dragging.i  = i;
						Dragging = &dragging;
					}
				}
			}

			if (e.kind == PL_EVENT_BUTTON_RELEASED) {
				if (e.button.id == PL_BUTTON_LEFT) {
					Dragging = nullptr;
				}
			}

			if (e.kind == PL_EVENT_RESIZE) {
				R_Flush(queue);
				R_ResizeRenderTargets(device, swap_chain, e.resize.w, e.resize.h);
			}
		}

		dragging.pos = cursor;

		while (accumulator >= dt) {
			state = Step(system, state, t, dt);
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

		for (int i = 0; i < MAX_STATE; ++i)
			R_DrawCircle(renderer, state.x[i], 0.1f, Vec4(1));

		for (const auto &force: Forces) {
			Vec2 a = state.x[force->indices[0]];
			Vec2 b = state.x[force->indices[1]];
			R_DrawLine(renderer, a, b, Vec4(1));
		}

		for (const auto &constraint : Constraints) {
			Vec2 a = state.x[constraint.i];
			Vec2 b = state.x[constraint.j];
			R_DrawLine(renderer, a, b, Vec4(1));
		}

		if (Dragging) {
			R_DrawLine(renderer, state.x[dragging.i], cursor, Vec4(1, 1, 0, 1));
		}

		//DrawSpring(renderer, state.x[0], state.x[1], rope.length, 20);
		//DrawSpring(renderer, state.x[1], Vec2(0), mass_spring.length, 20);


		//R_DrawCircleOutline(renderer, state.x, 0.5f, Vec4(1, 1, 0, 1));
		//R_DrawLine(renderer, state.x, Vec2(0), Vec4(1, 1, 0, 1));

		/////////////////////////////////////////////////////////////////////////////

		R_CameraView(renderer, 0.0f, width, 0.0f, height, -1.0f, 1.0f);
		String text = TmpFormat("%ms % FPS", frame_time_ms, (int)(1000.0f / frame_time_ms));

		R_DrawText(renderer, Vec2(0.0f, height - 25.0f), Vec4(1, 1, 0, 1), text);
		R_DrawText(renderer, Vec2(0.0f, height - 50.0f), Vec4(1), TmpFormat("%", state.x[0]));
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
