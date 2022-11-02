#pragma once

#include "Kr/KrMemory.h"

#define STBTT_malloc(x,u)  ((void)(u),M_Alloc(x))
#define STBTT_free(x,u)    ((void)(u),M_Free(x, 0))
#define STBTT_assert(x)    Assert(x)

#include "External/stb_truetype.h"
