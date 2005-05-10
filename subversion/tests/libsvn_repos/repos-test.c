/* repos-test.c --- tests for the filesystem
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "../svn_test.h"
#include "../svn_test_fs.h"

#include "dir-delta-editor.h"




static svn_error_t *
dir_deltas (const char **msg,
            svn_boolean_t msg_only,
            svn_test_opts_t *opts,
            apr_pool_t *pool)
{ 
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *revision_root;
  svn_revnum_t youngest_rev;
  void *edit_baton;
  const svn_delta_editor_t *editor;
  svn_test__tree_t expected_trees[8];
  int revision_count = 0;
  int i, j;
  apr_pool_t *subpool = svn_pool_create (pool);

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
  SVN_ERR (svn_test__create_repos (&repos, "test-repo-dir-deltas", 
                                   opts->fs_type, pool));
  fs = svn_repos_fs (repos);
  expected_trees[revision_count].num_entries = 0;
  expected_trees[revision_count++].entries = 0;

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, subpool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, subpool));
  SVN_ERR (svn_test__create_greek_tree (txn_root, subpool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, subpool));

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
                                   youngest_rev, subpool)); 
    SVN_ERR (svn_test__validate_tree 
             (revision_root, expected_trees[revision_count].entries,
              expected_trees[revision_count].num_entries, subpool));
    revision_count++;
  }
  svn_pool_clear (subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, subpool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, subpool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'a', "A/delta",     "This is the file 'delta'.\n" },
      { 'a', "A/epsilon",   "This is the file 'epsilon'.\n" },
      { 'a', "A/B/Z",       0 },
      { 'a', "A/B/Z/zeta",  "This is the file 'zeta'.\n" },
      { 'd', "A/C",         0 },
      { 'd', "A/mu",        "" },
      { 'd', "A/D/G/tau",   "" },
      { 'd', "A/D/H/omega", "" },
      { 'e', "iota",        "Changed file 'iota'.\n" },
      { 'e', "A/D/G/rho",   "Changed file 'rho'.\n" }
    };
    SVN_ERR (svn_test__txn_script_exec (txn_root, script_entries, 10, 
                                        subpool));
  }
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, subpool));

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
                                   youngest_rev, subpool)); 
    SVN_ERR (svn_test__validate_tree 
             (revision_root, expected_trees[revision_count].entries,
              expected_trees[revision_count].num_entries, subpool));
    revision_count++;
  } 
  svn_pool_clear (subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, subpool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, subpool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'a', "A/mu",        "Re-added file 'mu'.\n" },
      { 'a', "A/D/H/omega", 0 }, /* re-add omega as directory! */
      { 'd', "iota",        "" },
      { 'e', "A/delta",     "This is the file 'delta'.\nLine 2.\n" }
    };
    SVN_ERR (svn_test__txn_script_exec (txn_root, script_entries, 4, subpool));
  }
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, subpool));

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
                                   youngest_rev, subpool)); 
    SVN_ERR (svn_test__validate_tree 
             (revision_root, expected_trees[revision_count].entries,
              expected_trees[revision_count].num_entries, subpool));
    revision_count++;
  }
  svn_pool_clear (subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, subpool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, subpool));
  SVN_ERR (svn_fs_revision_root (&revision_root, fs, youngest_rev, subpool)); 
  SVN_ERR (svn_fs_copy (revision_root, "A/D/G",
                        txn_root, "A/D/G2",
                        subpool));
  SVN_ERR (svn_fs_copy (revision_root, "A/epsilon",
                        txn_root, "A/B/epsilon",
                        subpool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, subpool));

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
              expected_trees[revision_count].num_entries, subpool));
    revision_count++;
  }
  svn_pool_clear (subpool);

  /* THE BIG IDEA: Now that we have a collection of revisions, let's
     first make sure that given any two revisions, we can get the
     right delta between them.  We'll do this by selecting our two
     revisions, R1 and R2, basing a transaction off R1, deltafying the
     txn with respect to R2, and then making sure our final txn looks
     exactly like R2.  This should work regardless of the
     chronological order in which R1 and R2 were created.  */
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
                                         "",
                                         subpool));

          /* Here's the kicker...do the directory delta. */
          SVN_ERR (svn_fs_revision_root (&revision_root, fs, j, subpool)); 
          SVN_ERR (svn_repos_dir_delta (txn_root,
                                        "",
                                        "",
                                        revision_root,
                                        "",
                                        editor,
                                        edit_baton,
                                        NULL,
                                        NULL,
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
                    expected_trees[j].num_entries, subpool));

          /* We don't really want to do anything with this
             transaction...so we'll abort it (good for software, bad
             bad bad for society). */
          svn_error_clear (svn_fs_abort_txn (txn, subpool));
          svn_pool_clear (subpool);
        }
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
node_tree_delete_under_copy (const char **msg,
                             svn_boolean_t msg_only,
                             svn_test_opts_t *opts,
                             apr_pool_t *pool)
{ 
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *revision_root, *revision_2_root;
  svn_revnum_t youngest_rev;
  void *edit_baton;
  const svn_delta_editor_t *editor; 
  svn_repos_node_t *tree;
  apr_pool_t *subpool = svn_pool_create (pool);

  *msg = "test deletions under copies in node_tree code";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a filesystem and repository. */
  SVN_ERR (svn_test__create_repos (&repos, "test-repo-del-under-copy", 
                                   opts->fs_type, pool));
  fs = svn_repos_fs (repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));

  /* Create and commit the greek tree. */
  SVN_ERR (svn_test__create_greek_tree (txn_root, pool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, pool));

  /* Now, commit again, this time after copying a directory, and then
     deleting some paths under that directory. */
  SVN_ERR (svn_fs_revision_root (&revision_root, fs, youngest_rev, pool)); 
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, pool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  SVN_ERR (svn_fs_copy (revision_root, "A", txn_root, "Z", pool));
  SVN_ERR (svn_fs_delete (txn_root, "Z/D/G/rho", pool));
  SVN_ERR (svn_fs_delete (txn_root, "Z/D/H", pool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, pool));

  /* Now, we run the node_tree editor code, and see that a) it doesn't
     bomb out, and b) that our nodes are all good. */
  SVN_ERR (svn_fs_revision_root (&revision_2_root, fs, youngest_rev, pool)); 
  SVN_ERR (svn_repos_node_editor (&editor, &edit_baton, repos,
                                  revision_root, revision_2_root, 
                                  pool, subpool));
  SVN_ERR (svn_repos_replay (revision_2_root, editor, edit_baton, subpool));
  
  /* Get the root of the generated tree, and cleanup our mess. */
  tree = svn_repos_node_from_baton (edit_baton);
  svn_pool_destroy (subpool);

  /* See that we got what we expected (fortunately, svn_repos_replay
     drivers editor paths in a predictable fashion!). */

  if (! (tree /* / */
         && tree->child /* /Z */
         && tree->child->child /* /Z/D */
         && tree->child->child->child /* /Z/D/G */
         && tree->child->child->child->child /* /Z/D/G/rho */
         && tree->child->child->child->sibling)) /* /Z/D/H */
    return svn_error_create (SVN_ERR_TEST_FAILED, NULL, 
                             "Generated node tree is bogus.");

  if (! ((strcmp (tree->name, "") == 0)
         && (strcmp (tree->child->name, "Z") == 0)
         && (strcmp (tree->child->child->name, "D") == 0)
         && (strcmp (tree->child->child->child->name, "G") == 0)
         && ((strcmp (tree->child->child->child->child->name, "rho") == 0)
             && (tree->child->child->child->child->kind == svn_node_file)
             && (tree->child->child->child->child->action == 'D'))
         && ((strcmp (tree->child->child->child->sibling->name, "H") == 0)
             && (tree->child->child->child->sibling->kind == svn_node_dir)
             && (tree->child->child->child->sibling->action == 'D'))))
    return svn_error_create (SVN_ERR_TEST_FAILED, NULL, 
                             "Generated node tree is bogus.");

  return SVN_NO_ERROR;
}


