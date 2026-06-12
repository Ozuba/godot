/**************************************************************************/
/*  display_server_switch.h                                               */
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

#pragma once

#include "switch_wrapper.h"

#include "core/input/input.h"
#include "servers/display/display_server.h"

#ifdef GLES3_ENABLED
#include <EGL/egl.h>
#endif

class NativeMenu;
#ifdef RD_ENABLED
class RenderingContextDriver;
class RenderingDevice;
#endif

class DisplayServerSwitch : public DisplayServer {
	GDSOFTCLASS(DisplayServerSwitch, DisplayServer);

	friend class DisplayServer;

	static Vector<String> get_rendering_drivers_func();
	static DisplayServer *create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error);

	static void _dispatch_input_events(const Ref<InputEvent> &p_event);
	void _dispatch_input_event(const Ref<InputEvent> &p_event);

#ifdef GLES3_ENABLED
	Error _initialize_egl();
	void _finalize_egl();
#endif
#ifdef VULKAN_ENABLED
	Error _initialize_vulkan(DisplayServerEnums::VSyncMode p_vsync_mode);
	void _resize_vulkan_window(const Size2i &p_size);
#endif

	void _process_touch();

	static void _applet_hook(AppletHookType p_hook, void *p_param);
	void _update_operation_mode();

	void _initialize_swkbd();
	void _send_key(Key p_key, char32_t p_unicode);

	static void _swkbd_string_changed(const char *p_str, SwkbdChangedStringArg *p_arg);
	static void _swkbd_moved_cursor(const char *p_str, SwkbdMovedCursorArg *p_arg);
	static void _swkbd_decided_enter(const char *p_str, SwkbdDecidedEnterArg *p_arg);
	static void _swkbd_decided_cancel();

	NativeMenu *native_menu = nullptr;
	Callable input_event_callback;
	Callable rect_changed_callback;

	String rendering_driver;

#ifdef GLES3_ENABLED
	EGLDisplay egl_display = EGL_NO_DISPLAY;
	EGLSurface egl_surface = EGL_NO_SURFACE;
	EGLContext egl_context = EGL_NO_CONTEXT;
	EGLConfig egl_config = nullptr;
#endif

#ifdef RD_ENABLED
	RenderingContextDriver *rendering_context = nullptr;
	RenderingDevice *rendering_device = nullptr;
	Size2i rd_window_size;
#endif

	DisplayServerEnums::VSyncMode vsync_mode = DisplayServerEnums::VSYNC_ENABLED;

	AppletHookCookie applet_hook_cookie;
	bool operation_mode_dirty = false;

	SwkbdInline inline_keyboard;
	bool swkbd_created = false;
	bool swkbd_open = false;
	int swkbd_eat_string_events = 0;
	u32 swkbd_last_len = 0;
	s32 swkbd_last_cursor = 0;

	// Maximum of 16 touch points reported by HID.
	int last_touch_count = 0;
	Vector2 last_touch_pos[16];

