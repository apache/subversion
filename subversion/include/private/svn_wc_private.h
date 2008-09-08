/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
svn_wc__is_sendable_status(svn_wc_status2_t *status,
                           svn_boolean_t no_ignore,
                           svn_boolean_t get_all);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_PRIVATE_H */
