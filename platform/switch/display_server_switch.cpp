/**************************************************************************/
/*  display_server_switch.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "display_server_switch.h"

#include "os_switch.h"

#include "core/input/input_event.h"
#include "servers/display/native_menu.h"

#ifdef GLES3_ENABLED
#include "drivers/gles3/rasterizer_gles3.h"
#endif

#include <cstdio>

Vector<String> DisplayServerSwitch::get_rendering_drivers_func() {
	Vector<String> drivers;
#ifdef GLES3_ENABLED
	drivers.push_back("opengl3");
#endif
	return drivers;
}

DisplayServer *DisplayServerSwitch::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	DisplayServer *ds = memnew(DisplayServerSwitch(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, p_parent_window, r_error));
	if (r_error != OK) {
		OS::get_singleton()->alert(
				"Unable to initialize the OpenGL ES 3.0 video driver.",
				"Unable to initialize video driver");
	}
	return ds;
}

void DisplayServerSwitch::register_switch_driver() {
	register_create_function("switch", create_func, get_rendering_drivers_func);
}

void DisplayServerSwitch::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	static_cast<DisplayServerSwitch *>(get_singleton())->_dispatch_input_event(p_event);
}

void DisplayServerSwitch::_dispatch_input_event(const Ref<InputEvent> &p_event) {
	if (input_event_callback.is_valid()) {
		input_event_callback.call(p_event);
	}
}

bool DisplayServerSwitch::has_feature(DisplayServerEnums::Feature p_feature) const {
	switch (p_feature) {
		case DisplayServerEnums::FEATURE_TOUCHSCREEN:
		case DisplayServerEnums::FEATURE_SWAP_BUFFERS:
			return true;
		default:
			return false;
	}
}

Size2i DisplayServerSwitch::screen_get_size(int p_screen) const {
	return window_get_size();
}

Size2i DisplayServerSwitch::window_get_size(DisplayServerEnums::WindowID p_window) const {
	if (egl_display != EGL_NO_DISPLAY && egl_surface != EGL_NO_SURFACE) {
		EGLint width = 0;
		EGLint height = 0;
		eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &width);
		eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &height);
		if (width > 0 && height > 0) {
			return Size2i(width, height);
		}
	}
	return Size2i(1280, 720);
}

int64_t DisplayServerSwitch::window_get_native_handle(DisplayServerEnums::HandleType p_handle_type, DisplayServerEnums::WindowID p_window) const {
	switch (p_handle_type) {
		case DisplayServerEnums::DISPLAY_HANDLE: {
			return reinterpret_cast<int64_t>(egl_display);
		}
		case DisplayServerEnums::WINDOW_HANDLE: {
			return reinterpret_cast<int64_t>(nwindowGetDefault());
		}
		case DisplayServerEnums::OPENGL_CONTEXT: {
			return reinterpret_cast<int64_t>(egl_context);
		}
		default: {
			return 0;
		}
	}
}

void DisplayServerSwitch::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
	if (egl_display == EGL_NO_DISPLAY) {
		return;
	}
	vsync_mode = (p_vsync_mode == DisplayServerEnums::VSYNC_DISABLED) ? DisplayServerEnums::VSYNC_DISABLED : DisplayServerEnums::VSYNC_ENABLED;
	eglSwapInterval(egl_display, vsync_mode == DisplayServerEnums::VSYNC_DISABLED ? 0 : 1);
}

void DisplayServerSwitch::release_rendering_thread() {
	if (egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}
}

void DisplayServerSwitch::swap_buffers() {
	if (egl_display != EGL_NO_DISPLAY) {
		eglSwapBuffers(egl_display, egl_surface);
	}
}

void DisplayServerSwitch::_process_touch() {
	Input *input = Input::get_singleton();

	HidTouchScreenState touch_state = { 0 };
	if (!hidGetTouchScreenStates(&touch_state, 1)) {
		return;
	}

	int touch_count = MIN((int)touch_state.count, 16);

	if (touch_count != last_touch_count) {
		if (touch_count > last_touch_count) {
			// Gained new touches, add them.
			for (int i = last_touch_count; i < touch_count; i++) {
				Vector2 pos(touch_state.touches[i].x, touch_state.touches[i].y);
				last_touch_pos[i] = pos;

				Ref<InputEventScreenTouch> st;
				st.instantiate();
				st->set_index(i);
				st->set_position(pos);
				st->set_pressed(true);
				input->parse_input_event(st);
			}
		} else {
			// Lost touches, release them.
			for (int i = touch_count; i < last_touch_count; i++) {
				Ref<InputEventScreenTouch> st;
				st.instantiate();
				st->set_index(i);
				st->set_position(last_touch_pos[i]);
				st->set_pressed(false);
				input->parse_input_event(st);
			}
		}
	} else {
		for (int i = 0; i < touch_count; i++) {
			Vector2 pos(touch_state.touches[i].x, touch_state.touches[i].y);
			if (pos == last_touch_pos[i]) {
				continue;
			}

			Ref<InputEventScreenDrag> sd;
			sd.instantiate();
			sd->set_index(i);
			sd->set_position(pos);
			sd->set_relative(pos - last_touch_pos[i]);
			last_touch_pos[i] = pos;
			input->parse_input_event(sd);
		}
	}

	last_touch_count = touch_count;
}

void DisplayServerSwitch::process_events() {
	_process_touch();
	OS_Switch::get_singleton()->process_joypads();
	Input::get_singleton()->flush_buffered_events();
}

Error DisplayServerSwitch::_initialize_egl() {
	egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (egl_display == EGL_NO_DISPLAY) {
		ERR_PRINT(vformat("Could not connect to EGL display. Error: %d", eglGetError()));
		return ERR_UNAVAILABLE;
	}

	if (!eglInitialize(egl_display, nullptr, nullptr)) {
		ERR_PRINT(vformat("Could not initialize EGL display connection. Error: %d", eglGetError()));
		egl_display = EGL_NO_DISPLAY;
		return ERR_UNAVAILABLE;
	}

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		ERR_PRINT(vformat("Could not bind the OpenGL ES API. Error: %d", eglGetError()));
		_finalize_egl();
		return ERR_UNAVAILABLE;
	}

	static const EGLint attribute_list[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};

	EGLConfig config = nullptr;
	EGLint num_configs = 0;
	eglChooseConfig(egl_display, attribute_list, &config, 1, &num_configs);
	if (num_configs == 0) {
		ERR_PRINT(vformat("No EGL config found. Error: %d", eglGetError()));
		_finalize_egl();
		return ERR_UNAVAILABLE;
	}

	egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)nwindowGetDefault(), nullptr);
	if (egl_surface == EGL_NO_SURFACE) {
		ERR_PRINT(vformat("EGL surface creation failed. Error: %d", eglGetError()));
		_finalize_egl();
		return ERR_UNAVAILABLE;
	}

	static const EGLint context_attribute_list[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};

	egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribute_list);
	if (egl_context == EGL_NO_CONTEXT) {
		ERR_PRINT(vformat("EGL context creation failed. Error: %d", eglGetError()));
		_finalize_egl();
		return ERR_UNAVAILABLE;
	}

	if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
		ERR_PRINT(vformat("Could not make the EGL context current. Error: %d", eglGetError()));
		_finalize_egl();
		return ERR_UNAVAILABLE;
	}

	return OK;
}

void DisplayServerSwitch::_finalize_egl() {
	if (egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (egl_context != EGL_NO_CONTEXT) {
			eglDestroyContext(egl_display, egl_context);
			egl_context = EGL_NO_CONTEXT;
		}
		if (egl_surface != EGL_NO_SURFACE) {
			eglDestroySurface(egl_display, egl_surface);
			egl_surface = EGL_NO_SURFACE;
		}
		eglTerminate(egl_display);
		egl_display = EGL_NO_DISPLAY;
	}
}

DisplayServerSwitch::DisplayServerSwitch(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	r_error = ERR_UNAVAILABLE;

	native_menu = memnew(NativeMenu);

	hidInitializeTouchScreen();

#ifdef GLES3_ENABLED
	if (p_rendering_driver == "opengl3") {
		if (_initialize_egl() != OK) {
			return;
		}

		RasterizerGLES3::make_current(false);
	}
#endif

	window_set_vsync_mode(p_vsync_mode);

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	r_error = OK;
}

DisplayServerSwitch::~DisplayServerSwitch() {
	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}

	_finalize_egl();
}
