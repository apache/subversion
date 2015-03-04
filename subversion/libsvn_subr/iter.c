/* iter.c : iteration drivers
 *
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
 */


#include "svn_iter.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "private/svn_sorts_private.h"
#include "private/svn_dep_compat.h"

#include "svn_error_codes.h"

static svn_error_t internal_break_error =
  {
    SVN_ERR_ITER_BREAK, /* APR status */
    NULL, /* message */
    NULL, /* child error */
    NULL, /* pool */
    __FILE__, /* file name */
    __LINE__ /* line number */
  };

#if APR_VERSION_AT_LEAST(1, 4, 0)
struct hash_do_baton
{
  void *baton;
  svn_iter_apr_hash_cb_t func;
  svn_error_t *err;
  apr_pool_t *iterpool;
};

static
int hash_do_callback(void *baton,
                     const void *key,
                     apr_ssize_t klen,
                     const void *value)
{
  struct hash_do_baton *hdb = baton;

  svn_pool_clear(hdb->iterpool);
  hdb->err = (*hdb->func)(hdb->baton, key, klen, (void *)value, hdb->iterpool);

  return hdb->err == SVN_NO_ERROR;
}
#endif

svn_error_t *
svn_iter_apr_hash(svn_boolean_t *completed,
                  apr_hash_t *hash,
                  svn_iter_apr_hash_cb_t func,
                  void *baton,
                  apr_pool_t *pool)
{
#if APR_VERSION_AT_LEAST(1, 4, 0)
  struct hash_do_baton hdb;
  svn_boolean_t error_received;

  hdb.func = func;
  hdb.baton = baton;
  hdb.iterpool = svn_pool_create(pool);

  error_received = !apr_hash_do(hash_do_callback, &hdb, hash);

  svn_pool_destroy(hdb.iterpool);

  if (completed)
    *completed = !error_received;

  if (!error_received)
    return SVN_NO_ERROR;

  if (hdb.err->apr_err == SVN_ERR_ITER_BREAK
        && hdb.err != &internal_break_error)
    {
        /* Errors - except those created by svn_iter_break() -
           need to be cleared when not further propagated. */
        svn_error_clear(hdb.err);

        hdb.err = SVN_NO_ERROR;
    }

  return hdb.err;
#else
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, hash);
       ! err && hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      apr_ssize_t len;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, &len, &val);
      err = (*func)(baton, key, len, val, iterpool);
    }

  if (completed)
    *completed = ! err;

  if (err && err->apr_err == SVN_ERR_ITER_BREAK)
    {
      if (err != &internal_break_error)
        /* Errors - except those created by svn_iter_break() -
           need to be cleared when not further propagated. */
        svn_error_clear(err);

      err = SVN_NO_ERROR;
    }

  /* Clear iterpool, because callers may clear the error but have no way
     to clear the iterpool with potentially lots of allocated memory */
  svn_pool_destroy(iterpool);

  return err;
#endif
}

svn_error_t *
svn_iter_apr_array(svn_boolean_t *completed,
                   const apr_array_header_t *array,
                   svn_iter_apr_array_cb_t func,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  for (i = 0; (! err) && i < array->nelts; ++i)
    {
      void *item = array->elts + array->elt_size*i;

      svn_pool_clear(iterpool);

      err = (*func)(baton, item, iterpool);
    }

  if (completed)
    *completed = ! err;

  if (err && err->apr_err == SVN_ERR_ITER_BREAK)
    {
      if (err != &internal_break_error)
        /* Errors - except those created by svn_iter_break() -
           need to be cleared when not further propagated. */
        svn_error_clear(err);

      err = SVN_NO_ERROR;
    }

  /* Clear iterpool, because callers may clear the error but have no way
     to clear the iterpool with potentially lots of allocated memory */
  svn_pool_destroy(iterpool);

  return err;
}

/* Note: Although this is a "__" function, it is in the public ABI, so
 * we can never remove it or change its signature. */
svn_error_t *
svn_iter__break(void)
{
  return &internal_break_error;
}

#if !APR_VERSION_AT_LEAST(1, 5, 0)
const void *apr_hash_this_key(apr_hash_index_t *hi)
{
  const void *key;

  apr_hash_this((apr_hash_index_t *)hi, &key, NULL, NULL);
  return key;
}

apr_ssize_t apr_hash_this_key_len(apr_hash_index_t *hi)
{
  apr_ssize_t klen;

  apr_hash_this((apr_hash_index_t *)hi, NULL, &klen, NULL);
  return klen;
}

void *apr_hash_this_val(apr_hash_index_t *hi)
{
  void *val;

  apr_hash_this((apr_hash_index_t *)hi, NULL, NULL, &val);
  return val;
}
#endif


/* ====================================================================== */

svn_array_t *
svn_array_make(apr_pool_t *pool)
{
  return apr_array_make(pool, 0, sizeof(void *));
}

