#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "xrp_alloc.h"

#define PAGE_SIZE 4096
#define GFP_KERNEL 0
#define ALIGN(v, a) (((v) + (a) - 1) & -(a))

enum {
	false,
	true,
};

typedef int bool;

#ifdef DEBUG
#define pr_debug printf
#else
static inline void pr_debug(const char *fmt, ...)
{
	(void)fmt;
}
#endif

static void *kzalloc(size_t sz, int flags)
{
	(void)flags;
	return calloc(1, sz);
}

static void kfree(void *p)
{
	free(p);
}

static void xrp_pool_lock(struct xrp_allocation_pool *pool)
{
}

static void xrp_pool_unlock(struct xrp_allocation_pool *pool)
{
}

static void xrp_allocation_get(struct xrp_allocation *allocation)
{
}

static void atomic_set(atomic_t *p, uint32_t v)
{
	*((volatile atomic_t *)p) = v;
}

long xrp_init_pool(struct xrp_allocation_pool *pool,
		   phys_addr_t start, u32 size)
{
	struct xrp_allocation *allocation = malloc(sizeof(*allocation));

	*allocation = (struct xrp_allocation){
		.start = start,
		.size = size,
		.pool = pool,
	};
	*pool = (struct xrp_allocation_pool){
		.start = start,
		.size = size,
		.free_list = allocation,
	};
	return 0;
}

void xrp_free(struct xrp_allocation *xrp_allocation)
{
	struct xrp_allocation_pool *pool = xrp_allocation->pool;
	struct xrp_allocation **pcur;

	pr_debug("%s: %pap x %d\n", __func__,
		 &xrp_allocation->start, xrp_allocation->size);

	xrp_pool_lock(pool);

	for (pcur = &pool->free_list; ; pcur = &(*pcur)->next) {
		struct xrp_allocation *cur = *pcur;

		if (cur && cur->start + cur->size == xrp_allocation->start) {
			struct xrp_allocation *next = cur->next;

			pr_debug("merging block tail: %pap x 0x%x ->\n",
				 &cur->start, cur->size);
			cur->size += xrp_allocation->size;
			pr_debug("... -> %pap x 0x%x\n",
				 &cur->start, cur->size);
			kfree(xrp_allocation);

			if (next && cur->start + cur->size == next->start) {
				pr_debug("merging with next block: %pap x 0x%x ->\n",
					 &cur->start, cur->size);
				cur->size += next->size;
				cur->next = next->next;
				pr_debug("... -> %pap x 0x%x\n",
					 &cur->start, cur->size);
				kfree(next);
			}
			break;
		}

		if (!cur || xrp_allocation->start < cur->start) {
			if (cur && xrp_allocation->start + xrp_allocation->size == cur->start) {
				pr_debug("merging block head: %pap x 0x%x ->\n",
					 &cur->start, cur->size);
				cur->size += xrp_allocation->size;
				cur->start = xrp_allocation->start;
				pr_debug("... -> %pap x 0x%x\n",
					 &cur->start, cur->size);
				kfree(xrp_allocation);
			} else {
				pr_debug("inserting new free block\n");
				xrp_allocation->next = cur;
				*pcur = xrp_allocation;
			}
			break;
		}
	}

	xrp_pool_unlock(pool);
}

long xrp_allocate(struct xrp_allocation_pool *pool,
		  u32 size, u32 align, struct xrp_allocation **alloc)
{
	struct xrp_allocation **pcur;
	struct xrp_allocation *cur = NULL;
	struct xrp_allocation *new;
	phys_addr_t aligned_start = 0;
	bool found = false;

	if (!size || (align & (align - 1)))
		return -EINVAL;
	if (!align)
		align = 1;

	new = kzalloc(sizeof(struct xrp_allocation), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	align = ALIGN(align, PAGE_SIZE);
	size = ALIGN(size, PAGE_SIZE);

	xrp_pool_lock(pool);

	/* on exit free list is fixed */
	for (pcur = &pool->free_list; *pcur; pcur = &(*pcur)->next) {
		cur = *pcur;
		aligned_start = ALIGN(cur->start, align);

		if (aligned_start >= cur->start &&
		    aligned_start - cur->start + size <= cur->size) {
			if (aligned_start == cur->start) {
				if (aligned_start + size == cur->start + cur->size) {
					pr_debug("reusing complete block: %pap x %x\n", &cur->start, cur->size);
					*pcur = cur->next;
				} else {
					pr_debug("cutting block head: %pap x %x ->\n", &cur->start, cur->size);
					cur->size -= aligned_start + size - cur->start;
					cur->start = aligned_start + size;
					pr_debug("... -> %pap x %x\n", &cur->start, cur->size);
					cur = NULL;
				}
			} else {
				if (aligned_start + size == cur->start + cur->size) {
					pr_debug("cutting block tail: %pap x %x ->\n", &cur->start, cur->size);
					cur->size = aligned_start - cur->start;
					pr_debug("... -> %pap x %x\n", &cur->start, cur->size);
					cur = NULL;
				} else {
					pr_debug("splitting block into two: %pap x %x ->\n", &cur->start, cur->size);
					new->start = aligned_start + size;
					new->size = cur->start + cur->size - new->start;

					cur->size = aligned_start - cur->start;

					new->next = cur->next;
					cur->next = new;
					pr_debug("... -> %pap x %x + %pap x %x\n", &cur->start, cur->size, &new->start, new->size);

					cur = NULL;
					new = NULL;
				}
			}
			found = true;
			break;
		} else {
			cur = NULL;
		}
	}

	xrp_pool_unlock(pool);

	if (!found) {
		kfree(cur);
		kfree(new);
		return -ENOMEM;
	}

	if (!cur) {
		cur = new;
		new = NULL;
	}
	if (!cur) {
		cur = kzalloc(sizeof(struct xrp_allocation), GFP_KERNEL);
		if (!cur)
			return -ENOMEM;
	}
	if (new)
		kfree(new);

	pr_debug("returning: %pap x %x\n", &aligned_start, size);
	cur->start = aligned_start;
	cur->size = size;
	cur->pool = pool;
	atomic_set(&cur->ref, 0);
	xrp_allocation_get(cur);
	*alloc = cur;

	return 0;
}

phys_addr_t xrp_allocation_offset(const struct xrp_allocation *allocation)
{
	return allocation->start - allocation->pool->start;
}