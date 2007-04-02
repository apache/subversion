/* fs-util.c : internal utility functions used by both FSFS and BDB back
 * ends.
 *
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
 */

#include <string.h>

#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_path.h"
#include "private/svn_fs_util.h"

const char *
svn_fs__canonicalize_abspath(const char *path, apr_pool_t *pool)
{
  char *newpath;
  int path_len;
  int path_i = 0, newpath_i = 0;
  svn_boolean_t eating_slashes = FALSE;

  /* No PATH?  No problem. */
  if (! path)
    return NULL;
  
  /* Empty PATH?  That's just "/". */
  if (! *path)
    return apr_pstrdup(pool, "/");

  /* Now, the fun begins.  Alloc enough room to hold PATH with an
     added leading '/'. */
  path_len = strlen(path);
  newpath = apr_pcalloc(pool, path_len + 2);

  /* No leading slash?  Fix that. */
  if (*path != '/')
    {
      newpath[newpath_i++] = '/';
    }
  
  for (path_i = 0; path_i < path_len; path_i++)
    {
      if (path[path_i] == '/')
        {
          /* The current character is a '/'.  If we are eating up
             extra '/' characters, skip this character.  Else, note
             that we are now eating slashes. */
          if (eating_slashes)
            continue;
          eating_slashes = TRUE;
        }
      else
        {
          /* The current character is NOT a '/'.  If we were eating
             slashes, we need not do that any more. */
          if (eating_slashes)
            eating_slashes = FALSE;
        }

      /* Copy the current character into our new buffer. */
      newpath[newpath_i++] = path[path_i];
    }
  
  /* Did we leave a '/' attached to the end of NEWPATH (other than in
     the root directory case)? */
  if ((newpath[newpath_i - 1] == '/') && (newpath_i > 1))
    newpath[newpath_i - 1] = '\0';

  return newpath;
}

