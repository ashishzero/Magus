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

enum Rigid_Body_Flags : uint {
	RIGID_BODY_IS_AWAKE = 0x1
};

struct Rigid_Body {
	Vec2  position;
	Vec2  orientation;
	Vec2  velocity;
	float rotation;

	float damping;
	float angular_damping;
	float inv_mass;
	float inv_inertia;

	Vec2  acceleration;
	Vec2  force;
	float torque;

	// TODO: We don't really need this here
	Transform2d transform;

	uint  flags;
};

Vec2 LocalToWorld(const Rigid_Body *body, Vec2 local) {
	return LocalToWorld(body->transform, local);
}

Vec2 WorldToLocal(const Rigid_Body *body, Vec2 world) {
	return WorldToLocal(body->transform, world);
}

Vec2 LocalDirectionToWorld(const Rigid_Body *body, Vec2 local_dir) {
	return LocalDirectionToWorld(body->transform, local_dir);
}

Vec2 WorldDirectionToLocal(const Rigid_Body *body, Vec2 world_dir) {
	return WorldDirectionToLocal(body->transform, world_dir);
}

void ApplyForce(Rigid_Body *body, Vec2 force) {
	body->force += force;

	body->flags |= RIGID_BODY_IS_AWAKE;
}

void ApplyForce(Rigid_Body *body, Vec2 force, Vec2 point_in_world) {
	Vec2 rel = point_in_world - body->position;

	body->force += force;
	body->torque += CrossProduct(force, rel);

	body->flags |= RIGID_BODY_IS_AWAKE;
}

void ApplyForceAtBodyPoint(Rigid_Body *body, Vec2 force, Vec2 point) {
	Vec2 point_in_world = LocalToWorld(body->transform, point);
	return ApplyForce(body, force, point_in_world);
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
	if (body->inv_mass == 0.0f) return;
	Vec2 force = g / body->inv_mass;
	ApplyForce(body, force);
}

void ApplySpring(Rigid_Body *a, Rigid_Body *b, Vec2 a_connection, Vec2 b_connection, float k, float rest_length) {
	a_connection = LocalToWorld(a->transform, a_connection);
	b_connection = LocalToWorld(b->transform, b_connection);

	Vec2 dir     = a_connection - b_connection;
	float length = Length(dir);

	float magnitude = Absolute(rest_length - length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);

	ApplyForce(a, -force, a_connection);
	ApplyForce(b, force, b_connection);
}

void ApplySpring(Rigid_Body *body, Vec2 connection, Vec2 anchor, float k, float rest_length) {
	connection = LocalToWorld(body->transform, connection);

	Vec2 dir = connection - anchor;
	float length = Length(dir);

	float magnitude = Absolute(rest_length - length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(body, -force);
}

void ApplyBungee(Rigid_Body *a, Rigid_Body *b, Vec2 connection_a, Vec2 connection_b, float k, float rest_length) {
	connection_a = LocalToWorld(a->transform, connection_a);
	connection_b = LocalToWorld(b->transform, connection_b);

	Vec2 dir = connection_a - connection_b;
	float length = Length(dir);

	if (length <= rest_length) return;

	float magnitude = (length - rest_length) * k;
	Vec2 force = magnitude * NormalizeZ(dir);
	ApplyForce(a, -force);
	ApplyForce(b, force);
}

void ApplyBungee(Rigid_Body *body, Vec2 connection, Vec2 anchor, float k, float rest_length) {
	connection = LocalToWorld(body->transform, connection);

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

Transform2d CalcTransform(Rigid_Body *body) {
	float cosine = body->orientation.x;
	float sine   = body->orientation.y;

	Transform2d transform;
	transform.rot = Rotation2x2(body->orientation);
	transform.pos = body->position;

	return transform;
}

void PrepareForNextFrame(Rigid_Body *body) {
	body->transform = CalcTransform(body);
	body->force     = Vec2(0);
	body->torque    = 0;
}

void Integrate(Rigid_Body *body, float dt) {
	if (body->inv_mass == 0.0f) return;

	Vec2 acceleration = body->acceleration;
	acceleration += body->force * body->inv_mass;
	float angular_acceleration = body->torque * body->inv_inertia;

	body->velocity += acceleration * dt;
	body->velocity *= Pow(body->damping, dt);

	body->rotation += angular_acceleration * dt;
	body->rotation *= Pow(body->angular_damping, dt);

	body->position += body->velocity * dt;
	body->orientation = ComplexProduct(body->orientation, Arm(body->rotation * dt));
}

struct Rigid_Body_Pair {
	Rigid_Body *bodies[2];
};

struct Contact {
	Rigid_Body_Pair pair;
	Vec2            point;
	Vec2            normal;
	float           penetration;
	float           restitution;
	float           friction;
	Contact *       next;
};

float CalcSeparatingVelocity(Contact *contact) {
	Rigid_Body *a    = contact->pair.bodies[0];
	Rigid_Body *b    = contact->pair.bodies[1];
	Vec2 relative    = a->velocity - b->velocity;
	float separation = DotProduct(relative, contact->normal);
	return separation;
}

void ResolveVelocity(Contact *contact, float dt) {
	Rigid_Body *a = contact->pair.bodies[0];
	Rigid_Body *b = contact->pair.bodies[1];

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

		float inv_mass = a->inv_mass + b->inv_mass;
		if (inv_mass <= 0) return;

		float impulse = delta / inv_mass;

		Vec2 impulse_per_inv_mass = impulse * contact->normal;

		a->velocity += impulse_per_inv_mass * a->inv_mass;
		b->velocity -= impulse_per_inv_mass * b->inv_mass;
	}
}

void ResolvePosition(Contact *contact) {
	Rigid_Body *a = contact->pair.bodies[0];
	Rigid_Body *b = contact->pair.bodies[1];

	if (contact->penetration > 0) {
		float inv_mass = a->inv_mass + b->inv_mass;
		if (inv_mass <= 0) return;

		Vec2 move_per_inv_mass = contact->penetration / inv_mass * contact->normal;

		a->position += move_per_inv_mass * a->inv_mass;
		b->position -= move_per_inv_mass * b->inv_mass;

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
		ResolvePosition(&contacts[max_i]); // TODO: update penetrations separately??
	}
	return iter;
}

int ApplyCableConstraint(Rigid_Body *a, Rigid_Body *b, float max_length, float restitution, Contact *contact) {
	float length = Distance(a->position, b->position);

	if (length < max_length)
		return 0;

	contact->pair.bodies[0]   = a;
	contact->pair.bodies[1]   = b;
	contact->normal      = NormalizeZ(b->position - a->position);
	contact->penetration = length - max_length;
	contact->restitution = restitution;

	return 1;
}

int ApplyRodConstraint(Rigid_Body *a, Rigid_Body *b, float length, Contact *contact) {
	float curr_length = Distance(a->position, b->position);

	if (curr_length == length)
		return 0;

	contact->pair.bodies[0]   = a;
	contact->pair.bodies[1]   = b;
	contact->normal      = NormalizeZ(b->position - a->position) * Sgn(curr_length - length);
	contact->penetration = Absolute(curr_length - length);
	contact->restitution = 0;

	return 1;
}

int ApplyMagnetRepelConstraint(Rigid_Body *a, Rigid_Body *b, float max_length, float restitution, Contact *contact) {
	float length = Distance(a->position, b->position);

	if (length > max_length)
		return 0;

	contact->pair.bodies[0]   = a;
	contact->pair.bodies[1]   = b;
	contact->normal      = NormalizeZ(a->position - b->position);
	contact->penetration = max_length - length;
	contact->restitution = restitution;

	return 1;
}

void RandomRigidBodies(Array_View<Rigid_Body> bodies) {
	for (Rigid_Body &body : bodies) {
		body.inv_mass = 1.0f / RandomFloat(0.1f, 0.5f);
		body.inv_inertia = 0.0f;
		body.damping = 0.8f;
		body.angular_damping = 0.8f;
		body.position = RandomVec2(Vec2(-5, 3), Vec2(5, 4));
		body.orientation = Vec2(1, 0);
		body.velocity = Vec2(0);
		body.rotation = 0;
		body.force = Vec2(0);
		body.acceleration = Vec2(0, -10);
		body.transform = CalcTransform(&body);
		body.flags = 0;
	}
}

// TODO: Continuous collision detection

enum Fixture_Shape {
	FIXTURE_SHAPE_CIRCLE,
	FIXTURE_SHAPE_CAPSULE,
	FIXTURE_SHAPE_POLYGON,
	FIXTURE_SHAPE_LINE,
	FIXTURE_SHAPE_COUNT,
};

struct Fixture {
	Fixture_Shape shape;

	Fixture() {}
	Fixture(Fixture_Shape _shape): shape(_shape) {}
};

struct Geometry {
	uint32_t  count;
	Fixture **fixtures;
};

template <typename Payload>
struct TFixture : Fixture {
	Payload payload;
};

//
//
//

struct MTV {
	Vec2  normal;
	float penetration;
};

bool Overlaps(Vec2 p1, Vec2 p2) {
	return p2.y > p1.x && p1.y > p2.x;
}

float CalcOverlap(Vec2 p1, Vec2 p2) {
	float overlap = Min(p1.y, p2.y) - Max(p1.x, p2.x);

	// if one contains the either
	if (p1.x > p2.x && p1.y < p2.y) {
		overlap += Min(p1.x - p2.x, p2.y - p1.y);
	} else if (p2.x > p1.x && p2.y < p1.y) {
		overlap += Min(p2.x - p1.x, p1.y - p2.y);
	}

	return overlap;
}

// todo: verify
template <typename ShapeA, typename ShapeB>
bool SeparateAxis(const ShapeA &a, const Transform2d &ta, const ShapeB &b, const Transform2d &tb, MTV *mtv) {
	float overlap = FLT_MAX;
	Vec2 normal   = Vec2(0, 0);

	Vec2 axis;
	
	for (uint index = 0; GetAxis(index, a, ta, &axis); ++index) {
		Vec2 p1 = Project(a, ta, axis);
		Vec2 p2 = Project(b, tb, axis);

		if (!Overlaps(p1, p1)) {
			return false;
		}

		float o = CalcOverlap(p1, p2);
		if (o < overlap) {
			overlap = o;
			normal  = axis;
		}
	}

	for (uint index = 0; GetAxis(index, b, tb, &axis); ++index) {
		Vec2 p1 = Project(a, ta, axis);
		Vec2 p2 = Project(b, tb, axis);

		if (!Overlaps(p1, p2)) {
			return true;
		}

		float o = CalcOverlap(p1, p2);
		if (o < overlap) {
			overlap = o;
			normal  = axis;
		}
	}

	// Face the normal from shape a to shape b
	if (DotProduct(normal, tb.pos - ta.pos) < 0.0f) {
		normal = -normal;
	}

	mtv->normal      = NormalizeZ(normal);
	mtv->penetration = overlap;

	return true;
}

//
//
//

static const Vec2 RectAxis[]          = { Vec2(1, 0), Vec2(0, 1) };
static const Vec2 RectVertexOffsets[] = { Vec2(-1, -1), Vec2(1, -1), Vec2(1, 1), Vec2(-1, 1) };

bool GetAxis(uint index, const Rect &rect, const Transform2d &transform, Vec2 *axis) {
	if (index < 2) {
		*axis = LocalDirectionToWorld(transform, RectAxis[index]);
		*axis = NormalizeZ(*axis);
		return true;
	}
	return false;
}

bool GetAxis(uint index, const Polygon &polygon, const Transform2d &transform, Vec2 *axis) {
	if (index < polygon.count) {
		Vec2 p1 = polygon.vertices[index];
		Vec2 p2 = polygon.vertices[(index + 1) % polygon.count];
		Vec2 n  = PerpendicularVector(p1, p2);
		*axis   = LocalDirectionToWorld(transform, n);
		*axis   = NormalizeZ(*axis);
		return true;
	}
	return false;
}

Vec2 Project(const Rect &rect, const Transform2d &transform, Vec2 axis) {
	Vec2 vertex = LocalToWorld(transform, rect.center + RectVertexOffsets[0] * rect.half_size);

	float min = DotProduct(axis, vertex);
	float max = min;

	for (int i = 1; i < ArrayCount(RectVertexOffsets); ++i) {
		vertex  = LocalToWorld(transform, rect.center + RectVertexOffsets[i] * rect.half_size);
		float p = DotProduct(axis, vertex);
		if (p < min)
			min = p;
		else if (p > max)
			max = p;
	}

	return Vec2(min, max);
}

Vec2 Project(const Polygon &polygon, const Transform2d &transform, Vec2 axis) {
	Vec2 vertex = LocalToWorld(transform, polygon.vertices[0]);

	float min = DotProduct(axis, vertex);
	float max = min;

	for (ptrdiff_t i = 1; i < polygon.count; ++i) {
		vertex  = LocalToWorld(transform, polygon.vertices[i]);
		float p = DotProduct(axis, vertex);
		if (p < min)
			min = p;
		else if (p > max)
			max = p;
	}

	return Vec2(min, max);
}

Vec2 ProjectPolygonToAxis(const Array_View<Vec2> vertices, Vec2 axis) {
	float min = DotProduct(axis, vertices[0]);
	float max = min;

	for (ptrdiff_t i = 1; i < vertices.count; ++i) {
		float p = DotProduct(axis, vertices[i]);
		if (p < min)
			min = p;
		else if (p > max)
			max = p;
	}

	return Vec2(min, max);
}

Vec2 ProjectPolygonToAxis(const Vec2 *vertices, uint count, Vec2 axis) {
	return ProjectPolygonToAxis(Array_View<Vec2>(vertices, count), axis);
}

Vec2 ProjectPolygonToAxis(const Polygon &polygon, Vec2 axis) {
	return ProjectPolygonToAxis(Array_View<Vec2>(polygon.vertices, polygon.count), axis);
}

//
//
//

void FillSurfaceData(Contact *contact, Rigid_Body_Pair pair, const Fixture *first, const Fixture *second) {
	contact->pair = pair;
	//todo: fill rest of the data
	//Unimplemented();
	contact->friction = 0;
	contact->restitution = 0;
}

struct Contact_List {
	Contact *first;
	M_Arena *arena;
	Contact  fallback;
};

Contact *PushContact(Contact_List *contacts, Rigid_Body_Pair pair, const Fixture *a, const Fixture *b) {
	Contact *contact = M_PushType(contacts->arena, Contact);
	if (contact) {
		FillSurfaceData(contact, pair, a, b);

		contact->next   = contacts->first;
		contacts->first = contact;

		return contact;
	}

	LogWarning("[Physics]: Failed to allocate new contact point");
	return &contacts->fallback;
}

void CollideCircleCircle(const TFixture<Circle> *fixture_a, const TFixture<Circle> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Circle &a = fixture_a->payload;
	const Circle &b = fixture_b->payload;

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

	Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
	contact->point       = a_pos + factor * midline;
	contact->normal      = normal;
	contact->penetration = min_dist - length;
}

void CollideCircleCapsule(const TFixture<Circle> *fixture_a, const TFixture<Capsule> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Circle &a = fixture_a->payload;
	const Capsule &b = fixture_b->payload;

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

	Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
	contact->point       = LocalToWorld(pair.bodies[1], contact_point);
	contact->normal      = LocalDirectionToWorld(pair.bodies[1], normal);
	contact->penetration = radius - length;
}

void CollideCirclePolygon(const TFixture<Circle> *fixture_a, const TFixture<Polygon> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Circle &a  = fixture_a->payload;
	const Polygon &b = fixture_b->payload;

	const Transform2d &ta = pair.bodies[0]->transform;
	const Transform2d &tb = pair.bodies[1]->transform;

	Vec2 points[2];
	if (GilbertJohnsonKeerthi(a.center, ta, b, tb, points)) {
		Vec2 dir = points[1] - points[0];
		float dist2 = LengthSq(points[0] - points[1]);

		if (dist2 > a.radius * a.radius)
			return;

		float dist = SquareRoot(dist2);
		Assert(dist != 0);

		Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
		contact->point       = points[1];
		contact->normal      = dir / dist;
		contact->penetration = a.radius - dist;

		return;
	}

	// Find the edge having the least distance from the origin of the circle
	Vec2 center = LocalToWorld(ta, a.center);
	center = WorldToLocal(tb, center);

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

	Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
	contact->point       = LocalToWorld(tb, point);
	contact->normal      = LocalDirectionToWorld(tb, normal);
	contact->penetration = a.radius + dist;
}

void CollideCircleLine(const TFixture<Circle> *fixture_a, const TFixture<Line> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Circle &a = fixture_a->payload;
	const Line &b   = fixture_b->payload;

	Vec2 a_pos  = LocalToWorld(pair.bodies[0], a.center);
	Vec2 normal = LocalDirectionToWorld(pair.bodies[1], b.normal);

	float perp_dist = DotProduct(normal, a_pos);

	float dist = perp_dist - a.radius - b.offset;

	if (dist > 0) {
		return;
	}

	Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
	contact->point       = a_pos - (dist + a.radius) * normal;
	contact->normal      = normal;
	contact->penetration = -dist;
}

void CollideCapsuleCapsule(const TFixture<Capsule> *fixture_a, const TFixture<Capsule> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Capsule &a = fixture_a->payload;
	const Capsule &b = fixture_b->payload;

	const Transform2d &ta = pair.bodies[0]->transform;
	const Transform2d &tb = pair.bodies[1]->transform;

	Line_Segment l1, l2;
	l1.a = LocalToWorld(ta, a.centers[0]);
	l1.b = LocalToWorld(ta, a.centers[1]);
	l2.a = LocalToWorld(tb, b.centers[0]);
	l2.b = LocalToWorld(tb, b.centers[1]);

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
					Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
					contact->point       = points[i];
					contact->normal      = normal;
					contact->penetration = penetration;
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
		normal = LocalDirectionToWorld(ta, Vec2(0, 1)); // using arbritrary normal
	}

	float factor       = a.radius / radius;
	Vec2 contact_point = points.a + factor * midline;

	Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
	contact->point       = contact_point;
	contact->normal      = normal;
	contact->penetration = radius - dist;
}

