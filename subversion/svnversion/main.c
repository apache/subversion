/*
 * ====================================================================
 * Copyright (c) 2003-2008 CollabNet.  All rights reserved.
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "svn_opt.h"

#include "svn_private_config.h"

#define SVNVERSION_OPT_VERSION SVN_OPT_FIRST_LONGOPT_ID


static svn_error_t *
version(apr_pool_t *pool)
{
  return svn_opt_print_help3(NULL, "svnversion", TRUE, FALSE, NULL, NULL,
                             NULL, NULL, NULL, NULL, pool);
}

static void
usage(apr_pool_t *pool)
{
  svn_error_clear(svn_cmdline_fprintf
                  (stderr, pool, _("Type 'svnversion --help' for usage.\n")));
  exit(1);
}


static void
help(const apr_getopt_option_t *options, apr_pool_t *pool)
{
  svn_error_clear
    (svn_cmdline_fprintf
     (stdout, pool,
      _("usage: svnversion [OPTIONS] [WC_PATH [TRAIL_URL]]\n\n"
        "  Produce a compact 'version number' for the working copy path\n"
        "  WC_PATH.  TRAIL_URL is the trailing portion of the URL used to\n"
        "  determine if WC_PATH itself is switched (detection of switches\n"
        "  within WC_PATH does not rely on TRAIL_URL).  The version number\n"
        "  is written to standard output.  For example:\n"
        "\n"
        "    $ svnversion . /repos/svn/trunk\n"
        "    4168\n"
        "\n"
        "  The version number will be a single number if the working\n"
        "  copy is single revision, unmodified, not switched and with\n"
        "  an URL that matches the TRAIL_URL argument.  If the working\n"
        "  copy is unusual the version number will be more complex:\n"
        "\n"
        "   4123:4168     mixed revision working copy\n"
        "   4168M         modified working copy\n"
        "   4123S         switched working copy\n"
        "   4123P         partial working copy, from a sparse checkout\n"
        "   4123:4168MS   mixed revision, modified, switched working copy\n"
        "\n"
        "  If invoked on a directory that is not a working copy, an\n"
        "  exported directory say, the program will output 'exported'.\n"
        "\n"
        "  If invoked without arguments WC_PATH will be the current directory.\n"
        "\n"
        "Valid options:\n")));
  while (options->description)
    {
      const char *optstr;
      svn_opt_format_option(&optstr, options, TRUE, pool);
      svn_error_clear(svn_cmdline_fprintf(stdout, pool, "  %s\n", optstr));
      ++options;
    }
  svn_error_clear(svn_cmdline_fprintf(stdout, pool, "\n"));
  exit(0);
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
  return svn_ver_check_list(&my_version, checklist);
}

/*
 * Why is this not an svn subcommand?  I have this vague idea that it could
 * be run as part of the build process, with the output embedded in the svn
 * program.  Obviously we don't want to have to run svn when building svn.
 */
int
main(int argc, const char *argv[])
{
  const char *wc_path, *trail_url;
  apr_allocator_t *allocator;
  apr_pool_t *pool;
  int wc_format;
  svn_wc_revision_status_t *res;
  svn_boolean_t no_newline = FALSE, committed = FALSE;
  svn_error_t *err;
  apr_getopt_t *os;
  const apr_getopt_option_t options[] =
    {
      {"no-newline", 'n', 0, N_("do not output the trailing newline")},
      {"committed",  'c', 0, N_("last changed rather than current revisions")},
      {"help", 'h', 0, N_("display this help")},
      {"version", SVNVERSION_OPT_VERSION, 0,
       N_("show program version information")},
      {0,             0,  0,  0}
    };

  /* Initialize the app. */
  if (svn_cmdline_init("svnversion", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
   * given this application is single threaded.
   */
  if (apr_allocator_create(&allocator))
    return EXIT_FAILURE;

  apr_allocator_max_free_set(allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);

  pool = svn_pool_create_ex(NULL, allocator);
  apr_allocator_owner_set(allocator, pool);

  /* Check library versions */
  err = check_lib_versions();
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnversion: ");

#if defined(WIN32) || defined(__CYGWIN__)
  /* Set the working copy administrative directory name. */
  if (getenv("SVN_ASP_DOT_NET_HACK"))
    {
      err = svn_wc_set_adm_dir("_svn", pool);
      if (err)
        return svn_cmdline_handle_exit_error(err, pool, "svnversion: ");
    }
#endif

  err = svn_cmdline__getopt_init(&os, argc, argv, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnversion: ");

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
          return EXIT_FAILURE;
        }
      switch (opt)
        {
        case 'n':
          no_newline = TRUE;
          break;
        case 'c':
          committed = TRUE;
          break;
        case 'h':
          help(options, pool);
          break;
        case SVNVERSION_OPT_VERSION:
          SVN_INT_ERR(version(pool));
          exit(0);
          break;
        default:
          usage(pool);
          return EXIT_FAILURE;
        }
    }

  if (os->ind > argc || os->ind < argc - 2)
    {
      usage(pool);
      return EXIT_FAILURE;
    }

  SVN_INT_ERR(svn_utf_cstring_to_utf8
              (&wc_path, (os->ind < argc) ? os->argv[os->ind] : ".",
               pool));
  wc_path = svn_path_internal_style(wc_path, pool);

  if (os->ind+1 < argc)
    SVN_INT_ERR(svn_utf_cstring_to_utf8
                (&trail_url, os->argv[os->ind+1], pool));
  else
    trail_url = NULL;

  SVN_INT_ERR(svn_wc_check_wc(wc_path, &wc_format, pool));
  if (! wc_format)
    {
      svn_node_kind_t kind;
      SVN_INT_ERR(svn_io_check_path(wc_path, &kind, pool));
      if (kind == svn_node_dir)
        {
          SVN_INT_ERR(svn_cmdline_printf(pool, _("exported%s"),
                                         no_newline ? "" : "\n"));
          svn_pool_destroy(pool);
          return EXIT_SUCCESS;
        }
      else
        {
          svn_error_clear
            (svn_cmdline_fprintf(stderr, pool,
                                 _("'%s' not versioned, and not exported\n"),
                                 wc_path));
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }
    }


  SVN_INT_ERR(svn_wc_revision_status(&res, wc_path, trail_url, committed,
                                     NULL, NULL, pool));

  /* Build compact '123[:456]M?S?' string. */
  SVN_INT_ERR(svn_cmdline_printf(pool, "%ld", res->min_rev));
  if (res->min_rev != res->max_rev)
    SVN_INT_ERR(svn_cmdline_printf(pool, ":%ld", res->max_rev));
  if (res->modified)
    SVN_INT_ERR(svn_cmdline_fputs("M", stdout, pool));
  if (res->switched)
    SVN_INT_ERR(svn_cmdline_fputs("S", stdout, pool));
  if (res->sparse_checkout)
    SVN_INT_ERR(svn_cmdline_fputs("P", stdout, pool));

  if (! no_newline)
    SVN_INT_ERR(svn_cmdline_fputs("\n", stdout, pool));

  svn_pool_destroy(pool);

  /* Flush stdout to make sure that the user will see any printing errors. */
  SVN_INT_ERR(svn_cmdline_fflush(stdout));

  return EXIT_SUCCESS;
}
