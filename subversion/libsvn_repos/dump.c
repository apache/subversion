/* dump.c --- writing filesystem contents into a portable 'dumpfile' format.
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


#include <string.h>
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"



svn_error_t *
svn_repos_dump_fs (svn_fs_t *fs,
                   apr_file_t *file,
                   svn_revnum_t start_rev,
                   svn_revnum_t end_rev,
                   apr_pool_t *pool)
{

  /* Use default vals if necessary. */
  if (! SVN_IS_VALID_REVNUM(start_rev))
    start_rev = 0;
  if (! SVN_IS_VALID_REVNUM(end_rev))
    SVN_ERR (svn_fs_youngest_rev (&end_rev, fs, pool));

  /* Validate the starting and ending revs. */
  /* ### todo. */

  /* ### fooooooo testing */
  apr_file_printf (file, "Hello, world!\n");
  apr_file_printf (file, "I will now dump revisions %ld through %ld.\n",
                   start_rev, end_rev);

  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
