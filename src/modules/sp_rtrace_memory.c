/*
 * This file is part of sp-rtrace package.
 *
 * Copyright (C) 2010 by Nokia Corporation
 *
 * Contact: Eero Tamminen <eero.tamminen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include "config.h"

/**
 * @file sp-_rtrace_memory.c
 *
 * Memory tracing module (libsp-rtrace-memory.so) implementation.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#include <pthread.h>

#include "sp_rtrace_main.h"
#include "common/sp_rtrace_proto.h"
#include "debug_log.h"
#include "common/utils.h"

/* Module information */
static sp_rtrace_module_info_t module_info = {
		.type = MODULE_TYPE_PRELOAD,
		.version_major = 1,
		.version_minor = 0,
		.name = "memory",
		.description = "Memory allocation/deallocation tracing module. "
				       "Tracks calls of malloc, calloc, realloc, posix_memalign "
				       "and free functions.",
};


/* Internal allocation function emulation heap.
 *
 * The heap structure is:
 *   [size1][chunk1][size2][chunk2]...[sizeN][chunkN]
 * Where:
 *   sizeX - size of chunkX + 4 bytes of the sizeX value.
 *   chunkX - the allocated memory chunk X.
 */

#define EMU_HEAP_SIZE       1024 * 1024
#define EMU_HEAP_ALIGN      8
static char emu_heap[EMU_HEAP_SIZE] = { 0 };
static char* emu_heap_tail = emu_heap;

/* Target function prototypes */
typedef void* (*malloc_t)(size_t size);
typedef void* (*calloc_t)(size_t nmemb, size_t size);
typedef void* (*realloc_t)(void* ptr, size_t size);
typedef int (*posix_memalign_t)(void** ptr, size_t align, size_t size);
typedef void (*free_t)(void*);

/**
 * Target function references.
 */
typedef struct trace_t {
	malloc_t malloc;
	calloc_t calloc;
	realloc_t realloc;
	posix_memalign_t posix_memalign;
	free_t free;
} trace_t;

/* original function references */
static trace_t trace_off = { 0 };
/* tracing function references */
static trace_t trace_on;
/* target function emulation (local implementation) */
static trace_t trace_emu;

/* Runtime function references */
static trace_t* trace_rt = &trace_emu;

/**
 * Enables/disables tracing.
 *
 * @param[in] value   enable tracing if true, otherwise disable it.
 * @return
 */
static void enable_tracing(bool value)
{
	trace_rt = value ? &trace_on : &trace_off;
}

/**
 * Initializes original function references.
 *
 * @return
 */
static void trace_initialize()
{
	trace_off.malloc = (malloc_t) dlsym(RTLD_NEXT, "malloc");
	trace_off.free = (free_t) dlsym(RTLD_NEXT, "free");
	trace_off.calloc = (calloc_t) dlsym(RTLD_NEXT, "calloc");
	trace_off.realloc = (realloc_t) dlsym(RTLD_NEXT, "realloc");
	trace_off.posix_memalign = (posix_memalign_t) dlsym(RTLD_NEXT, "posix_memalign");

	enable_tracing(false);
}

/*
 * Emulation functions.
 */
static void* emu_alloc_mem(size_t size, size_t align)
{
	/* get unaligned address of the next free block */
	int chunk_size = *(int*)emu_heap_tail;
	char* new_ptr = emu_heap_tail + chunk_size + sizeof(int);
	/* adjust address to comply requested alignment */
	size_t offset = (unsigned long)new_ptr & (align - 1);
	if (offset) {
		new_ptr += align - offset;
	}

	/* check if the requested size is not outside internal heap limits */
	if (new_ptr + size >= emu_heap + EMU_HEAP_SIZE) {
		MSG_ERROR_CONST("ERROR: internal allocation emulation heap exceeded limits.\n");
		exit(-1);
	}
	/* point heap tail to the new chunk size value location */
	emu_heap_tail = new_ptr - sizeof(int);
	/* set the new chunk size value */
	*(int*) emu_heap_tail = size + sizeof(int);
	return new_ptr;
}


