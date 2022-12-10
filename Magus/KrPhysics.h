#pragma once
#include "Kr/KrMath.h"
#include "Kr/KrArray.h"

enum Shape_Kind {
	SHAPE_KIND_CIRCLE,
	SHAPE_KIND_CAPSULE,
	SHAPE_KIND_POLYGON,
	SHAPE_KIND_LINE,
	SHAPE_KIND_COUNT,
};

struct Shape { Shape_Kind shape; uint32_t surface; };
template <typename T> struct TShape : Shape { T data; };

template <typename T>
const T &GetShapeData(const Shape *_shape) {
	const TShape<T> *shape = (const TShape<T> *)_shape;
	return shape->data;
}

struct Geometry {
	uint    Count;
	Shape **Data;
};

struct Contact_Solver_Data {
	Mat2 transform;
	Vec2 closing_velocity;
	//Vec2 desired_delta_velocity;
	Vec2 relative_positions[2];
};

struct Rigid_Body;

struct Contact_Manifold { // todo: rename
	Rigid_Body *Bodies[2];
	Vec2        P;
	Vec2        N;
	float       Penetration;
	float       kRestitution;
	float       kFriction;

	Contact_Solver_Data data;
};

struct Contact_Desc { // todo: rename
	Array<Contact_Manifold> manifolds;
	Contact_Manifold        fallback;
};

//
//
//

enum Rigid_Body_Kind : uint32_t {
	RIGID_BODY_STATIC,
	RIGID_BODY_KINEMATIC,
	RIGID_BODY_DYNAMIC,
};

enum Rigid_Body_Flags : uint32_t {
	RIGID_BODY_IS_AWAKE    = 0x1,
	RIGID_BODY_ROTATES     = 0x2,
	RIGID_BODY_ALLOW_SLEEP = 0x4
};

struct Rigid_Body {
	Vec2     P;
	Vec2     W;
	Vec2     dP;
	float    dW;

	float    DF;
	float    WDF;
	float    invM;
	float    invI;

	Vec2     d2P;
	Vec2     F;
	float    T;


	float depth;

	Rigid_Body_Kind Kind;
	uint            Flags;

	Geometry        Shapes;
};

//
//
//

bool             IsAwake(Rigid_Body *body);
void             Wake(Rigid_Body *body);
Vec2             LocalToWorld(const Rigid_Body *body, Vec2 P);
Vec2             WorldToLocal(const Rigid_Body *body, Vec2 P);
Vec2             LocalDirectionToWorld(const Rigid_Body *body, Vec2 N);
Vec2             WorldDirectionToLocal(const Rigid_Body *body, Vec2 N);

Transform2d      CalculateRigidBodyTransform(const Rigid_Body *body);

void             ApplyForce(Rigid_Body *body, Vec2 F);
void             ApplyForce(Rigid_Body *body, Vec2 F, Vec2 P);
void             ApplyForceAtBodyPoint(Rigid_Body *body, Vec2 F, Vec2 rP);
void             ApplyTorque(Rigid_Body *body, float T);
void             ApplyLinearImpulse(Rigid_Body *body, Vec2 I);
void             ApplyLinearImpulse(Rigid_Body *body, Vec2 I, Vec2 P);
void             ApplyLinearImpulseAtBodyPoint(Rigid_Body *body, Vec2 I, Vec2 rP);
void             AppleAngularImpulse(Rigid_Body *body, float I);

void              SetSurfaceData(uint i, uint j, float kRestitution, float kFriction);
void              GetSurfaceData(uint i, uint j, float *kRestitution, float *kFriction);
float             GetSurfaceFriction(uint i, uint j);
float             GetSurfaceRestitution(uint i, uint j);
Contact_Manifold *AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], float kRestitution, float kFriction);
Contact_Manifold *AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], const Shape *a, const Shape *b);
void              AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], const Shape *a, const Shape *b, Vec2 N, Vec2 P, float penetration);
void              AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], float kRestitution, float kFriction, Vec2 N, Vec2 P, float penetration);
