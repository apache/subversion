/*
 * checkout.c : read a repository and drive a checkout editor.
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

#include "ra_local.h"
#include <assert.h>
#include <string.h>
#include "svn_path.h"
#include "svn_private_config.h"


svn_error_t *
svn_ra_local__split_URL(svn_repos_t **repos,
                        const char **repos_url,
                        const char **fs_path,
                        const char *URL,
                        apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  const char *repos_root;
  const char *hostname, *path;
  svn_stringbuf_t *urlbuf;

  /* Verify that the URL is well-formed (loosely) */

  /* First, check for the "file://" prefix. */
  if (strncmp(URL, "file://", 7) != 0)
    return svn_error_createf 
      (SVN_ERR_RA_ILLEGAL_URL, NULL, 
       _("Local URL '%s' does not contain 'file://' prefix"), URL);
  
  /* Then, skip what's between the "file://" prefix and the next
     occurance of '/' -- this is the hostname, and we are considering
     everything from that '/' until the end of the URL to be the
     absolute path portion of the URL. */
  hostname = URL + 7;
  path = strchr(hostname, '/');
  if (! path)
    return svn_error_createf 
      (SVN_ERR_RA_ILLEGAL_URL, NULL, 
       _("Local URL '%s' contains only a hostname, no path"), URL);

  /* Treat localhost as an empty hostname. */
  if (hostname != path)
    {
      hostname = svn_path_uri_decode(apr_pstrmemdup(pool, hostname,
                                                    path - hostname), pool);
      if (strncmp(hostname, "localhost", 9) == 0)
	  hostname = NULL;
    }
  else
    hostname = NULL;
  
  /* Duplicate the URL, starting at the top of the path.
     At the same time, we URI-decode the path. */
#if defined(WIN32) || defined(__CYGWIN__)
  /* On Windows, we'll typically have to skip the leading / if the
     path starts with a drive letter.  Like most Web browsers, We
     support two variants of this scheme:

         file:///X:/path    and
         file:///X|/path

    Note that, at least on WinNT and above,  file:////./X:/path  will
    also work, so we must make sure the transformation doesn't break
    that, and  file:///path  (that looks within the current drive
    only) should also keep working.
    If we got a non-empty hostname other than localhost, we convert this
    into an UNC path.  In this case, we obviously don't strip the slash
    even if the path looks like it starts with a drive letter.
    Another thing to remember is that the form file:///\machine/share
    was the only way to access UNC paths in svn before 1.2.  We
    need to support that for compatibility with old working copies.
  */
  {
    static const char valid_drive_letters[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    /* Casting away const! */
    char *dup_path = (char *)svn_path_uri_decode(path, pool);
    if (!hostname && dup_path[1] && strchr(valid_drive_letters, dup_path[1])
        && (dup_path[2] == ':' || dup_path[2] == '|')
        && dup_path[3] == '/')
      {
        /* Skip the leading slash. */
        ++dup_path;
        /* We're using path below to calculate fs_path, so keep it in sync. */
        ++path;
        if (dup_path[1] == '|')
          dup_path[1] = ':';
      }
    if (hostname)
      /* We still know that the path starts with a slash. */
      repos_root = apr_pstrcat(pool, "//", hostname, path, NULL);
    else
      repos_root = dup_path;
  }
#else
  /* Currently, the only hostnames we are allowing on non-Win32 platforms
     are the empty string and 'localhost'. */
  if (hostname)
    return svn_error_createf
      (SVN_ERR_RA_ILLEGAL_URL, NULL, 
       _("Local URL '%s' contains unsupported hostname"), URL);

  repos_root = svn_path_uri_decode(path, pool);
#endif

  /* Search for a repository in the full path. */
  repos_root = svn_repos_find_root_path(repos_root, pool);
  if (!repos_root)
    return svn_error_createf 
      (SVN_ERR_RA_LOCAL_REPOS_OPEN_FAILED, NULL,
       _("Unable to open repository '%s'"), URL);

  /* Attempt to open a repository at URL. */
  err = svn_repos_open(repos, repos_root, pool);
  if (err)
    return svn_error_createf 
      (SVN_ERR_RA_LOCAL_REPOS_OPEN_FAILED, err,
       _("Unable to open repository '%s'"), URL);

  /* What remains of URL after being hacked at in the previous step is
     REPOS_URL.  FS_PATH is what we've hacked off in the process.
     Note that path is not encoded and what we gave to svn_root_find_root_path
     may have been destroyed by that function.  So we have to decode it once
     more.  But then, it is ours...
     We want the suffix of path after the repos root part.  Note that
     repos_root may contain //hostname, but path doesn't.  */
  *fs_path = svn_path_uri_decode(path, pool)
    + (strlen(repos_root)
       - (hostname ? strlen(hostname) + 2 : 0));

  /* Remove the path components in *fs_path from the original URL, to get
     the URL to the repository root. */
  urlbuf = svn_stringbuf_create(URL, pool);
  svn_path_remove_components(urlbuf,
                             svn_path_component_count(*fs_path));
  *repos_url = urlbuf->data;

  return SVN_NO_ERROR;
}
