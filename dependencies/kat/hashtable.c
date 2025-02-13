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

#include "hashtable.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/**
 * @brief Initializes the hashtable.
 *
 * @param ht Hashtable structure pointer to be
 * initialized.
 *
 * @return Returns 0 if success and a negative number
 * otherwise.
 */
int hashtable_init(
                   struct hashtable **ht,
                   void (*setup_algorithm)(struct hashtable **ht)
                   )
{
    struct hashtable *out;
    out = calloc(1, sizeof(struct hashtable));
    if (out == NULL)
        return (-1);
    
    out->capacity = HASHTABLE_DEFAULT_SIZE;
    out->elements = 0;
    out->bucket = calloc(out->capacity, sizeof(void *));
    if (out->bucket == NULL)
    {
        free(out);
        return (-1);
    }
    
    /*
     * Setup algorith, comparator and key size.
     *
     * If NULL, uses splitmix64 as default.
     */
    if (setup_algorithm != NULL)
        setup_algorithm(&out);
    else
        hashtable_splitmix64_setup(&out);
    
    *ht = out;
    return (0);
}

/**
 * @brief Deallocates the hashtable.
 *
 * @param ht Hashtable pointer.
 * @param dealloc Deallocation indicator: if different from 0, also
 *        deallocates the value, if 0 not.
 *
 * @return Returns 0 if success.
 */
int hashtable_finish(struct hashtable **ht, int dealloc)
{
    struct hashtable *h;  /* Hashtable.    */
    struct list *l_ptr;   /* List pointer. */
    
    h = *ht;
    
    /* Invalid array. */
    if (h == NULL)
        return (-1);
    
    /*
     * For each bucket, deallocates each list.
     */
    for (size_t i = 0; i < h->capacity; i++)
    {
        struct list *l_ptr_next;
        
        l_ptr = h->bucket[i];
        while (l_ptr != NULL)
        {
            l_ptr_next = l_ptr->next;
            if (dealloc)
                free(l_ptr->value);
            
            free(l_ptr);
            l_ptr = l_ptr_next;
        }
    }
    
    free(h->bucket);
    free(h);
    return (0);
}

/**
 * @brief Calculates the bucket size accordingly with the
 * current hash function set and the current hashtable
 * capacity.
 *
 * @param ht Hashtable pointer.
 * @param key Key to be hashed.
 *
 * @return Returns a value between [0,h->capacity] that
 * should be used to address the target bucket for the
 * hash table.
 */
inline static size_t hashtable_bucket_index(struct hashtable **ht, const void *key)
{
    struct hashtable *h;  /* Hashtable.    */
    uint64_t hash;        /* Hash value.   */
    size_t index;         /* Bucket index. */
    
    /*
     * Its important to note that the hashtable->capacity
     * will always be power of 2, thus, allowing the &-1
     * as a modulus operation.
     */
    h = *ht;
    hash  = h->hash(key, h->key_size);
    index = hash & (h->capacity - 1);
    
    return (index);
}

/**
 * @brief Adds the current @p key and @p value into the hashtable.
 *
 * @param ht Hashtable pointer.
 * @param key Key to be added.
 * @param value Value to be added.
 *
 * The hash table will grows twice whenever it reaches
 * its threshold of 60% percent.
 *
 * @return Returns 0 if success and a negative number otherwise.
 */
