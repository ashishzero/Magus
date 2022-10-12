#include "Kr/KrPrelude.h"
#include "Kr/KrMedia.h"

#include "Render2d.h"
#include "Render2dImpl.h"

int Main(int argc, char **argv) {
	PL_Init();
	
	PL_Window *window = PL_CreateWindow("Magus", 0, 0, false);
	if (!window)
		FatalError("Failed to create windows");

	R_Renderer2d *renderer = R_CreateRenderer2d(new R_Backend2d);

	bool running = true;

	while (running) {
		Array_View<PL_Event> events = PL_PollEvents();

		for (const PL_Event &e : events) {
			if (e.kind == PL_EVENT_QUIT || e.kind == PL_EVENT_CLOSE) {
				PL_DestroyWindow(window);
				running = false;
				break;
			}

			if (e.kind == PL_EVENT_KEY_PRESSED && e.key.id == PL_KEY_ESCAPE) {
				PL_PostQuitMessage();
			}
		}

		ThreadResetScratchpad();
	}

	return 0;
}
