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

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "writer.h"
#include "filter.h"
#include "leaks_sort.h"
#include "sp_rtrace_postproc.h"

#include "common/formatter.h"
#include "common/header.h"

#define TRY(x) {\
	int rc = x;\
	if (rc < 0) {\
		fprintf(stderr, "Error while writing output data (%s)\n", strerror(-rc));\
		exit (-1);\
	}\
}

/**
 * Writes module information log record into text log.
 *
 * @param[in] minfo   the module information data.
 * @param[in] formatter  the log file handle.
 * @return
 */
static int write_module_info(rd_minfo_t* minfo, FILE* fp)
{
	TRY(formatter_printf(fp, "## tracing module: [%x] %s (%d.%d)\n", minfo->id, minfo->name, minfo->vmajor, minfo->vminor));
	return 0;
}


/**
 * Range function for dlist_foreach2_in.
 *
 * @param[in] comment   the comment to check
 * @param[in] data      the current call index.
 * @return              true if the comment index is less than the current call index.
 */
static long comment_check_index(rd_comment_t* comment, void* data)
{
	long index = (long)data;
	return comment->index < index;
}

/**
 * Writes function call data into text log.
 *
 * This function writes full function call data including backtrace
 * and function arguments (TODO).
 * @param[in] call   the function call data.
 * @param[in] fmt    the formatter data.
 * @return
 */
static int write_function_call(rd_fcall_t* call, fmt_data_t* fmt)
{
	/* first write all comments with index less than current call index */
	if (fmt->comment) {
		fmt->comment = dlist_foreach2_in(&fmt->rd->comments, fmt->comment,
				(op_binary_t)comment_check_index, (void*)(long)call->index, (op_binary_t)formatter_write_comment, fmt->fp);
	}

	/* then write the function call itself */
	TRY(formatter_write_fcall(call, fmt->fp));
	if (call->args) TRY(formatter_write_fargs(call->args, fmt->fp));
	if (call->trace) TRY(formatter_write_ftrace(call->trace, fmt->fp));
	return 0;
}

/**
 * Writes compressed function call record into text log.
 *
 * This function writes only the function call record without
 * the backtrace. However arguments must still be written (TODO).
 * @param[in] call   the function call data.
 * @param[in] fmt    the formatter data.
 * @return
 */
static int write_compressed_function_call(ref_node_t* node, fmt_data_t* fmt)
{
	rd_fcall_t* call = (rd_fcall_t*)node->ref;

	/* first write all comments with index less than current call index */
	if (fmt->comment) {
		fmt->comment = dlist_foreach2_in(&fmt->rd->comments, fmt->comment,
				(op_binary_t)comment_check_index, (void*)(long)call->index, (op_binary_t)formatter_write_comment, fmt->fp);
	}

	/* then write the function call itself */
	TRY(formatter_write_fcall(call, fmt->fp));
	if (call->args) TRY(formatter_write_fargs(call->args, fmt->fp));
	return 0;
}


/**
 * Writes compressed backtrace data into text log.
 *
 * The compressed backtrace data consists of all associated function data
 * plus the backtrace itself. Basically it means grouping function calls
 * by backtraces.
 * @param[in] trace   the backtrace data.
 * @param[in] fmt     the log file handle.
 * @return
 */
static int write_compressed_backtrace(ftrace_ref_t* trace, fmt_data_t* fmt)
{
	dlist_foreach2(&trace->ref->calls, (op_binary_t)write_compressed_function_call, fmt);
	TRY(formatter_printf(fmt->fp, "# allocation summary: %d block(s) with total size %d\n",
            trace->leak_count, trace->leak_size));
	TRY(formatter_write_ftrace(trace->ref, fmt->fp));
	return 0;
}

/**
 * Writes heap statistics information.
 *
 * @param[in] fp     the output stream.
 * @param[in] hinfo  the heap information data.
 * @return
 */
static void write_heap_information(FILE* fp, rd_hinfo_t* hinfo)
{
	TRY(formatter_printf(fp, "## heap status information:\n"));
	TRY(formatter_printf(fp, "##   heap bottom 0x%lx\n", hinfo->heap_bottom));
	TRY(formatter_printf(fp, "##   heap top 0x%lx\n", hinfo->heap_top));
	TRY(formatter_printf(fp, "##   lowest block 0x%lx\n", hinfo->lowest_block));
	TRY(formatter_printf(fp, "##   highest block 0x%lx\n", hinfo->highest_block));
	TRY(formatter_printf(fp, "##   non-mapped space allocated from system %d\n", hinfo->arena));
	TRY(formatter_printf(fp, "##   number of free chunks %d\n", hinfo->ordblks));
	TRY(formatter_printf(fp, "##   number of fastbin blocks %d\n", hinfo->smblks));
	TRY(formatter_printf(fp, "##   number of mapped regions %d\n", hinfo->hblks));
	TRY(formatter_printf(fp, "##   space in mapped regions %d\n", hinfo->hblkhd));
	TRY(formatter_printf(fp, "##   maximum total allocated space %d\n", hinfo->usmblks));
	TRY(formatter_printf(fp, "##   space available in freed fastbin blocks %d\n", hinfo->fsmblks));
	TRY(formatter_printf(fp, "##   total allocated space %d\n", hinfo->uordblks));
	TRY(formatter_printf(fp, "##   total free space %d\n", hinfo->fordblks));
	TRY(formatter_printf(fp, "##   top-most, releasable (via malloc_trim) space %d\n", hinfo->keepcost));
}

