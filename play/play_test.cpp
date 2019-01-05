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


void tone_generator_audio_callback(P_AudioBuffer *buffer) {
	int16_t *sample = buffer->samples;
	int16_t *end_sample = sample + buffer->samples_count;
	while (sample < end_sample) {
		float seconds_per_sample = 1 / (float)buffer->format.samples_per_second;
		float seconds = (float)sample_index  * seconds_per_sample;
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

P_AudioBuffer music;
uint32_t music_sample_index;


void music_audio_callback(P_AudioBuffer *buffer) {
	int16_t *sample = buffer->samples;
	int16_t *end_sample = sample + buffer->samples_count;
	float seconds_per_sample = 1 / (float)buffer->format.samples_per_second;
	while (sample < end_sample) {
		*sample = music.samples[music_sample_index];
		sample++;
		music_sample_index++;
		if (music_sample_index == music.samples_count) {
			music_sample_index = 0;
		}
	}
}

int CALLBACK WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	
	P_Image image;
	P_Bool load_image_result = p_load_image("c:\\dev\\i4t_play\\lena.png", &image);
	P_Bool load_audio_result = p_load_audio("c:\\dev\\i4t_play\\Master.wav", &music);

	sample_index = 0;
	frequency = 500.0f;
	amplitude = 0.1f;
	wobble_amplitude = 50.0f;
	wobble_frequency = 2.0f;
	//p.audio.callback = (P_AudioCallback)tone_generator_audio_callback; //play sound
	p.audio.callback = (P_AudioCallback)music_audio_callback;
	
	p_initialize(&p);
	
	glEnable(GL_TEXTURE_2D);
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels);
	
	
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
		glTexCoord2f(0.0f, 0.0f);
		glVertex2f(-1.0f, -1.0f);
		glTexCoord2f(1.0f, 0.0f);
		glVertex2f(1.0f, -1.0f);
		glTexCoord2f(1.0f, 1.0f);
		glVertex2f(1.0f, 1.0f);
		glTexCoord2f(0.0f, 1.0f);
		glVertex2f(-1.0f, 1.0f);
		glEnd();
		p_push(&p);
	}
}