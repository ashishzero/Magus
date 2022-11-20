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
