#pragma once

#include "Kr/KrMath.h"
#include "Kr/KrCommon.h"
#include "Kr/KrMap.h"
#include "Render2d.h"

int32_t HexLength(Vec3i h) {
	return (abs(h.x) + abs(h.y) + abs(h.z)) / 2;
}

int32_t HexDistance(Vec3i a, Vec3i b) {
	return HexLength(b - a);
}

enum Hex_Dir {
	HEX_DIR_R,
	HEX_DIR_BR,
	HEX_DIR_BL,
	HEX_DIR_L,
	HEX_DIR_TL,
	HEX_DIR_TR,
};

static const String HexDirNames[] = {
	"Right", "Bottom-Right", "Bottom-Left", "Left", "Top-Left", "Top-Right"
};

static const Vec3i HexDirectionValues[] = {
	Vec3i(1, 0, -1), Vec3i(1, -1, 0), Vec3i(0, -1, 1),
	Vec3i(-1, 0, 1), Vec3i(-1, 1, 0), Vec3i(0, 1, -1)
};

static_assert(ArrayCount(HexDirNames) == ArrayCount(HexDirectionValues), "");

Vec3i HexDirection(int i) {
	Assert(i >= 0 && i < ArrayCount(HexDirectionValues));
	return HexDirectionValues[i];
}

Vec3i HexNeighbor(Vec3i h, int dir) {
	return h + HexDirection(dir);
}

Vec3 HexNeighbor(Vec3 h, int dir) {
	Vec3i offset = HexDirection(dir);
	return h + Vec3((float)offset.x, (float)offset.y, (float)offset.z);
}

enum Hex_Kind {
	HEX_POINTY_TOP,
	HEX_FLAT_TOP
};

static const Mat2 HexTransforms[] = {
	Mat2(sqrtf(3.0f), sqrtf(3.0f) / 2.0f, 0.0f, 3.0f / 2.0f),
	Mat2(3.0f / 2.0f, 0.0f, sqrtf(3.0f) / 2.0f, sqrtf(3.0f))
};

static const Mat2 HexInvTransforms[] = {
	Mat2(sqrtf(3.0f) / 3.0f, -1.0f / 3.0f, 0.0f, 2.0f / 3.0f),
	Mat2(2.0f / 3.0f, 0.0f, -1.0f / 3.0f, sqrtf(3.0f) / 3.0f)
};

static const float HexStartAngle[] = { 0.5f, 0.0f };

Vec2 HexToPixel(Vec2 hex, Vec2 origin, Vec2 scale, Hex_Kind kind) {
	Vec2 pixel = HexTransforms[kind] * hex;
	Vec2 result = pixel * scale + origin;
	return result;
}

Vec2 HexToPixel(Vec3 hex, Vec2 origin, Vec2 scale, Hex_Kind kind) {
	return HexToPixel(hex._0.xy, origin, scale, kind);
}

Vec2 HexToPixel(Vec3i hex, Vec2 origin, Vec2 scale, Hex_Kind kind) {
	Vec2 hexf = Vec2((float)hex.x, (float)hex.y);
	return HexToPixel(hexf, origin, scale, kind);
}

Vec3 PixelToHex(Vec2 p, Vec2 origin, Vec2 scale, Hex_Kind kind) {
	p = (p - origin) / scale;
	Vec2 t = HexInvTransforms[kind] * p;
	float q = t.x;
	float r = t.y;
	return Vec3(q, r, -q - r);
}

Vec3i HexRound(Vec3 h) {
	int32_t q = (int32_t)(roundf(h.x));
	int32_t r = (int32_t)(roundf(h.y));
	int32_t s = (int32_t)(roundf(h.z));
	float q_diff = Absolute((float)q - h.x);
	float r_diff = Absolute((float)r - h.y);
	float s_diff = Absolute((float)s - h.z);

	if (q_diff > r_diff && q_diff > s_diff) {
		q = -r - s;
	}
	else if (r_diff > s_diff) {
		r = -q - s;
	}
	else {
		s = -q - r;
	}

	return Vec3i(q, r, s);
}

Vec2 HexCornerOffset(int corner, Vec2 size, Hex_Kind kind) {
	float angle = 2.0f * PI * (HexStartAngle[kind] + corner) / 6;
	return Vec2(size.x * cosf(angle), size.y * sinf(angle));
}

