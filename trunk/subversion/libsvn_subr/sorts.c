/*
 * sorts.c:   all sorts of sorts
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <string.h>       /* for strncmp() */
#include <stdlib.h>       /* for qsort()   */
#include "svn_string.h"
#include "svn_path.h"
#include "svn_sorts.h"



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
svn_sort_compare_items_as_paths (const svn_item_t *a, const svn_item_t *b)
{
  svn_stringbuf_t str_a, str_b;

  /* ### these are bogus! compare_paths ought to take svn_string_t */

  str_a.data = (char *)a->key;
  str_a.len = str_a.blocksize = a->klen;
  str_a.pool = NULL;

  str_b.data = (char *)b->key;
  str_b.len = str_b.blocksize = b->klen;
  str_b.pool = NULL;

  return svn_path_compare_paths (&str_a, &str_b);
}


int
svn_sort_compare_strings_as_paths (const void *a, const void *b)
{
  const svn_stringbuf_t *str_a = *((svn_stringbuf_t **) a);
  const svn_stringbuf_t *str_b = *((svn_stringbuf_t **) b);
  return svn_path_compare_paths (str_a, str_b);
}

int
svn_sort_compare_revisions (const void *a, const void *b)
{
  svn_revnum_t a_rev = *(svn_revnum_t *)a;
  svn_revnum_t b_rev = *(svn_revnum_t *)b;

  if (a_rev == b_rev)
    return 0;

  return a_rev < b_rev ? 1 : -1;
}


#ifndef apr_hash_sort_keys

/* see svn_sorts.h for documentation */
apr_array_header_t *
apr_hash_sorted_keys (apr_hash_t *ht,
                      int (*comparison_func) (const svn_item_t *,
                                              const svn_item_t *),
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary;

  /* allocate an array with only one element to begin with. */
  ary = apr_array_make (pool, 1, sizeof(svn_item_t));

  /* loop over hash table and push all keys into the array */
  for (hi = apr_hash_first (pool, ht); hi; hi = apr_hash_next (hi))
    {
      svn_item_t *item = apr_array_push (ary);

      apr_hash_this (hi, &item->key, &item->klen, &item->value);
    }
  
  /* now quicksort the array.  */
  qsort (ary->elts, ary->nelts, ary->elt_size,
         (int (*)(const void *, const void *))comparison_func);

  return ary;
}
#endif /* apr_hash_sort_keys */



/** Sorting properties **/

enum svn_prop_kind 
svn_property_kind (int *prefix_len,
                   const char *prop_name)
{
  apr_size_t wc_prefix_len = sizeof (SVN_PROP_WC_PREFIX) - 1;
  apr_size_t entry_prefix_len = sizeof (SVN_PROP_ENTRY_PREFIX) - 1;

  if (strncmp (prop_name, SVN_PROP_WC_PREFIX, wc_prefix_len) == 0)
    {
      *prefix_len = wc_prefix_len;
      return svn_prop_wc_kind;     
    }

  if (strncmp (prop_name, SVN_PROP_ENTRY_PREFIX, entry_prefix_len) == 0)
    {
      *prefix_len = entry_prefix_len;
      return svn_prop_entry_kind;     
    }

  /* else... */
  *prefix_len = 0;
  return svn_prop_regular_kind;
}


svn_error_t *
svn_categorize_props (const apr_array_header_t *proplist,
                      apr_array_header_t **entry_props,
                      apr_array_header_t **wc_props,
                      apr_array_header_t **regular_props,
                      apr_pool_t *pool)
{
  int i, len;
  *entry_props = apr_array_make (pool, 1, sizeof(svn_prop_t));
  *wc_props = apr_array_make (pool, 1, sizeof(svn_prop_t));
  *regular_props = apr_array_make (pool, 1, sizeof(svn_prop_t));

  for (i = 0; i < proplist->nelts; i++)
    {
      svn_prop_t *prop, *newprop;
      enum svn_prop_kind kind;
      
      prop = &APR_ARRAY_IDX(proplist, i, svn_prop_t);      
      kind = svn_property_kind (&len, prop->name);

      if (kind == svn_prop_regular_kind)
        newprop = apr_array_push (*regular_props);
      else if (kind == svn_prop_wc_kind)
        newprop = apr_array_push (*wc_props);
      else if (kind == svn_prop_entry_kind)
        newprop = apr_array_push (*entry_props);
      else
        return svn_error_createf (SVN_ERR_UNKNOWN_PROP_KIND, 0, NULL, pool,
                                  "svn_categorize_props: unknown prop kind "
                                  "for property '%s'", prop->name);
      newprop->name = prop->name;
      newprop->value = prop->value;
    }

  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
