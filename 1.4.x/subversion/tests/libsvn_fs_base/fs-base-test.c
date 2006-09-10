/* fs-test.c --- tests for the filesystem
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

#include "svn_pools.h"
#include "svn_time.h"
#include "svn_string.h"
#include "svn_fs.h"
#include "svn_md5.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"

#include "../../libsvn_fs_base/id.h"
#include "../../libsvn_fs_base/trail.h"
#include "../../libsvn_fs_base/bdb/txn-table.h"
#include "../../libsvn_fs_base/bdb/nodes-table.h"

#include "../../libsvn_delta/delta.h"

#define SET_STR(ps, s) ((ps)->data = (s), (ps)->len = strlen(s))


/*-----------------------------------------------------------------*/

/** The actual fs-tests called by `make check` **/

/* Create a filesystem.  */
static svn_error_t *
create_berkeley_filesystem(const char **msg,
                           svn_boolean_t msg_only,
                           svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  svn_fs_t *fs;

  *msg = "svn_fs_create_berkeley";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create and close a repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-create-berkeley",
                              "bdb", pool));
  
  return SVN_NO_ERROR;
}


/* Generic Berkeley DB error handler function. */
static void
berkeley_error_handler(const char *errpfx, char *msg)
{
  fprintf(stderr, "%s%s\n", errpfx ? errpfx : "", msg);
}


/* Open an existing filesystem.  */
static svn_error_t *
open_berkeley_filesystem(const char **msg,
                         svn_boolean_t msg_only,
                         svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  svn_fs_t *fs, *fs2;

  *msg = "open an existing Berkeley DB filesystem";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create and close a repository (using fs). */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-open-berkeley",
                              "bdb", pool));

  /* Create a different fs object, and use it to re-open the
     repository again.  */
  SVN_ERR(svn_test__fs_new(&fs2, pool));
  SVN_ERR(svn_fs_open_berkeley(fs2, "test-repo-open-berkeley"));

  /* Provide a handler for Berkeley DB error messages.  */
  SVN_ERR(svn_fs_set_berkeley_errcall(fs2, berkeley_error_handler));

  return SVN_NO_ERROR;
}


/* Set *PRESENT to true if entry NAME is present in directory PATH
   under ROOT, else set *PRESENT to false. */
static svn_error_t *
check_entry(svn_fs_root_t *root,
            const char *path,
            const char *name,
            svn_boolean_t *present,
            apr_pool_t *pool)
{
  apr_hash_t *entries;
  svn_fs_dirent_t *ent;

  SVN_ERR(svn_fs_dir_entries(&entries, root, path, pool));
  ent = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  if (ent)
    *present = TRUE;
  else
    *present = FALSE;

  return SVN_NO_ERROR;
}


/* Return an error if entry NAME is absent in directory PATH under ROOT. */
static svn_error_t *
check_entry_present(svn_fs_root_t *root, const char *path, 
                    const char *name, apr_pool_t *pool)
{
  svn_boolean_t present;
  SVN_ERR(check_entry(root, path, name, &present, pool));

  if (! present)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "entry \"%s\" absent when it should be present", name);

  return SVN_NO_ERROR;
}


/* Return an error if entry NAME is present in directory PATH under ROOT. */
static svn_error_t *
check_entry_absent(svn_fs_root_t *root, const char *path, 
                   const char *name, apr_pool_t *pool)
{
  svn_boolean_t present;
  SVN_ERR(check_entry(root, path, name, &present, pool));

  if (present)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       "entry \"%s\" present when it should be absent", name);

  return SVN_NO_ERROR;
}


struct check_id_args
{
  svn_fs_t *fs;
  const svn_fs_id_t *id;
  svn_boolean_t present;
};


static svn_error_t *
txn_body_check_id(void *baton, trail_t *trail)
{
  struct check_id_args *args = baton;
  node_revision_t *noderev;
  svn_error_t *err;

  err = svn_fs_bdb__get_node_revision(&noderev, args->fs, args->id, 
                                      trail, trail->pool);

  if (err && (err->apr_err == SVN_ERR_FS_ID_NOT_FOUND))
    args->present = FALSE;
  else if (! err)
    args->present = TRUE;
  else
    {
      svn_string_t *id_str = svn_fs_unparse_id(args->id, trail->pool);
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, err,
         "error looking for node revision id \"%s\"", id_str->data);
    }
  svn_error_clear(err);

  return SVN_NO_ERROR;
}


/* Set *PRESENT to true if node revision ID is present in filesystem
   FS, else set *PRESENT to false. */
static svn_error_t *
check_id(svn_fs_t *fs, const svn_fs_id_t *id, svn_boolean_t *present,
         apr_pool_t *pool)
{
  struct check_id_args args;

  args.id = id;
  args.fs = fs;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_check_id, &args, pool));

  if (args.present)
    *present = TRUE;
  else
    *present = FALSE;

  return SVN_NO_ERROR;
}


/* Return error if node revision ID is not present in FS. */
static svn_error_t *
check_id_present(svn_fs_t *fs, const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_boolean_t present;
  SVN_ERR(check_id(fs, id, &present, pool));

  if (! present)
    {
      svn_string_t *id_str = svn_fs_unparse_id(id, pool);
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         "node revision id \"%s\" absent when should be present",
         id_str->data);
    }

  return SVN_NO_ERROR;
}


/* Return error if node revision ID is present in FS. */
static svn_error_t *
check_id_absent(svn_fs_t *fs, const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_boolean_t present;
  SVN_ERR(check_id(fs, id, &present, pool));

  if (present)
    {
      svn_string_t *id_str = svn_fs_unparse_id(id, pool);
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         "node revision id \"%s\" present when should be absent",
         id_str->data);
    }

  return SVN_NO_ERROR;
}


/* Test that aborting a Subversion transaction works.

   NOTE: This function tests internal filesystem interfaces, not just
   the public filesystem interface.  */
