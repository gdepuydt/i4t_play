#define _CRT_SECURE_NO_WARNINGS
#include <malloc.h>
#define _USE_MATH_DEFINES 
#include <math.h>
//#define _NO_CRT_STDIO_INLINE
#include <stdio.h>
#include <stdarg.h>
#define NO_STRICT
#include <windows.h>
#include <wincodec.h>
#include <xinput.h>
#include <mmdeviceapi.h> 
#include <audioclient.h>

#include "Play.h"



#define check(x) \
	if(!(x)) { MessageBoxA(0, #x, "Assertion Failure", MB_OK); __debugbreak();}

#define check_succeeded(x) check(SUCCEEDED(x))

#ifdef __DEBUG
#define assert(x) check(x)
#else
#define assert(x)
#endif




P_EXTERN_BEGIN


void debug_out(const char *format, ...) {
	static char buffer[1024];
	va_list arg_list;
	va_start(arg_list, format);
	vsprintf_s(buffer, sizeof(buffer), format, arg_list);
	va_end(arg_list);
	OutputDebugStringA(buffer);
}


void p_exit_with_error(Play *p) {
	MessageBoxA(0, p->error, "Play Error", MB_OK);
	ExitProcess(1);
}

void p_digital_button_update(P_DigitalButton *button, P_Bool down) {
	P_Bool was_down = button->down;
	button->down = down;
	button->released = was_down && !down; 
	button->pressed = !was_down && down;
	
}

void p_analog_button_update(P_AnalogButton *button, float value) {
	button->value = value;
	P_Bool was_down = button->down;
	button->down = (value >= button->threshold);
	button->pressed = !was_down && button->down;
	button->released = was_down && !button->down;
}

void p_stick_update(P_Stick *stick, float x, float y) {
	if (fabs(x) <= stick->threshold) {
		stick->x = 0.0;
	}
	
	stick->x = x;

	if (fabs(y) <= stick->threshold) {
		stick->y = 0.0;
	}
	stick->y = y;
}

/*
// Window
*/

static LRESULT CALLBACK p_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
	LRESULT result = 0;
	Play *p = (Play *)GetWindowLongPtr(window, GWLP_USERDATA);
	
	switch (message) {
	case WM_SIZE:
		p->window.resized = P_TRUE;
		break;
	case WM_INPUT: {
		UINT size;
		GetRawInputData((HRAWINPUT)lparam, RID_INPUT, 0, &size, sizeof(RAWINPUTHEADER));
		void *buffer = _alloca(size);
		if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) == size) {
			RAWINPUT *raw_input = (RAWINPUT *)buffer;
			if (raw_input->header.dwType == RIM_TYPEMOUSE && raw_input->data.mouse.usFlags == MOUSE_MOVE_RELATIVE) {
				
				p->mouse.delta_position.x += raw_input->data.mouse.lLastX;
				p->mouse.delta_position.y += raw_input->data.mouse.lLastY;

				USHORT button_flags = raw_input->data.mouse.usButtonFlags;

				P_Bool left_button_down = p->mouse.left_button.down;
				if (button_flags & RI_MOUSE_LEFT_BUTTON_DOWN) {
					left_button_down = P_TRUE;
				}
				if (button_flags & RI_MOUSE_LEFT_BUTTON_UP) {
					left_button_down = P_FALSE;
				}
				p_digital_button_update(&p->mouse.left_button, left_button_down);

				P_Bool right_button_down = p->mouse.right_button.down;
				if (button_flags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
					right_button_down = P_TRUE;
				}
				if (button_flags & RI_MOUSE_RIGHT_BUTTON_UP) {
					right_button_down = P_FALSE;
				}
				p_digital_button_update(&p->mouse.right_button, right_button_down);
				if (button_flags & RI_MOUSE_WHEEL) {
					p->mouse.delta_wheel += ((SHORT)raw_input->data.mouse.usButtonData) / WHEEL_DELTA;
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
		if (ascii_length == 1 && p->text_length + 1 < sizeof(p->text) - 1) {
			p->text[p->text_length] = ascii_character;
			p->text[p->text_length + 1] = 0;
			p->text_length += ascii_length;
		}
		break;
	}
	case WM_DESTROY:
		p->quit = P_TRUE;
		break;
	case WM_TIMER:
		SwitchToFiber(p->win32.main_fiber);
		break;
	default:
		result = DefWindowProcW(window, message, wparam, lparam);
	}
	return result;
}

static void CALLBACK p_message_fiber_proc(Play *p) {
	SetTimer(p->win32.window, 1, 1, 0);
	for (;;) {
		MSG message;
		if (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}
		SwitchToFiber(p->win32.main_fiber);
	}
}

P_Bool p_window_initialize(Play *p) {
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
		if (AdjustWindowRect(&window_rectangle, WS_OVERLAPPEDWINDOW, 0)) {
			window_width = window_rectangle.right - window_rectangle.left;
			window_height = window_rectangle.bottom - window_rectangle.top;
		}
	}

	WNDCLASSA window_class = { 0 };
	window_class.lpfnWndProc = p_window_proc;
	window_class.lpszClassName = "play";
	window_class.style = CS_HREDRAW | CS_VREDRAW;

	if (RegisterClassA(&window_class) == 0) {
		p->error = "Failed to initialize window class.";
		return P_FALSE;
	}

	p->win32.window = CreateWindowA("play", p->window.title, WS_OVERLAPPEDWINDOW, window_x, window_y, window_width, window_height, 0, 0, 0, 0);
	if (!p->win32.window) {
		p->error = "Failed to create window";
		return P_FALSE;
	}

	//enable acces to user data in the wnd_proc function
	SetWindowLongPtr(p->win32.window, GWLP_USERDATA, (LONG_PTR)p);
	ShowWindow(p->win32.window, SW_SHOW);
	p->win32.device_context = GetDC(p->win32.window);
	return P_TRUE;
}

void p_window_pull(Play *p) {

	p->text[0] = 0;
	p->text_length = 0;
	p->mouse.delta_position.x = 0;
	p->mouse.delta_position.y = 0;
	p->mouse.delta_wheel = 0;
	p->mouse.left_button.pressed = P_FALSE;
	p->mouse.right_button.pressed = P_FALSE;
	p->mouse.left_button.released = P_FALSE;
	p->mouse.right_button.released = P_FALSE;

	SwitchToFiber(p->win32.message_fiber);

	p->mouse.wheel += p->mouse.delta_wheel;

	/*p->mouse.position.x += p->mouse.delta_position.x;
	p->mouse.position.y += p->mouse.delta_position.y;*/

	RECT client_rectangle;
	GetClientRect(p->win32.window, &client_rectangle);
	p->window.size.x = client_rectangle.right - client_rectangle.left;
	p->window.size.y = client_rectangle.bottom - client_rectangle.top;

	POINT window_position = { client_rectangle.left, client_rectangle.top };
	ClientToScreen(p->win32.window, &window_position);

	p->window.pos.x = window_position.x;
	p->window.pos.y = window_position.y;
}

//
//Play_Time
//

P_Bool p_time_initialize(Play *p) {
	LARGE_INTEGER large_integer;
	QueryPerformanceFrequency(&large_integer);
	p->time.ticks_per_second = large_integer.QuadPart;
	QueryPerformanceCounter(&large_integer);
	p->time.initial_ticks = large_integer.QuadPart;
	return P_TRUE;
}

void p_time_pull(Play *p) {
	LARGE_INTEGER large_integer;
	QueryPerformanceCounter(&large_integer);
	uint64_t current_ticks = large_integer.QuadPart;

	p->time.delta_ticks = current_ticks - p->time.ticks - p->time.initial_ticks;
	p->time.ticks = current_ticks - p->time.initial_ticks;

	p->time.delta_nanoseconds = 1000 * 1000 * 1000 * (p->time.delta_ticks / p->time.ticks_per_second);
	p->time.delta_microseconds = p->time.delta_nanoseconds / 1000;
	p->time.delta_milliseconds = p->time.delta_microseconds / 1000;
	p->time.delta_seconds = (float)p->time.delta_ticks / (float)p->time.ticks_per_second;
	
	p->time.nanoseconds = 1000 * 1000 * 1000 * (p->time.ticks / p->time.ticks_per_second);
	p->time.microseconds = p->time.nanoseconds / 1000;
	p->time.milliseconds = p->time.microseconds / 1000;
	p->time.seconds = (double)p->time.ticks / (double)p->time.ticks_per_second;
}

//
//Play_Keyboard
//

void p_keyboard_pull(Play *p) {
	BYTE keyboard_state[256] = {0};
	GetKeyboardState(keyboard_state);

	for (int key = 0; key < 256; key++) {
		p_digital_button_update(p->keys + key, keyboard_state[key] >> 7);
	}
}

//
//Play_Mouse
//

P_Bool p_mouse_initialize(Play *p) {
	RAWINPUTDEVICE raw_input_device = { 0 };
	raw_input_device.usUsagePage = 0x01;
	raw_input_device.usUsage = 0x02;
	raw_input_device.dwFlags = 0;
	raw_input_device.hwndTarget = p->win32.window;
	if (!RegisterRawInputDevices(&raw_input_device, 1, sizeof(raw_input_device))) {
		p->error = "Failed to register input device: USB mouse.";
		return P_FALSE;
	}
	return P_TRUE;
}

void p_mouse_pull(Play *p) {
	POINT mouse_position;
	GetCursorPos(&mouse_position);
	mouse_position.x -= p->window.pos.x;
	mouse_position.y -= p->window.pos.y;

	p->mouse.position.x = mouse_position.x;
	p->mouse.position.y = mouse_position.y;
}

//
//Pay_Gamepad
//

P_Bool p_gamepad_initialize(Play *p) {
	HMODULE xinput_module = LoadLibraryA("XInput1_4.dll");
	if (xinput_module) {
		p->win32.xinput_get_state = (XINPUTGETSTATE)GetProcAddress(xinput_module, "XInputGetState");
	}

	float trigger_threshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 255.f;
	float  left_thumb_threshold = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.f;
	float  right_thumb_threshold = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.f;

	p->gamepad.left_trigger.threshold = trigger_threshold;
	p->gamepad.right_trigger.threshold = trigger_threshold;
	p->gamepad.left_thumb_stick.threshold = left_thumb_threshold;
	p->gamepad.right_thumb_stick.threshold = right_thumb_threshold;
	return P_TRUE;
}

void p_gamepad_pull(Play *p) {
	XINPUT_STATE xinput_state = { 0 };
	if (p->win32.xinput_get_state(0, &xinput_state) != ERROR_SUCCESS) {
		p->gamepad.connected = P_FALSE;
		return;
	}

	p->gamepad.connected = P_TRUE;

	p_digital_button_update(&p->gamepad.a_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0);
	p_digital_button_update(&p->gamepad.b_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0);
	p_digital_button_update(&p->gamepad.x_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0);
	p_digital_button_update(&p->gamepad.y_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0);
	 			
	p_digital_button_update(&p->gamepad.left_shoulder_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
	p_digital_button_update(&p->gamepad.right_shoulder_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
	 			
	p_digital_button_update(&p->gamepad.up_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0);
	p_digital_button_update(&p->gamepad.down_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
	p_digital_button_update(&p->gamepad.left_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
	p_digital_button_update(&p->gamepad.right_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
	 				 
	p_digital_button_update(&p->gamepad.left_thumb_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
	p_digital_button_update(&p->gamepad.right_thumb_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
	 		
	p_digital_button_update(&p->gamepad.back_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0);
	p_digital_button_update(&p->gamepad.start_button, (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0);
	
	p_analog_button_update(&p->gamepad.left_trigger, xinput_state.Gamepad.bLeftTrigger / 255.f);
	p_analog_button_update(&p->gamepad.right_trigger, xinput_state.Gamepad.bRightTrigger / 255.f);

	//Scale values between -1.0 and 1.0
	#define CONVERT(x) (2.0f * (((x + 32768) / 65535.f) - 0.5f)) 
	p_stick_update(&p->gamepad.left_thumb_stick, CONVERT(xinput_state.Gamepad.sThumbLX + 32767.f), CONVERT(xinput_state.Gamepad.sThumbLY));
	p_stick_update(&p->gamepad.right_thumb_stick, CONVERT(xinput_state.Gamepad.sThumbRX + 32767.f), CONVERT(xinput_state.Gamepad.sThumbRY));
	#undef CONVERT
}

//
//Play_Audio
//

/*
static void p_audio_default_callback(P_AudioRequest *request) {
	FillMemory(request->sample, sizeof(int16_t) * (request->end_sample - request->sample), 0);
}*/

DWORD p_audio_threadproc(void *parameter) {
	Play *p = (Play *)parameter;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	HANDLE buffer_ready_event = CreateEvent(0, 0, 0, 0);
	p->win32.audio_client->SetEventHandle(buffer_ready_event);
	uint32_t buffer_frame_count;
	p->win32.audio_client->GetBufferSize(&buffer_frame_count);
	size_t sample_count = buffer_frame_count * p->audio.channels;
	p->win32.audio_client->Start();
	for (;;) {
		WaitForSingleObject(buffer_ready_event, INFINITE);
		P_AudioRequest request;
		request.samples_per_second = p->audio.samples_per_second;
		request.channels = p->audio.channels;
		request.bytes_per_sample = p->audio.bytes_per_sample;
		p->win32.audio_render_client->GetBuffer(buffer_frame_count, (BYTE**)&request.sample);
		request.end_sample = request.sample + sample_count;
		p->audio.callback(&request);
		p->win32.audio_render_client->ReleaseBuffer(buffer_frame_count, 0);
	}
	return 0;
}

P_Bool p_audio_initialize(Play *p) {
	/*if (!p->audio.callback) {
		p->audio.callback = p_audio_default_callback;
	}*/
	check_succeeded(CoInitializeEx(0, COINITBASE_MULTITHREADED));

	IMMDeviceEnumerator *device_enumerator;
	check_succeeded(CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_INPROC, IID_PPV_ARGS(&device_enumerator)));
	
	IMMDevice *audio_device;
	check_succeeded(device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audio_device));
	
	IAudioClient *audio_client;
	check_succeeded(audio_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, 0, (void **)&audio_client));

	REFERENCE_TIME device_period;
	check_succeeded(audio_client->GetDevicePeriod(0, &device_period));

	WAVEFORMATEX audio_format = { 0 };
	audio_format.wFormatTag = WAVE_FORMAT_PCM;
	audio_format.nChannels = 1;
	audio_format.nSamplesPerSec = 44100;
	audio_format.wBitsPerSample = 16;
	audio_format.nBlockAlign = (audio_format.nChannels * audio_format.wBitsPerSample) / 8;
	audio_format.nAvgBytesPerSec = audio_format.nSamplesPerSec * audio_format.nBlockAlign;
	
	DWORD audio_client_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_RATEADJUST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
	check_succeeded(audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, audio_client_flags, device_period, device_period, &audio_format, 0));

	IAudioRenderClient *audio_render_client;
	check_succeeded(audio_client->GetService(IID_PPV_ARGS(&audio_render_client)));
	
	p->audio.samples_per_second = audio_format.nSamplesPerSec;
	p->win32.audio_client = audio_client;
	p->win32.audio_render_client = audio_render_client;
	CreateThread(0, 0, p_audio_threadproc, p, 0, 0);
	
	device_enumerator->Release();
	return P_TRUE;
}


//
//Play
//

P_Bool p_pull(Play *p) {
	if (!p->initialized) {
		if (!p->error) {
			p->error = "Play was not initialized.";
		}
		p_exit_with_error(p);
		return P_FALSE;
	}
	p_window_pull(p);
	p_time_pull(p);
	p_keyboard_pull(p);
	p_mouse_pull(p);
	p_gamepad_pull(p);
	return !p->quit;
}

void p_push(Play *p) {
	// ...
}

P_Bool p_initialize(Play *p) {

	if (!p_window_initialize(p)) {
		return P_FALSE;
	}

	if (!p_time_initialize(p)) {
		return P_FALSE;
	}

	if (!p_mouse_initialize(p)) {
		
			return P_FALSE;
	}

	if (!p_gamepad_initialize(p)) {
		return P_FALSE;
	}

	if (!p_audio_initialize(p)) {
		return P_FALSE;
	}

	p->initialized = P_TRUE;
	p_pull(p);
	return P_TRUE;
}

P_EXTERN_END