void HexCorners(Vec3i h, Vec2 origin, Vec2 scale, Hex_Kind kind, Vec2 corners[6]) {
	Vec2 center = HexToPixel(h, origin, scale, kind);
	for (int i = 0; i < 6; i++) {
		Vec2 offset = HexCornerOffset(i, scale, kind);
		corners[i] = center + offset;
	}
}

void HexCorners(Vec3 h, Vec2 origin, Vec2 scale, Hex_Kind kind, Vec2 corners[6]) {
	Vec2 center = HexToPixel(h, origin, scale, kind);
	for (int i = 0; i < 6; i++) {
		Vec2 offset = HexCornerOffset(i, scale, kind);
		corners[i] = center + offset;
	}
}

Vec3i Hex(int32_t q, int32_t r, int32_t s) {
	Assert(q + r + s == 0);
	return Vec3i(q, r, s);
}

Vec3i Hex(int32_t q, int32_t r) {
	return Hex(q, r, -q - r);
}

//
//
//

constexpr Hex_Kind HexKindCurrent = HEX_POINTY_TOP;
const     float    HexRadius = 0.5f;

struct Entity {
	Vec3i position;
	Vec3  render_position;
	Array<Vec3i> target_positions;
};

struct Force_Field : Entity {
	Array<Hex_Dir> direction;
};

struct Rotor : Entity {
	int     length;
	float   render_angle;
	Hex_Dir dir;
	Hex_Dir target_dir;
};

struct Actor : Entity {
};

struct Hex_Tile {
	Vec3i   pos;
};

struct Hex_Map {
	ptrdiff_t       index[100][100];
	Array<Hex_Tile> tiles;
};

Hex_Tile *HexFindTile(Hex_Map *map, Vec3i pos) {
	int32_t x = pos.x + 50;
	int32_t y = pos.y + 50;

	if (IsInRange(0, 99, x) && IsInRange(0, 99, y)) {
		ptrdiff_t index = map->index[y][x];
		if (index > 0) {
			return &map->tiles[index - 1];
		}
	}

	return nullptr;
}

Hex_Tile *HexFindOrDefaultTile(Hex_Map *map, Vec3i pos) {
	int32_t x = pos.x + 50;
	int32_t y = pos.y + 50;

	if (IsInRange(0, 99, x) && IsInRange(0, 99, y)) {
		ptrdiff_t index = map->index[y][x];
		if (index > 0) return &map->tiles[index - 1];
		Hex_Tile *tile = Append(&map->tiles);
		if (tile) {
			Assert(map->tiles.count);
			*tile = Hex_Tile{};
			tile->pos = pos;
			map->index[y][x] = map->tiles.count;
		}
		return tile;
	}

	return nullptr;
}

void HexRemoveTile(Hex_Map *map, Vec3i pos) {
	Hex_Tile *tile = HexFindTile(map, pos);
	if (tile) {
		int32_t x = tile->pos.x + 50;
		int32_t y = tile->pos.y + 50;

		ptrdiff_t index = map->index[y][x];
		Assert(index > 0);
		map->index[y][x] = 0;

		RemoveUnordered(&map->tiles, index - 1);
		Vec3i new_pos = map->tiles[index - 1].pos;

		map->index[new_pos.y + 50][new_pos.x + 50] = index;
	}
}

Hex_Tile *HexToggleTileLife(Hex_Map *map, Vec3i pos) {
	// todo: optimize this
	if (HexFindTile(map, pos)) {
		HexRemoveTile(map, pos);
		return nullptr;
	}

	return HexFindOrDefaultTile(map, pos);
}

struct Hex_Neighbors {
	int   count;
	Vec3i data[6];

	Vec3i operator[](int index) {
		Assert(index < count);
		return data[index];
	}
};

int NeighborCount(const Hex_Neighbors &neighbors) {
	return neighbors.count;
}

Hex_Neighbors Neighbors(Hex_Map *map, Vec3i val) {
	Hex_Neighbors neighbors;
	neighbors.count = 0;
	for (int i = 0; i < 6; ++i) {
		Vec3i neighbor = HexNeighbor(val, i);
		if (HexFindTile(map, val)) {
			neighbors.data[neighbors.count++] = neighbor;
		}
	}
	return neighbors;
}

int NavigationCost(Vec3i start, Vec3i target) {
	return HexDistance(start, target);
}