/* Helper for revisions_changed(). */
static const char *
print_chrevs (const apr_array_header_t *revs_got,
              int num_revs_expected,
              const svn_revnum_t *revs_expected,
              apr_pool_t *pool)
{
  int i;
  const char *outstr;
  svn_revnum_t rev;

  outstr = apr_psprintf (pool, "Got: { ");
  if (revs_got)
    {
      for (i = 0; i < revs_got->nelts; i++)
        {
          rev = ((svn_revnum_t *)revs_got->elts)[i];
          outstr = apr_pstrcat (pool, 
                                outstr,
                                apr_psprintf (pool, "%ld ", rev),
                                NULL);
        }
    }
  outstr = apr_pstrcat (pool, outstr, "}  Expected: { ", NULL);
  for (i = 0; i < num_revs_expected; i++)
    {
      outstr = apr_pstrcat (pool, 
                            outstr,
                            apr_psprintf (pool, "%ld ",
                                          revs_expected[i]),
                            NULL);
    }
  return apr_pstrcat (pool, outstr, "}", NULL);
}


/* Implements svn_repos_history_func_t interface.  Accumulate history
   revisions the apr_array_header_t * which is the BATON. */
static svn_error_t *
history_to_revs_array (void *baton,
                       const char *path,
                       svn_revnum_t revision,
                       apr_pool_t *pool)
{
  apr_array_header_t *revs_array = baton;
  APR_ARRAY_PUSH (revs_array, svn_revnum_t) = revision;
  return SVN_NO_ERROR;
}

