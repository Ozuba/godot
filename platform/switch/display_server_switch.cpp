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
		case DisplayServerEnums::FEATURE_VIRTUAL_KEYBOARD:
			return swkbd_created;
		default:
			return false;
	}
}

void DisplayServerSwitch::_applet_hook(AppletHookType p_hook, void *p_param) {
	DisplayServerSwitch *ds = static_cast<DisplayServerSwitch *>(p_param);
	if (p_hook == AppletHookType_OnOperationMode || p_hook == AppletHookType_OnResume) {
		ds->operation_mode_dirty = true;
	}
}

void DisplayServerSwitch::_update_operation_mode() {
	operation_mode_dirty = false;

	const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
	const Size2i new_size = docked ? Size2i(1920, 1080) : Size2i(1280, 720);
	if (egl_display == EGL_NO_DISPLAY || window_get_size() == new_size) {
		return;
	}

	// The EGL surface is tied to the native window dimensions, so it has to
	// be recreated when switching between handheld and docked mode.
	eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl_surface != EGL_NO_SURFACE) {
		eglDestroySurface(egl_display, egl_surface);
		egl_surface = EGL_NO_SURFACE;
	}

	NWindow *win = nwindowGetDefault();
	nwindowSetDimensions(win, new_size.width, new_size.height);

	egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)win, nullptr);
	if (egl_surface == EGL_NO_SURFACE) {
		ERR_PRINT(vformat("Failed to recreate EGL surface after mode switch. Error: %d", eglGetError()));
		return;
	}
	eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
	eglSwapInterval(egl_display, vsync_mode == DisplayServerEnums::VSYNC_DISABLED ? 0 : 1);

	if (rect_changed_callback.is_valid()) {
		rect_changed_callback.call(Rect2i(Point2i(), new_size));
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

// The inline software keyboard reports text changes as diffs against the
// previous string; translate them to key events for Godot's controls.
void DisplayServerSwitch::_send_key(Key p_key, char32_t p_unicode) {
	for (int pressed = 1; pressed >= 0; pressed--) {
		Ref<InputEventKey> ev;
		ev.instantiate();
		ev->set_echo(false);
		ev->set_pressed(pressed);
		ev->set_keycode(p_key);
		ev->set_physical_keycode(p_key);
		ev->set_key_label(p_key);
		ev->set_unicode(p_unicode);
		Input::get_singleton()->parse_input_event(ev);
	}
}

void DisplayServerSwitch::_swkbd_string_changed(const char *p_str, SwkbdChangedStringArg *p_arg) {
	DisplayServerSwitch *ds = static_cast<DisplayServerSwitch *>(get_singleton());

	// A string-changed event fires on appear and when the text is set
	// programmatically; those must not produce key events.
	if (ds->swkbd_eat_string_events > 0) {
		ds->swkbd_eat_string_events--;
		ds->swkbd_last_len = p_arg->stringLen;
		return;
	}

	if (p_arg->stringLen < ds->swkbd_last_len) {
		ds->_send_key(Key::BACKSPACE, 0);
	} else if (p_arg->stringLen > 0) {
		String text = String::utf8(p_str);
		if (text.length() > 0) {
			char32_t c = text[MIN((int)p_arg->stringLen, text.length()) - 1];
			ds->_send_key(Key::NONE, c);
		}
	}
	ds->swkbd_last_len = p_arg->stringLen;
}

void DisplayServerSwitch::_swkbd_moved_cursor(const char *p_str, SwkbdMovedCursorArg *p_arg) {
	DisplayServerSwitch *ds = static_cast<DisplayServerSwitch *>(get_singleton());
	if (p_arg->cursorPos < ds->swkbd_last_cursor) {
		ds->_send_key(Key::LEFT, 0);
	} else {
		ds->_send_key(Key::RIGHT, 0);
	}
	ds->swkbd_last_cursor = p_arg->cursorPos;
}

void DisplayServerSwitch::_swkbd_decided_enter(const char *p_str, SwkbdDecidedEnterArg *p_arg) {
	DisplayServerSwitch *ds = static_cast<DisplayServerSwitch *>(get_singleton());
	ds->_send_key(Key::ENTER, 0);
	ds->swkbd_open = false;
}

void DisplayServerSwitch::_swkbd_decided_cancel() {
	DisplayServerSwitch *ds = static_cast<DisplayServerSwitch *>(get_singleton());
	ds->swkbd_open = false;
}

void DisplayServerSwitch::_initialize_swkbd() {
	if (R_FAILED(swkbdInlineCreate(&inline_keyboard))) {
		return;
	}

	Result res;
	int applet_type = appletGetAppletType();
	if (applet_type == AppletType_Application || applet_type == AppletType_SystemApplication) {
		res = swkbdInlineLaunch(&inline_keyboard);
	} else {
		res = swkbdInlineLaunchForLibraryApplet(&inline_keyboard, SwkbdInlineMode_AppletDisplay, 0);
	}
	if (R_FAILED(res)) {
		swkbdInlineClose(&inline_keyboard);
		return;
	}

	swkbdInlineSetChangedStringCallback(&inline_keyboard, _swkbd_string_changed);
	swkbdInlineSetMovedCursorCallback(&inline_keyboard, _swkbd_moved_cursor);
	swkbdInlineSetDecidedEnterCallback(&inline_keyboard, _swkbd_decided_enter);
	swkbdInlineSetDecidedCancelCallback(&inline_keyboard, _swkbd_decided_cancel);

	swkbd_created = true;
}

void DisplayServerSwitch::virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect, DisplayServerEnums::VirtualKeyboardType p_type, int p_max_length, int p_cursor_start, int p_cursor_end) {
	if (!swkbd_created || swkbd_open) {
		return;
	}

	SwkbdType type = SwkbdType_Normal;
	switch (p_type) {
		case DisplayServerEnums::KEYBOARD_TYPE_NUMBER:
		case DisplayServerEnums::KEYBOARD_TYPE_NUMBER_DECIMAL:
		case DisplayServerEnums::KEYBOARD_TYPE_PHONE:
			type = SwkbdType_NumPad;
			break;
		default:
			type = SwkbdType_Normal;
			break;
	}

	SwkbdAppearArg appear_arg;
	swkbdInlineMakeAppearArg(&appear_arg, type);
	if (p_max_length > 0) {
		appear_arg.stringLenMax = p_max_length;
	}

	CharString existing = p_existing_text.utf8();
	swkbdInlineSetInputText(&inline_keyboard, existing.get_data());
	int cursor = p_cursor_start >= 0 ? p_cursor_start : p_existing_text.length();
	swkbdInlineSetCursorPos(&inline_keyboard, cursor);
	swkbd_last_cursor = cursor;

	// Eat the appear + set-text string events.
	swkbd_eat_string_events = 2;

	swkbdInlineAppear(&inline_keyboard, &appear_arg);
	swkbd_open = true;
}

