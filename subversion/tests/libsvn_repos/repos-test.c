/* repos-test.c --- tests for the filesystem
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>
#include <apr_md5.h>
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_test.h"
#include "../fs-helpers.h"

#include "dir-delta-editor.h"




static svn_error_t *
dir_deltas (const char **msg,
            svn_boolean_t msg_only,
            apr_pool_t *pool)
{ 
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *revision_root;
  svn_revnum_t youngest_rev;
  void *edit_baton;
  const svn_delta_edit_fns_t *editor;
  svn_test__tree_t expected_trees[8];
  int revision_count = 0;
  int i, j;
  apr_pool_t *subpool;

  *msg = "test svn_repos_dir_delta";

  if (msg_only)
    return SVN_NO_ERROR;

  /* The Test Plan
     
     The filesystem function svn_repos_dir_delta exists to drive an
     editor in such a way that given a source tree S and a target tree
     T, that editor manipulation will transform S into T, insomuch as
     directories and files, and their contents and properties, go.
     The general notion of the test plan will be to create pairs of
     trees (S, T), and an editor that edits a copy of tree S, run them
     through svn_repos_dir_delta, and then verify that the edited copy of
     S is identical to T when it is all said and done.  */

  /* Create a filesystem and repository. */
  SVN_ERR (svn_test__create_repos (&repos, "test-repo-dir-deltas", pool));
  fs = svn_repos_fs (repos);
  expected_trees[revision_count].num_entries = 0;
  expected_trees[revision_count++].entries = 0;

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));

  /* Create and commit the greek tree. */
  SVN_ERR (svn_test__create_greek_tree (txn_root, pool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn));
  SVN_ERR (svn_fs_close_txn (txn));

  /***********************************************************************/
  /* REVISION 1 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",        "This is the file 'iota'.\n" },
      { "A",           0 },
      { "A/mu",        "This is the file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/C",         0 },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "This is the file 'rho'.\n" },
      { "A/D/G/tau",   "This is the file 'tau'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", "This is the file 'omega'.\n" }
    };
    expected_trees[revision_count].entries = expected_entries;
    expected_trees[revision_count].num_entries = 20;
    SVN_ERR (svn_fs_revision_root (&revision_root, fs, 
                                   youngest_rev, pool)); 
    SVN_ERR (svn_test__validate_tree 
             (revision_root, expected_trees[revision_count].entries,
              expected_trees[revision_count].num_entries, pool));
    revision_count++;
  }

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, pool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'a', "A/delta",     "This is the file 'delta'.\n" },
      { 'a', "A/epsilon",   "This is the file 'epsilon'.\n" },
      { 'a', "A/B/Z",       0 },
      { 'a', "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { 'd', "A/C",         0 },
      { 'd', "A/mu"         "" },
      { 'd', "A/D/G/tau",   "" },
      { 'd', "A/D/H/omega", "" },
      { 'e', "iota",        "Changed file 'iota'.\n" },
      { 'e', "A/D/G/rho",   "Changed file 'rho'.\n" }
    };
    SVN_ERR (svn_test__txn_script_exec (txn_root, script_entries, 10, pool));
  }
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn));
  SVN_ERR (svn_fs_close_txn (txn));

  /***********************************************************************/
  /* REVISION 2 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "iota",        "Changed file 'iota'.\n" },
      { "A",           0 },
      { "A/delta",     "This is the file 'delta'.\n" },
      { "A/epsilon",   "This is the file 'epsilon'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/B/Z",       0 },
      { "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "Changed file 'rho'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" }
    };
    expected_trees[revision_count].entries = expected_entries;
    expected_trees[revision_count].num_entries = 20;
    SVN_ERR (svn_fs_revision_root (&revision_root, fs, 
                                   youngest_rev, pool)); 
    SVN_ERR (svn_test__validate_tree 
             (revision_root, expected_trees[revision_count].entries,
              expected_trees[revision_count].num_entries, pool));
    revision_count++;
  } 

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, pool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'a', "A/mu",        "Re-added file 'mu'.\n" },
      { 'a', "A/D/H/omega", 0 }, /* re-add omega as directory! */
      { 'd', "iota",        "" },
      { 'e', "A/delta",     "This is the file 'delta'.\nLine 2.\n" }
    };
    SVN_ERR (svn_test__txn_script_exec (txn_root, script_entries, 4, pool));
  }
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn));
  SVN_ERR (svn_fs_close_txn (txn));

  /***********************************************************************/
  /* REVISION 3 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "A",           0 },
      { "A/delta",     "This is the file 'delta'.\nLine 2.\n" },
      { "A/epsilon",   "This is the file 'epsilon'.\n" },
      { "A/mu",        "Re-added file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/B/Z",       0 },
      { "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "Changed file 'rho'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", 0 }
    };
    expected_trees[revision_count].entries = expected_entries;
    expected_trees[revision_count].num_entries = 21;
    SVN_ERR (svn_fs_revision_root (&revision_root, fs, 
                                   youngest_rev, pool)); 
    SVN_ERR (svn_test__validate_tree 
             (revision_root, expected_trees[revision_count].entries,
              expected_trees[revision_count].num_entries, pool));
    revision_count++;
  }

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, pool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  SVN_ERR (svn_fs_copy (revision_root, "A/D/G",
                        txn_root, "A/D/G2",
                        pool));
  SVN_ERR (svn_fs_copy (revision_root, "A/epsilon",
                        txn_root, "A/B/epsilon",
                        pool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn));
  SVN_ERR (svn_fs_close_txn (txn));

  /***********************************************************************/
  /* REVISION 4 */
  /***********************************************************************/
  {
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "A",           0 },
      { "A/delta",     "This is the file 'delta'.\nLine 2.\n" },
      { "A/epsilon",   "This is the file 'epsilon'.\n" },
      { "A/mu",        "Re-added file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/epsilon", "This is the file 'epsilon'.\n" },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/B/F",       0 },
      { "A/B/Z",       0 },
      { "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { "A/D",         0 },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "Changed file 'rho'.\n" },
      { "A/D/G2",      0 },
      { "A/D/G2/pi",   "This is the file 'pi'.\n" },
      { "A/D/G2/rho",  "Changed file 'rho'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", 0 }
    };
    expected_trees[revision_count].entries = expected_entries;
    expected_trees[revision_count].num_entries = 25;
    SVN_ERR (svn_fs_revision_root (&revision_root, fs, 
                                   youngest_rev, pool)); 
    SVN_ERR (svn_test__validate_tree 
             (revision_root, expected_trees[revision_count].entries,
              expected_trees[revision_count].num_entries, pool));
    revision_count++;
  }

  /* THE BIG IDEA: Now that we have a collection of revisions, let's
     first make sure that given any two revisions, we can get the
     right delta between them.  We'll do this by selecting our two
     revisions, R1 and R2, basing a transaction off R1, deltafying the
     txn with respect to R2, and then making sure our final txn looks
     exactly like R2.  This should work regardless of the
     chronological order in which R1 and R2 were created.  */
  subpool = svn_pool_create (pool);
  for (i = 0; i < revision_count; i++)
    {
      for (j = 0; j < revision_count; j++)
        {
          /* Prepare a txn that will receive the changes from
             svn_repos_dir_delta */
          SVN_ERR (svn_fs_begin_txn (&txn, fs, i, subpool));
          SVN_ERR (svn_fs_txn_root (&txn_root, txn, subpool));

          /* Get the editor that will be modifying our transaction. */
          SVN_ERR (dir_delta_get_editor (&editor,
                                         &edit_baton,
                                         fs,
                                         txn_root,
                                         svn_stringbuf_create ("", subpool),
                                         subpool));

          /* Here's the kicker...do the directory delta. */
          SVN_ERR (svn_fs_revision_root (&revision_root, fs, j, subpool)); 
          SVN_ERR (svn_repos_dir_delta (txn_root,
                                        "",
                                        NULL,
                                        revision_root,
                                        "",
                                        editor,
                                        edit_baton,
                                        TRUE,
                                        TRUE,
                                        FALSE,
                                        FALSE,
                                        subpool));

          /* Hopefully at this point our transaction has been modified
             to look exactly like our latest revision.  We'll check
             that. */
          SVN_ERR (svn_test__validate_tree 
                   (txn_root, expected_trees[j].entries,
                    expected_trees[j].num_entries, pool));

          /* We don't really want to do anything with this
             transaction...so we'll abort it (good for software, bad
             bad bad for society). */
          svn_fs_abort_txn (txn);
          svn_pool_clear (subpool);
        }
    }

  svn_pool_destroy (subpool);
  svn_repos_close (repos);
  return SVN_NO_ERROR;
}



/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS (dir_deltas),
    SVN_TEST_NULL
  };



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
