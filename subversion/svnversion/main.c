/*
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "svn_opt.h"
#include <apr_tables.h>

#include "svn_private_config.h"

#define SVNVERSION_OPT_VERSION SVN_OPT_FIRST_LONGOPT_ID

struct status_baton
{
  svn_revnum_t min_rev;   /* lowest revision found. */
  svn_revnum_t max_rev;   /* highest revision found. */
  svn_boolean_t switched; /* is anything switched? */
  svn_boolean_t modified; /* is anything modified? */
  svn_boolean_t committed; /* examine last committed revisions */
  svn_boolean_t done;     /* note completion of our task. */
  const char *wc_path;    /* path whose URL we're looking for. */
  const char *wc_url;     /* URL for the path whose URL we're looking for. */
  apr_pool_t *pool;       /* pool in which to store alloc-needy things. */
};


/* An svn_wc_status_func_t callback function for anaylyzing status
   structures. */
static void
analyze_status (void *baton,
                const char *path,
                svn_wc_status_t *status)
{
  struct status_baton *sb = baton;
  
  if (sb->done)
    return;

  if (! status->entry)
    return;

  /* Added files have a revision of no interest */
  if (status->text_status != svn_wc_status_added)
    {
      svn_revnum_t item_rev = (sb->committed
                               ? status->entry->cmt_rev
                               : status->entry->revision);

      if (sb->min_rev == SVN_INVALID_REVNUM || item_rev < sb->min_rev)
        sb->min_rev = item_rev;

      if (sb->max_rev == SVN_INVALID_REVNUM || item_rev > sb->max_rev)
        sb->max_rev = item_rev;
    }

  sb->switched |= status->switched;
  sb->modified |= (status->text_status != svn_wc_status_normal);
  sb->modified |= (status->prop_status != svn_wc_status_normal
                   && status->prop_status != svn_wc_status_none);
  
  if (sb->wc_path 
      && (! sb->wc_url) 
      && (strcmp (path, sb->wc_path) == 0)
      && (status->entry))
    sb->wc_url = apr_pstrdup (sb->pool, status->entry->url);
}


/* This implements `svn_wc_notify_func_t'. */
static void
notify (void *baton,
        const char *path,
        svn_wc_notify_action_t action,
        svn_node_kind_t kind,
        const char *mime_type,
        svn_wc_notify_state_t content_state,
        svn_wc_notify_state_t prop_state,
        svn_revnum_t revision)
{
  struct status_baton *sb = baton;
  if ((action == svn_wc_notify_status_external)
      || (action == svn_wc_notify_status_completed))
    sb->done = TRUE;
}


/* This implements `svn_cancel_func_t'. */
static svn_error_t *
cancel (void *baton)
{
  struct status_baton *sb = baton;
  if (sb->done)
    return svn_error_create (SVN_ERR_CANCELLED, NULL, "Finished");
  else
    return SVN_NO_ERROR;
}

static svn_error_t * version(apr_getopt_t *os, apr_pool_t *pool)
{
  return svn_opt_print_help(os, "svnversion", TRUE, FALSE, NULL, NULL,
                            NULL, NULL, NULL, pool);
}

static void
usage(const apr_getopt_option_t *options, apr_pool_t *pool)
{
  svn_error_clear
    (svn_cmdline_fprintf
     (stderr, pool,
      _("usage: svnversion [OPTIONS] WC_PATH [TRAIL_URL]\n\n"
        "  Produce a compact \"version number\" for the working copy path\n"
        "  WC_PATH.  TRAIL_URL is the trailing portion of the URL used to\n"
        "  determine if WC_PATH itself is switched (detection of switches\n"
        "  within WC_PATH does not rely on TRAIL_URL).  The version number\n"
        "  is written to standard output.  For example:\n"
        "\n"
        "    $ svnversion . /repos/svn/trunk \n"
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
        "   4123:4168MS   mixed revision, modified, switched working copy\n"
        "\n"
        "  If invoked on a directory that is not a working copy, an\n"
        "  exported directory say, the program will output \"exported\".\n"
        "\n"
        "Valid options:\n")));
  while (options->description)
    {
      const char *optstr;
      svn_opt_format_option(&optstr, options, TRUE, pool);
      svn_error_clear (svn_cmdline_fprintf(stderr, pool, "  %s\n", optstr));
      ++options;
    }
}


/* Version compatibility check */
static svn_error_t *
check_lib_versions (void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",   svn_subr_version },
      { "svn_client", svn_client_version },
      { "svn_wc",     svn_wc_version },
      { NULL, NULL }
    };

   SVN_VERSION_DEFINE (my_version);
   return svn_ver_check_list (&my_version, checklist);
}


/*
 * Why is this not an svn subcommand?  I have this vague idea that it could
 * be run as part of the build process, with the output embedded in the svn
 * program.  Obviously we don't want to have to run svn when building svn.
 * We could always put this into libsvn_client and share it between
 * svnversion and svn.
 */
