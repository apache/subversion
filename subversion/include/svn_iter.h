/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file svn_iter.h
 * @brief The Subversion Iteration drivers helper routines
 *
 */

#ifndef SVN_ITER_H
#define SVN_ITER_H

#include <apr.h>         /* for apr_ssize_t */
#include <apr_pools.h>   /* for apr_pool_t */
#include <apr_hash.h>    /* for apr_hash_t */
#include <apr_tables.h>  /* for apr_array_header_t */

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Callback function for use with svn_iter_apr_hash().
 * Use @a pool for temporary allocation, it's cleared between invocations.
 *
 * @a key, @a klen and @a val are the values normally retrieved with
 * apr_hash_this().
 *
 * @a baton is the baton passed into svn_iter_apr_hash().
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_iter_apr_hash_cb_t)(void *baton,
                                               const void *key,
                                               apr_ssize_t klen,
                                               void *val, apr_pool_t *pool);

/** Iterate over the elements in @a hash, calling @a func for each one until
 * there are no more elements or @a func returns an error.
 *
 * Uses @a pool for temporary allocations.
 *
 * If @a completed is not NULL, then on return - if @a func returns no
 * errors - @a *completed will be set to @c TRUE.
 *
 * If @a func returns an error other than @c SVN_ERR_ITER_BREAK, that
 * error is returned.  When @a func returns @c SVN_ERR_ITER_BREAK,
 * iteration is interrupted, but no error is returned and @a *completed is
 * set to @c FALSE (even if this iteration was the last one).
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_iter_apr_hash(svn_boolean_t *completed,
                  apr_hash_t *hash,
                  svn_iter_apr_hash_cb_t func,
                  void *baton,
                  apr_pool_t *pool);

/** Iteration callback used in conjunction with svn_iter_apr_array().
 *
 * Use @a pool for temporary allocation, it's cleared between invocations.
 *
 * @a baton is the baton passed to svn_iter_apr_array().  @a item
 * is a pointer to the item written to the array with the APR_ARRAY_PUSH()
 * macro.
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_iter_apr_array_cb_t)(void *baton,
                                                void *item,
                                                apr_pool_t *pool);

/** Iterate over the elements in @a array calling @a func for each one until
 * there are no more elements or @a func returns an error.
 *
 * Uses @a pool for temporary allocations.
 *
 * If @a completed is not NULL, then on return - if @a func returns no
 * errors - @a *completed will be set to @c TRUE.
 *
 * If @a func returns an error other than @c SVN_ERR_ITER_BREAK, that
 * error is returned.  When @a func returns @c SVN_ERR_ITER_BREAK,
 * iteration is interrupted, but no error is returned and @a *completed is
 * set to @c FALSE (even if this iteration was the last one).
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_iter_apr_array(svn_boolean_t *completed,
                   const apr_array_header_t *array,
                   svn_iter_apr_array_cb_t func,
                   void *baton,
                   apr_pool_t *pool);


/** Internal routine used by svn_iter_break() macro.
 */
svn_error_t *
svn_iter__break(void);


/** Helper macro to break looping in svn_iter_apr_array() and
 * svn_iter_apr_hash() driven loops.
 *
 * @note The error is just a means of communicating between
 *       driver and callback.  There is no need for it to exist
 *       past the lifetime of the iterpool.
 *
 * @since New in 1.5.
 */
#define svn_iter_break(pool) return svn_iter__break()


/* ====================================================================== */

/** Like apr_hash_get() but the hash key is an integer. */
void *
svn_int_hash_get(apr_hash_t *ht,
                 int key);

/** Like apr_hash_set() but the hash key is an integer. */
void
svn_int_hash_set(apr_hash_t *ht,
                 int key,
                 const void *val);

/** Like apr_hash_this_key() but the hash key is an integer. */
int
svn_int_hash_this_key(apr_hash_index_t *hi);


/* ====================================================================== */

/** A hash iterator for iterating over an array or a hash table in
 * its natural order or in sorted order.
 *
 * For an array, the @a i and @a val members provide the index and value
 * of the current item.
 *
 * For a hash table, the @c key, @c klen and @c val members provide the
 * key, key length (in bytes) and value of the current item.
 *
 * The @c iterpool member provides a managed iteration pool. It is cleared
 * at the start of each iteration and destroyed at the end of iteration.
 */
