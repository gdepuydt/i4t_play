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
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

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


static void p_audio_default_callback(P_AudioBuffer *buffer) {
	FillMemory(buffer->samples, sizeof(int16_t) * buffer->samples_count, 0);
}

DWORD p_audio_threadproc(void *parameter) {
	Play *p = (Play *)parameter;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	HANDLE buffer_ready_event = CreateEvent(0, 0, 0, 0);
	p->win32.audio_client->SetEventHandle(buffer_ready_event);
	uint32_t buffer_frame_count;
	p->win32.audio_client->GetBufferSize(&buffer_frame_count);
	uint32_t buffer_sample_count = buffer_frame_count * p->audio.format.channels;
	p->win32.audio_client->Start();
	for (;;) {
		DWORD wait_result = WaitForSingleObject(buffer_ready_event, INFINITE);
		check(wait_result == WAIT_OBJECT_0);
		P_AudioBuffer buffer;
		buffer.format = p->audio.format;
		uint32_t padding_frame_count;
		p->win32.audio_client->GetCurrentPadding(&padding_frame_count);
		uint32_t padding_sample_count = padding_frame_count * buffer.format.channels;
		uint32_t fill_sample_count = buffer_sample_count - padding_sample_count;
		p->win32.audio_render_client->GetBuffer(fill_sample_count, (BYTE**)&buffer.samples);
		buffer.samples_count = fill_sample_count;
		p->audio.callback(&buffer);
		p->win32.audio_render_client->ReleaseBuffer(fill_sample_count, 0);
	}
	return 0;
}

static WAVEFORMATEX win32_audio_format = {WAVE_FORMAT_PCM, 1, 44100, 44100 * 2, 2, 16, 0};
static P_AudioFormat p_audio_format = {44100, 1, 2};

P_Bool p_audio_initialize(Play *p) {
	if (!p->audio.callback) {
		p->audio.callback = p_audio_default_callback;
	}
	check_succeeded(CoInitializeEx(0, COINITBASE_MULTITHREADED));

	IMMDeviceEnumerator *device_enumerator;
	check_succeeded(CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_INPROC, IID_PPV_ARGS(&device_enumerator)));
	
	IMMDevice *audio_device;
	check_succeeded(device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audio_device));
	
	IAudioClient *audio_client;
	check_succeeded(audio_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, 0, (void **)&audio_client));

	REFERENCE_TIME device_period;
	check_succeeded(audio_client->GetDevicePeriod(0, &device_period));

	/*WAVEFORMATEX audio_format = { 0 };
	audio_format.wFormatTag = WAVE_FORMAT_PCM;
	audio_format.nChannels = 1;
	audio_format.nSamplesPerSec = 44100;
	audio_format.wBitsPerSample = 16;
	audio_format.nBlockAlign = (audio_format.nChannels * audio_format.wBitsPerSample) / 8;
	audio_format.nAvgBytesPerSec = audio_format.nSamplesPerSec * audio_format.nBlockAlign;*/

	REFERENCE_TIME audio_buffer_duration = 300000; //30 milliseconds
	
	DWORD audio_client_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_RATEADJUST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
	check_succeeded(audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, audio_client_flags, audio_buffer_duration, 0, &win32_audio_format, 0));

	IAudioRenderClient *audio_render_client;
	check_succeeded(audio_client->GetService(IID_PPV_ARGS(&audio_render_client)));
	
	p->audio.format.samples_per_second = win32_audio_format.nSamplesPerSec;
	p->audio.format.channels = win32_audio_format.nChannels;
	p->audio.format.bytes_per_sample = (win32_audio_format.nChannels * win32_audio_format.wBitsPerSample) / 8;
	p->win32.audio_client = audio_client;
	p->win32.audio_render_client = audio_render_client;
	CreateThread(0, 0, p_audio_threadproc, p, 0, 0);
	
	device_enumerator->Release();
	return P_TRUE;
}

//
//OpenGL
//

void p_opengl_push(Play *p) {
	SwapBuffers(p->win32.device_context);
}


P_Bool p_opengl_initialize(Play *p) {
	PIXELFORMATDESCRIPTOR pixel_format_descriptor;
	pixel_format_descriptor.nSize = sizeof(pixel_format_descriptor);
	pixel_format_descriptor.dwFlags = LPD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
	pixel_format_descriptor.iPixelType = PFD_TYPE_RGBA;
	pixel_format_descriptor.cColorBits = 24;
	pixel_format_descriptor.cAlphaBits = 0;
	pixel_format_descriptor.cDepthBits = 24;
	pixel_format_descriptor.cStencilBits = 8;
	int pixel_format = ChoosePixelFormat(p->win32.device_context, &pixel_format_descriptor);
	if (!pixel_format) {
		return P_FALSE;
	}
	if (!DescribePixelFormat(p->win32.device_context, pixel_format, sizeof(pixel_format_descriptor), &pixel_format_descriptor)) {
		return P_FALSE;
	}
	if (!SetPixelFormat(p->win32.device_context, pixel_format, &pixel_format_descriptor)) {
		return P_FALSE;
	}
	p->win32.wgl_context = wglCreateContext(p->win32.device_context);
	if (!p->win32.wgl_context) {
		return P_FALSE;
	}
	wglMakeCurrent(p->win32.device_context, p->win32.wgl_context);
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
	p_opengl_push(p);
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

	if (!p_opengl_initialize(p)) {
		return P_FALSE;
	}

	p->initialized = P_TRUE;
	p_pull(p);
	return P_TRUE;
}

//
// Utilities
//


//
//P_Image
//

