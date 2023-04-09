// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "helpers.h"

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
#define INITIAL_HEAP_ALLOCATION 128 * 1024
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define BLOCK_SIZE ALIGN(sizeof(struct block_meta))
#define MMAP_THRESHOLD INITIAL_HEAP_ALLOCATION
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// #define check_can_split(size) (size >= BLOCK_SIZE + 1)
// #define GET_VOID_PTR(ptr, offset) ((void *)((char *)(ptr) + BLOCK_SIZE + (offset)))
// #define GET_BLOCK_PTR(ptr) ((struct block_meta *)((char *)(ptr)-BLOCK_SIZE))

static struct block_meta *start_heap;
// TODO add a global variable to keep track of the last block
// and remove the need for get_end()
// TODO document more
// TODO remove dead code
// TODO make a convention for aligned size
// TODO separate the code in multiple files
// TODO extract this syntax into a macro maybe (void *)((char *)end + BLOCK_SIZE)
// TODO create macro for iterating over the blocks (hint look at Unikraft)
// TODO create common function for malloc and calloc

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

struct block_meta *get_end()
{
	if (!start_heap)
		return NULL;
	struct block_meta *current = start_heap;
	while (current->next)
		current = current->next;
	return current;
}

void *sbrk_allocate(size_t size)
{
	// check if we can expand the last block
	struct block_meta *end = get_end();
	if (end && end->status == STATUS_FREE)
	{
		// TODO extract this in a separate function
		size_t diff = size - end->size;
		end->size += diff;
		end->status = STATUS_ALLOC;
		// call sbrk to extend the heap
		void *ptr = sbrk(diff);
		DIE(ptr == (void *)-1, "sbrk");
		return get_data_ptr(end, 0);
	}

	size_t total_size = size + BLOCK_SIZE;
	void *ptr = sbrk(total_size);
	DIE(ptr == (void *)-1, "sbrk");

	// first allocation - initialise the heap
	if (!end)
	{
		start_heap = ptr;
		start_heap->size = size;
		start_heap->status = STATUS_FREE;
		start_heap->next = NULL;
	}
	else
	{
		struct block_meta *new = ptr;
		new->size = size;
		new->status = STATUS_ALLOC;
		new->next = NULL;
		end->next = new;
	}
	return ptr + BLOCK_SIZE;
}

/**
 * Initialise the heap with a single free block using sbrk
 */
void initialise_heap()
{
	sbrk_allocate(INITIAL_HEAP_ALLOCATION - BLOCK_SIZE);
}

/**
 * Coalesce free blocks
 */
void coalesce_blocks()
{
	struct block_meta *current = start_heap;
	while (current)
	{
		if (current->status == STATUS_FREE)
		{
			struct block_meta *next = current->next;
			if (next && next->status == STATUS_FREE)
			{
				current->size += next->size + BLOCK_SIZE;
				current->next = next->next;
				continue;
			}
		}
		current = current->next;
	}
}

/**
 * Find the best block for the given size using best fit
 */
void *get_best_block(size_t size)
{
	// before searching, coalesce blocks
	coalesce_blocks();
	// find best block
	struct block_meta *current = start_heap;
	struct block_meta *best = NULL;
	while (current)
	{
		if (current->status == STATUS_FREE && current->size >= size)
		{
			if (!best || current->size < best->size)
			{
				best = current;
			}
		}
		current = current->next;
	}
	return best;
}

/**
 * Split a block into two blocks if possible
 * param block: block to split
 * param size: size of the first block
 */
void try_split(struct block_meta *block, size_t size)
{
	size_t remaining_size = block->size - size;
	if (check_can_split(remaining_size))
	{
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

void *alloc_common(size_t size, size_t max)
{
	if (size_overflows(size) || !size)
		return NULL;

	size_t aligned_size = ALIGN(size);
	size_t total_size = aligned_size + BLOCK_SIZE;

	if (!start_heap && total_size < max)
		initialise_heap();

	void *ret = NULL;
	if (total_size >= max)
	{
		// TODO extract this into a function
		void *ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
						 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		DIE(ptr == MAP_FAILED, "mmap");

		struct block_meta *new = (struct block_meta *)ptr;
		new->size = aligned_size;
		new->status = STATUS_MAPPED;
		new->next = NULL;
		ret = get_data_ptr(new, 0);
	}
	else
	{
		struct block_meta *block = (struct block_meta *)get_best_block(aligned_size);
		if (block)
		{
			block->status = STATUS_ALLOC;
			// split block if possible
			try_split(block, aligned_size);
			// mark block as allocated

			ret = get_data_ptr(block, 0);
		}
		else
		{
			// allocate memory with brk
			ret = sbrk_allocate(aligned_size);
		}
	}
	
	return ret;
}

void *os_malloc(size_t size)
{
	return alloc_common(size, MMAP_THRESHOLD);
}

void os_free(void *ptr)
{
	if (!ptr)
		return;
	// Find the block and then mark it as free
	struct block_meta *block = get_block_ptr(ptr);
	if (block->status == STATUS_FREE)
		return;
	else if (block->status == STATUS_ALLOC)
	{
		block->status = STATUS_FREE;
	}
	else
	{
		// call munmap
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
	{
		return os_malloc(size);
	}
	if (!size)
	{
		os_free(ptr);
		return NULL;
	}
	// get the block
	struct block_meta *block = get_block_ptr(ptr);
	if (block->status == STATUS_FREE)
	{
		return NULL;
	}
	size_t aligned_size = ALIGN(size);
	if (aligned_size < MMAP_THRESHOLD && block->status == STATUS_ALLOC) // only for the heap
	{
		// check if the current block can be used
		if (block->size >= aligned_size)
		{
			// split block if possible
			try_split(block, aligned_size);
			return ptr;
		}

		// try to expand with free next blocks
		struct block_meta *current = block->next;
		while (current && current->status == STATUS_FREE)
		{
			block->size += current->size + BLOCK_SIZE;
			block->next = current->next;
			current = current->next;

			if (block->size >= aligned_size)
			{
				// split block if possible
				try_split(block, aligned_size);
				return ptr;
			}
		}
		if (current == NULL)
		{
			// expand the heap
			size_t diff = aligned_size - block->size;
			if (diff < MMAP_THRESHOLD)
			{
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
	if (new_ptr)
	{
		memcpy(new_ptr, ptr, MIN(block->size, size));
		os_free(ptr);
		return new_ptr;
	}
	return NULL;
}
