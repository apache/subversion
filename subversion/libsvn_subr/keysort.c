/*
 * keysort.c:   convert a hash into a sorted array
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <string.h>       /* for strncmp() */
#include <stdlib.h>       /* for qsort()   */
#include "svn_string.h"
#include "svn_path.h"
#include "svn_hash.h"



/*** apr_hash_sorted_keys() ***/

/* (Should this be a permanent part of APR?)

   OK, folks, here's what's going on.  APR hash tables hash on
   key/klen objects, and store associated generic values.  They work
   great, but they have no ordering.

   The point of this exercise is to somehow arrange a hash's keys into
   an "ordered list" of some kind -- in this case, a nicely sorted
   one.

   We're using APR arrays, therefore, because that's what they are:
   ordered lists.  However, what "keys" should we put in the array?
   Clearly, (const char *) objects aren't general enough.  Or rather,
   they're not as general as APR's hash implementation, which stores
   (void *)/length as keys.  We don't want to lose this information.

   Therefore, it makes sense to store pointers to {void *, size_t}
   structures in our array.  No such apr object exists... BUT... if we
   can use a new type svn_item_t which contains {char *, size_t, void
   *}.  If store these objects in our array, we get the hash value
   *for free*.  When looping over the final array, we don't need to
   call apr_hash_get().  Major bonus!
 */


int
svn_sort_compare_as_paths (const void *obj1, const void *obj2)
{
  svn_item_t *item1, *item2;
  svn_string_t str1, str2;

  item1 = *((svn_item_t **) obj1);
  item2 = *((svn_item_t **) obj2);

  str1.data = item1->key;
  str1.len = item1->size;
  str1.pool = NULL;
  str2.data = item2->key;
  str2.len = item2->size;
  str2.pool = NULL;

  return svn_path_compare_paths (&str1, &str2, svn_path_local_style);
}


#ifndef apr_hash_sort_keys

/* see svn_hash.h for documentation */
apr_array_header_t *
apr_hash_sorted_keys (apr_hash_t *ht,
                      int (*comparison_func) (const void *, const void *),
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary;

  /* allocate an array with only one element to begin with. */
  ary = apr_array_make (pool, 1, sizeof(svn_item_t *));

  /* loop over hash table and push all keys into the array */
  for (hi = apr_hash_first (ht); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t klen;
      void *value;
      svn_item_t **receiver;
      svn_item_t *item = apr_pcalloc (pool, sizeof(*item));

      apr_hash_this (hi, &key, &klen, &value);
      item->key = (char *) key;
      item->size = klen;
      item->data = value;
      
      receiver = (svn_item_t **)apr_array_push (ary);
      *receiver = item;
    }
  
  /* now quicksort the array.  */
  qsort (ary->elts, ary->nelts, ary->elt_size, comparison_func);

  return ary;
}
#endif /* apr_hash_sort_keys */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
