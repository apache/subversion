/**
 * @copyright
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


/** Similar to svn_wc__get_entry() and svn_wc__entry_versioned().
 *
 * This function allows callers in libsvn_client to directly fetch entry data
 * without having to open up an adm_access baton.  Its error and return
 * semantics are the same as svn_wc__entry_versioned(), and parameters are the
 * same as svn_wc__get_entry() (defined in libsvn_wc/entries.h).
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

/** Similar to svn_wc__get_entry_versioned(), but returns a NULL entry
 * instead of throwing an error (just like svn_wc_entry()).
 */
svn_error_t *
svn_wc__maybe_get_entry(const svn_wc_entry_t **entry,
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
   changelist names) is NULL or if LOCAL_ABSPATH is part of a changelist in
   CLHASH. */
svn_boolean_t
svn_wc__changelist_match(svn_wc_context_t *wc_ctx,
                         const char *local_abspath,
                         apr_hash_t *clhash,
                         apr_pool_t *scratch_pool);


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
 */
svn_boolean_t
svn_wc__is_sendable_status(const svn_wc_status2_t *status,
                           svn_boolean_t no_ignore,
                           svn_boolean_t get_all);

/* For the LOCAL_ABSPATH entry in WC_CTX, set the
 * file_external_path to URL, the file_external_peg_rev to *PEG_REV
 * and the file_external_rev to *REV.  The URL may be NULL which
 * clears the file external information in the entry.  The repository
 * root URL is given in REPOS_ROOT_URL and is used to store a
 * repository root relative path in the entry.  SCRATCH_POOL is used for
 * temporary allocations.
 */
svn_error_t *
svn_wc__set_file_external_location(svn_wc_context_t *wc_ctx,
                                   const char *local_abspath,
                                   const char *url,
                                   const svn_opt_revision_t *peg_rev,
                                   const svn_opt_revision_t *rev,
                                   const char *repos_root_url,
                                   apr_pool_t *scratch_pool);


/** Set @a *tree_conflict to a newly allocated @c
 * svn_wc_conflict_description_t structure describing the tree
 * conflict state of @a victim_abspath, or to @c NULL if @a victim_abspath
 * is not in a state of tree conflict. @a wc_ctx is a working copy context
 * used to access @a victim_path.  Allocate @a *tree_conflict in @a result_pool,
 * use @a scratch_pool for temporary allocations.
 */