static svn_error_t *
abort_txn(const char **msg,
          svn_boolean_t msg_only,
          svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn1, *txn2;
  svn_fs_root_t *txn1_root, *txn2_root;
  const char *txn1_name, *txn2_name;

  *msg = "abort a transaction";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare two txns to receive the Greek tree. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-abort-txn",
                              "bdb", pool));
  SVN_ERR(svn_fs_begin_txn(&txn1, fs, 0, pool));
  SVN_ERR(svn_fs_begin_txn(&txn2, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn1_root, txn1, pool));
  SVN_ERR(svn_fs_txn_root(&txn2_root, txn2, pool));

  /* Save their names for later. */
  SVN_ERR(svn_fs_txn_name(&txn1_name, txn1, pool));
  SVN_ERR(svn_fs_txn_name(&txn2_name, txn2, pool));
  
  /* Create greek trees in them. */
  SVN_ERR(svn_test__create_greek_tree(txn1_root, pool));
  SVN_ERR(svn_test__create_greek_tree(txn2_root, pool));

  /* The test is to abort txn2, while leaving txn1.
   *
   * After we abort txn2, we make sure that a) all of its nodes
   * disappeared from the database, and b) none of txn1's nodes
   * disappeared.
   *
   * Finally, we create a third txn, and check that the name it got is
   * different from the names of txn1 and txn2.
   */

  {
    /* Yes, I really am this paranoid. */

    /* IDs for every file in the standard Greek Tree. */
    const svn_fs_id_t
      *t1_root_id,    *t2_root_id,
      *t1_iota_id,    *t2_iota_id,
      *t1_A_id,       *t2_A_id,
      *t1_mu_id,      *t2_mu_id,
      *t1_B_id,       *t2_B_id,
      *t1_lambda_id,  *t2_lambda_id,
      *t1_E_id,       *t2_E_id,
      *t1_alpha_id,   *t2_alpha_id,
      *t1_beta_id,    *t2_beta_id,
      *t1_F_id,       *t2_F_id,
      *t1_C_id,       *t2_C_id,
      *t1_D_id,       *t2_D_id,
      *t1_gamma_id,   *t2_gamma_id,
      *t1_H_id,       *t2_H_id,
      *t1_chi_id,     *t2_chi_id,
      *t1_psi_id,     *t2_psi_id,
      *t1_omega_id,   *t2_omega_id,
      *t1_G_id,       *t2_G_id,
      *t1_pi_id,      *t2_pi_id,
      *t1_rho_id,     *t2_rho_id,
      *t1_tau_id,     *t2_tau_id;
    
    SVN_ERR(svn_fs_node_id(&t1_root_id, txn1_root, "", pool));
    SVN_ERR(svn_fs_node_id(&t2_root_id, txn2_root, "", pool));
    SVN_ERR(svn_fs_node_id(&t1_iota_id, txn1_root, "iota", pool));
    SVN_ERR(svn_fs_node_id(&t2_iota_id, txn2_root, "iota", pool));
    SVN_ERR(svn_fs_node_id(&t1_A_id, txn1_root, "/A", pool));
    SVN_ERR(svn_fs_node_id(&t2_A_id, txn2_root, "/A", pool));
    SVN_ERR(svn_fs_node_id(&t1_mu_id, txn1_root, "/A/mu", pool));
    SVN_ERR(svn_fs_node_id(&t2_mu_id, txn2_root, "/A/mu", pool));
    SVN_ERR(svn_fs_node_id(&t1_B_id, txn1_root, "/A/B", pool));
    SVN_ERR(svn_fs_node_id(&t2_B_id, txn2_root, "/A/B", pool));
    SVN_ERR(svn_fs_node_id(&t1_lambda_id, txn1_root, "/A/B/lambda", pool));
    SVN_ERR(svn_fs_node_id(&t2_lambda_id, txn2_root, "/A/B/lambda", pool));
    SVN_ERR(svn_fs_node_id(&t1_E_id, txn1_root, "/A/B/E", pool));
    SVN_ERR(svn_fs_node_id(&t2_E_id, txn2_root, "/A/B/E", pool));
    SVN_ERR(svn_fs_node_id(&t1_alpha_id, txn1_root, "/A/B/E/alpha", pool));
    SVN_ERR(svn_fs_node_id(&t2_alpha_id, txn2_root, "/A/B/E/alpha", pool));
    SVN_ERR(svn_fs_node_id(&t1_beta_id, txn1_root, "/A/B/E/beta", pool));
    SVN_ERR(svn_fs_node_id(&t2_beta_id, txn2_root, "/A/B/E/beta", pool));
    SVN_ERR(svn_fs_node_id(&t1_F_id, txn1_root, "/A/B/F", pool));
    SVN_ERR(svn_fs_node_id(&t2_F_id, txn2_root, "/A/B/F", pool));
    SVN_ERR(svn_fs_node_id(&t1_C_id, txn1_root, "/A/C", pool));
    SVN_ERR(svn_fs_node_id(&t2_C_id, txn2_root, "/A/C", pool));
    SVN_ERR(svn_fs_node_id(&t1_D_id, txn1_root, "/A/D", pool));
    SVN_ERR(svn_fs_node_id(&t2_D_id, txn2_root, "/A/D", pool));
    SVN_ERR(svn_fs_node_id(&t1_gamma_id, txn1_root, "/A/D/gamma", pool));
    SVN_ERR(svn_fs_node_id(&t2_gamma_id, txn2_root, "/A/D/gamma", pool));
    SVN_ERR(svn_fs_node_id(&t1_H_id, txn1_root, "/A/D/H", pool));
    SVN_ERR(svn_fs_node_id(&t2_H_id, txn2_root, "/A/D/H", pool));
    SVN_ERR(svn_fs_node_id(&t1_chi_id, txn1_root, "/A/D/H/chi", pool));
    SVN_ERR(svn_fs_node_id(&t2_chi_id, txn2_root, "/A/D/H/chi", pool));
    SVN_ERR(svn_fs_node_id(&t1_psi_id, txn1_root, "/A/D/H/psi", pool));
    SVN_ERR(svn_fs_node_id(&t2_psi_id, txn2_root, "/A/D/H/psi", pool));
    SVN_ERR(svn_fs_node_id(&t1_omega_id, txn1_root, "/A/D/H/omega", pool));
    SVN_ERR(svn_fs_node_id(&t2_omega_id, txn2_root, "/A/D/H/omega", pool));
    SVN_ERR(svn_fs_node_id(&t1_G_id, txn1_root, "/A/D/G", pool));
    SVN_ERR(svn_fs_node_id(&t2_G_id, txn2_root, "/A/D/G", pool));
    SVN_ERR(svn_fs_node_id(&t1_pi_id, txn1_root, "/A/D/G/pi", pool));
    SVN_ERR(svn_fs_node_id(&t2_pi_id, txn2_root, "/A/D/G/pi", pool));
    SVN_ERR(svn_fs_node_id(&t1_rho_id, txn1_root, "/A/D/G/rho", pool));
    SVN_ERR(svn_fs_node_id(&t2_rho_id, txn2_root, "/A/D/G/rho", pool));
    SVN_ERR(svn_fs_node_id(&t1_tau_id, txn1_root, "/A/D/G/tau", pool));
    SVN_ERR(svn_fs_node_id(&t2_tau_id, txn2_root, "/A/D/G/tau", pool));

    /* Abort just txn2. */
    SVN_ERR(svn_fs_abort_txn(txn2, pool));

    /* Now test that all the nodes in txn2 at the time of the abort
     * are gone, but all of the ones in txn1 are still there. 
     */

    /* Check that every node rev in t2 has vanished from the fs. */
    SVN_ERR(check_id_absent(fs, t2_root_id, pool));
    SVN_ERR(check_id_absent(fs, t2_iota_id, pool));
    SVN_ERR(check_id_absent(fs, t2_A_id, pool));
    SVN_ERR(check_id_absent(fs, t2_mu_id, pool));
    SVN_ERR(check_id_absent(fs, t2_B_id, pool));
    SVN_ERR(check_id_absent(fs, t2_lambda_id, pool));
    SVN_ERR(check_id_absent(fs, t2_E_id, pool));
    SVN_ERR(check_id_absent(fs, t2_alpha_id, pool));
    SVN_ERR(check_id_absent(fs, t2_beta_id, pool));
    SVN_ERR(check_id_absent(fs, t2_F_id, pool));
    SVN_ERR(check_id_absent(fs, t2_C_id, pool));
    SVN_ERR(check_id_absent(fs, t2_D_id, pool));
    SVN_ERR(check_id_absent(fs, t2_gamma_id, pool));
    SVN_ERR(check_id_absent(fs, t2_H_id, pool));
    SVN_ERR(check_id_absent(fs, t2_chi_id, pool));
    SVN_ERR(check_id_absent(fs, t2_psi_id, pool));
    SVN_ERR(check_id_absent(fs, t2_omega_id, pool));
    SVN_ERR(check_id_absent(fs, t2_G_id, pool));
    SVN_ERR(check_id_absent(fs, t2_pi_id, pool));
    SVN_ERR(check_id_absent(fs, t2_rho_id, pool));
    SVN_ERR(check_id_absent(fs, t2_tau_id, pool));
    
    /* Check that every node rev in t1 is still in the fs. */
    SVN_ERR(check_id_present(fs, t1_root_id, pool));
    SVN_ERR(check_id_present(fs, t1_iota_id, pool));
    SVN_ERR(check_id_present(fs, t1_A_id, pool));
    SVN_ERR(check_id_present(fs, t1_mu_id, pool));
    SVN_ERR(check_id_present(fs, t1_B_id, pool));
    SVN_ERR(check_id_present(fs, t1_lambda_id, pool));
    SVN_ERR(check_id_present(fs, t1_E_id, pool));
    SVN_ERR(check_id_present(fs, t1_alpha_id, pool));
    SVN_ERR(check_id_present(fs, t1_beta_id, pool));
    SVN_ERR(check_id_present(fs, t1_F_id, pool));
    SVN_ERR(check_id_present(fs, t1_C_id, pool));
    SVN_ERR(check_id_present(fs, t1_D_id, pool));
    SVN_ERR(check_id_present(fs, t1_gamma_id, pool));
    SVN_ERR(check_id_present(fs, t1_H_id, pool));
    SVN_ERR(check_id_present(fs, t1_chi_id, pool));
    SVN_ERR(check_id_present(fs, t1_psi_id, pool));
    SVN_ERR(check_id_present(fs, t1_omega_id, pool));
    SVN_ERR(check_id_present(fs, t1_G_id, pool));
    SVN_ERR(check_id_present(fs, t1_pi_id, pool));
    SVN_ERR(check_id_present(fs, t1_rho_id, pool));
    SVN_ERR(check_id_present(fs, t1_tau_id, pool));
  }

  /* Test that txn2 itself is gone, by trying to open it. */
  {
    svn_fs_txn_t *txn2_again;
    svn_error_t *err;

    err = svn_fs_open_txn(&txn2_again, fs, txn2_name, pool);
    if (err && (err->apr_err != SVN_ERR_FS_NO_SUCH_TRANSACTION))
      {
        return svn_error_create
          (SVN_ERR_FS_GENERAL, err,
           "opening non-existent txn got wrong error");
      }
    else if (! err)
      {
        return svn_error_create
          (SVN_ERR_FS_GENERAL, NULL,
           "opening non-existent txn failed to get error");
      }
    svn_error_clear(err);
  }

  /* Test that txn names are not recycled, by opening a new txn.  */
  {
    svn_fs_txn_t *txn3;
    const char *txn3_name;

    SVN_ERR(svn_fs_begin_txn(&txn3, fs, 0, pool));
    SVN_ERR(svn_fs_txn_name(&txn3_name, txn3, pool));

    if ((strcmp(txn3_name, txn2_name) == 0)
        || (strcmp(txn3_name, txn1_name) == 0))
      {
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           "txn name \"%s\" was recycled", txn3_name);
      }
  }

  /* Test that aborting a txn that's already committed fails. */
  {
    svn_fs_txn_t *txn4;
    const char *txn4_name;
    svn_revnum_t new_rev;
    const char *conflict;
    svn_error_t *err;

    SVN_ERR(svn_fs_begin_txn(&txn4, fs, 0, pool));
    SVN_ERR(svn_fs_txn_name(&txn4_name, txn4, pool));
    SVN_ERR(svn_fs_commit_txn(&conflict, &new_rev, txn4, pool));
    err = svn_fs_abort_txn(txn4, pool);
    if (! err)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, NULL,
         "expected error trying to abort a committed txn; got none");
    else if (err->apr_err != SVN_ERR_FS_TRANSACTION_NOT_MUTABLE)
      return svn_error_create
        (SVN_ERR_FS_GENERAL, err,
         "got an unexpected error trying to abort a committed txn");
    else
      svn_error_clear(err);
  }
  return SVN_NO_ERROR;
}


