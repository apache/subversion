/*
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

/* gcc hash-test.c -I/usr/include/apr-1 -Isubversion/include -lsvn_subr-1 -lapr-1

   Shows how bad the standard APR hash function can be for 4/8-byte
   svn_revnum_t keys. Putting the first 1,000,000 revisions into a
   hash table reveals that 96% of the keys end up in chains with 6 or
   7 hash collisions, that means almost all hash lookups degrade to a
   linked list scan.

   Subversion has a different hash function available, accessed by
   using svn_hash__make() instead of apr_hash_make(), that doesn't use
   a seed and it is much better for svn_revnum_t keys. Another option
   would be to use the svn_revnum_t values directly as keys with a
   no-op hash function.

 */

#include <apr_pools.h>
#include <apr_hash.h>
#include <stdlib.h>
#include <stdio.h>
#include "private/svn_subr_private.h"

struct e_t {
  struct e_t *next;
  unsigned int hash;
  const void *key;
  apr_ssize_t klen;
  const void *val;
};

struct i_t {
  struct h_t *h;
  struct e_t *t;
  struct e_t *n;
  unsigned int i;
};

struct h_t {
  apr_pool_t *pool;
  struct e_t **array;
  struct i_t it;
  unsigned int count;
  unsigned int max;
  unsigned int seed;
  void *func;
  struct e_t *free;
};

static void test_hash(apr_hash_t *hash, const char *name)
{
  struct h_t *hack = (struct h_t *)hash;
  int num = 0, max = 0, running = 0, i;
  const int hist_len = 15;
  int hist[hist_len];

  for (i = 0; i < hist_len; ++i)
    hist[i] = 0;

  for (i = 0; i <= hack->max; ++i)
    {
      struct e_t *e = hack->array[i];
      int j = 0;
      while (e)
        {
          ++j;
          ++num;
          e = e->next;
        }
      if (j)
        {
          if (j > max)
            max = j;
          if (j < hist_len)
            ++hist[j - 1];
        }
    }

  printf("--\n%s\n--\nalloc:%d entries:%d seed:%0x\nhistogram\n",
         name, hack->max, hack->count, hack->seed);
  for (i = 0; i < hist_len; ++i)
    printf("%d ", hist[i]);
  printf("\ncummulative\n");
  for (i = 0; i < hist_len && running < hack->count; ++i)
    {
      running += (i + 1) * hist[i];
      printf("%0.2f ", ((float)running)/num);
    }
  printf("\nlongest:%d found:%d\n", max, num);
}

unsigned int
hash_simple64(const char *key, apr_ssize_t *klen)
{
  unsigned int *p = (unsigned int *)key;
  return p[0] ^ p[1];
}


int main(int argc, char *argv[])
{
  apr_pool_t *pool;
  apr_hash_t *hash;
  long i;
  long min = 1;
  long max = 1000000;

  if (argc > 1)
    min = atol(argv[1]);
  if (argc > 2)
    max = atol(argv[2]);
  apr_initialize();
  apr_pool_create(&pool, NULL);

  hash = apr_hash_make(pool);
  for (i = min; i <= max; ++i)
    {
      apr_int32_t *mapped = apr_palloc(pool, sizeof(apr_int32_t) * 2);
      mapped[0] = i;
      mapped[1] = i + 1;
      apr_hash_set(hash, mapped, sizeof(apr_int32_t), mapped + 1);
    }
  test_hash(hash, "apr 32-bit keys");
  apr_pool_clear(pool);

  hash = apr_hash_make(pool);
  for (i = min; i <= max; ++i)
    {
      apr_int64_t *mapped = apr_palloc(pool, sizeof(apr_int64_t) * 2);
      mapped[0] = i;
      mapped[1] = i + 1;
      apr_hash_set(hash, mapped, sizeof(apr_int64_t), mapped + 1);
    }
  test_hash(hash, "apr 64-bit keys");
  apr_pool_clear(pool);

  hash = svn_hash__make(pool);
  for (i = min; i <= max; ++i)
    {
      apr_int32_t *mapped = apr_palloc(pool, sizeof(apr_int32_t) * 2);
      mapped[0] = i;
      mapped[1] = i + 1;
      apr_hash_set(hash, mapped, sizeof(apr_int32_t), mapped + 1);
    }
  test_hash(hash, "svn 32-bit keys");
  apr_pool_clear(pool);

  hash = svn_hash__make(pool);
  for (i = min; i <= max; ++i)
    {
      apr_int64_t *mapped = apr_palloc(pool, sizeof(apr_int64_t) * 2);
      mapped[0] = i;
      mapped[1] = i + 1;
      apr_hash_set(hash, mapped, sizeof(apr_int64_t), mapped + 1);
    }
  test_hash(hash, "svn 64-bit keys");
  apr_pool_clear(pool);

  hash = apr_hash_make_custom(pool, hash_simple64);
  for (i = min; i <= max; ++i)
    {
      apr_int64_t *mapped = apr_palloc(pool, sizeof(apr_int64_t) * 2);
      mapped[0] = i;
      mapped[1] = i + 1;
      apr_hash_set(hash, mapped, sizeof(apr_int64_t), mapped + 1);
    }
  test_hash(hash, "simple 64-bit keys");
  apr_pool_clear(pool);

  apr_pool_destroy(pool);
  return 0;
}