svn_array_t *
svn_array_make_n(apr_pool_t *pool, int elements)
{
  return apr_array_make(pool, elements, sizeof(void *));
}

/* ### inefficient implementation */
void
svn_array_ensure(svn_array_t *array, int elements)
{
  int old_nelts = array->nelts;

  while (array->nalloc < elements)
    apr_array_push(array);
  array->nelts = old_nelts;
}

apr_array_header_t *
svn_array_dup_shallow(const apr_array_header_t *array,
                      apr_pool_t *pool)
{
  apr_array_header_t *new_array = apr_array_copy(pool, array);

  return new_array;
}

/* ### This implementation comes from ptr_array_dup(), a local helper
 *     for svn_rangelist_dup(). */
apr_array_header_t *
svn_array_dup_simple(const apr_array_header_t *array,
                     size_t object_size,
                     apr_pool_t *pool)
{
  apr_array_header_t *new_array = apr_array_make(pool, array->nelts,
                                                 sizeof(void *));

  /* allocate target range buffer with a single operation */
  char *copy = apr_palloc(pool, object_size * array->nelts);

  /* for efficiency, directly address source and target reference buffers */
  void **source = (void **)(array->elts);
  void **target = (void **)(new_array->elts);
  int i;

  /* copy ranges iteratively and link them into the target range list */
  for (i = 0; i < array->nelts; i++)
    {
      target[i] = &copy[i * object_size];
      memcpy(target[i], source[i], object_size);
    }
  new_array->nelts = array->nelts;

  return new_array;
}

apr_array_header_t *
svn_array_dup_compound(const apr_array_header_t *array,
                       void *(*element_dup_func)(const void *, apr_pool_t *),
                       apr_pool_t *pool)
{
  apr_array_header_t *new_array = apr_array_make(pool, array->nelts,
                                                 sizeof(void *));
  int i;

  for (i = 0; i < array->nelts; i++)
    {
      svn_array_set(new_array, i,
                    element_dup_func(svn_array_get(array, i), pool));
    }
  new_array->nelts = array->nelts;

  return new_array;
}

void *
svn_array_get(const svn_array_t *array,
              int i)
{
  return APR_ARRAY_IDX(array, i, void *);
}

void
svn_array_set(svn_array_t *array,
              int i,
              const void *value)
{
  APR_ARRAY_IDX(array, i, const void *) = value;
}

/* Clean up internal state of IT. */
static void
iter_cleanup(svn_iter_t *it)
{
  if (it->iterpool)
    svn_pool_destroy(it->iterpool);
#ifdef SVN_DEBUG
  memset(it, 0, sizeof(*it));
#endif
}

svn_iter_t *
svn_array__first(apr_pool_t *pool,
                 const svn_array_t *array)
{
  svn_iter_t *it;

  if (! array->nelts)
    return NULL;

  it = apr_pcalloc(pool, sizeof(*it));
  it->array = array;
  it->i = 0;
  it->val = svn_array_get(array, 0);
  it->iterpool = svn_pool_create(pool);
  return it;
}

svn_iter_t *
svn_array__sorted_first(apr_pool_t *pool,
                        const svn_array_t *array,
                        int (*comparison_func)(const void *,
                                               const void *))
{
  svn_array_t *new_array = svn_array_dup_shallow(array, pool);

  svn_sort__array(new_array, comparison_func);
  return svn_array__first(pool, new_array);
}

svn_iter_t *
svn_array__next(svn_iter_t *it)
{
  it->i++;
  if (it->i >= it->array->nelts)
    {
      iter_cleanup(it);
      return NULL;
    }

  it->val = svn_array_get(it->array, it->i);
  svn_pool_clear(it->iterpool);
  return it;
}


/* ====================================================================== */

/* Exercise the svn_array API. */

static void *test_dup_cstring(const void *s, apr_pool_t *pool)
{
  return apr_pstrdup(pool, s);
}

static void
test_array(apr_pool_t *pool)
{
  svn_array_t *a = svn_array_make(pool);
  const char *str;
  const svn_array_t *b;

  /* get and set and push */
  SVN_ARRAY_PUSH(a) = "hello";
  str = svn_array_get(a, 0);
  svn_array_set(a, 1, str);
  SVN_ARRAY_PUSH(a) = str;

  /* duplication */
  b = svn_array_dup_shallow(a, pool);
  b = SVN_ARRAY_DUP_SIMPLE(b, char *, pool);
  b = svn_array_dup_compound(b, test_dup_cstring, pool);

  /* iteration with svn_array__[sorted_]first() */
  {
    svn_iter_t *i;

    for (i = svn_array__first(pool, a); i; i = svn_array__next(i))
      printf("%s", (char *)i->val);
    for (i = svn_array__sorted_first(pool, b, svn_sort_compare_paths);
         i; i = svn_array__next(i))
      printf("%s", (char *)i->val);
  }

  /* iteration, typed, with SVN_ARRAY_ITER[_SORTED]() */
  {
    SVN_ITER_T(char) *ia;
    SVN_CONST_ITER_T(char) *ib;

    for (SVN_ARRAY_ITER(ia, a, pool))
      printf("%s", ia->val);
    for (SVN_ARRAY_ITER_SORTED(ib, b, svn_sort_compare_paths, pool))
      printf("%s", ib->val);
  }
}