void CollideCapsulePolygon(const TFixture<Capsule> *fixture_a, const TFixture<Polygon> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Capsule &a = fixture_a->payload;
	const Polygon &b = fixture_b->payload;

	const Transform2d &ta = pair.bodies[0]->transform;
	const Transform2d &tb = pair.bodies[1]->transform;

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
		edge = FurthestEdge(b, WorldToLocal(tb, world_points[1]));

		world_normal = world_points[1] - world_points[0];

		if (IsNull(world_normal)) {
			world_normal = PerpendicularVector(edge.a, edge.b);
			world_normal = LocalDirectionToWorld(tb, world_normal);
		}

		world_normal = NormalizeZ(world_normal);
	} else {
		Vec2 c0 = LocalToWorld(ta, a.centers[0]);
		Vec2 c1 = LocalToWorld(ta, a.centers[1]);

		c0 = WorldToLocal(tb, c0);
		c1 = WorldToLocal(tb, c1);

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

		world_normal    = LocalDirectionToWorld(tb, normal);
		world_points[0] = LocalToWorld(tb, best_points.a);
		world_points[1] = LocalToWorld(tb, best_points.b);
	}

	Vec2 c0 = LocalToWorld(ta, a.centers[0]);
	Vec2 c1 = LocalToWorld(ta, a.centers[1]);

	Vec2 e0 = LocalToWorld(tb, edge.a);
	Vec2 e1 = LocalToWorld(tb, edge.b);

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
					Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
					contact->point       = world_points[i];
					contact->normal      = world_normal;
					contact->penetration = penetration;
				}

				return;
			}
		}
	}

	if (IsNull(world_normal)) {
		// overlapping center
		world_normal = Vec2(0, 1); // arbritrary normal
	}

	Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
	contact->point       = world_points[1];
	contact->normal      = world_normal;
	contact->penetration = penetration;
}