svn_error_t *
svn_wc__get_tree_conflict(const svn_wc_conflict_description2_t **tree_conflict,
                          svn_wc_context_t *wc_ctx,
                          const char *victim_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/** Record the tree conflict described by @a conflict in the WC for
 * @a conflict->local_abspath.  Use @a scratch_pool for all temporary
 * allocations.
 */
svn_error_t *
svn_wc__add_tree_conflict(svn_wc_context_t *wc_ctx,
                          const svn_wc_conflict_description2_t *conflict,
                          apr_pool_t *scratch_pool);

/* Remove any tree conflict on victim @a victim_abspath using @a wc_ctx.
 * (If there is no such conflict recorded, do nothing and return success.)
 *
 * Do all temporary allocations in @a scratch_pool.
 */
svn_error_t *
svn_wc__del_tree_conflict(svn_wc_context_t *wc_ctx,
                          const char *victim_abspath,
                          apr_pool_t *scratch_pool);

/*
 * Read tree conflict descriptions from @a conflict_data.  Set @a *conflicts
 * to a hash of pointers to svn_wc_conflict_description2_t objects indexed by
 * svn_wc_conflict_description2_t.local_abspath, all newly allocated in @a
 * pool.  @a dir_path is the path to the working copy directory whose conflicts
 * are being read.  The conflicts read are the tree conflicts on the immediate
 * child nodes of @a dir_path.  Do all allocations in @a pool.
 */
svn_error_t *
svn_wc__read_tree_conflicts(apr_hash_t **conflicts,
                            const char *conflict_data,
                            const char *dir_path,
                            apr_pool_t *pool);

/** Return a duplicate of @a conflict, allocated in @a pool.
 * A deep copy of all members, except the adm_access member, will be made.
 */
svn_wc_conflict_description_t *
svn_wc__conflict_description_dup(const svn_wc_conflict_description_t *conflict,
                                 apr_pool_t *pool);

/** Like svn_wc_is_wc_root(), but it doesn't consider switched subdirs or
 * deleted entries as working copy roots.
 */
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
 */
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


/**
 * The following are temporary APIs to aid in the transition from wc-1 to
 * wc-ng.  Use them for new development now, but they may be disappearing
 * before the 1.7 release.
 */

/** A callback invoked by the generic node-walker function.  */
typedef svn_error_t *(*svn_wc__node_found_func_t)(const char *local_abspath,
                                                  void *walk_baton,
                                                  apr_pool_t *scratch_pool);

/**
 * Retrieve an @a adm_access for @a path from the @a wc_ctx.
 * If the @a adm_access for @a local_abspath is not found, this
 * function sets @a *adm_acess to NULL and does not return an error.
 */
svn_error_t *
svn_wc__adm_retrieve_from_context(svn_wc_adm_access_t **adm_access,
                                  svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  apr_pool_t *pool);


/*
 * Convert from svn_wc_conflict_description2_t to svn_wc_conflict_description_t.
 * Allocate the result in RESULT_POOL.
 */
svn_wc_conflict_description_t *
svn_wc__cd2_to_cd(const svn_wc_conflict_description2_t *conflict,
                  apr_pool_t *result_pool);


/*
 * Convert from svn_wc_conflict_description_t to svn_wc_conflict_description2_t.
 * Allocate the result in RESULT_POOL.
 */
svn_wc_conflict_description2_t *
svn_wc__cd_to_cd2(const svn_wc_conflict_description_t *conflict,
                  apr_pool_t *result_pool);


/**
 * Fetch the absolute paths of all the working children of @a dir_abspath
 * into @a *children, allocated in @a result_pool.  Use @a wc_ctx to access
 * the working copy, and @a scratch_pool for all temporary allocations.
 */
svn_error_t *
svn_wc__node_get_children(const apr_array_header_t **children,
                          svn_wc_context_t *wc_ctx,
                          const char *dir_abspath,
                          svn_boolean_t show_hidden,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/**
 * Fetch the repository root information for a given @a local_abspath into
 * @a *repos_root_url and @a repos_uuid. Use @wc_ctx to access the working copy
 * for @a local_abspath, @a scratch_pool for all temporary allocations,
 * @a result_pool for result allocations. Note: the result may be NULL if the
 * given node has no repository root associated with it (e.g. locally added).
 *
 * If @a scan_added is TRUE, scan parents to find the intended repos root
 * and/or UUID of added nodes. Otherwise set @a *repos_root_url and
 * *repos_uuid to NULL for added nodes.
 *
 * Either input value may be NULL, indicating no interest.
 */
svn_error_t *
svn_wc__node_get_repos_info(const char **repos_root_url,
                            const char **repos_uuid,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            svn_boolean_t scan_added,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);


/**
 * Set @a kind to the @c svn_node_kind_t of @a abspath.  Use @a wc_ctx
 * to access the working copy, and @a scratch_pool for all temporary
 * allocations.  If @a abspath is not present in the working copy and
 * @a show_hidden is FALSE then set @a kind to @c svn_node_none.
 */
svn_error_t *
svn_wc__node_get_kind(svn_node_kind_t *kind,
                      svn_wc_context_t *wc_ctx,
                      const char *abspath,
                      svn_boolean_t show_hidden,
                      apr_pool_t *scratch_pool);


/**
 * Get the depth of @a local_abspath using @a wc_ctx.  If @a local_abspath is
 * not in the working copy, return @c SVN_ERR_WC_PATH_NOT_FOUND.
 */
svn_error_t *
svn_wc__node_get_depth(svn_depth_t *depth,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool);

/**
 * Get the changed revision, date and author for @a local_abspath using @a
 * wc_ctx.  Allocate the return values in @a result_pool; use @a scratch_pool
 * for temporary allocations.  Any of the return pointers may be @c NULL, in
 * which case they are not set.
 *
 * If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.
 */
svn_error_t *
svn_wc__node_get_changed_info(svn_revnum_t *changed_rev,
                              apr_time_t *changed_date,
                              const char **changed_author,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/**
 * Set @a *changelist to the changelist to which @a local_abspath belongs.
 * Allocate the result in @a result_pool and use @a scratch_pool for temporary
 * allocations.
 */
svn_error_t *
svn_wc__node_get_changelist(const char **changelist,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);


/**
 * Set @a *url to the corresponding url for @a local_abspath, using @a wc_ctx.
 * If the node is added, return the url it will have in the repository.
 *
 * If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.
 */
svn_error_t *
svn_wc__node_get_url(const char **url,
                     svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);


/**
 * Recursively call @a callbacks->found_node for all nodes underneath
 * @a local_abspath.
 */
svn_error_t *
svn_wc__node_walk_children(svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           svn_boolean_t show_hidden,
                           svn_wc__node_found_func_t walk_callback,
                           void *walk_baton,
                           svn_depth_t walk_depth,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *scratch_pool);

/**
 * Set @a *is_deleted to TRUE if @a local_abspath is deleted, using
 * @a wc_ctx.  If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.  Use @a scratch_pool for all temporary
 * allocations.
 */
svn_error_t *
svn_wc__node_is_status_delete(svn_boolean_t *is_deleted,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool);

/**
 * Set @a *is_deleted to whether @a local_abspath is obstructed, using
 * @a wc_ctx.  If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.  Use @a scratch_pool for all temporary
 * allocations.
 */
svn_error_t *
svn_wc__node_is_status_obstructed(svn_boolean_t *is_obstructed,
                                  svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  apr_pool_t *scratch_pool);

/**
 * Set @a *is_deleted to whether @a local_abspath is absent, using
 * @a wc_ctx.  If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.  Use @a scratch_pool for all temporary
 * allocations.
 */
svn_error_t *
svn_wc__node_is_status_absent(svn_boolean_t *is_absent,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool);

/**
 * Set @a *is_present to whether @a local_abspath is present, using
 * @a wc_ctx.  If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.  Use @a scratch_pool for all temporary
 * allocations.
 */
svn_error_t *
svn_wc__node_is_status_present(svn_boolean_t *is_present,
                               svn_wc_context_t *wc_ctx,
                               const char *local_abspath,
                               apr_pool_t *scratch_pool);

/**
 * Set @a *is_added to whether @a local_abspath is added, using
 * @a wc_ctx.  If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.  Use @a scratch_pool for all temporary
 * allocations.
 */
svn_error_t *
svn_wc__node_is_status_added(svn_boolean_t *is_added,
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             apr_pool_t *scratch_pool);

/**
 * Get the base revision of @a local_abspath using @a wc_ctx.  If
 * @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.
 */
svn_error_t *
svn_wc__node_get_base_rev(svn_revnum_t *base_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          apr_pool_t *scratch_pool);

/**
 * Get the lock token of @a local_abspath using @a wc_ctx or NULL
 * if there is no lock.  If @a local_abspath is not in the working
*  copy, return @c SVN_ERR_WC_PATH_NOT_FOUND.
 */
svn_error_t *
svn_wc__node_get_lock_token(const char **lock_token,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);


/**
 * Recursively acquire write locks for @a local_abspath if
 * @a anchor_abspath is NULL.  If @a anchor_abspath is not NULL then
 * recursively acquire write locks for the anchor of @a local_abspath
 * and return the anchor path in @a *anchor_abspath.  Use @a wc_ctx
 * for working copy access.
 *
 * ### @a anchor_abspath should be removed when we move to centralised
 * ### metadata as it will be unnecessary.
 */
svn_error_t *
svn_wc__acquire_write_lock(const char **anchor_abspath,
                           svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);


/**
 * Recursively release write locks for @a local_abspath, using @a wc_ctx
 * for working copy access.
 */
svn_error_t *
svn_wc__release_write_lock(svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_PRIVATE_H */
