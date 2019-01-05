#pragma once

#ifdef __cplusplus
#define P_EXTERN_BEGIN extern "C" {
#define P_EXTERN_END }
#else
#define P_EXTERN_BEGIN
#define P_EXTERN_END
#endif

#include <stdint.h>


P_EXTERN_BEGIN

enum {
	P_FALSE = 0,
	P_TRUE = 1,
	P_MAX_KEYS = 256,
	P_MAX_TEXT = 256,
	P_MAX_ERROR = 1024,
	P_CTRL = 0x11,
	P_ALT = 0x12,
	P_SHIFT = 0x10,
	P_MAX_AUDIO_BUFFER = 2 * 1024,
};

typedef uint8_t P_Bool;
typedef int16_t P_SoundSample;

struct P_Int2 {
	int x;
	int y;
};

struct P_DigitalButton {
	P_Bool down;
	P_Bool pressed; // !down -> down
	P_Bool released; //down -> !down
};

struct P_AnalogButton {
	float threshold; //defaults to 0.5
	float value; //0.0 to 1.0
	P_Bool down; //value <= threshold
	P_Bool pressed; // !down -> down
	P_Bool released; //down -> !down
};

struct P_Stick {
	float threshold;
	float x;
	float y;
};

struct P_Gamepad {
	P_Bool connected;
	P_DigitalButton a_button;
	P_DigitalButton b_button;
	P_DigitalButton x_button;
	P_DigitalButton y_button;
	P_DigitalButton left_shoulder_button;
	P_DigitalButton right_shoulder_button;
	P_DigitalButton up_button;
	P_DigitalButton down_button;
	P_DigitalButton left_button;
	P_DigitalButton right_button;
	P_AnalogButton left_trigger;
	P_AnalogButton right_trigger;
	P_Stick left_thumb_stick;
	P_Stick right_thumb_stick;
	P_DigitalButton left_thumb_button;
	P_DigitalButton right_thumb_button;
	P_DigitalButton back_button;
	P_DigitalButton start_button;
};

struct P_Mouse {
	P_DigitalButton left_button;
	P_DigitalButton right_button;
	P_Int2 delta_position;
	P_Int2 position; //client window relative
	int wheel;
	int delta_wheel;
};

struct P_Window {
	const char *title;
	P_Int2 pos;
	P_Int2 size;
	P_Bool resized;
};

struct P_AudioFormat {
	uint32_t samples_per_second;
	uint32_t channels;
	uint32_t bytes_per_sample;
};

struct P_AudioBuffer {
	int16_t *samples;
	size_t samples_count;
	P_AudioFormat format;
};

typedef void(*P_AudioCallback)(P_AudioBuffer *buffer);

struct P_Audio {
	P_AudioFormat format;
	P_AudioCallback callback;
};

struct P_Time {
	float delta_seconds;
	uint64_t delta_ticks;
	uint64_t delta_nanoseconds;
	uint64_t delta_microseconds;
	uint64_t delta_milliseconds;
	uint64_t delta_sound_samples; 

	double seconds; 
	uint64_t ticks;
	uint64_t nanoseconds;
	uint64_t microseconds;
	uint64_t milliseconds;

	uint64_t initial_ticks;
	uint64_t ticks_per_second;
};


typedef void *HANDLE;
typedef struct _XINPUT_STATE XINPUT_STATE;
typedef unsigned long(__stdcall *XINPUTGETSTATE)(unsigned long dwUserIndex, XINPUT_STATE* pState);

struct IAudioClient;
struct IAudioRenderClient;

struct P_Win32 {
	HANDLE window;
	HANDLE device_context;

	void *main_fiber;
	void *message_fiber;

	XINPUTGETSTATE xinput_get_state;

	IAudioClient *audio_client;
	IAudioRenderClient *audio_render_client;

	HANDLE wgl_context;
};



struct Play {

	P_Bool initialized;
	P_Bool quit;
	
	const char *error; 
	char *error_buffer[P_MAX_ERROR];
		
	P_Window window;
  	P_DigitalButton keys[P_MAX_KEYS]; 
	P_Gamepad gamepad;
	P_Mouse mouse;
	P_Time time;
	P_Audio audio;
	P_Win32 win32;
	char text[P_MAX_TEXT];
	size_t text_length;
};

P_Bool p_initialize(Play *p);
P_Bool p_pull(Play *p);
void p_push(Play *p);

struct P_Image {
	uint8_t *pixels;
	uint32_t channels;
	uint32_t width;
	uint32_t height;
};
P_Bool p_load_image(const char *filename, P_Image *image);
P_Bool p_load_audio(const char *filename, P_AudioBuffer *audio);


void debug_out(const char *format, ...);

P_EXTERN_END