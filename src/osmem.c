// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "helpers.h"
#include "pointerhelpers.h"

void initialise_heap(void)
{
	sbrk_allocate(INITIAL_HEAP_ALLOCATION - BLOCK_SIZE);
}

void *sbrk_allocate(size_t size)
{
	void *ret = bl_try_expand_last_black(size);

	if (ret)
		return ret;

	size_t total_size = size + BLOCK_SIZE;
	void *ptr = sbrk(total_size);

	DIE(ptr == (void *)-1, "sbrk");

	bl_is_list_empty() ? bl_initialise_list(ptr, size) : bl_add_block(ptr, size);

	return get_data_ptr(ptr, 0);
}

void *mmap_allocate(size_t total_size, size_t size)
{
	void *ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	DIE(ptr == MAP_FAILED, "mmap");

	bl_mmap_fill_block(ptr, size);
	return get_data_ptr(ptr, 0);
}

void *alloc_common(size_t size, size_t max)
{
	if (size_overflows(size) || !size)
		return NULL;

	size_t aligned_size = ALIGN(size);
	size_t total_size = aligned_size + BLOCK_SIZE;

	if (bl_is_list_empty() && total_size < max)
		initialise_heap();

	void *ret = NULL;

	if (total_size >= max) {
		ret = mmap_allocate(total_size, aligned_size);
	} else { // heap - sbrk
		struct block_meta *block = bl_get_best(aligned_size);

		if (block) { // try to fit in a free block
			block->status = STATUS_ALLOC;
			bl_try_split(block, aligned_size);
			ret = get_data_ptr(block, 0);
		} else {
			ret = sbrk_allocate(aligned_size); // allocate memory with sbrk
		}
	}

	return ret;
}

void *os_malloc(size_t size)
{
	return alloc_common(size, MY_MMAP_THRESHOLD);
}

void os_free(void *ptr)
{
	if (!ptr)
		return;
	struct block_meta *block = get_block_ptr(ptr);

	if (block->status == STATUS_FREE) {
		return;
	} else if (block->status == STATUS_ALLOC) {
		block->status = STATUS_FREE;
	} else {
		size_t total_size = block->size + BLOCK_SIZE;
		int ret = munmap((void *)block, total_size);

		DIE(ret == -1, "munmap");
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	int page_size = getpagesize();

	size_t size_sum = nmemb * size;
	void *ret = alloc_common(size_sum, page_size);

	if (ret)
		memset(ret, 0, size_sum);
	return ret;
}

void *os_realloc(void *ptr, size_t size)
{
	if (!ptr)
		return os_malloc(size);
	if (!size) {
		os_free(ptr);
		return NULL;
	}
	// get the block
	struct block_meta *block = get_block_ptr(ptr);

	if (block->status == STATUS_FREE)
		return NULL;
	size_t aligned_size = ALIGN(size);

	if (aligned_size < MY_MMAP_THRESHOLD && block->status == STATUS_ALLOC) {
		// check if the current block can be used
		if (block->size >= aligned_size) {
			// split block if possible
			bl_try_split(block, aligned_size);
			return ptr;
		}

		// try to expand with free next blocks
		struct block_meta *current = block->next;

		while (current && current->status == STATUS_FREE) {
			block->size += current->size + BLOCK_SIZE;
			block->next = current->next;
			current = current->next;

			if (block->size >= aligned_size) {
				// split block if possible
				bl_try_split(block, aligned_size);
				return ptr;
			}
		}
		if (current == NULL) {
			// expand the heap
			size_t diff = aligned_size - block->size;

			if (diff < MY_MMAP_THRESHOLD) {
				block->size += diff;
				// call sbrk to extend the heap
				void *sbrk_ptr = sbrk(diff);

				DIE(sbrk_ptr == (void *)-1, "sbrk");
				return get_data_ptr(block, 0);
			}
		}
	}

	// if resizing fails than try the classic way
	void *new_ptr = os_malloc(size);

	if (new_ptr) {
		memcpy(new_ptr, ptr, MY_MIN(block->size, size));
		os_free(ptr);
		return new_ptr;
	}
	return NULL;
}
