#pragma once
#include "Kr/KrPlatform.h"

struct PL_Window;

struct R_Device;
struct R_Swap_Chain;
struct R_Render_Target;

enum R_Device_Flags {
	R_DEVICE_DEBUG_ENABLE = 0x1
};

R_Device *       R_CreateDevice(uint32_t device_flags);
void             R_DestroyDevice(R_Device *device);

R_Swap_Chain *   R_CreateSwapChain(R_Device *device, PL_Window *window);
void             R_DestroySwapChain(R_Device *device, R_Swap_Chain *swap_chain);

void             R_SetSyncInterval(R_Swap_Chain *swap_chain, uint32_t interval);
R_Render_Target *R_GetSwapChainRenderTarget(R_Swap_Chain *swap_chain);
void             R_GetSwapChainRenderTargetSize(R_Swap_Chain *swap_chain, uint32_t *w, uint32_t *h);
void             R_Present(R_Swap_Chain *swap_chain);

void             R_GetRenderTargetSize(R_Render_Target *render_target, uint32_t *w, uint32_t *h);