/* This tests deleting of mutable nodes.  We build a tree in a
 * transaction, then try to delete various items in the tree.  We
 * never commit the tree, so every entry being deleted points to a 
 * mutable node. 
 * 
 * ### todo: this test was written before commits worked.  It might
 * now be worthwhile to combine it with delete().
 */
static svn_error_t *
delete_mutables(const char **msg,
                svn_boolean_t msg_only,
                svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_error_t *err;

  *msg = "delete mutable nodes from directories";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-del-from-dir",
                              "bdb", pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  
  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  /* Baby, it's time to test like you've never tested before.  We do
   * the following, in this order:
   *
   *    1. Delete a single file somewhere, succeed.
   *    2. Delete two files of three, then make sure the third remains.
   *    3. Delete the third and last file.
   *    4. Try again to delete the dir, succeed.
   *    5. Delete one of the natively empty dirs, succeed.
   *    6. Try to delete root, fail.
   *    7. Try to delete a top-level file, succeed.
   *
   * Specifically, that's:
   *
   *    1. Delete A/D/gamma.
   *    2. Delete A/D/G/pi, A/D/G/rho.
   *    3. Delete A/D/G/tau.
   *    4. Try again to delete A/D/G, succeed.
   *    5. Delete A/C.
   *    6. Try to delete /, fail.
   *    7. Try to delete iota, succeed.
   *
   * Before and after each deletion or attempted deletion, we probe
   * the affected directory, to make sure everything is as it should
   * be.
   */

  /* 1 */
  {
    const svn_fs_id_t *gamma_id;
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "A/D/gamma", pool));

    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(check_id_present(fs, gamma_id, pool));

    SVN_ERR(svn_fs_delete(txn_root, "A/D/gamma", pool));

    SVN_ERR(check_entry_absent(txn_root, "A/D", "gamma", pool));
    SVN_ERR(check_id_absent(fs, gamma_id, pool));
  }

  /* 2 */
  {
    const svn_fs_id_t *pi_id, *rho_id, *tau_id;
    SVN_ERR(svn_fs_node_id(&pi_id, txn_root, "A/D/G/pi", pool));
    SVN_ERR(svn_fs_node_id(&rho_id, txn_root, "A/D/G/rho", pool));
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "A/D/G/tau", pool));

    SVN_ERR(check_entry_present(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(check_id_present(fs, pi_id, pool));
    SVN_ERR(check_id_present(fs, rho_id, pool));
    SVN_ERR(check_id_present(fs, tau_id, pool));

    SVN_ERR(svn_fs_delete(txn_root, "A/D/G/pi", pool));

    SVN_ERR(check_entry_absent(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(check_id_absent(fs, pi_id, pool));
    SVN_ERR(check_id_present(fs, rho_id, pool));
    SVN_ERR(check_id_present(fs, tau_id, pool));

    SVN_ERR(svn_fs_delete(txn_root, "A/D/G/rho", pool));

    SVN_ERR(check_entry_absent(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(check_id_absent(fs, pi_id, pool));
    SVN_ERR(check_id_absent(fs, rho_id, pool));
    SVN_ERR(check_id_present(fs, tau_id, pool));
  }

  /* 3 */
  {
    const svn_fs_id_t *tau_id;
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "A/D/G/tau", pool));

    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(check_id_present(fs, tau_id, pool));

    SVN_ERR(svn_fs_delete(txn_root, "A/D/G/tau", pool));

    SVN_ERR(check_entry_absent(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(check_id_absent(fs, tau_id, pool));
  }

  /* 4 */
  {
    const svn_fs_id_t *G_id;
    SVN_ERR(svn_fs_node_id(&G_id, txn_root, "A/D/G", pool));

    SVN_ERR(check_entry_present(txn_root, "A/D", "G", pool));
    SVN_ERR(check_id_present(fs, G_id, pool));

    SVN_ERR(svn_fs_delete(txn_root, "A/D/G", pool));        /* succeed */

    SVN_ERR(check_entry_absent(txn_root, "A/D", "G", pool));
    SVN_ERR(check_id_absent(fs, G_id, pool));
  }

  /* 5 */
  {
    const svn_fs_id_t *C_id;
    SVN_ERR(svn_fs_node_id(&C_id, txn_root, "A/C", pool));

    SVN_ERR(check_entry_present(txn_root, "A", "C", pool));
    SVN_ERR(check_id_present(fs, C_id, pool));

    SVN_ERR(svn_fs_delete(txn_root, "A/C", pool));

    SVN_ERR(check_entry_absent(txn_root, "A", "C", pool));
    SVN_ERR(check_id_absent(fs, C_id, pool));
  }

  /* 6 */
  {
    const svn_fs_id_t *root_id;
    SVN_ERR(svn_fs_node_id(&root_id, txn_root, "", pool));

    err = svn_fs_delete(txn_root, "", pool);

    if (err && (err->apr_err != SVN_ERR_FS_ROOT_DIR))
      {
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, err,
           "deleting root directory got wrong error");
      }
    else if (! err)
      {
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, NULL,
           "deleting root directory failed to get error");
      }
    svn_error_clear(err);

    SVN_ERR(check_id_present(fs, root_id, pool));
  }

  /* 7 */
  {
    const svn_fs_id_t *iota_id;
    SVN_ERR(svn_fs_node_id(&iota_id, txn_root, "iota", pool));

    SVN_ERR(check_entry_present(txn_root, "", "iota", pool));
    SVN_ERR(check_id_present(fs, iota_id, pool));

    SVN_ERR(svn_fs_delete(txn_root, "iota", pool));

    SVN_ERR(check_entry_absent(txn_root, "", "iota", pool));
    SVN_ERR(check_id_absent(fs, iota_id, pool));
  }

  return SVN_NO_ERROR;
}


/* This tests deleting in general.
 * 
 * ### todo: this test was written after (and independently of)
 * delete_mutables().  It might be worthwhile to combine them.
 */
static svn_error_t *
delete(const char **msg,
       svn_boolean_t msg_only,
       svn_test_opts_t *opts,
       apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t new_rev;

  *msg = "delete nodes tree";

  if (msg_only)
    return SVN_NO_ERROR;

  /* This function tests 5 cases:
   *
   * 1. Delete mutable file.
   * 2. Delete mutable directory.
   * 3. Delete mutable directory with immutable nodes.
   * 4. Delete immutable file.
   * 5. Delete immutable directory.
   */

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-del-tree",
                              "bdb", pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  /* 1. Delete mutable file. */
  {
    const svn_fs_id_t *iota_id, *gamma_id;
    static svn_test__tree_entry_t expected_entries[] = {
      /* path, contents (0 = dir) */
      { "A",           0 },
      { "A/mu",        "This is the file 'mu'.\n" },
      { "A/B",         0 },
      { "A/B/lambda",  "This is the file 'lambda'.\n" },
      { "A/B/E",       0 },
      { "A/B/E/alpha", "This is the file 'alpha'.\n" },
      { "A/B/E/beta",  "This is the file 'beta'.\n" },
      { "A/C",         0 },
      { "A/B/F",       0 },
      { "A/D",         0 },
      { "A/D/G",       0 },
      { "A/D/G/pi",    "This is the file 'pi'.\n" },
      { "A/D/G/rho",   "This is the file 'rho'.\n" },
      { "A/D/G/tau",   "This is the file 'tau'.\n" },
      { "A/D/H",       0 },
      { "A/D/H/chi",   "This is the file 'chi'.\n" },
      { "A/D/H/psi",   "This is the file 'psi'.\n" },
      { "A/D/H/omega", "This is the file 'omega'.\n" }
    };

    /* Check nodes revision ID is gone.  */
    SVN_ERR(svn_fs_node_id(&iota_id, txn_root, "iota", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "A/D/gamma", pool));

    SVN_ERR(check_entry_present(txn_root, "", "iota", pool));
    SVN_ERR(check_id_present(fs, iota_id, pool));
    SVN_ERR(check_id_present(fs, gamma_id, pool));

    /* Try deleting mutable files. */
    SVN_ERR(svn_fs_delete(txn_root, "iota", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/gamma", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "iota", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D", "gamma", pool));
    SVN_ERR(check_id_absent(fs, iota_id, pool));
    SVN_ERR(check_id_absent(fs, gamma_id, pool));

    /* Validate the tree.  */
    SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 18, pool));
  }
  /* Abort transaction.  */
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* 2. Delete mutable directory. */

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  {
    const svn_fs_id_t *A_id, *mu_id, *B_id, *lambda_id, *E_id, *alpha_id,
      *beta_id, *F_id, *C_id, *D_id, *gamma_id, *H_id, *chi_id,
      *psi_id, *omega_id, *G_id, *pi_id, *rho_id, *tau_id;

    /* Check nodes revision ID is gone.  */
    SVN_ERR(svn_fs_node_id(&A_id, txn_root, "/A", pool));
    SVN_ERR(check_entry_present(txn_root, "", "A", pool));
    SVN_ERR(svn_fs_node_id(&mu_id, txn_root, "/A/mu", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "mu", pool));
    SVN_ERR(svn_fs_node_id(&B_id, txn_root, "/A/B", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "B", pool));
    SVN_ERR(svn_fs_node_id(&lambda_id, txn_root, "/A/B/lambda", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "lambda", pool));
    SVN_ERR(svn_fs_node_id(&E_id, txn_root, "/A/B/E", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "E", pool));
    SVN_ERR(svn_fs_node_id(&alpha_id, txn_root, "/A/B/E/alpha", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "alpha", pool));
    SVN_ERR(svn_fs_node_id(&beta_id, txn_root, "/A/B/E/beta", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "beta", pool));
    SVN_ERR(svn_fs_node_id(&F_id, txn_root, "/A/B/F", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "F", pool));
    SVN_ERR(svn_fs_node_id(&C_id, txn_root, "/A/C", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "C", pool));
    SVN_ERR(svn_fs_node_id(&D_id, txn_root, "/A/D", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "D", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "/A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(svn_fs_node_id(&H_id, txn_root, "/A/D/H", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "H", pool));
    SVN_ERR(svn_fs_node_id(&chi_id, txn_root, "/A/D/H/chi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "chi", pool));
    SVN_ERR(svn_fs_node_id(&psi_id, txn_root, "/A/D/H/psi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "psi", pool));
    SVN_ERR(svn_fs_node_id(&omega_id, txn_root, "/A/D/H/omega", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "omega", pool));
    SVN_ERR(svn_fs_node_id(&G_id, txn_root, "/A/D/G", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "G", pool));
    SVN_ERR(svn_fs_node_id(&pi_id, txn_root, "/A/D/G/pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(svn_fs_node_id(&rho_id, txn_root, "/A/D/G/rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "/A/D/G/tau", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));

    /* Try deleting a mutable empty dir. */
    SVN_ERR(svn_fs_delete(txn_root, "A/C", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/B/F", pool));
    SVN_ERR(check_entry_absent(txn_root, "A", "C", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/B", "F", pool));
    SVN_ERR(check_id_absent(fs, C_id, pool));
    SVN_ERR(check_id_absent(fs, F_id, pool));

    /* Now delete a mutable non-empty dir. */
    SVN_ERR(svn_fs_delete(txn_root, "A", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "A", pool));
    SVN_ERR(check_id_absent(fs, A_id, pool));
    SVN_ERR(check_id_absent(fs, mu_id, pool));
    SVN_ERR(check_id_absent(fs, B_id, pool));
    SVN_ERR(check_id_absent(fs, lambda_id, pool));
    SVN_ERR(check_id_absent(fs, E_id, pool));
    SVN_ERR(check_id_absent(fs, alpha_id, pool));
    SVN_ERR(check_id_absent(fs, beta_id, pool));
    SVN_ERR(check_id_absent(fs, D_id, pool));
    SVN_ERR(check_id_absent(fs, gamma_id, pool));
    SVN_ERR(check_id_absent(fs, H_id, pool));
    SVN_ERR(check_id_absent(fs, chi_id, pool));
    SVN_ERR(check_id_absent(fs, psi_id, pool));
    SVN_ERR(check_id_absent(fs, omega_id, pool));
    SVN_ERR(check_id_absent(fs, G_id, pool));
    SVN_ERR(check_id_absent(fs, pi_id, pool));
    SVN_ERR(check_id_absent(fs, rho_id, pool));
    SVN_ERR(check_id_absent(fs, tau_id, pool));

    /* Validate the tree.  */
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        "This is the file 'iota'.\n" } };
      SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 1, pool));
    }
  }

  /* Abort transaction.  */
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* 3. Delete mutable directory with immutable nodes. */

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));

  /* Commit the greek tree. */
  SVN_ERR(svn_fs_commit_txn(NULL, &new_rev, txn, pool));

  /* Create new transaction. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  {
    const svn_fs_id_t *A_id, *mu_id, *B_id, *lambda_id, *E_id, *alpha_id,
      *beta_id, *F_id, *C_id, *D_id, *gamma_id, *H_id, *chi_id,
      *psi_id, *omega_id, *G_id, *pi_id, *rho_id, *tau_id, *sigma_id;

    /* Create A/D/G/sigma.  This makes all components of A/D/G
       mutable.  */
    SVN_ERR(svn_fs_make_file(txn_root, "A/D/G/sigma", pool));
    SVN_ERR(svn_test__set_file_contents(txn_root, "A/D/G/sigma",
                                        "This is another file 'sigma'.\n", pool));

    /* Check that mutable node-revision-IDs are removed and immutable
       ones still exist.  */
    SVN_ERR(svn_fs_node_id(&A_id, txn_root, "/A", pool));
    SVN_ERR(check_entry_present(txn_root, "", "A", pool));
    SVN_ERR(svn_fs_node_id(&mu_id, txn_root, "/A/mu", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "mu", pool));
    SVN_ERR(svn_fs_node_id(&B_id, txn_root, "/A/B", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "B", pool));
    SVN_ERR(svn_fs_node_id(&lambda_id, txn_root, "/A/B/lambda", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "lambda", pool));
    SVN_ERR(svn_fs_node_id(&E_id, txn_root, "/A/B/E", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "E", pool));
    SVN_ERR(svn_fs_node_id(&alpha_id, txn_root, "/A/B/E/alpha", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "alpha", pool));
    SVN_ERR(svn_fs_node_id(&beta_id, txn_root, "/A/B/E/beta", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "beta", pool));
    SVN_ERR(svn_fs_node_id(&F_id, txn_root, "/A/B/F", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "F", pool));
    SVN_ERR(svn_fs_node_id(&C_id, txn_root, "/A/C", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "C", pool));
    SVN_ERR(svn_fs_node_id(&D_id, txn_root, "/A/D", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "D", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "/A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(svn_fs_node_id(&H_id, txn_root, "/A/D/H", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "H", pool));
    SVN_ERR(svn_fs_node_id(&chi_id, txn_root, "/A/D/H/chi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "chi", pool));
    SVN_ERR(svn_fs_node_id(&psi_id, txn_root, "/A/D/H/psi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "psi", pool));
    SVN_ERR(svn_fs_node_id(&omega_id, txn_root, "/A/D/H/omega", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "omega", pool));
    SVN_ERR(svn_fs_node_id(&G_id, txn_root, "/A/D/G", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "G", pool));
    SVN_ERR(svn_fs_node_id(&pi_id, txn_root, "/A/D/G/pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(svn_fs_node_id(&rho_id, txn_root, "/A/D/G/rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "/A/D/G/tau", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));
    SVN_ERR(svn_fs_node_id(&sigma_id, txn_root, "/A/D/G/sigma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "sigma", pool));

    /* Delete "A" */
    SVN_ERR(svn_fs_delete(txn_root, "A", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "A", pool));
    SVN_ERR(check_id_absent(fs, A_id, pool));
    SVN_ERR(check_id_present(fs, mu_id, pool));
    SVN_ERR(check_id_present(fs, B_id, pool));
    SVN_ERR(check_id_present(fs, lambda_id, pool));
    SVN_ERR(check_id_present(fs, E_id, pool));
    SVN_ERR(check_id_present(fs, alpha_id, pool));
    SVN_ERR(check_id_present(fs, beta_id, pool));
    SVN_ERR(check_id_present(fs, F_id, pool));
    SVN_ERR(check_id_present(fs, C_id, pool));
    SVN_ERR(check_id_absent(fs, D_id, pool));
    SVN_ERR(check_id_present(fs, gamma_id, pool));
    SVN_ERR(check_id_present(fs, H_id, pool));
    SVN_ERR(check_id_present(fs, chi_id, pool));
    SVN_ERR(check_id_present(fs, psi_id, pool));
    SVN_ERR(check_id_present(fs, omega_id, pool));
    SVN_ERR(check_id_absent(fs, G_id, pool));
    SVN_ERR(check_id_present(fs, pi_id, pool));
    SVN_ERR(check_id_present(fs, rho_id, pool));
    SVN_ERR(check_id_present(fs, tau_id, pool));
    SVN_ERR(check_id_absent(fs, sigma_id, pool));

    /* Validate the tree.  */
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        "This is the file 'iota'.\n" }
      };
      
      SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 1, pool));
    }
  }

  /* Abort transaction.  */
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* 4. Delete immutable file. */

  /* Create new transaction. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  {
    const svn_fs_id_t *iota_id, *gamma_id;

    /* Check nodes revision ID is present.  */
    SVN_ERR(svn_fs_node_id(&iota_id, txn_root, "iota", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "", "iota", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(check_id_present(fs, iota_id, pool));
    SVN_ERR(check_id_present(fs, gamma_id, pool));

    /* Delete some files. */
    SVN_ERR(svn_fs_delete(txn_root, "iota", pool));
    SVN_ERR(svn_fs_delete(txn_root, "A/D/gamma", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "iota", pool));
    SVN_ERR(check_entry_absent(txn_root, "A/D", "iota", pool));
    SVN_ERR(check_id_present(fs, iota_id, pool));
    SVN_ERR(check_id_present(fs, gamma_id, pool));

    /* Validate the tree.  */
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
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
        { "A/D/G",       0 },
        { "A/D/G/pi",    "This is the file 'pi'.\n" },
        { "A/D/G/rho",   "This is the file 'rho'.\n" },
        { "A/D/G/tau",   "This is the file 'tau'.\n" },
        { "A/D/H",       0 },
        { "A/D/H/chi",   "This is the file 'chi'.\n" },
        { "A/D/H/psi",   "This is the file 'psi'.\n" },
        { "A/D/H/omega", "This is the file 'omega'.\n" }
      };
      SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 18, pool));
    }
  }

  /* Abort transaction.  */
  SVN_ERR(svn_fs_abort_txn(txn, pool));

  /* 5. Delete immutable directory. */

  /* Create new transaction. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  {
    const svn_fs_id_t *A_id, *mu_id, *B_id, *lambda_id, *E_id, *alpha_id,
      *beta_id, *F_id, *C_id, *D_id, *gamma_id, *H_id, *chi_id,
      *psi_id, *omega_id, *G_id, *pi_id, *rho_id, *tau_id;

    /* Check nodes revision ID is present.  */
    SVN_ERR(svn_fs_node_id(&A_id, txn_root, "/A", pool));
    SVN_ERR(check_entry_present(txn_root, "", "A", pool));
    SVN_ERR(svn_fs_node_id(&mu_id, txn_root, "/A/mu", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "mu", pool));
    SVN_ERR(svn_fs_node_id(&B_id, txn_root, "/A/B", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "B", pool));
    SVN_ERR(svn_fs_node_id(&lambda_id, txn_root, "/A/B/lambda", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "lambda", pool));
    SVN_ERR(svn_fs_node_id(&E_id, txn_root, "/A/B/E", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "E", pool));
    SVN_ERR(svn_fs_node_id(&alpha_id, txn_root, "/A/B/E/alpha", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "alpha", pool));
    SVN_ERR(svn_fs_node_id(&beta_id, txn_root, "/A/B/E/beta", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B/E", "beta", pool));
    SVN_ERR(svn_fs_node_id(&F_id, txn_root, "/A/B/F", pool));
    SVN_ERR(check_entry_present(txn_root, "A/B", "F", pool));
    SVN_ERR(svn_fs_node_id(&C_id, txn_root, "/A/C", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "C", pool));
    SVN_ERR(svn_fs_node_id(&D_id, txn_root, "/A/D", pool));
    SVN_ERR(check_entry_present(txn_root, "A", "D", pool));
    SVN_ERR(svn_fs_node_id(&gamma_id, txn_root, "/A/D/gamma", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "gamma", pool));
    SVN_ERR(svn_fs_node_id(&H_id, txn_root, "/A/D/H", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "H", pool));
    SVN_ERR(svn_fs_node_id(&chi_id, txn_root, "/A/D/H/chi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "chi", pool));
    SVN_ERR(svn_fs_node_id(&psi_id, txn_root, "/A/D/H/psi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "psi", pool));
    SVN_ERR(svn_fs_node_id(&omega_id, txn_root, "/A/D/H/omega", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/H", "omega", pool));
    SVN_ERR(svn_fs_node_id(&G_id, txn_root, "/A/D/G", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D", "G", pool));
    SVN_ERR(svn_fs_node_id(&pi_id, txn_root, "/A/D/G/pi", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "pi", pool));
    SVN_ERR(svn_fs_node_id(&rho_id, txn_root, "/A/D/G/rho", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "rho", pool));
    SVN_ERR(svn_fs_node_id(&tau_id, txn_root, "/A/D/G/tau", pool));
    SVN_ERR(check_entry_present(txn_root, "A/D/G", "tau", pool));

    /* Delete "A" */
    SVN_ERR(svn_fs_delete(txn_root, "A", pool));
    SVN_ERR(check_entry_absent(txn_root, "", "A", pool));
    SVN_ERR(check_id_present(fs, A_id, pool));
    SVN_ERR(check_id_present(fs, mu_id, pool));
    SVN_ERR(check_id_present(fs, B_id, pool));
    SVN_ERR(check_id_present(fs, lambda_id, pool));
    SVN_ERR(check_id_present(fs, E_id, pool));
    SVN_ERR(check_id_present(fs, alpha_id, pool));
    SVN_ERR(check_id_present(fs, beta_id, pool));
    SVN_ERR(check_id_present(fs, F_id, pool));
    SVN_ERR(check_id_present(fs, C_id, pool));
    SVN_ERR(check_id_present(fs, D_id, pool));
    SVN_ERR(check_id_present(fs, gamma_id, pool));
    SVN_ERR(check_id_present(fs, H_id, pool));
    SVN_ERR(check_id_present(fs, chi_id, pool));
    SVN_ERR(check_id_present(fs, psi_id, pool));
    SVN_ERR(check_id_present(fs, omega_id, pool));
    SVN_ERR(check_id_present(fs, G_id, pool));
    SVN_ERR(check_id_present(fs, pi_id, pool));
    SVN_ERR(check_id_present(fs, rho_id, pool));
    SVN_ERR(check_id_present(fs, tau_id, pool));

    /* Validate the tree.  */
    {
      static svn_test__tree_entry_t expected_entries[] = {
        /* path, contents (0 = dir) */
        { "iota",        "This is the file 'iota'.\n" }
      };
      SVN_ERR(svn_test__validate_tree(txn_root, expected_entries, 1, pool));
    }
  }

  return SVN_NO_ERROR;
}



struct node_created_rev_args {
  const char *path;
  svn_revnum_t rev;
};


static svn_error_t *
canonicalize_abspath(const char **msg,
                     svn_boolean_t msg_only,
                     svn_test_opts_t *opts,
                     apr_pool_t *pool)
{ 
  apr_size_t i;
  const char *paths[21][2] = 
    /* in                      out */
  { { NULL,                    NULL },
    { "",                      "/" },
    { "/",                     "/" },
    { "//",                    "/" },
    { "///",                   "/" },
    { "foo",                   "/foo" },
    { "foo/",                  "/foo" },
    { "foo//",                 "/foo" },
    { "/foo",                  "/foo" },
    { "/foo/",                 "/foo" },
    { "/foo//",                "/foo" },
    { "//foo//",               "/foo" },
    { "foo/bar",               "/foo/bar" },
    { "foo/bar/",              "/foo/bar" },
    { "foo/bar//",             "/foo/bar" },
    { "foo//bar",              "/foo/bar" },
    { "foo//bar/",             "/foo/bar" },
    { "foo//bar//",            "/foo/bar" },
    { "/foo//bar//",           "/foo/bar" },
    { "//foo//bar//",          "/foo/bar" },
    { "///foo///bar///baz///", "/foo/bar/baz" },
  };

  *msg = "test svn_fs_base__canonicalize_abspath";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < (sizeof(paths) / 2 / sizeof(const char *)); i++)
    {
      const char *input = paths[i][0];
      const char *output = paths[i][1];
      const char *actual = svn_fs_base__canonicalize_abspath(input, pool);
      
      if ((! output) && (! actual))
        continue;
      if ((! output) && actual)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "expected NULL path; got '%s'", actual);
      if (output && (! actual))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "expected '%s' path; got NULL", output);
      if (strcmp(output, actual))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "expected '%s' path; got '%s'",
                                 output, actual);
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
create_within_copy(const char **msg,
                   svn_boolean_t msg_only,
                   svn_test_opts_t *opts,
                   apr_pool_t *pool)
{ 
  apr_pool_t *spool = svn_pool_create(pool);
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  int i;
  svn_revnum_t youngest_rev = 0;
  
  *msg = "create new items within a copied directory";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-create-within-copy",
                              "bdb", pool));

  /*** Revision 1:  Create the greek tree in revision.  ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  svn_pool_clear(spool);

  /*** Revision 2:  Copy A/D to A/D3 ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D", txn_root, "A/D3", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  svn_pool_clear(spool);

  /*** Revision 3:  Copy A/D/G to A/D/G2 ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D/G", txn_root, "A/D/G2", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  svn_pool_clear(spool);

  /*** Revision 4: Copy A/D to A/D2 and create up and I in the existing
   A/D/G2, in the new A/D2, and in the nested, new A/D2/G2 ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_copy(rev_root, "A/D", txn_root, "A/D2", spool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A/D/G2/I", spool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D/G2/up", spool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A/D2/I", spool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D2/up", spool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A/D2/G2/I", spool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D2/G2/up", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  svn_pool_clear(spool);

  /*** Revision 5:  Create A/D3/down and A/D3/J ***/
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, spool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, spool));
  SVN_ERR(svn_fs_make_file(txn_root, "A/D3/down", spool));
  SVN_ERR(svn_fs_make_dir(txn_root, "A/D3/J", spool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, spool));
  svn_pool_clear(spool);

  {
    /* New items should have same CopyID as their parent */
    const char *pathgroup[4][3] =
      {
        { "A/D/G2",
          "A/D/G2/I",
          "A/D/G2/up" },
        { "A/D2",
          "A/D2/I",
          "A/D2/up" },
        { "A/D2/G2",
          "A/D2/G2/I",
          "A/D2/G2/up" },
        { "A/D3",
          "A/D3/down",
          "A/D3/J" }
      };

    SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, spool));

    for (i = 0; i < 4; i++)
      {
        const svn_fs_id_t *lead_id;
        const char *lead_copy_id;
        int j;

        /* Get the FSIdentifier for the first path in each group... */
        SVN_ERR(svn_fs_node_id(&lead_id, rev_root, pathgroup[i][0], spool));
        lead_copy_id = svn_fs_base__id_copy_id(lead_id);

        for (j = 1; j < 3; j++)
          {
            const svn_fs_id_t *id;
            const char *copy_id;

            /* ... and make sure the other members of the group have
               the same copy_id component as the 'lead' member. */

            SVN_ERR(svn_fs_node_id(&id, rev_root, pathgroup[i][j], spool));
            copy_id = svn_fs_base__id_copy_id(id);

            if (strcmp(copy_id, lead_copy_id) != 0)
              return svn_error_createf
                (SVN_ERR_TEST_FAILED, NULL,
                 "'%s' id: expected copy_id '%s'; got copy_id '%s'",
                 pathgroup[i][j], lead_copy_id, copy_id);
          }
      }
    svn_pool_clear(spool);
  }

  svn_pool_destroy(spool);
  return SVN_NO_ERROR;
}


/* Test the skip delta support by commiting so many changes to a file
 * that some of its older revisions become reachable by skip deltas,
 * then try retrieving those revisions.
 */
static svn_error_t *
skip_deltas(const char **msg,
            svn_boolean_t msg_only,
            svn_test_opts_t *opts,
            apr_pool_t *pool)
{ 
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_revnum_t youngest_rev = 0;
  const char *one_line = "This is a line in file 'f'.\n";
  svn_stringbuf_t *f = svn_stringbuf_create(one_line, pool); 

  *msg = "test skip deltas";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-skip-deltas",
                              "bdb", pool));

  /* Create the file. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
  SVN_ERR(svn_fs_make_file(txn_root, "f", subpool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "f", f->data, subpool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
  SVN_ERR(svn_fs_deltify_revision(fs, youngest_rev, subpool));
  svn_pool_clear(subpool);

  /* Now, commit changes to the file 128 times. */
  while (youngest_rev <= 128)
    {
      /* Append another line to the ever-growing file contents. */
      svn_stringbuf_appendcstr(f, one_line); 

      /* Commit the new contents. */
      SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, subpool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, subpool));
      SVN_ERR(svn_test__set_file_contents(txn_root, "f", f->data, subpool));
      SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, subpool));
      SVN_ERR(svn_fs_deltify_revision(fs, youngest_rev, subpool));
      svn_pool_clear(subpool);
    }

  /* Now go back and check revision 1. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, 1, pool));
  SVN_ERR(svn_test__get_file_contents(rev_root, "f", &f, pool));
  if (strcmp(one_line, f->data) != 0)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL, 
       "Wrong contents.  Expected:\n   '%s'\nGot:\n   '%s'\n", 
       one_line, f->data);

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Trail-ish helpers for redundant_copy(). */
struct get_txn_args
{
  transaction_t **txn;
  const char *txn_name;
  svn_fs_t *fs;
};