void DisplayServerSwitch::virtual_keyboard_hide() {
	if (!swkbd_created || !swkbd_open) {
		return;
	}
	swkbdInlineDisappear(&inline_keyboard);
	swkbd_open = false;
}

int DisplayServerSwitch::virtual_keyboard_get_height() const {
	if (!swkbd_open) {
		return 0;
	}
	// The inline keyboard covers roughly the bottom 40% of the screen.
	return window_get_size().height * 2 / 5;
}

void DisplayServerSwitch::process_events() {
	if (operation_mode_dirty) {
		_update_operation_mode();
	}
	if (swkbd_created) {
		swkbdInlineUpdate(&inline_keyboard, nullptr);
	}
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

	EGLint num_configs = 0;
	eglChooseConfig(egl_display, attribute_list, &egl_config, 1, &num_configs);
	if (num_configs == 0) {
		ERR_PRINT(vformat("No EGL config found. Error: %d", eglGetError()));
		_finalize_egl();
		return ERR_UNAVAILABLE;
	}

	// Size the native window for the current operation mode up front.
	NWindow *win = nwindowGetDefault();
	if (appletGetOperationMode() == AppletOperationMode_Console) {
		nwindowSetDimensions(win, 1920, 1080);
	} else {
		nwindowSetDimensions(win, 1280, 720);
	}

	egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)win, nullptr);
	if (egl_surface == EGL_NO_SURFACE) {
		ERR_PRINT(vformat("EGL surface creation failed. Error: %d", eglGetError()));
		_finalize_egl();
		return ERR_UNAVAILABLE;
	}

	static const EGLint context_attribute_list[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};

	egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribute_list);
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

	appletHook(&applet_hook_cookie, _applet_hook, this);

	_initialize_swkbd();

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	r_error = OK;
}

DisplayServerSwitch::~DisplayServerSwitch() {
	if (swkbd_created) {
		swkbdInlineClose(&inline_keyboard);
		swkbd_created = false;
	}

	appletUnhook(&applet_hook_cookie);

	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}

	_finalize_egl();
}
