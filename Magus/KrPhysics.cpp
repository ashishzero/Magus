#include "KrPhysics.h"

Vec2 LocalToWorld(const Rigid_Body *body, Vec2 local) {
	Vec2 p = ComplexProduct(body->W, local);
	p += body->P;
	return p;
}

Vec2 WorldToLocal(const Rigid_Body *body, Vec2 world) {
	world -= body->P;
	Vec2 p = ComplexProduct(ComplexConjugate(body->W), world);
	return p;
}

Vec2 LocalDirectionToWorld(const Rigid_Body *body, Vec2 local_dir) {
	Vec2 p = ComplexProduct(body->W, local_dir);
	return p;
}

Vec2 WorldDirectionToLocal(const Rigid_Body *body, Vec2 world_dir) {
	Vec2 p = ComplexProduct(ComplexConjugate(body->W), world_dir);
	return p;
}

Transform2d CalculateRigidBodyTransform(Rigid_Body *body) {
	float cosine = body->W.x;
	float sine = body->W.y;

	Transform2d transform;
	transform.rot = Rotation2x2(body->W);
	transform.pos = body->P;

	return transform;
}

void ApplyForce(Rigid_Body *body, Vec2 force) {
	body->F += force;

	body->flags |= RIGID_BODY_IS_AWAKE;
}

void ApplyForce(Rigid_Body *body, Vec2 force, Vec2 point_in_world) {
	Vec2 rel = point_in_world - body->P;

	body->F += force;
	body->T += CrossProduct(force, rel);

	body->flags |= RIGID_BODY_IS_AWAKE;
}

void ApplyForceAtBodyPoint(Rigid_Body *body, Vec2 force, Vec2 point) {
	Vec2 point_in_world = LocalToWorld(body, point);
	return ApplyForce(body, force, point_in_world);
}