typedef struct svn_iter_t
{
  /* private: the original hash for unsorted hash iteration */
  apr_hash_index_t *apr_hi;

  /* private: the original or sorted array for array iteration, or an array
     of (svn_sort__item_t) hash items for sorted hash iteration */
  const apr_array_header_t *array;

  /* current element: iteration order index (array only; undefined for hash) */
  int i;
  /* current element: key (hash only; undefined for an array) */
  const char *key;
  /* current element: key length (hash only; undefined for an array) */
  apr_ssize_t klen;
  /* current element: value (array or hash) */
  void *val;

  /* iteration pool */
  apr_pool_t *iterpool;
} svn_iter_t;

/* Type-templated iterator.
 * ### Layout must be the same as svn_iter_t.
 */
#define SVN_ITER_T(element_target_type) \
  struct { \
    apr_hash_index_t *apr_hi; \
    const apr_array_header_t *array; \
    int i;  /* the current index into ARRAY */ \
    const char *key; \
    apr_ssize_t klen; \
    element_target_type *val; \
    apr_pool_t *iterpool; \
  }

/* Type-templated iterator with pointer to 'const' elements.
 * ### Layout must be the same as svn_iter_t.
 * ### One of the main uses should be to iterate over a const collection,
 *     but the iteration implementation doesn't currently distinguish const
 *     from non-const collections.
 */
#define SVN_CONST_ITER_T(element_target_type) \
  SVN_ITER_T(element_target_type const)

/* ====================================================================== */

/*
 * An array of pointers to objects.
 *
 *   - (void *) element type
 *   - an element may not be null
 *     ### TODO: Allow an element to be null.
 */

/** An array, assumed to be an array of pointers. */
typedef apr_array_header_t svn_array_t;

/** Return a new, empty array, allocated in @a pool. */
svn_array_t *
svn_array_make(apr_pool_t *pool);

/** Return a new, empty array, with initial space for @a elements elements,
 * allocated in POOL. The current number of elements is set to 0.
 *
 * @note This is for performance optimization.
 */
svn_array_t *
svn_array_make_n(apr_pool_t *pool, int elements);

/** Ensure the array has space for at least @a elements elements in total.
 * The current number of elements is unchanged.
 *
 * @note This is for performance optimization.
 */
void
svn_array_ensure(svn_array_t *array, int elements);

/** Shallow-copy an array of pointers to simple objects.
 *
 * Return a duplicate in @a pool of the array @a array of pointers.
 * Do not duplicate the pointed-to objects.
 */
apr_array_header_t *
svn_array_dup_shallow(const apr_array_header_t *array,
                      apr_pool_t *pool);

/** Deep-copy an array of pointers to simple objects.
 *
 * Return a duplicate in @a pool of the array @a array of pointers to
 * objects of size @a object_size bytes. Duplicate each object bytewise.
 */
apr_array_header_t *
svn_array_dup_simple(const apr_array_header_t *array,
                     size_t object_size,
                     apr_pool_t *pool);

/** Deep-copy an array of pointers to simple objects.
 *
 * Return a duplicate in @a pool of the array @a array of pointers to
 * objects of type @a element_type. Duplicate each object bytewise.
 */
#define SVN_ARRAY_DUP_SIMPLE(array, element_type, pool) \
  svn_array_dup_simple(array, sizeof(element_type), pool)

/** Deep-copy an array of pointers to compound objects.
 *
 * Return a duplicate in @a pool of the array @a array of pointers to
 * compound objects. Use @a element_dup_func to duplicate each element.
 *
 * ### A more efficient version could be offered, taking a "copy
 *     constructor" rather than a "dup" function for the elements,
 *     and using a hybrid of this implementation and the
 *     svn_array_dup_simple() implementation. (A copy constructor
 *     constructs a copy at a specified address.)
 */
apr_array_header_t *
svn_array_dup_compound(const apr_array_header_t *array,
                       void *(*element_dup_func)(const void *, apr_pool_t *),
                       apr_pool_t *pool);

/** Get element number @a i in @a array. */
void *
svn_array_get(const svn_array_t *array,
              int i);

/** Set element number @a i in @a array to @a val. */
void
svn_array_set(svn_array_t *array,
              int i,
              const void *val);

/* These pop/push macros are intended to be similar to the APR ones. */
#define svn_array_pop(array) ((void **)apr_array_pop(array))
#define svn_array_push(array) ((const void **)apr_array_push(array))
#define SVN_ARRAY_PUSH(array) (*(svn_array_push(array)))

/** Start iterating over the array @a array, in arbitrary order.
 *
 * Return an iterator, or null if there are no items in @a array.
 */
