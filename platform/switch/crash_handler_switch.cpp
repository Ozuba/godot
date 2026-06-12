/**************************************************************************/
/*  crash_handler_switch.cpp                                              */
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

#include "switch_logger.h"
#include "switch_wrapper.h"

#include <cstdio>

// Fatal exception handler: dump registers and a frame-pointer backtrace to
// the SD card with module-relative offsets, so they can be symbolized with
// aarch64-none-elf-addr2line against the matching .elf.

extern "C" {

alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
	// Get the buffered boot log onto the SD card before anything else; the
	// final lines before the fault are usually the interesting ones.
	switch_log_flush();

	FILE *f = fopen("sdmc:/godot_crash.log", "w");
	if (!f) {
		return;
	}

	// Module load base for ASLR relocation. (&__start__ folds to 0 here, so
	// ask the kernel for the .text region containing this function instead;
	// the NRO's text segment starts at the load base.)
	uintptr_t base = 0;
	MemoryInfo mem_info = {};
	u32 page_info = 0;
	if (R_SUCCEEDED(svcQueryMemory(&mem_info, &page_info, (u64)&__libnx_exception_handler))) {
		base = mem_info.addr;
	}

	fprintf(f, "Godot crash on Horizon\n");
	fprintf(f, "error_desc=0x%x base=0x%lx\n", ctx->error_desc, (unsigned long)base);
	// Runtime address of this function, to compute the ASLR slide host-side:
	// slide = handler_runtime - handler_elf_vaddr (from nm).
	fprintf(f, "handler=0x%lx\n", (unsigned long)(uintptr_t)&__libnx_exception_handler);
	fprintf(f, "pc=0x%lx (+0x%lx)\n", (unsigned long)ctx->pc.x, (unsigned long)(ctx->pc.x - base));
	fprintf(f, "lr=0x%lx (+0x%lx)\n", (unsigned long)ctx->lr.x, (unsigned long)(ctx->lr.x - base));
	fprintf(f, "sp=0x%lx fp=0x%lx far=0x%lx\n", (unsigned long)ctx->sp.x, (unsigned long)ctx->fp.x, (unsigned long)ctx->far.x);
	for (int i = 0; i < 29; i++) {
		fprintf(f, "x%d=0x%lx\n", i, (unsigned long)ctx->cpu_gprs[i].x);
	}

	// Walk the frame pointer chain. Be defensive: a bad frame pointer must
	// not fault inside the exception handler.
	fprintf(f, "backtrace:\n");
	u64 fp = ctx->fp.x;
	for (int depth = 0; depth < 32; depth++) {
		if (fp == 0 || (fp & 7) != 0 || fp < 0x1000) {
			break;
		}
		u64 *frame = (u64 *)fp;
		u64 next_fp = frame[0];
		u64 ret_lr = frame[1];
		if (ret_lr < base) {
			fprintf(f, "  #%d lr=0x%lx (external)\n", depth, (unsigned long)ret_lr);
		} else {
			fprintf(f, "  #%d lr=0x%lx (+0x%lx)\n", depth, (unsigned long)ret_lr, (unsigned long)(ret_lr - base));
		}
		if (next_fp <= fp) {
			break;
		}
		fp = next_fp;
	}

	fclose(f);
}

} // extern "C"
