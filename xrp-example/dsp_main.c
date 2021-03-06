/*
 * Copyright (c) 2016 - 2017 Cadence Design Systems Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtensa/corebits.h>
#include <xtensa/xtruntime.h>
#include "xrp_api.h"
#include "xrp_dsp_hw.h"
#include "example_namespace.h"

static void hang(void) __attribute__((noreturn));
static void hang(void)
{
	for (;;);
}

void abort(void)
{
	fprintf(stderr, "abort() is called; halting\n");
	hang();
}

static void exception(void)
{
	unsigned long exccause, excvaddr, ps, epc1;

	__asm__ volatile ("rsr %0, exccause\n\t"
			  "rsr %1, excvaddr\n\t"
			  "rsr %2, ps\n\t"
			  "rsr %3, epc1"
			  : "=a"(exccause), "=a"(excvaddr),
			    "=a"(ps), "=a"(epc1));

	fprintf(stderr, "%s: EXCCAUSE = %ld, EXCVADDR = 0x%08lx, PS = 0x%08lx, EPC1 = 0x%08lx\n",
		__func__, exccause, excvaddr, ps, epc1);
	hang();
}

static void register_exception_handlers(void)
{
	static const int cause[] = {
		EXCCAUSE_ILLEGAL,
		EXCCAUSE_INSTR_ERROR,
		EXCCAUSE_LOAD_STORE_ERROR,
		EXCCAUSE_DIVIDE_BY_ZERO,
		EXCCAUSE_PRIVILEGED,
		EXCCAUSE_UNALIGNED,
		EXCCAUSE_INSTR_DATA_ERROR,
		EXCCAUSE_LOAD_STORE_DATA_ERROR,
		EXCCAUSE_INSTR_ADDR_ERROR,
		EXCCAUSE_LOAD_STORE_ADDR_ERROR,
		EXCCAUSE_ITLB_MISS,
		EXCCAUSE_ITLB_MULTIHIT,
		EXCCAUSE_INSTR_RING,
		EXCCAUSE_INSTR_PROHIBITED,
		EXCCAUSE_DTLB_MISS,
		EXCCAUSE_DTLB_MULTIHIT,
		EXCCAUSE_LOAD_STORE_RING,
		EXCCAUSE_LOAD_PROHIBITED,
		EXCCAUSE_STORE_PROHIBITED,
	};
	unsigned i;

	for (i = 0; i < sizeof(cause) / sizeof(cause[0]); ++i) {
		_xtos_set_exception_handler(cause[i], exception);
	}
}

void xrp_run_command(const void *in_data, size_t in_data_size,
		     void *out_data, size_t out_data_size,
		     struct xrp_buffer_group *buffer_group,
		     enum xrp_status *status)
{
	(void)in_data;
	(void)in_data_size;
	(void)out_data;
	(void)out_data_size;
	(void)buffer_group;
	printf("%s\n", __func__);
	if (status)
		*status = XRP_STATUS_SUCCESS;
}

static enum xrp_status example_v1_handler(void *handler_context,
					  const void *in_data, size_t in_data_size,
					  void *out_data, size_t out_data_size,
					  struct xrp_buffer_group *buffer_group)
{
	size_t i;
	uint32_t sz = 0;

	(void)handler_context;
	printf("%s, in_data_size = %zu, out_data_size = %zu\n",
	       __func__, in_data_size, out_data_size);

	for (i = 0; i < in_data_size; ++i) {
		if (i < out_data_size)
			((uint8_t *)out_data)[i] =
				((uint8_t *)in_data)[i] + i;
	}

	if (in_data_size >= sizeof(sz))
		memcpy(&sz, in_data, sizeof(sz));

	for (i = 0; sz; i += 2) {
		struct xrp_buffer *sbuf = xrp_get_buffer_from_group(buffer_group, i, NULL);
		struct xrp_buffer *dbuf = xrp_get_buffer_from_group(buffer_group, i + 1, NULL);
		void *src, *dst;

		if (!sbuf || !dbuf)
			break;

		src = xrp_map_buffer(sbuf, 0, sz, XRP_READ, NULL);
		dst = xrp_map_buffer(dbuf, 0, sz, XRP_WRITE, NULL);

		if (!src || !dst) {
			xrp_release_buffer(sbuf, NULL);
			xrp_release_buffer(dbuf, NULL);
			break;
		}

		printf("%s: copy %d bytes from %p to %p\n",
		       __func__, sz, src, dst);
		memcpy(dst, src, sz);
		xrp_unmap_buffer(sbuf, src, NULL);
		xrp_unmap_buffer(dbuf, dst, NULL);
		xrp_release_buffer(sbuf, NULL);
		xrp_release_buffer(dbuf, NULL);
	}

	sz = 0;
	xrp_buffer_group_get_info(buffer_group, XRP_BUFFER_GROUP_SIZE_SIZE_T,
				  0, &sz, sizeof(sz), NULL);

	return sz == i ? XRP_STATUS_SUCCESS : XRP_STATUS_FAILURE;
}

static enum xrp_status example_v2_handler(void *handler_context,
					  const void *in_data, size_t in_data_size,
					  void *out_data, size_t out_data_size,
					  struct xrp_buffer_group *buffer_group)
{
	const struct example_v2_cmd *cmd = in_data;

	(void)handler_context;
	(void)out_data;
	(void)out_data_size;
	(void)buffer_group;

	if (in_data_size < sizeof(*cmd)) {
		return XRP_STATUS_FAILURE;
	}
	switch (cmd->cmd) {
	case EXAMPLE_V2_CMD_OK:
		return XRP_STATUS_SUCCESS;
	case EXAMPLE_V2_CMD_FAIL:
		return XRP_STATUS_FAILURE;
	default:
		return XRP_STATUS_FAILURE;
	}
}

static enum xrp_status test_ns(struct xrp_device *device)
{
	enum xrp_status status;
	char test_nsid[16][XRP_NAMESPACE_ID_SIZE];
	size_t i;

	for (i = 0; i < sizeof(test_nsid) / sizeof(test_nsid[0]); ++i) {
		size_t j;

		for (j = 0; j < XRP_NAMESPACE_ID_SIZE; ++j) {
			test_nsid[i][j] = rand();
		}
	}
	for (i = 0; i < sizeof(test_nsid) / sizeof(test_nsid[0]); ++i) {
		xrp_device_register_namespace(device, test_nsid[i],
					      NULL, NULL, &status);
		if (status != XRP_STATUS_SUCCESS) {
			printf("xrp_register_namespace failed\n");
			return XRP_STATUS_FAILURE;
		}
	}
	for (i = 0; i < sizeof(test_nsid) / sizeof(test_nsid[0]); ++i) {
		xrp_device_unregister_namespace(device, test_nsid[i],
						&status);
		if (status != XRP_STATUS_SUCCESS) {
			printf("xrp_unregister_namespace failed\n");
			return XRP_STATUS_FAILURE;
		}
	}
	return XRP_STATUS_SUCCESS;
}

int main(void)
{
	enum xrp_status status;
	struct xrp_device *device;

	register_exception_handlers();
	device = xrp_open_device(0, &status);
	if (status != XRP_STATUS_SUCCESS) {
		printf("xrp_open_device failed\n");
		return 1;
	}
	status = test_ns(device);
	if (status != XRP_STATUS_SUCCESS) {
		printf("test_ns failed\n");
		return 1;
	}
	xrp_device_register_namespace(device, XRP_EXAMPLE_V1_NSID,
				      example_v1_handler, NULL, &status);
	if (status != XRP_STATUS_SUCCESS) {
		printf("xrp_register_namespace for XRP_EXAMPLE_V1_NSID failed\n");
		return 1;
	}
	xrp_device_register_namespace(device, XRP_EXAMPLE_V2_NSID,
				      example_v2_handler, NULL, &status);
	if (status != XRP_STATUS_SUCCESS) {
		printf("xrp_register_namespace for XRP_EXAMPLE_V2_NSID failed\n");
		return 1;
	}
	for (;;) {
		status = xrp_device_dispatch(device);
		if (status == XRP_STATUS_PENDING)
			xrp_hw_wait_device_irq();
	}
	return 0;
}
