/* diff3-test.c -- test driver for 3-way text merges
 *
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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


static
svn_error_t *
do_diff3(apr_file_t *output_file,
         const char *original, const char *modified, const char *latest,
         svn_boolean_t *has_changes,
         apr_pool_t *pool)
{
  svn_diff_t *diff;

  SVN_ERR(svn_diff_file_diff3(&diff, original, modified, latest, pool));

  *has_changes = svn_diff_contains_diffs(diff);

  SVN_ERR(svn_diff_file_output_merge(output_file, diff,
                                     original, modified, latest,
                                     NULL, NULL, NULL, NULL,
                                     FALSE,
                                     FALSE,
                                     pool));

  return NULL;
}

int main(int argc, char *argv[])
{
  apr_pool_t *pool;
  apr_file_t *output_file;
  int rc;

  apr_initialize();

  pool = svn_pool_create(NULL);

  apr_file_open_stdout(&output_file, pool);

  if (argc == 4)
    {
      svn_boolean_t has_changes;
      svn_error_t *svn_err;

      svn_err = do_diff3(output_file, argv[2], argv[1], argv[3], &has_changes, pool);
      if (svn_err == NULL)
        {
          rc = has_changes ? 1 : 0;
        }
      else
        {
          svn_handle_error(svn_err, stdout, FALSE);
          rc = 2;
        }
    }
  else
    {
      apr_file_printf(output_file, "Usage: %s <mine> <older> <yours>\n", argv[0]);
      rc = 2;
    }

  apr_terminate();

  return rc;
}
