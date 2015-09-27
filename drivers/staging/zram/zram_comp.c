/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "zram_comp.h"

#ifdef CONFIG_ZRAM_LZO_COMPRESS
#include "zcomp_lzo.h"
#endif

#ifdef CONFIG_ZRAM_LZ4_COMPRESS
#include "zcomp_lz4.h"
#endif

struct zcomp {
        const char *name;
        int (*create)(struct zram_comp *comp);
        void (*destroy)(struct zram_comp *comp);
};

static struct zcomp compressors[] = {
#ifdef CONFIG_ZRAM_LZO_COMPRESS
        {
                .name = "lzo",
                .create = zcomp_lzo_create,
                .destroy = zcomp_lzo_destroy
        },
#endif
#ifdef CONFIG_ZRAM_LZ4_COMPRESS
        {
                .name = "lz4",
                .create = zcomp_lz4_create,
                .destroy = zcomp_lz4_destroy
        },
#endif
        {}
};

static void workmem_free(struct zcomp_workmem *workmem)
{
        vfree(workmem->buf);
        vfree(workmem->mem);
        kfree(workmem);
}

/* allocate new workmem structure with ->mem of requested size,
 * return NULL on error */
static struct zcomp_workmem *workmem_alloc(size_t sz)
{
        struct zcomp_workmem *workmem = kmalloc(sizeof(*workmem), GFP_NOFS);
        if (!workmem)
                return NULL;

        INIT_LIST_HEAD(&workmem->list);
        /* algorithm specific working memory buffer */
        workmem->mem = vmalloc(sz);
        /* allocate 2 pages. 1 for compressed data, plus 1 extra for the
         * case when compressed size is larger than the original one. */
        workmem->buf = vmalloc(2 * PAGE_SIZE);
        if (!workmem->mem || !workmem->buf)
                goto fail;

        return workmem;
fail:
        workmem_free(workmem);
        return NULL;
}

/* get existing idle workmem or wait until other process release
 * (workmem_put()) one for us */
struct zcomp_workmem *wm_policy_workmem_get(struct zcomp_wm_policy *policy)
{
        struct zcomp_workmem *wm;
retry:
        spin_lock(&policy->buffer_lock);
        if (!list_empty(&policy->idle_workmem)) {
                wm = list_entry(policy->idle_workmem.next,
                                struct zcomp_workmem, list);
                list_del(&wm->list);
                spin_unlock(&policy->buffer_lock);
                return wm;
        } else {
                DEFINE_WAIT(wait);

                spin_unlock(&policy->buffer_lock);
                prepare_to_wait_exclusive(&policy->workmem_wait, &wait,
                                TASK_UNINTERRUPTIBLE);
                if (list_empty(&policy->idle_workmem))
                        schedule();
                finish_wait(&policy->workmem_wait, &wait);
                goto retry;
        }
        /* should never happen */
        return NULL;
}

/* add workmem back to idle list and wake up waiter (if any) */
void wm_policy_workmem_put(struct zcomp_wm_policy *policy,
                struct zcomp_workmem *workmem)
{
        spin_lock(&policy->buffer_lock);
        list_add_tail(&workmem->list, &policy->idle_workmem);
        spin_unlock(&policy->buffer_lock);

        if (waitqueue_active(&policy->workmem_wait))
                wake_up(&policy->workmem_wait);
}

int wm_policy_init(struct zcomp_wm_policy *policy, size_t sz)
{
        struct zcomp_workmem *wm;

        spin_lock_init(&policy->buffer_lock);
        INIT_LIST_HEAD(&policy->idle_workmem);
        init_waitqueue_head(&policy->workmem_wait);

        /* allocate at least one workmem during initialisation,
         * so zram write() will not get into trouble in case of
         * low memory */
        wm = workmem_alloc(sz);
        if (!wm)
                return -EINVAL;
        list_add_tail(&wm->list, &policy->idle_workmem);
        return 0;
}

void wm_policy_free(struct zcomp_wm_policy *policy)
{
        struct zcomp_workmem *wm;
        while (!list_empty(&policy->idle_workmem)) {
                wm = list_entry(policy->idle_workmem.next,
                                struct zcomp_workmem, list);
                list_del(&wm->list);
                workmem_free(wm);
        }
}

/* free allocated workmem buffers and zram_comp */
void zcomp_destroy(struct zram_comp *comp)
{
        comp->destroy(comp);
        kfree(comp);
}

/* search available compressors for requested algorithm.
 * allocate new zram_comp and initialize it. return NULL
 * if requested algorithm is not supported or in case
 * of init error */
struct zram_comp *zcomp_create(const char *compress)
{
        struct zram_comp *comp;
        int i;

        BUILD_BUG_ON(ARRAY_SIZE(compressors) == 1);

        for (i = 0; i < ARRAY_SIZE(compressors) - 1; i++) {
                if (sysfs_streq(compress, compressors[i].name))
                        break;
        }
        /* nothing found */
        if (i == ARRAY_SIZE(compressors) - 1)
                return NULL;

        comp = kzalloc(sizeof(struct zram_comp), GFP_KERNEL);
        if (!comp)
                return NULL;

        comp->name = compressors[i].name;
        comp->create = compressors[i].create;
        comp->destroy = compressors[i].destroy;

        if (comp->create(comp)) {
                zcomp_destroy(comp);
                return NULL;
        }
        return comp;
}

/* show available compressors */
ssize_t zcomp_available_show(struct zram_comp *comp, char *buf)
{
        ssize_t sz = 0;
        int i;
        for (i = 0; i < ARRAY_SIZE(compressors) - 1; i++) {
                if (comp && !strcmp(comp->name, compressors[i].name))
                        sz += sprintf(buf + sz, "<%s> ", compressors[i].name);
                else
                        sz += sprintf(buf + sz, "%s ", compressors[i].name);
        }
        sz += sprintf(buf + sz, "\n");
        return sz;
}