int hashtable_add(struct hashtable **ht, void *key, void *value)
{
    struct hashtable *h;    /* Hashtable.    */
    struct list *l_entry;   /* List entry.   */
    struct list *l_ptr;     /* List pointer. */
    struct list *l_ptr_nxt; /* List pointer. */
    size_t hash;            /* Hash value.   */
    
    h = *ht;
    
    /* Hashtable exists?. */
    if (h == NULL)
        return (-1);
    
    /* Asserts if we have space enough. */
    if (h->elements >= h->capacity*0.6)
    {
        size_t old_capacity;
        struct list **new_buckets;
        
        /* Allocate new buckets. */
        new_buckets = calloc(h->capacity << 1, sizeof(void *));
        if (new_buckets == NULL)
            return (-1);
        
        old_capacity  = h->capacity;
        h->capacity <<= 1;
        
#if HASHTABLE_DEBUG
        h->collisions = 0;
#endif
        
        /* Move elements to it. */
        for (size_t i = 0; i < old_capacity; i++)
        {
            l_ptr = h->bucket[i];
            while (l_ptr != NULL)
            {
                l_ptr_nxt = l_ptr->next;
                
                if (l_ptr->key != NULL)
                {
                    /* Recalculates the hash for each key inside the bucket. */
                    hash = hashtable_bucket_index(ht, l_ptr->key);
                    l_ptr->next = new_buckets[hash];
#if HASHTABLE_DEBUG
                    if(new_buckets[hash] != NULL)
                        h->collisions++;
#endif
                    new_buckets[hash] = l_ptr;
                }
                
                l_ptr = l_ptr_nxt;
            }
        }
        
        free(h->bucket);
        h->bucket = new_buckets;
    }
    
    /* Hash it. */
    hash = hashtable_bucket_index(ht, key);
    
    /*
     * Loops through the list in order to see if the key already exists,
     * if so, overwrite the value.
     */
    l_ptr = h->bucket[hash];
    while (l_ptr != NULL)
    {
        if (l_ptr->key != NULL && h->cmp(l_ptr->key, key) == 0)
        {
            l_ptr->value = value;
            return (0);
        }
        l_ptr = l_ptr->next;
    }
    
    /* Allocate a new list and adds into the appropriate location. */
    l_entry = calloc(1, sizeof(struct list));
    if (l_entry == NULL)
        return (-1);
    
    /* Fill the entry. */
    l_entry->key = key;
    l_entry->value = value;
    l_entry->hash = hash;
    l_entry->next = h->bucket[hash];
    
    /* Add into the top of the buckets list. */
#if HASHTABLE_DEBUG
    if (h->bucket[hash] != NULL)
        h->collisions++;
#endif
    
    h->bucket[hash] = l_entry;
    h->elements++;
    
    return (0);
}

/**
 * @brief Retrieves the value belonging to the parameter @p key.
 *
 * @param ht Hashtable pointer.
 * @param key Corresponding key to be retrieved.
 *
 * @return Returns the value belonging to the @p key, or NULL
 * if not found.
 */
void* hashtable_get(struct hashtable **ht, void *key)
{
    struct hashtable *h;   /* Hashtable.    */
    struct list *l_ptr;    /* List pointer. */
    uint64_t hash;         /* Hash value.   */
    
    h = *ht;
    
    /* Hashtable exists and has at least one element?. */
    if (h == NULL || h->elements < 1)
        return (NULL);
    
    /* Hash it. */
    hash = hashtable_bucket_index(ht, key);
    
    /*
     * Loops through the list in order to see if the key exists,
     * if so, gets the value.
     */
    l_ptr = h->bucket[hash];
    while (l_ptr != NULL)
    {
        if (l_ptr->key != NULL && h->cmp(l_ptr->key, key) == 0)
            return (l_ptr->value);
        
        l_ptr = l_ptr->next;
    }
    
    /* If not found, return NULL. */
    return (NULL);
}

/*===========================================================================*
 *                         -.- Hash functions -.-                            *
 *===========================================================================*/

/**
 * @brief Generic key comparator.
 *
 * Compares two given pointers and returns a negative, 0 or positive
 * number if less than, equal or greater for the keys specified.
 *
 * @param key1 First key to be compared.
 * @param key2 Second key to be compared.
 *
 * @returns Returns a negative, 0 or positive number if @p key1
 * is less than, equal or greater than @p key2.
 */
int hashtable_cmp_ptr(const void *key1, const void *key2)
{
    return ( (int)( ((uintptr_t)key1) - ((uintptr_t)key2) ) );
}

