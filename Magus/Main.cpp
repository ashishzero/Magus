#include "Kr/KrMedia.h"
#include "Kr/KrMath.h"
#include "Kr/KrArray.h"
#include "Kr/KrString.h"
#include "Kr/KrLog.h"
#include "Kr/KrMap.h"
#include "Kr/KrMathFormat.h"

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

	R_Device *                   device;

	M_Arena *                    temp_arena;
	M_Allocator                  allocator;
};

static Resource_Manager Manager;

template <typename T>
struct Resource_Handle {
	uint32_t index = 0;

	Resource_Handle() {}
	Resource_Handle(uint32_t i): index(i) {}
	Resource_Handle(nullptr_t):  index(0) {}
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
	file.path      = path;
	file.data      = content;
	file.index     = 0;
	file.cp_ranges = codepoint_ranges;

	R_Font_Config config;
	config.files       = Array_View<R_Font_File>(&file, 1);
	config.replacement = '?';
	config.texture     = R_FONT_TEXTURE_RGBA;

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
	if (!font ) return;

	uint32_t index = font.index - 1;
	Assert(Manager.fonts[index].reference);

	uint32_t val = PL_InterlockedDecrement(&Manager.fonts[index].reference);

	if (val == 0) {
		ReleaseFont(Manager.fonts[index].data);
		Manager.fonts[index].data = nullptr;

		Span span   = Manager.fonts[index].source;
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

		Span span   = Manager.textures[index].source;
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

		Span span   = Manager.pipelines[index].source;
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

static const Vec3i HexDirectionValues[]  = {
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
	Vec2 pixel  = HexTransforms[kind] * hex;
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
	int32_t q    = (int32_t)(roundf(h.x));
	int32_t r    = (int32_t)(roundf(h.y));
	int32_t s    = (int32_t)(roundf(h.z));
	float q_diff = Absolute((float)q - h.x);
	float r_diff = Absolute((float)r - h.y);
	float s_diff = Absolute((float)s - h.z);

	if (q_diff > r_diff && q_diff > s_diff) {
		q = -r - s;
	} else if (r_diff > s_diff) {
		r = -q - s;
	} else {
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
		corners[i]  = center + offset;
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
const     float    HexRadius      = 0.5f;

struct Force_Field {
	Vec3i pos;

	Array<Hex_Dir> direction;
};

struct Actor {
	Vec3         render_pos;
	Vec3i        pos;
	Array<Vec3i> target_pos;
};

struct Hex_Tile {
	Vec3i   pos;

	bool has_dir = false;
	Hex_Dir dir;

	Actor *actor = nullptr;
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

		ptrdiff_t index  = map->index[y][x];
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
		} else {
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
		} else {
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
	Priority_Queue(M_Allocator allocator): min_heap(allocator) {}
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

	M_Temporary temp      = M_BeginTemporaryMemory(arena);
	M_Allocator allocator = M_GetArenaAllocator(arena);

	Priority_Queue queue = Priority_Queue<Vec3i>(allocator);
	Map reverse_paths    = Map<Vec3i, Vec3i>(allocator);
	Map path_cost        = Map<Vec3i, int>(allocator);

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

int Main(int argc, char **argv) {
	PL_ThreadCharacteristics(PL_THREAD_GAMES);

	PL_Window *window = PL_CreateWindow("Magus", 0, 0, false);
	if (!window)
		FatalError("Failed to create windows");

	R_Device *device = R_CreateDevice(R_DEVICE_DEBUG_ENABLE);
	R_Queue *queue   = R_CreateRenderQueue(device);

	R_Swap_Chain *swap_chain = R_CreateSwapChain(device, window);

	R_List *render_list    = R_CreateRenderList(device);
	R_Renderer2d *renderer = R_CreateRenderer2dFromDevice(device);

	Manager.device     = device;
	Manager.temp_arena = M_ArenaAllocate(GigaBytes(1));
	Manager.allocator  = M_GetDefaultHeapAllocator();

	H_Pipeline pipeline  = LoadPipeline("Shaders/HLSL/Quad.shader");
	H_Texture arrow_head = LoadTexture("ArrowHead.png");

	float width, height;
	R_GetRenderTargetSize(swap_chain, &width, &height);

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

	force_field.pos = Hex(0, 3, -3);
	Append(&force_field.direction, HEX_DIR_TR);
	Append(&force_field.direction, HEX_DIR_TR);
	Append(&force_field.direction, HEX_DIR_R);
	Append(&force_field.direction, HEX_DIR_R);
	Append(&force_field.direction, HEX_DIR_BL);
	Append(&force_field.direction, HEX_DIR_BL);
	Append(&force_field.direction, HEX_DIR_L);
	Append(&force_field.direction, HEX_DIR_L);

	Actor actor;
	actor.pos = Vec3i(0, 3, -3);
	actor.render_pos = Vec3(actor.pos.x, actor.pos.y, actor.pos.z);

	// fill the tiles with the information

	Hex_Tile *tile = HexFindOrDefaultTile(map, actor.pos);
	tile->actor = &actor;

	{
		Vec3i p = force_field.pos;
		for (ptrdiff_t i = 0; i < force_field.direction.count; ++i) {
			tile = HexFindOrDefaultTile(map, p);
			tile->has_dir = true;
			tile->dir = force_field.direction[i];
			p = HexNeighbor(p, force_field.direction[i]);
		}
	}

#if 0
	Hex_Tile *tile;
	tile = HexFindOrDefaultTile(map, Hex(0, 3, -3));
	tile->is_force = true;
	tile->force_dir = HEX_DIR_TR;

	tile = HexFindOrDefaultTile(map, Hex(0, 4, -4));
	tile->is_force = true;
	tile->force_dir = HEX_DIR_TR;

	tile = HexFindOrDefaultTile(map, Hex(0, 5, -5));
	tile->is_force = true;
	tile->force_dir = HEX_DIR_R;

	tile = HexFindOrDefaultTile(map, Hex(1, 5, -6));
	tile->is_force = true;
	tile->force_dir = HEX_DIR_R;

	tile = HexFindOrDefaultTile(map, Hex(2, 5, -7));
	tile->is_force = true;
	tile->force_dir = HEX_DIR_BL;

	tile = HexFindOrDefaultTile(map, Hex(2, 4, -6));
	tile->is_force = true;
	tile->force_dir = HEX_DIR_BL;

	tile = HexFindOrDefaultTile(map, Hex(2, 3, -5));
	tile->is_force = true;
	tile->force_dir = HEX_DIR_L;

	tile = HexFindOrDefaultTile(map, Hex(1, 3, -4));
	tile->is_force = true;
	tile->force_dir = HEX_DIR_L;
#endif


	//int direction = 0;

	//Vec3 pos = Vec3(0, 0, 0);
	//Array<Vec3i> target_pos;

	Vec2 cursor_pos = Vec2(0);
	bool move_pressed = false;

	uint64_t counter = PL_GetPerformanceCounter();
	float frequency  = PL_GetPerformanceFrequency();
	float frame_time = 0.0f;

	bool running = true;

	while (running) {
		Array_View<PL_Event> events = PL_PollEvents();

		move_pressed = false;

		for (const PL_Event &e : events) {
			if (e.kind == PL_EVENT_QUIT || e.kind == PL_EVENT_CLOSE) {
				PL_DestroyWindow(window);
				running = false;
				break;
			}

			if (e.kind == PL_EVENT_CURSOR) {
				cursor_pos.x = (float)e.cursor.x;
				cursor_pos.y = (float)e.cursor.y;
			}

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
				} else if (e.button.id == PL_BUTTON_RIGHT) {
					HexToggleTileLife(map, hex);
				}
			}

			//if (e.kind == PL_EVENT_KEY_PRESSED) {
			//	if (e.key.id == PL_KEY_SPACE && !e.key.repeat)
			//		direction = (direction + 1) % ArrayCount(HexDirNames);
			//}

			if (e.kind == PL_EVENT_RESIZE) {
				R_Flush(queue);
				R_ResizeRenderTargets(device, swap_chain, e.resize.w, e.resize.h);
			}
		}

		R_GetRenderTargetSize(swap_chain, &width, &height);

		float cam_height = 10.0f;
		float aspect_ratio = width / height;

		float cam_width = aspect_ratio * cam_height;

		Vec2 cursor_cam_pos = MapRange(Vec2(0), Vec2(width, height),
			-0.5f * Vec2(cam_width, cam_height), 0.5f * Vec2(cam_width, cam_height),
			cursor_pos);

		Vec3 hex_cursor = PixelToHex(cursor_cam_pos, Vec2(0), Vec2(HexRadius), HexKindCurrent);

		Vec3i hex = HexRound(hex_cursor);

		//auto path = FindPath(map, HexRound(pos), hex);
		//Defer{ Free(&path); };

		//if (move_pressed) {
		//	if (HexFindTile(map, hex)) {
		//		Reset(&target_pos);
		//		Append(&target_pos, path);
		//	}
		//}

		if (actor.target_pos.count) {
			Vec3i t_posi = First(actor.target_pos);
			Vec3 t_pos   = Vec3((float)t_posi.x, (float)t_posi.y, (float)t_posi.z);

			actor.render_pos = MoveTowards(actor.render_pos, t_pos, 0.1f);

			if (IsNull(actor.render_pos - t_pos)) {
				actor.render_pos = t_pos;
				actor.pos        = t_posi;
				Remove(&actor.target_pos, 0);
			}
		}

		Hex_Tile *actor_tile_info = HexFindTile(map, actor.pos);

		if (actor_tile_info) {
			if (actor_tile_info->has_dir) {
				Vec3i neighbor = HexNeighbor(actor_tile_info->pos, actor_tile_info->dir);
				if (actor.target_pos.count) {
					if (Last(actor.target_pos) != neighbor)
						Append(&actor.target_pos, neighbor);
				} else {
					Append(&actor.target_pos, neighbor);
				}
			}
		}

#if 0
		static int update_counter = 0;

		if (update_counter % 100 == 0) {
			for (int y = -50; y < 50; ++y) {
				for (int x = -50; x < 50; ++x) {
					Hex_Tile *tile = HexFindTile(map, Hex(x, y));
					if (!tile) continue;

					if (tile->has_dir && tile->actor) {
						Vec3i next_pos = HexNeighbor(tile->pos, tile->dir);
						Hex_Tile *new_tile = HexFindTile(map, next_pos);

						new_tile->actor = tile->actor;
						tile->actor = nullptr;

						Append(&new_tile->actor->target_pos, new_tile->pos);
					}
				}
			}
		}
		update_counter += 1;
#endif

		R_NextFrame(renderer, R_Rect(0.0f, 0.0f, width, height));
		R_SetPipeline(renderer, GetResource(pipeline));

		R_CameraView(renderer, 0.0f, width, 0.0f, height, -1.0f, 1.0f);

		String text = TmpFormat("%ms % FPS", frame_time, (int)(1000.0f / frame_time));
		R_DrawText(renderer, Vec2(0.0f, height - 25.0f), Vec4(1), text);
		//R_DrawText(renderer, Vec2(0.0f, height - 50.0f), Vec4(1), HexDirNames[direction]);
		R_DrawText(renderer, Vec2(0.0f, height - 50.0f), Vec4(1), TmpFormat("%", hex));
		R_DrawText(renderer, Vec2(0.0f, height - 75.0f), Vec4(1), TmpFormat("%", actor.target_pos.count));

		/////////////////////////////////////////////////////////////////////////////

		R_CameraView(renderer, aspect_ratio, cam_height);
		R_SetLineThickness(renderer, 2.0f * cam_height / height);

		Vec2 origin = Vec2(0.0f);

		for (int y = -50; y < 50; ++y) {
			for (int x = -50; x < 50; ++x) {
				Hex_Tile *tile = HexFindTile(map, Hex(x, y));
				if (tile) {
					DrawHexagon(renderer, tile->pos);
				}
			}
		}

		R_PushTexture(renderer, GetResource(arrow_head));
		for (int y = -50; y < 50; ++y) {
			for (int x = -50; x < 50; ++x) {
				Hex_Tile *tile = HexFindTile(map, Hex(x, y));
				if (!tile) continue;

				if (tile->has_dir) {
					float angle = DegToRad(tile->dir * -60.0f);
					Vec2 fpos   = HexToPixel(Hex(x, y), Vec2(0), Vec2(HexRadius), HexKindCurrent);
					R_DrawRectCenteredRotated(renderer, fpos, Vec2(0.5f), angle, Vec4(1, 1, 0, 1));
				}
			}
		}
		/*
		Vec3i current_pos = force_field.pos;
		for (ptrdiff_t i = 0; i < force_field.direction.count; ++i) {
			Vec2 p = HexToPixel(current_pos, Vec2(0), Vec2(HexRadius), HexKindCurrent);
			float angle = force_field.direction[i] * -60.0f;
			R_DrawRectCenteredRotated(renderer, p, Vec2(0.5f), DegToRad(angle), Vec4(1, 1, 0, 1));
			current_pos = HexNeighbor(current_pos, force_field.direction[i]);
		}
		*/
		R_PopTexture(renderer);


		/*for (auto p : target_pos) {
			DrawHexagon(renderer, p, Vec4(1, 1, 0, 1));
		}

		if (HexFindTile(map, hex))
			DrawHexagon(renderer, hex, Vec4(0, 1, 1, 1));
		else
			DrawHexagon(renderer, hex, Vec4(1, 1, 0, 1));

		Vec2 hex_pos = HexToPixel(pos, Vec2(0), Vec2(HexRadius), HexKindCurrent);
		R_DrawCircle(renderer, hex_pos, 0.45f, Vec4(1));*/

		R_SetLineThickness(renderer, 5.0f * cam_height / height);

		Vec2 pos = HexToPixel(actor.render_pos, Vec2(0), Vec2(HexRadius), HexKindCurrent);

		R_DrawCircleOutline(renderer, pos, 0.40f, Vec4(1, 0, 0, 1));

		//for (Vec3i pos : path) {
		//	Vec2 ppos = HexToPixel(pos, Vec2(0), Vec2(HexRadius), HexKindCurrent);
		//	R_PathTo(renderer, ppos);
		//}
		//R_PathTo(renderer, HexToPixel(pos, Vec2(0), Vec2(HexRadius), HexKindCurrent));
		//R_DrawPathStroked(renderer, Vec4(0, 1, 1, 1));

		//hex = HexNeighbor(Vec3i(0), direction);
		//DrawHexagon(renderer, hex, Vec4(1, 0, 0, 1));

		//float angle = DegToRad(-direction * 60.0f);

		//Vec2 fpos = HexToPixel(hex, Vec2(0), Vec2(HexRadius), HexKindCurrent);

		//R_PushTexture(renderer, GetResource(arrow_head));
		//R_DrawRectCenteredRotated(renderer, fpos, Vec2(0.5f), angle, Vec4(1, 1, 0, 1));
		//R_PopTexture(renderer);

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
		uint64_t counts = current - counter;
		counter = current;

		frame_time = 1000.0f * (float)counts / frequency;
	}

	R_Flush(queue);
	ReleaseAll();

	R_DestroyRenderList(render_list);
	R_DestroySwapChain(device, swap_chain);
	R_DestroyRenderQueue(queue);
	R_DestroyDevice(device);

	return 0;
}
