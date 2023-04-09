// SPDX-License-Identifier: BSD-3-Clause

#include "helpers.h"
#include "pointerhelpers.h"

/* Start of the heap */
static struct block_meta *start_heap;

struct block_meta *bl_get_end(void)
{
	if (!start_heap)
		return NULL;
	struct block_meta *current = start_heap;

	while (current->next)
		current = current->next;
	return current;
}

void *bl_try_expand_last_black(size_t size)
{
	struct block_meta *end = bl_get_end();

	if (end && end->status == STATUS_FREE) {
		size_t diff = size - end->size;

		end->size += diff;
		end->status = STATUS_ALLOC;
		// call sbrk to extend the heap
		void *ptr = sbrk(diff);

		DIE(ptr == (void *)-1, "sbrk");
		return get_data_ptr(end, 0);
	}
	return NULL;
}

void bl_coalesce(void)
{
	struct block_meta *current = start_heap;

	while (current) {
		if (current->status == STATUS_FREE) {
			struct block_meta *next = current->next;

			if (next && next->status == STATUS_FREE) {
				current->size += next->size + BLOCK_SIZE;
				current->next = next->next;
				continue;
			}
		}
		current = current->next;
	}
}

struct block_meta *bl_get_best(size_t size)
{
	// before searching, coalesce blocks
	bl_coalesce();

	struct block_meta *current = start_heap;
	struct block_meta *best = NULL;

	while (current) {
		if (current->status == STATUS_FREE && current->size >= size)
			if (!best || current->size < best->size)
				best = current;
		current = current->next;
	}
	return best;
}

void bl_try_split(struct block_meta *block, size_t size)
{
	size_t remaining_size = block->size - size;

	if (check_can_split(remaining_size)) {
		// create new block (everything is aligned)
		struct block_meta *new_block = get_data_ptr(block, size);

		new_block->size = block->size - size - BLOCK_SIZE;
		new_block->status = STATUS_FREE;
		new_block->next = block->next;

		// update current block
		block->size = size;
		block->next = new_block;
	}
}

void bl_initialise_list(void *ptr, size_t size)
{
	start_heap = ptr;
	start_heap->size = size;
	start_heap->status = STATUS_FREE;
	start_heap->next = NULL;
}

void bl_add_block(void *ptr, size_t size)
{
	struct block_meta *end = bl_get_end();

	if (end) {
		struct block_meta *new = ptr;

		new->size = size;
		new->status = STATUS_ALLOC;
		new->next = NULL;
		end->next = new;
	}
}

int bl_is_list_empty(void)
{
	return start_heap == NULL;
}

void bl_mmap_fill_block(void *ptr, size_t size)
{
	struct block_meta *new = (struct block_meta *)ptr;

	new->size = size;
	new->status = STATUS_MAPPED;
	new->next = NULL;
}
