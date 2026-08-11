#include "buddy.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define NULL ((void *)0)
#define MAX_RANK 16
#define PAGE_SIZE 4096
#define IS_VALID_RANK(r) ((r) >= 1 && (r) <= MAX_RANK)

// Free block header stored at start of free block
struct free_header {
    unsigned int rank;              // rank of this free block (1..MAX_RANK)
    struct free_header *next;
    struct free_header *prev;
};

static void *base_addr = NULL;
static int total_pages = 0;
static int max_rank = 0;            // largest rank that fits total_pages
static unsigned char *allocated_rank_map = NULL; // maps page offset to rank of allocated block start, 0 means free
static struct free_header *free_head[MAX_RANK + 1]; // index 1..MAX_RANK
static struct free_header *free_tail[MAX_RANK + 1];

// Debug
#ifdef DEBUG
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

// Convert page offset to pointer
static inline void *page_to_ptr(int page_offset) {
    return base_addr + page_offset * PAGE_SIZE;
}

// Convert pointer to page offset (must be within pool)
static inline int ptr_to_page(void *ptr) {
    return ((char *)ptr - (char *)base_addr) / PAGE_SIZE;
}

// Compute size in pages for a given rank
static inline int rank_to_pages(int rank) {
    return 1 << (rank - 1);
}

// Helper to add a free block to free list at tail (maintain address order)
static void add_free_block(int page_offset, int rank) {
    struct free_header *header = (struct free_header *)page_to_ptr(page_offset);
    header->rank = rank;
    header->next = NULL;
    header->prev = free_tail[rank];
    if (free_tail[rank] == NULL) {
        free_head[rank] = free_tail[rank] = header;
    } else {
        free_tail[rank]->next = header;
        free_tail[rank] = header;
    }
    DEBUG_PRINT("add_free_block: offset=%d rank=%d\n", page_offset, rank);
}

// Helper to remove a free block from its free list
static void remove_free_block(struct free_header *header) {
    int rank = header->rank;
    if (header->prev != NULL) {
        header->prev->next = header->next;
    } else {
        free_head[rank] = header->next;
    }
    if (header->next != NULL) {
        header->next->prev = header->prev;
    } else {
        free_tail[rank] = header->prev;
    }
    // Clear header links (optional)
    header->next = header->prev = NULL;
    DEBUG_PRINT("remove_free_block: offset=%d rank=%d\n", ptr_to_page(header), rank);
}

// Find and remove the free block with smallest page offset of given rank
static int remove_smallest_free_block(int rank) {
    struct free_header *header = free_head[rank];
    if (header == NULL) {
        return -1;
    }
    // Since we insert at tail in order, head is smallest offset
    int page_offset = ptr_to_page(header);
    remove_free_block(header);
    DEBUG_PRINT("remove_smallest_free_block: rank=%d offset=%d\n", rank, page_offset);
    return page_offset;
}

// Check if a block starting at page_offset with given rank is free
static int is_block_free(int page_offset, int rank) {
    // Check that the entire block is free (allocated_rank_map entries zero)
    int pages = rank_to_pages(rank);
    for (int i = 0; i < pages; i++) {
        if (allocated_rank_map[page_offset + i] != 0) {
            return 0;
        }
    }
    // Additionally, the block should be in free list (not split)
    struct free_header *cur = free_head[rank];
    while (cur != NULL) {
        if (ptr_to_page(cur) == page_offset) {
            return 1;
        }
        cur = cur->next;
    }
    return 0;
}

// Split a free block of rank r > target_rank, return page offset of target rank block (leftmost)
static int split_block(int page_offset, int r, int target_rank) {
    DEBUG_PRINT("split_block: start_offset=%d start_rank=%d target_rank=%d\n", page_offset, r, target_rank);
    while (r > target_rank) {
        // Remove the block of rank r from free list
        struct free_header *header = (struct free_header *)page_to_ptr(page_offset);
        remove_free_block(header);
        // Create two buddies of rank r-1
        int left_offset = page_offset;
        int right_offset = page_offset + rank_to_pages(r - 1);
        add_free_block(left_offset, r - 1);
        add_free_block(right_offset, r - 1);
        r--;
        // Continue splitting left buddy (choose left-first)
        page_offset = left_offset;
    }
    // Now we have a free block of target_rank at page_offset (still in free list)
    DEBUG_PRINT("split_block result offset=%d rank=%d\n", page_offset, target_rank);
    return page_offset;
}

// Merge buddy blocks upwards
static void merge_buddies(int page_offset, int rank) {
    DEBUG_PRINT("merge_buddies: offset=%d rank=%d\n", page_offset, rank);
    while (rank < max_rank) {
        int buddy_offset = page_offset ^ rank_to_pages(rank);
        DEBUG_PRINT("  buddy_offset=%d\n", buddy_offset);
        // Check if buddy is free and of same rank
        if (is_block_free(buddy_offset, rank)) {
            DEBUG_PRINT("  buddy free, merging\n");
            // Remove both buddies
            struct free_header *header1 = (struct free_header *)page_to_ptr(page_offset);
            struct free_header *header2 = (struct free_header *)page_to_ptr(buddy_offset);
            remove_free_block(header1);
            remove_free_block(header2);
            // Compute parent block offset (min of page_offset and buddy_offset)
            int parent_offset = page_offset < buddy_offset ? page_offset : buddy_offset;
            // Add merged block of rank+1
            add_free_block(parent_offset, rank + 1);
            page_offset = parent_offset;
            rank++;
        } else {
            break;
        }
    }
}

