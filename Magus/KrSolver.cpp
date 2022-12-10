#include "Kr/KrCommon.h"

struct Matrix {
	uint32_t d;
	float   *m;

	float *operator[](uint32_t y) {
		Assert(y < d);
		return &m[y * d];
	}

	const float *operator[](uint32_t y) const {
		Assert(y < d);
		return &m[y * d];
	}
};

struct Vector {
	uint32_t d;
	float   *m;

	float &operator[](uint32_t i) {
		Assert(i < d);
		return m[i];
	}

	const float &operator[](uint32_t i) const {
		Assert(i < d);
		return m[i];
	}
};

void Multiply(Matrix *dst, const Matrix &l, const Matrix &r) {
	Matrix &result = *dst;

	Assert(result.d == l.d && l.d == r.d);

	uint32_t d = result.d;

	for (uint32_t y = 0; y < d; ++y) {
		for (uint32_t x = 0; x < d; ++x) {
			float acc = 0;
			for (uint32_t i = 0; i < d; ++i) {
				acc += l[y][i] * r[i][x];
			}
			result[y][x] = acc;
		}
	}
}

void Transform(Vector *dst, const Matrix &m, const Vector &v) {
	Vector &result = *dst;

	Assert(result.d == m.d && m.d == v.d);

	uint32_t d = result.d;

	for (uint32_t y = 0; y < d; ++y) {
		float acc = 0;
		for (uint32_t x = 0; x < d; ++x) {
			acc += m[y][x] * v[x];
		}
		result[y] = acc;
	}
}

void TransformTransposed(Vector *dst, const Matrix &m, const Vector &v) {
	Vector &result = *dst;

	Assert(result.d == m.d && m.d == v.d);

	uint32_t d = result.d;

	for (uint32_t x = 0; x < d; ++x) {
		float acc = 0;
		for (uint32_t y = 0; y < d; ++y) {
			acc += m[y][x] * v[y];
		}
		result[x] = acc;
	}
}

#include "Kr/KrMath.h"
#include "Kr/KrCommon.h"

struct Damping {
	float linear;
	float rotational;
};

#if 0
struct Rigid_Body {
	float inv_mass;
	float inv_inertia;

	Vec2  x;
	Vec2  R;
	Vec2  P;
	Vec2  L;


};

struct Physics_System {
	uint n;
	Vec2 x;
	Vec2 r;
	Vec2 p;
	Vec2 l;

	//uint     count;
	//Vec2    *positions;
	//Vec2    *orientations;
	//Vec2    *velocities;
	//float   *rotations;
	//Vec2    *accelerations;
	//float   *rotational_accelerations;
	//Damping *damping;
};

void Ode(Physics_System *system, float t, float dt) {
	for (uint i = 0; i < system->count; ++i) {
		system->velocities[i] += system->accelerations[i] * dt;
		system->rotations[i] += system->rotational_accelerations[i] * dt;

		// Approximation
		system->velocities[i] *= 1.0f / (1.0f + dt * system->damping[i].linear);
		system->rotations[i] *= 1.0f / (1.0f + dt * system->damping[i].angular);

		system->positions[i] += system->velocities[i] * dt;
		system->orientations[i] = ComplexProduct(system->orientations[i], Arm(system->rotations[i] * dt));
	}
}
#endif
