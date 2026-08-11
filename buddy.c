#include "buddy.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define NULL ((void *)0)
#define MAX_RANK 16
#define PAGE_SIZE 4096
#define IS_VALID_RANK(r) ((r) >= 1 && (r) <= MAX_RANK)

// Bitset macros
typedef uint64_t bitset_t;
#define BITSET_BITS (sizeof(bitset_t) * 8)
#define BITSET_SIZE(n) (((n) + BITSET_BITS - 1) / BITSET_BITS)
#define BITSET_SET(arr, i) ((arr)[(i) / BITSET_BITS] |= (1ULL << ((i) % BITSET_BITS)))
#define BITSET_CLEAR(arr, i) ((arr)[(i) / BITSET_BITS] &= ~(1ULL << ((i) % BITSET_BITS)))
#define BITSET_TEST(arr, i) (((arr)[(i) / BITSET_BITS] >> ((i) % BITSET_BITS)) & 1ULL)
#define BITSET_FIND_SET(arr, n) bitset_find_set(arr, n)

static inline int bitset_find_set(bitset_t *arr, int n) {
    int words = BITSET_SIZE(n);
    for (int w = 0; w < words; w++) {
        bitset_t val = arr[w];
        if (val) {
            int base = w * BITSET_BITS;
            // Use builtin to find first set bit
            int bit = __builtin_ctzll(val);
            int idx = base + bit;
            if (idx < n) return idx;
        }
    }
    return -1;
}

static inline int bitset_popcount(bitset_t *arr, int n) {
    int words = BITSET_SIZE(n);
    int count = 0;
    for (int w = 0; w < words; w++) {
        count += __builtin_popcountll(arr[w]);
    }
    return count;
}

static void *base_addr = NULL;
static int total_pages = 0;
static int max_rank = 0;
static int pages_power = 0; // largest power of two <= total_pages
static unsigned char *allocated_rank_map = NULL; // for start offset only
// bitsets for each rank (1..MAX_RANK)
static bitset_t *free_bitset[MAX_RANK + 1];
static int blocks_count[MAX_RANK + 1]; // number of blocks of this rank in the power-of-two region

// Compute size in pages for a given rank
static inline int rank_to_pages(int rank) {
    return 1 << (rank - 1);
}

// Convert page offset to pointer
static inline void *page_to_ptr(int page_offset) {
    return base_addr + page_offset * PAGE_SIZE;
}

// Convert pointer to page offset
static inline int ptr_to_page(void *ptr) {
    return ((char *)ptr - (char *)base_addr) / PAGE_SIZE;
}

// Initialize bitset for rank r
static void init_bitset(int r) {
    int size = rank_to_pages(r);
    int count = pages_power / size;
    blocks_count[r] = count;
    free_bitset[r] = (bitset_t *)calloc(BITSET_SIZE(count), sizeof(bitset_t));
}

// Set a block as free
static void set_block_free(int page_offset, int rank) {
    int size = rank_to_pages(rank);
    int block_idx = page_offset / size;
    BITSET_SET(free_bitset[rank], block_idx);
}

// Clear block free flag
static void clear_block_free(int page_offset, int rank) {
    int size = rank_to_pages(rank);
    int block_idx = page_offset / size;
    BITSET_CLEAR(free_bitset[rank], block_idx);
}

// Test if block is free
static int is_block_free(int page_offset, int rank) {
    int size = rank_to_pages(rank);
    int block_idx = page_offset / size;
    return BITSET_TEST(free_bitset[rank], block_idx);
}

// Find a free block of given rank, return page offset or -1
static int find_free_block(int rank) {
    int idx = BITSET_FIND_SET(free_bitset[rank], blocks_count[rank]);
    if (idx < 0) return -1;
    return idx * rank_to_pages(rank);
}

// Split a free block of rank r > target_rank, return offset of target rank block
static int split_block(int page_offset, int r, int target_rank) {
    while (r > target_rank) {
        // Remove the block from free list
        clear_block_free(page_offset, r);
        // Create two buddies of rank r-1
        int left_offset = page_offset;
        int right_offset = page_offset + rank_to_pages(r - 1);
        set_block_free(left_offset, r - 1);
        set_block_free(right_offset, r - 1);
        r--;
        page_offset = left_offset; // continue splitting left buddy
    }
    return page_offset;
}

