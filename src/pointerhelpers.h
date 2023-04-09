/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#ifndef _POINTERHELPERS_H
#define _POINTERHELPERS_H

#include "helpers.h"

/**
 * Check if the given size can be split into two blocks
 */
static inline int check_can_split(size_t size)
{
	return size >= BLOCK_SIZE + 1;
}

/**
 * Get the pointer to the data of a block
 */
static inline void *get_data_ptr(void *ptr, size_t offset)
{
	return (void *)((char *)ptr + BLOCK_SIZE + offset);
}

/**
 * Get the pointer to the meta block of memory from the data pointer
 */
static inline struct block_meta *get_block_ptr(void *ptr)
{
	return (struct block_meta *)((char *)ptr - BLOCK_SIZE);
}

/**
 * Check if the given size overflows
 * Taken from musl libc
 */
static inline int size_overflows(size_t n)
{
	if (n >= SIZE_MAX / 2 - 4096)
	{
		errno = ENOMEM;
		return 1;
	}
	return 0;
}

#endif