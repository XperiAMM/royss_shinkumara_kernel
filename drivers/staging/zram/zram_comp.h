/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ZRAM_COMP_H_
#define _ZRAM_COMP_H_

#include <linux/types.h>
#include <linux/spinlock.h>

struct zcomp_workmem {
        void *mem;      /* algorithm workmem */
        void *buf;     /* compression/decompression buffer */
        struct list_head list;
};

/* default device's compressing backend workmem control
 * policy (usually device ->private)*/
struct zcomp_wm_policy {
        /* protect workmem list */
        spinlock_t buffer_lock;
        /* list of available workmems */
        struct list_head idle_workmem;
        wait_queue_head_t workmem_wait;
};

/* workmem get() and put() for default zcomp_wm_policy. compressing backend
 * may define its own wm policy and call custom get() and put() */
struct zcomp_workmem *wm_policy_workmem_get(struct zcomp_wm_policy *policy);
void wm_policy_workmem_put(struct zcomp_wm_policy *policy,
                        struct zcomp_workmem *workmem);

int wm_policy_init(struct zcomp_wm_policy *policy, size_t sz);
void wm_policy_free(struct zcomp_wm_policy *policy);

/* per-device compression frontend */
struct zram_comp {
        int (*compress)(const unsigned char *src, size_t src_len,
                        unsigned char *dst, size_t *dst_len, void *wrkmem);

        int (*decompress)(const unsigned char *src, size_t src_len,
                        unsigned char *dst, size_t *dst_len);

        struct zcomp_workmem *(*workmem_get)(struct zram_comp *comp);
        void (*workmem_put)(struct zram_comp *comp,
                        struct zcomp_workmem *workmem);

        int (*create)(struct zram_comp *);
        void (*destroy)(struct zram_comp *);

        void *private;
        const char *name;
};

struct zram_comp *zcomp_create(const char *comp);
void zcomp_destroy(struct zram_comp *comp);

ssize_t zcomp_available_show(struct zram_comp *comp, char *buf);
#endif /* _ZRAM_COMP_H_ */
