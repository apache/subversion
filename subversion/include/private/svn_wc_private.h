/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
 * @endcopyright
 *
 * @file svn_wc_private.h
 * @brief The Subversion Working Copy Library - Internal routines
 *
 * Requires:
 *            - A working copy
 *
 * Provides:
 *            - Ability to manipulate working copy's versioned data.
 *            - Ability to manipulate working copy's administrative files.
 *
 * Used By:
 *            - Clients.
 */

#ifndef SVN_WC_PRIVATE_H
#define SVN_WC_PRIVATE_H

#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Same as svn_wc_entry() except that the entry returned
 * is a non @c NULL entry.
 *
 * Returns an error when svn_wc_entry() would have returned a @c NULL entry.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_wc__entry_versioned(const svn_wc_entry_t **entry,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        svn_boolean_t show_hidden,
                        apr_pool_t *pool);


/** Similar to svn_wc__get_entry() and svn_wc__entry_versioned().
 *
 * This function allows callers in libsvn_client to directly fetch entry data
 * without having to open up an adm_access baton.  Its error and return
 * semantics are the same as svn_wc__entry_versioned(), and parameters are the
 * same as svn_wc__get_entry() (defined in libsvn_wc/entries.h).
 *
 * @since New in 1.7. (but it shouldn't make the final release).
 */
svn_error_t *
svn_wc__get_entry_versioned(const svn_wc_entry_t **entry,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            svn_node_kind_t kind,
                            svn_boolean_t show_hidden,
                            svn_boolean_t need_parent_stub,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);


/** Given a @a local_abspath with a @a wc_ctx, set @a *switched to
 * TRUE if @a local_abspath is switched, otherwise set @a *switched to FALSE.
 * If neither @a local_abspath or its parent have valid URLs, return
 * @c SVN_ERR_ENTRY_MISSING_URL.  All temporaryallocations are done in
 * @a scratch_pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc__path_switched(svn_boolean_t *switched,
                      svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      apr_pool_t *scratch_pool);


/* Return the shallowest sufficient @c levels_to_lock value for @a depth;
 * see the @a levels_to_lock parameter of svn_wc_adm_open3() and
 * similar functions for more information.
 */
#define SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth)              \
  (((depth) == svn_depth_empty || (depth) == svn_depth_files) \
   ? 0 : (((depth) == svn_depth_immediates) ? 1 : -1))


/* Return TRUE iff CLHASH (a hash whose keys are const char *
   changelist names) is NULL or if ENTRY->changelist (which may be
   NULL) is a key in CLHASH.  */
#define SVN_WC__CL_MATCH(clhash, entry) \
        (((clhash == NULL) \
          || (entry \
              && entry->changelist \
              && apr_hash_get(clhash, entry->changelist, \
                              APR_HASH_KEY_STRING))) ? TRUE : FALSE)


/* Set *MODIFIED_P to true if VERSIONED_FILE_ABSPATH is modified with respect
 * to BASE_FILE_ABSPATH, or false if it is not.  The comparison compensates
 * for VERSIONED_FILE_ABSPATH's eol and keyword properties, but leaves
 * BASE_FILE_ABSPATH alone (as though BASE_FILE_ABSPATH were a text-base file,
 * which it usually is, only sometimes we're calling this on incoming
 * temporary text-bases).  If COMPARE_TEXTBASES is false, a clean copy of the
 * versioned file is compared to VERSIONED_FILE_ABSPATH.
 *
 * If an error is returned, the effect on *MODIFIED_P is undefined.
 *
 * Use SCRATCH_POOL for temporary allocation; WC_CTX is the normal thing.
 */
svn_error_t *
svn_wc__versioned_file_modcheck(svn_boolean_t *modified_p,
                                svn_wc_context_t *wc_ctx,
                                const char *versioned_file_abspath,
                                const char *base_file_abspath,
                                svn_boolean_t compare_textbases,
                                apr_pool_t *scratch_pool);

