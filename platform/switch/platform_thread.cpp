/**************************************************************************/
/*  platform_thread.cpp                                                   */
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

#include "core/os/thread.h"

#include "core/error/error_macros.h"
#include "core/object/script_language.h"
#include "core/os/memory.h"
#include "core/string/ustring.h"

#include <sched.h>

SafeNumeric<uint64_t> Thread::id_counter(1); // The first value after .increment() is 2, hence by default the main thread ID should be 1.
thread_local Thread::ID Thread::caller_id = Thread::id_counter.increment();

Thread::PlatformFunctions Thread::platform_functions;

void Thread::_set_platform_functions(const PlatformFunctions &p_functions) {
	platform_functions = p_functions;
}

void Thread::yield() {
	sched_yield();
}

void Thread::callback(ID p_caller_id, const Settings &p_settings, Callback p_callback, void *p_userdata) {
	Thread::caller_id = p_caller_id;
	if (platform_functions.set_priority) {
		platform_functions.set_priority(p_settings.priority);
	}
	if (platform_functions.init) {
		platform_functions.init();
	}
	ScriptServer::thread_enter(); // Scripts may need to attach a stack.
	if (platform_functions.wrapper) {
		platform_functions.wrapper(p_callback, p_userdata);
	} else {
		p_callback(p_userdata);
	}
	ScriptServer::thread_exit();
	if (platform_functions.term) {
		platform_functions.term();
	}
}

struct ThreadStartData {
	Thread::ID id;
	Thread::Settings settings;
	Thread::Callback callback;
	void *userdata;
};

void *Thread::trampoline(void *p_data) {
	ThreadStartData *tsd = static_cast<ThreadStartData *>(p_data);
	Thread::callback(tsd->id, tsd->settings, tsd->callback, tsd->userdata);
	memdelete(tsd);
	return nullptr;
}

Thread::ID Thread::start(Thread::Callback p_callback, void *p_user, const Settings &p_settings) {
	ERR_FAIL_COND_V_MSG(id != UNASSIGNED_ID, UNASSIGNED_ID, "A Thread object has been re-started without wait_to_finish() having been called on it.");
	id = id_counter.increment();

	ThreadStartData *tsd = memnew(ThreadStartData);
	tsd->id = id;
	tsd->settings = p_settings;
	tsd->callback = p_callback;
	tsd->userdata = p_user;

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, STACK_SIZE);
	int err = pthread_create(&pthread, &attr, &Thread::trampoline, tsd);
	pthread_attr_destroy(&attr);

	if (err != 0) {
		memdelete(tsd);
		id = UNASSIGNED_ID;
		ERR_FAIL_V_MSG(UNASSIGNED_ID, vformat("pthread_create failed with error %d.", err));
	}
	return id;
}

bool Thread::is_started() const {
	return id != UNASSIGNED_ID;
}

void Thread::wait_to_finish() {
	ERR_FAIL_COND_MSG(id == UNASSIGNED_ID, "Attempt of waiting to finish on a thread that was never started.");
	ERR_FAIL_COND_MSG(id == get_caller_id(), "Threads can't wait to finish on themselves, another thread must wait.");
	pthread_join(pthread, nullptr);
	pthread = 0;
	id = UNASSIGNED_ID;
}

Error Thread::set_name(const String &p_name) {
	if (platform_functions.set_name) {
		return platform_functions.set_name(p_name);
	}

	return ERR_UNAVAILABLE;
}

Thread::~Thread() {
	if (id != UNASSIGNED_ID) {
#ifdef DEBUG_ENABLED
		WARN_PRINT(
				"A Thread object is being destroyed without its completion having been realized.\n"
				"Please call wait_to_finish() on it to ensure correct cleanup.");
#endif
		pthread_detach(pthread);
	}
}
