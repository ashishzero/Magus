#include "Kr/KrMemory.h"
#include "Kr/KrLog.h"

#include "RenderBackend.h"
#include "Image.h"

R_Texture *LoadTexture(M_Arena *arena, R_Device *device, const String content, const String path) {
	M_Allocator allocator   = ThreadContext.allocator;
	ThreadContext.allocator = M_GetArenaAllocator(arena);

	stbi_set_flip_vertically_on_load(1);

	int x, y, n;
	uint8_t *pixels = stbi_load_from_memory(content.data, (int)content.count, &x, &y, &n, 4);

	ThreadContext.allocator = allocator;

	if (pixels) {
		R_Texture *texture = R_CreateTexture(device, R_FORMAT_RGBA8_UNORM, x, y, x * 4, pixels, 0);
		return texture;
	}

	LogError("Resource Texture: Failed to load texture: %. Reason: %", path, stbi_failure_reason());

	return nullptr;
}

void ReleaseTexture(R_Texture *texture) {
	R_DestroyTexture(texture);
}
