/*
 * checkout.c : read a repository and drive a checkout editor.
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

#include "ra_local.h"
#include <assert.h>
#include <string.h>
#include "svn_pools.h"
#include "svn_private_config.h"

svn_error_t *
svn_ra_local__split_URL (const char **repos_path,
                         const char **fs_path,
                         const char *URL,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  const char *candidate_url;
  const char *hostname, *path;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_repos_t *repos;

  /* Verify that the URL is well-formed (loosely) */

  /* First, check for the "file://" prefix. */
  if (strncmp (URL, "file://", 7) != 0)
    return svn_error_createf 
      (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool, 
       "svn_ra_local__split_URL: URL does not contain `file://' prefix\n"
       "   (%s)", URL);
  
  /* Then, skip what's between the "file://" prefix and the next
     occurance of '/' -- this is the hostname, and we are considering
     everything from that '/' until the end of the URL to be the
     absolute path portion of the URL. */
  hostname = URL + 7;
  path = strchr (hostname, '/');
  if (! path)
    return svn_error_createf 
      (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool, 
       "svn_ra_local__split_URL: URL contains only a hostname, no path\n"
       "   (%s)", URL);

  /* Currently, the only hostnames we are allowing are the empty
     string and 'localhost' */
  if ((hostname != path) && (strncmp (hostname, "localhost", 9) != 0))
    return svn_error_createf
      (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool, 
       "svn_ra_local__split_URL: URL contains unsupported hostname\n"
       "   (%s)", URL);


  /* Duplicate the URL, starting at the top of the path */
#ifndef SVN_WIN32
  candidate_url = apr_pstrdup (subpool, path);
#else  /* SVN_WIN32 */
  /* On Windows, we'll typically have to skip the leading / if the
     path starts with a drive letter.  Like most Web browsers, We
     support two variants of this schema:

         file:///X:/path    and
         file:///X|/path

    Note that, at least on WinNT and above,  file:////./X:/path  will
    also work, so we must make sure the transformation doesn't break
    that, and  file:///path  (that looks within the current drive
    only) should also keep working. */
 {
   static const char valid_drive_letters[] =
     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
   if (path[1] && strchr(valid_drive_letters, path[1])
       && (path[2] == ':' || path[2] == '|')
       && path[3] == '/')
     {
       char *const dup_path = apr_pstrdup (subpool, ++path);
       if (dup_path[1] == '|')
         dup_path[1] = ':';
       candidate_url = dup_path;
     }
   else
     candidate_url = apr_pstrdup (subpool, path);
 }
#endif /* SVN_WIN32 */

  /* Loop, trying to open a repository at URL.  If this fails, remove
     the last component from the URL, then try again. */
  while (1)
    {
      /* Attempt to open a repository at URL. */
      err = svn_repos_open (&repos, candidate_url, subpool);

      /* Hey, cool, we were successfully.  Stop loopin'. */
      if (err == SVN_NO_ERROR)
        break;

      /* If we're down to an empty path here, and we still haven't
         found the repository, we're just out of luck.  Time to bail
         and face the music. */
      if (svn_path_is_empty_nts (candidate_url))
        break;

      /* We didn't successfully open the repository, and we haven't
         hacked this path down to a bare nub yet, so we'll chop off
         the last component of this path. */
      candidate_url = svn_path_remove_component_nts (candidate_url, subpool);
    }

  /* If we are still sitting in an error-ful state, we must not have
     found the repository.  We give up. */
  if (err)
    return svn_error_createf 
      (SVN_ERR_RA_REPOSITORY_NOT_FOUND, 0, NULL, pool, 
       "svn_ra_local__split_URL: Unable to find valid repository\n"
       "   (%s)", URL);

  /* We apparently found a repository.  Let's close it since we aren't
     really going to do anything with it. */
  SVN_ERR (svn_repos_close (repos));

  /* What remains of URL after being hacked at in the previous step is
     REPOS_PATH.  FS_PATH is what we've hacked off in the process.  We
     need to make sure these are allocated in the -original- pool. */
  *repos_path = apr_pstrdup (pool, candidate_url);
  *fs_path = apr_pstrdup (pool, path + strlen (candidate_url));

  /* Destroy our temporary memory pool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */





