#pragma once
#include "Render2d.h"

struct R_Device;

R_Backend2d * R_CreateBackend2d(R_Device *device);
R_Renderer2d *R_CreateRenderer2dFromDevice(R_Device *device, const R_Specification2d &spec = Renderer2dDefaultSpec);
