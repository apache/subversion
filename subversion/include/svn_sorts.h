/*
 * svn_sorts.h :  all sorts of sorts.
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




#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_SORTS_H
#define SVN_SORTS_H

#include <apr_pools.h>
#include <apr_tables.h>         /* for apr_array_header_t */
#include <apr_hash.h>
#include <apr_file_io.h>



/* Compare two svn_string_t's, returning an integer greater than,
 * equal to, or less than 0, according as A is greater than, equal to,
 * or less than B.
 *
 * You can use this to sort an apr_array_header_t HDR of svn_string_t's
 * like so:
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
int svn_sort_compare_items_as_paths (const void *a, const void *b);


#ifndef apr_hash_sorted_keys
/* Sort HT according to its keys, return an apr_array_header_t
 * containing svn_item_t's representing those keys and values (i.e.,
 * for each svn_item_t ITEM in the returned array, ITEM->key is the
 * hash key, and ITEM->size and ITEM->data contain the hash value).
 * 
 * Storage is shared with the original hash, not copied.
 *
 * COMPARISON_FUNC should take two svn_item_t's and return an integer
 * greater than, equal to, or less than 0, according as the first item
 * is greater than, equal to, or less than the second.
 *
 * NOTE:
 * This is here because apr_item_t was removed from apr, and we wanted
 * a structure like that to store the result of sorting a hash.  So we
 * use svn_item_t.  For historical reasons, this function is still in
 * the apr namespace.  Probably it should get renamespaced.  Heck,
 * some days I feel like getting renamespaced myself.
 */
apr_array_header_t *
apr_hash_sorted_keys (apr_hash_t *ht,
                      int (*comparison_func) (const void *, const void *),
                      apr_pool_t *pool);
#endif /* apr_hash_sorted_keys */


#endif /* SVN_SORTS_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
