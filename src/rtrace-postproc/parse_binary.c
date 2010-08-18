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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "sp_rtrace_postproc.h"
#include "common/sp_rtrace_proto.h"

#include "parse_binary.h"
#include "debug_log.h"
#include "common/utils.h"

/* the read buffer size */
#define BUFFER_SIZE			4096

/* the current function call index */
static int call_index = 1;

/**
 * Reads handshake packet.
 *
 * @param[in] data   the binary data.
 * @param[in] size   the data size.
 * @return           the number of bytes processed.
 */
static rd_hshake_t* read_handshake_packet(rd_t* rd, const char* data, int size)
{
	/* reset the function call index as handshake packet means parsing new data */
	call_index = 1;
    /**/
	rd_hshake_t* hs = (rd_hshake_t*)malloc_a(sizeof(rd_hshake_t));
	unsigned char len;
	data += read_byte(data, &hs->vmajor);
	data += read_byte(data, &hs->vminor);
	data += read_byte(data, &len);
	hs->arch = malloc_a(len + 1);
	memcpy(hs->arch, data, len);
	hs->arch[len] = '\0';
	data += len;
	data += read_byte(data, &hs->endianness);
	read_byte(data, &hs->pointer_size);
	return hs;
}

/**
 * Reads context registry packet.
 *
 * @param[in] data   the binary data.
 * @param[in] size   the data size.
 * @return           the context registry record.
 */
static rd_context_t* read_packet_CT(const char* data, int size)
{
	rd_context_t* context = (rd_context_t*)dlist_create_node(sizeof(rd_context_t));
	data += read_dword(data, &context->id);
	read_stringa(data, &context->name);
	return context;
}

/**
 * Reads memory mapping packet.
 *
 * @param[in] data   the binary data.
 * @param[in] size   the data size.
 * @return           the memory mapping record.
 */
static rd_mmap_t* read_packet_MM(const char* data, int size)
{
	rd_mmap_t* fmap = (rd_mmap_t*)dlist_create_node(sizeof(rd_mmap_t));
	data += read_pointer(data, &fmap->from);
	data += read_pointer(data, &fmap->to);
	read_stringa(data, &fmap->module);
	return fmap;
}

/**
 * Reads process info packet.
 *
 * @param[in] data   the binary data.
 * @param[in] size   the data size.
 * @return           the process info record.
 */
static rd_pinfo_t* read_packet_PI(const char* data, int size)
{
	rd_pinfo_t* info = (rd_pinfo_t*)malloc_a(sizeof(rd_pinfo_t));
	data += read_dword(data, &info->pid);

	unsigned int sec, usec;
	data += read_dword(data, &sec);
	data += read_dword(data, &usec);
	info->timestamp.tv_sec = sec;
	info->timestamp.tv_usec = usec;

	read_stringa(data, &info->name);

	return info;
}

/**
 * Reads module info packet.
 *
 * @param[in] data   the binary data.
 * @param[in] size   the data size.
 * @return           the module info record.
 */
static rd_minfo_t* read_packet_MI(const char* data, int size)
{
	rd_minfo_t* info = (rd_minfo_t*)dlist_create_node(sizeof(rd_minfo_t));
    data += read_byte(data, &info->vmajor);
    data += read_byte(data, &info->vminor);
    read_stringa(data, &info->name);
	return info;
}

/**
 * Reads function call packet.
 *
 * @param[in] data   the binary data.
 * @param[in] size   the data size.
 * @return           the function call record.
 */
static rd_fcall_t* read_packet_FC(const char* data, int size)
{
    rd_fcall_t* call = (rd_fcall_t*)dlist_create_node(sizeof(rd_fcall_t));
    call->index = call_index++;
    data += read_dword(data, &call->context);
    data += read_dword(data, &call->timestamp);
    data += read_byte(data, &call->type);
    data += read_stringa(data, &call->name);
    data += read_dword(data, (unsigned int*)&call->res_size);
    read_pointer(data, &call->res_id);
    call->trace = NULL;
    call->args = NULL;
    call->ref = NULL;
    return call;
}


/**
 * Reads function trace packet.
 *
 * @param[in] data   the binary data.
 * @param[in] size   the data size.
 * @return           the function trace record.
 */
static rd_ftrace_t* read_packet_BT(const char* data, int size)
{
    rd_ftrace_t* trace = (rd_ftrace_t*)htable_create_node(sizeof(rd_ftrace_t));
    trace->ref_count = 0;
    data += read_word(data, &trace->nframes);
    trace->frames = (void**)malloc_a(sizeof(void*) * trace->nframes);
    memcpy(trace->frames, data, sizeof(void*) * trace->nframes);
    /* binary packets can't contain resolved address names */
    trace->resolved_names = NULL;

    dlist_init(&trace->calls);
    return trace;
}

static rd_hinfo_t* read_packet_HI(const char* data, int size)
{
	rd_hinfo_t* hinfo = (rd_hinfo_t*)calloc_a(1, sizeof(rd_hinfo_t));

	data += read_pointer(data, &hinfo->heap_bottom);
	data += read_pointer(data, &hinfo->heap_top);
	data += read_dword(data, (unsigned int*)&hinfo->arena);
	data += read_dword(data, (unsigned int*)&hinfo->ordblks);
	data += read_dword(data, (unsigned int*)&hinfo->smblks);
	data += read_dword(data, (unsigned int*)&hinfo->hblks);
	data += read_dword(data, (unsigned int*)&hinfo->hblkhd);
	data += read_dword(data, (unsigned int*)&hinfo->usmblks);
	data += read_dword(data, (unsigned int*)&hinfo->fsmblks);
	data += read_dword(data, (unsigned int*)&hinfo->uordblks);
	data += read_dword(data, (unsigned int*)&hinfo->fordblks);
	data += read_dword(data, (unsigned int*)&hinfo->keepcost);

	return hinfo;
}

