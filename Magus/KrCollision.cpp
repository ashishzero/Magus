#include "KrCollision.h"

static void CollideCircleCircle(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Circle &a = GetShapeData<Circle>(shape_a);
	const Circle &b = GetShapeData<Circle>(shape_b);

	Vec2 a_pos = LocalToWorld(bodies[0], a.center);
	Vec2 b_pos = LocalToWorld(bodies[1], b.center);

	Vec2 midline   = a_pos - b_pos;
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

	Contact_Manifold *manifold = AddContact(contacts, bodies, shape_a, shape_b);
	manifold->P           = a_pos + factor * midline;
	manifold->N           = normal;
	manifold->Penetration = min_dist - length;
}

static void CollideCircleCapsule(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Circle &a  = GetShapeData<Circle>(shape_a);
	const Capsule &b = GetShapeData<Capsule>(shape_b);

	Vec2 point = LocalToWorld(bodies[0], a.center);

	// Transform into Capsule's local space
	point = WorldToLocal(bodies[1], point);

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

	Contact_Manifold *manifold = AddContact(contacts, bodies, shape_a, shape_b);
	manifold->P       = LocalToWorld(bodies[1], contact_point);
	manifold->N      = LocalDirectionToWorld(bodies[1], normal);
	manifold->Penetration = radius - length;
}

static void CollideCirclePolygon(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Circle &a  = GetShapeData<Circle>(shape_a);
	const Polygon &b = GetShapeData<Polygon>(shape_b);

	Transform2d ta = CalculateRigidBodyTransform(bodies[0]);
	Transform2d tb = CalculateRigidBodyTransform(bodies[1]);

	Vec2 points[2];
	if (GilbertJohnsonKeerthi(a.center, ta, b, tb, points)) {
		Vec2 dir    = points[0] - points[1];
		float dist2 = LengthSq(dir);

		if (dist2 > a.radius * a.radius)
			return;

		float dist = SquareRoot(dist2);
		Assert(dist != 0);

		Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
		manifold->P       = points[1];
		manifold->N      = dir / dist;
		manifold->Penetration = a.radius - dist;

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
		normal = PerpendicularVector(b.vertices[best_j], b.vertices[best_i]);
		normal = NormalizeZ(normal);
	}

	Contact_Manifold *manifold = AddContact(contacts, bodies, shape_a, shape_b);
	manifold->P                = TransformPoint(tb, point);
	manifold->N                = TransformDirection(tb, normal);
	manifold->Penetration      = a.radius + dist;
}

static void CollideCircleLine(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Circle &a = GetShapeData<Circle>(shape_a);
	const Line &b   = GetShapeData<Line>(shape_b);

	Vec2 a_pos  = LocalToWorld(bodies[0], a.center);
	Vec2 normal = LocalDirectionToWorld(bodies[1], b.normal);

	float perp_dist = DotProduct(normal, a_pos);

	float dist = perp_dist - a.radius - b.offset;

	if (dist > 0) {
		return;
	}

	Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
	manifold->P       = a_pos - (dist + a.radius) * normal;
	manifold->N      = normal;
	manifold->Penetration = -dist;
}

static void CollideCapsuleCapsule(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Capsule &a = GetShapeData<Capsule>(shape_a);
	const Capsule &b = GetShapeData<Capsule>(shape_b);

	Transform2d ta = CalculateRigidBodyTransform(bodies[0]);
	Transform2d tb = CalculateRigidBodyTransform(bodies[1]);

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
					Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
					manifold->P       = points[i];
					manifold->N      = normal;
					manifold->Penetration = penetration;
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

	Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
	manifold->P       = contact_point;
	manifold->N      = normal;
	manifold->Penetration = radius - dist;
}

static void CollideCapsulePolygon(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Capsule &a = GetShapeData<Capsule>(shape_a);
	const Polygon &b = GetShapeData<Polygon>(shape_b);

	Transform2d ta = CalculateRigidBodyTransform(bodies[0]);
	Transform2d tb = CalculateRigidBodyTransform(bodies[1]);

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
					Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
					manifold->P       = world_points[i];
					manifold->N      = world_normal;
					manifold->Penetration = penetration;
				}

				return;
			}
		}
	}

	if (IsNull(world_normal)) {
		// overlapping center
		world_normal = Vec2(0, 1); // arbritrary normal
	}

	Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
	manifold->P       = world_points[1];
	manifold->N      = world_normal;
	manifold->Penetration = penetration;
}

static void CollideCapsuleLine(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Capsule &a = GetShapeData<Capsule>(shape_a);
	const Line &b    = GetShapeData<Line>(shape_b);

	Vec2 centers[2];
	centers[0]  = LocalToWorld(bodies[0], a.centers[0]);
	centers[1]  = LocalToWorld(bodies[0], a.centers[1]);
	Vec2 normal = LocalDirectionToWorld(bodies[1], b.normal);

	uint contact_count = 0;

	for (Vec2 center : centers) {
		float perp_dist = DotProduct(b.normal, center);
		float dist      = perp_dist - a.radius - b.offset;

		if (dist <= 0) {
			Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
			manifold->P       = center - (dist + a.radius) * normal;
			manifold->N      = normal;
			manifold->Penetration = -dist;

			contact_count += 1;
		}
	}
}

