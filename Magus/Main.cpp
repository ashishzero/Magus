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

#include "KrPhysics.h"
#include "KrCollision.h"

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

struct Input_Map {
	PL_Key left[2];
	PL_Key right[2];
	PL_Key up[2];
	PL_Key down[2];
};

struct Controller {
	Vec2 axis;
};

void ApplyDrag(Rigid_Body *body, float k1, float k2) {
	Vec2 velocity = body->dP;
	float speed   = Length(velocity);
	float drag    = k1 * speed + k2 * speed * speed;

	if (speed)
		velocity /= speed;

	Vec2 force = -velocity * drag;
	ApplyForce(body, force);
}

void ApplyGravity(Rigid_Body *body, Vec2 g) {
	if (body->invM == 0.0f) return;
	Vec2 force = g / body->invM;
	ApplyForce(body, force);
}

void ApplySpring(Rigid_Body *a, Rigid_Body *b, Vec2 a_connection, Vec2 b_connection, float k, float rest_length) {
	a_connection = LocalToWorld(a, a_connection);
	b_connection = LocalToWorld(b, b_connection);

	Vec2 dir     = a_connection - b_connection;
	float length = Length(dir);

	float magnitude = Absolute(rest_length - length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);

	ApplyForce(a, -force, a_connection);
	ApplyForce(b, force, b_connection);
}

