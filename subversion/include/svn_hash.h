/*
 * svn_hash.h :  dumping and reading hash tables to/from files.
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


#ifndef SVN_HASH_H
#define SVN_HASH_H

#include <apr_pools.h>
#include <apr_tables.h>         /* for apr_array_header_t */
#include <apr_hash.h>
#include <apr_file_io.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The longest the "K <number>" line can be in one of our hashdump files. */
#define SVN_KEYLINE_MAXLEN 100


/*----------------------------------------------------*/

/*** Reading/writing hashtables to disk ***/

/* Read a hash table from SRCFILE, storing the resultants names and
 * values in HASH.  Use a POOL for all allocations.  HASH will have
 * const char * keys and svn_string_t * values.  
 */
apr_status_t svn_hash_read (apr_hash_t *hash, 
                            apr_file_t *srcfile,
                            apr_pool_t *pool);

/* Dump HASH to DESTFILE.  Use POOL for all allocations.  HASH has
 * const char * keys and svn_string_t values.  
 */
apr_status_t svn_hash_write (apr_hash_t *hash, 
                             apr_file_t *destfile,
                             apr_pool_t *pool);



/*** Taking the "diff" of two hash tables. ***/

/* Hash key status indicator for svn_hash_diff_func_t.  */
enum svn_hash_diff_key_status
  {
    svn_hash_diff_key_both,  /* Key is present in both hashes. */
    svn_hash_diff_key_a,     /* Key is present in first hash only. */
    svn_hash_diff_key_b      /* Key is present in second hash only. */
  };


/* Function type for expressing a key's status between two hash tables. */
typedef svn_error_t *(*svn_hash_diff_func_t)
       (const void *key, apr_ssize_t klen,
        enum svn_hash_diff_key_status status,
        void *baton);


/* For each key in the union of HASH_A's and HASH_B's keys, invoke
 * DIFF_FUNC exactly once, passing the key, the key's length, an enum
 * svn_hash_diff_key_status indicating which table(s) the key appears
 * in, and DIFF_FUNC_BATON.
 *
 * Process all keys of HASH_A first, then all remaining keys of HASH_B. 
 *
 * If DIFF_FUNC returns error, return that error immediately, without
 * applying DIFF_FUNC to anything else.
 *
 * HASH_A or HASH_B or both may be null; treat a null table as though
 * empty.
 *
 * Use POOL for temporary allocation.
 */
svn_error_t *svn_hash_diff (apr_hash_t *hash_a,
                            apr_hash_t *hash_b,
                            svn_hash_diff_func_t diff_func,
                            void *diff_func_baton,
                            apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_HASH_H */