static void* emu_malloc(size_t size)
{
	return emu_alloc_mem(size, EMU_HEAP_ALIGN);
}

static void* emu_calloc(size_t nmemb, size_t size)
{
	int total_size = nmemb * size;

	char* memptr = emu_alloc_mem(total_size, EMU_HEAP_ALIGN);
	char* ptr = memptr;
	while (ptr - memptr < total_size)
		*ptr++ = 0;
	return memptr;
}

static int emu_posix_memalign(void **memptr, size_t alignment, size_t size)
{
	if (alignment % sizeof(void *) != 0 || ((alignment - 1) & alignment) || alignment == 0) {
		return EINVAL;
	}
	/* ensure that the returned pointer is also EMU_HEAP_ALIGN aligned */
	if (alignment < EMU_HEAP_ALIGN)
		alignment = EMU_HEAP_ALIGN;

	*memptr = emu_alloc_mem(size, alignment);
	return *memptr != NULL ? 0 : ENOMEM;
}

static void emu_free(void* ptr)
{
	/* can only free the last allocation */
	if (emu_heap_tail + 4 == ptr) {
		*(int*) emu_heap_tail = 0;
	}
}

static void* emu_realloc(void* ptr, size_t size)
{
	int chunk_size = 0;
	if (ptr) chunk_size = *(int*) (ptr - 4);

	emu_free(ptr);
	void* ptr_new = emu_alloc_mem(size, EMU_HEAP_ALIGN);

	/* if the allocated pointer is not the same as old pointer
	 * (in other words if the old pointer was not the last
	 * allocated pointer) copy the data.
	 */
	if (ptr_new && ptr_new != ptr) {
		if (size > chunk_size)
			size = chunk_size;

		char* dst_ptr = ptr_new;
		char* src_ptr = ptr;
		while ((long)dst_ptr < (long)ptr_new + size) {
			*dst_ptr++ = *src_ptr++;
		}
	}
	return ptr_new;
}

static trace_t trace_emu = {
	.malloc = emu_malloc,
	.calloc = emu_calloc,
	.realloc = emu_realloc,
	.posix_memalign = emu_posix_memalign,
	.free = emu_free,
};

/*
 * End of emulation functions
 */

/**
 * Check if the specified pointer is allocated on the internal heap.
 *
 * @param[in] ptr   the pointer to check.
 * @return          bool - the pointer is allocated on the internal heap.
 */
static bool is_in_internal_heap(void* ptr)
{
	return (char*) ptr >= emu_heap && (char*) ptr < emu_heap + EMU_HEAP_SIZE;
}

/*
 * Tracing functions
 */
static void* trace_malloc(size_t size)
{
	void* rc = trace_off.malloc(size);
	/* unlock backtrace after the original function has been called */
	backtrace_lock = 0;

	if (rc) {
		sp_rtrace_write_function_call(SP_RTRACE_FTYPE_ALLOC, "malloc", size, rc);
		sp_rtrace_store_heap_info();
	}
	return rc;
}

static void* trace_calloc(size_t nmemb, size_t size)
{
	void* rc = trace_off.calloc(nmemb, size);
	/* unlock backtrace after the original function has been called */
	backtrace_lock = 0;

	if (rc) {
		sp_rtrace_write_function_call(SP_RTRACE_FTYPE_ALLOC, "calloc", nmemb * size, rc);
		sp_rtrace_store_heap_info();
	}
	return rc;
}

static void* trace_realloc(void* ptr, size_t size)
{
	void* rc = trace_off.realloc(ptr, size);
	/* if allocation was successful or the requested size was 0,
	 *  and the old pointer was not NULL - register old pointer freeing */
	if ((rc || !size) && ptr) {
		sp_rtrace_write_function_call(SP_RTRACE_FTYPE_FREE, "realloc", 0, ptr);
	}
	/* if allocation was successful register new pointer allocation */
	if (rc) {
		sp_rtrace_write_function_call(SP_RTRACE_FTYPE_ALLOC, "realloc", size, rc);
		sp_rtrace_store_heap_info();
	}
	return rc;
}

