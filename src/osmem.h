/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include "printf.h"

void *os_malloc(size_t size);
void os_free(void *ptr);
void *os_calloc(size_t nmemb, size_t size);
void *os_realloc(void *ptr, size_t size);
void initialise_heap(void);
void *sbrk_allocate(size_t size);
void *mmap_allocate(size_t total_size, size_t size);
void *alloc_common(size_t size, size_t max);
