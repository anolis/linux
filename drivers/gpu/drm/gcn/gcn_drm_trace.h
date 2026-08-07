/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gcn_vi

#if !defined(_GCN_DRM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _GCN_DRM_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(gcn_vi_write,
	TP_PROTO(unsigned int sequence, u8 width, u8 offset, u32 value, unsigned long caller),
	TP_ARGS(sequence, width, offset, value, caller),
	TP_STRUCT__entry(
		__field(unsigned int, sequence)
		__field(u8, width)
		__field(u8, offset)
		__field(u32, value)
		__field(unsigned long, caller)
	),
	TP_fast_assign(
		__entry->sequence = sequence;
		__entry->width = width;
		__entry->offset = offset;
		__entry->value = value;
		__entry->caller = caller;
	),
	TP_printk("seq=%u width=%u offset=0x%02x value=0x%08x caller=%ps",
		  __entry->sequence, __entry->width, __entry->offset,
		  __entry->value, (void *)__entry->caller)
);

#endif /* _GCN_DRM_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/gcn
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE gcn_drm_trace

#include <trace/define_trace.h>
