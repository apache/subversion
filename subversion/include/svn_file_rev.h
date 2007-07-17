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
 * @file svn_file_rev.h
 * @type definitions for svn_file_revs() related functions
 */


#ifndef SVN_FILE_REV_H
#define SVN_FILE_REV_H

#include <apr_pools.h>
#include "svn_delta.h"
#include "svn_types.h"
#include "svn_error.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * The callback invoked by file rev loopers, such as
 * svn_ra_plugin_t.get_file_revs2() and svn_repos_get_file_revs2().
 *
 * @a baton is provided by the caller, @a path is the pathname of the file
 * in revision @a rev and @a rev_props are the revision properties.
 *
 * If @a delta_handler and @a delta_baton are non-NULL, they may be set to a
 * handler/baton which will be called with the delta between the previous
 * revision and this one after the return of this callback.  They may be
 * left as NULL/NULL.
 *
 * @a result_of_merge will be @c TRUE if the revision being returned was
 * included as the result of a merge.
 *
 * @a prop_diffs is an array of svn_prop_t elements indicating the property
 * delta for this and the previous revision.
 *
 * @a pool may be used for temporary allocations, but you can't rely
 * on objects allocated to live outside of this particular call and
 * the immediately following calls to @a *delta_handler if any.  (Pass
 * in a pool via @a baton if need be.)
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_file_rev_handler_t)
  (void *baton,
   const char *path,
   svn_revnum_t rev,
   apr_hash_t *rev_props,
   svn_boolean_t result_of_merge,
   svn_txdelta_window_handler_t *delta_handler,
   void **delta_baton,
   apr_array_header_t *prop_diffs,
   apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_REPOS_H */