/* ====================================================================== */

svn_iter_t *
svn_hash__first(apr_pool_t *pool,
                apr_hash_t *ht)
{
  svn_iter_t *hi = apr_pcalloc(pool, sizeof(*hi));

  hi->array = NULL;

  hi->apr_hi = apr_hash_first(pool, ht);
  if (! hi->apr_hi)
    return NULL;

  apr_hash_this(hi->apr_hi, (const void **)&hi->key, &hi->klen, &hi->val);
  hi->iterpool = svn_pool_create(pool);
  return hi;
}

svn_iter_t *
svn_hash__sorted_first(apr_pool_t *pool,
                       apr_hash_t *ht,
                       int (*comparison_func)(const svn_sort__item_t *,
                                              const svn_sort__item_t *))
{
  svn_iter_t *hi = apr_palloc(pool, sizeof(*hi));

  hi->apr_hi = NULL;

  if (apr_hash_count(ht) == 0)
    return NULL;

  hi->array = svn_sort__hash(ht, comparison_func, pool);
  hi->i = 0;
  hi->key = APR_ARRAY_IDX(hi->array, hi->i, svn_sort__item_t).key;
  hi->klen = APR_ARRAY_IDX(hi->array, hi->i, svn_sort__item_t).klen;
  hi->val = APR_ARRAY_IDX(hi->array, hi->i, svn_sort__item_t).value;
  hi->iterpool = svn_pool_create(pool);
  return hi;
}

svn_iter_t *
svn_hash__next(svn_iter_t *hi)
{
  if (hi->apr_hi)  /* is an unsorted iterator */
    {
      hi->apr_hi = apr_hash_next(hi->apr_hi);
      if (! hi->apr_hi)
        {
          iter_cleanup(hi);
          return NULL;
        }
      apr_hash_this(hi->apr_hi, (const void **)&hi->key, &hi->klen, &hi->val);
    }

  if (hi->array)  /* is a sorted iterator */
    {
      hi->i++;
      if (hi->i >= hi->array->nelts)
        {
          iter_cleanup(hi);
          return NULL;
        }
      hi->key = APR_ARRAY_IDX(hi->array, hi->i, svn_sort__item_t).key;
      hi->klen = APR_ARRAY_IDX(hi->array, hi->i, svn_sort__item_t).klen;
      hi->val = APR_ARRAY_IDX(hi->array, hi->i, svn_sort__item_t).value;
    }

  svn_pool_clear(hi->iterpool);
  return hi;
}


/* ====================================================================== */

/* Exercise the svn_hash API. */
static void
test_hash(apr_pool_t *pool)
{
  apr_hash_t *hash = apr_hash_make(pool);
  svn_iter_t *hi;
  SVN_ITER_T(const char) *it;

  svn_hash_sets(hash, "K1", "V1");
  svn_hash_sets(hash, "Key2", "Value2");
  svn_hash_sets(hash, "Key three", "Value three");
  svn_hash_sets(hash, "Fourth key", "Fourth value");

  printf("Hash iteration, unsorted:");
  for (hi = svn_hash__first(pool, hash); hi; hi = svn_hash__next(hi))
    {
      const char *k = apr_psprintf(hi->iterpool, "[%s]", hi->key);
      apr_ssize_t l = hi->klen;
      const char *v = hi->val;

      printf("  key[%d]: %-20s val: %s\n", (int)l, k, v);
    }

  printf("Hash iteration, sorted:");
  for (hi = svn_hash__sorted_first(pool, hash, svn_sort_compare_items_lexically);
       hi; hi = svn_hash__next(hi))
    {
      const char *k = apr_psprintf(hi->iterpool, "[%s]", hi->key);
      apr_ssize_t l = hi->klen;
      const char *v = hi->val;

      printf("  key[%d]: %-20s val: %s\n", (int)l, k, v);
    }

  printf("Hash iteration, typed, unsorted:");
  for (SVN_HASH_ITER(it, pool, hash))
    {
      printf("  key[%d]: %-20s val: %s\n",
             (int)it->klen,
             apr_psprintf(it->iterpool, "[%s]", it->key),
             it->val);
    }

  printf("Hash iteration, typed, sorted:");
  for (SVN_HASH_ITER_SORTED(it, hash, svn_sort_compare_items_lexically, pool))
    {
      printf("  key[%d]: %-20s val: %s\n",
             (int)it->klen,
             apr_psprintf(it->iterpool, "[%s]", it->key),
             it->val);
    }

}
