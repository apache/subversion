/* repos-test.c --- tests for the filesystem
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

#include <stdlib.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_time.h>

#include "../svn_test.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_hash.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_config.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_version.h"
#include "private/svn_repos_private.h"
#include "private/svn_dep_compat.h"

/* be able to look into svn_config_t */
#include "../../libsvn_subr/config_impl.h"

#include "../svn_test_fs.h"

#include "dir-delta-editor.h"

/* Used to terminate lines in large multi-line string literals. */
#define NL APR_EOL_STR

/* Compare strings, like strcmp but either or both may be NULL which
 * compares equal to NULL and not equal to any non-NULL string. */
static int
strcmp_null(const char *s1, const char *s2)
{
  if (s1 && s2)
    return strcmp(s1, s2);
  else if (s1 || s2)
    return 1;
  else
    return 0;
}



static svn_error_t *
dir_deltas(const svn_test_opts_t *opts,
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
  apr_pool_t *subpool = svn_pool_create(pool);

  /* The Test Plan

     The filesystem function svn_repos_dir_delta2 exists to drive an
     editor in such a way that given a source tree S and a target tree
     T, that editor manipulation will transform S into T, insomuch as
     directories and files, and their contents and properties, go.
     The general notion of the test plan will be to create pairs of
     trees (S, T), and an editor that edits a copy of tree S, run them
     through svn_repos_dir_delta2, and then verify that the edited copy of
     S is identical to T when it is all said and done.  */

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-dir-deltas",
                                 opts, pool));
  fs = svn_repos_fs(repos);
  expected_trees[revision_count].num_entries = 0;
  expected_trees[revision_count++].entries = 0;

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

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
    SVN_ERR(svn_fs_revision_root(&revision_root, fs,
                                 youngest_rev, subpool));
    SVN_ERR(svn_test__validate_tree
            (revision_root, expected_trees[revision_count].entries,
             expected_trees[revision_count].num_entries, subpool));
    revision_count++;
  }
  svn_pool_clear(subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
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
    SVN_ERR(svn_test__txn_script_exec(txn_root, script_entries, 10,
                                      subpool));
  }
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

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
    SVN_ERR(svn_fs_revision_root(&revision_root, fs,
                                 youngest_rev, subpool));
    SVN_ERR(svn_test__validate_tree
            (revision_root, expected_trees[revision_count].entries,
             expected_trees[revision_count].num_entries, subpool));
    revision_count++;
  }
  svn_pool_clear(subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'a', "A/mu",        "Re-added file 'mu'.\n" },
      { 'a', "A/D/H/omega", 0 }, /* re-add omega as directory! */
      { 'd', "iota",        "" },
      { 'e', "A/delta",     "This is the file 'delta'.\nLine 2.\n" }
    };
    SVN_ERR(svn_test__txn_script_exec(txn_root, script_entries, 4, subpool));
  }
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

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
    SVN_ERR(svn_fs_revision_root(&revision_root, fs,
                                 youngest_rev, subpool));
    SVN_ERR(svn_test__validate_tree
            (revision_root, expected_trees[revision_count].entries,
             expected_trees[revision_count].num_entries, subpool));
    revision_count++;
  }
  svn_pool_clear(subpool);

  /* Make a new txn based on the youngest revision, make some changes,
     and commit those changes (which makes a new youngest
     revision). */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&revision_root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_copy(revision_root, "A/D/G",
                      txn_root, "A/D/G2",
                      subpool));
  SVN_ERR(svn_fs_copy(revision_root, "A/epsilon",
                      txn_root, "A/B/epsilon",
                      subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

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
    SVN_ERR(svn_fs_revision_root(&revision_root, fs,
                                 youngest_rev, pool));
    SVN_ERR(svn_test__validate_tree
            (revision_root, expected_trees[revision_count].entries,
             expected_trees[revision_count].num_entries, subpool));
    revision_count++;
  }
  svn_pool_clear(subpool);

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
             svn_repos_dir_delta2 */
          SVN_ERR(svn_fs_begin_txn(&txn, fs, i, subpool));
          SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));

          /* Get the editor that will be modifying our transaction. */
          SVN_ERR(dir_delta_get_editor(&editor,
                                       &edit_baton,
                                       fs,
                                       txn_root,
                                       "",
                                       subpool));

          /* Here's the kicker...do the directory delta. */
          SVN_ERR(svn_fs_revision_root(&revision_root, fs, j, subpool));
          SVN_ERR(svn_repos_dir_delta2(txn_root,
                                       "",
                                       "",
                                       revision_root,
                                       "",
                                       editor,
                                       edit_baton,
                                       NULL,
                                       NULL,
                                       TRUE,
                                       svn_depth_infinity,
                                       FALSE,
                                       FALSE,
                                       subpool));

          /* Hopefully at this point our transaction has been modified
             to look exactly like our latest revision.  We'll check
             that. */
          SVN_ERR(svn_test__validate_tree
                  (txn_root, expected_trees[j].entries,
                   expected_trees[j].num_entries, subpool));

          /* We don't really want to do anything with this
             transaction...so we'll abort it (good for software, bad
             bad bad for society). */
          svn_error_clear(svn_fs_abort_txn(txn, subpool));
          svn_pool_clear(subpool);
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
node_tree_delete_under_copy(const svn_test_opts_t *opts,
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
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-del-under-copy",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create and commit the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Now, commit again, this time after copying a directory, and then
     deleting some paths under that directory. */
  SVN_ERR(svn_fs_revision_root(&revision_root, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_copy(revision_root, "A", txn_root, "Z", pool));
  SVN_ERR(svn_fs_delete(txn_root, "Z/D/G/rho", pool));
  SVN_ERR(svn_fs_delete(txn_root, "Z/D/H", pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Now, we run the node_tree editor code, and see that a) it doesn't
     bomb out, and b) that our nodes are all good. */
  SVN_ERR(svn_fs_revision_root(&revision_2_root, fs, youngest_rev, pool));
  SVN_ERR(svn_repos_node_editor(&editor, &edit_baton, repos,
                                revision_root, revision_2_root,
                                pool, subpool));
  SVN_ERR(svn_repos_replay2(revision_2_root, "", SVN_INVALID_REVNUM, FALSE,
                            editor, edit_baton, NULL, NULL, subpool));

  /* Get the root of the generated tree, and cleanup our mess. */
  tree = svn_repos_node_from_baton(edit_baton);
  svn_pool_destroy(subpool);

  /* See that we got what we expected (fortunately, svn_repos_replay
     drivers editor paths in a predictable fashion!). */

  if (! (tree /* / */
         && tree->child /* /Z */
         && tree->child->child /* /Z/D */
         && tree->child->child->child /* /Z/D/G */
         && tree->child->child->child->child /* /Z/D/G/rho */
         && tree->child->child->child->sibling)) /* /Z/D/H */
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Generated node tree is bogus.");

  if (! ((strcmp(tree->name, "") == 0)
         && (strcmp(tree->child->name, "Z") == 0)
         && (strcmp(tree->child->child->name, "D") == 0)
         && (strcmp(tree->child->child->child->name, "G") == 0)
         && ((strcmp(tree->child->child->child->child->name, "rho") == 0)
             && (tree->child->child->child->child->kind == svn_node_file)
             && (tree->child->child->child->child->action == 'D'))
         && ((strcmp(tree->child->child->child->sibling->name, "H") == 0)
             && (tree->child->child->child->sibling->kind == svn_node_dir)
             && (tree->child->child->child->sibling->action == 'D'))))
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Generated node tree is bogus.");

  return SVN_NO_ERROR;
}


/* Helper for revisions_changed(). */
static const char *
print_chrevs(const apr_array_header_t *revs_got,
             int num_revs_expected,
             const svn_revnum_t *revs_expected,
             apr_pool_t *pool)
{
  int i;
  const char *outstr;
  svn_revnum_t rev;

  outstr = apr_psprintf(pool, "Got: { ");
  if (revs_got)
    {
      for (i = 0; i < revs_got->nelts; i++)
        {
          rev = APR_ARRAY_IDX(revs_got, i, svn_revnum_t);
          outstr = apr_pstrcat(pool,
                               outstr,
                               apr_psprintf(pool, "%ld ", rev),
                               SVN_VA_NULL);
        }
    }
  outstr = apr_pstrcat(pool, outstr, "}  Expected: { ", SVN_VA_NULL);
  for (i = 0; i < num_revs_expected; i++)
    {
      outstr = apr_pstrcat(pool,
                           outstr,
                           apr_psprintf(pool, "%ld ",
                                        revs_expected[i]),
                           SVN_VA_NULL);
    }
  return apr_pstrcat(pool, outstr, "}", SVN_VA_NULL);
}


/* Implements svn_repos_history_func_t interface.  Accumulate history
   revisions the apr_array_header_t * which is the BATON. */
static svn_error_t *
history_to_revs_array(void *baton,
                      const char *path,
                      svn_revnum_t revision,
                      apr_pool_t *pool)
{
  apr_array_header_t *revs_array = baton;
  APR_ARRAY_PUSH(revs_array, svn_revnum_t) = revision;
  return SVN_NO_ERROR;
}

struct revisions_changed_results
{
  const char *path;
  int num_revs;
  svn_revnum_t revs_changed[11];
};


static svn_error_t *
revisions_changed(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  apr_pool_t *spool = svn_pool_create(pool);
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t youngest_rev = 0;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-revisions-changed",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /*** Testing Algorithm ***

     1.  Create a greek tree in revision 1.
     2.  Make a series of new revisions, changing a file here and file
         there.
     3.  Loop over each path in each revision, verifying that we get
         the right revisions-changed array back from the filesystem.
  */

  /* Created the greek tree in revision 1. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 2 - mu, alpha, omega */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/mu", "2", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/alpha", "2", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/H/omega", "2", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 3 - iota, lambda, psi, omega */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "iota", "3", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/lambda", "3", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/H/psi", "3", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/H/omega", "3", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 4 - iota, beta, gamma, pi, rho */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "iota", "4", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/beta", "4", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/gamma", "4", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G/pi", "4", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G/rho", "4", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 5 - mu, alpha, tau, chi */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/mu", "5", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/alpha", "5", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G/tau", "5", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/H/chi", "5", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 6 - move A/D to A/Z */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D", txn_root, "A/Z", spool));
  SVN_ERR(svn_fs_delete(txn_root, "A/D", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 7 - edit A/Z/G/pi */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/Z/G/pi", "7", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 8 - move A/Z back to A/D, edit iota */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/Z", txn_root, "A/D", spool));
  SVN_ERR(svn_fs_delete(txn_root, "A/Z", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "iota", "8", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 9 - copy A/D/G to A/D/Q */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D/G", txn_root, "A/D/Q", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

  /* Revision 10 - edit A/D/Q/pi and A/D/Q/rho */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/Q/pi", "10", spool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/Q/rho", "10", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(spool);

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
        apr_array_header_t *revs = apr_array_make(spool, 10,
                                                  sizeof(svn_revnum_t));

        SVN_ERR(svn_repos_history(fs, path, history_to_revs_array, revs,
                                  0, youngest_rev, TRUE, spool));

        /* Are we at least looking at the right number of returned
           revisions? */
        if ((! revs) || (revs->nelts != num_revs))
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "Changed revisions differ from expected for '%s'\n%s",
             path, print_chrevs(revs, num_revs, revs_changed, spool));

        /* Do the revisions lists match up exactly? */
        for (i = 0; i < num_revs; i++)
          {
            svn_revnum_t rev = APR_ARRAY_IDX(revs, i, svn_revnum_t);
            if (rev != revs_changed[i])
              return svn_error_createf
                (SVN_ERR_FS_GENERAL, NULL,
                 "Changed revisions differ from expected for '%s'\n%s",
                 path, print_chrevs(revs, num_revs, revs_changed, spool));
          }

        /* Clear the per-iteration subpool. */
        svn_pool_clear(spool);
      }
  }

  /* Destroy the subpool. */
  svn_pool_destroy(spool);

  return SVN_NO_ERROR;
}



struct locations_info
{
  svn_revnum_t rev;
  const char *path;
};