void ApplySpring(Rigid_Body *body, Vec2 connection, Vec2 anchor, float k, float rest_length) {
	connection = LocalToWorld(body, connection);

	Vec2 dir = connection - anchor;
	float length = Length(dir);

	float magnitude = Absolute(rest_length - length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(body, -force);
}

void ApplyBungee(Rigid_Body *a, Rigid_Body *b, Vec2 connection_a, Vec2 connection_b, float k, float rest_length) {
	connection_a = LocalToWorld(a, connection_a);
	connection_b = LocalToWorld(b, connection_b);

	Vec2 dir = connection_a - connection_b;
	float length = Length(dir);

	if (length <= rest_length) return;

	float magnitude = (length - rest_length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(a, -force);
	ApplyForce(b, force);
}

void ApplyBungee(Rigid_Body *body, Vec2 connection, Vec2 anchor, float k, float rest_length) {
	connection = LocalToWorld(body, connection);

	Vec2 dir = connection - anchor;
	float length = Length(dir);

	if (length <= rest_length) return;

	float magnitude = (length - rest_length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(body, -force);
}

void ApplyBouyancy(Rigid_Body *body, Vec2 bouyancy_center, float submerged_volume, float volume, float liq_density, Vec2 gravity) {
	if (submerged_volume < 0)
		return;

	if (submerged_volume <= volume) {
		Vec2 force = liq_density * gravity * submerged_volume;
		ApplyForceAtBodyPoint(body, -force, bouyancy_center);
	}
}

void PrepareForNextFrame(Rigid_Body *body) {
	body->F = Vec2(0);
	body->T = 0;
}

void Integrate(Rigid_Body *body, float dt) {
	uint skip = RIGID_BODY_IS_STATIC | ~RIGID_BODY_IS_AWAKE;
	if (body->Flags & skip)
		return;

	Vec2 d2P = body->d2P;
	d2P += body->F * body->invM;
	body->dP += d2P * dt;
	body->dP *= Pow(body->DF, dt);
	body->P += body->dP * dt;

	if (body->Flags & RIGID_BODY_ROTATES) {
		float d2W = body->T * body->invI;
		body->dW += d2W * dt;
		body->dW *= Pow(body->WDF, dt);
		body->W = ComplexProduct(body->W, Arm(body->dW * dt));
		body->W = NormalizeZ(body->W);
	}
}

#if 0

void CalculateSolverData(Contact_Manifold *manifold, float dt) {
	const Rigid_Body_Pair &pair = manifold->pair;

	manifold->data.transform.rows[0] = manifold->normal;
	manifold->data.transform.rows[1] = Vec2(-manifold->normal.y, manifold->normal.x);

	manifold->data.relative_positions[0] = manifold->point - pair.bodies[0]->position;
	manifold->data.relative_positions[1] = manifold->point - pair.bodies[1]->position;

	Vec2 velocity = pair.bodies[0]->velocity - pair.bodies[1]->velocity;
	velocity += ComplexProduct(Arm(pair.bodies[0]->rotation), manifold->data.relative_positions[0]);
	velocity -= ComplexProduct(Arm(pair.bodies[1]->rotation), manifold->data.relative_positions[1]);

	// TODO: use this??
	Vec2 constant_velocity = dt * (pair.bodies[0]->acceleration - pair.bodies[1]->acceleration);

	manifold->data.closing_velocity = TransformPointTransposed(manifold->data.transform, velocity);

}

void ResolvePosition(Contact_Manifold *manifold) {
	const Rigid_Body_Pair &pair = manifold->pair;

	float delta_rot[2];

	float linear_inertia[2];
	float angular_inertia[2];

	float total_inertia = 0.0f;
	for (int i = 0; i < 2; ++i) {
		Vec2 relative_pos      = manifold->point - pair.bodies[i]->transform.pos;
		float impulsive_torque = CrossProduct(relative_pos, manifold->normal);
		float unit_impulse     = pair.bodies[i]->inv_inertia * impulsive_torque;
		Vec2  unit_rotation    = ComplexProduct(Arm(unit_impulse), relative_pos);

		angular_inertia[i] = DotProduct(unit_rotation, manifold->normal);
		linear_inertia[i]  = pair.bodies[i]->inv_mass;

		delta_rot[i]       = AngleBetween(NormalizeZ(relative_pos), manifold->normal);

		if (Absolute(delta_rot[i]) > COLLISION_RESOLVER_ANGULAR_LIMIT) {
			float penetration_frac = MapRange(0.0f, Absolute(delta_rot[i]), 0.0f, manifold->penetration, COLLISION_RESOLVER_ANGULAR_LIMIT);

			if (delta_rot[i] >= 0.0f) {
				delta_rot[i] = COLLISION_RESOLVER_ANGULAR_LIMIT;
			} else {
				delta_rot[i] = -COLLISION_RESOLVER_ANGULAR_LIMIT;
			}

			float frac     = 1.0f - penetration_frac / manifold->penetration;
			float transfer = frac * angular_inertia[i];

			angular_inertia[i] -= transfer;
			linear_inertia[i]  += transfer;
		}

		total_inertia += linear_inertia[i] + angular_inertia[i];
	}

	float inverse_inertia = 1.0f / total_inertia;

	Vec2 delta_pos   = manifold->normal * manifold->penetration;

	pair.bodies[0]->position -= linear_inertia[0] * inverse_inertia * delta_pos;
	pair.bodies[1]->position += linear_inertia[1] * inverse_inertia * delta_pos;

	pair.bodies[0]->orientation = ComplexProduct(pair.bodies[0]->orientation, Arm(-angular_inertia[0] * inverse_inertia * delta_rot[0]));
	pair.bodies[1]->orientation = ComplexProduct(pair.bodies[1]->orientation, Arm(angular_inertia[1] * inverse_inertia * delta_rot[1]));
}

void SolvePositions(Array_View<Contact_Manifold> manifolds, uint max_iters) {
	for (uint iter = 0; iter < max_iters; ++iter) {
		Contact_Manifold *worst_manifold = nullptr;
		float worst_penetration          = 0.0f;

		for (ptrdiff_t index = 0; index < manifolds.count; ++index) {
			if (manifolds[index].penetration > worst_penetration) {
				worst_manifold = &manifolds[index];
				worst_penetration = manifolds[index].penetration;
			}
		}

		if (!worst_manifold) break;

		ApplyPositionChange(worst_manifold);
	}
}

constexpr float COLLISION_RESOLVER_ANGULAR_LIMIT = DegToRad(45);

void ResolveVelocity(Contact_Manifold *manifold, float dt) {
	const Rigid_Body_Pair &pair = manifold->pair;

	Mat2 contact_transform;
	contact_transform.rows[0] = manifold->normal;
	contact_transform.rows[1] = Vec2(-manifold->normal.y, manifold->normal.x);

	Vec2 relative_pos[2];
	relative_pos[0] = manifold->point - pair.bodies[0]->transform.pos;
	relative_pos[1] = manifold->point - pair.bodies[1]->transform.pos;

	float delta = 0.0;
	for (int i = 0; i < 2; ++i) {
		float impulsive_torque = CrossProduct(relative_pos[i], manifold->normal);
		float unit_impulse     = pair.bodies[i]->inv_inertia * impulsive_torque;
		Vec2  unit_rotation    = ComplexProduct(Arm(unit_impulse), relative_pos[i]);

		delta += DotProduct(unit_rotation, manifold->normal);
		delta += pair.bodies[i]->inv_mass;
	}

	Vec2 closing_velocity = pair.bodies[0]->velocity - pair.bodies[1]->velocity;
	closing_velocity += ComplexProduct(Arm(pair.bodies[0]->rotation), relative_pos[0]);
	closing_velocity -= ComplexProduct(Arm(pair.bodies[1]->rotation), relative_pos[1]);

	Vec2  contact_velocity = TransformPointTransposed(contact_transform, closing_velocity);
	float delta_velocity   = -contact_velocity.x * (1 + manifold->restitution);

	Vec2 impulse = TransformPoint(contact_transform, Vec2(delta_velocity / delta, 0.0f));

	for (int i = 0; i < 2; ++i) {
		pair.bodies[i]->velocity += pair.bodies[i]->inv_mass * impulse;
		pair.bodies[i]->rotation += pair.bodies[i]->inv_inertia * CrossProduct(relative_pos[i], impulse);

		impulse = -impulse;
	}
}
#endif

float CalcSeparatingVelocity(Contact_Manifold *manifold) {
	Rigid_Body *a = manifold->bodies[0];
	Rigid_Body *b = manifold->bodies[1];
	Vec2 relative = a->dP - b->dP;
	float separation = DotProduct(relative, manifold->normal);
	return separation;
}

void ResolveVelocity(Contact_Manifold *manifold, float dt) {
	Rigid_Body *a = manifold->bodies[0];
	Rigid_Body *b = manifold->bodies[1];

	float inv_mass = a->invM + b->invM;
	if (inv_mass <= 0) return;

	float separation = CalcSeparatingVelocity(manifold);

	if (separation <= 0) {
		float bounce = separation * manifold->restitution;

		Vec2 relative = (a->d2P - b->d2P) * dt;
		float acc_separation = DotProduct(relative, manifold->normal);

		if (acc_separation < 0) {
			bounce -= acc_separation * manifold->restitution;
			bounce = Max(0.0f, bounce);
		}

		float delta  = -(bounce + separation);

		float impulse = delta / inv_mass;

		Vec2 impulse_per_inv_mass = impulse * manifold->normal;

		a->dP += impulse_per_inv_mass * a->invM;
		b->dP -= impulse_per_inv_mass * b->invM;
	}
}

void ResolvePosition(Contact_Manifold *manifold) {
	Rigid_Body *a = manifold->bodies[0];
	Rigid_Body *b = manifold->bodies[1];

	if (manifold->penetration > 0) {
		float inv_mass = a->invM + b->invM;
		if (inv_mass <= 0) return;

		Vec2 move_per_inv_mass = manifold->penetration / inv_mass * manifold->normal;

		a->P += move_per_inv_mass * a->invM;
		b->P -= move_per_inv_mass * b->invM;

		manifold->penetration = 0; // TODO: Is this required?
	}
}

int ResolveCollisions(Array_View<Contact_Manifold> contacts, int max_iters, float dt) {
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
		ResolvePosition(&contacts[max_i]); // TODO: update penetrations separately??
	}
	return iter;
}

int ApplyCableConstraint(Rigid_Body *a, Rigid_Body *b, float max_length, float restitution, Contact_Manifold *manifold) {
	float length = Distance(a->P, b->P);

	if (length < max_length)
		return 0;

	manifold->bodies[0]   = a;
	manifold->bodies[1]   = b;
	manifold->normal      = NormalizeZ(b->P - a->P);
	manifold->penetration = length - max_length;
	manifold->restitution = restitution;

	return 1;
}

int ApplyRodConstraint(Rigid_Body *a, Rigid_Body *b, float length, Contact_Manifold *manifold) {
	float curr_length = Distance(a->P, b->P);

	if (curr_length == length)
		return 0;

	manifold->bodies[0]   = a;
	manifold->bodies[1]   = b;
	manifold->normal      = NormalizeZ(b->P - a->P) * Sgn(curr_length - length);
	manifold->penetration = Absolute(curr_length - length);
	manifold->restitution = 0;

	return 1;
}

int ApplyMagnetRepelConstraint(Rigid_Body *a, Rigid_Body *b, float max_length, float restitution, Contact_Manifold *manifold) {
	float length = Distance(a->P, b->P);

	if (length > max_length)
		return 0;

	manifold->bodies[0]   = a;
	manifold->bodies[1]   = b;
	manifold->normal      = NormalizeZ(a->P - b->P);
	manifold->penetration = max_length - length;
	manifold->restitution = restitution;

	return 1;
}

void RandomRigidBodies(Array_View<Rigid_Body> bodies) {
	for (Rigid_Body &body : bodies) {
		body.invM = 1.0f / RandomFloat(0.1f, 0.5f);
		body.invI = 0.0f;
		body.DF = 0.8f;
		body.WDF = 0.8f;
		body.P = RandomVec2(Vec2(-5, 3), Vec2(5, 4));
		body.W = Vec2(1, 0);
		body.dP = Vec2(0);
		body.dW = 0;
		body.F = Vec2(0);
		body.d2P = Vec2(0, -10);
		body.Flags = 0;
	}
}

// TODO: Continuous collision detection

//
//
//

static String_Builder frame_builder;

#define DebugText(...) WriteFormatted(&frame_builder, __VA_ARGS__)

// todo: into renderer?
void DrawShape(R_Renderer2d *renderer, const Vec2 &point, Vec4 color = Vec4(1), const Transform2d &transform = { Identity2x2(), Vec2(0) }) {
	const Mat4 &transform4 = Translation(Vec3(transform.pos, 0.0f)) * RotationZ(transform.rot.m[0], transform.rot.m[2]);
	R_PushTransform(renderer, transform4);
	R_DrawCircle(renderer, point, 0.1f, color);
	R_PopTransform(renderer);
}

void DrawShape(R_Renderer2d *renderer, const Line_Segment &line, Vec4 color = Vec4(1), const Transform2d &transform = { Identity2x2(), Vec2(0) }) {
	const Mat4 &transform4 = Translation(Vec3(transform.pos, 0.0f)) * RotationZ(transform.rot.m[0], transform.rot.m[2]);
	R_PushTransform(renderer, transform4);
	R_DrawLine(renderer, line.a, line.b, color);
	R_PopTransform(renderer);
}

void DrawShape(R_Renderer2d *renderer, const Line &line, Vec4 color = Vec4(1), const Transform2d &transform = { Identity2x2(), Vec2(0) }) {
	Vec2 perp = Vec2(-line.normal.y, line.normal.x);

	float length = 20.0f;

	Vec2 a = -perp * length + line.normal * line.offset;
	Vec2 b = perp * length + line.normal * line.offset;

	Line_Segment segment = { a,b };
	Transform2d vt;
	vt.rot = transform.rot;
	vt.pos = Vec2(0);
	DrawShape(renderer, segment, color, vt);
}

void DrawShape(R_Renderer2d *renderer, const Rect &rect, Vec4 color = Vec4(1), const Transform2d &transform = { Identity2x2(), Vec2(0) }) {
	const Mat4 &transform4 = Translation(Vec3(transform.pos, 0.0f)) * RotationZ(transform.rot.m[0], transform.rot.m[2]);
	R_PushTransform(renderer, transform4);
	R_DrawRectOutline(renderer, rect.center, rect.half_size * 2.0f, color);
	R_PopTransform(renderer);
}

void DrawShape(R_Renderer2d *renderer, const Circle &circle, Vec4 color = Vec4(1), const Transform2d &transform = { Identity2x2(), Vec2(0) }) {
	const Mat4 &transform4 = Translation(Vec3(transform.pos, 0.0f)) * RotationZ(transform.rot.m[0], transform.rot.m[2]);
	R_PushTransform(renderer, transform4);
	R_DrawCircleOutline(renderer, circle.center, circle.radius, color);
	R_PopTransform(renderer);
}

void DrawShape(R_Renderer2d *renderer, const Capsule &capsule, Vec4 color = Vec4(1), const Transform2d &transform = { Identity2x2(), Vec2(0) }) {
	const Mat4 &transform4 = Translation(Vec3(transform.pos, 0.0f)) * RotationZ(transform.rot.m[0], transform.rot.m[2]);
	R_PushTransform(renderer, transform4);
	Vec2 dir = capsule.centers[1] - capsule.centers[0];
	float angle = atan2f(dir.y, dir.x);
	R_ArcTo(renderer, capsule.centers[0], capsule.radius, capsule.radius, angle + TAU, angle + 3 * TAU);
	R_ArcTo(renderer, capsule.centers[1], capsule.radius, capsule.radius, angle - TAU, angle + TAU);
	R_DrawPathStroked(renderer, color, true);
	R_DrawLine(renderer, capsule.centers[0], capsule.centers[1], color);
	R_PopTransform(renderer);
}

void DrawShape(R_Renderer2d *renderer, const Polygon &polygon, Vec4 color = Vec4(1), const Transform2d &transform = { Identity2x2(), Vec2(0) }) {
	const Mat4 &transform4 = Translation(Vec3(transform.pos, 0.0f)) * RotationZ(transform.rot.m[0], transform.rot.m[2]);
	R_PushTransform(renderer, transform4);
	R_DrawPolygonOutline(renderer, polygon.vertices, polygon.count, color);
	R_PopTransform(renderer);
}

static const Vec2 RectAxis[] = { Vec2(1, 0), Vec2(0, 1) };
static const Vec2 RectVertexOffsets[] = { Vec2(-1, -1), Vec2(1, -1), Vec2(1, 1), Vec2(-1, 1) };

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

	Rigid_Body bodies[5] = {};

	float boat_half_width_upper = 2.0f;
	float boat_half_width_lower = 1.8f;
	float boat_half_height      = 1.0f;

	Vec2 boat_bouyancy_centers[] = {
		Vec2(+0.0f, 0.0f),
	};

	Rigid_Body boat = {};
	boat.P        = Vec2(0);
	boat.W     = Arm(0.0f);
	boat.dP        = Vec2(0);
	boat.dW        = 0.0f;
	boat.DF         = 0.8f;
	boat.WDF = 0.8f;
	boat.invM        = 1.0f / 800.0f;
	boat.invI     = 800.0f * (Square(boat_half_width_lower) + Square(boat_half_height)) / 12.0f;
	boat.d2P    = Vec2(0, -10.0f);
	boat.F           = Vec2(0);
	boat.T          = 0.0f;
	boat.Flags           = 0;

	float ground_level = -2.0f;

	Rigid_Body ground = {};
	ground.P = Vec2(0, ground_level);
	ground.dP = Vec2(0);
	ground.F    = Vec2(0);
	ground.DF  = 1;
	ground.invM = 0;

	RandomRigidBodies(bodies);

	Array<Contact_Manifold> contacts;
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

	float angle = 0.0f;

	const float dt    = 1.0f / 60.0f;
	float accumulator = dt;

	float frame_time_ms = 0.0f;

	bool follow = false;

	Vec2 cursor_cam_pos = Vec2(0);

	float control = 0.0f;
	Vec2 init_pos = Vec2(0);

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

			if (e.kind == PL_EVENT_KEY_PRESSED && e.key.id == PL_KEY_R) {
				angle += 0.5f;
			}

			if (e.kind == PL_EVENT_KEY_PRESSED && e.key.id == PL_KEY_RETURN) {
				if (!e.key.repeat) {
					RandomRigidBodies(bodies);
					if (control == 0.0f) {
						init_pos = Vec2(0.0f);
						control = 1.0f;
					} else {
						init_pos  = cursor_cam_pos;
						control = 0.0f;
					}
					angle = 0.0f;
				}
			}

			if (e.kind == PL_EVENT_CONTROLLER_THUMB_LEFT) {
				if (e.window == window)
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
			for (Rigid_Body &body : bodies) {
				PrepareForNextFrame(&body);
			}
			PrepareForNextFrame(&boat);

			ApplyForce(&boat, Vec2(1000, 0) * controller.axis);

			//ApplyForce(&bodies[0], 10.0f * controller.axis);
			ApplyBungee(&bodies[0], &bodies[1], Vec2(0), Vec2(0), 9.0f, 0.5f);

			float submerged_height = ground.P.y - (boat.P.y - boat_half_height);

			float volume = 0.5f * 2.0f * (boat_half_width_lower + boat_half_width_upper) * 2.0f * boat_half_height;
			float submerged_volume = 0.5f * 2.0f * (boat_half_width_lower + boat_half_width_upper) * 2.0f * submerged_height;

			for (Vec2 b : boat_bouyancy_centers) {
				ApplyBouyancy(&boat, b, submerged_volume, volume, 400, Vec2(0, -10));
			}

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

			Integrate(&boat, dt);

			Reset(&contacts);

			Contact_Manifold manifold;
			for (Rigid_Body &body : bodies) {
				if (body.P.y <= ground.P.y) {
					manifold.bodies[0]   = &body;
					manifold.bodies[1]   = &ground;
					manifold.normal      = Vec2(0, 1);
					manifold.restitution = 0.5f;
					manifold.penetration = ground.P.y - body.P.y;

					Append(&contacts, manifold);
				}
			}

#if 1
			if (boat.P.y <= ground.P.y) {
				manifold.bodies[0]   = &boat;
				manifold.bodies[1]   = &ground;
				manifold.normal      = Vec2(0, 1);
				manifold.restitution = 0.5f;
				manifold.penetration = ground.P.y - boat.P.y;

				Append(&contacts, manifold);
			}
#endif

			if (ApplyCableConstraint(&bodies[0], &bodies[1], 2, 0.0f, &manifold)) {
				Append(&contacts, manifold);
			}

			iterations = ResolveCollisions(contacts, 2 * (int)contacts.count, dt);

			if (follow) {
				camera_pos = Lerp(camera_pos, boat.P, 0.5f);
				camera_dist = Lerp(camera_dist, 0.8f, 0.1f);
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

		cursor_cam_pos = MapRange(Vec2(0), Vec2(width, height),
			-0.5f * Vec2(cam_width, cam_height), 0.5f * Vec2(cam_width, cam_height),
			cursor_pos);

		R_NextFrame(renderer, R_Rect(0.0f, 0.0f, width, height));
		R_SetPipeline(renderer, GetResource(pipeline));

		R_CameraView(renderer, aspect_ratio, cam_height);
		R_SetLineThickness(renderer, 2.0f * cam_height / height);

		Mat4 camera_transform = Translation(Vec3(camera_pos, 0)) * Scale(Vec3(camera_dist, camera_dist, 1.0f));
		camera_transform      = Identity();
		Mat4 world_transform  = Inverse(camera_transform);

		// TODO: replace mat4 with transform2d in renderer
		R_PushTransform(renderer, world_transform);
		R_DrawCircle(renderer, cursor_cam_pos, 0.1f, Vec4(1));

		R_DrawRectCentered(renderer, anchor, Vec2(0.1f), Vec4(1, 1, 0, 1));
		for (const Rigid_Body &body : bodies) {
			float radius = 1.0f / body.invM;
			R_DrawCircleOutline(renderer, body.P, radius, Vec4(1));
			R_DrawRectCentered(renderer, body.P, Vec2(0.1f), Vec4(1));
			//R_DrawLine(renderer, body.position, anchor, Vec4(1, 1, 0, 1));
		}

		float radius = 1.0f / bodies[0].invM;
		R_DrawCircleOutline(renderer, bodies[0].P, radius, Vec4(1, 0, 1, 1));
		R_DrawRectCentered(renderer, bodies[0].P, Vec2(0.1f), Vec4(1));

		R_DrawLine(renderer, Vec2(-10.0f, ground.P.y), Vec2(10.0f, ground.P.y), Vec4(0, 0, 1, 1));
		R_DrawLine(renderer, bodies[0].P, bodies[1].P, Vec4(1, 0, 0, 1));

		//Mat4 transform = Translation(Vec3(boat.position, 0.0f)) * RotationZ(Vec2Arg(boat.orientation)) * Translation(Vec3(-boat.position, 0.0f));

		Mat4 transform = Translation(Vec3(boat.P, 0.0f)) * RotationZ(Vec2Arg(boat.W));

		R_PushTransform(renderer, transform);

		R_PathTo(renderer, Vec2(boat_half_width_upper, boat_half_height));
		R_PathTo(renderer, Vec2(-boat_half_width_upper, boat_half_height));
		R_PathTo(renderer, Vec2(-boat_half_width_lower, -boat_half_height));
		R_PathTo(renderer, Vec2(boat_half_width_lower, -boat_half_height));

		R_DrawPathStroked(renderer, Vec4(1), true);

		R_PopTransform(renderer);

		/////////////////////////////////////////////////////////////////////////////

		R_CameraView(renderer, 0.0f, width, 0.0f, height, -1.0f, 1.0f);
		String text = TmpFormat("%ms % FPS", frame_time_ms, (int)(1000.0f / frame_time_ms));

		R_DrawText(renderer, Vec2(0.0f, height - 25.0f), Vec4(1, 1, 0, 1), text);
		R_DrawText(renderer, Vec2(0.0f, height - 50.0f), Vec4(1), TmpBuildString(&frame_builder));

		Reset(&frame_builder);

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