static IWICImagingFactory *wic_factory; //TODO: multithreading
static SRWLOCK  wic_factory_lock = SRWLOCK_INIT;

P_Bool p_load_image(const char *filename, P_Image *image) {
	if (!wic_factory) {
		AcquireSRWLockExclusive(&wic_factory_lock);
		if (!wic_factory) {
			CoInitializeEx(0, COINITBASE_MULTITHREADED);
			if (!SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, 0, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory)))) {
				ReleaseSRWLockExclusive(&wic_factory_lock);
				return P_FALSE;
			}
			ReleaseSRWLockExclusive(&wic_factory_lock);
		}
	}
	P_Bool result = P_FALSE;
	IWICBitmapDecoder *image_decoder = 0;
	IWICBitmapFrameDecode *image_frame = 0;
	IWICBitmapSource *rgba_image_frame = 0;
	uint32_t buffer_size = 0;
	uint32_t buffer_stride = 0;

	int wide_filename_length = MultiByteToWideChar(CP_UTF8, 0, filename, -1, 0, 0);
	WCHAR *wide_filename = (WCHAR *)_alloca(wide_filename_length * sizeof(WCHAR));
	MultiByteToWideChar(CP_UTF8, 0, filename, -1, wide_filename, wide_filename_length);
	
	if (!SUCCEEDED(wic_factory->CreateDecoderFromFilename(wide_filename, 0, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &image_decoder))) {
		goto done;
	}
	if (!SUCCEEDED(image_decoder->GetFrame(0, &image_frame))) {
		goto done;
	}
	if (!SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, image_frame, &rgba_image_frame))) {
		goto done;
	}

	uint32_t width;
	uint32_t height;
	image_frame->GetSize(&width, &height);
	image->width = width;
	image->height = height;
	image->channels = 4;
	buffer_size = 4 * width * height;
	image->pixels = (uint8_t *)malloc(buffer_size);
	buffer_stride = 4 * width;
	if (!SUCCEEDED(rgba_image_frame->CopyPixels(0, buffer_stride, buffer_size, image->pixels))) {
		free(image->pixels);
		goto done;
	}

	result = P_TRUE;
	
done:
	if (image_decoder) {
		image_decoder->Release();
	}
	if (image_frame) {
		image_frame->Release();
	}
	if (rgba_image_frame) {
		rgba_image_frame->Release();
	}
	return result;
}

//
//Sound Media
//

static P_Bool mf_initialized;

P_Bool p_load_audio(const char *filename, P_AudioBuffer *audio) {
	if (!mf_initialized) {
		CoInitializeEx(0, COINIT_MULTITHREADED);
		if (!SUCCEEDED(MFStartup(MF_VERSION, 0))) {
			return P_FALSE;
		}
		mf_initialized = true;
	}
	int wide_filename_length = MultiByteToWideChar(CP_UTF8, 0, filename, -1, 0, 0);
	WCHAR *wide_filename = (WCHAR *)_alloca(wide_filename_length * sizeof(WCHAR));
	MultiByteToWideChar(CP_UTF8, 0, filename, -1, wide_filename, wide_filename_length);
	
	IMFSourceReader *source_reader = 0;
	IMFMediaType *media_type = 0;

	P_Bool result = P_FALSE;
	size_t buffer_capacity = p_audio_format.samples_per_second * p_audio_format.bytes_per_sample;
	size_t buffer_size = 0;
	char *buffer = (char *)malloc(buffer_capacity);
	DWORD flags = 0;
	
	if (!SUCCEEDED(MFCreateSourceReaderFromURL(wide_filename, 0, &source_reader))) {
		goto done;
	}
	if (!SUCCEEDED(MFCreateMediaType(&media_type))) {
		goto done;
	}
	media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	media_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, win32_audio_format.nChannels);
	media_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, win32_audio_format.nSamplesPerSec);
	media_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, win32_audio_format.nBlockAlign);
	media_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, win32_audio_format.nAvgBytesPerSec);
	media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, win32_audio_format.wBitsPerSample);
	media_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	if (!SUCCEEDED(source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, media_type))) {
		goto done;
	}
	
	for (;;) {
		DWORD stream_index;
		LONGLONG timestamp;
		IMFSample *sample;
		if (!SUCCEEDED(source_reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &stream_index, &flags, &timestamp, &sample))) {
			free(buffer);
			goto done;
		}
		
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
			break;
		}
		IMFMediaBuffer *sample_buffer;
		sample->ConvertToContiguousBuffer(&sample_buffer);
		DWORD sample_buffer_size;
		sample_buffer->GetCurrentLength(&sample_buffer_size);
		size_t new_buffer_size = buffer_size + sample_buffer_size;
		if (new_buffer_size > buffer_capacity) {
			buffer_capacity *= 2;
			if (buffer_capacity < new_buffer_size) {
				buffer_capacity = new_buffer_size;
			}
			buffer = (char *)realloc(buffer, buffer_capacity);
		}
		BYTE *sample_buffer_pointer;
		sample_buffer->Lock(&sample_buffer_pointer, 0, 0);
		CopyMemory(buffer + buffer_size, sample_buffer_pointer, sample_buffer_size);
		buffer_size += sample_buffer_size;
		sample_buffer->Unlock();
		sample_buffer->Release();
		sample->Release();
	}

	audio->format = p_audio_format;
	audio->samples = (int16_t *)buffer;
	audio->samples_count = buffer_size / (p_audio_format.bytes_per_sample);
	
	

	//result = P_TRUE;


done:
	if (source_reader) {
		source_reader->Release();
	}
	if (media_type) {
		media_type->Release();
	}

	return result;

}

P_EXTERN_END