static void CollidePolygonLine(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Polygon &a = GetShapeData<Polygon>(shape_a);
	const Line &b    = GetShapeData<Line>(shape_b);

	Vec2 world_normal = LocalDirectionToWorld(bodies[1], b.normal);
	Vec2 direction    = -WorldDirectionToLocal(bodies[0], world_normal);

	Line_Segment edge = FurthestEdge(a, direction);

	Vec2 vertices[] = { edge.a, edge.b };
	Transform2d ta  = CalculateRigidBodyTransform(bodies[0]);

	for (Vec2 vertex : vertices) {
		vertex     = TransformPoint(ta, vertex);
		float perp = DotProduct(world_normal, vertex);
		float dist = perp - b.offset;

		if (dist <= 0.0f) {
			Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_b, shape_a);
			manifold->P       = vertex - dist * world_normal;
			manifold->N      = world_normal;
			manifold->Penetration = -dist;
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

// todo: optimize: https://www.gdcvault.com/play/1017646/Physics-for-Game-Programmers-The
static void CollidePolygonPolygon(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
	const Polygon &a = GetShapeData<Polygon>(shape_a);
	const Polygon &b = GetShapeData<Polygon>(shape_b);

	Transform2d ta = CalculateRigidBodyTransform(bodies[0]);
	Transform2d tb = CalculateRigidBodyTransform(bodies[1]);

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
		best_normal = normal;
	}

	for (i = 0; i < a.count - 1; ++i) {
		j = i + 1;

		normal = NormalizeZ(PerpendicularVector(a.vertices[i], a.vertices[j]));
		if (!PolygonPolygonOverlap(a, ta, b, tb, normal, &overlap))
			return;

		if (overlap < min_overlap) {
			min_overlap = overlap;
			best_normal = normal;
		}
	}

	i = b.count - 1;
	j = 0;

	normal = NormalizeZ(PerpendicularVector(b.vertices[i], b.vertices[j]));
	if (!PolygonPolygonOverlap(a, ta, b, tb, normal, &overlap))
		return;

	if (overlap < min_overlap) {
		min_overlap = overlap;
		best_normal = -normal;
	}

	for (i = 0; i < b.count - 1; ++i) {
		j = i + 1;

		normal = NormalizeZ(PerpendicularVector(b.vertices[i], b.vertices[j]));
		if (!PolygonPolygonOverlap(a, ta, b, tb, normal, &overlap))
			return;

		if (overlap < min_overlap) {
			min_overlap = overlap;
			best_normal = -normal;
		}
	}

	if (DotProduct(best_normal, ta.pos - tb.pos) < 0.0f)
		best_normal = -best_normal;

	float penetration = overlap;
	normal            = best_normal;

	Farthest_Edge_Desc edge_a = FarthestEdgeDesc(a, ta, -normal);
	Farthest_Edge_Desc edge_b = FarthestEdgeDesc(b, tb, normal);

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

				Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
				manifold->N      = normal;
				manifold->P       = points[i];
				manifold->Penetration = DotProduct(reference_normal, reference.vertices[0] - points[i]);
			}
		}

		return;
	}

	// Only single vertex is in the reference edge
	Contact_Manifold *manifold     = AddContact(contacts, bodies, shape_a, shape_b);
	manifold->N      = normal;
	manifold->P       = incident.furthest_vertex;
	manifold->Penetration = penetration;
}

static void CollideLineLine(const Shape *shape_a, const Shape *shape_b, Rigid_Body *(&bodies)[2], Contact_Desc *contacts) {
}

//
//
//

typedef void(*Collide_Proc)(const Shape *, const Shape *, Rigid_Body *(&bodies)[2], Contact_Desc *);

static Collide_Proc Collides[SHAPE_KIND_COUNT][SHAPE_KIND_COUNT] = {
	{ CollideCircleCircle, CollideCircleCapsule,  CollideCirclePolygon,  CollideCircleLine,  },
	{ nullptr,             CollideCapsuleCapsule, CollideCapsulePolygon, CollideCapsuleLine, },
	{ nullptr,             nullptr,               CollidePolygonPolygon, CollidePolygonLine, },
	{ nullptr,             nullptr,               nullptr,               CollideLineLine     },
};

void Collide(Shape *first, Shape *second, Rigid_Body *first_body, Rigid_Body *second_body, Contact_Desc *contacts) {
	Rigid_Body *bodies[2];

	if (first->shape < second->shape) {
		bodies[0] = first_body;
		bodies[1] = second_body;
	} else {
		Swap(&first, &second);
		bodies[0] = second_body;
		bodies[1] = first_body;
	}

	Collides[first->shape][second->shape](first, second, bodies, contacts);
}
