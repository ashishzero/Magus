#pragma once
#include "Kr/KrPrelude.h"

struct R_Device;
struct R_Pipeline;

R_Pipeline *Resource_LoadPipeline(M_Arena *arena, R_Device *device, String content, String path);