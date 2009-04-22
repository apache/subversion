/*
 * svn-populate-node-origins-index.c : Populate the repository's node
 *                                     origins index.
 *
 * ====================================================================
 * Copyright (c) 2007-2009 CollabNet.  All rights reserved.
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
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_utf.h"

/* Used to terminate lines in large multi-line string literals. */
#define NL APR_EOL_STR

static const char *usage_summary =
  "Crawl the Subversion repository located at REPOS-PATH in an attempt to"  NL
  "populate that repository's index of node origins.  "                     NL
  ""                                                                        NL
  "The node origins index is new as of Subversion 1.5, and behaves as a"    NL
  "cache to vastly speed up certain history-querying operations.  For"      NL
  "compatibility with repositories created with pre-1.5 versions of"        NL
  "Subversion, Subversion will gracefully handle cache misses by doing a"   NL
  "brute-force calculation of the query answer and lazily populating the"   NL
  "index with answers it calculates.  Unfortunately, calculating that"      NL
  "information using the brute-force method (instead of having the"         NL
  "information appear in the index organically) can be very costly."        NL
  ""                                                                        NL
  "This tool triggers the lazy index population logic built into"           NL
  "Subversion in a fashion far more efficient than is likely to happen"     NL
  "during typical repository usage.  It can be run while the repository"    NL
  "is online, too, without interrupting normal Subversion activities."      NL;

/* Print a usage message for this program (PROGNAME), possibly with an
   error message ERR_MSG, if not NULL.  */
static void
usage_maybe_with_err(const char *progname, const char *err_msg)
{
  FILE *out;

  out = err_msg ? stderr : stdout;
  fprintf(out, "Usage: %s REPOS-PATH\n\n%s", progname, usage_summary);
  if (err_msg)
    fprintf(out, "\nERROR: %s\n", err_msg);
}

/* Build the node-origins index any newly added items introduced in
   REVISION in FS.  Set *COUNT to the number of new items found.  */
static svn_error_t *
index_revision_adds(int *count, svn_fs_t *fs,
                    svn_revnum_t revision, apr_pool_t *pool)
{
  svn_fs_root_t *root;
  apr_hash_t *changes;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  *count = 0;
  SVN_ERR(svn_fs_revision_root(&root, fs, revision, pool));
  SVN_ERR(svn_fs_paths_changed(&changes, root, pool));

  /* No paths changed in this revision?  Nothing to do.  */
  if (apr_hash_count(changes) == 0)
    return SVN_NO_ERROR;

  subpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, changes); hi; hi = apr_hash_next(hi))
    {
      const void *path;
      void *val;
      svn_fs_path_change_t *change;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &path, NULL, &val);
      change = val;
      if ((change->change_kind == svn_fs_path_change_add)
          || (change->change_kind == svn_fs_path_change_replace))
        {
          const char *copyfrom_path;
          svn_revnum_t copyfrom_rev;

          SVN_ERR(svn_fs_copied_from(&copyfrom_rev, &copyfrom_path,
                                     root, path, subpool));
          if (! (copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_rev)))
            {
              svn_revnum_t origin;
              SVN_ERR(svn_fs_node_origin_rev(&origin, root, path, subpool));
              (*count)++;
            }
        }
    }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Build the node-origins index for the repository located at REPOS_PATH.  */
static svn_error_t *
build_index(const char *repos_path, apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_revnum_t youngest_rev;
  int i, slotsize;
  const char *progress_fmt;
  apr_pool_t *subpool;

  /* Open the repository. */
  SVN_ERR(svn_repos_open(&repos, repos_path, pool));

  /* Get a filesystem object. */
  fs = svn_repos_fs(repos);

  /* Fetch the youngest revision of the repository. */
  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, pool));
  slotsize = strlen(apr_ltoa(pool, youngest_rev));
  progress_fmt = apr_psprintf(pool,
                              "[%%%dd/%%%dd]  Found %%d new lines of history."
                              "\n", slotsize, slotsize);

  /* Now, iterate over all the revisions, calling index_revision_adds(). */
  subpool = svn_pool_create(pool);
  for (i = 0; i < youngest_rev; i++)
    {
      int count;
      svn_pool_clear(subpool);
      SVN_ERR(index_revision_adds(&count, fs, i + 1, subpool));
      printf(progress_fmt, i + 1, youngest_rev, count);
    }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


int
main(int argc, const char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err = SVN_NO_ERROR;
  const char *repos_path;

  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init(argv[0], stderr) == EXIT_FAILURE)
    return EXIT_FAILURE;

  pool = svn_pool_create(NULL);

  if (argc <= 1)
    {
      usage_maybe_with_err(argv[0], "Not enough arguments.");
      goto cleanup;
    }

  /* Convert argv[1] into a UTF8, internal-format, canonicalized path. */
  if ((err = svn_utf_cstring_to_utf8(&repos_path, argv[1], pool)))
    goto cleanup;
  repos_path = svn_path_internal_style(repos_path, pool);
  repos_path = svn_path_canonicalize(repos_path, pool);

  if ((err = build_index(repos_path, pool)))
    goto cleanup;

 cleanup:
  svn_pool_destroy(pool);

  if (err)
    {
      svn_handle_error2(err, stderr, FALSE,
                        "svn-populate-node-origins-index: ");
      return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}
