/* diff-test.c -- test driver for text diffs
 *
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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


#include <apr.h>
#include <apr_general.h>
#include <apr_file_io.h>

#include "svn_pools.h"
#include "svn_diff.h"
#include "svn_io.h"


static svn_error_t *
do_diff(svn_stream_t *ostream,
        const char *original, const char *modified,
        svn_boolean_t *has_changes,
        apr_pool_t *pool)
{
  svn_diff_t *diff;

  SVN_ERR(svn_diff_file_diff(&diff, original, modified, pool));

  *has_changes = svn_diff_contains_diffs(diff);

  SVN_ERR(svn_diff_file_output_unified(ostream, diff,
                                       original, modified,
                                       NULL, NULL, pool));

  return NULL;
}

int main(int argc, char *argv[])
{
  apr_pool_t *pool;
  svn_stream_t *ostream;
  int rc;
  svn_error_t *svn_err;

  apr_initialize();

  pool = svn_pool_create(NULL);

  svn_err = svn_stream_for_stdout(&ostream, pool);
  if (svn_err)
    {
      svn_handle_error2(svn_err, stdout, FALSE, "diff-test: ");
      rc = 2;
    }
  else if (argc == 3)
    {
      svn_boolean_t has_changes;

      svn_err = do_diff(ostream, argv[1], argv[2], &has_changes, pool);
      if (svn_err == NULL)
        {
          rc = has_changes ? 1 : 0;
        }
      else
        {
          svn_handle_error2(svn_err, stdout, FALSE, "diff-test: ");
          rc = 2;
        }
    }
  else
    {
      svn_error_clear(svn_stream_printf(ostream, pool,
                                        "Usage: %s <file1> <file2>\n",
                                        argv[0]));
      rc = 2;
    }

  apr_terminate();

  return rc;
}