// Dump free lists (debug)
static void dump_free_lists(void) {
#ifdef DEBUG
    for (int r = 1; r <= max_rank; r++) {
        struct free_header *cur = free_head[r];
        if (cur) {
            DEBUG_PRINT("free list rank %d:", r);
            while (cur) {
                DEBUG_PRINT(" %d", ptr_to_page(cur));
                cur = cur->next;
            }
            DEBUG_PRINT("\n");
        }
    }
#endif
}

int init_page(void *p, int pgcount) {
    // Already initialized? For simplicity, allow only once.
    if (base_addr != NULL) {
        // Could reinitialize, but we just reset.
    }
    base_addr = p;
    total_pages = pgcount;
    // Determine max_rank such that 2^(max_rank-1) <= total_pages
    max_rank = 1;
    while (max_rank < MAX_RANK && rank_to_pages(max_rank + 1) <= total_pages) {
        max_rank++;
    }
    DEBUG_PRINT("init_page: base=%p total_pages=%d max_rank=%d\n", p, pgcount, max_rank);
    // Allocate rank map
    allocated_rank_map = (unsigned char *)calloc(total_pages, sizeof(unsigned char));
    if (allocated_rank_map == NULL) {
        return -ENOSPC; // Out of memory (should not happen)
    }
    // Initialize free lists
    for (int i = 1; i <= MAX_RANK; i++) {
        free_head[i] = free_tail[i] = NULL;
    }
    // Partition total_pages into power-of-two blocks
    int remaining = total_pages;
    int start_page = 0;
    while (remaining > 0) {
        int r = max_rank;
        while (r > 0 && rank_to_pages(r) > remaining) {
            r--;
        }
        // r is now largest rank that fits
        add_free_block(start_page, r);
        start_page += rank_to_pages(r);
        remaining -= rank_to_pages(r);
    }
    dump_free_lists();
    return OK;
}

void *alloc_pages(int rank) {
    if (!IS_VALID_RANK(rank)) {
        return ERR_PTR(-EINVAL);
    }
    DEBUG_PRINT("alloc_pages rank=%d\n", rank);
    // Find a free block of given rank (smallest offset)
    if (free_head[rank] != NULL) {
        int page_offset = remove_smallest_free_block(rank);
        // Mark as allocated
        allocated_rank_map[page_offset] = rank;
        DEBUG_PRINT("  allocated offset=%d\n", page_offset);
        dump_free_lists();
        return page_to_ptr(page_offset);
    }
    // Need to split a larger block
    for (int r = rank + 1; r <= max_rank; r++) {
        if (free_head[r] != NULL) {
            int page_offset = remove_smallest_free_block(r);
            // Split down to target rank
            int target_offset = split_block(page_offset, r, rank);
            // The target block is already in free list (added by split_block)
            // Remove it from free list
            struct free_header *target_header = (struct free_header *)page_to_ptr(target_offset);
            remove_free_block(target_header);
            allocated_rank_map[target_offset] = rank;
            DEBUG_PRINT("  allocated via split offset=%d\n", target_offset);
            dump_free_lists();
            return page_to_ptr(target_offset);
        }
    }
    // No space
    DEBUG_PRINT("  no space\n");
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
        // Not allocated
        return -EINVAL;
    }
    DEBUG_PRINT("return_pages offset=%d rank=%d\n", page_offset, rank);
    // Mark as free
    allocated_rank_map[page_offset] = 0;
    // Add to free list
    add_free_block(page_offset, rank);
    // Try to merge with buddy
    merge_buddies(page_offset, rank);
    dump_free_lists();
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
        int pages = rank_to_pages(r);
        if (page_offset % pages != 0) {
            continue;
        }
        // Check entire block free
        int free = 1;
        for (int i = 0; i < pages; i++) {
            if (allocated_rank_map[page_offset + i] != 0) {
                free = 0;
                break;
            }
        }
        if (free) {
            // Also ensure block is not split (i.e., exists in free list)
            struct free_header *cur = free_head[r];
            while (cur != NULL) {
                if (ptr_to_page(cur) == page_offset) {
                    return r;
                }
                cur = cur->next;
            }
        }
    }
    // Should not happen (at least rank 1)
    return -EINVAL;
}

int query_page_counts(int rank) {
    if (!IS_VALID_RANK(rank)) {
        return -EINVAL;
    }
    int count = 0;
    struct free_header *cur = free_head[rank];
    while (cur != NULL) {
        count++;
        cur = cur->next;
    }
    // Each block contributes rank_to_pages(rank) pages
    DEBUG_PRINT("query_page_counts rank=%d count=%d\n", rank, count);
    return count;
}