svn_iter_t *
svn_array__first(apr_pool_t *pool,
                 const svn_array_t *array);

/** Start iterating over the array @a array, in sorted order according to
 * @a comparison_func.  Return a pointer to the first element, or NULL if
 * there are no elements.
 *
 * It is permissible to change the original array @a array during the
 * iteration.  Doing so will not affect the sequence of elements returned
 * by svn_array__next(), as svn_array__sorted_first() takes a snapshot of
 * pointers to the original keys and values.  The memory in which the
 * original keys and values of HT are stored must remain available during
 * the iteration.
 */
svn_iter_t *
svn_array__sorted_first(apr_pool_t *pool,
                        const svn_array_t *array,
                        int (*comparison_func)(const void *,
                                               const void *));

/** Return a pointer to the next element of the array being iterated by
 * @a i, or NULL if there are no more elements.
 */
svn_iter_t *
svn_array__next(svn_iter_t *i);

/** Iteration over the array @a array.
 *
 * To be used in the parentheses of a for() loop.
 * @a i is the iterator variable declared with "SVN_[CONST_]ITER_T(type) *i;".
 */
#define SVN_ARRAY_ITER(i, array, pool) \
  i = (void *)svn_array__first(pool, array); \
  i; \
  i = (void *)svn_array__next((void *)i)

/** Like SVN_ARRAY_ITER but using the array's own pool. */
#define SVN_ARRAY_ITER_NO_POOL(i, array) \
  SVN_ARRAY_ITER(i, array, (array)->pool)

/** Like SVN_ARRAY_ITER but iterating over a copy of the array sorted by
 * @a comparison_func. */
#define SVN_ARRAY_ITER_SORTED(i, array, comparison_func, pool) \
  i = (void *)svn_array__sorted_first(pool, array, comparison_func); \
  i; \
  i = (void *)svn_array__next((void *)i)

/** Like SVN_ARRAY_SORTED but using the array's own pool. */
#define SVN_ARRAY_ITER_SORTED_NO_POOL(i, array, comparison_func) \
  SVN_ARRAY_ITER_SORTED(i, array, comparison_func, (array)->pool)

/* ====================================================================== */

/*
 * A hash table in which:
 *   - keys are assumed to be null-terminated (const char *) strings
 *   - iteration in sorted order is possible
 *   - an iteration pool is provided
 */

struct svn_sort__item_t;

/** Start iterating over the hash table @a ht, in arbitrary order.
 *
 * Similar to apr_hash_first().
 */
svn_iter_t *
svn_hash__first(apr_pool_t *pool,
                apr_hash_t *ht);

/** Start iterating over the hash table @a ht, in sorted order according to
 * @a comparison_func.  Return a pointer to the first element, or NULL if
 * there are no elements.
 *
 * It is permissible to change the original hash table @a ht during the
 * iteration.  Doing so will not affect the sequence of elements returned
 * by svn_hash__next(), as svn_hash__sorted_first() takes a snapshot of
 * pointers to the original keys and values.  The memory in which the
 * original keys and values of HT are stored must remain available during
 * the iteration.
 */
svn_iter_t *
svn_hash__sorted_first(apr_pool_t *pool,
                       apr_hash_t *ht,
                       int (*comparison_func)(const struct svn_sort__item_t *,
                                              const struct svn_sort__item_t *));

/** Return a pointer to the next element of the hash table being iterated by
 * @a hi, or NULL if there are no more elements.
 */
svn_iter_t *
svn_hash__next(svn_iter_t *hi);

/* Type-templated iteration.
 * To be used in the parentheses of a for() loop.
 * I is the iterator variable declared with "SVN_[CONST_]ITER_T(type) *i;".
 */
#define SVN_HASH_ITER(i, pool, ht) \
  i = (void *)svn_hash__first(pool, ht); \
  i; \
  i = (void *)svn_hash__next((void *)i)

#define SVN_HASH_ITER_NO_POOL(i, ht) \
  SVN_HASH_ITER(i, apr_hash_pool_get(ht), ht)

#define SVN_HASH_ITER_SORTED(i, ht, comparison_func, pool) \
  i = (void *)svn_hash__sorted_first(pool, ht, comparison_func); \
  i; \
  i = (void *)svn_hash__next((void *)i)

#define SVN_HASH_ITER_SORTED_NO_POOL(i, ht, comparison_func) \
  SVN_HASH_ITER_SORTED(i, ht, comparison_func, apr_hash_pool_get(ht))


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_ITER_H */