/**
 * @Brief Generic string key comparator.
 *
 * Compares two given strings and returns a negative, 0 or positive
 * number if less than, equal or greater for the keys specified.
 *
 * @param key1 First string key to be compared.
 * @param key2 Second string key to be compared.
 *
 * @returns Returns a negative, 0 or positive number if @p key1
 * is less than, equal or greater than @p key2.
 */
int hashtable_cmp_string(const void *key1, const void *key2)
{
    return (strcmp(key1, key2));
}

/*---------------------------------------------------------------------------*
 * sdbm Hash Functions                                                       *
 *---------------------------------------------------------------------------*/

/**
 * @brief sdbm algorithm.
 *
 * @param key String key to be hashed.
 *
 * @return Returns a 64-bit hashed number for the @p key argument.
 *
 * @note This algorithm shows a decent randomness, low collisions rate,
 * fast and easy to implement.
 *
 * More information about this and others:
 * https://softwareengineering.stackexchange.com/a/145633/
 */
uint64_t hashtable_sdbm(const void *key, size_t size)
{
    unsigned char *str;  /* String pointer.    */
    uint64_t hash;       /* Resulting hash.    */
    int c;               /* Current character. */
    ((void)size);
    
    str = (unsigned char *)key;
    hash = 0;
    
    while ((c = *str++) != 0)
        hash = c + (hash << 6) + (hash << 16) - hash;
    
    return (hash);
}

/**
 * @brief Setup for the sdbm hash function.
 *
 * @param ht Hashtable pointer.
 */
void hashtable_sdbm_setup(struct hashtable **ht)
{
    (*ht)->hash = hashtable_sdbm;
    (*ht)->cmp = hashtable_cmp_string;
    (*ht)->key_size = 1;
}

/*----------------------------------------------------------------------------*
 * Based in splitmix64, from Better Bit Mixing - Improving on MurmurHash3's   *
 * 64-bit Finalizer                                                           *
 *                                                                            *
 * Link:http://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html*
 * ----                                                                       *
 * I've found this particular interesting and the results showed that the     *
 * resulting hash is very close to MurMur3 ;-).                               *
 *----------------------------------------------------------------------------*/

/**
 * @brief splitmix64 based algorithm.
 *
 * This algorithm seems to be the 'Mix13' for the link above.
 *
 * @param key Key to be hashed.
 * @param size Pointer size (unused here).
 *
 * @return Returns a hashed number for the @p key argument.
 */
uint64_t hashtable_splitmix64_based(const void *key, size_t size)
{
    uint64_t x; /* Hash key. */
    ((void)size);
    
    x = (uint64_t)key;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
    x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
    x = x ^ (x >> 31);
    return (x);
}

/**
 * @brief Setup for the splitmix64 hash function.
 *
 * @param ht Hashtable pointer.
 */
void hashtable_splitmix64_setup(struct hashtable **ht)
{
    (*ht)->hash = hashtable_splitmix64_based;
    (*ht)->cmp = hashtable_cmp_ptr;
    (*ht)->key_size = sizeof(void *);
}

/*---------------------------------------------------------------------------*
 * MurMur3 Hash Functions                                                    *
 *---------------------------------------------------------------------------*/

/**
 * @brief Setup for the MurMur3 hash function.
 *
 * @param ht Hashtable pointer.
 */
void hashtable_MurMur3_setup(struct hashtable **ht)
{
    (*ht)->hash = hashtable_MurMur3_hash;
    (*ht)->cmp = hashtable_cmp_ptr;
    (*ht)->key_size = sizeof(void *);
}

/* ---------------- *
 * MurMur Helpers   *
 * ---------------- */

/**
 * @brief Left-rotate an unsigned 64-bit integer by
 * @p r times.
 *
 * @param x Number to be rotated.
 * @param r Number of times.
 *
 * @return Rotated number.
 */
static inline uint64_t rotl64(uint64_t x, int8_t r)
{
    return (x << r) | (x >> (64 - r));
}

