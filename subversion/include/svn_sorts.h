/*
 * svn_sorts.h :  all sorts of sorts.
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


#ifndef SVN_SORTS_H
#define SVN_SORTS_H

#include <apr_pools.h>
#include <apr_tables.h>         /* for apr_array_header_t */
#include <apr_hash.h>
#include <apr_file_io.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* This structure is used to hold a key/value from a hash table */
typedef struct {
  const void *key;      /* pointer to the key */
  apr_ssize_t klen;     /* size of the key */

  void *value;          /* pointer to the value */
} svn_item_t;


/* Compare two svn_stringbuf_t's, returning an integer greater than,
 * equal to, or less than 0, according as A is greater than, equal to,
 * or less than B.
 *
 * You can use this to do an in-place sort of an apr_array_header_t
 * HDR of svn_stringbuf_t's like so:
 * 
 *   qsort (HDR->elts, HDR->nelts, HDR->elt_size,
 *          svn_path_compare_strings_as_paths);
 */
int svn_sort_compare_strings_as_paths (const void *a, const void *b);


/* Compare two svn_item_t's, returning an integer greater than,
 * equal to, or less than 0, according as A is greater than, equal to,
 * or less than B.
 * 
 * This is useful for converting a hash into a sorted
 * apr_array_header_t.  For example, to convert hash HSH to a sorted
 * array, do this:
 * 
 *   apr_array_header_t *HDR;
 *   HDR = apr_hash_sorted_keys (HSH, svn_sort_compare_items_as_paths, pool);
 */
int svn_sort_compare_items_as_paths (const svn_item_t *a, const svn_item_t *b);


/* Compare two svn_revnum_t's, returning an integer greater than, equal
 * to, or less than 0, according as B is greater than, equal to, or less
 * than A. Note that this sorts newest revsion to oldest (IOW, descending
 * order).
 *
 * This is useful for converting an array of revisions into a sorted
 * apr_array_header_t. You are responsible for detecting, preventing or
 * removing duplicates.
 */
int svn_sort_compare_revisions (const void *a, const void *b);


#ifndef apr_hash_sorted_keys
/* Sort HT according to its keys, return an apr_array_header_t
   containing svn_item_t structures holding those keys and values
   (i.e. for each svn_item_t ITEM in the returned array, ITEM->key
   and is the ITEM->size are the hash key, and ITEM->data points to
   the hash value).

   Storage is shared with the original hash, not copied.

   COMPARISON_FUNC should take two svn_item_t's and return an integer
   greater than, equal to, or less than 0, according as the first item
   is greater than, equal to, or less than the second.

   NOTE:
   This function and the svn_item_t should go over to APR. Got a Round Tuit?
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

#endif /* SVN_SORTS_H */
