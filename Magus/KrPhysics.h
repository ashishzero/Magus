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

struct Geometry {
	uint    count;
	Shape **shapes;
};

struct Contact_Solver_Data {
	Mat2 transform;
	Vec2 closing_velocity;
	//Vec2 desired_delta_velocity;
	Vec2 relative_positions[2];
};

struct Rigid_Body;

struct Contact_Manifold {
	Rigid_Body *bodies[2];
	Vec2        point;
	Vec2        normal;
	float       penetration;

	// Surface information
	float       restitution;
	float       friction;

	Contact_Solver_Data data;
};

struct Contact_Desc {
	Array<Contact_Manifold> manifolds;
	Contact_Manifold        fallback;
};

//
//
//

enum Rigid_Body_Flags : uint {
	RIGID_BODY_IS_STATIC = 0x1,
	RIGID_BODY_IS_AWAKE  = 0x2,
	RIGID_BODY_ROTATES   = 0x4,
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

	uint     Flags;

	Geometry geometry;
};

//
//
//

void             Wake(Rigid_Body *body);
Vec2             LocalToWorld(const Rigid_Body *body, Vec2 local);
Vec2             WorldToLocal(const Rigid_Body *body, Vec2 world);
Vec2             LocalDirectionToWorld(const Rigid_Body *body, Vec2 local_dir);
Vec2             WorldDirectionToLocal(const Rigid_Body *body, Vec2 world_dir);

Transform2d      CalculateRigidBodyTransform(Rigid_Body *body);

void             ApplyForce(Rigid_Body *body, Vec2 force);
void             ApplyForce(Rigid_Body *body, Vec2 force, Vec2 point_in_world);
void             ApplyForceAtBodyPoint(Rigid_Body *body, Vec2 force, Vec2 point);
void             ApplyTorque(Rigid_Body *body, float T);
void             ApplyLinearImpulse(Rigid_Body *body, Vec2 I);
void             ApplyLinearImpulse(Rigid_Body *body, Vec2 I, Vec2 P);
void             ApplyLinearImpulseAtBodyPoint(Rigid_Body *body, Vec2 I, Vec2 rP);
void             AppleAngularImpulse(Rigid_Body *body, float I);

void              SetSurfaceData(uint i, uint j, float restitution, float friction);
void              GetSurfaceData(uint i, uint j, float *restitution, float *friction);
float             GetSurfaceFriction(uint i, uint j);
float             GetSurfaceRestitution(uint i, uint j);
Contact_Manifold *AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], float restitution, float friction);
Contact_Manifold *AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], const Shape *a, const Shape *b);
void              AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], const Shape *a, const Shape *b, Vec2 normal, Vec2 point, float penetration);
void              AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], float restitution, float friction, Vec2 normal, Vec2 point, float penetration);
