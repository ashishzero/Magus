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
	body->F     = Vec2(0);
	body->T    = 0;
}

void Integrate(Rigid_Body *body, float dt) {
	if (body->invM == 0.0f) return;

	Vec2 d2P = body->d2P;
	d2P += body->F * body->invM;
	float d2W = body->T * body->invI;

	body->dP += d2P * dt;
	body->dP *= Pow(body->DF, dt);

	body->dW += d2W * dt;
	body->dW *= Pow(body->WDF, dt);

	body->P += body->dP * dt;
	body->W = ComplexProduct(body->W, Arm(body->dW * dt));
	body->W = NormalizeZ(body->W);
}

struct Rigid_Body_Pair {
	Rigid_Body *bodies[2];
};

struct Contact_Solver_Data {
	Mat2 transform;
	Vec2 closing_velocity;
	//Vec2 desired_delta_velocity;
	Vec2 relative_positions[2];
};

struct Contact_Manifold {
	Rigid_Body_Pair     pair;
	Vec2                point;
	Vec2                normal;
	float               penetration;

	// Surface information
	float               restitution;
	float               friction;

	Contact_Solver_Data data;
};

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
	Rigid_Body *a = manifold->pair.bodies[0];
	Rigid_Body *b = manifold->pair.bodies[1];
	Vec2 relative = a->dP - b->dP;
	float separation = DotProduct(relative, manifold->normal);
	return separation;
}

void ResolveVelocity(Contact_Manifold *manifold, float dt) {
	Rigid_Body *a = manifold->pair.bodies[0];
	Rigid_Body *b = manifold->pair.bodies[1];

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
	Rigid_Body *a = manifold->pair.bodies[0];
	Rigid_Body *b = manifold->pair.bodies[1];

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

	manifold->pair.bodies[0]   = a;
	manifold->pair.bodies[1]   = b;
	manifold->normal      = NormalizeZ(b->P - a->P);
	manifold->penetration = length - max_length;
	manifold->restitution = restitution;

	return 1;
}

int ApplyRodConstraint(Rigid_Body *a, Rigid_Body *b, float length, Contact_Manifold *manifold) {
	float curr_length = Distance(a->P, b->P);

	if (curr_length == length)
		return 0;

	manifold->pair.bodies[0]   = a;
	manifold->pair.bodies[1]   = b;
	manifold->normal      = NormalizeZ(b->P - a->P) * Sgn(curr_length - length);
	manifold->penetration = Absolute(curr_length - length);
	manifold->restitution = 0;

	return 1;
}

int ApplyMagnetRepelConstraint(Rigid_Body *a, Rigid_Body *b, float max_length, float restitution, Contact_Manifold *manifold) {
	float length = Distance(a->P, b->P);

	if (length > max_length)
		return 0;

	manifold->pair.bodies[0]   = a;
	manifold->pair.bodies[1]   = b;
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
		body.flags = 0;
	}
}

// TODO: Continuous collision detection

//
//
//

void FillSurfaceData(Contact_Manifold *manifold, Rigid_Body_Pair pair, const Shape *first, const Shape *second) {
	manifold->pair = pair;
	//todo: fill rest of the data
	//Unimplemented();
	manifold->friction = 0;
	manifold->restitution = 0;
}

using Contact_List = Array<Contact_Manifold>;

Contact_Manifold *PushContact(Contact_List *contacts, Rigid_Body_Pair pair, const Shape *a, const Shape *b) {
	Contact_Manifold *manifold = Append(contacts);
	if (manifold) {
		FillSurfaceData(manifold, pair, a, b);
		return manifold;
	}

	static Contact_Manifold Fallback;
	LogWarning("[Physics]: Failed to allocate new manifold point");
	return &Fallback;
}

template <typename T>
const T &GetShapeData(const Shape *_shape) {
	const TShape<T> *shape = (const TShape<T> *)_shape;
	return shape->data;
}

