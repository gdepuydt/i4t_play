#include <math.h>
#include "Play.h"

extern "C" {
	int _fltused;
}

static Play p;

float gain = 1.0f;
float amplitude = 1.0f;
float frequency= 500;
float wobble_amplitude;
float wobble_frequency;

bool fade_out;
bool fade_in;

float fade_step;

int sample_index;


void audio_callback(P_AudioRequest *request) {
	int16_t *sample = request->sample;
	while (sample < request->end_sample) {
		float seconds = (float)sample_index / (float)request->samples_per_second;
		*sample = (int16_t)((gain * amplitude * sin(frequency * seconds)) * INT16_MAX);
		sample_index++;
		sample++;
		if (fade_out) {
			if (gain > 0.0f) {
				gain -= fade_step;
			}
			if (gain <= 0.0f) {
				gain = 0.0f;
				fade_out = false;
			}
		}
		if (fade_in) {
			if (gain < 1.0f) {
				gain += fade_step;
			}
			if (gain > 1.0f) {
				gain = 1.0f;
				fade_in = false;
			}
		}
	}
}

void WinMain() {
	sample_index = 0;
	frequency = 500.0f;
	amplitude = 0.1f;
	wobble_amplitude = 50.0f;
	wobble_frequency = 2.0f;
	p.audio.callback = (P_AudioCallback)audio_callback;
	
	p_initialize(&p);

	while (p_pull(&p)) {

		static float last_print_time = 0.0;
		if ((p.time.seconds - last_print_time) > 1.0f) {
			debug_out("x=%d, y=%d, dx=%d, dy=%d\n", p.window.pos.x, p.window.pos.y, p.window.size.x, p.window.size.y);
			debug_out("delta_ticks = %llu, time_ticks = %llu\n", p.time.delta_ticks, p.time.ticks);
			last_print_time = p.time.seconds;
		}
		/*if (p.window.resized) {
			debug_out("Window resized: %d, %d\n", p.window.size.x, p.window.size.y);
		}*/
	}
}