void CollideCapsuleLine(const TFixture<Capsule> *fixture_a, const TFixture<Line> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Capsule &a = fixture_a->payload;
	const Line &b    = fixture_b->payload;

	Vec2 centers[2];
	centers[0]  = LocalToWorld(pair.bodies[0], a.centers[0]);
	centers[1]  = LocalToWorld(pair.bodies[0], a.centers[1]);
	Vec2 normal = LocalDirectionToWorld(pair.bodies[1], b.normal);

	uint contact_count = 0;

	for (Vec2 center : centers) {
		float perp_dist = DotProduct(b.normal, center);
		float dist      = perp_dist - a.radius - b.offset;

		if (dist <= 0) {
			Contact *contact     = PushContact(contacts, pair, fixture_a, fixture_b);
			contact->point       = center - (dist + a.radius) * normal;
			contact->normal      = normal;
			contact->penetration = -dist;

			contact_count += 1;
		}
	}
}

void CollidePolygonLine(const TFixture<Polygon> *fixture_a, const TFixture<Line> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Polygon &a = fixture_a->payload;
	const Line &b    = fixture_b->payload;

	const Transform2d &ta = pair.bodies[0]->transform;

	Vec2 world_normal = LocalDirectionToWorld(pair.bodies[1], b.normal);
	Vec2 direction    = -WorldDirectionToLocal(ta, world_normal);

	Line_Segment edge = FurthestEdge(a, direction);

	Vec2 vertices[] = { edge.a, edge.b };

	for (Vec2 vertex : vertices) {
		vertex     = LocalToWorld(ta, vertex);
		float perp = DotProduct(world_normal, vertex);
		float dist = perp - b.offset;

		if (dist <= 0.0f) {
			Contact *contact     = PushContact(contacts, pair, fixture_b, fixture_a);
			contact->point       = vertex - dist * world_normal;
			contact->normal      = world_normal;
			contact->penetration = -dist;
		}
	}
}