void CollideCircleCircle(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Circle &a = GetShapeData<Circle>(fixture_a);
	const Circle &b = GetShapeData<Circle>(fixture_b);

	Vec2 a_pos = LocalToWorld(pair.bodies[0], a.center);
	Vec2 b_pos = LocalToWorld(pair.bodies[1], b.center);

	Vec2 midline   = b_pos - a_pos;
	float length2  = LengthSq(midline);
	float min_dist = a.radius + b.radius;

	if (length2 > min_dist * min_dist) {
		return;
	}

	float length;
	Vec2  normal;

	if (length2) {
		length = SquareRoot(length2);
		normal = midline / length;
	} else {
		// Degenerate case (centers of circles are overlapping)
		length = 0;
		normal = Vec2(0, 1); // arbritray normal
	}

	float factor = a.radius / (a.radius + b.radius);

	Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
	manifold->point       = a_pos + factor * midline;
	manifold->normal      = normal;
	manifold->penetration = min_dist - length;
}

void CollideCircleCapsule(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Circle &a  = GetShapeData<Circle>(fixture_a);
	const Capsule &b = GetShapeData<Capsule>(fixture_b);

	Vec2 point = LocalToWorld(pair.bodies[0], a.center);

	// Transform into Capsule's local space
	point = WorldToLocal(pair.bodies[1], point);

	Vec2 closest = NearestPointInLineSegment(point, b.centers[0], b.centers[1]);
	float dist2 = LengthSq(closest - point);
	float radius = a.radius + b.radius;

	if (dist2 > radius * radius) {
		return;
	}

	Vec2 midline = closest - point;

	float length;
	Vec2  normal;

	if (dist2) {
		length = SquareRoot(dist2);
		normal = midline / length;
	} else {
		// Degenerate case (centers of circles are overlapping), using normal perpendicular vector of capsule line
		length = 0;
		normal = PerpendicularVector(b.centers[0], b.centers[1]);

		if (!IsNull(normal))
			normal = NormalizeZ(normal);
		else
			normal = Vec2(0, 1); // capsule is circle, using arbritrary normal
	}

	float factor = a.radius / radius;
	Vec2 contact_point = point + factor * midline;

	Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
	manifold->point       = LocalToWorld(pair.bodies[1], contact_point);
	manifold->normal      = LocalDirectionToWorld(pair.bodies[1], normal);
	manifold->penetration = radius - length;
}

void CollideCirclePolygon(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Circle &a  = GetShapeData<Circle>(fixture_a);
	const Polygon &b = GetShapeData<Polygon>(fixture_b);

	Transform2d ta = CalculateRigidBodyTransform(pair.bodies[0]);
	Transform2d tb = CalculateRigidBodyTransform(pair.bodies[1]);

	Vec2 points[2];
	if (GilbertJohnsonKeerthi(a.center, ta, b, tb, points)) {
		Vec2 dir = points[1] - points[0];
		float dist2 = LengthSq(points[0] - points[1]);

		if (dist2 > a.radius * a.radius)
			return;

		float dist = SquareRoot(dist2);
		Assert(dist != 0);

		Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
		manifold->point       = points[1];
		manifold->normal      = dir / dist;
		manifold->penetration = a.radius - dist;

		return;
	}

	// Find the edge having the least distance from the origin of the circle
	Vec2 center = TransformPoint(ta, a.center);
	center = TransformPointTransposed(tb, center);

	uint best_i = b.count - 1;
	uint best_j = 0;

	Vec2 point  = NearestPointInLineSegment(center, b.vertices[b.count - 1], b.vertices[0]);
	float dist2 = LengthSq(point - center);

	for (uint i = 0; i < b.count - 1; ++i) {
		Vec2 next_point = NearestPointInLineSegment(center, b.vertices[i], b.vertices[i + 1]);
		float new_dist2 = LengthSq(next_point - center);

		if (new_dist2 < dist2) {
			dist2  = new_dist2;
			point  = next_point;
			best_i = i;
			best_j = i + 1;
		}
	}

	float dist = SquareRoot(dist2);

	Vec2 normal;
	if (dist) {
		normal = (center - point) / dist;
	} else {
		normal = PerpendicularVector(b.vertices[best_i], b.vertices[best_j]);
		normal = NormalizeZ(normal);
	}

	Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
	manifold->point       = TransformPoint(tb, point);
	manifold->normal      = TransformDirection(tb, normal);
	manifold->penetration = a.radius + dist;
}

