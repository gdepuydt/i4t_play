#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>
#include <xinput.h>
#include <math.h>


#define assert(x) \
	if(!(x)) { MessageBoxA(0, #x, "Assertion Failure", MB_OK); __debugbreak();}

void debug_out(const char *format, ...) {
	static char buffer[1024];
	va_list arg_list;
	va_start(arg_list, format);
	vsprintf_s(buffer, sizeof(buffer), format, arg_list);
	va_end(arg_list);
	OutputDebugStringA(buffer);
}

enum {
	P_MAX_KEYS = 256,
	P_MAX_TEXT = 256,
	P_MAX_ERROR = 1024,
	P_MAX_SOUND_SAMPLES = 1024 * 1024,
	P_TRUE = 1,
	P_FALSE = 0,
	P_CTRL = VK_CONTROL,
	P_ALT = VK_MENU,
	P_SHIFT = VK_SHIFT,
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

typedef DWORD (WINAPI *XINPUTGETSTATE)(DWORD dwUserIndex, XINPUT_STATE* pState);

struct P_Win32 {
	HWND window;
	void *main_fiber;
	void *message_fiber;
	XINPUTGETSTATE xinput_get_state;
};

struct P_Window {
	const char *title;
	P_Int2 pos;
	P_Int2 size;
};

struct Play {
	//Window
	P_Window window;

	//Error
	const char *error; //0 if no error
	char *error_buffer[P_MAX_ERROR];

	//Keyboard
	P_DigitalButton keys[P_MAX_KEYS]; //indexed by scancodes
	
	//Gamepad
	P_Gamepad gamepad;

	//Mouse
	P_Mouse mouse;
	
	//Time
	float delta_seconds;
	uint64_t delta_ticks;
	uint64_t delta_nanoseconds;
	uint64_t delta_microseconds;
	uint64_t delta_milliseconds;
	uint64_t delta_samples; //how many audio samples we need to calculate this frame

	double seconds; //53 bits of integer precision
	uint64_t ticks;
	uint64_t nanoseconds;
	uint64_t microseconds;
	uint64_t milliseconds;

	uint64_t initial_ticks;
	uint64_t ticks_per_second;
	

	//Sound
	uint32_t sound_samples_per_second; //e.g. 44.4k
	int16_t *sound_samples; //points to beginning of sample_buffer after update
	int16_t sound_sample_buffer[P_MAX_SOUND_SAMPLES];

	//Text
	char *text; //0 if no new text, stored in utf8
	char *text_end;
	char text_buffer[P_MAX_TEXT];

	//State
	P_Bool initialized;
	P_Bool quit;

	//Win32-specific stuff
	P_Win32 win32;
};

int16_t p_generate_sample() {
	// ...
	return 0;
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

//error

static void p_exit_with_error(Play *p) {
	MessageBoxA(0, p->error, "Play Error", MB_OK);
	ExitProcess(0);
}


/*
	PULL
*/



void p_pull_window(Play *p) {
	//This is the actual canvas of the rectange
	RECT client_rectangle;
	GetClientRect(p->win32.window, &client_rectangle);
	p->window.size.x = client_rectangle.right - client_rectangle.left;
	p->window.size.y = client_rectangle.bottom - client_rectangle.top;
	
	POINT window_position = { client_rectangle.left, client_rectangle.top };
	ClientToScreen(p->win32.window, &window_position);

	p->window.pos.x = window_position.x;
	p->window.pos.y = window_position.y;
}

void p_pull_time(Play *p) {
	LARGE_INTEGER large_integer;
	QueryPerformanceCounter(&large_integer);
	uint64_t current_ticks = large_integer.QuadPart;
	p->delta_ticks = current_ticks - p->ticks - p->initial_ticks;
	p->ticks = current_ticks - p->initial_ticks;

	p->delta_nanoseconds = 1000 * 1000 * 1000 * (p->delta_ticks / p->ticks_per_second);
	p->delta_microseconds = p->delta_nanoseconds / 1000;
	p->delta_milliseconds = p->delta_microseconds / 1000;
	p->delta_seconds = (float)p->delta_ticks / (float)p->ticks_per_second;
	//TODO: fill in delta_samples once we get to the sound processing

	p->nanoseconds = 1000 * 1000 * 1000 * (p->ticks / p->ticks_per_second);
	p->microseconds = p->nanoseconds / 1000;
	p->milliseconds = p->microseconds / 1000;
	p->seconds = (double)p->ticks / (double)p->ticks_per_second;

}

void p_reset_digital_button(P_DigitalButton *button) {
	button->pressed = P_FALSE;
	button->released = P_FALSE;
}

void p_pull_digital_button(P_DigitalButton *button, P_Bool down) {
	P_Bool was_down = button->down; //Is the button currently pressed down.
	button->released = was_down && !down; 
	button->pressed = !was_down && down;
	button->down = down;
}

void p_pull_analog_button(P_AnalogButton *button, float value) {
	button->value = value;
	P_Bool was_down = button->down;
	button->down = (value >= button->threshold);
	button->pressed = !was_down && button->down;
	button->released = was_down && !button->down;
}

void p_pull_axis(P_Axis *axis, float value) {
	if (fabs(value) <= axis->threshold) {
		axis->value = 0.0;
	}
	axis->value = value;
}

void p_pull_keys(Play *p) {
	BYTE keyboard_state[256];
	if (!GetKeyboardState(keyboard_state)) {
		return;
	}
	for (int key = 0; key < 256; key++) {
		p_pull_digital_button(p->keys + key, keyboard_state[key] >> 7);
	}
}

void p_pull_mouse(Play *p) {
	POINT mouse_position;
	GetCursorPos(&mouse_position);
	mouse_position.x -= p->window.pos.x;
	mouse_position.y -= p->window.pos.y;

	p->mouse.position.x = mouse_position.x;
	p->mouse.position.y = mouse_position.y;
}

void p_pull_gamepad(Play *p) {
	if (!p->win32.xinput_get_state) {
		return;
	}
	XINPUT_STATE xinput_state = { 0 };
	p->win32.xinput_get_state(0, &xinput_state);

	p_pull_digital_button(&p->gamepad.a_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0);
	p_pull_digital_button(&p->gamepad.b_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0);
	p_pull_digital_button(&p->gamepad.x_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0);
	p_pull_digital_button(&p->gamepad.y_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0);

	p_pull_digital_button(&p->gamepad.left_shoulder_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
	p_pull_digital_button(&p->gamepad.right_shoulder_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
	
	p_pull_digital_button(&p->gamepad.up_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0);
	p_pull_digital_button(&p->gamepad.down_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
	p_pull_digital_button(&p->gamepad.left_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
	p_pull_digital_button(&p->gamepad.right_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);

	p_pull_digital_button(&p->gamepad.left_thumb_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
	p_pull_digital_button(&p->gamepad.right_thumb_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
	
	p_pull_digital_button(&p->gamepad.back_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0);
	p_pull_digital_button(&p->gamepad.start_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0);

	p_pull_analog_button(&p->gamepad.left_trigger, xinput_state.Gamepad.bLeftTrigger / 255.f);
	p_pull_analog_button(&p->gamepad.right_trigger, xinput_state.Gamepad.bRightTrigger / 255.f);

	p_pull_axis(&p->gamepad.left_thumb_stick.x, xinput_state.Gamepad.sThumbLX / 32767.f);
	p_pull_axis(&p->gamepad.left_thumb_stick.y, xinput_state.Gamepad.sThumbLY / 32767.f);
	p_pull_axis(&p->gamepad.right_thumb_stick.x, xinput_state.Gamepad.sThumbRX / 32767.f);
	p_pull_axis(&p->gamepad.right_thumb_stick.y, xinput_state.Gamepad.sThumbRY / 32767.f);

}




//pull, push, update

void p_pull(Play *p) {
	if (!p->initialized) {
		if (!p->error) {
			p->error = "Play was not initialized.";
		}
		p_exit_with_error(p);
		return;
	}
	
	//required pre-processing before entering the message loop
	p->text_end = p->text_buffer;
	p->text = 0;
	p->mouse.delta_position.x = 0;
	p->mouse.delta_position.y = 0;
	p->mouse.delta_wheel = 0;
	
	//Making sure the edge cased are always reset for each frame, otherwise the edge case will be triggered erroneously witheach frame after a edge case transition
	p_reset_digital_button(&p->mouse.left_button);
	p_reset_digital_button(&p->mouse.right_button);


	//Handle the window messages
	SwitchToFiber(p->win32.message_fiber);
	
	//required post-processing after exiting the message loop.
	p->mouse.wheel += p->mouse.delta_wheel;
	p->mouse.position.x += p->mouse.delta_position.x;
	p->mouse.position.y += p->mouse.delta_position.y;
	p_pull_keys(p);
	if (p->text_end != p->text_buffer) {
		p->text = p->text_buffer;
	}
	p_pull_window(p);
	p_pull_time(p);
	p_pull_mouse(p);
	p_pull_gamepad(p);
}

void p_push(Play *p) {
	// ...
}

void p_update(Play *p) {
	p_push(p);
	p_pull(p);
}

/*
// Window Procedure
*/

static LRESULT CALLBACK p_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
	//pull in the user data, so we can access (and switch to) the main app fiber after timeout processing messages    
	Play *p = (Play *)GetWindowLongPtr(window, GWLP_USERDATA);

	LRESULT result = 0;
	switch (message) {
	case WM_INPUT: {
		UINT size;
		GetRawInputData((HRAWINPUT)lparam, RID_INPUT, 0, &size, sizeof(RAWINPUTHEADER));
		void *buffer = _alloca(size);
		if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) == size) {
			RAWINPUT *raw_input = (RAWINPUT *)buffer;
			if (raw_input->header.dwType == RIM_TYPEMOUSE && raw_input->data.mouse.usFlags == MOUSE_MOVE_RELATIVE) {
				//debug_out("x=%d y=%d\n", raw_input->data.mouse.lLastX, raw_input->data.mouse.lLastY);
				p->mouse.delta_position.x += raw_input->data.mouse.lLastX; //delta accross the frame, therefore we have to add the 'sub'-deltas
				p->mouse.delta_position.y += raw_input->data.mouse.lLastY;

				USHORT button_flags = raw_input->data.mouse.usButtonFlags;
				P_Bool left_button_down = p->mouse.left_button.down;
				if (button_flags & RI_MOUSE_LEFT_BUTTON_DOWN) {
					left_button_down = P_TRUE;
				}
				if (button_flags & RI_MOUSE_LEFT_BUTTON_UP) {
					left_button_down = P_FALSE;
				}
				p_pull_digital_button(&p->mouse.left_button, left_button_down);

				P_Bool right_button_down = p->mouse.right_button.down;
				if (button_flags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
					right_button_down = P_TRUE;
				}
				if (button_flags & RI_MOUSE_RIGHT_BUTTON_UP) {
					right_button_down = P_FALSE;
				}
				p_pull_digital_button(&p->mouse.right_button, right_button_down);
				if (button_flags & RI_MOUSE_WHEEL) {
					p->mouse.delta_wheel += ((SHORT)raw_input->data.mouse.usButtonData) /WHEEL_DELTA;
				}
			
			}
		}
		result = DefWindowProcA(window, message, wparam, lparam);
		break;
	}
	case WM_CHAR: {
		WCHAR utf16_character = (WCHAR)wparam;
		char ascii_character;
		uint32_t ascii_length = WideCharToMultiByte(CP_ACP, 0, &utf16_character, 1, &ascii_character, 1, 0, 0);
		if (ascii_length == 1 && p->text_end + 1 < p->text_buffer + sizeof(p->text_buffer) - 1) {
			*p->text_end = ascii_character;
			p->text_end[1] = 0;
			p->text_end += ascii_length;
		}
		break;
	}
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

P_Bool p_initialize(Play *p) {
	
	if (!p->window.title) {
		p->window.title = "Play";
	}
	int window_x;
	if (p->window.pos.x) {
		window_x = p->window.pos.x;
	}
	else {
		window_x = CW_USEDEFAULT;
	}

	int window_y;
	if (p->window.pos.y) {
		window_y = p->window.pos.y;
	}
	else {
		window_y = CW_USEDEFAULT;
	}

	int window_width;
	if (p->window.size.x) {
		window_width = p->window.size.x;
	}
	else {
		window_width = CW_USEDEFAULT;
	}
	int window_height;
	if (p->window.size.y) {
		window_height = p->window.size.y;
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

	HMODULE xinput_module = LoadLibraryA("XInput1_4.dll");
	if (xinput_module) {
		p->win32.xinput_get_state = (XINPUTGETSTATE)GetProcAddress(xinput_module, "XInputGetState");
	}

	float trigger_threshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 255.f;
	float  left_thumb_threshold = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.f;
	float  right_thumb_threshold = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.f;

	p->gamepad.left_trigger.threshold = trigger_threshold;
	p->gamepad.right_trigger.threshold = trigger_threshold;
	p->gamepad.left_thumb_stick.x.threshold = left_thumb_threshold;
	p->gamepad.left_thumb_stick.y.threshold = left_thumb_threshold;
	p->gamepad.right_thumb_stick.x.threshold = right_thumb_threshold;
	p->gamepad.right_thumb_stick.y.threshold = right_thumb_threshold;

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

	p->win32.window = CreateWindowA("play", p->window.title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, window_x, window_y, window_width, window_height, 0, 0, 0, 0);
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

	//USB mouse
	RAWINPUTDEVICE raw_input_device = {0};
	raw_input_device.usUsagePage = 0x01;
	raw_input_device.usUsage = 0x02;
	raw_input_device.dwFlags = 0;
	raw_input_device.hwndTarget = p->win32.window;
	if (!RegisterRawInputDevices(&raw_input_device, 1, sizeof(raw_input_device))) {
		p->error = "Failed to register input device: USB mouse.";
		return P_FALSE;
	}


	p->initialized = P_TRUE;
	p_pull(p);

	return P_TRUE;
}

//Example code: zero initialize the Play struct
Play p;

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	//p.size.x = 100;
	//p.size.y = 200;
	p_initialize(&p);
	while (!p.quit) {
#if 0
		static double last_print_time = 0.0;
		if ((p.seconds - last_print_time) > 0.75) {
			debug_out("x=%d, y=%d, dx=%d, dy=%d\n", p.pos.x, p.pos.y, p.size.x, p.size.y);
			debug_out("delta_ticks = %llu, time_ticks = %llu\n", p.delta_ticks, p.ticks);
			//debug_out("\n", );
			last_print_time = p.seconds;
		}
#endif
		if (p.mouse.delta_wheel) {
			debug_out("wheel delta: %d wheel: %d\n", p.mouse.delta_wheel, p.mouse.wheel);
		}
		
		if (p.mouse.left_button.pressed) {
			debug_out("left mouse button pressed: %d, %d\n", p.mouse.position.x, p.mouse.position.y);
		}
		if (p.mouse.left_button.released) {
			debug_out("left mouse button released\n");
		}
		if (p.mouse.right_button.pressed) {
			debug_out("right mouse button pressed\n");
		}
		if (p.mouse.right_button.released) {
			debug_out("right mouse button released\n");
		}
		
		if (p.text) {
			debug_out("%s\n", p.text);
		}
		if (p.keys[P_CTRL].pressed) {
			debug_out("Ctrl is pressed.\n");
		}
		if (p.keys[P_CTRL].released) {
			debug_out("Ctrl is released.\n");
		}
		if (p.keys[P_ALT].pressed) {
			debug_out("Alt is pressed.\n");
		}
		if (p.keys[P_ALT].released) {
			debug_out("Alt is released.\n");
		}
		if (p.keys[P_SHIFT].pressed) {
			debug_out("Shift is pressed.\n");
		}
		if (p.keys[P_SHIFT].released) {
			debug_out("Shift is released.\n");
		}
		p_update(&p);
	}

	return 0;
}