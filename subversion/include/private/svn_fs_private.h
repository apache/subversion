/*
 * svn_fs_private.h: Private declarations for the filesystem layer to
 * be consumed by libsvn_fs* and non-libsvn_fs* modules.
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

#ifndef SVN_FS_PRIVATE_H
#define SVN_FS_PRIVATE_H

#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The maximum length of a transaction name.  The Berkeley DB backend
   generates transaction names from a sequence expressed as a base 36
   number with a maximum of MAX_KEY_SIZE (currently 200) bytes.  The
   FSFS backend generates transaction names of the form
   <rev>-<base 36-number> where the base 36 number is a sequence value
   with a maximum length of MAX_KEY_SIZE bytes.  The maximum length is
   212, but use 220 just to have some extra space:
     10   -> 32 bit revision number
     1    -> '-'
     200  -> 200 digit base 36 number
     1    -> '\0'
 */
#define SVN_FS__TXN_MAX_LEN 220

/** Retrieve the lock-tokens associated in the context @a access_ctx.
 * The tokens are in a hash keyed with <tt>const char *</tt> tokens,
 * and with <tt>const char *</tt> values for the paths associated.
 *
 * You should always use svn_fs_access_add_lock_token2() if you intend
 * to use this function.  The result of the function is not guaranteed
 * if you use it with the deprecated svn_fs_access_add_lock_token()
 * API.
 *
 * @since New in 1.6. */
apr_hash_t *
svn_fs__access_get_lock_tokens(svn_fs_access_t *access_ctx);


/**
 * Same as svn_fs_begin_txn2(), except it begins an obliteration-txn
 * that can be used to replace revision @a rev. @a rev must be a valid
 * revision number at the time of this call. This transaction cannot be
 * committed with a normal commit but only with
 * svn_fs__commit_obliteration_txn().
 *
 * @note You usually don't want to call this directly.
 * Instead, call svn_repos__obliterate_path_rev(), which honors the
 * repository's hook configurations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_fs__begin_obliteration_txn(svn_fs_txn_t **txn_p,
                               svn_fs_t *fs,
                               svn_revnum_t rev,
                               apr_pool_t *pool);


/** Commit the obliteration-txn @a txn. Similar to svn_fs_commit_txn() but
 * replaces the revision @a rev, which must be the same revision as was
 * specified when the transaction was begun. No conflict is possible.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_fs__commit_obliteration_txn(svn_revnum_t rev,
                                svn_fs_txn_t *txn,
                                apr_pool_t *pool);


/* Access the process-global (singleton) membuffer cache. The first call
 * will automatically allocate the cache using the current cache config.
 * NULL will be returned if the desired cache size is 0.
 *
 * @since New in 1.7.
 */
struct svn_membuffer_t *
svn_fs__get_global_membuffer_cache(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_PRIVATE_H */
