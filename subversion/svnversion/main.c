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

#include "../clients/init_cmdline.h"

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_utf.h"
#include "svn_path.h"
#include <apr_tables.h>

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
  apr_pool_t *pool;
  svn_error_t *err;
  apr_hash_t *status_hash;
  apr_hash_index_t *hi;
  svn_revnum_t youngest;
  svn_boolean_t switched = FALSE, modified = FALSE;
  svn_revnum_t min_revnum = SVN_INVALID_REVNUM, max_revnum = SVN_INVALID_REVNUM;
  const svn_wc_status_t *status;
  svn_client_ctx_t ctx = { 0 };

  if (argc != 2 && argc != 3)
    {
      fprintf(stderr, "usage: svnversion wc_path [trail_url]\n");
      return EXIT_FAILURE;
    }

  /* Initialize the app. */
  if (init_cmdline ("svnversion", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool. */
  pool = svn_pool_create (NULL);

  SVN_INT_ERR (svn_utf_cstring_to_utf8 (&wc_path, argv[1], NULL, pool));
  wc_path = svn_path_internal_style (wc_path, pool);
  err = svn_client_status (&status_hash, &youngest, wc_path, TRUE, TRUE,
                           FALSE, FALSE, &ctx, pool);
  if (err)
    {
      svn_node_kind_t kind;
      svn_error_t *err3 = svn_io_check_path (wc_path, &kind, pool);
      if (! err3 && kind == svn_node_dir)
        {
          printf ("exported\n");
          svn_pool_destroy (pool);
          return EXIT_SUCCESS;
        }
      svn_handle_error (err, stderr, 0);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  for (hi = apr_hash_first (pool, status_hash); hi; hi = apr_hash_next (hi))
    {
      void *val;

      apr_hash_this (hi, NULL, NULL, &val);
      status = val;

      if (!status->entry)
        continue;

      /* Added files have a revision of no interest */
      if (status->text_status != svn_wc_status_added)
        {
          if (min_revnum == SVN_INVALID_REVNUM
              || status->entry->revision < min_revnum)
            min_revnum = status->entry->revision;

          if (max_revnum == SVN_INVALID_REVNUM
              || status->entry->revision > max_revnum)
            max_revnum = status->entry->revision;
        }

      switched |= status->switched;
      modified |= (status->text_status != svn_wc_status_normal);
      modified |= (status->prop_status != svn_wc_status_normal
                   && status->prop_status != svn_wc_status_none);
    }

  /* If the trailing part of the URL of the working copy directory does not
     match the given trailing URL then the whole working copy is
     switched. */
  if (! switched && argc == 3)
    {
      const char *trail_url;
      SVN_INT_ERR (svn_utf_cstring_to_utf8 (&trail_url, argv[2], NULL, pool));

      status = apr_hash_get (status_hash, wc_path, APR_HASH_KEY_STRING);
      if (! status)
        switched = TRUE;
      else
        {
          apr_size_t len1 = strlen (trail_url);
          apr_size_t len2 = strlen (status->entry->url);
          if (len1 > len2
              || strcmp (status->entry->url + len2 - len1, trail_url))
            switched = TRUE;
        }
    }

  printf ("%" SVN_REVNUM_T_FMT, min_revnum);
  if (min_revnum != max_revnum)
    printf (":%" SVN_REVNUM_T_FMT, max_revnum);
  if (modified)
    fputs ("M", stdout);
  if (switched)
    fputs ("S", stdout);
  fputs ("\n", stdout);

  svn_pool_destroy (pool);

  return EXIT_SUCCESS;
}