/**
 * @brief 64-bit finalizer of MurMur3 Hash.
 *
 * @param k Intermediate hash value.
 *
 * @return Returns a hash value with higher entropy.
 */
static inline uint64_t fmix64(uint64_t k)
{
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccd;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53;
    k ^= k >> 33;
    return (k);
}

/**
 * @brief MurMur3 algorithm highly based in the publicly code
 * available in: https://github.com/aappleby/smhasher.
 *
 * This algorithm takes an input @p key and a @p size
 * and returns a hashed value of the pointer itself.
 *
 * @return Returns a hashed value of the key @p key.
 */
uint64_t hashtable_MurMur3_hash(const void *key, size_t size)
{
    const uint64_t nblocks = size / 4;
    
    /* Seeds are nulled for now. */
    uint64_t h1 = 0;
    uint64_t h2 = 0;
    
    const uint64_t c1 = 0x87c37b91114253d5;
    const uint64_t c2 = 0x4cf5ad432745937f;
    
    /* Body. */
    for(int i = 0; i < nblocks; i++)
    {
        uint64_t k1 = ((uint64_t) key >> (2 * i)) & 0xff;
        uint64_t k2 = rotl64(k1, 13);
        
        k1 *= c1;
        k1    = rotl64(k1, 31);
        k1 *= c2;
        h1 ^= k1;
        h1 = rotl64(h1, 27);
        h1 += h2;
        h1 = h1 * 5 + 0x52dce729;
        
        k2 *= c2;
        k2  = rotl64(k2, 33);
        k2 *= c1;
        h2 ^= k2;
        h2 = rotl64(h2, 31);
        h2 += h1;
        h2 = h2 * 5 + 0x38495ab5;
    }
    
    /*
     * Finalization
     * Since its power of two, the tail is not needed.
     */
    h1 ^= size; h2 ^= size;
    
    h1 += h2;
    h2 += h1;
    
    h1 = fmix64(h1);
    h2 = fmix64(h2);
    
    h1 += h2;
    h2 += h1;
    
    return (h1);
}

/*===========================================================================*
 *                               -.- Tests -.-                               *
 *===========================================================================*/

/* Array size test. */
#define ARRAY_SIZE  1024

/* Set to 1 to dump all buckets. */
#define DUMP_BUCKET 0

/* Considers each element as an integer value. */
#define BUCKET_AS_INTEGER 0

/**
 * @brief Print some statistics regarding the bucket usage, collisions
 * and etc, useful to know if the currently hash function used is
 * performing as expected.
 *
 * @param ht Hashtable pointer.
 */
