#include "KrPhysics.h"
#include "Kr/KrLog.h"

#ifndef KR_MAX_SURFACE_COUNT
#define KR_MAX_SURFACE_COUNT 64
#endif

static constexpr uint MAX_SURFACE_COUNT = KR_MAX_SURFACE_COUNT;

static float FrictionTable[MAX_SURFACE_COUNT][MAX_SURFACE_COUNT];
static float RestitutionTable[MAX_SURFACE_COUNT][MAX_SURFACE_COUNT];

Vec2 LocalToWorld(const Rigid_Body *body, Vec2 P) {
	Vec2 p = ComplexProduct(body->W, P);
	p += body->P;
	return p;
}

Vec2 WorldToLocal(const Rigid_Body *body, Vec2 P) {
	P -= body->P;
	Vec2 p = ComplexProduct(ComplexConjugate(body->W), P);
	return p;
}

Vec2 LocalDirectionToWorld(const Rigid_Body *body, Vec2 N) {
	Vec2 p = ComplexProduct(body->W, N);
	return p;
}

Vec2 WorldDirectionToLocal(const Rigid_Body *body, Vec2 N) {
	Vec2 p = ComplexProduct(ComplexConjugate(body->W), N);
	return p;
}

Transform2d CalculateRigidBodyTransform(const Rigid_Body *body) {
	float cosine = body->W.x;
	float sine = body->W.y;

	Transform2d transform;
	transform.rot = Rotation2x2(body->W);
	transform.pos = body->P;

	return transform;
}

bool IsAwake(Rigid_Body *body) {
	return (body->Flags & RIGID_BODY_IS_AWAKE);
}

void Wake(Rigid_Body *body) {
	body->Flags |= RIGID_BODY_IS_AWAKE;
}

void ApplyForce(Rigid_Body *body, Vec2 F) {
	body->F += F;
	Wake(body);
}

void ApplyForce(Rigid_Body *body, Vec2 F, Vec2 P) {
	Vec2 rP = P - body->P;

	body->F += F;

	if (body->Flags & RIGID_BODY_ROTATES)
		body->T += CrossProduct(F, rP);

	Wake(body);
}

void ApplyForceAtBodyPoint(Rigid_Body *body, Vec2 force, Vec2 rP) {
	Vec2 P = LocalToWorld(body, rP);
	ApplyForce(body, force, P);
}

void ApplyTorque(Rigid_Body *body, float T) {
	body->T += T;
	Wake(body);
}

void ApplyLinearImpulse(Rigid_Body *body, Vec2 I) {
	body->dP += body->invM * I;
	Wake(body);
}

void ApplyLinearImpulse(Rigid_Body *body, Vec2 I, Vec2 P) {
	Vec2 rP = P - body->P;

	body->dP += body->invM * I;

	if (body->Flags & RIGID_BODY_ROTATES)
		body->dW += body->invI * CrossProduct(I, rP);

	Wake(body);
}

void ApplyLinearImpulseAtBodyPoint(Rigid_Body *body, Vec2 I, Vec2 rP) {
	Vec2 P = LocalToWorld(body, rP);
	ApplyLinearImpulse(body, I, P);
}

void AppleAngularImpulse(Rigid_Body *body, float I) {
	body->dW += body->invI * I;
	Wake(body);
}

void SetSurfaceData(uint i, uint j, float restitution, float friction) {
	Assert(i < MAX_SURFACE_COUNT && j < MAX_SURFACE_COUNT);
	RestitutionTable[i][j] = restitution;
	RestitutionTable[j][i] = restitution;
	FrictionTable[i][j]    = friction;
	FrictionTable[j][i]    = friction;
}

void GetSurfaceData(uint i, uint j, float *restitution, float *friction) {
	Assert(i < MAX_SURFACE_COUNT && j < MAX_SURFACE_COUNT);
	*restitution = RestitutionTable[i][j];
	*friction    = FrictionTable[i][j];
}

float GetSurfaceFriction(uint i, uint j) {
	Assert(i < MAX_SURFACE_COUNT && j < MAX_SURFACE_COUNT);
	return FrictionTable[i][j];
}

float GetSurfaceRestitution(uint i, uint j) {
	Assert(i < MAX_SURFACE_COUNT && j < MAX_SURFACE_COUNT);
	return RestitutionTable[i][j];
}

Contact_Manifold *AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], float kRestitution, float kFriction) {
	Contact_Manifold *manifold = Append(&contacts->manifolds);
	if (manifold) {
		manifold->Bodies[0]    = bodies[0];
		manifold->Bodies[1]    = bodies[1];
		manifold->kRestitution = kRestitution;
		manifold->kFriction    = kFriction;
		return manifold;
	}

	LogWarning("[Physics]: Failed to allocate new manifold point");
	return &contacts->fallback;
}

Contact_Manifold *AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], const Shape *a, const Shape *b) {
	float restitution = GetSurfaceRestitution(a->surface, b->surface);
	float friction    = GetSurfaceFriction(a->surface, b->surface);
	return AddContact(contacts, bodies, restitution, friction);
}

void AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], const Shape *a, const Shape *b, Vec2 N, Vec2 P, float penetration) {
	Contact_Manifold *manifold = AddContact(contacts, bodies, a, b);
	manifold->N           = N;
	manifold->P           = P;
	manifold->Penetration = penetration;
}

void AddContact(Contact_Desc *contacts, Rigid_Body *(&bodies)[2], float kRestitution, float kFriction, Vec2 N, Vec2 P, float penetration) {
	Contact_Manifold *manifold = AddContact(contacts, bodies, kRestitution, kFriction);
	manifold->N           = N;
	manifold->P           = P;
	manifold->Penetration = penetration;
}