int HeuristicCost(Vec3i start, Vec3i target) {
	return HexDistance(start, target);
}

#pragma region
template <typename T>
void HeapPush(T *root, ptrdiff_t count) {
	ptrdiff_t height = count - 1;
	while (height != 0) {
		ptrdiff_t parent = (height - 1) / 2;
		if (root[parent] > root[height]) {
			Swap(&root[parent], &root[height]);
			height = parent;
		}
		else {
			break;
		}
	}
}

template <typename T>
void HeapPop(T *root, ptrdiff_t count) {
	Assert(count > 0);

	ptrdiff_t height = count - 1;

	root[0] = root[height];

	ptrdiff_t current = 0;
	ptrdiff_t left = 2 * current + 1;

	while (left < height) {
		ptrdiff_t index = left;

		ptrdiff_t right = 2 * current + 2;
		if (right < height) {
			if (root[index] > root[right]) {
				index = right;
			}
		}

		if (root[current] > root[index]) {
			Swap(&root[current], &root[index]);
			current = index;
		}
		else {
			break;
		}

		left = 2 * current + 1;
	}
}

template <typename T>
void HeapSort(T *arr, ptrdiff_t count) {
	for (ptrdiff_t i = 1; i < count; ++i) {
		HeapPush(arr, i);
	}
}

template <typename T>
struct Priority_Node {
	T   data;
	int priority;
};

template <typename T>
struct Priority_Queue {
	using Node = Priority_Node<T>;

	Array<Node> min_heap;

	Priority_Queue() {}
	Priority_Queue(M_Allocator allocator) : min_heap(allocator) {}
};

template <typename T>
bool operator==(const Priority_Node<T> &a, const Priority_Node<T> &b) {
	return a.priority == b.priority;
}

template <typename T>
bool operator>(const Priority_Node<T> &a, const Priority_Node<T> &b) {
	return a.priority > b.priority;
}

template <typename T>
bool operator<(const Priority_Node<T> &a, const Priority_Node<T> &b) {
	return a.priority < b.priority;
}

template <typename T>
void Put(Priority_Queue<T> *queue, T val, int priority) {
	Append(&queue->min_heap, { val, priority });
	HeapPush(queue->min_heap.data, queue->min_heap.count);
}

template <typename T>
T Pop(Priority_Queue<T> *queue) {
	Priority_Node<T> val = First(queue->min_heap);
	HeapPop(queue->min_heap.data, queue->min_heap.count);
	queue->min_heap.count -= 1;
	return val.data;
}

template <typename T>
bool IsEmpty(const Priority_Queue<T> &q) {
	return q.min_heap.count == 0;
}

Array_View<Vec3i> FindPath(Hex_Map *map, Vec3i start, Vec3i target) {
	M_Arena *arena = ThreadScratchpad(0);

	M_Temporary temp = M_BeginTemporaryMemory(arena);
	M_Allocator allocator = M_GetArenaAllocator(arena);

	Priority_Queue queue = Priority_Queue<Vec3i>(allocator);
	Map reverse_paths = Map<Vec3i, Vec3i>(allocator);
	Map path_cost = Map<Vec3i, int>(allocator);

	Put(&queue, start, 0);
	Put(&path_cost, start, 0);

	while (!IsEmpty(queue)) {
		Vec3i current = Pop(&queue);

		if (current == target)
			break;

		auto neighbors = Neighbors(map, current);

		for (int i = 0; i < NeighborCount(neighbors); ++i) {
			Vec3i next = neighbors[i];

			int cost = path_cost[current] + NavigationCost(current, next);
			if (!Find(path_cost, next) || cost < path_cost[next]) {
				path_cost[next] = cost;
				int priority = cost + HeuristicCost(next, target);
				Put(&queue, next, priority);
				reverse_paths[next] = current;
			}
		}
	}

	Array<Vec3i> path;

	if (Find(reverse_paths, target)) {
		Vec3i current = target;
		while (current != start) {
			Append(&path, current);
			current = reverse_paths[current];
		}
	}

	M_EndTemporaryMemory(&temp);

	return path;
}
#pragma endregion Path Finding