static int trace_posix_memalign(void **memptr, size_t alignment, size_t size)
{
	int rc = trace_off.posix_memalign(memptr, alignment, size);
	if (rc == 0) {
		sp_rtrace_write_function_call(SP_RTRACE_FTYPE_ALLOC, "posix_memalign", size, *memptr);
		sp_rtrace_store_heap_info();
	}
	return rc;
}

static void trace_free(void* ptr)
{
	trace_off.free(ptr);
	/* unlock backtrace after the original function has been called */
	backtrace_lock = 0;

	sp_rtrace_write_function_call(SP_RTRACE_FTYPE_FREE, "free", 0, ptr);
	sp_rtrace_store_heap_info();
}

static trace_t trace_on = {
	.malloc = trace_malloc,
	.calloc = trace_calloc,
	.realloc = trace_realloc,
	.posix_memalign = trace_posix_memalign,
	.free = trace_free,
};

/*
 * Target functions.
 */

void* malloc(size_t size)
{
	/* synchronize allocation functions used by backtrace */
	int tid = pthread_self();
	if (backtrace_lock == tid) {
		return trace_off.malloc(size);
	}
	while (__sync_bool_compare_and_swap(&backtrace_lock, 0, tid));
	void* ptr = trace_rt->malloc(size);
	backtrace_lock = 0;
	return ptr;
}

void *calloc(size_t nmemb, size_t size)
{
	/* synchronize allocation functions used by backtrace */
	int tid = pthread_self();
	if (backtrace_lock == tid) {
		return trace_off.calloc(nmemb, size);
	}
	while (__sync_bool_compare_and_swap(&backtrace_lock, 0, tid));
	void* ptr = trace_rt->calloc(nmemb, size);
	backtrace_lock = 0;
	return ptr;
}

void* realloc(void* ptr, size_t size)
{
	if (is_in_internal_heap(ptr)) {
		void* ptr_new = trace_rt->malloc(size);
		if (ptr_new && ptr) {
			char* dptr = (char*) ptr_new;
			char* sptr = (char*) ptr;
			int chunk_size = *(int*)((char*)ptr - 4);
			if (size > chunk_size) size = chunk_size;
			while ((long)dptr < (long)ptr_new + size) {
				*dptr++ = *sptr++;
			}
			emu_free(ptr);
		}
		return ptr_new;
	}
	return trace_rt->realloc(ptr, size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	return trace_rt->posix_memalign(memptr, alignment, size);
}

void free(void* ptr)
{
	if (is_in_internal_heap(ptr)) {
		emu_free(ptr);
		return;
	}
	/* synchronize allocation functions used by backtrace */
	int tid = pthread_self();
	if (backtrace_lock == tid) {
		trace_off.free(ptr);
		return;
	}
	while (__sync_bool_compare_and_swap(&backtrace_lock, 0, tid));
	trace_rt->free(ptr);
	backtrace_lock = 0;
}

static void trace_memory_init(void) __attribute__((constructor));
static void trace_memory_fini(void) __attribute__((destructor));

static void trace_memory_init(void)
{
	LOG("initializing %s (%d.%d)", module_info.name, module_info.version_major, module_info.version_minor);
	trace_initialize();

	sp_rtrace_register_module(module_info.name, module_info.version_major, module_info.version_minor, enable_tracing);
}

static void trace_memory_fini(void)
{
	enable_tracing(false);
	LOG("fini");
}

/**
 * Gets module information data.
 *
 * @return  the module information data.
 */
const sp_rtrace_module_info_t* sp_rtrace_get_module_info()
{
	return &module_info;
}