static String_Builder frame_builder;

#define DebugText(...) WriteFormatted(&frame_builder, __VA_ARGS__)


//
//
//

// todo: verify

struct Farthest_Edge {
	Vec2 vector;
	Vec2 points[2];
	Vec2 max;
};

static Farthest_Edge FindFarthestEdge(const Polygon &polygon, Vec2 normal) {
	const Vec2 *vertices = polygon.vertices;
	uint count = polygon.count;

	float max   = DotProduct(normal, vertices[0]);
	uint best_i = 0;

	for (uint i = 1; i < count; ++i) {
		float proj = DotProduct(normal, vertices[i]);
		if (proj > max) {
			max    = proj;
			best_i = i;
		}
	}

	Vec2 v  = vertices[best_i];
	Vec2 v0 = best_i ? vertices[best_i - 1] : vertices[count - 1];
	Vec2 v1 = best_i + 1 < count ? vertices[best_i + 1] : vertices[0];

	Vec2 l = NormalizeZ(v - v0);
	Vec2 r = NormalizeZ(v - v1);

	Farthest_Edge  edge;

	if (DotProduct(r, normal) <= DotProduct(l, normal)) {
		edge.vector    = r;
		edge.points[0] = v0;
		edge.points[1] = v;
		edge.max       = v;
	} else {
		edge.vector    = l;
		edge.points[0] = v;
		edge.points[1] = v1;
		edge.max       = v;
	}

	return edge;
}

