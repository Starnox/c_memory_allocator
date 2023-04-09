/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#ifndef _HELPERS_H_
#define _HELPERS_H_

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if __WORDSIZE == 64
#define SIZE_MAX (18446744073709551615UL)
#else
#if __WORDSIZE32_SIZE_ULONG
#define SIZE_MAX (4294967295UL)
#else
#define SIZE_MAX (4294967295U)
#endif
#endif

#define uintptr_t unsigned long
#define INITIAL_HEAP_ALLOCATION (128 * 1024)
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define BLOCK_SIZE ALIGN(sizeof(struct block_meta))
#define MY_MMAP_THRESHOLD INITIAL_HEAP_ALLOCATION
#define MY_MIN(a, b) ((a) < (b) ? (a) : (b))

#define DIE(assertion, call_description)                       \
	do                                                         \
	{                                                          \
		if (assertion)                                         \
		{                                                      \
			fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__); \
			perror(call_description);                          \
			exit(errno);                                       \
		}                                                      \
	} while (0)

/* Structure to hold memory block metadata */
struct block_meta
{
	size_t size;
	int status;
	struct block_meta *next;
};

/* Block metadata status values */
#define STATUS_FREE 0
#define STATUS_ALLOC 1
#define STATUS_MAPPED 2

/**
 * Get the pointer to the end of the heap
 */
struct block_meta *bl_get_end(void);

/**
 * Try to expand the last block of the heap
 */
void *bl_try_expand_last_black(size_t size);

/**
 * Get the pointer to the best block using best fit algorithm
 */
struct block_meta *bl_get_best(size_t size);

/**
 * Try to split the block
 */
void bl_try_split(struct block_meta *block, size_t size);

/**
 * Coalesce free blocks
 */
void bl_coalesce(void);

/**
 * Initialise block list
 */
void bl_initialise_list(void *ptr, size_t size);

/**
 * Add a block to the list
 */
void bl_add_block(void *ptr, size_t size);

/**
 * Check if the list is empty
 */
int bl_is_list_empty(void);

/**
 * Write metadata to a mmap allocated block
 */
void bl_mmap_fill_block(void *ptr, size_t size);

#endif /* _HELPERS_H_ */