void CollideCircleLine(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Circle &a = GetShapeData<Circle>(fixture_a);
	const Line &b   = GetShapeData<Line>(fixture_b);

	Vec2 a_pos  = LocalToWorld(pair.bodies[0], a.center);
	Vec2 normal = LocalDirectionToWorld(pair.bodies[1], b.normal);

	float perp_dist = DotProduct(normal, a_pos);

	float dist = perp_dist - a.radius - b.offset;

	if (dist > 0) {
		return;
	}

	Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
	manifold->point       = a_pos - (dist + a.radius) * normal;
	manifold->normal      = normal;
	manifold->penetration = -dist;
}

void CollideCapsuleCapsule(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Capsule &a = GetShapeData<Capsule>(fixture_a);
	const Capsule &b = GetShapeData<Capsule>(fixture_b);

	Transform2d ta = CalculateRigidBodyTransform(pair.bodies[0]);
	Transform2d tb = CalculateRigidBodyTransform(pair.bodies[1]);

	Line_Segment l1, l2;
	l1.a = TransformPoint(ta, a.centers[0]);
	l1.b = TransformPoint(ta, a.centers[1]);
	l2.a = TransformPoint(tb, b.centers[0]);
	l2.b = TransformPoint(tb, b.centers[1]);

	Line_Segment points = NearestPointsInLineSegments(l1, l2);

	Vec2  midline = points.b - points.a;
	float dist2   = LengthSq(midline);
	float radius  = a.radius + b.radius;

	if (dist2 > radius * radius) {
		return;
	}

	float dist = SquareRoot(dist2);

	Vec2 dir1 = l1.b - l1.a;
	Vec2 dir2 = l2.b - l2.a;

	float length1 = LengthSq(dir1);
	float length2 = LengthSq(dir2);

	dir1 = NormalizeZ(dir1);
	dir2 = NormalizeZ(dir2);

	if (Absolute(Determinant(dir1, dir2)) <= REAL_EPSILON) {
		// Capsules are parallel

		if (length1 && length2) {
			Line_Segment reference, incident;
			Vec2 dir;

			float incident_r;

			if (length1 >= length2) {
				dir        = dir1;
				reference  = l1;
				incident   = l2;
				incident_r = b.radius;
			} else {
				dir        = dir2;
				reference  = l2;
				incident   = l1;
				incident_r = a.radius;
			}

			float min = DotProduct(dir, reference.a);
			float max = DotProduct(dir, reference.b);
			float d1  = DotProduct(dir, incident.a);
			float d2  = DotProduct(dir, incident.b);

			// capsules are stacked
			if (IsInRange(min, max, d1) || IsInRange(min, max, d2)) {
				float clipped_d1 = Clamp(min, max, d1);
				float clipped_d2 = Clamp(min, max, d2);

				Vec2 normal = Vec2(-dir.y, dir.x);
				normal = NormalizeZ(normal);
				if (DotProduct(normal, midline) < 0.0f)
					normal = -normal;

				float penetration = radius - dist;
				float inv_range   = 1.0f / (d2 - d1);

				float factor = incident_r / radius;

				Vec2 points[2];
				points[0] = incident.a + (clipped_d1 - d1) * inv_range * (incident.b - incident.a);
				points[1] = incident.a + (clipped_d2 - d1) * inv_range * (incident.b - incident.a);
				points[0] = points[0] - factor * normal * dist;
				points[1] = points[1] - factor * normal * dist;

				for (int i = 0; i < 2; ++i) {
					Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
					manifold->point       = points[i];
					manifold->normal      = normal;
					manifold->penetration = penetration;
				}

				return;
			}
		}
	}

	Vec2 normal;
	if (dist) {
		normal = midline / dist;
	} else {
		// capsules are intersecting, degenerate case
		normal = TransformDirection(ta, Vec2(0, 1)); // using arbritrary normal
	}

	float factor       = a.radius / radius;
	Vec2 contact_point = points.a + factor * midline;

	Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
	manifold->point       = contact_point;
	manifold->normal      = normal;
	manifold->penetration = radius - dist;
}