static Line_Segment ClipLineSegment(Vec2 input_a, Vec2 input_b, Vec2 region_a, Vec2 region_b) {
	Assert(!AlmostEqual(region_a, region_b));

	Vec2 dir = NormalizeZ(region_b - region_a);

	float min = DotProduct(dir, region_a);
	float max = DotProduct(dir, region_b);

	float d1 = DotProduct(dir, input_a);
	float d2 = DotProduct(dir, input_b);

	float n1 = Clamp(min, max, d1);
	float n2 = Clamp(min, max, d2);

	Line_Segment clipped;
	clipped.a = Absolute(n1 / d1) * input_a;
	clipped.b = Absolute(n2 / d2) * input_b;

	return clipped;
}

static Line_Segment ClipLineSegment(Line_Segment input, Line_Segment region) {
	return ClipLineSegment(input.a, input.b, region.a, region.b);
}

// @TODO: optimize this procedure's use case
static bool ClipPoints(Vec2 points[2], Vec2 a, Vec2 b) {
	Vec2 v1 = points[0];
	Vec2 v2 = points[1];

	Vec2 dir = NormalizeZ(b - a);

	float min = DotProduct(dir, a);
	float max = DotProduct(dir, b);

	float d1 = DotProduct(dir, v1);
	float d2 = DotProduct(dir, v2);

	float d = d1 + d2;
	if (d == 0) return false;

	Vec2 t = (v2 - v1) / d;
	points[0] = v1 + t * min;
	points[1] = v1 + t * max;

	return true;
}

