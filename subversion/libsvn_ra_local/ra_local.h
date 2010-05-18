/*
 * ra_local.h : shared internal declarations for ra_local module
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_RA_LOCAL_H
#define SVN_LIBSVN_RA_LOCAL_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_ra.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Structures **/

/* A baton which represents a single ra_local session. */
typedef struct svn_ra_local__session_baton_t
{
  /* The user accessing the repository. */
  const char *username;

  /* The URL of the session, split into two components. */
  const char *repos_url;
  svn_stringbuf_t *fs_path;  /* URI-decoded, always with a leading slash. */

  /* A repository object. */
  svn_repos_t *repos;

  /* The filesystem object associated with REPOS above (for
     convenience). */
  svn_fs_t *fs;

  /* The UUID associated with REPOS above (cached) */
  const char *uuid;

  /* Callbacks/baton passed to svn_ra_open. */
  const svn_ra_callbacks2_t *callbacks;
  void *callback_baton;
} svn_ra_local__session_baton_t;




/** Private routines **/




/* Given a `file://' URL, figure out which portion specifies a
   repository on local disk, and return that in REPOS_URL (if not
   NULL); URI-decode and return the remainder (the path *within* the
   repository's filesystem) in FS_PATH.  Open REPOS to the repository
   root (if not NULL).  Allocate the return values in POOL.
   Currently, we are not expecting to handle `file://hostname/'-type
   URLs; hostname, in this case, is expected to be the empty string or
   "localhost". */
svn_error_t *
svn_ra_local__split_URL(svn_repos_t **repos,
                        const char **repos_url,
                        const char **fs_path,
                        const char *URL,
                        apr_pool_t *pool);




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_RA_LOCAL_H */