void DrawHexagon(R_Renderer2d *renderer, Vec3i pos, Vec4 color = Vec4(1), bool outline = true) {
	Vec2 corners[6];
	HexCorners(pos, Vec2(0.0f), Vec2(HexRadius), HexKindCurrent, corners);
	if (outline)
		R_DrawPolygonOutline(renderer, corners, 6, color);
	else
		R_DrawPolygon(renderer, corners, 6, color);
}

void DrawHexagon(R_Renderer2d *renderer, Vec3 pos, Vec4 color = Vec4(1), bool outline = true) {
	Vec2 corners[6];
	HexCorners(pos, Vec2(0.0f), Vec2(HexRadius), HexKindCurrent, corners);
	if (outline)
		R_DrawPolygonOutline(renderer, corners, 6, color);
	else
		R_DrawPolygon(renderer, corners, 6, color);
}

Vec3 Vec3iF(Vec3i v) {
	return Vec3((float)v.x, (float)v.y, (float)v.z);
}

void HexStuffs() {
	Hex_Map *map = new Hex_Map;

	for (int i = 0; i < 100; ++i) {
		for (int j = 0; j < 100; ++j) {
			map->index[i][j] = 0;
		}
	}

	// pointy top
	for (int r = -5; r <= 5; r++) {
		int r_offset = r >> 1;
		for (int q = -8 - r_offset; q <= 8 - r_offset; q++) {
			HexFindOrDefaultTile(map, Hex(q, r));
		}
	}

	Force_Field force_field;

	force_field.position = Hex(0, 0, 0);
	force_field.render_position = Vec3iF(force_field.position);
	Append(&force_field.direction, HEX_DIR_TR);
	Append(&force_field.direction, HEX_DIR_TR);
	Append(&force_field.direction, HEX_DIR_R);
	Append(&force_field.direction, HEX_DIR_R);
	Append(&force_field.direction, HEX_DIR_BL);
	Append(&force_field.direction, HEX_DIR_BL);
	Append(&force_field.direction, HEX_DIR_L);
	Append(&force_field.direction, HEX_DIR_L);

	Actor actors[2];
	actors[0].position = Vec3i(0, 2, -2);
	actors[0].render_position = Vec3iF(actors[0].position);

	actors[1].position = HexNeighbor(actors[0].position, HEX_DIR_R);
	actors[1].render_position = Vec3iF(actors[1].position);

	Rotor rotor;
	rotor.position = Vec3i(0, 4, -4);
	rotor.render_position = Vec3iF(rotor.position);
	rotor.dir = HEX_DIR_TR;
	rotor.target_dir = rotor.dir;
	rotor.length = 4;
	rotor.render_angle = rotor.dir * -60.0f;

#if 0
	if (e.kind == PL_EVENT_BUTTON_PRESSED) {
		float cam_height = 10.0f;
		float aspect_ratio = width / height;

		float cam_width = aspect_ratio * cam_height;

		Vec2 cursor_cam_pos = MapRange(Vec2(0), Vec2(width, height),
			-0.5f * Vec2(cam_width, cam_height), 0.5f * Vec2(cam_width, cam_height),
			cursor_pos);

		Vec3 hex_cursor = PixelToHex(cursor_cam_pos, Vec2(0), Vec2(HexRadius), HexKindCurrent);

		Vec3i hex = HexRound(hex_cursor);

		if (e.button.id == PL_BUTTON_LEFT) {
			move_pressed = true;
		}
		else if (e.button.id == PL_BUTTON_RIGHT) {
			HexToggleTileLife(map, hex);
		}
	}
#endif

#if 0

	Vec3 hex_cursor = PixelToHex(cursor_cam_pos, Vec2(0), Vec2(HexRadius), HexKindCurrent);

	Vec3i hex = HexRound(hex_cursor);

	Entity *entities[] = { &actors[0], &actors[1], &rotor, &force_field };

	for (Entity *entity : entities) {
		if (entity->target_positions.count) {
			Vec3i t_posi = First(entity->target_positions);
			Vec3 t_pos = Vec3iF(t_posi);

			entity->render_position = MoveTowards(entity->render_position, t_pos, 0.08f);

			if (IsNull(entity->render_position - t_pos)) {
				entity->render_position = t_pos;
				entity->position = t_posi;
				Remove(&entity->target_positions, 0); // todo: optimize
			}
		}
	}

	{
		float render_angle = rotor.render_angle;
		float target_angle = rotor.target_dir * -60.0f;

		float offset_angle = 0.0f;
		float diff = target_angle - render_angle;
		if (Absolute(diff) > 180.0f) {
			offset_angle = -Sgn(diff) * 360.0f;
		}

		rotor.render_angle = MoveTowards(render_angle, target_angle + offset_angle, 1.f);

		if (IsNull(render_angle - (target_angle + offset_angle))) {
			rotor.dir = rotor.target_dir;
			rotor.render_angle = rotor.dir * -60.0f;
			//rotor.target_dir = (Hex_Dir)((rotor.target_dir + 1) % 6);
			if (rotor.target_dir)
				rotor.target_dir = (Hex_Dir)(rotor.target_dir - 1);
			else
				rotor.target_dir = (Hex_Dir)5;
		}
	}

	for (Entity *entity : entities) {
		if (entity == &force_field) continue;

		Vec3i force_pos = force_field.position;
		for (ptrdiff_t i = 0; i < force_field.direction.count; ++i) {
			Vec3i neighbor = HexNeighbor(force_pos, force_field.direction[i]);

			if (entity->position == force_pos) {
				if (entity->target_positions.count) {
					if (Last(entity->target_positions) != neighbor)
						Append(&entity->target_positions, neighbor);
				}
				else {
					Append(&entity->target_positions, neighbor);
				}

				break;
			}

			force_pos = neighbor;
		}
	}

	//R_DrawText(renderer, Vec2(0.0f, height - 50.0f), Vec4(1), HexDirNames[direction]);
	R_DrawText(renderer, Vec2(0.0f, height - 50.0f), Vec4(1), TmpFormat("%", hex));
	R_DrawText(renderer, Vec2(0.0f, height - 75.0f), Vec4(1), TmpFormat("Dir %", HexDirNames[rotor.dir]));
	R_DrawText(renderer, Vec2(0.0f, height - 100.0f), Vec4(1), TmpFormat("Target %", HexDirNames[rotor.target_dir]));
	R_DrawText(renderer, Vec2(0.0f, height - 125.0f), Vec4(1), TmpFormat("Angle %", rotor.render_angle));

	Vec2 origin = Vec2(0.0f);

	for (int y = -50; y < 50; ++y) {
		for (int x = -50; x < 50; ++x) {
			Hex_Tile *tile = HexFindTile(map, Hex(x, y));
			if (tile) {
				DrawHexagon(renderer, tile->pos);
			}
		}
	}

	// todo: use render position
	R_PushTexture(renderer, GetResource(arrow_head));
	Vec3i current_pos = force_field.position;
	for (ptrdiff_t i = 0; i < force_field.direction.count; ++i) {
		Vec2 p = HexToPixel(current_pos, Vec2(0), Vec2(HexRadius), HexKindCurrent);
		float angle = force_field.direction[i] * -60.0f;
		R_DrawRectCenteredRotated(renderer, p, Vec2(0.5f), DegToRad(angle), Vec4(1, 1, 0, 1));
		current_pos = HexNeighbor(current_pos, force_field.direction[i]);
	}
	R_PopTexture(renderer);

	R_SetLineThickness(renderer, 5.0f * cam_height / height);

	for (auto actor : actors) {
		Vec2 pos = HexToPixel(actor.render_position, Vec2(0), Vec2(HexRadius), HexKindCurrent);
		R_DrawCircleOutline(renderer, pos, 0.40f, Vec4(1, 0, 0, 1));
	}

	Vec2 pos = HexToPixel(rotor.render_position, Vec2(0), Vec2(HexRadius), HexKindCurrent);
	R_DrawCircleOutline(renderer, pos, 0.40f, Vec4(0, 1, 1, 1));

	R_SetLineThickness(renderer, 3.0f * cam_height / height);

	Mat4 transform = Translation(Vec3(pos, 0)) * RotationZ(DegToRad(rotor.render_angle)) * Translation(Vec3(-pos, 0));
	R_PushTransform(renderer, transform);

	Vec3 render_pos = rotor.render_position;
	for (int i = 0; i < rotor.length - 1; ++i) {
		render_pos = HexNeighbor(render_pos, HEX_DIR_R);
		DrawHexagon(renderer, render_pos, Vec4(0, 1, 1, 1));
	}

	R_PopTransform(renderer);

	DrawHexagon(renderer, hex, Vec4(1, 0, 1, 1));
#endif

}