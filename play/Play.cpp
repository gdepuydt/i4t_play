#include <windows.h>
#include <stdint.h>
#include <stdio.h>

enum {
	PLAY_MAX_KEYS = 256,
	PLAY_MAX_TEXT = 256,
	PLAY_MAX_SAMPLES = 256
};

typedef uint8_t P_Bool;

struct P_Int2 {
	int x;
	int y;
};

struct P_Float2 {
	float x;
	float y;
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

struct P_Axis {
	float value;  //-1.0 to 1.0
	float threshold; //deadzone threshold, (abs)value <= threshold gets clamped to 0
};

struct P_Stick {
	P_Axis x;
	P_Axis y;
};

struct P_Gamepad {
	P_DigitalButton a_button;
	P_DigitalButton b_button;
	P_DigitalButton x_button;
	P_DigitalButton y_button;
	P_DigitalButton left_bumper_button;
	P_DigitalButton right_bumper_button;
	P_DigitalButton up_button;
	P_DigitalButton down_button;
	P_AnalogButton left_trigger;
	P_AnalogButton right_trigger;
	P_Stick left_thumb_stick;
	P_Stick right_thumb_stick;
	P_DigitalButton left_thumb_button;
	P_DigitalButton right_thumb_button;
	P_DigitalButton select_button;
	P_DigitalButton play_button;
};

struct P_Mouse {
	P_DigitalButton left_button;
	P_DigitalButton right_button;
	P_Int2 delta_position;
	P_Int2 position; //window relative
	P_Int2 screen_position; //
	int wheel;
	int wheel_delta;
};

struct Play {
	//Window
	char *name;
	P_Int2 pos;
	P_Int2 size;

	//Keyboard
	P_DigitalButton keys[PLAY_MAX_KEYS];
	
	//Gamepad
	P_Gamepad gamepad;

	//Mouse
	P_Mouse mouse;
	
	//Time
	float delta_seconds;
	uint64_t delta_nano_seconds;
	uint64_t delta_micro_seconds;
	uint64_t delta_milli_seconds;
	uint64_t delta_samples; //how many audio samples we need to calculate this frame

	double time_seconds; //53 bits of integer precision
	uint64_t time_nanoseconds;
	uint64_t time_microseconds;
	uint64_t time_milliseconds;

	//Sound
	uint32_t samples_per_second; //e.g. 44.4k
	int16_t *samples; //points to beginning of sample_buffer after update
	int16_t sample_buffer[PLAY_MAX_SAMPLES];

	//Text
	char *text; //0 if no new text, stored in utf8
	char text_buffer[PLAY_MAX_TEXT];

	//State
	P_Bool quit;
};

Play p;

int16_t generate_sample() {
	return 0;
}

void play_initialize(Play *play) {
	return;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	return 0;
}