// Merge buddies upward
static void merge_buddies(int page_offset, int rank) {
    while (rank < max_rank) {
        int size = rank_to_pages(rank);
        int buddy_offset = page_offset ^ size;
        if (is_block_free(buddy_offset, rank)) {
            // Remove both buddies
            clear_block_free(page_offset, rank);
            clear_block_free(buddy_offset, rank);
            // Parent block offset (min of the two)
            int parent_offset = page_offset < buddy_offset ? page_offset : buddy_offset;
            // Set parent free
            set_block_free(parent_offset, rank + 1);
            page_offset = parent_offset;
            rank++;
        } else {
            break;
        }
    }
}

int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;
    // Find largest power of two <= total_pages
    pages_power = 1;
    while (pages_power * 2 <= total_pages) {
        pages_power *= 2;
    }
    // Determine max rank that fits within pages_power
    max_rank = 1;
    while (max_rank < MAX_RANK && rank_to_pages(max_rank + 1) <= pages_power) {
        max_rank++;
    }
    // Allocate rank map (size total_pages, but we only use up to pages_power)
    allocated_rank_map = (unsigned char *)calloc(total_pages, sizeof(unsigned char));
    if (allocated_rank_map == NULL) {
        return -ENOSPC;
    }
    // Initialize bitsets for each rank
    for (int r = 1; r <= MAX_RANK; r++) {
        free_bitset[r] = NULL;
        blocks_count[r] = 0;
    }
    for (int r = 1; r <= max_rank; r++) {
        init_bitset(r);
    }
    // Mark the whole power-of-two region as free (rank max_rank)
    set_block_free(0, max_rank);
    return OK;
}

void *alloc_pages(int rank) {
    if (!IS_VALID_RANK(rank)) {
        return ERR_PTR(-EINVAL);
    }
    // Find free block of requested rank
    int offset = find_free_block(rank);
    if (offset >= 0) {
        clear_block_free(offset, rank);
        allocated_rank_map[offset] = rank;
        return page_to_ptr(offset);
    }
    // Need to split a larger block
    for (int r = rank + 1; r <= max_rank; r++) {
        offset = find_free_block(r);
        if (offset >= 0) {
            // Split down to target rank
            clear_block_free(offset, r);
            int target_offset = split_block(offset, r, rank);
            // target_offset is free (added during split)
            clear_block_free(target_offset, rank);
            allocated_rank_map[target_offset] = rank;
            return page_to_ptr(target_offset);
        }
    }
    // No space
    return ERR_PTR(-ENOSPC);
}

int return_pages(void *p) {
    if (p == NULL || IS_ERR(p)) {
        return -EINVAL;
    }
    int page_offset = ptr_to_page(p);
    if (page_offset < 0 || page_offset >= total_pages) {
        return -EINVAL;
    }
    int rank = allocated_rank_map[page_offset];
    if (rank == 0) {
        return -EINVAL;
    }
    allocated_rank_map[page_offset] = 0;
    // Mark block as free
    set_block_free(page_offset, rank);
    // Try merging with buddy
    merge_buddies(page_offset, rank);
    return OK;
}

int query_ranks(void *p) {
    if (p == NULL || IS_ERR(p)) {
        return -EINVAL;
    }
    int page_offset = ptr_to_page(p);
    if (page_offset < 0 || page_offset >= total_pages) {
        return -EINVAL;
    }
    // Check if allocated
    int rank = allocated_rank_map[page_offset];
    if (rank != 0) {
        return rank;
    }
    // Free page: find maximum rank such that block aligned and fully free
    for (int r = max_rank; r >= 1; r--) {
        int size = rank_to_pages(r);
        if (page_offset % size != 0) continue;
        if (is_block_free(page_offset, r)) {
            // Ensure the whole block is free (by checking that no sub-block is allocated)
            // Since we maintain consistency, if a block is free, all its pages are free.
            // However, there could be a situation where a block is free but some sub-blocks
            // are allocated? Not possible because we split only when needed.
            // For safety, we can verify that no allocated_rank_map entry within the block.
            return r;
        }
    }
    // Should not happen
    return -EINVAL;
}

int query_page_counts(int rank) {
    if (!IS_VALID_RANK(rank)) {
        return -EINVAL;
    }
    int free_blocks = bitset_popcount(free_bitset[rank], blocks_count[rank]);
    return free_blocks;
}