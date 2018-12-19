#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define assert(x) \
	if(!(x)) { MessageBoxA(0, #x, "Assertion Failure", MB_OK); __debugbreak();}

enum {
	P_MAX_KEYS = 256,
	P_MAX_TEXT = 256,
	P_MAX_ERROR = 1024,
	P_MAX_SOUND_SAMPLES = 1024 * 1024,
	P_TRUE = 1,
	P_FALSE = 0,
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

struct P_Win32 {
	HWND window;
	void *main_fiber;
	void *message_fiber;
};

struct Play {
	//Window
	const char *name;
	P_Int2 pos;
	P_Int2 size;

	//Error
	const char *error; //0 if no error
	char *error_buffer[P_MAX_ERROR];

	//Keyboard
	P_DigitalButton keys[P_MAX_KEYS];
	
	//Gamepad
	P_Gamepad gamepad;

	//Mouse
	P_Mouse mouse;
	
	//Time
	
	float delta_seconds;
	uint64_t delta_ticks;
	uint64_t delta_nano_seconds;
	uint64_t delta_micro_seconds;
	uint64_t delta_milli_seconds;
	uint64_t delta_samples; //how many audio samples we need to calculate this frame

	double time_seconds; //53 bits of integer precision
	uint64_t time_ticks;
	uint64_t time_nanoseconds;
	uint64_t time_microseconds;
	uint64_t time_milliseconds;

	uint64_t initial_ticks;
	uint64_t ticks_per_second;
	

	//Sound
	uint32_t sound_samples_per_second; //e.g. 44.4k
	int16_t *sound_samples; //points to beginning of sample_buffer after update
	int16_t sound_sample_buffer[P_MAX_SOUND_SAMPLES];

	//Text
	char *text; //0 if no new text, stored in utf8
	char text_buffer[P_MAX_TEXT];

	//State
	P_Bool quit;

	//Win32-specific stuff
	P_Win32 win32;
};

int16_t p_generate_sample() {
	// ...
	return 0;
}

static LRESULT CALLBACK p_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
	//pull in the user data, so we can access (and switch to) the main app fiber after timeout processing messages    
	Play *p = (Play *)GetWindowLongPtr(window, GWLP_USERDATA);

	LRESULT result = 0;
	switch (message) {
	case WM_DESTROY:
		p->quit = P_TRUE;
		break;
	case WM_TIMER:
		SwitchToFiber(p->win32.main_fiber);
		break;
	case WM_ENTERMENULOOP:
	case WM_ENTERSIZEMOVE:
		//Every millisecond a timer message is catched by the WM_TIMER message the program switched to the main fiber. 
		//this way we don't end up in  a recursive loop of unresponsiveness.
		SetTimer(window, 0, 1, 0);
		break;
	case WM_EXITMENULOOP:
	case WM_EXITSIZEMOVE:
		KillTimer(window, 0);
		break;

	default:
		result = DefWindowProcW(window, message, wparam, lparam);
	}
	return result;
}

//message handling happens in its own loop
static void CALLBACK p_message_fiber_proc(Play *p) {
	for (;;) {
		MSG message;
		if (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}
		SwitchToFiber(p->win32.main_fiber);
	}
}

static void p_pull_window(Play *p) {
	RECT window_rectangle;
	GetWindowRect(p->win32.window, &window_rectangle);

	//This is the actual canvas of the rectange
	RECT client_rectangle;
	GetClientRect(p->win32.window, &client_rectangle);

	p->pos.x = window_rectangle.left + client_rectangle.left;
	p->pos.y = window_rectangle.top + client_rectangle.top;
	p->size.x = client_rectangle.right - client_rectangle.left;
	p->size.y = client_rectangle.bottom - client_rectangle.top;
}

static void p_pull_time(Play *p) {
	LARGE_INTEGER large_integer;
	QueryPerformanceCounter(&large_integer);
	uint64_t current_ticks = large_integer.QuadPart;
	p->delta_ticks = current_ticks - p->time_ticks;
	p->time_ticks = current_ticks;

	p->delta_nano_seconds = 1000 * 1000 * 1000 * (p->delta_ticks / p->ticks_per_second);
	p->delta_micro_seconds = p->delta_nano_seconds / 1000;
	p->delta_milli_seconds = p->delta_micro_seconds / 1000;
	p->delta_seconds = (float)p->delta_ticks / (float)p->ticks_per_second;

}

void p_pull(Play *p) {
	SwitchToFiber(p->win32.message_fiber);
	p_pull_window(p);
	p_pull_time(p);
}

P_Bool p_initialize(Play *p) {
	
	if (!p->name) {
		p->name = "Play";
	}
	int window_x;
	if (p->pos.x) {
		window_x = p->pos.x;
	}
	else {
		window_x = CW_USEDEFAULT;
	}

	int window_y;
	if (p->pos.y) {
		window_y = p->pos.y;
	}
	else {
		window_y = CW_USEDEFAULT;
	}

	int window_width;
	if (p->size.x) {
		window_width = p->size.x;
	}
	else {
		window_width = CW_USEDEFAULT;
	}
	int window_height;
	if (p->size.y) {
		window_height = p->size.y;
	}
	else {
		window_height = CW_USEDEFAULT;
	}

	p->win32.main_fiber = ConvertThreadToFiber(0);
	assert(p->win32.main_fiber);
	p->win32.message_fiber = CreateFiber(0, (LPFIBER_START_ROUTINE)p_message_fiber_proc, p);
	assert(p->win32.message_fiber);

	if (window_height != CW_USEDEFAULT && window_width != CW_USEDEFAULT) {
		RECT window_rectangle;
		window_rectangle.left = 0;
		window_rectangle.right = window_width;
		window_rectangle.top = 0;
		window_rectangle.bottom = window_height;

		//adjust the window rectangle so that the client rectangle will have the xpected size 
		if (AdjustWindowRect(&window_rectangle, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0)) {
			window_width = window_rectangle.right - window_rectangle.left;
			window_height = window_rectangle.bottom - window_rectangle.top;
		}
	}

	//
	//Window creation
	//

	WNDCLASSA window_class = { 0 };
	window_class.lpfnWndProc = p_window_proc;
	window_class.lpszClassName = "play";
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	
	if (RegisterClassA(&window_class) == 0) {
		p->error = "Failed to initialize window class.";
		return P_FALSE;
	}

	p->win32.window = CreateWindowA("play", p->name, WS_OVERLAPPEDWINDOW | WS_VISIBLE, window_x, window_y, window_width, window_height, 0, 0, 0, 0);
	if (!p->win32.window) {
		p->error = "Failed to create window";
		return P_FALSE;
	}

	//enable acces to user data in the wnd_proc function
	SetWindowLongPtr(p->win32.window, GWLP_USERDATA, (LONG_PTR)p);

	LARGE_INTEGER large_integer;
	QueryPerformanceFrequency(&large_integer);
	p->ticks_per_second = large_integer.QuadPart;
	QueryPerformanceCounter(&large_integer);
	p->initial_ticks = large_integer.QuadPart;

	p_pull(p);

	return P_TRUE;
}

void p_push(Play *p) {

}

//Example code: zero initialize the Play struct
Play p;

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	//p.size.x = 100;
	//p.size.y = 200;
	assert(p_initialize(&p));
	while (!p.quit) {
		p_pull(&p);
		char temp[1024];
		static DWORD last_print_ticks = 0;
		if (GetTickCount() - last_print_ticks > 250) {
			sprintf_s(temp, sizeof(temp), "x=%d, y=%d, dx=%d, dy=%d\ndelta_ticks=%llu, time_ticks=%llu\n", p.pos.x, p.pos.y, p.size.x, p.size.y, p.delta_ticks, p.time_ticks);
			OutputDebugStringA(temp);
			last_print_ticks = GetTickCount();
		}
		p_push(&p);
	}

	return 0;
}