/**
 * Reads generic packet.
 *
 * @param[in] data   the binary data.
 * @param[in] size   the data size.
 * @return           the number of bytes processed.
 */
static int read_generic_packet(rd_t* rd, const char* data, int size)
{
	/* first check if the packet contains enough data to read size value */
	if (size < 2) return -1;

	static rd_fcall_t* fcall_prev = NULL;
	unsigned short len, type;

	/* check if the buffer has at least one full packet */
	int offset = read_word(data, &len);
	len += offset;
	if (len > size) {
		return -1;
	}
	/* read packet type */
	offset += read_word(data + offset, &type);
	data += offset;
	int data_len = len - offset;

	/* process packet depending on its type */
	switch (type) {
		case SP_RTRACE_PROTO_MEMORY_MAP: {
			dlist_add(&rd->mmaps, read_packet_MM(data, data_len));
			break;
		}
		case SP_RTRACE_PROTO_CONTEXT_REGISTRY: {
			dlist_add(&rd->contexts, read_packet_CT(data, data_len));
			break;
		}
		case SP_RTRACE_PROTO_FUNCTION_CALL: {
            fcall_prev = read_packet_FC(data, data_len);
            dlist_add(&rd->calls, fcall_prev);
            break;
        }
		case SP_RTRACE_PROTO_BACKTRACE: {
            rd_ftrace_t* trace = read_packet_BT(data, data_len);
			/* check if function call record for this backtrace has been processed.
			 * It should have been a record processed right before this one.
			 */
			if (fcall_prev) {
				rd_fcall_set_ftrace(rd, fcall_prev, trace);
			}
			else {
				fprintf(stderr, "WARNING: a backtrace packet did not follow function call packet\n");
			}
            break;
		}
		case SP_RTRACE_PROTO_FUNCTION_ARGS: {
			/* TODO: */
			break;
		}
		case SP_RTRACE_PROTO_PROCESS_INFO: {
			rd->pinfo = read_packet_PI(data, data_len);
			break;
		}
		case SP_RTRACE_PROTO_MODULE_INFO: {
			dlist_add(&rd->minfo, read_packet_MI(data, data_len));
			break;
		}
		case SP_RTRACE_PROTO_HEAP_INFO: {
			rd->hinfo = read_packet_HI(data, data_len);
			break;
		}
		default:
			break;
	}
	if (type != SP_RTRACE_PROTO_FUNCTION_CALL) {
	    fcall_prev = NULL;
	}
	return len;
}

/**
 * Read data from the specified file descriptor and process it.
 *
 * @param[out] rd   the resource trace data.
 * @param[in] fd    the data source file descriptor.
 * @return
 */
static void read_binary_data(rd_t* rd, int fd)
{
	char buffer[BUFFER_SIZE * 2], *ptr_in = buffer;
	int n, data_len, size;

	/* read and process the handshake packet */
	n = read(fd, buffer, BUFFER_SIZE - 1);
	data_len = *(unsigned char*)ptr_in++;
	if (data_len >= n || (rd->hshake = read_handshake_packet(rd, ptr_in, data_len)) == NULL) {
		/* A handshake packet fragmentation is a sign of error,
		 * as the handshake packet size is less than 256 bytes
		 * and it's the first packet written into pipe. So in
		 * theory it can't be fragmented.
		 */
		fprintf(stderr, "ERROR: handshaking packet processing failed\n");
		exit (-1);
	}
	/* check for architecture compatibility */
	short endian = 0x0100;
	char endianness = *(char*)&endian;
	if (rd->hshake->endianness != endianness || sizeof(void*) != rd->hshake->pointer_size) {
		fprintf(stderr, "ERROR: unsupported architecture: endianess(%d:%d), pointer size(%d:%d)\n",
				rd->hshake->endianness, endianness, rd->hshake->pointer_size, (int)sizeof(void*));
		fprintf(stderr, "This could happen when text file is being processed without correct format option.\n");
	    exit (-1);
	}
	if (strcmp(rd->hshake->arch, BUILD_ARCH)) {
		fprintf(stderr, "ERROR: unsupported architecture: %s (expected %s)\n",
				rd->hshake->arch, BUILD_ARCH);
	    exit (-1);
	}
	n -= data_len + 1;
	ptr_in += data_len;

	/* main packet reading/processing loop */
	while (true) {
		/* read packets from the buffer */
		while (true) {
			size = read_generic_packet(rd, ptr_in, n);
			if (size <= 0) break;
			ptr_in += size;
			n -= size;
		}

		/* move the incomplete packet to the beginning of buffer */
		memmove(buffer, ptr_in, n);
		/* read new data chunk into buffer */
		int nbytes = read(fd, buffer + n, BUFFER_SIZE - 1);
		if (nbytes <= 0) break;
		n += nbytes;
		ptr_in = buffer;
	}
}

/*
 * Public API implementation.
 */
void process_binary_data(rd_t* rd, int fd)
{
	read_binary_data(rd, fd);

	if (postproc_options.input_file) {
		close(fd);
	}
}
