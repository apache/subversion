/*
 * checkout.c : read a repository and drive a checkout editor.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "ra_local.h"
#include <assert.h>
#include <string.h>


svn_error_t *
svn_ra_local__split_URL (svn_stringbuf_t **repos_path,
                         svn_stringbuf_t **fs_path,
                         svn_stringbuf_t *URL,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stringbuf_t *url;
  char *hostname, *url_data, *path;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_fs_t *test_fs = svn_fs_new (subpool);

  /* Verify that the URL is well-formed (loosely) */
  url_data = URL->data;

  /* First, check for the "file://" prefix. */
  if (memcmp ("file://", url_data, 7))
    return svn_error_create 
      (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool, 
       ("svn_ra_local__split_URL: URL does not contain `file://' prefix"));
  
  /* Then, skip what's between the "file://" prefix and the next
     occurance of '/' -- this is the hostname, and we are considering
     everything from that '/' until the end of the URL to be the
     absolute path portion of the URL. */
  hostname = url_data + 7;
  path = strchr (hostname, '/');
  if (! path)
    return svn_error_create 
      (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool, 
       ("svn_ra_local__split_URL: URL contains only a hostname, no path"));

  /* Currently, the only hostnames we are allowing are the empty
     string and 'localhost' */
  if ((hostname != path) && (memcmp (hostname, "localhost", 9)))
    return svn_error_create 
      (SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool, 
       ("svn_ra_local__split_URL: URL contains unsupported hostname"));

  /* Duplicate the URL, starting at the top of the path */
  url = svn_string_create ((const char *)path, subpool);

  /* Loop, trying to open a FS at URL.  If this fails, remove the last
     component from the URL, then try again. */
  while (1)
    {
      /* Attempt to open FS */
      err = svn_fs_open_berkeley (test_fs, url->data);

      /* Hey, cool, we were successfully.  Stop loopin'. */
      if (err == SVN_NO_ERROR)
        break;

      /* If we're down to an empty path here, and we still haven't
         found the filesystem, we're just out of luck.  Time to bail
         and face the music. */
      if (svn_path_is_empty (url, svn_path_url_style))
        break;

      /* We didn't successfully open the filesystem, and we haven't
         hacked this path down to a bare nub yet, so we'll chop off
         the last component of this path. */
      svn_path_remove_component (url, svn_path_url_style);
    }

  /* If we are still sitting in an error-ful state, we must not have
     found the filesystem.  We give up. */
  if (err)
    return svn_error_create 
      (SVN_ERR_RA_NOT_VERSIONED_RESOURCE, 0, NULL, pool, 
       ("svn_ra_local__split_URL: Unable to find valid repository"));
  
  /* We apparently found a filesystem.  Let's close it since we aren't
     really going to do anything with it. */
  SVN_ERR (svn_fs_close_fs (test_fs));

  /* What remains of URL after being hacked at in the previous step is
     REPOS_PATH.  FS_PATH is what we've hacked off in the process.  We
     need to make sure these are allocated in the -original- pool. */
  *repos_path = svn_string_dup (url, pool);
  *fs_path = svn_string_create (path + url->len, pool);

  /* Destroy our temporary memory pool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */





