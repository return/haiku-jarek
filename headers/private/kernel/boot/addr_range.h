/*
 * Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KERNEL_BOOT_ADDR_RANGE_H
#define KERNEL_BOOT_ADDR_RANGE_H


#include <SupportDefs.h>


typedef struct addr_range {
	uint64 start;
	uint64 size;
} _PACKED addr_range;


#ifdef __cplusplus
extern "C" {
#endif

status_t insert_address_range(addr_range* ranges, uint32* _numRanges,
	uint32 maxRanges, uint64 start, uint64 size);
status_t remove_address_range(addr_range* ranges, uint32* _numRanges,
	uint32 maxRanges, uint64 start, uint64 size);
void sort_address_ranges(addr_range* ranges, uint32 numRanges);

void ignore_physical_memory_ranges_beyond_4gb();

#ifdef __cplusplus
}
#endif

#endif	/* KERNEL_BOOT_ADDR_RANGE_H */