int
main(int argc, const char *argv[])
{
  const char *wc_path;
  apr_allocator_t *allocator;
  apr_pool_t *pool;
  int wc_format;
  svn_client_ctx_t ctx = { 0 };
  struct status_baton sb;
  svn_opt_revision_t rev;
  svn_boolean_t no_newline = FALSE;
  svn_error_t *err;
  apr_getopt_t *os;
  const apr_getopt_option_t options[] =
    {
      {"no-newline", 'n', 0, N_("do not output the trailing newline")},
      {"committed",  'c', 0, N_("last changed rather than current revisions")},
      {"version", SVNVERSION_OPT_VERSION, 0, N_("show version information")},
      {0,             0,  0,  0}
    };

  /* Initialize the app. */
  if (svn_cmdline_init ("svnversion", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a seperate mutexless allocator,
   * given this application is single threaded.
   */
  if (apr_allocator_create (&allocator))
    return EXIT_FAILURE;

  apr_allocator_max_free_set (allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);

  pool = svn_pool_create_ex (NULL, allocator);
  apr_allocator_owner_set (allocator, pool);

  /* Check library versions */
  err = check_lib_versions ();
  if (err)
    {
      svn_handle_error (err, stderr, FALSE);
      svn_error_clear (err);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  sb.switched = FALSE;
  sb.modified = FALSE;
  sb.committed = FALSE;
  sb.min_rev = SVN_INVALID_REVNUM;
  sb.max_rev = SVN_INVALID_REVNUM;
  sb.wc_path = NULL;
  sb.wc_url = NULL;
  sb.done = FALSE;
  sb.pool = pool;

  apr_getopt_init(&os, pool, argc, argv);
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
          usage(options, pool);
          return EXIT_FAILURE;
        }
      switch (opt)
        {
        case 'n':
          no_newline = TRUE;
          break;
        case 'c':
          sb.committed = TRUE;
          break;
        case SVNVERSION_OPT_VERSION:
          SVN_INT_ERR(version(os, pool));
          exit(0);
          break;
        default:
          usage(options, pool);
          return EXIT_FAILURE;
        }
    }

  if (os->ind >= argc || os->ind < argc - 2)
    {
      usage(options, pool);
      return EXIT_FAILURE;
    }

  SVN_INT_ERR (svn_utf_cstring_to_utf8 (&wc_path, os->argv[os->ind++], pool));
  wc_path = svn_path_internal_style (wc_path, pool);
  SVN_INT_ERR (svn_wc_check_wc (wc_path, &wc_format, pool));
  if (! wc_format)
    {
      svn_node_kind_t kind;
      SVN_INT_ERR(svn_io_check_path (wc_path, &kind, pool));
      if (kind == svn_node_dir)
        {
          SVN_INT_ERR (svn_cmdline_printf (pool, _("exported%s"), 
                                           no_newline ? "" : "\n"));
          svn_pool_destroy (pool);
          return EXIT_SUCCESS;
        }
      else
        {
          svn_error_clear
            (svn_cmdline_fprintf (stderr, pool,
                                  _("'%s' not versioned, and not exported\n"),
                                  wc_path));
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
    }

  sb.wc_path = wc_path;
  rev.kind = svn_opt_revision_unspecified;
  ctx.config = apr_hash_make (pool);

  /* Setup the notification and cancellation callbacks, and their
     shared baton (which is also shared with the status function). */
  ctx.notify_func = notify;
  ctx.notify_baton = &sb;
  ctx.cancel_func = cancel;
  ctx.cancel_baton = &sb;

  err = svn_client_status (NULL, wc_path, &rev, analyze_status, 
                           &sb, TRUE, TRUE, FALSE, FALSE, &ctx, pool);
  if (err && (err->apr_err == SVN_ERR_CANCELLED))
    svn_error_clear (err);
  else
    SVN_INT_ERR (err);

  if ((! sb.switched ) && (os->ind < argc))
    {
      /* If the trailing part of the URL of the working copy directory
         does not match the given trailing URL then the whole working
         copy is switched. */
      const char *trail_url;
      SVN_INT_ERR (svn_utf_cstring_to_utf8 (&trail_url, os->argv[os->ind],
                                            pool));
      if (! sb.wc_url)
        {
          sb.switched = TRUE;
        }
      else
        {
          apr_size_t len1 = strlen (trail_url);
          apr_size_t len2 = strlen (sb.wc_url);
          if ((len1 > len2) || strcmp (sb.wc_url + len2 - len1, trail_url))
            sb.switched = TRUE;
        }
    }

  SVN_INT_ERR (svn_cmdline_printf (pool, "%ld", sb.min_rev));
  if (sb.min_rev != sb.max_rev)
    SVN_INT_ERR (svn_cmdline_printf (pool, ":%ld", sb.max_rev));
  if (sb.modified)
    SVN_INT_ERR (svn_cmdline_fputs ("M", stdout, pool));
  if (sb.switched)
    SVN_INT_ERR (svn_cmdline_fputs ("S", stdout, pool));
  if (! no_newline)
    SVN_INT_ERR (svn_cmdline_fputs ("\n", stdout, pool));

  svn_pool_destroy (pool);

  /* Flush stdout to make sure that the user will see any printing errors. */
  SVN_INT_ERR (svn_cmdline_fflush (stdout));

  return EXIT_SUCCESS;
}
