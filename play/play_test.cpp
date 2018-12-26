#include "Play.h"

extern "C" {
	int _fltused;
}

static Play p;


void WinMain() {
	p_initialize(&p);

	while (p_pull(&p)) {

		static float last_print_time = 0.0;
		if ((p.time.seconds - last_print_time) > 1.0f) {
			debug_out("x=%d, y=%d, dx=%d, dy=%d\n", p.window.pos.x, p.window.pos.y, p.window.size.x, p.window.size.y);
			debug_out("delta_ticks = %llu, time_ticks = %llu\n", p.time.delta_ticks, p.time.ticks);
			last_print_time = p.time.seconds;
		}
		if (p.window.resized) {
			debug_out("Window resized: %d, %d\n", p.window.size.x, p.window.size.y);
		}
	}
}