void CollideCapsulePolygon(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Capsule &a = GetShapeData<Capsule>(fixture_a);
	const Polygon &b = GetShapeData<Polygon>(fixture_b);

	Transform2d ta = CalculateRigidBodyTransform(pair.bodies[0]);
	Transform2d tb = CalculateRigidBodyTransform(pair.bodies[1]);

	float dist;

	float        penetration;
	Line_Segment edge;
	Vec2         world_normal;
	Vec2         world_points[2];

	if (GilbertJohnsonKeerthi(Line_Segment{ a.centers[0], a.centers[1] }, ta, b, tb, world_points)) {
		// capsule's line does not intersect the polygon (shallow case)
		float dist2 = LengthSq(world_points[1] - world_points[0]);

		if (dist2 > a.radius * a.radius) {
			return;
		}

		dist = SquareRoot(dist2);
		penetration = a.radius - dist;
		edge = FurthestEdge(b, TransformPointTransposed(tb, world_points[1]));

		world_normal = world_points[1] - world_points[0];

		if (IsNull(world_normal)) {
			world_normal = PerpendicularVector(edge.a, edge.b);
			world_normal = TransformDirection(tb, world_normal);
		}

		world_normal = NormalizeZ(world_normal);
	} else {
		Vec2 c0 = TransformPoint(ta, a.centers[0]);
		Vec2 c1 = TransformPoint(ta, a.centers[1]);

		c0 = TransformPointTransposed(tb, c0);
		c1 = TransformPointTransposed(tb, c1);

		uint best_i = b.count - 1;
		uint best_j = 0;

		Line_Segment best_points = NearestPointsInLineSegments(c0, c1, b.vertices[best_i], b.vertices[best_j]);
		float best_dist2 = LengthSq(best_points.b - best_points.a);

		for (uint next_i = 0; next_i < b.count - 1; ++next_i) {
			uint next_j = next_i + 1;

			Line_Segment next_points = NearestPointsInLineSegments(c0, c1, b.vertices[next_i], b.vertices[next_j]);
			float next_dist2 = LengthSq(next_points.b - next_points.a);

			if (next_dist2 < best_dist2) {
				best_dist2  = next_dist2;
				best_points = next_points;
				best_i = next_i;
				best_j = next_j;
			}
		}

		Vec2 normal = PerpendicularVector(b.vertices[best_i], b.vertices[best_j]);
		normal      = NormalizeZ(normal);

		edge        = { b.vertices[best_i],b.vertices[best_j] };

		float t;
		if (LineLineIntersection(c0, c1, b.vertices[best_i], b.vertices[best_j], &t)) {
			dist        = SquareRoot(best_dist2);
			penetration = a.radius + dist;
		} else {
			float proj = DotProduct(normal, best_points.a - c0);
			if (proj < 0.0f) {
				penetration = a.radius - proj;
			} else {
				penetration = a.radius - DotProduct(normal, best_points.b - c1);
			}
			dist = 0.0f;
		}

		world_normal    = TransformDirection(tb, normal);
		world_points[0] = TransformPoint(tb, best_points.a);
		world_points[1] = TransformPoint(tb, best_points.b);
	}

	Vec2 c0 = TransformPoint(ta, a.centers[0]);
	Vec2 c1 = TransformPoint(ta, a.centers[1]);

	Vec2 e0 = TransformPoint(tb, edge.a);
	Vec2 e1 = TransformPoint(tb, edge.b);

	Vec2 dir1 = c1 - c0;
	Vec2 dir2 = e1 - e0;

	float length1 = LengthSq(dir1);
	float length2 = LengthSq(dir2);

	dir1 = NormalizeZ(dir1);
	dir2 = NormalizeZ(dir2);

	if (Absolute(Determinant(dir1, dir2)) <= REAL_EPSILON) {
		// capsule is parallel to some edge of polygon

		if (length1 && length2) {
			Line_Segment reference, incident;
			Vec2 dir;

			float factor;

			if (length1 >= length2) {
				dir       = dir1;
				reference = { c0,c1 };
				incident  = { e0,e1 };
				factor    = 0.0f;
			} else {
				dir       = dir2;
				reference = { e0,e1 };
				incident  = { c0,c1 };
				factor    = 1.0f;
			}

			float min = DotProduct(dir, reference.a);
			float max = DotProduct(dir, reference.b);
			float d1  = DotProduct(dir, incident.a);
			float d2  = DotProduct(dir, incident.b);

			// capsules is stacked
			if (IsInRange(min, max, d1) || IsInRange(min, max, d2)) {
				float clipped_d1 = Clamp(min, max, d1);
				float clipped_d2 = Clamp(min, max, d2);

				float inv_range   = 1.0f / (d2 - d1);

				float offset = factor * Min(dist, a.radius);

				world_points[0] = incident.a + (clipped_d1 - d1) * inv_range * (incident.b - incident.a);
				world_points[1] = incident.a + (clipped_d2 - d1) * inv_range * (incident.b - incident.a);
				world_points[0] = world_points[0] + offset * world_normal;
				world_points[1] = world_points[1] + offset * world_normal;

				for (int i = 0; i < 2; ++i) {
					Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
					manifold->point       = world_points[i];
					manifold->normal      = world_normal;
					manifold->penetration = penetration;
				}

				return;
			}
		}
	}

	if (IsNull(world_normal)) {
		// overlapping center
		world_normal = Vec2(0, 1); // arbritrary normal
	}

	Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
	manifold->point       = world_points[1];
	manifold->normal      = world_normal;
	manifold->penetration = penetration;
}