public:
	bool has_feature(DisplayServerEnums::Feature p_feature) const override;
	String get_name() const override { return "Switch"; }

	int get_screen_count() const override { return 1; }
	int get_primary_screen() const override { return 0; }
	Point2i screen_get_position(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return Point2i(); }
	Size2i screen_get_size(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	Rect2i screen_get_usable_rect(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return Rect2i(Point2i(), screen_get_size(p_screen)); }
	int screen_get_dpi(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return 236; }
	float screen_get_scale(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return 1; }
	float screen_get_max_scale() const override { return 1; }
	float screen_get_refresh_rate(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return 60.0f; }
	void screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) override {}
	void screen_set_keep_on(bool p_enable) override {}

	Vector<DisplayServerEnums::WindowID> get_window_list() const override {
		Vector<DisplayServerEnums::WindowID> list;
		list.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
		return list;
	}

	DisplayServerEnums::WindowID create_sub_window(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Rect2i &p_rect = Rect2i(), bool p_exclusive = false, DisplayServerEnums::WindowID p_transient_parent = DisplayServerEnums::INVALID_WINDOW_ID) override { return 0; }
	void show_window(DisplayServerEnums::WindowID p_id) override {}
	void delete_sub_window(DisplayServerEnums::WindowID p_id) override {}

	DisplayServerEnums::WindowID get_window_at_screen_position(const Point2i &p_position) const override { return DisplayServerEnums::MAIN_WINDOW_ID; }

	void window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override { window_attached_instance_id = p_instance; }
	ObjectID window_get_attached_instance_id(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return window_attached_instance_id; }

	void window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {
		rect_changed_callback = p_callable;
	}

	void window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	void window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {
		input_event_callback = p_callable;
	}

	void window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	void window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	void window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	void window_set_mouse_passthrough(const Vector<Vector2> &p_region, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	int window_get_current_screen(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return 0; }
	void window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	Point2i window_get_position(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return Point2i(); }
	Point2i window_get_position_with_decorations(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return Point2i(); }
	void window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	void window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) override {}

	void window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	Size2i window_get_max_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return Size2i(); }

	void window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	Size2i window_get_min_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return Size2i(); }

	void window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	Size2i window_get_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	Size2i window_get_size_with_decorations(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return window_get_size(p_window); }

	void window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	DisplayServerEnums::WindowMode window_get_mode(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return DisplayServerEnums::WINDOW_MODE_FULLSCREEN; }

	void window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	DisplayServerEnums::VSyncMode window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const override { return vsync_mode; }

	bool window_is_maximize_allowed(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return false; }

	void window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	bool window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return false; }

	void window_request_attention(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	void window_set_taskbar_progress_value(float p_value, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	void window_set_taskbar_progress_state(DisplayServerEnums::ProgressState p_state, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	void window_move_to_foreground(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	bool window_is_focused(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return true; }

	bool window_can_draw(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return true; }

	bool can_any_window_draw() const override { return true; }

	void window_set_ime_active(const bool p_active, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	void window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	int64_t window_get_native_handle(DisplayServerEnums::HandleType p_handle_type, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;

	void process_events() override;

	void set_native_icon(const String &p_filename) override {}
	void set_icon(const Ref<Image> &p_icon) override {}

	void help_set_search_callbacks(const Callable &p_search_callback = Callable(), const Callable &p_action_callback = Callable()) override {}

	bool tts_is_speaking() const override { return false; }
	bool tts_is_paused() const override { return false; }
	TypedArray<Dictionary> tts_get_voices() const override { return TypedArray<Dictionary>(); }
	void tts_speak(const String &p_text, const String &p_voice, int p_volume = 50, float p_pitch = 1.0f, float p_rate = 1.0f, int64_t p_utterance_id = 0, bool p_interrupt = false) override {}
	void tts_pause() override {}
	void tts_resume() override {}
	void tts_stop() override {}

	void mouse_set_mode(DisplayServerEnums::MouseMode p_mode) override {}
	void mouse_set_mode_override(DisplayServerEnums::MouseMode p_mode) override {}
	void mouse_set_mode_override_enabled(bool p_override_enabled) override {}
	Point2i mouse_get_position() const override { return Point2i(); }
	void clipboard_set(const String &p_text) override {}
	void clipboard_set_primary(const String &p_text) override {}

	void virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect = Rect2(), DisplayServerEnums::VirtualKeyboardType p_type = DisplayServerEnums::KEYBOARD_TYPE_DEFAULT, int p_max_length = -1, int p_cursor_start = -1, int p_cursor_end = -1) override;
	void virtual_keyboard_hide() override;
	int virtual_keyboard_get_height() const override;

	void cursor_set_shape(DisplayServerEnums::CursorShape p_shape) override {}
	void cursor_set_custom_image(const Ref<Resource> &p_cursor, DisplayServerEnums::CursorShape p_shape = DisplayServerEnums::CURSOR_ARROW, const Vector2 &p_hotspot = Vector2()) override {}

	Error dialog_show(String p_title, String p_description, Vector<String> p_buttons, const Callable &p_callback) override { return ERR_UNAVAILABLE; }
	Error dialog_input_text(String p_title, String p_description, String p_partial, const Callable &p_callback) override { return ERR_UNAVAILABLE; }
	Error file_dialog_show(const String &p_title, const String &p_current_directory, const String &p_filename, bool p_show_hidden, DisplayServerEnums::FileDialogMode p_mode, const Vector<String> &p_filters, const Callable &p_callback, DisplayServerEnums::WindowID p_window_id) override { return ERR_UNAVAILABLE; }
	Error file_dialog_with_options_show(const String &p_title, const String &p_current_directory, const String &p_root, const String &p_filename, bool p_show_hidden, DisplayServerEnums::FileDialogMode p_mode, const Vector<String> &p_filters, const TypedArray<Dictionary> &p_options, const Callable &p_callback, DisplayServerEnums::WindowID p_window_id) override { return ERR_UNAVAILABLE; }

	void release_rendering_thread() override;
	void swap_buffers() override;

	DisplayServerEnums::IndicatorID create_status_indicator(const Ref<Texture2D> &p_icon, const String &p_tooltip, const Callable &p_callback) override { return DisplayServerEnums::INVALID_INDICATOR_ID; }
	void status_indicator_set_icon(DisplayServerEnums::IndicatorID p_id, const Ref<Texture2D> &p_icon) override {}
	void status_indicator_set_tooltip(DisplayServerEnums::IndicatorID p_id, const String &p_tooltip) override {}
	void status_indicator_set_callback(DisplayServerEnums::IndicatorID p_id, const Callable &p_callback) override {}
	void delete_status_indicator(DisplayServerEnums::IndicatorID p_id) override {}

	static void register_switch_driver();

	DisplayServerSwitch(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error);
	~DisplayServerSwitch();

private:
	ObjectID window_attached_instance_id;
};
