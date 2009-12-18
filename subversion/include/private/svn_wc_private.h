/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007-2008 CollabNet.  All rights reserved.
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

/** Internal function used by the svn_wc_entry_versioned() macro.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_wc__entry_versioned_internal(const svn_wc_entry_t **entry,
                                 const char *path,
                                 svn_wc_adm_access_t *adm_access,
                                 svn_boolean_t show_hidden,
                                 const char *caller_filename,
                                 int caller_lineno,
                                 apr_pool_t *pool);


/** Same as svn_wc_entry() except that the entry returned
 * is a non @c NULL entry.
 *
 * Returns an error when svn_wc_entry() would have returned a @c NULL entry.
 *
 * @since New in 1.5.
 */

#ifdef SVN_DEBUG
#define svn_wc__entry_versioned(entry, path, adm_access, show_hidden, pool) \
  svn_wc__entry_versioned_internal((entry), (path), (adm_access), \
                                   (show_hidden), __FILE__, __LINE__, (pool))
#else
#define svn_wc__entry_versioned(entry, path, adm_access, show_hidden, pool) \
  svn_wc__entry_versioned_internal((entry), (path), (adm_access), \
                                   (show_hidden), NULL, 0, (pool))
#endif


/** Given a @a wcpath with its accompanying @a entry, set @a *switched to
 * true if @a wcpath is switched, otherwise set @a *switched to false.
 * If @a entry is an incomplete entry obtained from @a wcpath's parent return
 * @c SVN_ERR_ENTRY_MISSING_URL.  All allocations are done in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_wc__path_switched(const char *wcpath,
                      svn_boolean_t *switched,
                      const svn_wc_entry_t *entry,
                      apr_pool_t *pool);


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


/* Set *MODIFIED_P to true if VERSIONED_FILE is modified with respect
 * to BASE_FILE, or false if it is not.  The comparison compensates
 * for VERSIONED_FILE's eol and keyword properties, but leaves
 * BASE_FILE alone (as though BASE_FILE were a text-base file, which
 * it usually is, only sometimes we're calling this on incoming
 * temporary text-bases).  ADM_ACCESS must be an access baton for
 * VERSIONED_FILE.  If COMPARE_TEXTBASES is false, a clean copy of the
 * versioned file is compared to VERSIONED_FILE.
 *
 * If an error is returned, the effect on *MODIFIED_P is undefined.
 *
 * Use POOL for temporary allocation.
 */
svn_error_t *
svn_wc__versioned_file_modcheck(svn_boolean_t *modified_p,
                                const char *versioned_file,
                                svn_wc_adm_access_t *adm_access,
                                const char *base_file,
                                svn_boolean_t compare_textbases,
                                apr_pool_t *pool);

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
 * conflict state of @a victim_path, or to @c NULL if @a victim_path
 * is not in a state of tree conflict. @a adm_access is the admin
 * access baton for @a victim_path. Use @a pool for all allocations.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__get_tree_conflict(svn_wc_conflict_description_t **tree_conflict,
                          const char *victim_path,
                          svn_wc_adm_access_t *adm_access,
                          apr_pool_t *pool);

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
 * to an array of pointers to svn_wc_conflict_description_t objects, all
 * newly allocated in @a pool.  @a dir_path is the path to the
 * working copy directory whose conflicts are being read.  The conflicts
 * read are the tree conflicts on the immediate child nodes of @a
 * dir_path.  Do all allocations in @a pool.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_wc__read_tree_conflicts(apr_array_header_t **conflicts,
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
 * @since New in 1.6.*/
svn_error_t *
svn_wc__strictly_is_wc_root(svn_boolean_t *wc_root,
                            const char *path,
                            svn_wc_adm_access_t *adm_access,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_PRIVATE_H */
