/*
 * sorts.c:   all sorts of sorts
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include <assert.h>
#include "svn_string.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "svn_props.h"
#include "svn_error.h"



/*** svn_sort__hash() ***/

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
   can use a new type svn_sort__item_t which contains {char *, size_t, void
   *}.  If store these objects in our array, we get the hash value
   *for free*.  When looping over the final array, we don't need to
   call apr_hash_get().  Major bonus!
 */


int
svn_sort_compare_items_as_paths(const svn_sort__item_t *a,
                                const svn_sort__item_t *b)
{
  const char *astr, *bstr;

  astr = a->key;
  bstr = b->key;
  assert(astr[a->klen] == '\0');
  assert(bstr[b->klen] == '\0');
  return svn_path_compare_paths(astr, bstr);
}


int
svn_sort_compare_items_lexically(const svn_sort__item_t *a,
                                 const svn_sort__item_t *b)
{
  int val;
  apr_size_t len;

  /* Compare bytes of a's key and b's key up to the common length. */
  len = (a->klen < b->klen) ? a->klen : b->klen;
  val = memcmp(a->key, b->key, len);
  if (val != 0)
    return val;

  /* They match up until one of them ends; whichever is longer is greater. */
  return (a->klen < b->klen) ? -1 : (a->klen > b->klen) ? 1 : 0;
}


int
svn_sort_compare_revisions(const void *a, const void *b)
{
  svn_revnum_t a_rev = *(const svn_revnum_t *)a;
  svn_revnum_t b_rev = *(const svn_revnum_t *)b;

  if (a_rev == b_rev)
    return 0;

  return a_rev < b_rev ? 1 : -1;
}


int 
svn_sort_compare_paths(const void *a, const void *b)
{
  const char *item1 = *((const char * const *) a);
  const char *item2 = *((const char * const *) b);

  return svn_path_compare_paths(item1, item2);
}



apr_array_header_t *
svn_sort__hash(apr_hash_t *ht,
               int (*comparison_func)(const svn_sort__item_t *,
                                      const svn_sort__item_t *),
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary;

  /* allocate an array with enough elements to hold all the keys. */
  ary = apr_array_make(pool, apr_hash_count(ht), sizeof(svn_sort__item_t));

  /* loop over hash table and push all keys into the array */
  for (hi = apr_hash_first(pool, ht); hi; hi = apr_hash_next(hi))
    {
      svn_sort__item_t *item = apr_array_push(ary);

      apr_hash_this(hi, &item->key, &item->klen, &item->value);
    }
  
  /* now quicksort the array.  */
  qsort(ary->elts, ary->nelts, ary->elt_size,
        (int (*)(const void *, const void *))comparison_func);

  return ary;
}



/** Sorting properties **/

svn_boolean_t
svn_prop_is_svn_prop(const char *prop_name)
{
  return strncmp(prop_name, SVN_PROP_PREFIX, (sizeof(SVN_PROP_PREFIX) - 1)) 
         ? FALSE 
         : TRUE;
}


svn_prop_kind_t
svn_property_kind(int *prefix_len,
                  const char *prop_name)
{
  apr_size_t wc_prefix_len = sizeof(SVN_PROP_WC_PREFIX) - 1;
  apr_size_t entry_prefix_len = sizeof(SVN_PROP_ENTRY_PREFIX) - 1;

  if (strncmp(prop_name, SVN_PROP_WC_PREFIX, wc_prefix_len) == 0)
    {
      if (prefix_len)
        *prefix_len = wc_prefix_len;
      return svn_prop_wc_kind;     
    }

  if (strncmp(prop_name, SVN_PROP_ENTRY_PREFIX, entry_prefix_len) == 0)
    {
      if (prefix_len)
        *prefix_len = entry_prefix_len;
      return svn_prop_entry_kind;     
    }

  /* else... */
  if (prefix_len)
    *prefix_len = 0;
  return svn_prop_regular_kind;
}


svn_error_t *
svn_categorize_props(const apr_array_header_t *proplist,
                     apr_array_header_t **entry_props,
                     apr_array_header_t **wc_props,
                     apr_array_header_t **regular_props,
                     apr_pool_t *pool)
{
  int i;
  if (entry_props)
    *entry_props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  if (wc_props)
    *wc_props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  if (regular_props)
    *regular_props = apr_array_make(pool, 1, sizeof(svn_prop_t));

  for (i = 0; i < proplist->nelts; i++)
    {
      svn_prop_t *prop, *newprop;
      enum svn_prop_kind kind;
      
      prop = &APR_ARRAY_IDX(proplist, i, svn_prop_t);      
      kind = svn_property_kind(NULL, prop->name);
      newprop = NULL;

      if (kind == svn_prop_regular_kind)
        {
          if (regular_props)
            newprop = apr_array_push(*regular_props);
        }
      else if (kind == svn_prop_wc_kind)
        {
          if (wc_props)
            newprop = apr_array_push(*wc_props);
        }
      else if (kind == svn_prop_entry_kind)
        {
          if (entry_props)
            newprop = apr_array_push(*entry_props);
        }
      else
        /* Technically this can't happen, but might as well have the
           code ready in case that ever changes. */
        return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                                 "Bad property kind for property '%s'",
                                 prop->name);

      if (newprop)
        {
          newprop->name = prop->name;
          newprop->value = prop->value;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_prop_diffs(apr_array_header_t **propdiffs,
               apr_hash_t *target_props,
               apr_hash_t *source_props,
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary = apr_array_make(pool, 1, sizeof(svn_prop_t));

  /* Note: we will be storing the pointers to the keys (from the hashes)
     into the propdiffs array.  It is acceptable for us to
     reference the same memory as the base/target_props hash. */

  /* Loop over SOURCE_PROPS and examine each key.  This will allow us to
     detect any `deletion' events or `set-modification' events.  */
  for (hi = apr_hash_first(pool, source_props); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_string_t *propval1, *propval2;

      /* Get next property */
      apr_hash_this(hi, &key, &klen, &val);
      propval1 = val;

      /* Does property name exist in TARGET_PROPS? */
      propval2 = apr_hash_get(target_props, key, klen);

      if (propval2 == NULL)
        {
          /* Add a delete event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = NULL;
        }
      else if (! svn_string_compare(propval1, propval2))
        {
          /* Add a set (modification) event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = svn_string_dup(propval2, pool);
        }
    }

  /* Loop over TARGET_PROPS and examine each key.  This allows us to
     detect `set-creation' events */
  for (hi = apr_hash_first(pool, target_props); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_string_t *propval;

      /* Get next property */
      apr_hash_this(hi, &key, &klen, &val);
      propval = val;

      /* Does property name exist in SOURCE_PROPS? */
      if (NULL == apr_hash_get(source_props, key, klen))
        {
          /* Add a set (creation) event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = svn_string_dup(propval, pool);
        }
    }

  /* Done building our array of user events. */
  *propdiffs = ary;

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_prop_needs_translation(const char *propname)
{
  /* ### Someday, we may want to be picky and choosy about which
     properties require UTF8 and EOL conversion.  For now, all "svn:"
     props need it.  */

  return svn_prop_is_svn_prop(propname);
}