void CollideCapsuleLine(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Capsule &a = GetShapeData<Capsule>(fixture_a);
	const Line &b    = GetShapeData<Line>(fixture_b);

	Vec2 centers[2];
	centers[0]  = LocalToWorld(pair.bodies[0], a.centers[0]);
	centers[1]  = LocalToWorld(pair.bodies[0], a.centers[1]);
	Vec2 normal = LocalDirectionToWorld(pair.bodies[1], b.normal);

	uint contact_count = 0;

	for (Vec2 center : centers) {
		float perp_dist = DotProduct(b.normal, center);
		float dist      = perp_dist - a.radius - b.offset;

		if (dist <= 0) {
			Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
			manifold->point       = center - (dist + a.radius) * normal;
			manifold->normal      = normal;
			manifold->penetration = -dist;

			contact_count += 1;
		}
	}
}

void CollidePolygonLine(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Polygon &a = GetShapeData<Polygon>(fixture_a);
	const Line &b    = GetShapeData<Line>(fixture_b);

	Vec2 world_normal = LocalDirectionToWorld(pair.bodies[1], b.normal);
	Vec2 direction    = -WorldDirectionToLocal(pair.bodies[0], world_normal);

	Line_Segment edge = FurthestEdge(a, direction);

	Vec2 vertices[] = { edge.a, edge.b };
	Transform2d ta  = CalculateRigidBodyTransform(pair.bodies[0]);

	for (Vec2 vertex : vertices) {
		vertex     = TransformPoint(ta, vertex);
		float perp = DotProduct(world_normal, vertex);
		float dist = perp - b.offset;

		if (dist <= 0.0f) {
			Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_b, fixture_a);
			manifold->point       = vertex - dist * world_normal;
			manifold->normal      = world_normal;
			manifold->penetration = -dist;
		}
	}
}

struct Farthest_Edge_Desc {
	Vec2 direction;
	Vec2 vertices[2];
	Vec2 furthest_vertex;
};

