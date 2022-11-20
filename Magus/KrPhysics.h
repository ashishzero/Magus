#pragma once
#include "Kr/KrMath.h"

enum Shape_Kind {
	SHAPE_KIND_CIRCLE,
	SHAPE_KIND_CAPSULE,
	SHAPE_KIND_POLYGON,
	SHAPE_KIND_LINE,
	SHAPE_KIND_COUNT,
};

struct Shape { Shape_Kind shape; };
template <typename T> struct TShape : Shape { T data; };

struct Geometry {
	uint    count;
	Shape **shapes;
};

//
//
//

enum Rigid_Body_Flags : uint {
	RIGID_BODY_IS_AWAKE = 0x1
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

	uint     flags;

	Geometry geometry;
};

//
//
//

Vec2        LocalToWorld(const Rigid_Body *body, Vec2 local);
Vec2        WorldToLocal(const Rigid_Body *body, Vec2 world);
Vec2        LocalDirectionToWorld(const Rigid_Body *body, Vec2 local_dir);
Vec2        WorldDirectionToLocal(const Rigid_Body *body, Vec2 world_dir);

Transform2d CalculateRigidBodyTransform(Rigid_Body *body);

void        ApplyForce(Rigid_Body *body, Vec2 force);
void        ApplyForce(Rigid_Body *body, Vec2 force, Vec2 point_in_world);
void        ApplyForceAtBodyPoint(Rigid_Body *body, Vec2 force, Vec2 point);
