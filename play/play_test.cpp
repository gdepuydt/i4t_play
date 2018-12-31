#include <math.h>
#include <windows.h>
#include <gl/gl.h>
#include "Play.h"

/*extern "C" {
	int _fltused;
}*/

static Play p;

uint32_t sample_index;
float frequency = 500;
float gain = 0.5f;
float amplitude = 1.0f;
float wobble_amplitude;
float wobble_frequency;

bool fade_out = P_TRUE;
bool fade_in = P_TRUE;

float fade_step = 0.00001f;

float lerp(float x, float y, float t) {
	return (1 - t)*x + t * y;
}


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

int CALLBACK WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	sample_index = 0;
	frequency = 500.0f;
	amplitude = 0.1f;
	wobble_amplitude = 50.0f;
	wobble_frequency = 2.0f;
	//p.audio.callback = (P_AudioCallback)audio_callback; //play sound
	
	p_initialize(&p);

	while (p_pull(&p)) {
		if (p.mouse.left_button.pressed) {
			debug_out("LMB pressed: %d; %d\n", p.mouse.position.x, p.mouse.position.y);
			frequency = (float)p.mouse.position.x;
			if (frequency < 0.0f) {
				frequency = 0.0f;
			}
			amplitude = (float)p.mouse.position.y / (float)p.window.size.y;
			if (amplitude <= 0.0f) {
				amplitude = 0.0f;
			}
			if (amplitude >= 0.0f) {
				amplitude = 1.0f;
			}
		}
		glViewport(0, 0, p.window.size.x, p.window.size.y);
		glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glBegin(GL_QUADS);
		glColor3f(0.0f, 0.0f, 1.0f);
		glVertex2f(-0.5f, -0.5f);
		glVertex2f(0.5f, -0.5f);
		glVertex2f(0.5f, 0.5f);
		glVertex2f(-0.5f, 0.5f);
		glEnd();
		p_push(&p);
	}
}