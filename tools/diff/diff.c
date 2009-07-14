/* diff-test.c -- test driver for text diffs
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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
