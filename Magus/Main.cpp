#include "Kr/KrPrelude.h"
#include "Kr/KrMedia.h"
#include "Kr/KrMath.h"
#include "Kr/KrArray.h"

#include "RenderBackend.h"

int Main(int argc, char **argv) {
	PL_Init();

	PL_ThreadCharacteristics(PL_THREAD_GAMES);
	
	PL_Window *window = PL_CreateWindow("Magus", 0, 0, false);
	if (!window)
		FatalError("Failed to create windows");

	R_Device *device = R_CreateDevice(R_DEVICE_DEBUG_ENABLE);

	R_Command_Queue *command_queue = R_CreateCommandQueue(device);

	R_Swap_Chain *swap_chain = R_CreateSwapChain(device, window);

	R_Command_Buffer *command_buffer = R_CreateCommandBuffer(device);

	bool running = true;

	while (running) {
		Array_View<PL_Event> events = PL_PollEvents();

		for (const PL_Event &e : events) {
			if (e.kind == PL_EVENT_QUIT || e.kind == PL_EVENT_CLOSE) {
				PL_DestroyWindow(window);
				running = false;
				break;
			}
		}

		R_Render_Target *render_target = R_GetSwapChainRenderTarget(swap_chain);

		float clear_color[] = { .12f, .12f, .12f, 1.0f };
		R_ClearRenderTarget(command_buffer, render_target, clear_color);

		R_Submit(command_queue, command_buffer);

		R_Present(swap_chain);

		ThreadResetScratchpad();
	}

	R_DestroyCommandBuffer(command_buffer);
	R_DestroyCommandQueue(command_queue);

	R_DestroySwapChain(device, swap_chain);
	R_DestroyDevice(device);

	return 0;
}