static Farthest_Edge_Desc FarthestEdgeDesc(const Polygon &polygon, const Transform2d &transform, Vec2 world_normal) {
	Vec2 normal = TransformDirectionTransposed(transform, world_normal);

	uint index = FurthestVertexIndex(polygon, normal);

	Vec2 v  = polygon.vertices[index];
	Vec2 v0 = index ? polygon.vertices[index - 1] : polygon.vertices[polygon.count - 1];
	Vec2 v1 = index + 1 < polygon.count ? polygon.vertices[index + 1] : polygon.vertices[0];

	Vec2 d0 = NormalizeZ(v - v0);
	Vec2 d1 = NormalizeZ(v - v1);

	Farthest_Edge_Desc  edge;

	if (DotProduct(d0, normal) <= DotProduct(d1, normal)) {
		edge.direction       = d0;
		edge.vertices[0]     = v0;
		edge.vertices[1]     = v;
		edge.furthest_vertex = v;
	} else {
		edge.direction       = -d1;
		edge.vertices[0]     = v;
		edge.vertices[1]     = v1;
		edge.furthest_vertex = v;
	}

	edge.direction       = TransformDirection(transform, edge.direction);
	edge.vertices[0]     = TransformPoint(transform, edge.vertices[0]);
	edge.vertices[1]     = TransformPoint(transform, edge.vertices[1]);
	edge.furthest_vertex = TransformPoint(transform, edge.furthest_vertex);

	return edge;
}

static Vec2 ProjectPolygon(const Polygon &polygon, const Transform2d &transform, Vec2 normal) {
	Vec2 vertex = TransformPoint(transform, polygon.vertices[0]);

	float min = DotProduct(normal, vertex);
	float max = min;

	for (ptrdiff_t i = 1; i < polygon.count; ++i) {
		vertex = TransformPoint(transform, polygon.vertices[i]);
		float p = DotProduct(normal, vertex);
		if (p < min)
			min = p;
		else if (p > max)
			max = p;
	}

	return Vec2(min, max);
}

static bool PolygonPolygonOverlap(const Polygon &a, const Transform2d &ta, const Polygon &b, const Transform2d &tb, Vec2 normal, float *overlap) {
	Vec2 p1 = ProjectPolygon(a, ta, normal);
	Vec2 p2 = ProjectPolygon(b, tb, normal);

	if (p2.y > p1.x && p1.y > p2.x) {
		*overlap = Min(p1.y, p2.y) - Max(p1.x, p2.x);

		// if one contains the either
		if (p1.x > p2.x && p1.y < p2.y) {
			*overlap += Min(p1.x - p2.x, p2.y - p1.y);
		} else if (p2.x > p1.x && p2.y < p1.y) {
			*overlap += Min(p2.x - p1.x, p1.y - p2.y);
		}

		return true;
	}

	return false;
}

