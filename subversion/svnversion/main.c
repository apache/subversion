/*
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_utf.h"
#include "svn_path.h"
#include <apr_tables.h>


struct status_baton
{
  svn_revnum_t min_rev;   /* lowest revision found. */
  svn_revnum_t max_rev;   /* highest revision found. */
  svn_boolean_t switched; /* is anything switched? */
  svn_boolean_t modified; /* is anything modified? */
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

  if (! status->entry)
    return;

  /* Added files have a revision of no interest */
  if (status->text_status != svn_wc_status_added)
    {
      if (sb->min_rev == SVN_INVALID_REVNUM 
          || status->entry->revision < sb->min_rev)
        sb->min_rev = status->entry->revision;

      if (sb->max_rev == SVN_INVALID_REVNUM
          || status->entry->revision > sb->max_rev)
        sb->max_rev = status->entry->revision;
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


/* The svnversion program uses svn_client_status to produce a compact
 * "version number" for the Subversion working copy.  The program takes one
 * or two arguments, the first is the path to the working copy, the second
 * is the trailing portion of the trunk URL.  The version number is written
 * to standard output.  Here is an example
 *
 *   $ svnversion . /repos/svn/trunk 
 *   4168
 *
 * The version number will be a single number if the working copy is single
 * revision, unmodified, not switched and with an URL that matches the
 * trunk URL argument.  If the working copy is unusual the version number
 * will be more coplex:
 *
 *   4123:4168       a mixed revision working copy
 *   4168M           a modified working copy
 *   4123S           a switched working copy
 *   4123:4168MS     a mixed revision, modified, switched working copy
 *
 * If invoked on a directory that is not a working copy, an exported
 * directory say, the program will output "exported".
 *
 * Why is this not an svn subcommand?  I have this vague idea that it could
 * be run as part of the build process, with the output embedded in the svn
 * program.  Obviously we don't want to have to run svn when building svn.
 * We could always put this into libsvn_client and share it between
 * svnversion and svn.
 */
int
main(int argc, char *argv[])
{
  const char *wc_path;
  apr_allocator_t *allocator;
  apr_pool_t *pool;
  svn_revnum_t youngest;
  int wc_format;
  svn_client_ctx_t ctx = { 0 };
  struct status_baton sb;
  svn_opt_revision_t rev;
  
  if (argc != 2 && argc != 3)
    {
      fprintf(stderr, "usage: svnversion wc_path [trail_url]\n");
      return EXIT_FAILURE;
    }

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

  ctx.config = apr_hash_make (pool);

  SVN_INT_ERR (svn_utf_cstring_to_utf8 (&wc_path, argv[1], pool));
  wc_path = svn_path_internal_style (wc_path, pool);
  SVN_INT_ERR (svn_wc_check_wc (wc_path, &wc_format, pool));
  if (! wc_format)
    {
      svn_node_kind_t kind;
      SVN_INT_ERR(svn_io_check_path (wc_path, &kind, pool));
      if (kind == svn_node_dir)
        {
          printf ("exported\n");
          svn_pool_destroy (pool);
          return EXIT_SUCCESS;
        }
      else
        {
          fprintf (stderr, "'%s' not versioned, and not exported\n", wc_path);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
    }

  sb.switched = FALSE;
  sb.modified = FALSE;
  sb.min_rev = SVN_INVALID_REVNUM;
  sb.max_rev = SVN_INVALID_REVNUM;
  sb.wc_path = wc_path;
  sb.wc_url = NULL;
  sb.pool = pool;
  rev.kind = svn_opt_revision_unspecified;
  SVN_INT_ERR (svn_client_status (&youngest, wc_path, &rev, analyze_status, 
                                  &sb, TRUE, TRUE, FALSE, FALSE, &ctx, pool));

  if ((! sb.switched ) && (argc == 3))
    {
      /* If the trailing part of the URL of the working copy directory
         does not match the given trailing URL then the whole working
         copy is switched. */
      const char *trail_url;
      SVN_INT_ERR (svn_utf_cstring_to_utf8 (&trail_url, argv[2], pool));
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

  printf ("%" SVN_REVNUM_T_FMT, sb.min_rev);
  if (sb.min_rev != sb.max_rev)
    printf (":%" SVN_REVNUM_T_FMT, sb.max_rev);
  if (sb.modified)
    fputs ("M", stdout);
  if (sb.switched)
    fputs ("S", stdout);
  fputs ("\n", stdout);

  svn_pool_destroy (pool);

  return EXIT_SUCCESS;
}