// todo: verify
uint PolygonVsPolygon(const TFixture<Polygon> *fixture_a, const TFixture<Polygon> *fixture_b, Rigid_Body_Pair pair, Contact_List *contacts) {
	const Polygon &a = fixture_a->payload;
	const Polygon &b = fixture_b->payload;

	const Transform2d &ta = pair.bodies[0]->transform;
	const Transform2d &tb = pair.bodies[1]->transform;

	MTV mtv;
	if (!SeparateAxis(a, ta, b, tb, &mtv))
		return 0;

	Farthest_Edge e1 = FindFarthestEdge(a, WorldDirectionToLocal(ta, mtv.normal));
	Farthest_Edge e2 = FindFarthestEdge(b, WorldDirectionToLocal(tb, -mtv.normal));

	e1.vector    = LocalDirectionToWorld(ta, e1.vector);
	e1.points[0] = LocalToWorld(ta, e1.points[0]);
	e1.points[1] = LocalToWorld(ta, e1.points[1]);

	e2.vector    = LocalDirectionToWorld(tb, e2.vector);
	e2.points[0] = LocalToWorld(tb, e2.points[0]);
	e2.points[1] = LocalToWorld(tb, e2.points[1]);

	Farthest_Edge reference, incident;
	bool flip = false;

	if (DotProduct(e1.vector, mtv.normal) <= DotProduct(e2.vector, -mtv.normal)) {
		reference = e1;
		incident  = e2;
	} else {
		reference = e2;
		incident  = e1;
		flip      = true;
	}

	Vec2 points[2] = { incident.points[0], incident.points[1] };

	if (!ClipPoints(points, reference.points[0], reference.points[1]))
		return false;

	Vec2 normal = PerpendicularVector(reference.points[0], reference.points[1]);

	if (flip) normal = -normal;
	normal = NormalizeZ(normal);

	float max = DotProduct(normal, reference.max);

	uint count = 0;

	float penetration = DotProduct(normal, points[0]);
	if (penetration <= max) {
		Contact *contact = PushContact(contacts, pair, fixture_a, fixture_b);

		contact->normal      = mtv.normal;
		contact->point       = points[0];
		contact->penetration = penetration;

		count += 1;
	}

	penetration = DotProduct(normal, points[1]);
	if (penetration <= max) {
		Contact *contact = PushContact(contacts, pair, fixture_a, fixture_b);
		
		contact->normal      = mtv.normal;
		contact->point       = points[1];
		contact->penetration = penetration;

		count += 1;
	}

	return count;
}

