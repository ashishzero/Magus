#pragma once

#include "Kr/KrMemory.h"

#define STBI_ASSERT(x)            Assert(x)
#define STBI_MALLOC(sz)           M_Alloc(sz)
#define STBI_REALLOC(p,newsz)     M_Realloc(p,0,newsz)
#define STBI_FREE(p)              M_Free(p,0)
#include "External/stb_image.h"