/**
 * Return a boolean answer to the question "Is @a status something that
 * should be reported?".  @a no_ignore and @a get_all are the same as
 * svn_wc_get_status_editor4().
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_wc__is_sendable_status(const svn_wc_status2_t *status,
                           svn_boolean_t no_ignore,
                           svn_boolean_t get_all);

/* For the NAME entry in the entries in ADM_ACCESS, set the
 * file_external_path to URL, the file_external_peg_rev to *PEG_REV
 * and the file_external_rev to *REV.  The URL may be NULL which
 * clears the file external information in the entry.  The repository
 * root URL is given in REPOS_ROOT_URL and is used to store a
 * repository root relative path in the entry.  POOL is used for
 * temporary allocations.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__set_file_external_location(svn_wc_adm_access_t *adm_access,
                                   const char *name,
                                   const char *url,
                                   const svn_opt_revision_t *peg_rev,
                                   const svn_opt_revision_t *rev,
                                   const char *repos_root_url,
                                   apr_pool_t *pool);

/** Set @a *tree_conflict to a newly allocated @c
 * svn_wc_conflict_description_t structure describing the tree
 * conflict state of @a victim_abspath, or to @c NULL if @a victim_abspath
 * is not in a state of tree conflict. @a wc_ctx is a working copy context
 * used to access @a victim_path.  Allocate @a *tree_conflict in @a result_pool,
 * use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc__get_tree_conflict(svn_wc_conflict_description_t **tree_conflict,
                          svn_wc_context_t *wc_ctx,
                          const char *victim_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/** Record the tree conflict described by @a conflict in the WC.
 * @a adm_access must be a write-access baton for the parent directory of
 * @a victim->path. Use @a pool for all allocations.
 *
 * Warning: This function updates the entry on disk but not the cached entry
 * in @a adm_access.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__add_tree_conflict(const svn_wc_conflict_description_t *conflict,
                          svn_wc_adm_access_t *adm_access,
                          apr_pool_t *pool);

/* Remove any tree conflict on victim @a victim_path from the directory entry
 * belonging to @a adm_access. (If there is no such conflict recorded, do
 * nothing and return success.) @a adm_access must be an access baton for the
 * parent directory of @a victim_path.
 *
 * Warning: This function updates the entry on disk but not the cached entry
 * in @a adm_access.
 *
 * Do all allocations in @a pool.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__del_tree_conflict(const char *victim_path,
                          svn_wc_adm_access_t *adm_access,
                          apr_pool_t *pool);

/*
 * Read tree conflict descriptions from @a conflict_data.  Set @a *conflicts
 * to a hash of pointers to svn_wc_conflict_description_t objects indexed by
 * svn_wc_conflict_description_t.path, all newly allocated in @a pool.  @a
 * dir_path is the path to the working copy directory whose conflicts are
 * being read.  The conflicts read are the tree conflicts on the immediate
 * child nodes of @a dir_path.  Do all allocations in @a pool.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__read_tree_conflicts(apr_hash_t **conflicts,
                            const char *conflict_data,
                            const char *dir_path,
                            apr_pool_t *pool);

/** Return a duplicate of @a conflict, allocated in @a pool.
 * A deep copy of all members, except the adm_access member, will be made.
 *
 * @since New in 1.6.
 */
svn_wc_conflict_description_t *
svn_wc__conflict_description_dup(const svn_wc_conflict_description_t *conflict,
                                 apr_pool_t *pool);

/** Like svn_wc_is_wc_root(), but it doesn't consider switched subdirs or
 * deleted entries as working copy roots.
 *
 * @since New in 1.7.*/
svn_error_t *
svn_wc__strictly_is_wc_root(svn_boolean_t *wc_root,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool);

/** Like svn_wc_adm_open3() but with a svn_wc_ctx_t* instead of an associated
 * baton.
 *
 * ### BH: This function is not for public consumption. New code should either
 *         use the deprecated access battons or the new wc contexts but not
 *         both. Too bad the WC-NG conversion is not done yet.
 *
 * @since New in 1.7.*/
svn_error_t *
svn_wc__adm_open_in_context(svn_wc_adm_access_t **adm_access,
                            svn_wc_context_t *wc_ctx,
                            const char *path,
                            svn_boolean_t write_lock,
                            int levels_to_lock,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *pool);

/** Like svn_wc_adm_probe_open3(), but with a svn_wc_context_t * instead of
 * an associated baton.
 *
 * ### See usage note to svn_wc__adm_open_in_context(), above.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc__adm_probe_in_context(svn_wc_adm_access_t **adm_access,
                             svn_wc_context_t *wc_ctx,
                             const char *path,
                             svn_boolean_t write_lock,
                             int levels_to_lock,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *pool);

/** Like svn_wc_adm_open_anchor(), but with a svn_wc_context_t * to use
 * when opening the access batons.
 *
 * NOT FOR NEW DEVELOPMENT!  (See note to svn_wc__adm_open_in_context().)
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc__adm_open_anchor_in_context(svn_wc_adm_access_t **anchor_access,
                                   svn_wc_adm_access_t **target_access,
                                   const char **target,
                                   svn_wc_context_t *wc_ctx,
                                   const char *path,
                                   svn_boolean_t write_lock,
                                   int levels_to_lock,
                                   svn_cancel_func_t cancel_func,
                                   void *cancel_baton,
                                   apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_PRIVATE_H */
