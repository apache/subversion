/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_sorts.h
 * @brief all sorts of sorts.
 *
 * @{
 */


#ifndef SVN_SORTS_H
#define SVN_SORTS_H

#include <apr_pools.h>
#include <apr_tables.h>         /* for apr_array_header_t */
#include <apr_hash.h>
#include <apr_file_io.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** This structure is used to hold a key/value from a hash table */
typedef struct {
  /** pointer to the key */
  const void *key;

  /** size of the key */
  apr_ssize_t klen;

  /** pointer to the value */
  void *value;
} svn_item_t;


/** Compare two @c svn_item_t's.
 *
 * Compare two @c svn_item_t's, returning an integer greater than,
 * equal to, or less than 0, according as @a a is greater than, equal to,
 * or less than @a b.
 * 
 * This is useful for converting a hash into a sorted
 * @c apr_array_header_t.  For example, to convert hash @a hsh to a sorted
 * array, do this:
 * 
 *<pre>   apr_array_header_t *hdr;
 *   hdr = apr_hash_sorted_keys (hsh, @c svn_sort_compare_items_as_paths,
 *                               pool);</pre>
 *
 * The key strings must be null-terminated, even though klen does not
 * include the terminator.
 */
int svn_sort_compare_items_as_paths (const svn_item_t *a, const svn_item_t *b);


/** Compare two @c svn_revnum_t's.
 *
 * Compare two @c svn_revnum_t's, returning an integer greater than, equal
 * to, or less than 0, according as @a b is greater than, equal to, or less
 * than @a a. Note that this sorts newest revsion to oldest (IOW, descending
 * order).
 *
 * This is useful for converting an array of revisions into a sorted
 * @c apr_array_header_t. You are responsible for detecting, preventing or
 * removing duplicates.
 */
int svn_sort_compare_revisions (const void *a, const void *b);


#ifndef apr_hash_sorted_keys
/** Sort a hashtable according to it's keys and return an array.
 *
 * Sort @a ht according to its keys, return an @c apr_array_header_t
 * containing @c svn_item_t structures holding those keys and values
 * (i.e. for each @c svn_item_t @a item in the returned array, @a item->key
 * and is the @a item->size are the hash key, and @a item->data points to
 * the hash value).
 *
 * Storage is shared with the original hash, not copied.
 *
 * @a comparison_func should take two @c svn_item_t's and return an integer
 * greater than, equal to, or less than 0, according as the first item
 * is greater than, equal to, or less than the second.
 *
 * NOTE:
 * This function and the @a svn_item_t should go over to APR. Got a Round Tuit?
 */
apr_array_header_t *
apr_hash_sorted_keys (apr_hash_t *ht,
                      int (*comparison_func) (const svn_item_t *,
                                              const svn_item_t *),
                      apr_pool_t *pool);
#endif /* apr_hash_sorted_keys */

#ifdef __cplusplus
}
#endif /* __cplusplus */
/** @} */

#endif /* SVN_SORTS_H */