static svn_error_t *
txn_body_get_txn(void *baton, trail_t *trail)
{
  struct get_txn_args *args = baton;
  return svn_fs_bdb__get_txn(args->txn, args->fs, args->txn_name, 
                             trail, trail->pool);
}


static svn_error_t *
redundant_copy(const char **msg,
               svn_boolean_t msg_only,
               svn_test_opts_t *opts,
               apr_pool_t *pool)
{ 
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  const char *txn_name;
  transaction_t *transaction;
  svn_fs_root_t *txn_root, *rev_root;
  const svn_fs_id_t *old_D_id, *new_D_id;
  svn_revnum_t youngest_rev = 0;
  struct get_txn_args args;
  
  *msg = "ensure no-op for redundant copies";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-redundant-copy",
                              "bdb", pool));

  /* Create the greek tree in revision 1. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(NULL, &youngest_rev, txn, pool));

  /* In a transaction, copy A to Z. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_copy(rev_root, "A", txn_root, "Z", pool));

  /* Now, examine the transaction.  There should have been only one
     copy there. */
  args.fs = fs;
  args.txn_name = txn_name;
  args.txn = &transaction;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_get_txn, &args, pool));
  if (transaction->copies->nelts != 1)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Expected 1 copy; got %d",
                             transaction->copies->nelts);

  /* Get the node-rev-id for A/D (the reason will be clear a little later). */
  SVN_ERR(svn_fs_node_id(&old_D_id, txn_root, "A/D", pool));

  /* Now copy A/D/G Z/D/G. */
  SVN_ERR(svn_fs_copy(rev_root, "A/D/G", txn_root, "Z/D/G", pool));

  /* Now, examine the transaction.  There should still only have been
     one copy operation that "took". */
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_get_txn, &args, pool));
  if (transaction->copies->nelts != 1)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Expected only 1 copy; got %d",
                             transaction->copies->nelts);

  /* Finally, check the node-rev-id for "Z/D" -- it should never have
     been made mutable (since the second copy should not have taken
     place). */
  SVN_ERR(svn_fs_node_id(&new_D_id, txn_root, "A/D", pool));
  if (! svn_string_compare(svn_fs_unparse_id(old_D_id, pool),
                           svn_fs_unparse_id(new_D_id, pool)))
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, NULL,
       "Expected equivalent node-rev-ids; got differing ones");

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(create_berkeley_filesystem),
    SVN_TEST_PASS(open_berkeley_filesystem),
    SVN_TEST_PASS(delete_mutables),
    SVN_TEST_PASS(delete),
    SVN_TEST_PASS(abort_txn),
    SVN_TEST_PASS(create_within_copy),
    SVN_TEST_PASS(canonicalize_abspath),
    SVN_TEST_PASS(skip_deltas),
    SVN_TEST_PASS(redundant_copy),
    SVN_TEST_NULL
  };
