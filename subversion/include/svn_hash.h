/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @file svn_hash.h
 * @brief Dumping and reading hash tables to/from files.
 */


#ifndef SVN_HASH_H
#define SVN_HASH_H

#include <apr_pools.h>
#include <apr_tables.h>         /* for apr_array_header_t */
#include <apr_hash.h>
#include <apr_file_io.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** The longest the "K <number>" line can be in one of our hashdump files. */
#define SVN_KEYLINE_MAXLEN 100


/*----------------------------------------------------*/

/** Reading/writing hashtables to disk
 *
 * @defgroup svn_hash_read_write reading and writing hashtables to disk
 * @{
 */

/** Read a hash table from a file.
 *
 * Read a hash table from @a srcfile, storing the resultants names and
 * values in @a hash.  Use a @a pool for all allocations.  @a hash will 
 * have <tt>const char *</tt> keys and <tt>svn_string_t *</tt> values.  
 */
apr_status_t svn_hash_read (apr_hash_t *hash, 
                            apr_file_t *srcfile,
                            apr_pool_t *pool);

/** Write a hash to a file.
 *
 * Dump @a hash to @a destfile.  Use @a pool for all allocations.  @a hash 
 * has <tt>const char *</tt> keys and <tt>svn_string_t *</tt> values.  
 */
apr_status_t svn_hash_write (apr_hash_t *hash, 
                             apr_file_t *destfile,
                             apr_pool_t *pool);

/** @} */


/** Taking the "diff" of two hash tables.
 *
 * @defgroup svn_hash_diff taking the diff of two hash tables.
 * @{
 */

/** Hash key status indicator for svn_hash_diff_func_t.  */
enum svn_hash_diff_key_status
  {
    /* Key is present in both hashes. */
    svn_hash_diff_key_both,

    /* Key is present in first hash only. */
    svn_hash_diff_key_a,

    /* Key is present in second hash only. */
    svn_hash_diff_key_b
  };


/** Function type for expressing a key's status between two hash tables. */
typedef svn_error_t *(*svn_hash_diff_func_t)
       (const void *key, apr_ssize_t klen,
        enum svn_hash_diff_key_status status,
        void *baton);


/** Take the diff of two hashtables.
 *
 * For each key in the union of @a hash_a's and @a hash_b's keys, invoke
 * @a diff_func exactly once, passing the key, the key's length, an enum
 * @c svn_hash_diff_key_status indicating which table(s) the key appears
 * in, and @a diff_func_baton.
 *
 * Process all keys of @a hash_a first, then all remaining keys of @a hash_b. 
 *
 * If @a diff_func returns error, return that error immediately, without
 * applying @a diff_func to anything else.
 *
 * @a hash_a or @a hash_b or both may be null; treat a null table as though
 * empty.
 *
 * Use @a pool for temporary allocation.
 */
svn_error_t *svn_hash_diff (apr_hash_t *hash_a,
                            apr_hash_t *hash_b,
                            svn_hash_diff_func_t diff_func,
                            void *diff_func_baton,
                            apr_pool_t *pool);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_HASH_H */
