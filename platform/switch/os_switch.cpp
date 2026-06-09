/**************************************************************************/
/*  os_switch.cpp                                                         */
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

#include "os_switch.h"

#include "display_server_switch.h"
#include "switch_wrapper.h"

#include "core/config/project_settings.h"
#include "core/os/main_loop.h"
#include "drivers/unix/dir_access_unix.h"
#include "drivers/unix/file_access_unix.h"
#include "drivers/unix/ip_unix.h"
#include "drivers/unix/net_socket_unix.h"
#include "drivers/unix/thread_posix.h"
#include "main/main.h"
#include "servers/audio/audio_server.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

void OS_Switch::initialize() {
	init_thread_posix();

	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_USERDATA);
	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_FILESYSTEM);
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_RESOURCES);
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_USERDATA);
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_FILESYSTEM);

	NetSocketUnix::make_default();
	IPUnix::make_default();
}

void OS_Switch::initialize_joypads() {
	joypad = memnew(JoypadSwitch(Input::get_singleton()));
}

void OS_Switch::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

void OS_Switch::delete_main_loop() {
	if (main_loop) {
		memdelete(main_loop);
	}
	main_loop = nullptr;
}

void OS_Switch::finalize() {
	if (joypad) {
		memdelete(joypad);
		joypad = nullptr;
	}
}

void OS_Switch::finalize_core() {
	NetSocketUnix::cleanup();
}

bool OS_Switch::_check_internal_feature_support(const String &p_feature) {
	if (p_feature == "mobile") {
		return true;
	}
	if (p_feature == "switch" || p_feature == "horizon") {
		return true;
	}
	if (p_feature == "arm64") {
		return true;
	}
	return false;
}

OS_Switch *OS_Switch::get_singleton() {
	return static_cast<OS_Switch *>(OS::get_singleton());
}

Vector<String> OS_Switch::get_video_adapter_driver_info() const {
	Vector<String> info;
	info.push_back("mesa_nouveau");
	return info;
}

String OS_Switch::get_stdin_string(int64_t p_buffer_size) {
	return String();
}

PackedByteArray OS_Switch::get_stdin_buffer(int64_t p_buffer_size) {
	return PackedByteArray();
}

Error OS_Switch::get_entropy(uint8_t *r_buffer, int p_bytes) {
	randomGet(r_buffer, p_bytes);
	return OK;
}

Error OS_Switch::execute(const String &p_path, const List<String> &p_arguments, String *r_pipe, int *r_exitcode, bool read_stderr, Mutex *p_pipe_mutex, bool p_open_console) {
	return ERR_UNAVAILABLE;
}

Error OS_Switch::create_process(const String &p_path, const List<String> &p_arguments, ProcessID *r_child_id, bool p_open_console) {
	return ERR_UNAVAILABLE;
}

Error OS_Switch::kill(const ProcessID &p_pid) {
	return ERR_UNAVAILABLE;
}

bool OS_Switch::is_process_running(const ProcessID &p_pid) const {
	return false;
}

int OS_Switch::get_process_exit_code(const ProcessID &p_pid) const {
	return -1;
}

bool OS_Switch::has_environment(const String &p_var) const {
	return getenv(p_var.utf8().get_data()) != nullptr;
}

String OS_Switch::get_environment(const String &p_var) const {
	const char *val = getenv(p_var.utf8().get_data());
	if (val) {
		return String::utf8(val);
	}
	return String();
}

void OS_Switch::set_environment(const String &p_var, const String &p_value) const {
	setenv(p_var.utf8().get_data(), p_value.utf8().get_data(), 1);
}

void OS_Switch::unset_environment(const String &p_var) const {
	unsetenv(p_var.utf8().get_data());
}

String OS_Switch::get_name() const {
	return "Switch";
}

String OS_Switch::get_distribution_name() const {
	return "Horizon";
}

String OS_Switch::get_version() const {
	return "1.0";
}

MainLoop *OS_Switch::get_main_loop() const {
	return main_loop;
}

OS::DateTime OS_Switch::get_datetime(bool p_utc) const {
	time_t t = time(nullptr);
	struct tm lt;
	if (p_utc) {
		gmtime_r(&t, &lt);
	} else {
		localtime_r(&t, &lt);
	}
	DateTime ret;
	ret.year = 1900 + lt.tm_year;
	ret.month = (Month)(lt.tm_mon + 1);
	ret.day = lt.tm_mday;
	ret.weekday = (Weekday)lt.tm_wday;
	ret.hour = lt.tm_hour;
	ret.minute = lt.tm_min;
	ret.second = lt.tm_sec;
	ret.dst = lt.tm_isdst;
	return ret;
}

OS::TimeZoneInfo OS_Switch::get_time_zone_info() const {
	TimeZoneInfo ret;
	ret.name = "UTC";
	ret.bias = 0;
	return ret;
}

void OS_Switch::delay_usec(uint32_t p_usec) const {
	svcSleepThread((int64_t)p_usec * 1000ll);
}

uint64_t OS_Switch::get_ticks_usec() const {
	static u64 tick_freq = armGetSystemTickFreq();
	return armGetSystemTick() / (tick_freq / 1000000);
}

String OS_Switch::get_executable_path() const {
	if (exec_path.is_empty()) {
		return OS::get_executable_path();
	}
	return exec_path;
}

void OS_Switch::set_executable_path(const char *p_execpath) {
	exec_path = String::utf8(p_execpath);
}

String OS_Switch::get_data_path() const {
	return "sdmc:/switch";
}

String OS_Switch::get_config_path() const {
	return get_data_path();
}

String OS_Switch::get_cache_path() const {
	return get_data_path();
}

String OS_Switch::get_user_data_dir(const String &p_user_dir) const {
	return get_data_path().path_join(get_godot_dir_name()).path_join("app_userdata").path_join(p_user_dir);
}

int OS_Switch::get_processor_count() const {
	// Three cores are available to applications (the fourth is reserved by the OS).
	return 3;
}

void OS_Switch::alert(const String &p_alert, const String &p_title) {
	printf("ALERT: %s: %s\n", p_title.utf8().get_data(), p_alert.utf8().get_data());
}

void OS_Switch::process_joypads() {
	if (joypad) {
		joypad->process();
	}
}

void OS_Switch::run() {
	if (!main_loop) {
		return;
	}

	main_loop->initialize();

	while (appletMainLoop()) {
		DisplayServer::get_singleton()->process_events();

		if (Main::iteration()) {
			break;
		}
	}

	main_loop->finalize();
}

OS_Switch::OS_Switch() {
	AudioDriverManager::add_driver(&audio_driver);
	DisplayServerSwitch::register_switch_driver();
}

OS_Switch::~OS_Switch() {
}
