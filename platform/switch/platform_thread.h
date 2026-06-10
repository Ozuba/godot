/**************************************************************************/
/*  platform_thread.h                                                     */
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

// pthread-based Thread implementation for Horizon, replacing the engine's
// std::thread one (see PLATFORM_THREAD_OVERRIDE in core/os/thread.h).
//
// std::thread cannot specify a stack size, and threads on libnx default to
// 128 KiB stacks - an order of magnitude smaller than on desktop platforms.
// Deeply recursive engine code (e.g. Jolt's narrowphase running on
// WorkerThreadPool threads) overflows that, crashing with a data abort at
// the stack guard. pthread_create on libnx honors the attribute stack size,
// so every engine thread is given a desktop-like stack instead.

#include "core/error/error_list.h"
#include "core/templates/safe_refcount.h"
#include "core/typedefs.h"

#ifndef THREADS_ENABLED
#error The Switch platform requires threads.
#endif

#include <pthread.h>

class String;

class Thread {
public:
	typedef void (*Callback)(void *p_userdata);

	typedef uint64_t ID;

	enum : ID {
		UNASSIGNED_ID = 0,
		MAIN_ID = 1
	};

	enum Priority {
		PRIORITY_LOW,
		PRIORITY_NORMAL,
		PRIORITY_HIGH
	};

	struct Settings {
		Priority priority;
		Settings() { priority = PRIORITY_NORMAL; }
	};

	struct PlatformFunctions {
		Error (*set_name)(const String &) = nullptr;
		void (*set_priority)(Thread::Priority) = nullptr;
		void (*init)() = nullptr;
		void (*wrapper)(Thread::Callback, void *) = nullptr;
		void (*term)() = nullptr;
	};

	// At a negligible memory cost, we use a conservatively high value.
	static constexpr size_t CACHE_LINE_BYTES = 128;

	static constexpr size_t STACK_SIZE = 2 * 1024 * 1024;

private:
	friend class Main;

	static PlatformFunctions platform_functions;

	ID id = UNASSIGNED_ID;
	pthread_t pthread = 0;

	static SafeNumeric<uint64_t> id_counter;
	static thread_local ID caller_id;

	static void *trampoline(void *p_data);
	static void callback(ID p_caller_id, const Settings &p_settings, Callback p_callback, void *p_userdata);

	static void make_main_thread() { caller_id = MAIN_ID; }
	static void release_main_thread() { caller_id = id_counter.increment(); }

public:
	static void _set_platform_functions(const PlatformFunctions &p_functions);

	static void yield();

	_FORCE_INLINE_ ID get_id() const { return id; }
	// get the ID of the caller thread
	_FORCE_INLINE_ static ID get_caller_id() {
		return caller_id;
	}
	// get the ID of the main thread
	_FORCE_INLINE_ static ID get_main_id() { return MAIN_ID; }

	_FORCE_INLINE_ static bool is_main_thread() { return caller_id == MAIN_ID; }

	static Error set_name(const String &p_name);

	ID start(Thread::Callback p_callback, void *p_user, const Settings &p_settings = Settings());
	bool is_started() const;
	///< waits until thread is finished, and deallocates it.
	void wait_to_finish();

	~Thread();
};