void CollidePolygonPolygon(const Shape *fixture_a, const Shape *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Polygon &a = GetShapeData<Polygon>(fixture_a);
	const Polygon &b = GetShapeData<Polygon>(fixture_b);

	Transform2d ta = CalculateRigidBodyTransform(pair.bodies[0]);
	Transform2d tb = CalculateRigidBodyTransform(pair.bodies[1]);

	// Separate Axis Theorem
	float min_overlap = FLT_MAX;
	Vec2 best_normal  = Vec2(0, 0);

	float overlap;

	uint i = a.count - 1;
	uint j = 0;

	Vec2 normal = NormalizeZ(PerpendicularVector(a.vertices[i], a.vertices[j]));
	if (!PolygonPolygonOverlap(a, ta, b, tb, normal, &overlap))
		return;

	if (overlap < min_overlap) {
		min_overlap = overlap;
		best_normal = -normal;
	}

	for (i = 0; i < a.count - 1; ++i) {
		j = i + 1;

		normal = NormalizeZ(PerpendicularVector(a.vertices[i], a.vertices[j]));
		if (!PolygonPolygonOverlap(a, ta, b, tb, normal, &overlap))
			return;

		if (overlap < min_overlap) {
			min_overlap = overlap;
			best_normal = -normal;
		}
	}

	i = b.count - 1;
	j = 0;

	normal = NormalizeZ(PerpendicularVector(b.vertices[i], b.vertices[j]));
	if (!PolygonPolygonOverlap(a, ta, b, tb, normal, &overlap))
		return;

	if (overlap < min_overlap) {
		min_overlap = overlap;
		best_normal = normal;
	}

	for (i = 0; i < b.count - 1; ++i) {
		j = i + 1;

		normal = NormalizeZ(PerpendicularVector(b.vertices[i], b.vertices[j]));
		if (!PolygonPolygonOverlap(a, ta, b, tb, normal, &overlap))
			return;

		if (overlap < min_overlap) {
			min_overlap = overlap;
			best_normal = normal;
		}
	}

	if (DotProduct(best_normal, tb.pos - ta.pos) < 0.0f)
		best_normal = -best_normal;

	float penetration = overlap;
	normal            = best_normal;

	Farthest_Edge_Desc edge_a = FarthestEdgeDesc(a, ta, normal);
	Farthest_Edge_Desc edge_b = FarthestEdgeDesc(b, tb, -normal);

	Farthest_Edge_Desc reference, incident;

	float dot_a = Absolute(DotProduct(edge_a.direction, normal));
	float dot_b = Absolute(DotProduct(edge_b.direction, normal));

 	if (dot_a < dot_b) {
		reference = edge_a;
		incident  = edge_b;
	} else if (dot_b < dot_a) {
		reference = edge_b;
		incident  = edge_a;
	} else {
		if (LengthSq(edge_a.vertices[1] - edge_a.vertices[0]) >= LengthSq(edge_b.vertices[1] - edge_b.vertices[0])) {
			reference = edge_a;
			incident  = edge_b;
		} else {
			reference = edge_b;
			incident  = edge_a;
		}
	}

	float d1  = DotProduct(reference.direction, incident.vertices[0]);
	float d2  = DotProduct(reference.direction, incident.vertices[1]);
	float d   = d2 - d1;

	if (d) {
		float min = DotProduct(reference.direction, reference.vertices[0]);
		float max = DotProduct(reference.direction, reference.vertices[1]);

		float clipped_d1 = Clamp(min, max, d1);
		float clipped_d2 = Clamp(min, max, d2);
		float inv_range  = 1.0f / (d2 - d1);

		Vec2 points[2];
		Vec2 relative = incident.vertices[1] - incident.vertices[0];
		points[0]     = incident.vertices[0] + (clipped_d1 - d1) * inv_range * relative;
		points[1]     = incident.vertices[0] + (clipped_d2 - d1) * inv_range * relative;

		Vec2 reference_normal = Vec2(reference.direction.y, -reference.direction.x);
		float max_threshold   = DotProduct(reference_normal, reference.furthest_vertex);

		for (uint i = 0; i < 2; ++i){
			float depth = DotProduct(reference_normal, points[i]);

			if (depth <= max_threshold) {
				Vec2 p = reference.vertices[1] - points[i];

				Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
				manifold->normal      = normal;
				manifold->point       = points[i];
				manifold->penetration = DotProduct(reference_normal, reference.vertices[0] - points[i]);
			}
		}

		return;
	}

	// Only single vertex is in the reference edge
	Contact_Manifold *manifold     = PushContact(contacts, pair, fixture_a, fixture_b);
	manifold->normal      = normal;
	manifold->point       = incident.furthest_vertex;
	manifold->penetration = penetration;
}

typedef void(*Collide_Proc)(const Shape *, const Shape *, Rigid_Body_Pair , Contact_List *);

static Collide_Proc Collides[SHAPE_KIND_COUNT][SHAPE_KIND_COUNT] = {
	{ CollideCircleCircle, CollideCircleCapsule,  CollideCirclePolygon,  CollideCircleLine,  },
	{ nullptr,             CollideCapsuleCapsule, CollideCapsulePolygon, CollideCapsuleLine, },
	{ nullptr,             nullptr,               CollidePolygonPolygon, CollidePolygonLine, },
	{ nullptr,             nullptr,               nullptr,               nullptr             },
};

void Collide(Rigid_Body *body_first, Shape *first, Rigid_Body *body_second, Shape *second, Contact_List *contacts) {
	if (first->shape > second->shape) {
		Swap(&body_first, &body_second);
		Swap(&first, &second);
	}

	Rigid_Body_Pair pair;
	pair.bodies[0] = body_first;
	pair.bodies[1] = body_second;

	Collides[first->shape][second->shape](first, second, pair, contacts);
}

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
	boat.flags           = 0;

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
					manifold.pair.bodies[0]   = &body;
					manifold.pair.bodies[1]   = &ground;
					manifold.normal      = Vec2(0, 1);
					manifold.restitution = 0.5f;
					manifold.penetration = ground.P.y - body.P.y;

					Append(&contacts, manifold);
				}
			}