//
//
//

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
	boat.position        = Vec2(0);
	boat.orientation     = Arm(0.0f);
	boat.velocity        = Vec2(0);
	boat.rotation        = 0.0f;
	boat.damping         = 0.8f;
	boat.angular_damping = 0.8f;
	boat.inv_mass        = 1.0f / 800.0f;
	boat.inv_inertia     = 800.0f * (Square(boat_half_width_lower) + Square(boat_half_height)) / 12.0f;
	boat.acceleration    = Vec2(0, -10.0f);
	boat.force           = Vec2(0);
	boat.torque          = 0.0f;
	boat.transform       = CalcTransform(&boat);
	boat.flags           = 0;

	float ground_level = -2.0f;

	Rigid_Body ground = {};
	ground.position = Vec2(0, ground_level);
	ground.velocity = Vec2(0);
	ground.force    = Vec2(0);
	ground.damping  = 1;
	ground.inv_mass = 0;

	RandomRigidBodies(bodies);

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

	float angle = 0.0f;

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
					//follow = !follow;

				}
				angle += 0.5f;
			}

			if (e.kind == PL_EVENT_KEY_PRESSED && e.key.id == PL_KEY_RETURN) {
				if (!e.key.repeat) {
					RandomRigidBodies(bodies);
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

			float submerged_height = ground.position.y - (boat.position.y - boat_half_height);

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

			Contact contact;
			for (Rigid_Body &body : bodies) {
				if (body.position.y <= ground.position.y) {
					contact.pair.bodies[0]   = &body;
					contact.pair.bodies[1]   = &ground;
					contact.normal      = Vec2(0, 1);
					contact.restitution = 0.5f;
					contact.penetration = ground.position.y - body.position.y;

					Append(&contacts, contact);
				}
			}

#if 0
			if (boat.position.y <= ground.position.y) {
				contact.bodies[0]   = &boat;
				contact.bodies[1]   = &ground;
				contact.normal      = Vec2(0, 1);
				contact.restitution = 0.5f;
				contact.penetration = ground.position.y - boat.position.y;

				Append(&contacts, contact);
			}
#endif

			if (ApplyCableConstraint(&bodies[0], &bodies[1], 2, 0.0f, &contact)) {
				Append(&contacts, contact);
			}

			iterations = ResolveCollisions(contacts, 2 * (int)contacts.count, dt);

			if (follow) {
				camera_pos = Lerp(camera_pos, boat.position, 0.5f);
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

		Vec2 cursor_cam_pos = MapRange(Vec2(0), Vec2(width, height),
			-0.5f * Vec2(cam_width, cam_height), 0.5f * Vec2(cam_width, cam_height),
			cursor_pos);

		R_NextFrame(renderer, R_Rect(0.0f, 0.0f, width, height));
		R_SetPipeline(renderer, GetResource(pipeline));

		R_CameraView(renderer, aspect_ratio, cam_height);
		R_SetLineThickness(renderer, 2.0f * cam_height / height);

		Mat4 camera_transform = Translation(Vec3(camera_pos, 0)) * Scale(Vec3(camera_dist, camera_dist, 1.0f));
		Mat4 world_transform  = Inverse(camera_transform);

		// FIXTURE_SHAPE_CIRCLE,
		// FIXTURE_SHAPE_CAPSULE,
		// FIXTURE_SHAPE_POLYGON,
		// FIXTURE_SHAPE_LINE,

		/*
				circle	capsule	polygon	line
		circle    .      .        .       .
		capsule   x      .        .       .
		polygon   x      x        ?       .
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

		//polygon.count = 4;
		//for (int i = 0; i < 4; ++i)
		//	polygon.vertices[i] = rect.center + RectVertexOffsets[i] * rect.half_size;

		Vec2 point = Vec2(0, 0);

		Rigid_Body body1 = {};
		Rigid_Body body2 = {};

		auto &first_shape = polygon;
		//first_shape.centers[0] = Vec2(-1, 0);
		//first_shape.centers[1] = Vec2(1, 0);
		//first_shape.radius = 0.5f;

		auto &second_shape = line;

		body1.transform.pos = cursor_cam_pos;
		body1.transform.rot = Rotation2x2(DegToRad(angle));
		body2.transform.pos = Vec2(0, 0);
		body2.transform.rot = Rotation2x2(DegToRad(15));

		TFixture<Fixed_Polygon<5>> fix1 = { FIXTURE_SHAPE_POLYGON, first_shape };
		TFixture<Line> fix2 = { FIXTURE_SHAPE_LINE, second_shape };

		DrawShape(renderer, (Polygon &)first_shape, Vec4(1), body1.transform);
		DrawShape(renderer, second_shape, Vec4(1), body2.transform);
		//R_DrawLine(renderer, first_shape.centers[0], first_shape.centers[1], Vec4(1));

		Contact_List contacts;
		contacts.first    = nullptr;
		contacts.arena    = ThreadScratchpad();
		contacts.fallback = {};

		CollidePolygonLine((TFixture<Polygon> *)&fix1, &fix2, {&body1, &body2}, &contacts);

		// origin
		R_DrawRectCentered(renderer, Vec2(0), Vec2(0.1f), Vec4(1, 1, 0, 1));

		for (Contact *contact = contacts.first; contact; contact = contact->next) {
			R_DrawLine(renderer, contact->point, contact->point - contact->penetration * contact->normal, Vec4(0, 1, 0, 1));
			R_DrawCircle(renderer, contact->point, 0.1f, Vec4(1, 1, 0, 1));
		}

#if 0
		point = NearestPoint(body1.transform.pos, (Polygon &)polygon);
		DrawShape(renderer, point, Vec4(1, 0, 0, 1));

		Line_Segment edge = FurthestEdge((Polygon &)polygon, body1.transform.pos);

		Vec4 color = Vec4(1, 1, 0, 1);
		if (Determinant(edge.a - body1.transform.pos, edge.b - body1.transform.pos) <= 0.0f)
			color = Vec4(0, 0, 1, 1);

		DrawShape(renderer, edge, color);

		R_DrawRectCentered(renderer, anchor, Vec2(0.1f), Vec4(1, 1, 0, 1));
		for (const Rigid_Body &body : bodies) {
			float radius = 1.0f / body.inv_mass;
			R_DrawCircleOutline(renderer, body.position, radius, Vec4(1));
			R_DrawRectCentered(renderer, body.position, Vec2(0.1f), Vec4(1));
			//R_DrawLine(renderer, body.position, anchor, Vec4(1, 1, 0, 1));
		}

		float radius = 1.0f / bodies[0].inv_mass;
		R_DrawCircleOutline(renderer, bodies[0].position, radius, Vec4(1, 0, 1, 1));
		R_DrawRectCentered(renderer, bodies[0].position, Vec2(0.1f), Vec4(1));

		R_DrawLine(renderer, Vec2(-10.0f, ground.position.y), Vec2(10.0f, ground.position.y), Vec4(0, 0, 1, 1));
		R_DrawLine(renderer, bodies[0].position, bodies[1].position, Vec4(1, 0, 0, 1));

		//Mat4 transform = Translation(Vec3(boat.position, 0.0f)) * RotationZ(Vec2Arg(boat.orientation)) * Translation(Vec3(-boat.position, 0.0f));

		Mat4 transform = Translation(Vec3(boat.transform.pos, 0.0f)) * RotationZ(Vec2Arg(boat.orientation));

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