typedef struct {
	FILE* fp;
	leak_data_t leaks[32];
} leaks_t;

static void write_leaks(rd_resource_t* res, leaks_t* leaks)
{
	TRY(formatter_printf(leaks->fp, "# Resource - %s (%s):\n", res->type, res->desc));
	int index = ffs(res->id) - 1;
	TRY(formatter_printf(leaks->fp, "# %d block(s) leaked with total size of %d bytes\n", leaks->leaks[index].count, leaks->leaks[index].total_size));

}

/*
 * Public API
 */

void write_leak_summary(fmt_data_t* fmt)
{
	leaks_t leaks = {.fp = fmt->fp};
	dlist_foreach2(&fmt->rd->calls, (op_binary_t)filter_sum_leaks, leaks.leaks);

	dlist_foreach2(&fmt->rd->resources, (op_binary_t)write_leaks, &leaks);
}


void write_trace_environment(fmt_data_t* fmt)
{
	/* prepare version and timestamp strings */
	char version[8], timestamp[64], spid[8], btdepth[16];
	sprintf(spid, "%d", fmt->rd->pinfo->pid);
	sprintf(version, "%d.%d", fmt->rd->hshake->vmajor, fmt->rd->hshake->vminor);
	struct tm* tm = localtime(&fmt->rd->pinfo->timestamp.tv_sec);
	sprintf(timestamp, "%d.%d.%d %02d:%02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	sprintf(btdepth, "%d", fmt->rd->pinfo->backtrace_depth);
	
	header_t header =  {
			.fields = { 
					version,                // HEADER_VERSION
					fmt->rd->hshake->arch,  // HEADER_ARCH 
					timestamp,              // HEADER_TIMESTAMP
					fmt->rd->pinfo->name,   // HEADER_PROCESS
					spid,                   // HEADER_PID
					NULL,                    // HEADER_FILTER
					fmt->rd->pinfo->backtrace_depth == -1 ? NULL : btdepth, // HEADER_BACKTRACE_DEPTH
			},
	};
	unsigned int filter = fmt->rd->filter;
	/* reset filter mask by leaving only permanent filters (leaks, resolve) */ 
	filter &= (FILTER_MASK_RESET);
	/* set filter mask according to options */
	if (postproc_options.compress) filter |= FILTER_MASK_COMPRESS;
	if (postproc_options.filter_leaks) filter |= FILTER_MASK_LEAKS;
	header_set_filter(&header, filter);
	
	/* write header data */
	TRY(formatter_write_header(&header, fmt->fp));

	/* write heap information if exists */
	if (fmt->rd->hinfo) write_heap_information(fmt->fp, fmt->rd->hinfo);

	/* write tracing module data */
	dlist_foreach2(&fmt->rd->minfo, (op_binary_t)write_module_info, fmt->fp);

	/* write context registry */
	dlist_foreach2(&fmt->rd->contexts, (op_binary_t)formatter_write_context, fmt->fp);

	/* write resource registry */
	dlist_foreach2(&fmt->rd->resources, (op_binary_t)formatter_write_resource, fmt->fp);

	/* write memory mapping data */
	dlist_foreach2(&fmt->rd->mmaps, (op_binary_t)formatter_write_mmap, fmt->fp);
}

void write_trace_calls(fmt_data_t* fmt)
{
	/* write the function call data (with backtraces and arguments) */
	if (postproc_options.compress) {
		dlist_t leaks;
		dlist_init(&leaks);
		leaks_sort(&fmt->rd->ftraces, &leaks, postproc_options.compare_leaks);
		dlist_foreach2(&leaks, (op_binary_t)write_compressed_backtrace, fmt);
		dlist_free(&leaks, (op_unary_t)free);
	}
	else {
		dlist_foreach2(&fmt->rd->calls, (op_binary_t)write_function_call, fmt);
	}

	if (fmt->comment) {
		fmt->comment = dlist_foreach2_in(&fmt->rd->comments, fmt->comment,
				(op_binary_t)comment_check_index, (void*)(long)LONG_MAX, (op_binary_t)formatter_write_comment, fmt->fp);
	}
}