void hashtable_print_stats(struct hashtable **ht)
{
    struct hashtable *h;        /* Hashtable.           */
    size_t used_buckets;        /* Used buckets.        */
    struct list *l_ptr;         /* List pointer.        */
    size_t max_bucket_size;     /* Max bucket size.     */
    size_t one_element_buckets; /* One-element buckets. */
    double elements_per_bucket; /* Elements per bucket. */
    double variance;            /* Variance.            */
    double stdev;               /* Standard Deviation.  */
    double mean;                /* Mean.                */
    
    /* Initialize. */
    max_bucket_size = 0;
    one_element_buckets = 0;
    elements_per_bucket = 0;
    used_buckets = 0;
    variance = 0.0;
    stdev = 0.0;
    mean = 0.0;
    
    h = *ht;
    
    /* If valid hashtable. */
    if (h == NULL || h->elements < 1)
        return;
    
#if DUMP_BUCKET
    printf("Buckets:\n");
#endif
    for (size_t i = 0; i < h->capacity; i++)
    {
        size_t bucket_size;
        bucket_size = 0;
        
#if DUMP_BUCKET
        printf("Bucket[%zu] = ", i);
#endif
        l_ptr = h->bucket[i];
        if (l_ptr != NULL)
            used_buckets++;
        
        while (l_ptr != NULL)
        {
            if (l_ptr->key != NULL && l_ptr->value != NULL)
            {
                elements_per_bucket++;
#if DUMP_BUCKET
#if BUCKET_AS_INTEGER
                printf("%d ", *((int *)l_ptr->value) );
#else
                printf("0x%" PRIx64 " ", (uint64_t)l_ptr->value);
#endif
#endif
            }
            l_ptr = l_ptr->next;
            bucket_size++;
        }
#if DUMP_BUCKET
        printf("\n");
#endif
        if (bucket_size > max_bucket_size)
            max_bucket_size = bucket_size;
        if (bucket_size == 1)
            one_element_buckets++;
        if (bucket_size > 0)
            variance += (bucket_size * bucket_size);
    }
    
    mean     = elements_per_bucket / used_buckets;
    variance = (variance - (used_buckets * (mean*mean))) / (used_buckets - 1);
    stdev    = sqrt(variance);
    
    printf("--------------------------- Stats ---------------------------\n");
    printf("Buckets available: %zu\n", h->capacity);
    printf("Used buckets: %zu (%f%%)\n", used_buckets,
           ((double)used_buckets/h->capacity)*100);
    printf("Max bucket size: %zu\n", max_bucket_size);
    printf("One element buckets: %zu (%f%%)\n", one_element_buckets,
           ((double)one_element_buckets/used_buckets)*100);
    printf("Mean elements per bucket: %f\n", mean);
    printf("    Variance: %f\n", variance);
    printf("    Standard Deviation: %f\n", stdev);
#if HASHTABLE_DEBUG
    printf("Collisions: %zu, elements: %zu\n", h->collisions, h->elements);
#endif
    printf("-------------------------------------------------------------\n");
}

#if 0
/**
 * @brief Hashtable integrity test.
 *
 * @param ht Hashtable pointer.
 *
 * @return Returns 0 if success and a negative number
 * otherwise.
 */
static int hashtable_integritytest(struct hashtable *ht)
{
    int *numbers; /* Numbers pointer.     */
    int *num;     /* Number poniter.      */
    
    /* Allocate a ARRAY_SIZE elements vector. */
    numbers = malloc(sizeof(int) * ARRAY_SIZE);
    if (numbers == NULL)
    {
        fprintf(stderr, "hashtable: failed to allocate an array\n");
        goto out2;
    }
    
    /* Fill in with previsible values. */
    for (int i = 0; i < ARRAY_SIZE; i++)
    {
        numbers[i] = i*10;
        if (hashtable_add(&ht, &numbers[i], &numbers[i]))
        {
            fprintf(stderr, "hashtable: unable to add key, number: %d, iter: %d\n",
                    numbers[i], i);
            goto out1;
        }
    }
    
    /* Check if all elements are retriavable. */
    for (int i = 0; i < ARRAY_SIZE; i++)
    {
        /* Attempt to get the number. */
        if ( ( num = hashtable_get(&ht, &numbers[i]) ) == NULL )
        {
            fprintf(stderr, "hashtable: unable to get key: %" PRIx64 ", iter: %d\n",
                    (uint64_t)&numbers[i], i);
            goto out1;
        }
        
        /* Make sure its as expected. */
        if (*num != i*10)
        {
            fprintf(stderr, "hashtable: error, num: %d / expected: %d, iter: %d\n",
                    *num, i*10, i);
            goto out1;
        }
    }
    
    /* --------------------------- Some stats --------------------------- */
    hashtable_print_stats(&ht);
    
    free(numbers);
    return (0);
out1:
    free(numbers);
    return (-1);
out2:
    return (-1);
}


/**
 * @brief Execute tests
 */
int main(void)
{
    struct hashtable *ht;
    
    /* Allocate hashtable. */
    if (hashtable_init(&ht, NULL))
    {
        fprintf(stderr, "hashtable: error while allocating hashtable\n");
        exit(EXIT_FAILURE);
    }
    
    /* Tests. */
    printf("Hashtable integrity test [%s]\n",
           !hashtable_integritytest(ht) ? "PASSED" : "FAILED");
    
    hashtable_finish(&ht, 0);
}
#endif