struct revisions_changed_results
{
  const char *path;
  int num_revs;
  svn_revnum_t revs_changed[11];
};


static svn_error_t *
revisions_changed (const char **msg,
                   svn_boolean_t msg_only,
                   svn_test_opts_t *opts,
                   apr_pool_t *pool)
{ 
  apr_pool_t *spool = svn_pool_create (pool);
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t youngest_rev = 0;
  
  *msg = "test svn_repos_history() (partially)";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a filesystem and repository. */
  SVN_ERR (svn_test__create_repos (&repos, "test-repo-revisions-changed", 
                                   opts->fs_type, pool));
  fs = svn_repos_fs (repos);

  /*** Testing Algorithm ***

     1.  Create a greek tree in revision 1.
     2.  Make a series of new revisions, changing a file here and file
         there.
     3.  Loop over each path in each revision, verifying that we get
         the right revisions-changed array back from the filesystem.
  */

  /* Created the greek tree in revision 1. */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_test__create_greek_tree (txn_root, spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 2 - mu, alpha, omega */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/mu", "2", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/B/E/alpha", "2", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/H/omega", "2", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 3 - iota, lambda, psi, omega */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "iota", "3", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/B/lambda", "3", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/H/psi", "3", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/H/omega", "3", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 4 - iota, beta, gamma, pi, rho */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "iota", "4", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/B/E/beta", "4", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/gamma", "4", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/G/pi", "4", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/G/rho", "4", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 5 - mu, alpha, tau, chi */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/mu", "5", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/B/E/alpha", "5", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/G/tau", "5", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/H/chi", "5", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 6 - move A/D to A/Z */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_fs_revision_root (&rev_root, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_copy (rev_root, "A/D", txn_root, "A/Z", spool));
  SVN_ERR (svn_fs_delete (txn_root, "A/D", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 7 - edit A/Z/G/pi */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/Z/G/pi", "7", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 8 - move A/Z back to A/D, edit iota */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_fs_revision_root (&rev_root, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_copy (rev_root, "A/Z", txn_root, "A/D", spool));
  SVN_ERR (svn_fs_delete (txn_root, "A/Z", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "iota", "8", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 9 - copy A/D/G to A/D/Q */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_fs_revision_root (&rev_root, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_copy (rev_root, "A/D/G", txn_root, "A/D/Q", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Revision 10 - edit A/D/Q/pi and A/D/Q/rho */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, youngest_rev, spool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/Q/pi", "10", spool));
  SVN_ERR (svn_test__set_file_contents (txn_root, "A/D/Q/rho", "10", spool));
  SVN_ERR (svn_fs_commit_txn (NULL, &youngest_rev, txn, spool));
  svn_pool_clear (spool);

  /* Now, it's time to verify our results. */
  {
    int j;
    /* Number, and list of, changed revisions for each path.  Note
       that for now, bubble-up in directories causes the directory to
       appear changed though no entries were added or removed, and no
       property mods occurred.  Also note that this matrix represents
       only the final state of the paths existing in HEAD of the
       repository.

       Notice for each revision, you can glance down that revision's
       column in this table and see all the paths modified directory or
       via bubble-up. */
    static const struct revisions_changed_results test_data[25] = {
      /* path,          num,    revisions changed... */
      { "",              11,    { 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 } },
      { "iota",           4,    {        8,          4, 3,    1    } },
      { "A",             10,    { 10, 9, 8, 7, 6, 5, 4, 3, 2, 1    } },
      { "A/mu",           3,    {                 5,       2, 1    } },
      { "A/B",            5,    {                 5, 4, 3, 2, 1    } },
      { "A/B/lambda",     2,    {                       3,    1    } },
      { "A/B/E",          4,    {                 5, 4,    2, 1    } },
      { "A/B/E/alpha",    3,    {                 5,       2, 1    } },
      { "A/B/E/beta",     2,    {                    4,       1    } },
      { "A/B/F",          1,    {                             1    } },
      { "A/C",            1,    {                             1    } },
      { "A/D",           10,    { 10, 9, 8, 7, 6, 5, 4, 3, 2, 1    } },
      { "A/D/gamma",      4,    {        8,    6,    4,       1    } },
      { "A/D/G",          6,    {        8, 7, 6, 5, 4,       1    } },
      { "A/D/G/pi",       5,    {        8, 7, 6,    4,       1    } },
      { "A/D/G/rho",      4,    {        8,    6,    4,       1    } },
      { "A/D/G/tau",      4,    {        8,    6, 5,          1    } },
      { "A/D/Q",          8,    { 10, 9, 8, 7, 6, 5, 4,       1    } },
      { "A/D/Q/pi",       7,    { 10, 9, 8, 7, 6,    4,       1    } },
      { "A/D/Q/rho",      6,    { 10, 9, 8,    6,    4,       1    } },
      { "A/D/Q/tau",      5,    {     9, 8,    6, 5,          1    } },
      { "A/D/H",          6,    {        8,    6, 5,    3, 2, 1    } },
      { "A/D/H/chi",      4,    {        8,    6, 5,          1    } },
      { "A/D/H/psi",      4,    {        8,    6,       3,    1    } },
      { "A/D/H/omega",    5,    {        8,    6,       3, 2, 1    } }
    };
    
    /* Now, for each path in the revision, get its changed-revisions
       array and compare the array to the static results above.  */
    for (j = 0; j < 25; j++)
      {
        int i;
        const char *path = test_data[j].path;
        int num_revs = test_data[j].num_revs;
        const svn_revnum_t *revs_changed = test_data[j].revs_changed;
        apr_array_header_t *revs = apr_array_make (spool, 10, 
                                                   sizeof (svn_revnum_t));

        SVN_ERR (svn_repos_history (fs, path, history_to_revs_array, revs, 
                                    0, youngest_rev, TRUE, spool));

        /* Are we at least looking at the right number of returned
           revisions? */
        if ((! revs) || (revs->nelts != num_revs))
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "Changed revisions differ from expected for '%s'\n%s",
             path, print_chrevs (revs, num_revs, revs_changed, spool));

        /* Do the revisions lists match up exactly? */
        for (i = 0; i < num_revs; i++)
          {
            svn_revnum_t rev = ((svn_revnum_t *)revs->elts)[i];
            if (rev != revs_changed[i])
              return svn_error_createf
                (SVN_ERR_FS_GENERAL, NULL,
                 "Changed revisions differ from expected for '%s'\n%s",
                 path, print_chrevs (revs, num_revs, revs_changed, spool));
          }
        
        /* Clear the per-iteration subpool. */
        svn_pool_clear (spool);
      }
  }

  /* Destroy the subpool. */
  svn_pool_destroy (spool);

  return SVN_NO_ERROR;
}



struct locations_info
{
  svn_revnum_t rev;
  const char *path;
};

/* Check that LOCATIONS contain everything in INFO and nothing more. */
static svn_error_t *
check_locations_info (apr_hash_t *locations, const struct locations_info *info)
{
  unsigned int i;
  for (i = 0; info->rev != 0; ++i, ++info)
    {
      const char *p = apr_hash_get (locations, &info->rev, sizeof
                                    (svn_revnum_t));
      if (!p)
        return svn_error_createf (SVN_ERR_TEST_FAILED, NULL,
                                  "Missing path for revision %ld", info->rev);
      if (strcmp (p, info->path) != 0)
        return svn_error_createf (SVN_ERR_TEST_FAILED, NULL,
                                  "Pth mismatch for rev %ld", info->rev);
    }

  if (apr_hash_count (locations) > i)
    return svn_error_create (SVN_ERR_TEST_FAILED, NULL,
                             "Returned locations contain too many elements.");
  
  return SVN_NO_ERROR;
}

/* Check that all locations in INFO exist in REPOS for PATH and PEG_REVISION.
 */
static svn_error_t *
check_locations (svn_fs_t *fs, struct locations_info *info,
                 const char *path, svn_revnum_t peg_revision,
                 apr_pool_t *pool)
{
  apr_array_header_t *a = apr_array_make (pool, 0, sizeof (svn_revnum_t));
  apr_hash_t *h;
  struct locations_info *iter;

  for (iter = info; iter->rev != 0; ++iter)
    *(svn_revnum_t *) apr_array_push (a) = iter->rev;

  SVN_ERR (svn_repos_trace_node_locations (fs, &h, path, peg_revision, a,
                                           NULL, NULL, pool));
  SVN_ERR (check_locations_info (h, info));

  return SVN_NO_ERROR;
}

static svn_error_t *
node_locations (const char **msg, 
                svn_boolean_t msg_only, 
                svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t youngest_rev;

  *msg = "test svn_repos_node_locations";
  if (msg_only)
    return SVN_NO_ERROR;

  /* Create the repository with a Greek tree. */
  SVN_ERR (svn_test__create_repos (&repos, "test-repo-node-locations", 
                                   opts->fs_type, pool));
  fs = svn_repos_fs (repos);
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, subpool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, subpool));
  SVN_ERR (svn_test__create_greek_tree (txn_root, subpool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, subpool));
  svn_pool_clear (subpool);

  /* Move a file. Rev 2. */
  SVN_ERR (svn_fs_revision_root (&root, fs, youngest_rev, subpool));
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, subpool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, subpool));
  SVN_ERR (svn_fs_copy (root, "/A/mu", txn_root, "/mu.new", subpool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, subpool));
  {
    struct locations_info info[] =
      {
        { 1, "/A/mu" },
        { 2, "/mu.new" },
        { 0 }
      };
    SVN_ERR (check_locations (fs, info, "/mu.new", 2, pool));
  }
  svn_pool_clear (subpool);
  
  return SVN_NO_ERROR;
}



/* Testing the reporter. */

/* Functions for an editor that will catch removal of defunct locks. */

/* The main editor baton. */
typedef struct rmlocks_baton_t {
  apr_hash_t *removed;
  apr_pool_t *pool;
} rmlocks_baton_t;

/* The file baton. */
typedef struct rmlocks_file_baton_t {
  rmlocks_baton_t *main_baton;
  const char *path;
} rmlocks_file_baton_t;

/* An svn_delta_editor_t function. */
static svn_error_t *
rmlocks_open_file (const char *path,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   apr_pool_t *file_pool,
                   void **file_baton)
{
  rmlocks_file_baton_t *fb = apr_palloc (file_pool, sizeof (*fb));
  rmlocks_baton_t *b = parent_baton;

  fb->main_baton = b;
  fb->path = apr_pstrdup (b->pool, path);

  *file_baton = fb;

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
rmlocks_change_prop (void *file_baton,
                     const char *name,
                     const svn_string_t *value,
                     apr_pool_t *pool)
{
  rmlocks_file_baton_t *fb = file_baton;

  if (strcmp (name, SVN_PROP_ENTRY_LOCK_TOKEN) == 0)
    {
      if (value != NULL)
        return svn_error_create (SVN_ERR_TEST_FAILED, NULL,
                                 "Value for lock-token property not NULL");

      /* We only want it removed once. */
      if (apr_hash_get (fb->main_baton->removed, fb->path,
                        APR_HASH_KEY_STRING) != NULL)
        return svn_error_createf (SVN_ERR_TEST_FAILED, NULL,
                                  "Lock token for '%s' already removed",
                                  fb->path);

      /* Mark as removed. */
      apr_hash_set (fb->main_baton->removed, fb->path, APR_HASH_KEY_STRING,
                    (void *)1);
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
rmlocks_open_root (void *edit_baton,
                   svn_revnum_t base_revision,
                   apr_pool_t *dir_pool,
                   void **root_baton)
{
  *root_baton = edit_baton;
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
rmlocks_open_directory (const char *path,
                        void *parent_baton,
                        svn_revnum_t base_revision,
                        apr_pool_t *pool,
                        void **dir_baton)
{
  *dir_baton = parent_baton;
  return SVN_NO_ERROR;
}

/* Create an svn_delta_editor/baton, storing them in EDITOR/EDIT_BATON,
   that will store paths for which lock tokens were *REMOVED in REMOVED.
   Allocate the editor and *REMOVED in POOL. */
static svn_error_t *
create_rmlocks_editor (svn_delta_editor_t **editor,
                       void **edit_baton,
                       apr_hash_t **removed,
                       apr_pool_t *pool)
{
  rmlocks_baton_t *baton = apr_palloc (pool, sizeof (*baton));

  /* Create the editor. */
  *editor = svn_delta_default_editor (pool);
  (*editor)->open_root = rmlocks_open_root;
  (*editor)->open_directory = rmlocks_open_directory;
  (*editor)->open_file = rmlocks_open_file;
  (*editor)->change_file_prop = rmlocks_change_prop;

  /* Initialize the baton. */
  baton->removed = apr_hash_make (pool);
  baton->pool = pool;
  *edit_baton = baton;

  *removed = baton->removed;

  return SVN_NO_ERROR;
}

/* Check that HASH contains exactly the const char * entries for all entries
   in the NULL-terminated array SPEC. */
static svn_error_t *
rmlocks_check (const char **spec, apr_hash_t *hash)
{
  apr_size_t n = 0;

  for (; *spec; ++spec, ++n)
    {
      if (! apr_hash_get (hash, *spec, APR_HASH_KEY_STRING))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "Lock token for '%s' should have been removed", *spec);
    }

  if (n < apr_hash_count (hash))
    return svn_error_create (SVN_ERR_TEST_FAILED, NULL,
                             "Lock token for one or more paths unexpectedly "
                             "removed");
  return SVN_NO_ERROR;
}

/* Test that defunct locks are removed by the reporter. */
static svn_error_t *
rmlocks (const char **msg,
         svn_boolean_t msg_only,
         svn_test_opts_t *opts,
         apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_revnum_t youngest_rev;
  svn_delta_editor_t *editor;
  void *edit_baton, *report_baton;
  svn_lock_t *l1, *l2, *l3, *l4;
  svn_fs_access_t *fs_access;
  apr_hash_t *removed;

  *msg = "test removal of defunct locks";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a filesystem and repository. */
  SVN_ERR (svn_test__create_repos (&repos, "test-repo-rmlocks", 
                                   opts->fs_type, pool));
  fs = svn_repos_fs (repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, subpool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, subpool));
  SVN_ERR (svn_test__create_greek_tree (txn_root, subpool));
  SVN_ERR (svn_repos_fs_commit_txn (NULL, repos, &youngest_rev, txn, subpool));
  svn_pool_clear (subpool);

  SVN_ERR (svn_fs_create_access (&fs_access, "user1", pool));
  SVN_ERR (svn_fs_set_access (fs, fs_access));

  /* Lock some files, break a lock, steal another and check that those get
     removed. */
  {
    const char *expected [] = { "A/mu", "A/D/gamma", NULL };

    SVN_ERR (svn_fs_lock (&l1, fs, "/iota", NULL, NULL, 0, 0, youngest_rev,
                          FALSE, subpool));
    SVN_ERR (svn_fs_lock (&l2, fs, "/A/mu", NULL, NULL, 0, 0, youngest_rev,
                          FALSE, subpool));
    SVN_ERR (svn_fs_lock (&l3, fs, "/A/D/gamma", NULL, NULL, 0, 0, youngest_rev,
                          FALSE, subpool));

    /* Break l2. */
    SVN_ERR (svn_fs_unlock (fs, "/A/mu", NULL, TRUE, subpool));

    /* Steal l3 from ourselves. */
    SVN_ERR (svn_fs_lock (&l4, fs, "/A/D/gamma", NULL, NULL, 0, 0, youngest_rev,
                          TRUE, subpool));

    /* Create the editor. */
    SVN_ERR (create_rmlocks_editor (&editor, &edit_baton, &removed, subpool));

    /* Report what we have. */
    SVN_ERR (svn_repos_begin_report (&report_baton, 1, "user1", repos, "/", "",
                                     NULL, FALSE, TRUE, FALSE, editor,
                                     edit_baton, NULL, NULL, subpool));
    SVN_ERR (svn_repos_set_path2 (report_baton, "", 1, FALSE, NULL,
                                  subpool));
    SVN_ERR (svn_repos_set_path2 (report_baton, "iota", 1, FALSE, l1->token,
                                  subpool));
    SVN_ERR (svn_repos_set_path2 (report_baton, "A/mu", 1, FALSE, l2->token,
                                  subpool));
    SVN_ERR (svn_repos_set_path2 (report_baton, "A/D/gamma", 1, FALSE,
                                  l3->token, subpool));
    
    /* End the report. */
    SVN_ERR (svn_repos_finish_report (report_baton, pool));

    /* And check that the edit did what we wanted. */
    SVN_ERR (rmlocks_check (expected, removed));
  }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS (dir_deltas),
    SVN_TEST_PASS (node_tree_delete_under_copy),
    SVN_TEST_PASS (revisions_changed),
    SVN_TEST_PASS (node_locations),
    SVN_TEST_PASS (rmlocks),
    SVN_TEST_NULL
  };