#if 0
			if (boat.position.y <= ground.position.y) {
				manifold.bodies[0]   = &boat;
				manifold.bodies[1]   = &ground;
				manifold.normal      = Vec2(0, 1);
				manifold.restitution = 0.5f;
				manifold.penetration = ground.position.y - boat.position.y;

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

		// FIXTURE_SHAPE_CIRCLE,
		// FIXTURE_SHAPE_CAPSULE,
		// FIXTURE_SHAPE_POLYGON,
		// FIXTURE_SHAPE_LINE,

		/*
				circle	capsule	polygon	line
		circle    .      .        .       .
		capsule   x      .        .       .
		polygon   x      x        .       .
		line      x      x        x       x
		*/

		// TODO: replace mat4 with transform2d in renderer
		R_PushTransform(renderer, world_transform);
		R_DrawCircle(renderer, cursor_cam_pos, 0.1f, Vec4(1));

		Capsule capsule;
		capsule.centers[0] = Vec2(-1, 0);
		capsule.centers[1] = Vec2(1, 0);
		capsule.radius = 1;

		//Line_Segment line;
		//line.a = Vec2(-1, -3);
		//line.b = Vec2(1, 1);

		Circle circle;
		circle.center = Vec2(0, 1);
		circle.radius = 1.5f;

		Rect rect;
		rect.center = Vec2(0, 0);
		rect.half_size = Vec2(2, 1);

		Line line;
		line.normal = Normalize(Vec2(0, 1));
		line.offset = 1;

		Fixed_Polygon<5> polygon;
		polygon.count = 5;
		polygon.vertices[0] = Vec2(2, 0);
		polygon.vertices[1] = Vec2(0, 2);
		polygon.vertices[2] = Vec2(-2, 0);
		polygon.vertices[3] = Vec2(-1, -2);
		polygon.vertices[4] = Vec2(1, -2);

		Vec2 point = Vec2(0, 0);

		Rigid_Body body1 = {};
		Rigid_Body body2 = {};

		Fixed_Polygon<5> first_shape = polygon;
		Fixed_Polygon<5> second_shape = polygon;

		/*first_shape.count = 4;
		for (int i = 0; i < 4; ++i)
			first_shape.vertices[i] = Vec2(0, 0.0f) + RectVertexOffsets[i] * Vec2(1);
		*/
		//second_shape.count = 4;
		//for (int i = 0; i < 4; ++i)
		//	second_shape.vertices[i] = rect.center + RectVertexOffsets[i] * rect.half_size;

		//body1.transform.pos = init_pos + control * cursor_cam_pos;
		//body1.transform.rot = Rotation2x2(DegToRad(angle));
		//body2.transform.pos = Vec2(1, 0);
		//body2.transform.rot = Rotation2x2(DegToRad(15));

		TShape<Fixed_Polygon<5>> fix1 = { SHAPE_KIND_POLYGON, first_shape };
		TShape<Fixed_Polygon<5>> fix2 = { SHAPE_KIND_POLYGON, second_shape };

		DrawShape(renderer, (Polygon &)first_shape, Vec4(1), CalculateRigidBodyTransform(&body1));
		DrawShape(renderer, (Polygon &)second_shape, Vec4(1), CalculateRigidBodyTransform(&body2));
		//R_DrawLine(renderer, first_shape.centers[0], first_shape.centers[1], Vec4(1));

		Contact_List contacts;

		Collide(&body1, &fix1, &body2, &fix2, &contacts);

		// origin
		R_DrawRectCentered(renderer, Vec2(0), Vec2(0.1f), Vec4(1, 1, 0, 1));

		for (const Contact_Manifold &manifold : contacts) {
			R_DrawLine(renderer, manifold.point, manifold.point - manifold.penetration * manifold.normal, Vec4(0, 1, 0, 1));
			R_DrawCircle(renderer, manifold.point, 0.1f, Vec4(1, 1, 0, 1));
		}

#if 1
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
#endif

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
