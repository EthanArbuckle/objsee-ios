/*
 * MIT License
 *
 * Copyright (c) 2019 Davidson Francis <davidsondfgl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>

/**
 * @brief Hashtable initial size.
 */
#define HASHTABLE_DEFAULT_SIZE 16

/**
 * @brief Enable additional debug data, like collision counting.
 */
#define HASHTABLE_DEBUG 1

/**
 * @brief Hashtable linked list structure.
 */
struct list
{
    void *key;          /* Entry key.       */
    void *value;        /* Entry value.     */
    uint64_t hash;      /* Entry hash.      */
    struct list *next;  /* Entry next list. */
};

/**
 * @brief Hashtable structure.
 */
struct hashtable
{
    struct list **bucket; /* Bucket list.      */
    size_t capacity;      /* Current capacity. */
    size_t elements;      /* Current elements. */
    ssize_t key_size;     /* Hash key size.    */
    
#if HASHTABLE_DEBUG
    size_t collisions;    /* Collisions count. */
#endif
    
    /* Function pointers. */
    uint64_t (*hash) (const void *key, size_t size);
    int (*cmp) (const void *key1, const void *key2);
};

/* ==================== External functions ==================== */
extern int hashtable_init(
                          struct hashtable **ht,
                          void (*setup_algorithm)(struct hashtable **ht)
                          );

extern int hashtable_finish(struct hashtable **ht, int dealloc);
extern int hashtable_add(struct hashtable **ht, void *key, void *value);
extern void* hashtable_get(struct hashtable **ht, void *key);
extern void hashtable_print_stats(struct hashtable **ht);

/* ==================== Hash functions ==================== */
extern int hashtable_cmp_ptr(const void *key1, const void *key2);

/* sdbm. */
extern void hashtable_sdbm_setup(struct hashtable **ht);
extern uint64_t hashtable_sdbm(const void *key, size_t size);

/* Splitmix64. */
extern void hashtable_splitmix64_setup(struct hashtable **ht);
extern uint64_t hashtable_splitmix64_hash(const void *key, size_t size);

/* MurMur3 Hash. */
extern void hashtable_MurMur3_setup(struct hashtable **ht);
extern uint64_t hashtable_MurMur3_hash(const void *key, size_t size);

#endif /* HASHTABLE_H */