/* Check that LOCATIONS contain everything in INFO and nothing more. */
static svn_error_t *
check_locations_info(apr_hash_t *locations, const struct locations_info *info)
{
  unsigned int i;
  for (i = 0; info->rev != 0; ++i, ++info)
    {
      const char *p = apr_hash_get(locations, &info->rev, sizeof(info->rev));
      if (!p)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Missing path for revision %ld", info->rev);
      if (strcmp(p, info->path) != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Pth mismatch for rev %ld", info->rev);
    }

  if (apr_hash_count(locations) > i)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Returned locations contain too many elements.");

  return SVN_NO_ERROR;
}

/* Check that all locations in INFO exist in REPOS for PATH and PEG_REVISION.
 */
static svn_error_t *
check_locations(svn_fs_t *fs, struct locations_info *info,
                const char *path, svn_revnum_t peg_revision,
                apr_pool_t *pool)
{
  apr_array_header_t *a = apr_array_make(pool, 0, sizeof(svn_revnum_t));
  apr_hash_t *h;
  struct locations_info *iter;

  for (iter = info; iter->rev != 0; ++iter)
    APR_ARRAY_PUSH(a, svn_revnum_t) = iter->rev;

  SVN_ERR(svn_repos_trace_node_locations(fs, &h, path, peg_revision, a,
                                         NULL, NULL, pool));
  SVN_ERR(check_locations_info(h, info));

  return SVN_NO_ERROR;
}

static svn_error_t *
node_locations(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t youngest_rev;

  /* Create the repository with a Greek tree. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-node-locations",
                                 opts, pool));
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Move a file. Rev 2. */
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_copy(root, "/A/mu", txn_root, "/mu.new", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  {
    struct locations_info info[] =
      {
        { 1, "/A/mu" },
        { 2, "/mu.new" },
        { 0 }
      };

    /* Test this twice, once with a leading slash, once without,
       because we know that the "without" form has caused us trouble
       in the past. */
    SVN_ERR(check_locations(fs, info, "/mu.new", 2, pool));
    SVN_ERR(check_locations(fs, info, "mu.new", 2, pool));
  }
  svn_pool_clear(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
node_locations2(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t youngest_rev = 0;

  /* Create the repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-node-locations2",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Revision 1:  Add a directory /foo  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/foo", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 2: Copy /foo to /bar, and add /bar/baz  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_copy(root, "/foo", txn_root, "/bar", subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "/bar/baz", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 3: Modify /bar/baz  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/bar/baz", "brrt", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 4: Modify /bar/baz again  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/bar/baz", "bzzz", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Now, check locations. */
  {
    struct locations_info info[] =
      {
        { 3, "/bar/baz" },
        { 2, "/bar/baz" },
        { 0 }
      };
    SVN_ERR(check_locations(fs, info, "/bar/baz", youngest_rev, pool));
  }

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
rmlocks_open_file(const char *path,
                  void *parent_baton,
                  svn_revnum_t base_revision,
                  apr_pool_t *file_pool,
                  void **file_baton)
{
  rmlocks_file_baton_t *fb = apr_palloc(file_pool, sizeof(*fb));
  rmlocks_baton_t *b = parent_baton;

  fb->main_baton = b;
  fb->path = apr_pstrdup(b->pool, path);

  *file_baton = fb;

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
rmlocks_change_prop(void *file_baton,
                    const char *name,
                    const svn_string_t *value,
                    apr_pool_t *pool)
{
  rmlocks_file_baton_t *fb = file_baton;

  if (strcmp(name, SVN_PROP_ENTRY_LOCK_TOKEN) == 0)
    {
      if (value != NULL)
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Value for lock-token property not NULL");

      /* We only want it removed once. */
      if (apr_hash_get(fb->main_baton->removed, fb->path,
                       APR_HASH_KEY_STRING) != NULL)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Lock token for '%s' already removed",
                                 fb->path);

      /* Mark as removed. */
      apr_hash_set(fb->main_baton->removed, fb->path, APR_HASH_KEY_STRING,
                   (void *)1);
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
rmlocks_open_root(void *edit_baton,
                  svn_revnum_t base_revision,
                  apr_pool_t *dir_pool,
                  void **root_baton)
{
  *root_baton = edit_baton;
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
rmlocks_open_directory(const char *path,
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
create_rmlocks_editor(svn_delta_editor_t **editor,
                      void **edit_baton,
                      apr_hash_t **removed,
                      apr_pool_t *pool)
{
  rmlocks_baton_t *baton = apr_palloc(pool, sizeof(*baton));

  /* Create the editor. */
  *editor = svn_delta_default_editor(pool);
  (*editor)->open_root = rmlocks_open_root;
  (*editor)->open_directory = rmlocks_open_directory;
  (*editor)->open_file = rmlocks_open_file;
  (*editor)->change_file_prop = rmlocks_change_prop;

  /* Initialize the baton. */
  baton->removed = apr_hash_make(pool);
  baton->pool = pool;
  *edit_baton = baton;

  *removed = baton->removed;

  return SVN_NO_ERROR;
}

/* Check that HASH contains exactly the const char * entries for all entries
   in the NULL-terminated array SPEC. */
static svn_error_t *
rmlocks_check(const char **spec, apr_hash_t *hash)
{
  apr_size_t n = 0;

  for (; *spec; ++spec, ++n)
    {
      if (! apr_hash_get(hash, *spec, APR_HASH_KEY_STRING))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "Lock token for '%s' should have been removed", *spec);
    }

  if (n < apr_hash_count(hash))
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Lock token for one or more paths unexpectedly "
                            "removed");
  return SVN_NO_ERROR;
}

/* Test that defunct locks are removed by the reporter. */
static svn_error_t *
rmlocks(const svn_test_opts_t *opts,
        apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_revnum_t youngest_rev;
  svn_delta_editor_t *editor;
  void *edit_baton, *report_baton;
  svn_lock_t *l1, *l2, *l3, *l4;
  svn_fs_access_t *fs_access;
  apr_hash_t *removed;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-rmlocks",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  SVN_ERR(svn_fs_create_access(&fs_access, "user1", pool));
  SVN_ERR(svn_fs_set_access(fs, fs_access));

  /* Lock some files, break a lock, steal another and check that those get
     removed. */
  {
    const char *expected [] = { "A/mu", "A/D/gamma", NULL };

    SVN_ERR(svn_fs_lock(&l1, fs, "/iota", NULL, NULL, 0, 0, youngest_rev,
                        FALSE, subpool));
    SVN_ERR(svn_fs_lock(&l2, fs, "/A/mu", NULL, NULL, 0, 0, youngest_rev,
                        FALSE, subpool));
    SVN_ERR(svn_fs_lock(&l3, fs, "/A/D/gamma", NULL, NULL, 0, 0, youngest_rev,
                        FALSE, subpool));

    /* Break l2. */
    SVN_ERR(svn_fs_unlock(fs, "/A/mu", NULL, TRUE, subpool));

    /* Steal l3 from ourselves. */
    SVN_ERR(svn_fs_lock(&l4, fs, "/A/D/gamma", NULL, NULL, 0, 0, youngest_rev,
                        TRUE, subpool));

    /* Create the editor. */
    SVN_ERR(create_rmlocks_editor(&editor, &edit_baton, &removed, subpool));

    /* Report what we have. */
    SVN_ERR(svn_repos_begin_report3(&report_baton, 1, repos, "/", "", NULL,
                                    FALSE, svn_depth_infinity, FALSE, FALSE,
                                    editor, edit_baton, NULL, NULL, 1024,
                                    subpool));
    SVN_ERR(svn_repos_set_path3(report_baton, "", 1,
                                svn_depth_infinity,
                                FALSE, NULL, subpool));
    SVN_ERR(svn_repos_set_path3(report_baton, "iota", 1,
                                svn_depth_infinity,
                                FALSE, l1->token, subpool));
    SVN_ERR(svn_repos_set_path3(report_baton, "A/mu", 1,
                                svn_depth_infinity,
                                FALSE, l2->token, subpool));
    SVN_ERR(svn_repos_set_path3(report_baton, "A/D/gamma", 1,
                                svn_depth_infinity,
                                FALSE, l3->token, subpool));

    /* End the report. */
    SVN_ERR(svn_repos_finish_report(report_baton, pool));

    /* And check that the edit did what we wanted. */
    SVN_ERR(rmlocks_check(expected, removed));
  }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* Helper for the authz test.  Set *AUTHZ_P to a representation of
   AUTHZ_CONTENTS, using POOL for temporary allocation. If DISK
   is TRUE then write the contents to a temp file and use
   svn_repos_authz_read() to get the data if FALSE write the
   data to a buffered stream and use svn_repos_authz_parse(). */
static svn_error_t *
authz_get_handle(svn_authz_t **authz_p, const char *authz_contents,
                 svn_boolean_t disk, apr_pool_t *pool)
{
  if (disk)
    {
      const char *authz_file_path;

      /* Create a temporary file. */
      SVN_ERR_W(svn_io_write_unique(&authz_file_path, NULL,
                                    authz_contents, strlen(authz_contents),
                                    svn_io_file_del_on_pool_cleanup, pool),
                "Writing temporary authz file");

      /* Read the authz configuration back and start testing. */
      SVN_ERR_W(svn_repos_authz_read(authz_p, authz_file_path, TRUE, pool),
                "Opening test authz file");

      /* Done with the file. */
      SVN_ERR_W(svn_io_remove_file(authz_file_path, pool),
                "Removing test authz file");
    }
  else
    {
      svn_stream_t *stream;

      stream = svn_stream_buffered(pool);
      SVN_ERR_W(svn_stream_puts(stream, authz_contents),
                "Writing authz contents to stream");

      SVN_ERR_W(svn_repos_authz_parse(authz_p, stream, NULL, pool),
                "Parsing the authz contents");

      SVN_ERR_W(svn_stream_close(stream),
                "Closing the stream");
    }

  return SVN_NO_ERROR;
}

struct check_access_tests {
  const char *path;
  const char *repo_name;
  const char *user;
  const svn_repos_authz_access_t required;
  const svn_boolean_t expected;
};

/* Helper for the authz test.  Runs a set of tests against AUTHZ_CFG
 * as defined in TESTS. */
static svn_error_t *
authz_check_access(svn_authz_t *authz_cfg,
                   const struct check_access_tests *tests,
                   apr_pool_t *pool)
{
  int i;
  svn_boolean_t access_granted;

  /* Loop over the test array and test each case. */
  for (i = 0; !(tests[i].path == NULL
               && tests[i].required == svn_authz_none); i++)
    {
      SVN_ERR(svn_repos_authz_check_access(authz_cfg,
                                           tests[i].repo_name,
                                           tests[i].path,
                                           tests[i].user,
                                           tests[i].required,
                                           &access_granted, pool));

      if (access_granted != tests[i].expected)
        {
          return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "Authz incorrectly %s %s%s access "
                                   "to %s%s%s for user %s",
                                   access_granted ?
                                   "grants" : "denies",
                                   tests[i].required
                                   & svn_authz_recursive ?
                                   "recursive " : "",
                                   tests[i].required
                                   & svn_authz_read ?
                                   "read" : "write",
                                   tests[i].repo_name ?
                                   tests[i].repo_name : "",
                                   tests[i].repo_name ?
                                   ":" : "",
                                   tests[i].path,
                                   tests[i].user ?
                                   tests[i].user : "-");
        }
    }

  return SVN_NO_ERROR;
}


/* Test that authz is giving out the right authorizations. */
static svn_error_t *
authz(apr_pool_t *pool)
{
  const char *contents;
  svn_authz_t *authz_cfg;
  svn_error_t *err;
  svn_boolean_t access_granted;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Definition of the paths to test and expected replies for each. */
  struct check_access_tests test_set[] = {
    /* Test that read rules are correctly used. */
    { "/A", "greek", NULL, svn_authz_read, TRUE },
    { "/iota", "greek", NULL, svn_authz_read, FALSE },
    /* Test that write rules are correctly used. */
    { "/A", "greek", "plato", svn_authz_write, TRUE },
    { "/A", "greek", NULL, svn_authz_write, FALSE },
    /* Test that pan-repository rules are found and used. */
    { "/A/B/lambda", "greek", "plato", svn_authz_read, TRUE },
    { "/A/B/lambda", "greek", NULL, svn_authz_read, FALSE },
    /* Test that authz uses parent path ACLs if no rule for the path
       exists. */
    { "/A/C", "greek", NULL, svn_authz_read, TRUE },
    /* Test that recursive access requests take into account the rules
       of subpaths. */
    { "/A/D", "greek", "plato", svn_authz_read | svn_authz_recursive, TRUE },
    { "/A/D", "greek", NULL, svn_authz_read | svn_authz_recursive, FALSE },
    /* Test global write access lookups. */
    { NULL, "greek", "plato", svn_authz_read, TRUE },
    { NULL, "greek", NULL, svn_authz_write, FALSE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* The test logic:
   *
   * 1. Perform various access tests on a set of authz rules.  Each
   * test has a known outcome and tests different aspects of authz,
   * such as inheriting parent-path authz, pan-repository rules or
   * recursive access.  'plato' is our friendly neighborhood user with
   * more access rights than other anonymous philosophers.
   *
   * 2. Load an authz file containing a cyclic dependency in groups
   * and another containing a reference to an undefined group.  Verify
   * that svn_repos_authz_read fails to load both and returns an
   * "invalid configuration" error.
   *
   * 3. Regression test for a bug in how recursion is handled in
   * authz.  The bug was that paths not under the parent path
   * requested were being considered during the determination of
   * access rights (eg. a rule for /dir2 matched during a lookup for
   * /dir), due to incomplete tests on path relations.
   */

  /* The authz rules for the phase 1 tests. */
  contents =
    "[greek:/A]"                                                             NL
    "* = r"                                                                  NL
    "plato = rw"                                                             NL
    ""                                                                       NL
    "[greek:/iota]"                                                          NL
    "* ="                                                                    NL
    ""                                                                       NL
    "[/A/B/lambda]"                                                          NL
    "plato = r"                                                              NL
    "* ="                                                                    NL
    ""                                                                       NL
    "[greek:/A/D]"                                                           NL
    "plato = r"                                                              NL
    "* = r"                                                                  NL
    ""                                                                       NL
    "[greek:/A/D/G]"                                                         NL
    "plato = r"                                                              NL
    "* ="                                                                    NL
    ""                                                                       NL
    "[greek:/A/B/E/beta]"                                                    NL
    "* ="                                                                    NL
    ""                                                                       NL
    "[/nowhere]"                                                             NL
    "nobody = r"                                                             NL
    ""                                                                       NL;

  /* Load the test authz rules. */
  SVN_ERR(authz_get_handle(&authz_cfg, contents, FALSE, subpool));

  /* Loop over the test array and test each case. */
  SVN_ERR(authz_check_access(authz_cfg, test_set, subpool));

  /* Repeat the previous test on disk */
  SVN_ERR(authz_get_handle(&authz_cfg, contents, TRUE, subpool));
  SVN_ERR(authz_check_access(authz_cfg, test_set, subpool));

  /* The authz rules for the phase 2 tests, first case (cyclic
     dependency). */
  contents =
    "[groups]"                                                               NL
    "slaves = cooks,scribes,@gladiators"                                     NL
    "gladiators = equites,thraces,@slaves"                                   NL
    ""                                                                       NL
    "[greek:/A]"                                                             NL
    "@slaves = r"                                                            NL;

  /* Load the test authz rules and check that group cycles are
     reported. */
  err = authz_get_handle(&authz_cfg, contents, FALSE, subpool);
  if (!err || err->apr_err != SVN_ERR_AUTHZ_INVALID_CONFIG)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_AUTHZ_INVALID_CONFIG",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  /* The authz rules for the phase 2 tests, second case (missing group
     definition). */
  contents =
    "[greek:/A]"                                                             NL
    "@senate = r"                                                            NL;

  /* Check that references to undefined groups are reported. */
  err = authz_get_handle(&authz_cfg, contents, FALSE, subpool);
  if (!err || err->apr_err != SVN_ERR_AUTHZ_INVALID_CONFIG)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_AUTHZ_INVALID_CONFIG",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  /* The authz rules for the phase 3 tests */
  contents =
    "[/]"                                                                    NL
    "* = rw"                                                                 NL
    ""                                                                       NL
    "[greek:/dir2/secret]"                                                   NL
    "* ="                                                                    NL;

  /* Load the test authz rules. */
  SVN_ERR(authz_get_handle(&authz_cfg, contents, FALSE, subpool));

  /* Verify that the rule on /dir2/secret doesn't affect this
     request */
  SVN_ERR(svn_repos_authz_check_access(authz_cfg, "greek",
                                       "/dir", NULL,
                                       (svn_authz_read
                                        | svn_authz_recursive),
                                       &access_granted, subpool));
  if (!access_granted)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Regression: incomplete ancestry test "
                            "for recursive access lookup.");

  /* The authz rules for the phase 4 tests */
  contents =
    "[greek:/dir2//secret]"                                                  NL
    "* ="                                                                    NL;
  SVN_TEST_ASSERT_ERROR(authz_get_handle(&authz_cfg, contents, FALSE, subpool),
                        SVN_ERR_AUTHZ_INVALID_CONFIG);

  /* Verify that the rule on /dir2/secret doesn't affect this
     request */
  SVN_ERR(svn_repos_authz_check_access(authz_cfg, "greek",
                                       "/dir", NULL,
                                       (svn_authz_read
                                        | svn_authz_recursive),
                                       &access_granted, subpool));
  if (!access_granted)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Regression: incomplete ancestry test "
                            "for recursive access lookup.");

  /* That's a wrap! */
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Test the supported authz wildcard variants. */
static svn_error_t *
test_authz_wildcards(apr_pool_t *pool)
{
  svn_authz_t *authz_cfg;

  /* Some non-trivially overlapping wildcard rules, convering all types
   * of wildcards: "any", "any-var", "prefix", "postfix" and "complex".
   *
   * Note that the rules are not in 1:1 correspondence to that enumeration.
   */
  const char *contents =
    "[:glob:/**/G]"                                                          NL
    "* = r"                                                                  NL
    ""                                                                       NL
    "[:glob:/A/*/G]"                                                         NL
    "* ="                                                                    NL
    ""                                                                       NL
    "[:glob:/A/**/*a*]"                                                      NL
    "* = r"                                                                  NL
    ""                                                                       NL
    "[:glob:/**/*a]"                                                         NL
    "* = rw"                                                                 NL
    ""                                                                       NL
    "[:glob:/A/**/g*]"                                                       NL
    "* ="                                                                    NL
    ""                                                                       NL
    "[:glob:/**/lambda]"                                                     NL
    "* = rw"                                                                 NL;

  /* Definition of the paths to test and expected replies for each. */
  struct check_access_tests test_set[] = {
    /* Test that read rules are correctly used. */
    { "/", NULL, NULL, svn_authz_read, FALSE },              /* default */
    { "/iota", NULL, NULL, svn_authz_write, TRUE },          /* rule 4 */
    { "/A", NULL, NULL, svn_authz_read, FALSE },             /* inherited */
    { "/A/mu", NULL, NULL, svn_authz_read, FALSE },          /* inherited */
    { "/A/B", NULL, NULL, svn_authz_read, FALSE },           /* inherited */
    { "/A/B/lambda", NULL, NULL, svn_authz_write, TRUE },    /* rule 6 */
    { "/A/B/E", NULL, NULL, svn_authz_read, FALSE },         /* inherited */
    { "/A/B/E/alpha", NULL, NULL, svn_authz_write, TRUE },   /* rule 4 */
    { "/A/B/E/beta", NULL, NULL, svn_authz_write, TRUE },    /* rule 4 */
    { "/A/B/F", NULL, NULL, svn_authz_read, FALSE },         /* inherited */
    { "/A/C", NULL, NULL, svn_authz_read, FALSE },           /* inherited */
    { "/A/D", NULL, NULL, svn_authz_read, FALSE },           /* inherited */
    { "/A/D/gamma", NULL, NULL, svn_authz_read, FALSE },     /* rule 5 */
    { "/A/D/G", NULL, NULL, svn_authz_read, FALSE },         /* rule 2 */
    { "/A/D/G/pi", NULL, NULL, svn_authz_read, FALSE },      /* inherited */
    { "/A/D/G/rho", NULL, NULL, svn_authz_read, FALSE },     /* inherited */
    { "/A/D/G/tau", NULL, NULL, svn_authz_read, TRUE },      /* rule 3 */
    { "/A/D/G/tau", NULL, NULL, svn_authz_write, FALSE },    /* rule 3 */
    { "/A/D/H", NULL, NULL, svn_authz_read, FALSE },         /* inherited */
    { "/A/D/H/chi", NULL, NULL, svn_authz_read, FALSE },     /* inherited */
    { "/A/D/H/psi", NULL, NULL, svn_authz_read, FALSE },     /* inherited */
    { "/A/D/H/omega", NULL, NULL, svn_authz_write, TRUE },   /* rule 4 */
    /* Non-greek tree paths: */
    { "/A/G", NULL, NULL, svn_authz_read, TRUE },            /* rule 1 */
    { "/A/G", NULL, NULL, svn_authz_write, FALSE },          /* rule 1 */
    { "/A/G/G", NULL, NULL, svn_authz_read, FALSE },         /* rule 2 */
    { "/G", NULL, NULL, svn_authz_read, TRUE },              /* rule 1 */
    { "/G", NULL, NULL, svn_authz_write, FALSE },            /* rule 1 */
    { "/Y/G", NULL, NULL, svn_authz_read, TRUE },            /* rule 1 */
    { "/Y/G", NULL, NULL, svn_authz_write, FALSE },          /* rule 1 */
    { "/X/Z/G", NULL, NULL, svn_authz_read, TRUE },          /* rule 1 */
    { "/X/Z/G", NULL, NULL, svn_authz_write, FALSE },        /* rule 1 */
    /* Rule 5 prevents recursive access anywhere below /A. */
    { "/", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/iota", NULL, NULL, svn_authz_read | svn_authz_recursive, TRUE },
    { "/iota", NULL, NULL, svn_authz_write | svn_authz_recursive, FALSE },
    { "/A", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/mu", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/B", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/B/lambda", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/B/E", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/B/E/alpha", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/B/E/beta", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/B/F", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/C", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/gamma", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/G", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/G/pi", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/G/rho", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/G/tau", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/H", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/H/chi", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/H/psi", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    { "/A/D/H/omega", NULL, NULL, svn_authz_read | svn_authz_recursive, FALSE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* Load the test authz rules. */
  SVN_ERR(authz_get_handle(&authz_cfg, contents, FALSE, pool));

  /* Loop over the test array and test each case. */
  SVN_ERR(authz_check_access(authz_cfg, test_set, pool));

  return SVN_NO_ERROR;
}

/* Test the authz performance with wildcard rules. */
static svn_error_t *
test_authz_wildcard_performance(apr_pool_t *pool)
{
  svn_authz_t *authz_cfg;
  svn_boolean_t access_granted;
  int i, k;
  apr_time_t start, end;

  /* Some non-trivially overlapping wildcard rules, convering all types
   * of wildcards: "any", "any-var", "prefix", "postfix" and "complex".
   */
  const char *contents =
    "[:glob:greek:/A/*/G]"                                                   NL
    "* ="                                                                    NL
    ""                                                                       NL
    "[:glob:greek:/A/**/*a*]"                                                NL
    "* = r"                                                                  NL
    ""                                                                       NL
    "[:glob:greek:/**/*a]"                                                   NL
    "* = rw"                                                                 NL
    ""                                                                       NL
    "[:glob:greek:/A/**/g*]"                                                 NL
    "* ="                                                                    NL
    ""                                                                       NL
    "[:glob:greek:/**/lambda]"                                               NL
    "* = rw"                                                                 NL;

  /* Load the test authz rules. */
  SVN_ERR(authz_get_handle(&authz_cfg, contents, FALSE, pool));

  start = apr_time_now();
  for (k = 0; k < 100000; ++k)
    for (i = 1; i < 4; ++i)
      {
        const char **path;
        const char *paths[] =
        { "/iota",
          "/A",
          "/A/mu",
          "/A/B",
          "/A/B/lambda",
          "/A/B/E",
          "/A/B/E/alpha",
          "/A/B/E/beta",
          "/A/B/F",
          "/A/C",
          "/A/D",
          "/A/D/gamma",
          "/A/D/G",
          "/A/D/G/pi",
          "/A/D/G/rho",
          "/A/D/G/tau",
          "/A/D/H",
          "/A/D/H/chi",
          "/A/D/H/psi",
          "/A/D/H/omega",
          NULL
        };

        for (path = paths; *path; ++path)
          SVN_ERR(svn_repos_authz_check_access(authz_cfg, "greek",
                                               *path, NULL, i,
                                               &access_granted, pool));
      }

  end = apr_time_now();
  printf("%"APR_TIME_T_FMT" musecs\n", end - start);
  printf("%"APR_TIME_T_FMT" checks / sec\n",
           (k * (i - 1) * 20 * 1000000l) / (end - start));

  return SVN_NO_ERROR;
}

/* Test that the latest definition wins, regardless of whether the ":glob:"
 * prefix has been given. */
static svn_error_t *
test_authz_prefixes(apr_pool_t *pool)
{
  svn_authz_t *authz_cfg;
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i, combi;

  /* Set all rights at some folder and replace them again.  Make sure to
   * cover the "/" b/c that already has an implicit rule, so we* overwrite
   * it twice.  The first 2 string placeholders in the rules are for the
   * repository name and the optional glob support marker. */
  const char *contents_format =
    "[%s%s%s]"                                                              NL
    "* = r"                                                                 NL
    "plato = rw"                                                            NL
    ""                                                                      NL
    "[%s%s%s]"                                                              NL
    "* ="                                                                   NL
    "plato = r"                                                             NL;

  /* The paths on which to apply this test. */
  enum { PATH_COUNT = 2 };
  const char *test_paths[PATH_COUNT] = { "/", "/A" };

  /* Definition of the paths to test and expected replies for each. */
  struct check_access_tests test_set1[] = {
    /* Test that read rules are correctly used. */
    { "", "greek", NULL, svn_authz_read, FALSE },
    /* Test that write rules are correctly used. */
    { "", "greek", "plato", svn_authz_read, TRUE },
    { "", "greek", "plato", svn_authz_write, FALSE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* To be used when global rules are specified after per-repos rules.
   * In that case, the global rules still win. */
  struct check_access_tests test_set2[] = {
    /* Test that read rules are correctly used. */
    { "", "greek", NULL, svn_authz_read, TRUE },
    { "", "greek", NULL, svn_authz_write, FALSE },
    /* Test that write rules are correctly used. */
    { "", "greek", "plato", svn_authz_read, TRUE },
    { "", "greek", "plato", svn_authz_write, TRUE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* There is a total of 16 combinations of authz content. */
  for (combi = 0; combi < 16; ++combi)
    {
      const char *contents;
      const char *glob1 = (combi & 1) ? ":glob:" : "";
      const char *glob2 = (combi & 2) ? ":glob:" : "";
      const char *repo1 = (combi & 4) ? "greek:" : "";
      const char *repo2 = (combi & 4) ? "" : "greek:";
      const char *test_path = test_paths[combi / 8];
      struct check_access_tests *test_set = (combi & 4) ? test_set2 : test_set1;

      /* Create and parse the authz rules. */
      svn_pool_clear(iterpool);
      contents = apr_psprintf(iterpool, contents_format,
                              glob1, repo1, test_path,
                              glob2, repo2, test_path);
      SVN_ERR(authz_get_handle(&authz_cfg, contents, FALSE, iterpool));

      /* iterate over all test paths */
      for (i = combi / 8; i < PATH_COUNT; ++i)
        {
          /* Set the path for all test cases to the current test path. */
          struct check_access_tests *test;
          for (test = test_set; test->path != NULL; ++test)
            test->path = test_paths[i];

          /* Loop over the test array and test each case. */
          SVN_ERR(authz_check_access(authz_cfg, test_set, iterpool));
        }
    }

  /* That's a wrap! */
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_authz_recursive_override(apr_pool_t *pool)
{
  svn_authz_t *authz_cfg;

  /* Set all rights at some folder and replace them again.  Make sure to
   * cover the "/" b/c that already has an implicit rule, so we* overwrite
   * it twice.  The first 2 string placeholders in the rules are for the
   * repository name and the optional glob support marker. */
  const char *contents =
    "[:glob:/A/B]"                                                          NL
    "plato = rw"                                                            NL
    ""                                                                      NL
    "[:glob:/A/**]"                                                         NL
    "plato = r"                                                             NL
    ""                                                                      NL
    "[:glob:/B/C]"                                                          NL
    "plato ="                                                               NL
    ""                                                                      NL
    "[:glob:/B/**]"                                                         NL
    "plato = rw"                                                            NL
    ""                                                                      NL
    "[:glob:/C/D]"                                                          NL
    "plato = rw"                                                            NL
    ""                                                                      NL
    "[:glob:/C/**/E]"                                                       NL
    "plato = r"                                                             NL
    ""                                                                      NL
    "[:glob:/D/E]"                                                          NL
    "plato = r"                                                             NL
    ""                                                                      NL
    "[:glob:/D/**/F]"                                                       NL
    "plato = rw"                                                            NL;

  /* Definition of the paths to test and expected replies for each. */
  struct check_access_tests test_set[] = {
    /* The root shall not be affected -> defaults to "no access". */
    { "/", NULL, "plato", svn_authz_read, FALSE },
    /* Recursive restriction of rights shall work. */
    { "/A", NULL, "plato", svn_authz_read | svn_authz_recursive, TRUE },
    { "/A", NULL, "plato", svn_authz_write | svn_authz_recursive, FALSE },
    /* Recursive extension of rights shall work. */
    { "/B", NULL, "plato", svn_authz_read | svn_authz_recursive, TRUE },
    { "/B", NULL, "plato", svn_authz_write | svn_authz_recursive, TRUE },
    /* Partial replacements shall not result in recursive rights. */
    { "/C", NULL, "plato", svn_authz_read | svn_authz_recursive, FALSE },
    { "/C/D", NULL, "plato", svn_authz_read | svn_authz_recursive, TRUE },
    { "/C/D", NULL, "plato", svn_authz_write | svn_authz_recursive, FALSE },
    { "/D", NULL, "plato", svn_authz_read | svn_authz_recursive, FALSE },
    { "/D/E", NULL, "plato", svn_authz_read | svn_authz_recursive, TRUE },
    { "/D/E", NULL, "plato", svn_authz_write | svn_authz_recursive, FALSE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  SVN_ERR(authz_get_handle(&authz_cfg, contents, FALSE, pool));

  /* Loop over the test array and test each case. */
  SVN_ERR(authz_check_access(authz_cfg, test_set, pool));

  /* That's a wrap! */
  return SVN_NO_ERROR;
}

static svn_error_t *
test_authz_pattern_tests(apr_pool_t *pool)
{
  svn_authz_t *authz_cfg;

  /* Rules will be considered for recursive access checks irrespective of
   * whether the respective paths actually do exist. */
  const char *contents =
    "[:glob:/**/Yeti]"                                                      NL
    "plato = r"                                                             NL
    ""                                                                      NL
    "[/]"                                                                   NL
    "plato = r"                                                             NL
    ""                                                                      NL
    "[/trunk]"                                                              NL
    "plato = rw"                                                            NL;

  /* Definition of the paths to test and expected replies for each. */
  struct check_access_tests test_set[] = {
    /* We have no recursive write access anywhere. */
    { "/", NULL, "plato", svn_authz_read | svn_authz_recursive, TRUE },
    { "/", NULL, "plato", svn_authz_write | svn_authz_recursive, FALSE },
    { "/trunk", NULL, "plato", svn_authz_read | svn_authz_recursive, TRUE },
    { "/trunk", NULL, "plato", svn_authz_write | svn_authz_recursive, FALSE },

    /* We do have ordinary write access to anything under /trunk that is
     * not a Yeti. */
    { "/trunk", NULL, "plato", svn_authz_write, TRUE },
    { "/trunk/A/B/C", NULL, "plato", svn_authz_write, TRUE },

    /* We don't have write access to Yetis. */
    { "/trunk/A/B/C/Yeti", NULL, "plato", svn_authz_write, FALSE },
    { "/trunk/Yeti", NULL, "plato", svn_authz_write, FALSE },
    { "/Yeti", NULL, "plato", svn_authz_write, FALSE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* Global override via "**" and selective override for a specific path. */
  const char *contents2 =
    "[:glob:/X]"                                                            NL
    "user1 ="                                                               NL
    ""                                                                      NL
    "[:glob:/X/**]"                                                         NL
    "user1 = rw"                                                            NL
    "user2 = rw"                                                            NL
    ""                                                                      NL
    "[:glob:/X/Y/Z]"                                                        NL
    "user2 ="                                                               NL;

  /* Definition of the paths to test and expected replies for each. */
  struct check_access_tests test_set2[] = {
    /* No access at the root*/
    { "/", NULL, "user1", svn_authz_read, FALSE },
    { "/", NULL, "user2", svn_authz_read, FALSE },

    /* User 1 has recursive write access anywhere. */
    { "/X", NULL, "user1", svn_authz_write | svn_authz_recursive, TRUE },
    { "/X/Y", NULL, "user1", svn_authz_read | svn_authz_recursive, TRUE },
    { "/X/Y/Z", NULL, "user1", svn_authz_read | svn_authz_recursive, TRUE },

    /* User 2 only has recursive read access to X/Y/Z. */
    { "/X", NULL, "user1", svn_authz_read | svn_authz_recursive, TRUE },
    { "/X", NULL, "user2", svn_authz_write | svn_authz_recursive, FALSE },
    { "/X/Y", NULL, "user2", svn_authz_write | svn_authz_recursive, FALSE },
    { "/X/Y/Z", NULL, "user2", svn_authz_write | svn_authz_recursive, FALSE },

    /* However, user2 has ordinary write access X and recursive write access
     * to anything not in X/Y/Z. */
    { "/X", NULL, "user2", svn_authz_write, TRUE },
    { "/X/A", NULL, "user2", svn_authz_write | svn_authz_recursive, TRUE },
    { "/X/Y/A", NULL, "user2", svn_authz_write | svn_authz_recursive, TRUE },

    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* Global patterns vs. global path rules. */
  const char *contents3 =
    "[groups]"                                                              NL
    "Team1 = user1"                                                         NL
    "Team2 = user1, user2"                                                  NL
    ""                                                                      NL
    "[/]"                                                                   NL
    "* ="                                                                   NL
    ""                                                                      NL
    "[:glob:Repo1:/**/folder*]"                                             NL
    "@Team1 = rw"                                                           NL
    ""                                                                      NL
    "[Repo2:/]"                                                             NL
    "@Team2 = r"                                                            NL;

  /* Definition of the paths to test and expected replies for each. */
  struct check_access_tests test_set3[] = {
    /* No access at the root of Repo1 (inherited from global settings) */
    { "/", "Repo1", "user1", svn_authz_read, FALSE },
    { "/", "Repo1", "user2", svn_authz_read, FALSE },

    /* r/o access for both users at the root of Repo2 */
    { "/", "Repo2", "user1", svn_authz_read, TRUE },
    { "/", "Repo2", "user2", svn_authz_read, TRUE },
    { "/", "Repo2", "user1", svn_authz_write, FALSE },
    { "/", "Repo2", "user2", svn_authz_write, FALSE },

    /* user1 has recursive write access (b/c there are no further rules
     * restricting the access once granted at the parent) wherever there is
     * a "folder..." in the  path, while user2 has no access at all. */
    { "/folder_1", "Repo1", "user1",
      svn_authz_write | svn_authz_recursive, TRUE },
    { "/folder_1", "Repo1", "user2", svn_authz_read, FALSE },
    { "/1_folder", "Repo1", "user1", svn_authz_read, FALSE },
    { "/foo/bar/folder_2/random", "Repo1", "user1",
      svn_authz_write | svn_authz_recursive, TRUE },
    { "/foo/bar/folder_2/random", "Repo1", "user2", svn_authz_read, FALSE },
    { "/foo/bar/2_folder/random", "Repo1", "user1", svn_authz_read, FALSE },
    { "/foo/bar/folder", "Repo1", "user1",
      svn_authz_write | svn_authz_recursive, TRUE },
    { "/foo/bar/folder", "Repo1", "user2", svn_authz_read, FALSE },

    /* Doesn't quite match the pattern: */
    { "/foo/bar/folde", "Repo1", "user1", svn_authz_read, FALSE },
    { "/foo/bar/folde", "Repo1", "user2", svn_authz_read, FALSE },

    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* Illustrate the difference between "matching" rule and "applying" rule.
   * "*" only _matches_ a single level and will _apply_ to sub-paths only
   * if no other rule _applies_.  The "**" rule applies to all paths in
   * trunk and will only be eclipsed for members of team1 and then only for
   * the first sub-level. */
  const char *contents4 =
    "[groups]"                                                              NL
    "team1 = user1, user3"                                                  NL
    "team2 = user2, user3"                                                  NL
    ""                                                                      NL
    "[:glob:Repo1:/trunk/**]"                                               NL
    "@team2 = rw"                                                           NL
    ""                                                                      NL
    "[:glob:Repo1:/trunk/*]"                                                NL
    "@team1 = r"                                                            NL;

  /* Definition of the paths to test and expected replies for each. */
  struct check_access_tests test_set4[] = {
    /* Team2 has r/w access to /trunk */
    { "/trunk", "Repo1", "user1", svn_authz_read, FALSE },
    { "/trunk", "Repo1", "user2", svn_authz_write, TRUE },
    { "/trunk", "Repo1", "user3", svn_authz_write, TRUE },

    /* At the first sub-level, team1 has only read access;
     * the remainder of team2 has write access. */
    { "/trunk/A", "Repo1", "user1", svn_authz_read, TRUE },
    { "/trunk/A", "Repo1", "user3", svn_authz_read, TRUE },
    { "/trunk/A", "Repo1", "user1", svn_authz_write, FALSE },
    { "/trunk/A", "Repo1", "user2", svn_authz_write, TRUE },
    { "/trunk/A", "Repo1", "user3", svn_authz_write, FALSE },

    /* At the second sub-level, team2 has full write access;
     * the remainder of team1 has still r/o access. */
    { "/trunk/A/B", "Repo1", "user2",
      svn_authz_write | svn_authz_recursive, TRUE },
    { "/trunk/A/B", "Repo1", "user3",
      svn_authz_write | svn_authz_recursive, TRUE },
    { "/trunk/A/B", "Repo1", "user1", svn_authz_read, TRUE },
    { "/trunk/A/B", "Repo1", "user1", svn_authz_write, FALSE },

    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* Verify that the rules are applies as expected. */
  SVN_ERR(authz_get_handle(&authz_cfg, contents, FALSE, pool));
  SVN_ERR(authz_check_access(authz_cfg, test_set, pool));

  SVN_ERR(authz_get_handle(&authz_cfg, contents2, FALSE, pool));
  SVN_ERR(authz_check_access(authz_cfg, test_set2, pool));

  SVN_ERR(authz_get_handle(&authz_cfg, contents3, FALSE, pool));
  SVN_ERR(authz_check_access(authz_cfg, test_set3, pool));

  SVN_ERR(authz_get_handle(&authz_cfg, contents4, FALSE, pool));
  SVN_ERR(authz_check_access(authz_cfg, test_set4, pool));

  /* That's a wrap! */
  return SVN_NO_ERROR;
}


/* Test in-repo authz paths */
static svn_error_t *
in_repo_authz(const svn_test_opts_t *opts,
                                 apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t youngest_rev;
  svn_authz_t *authz_cfg;
  const char *authz_contents;
  const char *repos_root;
  const char *repos_url;
  const char *authz_url;
  const char *noent_authz_url;
  svn_error_t *err;
  struct check_access_tests test_set[] = {
    /* reads */
    { "/A", NULL, NULL, svn_authz_read, FALSE },
    { "/A", NULL, "plato", svn_authz_read, TRUE },
    { "/A", NULL, "socrates", svn_authz_read, TRUE },
    /* writes */
    { "/A", NULL, NULL, svn_authz_write, FALSE },
    { "/A", NULL, "socrates", svn_authz_write, FALSE },
    { "/A", NULL, "plato", svn_authz_write, TRUE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* Test plan:
   * Create an authz file and put it in the repository.
   * Verify it can be read with an relative URL.
   * Verify it can be read with an absolute URL.
   * Verify non-existent path does not error out when must_exist is FALSE.
   * Verify non-existent path does error out when must_exist is TRUE.
   * Verify that an http:// URL produces an error.
   * Verify that an svn:// URL produces an error.
   */

  /* What we'll put in the authz file, it's simple since we're not testing
   * the parsing, just that we got what we expected. */
  authz_contents =
    ""                                                                       NL
    ""                                                                       NL
    "[/]"                                                                    NL
    "plato = rw"                                                             NL
    "socrates = r";

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-in-repo-authz",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Commit the authz file to the repo. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "authz", pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "authz", authz_contents,
                                      pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  repos_root = svn_repos_path(repos, pool);
  SVN_ERR(svn_uri_get_file_url_from_dirent(&repos_url, repos_root, pool));
  authz_url = svn_path_url_add_component2(repos_url, "authz", pool);
  noent_authz_url = svn_path_url_add_component2(repos_url, "A/authz", pool);

  /* absolute file URL. */
  SVN_ERR(svn_repos_authz_read2(&authz_cfg, authz_url, NULL, TRUE, pool));
  SVN_ERR(authz_check_access(authz_cfg, test_set, pool));

  /* Non-existent path in the repo with must_exist set to FALSE */
  SVN_ERR(svn_repos_authz_read2(&authz_cfg, noent_authz_url, NULL,
                                FALSE, pool));

  /* Non-existent path in the repo with must_exist set to TRUE */
  err = svn_repos_authz_read2(&authz_cfg, noent_authz_url, NULL, TRUE, pool);
  if (!err || err->apr_err != SVN_ERR_ILLEGAL_TARGET)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_ILLEGAL_TARGET",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  /* http:// URL which is unsupported */
  err = svn_repos_authz_read2(&authz_cfg, "http://example.com/repo/authz",
                              NULL, TRUE, pool);
  if (!err || err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_RA_ILLEGAL_URL",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  /* svn:// URL which is unsupported */
  err = svn_repos_authz_read2(&authz_cfg, "svn://example.com/repo/authz",
                              NULL, TRUE, pool);
  if (!err || err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_RA_ILLEGAL_URL",
                             err ? "unexpected" : "no");
  svn_error_clear(err);


  return SVN_NO_ERROR;
}


/* Test in-repo authz with global groups. */
static svn_error_t *
in_repo_groups_authz(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t youngest_rev;
  svn_authz_t *authz_cfg;
  const char *groups_contents;
  const char *authz_contents;
  const char *repos_root;
  const char *repos_url;
  const char *groups_url;
  const char *noent_groups_url;
  const char *authz_url;
  const char *empty_authz_url;
  const char *noent_authz_url;
  svn_error_t *err;
  struct check_access_tests test_set[] = {
    /* reads */
    { "/A", NULL, NULL, svn_authz_read, FALSE },
    { "/A", NULL, "plato", svn_authz_read, TRUE },
    { "/A", NULL, "socrates", svn_authz_read, TRUE },
    { "/A", NULL, "solon", svn_authz_read, TRUE },
    { "/A", NULL, "ephialtes", svn_authz_read, TRUE },
    /* writes */
    { "/A", NULL, NULL, svn_authz_write, FALSE },
    { "/A", NULL, "plato", svn_authz_write, FALSE },
    { "/A", NULL, "socrates", svn_authz_write, FALSE },
    { "/A", NULL, "solon", svn_authz_write, TRUE },
    { "/A", NULL, "ephialtes", svn_authz_write, TRUE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* Test plan:
   * 1. Create an authz file, a global groups file and an empty authz file,
   *    put all these files in the repository.  The empty authz file is
   *    required to perform the non-existent path checks (4-7) --
   *    otherwise we would get the authz validation error due to undefined
   *    groups.
   * 2. Verify that the groups file can be read with an relative URL.
   * 3. Verify that the groups file can be read with an absolute URL.
   * 4. Verify that non-existent groups file path does not error out when
   *    must_exist is FALSE.
   * 5. Same as (4), but when both authz and groups file paths do
   *    not exist.
   * 6. Verify that non-existent path for the groups file does error out when
   *    must_exist is TRUE.
   * 7. Verify that an http:// URL produces an error.
   * 8. Verify that an svn:// URL produces an error.
   */

  /* What we'll put in the authz and groups files, it's simple since
   * we're not testing the parsing, just that we got what we expected. */

  groups_contents =
    "[groups]"                                                               NL
    "philosophers = plato, socrates"                                         NL
    "senate = solon, ephialtes"                                              NL
    ""                                                                       NL;

  authz_contents =
    "[/]"                                                                    NL
    "@senate = rw"                                                           NL
    "@philosophers = r"                                                      NL
    ""                                                                       NL;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos,
                                 "test-repo-in-repo-global-groups-authz",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Commit the authz, empty authz and groups files to the repo. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_file(txn_root, "groups", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "authz", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "empty-authz", pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "groups",
                                      groups_contents, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "authz",
                                      authz_contents, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "empty-authz", "", pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Calculate URLs */
  repos_root = svn_repos_path(repos, pool);
  SVN_ERR(svn_uri_get_file_url_from_dirent(&repos_url, repos_root, pool));
  authz_url = svn_path_url_add_component2(repos_url, "authz", pool);
  empty_authz_url = svn_path_url_add_component2(repos_url, "empty-authz", pool);
  noent_authz_url = svn_path_url_add_component2(repos_url, "A/authz", pool);
  groups_url = svn_path_url_add_component2(repos_url, "groups", pool);
  noent_groups_url = svn_path_url_add_component2(repos_url, "A/groups", pool);


  /* absolute file URLs. */
  SVN_ERR(svn_repos_authz_read2(&authz_cfg, authz_url, groups_url, TRUE, pool));
  SVN_ERR(authz_check_access(authz_cfg, test_set, pool));

  /* Non-existent path for the groups file with must_exist
   * set to TRUE */
  SVN_ERR(svn_repos_authz_read2(&authz_cfg, empty_authz_url, noent_groups_url,
                                FALSE, pool));

  /* Non-existent paths for both the authz and the groups files
   * with must_exist set to TRUE */
  SVN_ERR(svn_repos_authz_read2(&authz_cfg, noent_authz_url, noent_groups_url,
                                FALSE, pool));

  /* Non-existent path for the groups file with must_exist
   * set to TRUE */
  err = svn_repos_authz_read2(&authz_cfg, empty_authz_url, noent_groups_url,
                              TRUE, pool);
  if (!err || err->apr_err != SVN_ERR_ILLEGAL_TARGET)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_ILLEGAL_TARGET",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  /* http:// URL which is unsupported */
  err = svn_repos_authz_read2(&authz_cfg, empty_authz_url,
                              "http://example.com/repo/groups",
                              TRUE, pool);
  if (!err || err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_RA_ILLEGAL_URL",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  /* svn:// URL which is unsupported */
  err = svn_repos_authz_read2(&authz_cfg, empty_authz_url,
                              "http://example.com/repo/groups",
                              TRUE, pool);
  if (!err || err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_RA_ILLEGAL_URL",
                             err ? "unexpected" : "no");
  svn_error_clear(err);


  return SVN_NO_ERROR;
}


/* Helper for the groups_authz test.  Set *AUTHZ_P to a representation of
   AUTHZ_CONTENTS in conjunction with GROUPS_CONTENTS, using POOL for
   temporary allocation.  If DISK is TRUE then write the contents to
   temporary files and use svn_repos_authz_read2() to get the data if FALSE
   write the data to a buffered stream and use svn_repos_authz_parse(). */
static svn_error_t *
authz_groups_get_handle(svn_authz_t **authz_p,
                        const char *authz_contents,
                        const char *groups_contents,
                        svn_boolean_t disk,
                        apr_pool_t *pool)
{
  if (disk)
    {
      const char *authz_file_path;
      const char *groups_file_path;

      /* Create temporary files. */
      SVN_ERR_W(svn_io_write_unique(&authz_file_path, NULL,
                                    authz_contents,
                                    strlen(authz_contents),
                                    svn_io_file_del_on_pool_cleanup, pool),
                "Writing temporary authz file");
      SVN_ERR_W(svn_io_write_unique(&groups_file_path, NULL,
                                    groups_contents,
                                    strlen(groups_contents),
                                    svn_io_file_del_on_pool_cleanup, pool),
                "Writing temporary groups file");

      /* Read the authz configuration back and start testing. */
      SVN_ERR_W(svn_repos_authz_read2(authz_p, authz_file_path,
                                      groups_file_path, TRUE, pool),
                "Opening test authz and groups files");

      /* Done with the files. */
      SVN_ERR_W(svn_io_remove_file(authz_file_path, pool),
                "Removing test authz file");
      SVN_ERR_W(svn_io_remove_file(groups_file_path, pool),
                "Removing test groups file");
    }
  else
    {
      svn_stream_t *stream;
      svn_stream_t *groups_stream;

      /* Create the streams. */
      stream = svn_stream_buffered(pool);
      groups_stream = svn_stream_buffered(pool);

      SVN_ERR_W(svn_stream_puts(stream, authz_contents),
                "Writing authz contents to stream");
      SVN_ERR_W(svn_stream_puts(groups_stream, groups_contents),
                "Writing groups contents to stream");

      /* Read the authz configuration from the streams and start testing. */
      SVN_ERR_W(svn_repos_authz_parse(authz_p, stream, groups_stream, pool),
                "Parsing the authz and groups contents");

      /* Done with the streams. */
      SVN_ERR_W(svn_stream_close(stream),
                "Closing the authz stream");
      SVN_ERR_W(svn_stream_close(groups_stream),
                "Closing the groups stream");
    }

  return SVN_NO_ERROR;
}

/* Test authz with global groups. */
static svn_error_t *
groups_authz(const svn_test_opts_t *opts,
             apr_pool_t *pool)
{
  svn_authz_t *authz_cfg;
  const char *authz_contents;
  const char *groups_contents;
  svn_error_t *err;

  struct check_access_tests test_set1[] = {
    /* reads */
    { "/A", "greek", NULL, svn_authz_read, FALSE },
    { "/A", "greek", "plato", svn_authz_read, TRUE },
    { "/A", "greek", "demetrius", svn_authz_read, TRUE },
    { "/A", "greek", "galenos", svn_authz_read, TRUE },
    { "/A", "greek", "pamphilos", svn_authz_read, FALSE },
    /* writes */
    { "/A", "greek", NULL, svn_authz_write, FALSE },
    { "/A", "greek", "plato", svn_authz_write, TRUE },
    { "/A", "greek", "demetrius", svn_authz_write, FALSE },
    { "/A", "greek", "galenos", svn_authz_write, FALSE },
    { "/A", "greek", "pamphilos", svn_authz_write, FALSE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  struct check_access_tests test_set2[] = {
    /* reads */
    { "/A", "greek", NULL, svn_authz_read, FALSE },
    { "/A", "greek", "socrates", svn_authz_read, FALSE },
    { "/B", "greek", NULL, svn_authz_read, FALSE},
    { "/B", "greek", "socrates", svn_authz_read, TRUE },
    /* writes */
    { "/A", "greek", NULL, svn_authz_write, FALSE },
    { "/A", "greek", "socrates", svn_authz_write, FALSE },
    { "/B", "greek", NULL, svn_authz_write, FALSE},
    { "/B", "greek", "socrates", svn_authz_write, TRUE },
    /* Sentinel */
    { NULL, NULL, NULL, svn_authz_none, FALSE }
  };

  /* Test plan:
   * 1. Ensure that a simple setup with global groups and access rights in
   *    two separate files works as expected.
   * 2. Verify that access rights written in the global groups file are
   *    discarded and affect nothing in authorization terms.
   * 3. Verify that local groups in the authz file are prohibited in
   *    conjunction with global groups (and that a configuration error is
   *    reported in this scenario).
   * 4. Ensure that group cycles in the global groups file are reported.
   *
   * All checks are performed twice -- for the configurations stored on disk
   * and in memory.  See authz_groups_get_handle.
   */

  groups_contents =
    "[groups]"                                                               NL
    "slaves = pamphilos,@gladiators"                                         NL
    "gladiators = demetrius,galenos"                                         NL
    "philosophers = plato"                                                   NL
    ""                                                                       NL;

  authz_contents =
    "[greek:/A]"                                                             NL
    "@slaves = "                                                             NL
    "@gladiators = r"                                                        NL
    "@philosophers = rw"                                                     NL
    ""                                                                       NL;

  SVN_ERR(authz_groups_get_handle(&authz_cfg, authz_contents,
                                  groups_contents, TRUE, pool));

  SVN_ERR(authz_check_access(authz_cfg, test_set1, pool));

  SVN_ERR(authz_groups_get_handle(&authz_cfg, authz_contents,
                                  groups_contents, FALSE, pool));

  SVN_ERR(authz_check_access(authz_cfg, test_set1, pool));

  /* Access rights in the global groups file are forbidden. */
  groups_contents =
    "[groups]"                                                               NL
    "philosophers = socrates"                                                NL
    ""                                                                       NL
    "[greek:/A]"                                                             NL
    "@philosophers = rw"                                                     NL
    ""                                                                       NL;

  authz_contents =
    "[greek:/B]"                                                             NL
    "@philosophers = rw"                                                     NL
    ""                                                                       NL;

  SVN_TEST_ASSERT_ERROR(
      authz_groups_get_handle(&authz_cfg, authz_contents,
                              groups_contents, TRUE, pool),
      SVN_ERR_AUTHZ_INVALID_CONFIG);
  SVN_TEST_ASSERT_ERROR(
      authz_groups_get_handle(&authz_cfg, authz_contents,
                              groups_contents, FALSE, pool),
      SVN_ERR_AUTHZ_INVALID_CONFIG);

  groups_contents =
    "[groups]"                                                               NL
    "philosophers = socrates"                                                NL
    ""                                                                       NL;
  SVN_ERR(authz_groups_get_handle(&authz_cfg, authz_contents,
                                  groups_contents, TRUE, pool));

  SVN_ERR(authz_check_access(authz_cfg, test_set2, pool));

  SVN_ERR(authz_groups_get_handle(&authz_cfg, authz_contents,
                                  groups_contents, FALSE, pool));

  SVN_ERR(authz_check_access(authz_cfg, test_set2, pool));

  /* Local groups cannot be used in conjunction with global groups. */
  groups_contents =
    "[groups]"                                                               NL
    "slaves = maximus"                                                       NL
    ""                                                                       NL;

  authz_contents =
    "[greek:/A]"                                                             NL
    "@slaves = "                                                             NL
    "@kings = rw"                                                            NL
    ""                                                                       NL
    "[groups]"                                                               NL
    /* That's an epic story of the slave who tried to become a king. */
    "kings = maximus"                                                        NL
    ""                                                                       NL;

  err = authz_groups_get_handle(&authz_cfg, authz_contents,
                                groups_contents, TRUE, pool);

  if (!err || err->apr_err != SVN_ERR_AUTHZ_INVALID_CONFIG)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_AUTHZ_INVALID_CONFIG",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  err = authz_groups_get_handle(&authz_cfg, authz_contents,
                                groups_contents, FALSE, pool);

  if (!err || err->apr_err != SVN_ERR_AUTHZ_INVALID_CONFIG)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_AUTHZ_INVALID_CONFIG",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  /* Ensure that group cycles are reported. */
  groups_contents =
    "[groups]"                                                               NL
    "slaves = cooks,scribes,@gladiators"                                     NL
    "gladiators = equites,thraces,@slaves"                                   NL
    ""                                                                       NL;

  authz_contents =
    "[greek:/A]"                                                             NL
    "@slaves = r"                                                            NL
    ""                                                                       NL;

  err = authz_groups_get_handle(&authz_cfg, authz_contents,
                                groups_contents, TRUE, pool);

  if (!err || err->apr_err != SVN_ERR_AUTHZ_INVALID_CONFIG)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_AUTHZ_INVALID_CONFIG",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  err = authz_groups_get_handle(&authz_cfg, authz_contents,
                                groups_contents, FALSE, pool);

  if (!err || err->apr_err != SVN_ERR_AUTHZ_INVALID_CONFIG)
    return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                             "Got %s error instead of expected "
                             "SVN_ERR_AUTHZ_INVALID_CONFIG",
                             err ? "unexpected" : "no");
  svn_error_clear(err);

  return SVN_NO_ERROR;
}

/* Callback for the commit editor tests that relays requests to
   authz. */
static svn_error_t *
commit_authz_cb(svn_repos_authz_access_t required,
                svn_boolean_t *allowed,
                svn_fs_root_t *root,
                const char *path,
                void *baton,
                apr_pool_t *pool)
{
  svn_authz_t *authz_file = baton;

  return svn_repos_authz_check_access(authz_file, "test", path,
                                      "plato", required, allowed,
                                      pool);
}



enum action_t {
  A_DELETE,
  A_ADD_FILE,
  A_ADD_DIR,
  A_CHANGE_FILE_PROP
};
struct authz_path_action_t
{
  enum action_t action;
  const char *path;
  svn_boolean_t authz_error_expected;
  const char *copyfrom_path;
};

/* Return the appropriate dir baton for the parent of PATH in *DIR_BATON,
   allocated in POOL. */
static svn_error_t *
get_dir_baton(void **dir_baton,
              const char *path,
              const svn_delta_editor_t *editor,
              void *root_baton,
              apr_pool_t *pool)
{
  int i;
  apr_array_header_t *path_bits = svn_path_decompose(path, pool);
  const char *path_so_far = "";

  *dir_baton = root_baton;
  for (i = 0; i < (path_bits->nelts - 1); i++)
    {
      const char *path_bit = APR_ARRAY_IDX(path_bits, i, const char *);
      path_so_far = svn_path_join(path_so_far, path_bit, pool);
      SVN_ERR(editor->open_directory(path_so_far, *dir_baton,
                                     SVN_INVALID_REVNUM, pool, dir_baton));
    }

  return SVN_NO_ERROR;
}

/* Return the appropriate file baton for PATH in *FILE_BATON, allocated in
   POOL. */
static svn_error_t *
get_file_baton(void **file_baton,
               const char *path,
               const svn_delta_editor_t *editor,
               void *root_baton,
               apr_pool_t *pool)
{
  void *dir_baton;

  SVN_ERR(get_dir_baton(&dir_baton, path, editor, root_baton, pool));

  SVN_ERR(editor->open_file(path, dir_baton, SVN_INVALID_REVNUM, pool,
                            file_baton));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_path_authz(svn_repos_t *repos,
                struct authz_path_action_t *path_action,
                svn_authz_t *authz_file,
                svn_revnum_t youngest_rev,
                apr_pool_t *scratch_pool)
{
  void *edit_baton;
  void *root_baton;
  void *dir_baton;
  void *file_baton;
  void *out_baton;
  const svn_delta_editor_t *editor;
  svn_error_t *err;
  svn_error_t *err2;

  /* Create a new commit editor in which we're going to play with
     authz */
  SVN_ERR(svn_repos_get_commit_editor4(&editor, &edit_baton, repos,
                                       NULL, "file://test", "/",
                                       "plato", "test commit", NULL,
                                       NULL, commit_authz_cb, authz_file,
                                       scratch_pool));

  /* Start fiddling.  First get the root, which is readonly. */
  SVN_ERR(editor->open_root(edit_baton, 1, scratch_pool, &root_baton));

  /* Fetch the appropriate baton for our action.  This may involve opening
     intermediate batons, but we only care about the final one for the
     cooresponding action. */
  if (path_action->action == A_CHANGE_FILE_PROP)
    SVN_ERR(get_file_baton(&file_baton, path_action->path, editor, root_baton,
                           scratch_pool));
  else
    SVN_ERR(get_dir_baton(&dir_baton, path_action->path, editor, root_baton,
                          scratch_pool));

  /* Test the appropriate action. */
  switch (path_action->action)
    {
      case A_DELETE:
        err = editor->delete_entry(path_action->path, SVN_INVALID_REVNUM,
                                   dir_baton, scratch_pool);
        break;

      case A_CHANGE_FILE_PROP:
        err = editor->change_file_prop(file_baton, "svn:test",
                                       svn_string_create("test", scratch_pool),
                                       scratch_pool);
        break;

      case A_ADD_FILE:
        err = editor->add_file(path_action->path, dir_baton,
                               path_action->copyfrom_path, youngest_rev,
                               scratch_pool, &out_baton);
        break;

      case A_ADD_DIR:
        err = editor->add_directory(path_action->path, dir_baton,
                                    path_action->copyfrom_path, youngest_rev,
                                    scratch_pool, &out_baton);
        break;

      default:
        SVN_TEST_ASSERT(FALSE);
    }

  /* Don't worry about closing batons, just abort the edit.  Since errors
     may be delayed, we need to capture results of the abort as well. */
  err2 = editor->abort_edit(edit_baton, scratch_pool);
  if (!err)
    err = err2;
  else
    svn_error_clear(err2);

  /* Check for potential errors. */
  if (path_action->authz_error_expected)
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_AUTHZ_UNWRITABLE);
  else
    SVN_ERR(err);

  return SVN_NO_ERROR;
}


/* Test that the commit editor is taking authz into account
   properly */
static svn_error_t *
commit_editor_authz(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t youngest_rev;
  svn_authz_t *authz_file;
  apr_pool_t *iterpool;
  const char *authz_contents;
  int i;
  struct authz_path_action_t path_actions[] = {
      { A_DELETE,             "/iota",      TRUE },
      { A_CHANGE_FILE_PROP,   "/iota",      TRUE },
      { A_ADD_FILE,           "/alpha",     TRUE },
      { A_ADD_FILE,           "/alpha",     TRUE,   "file://test/A/B/lambda" },
      { A_ADD_DIR,            "/I",         TRUE },
      { A_ADD_DIR,            "/J",         TRUE,   "file://test/A/D" },
      { A_ADD_FILE,           "/A/alpha",   TRUE },
      { A_ADD_FILE,           "/A/B/theta", FALSE },
      { A_DELETE,             "/A/mu",      FALSE },
      { A_ADD_DIR,            "/A/E",       FALSE },
      { A_ADD_DIR,            "/A/J",       FALSE,  "file://test/A/D" },
      { A_DELETE,             "A/D/G",      TRUE },
      { A_DELETE,             "A/D/H",      FALSE },
      { A_CHANGE_FILE_PROP,   "A/D/gamma",  FALSE }
    };

  /* The Test Plan
   *
   * We create a greek tree repository, then create a commit editor
   * and try to perform various operations that will run into authz
   * callbacks.  Check that all operations are properly
   * authorized/denied when necessary.  We don't try to be exhaustive
   * in the kinds of authz lookups.  We just make sure that the editor
   * replies to the calls in a way that proves it is doing authz
   * lookups.  Some actions are tested implicitly (such as open_file being
   * required for change_file_props).
   *
   * Note that because of the error handling requirements of the generic
   * editor API, each operation needs its own editor, which is handled by
   * a helper function above.
   */

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-commit-authz",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Load the authz rules for the greek tree. */
  authz_contents =
    ""                                                                       NL
    ""                                                                       NL
    "[/]"                                                                    NL
    "plato = r"                                                              NL
    ""                                                                       NL
    "[/A]"                                                                   NL
    "plato = rw"                                                             NL
    ""                                                                       NL
    "[/A/alpha]"                                                             NL
    "plato = "                                                               NL
    ""                                                                       NL
    "[/A/C]"                                                                 NL
    ""                                                                       NL
    "plato = "                                                               NL
    ""                                                                       NL
    "[/A/D]"                                                                 NL
    "plato = rw"                                                             NL
    ""                                                                       NL
    "[/A/D/G]"                                                               NL
    "plato = r"; /* No newline at end of file. */

  SVN_ERR(authz_get_handle(&authz_file, authz_contents, FALSE, pool));

  iterpool = svn_pool_create(pool);
  for (i = 0; i < (sizeof(path_actions) / sizeof(struct authz_path_action_t));
        i++)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(test_path_authz(repos, &path_actions[i], authz_file,
                              youngest_rev, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* This implements svn_commit_callback2_t. */
static svn_error_t *
dummy_commit_cb(const svn_commit_info_t *commit_info,
                void *baton, apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

/* Test using explicit txns during a commit. */
static svn_error_t *
commit_continue_txn(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *revision_root;
  svn_revnum_t youngest_rev;
  void *edit_baton;
  void *root_baton, *file_baton;
  const svn_delta_editor_t *editor;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *txn_name;

  /* The Test Plan
   *
   * We create a greek tree repository, then create a transaction and
   * a commit editor from that txn.  We do one change, abort the edit, reopen
   * the txn and create a new commit editor, do anyther change and commit.
   * We check that both changes were done.
   */

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-commit-continue",
                                 opts, subpool));
  fs = svn_repos_fs(repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, subpool));
  SVN_ERR(svn_repos_get_commit_editor4(&editor, &edit_baton, repos,
                                       txn, "file://test", "/",
                                       "plato", "test commit",
                                       dummy_commit_cb, NULL, NULL, NULL,
                                       subpool));

  SVN_ERR(editor->open_root(edit_baton, 1, subpool, &root_baton));

  SVN_ERR(editor->add_file("/f1", root_baton, NULL, SVN_INVALID_REVNUM,
                           subpool, &file_baton));
  SVN_ERR(editor->close_file(file_baton, NULL, subpool));
  /* This should leave the transaction. */
  SVN_ERR(editor->abort_edit(edit_baton, subpool));

  /* Reopen the transaction. */
  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, subpool));
  SVN_ERR(svn_repos_get_commit_editor4(&editor, &edit_baton, repos,
                                       txn, "file://test", "/",
                                       "plato", "test commit",
                                       dummy_commit_cb,
                                       NULL, NULL, NULL,
                                       subpool));

  SVN_ERR(editor->open_root(edit_baton, 1, subpool, &root_baton));

  SVN_ERR(editor->add_file("/f2", root_baton, NULL, SVN_INVALID_REVNUM,
                           subpool, &file_baton));
  SVN_ERR(editor->close_file(file_baton, NULL, subpool));

  /* Finally, commit it. */
  SVN_ERR(editor->close_edit(edit_baton, subpool));

  /* Check that the edits really happened. */
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
      { "A/D/H/omega", "This is the file 'omega'.\n" },
      { "f1",          "" },
      { "f2",          "" }
    };
    SVN_ERR(svn_fs_revision_root(&revision_root, fs,
                                 2, subpool));
    SVN_ERR(svn_test__validate_tree
            (revision_root, expected_entries,
             sizeof(expected_entries) / sizeof(expected_entries[0]),
             subpool));
  }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* A baton for check_location_segments(). */
struct nls_receiver_baton
{
  int count;
  const svn_location_segment_t *expected_segments;
};

/* Return a pretty-printed string representing SEGMENT. */
static const char *
format_segment(const svn_location_segment_t *segment,
               apr_pool_t *pool)
{
  return apr_psprintf(pool, "[r%ld-r%ld: /%s]",
                      segment->range_start,
                      segment->range_end,
                      segment->path ? segment->path : "(null)");
}

/* A location segment receiver for check_location_segments().
 * Implements svn_location_segment_receiver_t. */
static svn_error_t *
nls_receiver(svn_location_segment_t *segment,
             void *baton,
             apr_pool_t *pool)
{
  struct nls_receiver_baton *b = baton;
  const svn_location_segment_t *expected_segment = b->expected_segments + b->count;

  /* expected_segments->range_end can't be 0, so if we see that, it's
     our end-of-the-list sentry. */
  if (! expected_segment->range_end)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Got unexpected location segment: %s",
                             format_segment(segment, pool));

  if (expected_segment->range_start != segment->range_start
      || expected_segment->range_end != segment->range_end
      || strcmp_null(expected_segment->path, segment->path) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Location segments differ\n"
                             "   Expected location segment: %s\n"
                             "     Actual location segment: %s",
                             format_segment(expected_segment, pool),
                             format_segment(segment, pool));
  b->count++;
  return SVN_NO_ERROR;
}

/* Run a svn_repos_node_location_segments() query with REPOS, PATH, PEG_REV,
 * START_REV, END_REV.  Check that the result exactly matches the list of
 * segments EXPECTED_SEGMENTS, which is terminated by an entry with
 * 'range_end'==0.
 */
static svn_error_t *
check_location_segments(svn_repos_t *repos,
                        const char *path,
                        svn_revnum_t peg_rev,
                        svn_revnum_t start_rev,
                        svn_revnum_t end_rev,
                        const svn_location_segment_t *expected_segments,
                        apr_pool_t *pool)
{
  struct nls_receiver_baton b;
  const svn_location_segment_t *segment;

  /* Run svn_repos_node_location_segments() with a receiver that
     validates against EXPECTED_SEGMENTS.  */
  b.count = 0;
  b.expected_segments = expected_segments;
  SVN_ERR(svn_repos_node_location_segments(repos, path, peg_rev,
                                           start_rev, end_rev, nls_receiver,
                                           &b, NULL, NULL, pool));

  /* Make sure we saw all of our expected segments.  (If the
     'range_end' member of our expected_segments is 0, it's our
     end-of-the-list sentry.  Otherwise, it's some segment we expect
     to see.)  If not, raise an error.  */
  segment = expected_segments + b.count;
  if (segment->range_end)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Failed to get expected location segment: %s",
                             format_segment(segment, pool));
  return SVN_NO_ERROR;
}

/* Inputs and expected outputs for svn_repos_node_location_segments() tests.
 */
typedef struct location_segment_test_t
{
  /* Path and peg revision to query */
  const char *path;
  svn_revnum_t peg;
  /* Start (youngest) and end (oldest) revisions to query */
  svn_revnum_t start;
  svn_revnum_t end;

  /* Expected segments */
  svn_location_segment_t segments[10];

} location_segment_test_t;

static svn_error_t *
node_location_segments(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *root;
  svn_revnum_t youngest_rev = 0;

  static const location_segment_test_t subtests[] =
  {
    { /* Check locations for /@HEAD. */
      "", SVN_INVALID_REVNUM, SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
      {
        { 0, 7, "" },
        { 0 }
      }
    },
    { /* Check locations for A/D@HEAD. */
      "A/D", SVN_INVALID_REVNUM, SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
      {
        { 7, 7, "A/D" },
        { 3, 6, "A/D2" },
        { 1, 2, "A/D" },
        { 0 }
      }
    },
    { /* Check a subset of the locations for A/D@HEAD. */
      "A/D", SVN_INVALID_REVNUM, 5, 2,
      {
        { 3, 5, "A/D2" },
        { 2, 2, "A/D" },
        { 0 }
      },
    },
    { /* Check a subset of locations for A/D2@5. */
      "A/D2", 5, 3, 2,
      {
        { 3, 3, "A/D2" },
        { 2, 2, "A/D" },
        { 0 }
      },
    },
    { /* Check locations for A/D@6. */
      "A/D", 6, 6, SVN_INVALID_REVNUM,
      {
        { 1, 6, "A/D" },
        { 0 }
      },
    },
    { /* Check locations for A/D/G@HEAD. */
      "A/D/G", SVN_INVALID_REVNUM, SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
      {
        { 7, 7, "A/D/G" },
        { 6, 6, "A/D2/G" },
        { 5, 5, NULL },
        { 3, 4, "A/D2/G" },
        { 1, 2, "A/D/G" },
        { 0 }
      },
    },
    { /* Check a subset of the locations for A/D/G@HEAD. */
      "A/D/G", SVN_INVALID_REVNUM, 3, 2,
      {
        { 3, 3, "A/D2/G" },
        { 2, 2, "A/D/G" },
        { 0 }
      },
    },
    {
      NULL
    },
  };
  const location_segment_test_t *subtest;

  /* Bail (with success) on known-untestable scenarios */
  if ((strcmp(opts->fs_type, "bdb") == 0)
      && (opts->server_minor_version == 4))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "not supported for BDB in SVN 1.4");

  /* Create the repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-node-location-segments",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Revision 1: Create the Greek tree.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 2: Modify A/D/H/chi and A/B/E/alpha.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/H/chi", "2", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/alpha", "2", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 3: Copy A/D to A/D2.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_copy(root, "A/D", txn_root, "A/D2", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 4: Modify A/D/H/chi and A/D2/H/chi.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/H/chi", "4", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/D2/H/chi", "4", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 5: Delete A/D2/G.  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_delete(txn_root, "A/D2/G", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 6: Restore A/D2/G (from version 4).  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, 4, subpool));
  SVN_ERR(svn_fs_copy(root, "A/D2/G", txn_root, "A/D2/G", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 7: Move A/D2 to A/D (replacing it).  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_delete(txn_root, "A/D", subpool));
  SVN_ERR(svn_fs_copy(root, "A/D2", txn_root, "A/D", subpool));
  SVN_ERR(svn_fs_delete(txn_root, "A/D2", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /*  */
  for (subtest = subtests; subtest->path; subtest++)
    {
      SVN_ERR(check_location_segments(repos, subtest->path, subtest->peg,
                                      subtest->start, subtest->end,
                                      subtest->segments, pool));
    }

  return SVN_NO_ERROR;
}



/* Test that the reporter doesn't send deltas under excluded paths. */
static svn_error_t *
reporter_depth_exclude(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_revnum_t youngest_rev;
  const svn_delta_editor_t *editor;
  void *edit_baton, *report_baton;
  svn_error_t *err;

  SVN_ERR(svn_test__create_repos(&repos, "test-repo-reporter-depth-exclude",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Revision 2: make a bunch of changes */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  {
    static svn_test__txn_script_command_t script_entries[] = {
      { 'e', "iota",      "Changed file 'iota'.\n" },
      { 'e', "A/D/G/pi",  "Changed file 'pi'.\n" },
      { 'e', "A/mu",      "Changed file 'mu'.\n" },
      { 'a', "A/D/foo",    "New file 'foo'.\n" },
      { 'a', "A/B/bar",    "New file 'bar'.\n" },
      { 'd', "A/D/H",      NULL },
      { 'd', "A/B/E/beta", NULL }
    };
    SVN_ERR(svn_test__txn_script_exec(txn_root,
                                      script_entries,
                                      sizeof(script_entries)/
                                       sizeof(script_entries[0]),
                                      subpool));
  }
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Confirm the contents of r2. */
  {
    svn_fs_root_t *revision_root;
    static svn_test__tree_entry_t entries[] = {
      { "iota",        "Changed file 'iota'.\n" },
      { "A",           0 },
      { "A/mu",        "Changed file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/bar",     "New file 'bar'.\n" },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/F",       0 },
      { "A/C",         0 },
      { "A/D",         0 },
      { "A/D/foo",     "New file 'foo'.\n" },
      { "A/D/gamma",   "This is the file 'gamma'.\n" },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "Changed file 'pi'.\n" },
      { "A/D/G/rho",   "This is the file 'rho'.\n" },
      { "A/D/G/tau",   "This is the file 'tau'.\n" },
    };
    SVN_ERR(svn_fs_revision_root(&revision_root, fs,
                                 youngest_rev, subpool));
    SVN_ERR(svn_test__validate_tree(revision_root,
                                    entries,
                                    sizeof(entries)/sizeof(entries[0]),
                                    subpool));
  }

  /* Run an update from r1 to r2, excluding iota and everything under
     A/D.  Record the editor commands in a temporary txn. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 1, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(dir_delta_get_editor(&editor, &edit_baton, fs,
                               txn_root, "", subpool));

  SVN_ERR(svn_repos_begin_report3(&report_baton, 2, repos, "/", "", NULL,
                                  TRUE, svn_depth_infinity, FALSE, FALSE,
                                  editor, edit_baton, NULL, NULL, 0,
                                  subpool));
  SVN_ERR(svn_repos_set_path3(report_baton, "", 1,
                              svn_depth_infinity,
                              FALSE, NULL, subpool));
  SVN_ERR(svn_repos_set_path3(report_baton, "iota", SVN_INVALID_REVNUM,
                              svn_depth_exclude,
                              FALSE, NULL, subpool));
  SVN_ERR(svn_repos_set_path3(report_baton, "A/D", SVN_INVALID_REVNUM,
                              svn_depth_exclude,
                              FALSE, NULL, subpool));
  SVN_ERR(svn_repos_finish_report(report_baton, subpool));

  /* Confirm the contents of the txn. */
  /* This should have iota and A/D from r1, and everything else from
     r2. */
  {
    static svn_test__tree_entry_t entries[] = {
      { "iota",        "This is the file 'iota'.\n" },
      { "A",           0 },
      { "A/mu",        "Changed file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/bar",     "New file 'bar'.\n" },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
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
    SVN_ERR(svn_test__validate_tree(txn_root,
                                    entries,
                                    sizeof(entries)/sizeof(entries[0]),
                                    subpool));
  }

  /* Clean up after ourselves. */
  svn_error_clear(svn_fs_abort_txn(txn, subpool));
  svn_pool_clear(subpool);

  /* Expect an error on an illegal report for r1 to r2.  The illegal
     sequence is that we exclude A/D, then set_path() below A/D. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 1, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(dir_delta_get_editor(&editor, &edit_baton, fs,
                               txn_root, "", subpool));

  SVN_ERR(svn_repos_begin_report3(&report_baton, 2, repos, "/", "", NULL,
                                  TRUE, svn_depth_infinity, FALSE, FALSE,
                                  editor, edit_baton, NULL, NULL, 0,
                                  subpool));
  SVN_ERR(svn_repos_set_path3(report_baton, "", 1,
                              svn_depth_infinity,
                              FALSE, NULL, subpool));
  SVN_ERR(svn_repos_set_path3(report_baton, "iota", SVN_INVALID_REVNUM,
                              svn_depth_exclude,
                              FALSE, NULL, subpool));
  SVN_ERR(svn_repos_set_path3(report_baton, "A/D", SVN_INVALID_REVNUM,
                              svn_depth_exclude,
                              FALSE, NULL, subpool));

  /* This is the illegal call, since A/D was excluded above; the call
     itself will not error, but finish_report() will.  As of r868172,
     this delayed error behavior is not actually promised by the
     reporter API, which merely warns callers not to touch a path
     underneath a previously excluded path without defining what will
     happen if they do.  However, it's still useful to test for the
     error, since the reporter code is sensitive and we'd certainly
     want to know about it if the behavior were to change. */
  SVN_ERR(svn_repos_set_path3(report_baton, "A/D/G/pi",
                              SVN_INVALID_REVNUM,
                              svn_depth_infinity,
                              FALSE, NULL, subpool));
  err = svn_repos_finish_report(report_baton, subpool);
  if (! err)
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, NULL,
         "Illegal report of \"A/D/G/pi\" did not error as expected");
    }
  else if (err->apr_err != SVN_ERR_FS_NOT_FOUND)
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, err,
         "Illegal report of \"A/D/G/pi\" got wrong kind of error:");
    }

  /* Clean up after ourselves. */
  svn_error_clear(err);
  svn_error_clear(svn_fs_abort_txn(txn, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* Test if prop values received by the server are validated.
 * These tests "send" property values to the server and diagnose the
 * behaviour.
 */

/* Helper function that makes an arbitrary change to a given repository
 * REPOS and runs a commit with a specific revision property set to a
 * certain value. The property name, type and value are given in PROP_KEY,
 * PROP_KLEN and PROP_VAL, as in apr_hash_set(), using a const char* key.
 *
 * The FILENAME argument names a file in the test repository to add in
 * this commit, e.g. "/A/should_fail_1".
 *
 * On success, the given file is added to the repository. So, using
 * the same name multiple times on the same repository might fail. Thus,
 * use different FILENAME arguments for every call to this function
 * (e.g. "/A/f1", "/A/f2", "/A/f3" etc).
 */
static svn_error_t *
prop_validation_commit_with_revprop(const char *filename,
                                    const char *prop_key,
                                    apr_ssize_t prop_klen,
                                    const svn_string_t *prop_val,
                                    svn_repos_t *repos,
                                    apr_pool_t *pool)
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *root_baton;
  void *file_baton;

  /* Prepare revision properties */
  apr_hash_t *revprop_table = apr_hash_make(pool);

  /* Add the requested property */
  apr_hash_set(revprop_table, prop_key, prop_klen, prop_val);

  /* Set usual author and log props, if not set already */
  if (strcmp(prop_key, SVN_PROP_REVISION_AUTHOR) != 0)
    {
      apr_hash_set(revprop_table, SVN_PROP_REVISION_AUTHOR,
                   APR_HASH_KEY_STRING,
                   svn_string_create("plato", pool));
    }
  else if (strcmp(prop_key, SVN_PROP_REVISION_LOG) != 0)
    {
      apr_hash_set(revprop_table, SVN_PROP_REVISION_LOG,
                   APR_HASH_KEY_STRING,
                   svn_string_create("revision log", pool));
    }

  /* Make an arbitrary change and commit using above values... */

  SVN_ERR(svn_repos_get_commit_editor5(&editor, &edit_baton, repos,
                                       NULL, "file://test", "/",
                                       revprop_table,
                                       NULL, NULL, NULL, NULL, pool));

  SVN_ERR(editor->open_root(edit_baton, 0, pool, &root_baton));

  SVN_ERR(editor->add_file(filename, root_baton, NULL,
                           SVN_INVALID_REVNUM, pool,
                           &file_baton));

  SVN_ERR(editor->close_file(file_baton, NULL, pool));

  SVN_ERR(editor->close_directory(root_baton, pool));

  SVN_ERR(editor->close_edit(edit_baton, pool));

  return SVN_NO_ERROR;
}


/* Expect failure of invalid commit in these cases:
 *  - log message contains invalid UTF-8 octet (issue 1796)
 *  - log message contains invalid linefeed style (non-LF) (issue 1796)
 */
static svn_error_t *
prop_validation(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_error_t *err;
  svn_repos_t *repos;
  const char non_utf8_string[5] = { 'a', '\xff', 'b', '\n', 0 };
  const char *non_lf_string = "a\r\nb\n\rc\rd\n";
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-prop-validation",
                                 opts, subpool));


  /* Test an invalid commit log message: UTF-8 */
  err = prop_validation_commit_with_revprop
            ("/non_utf8_log_msg",
             SVN_PROP_REVISION_LOG, APR_HASH_KEY_STRING,
             svn_string_create(non_utf8_string, subpool),
             repos, subpool);

  if (err == SVN_NO_ERROR)
    return svn_error_create(SVN_ERR_TEST_FAILED, err,
                            "Failed to reject a log with invalid "
                            "UTF-8");
  else if (err->apr_err != SVN_ERR_BAD_PROPERTY_VALUE)
    return svn_error_create(SVN_ERR_TEST_FAILED, err,
                            "Expected SVN_ERR_BAD_PROPERTY_VALUE for "
                            "a log with invalid UTF-8, "
                            "got another error.");
  svn_error_clear(err);


  /* Test an invalid commit log message: LF */
  err = prop_validation_commit_with_revprop
            ("/non_lf_log_msg",
             SVN_PROP_REVISION_LOG, APR_HASH_KEY_STRING,
             svn_string_create(non_lf_string, subpool),
             repos, subpool);

  if (err == SVN_NO_ERROR)
    return svn_error_create(SVN_ERR_TEST_FAILED, err,
                            "Failed to reject a log with inconsistent "
                            "line ending style");
  else if (err->apr_err != SVN_ERR_BAD_PROPERTY_VALUE)
    return svn_error_create(SVN_ERR_TEST_FAILED, err,
                            "Expected SVN_ERR_BAD_PROPERTY_VALUE for "
                            "a log with inconsistent line ending style, "
                            "got another error.");
  svn_error_clear(err);


  /* Done. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* Tests for svn_repos_get_logsN() */

/* Log receiver which simple increments a counter. */
static svn_error_t *
log_receiver(void *baton,
             svn_log_entry_t *log_entry,
             apr_pool_t *pool)
{
  svn_revnum_t *count = baton;
  (*count)++;
  return SVN_NO_ERROR;
}


static svn_error_t *
get_logs(const svn_test_opts_t *opts,
         apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t start, end, youngest_rev = 0;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-get-logs",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Revision 1:  Add the Greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Revision 2:  Tweak A/mu and A/B/E/alpha. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/mu",
                                      "Revision 2", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/alpha",
                                      "Revision 2", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Revision 3:  Tweak A/B/E/alpha and A/B/E/beta. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/alpha",
                                      "Revision 3", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/B/E/beta",
                                      "Revision 3", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));


  for (start = 0; start <= youngest_rev; start++)
    {
      for (end = 0; end <= youngest_rev; end++)
        {
          svn_revnum_t start_arg = start ? start : SVN_INVALID_REVNUM;
          svn_revnum_t end_arg   = end ? end : SVN_INVALID_REVNUM;
          svn_revnum_t eff_start = start ? start : youngest_rev;
          svn_revnum_t eff_end   = end ? end : youngest_rev;
          int limit;
          svn_revnum_t max_logs =
            MAX(eff_start, eff_end) + 1 - MIN(eff_start, eff_end);
          svn_revnum_t num_logs;

          /* this may look like it can get in an infinite loop if max_logs
           * ended up being larger than the size limit can represent.  It
           * can't because a negative limit will end up failing to match
           * the existed number of logs. */
          for (limit = 0; limit <= max_logs; limit++)
            {
              svn_revnum_t num_expected = limit ? limit : max_logs;

              svn_pool_clear(subpool);
              num_logs = 0;
              SVN_ERR(svn_repos_get_logs4(repos, NULL, start_arg, end_arg,
                                          limit, FALSE, FALSE, FALSE, NULL,
                                          NULL, NULL, log_receiver, &num_logs,
                                          subpool));
              if (num_logs != num_expected)
                return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                         "Log with start=%ld,end=%ld,limit=%d "
                                         "returned %ld entries (expected %ld)",
                                         start_arg, end_arg, limit,
                                         num_logs, num_expected);
            }
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Tests for svn_repos_get_file_revsN() */

typedef struct file_revs_t {
    svn_revnum_t rev;
    const char *path;
    svn_boolean_t result_of_merge;
    const char *author;
} file_revs_t;

/* Finds the revision REV in the hash table passed in in BATON, and checks
   if the PATH and RESULT_OF_MERGE match are as expected. */
static svn_error_t *
file_rev_handler(void *baton, const char *path, svn_revnum_t rev,
                 apr_hash_t *rev_props, svn_boolean_t result_of_merge,
                 svn_txdelta_window_handler_t *delta_handler,
                 void **delta_baton, apr_array_header_t *prop_diffs,
                 apr_pool_t *pool)
{
  apr_hash_t *ht = baton;
  const char *author;
  file_revs_t *file_rev = apr_hash_get(ht, &rev, sizeof(rev));

  if (!file_rev)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Revision rev info not expected for rev %ld "
                             "from path %s",
                             rev, path);

  author = svn_prop_get_value(rev_props,
                              SVN_PROP_REVISION_AUTHOR);

  SVN_TEST_STRING_ASSERT(author, file_rev->author);
  SVN_TEST_STRING_ASSERT(path, file_rev->path);
  SVN_TEST_ASSERT(rev == file_rev->rev);
  SVN_TEST_ASSERT(result_of_merge == file_rev->result_of_merge);

  /* Remove this revision from this list so we'll be able to verify that we
     have seen all expected revisions. */
  apr_hash_set(ht, &rev, sizeof(rev), NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_get_file_revs(const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  svn_repos_t *repos = NULL;
  svn_fs_t *fs;
  svn_revnum_t youngest_rev = 0;
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;

  file_revs_t trunk_results[] = {
    { 2, "/trunk/A/mu", FALSE, "initial" },
    { 3, "/trunk/A/mu", FALSE, "user-trunk" },
    { 4, "/branches/1.0.x/A/mu", TRUE, "copy" },
    { 5, "/trunk/A/mu", FALSE, "user-trunk" },
    { 6, "/branches/1.0.x/A/mu", TRUE, "user-branch" },
    { 7, "/branches/1.0.x/A/mu", TRUE, "user-merge1" },
    { 8, "/trunk/A/mu", FALSE, "user-merge2" },
  };
  file_revs_t branch_results[] = {
    { 2, "/trunk/A/mu", FALSE, "initial" },
    { 3, "/trunk/A/mu", FALSE, "user-trunk" },
    { 4, "/branches/1.0.x/A/mu", FALSE, "copy" },
    { 5, "/trunk/A/mu", TRUE, "user-trunk" },
    { 6, "/branches/1.0.x/A/mu", FALSE, "user-branch" },
    { 7, "/branches/1.0.x/A/mu", FALSE, "user-merge1" },
  };
  apr_hash_t *ht_trunk_results = apr_hash_make(subpool);
  apr_hash_t *ht_branch_results = apr_hash_make(subpool);
  apr_hash_t *ht_reverse_results = apr_hash_make(subpool);

  for (i = 0; i < sizeof(trunk_results) / sizeof(trunk_results[0]); i++)
    apr_hash_set(ht_trunk_results, &trunk_results[i].rev,
                 sizeof(trunk_results[i].rev), &trunk_results[i]);

  for (i = 0; i < sizeof(branch_results) / sizeof(branch_results[0]); i++)
    apr_hash_set(ht_branch_results, &branch_results[i].rev,
                 sizeof(branch_results[i].rev), &branch_results[i]);

  for (i = 0; i < sizeof(trunk_results) / sizeof(trunk_results[0]); i++)
    if (!trunk_results[i].result_of_merge)
      apr_hash_set(ht_reverse_results, &trunk_results[i].rev,
                   sizeof(trunk_results[i].rev), &trunk_results[i]);

  /* Check for feature support */
  if (opts->server_minor_version && (opts->server_minor_version < 5))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "not supported in pre-1.5 SVN");

  /* Create the repository and verify blame results. */
  SVN_ERR(svn_test__create_blame_repository(&repos, "test-repo-get-filerevs",
                                            opts, subpool));
  fs = svn_repos_fs(repos);

  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, subpool));

  /* Verify blame of /trunk/A/mu */
  SVN_ERR(svn_repos_get_file_revs2(repos, "/trunk/A/mu", 0, youngest_rev,
                                   TRUE, NULL, NULL,
                                   file_rev_handler,
                                   ht_trunk_results,
                                   subpool));
  SVN_TEST_ASSERT(apr_hash_count(ht_trunk_results) == 0);

  /* Verify blame of /branches/1.0.x/A/mu */
  SVN_ERR(svn_repos_get_file_revs2(repos, "/branches/1.0.x/A/mu", 0,
                                   youngest_rev,
                                   TRUE, NULL, NULL,
                                   file_rev_handler,
                                   ht_branch_results,
                                   subpool));
  SVN_TEST_ASSERT(apr_hash_count(ht_branch_results) == 0);

  /* ### TODO: Verify blame of /branches/1.0.x/A/mu in range 6-7 */

  SVN_ERR(svn_repos_get_file_revs2(repos, "/trunk/A/mu", youngest_rev, 0,
                                   FALSE, NULL, NULL,
                                   file_rev_handler,
                                   ht_reverse_results,
                                   subpool));
  SVN_TEST_ASSERT(apr_hash_count(ht_reverse_results) == 0);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
issue_4060(const svn_test_opts_t *opts,
           apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_authz_t *authz_cfg;
  svn_boolean_t allowed;
  const char *authz_contents =
    "[/A/B]"                                                               NL
    "ozymandias = rw"                                                      NL
    "[/]"                                                                  NL
    "ozymandias = r"                                                       NL
    ""                                                                     NL;

  SVN_ERR(authz_get_handle(&authz_cfg, authz_contents, FALSE, subpool));

  SVN_ERR(svn_repos_authz_check_access(authz_cfg, "babylon",
                                       "/A/B/C", "ozymandias",
                                       svn_authz_write | svn_authz_recursive,
                                       &allowed, subpool));
  SVN_TEST_ASSERT(allowed);

  SVN_ERR(svn_repos_authz_check_access(authz_cfg, "",
                                       "/A/B/C", "ozymandias",
                                       svn_authz_write | svn_authz_recursive,
                                       &allowed, subpool));
  SVN_TEST_ASSERT(allowed);

  SVN_ERR(svn_repos_authz_check_access(authz_cfg, NULL,
                                       "/A/B/C", "ozymandias",
                                       svn_authz_write | svn_authz_recursive,
                                       &allowed, subpool));
  SVN_TEST_ASSERT(allowed);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Test svn_repos_delete(). */
static svn_error_t *
test_delete_repos(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  const char *path;
  svn_node_kind_t kind;

  /* We have to use a subpool to close the svn_repos_t before calling
     svn_repos_delete. */
  {
    svn_repos_t *repos;
    apr_pool_t *subpool = svn_pool_create(pool);
    SVN_ERR(svn_test__create_repos(&repos, "test-repo-delete-repos", opts,
                                   subpool));
    path = svn_repos_path(repos, pool);
    svn_pool_destroy(subpool);
  }

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  SVN_TEST_ASSERT(kind != svn_node_none);
  SVN_ERR(svn_repos_delete(path, pool));
  SVN_ERR(svn_io_check_path(path, &kind, pool));
  SVN_TEST_ASSERT(kind == svn_node_none);

  /* Recreate dir so that test cleanup doesn't fail. */
  SVN_ERR(svn_io_dir_make(path, APR_OS_DEFAULT, pool));

  return SVN_NO_ERROR;
}

/* Prepare a commit for the filename_with_control_chars() tests */
static svn_error_t *
fwcc_prepare(const svn_delta_editor_t **editor_p,
             void **edit_baton_p,
             void **root_baton,
             svn_repos_t *repos,
             apr_pool_t *scratch_pool)
{
  /* Checks for control characters are implemented in the commit editor,
   * not in the FS API. */
  SVN_ERR(svn_repos_get_commit_editor4(editor_p, edit_baton_p, repos,
                                       NULL, "file://test", "/",
                                       "plato", "test commit",
                                       dummy_commit_cb, NULL, NULL, NULL,
                                       scratch_pool));
  SVN_ERR((*editor_p)->open_root(*edit_baton_p, 1, scratch_pool, root_baton));
  return SVN_NO_ERROR;
}

/* Related to issue 4340, "filenames containing \n corrupt FSFS repositories" */
static svn_error_t *
filename_with_control_chars(const svn_test_opts_t *opts,
                            apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t youngest_rev = 0;
  svn_error_t *err;
  static const char *bad_paths[] = {
    "/bar\t",
    "/bar\n",
    "/\barb\az",
    "/\x02 baz",
    NULL,
  };
  const char *p;
  int i;
  void *edit_baton;
  void *root_baton;
  void *out_baton;
  const svn_delta_editor_t *editor;

  /* Create the repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-filename-with-cntrl-chars",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Revision 1:  Add a directory /foo  */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/foo", subpool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, subpool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  svn_pool_clear(subpool);

  /* Attempt to copy /foo to a bad path P. This should fail. */
  i = 0;
  do
    {
      p = bad_paths[i++];
      if (p == NULL)
        break;
      svn_pool_clear(subpool);

      SVN_ERR(fwcc_prepare(&editor, &edit_baton, &root_baton, repos, subpool));
      err = editor->add_directory(p, root_baton, "/foo", 1, subpool,
                                  &out_baton);
      if (!err)
        err = editor->close_edit(edit_baton, subpool);
      svn_error_clear(editor->abort_edit(edit_baton, subpool));
      SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_PATH_SYNTAX);
  } while (p);

  /* Attempt to add a file with bad path P. This should fail. */
  i = 0;
  do
    {
      p = bad_paths[i++];
      if (p == NULL)
        break;
      svn_pool_clear(subpool);

      SVN_ERR(fwcc_prepare(&editor, &edit_baton, &root_baton, repos, subpool));
      err = editor->add_file(p, root_baton, NULL, SVN_INVALID_REVNUM,
                             subpool, &out_baton);
      if (!err)
        err = editor->close_edit(edit_baton, subpool);
      svn_error_clear(editor->abort_edit(edit_baton, subpool));
      SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_PATH_SYNTAX);
  } while (p);


  /* Attempt to add a directory with bad path P. This should fail. */
  i = 0;
  do
    {
      p = bad_paths[i++];
      if (p == NULL)
        break;
      svn_pool_clear(subpool);

      SVN_ERR(fwcc_prepare(&editor, &edit_baton, &root_baton, repos, subpool));
      err = editor->add_directory(p, root_baton, NULL, SVN_INVALID_REVNUM,
                                  subpool, &out_baton);
      if (!err)
        err = editor->close_edit(edit_baton, subpool);
      svn_error_clear(editor->abort_edit(edit_baton, subpool));
      SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_PATH_SYNTAX);
  } while (p);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_repos_info(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_test_opts_t opts2;
  apr_hash_t *capabilities;
  svn_version_t *supports_version;
  svn_version_t v1_0_0 = {1, 0, 0, ""};
  svn_version_t v1_4_0 = {1, 4, 0, ""};
  int repos_format;
  svn_boolean_t is_fsx = strcmp(opts->fs_type, "fsx") == 0;

  opts2 = *opts;

  /* for repo types that have been around before 1.4 */
  if (!is_fsx)
    {
      opts2.server_minor_version = 3;
      SVN_ERR(svn_test__create_repos(&repos, "test-repo-info-3",
                                     &opts2, pool));
      SVN_ERR(svn_repos_capabilities(&capabilities, repos, pool, pool));
      SVN_TEST_ASSERT(apr_hash_count(capabilities) == 0);
      SVN_ERR(svn_repos_info_format(&repos_format, &supports_version, repos,
                                    pool, pool));
      SVN_TEST_ASSERT(repos_format == 3);
      SVN_TEST_ASSERT(svn_ver_equal(supports_version, &v1_0_0));
    }

  opts2.server_minor_version = 9;
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-info-9",
                                 &opts2, pool));
  SVN_ERR(svn_repos_capabilities(&capabilities, repos, pool, pool));
  SVN_TEST_ASSERT(apr_hash_count(capabilities) == 1);
  SVN_TEST_ASSERT(svn_hash_gets(capabilities, SVN_REPOS_CAPABILITY_MERGEINFO));
  SVN_ERR(svn_repos_info_format(&repos_format, &supports_version, repos,
                                pool, pool));
  SVN_TEST_ASSERT(repos_format == 5);
  SVN_TEST_ASSERT(svn_ver_equal(supports_version, &v1_4_0));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_config_pool(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  const char *repo_name = "test-repo-config-pool";
  svn_repos_t *repos;
  svn_stringbuf_t *cfg_buffer1, *cfg_buffer2;
  svn_config_t *cfg;
  apr_hash_t *sections1, *sections2;
  int i;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root, *rev_root;
  svn_revnum_t rev;
  const char *repo_root_url;
  const char *srcdir;
  svn_error_t *err;

  svn_repos__config_pool_t *config_pool;
  apr_pool_t *subpool = svn_pool_create(pool);

  const char *wrk_dir = svn_test_data_path("config_pool", pool);

  SVN_ERR(svn_io_make_dir_recursively(wrk_dir, pool));

  /* read all config info through a single config pool. */
  SVN_ERR(svn_repos__config_pool_create(&config_pool, TRUE, pool));

  /* have two different configurations  */
  SVN_ERR(svn_test_get_srcdir(&srcdir, opts, pool));
  SVN_ERR(svn_stringbuf_from_file2(
                        &cfg_buffer1,
                        svn_dirent_join(srcdir,
                                        "../libsvn_subr/config-test.cfg",
                                        pool),
                        pool));
  cfg_buffer2 = svn_stringbuf_dup(cfg_buffer1, pool);
  svn_stringbuf_appendcstr(cfg_buffer2, "\n[more]\nU=\"X\"\n");

  /* write them to 2x2 files */
  SVN_ERR(svn_io_write_atomic2(svn_dirent_join(wrk_dir,
                                               "config-pool-test1.cfg",
                                               pool),
                               cfg_buffer1->data, cfg_buffer1->len, NULL,
                               FALSE, pool));
  SVN_ERR(svn_io_write_atomic2(svn_dirent_join(wrk_dir,
                                               "config-pool-test2.cfg",
                                               pool),
                               cfg_buffer1->data, cfg_buffer1->len, NULL,
                               FALSE, pool));
  SVN_ERR(svn_io_write_atomic2(svn_dirent_join(wrk_dir,
                                               "config-pool-test3.cfg",
                                               pool),
                               cfg_buffer2->data, cfg_buffer2->len, NULL,
                               FALSE, pool));
  SVN_ERR(svn_io_write_atomic2(svn_dirent_join(wrk_dir,
                                               "config-pool-test4.cfg",
                                               pool),
                               cfg_buffer2->data, cfg_buffer2->len, NULL,
                               FALSE, pool));

  /* requesting a config over and over again should return the same
     (even though it is not being referenced) */
  sections1 = NULL;
  for (i = 0; i < 4; ++i)
    {
      SVN_ERR(svn_repos__config_pool_get(
                                    &cfg, config_pool,
                                    svn_dirent_join(wrk_dir,
                                                    "config-pool-test1.cfg",
                                                    pool),
                                    TRUE, NULL, subpool));

      if (sections1 == NULL)
        sections1 = cfg->sections;
      else
        SVN_TEST_ASSERT(cfg->sections == sections1);

      svn_pool_clear(subpool);
    }

  /* requesting the same config from another file should return the same
     (even though it is not being referenced) */
  for (i = 0; i < 4; ++i)
    {
      SVN_ERR(svn_repos__config_pool_get(
                                    &cfg, config_pool,
                                    svn_dirent_join(wrk_dir,
                                                    "config-pool-test2.cfg",
                                                    pool),
                                    TRUE, NULL, subpool));

      SVN_TEST_ASSERT(cfg->sections == sections1);

      svn_pool_clear(subpool);
    }

  /* reading a different configuration should return a different pointer */
  sections2 = NULL;
  for (i = 0; i < 2; ++i)
    {
      SVN_ERR(svn_repos__config_pool_get(
                                    &cfg, config_pool,
                                    svn_dirent_join(wrk_dir,
                                                    "config-pool-test3.cfg",
                                                    pool),
                                    TRUE, NULL, subpool));

      if (sections2 == NULL)
        sections2 = cfg->sections;
      else
        SVN_TEST_ASSERT(cfg->sections == sections2);

      SVN_TEST_ASSERT(sections1 != sections2);
      svn_pool_clear(subpool);
    }

  /* create an in-repo config */
  SVN_ERR(svn_dirent_get_absolute(&repo_root_url, repo_name, pool));
  SVN_ERR(svn_uri_get_file_url_from_dirent(&repo_root_url, repo_root_url,
                                           pool));

  SVN_ERR(svn_test__create_repos(&repos, repo_name, opts, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, svn_repos_fs(repos), 0, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_dir(root, "dir", pool));
  SVN_ERR(svn_fs_make_file(root, "dir/config", pool));
  SVN_ERR(svn_test__set_file_contents(root, "dir/config",
                                      cfg_buffer1->data, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* reading the config from the repo should still give cfg1 */
  SVN_ERR(svn_repos__config_pool_get(&cfg, config_pool,
                                     svn_path_url_add_component2(
                                                    repo_root_url,
                                                    "dir/config", pool),
                                     TRUE, NULL, subpool));
  SVN_TEST_ASSERT(cfg->sections == sections1);
  svn_pool_clear(subpool);

  /* create another in-repo config */
  SVN_ERR(svn_fs_begin_txn2(&txn, svn_repos_fs(repos), rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_revision_root(&rev_root, svn_repos_fs(repos), rev, pool));
  SVN_ERR(svn_fs_copy(rev_root, "dir", root, "another-dir", pool));
  SVN_ERR(svn_test__set_file_contents(root, "dir/config",
                                      cfg_buffer2->data, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &rev, txn, pool));

  /* reading the config from the repo should give cfg2 now */
  SVN_ERR(svn_repos__config_pool_get(&cfg, config_pool,
                                     svn_path_url_add_component2(
                                                    repo_root_url,
                                                    "dir/config", pool),
                                     TRUE, NULL, subpool));
  SVN_TEST_ASSERT(cfg->sections == sections2);
  svn_pool_clear(subpool);

  /* reading the copied config should still give cfg1 */
  SVN_ERR(svn_repos__config_pool_get(&cfg, config_pool,
                                     svn_path_url_add_component2(
                                                    repo_root_url,
                                                    "another-dir/config",
                                                    pool),
                                     TRUE, NULL, subpool));
  SVN_TEST_ASSERT(cfg->sections == sections1);
  svn_pool_clear(subpool);

  /* once again: repeated reads.  This triggers a different code path. */
  SVN_ERR(svn_repos__config_pool_get(&cfg, config_pool,
                                     svn_path_url_add_component2(
                                                    repo_root_url,
                                                    "dir/config", pool),
                                     TRUE, NULL, subpool));
  SVN_TEST_ASSERT(cfg->sections == sections2);
  SVN_ERR(svn_repos__config_pool_get(&cfg, config_pool,
                                     svn_path_url_add_component2(
                                                    repo_root_url,
                                                    "another-dir/config",
                                                    pool),
                                     TRUE, NULL, subpool));
  SVN_TEST_ASSERT(cfg->sections == sections1);
  svn_pool_clear(subpool);

  /* access paths that don't exist */
  SVN_TEST_ASSERT_ERROR(svn_repos__config_pool_get(&cfg, config_pool,
                          svn_path_url_add_component2(repo_root_url, "X",
                                                      pool),
                          TRUE, NULL, subpool),
                        SVN_ERR_ILLEGAL_TARGET);
  err = svn_repos__config_pool_get(&cfg, config_pool, "X.cfg", TRUE, NULL,
                                   subpool);
  SVN_TEST_ASSERT(err && APR_STATUS_IS_ENOENT(err->apr_err));
  svn_error_clear(err);
  svn_pool_clear(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_repos_fs_type(const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;

  /* Create test repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-repos_fs_type",
                                 opts, pool));

  SVN_TEST_STRING_ASSERT(svn_repos_fs_type(repos, pool), opts->fs_type);

  /* Re-open repository and verify fs-type again. */
  SVN_ERR(svn_repos_open3(&repos, svn_repos_path(repos, pool), NULL,
                          pool, pool));

  SVN_TEST_STRING_ASSERT(svn_repos_fs_type(repos, pool), opts->fs_type);

  return SVN_NO_ERROR;
}

static svn_error_t *
deprecated_access_context_api(const svn_test_opts_t *opts,
                              apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_access_t *access;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  const char *conflict;
  svn_revnum_t new_rev;
  const char *hook;

  /* Create test repository. */
  SVN_ERR(svn_test__create_repos(&repos,
                                 "test-repo-deprecated-access-context-api",
                                 opts, pool));

  /* Set an empty pre-commit hook. */
#ifdef WIN32
  hook = apr_pstrcat(pool, svn_repos_pre_commit_hook(repos, pool), ".bat",
                     SVN_VA_NULL);
  SVN_ERR(svn_io_file_create(hook,
                             "exit 0" APR_EOL_STR,
                             pool));
#else
  hook = svn_repos_pre_commit_hook(repos, pool);
  SVN_ERR(svn_io_file_create(hook,
                             "#!/bin/sh" APR_EOL_STR "exit 0" APR_EOL_STR,
                             pool));
  SVN_ERR(svn_io_set_file_executable(hook, TRUE, FALSE, pool));
#endif

  /* Set some access context using svn_fs_access_add_lock_token(). */
  SVN_ERR(svn_fs_create_access(&access, "jrandom", pool));
  SVN_ERR(svn_fs_access_add_lock_token(access, "opaquelocktoken:abc"));
  SVN_ERR(svn_fs_set_access(svn_repos_fs(repos), access));

  /* Commit a new revision. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit2(&txn, repos, 0,
                                             apr_hash_make(pool), pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_dir(root, "/whatever", pool));
  SVN_ERR(svn_repos_fs_commit_txn(&conflict, repos, &new_rev, txn, pool));

  SVN_TEST_STRING_ASSERT(conflict, NULL);
  SVN_TEST_ASSERT(new_rev == 1);

  return SVN_NO_ERROR;
}

static svn_error_t *
mkdir_delete_copy(svn_repos_t *repos,
                  const char *src,
                  const char *dst,
                  apr_pool_t *pool)
{
  svn_fs_t *fs = svn_repos_fs(repos);
  svn_revnum_t youngest_rev;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;

  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, pool));
  
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A/T", pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_delete(txn_root, "A/T", pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));

  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev - 1, pool));
  SVN_ERR(svn_fs_copy(rev_root, src, txn_root, dst, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));

  return SVN_NO_ERROR;
}

struct authz_read_baton_t {
  apr_hash_t *paths;
  apr_pool_t *pool;
  const char *deny;
};

static svn_error_t *
authz_read_func(svn_boolean_t *allowed,
                svn_fs_root_t *root,
                const char *path,
                void *baton,
                apr_pool_t *pool)
{
  struct authz_read_baton_t *b = baton;

  if (b->deny && !strcmp(b->deny, path))
    *allowed = FALSE;
  else
    *allowed = TRUE;

  svn_hash_sets(b->paths, apr_pstrdup(b->pool, path), (void*)1);

  return SVN_NO_ERROR;
}

static svn_error_t *
verify_locations(apr_hash_t *actual,
                 apr_hash_t *expected,
                 apr_hash_t *checked,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, expected); hi; hi = apr_hash_next(hi))
    {
      const svn_revnum_t *rev = apr_hash_this_key(hi);
      const char *path = apr_hash_get(actual, rev, sizeof(*rev));

      if (!path)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "expected %s for %d found (null)",
                                 (char*)apr_hash_this_val(hi), (int)*rev);
      else if (strcmp(path, apr_hash_this_val(hi)))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "expected %s for %d found %s",
                                 (char*)apr_hash_this_val(hi), (int)*rev, path);

    }

  for (hi = apr_hash_first(pool, actual); hi; hi = apr_hash_next(hi))
    {
      const svn_revnum_t *rev = apr_hash_this_key(hi);
      const char *path = apr_hash_get(expected, rev, sizeof(*rev));

      if (!path)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "found %s for %d expected (null)",
                                 (char*)apr_hash_this_val(hi), (int)*rev);
      else if (strcmp(path, apr_hash_this_val(hi)))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "found %s for %d expected %s",
                                 (char*)apr_hash_this_val(hi), (int)*rev, path);

      if (!svn_hash_gets(checked, path))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "did not check %s", path);
    }

  return SVN_NO_ERROR;
}

static void
set_expected(apr_hash_t *expected,
             svn_revnum_t rev,
             const char *path,
             apr_pool_t *pool)
{
  svn_revnum_t *rp = apr_palloc(pool, sizeof(svn_revnum_t));
  *rp = rev;
  apr_hash_set(expected, rp, sizeof(*rp), path);
}

static svn_error_t *
trace_node_locations_authz(const svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_revnum_t youngest_rev = 0;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  struct authz_read_baton_t arb;
  apr_array_header_t *revs = apr_array_make(pool, 10, sizeof(svn_revnum_t));
  apr_hash_t *locations;
  apr_hash_t *expected = apr_hash_make(pool);
  int i;

  /* Create test repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-trace-node-locations-authz",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* r1 create A */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A", pool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/f", pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "A/f", "foobar", pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));

  /* r4 copy A to B */
  SVN_ERR(mkdir_delete_copy(repos, "A", "B", pool));

  /* r7 copy B to C */
  SVN_ERR(mkdir_delete_copy(repos, "B", "C", pool));

  /* r10 copy C to D */
  SVN_ERR(mkdir_delete_copy(repos, "C", "D", pool));

  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, pool));
  SVN_ERR_ASSERT(youngest_rev == 10);

  arb.paths = apr_hash_make(pool);
  arb.pool = pool;
  arb.deny = NULL;

  apr_array_clear(revs);
  for (i = 0; i <= youngest_rev; ++i)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = i;
  set_expected(expected, 10, "/D/f", pool);
  set_expected(expected, 8, "/C/f", pool);
  set_expected(expected, 7, "/C/f", pool);
  set_expected(expected, 5, "/B/f", pool);
  set_expected(expected, 4, "/B/f", pool);
  set_expected(expected, 2, "/A/f", pool);
  set_expected(expected, 1, "/A/f", pool);
  apr_hash_clear(arb.paths);
  SVN_ERR(svn_repos_trace_node_locations(fs, &locations, "D/f", 10, revs,
                                         authz_read_func, &arb, pool));
  SVN_ERR(verify_locations(locations, expected, arb.paths, pool));

  apr_array_clear(revs);
  for (i = 1; i <= youngest_rev; ++i)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = i;
  apr_hash_clear(arb.paths);
  SVN_ERR(svn_repos_trace_node_locations(fs, &locations, "D/f", 10, revs,
                                         authz_read_func, &arb, pool));
  SVN_ERR(verify_locations(locations, expected, arb.paths, pool));

  apr_array_clear(revs);
  for (i = 2; i <= youngest_rev; ++i)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = i;
  set_expected(expected, 1, NULL, pool);
  apr_hash_clear(arb.paths);
  SVN_ERR(svn_repos_trace_node_locations(fs, &locations, "D/f", 10, revs,
                                         authz_read_func, &arb, pool));
  SVN_ERR(verify_locations(locations, expected, arb.paths, pool));

  apr_array_clear(revs);
  for (i = 3; i <= youngest_rev; ++i)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = i;
  set_expected(expected, 2, NULL, pool);
  apr_hash_clear(arb.paths);
  SVN_ERR(svn_repos_trace_node_locations(fs, &locations, "D/f", 10, revs,
                                         authz_read_func, &arb, pool));
  SVN_ERR(verify_locations(locations, expected, arb.paths, pool));

  apr_array_clear(revs);
  for (i = 6; i <= youngest_rev; ++i)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = i;
  set_expected(expected, 5, NULL, pool);
  set_expected(expected, 4, NULL, pool);
  apr_hash_clear(arb.paths);
  SVN_ERR(svn_repos_trace_node_locations(fs, &locations, "D/f", 10, revs,
                                         authz_read_func, &arb, pool));
  SVN_ERR(verify_locations(locations, expected, arb.paths, pool));

  arb.deny = "/B/f";
  apr_array_clear(revs);
  for (i = 0; i <= youngest_rev; ++i)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = i;
  apr_hash_clear(arb.paths);
  SVN_ERR(svn_repos_trace_node_locations(fs, &locations, "D/f", 10, revs,
                                         authz_read_func, &arb, pool));
  SVN_ERR(verify_locations(locations, expected, arb.paths, pool));

  apr_array_clear(revs);
  for (i = 6; i <= youngest_rev; ++i)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = i;
  apr_hash_clear(arb.paths);
  SVN_ERR(svn_repos_trace_node_locations(fs, &locations, "D/f", 10, revs,
                                         authz_read_func, &arb, pool));
  SVN_ERR(verify_locations(locations, expected, arb.paths, pool));

  APR_ARRAY_PUSH(revs, svn_revnum_t) = 0;
  apr_hash_clear(arb.paths);
  SVN_ERR(svn_repos_trace_node_locations(fs, &locations, "D/f", 10, revs,
                                         authz_read_func, &arb, pool));
  SVN_ERR(verify_locations(locations, expected, arb.paths, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
commit_aborted_txn(const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t new_rev;
  svn_revnum_t youngest_rev;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-commit-aborted-txn",
                                 opts, pool));

  /* Create and abort the transaction. */
  SVN_ERR(svn_repos_fs_begin_txn_for_commit2(&txn, repos, 0,
                                             apr_hash_make(pool), pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/A", pool));
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* Committing the aborted transaction should fail. */
  SVN_TEST_ASSERT_ANY_ERROR(svn_repos_fs_commit_txn(&conflict, repos,
                                                    &new_rev, txn, pool));

  /* Ensure that output arguments follow svn_repos_fs_commit_txn()'s
     contract -- NEW_REV should be set to SVN_INVALID_REVNUM and
     CONFLICT should be NULL. */
  SVN_TEST_ASSERT(new_rev == SVN_INVALID_REVNUM);
  SVN_TEST_ASSERT(conflict == NULL);

  /* Re-open repository and verify that it's still empty. */
  SVN_ERR(svn_repos_open3(&repos, svn_repos_path(repos, pool), NULL,
                          pool, pool));
  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, svn_repos_fs(repos), pool));
  SVN_TEST_ASSERT(youngest_rev == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
list_callback(const char *path,
              svn_dirent_t *dirent,
              void *baton,
              apr_pool_t *pool)
{
  *(int *)baton += 1;

  return SVN_NO_ERROR;
}


static svn_error_t *
test_list(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  svn_revnum_t youngest_rev;
  int counter = 0;
  apr_array_header_t *patterns;

  /* Create yet another greek tree repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-repo-list", opts, pool));
  fs = svn_repos_fs(repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* List all nodes under /A that contain an 'a'. */

  patterns = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(patterns, const char *) = "*a*";
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, pool));
  SVN_ERR(svn_repos_list(rev_root, "/A", patterns, svn_depth_infinity, FALSE,
                         NULL, NULL, list_callback, &counter, NULL, NULL,
                         pool));
  SVN_TEST_ASSERT(counter == 6);

  return SVN_NO_ERROR;
}

/* The test table.  */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(dir_deltas,
                       "test svn_repos_dir_delta2"),
    SVN_TEST_OPTS_PASS(node_tree_delete_under_copy,
                       "test deletions under copies in node_tree code"),
    SVN_TEST_OPTS_PASS(revisions_changed,
                       "test svn_repos_history() (partially)"),
    SVN_TEST_OPTS_PASS(node_locations,
                       "test svn_repos_node_locations"),
    SVN_TEST_OPTS_PASS(node_locations2,
                       "test svn_repos_node_locations some more"),
    SVN_TEST_OPTS_PASS(rmlocks,
                       "test removal of defunct locks"),
    SVN_TEST_PASS2(authz,
                   "test authz access control"),
    SVN_TEST_OPTS_PASS(in_repo_authz,
                       "test authz stored in the repo"),
    SVN_TEST_OPTS_PASS(in_repo_groups_authz,
                       "test authz and global groups stored in the repo"),
    SVN_TEST_OPTS_PASS(groups_authz,
                       "test authz with global groups"),
    SVN_TEST_OPTS_PASS(commit_editor_authz,
                       "test authz in the commit editor"),
    SVN_TEST_OPTS_PASS(commit_continue_txn,
                       "test commit with explicit txn"),
    SVN_TEST_OPTS_PASS(node_location_segments,
                       "test svn_repos_node_location_segments"),
    SVN_TEST_OPTS_PASS(reporter_depth_exclude,
                       "test reporter and svn_depth_exclude"),
    SVN_TEST_OPTS_PASS(prop_validation,
                       "test if revprops are validated by repos"),
    SVN_TEST_OPTS_PASS(get_logs,
                       "test svn_repos_get_logs ranges and limits"),
    SVN_TEST_OPTS_PASS(test_get_file_revs,
                       "test svn_repos_get_file_revsN"),
    SVN_TEST_OPTS_PASS(issue_4060,
                       "test issue 4060"),
    SVN_TEST_OPTS_PASS(test_delete_repos,
                       "test svn_repos_delete"),
    SVN_TEST_OPTS_PASS(filename_with_control_chars,
                       "test filenames with control characters"),
    SVN_TEST_OPTS_PASS(test_repos_info,
                       "test svn_repos_info_*"),
    SVN_TEST_OPTS_PASS(test_config_pool,
                       "test svn_repos__config_pool_*"),
    SVN_TEST_OPTS_PASS(test_repos_fs_type,
                       "test test_repos_fs_type"),
    SVN_TEST_OPTS_PASS(deprecated_access_context_api,
                       "test deprecated access context api"),
    SVN_TEST_OPTS_PASS(trace_node_locations_authz,
                       "authz for svn_repos_trace_node_locations"),
    SVN_TEST_OPTS_PASS(commit_aborted_txn,
                       "test committing a previously aborted txn"),
    SVN_TEST_PASS2(test_authz_prefixes,
                   "test authz prefixes"),
    SVN_TEST_PASS2(test_authz_recursive_override,
                   "test recursively authz rule override"),
    SVN_TEST_PASS2(test_authz_pattern_tests,
                   "test various basic authz pattern combinations"),
    SVN_TEST_PASS2(test_authz_wildcards,
                   "test the different types of authz wildcards"),
    SVN_TEST_SKIP2(test_authz_wildcard_performance, TRUE,
                   "optional authz wildcard performance test"),
    SVN_TEST_OPTS_PASS(test_list,
                       "test svn_repos_list"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
