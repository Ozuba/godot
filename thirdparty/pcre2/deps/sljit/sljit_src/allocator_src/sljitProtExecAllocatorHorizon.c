/*
 *    Stack-less Just-In-Time compiler
 *
 *    Copyright Zoltan Herczeg (hzmester@freemail.hu). All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice, this list of
 *      conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright notice, this list
 *      of conditions and the following disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Executable allocator for Horizon OS (Nintendo Switch homebrew), built on
 * libnx's JIT API. The CodeMemory backend provides two simultaneously valid
 * aliases of the same physical pages: a writable one and an executable one,
 * matching the dual-mapping model of the NetBSD allocator.
 */

#include <switch/types.h>
#include <switch/result.h>
#include <switch/kernel/jit.h>

#define SLJIT_HAS_CHUNK_HEADER
#define SLJIT_HAS_EXECUTABLE_OFFSET

struct sljit_chunk_header {
	void *executable;
	Jit jit;
};

static SLJIT_INLINE struct sljit_chunk_header* alloc_chunk(sljit_uw size)
{
	struct sljit_chunk_header *header;
	Jit jit;

	/* jitCreate requires page aligned sizes. */
	size = (size + 0xFFFu) & ~(sljit_uw)0xFFFu;

	if (R_FAILED(jitCreate(&jit, size)))
		return NULL;

	/* Only the CodeMemory backend keeps the writable and executable
	   aliases mapped at the same time, which this allocator requires. */
	if (jit.type != JitType_CodeMemory) {
		jitClose(&jit);
		return NULL;
	}

	jitTransitionToExecutable(&jit);

	header = (struct sljit_chunk_header *)jitGetRwAddr(&jit);
	header->executable = jitGetRxAddr(&jit);
	header->jit = jit;

	return header;
}

static SLJIT_INLINE void free_chunk(void *chunk, sljit_uw size)
{
	struct sljit_chunk_header *header = ((struct sljit_chunk_header *)chunk) - 1;
	/* jitClose unmaps the aliases, including the header itself; keep a
	   copy on the stack. */
	Jit jit = header->jit;

	(void)size;

	jitClose(&jit);
}

#include "sljitExecAllocatorCore.c"
