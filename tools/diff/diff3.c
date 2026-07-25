/* diff3.c -- test driver for 3-way text merges
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
#include "svn_opt.h"
#include "private/svn_token.h"


static svn_error_t *
do_diff3(svn_stream_t *ostream,
         const char *original,
         const char *modified,
         const char *latest,
         const char *conflict_original,
         const char *conflict_modified,
         const char *conflict_latest,
         svn_diff_conflict_display_style_t conflict_style,
         svn_boolean_t *has_changes,
         apr_pool_t *pool)
{
  svn_diff_t *diff;

  SVN_ERR(svn_diff_file_diff3_2(&diff, original, modified, latest,
                                svn_diff_file_options_create(pool), pool));

  *has_changes = svn_diff_contains_diffs(diff);

  SVN_ERR(svn_diff_file_output_merge3(ostream, diff,
                                      original, modified, latest,
                                      conflict_original,
                                      conflict_modified,
                                      conflict_latest,
                                      "=======",
                                      conflict_style,
                                      NULL, NULL, /* cancel */
                                      pool));

  return NULL;
}

int main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  svn_stream_t *ostream;
  int rc;
  svn_error_t *svn_err = SVN_NO_ERROR;
  apr_getopt_t *opts;
  svn_boolean_t help = FALSE;

  enum {
    conflict_style_opt = SVN_OPT_FIRST_LONGOPT_ID
  };
  static const apr_getopt_option_t options[] = {
    {"conflict-style", conflict_style_opt, 1, ""},
    {"label", 'L', 1, ""},
    {"show-overlap", 'E', 0, ""},
    {"merge", 'm', 0, ""},
    {"help", 'h', 0, ""},
    {NULL, '?', 0, ""},
    {NULL, 0, 0, NULL}
  };
  svn_diff_conflict_display_style_t conflict_style
    = svn_diff_conflict_display_modified_latest;
  const svn_token_map_t style_map[] = {
    { "modified-latest",
      svn_diff_conflict_display_modified_latest },
    { "resolved-modified-latest",
      svn_diff_conflict_display_resolved_modified_latest },
    { "modified-original-latest",
      svn_diff_conflict_display_modified_original_latest },
    { "modified",
      svn_diff_conflict_display_modified },
    { "latest",
      svn_diff_conflict_display_latest },
    { "only-conflicts",
      svn_diff_conflict_display_only_conflicts },
    {NULL, 0}
  };
  const char *conflict_original = NULL;
  const char *conflict_modified = NULL;
  const char *conflict_latest = NULL;

  apr_initialize();

  pool = svn_pool_create(NULL);

  apr_getopt_init(&opts, pool, argc, argv);
  opts->interleave = 1;
  while (!svn_err)
    {
      int opt;
      const char *arg;
      apr_status_t status = apr_getopt_long(opts, options, &opt, &arg);

      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        {
          svn_err = svn_error_wrap_apr(status, "getopt failure");
          break;
        }
      switch (opt)
        {
        case conflict_style_opt:
          {
            int val;
            svn_err = svn_token__from_word_err(&val, style_map, arg);
            conflict_style = val;
            break;
          }
        case 'L':
          if (!conflict_modified)
            conflict_modified = apr_pstrcat(pool, "<<<<<<< ", arg, SVN_VA_NULL);
          else if (!conflict_original)
            conflict_original = apr_pstrcat(pool, "||||||| ", arg, SVN_VA_NULL);
          else if (!conflict_latest)
            conflict_latest = apr_pstrcat(pool, ">>>>>>> ", arg, SVN_VA_NULL);
          else
            svn_err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                       "too many labels");
          break;
        case 'E':
        case 'm':
          /* These are allowed and ignored so that all the options
             passed when invoking --diff3-cmd are accepted as that
             makes it easier to use this as an external diff3
             program. */
          break;
        case 'h':
        case '?':
          help = TRUE;
          break;
        }
    }

  if (!svn_err)
    svn_err = svn_stream_for_stdout(&ostream, pool);
  if (svn_err)
    {
      svn_handle_error2(svn_err, stdout, FALSE, "diff3: ");
      svn_error_clear(svn_err);
      rc = 2;
    }
  else if (argc - opts->ind == 3 && !help)
    {
      svn_boolean_t has_changes;

      svn_err = do_diff3(ostream, argv[argc-2], argv[argc-3], argv[argc-1],
                         conflict_original, conflict_modified, conflict_latest,
                         conflict_style, &has_changes, pool);
      if (svn_err == NULL)
        {
          rc = has_changes ? 1 : 0;
        }
      else
        {
          svn_handle_error2(svn_err, stdout, FALSE, "diff3: ");
          rc = 2;
        }
    }
  else
    {
      svn_error_clear(svn_stream_printf(ostream, pool,
        "Usage: %s [options] <mine> <older> <yours>\n"
        "Options:\n"
        "  --conflict-style STYLE\n"
        "    where STYLE can be:\n"
        "      %s\n"
        "      %s\n"
        "      %s\n"
        "      %s\n"
        "      %s\n"
        "      %s\n"
        "\n"
        "  --label [-L] LABEL\n"
        "    can be repeated up to three times\n"
        "\n"
        "  --merge [-m]\n"
        "    ignored (present for compatibility)\n"
        "\n"
        "  --show-overlap [-E]\n"
        "    ignored (present for compatibility)\n",
        argv[0],
        svn_token__to_word(style_map,
                           svn_diff_conflict_display_modified_latest),
        svn_token__to_word(style_map,
                           svn_diff_conflict_display_resolved_modified_latest),
        svn_token__to_word(style_map,
                           svn_diff_conflict_display_modified_original_latest),
        svn_token__to_word(style_map,
                           svn_diff_conflict_display_modified),
        svn_token__to_word(style_map,
                           svn_diff_conflict_display_latest),
        svn_token__to_word(style_map,
                           svn_diff_conflict_display_only_conflicts)));
      rc = 2;
    }

  apr_terminate();

  return rc;
}
