/* svn-wc-db-tester.c
 *
 * This is a crude command line tool that makes it possible to
 * run the wc-db validation checks directly.
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "svn_opt.h"
#include "svn_version.h"

#include "private/svn_wc_private.h"
#include "private/svn_cmdline_private.h"

#include "../../../subversion/libsvn_wc/wc.h"
#include "../../../subversion/libsvn_wc/wc_db.h"

#include "svn_private_config.h"

#define OPT_VERSION SVN_OPT_FIRST_LONGOPT_ID

static svn_error_t *
version(apr_pool_t *pool)
{
  return svn_opt_print_help4(NULL, "svn-wc-db-tester", TRUE, FALSE, FALSE,
                             NULL, NULL, NULL, NULL, NULL, NULL, pool);
}

static void
usage(apr_pool_t *pool)
{
  svn_error_clear(svn_cmdline_fprintf
                  (stderr, pool,
                   _("Type 'svn-wc-db-tester --help' for usage.\n")));
}

struct verify_baton
{
  svn_boolean_t found_err;
};

static svn_error_t *
verify_cb(void *baton,
          const char *wc_abspath,
          const char *local_relpath,
          int op_depth,
          int id,
          const char *msg,
          apr_pool_t *scratch_pool)
{
  struct verify_baton *vb = baton;

  if (op_depth >= 0)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool, "%s (depth=%d) DBV%04d: %s\n",
                                 local_relpath, op_depth, id, msg));
    }
  else
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool, "%s DBV%04d: %s\n",
                                 local_relpath, id, msg));
    }

  vb->found_err = TRUE;
  return SVN_NO_ERROR;
}

static svn_error_t *
verify_db(int argc, const char *path, apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;
  struct verify_baton vb = { FALSE };

  /* Read the parameters */
  path = svn_dirent_internal_style(path, pool);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL, pool, pool));

  SVN_ERR(svn_wc__db_verify_db_full(wc_ctx->db, local_abspath,
                                    verify_cb, &vb, pool));

  if (vb.found_err)
    return svn_error_create(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
              _("Found one or more potential wc.db inconsistencies"));

  return SVN_NO_ERROR;
}


static void
help(const apr_getopt_option_t *options, apr_pool_t *pool)
{
  svn_error_clear
    (svn_cmdline_fprintf
     (stdout, pool,
      _("usage: svn-wc-db-tester [OPTIONS] WC_PATH\n\n"
        "  Run verifications on the working copy\n"
        "\n"
        "  WC_PATH's parent directory must be a working copy, otherwise a\n"
        "  tree conflict cannot be raised.\n"
        "\n"
        "Valid options:\n")));
  while (options->description)
    {
      const char *optstr;
      svn_opt_format_option(&optstr, options, TRUE, pool);
      svn_error_clear(svn_cmdline_fprintf(stdout, pool, "  %s\n", optstr));
      ++options;
    }
}


/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",   svn_subr_version },
      { "svn_wc",     svn_wc_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  apr_getopt_t *os;
  const apr_getopt_option_t options[] =
    {
      {"help", 'h', 0, N_("display this help")},
      {"version", OPT_VERSION, 0,
       N_("show program version information")},
      {0,             0,  0,  0}
    };
  apr_array_header_t *remaining_argv;

  /* Check library versions */
  SVN_ERR(check_lib_versions());

#if defined(WIN32) || defined(__CYGWIN__)
  /* Set the working copy administrative directory name. */
  if (getenv("SVN_ASP_DOT_NET_HACK"))
    {
      SVN_ERR(svn_wc_set_adm_dir("_svn", pool));
    }
#endif

  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));

  os->interleave = 1;
  while (1)
    {
      int opt;
      const char *arg;
      apr_status_t status = apr_getopt_long(os, options, &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        {
          usage(pool);
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }

      switch (opt)
        {
        case 'h':
          help(options, pool);
          return SVN_NO_ERROR;
        case OPT_VERSION:
          SVN_ERR(version(pool));
          return SVN_NO_ERROR;
        default:
          usage(pool);
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  /* Convert the remaining arguments to UTF-8. */
  remaining_argv = apr_array_make(pool, 0, sizeof(const char *));
  while (os->ind < argc)
    {
      const char *s;

      SVN_ERR(svn_utf_cstring_to_utf8(&s, os->argv[os->ind++], pool));
      APR_ARRAY_PUSH(remaining_argv, const char *) = s;
    }

  if (remaining_argv->nelts != 1)
    {
      usage(pool);
      *exit_code = EXIT_FAILURE;
      return SVN_NO_ERROR;
    }

  /* Do the main task */
  SVN_ERR(verify_db(remaining_argv->nelts,
                    APR_ARRAY_IDX(remaining_argv, 0, const char *),
                    pool));

  return SVN_NO_ERROR;
}

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;

  /* Initialize the app. */
  if (svn_cmdline_init("svn-wc-db-tester", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
   * given this application is single threaded.
   */
  pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  err = sub_main(&exit_code, argc, argv, pool);

  /* Flush stdout and report if it fails. It would be flushed on exit anyway
     but this makes sure that output is not silently lost if it fails. */
  err = svn_error_compose_create(err, svn_cmdline_fflush(stdout));

  if (err)
    {
      exit_code = EXIT_FAILURE;
      svn_cmdline_handle_exit_error(err, NULL, "svn-wc-db-tester: ");
    }

  svn_pool_destroy(pool);
  return exit_code;
}
