/*
 * svnmover.c: Subversion Multiple URL Client
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
 *
 */

/*  Multiple URL Command Client

    Combine a list of mv, cp and rm commands on URLs into a single commit.

    How it works: the command line arguments are parsed into an array of
    action structures.  The action structures are interpreted to build a
    tree of operation structures.  The tree of operation structures is
    used to drive an RA commit editor to produce a single commit.

    To build this client, type 'make svnmover' from the root of your
    Subversion source directory.
*/

#include <stdio.h>
#include <string.h>

#include <apr_lib.h>

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_client.h"
#include "private/svn_client_mtcc.h"
#include "svn_cmdline.h"
#include "svn_config.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_string.h"
#include "svn_subst.h"
#include "svn_utf.h"
#include "svn_version.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_editor3.h"
#include "private/svn_ra_private.h"
#include "private/svn_string_private.h"
#include "private/svn_sorts_private.h"

/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_client", svn_client_version },
      { "svn_subr",   svn_subr_version },
      { "svn_ra",     svn_ra_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}

/* ====================================================================== */

/* Construct a txn-path.
 */
static svn_editor3_txn_path_t
txn_path(const char *repos_relpath, svn_revnum_t revision,
         const char *created_relpath)
{
  svn_editor3_txn_path_t p;

  p.peg.rev = revision;
  p.peg.relpath = repos_relpath;
  p.relpath = created_relpath;
  return p;
}

/* Return a txn-path constructed by extending TXN_PATH with RELPATH.
 */
static svn_editor3_txn_path_t
txn_path_join(svn_editor3_txn_path_t txn_path,
              const char *relpath,
              apr_pool_t *result_pool)
{
  svn_editor3_txn_path_t p = txn_path;

  p.relpath = svn_relpath_join(p.relpath, relpath, result_pool);
  return p;
}

/* Return a human-readable string representation of LOC.
 */
static const char *
txn_path_str(svn_editor3_txn_path_t loc,
             apr_pool_t *result_pool)
{
  return apr_psprintf(result_pool, "%s@%ld//%s",
                      loc.peg.relpath, loc.peg.rev, loc.relpath);
}

/* Return the full relpath of LOC,
 * ### assuming the peg-path part doesn't need translation between its
 *     peg-revision and the transaction in which we interpret it.
 */
static const char *
txn_path_to_relpath(svn_editor3_txn_path_t loc,
                    apr_pool_t *result_pool)
{
  return svn_relpath_join(loc.peg.relpath, loc.relpath, result_pool);
}

/* Return the txn-path of the parent directory of LOC.
 */
static svn_editor3_txn_path_t
txn_path_dirname(svn_editor3_txn_path_t loc,
                 apr_pool_t *result_pool)
{
  svn_editor3_txn_path_t parent_loc;

  parent_loc.peg.rev = loc.peg.rev;
  if (*loc.relpath)
    {
      parent_loc.peg.relpath = apr_pstrdup(result_pool, loc.peg.relpath);
      parent_loc.relpath = svn_relpath_dirname(loc.relpath, result_pool);
    }
  else
    {
      parent_loc.peg.relpath = svn_relpath_dirname(loc.peg.relpath,
                                                   result_pool);
      parent_loc.relpath = apr_pstrdup(result_pool, loc.relpath);
    }
  return parent_loc;
}

/* ====================================================================== */

typedef struct mtcc_t
{
  apr_pool_t *pool;
  const char *repos_root_url;
  /*const char *anchor_repos_relpath;*/
  svn_revnum_t head_revision;
  svn_revnum_t base_revision;

  svn_ra_session_t *ra_session;
  svn_editor3_t *editor;
  svn_client_ctx_t *ctx;
} mtcc_t;

static svn_error_t *
mtcc_create(mtcc_t **mtcc_p,
            const char *anchor_url,
            svn_revnum_t base_revision,
            apr_hash_t *revprops,
            svn_commit_callback2_t commit_callback,
            void *commit_baton,
            svn_client_ctx_t *ctx,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  apr_pool_t *mtcc_pool = svn_pool_create(result_pool);
  mtcc_t *mtcc = apr_pcalloc(mtcc_pool, sizeof(*mtcc));

  mtcc->pool = mtcc_pool;
  mtcc->ctx = ctx;

  SVN_ERR(svn_client_open_ra_session2(&mtcc->ra_session, anchor_url,
                                      NULL /* wri_abspath */, ctx,
                                      mtcc_pool, scratch_pool));

  SVN_ERR(svn_ra_get_repos_root2(mtcc->ra_session, &mtcc->repos_root_url,
                                 result_pool));
  SVN_ERR(svn_ra_get_latest_revnum(mtcc->ra_session, &mtcc->head_revision,
                                   scratch_pool));

  if (! SVN_IS_VALID_REVNUM(base_revision))
    mtcc->base_revision = mtcc->head_revision;
  else if (base_revision > mtcc->head_revision)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("No such revision %ld (HEAD is %ld)"),
                             base_revision, mtcc->head_revision);
  else
    mtcc->base_revision = base_revision;

  SVN_ERR(svn_ra_get_commit_editor_ev3(mtcc->ra_session, &mtcc->editor,
                                       revprops,
                                       commit_callback, commit_baton,
                                       NULL /*lock_tokens*/, FALSE /*keep_locks*/,
                                       result_pool));
  *mtcc_p = mtcc;
  return SVN_NO_ERROR;
}

static svn_error_t *
mtcc_commit(mtcc_t *mtcc,
            apr_pool_t *scratch_pool)
{
  svn_error_t *err;

#if 0
  /* No changes -> no revision. Easy out */
  if (MTCC_UNMODIFIED(mtcc))
    {
      svn_editor3_abort(mtcc->editor);
      svn_pool_destroy(mtcc->pool);
      return SVN_NO_ERROR;
    }
#endif

#if 0
  const char *session_url;

  SVN_ERR(svn_ra_get_session_url(mtcc->ra_session, &session_url, scratch_pool));

  if (mtcc->root_op->kind != OP_OPEN_DIR)
    {
      const char *name;

      svn_uri_split(&session_url, &name, session_url, scratch_pool);

      if (*name)
        {
          SVN_ERR(mtcc_reparent(session_url, mtcc, scratch_pool));

          SVN_ERR(svn_ra_reparent(mtcc->ra_session, session_url, scratch_pool));
        }
    }
#endif

  err = svn_editor3_complete(mtcc->editor);

  return svn_error_trace(err);
}

static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, "r%ld committed by %s at %s\n",
                             commit_info->revision,
                             (commit_info->author
                              ? commit_info->author : "(no author)"),
                             commit_info->date));
  return SVN_NO_ERROR;
}

typedef enum action_code_t {
  ACTION_LIST_BRANCHES,
  ACTION_LIST_BRANCHES_R,
  ACTION_BRANCH,
  ACTION_BRANCHIFY,
  ACTION_DISSOLVE,
  ACTION_MV,
  ACTION_MKDIR,
  ACTION_CP,
  ACTION_RM
} action_code_t;

struct action {
  action_code_t action;

  /* revision (copy-from-rev of path[0] for cp) */
  svn_revnum_t rev;

  /* action    path[0]  path[1]
   * ------    -------  -------
   * list_br   path
   * branch    source   target
   * branchify path
   * dissolve  path
   * mv        source   target
   * mkdir     target   (null)
   * cp        source   target
   * rm        target   (null)
   */
  const char *path[2];
};

/* ====================================================================== */

/* WITH_OVERLAPPING_EIDS means an outer branch maintains EIDs for all the
 * elements of the nested branches in it, rather than just for the root
 * element of each immediate child branch.
 *
 * The idea is that this may facilitate some sort of tracking of moves
 * into and out of subbranches, but the idea is not fully developed.
 *
 * This is only partially implemented, and may not be a useful idea.
 */
/* #define WITH_OVERLAPPING_EIDS */

/* ### */
#define SVN_ERR_BRANCHING 123456

struct svn_branch_family_t;

/* Per-repository branching info.
 */
typedef struct svn_branch_repos_t
{
  svn_ra_session_t *ra_session;
  svn_editor3_t *editor;

  /* The root family in this repository. */
  struct svn_branch_family_t *root_family;

  /* The range of family ids assigned within this repos (starts at 0). */
  int next_fid;

  /* The pool in which this object lives. */
  apr_pool_t *pool;
} svn_branch_repos_t;

/* A branch family.
 */
typedef struct svn_branch_family_t
{
  /* --- Identity of this object --- */

  /* The repository in which this family exists. */
  svn_branch_repos_t *repos;

  /* The outer family of which this is a sub-family. NULL if this is the
     root family. */
  /*struct svn_branch_family_t *outer_family;*/

  /* The FID of this family within its repository. */
  int fid;

  /* --- Contents of this object --- */

  /* The branch definitions in this family. */
  apr_array_header_t *branch_definitions;

  /* The branch instances in this family. */
  apr_array_header_t *branch_instances;

  /* The range of branch ids assigned within this family. */
  int first_bid, next_bid;

  /* The range of element ids assigned within this family. */
  int first_eid, next_eid;

  /* The immediate sub-families of this family. */
  apr_array_header_t *sub_families;

  /* The pool in which this object lives. */
  apr_pool_t *pool;
} svn_branch_family_t;

/* A branch.
 *
 * A branch definition object describes the characteristics common to all
 * instances of a branch (with a given BID) in its family. (There is one
 * instance of this branch within each branch of its outer families.)
 *
 * Often, all branches in a family have the same root element. For example,
 * branching /trunk to /branches/br1 results in:
 *
 *      family 1, branch 1, root-EID 100
 *          EID 100 => /trunk
 *          ...
 *      family 1, branch 2, root-EID 100
 *          EID 100 => /branches/br1
 *          ...
 *
 * However, the root element of one branch may correspond to a non-root
 * element of another branch; such a branch could be called a "subtree
 * branch". Using the same example, branching from the trunk subtree
 * /trunk/D (which is not itself a branch root) results in:
 *
 *      family 1, branch 3: root-EID = 104
 *          EID 100 => (nil)
 *          ...
 *          EID 104 => /branches/branch-of-trunk-subtree-D
 *          ...
 *
 * If family 1 were nested inside an outer family, then there could be
 * multiple branch-instances for each branch-definition. In the above
 * example, all instances of (family 1, branch 1) will have root-EID 100,
 * and all instances of (family 1, branch 3) will have root-EID 104.
 */
typedef struct svn_branch_definition_t
{
  /* --- Identity of this object --- */

  /* The family of which this branch is a member. */
  svn_branch_family_t *family;

  /* The BID of this branch within its family. */
  int bid;

  /* The EID, within the outer family, of the branch root element. */
  /*int outer_family_eid_of_branch_root;*/

  /* --- Contents of this object --- */

  /* The EID within its family of its pathwise root element. */
  int root_eid;
} svn_branch_definition_t;

/* A branch instance.
 *
 * A branch instance object describes one branch in this family. (There is
 * one instance of this branch within each branch of its outer families.)
 */
typedef struct svn_branch_instance_t
{
  /* --- Identity of this object --- */

  svn_branch_definition_t *definition;

  /* The branch (instance?), within the outer family, that contains the
     root element of this branch. */
  /*svn_branch_instance_t *outer_family_branch_instance;*/

  /* --- Contents of this object --- */

  /* EID <-> path mapping in the current revision. Path is relative to
     repos root. There is always an entry for root_eid. */
  apr_hash_t *eid_to_path, *path_to_eid;
} svn_branch_instance_t;

/* element */
/*
typedef struct svn_branch_element_t
{
  int eid;
  svn_branch_family_t *family;
} svn_branch_element_t;
*/

/* Create a new branching metadata object */
static svn_branch_repos_t *
svn_branch_repos_create(svn_ra_session_t *ra_session,
                        svn_editor3_t *editor,
                        apr_pool_t *result_pool)
{
  svn_branch_repos_t *repos = apr_pcalloc(result_pool, sizeof(*repos));

  repos->ra_session = ra_session;
  repos->editor = editor;
  repos->pool = result_pool;
  return repos;
}

/* Create a new branch family object */
static svn_branch_family_t *
svn_branch_family_create(svn_branch_repos_t *repos,
                         int fid,
                         int first_bid,
                         int next_bid,
                         int first_eid,
                         int next_eid,
                         apr_pool_t *result_pool)
{
  svn_branch_family_t *f = apr_pcalloc(result_pool, sizeof(*f));

  f->fid = fid;
  f->repos = repos;
  f->branch_definitions = apr_array_make(result_pool, 1, sizeof(void *));
  f->branch_instances = apr_array_make(result_pool, 1, sizeof(void *));
  f->sub_families = apr_array_make(result_pool, 1, sizeof(void *));
  f->first_bid = first_bid;
  f->next_bid = next_bid;
  f->first_eid = first_eid;
  f->next_eid = next_eid;
  f->pool = result_pool;
  return f;
}

/* Create a new branch definition object */
static svn_branch_definition_t *
svn_branch_definition_create(svn_branch_family_t *family,
                             int bid,
                             int root_eid,
                             apr_pool_t *result_pool)
{
  svn_branch_definition_t *b = apr_pcalloc(result_pool, sizeof(*b));

  SVN_ERR_ASSERT_NO_RETURN(bid >= family->first_bid
                           && bid < family->next_bid);
  SVN_ERR_ASSERT_NO_RETURN(root_eid >= family->first_eid
                           && root_eid < family->next_eid);

  b->family = family;
  b->bid = bid;
  b->root_eid = root_eid;
  return b;
}

/* Create a new branch instance object */
static svn_branch_instance_t *
svn_branch_instance_create(svn_branch_definition_t *branch_definition,
                           apr_pool_t *result_pool)
{
  svn_branch_instance_t *b = apr_pcalloc(result_pool, sizeof(*b));

  b->definition = branch_definition;
  b->eid_to_path = apr_hash_make(result_pool);
  b->path_to_eid = apr_hash_make(result_pool);
  return b;
}

static const char *
branch_get_root_path(const svn_branch_instance_t *branch);

/* In BRANCH, set or change the path mapping for EID to point to PATH.
 *
 * Do not check for or remove any previous mappings; just overwrite
 * the mapping for EID to this new PATH and for PATH to this new EID.
 * EID MUST be a valid EID belonging to BRANCH's family.
 *
 * Duplicate EID and PATH into the mapping's pool.
 */
static void
branch_mapping_set(svn_branch_instance_t *branch,
                   int eid,
                   const char *path)
{
  apr_pool_t *pool = apr_hash_pool_get(branch->eid_to_path);
  int *eid_stored = apr_pmemdup(pool, &eid, sizeof(eid));

  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->definition->family->first_eid
                           && eid < branch->definition->family->next_eid);

  /* overwrite the EID-to-new-PATH and part of the mapping */
  path = apr_pstrdup(pool, path);
  apr_hash_set(branch->eid_to_path, eid_stored, sizeof(eid), path);
  svn_hash_sets(branch->path_to_eid, path, eid_stored);
}

/* In BRANCH, remove the path mapping for EID if there is one.
 *
 * Do not check for any previous mappings, or for validity of EID or any
 * mappings.
 */
static void
branch_mapping_unset_by_eid(svn_branch_instance_t *branch,
                            int eid)
{
  const char *path = apr_hash_get(branch->eid_to_path, &eid, sizeof(eid));

  if (path)
    {
      apr_hash_set(branch->eid_to_path, &eid, sizeof(eid), NULL);
      svn_hash_sets(branch->path_to_eid, path, NULL);
    }
}

/* In BRANCH, set or change the path mapping for EID to point to PATH.
 *
 * If a mapping from EID to a different path already exists, update it
 * to map to PATH. PATH MUST NOT already be mapped to a different EID.
 * PATH MUST be pathwise within the root path of BRANCH.
 */
static void
branch_mapping_update(svn_branch_instance_t *branch,
                      int eid,
                      const char *path)
{
  int *old_eid_p = svn_hash_gets(branch->path_to_eid, path);

  SVN_ERR_ASSERT_NO_RETURN(eid == branch->definition->root_eid
                           || svn_relpath_skip_ancestor(
                                branch_get_root_path(branch), path));
  /* we don't allow mapping a new EID to an existing path */
  SVN_ERR_ASSERT_NO_RETURN(! old_eid_p || *old_eid_p == eid);

  /* Remove any mapping to an old path, as the path-to-EID part of such a
     mapping would not be overwritten simply by setting the new mapping. */
  branch_mapping_unset_by_eid(branch, eid);

  branch_mapping_set(branch, eid, path);
}

/* In BRANCH, remove the path mapping for EID, so that the element EID
 * is considered as no longer existing in BRANCH.
 *
 * A mapping for EID MUST already exist in BRANCH.
 */
static void
branch_mapping_remove_by_eid(svn_branch_instance_t *branch,
                             int eid)
{
  const char *path = apr_hash_get(branch->eid_to_path, &eid, sizeof(eid));

  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->definition->family->first_eid
                           && eid < branch->definition->family->next_eid);
  SVN_ERR_ASSERT_NO_RETURN(path);

  apr_hash_set(branch->eid_to_path, &eid, sizeof(eid), NULL);
  svn_hash_sets(branch->path_to_eid, path, NULL);
}

/* In BRANCH, remove the path mapping for PATH, so that the element
 * at PATH is considered as no longer existing in BRANCH.
 *
 * A mapping for PATH MUST already exist in BRANCH.
 */
static void
branch_mapping_remove_by_path(svn_branch_instance_t *branch,
                              const char *path)
{
  int *eid_p = svn_hash_gets(branch->path_to_eid, path);

  SVN_ERR_ASSERT_NO_RETURN(svn_relpath_skip_ancestor(
                             branch_get_root_path(branch), path));
  SVN_ERR_ASSERT_NO_RETURN(eid_p);

  apr_hash_set(branch->eid_to_path, eid_p, sizeof(*eid_p), NULL);
  svn_hash_sets(branch->path_to_eid, path, NULL);
}

/* Return the EID in BRANCH of the element at repos-relpath PATH,
 * or -1 if PATH is not currently present in BRANCH.
 *
 * PATH MUST be pathwise within BRANCH.
 */
static int
branch_get_eid_by_path(const svn_branch_instance_t *branch,
                       const char *path)
{
  int *eid_p = svn_hash_gets(branch->path_to_eid, path);

  SVN_ERR_ASSERT_NO_RETURN(svn_relpath_skip_ancestor(
                             branch_get_root_path(branch), path));

  if (! eid_p)
    return -1;
  return *eid_p;
}

/* Return the repos-relpath for element EID of BRANCH, or NULL if EID
 * is not currently present in BRANCH.
 *
 * EID MUST be a valid EID belonging to BRANCH's family, but the element
 * need not be present in any branch.
 */
static const char *
branch_get_path_by_eid(const svn_branch_instance_t *branch,
                       int eid)
{
  const char *path = apr_hash_get(branch->eid_to_path, &eid, sizeof(eid));

  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->definition->family->first_eid
                           && eid < branch->definition->family->next_eid);

  return path;
}

/* Return the root repos-relpath of BRANCH. This is always available. */
static const char *
branch_get_root_path(const svn_branch_instance_t *branch)
{
  return branch_get_path_by_eid(branch, branch->definition->root_eid);
}

/*  */
static svn_boolean_t
branch_is_root_path(const svn_branch_instance_t *branch,
                    const char *rrpath)
{
  return branch_get_eid_by_path(branch, rrpath) == branch->definition->root_eid;
}

/* In BRANCH, update the path mappings to reflect a delete of a subtree
 * at FROM_PATH.
 *
 * FROM_PATH MUST be an existing path in BRANCH, and may be the root path
 * of BRANCH.
 *
 * If INCLUDE_SELF is true, include the element at FROM_PATH, otherwise
 * only act on children (recursively) of FROM_PATH.
 */
static svn_error_t *
branch_mappings_delete(svn_branch_instance_t *branch,
                       const char *from_path,
                       svn_boolean_t *include_self,
                       apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, branch->eid_to_path);
       hi; hi = apr_hash_next(hi))
    {
      const int *eid_p = apr_hash_this_key(hi);
      const char *this_path = apr_hash_this_val(hi);
      const char *remainder = svn_relpath_skip_ancestor(from_path, this_path);

      if (remainder && (include_self || remainder[0]))
        {
          branch_mapping_unset_by_eid(branch, *eid_p);
        }
    }

  return SVN_NO_ERROR;
}

/* In BRANCH, update the path mappings to reflect a move of a subtree
 * from FROM_PATH to TO_PATH.
 *
 * FROM_PATH MUST be an existing path in BRANCH, and may be the root path
 * of BRANCH.
 */
static svn_error_t *
branch_mappings_move(svn_branch_instance_t *branch,
                     const char *from_path,
                     const char *to_path,
                     apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, branch->eid_to_path);
       hi; hi = apr_hash_next(hi))
    {
      const int *eid_p = apr_hash_this_key(hi);
      const char *this_from_path = apr_hash_this_val(hi);
      const char *remainder = svn_relpath_skip_ancestor(from_path, this_from_path);

      if (remainder)
        {
          const char *this_to_path = svn_relpath_join(to_path, remainder,
                                                      scratch_pool);

          branch_mapping_unset_by_eid(branch, *eid_p);
          branch_mapping_set(branch, *eid_p, this_to_path);
        }
    }

  return SVN_NO_ERROR;
}

static int
family_add_new_element(svn_branch_family_t *family);

/* In TO_BRANCH, assign new EIDs and path mappings to reflect the copying
 * of a subtree from FROM_BRANCH:FROM_PATH to TO_BRANCH:TO_PATH. Assign
 * a new EID in TO_BRANCH's family for each copied element.
 *
 * FROM_BRANCH and TO_BRANCH may be the same or different branch instances
 * in the same or different branch families.
 *
 * FROM_PATH MUST be an existing path in FROM_BRANCH, and may be the
 * root path of FROM_BRANCH.
 *
 * If INCLUDE_SELF is true, include the element at FROM_PATH, otherwise
 * only act on children (recursively) of FROM_PATH.
 */
static svn_error_t *
branch_mappings_copy(svn_branch_instance_t *from_branch,
                     const char *from_path,
                     svn_branch_instance_t *to_branch,
                     const char *to_path,
                     svn_boolean_t *include_self,
                     apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, from_branch->eid_to_path);
       hi; hi = apr_hash_next(hi))
    {
      const char *this_from_path = apr_hash_this_val(hi);
      const char *remainder = svn_relpath_skip_ancestor(from_path, this_from_path);

      if (remainder && (include_self || remainder[0]))
        {
          const char *this_to_path = svn_relpath_join(to_path, remainder,
                                                      scratch_pool);
          int new_eid = family_add_new_element(to_branch->definition->family);

          branch_mapping_set(to_branch, new_eid, this_to_path);
        }
    }

  return SVN_NO_ERROR;
}

/* In TO_BRANCH, update the path mappings to reflect a branching of a
 * subtree from FROM_BRANCH:FROM_PATH to TO_BRANCH:TO_PATH.
 *
 * FROM_BRANCH and TO_BRANCH must be different branch instances in the
 * same branch family.
 *
 * FROM_PATH MUST be an existing path in FROM_BRANCH, and may be the
 * root path of FROM_BRANCH.
 *
 * TO_PATH MUST be a path in TO_BRANCH
 */
static svn_error_t *
branch_mappings_branch(svn_branch_instance_t *from_branch,
                       const char *from_path,
                       svn_branch_instance_t *to_branch,
                       const char *to_path,
                       apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(from_branch->definition->family == to_branch->definition->family);

  for (hi = apr_hash_first(scratch_pool, from_branch->eid_to_path);
       hi; hi = apr_hash_next(hi))
    {
      const int *eid_p = apr_hash_this_key(hi);
      const char *this_from_path = apr_hash_this_val(hi);
      const char *remainder = svn_relpath_skip_ancestor(from_path, this_from_path);

      if (remainder)
        {
          const char *this_to_path = svn_relpath_join(to_path, remainder,
                                                      scratch_pool);

          branch_mapping_set(to_branch, *eid_p, this_to_path);
        }
    }

  return SVN_NO_ERROR;
}

/* Return an array of pointers to the branch instances that are sub-branches
 * of BRANCH. */
static apr_array_header_t *
branch_get_sub_branches(const svn_branch_instance_t *branch,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *branch_root_rrpath = branch_get_root_path(branch);
  apr_array_header_t *sub_branches = apr_array_make(result_pool, 0, sizeof(void *));
  int i;

  for (i = 0; i < branch->definition->family->sub_families->nelts; i++)
    {
      svn_branch_family_t *family
        = APR_ARRAY_IDX(branch->definition->family->sub_families, i, void *);
      int b;

      for (b = 0; b < family->branch_instances->nelts; b++)
        {
          svn_branch_instance_t *sub_branch
            = APR_ARRAY_IDX(family->branch_instances, b, void *);
          const char *sub_branch_root_rrpath = branch_get_root_path(sub_branch);

          if (svn_relpath_skip_ancestor(branch_root_rrpath, sub_branch_root_rrpath))
            {
              APR_ARRAY_PUSH(sub_branches, void *) = sub_branch;
            }
        }
    }
  return sub_branches;
}

/* Delete the branch instance object BRANCH and any nested branch instances.
 */
static svn_error_t *
branch_instance_delete_r(svn_branch_instance_t *branch,
                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranches
    = branch_get_sub_branches(branch, scratch_pool, scratch_pool);
  int i;

  /* Delete nested branch instances, recursively */
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch_instance_t *b = APR_ARRAY_IDX(subbranches, i, void *);

      branch_instance_delete_r(b, scratch_pool);
    }

  /* Remove the record of this branch instance */
  for (i = 0; i < branch->definition->family->branch_instances->nelts; i++)
    {
      svn_branch_instance_t *b = APR_ARRAY_IDX(subbranches, i, void *);

      if (b == branch)
        {
          svn_sort__array_delete(branch->definition->family->branch_instances,
                                 i, 1);
        }
    }

  return SVN_NO_ERROR;
}

/* Copy the branch instance object BRANCH and any nested branch instances.
 *
 * ### Currently, the mapping will be empty.
 *     TODO: Duplicate the mapping, based on a new path.
 */
static svn_error_t *
branch_instance_copy_r(svn_branch_instance_t **new_branch_p,
                       svn_branch_instance_t *branch,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranches
    = branch_get_sub_branches(branch, scratch_pool, scratch_pool);
  int i;
  svn_branch_instance_t *new_branch;

  /* Duplicate this branch instance */
  new_branch = svn_branch_instance_create(branch->definition, result_pool);
  /* ### branch_mapping_dup(new_branch, branch) */

  /* Record this new branch instance in its family */
  APR_ARRAY_PUSH(branch->definition->family->branch_instances, void *)
    = new_branch;

  /* Copy nested branch instances, recursively */
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch_instance_t *b = APR_ARRAY_IDX(subbranches, i, void *);

      branch_instance_copy_r(&b, b, result_pool, scratch_pool);
    }

  *new_branch_p = new_branch;
  return SVN_NO_ERROR;
}

/* Create a new, empty family in OUTER_FAMILY.
 */
static svn_branch_family_t *
family_add_new_subfamily(svn_branch_family_t *outer_family)
{
  svn_branch_repos_t *repos = outer_family->repos;
  int fid = repos->next_fid++;
  svn_branch_family_t *family
    = svn_branch_family_create(repos, fid,
                               fid * 10, fid * 10,
                               fid * 100, fid * 100,
                               outer_family->pool);

  /* Register the family */
  APR_ARRAY_PUSH(outer_family->sub_families, void *) = family;

  return family;
}

/* Create a new branch definition in FAMILY, with root EID ROOT_EID.
 * Create a new, empty branch instance in FAMILY, empty except for the
 * root element which is at path ROOT_RRPATH.
 */
static svn_branch_instance_t *
family_add_new_branch(svn_branch_family_t *family,
                      int root_eid,
                      const char *root_rrpath)
{
  int bid = family->next_bid++;
  svn_branch_definition_t *branch_definition
    = svn_branch_definition_create(family, bid, root_eid, family->pool);
  svn_branch_instance_t *branch_instance
    = svn_branch_instance_create(branch_definition, family->pool);

  SVN_ERR_ASSERT_NO_RETURN(root_eid >= family->first_eid
                           && root_eid < family->next_eid);
  /* ROOT_RRPATH must not be within another branch of the family. */

  /* Register the branch */
  APR_ARRAY_PUSH(family->branch_definitions, void *) = branch_definition;
  /* ### Should create multiple instances, one per branch of parent family. */
  APR_ARRAY_PUSH(family->branch_instances, void *) = branch_instance;

  /* Initialize the root element */
  branch_mapping_update(branch_instance, root_eid, root_rrpath);
  return branch_instance;
}

/* Assign a new element id in FAMILY.
 */
static int
family_add_new_element(svn_branch_family_t *family)
{
  int eid = family->next_eid++;

  return eid;
}

/* Find the existing family with id FID in FAMILY (recursively, excluding
 * FAMILY itself). Assume FID is unique among all sub-families.
 *
 * Return NULL if not found.
 */
static svn_branch_family_t *
family_get_subfamily_by_id(const svn_branch_family_t *family,
                           int fid)
{
  int i;

  SVN_ERR_ASSERT_NO_RETURN(fid >= 0 && fid < family->repos->next_fid);

  for (i = 0; i < family->sub_families->nelts; i++)
    {
      svn_branch_family_t *f
        = APR_ARRAY_IDX(family->sub_families, i, svn_branch_family_t *);

      if (f->fid == fid)
        return f;
      f = family_get_subfamily_by_id(f, fid);
      if (f)
        return f;
    }

  return NULL;
}

/* Find the existing family with id FID in REPOS.
 *
 * Return NULL if not found.
 */
static svn_branch_family_t *
repos_get_family_by_id(const svn_branch_repos_t *repos,
                       int fid)
{
  svn_branch_family_t *f;

  SVN_ERR_ASSERT_NO_RETURN(fid >= 0 && fid < repos->next_fid);

  if (repos->root_family->fid == fid)
    {
      f = repos->root_family;
    }
  else
    {
      f = family_get_subfamily_by_id(repos->root_family, fid);
    }

  return f;
}

/* The default branching metadata for a new repository. */
static const char *default_repos_info
  = "r: fids 0 1 root-fid 0\n"
    "f0: bids 0 1 eids 0 1 parent-fid 0\n"
    "f0b0: root-eid 0\n"
    "f0b0e0: \n";

/* Create a new branch *NEW_BRANCH that belongs to FAMILY, initialized
 * with info parsed from STREAM, allocated in RESULT_POOL.
 */
static svn_error_t *
parse_branch_info(svn_branch_instance_t **new_branch,
                  svn_branch_family_t *family,
                  svn_stream_t *stream,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_branch_definition_t *branch_definition;
  svn_branch_instance_t *branch_instance;
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;
  int fid, bid, root_eid;
  int eid;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(! eof);
  n = sscanf(line->data, "f%db%d: root-eid %d\n",
             &fid, &bid, &root_eid);
  SVN_ERR_ASSERT(n == 3);

  SVN_ERR_ASSERT(fid == family->fid);
  branch_definition = svn_branch_definition_create(family, bid, root_eid,
                                                   result_pool);
  branch_instance = svn_branch_instance_create(branch_definition, result_pool);

  for (eid = family->first_eid; eid < family->next_eid; eid++)
    {
      int this_fid, this_bid, this_eid;
      int this_path_start_pos;
      char *this_path;

      SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
      SVN_ERR_ASSERT(! eof);
      n = sscanf(line->data, "f%db%de%d: %n\n",
                 &this_fid, &this_bid, &this_eid, &this_path_start_pos);
      SVN_ERR_ASSERT(n == 3);
      this_path = line->data + this_path_start_pos;
      if (strcmp(this_path, "(null)") != 0)
        {
          branch_mapping_update(branch_instance, this_eid, this_path);
        }
    }

  *new_branch = branch_instance;
  return SVN_NO_ERROR;
}

/* Create a new family *NEW_FAMILY as a sub-family of FAMILY, initialized
 * with info parsed from STREAM, allocated in RESULT_POOL.
 */
static svn_error_t *
parse_family_info(svn_branch_family_t **new_family,
                  int *parent_fid,
                  svn_branch_repos_t *repos,
                  svn_stream_t *stream,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;
  int fid, first_bid, next_bid, first_eid, next_eid;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(!eof);
  n = sscanf(line->data, "f%d: bids %d %d eids %d %d parent-fid %d\n",
             &fid,
             &first_bid, &next_bid, &first_eid, &next_eid,
             parent_fid);
  SVN_ERR_ASSERT(n == 6);

  *new_family = svn_branch_family_create(repos, fid,
                                         first_bid, next_bid,
                                         first_eid, next_eid,
                                         result_pool);

  return SVN_NO_ERROR;
}

/* Initialize REPOS with info parsed from STREAM, allocated in REPOS->pool.
 */
static svn_error_t *
parse_repos_info(svn_branch_repos_t *repos,
                 svn_stream_t *stream,
                 apr_pool_t *scratch_pool)
{
  int root_fid;
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n, i;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(! eof);
  n = sscanf(line->data, "r: fids %*d %d root-fid %d",
             &repos->next_fid, &root_fid);
  SVN_ERR_ASSERT(n == 2);

  /* parse the families */
  for (i = 0; i < repos->next_fid; i++)
    {
      svn_branch_family_t *family;
      int bid, parent_fid;

      SVN_ERR(parse_family_info(&family, &parent_fid, repos, stream,
                                repos->pool, scratch_pool));

      if (family->fid == root_fid)
        {
          repos->root_family = family;
        }
      else
        {
          svn_branch_family_t *parent_family
            = repos_get_family_by_id(repos, parent_fid);

          SVN_ERR_ASSERT(parent_family);
          APR_ARRAY_PUSH(parent_family->sub_families, void *) = family;
        }

      /* parse the branches */
      for (bid = family->first_bid; bid < family->next_bid; ++bid)
        {
          svn_branch_instance_t *branch;

          SVN_ERR(parse_branch_info(&branch, family, stream,
                                    family->pool, scratch_pool));
          APR_ARRAY_PUSH(family->branch_instances, void *) = branch;
        }
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
write_repos_info(svn_stream_t *stream,
                 svn_branch_repos_t *repos,
                 apr_pool_t *scratch_pool);

/* Create a new repository object and read the move-tracking /
 * branch-tracking metadata from the repository into it.
 */
static svn_error_t *
fetch_repos_info(svn_branch_repos_t **repos_p,
                 svn_ra_session_t *ra_session,
                 svn_editor3_t *editor,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_branch_repos_t *repos
    = svn_branch_repos_create(ra_session, editor, result_pool);
  svn_revnum_t youngest_rev;
  svn_string_t *value;
  svn_stream_t *stream;

  /* Read initial state from repository */
  SVN_ERR(svn_ra_get_latest_revnum(ra_session, &youngest_rev, scratch_pool));
  SVN_ERR(svn_ra_rev_prop(ra_session, youngest_rev, "svn-br-info", &value,
                          scratch_pool));
  if (! value)
    {
      value = svn_string_create(default_repos_info, scratch_pool);
      SVN_DBG(("fetch_repos_info: LOADED DEFAULT INFO:\n%s", value->data));
    }
  stream = svn_stream_from_string(value, scratch_pool);

  SVN_ERR(parse_repos_info(repos, stream, scratch_pool));

  /* Self-test: writing out the info should produce exactly the same string. */
  {
    svn_stringbuf_t *buf = svn_stringbuf_create_empty(scratch_pool);

    stream = svn_stream_from_stringbuf(buf, scratch_pool);
    SVN_ERR(write_repos_info(stream, repos, scratch_pool));
    SVN_ERR(svn_stream_close(stream));

    SVN_ERR_ASSERT(svn_string_compare(value,
                                      svn_stringbuf__morph_into_string(buf)));
  }

  *repos_p = repos;
  return SVN_NO_ERROR;
}

/* Write to STREAM a parseable representation of BRANCH.
 */
static svn_error_t *
write_branch_info(svn_stream_t *stream,
                  svn_branch_instance_t *branch,
                  apr_pool_t *scratch_pool)
{
  svn_branch_family_t *family = branch->definition->family;
  int eid;

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "f%db%d: root-eid %d\n",
                            family->fid, branch->definition->bid,
                            branch->definition->root_eid));
  for (eid = family->first_eid; eid < family->next_eid; eid++)
    {
      const char *path = apr_hash_get(branch->eid_to_path, &eid, sizeof (eid));
      if (path)
        {
          SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                    "f%db%de%d: %s\n",
                                    family->fid, branch->definition->bid, eid,
                                    path));
        }
      else
        {
          /* ### TODO: instead of writing this out, write nothing; but the
                 parser can't currently handle that. */
          SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                    "f%db%de%d: %s\n",
                                    family->fid, branch->definition->bid, eid,
                                    "(null)"));
        }
    }
  return SVN_NO_ERROR;
}

/* Write to STREAM a parseable representation of FAMILY whose parent
 * family id is PARENT_FID.
 */
static svn_error_t *
write_family_info(svn_stream_t *stream,
                  svn_branch_family_t *family,
                  int parent_fid,
                  apr_pool_t *scratch_pool)
{
  int i;

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "f%d: bids %d %d eids %d %d parent-fid %d\n",
                            family->fid,
                            family->first_bid, family->next_bid,
                            family->first_eid, family->next_eid,
                            parent_fid));

  for (i = 0; i < family->branch_instances->nelts; i++)
    {
      svn_branch_instance_t *branch
        = APR_ARRAY_IDX(family->branch_instances, i, void *);

      SVN_ERR(write_branch_info(stream, branch, scratch_pool));
    }

  if (family->sub_families)
    {
      for (i = 0; i < family->sub_families->nelts; i++)
        {
          svn_branch_family_t *f
            = APR_ARRAY_IDX(family->sub_families, i, void *);

          SVN_ERR(write_family_info(stream, f, family->fid, scratch_pool));
        }
    }
  return SVN_NO_ERROR;
}

/* Write to STREAM a parseable representation of REPOS.
 */
static svn_error_t *
write_repos_info(svn_stream_t *stream,
                 svn_branch_repos_t *repos,
                 apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "r: fids %d %d root-fid %d\n",
                            0, repos->next_fid,
                            repos->root_family->fid));

  SVN_ERR(write_family_info(stream, repos->root_family, 0, scratch_pool));

  return SVN_NO_ERROR;
}

/* Store the move-tracking / branch-tracking metadata from REPOS into the
 * repository.
 */
static svn_error_t *
store_repos_info(svn_branch_repos_t *repos,
                 apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(scratch_pool);
  svn_stream_t *stream = svn_stream_from_stringbuf(buf, scratch_pool);
  svn_revnum_t youngest_rev;

  SVN_ERR(write_repos_info(stream, repos, scratch_pool));

  SVN_ERR(svn_stream_close(stream));
  /*SVN_DBG(("store_repos_info: %s", buf->data));*/
  SVN_ERR(svn_ra_get_latest_revnum(repos->ra_session, &youngest_rev,
                                   scratch_pool));
  SVN_ERR(svn_ra_change_rev_prop2(repos->ra_session, youngest_rev, "svn-br-info",
                                  NULL, svn_stringbuf__morph_into_string(buf),
                                  scratch_pool));

  return SVN_NO_ERROR;
}

/* Set *INNER_BRANCH_P and *INNER_EID_P to the branch and element located
 * at LOC.
 *
 * LOC should be a member of an immediate sub-branch of OUTER_BRANCH,
 * including the root of such a sub-branch, but not the root of a
 * sub-sub-branch.
 *
 * If not found, set *INNER_BRANCH_P and *INNER_EID_P respectively to NULL
 * and -1.
 */
static svn_error_t *
branch_find_subbranch_element_by_location(svn_branch_instance_t **inner_branch_p,
                                          int *inner_eid_p,
                                          svn_branch_instance_t *outer_branch,
                                          svn_editor3_txn_path_t loc,
                                          apr_pool_t *scratch_pool)
{
  const char *outer_branch_root_rrpath = branch_get_root_path(outer_branch);
  const char *loc_rrpath = txn_path_to_relpath(loc, scratch_pool);
  apr_array_header_t *branch_instances;
  int i;

  if (! svn_relpath_skip_ancestor(outer_branch_root_rrpath, loc_rrpath))
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("location %s is not within branch %d (root path '%s')"),
                               loc_rrpath, outer_branch->definition->bid,
                               outer_branch_root_rrpath);
    }

  branch_instances = branch_get_sub_branches(outer_branch,
                                             scratch_pool, scratch_pool);
  /* Find the sub-branch that encloses LOC_RRPATH */
  for (i = 0; i < branch_instances->nelts; i++)
    {
      svn_branch_instance_t *sub_branch
        = APR_ARRAY_IDX(branch_instances, i, void *);
      const char *sub_branch_root_path = branch_get_root_path(sub_branch);

      if (svn_relpath_skip_ancestor(sub_branch_root_path, loc_rrpath))
        {
          /* The path we're looking for is path-wise in this sub-branch. */
          /* ### TODO: Check it's not a sub-sub-branch. */
          *inner_branch_p = sub_branch;
          *inner_eid_p = branch_get_eid_by_path(sub_branch, loc_rrpath);
          return SVN_NO_ERROR;
        }
    }

  *inner_branch_p = NULL;
  *inner_eid_p = -1;
  return SVN_NO_ERROR;
}

/* Find the (deepest) branch of which the path RRPATH is either the root
 * path or a normal, non-sub-branch path. An element need not exist at
 * RRPATH.
 *
 * Set *BRANCH_P to the deepest branch within ROOT_BRANCH (recursively,
 * including itself) that contains the path RRPATH.
 *
 * If EID_P is not null then set *EID_P to the element id of RRPATH in
 * *BRANCH_P, or to -1 if no element exists at RRPATH in that branch.
 *
 * If RRPATH is not within any branch in ROOT_BRANCH, set *BRANCH_P to
 * NULL and (if EID_P is not null) *EID_P to -1.
 */
static void
branch_find_nested_branch_element_by_path(
                                svn_branch_instance_t **branch_p,
                                int *eid_p,
                                svn_branch_instance_t *root_branch,
                                const char *rrpath,
                                apr_pool_t *scratch_pool)
{
  const char *branch_root_path = branch_get_root_path(root_branch);
  apr_array_header_t *branch_instances;
  int i;

  if (! svn_relpath_skip_ancestor(branch_root_path, rrpath))
    {
      /* The path we're looking for is not (path-wise) in this branch. */
      *branch_p = NULL;
      if (eid_p)
        *eid_p = -1;
      return;
    }

  /* The path we're looking for is (path-wise) in this branch. See if it
     is also in a sub-branch (recursively). */
  branch_instances = branch_get_sub_branches(root_branch,
                                             scratch_pool, scratch_pool);
  for (i = 0; i < branch_instances->nelts; i++)
    {
      svn_branch_instance_t *this_branch
        = APR_ARRAY_IDX(branch_instances, i, void *);
      svn_branch_instance_t *sub_branch;
      int sub_branch_eid;

      branch_find_nested_branch_element_by_path(&sub_branch, &sub_branch_eid,
                                                this_branch, rrpath,
                                                scratch_pool);
      if (sub_branch)
        {
           *branch_p = sub_branch;
           if (eid_p)
             *eid_p = sub_branch_eid;
           return;
         }
    }

  *branch_p = root_branch;
  if (eid_p)
    *eid_p = branch_get_eid_by_path(root_branch, rrpath);
}

/* Find the deepest branch in REPOS (recursively) of which RRPATH is
 * either the root element or a normal, non-sub-branch element.
 * If EID_P is not null, set *EID_P to the EID of RRPATH in that branch.
 *
 * An element need not exist at RRPATH.
 *
 * The result will never be NULL.
 */
static svn_branch_instance_t *
repos_get_branch_by_path(int *eid_p,
                         svn_branch_repos_t *repos,
                         const char *rrpath,
                         apr_pool_t *scratch_pool)
{
  svn_branch_instance_t *branch
    = APR_ARRAY_IDX(repos->root_family->branch_instances, 0, void *);

  branch_find_nested_branch_element_by_path(&branch, eid_p,
                                            branch, rrpath, scratch_pool);

  /* Any path must at least be within the repository root branch */
  SVN_ERR_ASSERT_NO_RETURN(branch);
  return branch;
}

/*  */
static svn_boolean_t
same_branch(const svn_branch_instance_t *branch1,
            const svn_branch_instance_t *branch2)
{
  return (branch1 == branch2);
}

/* If the location LOC is not in the branch BRANCH,
 * throw an error (branch nesting violation).
 */
static svn_error_t *
verify_path_in_branch(const svn_branch_instance_t *branch,
                      const char *rrpath,
                      apr_pool_t *scratch_pool)
{
  svn_branch_instance_t *target_branch
    = repos_get_branch_by_path(NULL, branch->definition->family->repos, rrpath,
                               scratch_pool);

  if (! same_branch(target_branch, branch))
    return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                             _("path '%s' is in branch '%s', "
                               "not in this branch '%s'"),
                             rrpath,
                             branch_get_root_path(target_branch),
                             branch_get_root_path(branch));
  return SVN_NO_ERROR;
}

/* If the location LOC is not in the branch BRANCH,
 * throw an error (branch nesting violation).
 */
static svn_error_t *
verify_source_in_branch(const svn_branch_instance_t *branch,
                        svn_editor3_txn_path_t loc,
                        apr_pool_t *scratch_pool)
{
  const char *rrpath = txn_path_to_relpath(loc, scratch_pool);
  svn_branch_instance_t *target_branch
    = repos_get_branch_by_path(NULL, branch->definition->family->repos, rrpath,
                               scratch_pool);

  if (! same_branch(target_branch, branch))
    return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                             _("source path '%s' is in branch '%s', "
                               "not in this branch '%s'"),
                             txn_path_str(loc, scratch_pool),
                             branch_get_root_path(target_branch),
                             branch_get_root_path(branch));
  return SVN_NO_ERROR;
}

/* If the location TGT_PARENT_LOC is not in the branch BRANCH,
 * throw an error (branch nesting violation).
 */
static svn_error_t *
verify_target_in_branch(const svn_branch_instance_t *branch,
                        svn_editor3_txn_path_t tgt_parent_loc,
                        apr_pool_t *scratch_pool)
{
  const char *rrpath = txn_path_to_relpath(tgt_parent_loc, scratch_pool);
  svn_branch_instance_t *target_branch
    = repos_get_branch_by_path(NULL, branch->definition->family->repos, rrpath,
                               scratch_pool);

  if (! same_branch(target_branch, branch))
    return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                             _("target parent path '%s' is in branch '%s', "
                               "not in this branch '%s'"),
                             txn_path_str(tgt_parent_loc, scratch_pool),
                             branch_get_root_path(target_branch),
                             branch_get_root_path(branch));
  return SVN_NO_ERROR;
}

/* Set *PATHS_P to an array of (const char *) repos-relative paths of
 * all the child elements (recursively) of BRANCH:LOC_RRPATH, excluding
 * itself.
 *
 * LOC_RRPATH must be a path inside BRANCH. If no element of BRANCH
 * exists at LOC_RRPATH, return an empty list.
 */
static svn_error_t *
branch_get_subtree_paths(apr_array_header_t **paths_p,
                         svn_branch_instance_t *branch,
                         const char *loc_rrpath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(svn_relpath_skip_ancestor(branch_get_root_path(branch),
                                           loc_rrpath) != NULL);

  *paths_p = apr_array_make(result_pool, 0, sizeof(char *));
  for (hi = apr_hash_first(scratch_pool, branch->eid_to_path);
       hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_val(hi);
      const char *remainder = svn_relpath_skip_ancestor(loc_rrpath, path);

      if (remainder && remainder[0])
        {
          APR_ARRAY_PUSH(*paths_p, const char *) = path;
        }
    }
  return SVN_NO_ERROR;
}

/* List all branch instances in FAMILY.
 *
 * If RECURSIVE is true, include branches in nested families.
 */
static svn_error_t *
family_list_branch_instances(svn_branch_family_t *family,
                             svn_boolean_t recursive,
                             apr_pool_t *scratch_pool)
{
  int b;

  printf("family %d (BIDs %d:%d, EIDs %d:%d)\n",
         family->fid,
         family->first_bid, family->next_bid,
         family->first_eid, family->next_eid);

  for (b = 0; b < family->branch_instances->nelts; b++)
    {
      svn_branch_instance_t *branch
        = APR_ARRAY_IDX(family->branch_instances, b, svn_branch_instance_t *);
      int eid;

      printf("  branch %d (root element %d -> '%s')\n",
             branch->definition->bid, branch->definition->root_eid,
             branch_get_root_path(branch));
      for (eid = family->first_eid; eid < family->next_eid; eid++)
        {
          printf("    e%d -> %s\n",
                 eid,
                 (char *)apr_hash_get(branch->eid_to_path, &eid, sizeof(eid)));
        }
    }

  if (recursive && family->sub_families)
    {
      int f;

      for (f = 0; f < family->sub_families->nelts; f++)
        {
          svn_branch_family_t *sub_family
            = APR_ARRAY_IDX(family->sub_families, f, svn_branch_family_t *);

          SVN_ERR(family_list_branch_instances(sub_family, recursive,
                                               scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* In BRANCH, make a new directory at PARENT_LOC:NEW_NAME. */
static svn_error_t *
svn_branch_mkdir(svn_branch_instance_t *branch,
                 svn_editor3_txn_path_t parent_loc,
                 const char *new_name,
                 apr_pool_t *scratch_pool)
{
  svn_editor3_t *editor = branch->definition->family->repos->editor;
  svn_editor3_txn_path_t loc = txn_path_join(parent_loc, new_name, scratch_pool);
  const char *loc_rrpath = txn_path_to_relpath(loc, scratch_pool);
  int eid;

  SVN_ERR(verify_target_in_branch(branch, parent_loc, scratch_pool));

  eid = family_add_new_element(branch->definition->family);

  SVN_ERR(svn_editor3_mk(editor, svn_node_dir, parent_loc, new_name));
  branch_mapping_update(branch, eid, loc_rrpath);
  return SVN_NO_ERROR;
}

/* Adjust BRANCH and its subbranches (recursively),
 * to reflect deletion of a subtree from FROM_PATH.
 *
 * FROM_PATH MUST be the location of a non-root element of BRANCH.
 * If FROM_PATH is the root of a subbranch and/or contains nested
 * subbranches, also delete them.
 *
 * <ifdef WITH_OVERLAPPING_EIDS> Also delete from each nested subbranch
 *   that contains FROM_PATH.</>
 */
static svn_error_t *
branch_delete_subtree_r(svn_branch_instance_t *branch,
                        const char *from_path,
                        apr_pool_t *scratch_pool)
{
  int eid;
  apr_array_header_t *subbranches;
  int i;

  /* FROM_PATH MUST be the location of a non-root element of BRANCH. */
  eid = branch_get_eid_by_path(branch, from_path);
  if (eid < 0)
    return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                             _("in branch %d, can't delete '%s': not found"),
                             branch->definition->bid, from_path);
  if (eid == branch->definition->root_eid)
    return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                             _("in branch %d, can't delete '%s': is root of this branch"),
                             branch->definition->bid, from_path);

  /* Delete any nested subbranches at or inside FROM_PATH.
     (If overlapping EIDs supported: also delete overlapping parts.) */
  subbranches = branch_get_sub_branches(branch, scratch_pool, scratch_pool);
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch_instance_t *subbranch
        = APR_ARRAY_IDX(subbranches, i, void *);
      const char *subbranch_root_path = branch_get_root_path(subbranch);
      const char *subbranch_within_from_path
        = svn_relpath_skip_ancestor(from_path, subbranch_root_path);

      if (subbranch_within_from_path)
        {
          /* Delete the whole subbranch (recursively) */
          SVN_ERR(branch_instance_delete_r(subbranch, scratch_pool));
        }

#ifdef WITH_OVERLAPPING_EIDS
      /* If FROM_PATH is inside (but not the root of) this subbranch,
         delete it from this subbranch.
         (It's not the root -- the first 'if' clause caught that.)
       */
      else if (svn_relpath_skip_ancestor(subbranch_root_path, from_path))
        {
          SVN_ERR(branch_delete_subtree_r(subbranch, from_path,
                                          scratch_pool));
        }
#endif
    }

  /* update the path mappings in this branch */
  SVN_ERR(branch_mappings_delete(branch, from_path, TRUE /*include_self*/,
                                 scratch_pool));

  return SVN_NO_ERROR;
}

/* Adjust BRANCH and its subbranches (recursively),
 * to reflect a move of a subtree from FROM_PATH to TO_PATH.
 *
 * FROM_PATH must be an existing element of BRANCH. (It may be the root.)
 * If FROM_PATH is the root of a subbranch and/or contains nested
 * subbranches, also move them.
 *
 * TO_PATH must be a non-existing path in an existing parent directory in
 * BRANCH.
 *
 * <ifdef WITH_OVERLAPPING_EIDS> Also delete from / add to / move within
 *   each nested subbranch that contains FROM_PATH / TO_PATH / both
 *   (respectively). </>
 */
static svn_error_t *
branch_move_subtree_r(svn_branch_instance_t *branch,
                      const char *from_path,
                      const char *to_path,
                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranches;
  int i;

  /* update the path mappings in this branch */
  SVN_ERR(branch_mappings_move(branch, from_path, to_path, scratch_pool));

  /* handle subbranches */
  subbranches = branch_get_sub_branches(branch, scratch_pool, scratch_pool);
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch_instance_t *subbranch
        = APR_ARRAY_IDX(subbranches, i, void *);
      const char *subbranch_root_path = branch_get_root_path(subbranch);
      const char *subbranch_within_from_path
        = svn_relpath_skip_ancestor(from_path, subbranch_root_path);
#ifdef WITH_OVERLAPPING_EIDS
      const char *from_path_within_subbranch
        = svn_relpath_skip_ancestor(subbranch_root_path, from_path);
      const char *to_path_within_subbranch
        = svn_relpath_skip_ancestor(subbranch_root_path, to_path);
#endif

      if (subbranch_within_from_path)
        {
          const char *subbranch_root_to_path
            = svn_relpath_join(to_path, subbranch_within_from_path,
                               scratch_pool);

          /* Move the whole subbranch (recursively) */
          SVN_ERR(branch_move_subtree_r(subbranch,
                                        subbranch_root_path,
                                        subbranch_root_to_path,
                                        scratch_pool));
        }

#ifdef WITH_OVERLAPPING_EIDS
      /* If FROM_PATH or TO_PATH is inside (but not the root of) this
         subbranch, move within or delete from or add to this subbranch.
         (FROM_PATH is not the root -- the first 'if' clause caught that.)
         (TO_PATH is not the root -- the root exists and TO_PATH doesn't.)
       */
      else if (from_path_within_subbranch && to_path_within_subbranch)
        {
          SVN_ERR(branch_move_subtree_r(subbranch, from_path, to_path,
                                        scratch_pool));
        }
      else if (from_path_within_subbranch)
        {
          SVN_ERR(branch_delete_subtree_r(subbranch, from_path,
                                          scratch_pool));
        }
      else if (to_path_within_subbranch)
        {
          /* ### Copy the external subtree at FROM_PATH (including nested
                 branches? -- but they're overlapping anyway so no need)
                 to SUBBRANCH:TO_PATH, as added or "copied"? elements. */
        }
#endif
    }
  return SVN_NO_ERROR;
}

/* Adjust OUTER_BRANCH and its subbranches (recursively),
 * to reflect branching a subtree from FROM_BRANCH:FROM_PATH to
 * create a new subbranch of OUTER_BRANCH at TO_PATH.
 *
 * FROM_BRANCH must be an immediate child branch of OUTER_BRANCH.
 *
 * FROM_PATH must be an existing element of FROM_BRANCH. It may be the
 * root of FROM_BRANCH. It must not be the root of a subbranch of
 * FROM_BRANCH.
 *
 * TO_PATH must be a non-existing path in an existing parent directory in
 * OUTER_BRANCH.
 *
 * <ifdef WITH_OVERLAPPING_EIDS> ### ? </>
 */
static svn_error_t *
branch_branch_subtree_r(svn_branch_instance_t **new_branch_p,
                        svn_branch_instance_t *outer_branch,
                        svn_branch_instance_t *from_branch,
                        const char *from_path,
                        const char *to_path,
                        apr_pool_t *scratch_pool)
{
  int inner_eid = branch_get_eid_by_path(from_branch, from_path);
  int to_outer_eid;
  svn_branch_instance_t *new_branch;
  apr_array_header_t *subbranches;
  int i;

  /* FROM_BRANCH must be an immediate child branch of OUTER_BRANCH. */
  /* SVN_ERR_ASSERT(...); */

  /* SVN_ERR_ASSERT(...); */

  /* assign new eid to root node (outer branch) */
  to_outer_eid = family_add_new_element(outer_branch->definition->family);
  branch_mapping_update(outer_branch, to_outer_eid, to_path);

  /* create new inner branch definition & instance */
  new_branch = family_add_new_branch(from_branch->definition->family,
                                     inner_eid, to_path);

  /* populate new branch instance with path mappings */
  SVN_ERR(branch_mappings_branch(from_branch, from_path,
                                 new_branch, to_path, scratch_pool));

  /* branch any subbranches of FROM_BRANCH */
  subbranches = branch_get_sub_branches(from_branch, scratch_pool, scratch_pool);
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch_instance_t *subbranch
        = APR_ARRAY_IDX(subbranches, i, void *);
      const char *subbranch_root_path = branch_get_root_path(subbranch);
      const char *subbranch_within_from_path
        = svn_relpath_skip_ancestor(from_path, subbranch_root_path);

      if (subbranch_within_from_path)
        {
          const char *subbranch_root_to_path
            = svn_relpath_join(to_path, subbranch_within_from_path,
                               scratch_pool);

          /* branch this subbranch into NEW_BRANCH (recursing) */
          SVN_ERR(branch_branch_subtree_r(NULL,
                                          new_branch,
                                          subbranch, subbranch_root_path,
                                          subbranch_root_to_path,
                                          scratch_pool));
        }
    }

  if (new_branch_p)
    *new_branch_p = new_branch;
  return SVN_NO_ERROR;
}

/* Adjust BRANCH and its subbranches (recursively),
 * to reflect a copy of a subtree from FROM_PATH to TO_PATH.
 *
 * FROM_PATH must be an existing element of BRANCH. (It may be the root.)
 * If FROM_PATH is the root of a subbranch and/or contains nested
 * subbranches, also copy them (by branching).
 *
 * TO_PATH must be a non-existing path in an existing parent directory in
 * BRANCH.
 *
 * <ifdef WITH_OVERLAPPING_EIDS> Also delete from / add to / copy within
 *   each nested subbranch that contains FROM_PATH / TO_PATH / both
 *   (respectively). </>
 */
static svn_error_t *
branch_copy_subtree_r(svn_branch_instance_t *branch,
                      const char *from_path,
                      const char *to_path,
                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranches;
  int i;

  /* assign new EIDs and update the path mappings in this branch */
  SVN_ERR(branch_mappings_copy(branch, from_path,
                               branch, to_path,
                               TRUE /*include_self*/, scratch_pool));

  /* handle subbranches */
  subbranches = branch_get_sub_branches(branch, scratch_pool, scratch_pool);
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch_instance_t *subbranch
        = APR_ARRAY_IDX(subbranches, i, void *);
      const char *subbranch_root_path = branch_get_root_path(subbranch);
      const char *subbranch_within_from_path
        = svn_relpath_skip_ancestor(from_path, subbranch_root_path);
#ifdef WITH_OVERLAPPING_EIDS
      const char *from_path_within_subbranch
        = svn_relpath_skip_ancestor(subbranch_root_path, from_path);
      const char *to_path_within_subbranch
        = svn_relpath_skip_ancestor(subbranch_root_path, to_path);
#endif

      if (subbranch_within_from_path)
        {
          const char *subbranch_root_to_path
            = svn_relpath_join(to_path, subbranch_within_from_path,
                               scratch_pool);

          /* branch the whole subbranch (recursively) */
          SVN_ERR(branch_branch_subtree_r(NULL,
                                          branch,
                                          subbranch, subbranch_root_path,
                                          subbranch_root_to_path,
                                          scratch_pool));
        }

#ifdef WITH_OVERLAPPING_EIDS
      /* If FROM_PATH or TO_PATH is inside (but not the root of) this
         subbranch, copy within or branch from or add to this subbranch.
         (FROM_PATH is not the root -- the first 'if' clause caught that.)
         (TO_PATH is not the root -- the root exists and TO_PATH doesn't.)
       */
      else if (from_path_within_subbranch && to_path_within_subbranch)
        {
          SVN_ERR(branch_copy_subtree_r(subbranch, from_path, to_path,
                                        scratch_pool));
        }
      else if (from_path_within_subbranch)
        {
          SVN_ERR(branch_branch_subtree_r(branch,
                                          subbranch, from_path,
                                          to_path,
                                          scratch_pool));
        }
      else if (to_path_within_subbranch)
        {
          /* ### Copy the external subtree at FROM_PATH (including nested
                 branches? -- but they're overlapping anyway so no need)
                 to SUBBRANCH:TO_PATH, as added or "copied"? elements. */
        }
#endif
    }
  return SVN_NO_ERROR;
}

/* In BRANCH, move the subtree at FROM_LOC to PARENT_LOC:NEW_NAME.
 *
 * FROM_LOC must be an existing non-root element of BRANCH. It may also
 * be the root of a subbranch and/or contain nested subbranches; these
 * will also be moved.
 *
 * PARENT_LOC must be an existing directory element in BRANCH.
 * <ifdef WITH_OVERLAPPING_EIDS> PARENT_LOC may be a subbranch root or
 * inside a subbranch. <else> PARENT_LOC is not a subbranch root. </>.
 */
static svn_error_t *
svn_branch_mv(svn_branch_instance_t *branch,
              svn_editor3_txn_path_t from_loc,
              svn_editor3_txn_path_t parent_loc,
              const char *new_name,
              apr_pool_t *scratch_pool)
{
  svn_editor3_t *editor = branch->definition->family->repos->editor;
  svn_editor3_txn_path_t to_loc = txn_path_join(parent_loc, new_name, scratch_pool);
  const char *from_path = txn_path_to_relpath(from_loc, scratch_pool);
  const char *to_path = txn_path_to_relpath(to_loc, scratch_pool);

  SVN_ERR(verify_source_in_branch(branch, from_loc, scratch_pool));
  SVN_ERR(verify_target_in_branch(branch, parent_loc, scratch_pool));

  SVN_ERR(svn_editor3_mv(editor, from_loc.peg, parent_loc, new_name));

  SVN_ERR(branch_move_subtree_r(branch, from_path, to_path, scratch_pool));
  return SVN_NO_ERROR;
}

/* In BRANCH, copy the subtree at FROM_LOC to PARENT_LOC:NEW_NAME.
 * Any nested subbranches inside FROM_LOC will be copied by branching.
 *
 * FROM_LOC must be an existing (root or non-root) element of BRANCH,
 * and must not be the root of a subbranch of BRANCH.
 *
 * PARENT_LOC must be an existing directory element in BRANCH.
 * <ifdef WITH_OVERLAPPING_EIDS> PARENT_LOC may be a subbranch root or
 * inside a subbranch. <else> PARENT_LOC is not a subbranch root. </>.
 */
static svn_error_t *
svn_branch_cp(svn_branch_instance_t *branch,
              svn_editor3_txn_path_t from_loc,
              svn_editor3_txn_path_t parent_loc,
              const char *new_name,
              apr_pool_t *scratch_pool)
{
  svn_editor3_t *editor = branch->definition->family->repos->editor;
  svn_editor3_txn_path_t to_loc = txn_path_join(parent_loc, new_name, scratch_pool);
  const char *from_path = txn_path_to_relpath(from_loc, scratch_pool);
  const char *to_path = txn_path_to_relpath(to_loc, scratch_pool);

  SVN_ERR(verify_source_in_branch(branch, from_loc, scratch_pool));
  SVN_ERR(verify_target_in_branch(branch, parent_loc, scratch_pool));

  SVN_ERR(svn_editor3_cp(editor, from_loc.peg, parent_loc, new_name));

  SVN_ERR(branch_copy_subtree_r(branch, from_path, to_path, scratch_pool));
  return SVN_NO_ERROR;
}

/* In OUTER_BRANCH, branch the existing sub-branch at FROM_LOC to create
 * a new branch at PARENT_LOC:NEW_NAME.
 *
 * FROM_LOC must be (root or non-root) path of an immediate sub-branch of
 * OUTER_BRANCH.
 */
static svn_error_t *
svn_branch_branch(svn_branch_instance_t *outer_branch,
                  svn_editor3_txn_path_t from_loc,
                  svn_editor3_txn_path_t parent_loc,
                  const char *new_name,
                  apr_pool_t *scratch_pool)
{
  svn_editor3_t *editor = outer_branch->definition->family->repos->editor;
  const char *from_rrpath = txn_path_to_relpath(from_loc, scratch_pool);
  svn_editor3_txn_path_t to_loc
    = txn_path_join(parent_loc, new_name, scratch_pool);
  const char *to_rrpath = txn_path_to_relpath(to_loc, scratch_pool);
  svn_branch_instance_t *from_inner_branch;
  int inner_eid;

  SVN_ERR(branch_find_subbranch_element_by_location(
            &from_inner_branch, &inner_eid,
            outer_branch, from_loc, scratch_pool));
  if (! from_inner_branch)
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("cannot branch from '%s': "
                                 "is not a sub-branch of '%s'"),
                               txn_path_str(from_loc, scratch_pool),
                               branch_get_root_path(outer_branch));
    }
  if (inner_eid < 0)
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("cannot branch from '%s': "
                                 "does not exist"),
                               txn_path_str(from_loc, scratch_pool));
    }

  SVN_ERR(verify_target_in_branch(outer_branch, parent_loc, scratch_pool));

  /* copy */
  SVN_ERR(svn_editor3_cp(editor, from_loc.peg, parent_loc, new_name));

  SVN_ERR(branch_branch_subtree_r(NULL,
                                  outer_branch,
                                  from_inner_branch, from_rrpath,
                                  to_rrpath,
                                  scratch_pool));

  return SVN_NO_ERROR;
}

/* Change the existing simple sub-tree at LOC into a sub-branch in a
 * new branch family.
 *
 * ### TODO: Also we must (in order to maintain correctness) branchify
 *     the corresponding subtrees in all other branches in this family.
 *
 * TODO: Allow adding to an existing family, by specifying a mapping.
 *
 *   create a new family
 *   create a new branch-def and branch-instance
 *   for each node in subtree:
 *     ?[unassign eid in outer branch (except root node)]
 *     assign a new eid in inner branch
 */
static svn_error_t *
svn_branch_branchify(svn_branch_instance_t *outer_branch,
                     svn_editor3_txn_path_t new_root_loc,
                     apr_pool_t *scratch_pool)
{
  /* ### TODO: First check ROOT_LOC is not already a branch root
         and the subtree at ROOT_LOC does not contain any branch roots. */

  svn_branch_family_t *new_family
    = family_add_new_subfamily(outer_branch->definition->family);
  int new_root_eid = family_add_new_element(new_family);
  const char *new_root_rrpath = txn_path_to_relpath(new_root_loc, scratch_pool);
  svn_branch_instance_t *new_branch
    = family_add_new_branch(new_family, new_root_eid, new_root_rrpath);

  SVN_DBG(("branchify(%s): new fid=%d, bid=%d",
           txn_path_str(new_root_loc, scratch_pool), new_family->fid, new_branch->definition->bid));

  /* assign new EIDs and update the path mappings in this branch */
  SVN_ERR(branch_mappings_copy(outer_branch, new_root_rrpath,
                               new_branch, new_root_rrpath,
                               FALSE /*include_self*/, scratch_pool));

#ifndef WITH_OVERLAPPING_EIDS
  /* remove old EIDs in outer branch */
  SVN_ERR(branch_mappings_delete(outer_branch, new_root_rrpath,
                                 FALSE /*include_self*/, scratch_pool));
#endif

  return SVN_NO_ERROR;
}

/* In BRANCH, remove the subtree at LOC, including any nested branches.
 *
 * LOC MUST be the location of a non-root element of BRANCH.
 * <ifdef WITH_OVERLAPPING_EIDS>
 *   LOC may be both in BRANCH and in one or more nested subbranches.</>
 */
static svn_error_t *
svn_branch_rm(svn_branch_instance_t *branch,
              svn_editor3_txn_path_t loc,
              apr_pool_t *scratch_pool)
{
  svn_editor3_t *editor = branch->definition->family->repos->editor;
  const char *rrpath = txn_path_to_relpath(loc, scratch_pool);

  SVN_ERR(svn_editor3_rm(editor, loc));

  SVN_ERR(branch_delete_subtree_r(branch, rrpath, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
execute(const char *branch_rrpath,
        const apr_array_header_t *actions,
        const char *anchor_url,
        const char *log_msg,
        apr_hash_t *revprops,
        svn_revnum_t base_revision,
        svn_client_ctx_t *ctx,
        apr_pool_t *pool)
{
  mtcc_t *mtcc;
  svn_editor3_t *editor;
  const char *repos_root_url;
  svn_branch_repos_t *repos;
  svn_branch_instance_t *branch;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_boolean_t made_changes = FALSE;
  int i;
  svn_error_t *err;

  /* Put the log message in the list of revprops, and check that the user
     did not try to supply any other "svn:*" revprops. */
  if (svn_prop_has_svn_prop(revprops, pool))
    return svn_error_create(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                            _("Standard properties can't be set "
                              "explicitly as revision properties"));
  svn_hash_sets(revprops, SVN_PROP_REVISION_LOG,
                svn_string_create(log_msg, pool));

  SVN_ERR(mtcc_create(&mtcc,
                      anchor_url, base_revision, revprops,
                      commit_callback, NULL,
                      ctx, pool, iterpool));
  editor = mtcc->editor;
  repos_root_url = mtcc->repos_root_url;
  base_revision = mtcc->base_revision;
  SVN_ERR(fetch_repos_info(&repos, mtcc->ra_session, editor, pool, pool));
  branch = repos_get_branch_by_path(NULL, repos, branch_rrpath, pool);
  SVN_DBG(("look up path '%s': found branch f%db%de%d at path '%s'",
           branch_rrpath, branch->definition->family->fid,
           branch->definition->bid, branch->definition->root_eid,
           branch_get_root_path(branch)));

  for (i = 0; i < actions->nelts; ++i)
    {
      struct action *action = APR_ARRAY_IDX(actions, i, struct action *);
      svn_editor3_txn_path_t path1_txn_loc = {{0},0};
      svn_editor3_txn_path_t path1_parent = {{0},0};
      const char *path1_name = NULL;
      svn_editor3_txn_path_t path2_parent = {{0},0};
      const char *path2_name = NULL;

      svn_pool_clear(iterpool);

      if (action->path[0])
        {
          const char *rrpath1
            = svn_uri_skip_ancestor(repos_root_url, action->path[0], pool);
          path1_txn_loc = txn_path(rrpath1, base_revision, ""); /* ### need to
            find which part of given path was pre-existing and which was created */
          path1_parent = txn_path(svn_relpath_dirname(rrpath1, pool), base_revision, ""); /* ### need to
            find which part of given path was pre-existing and which was created */
          path1_name = svn_relpath_basename(rrpath1, NULL);
        }
      if (action->path[1])
        {
          const char *rrpath2
            = svn_uri_skip_ancestor(repos_root_url, action->path[1], pool);
          path2_parent = txn_path(svn_relpath_dirname(rrpath2, pool), base_revision, ""); /* ### need to
            find which part of given path was pre-existing and which was created */
          path2_name = svn_relpath_basename(rrpath2, NULL);
        }
      switch (action->action)
        {
        case ACTION_LIST_BRANCHES:
          SVN_ERR(family_list_branch_instances(branch->definition->family,
                                               FALSE, iterpool));
          break;
        case ACTION_LIST_BRANCHES_R:
          SVN_ERR(family_list_branch_instances(branch->definition->family,
                                               TRUE, iterpool));
          break;
        case ACTION_BRANCH:
          SVN_ERR(svn_branch_branch(branch,
                                    path1_txn_loc, path2_parent, path2_name,
                                    iterpool));
          made_changes = TRUE;
          break;
        case ACTION_BRANCHIFY:
          SVN_ERR(svn_branch_branchify(branch,
                                       path1_txn_loc,
                                       iterpool));
          made_changes = TRUE;
          break;
        case ACTION_DISSOLVE:
          return svn_error_create(SVN_ERR_BRANCHING, NULL,
                                  _("'dissolve' operation not implemented"));
          made_changes = TRUE;
          break;
        case ACTION_MV:
          SVN_ERR(svn_branch_mv(branch,
                                path1_txn_loc, path2_parent, path2_name,
                                iterpool));
          made_changes = TRUE;
          break;
        case ACTION_CP:
          path1_txn_loc.peg.rev = action->rev;
          SVN_ERR(svn_branch_cp(branch,
                                path1_txn_loc, path2_parent, path2_name,
                                iterpool));
          made_changes = TRUE;
          break;
        case ACTION_RM:
          SVN_ERR(svn_branch_rm(branch,
                                path1_txn_loc,
                                iterpool));
          made_changes = TRUE;
          break;
        case ACTION_MKDIR:
          SVN_ERR(svn_branch_mkdir(branch,
                                   path1_parent, path1_name,
                                   iterpool));
          made_changes = TRUE;
          break;
        default:
          SVN_ERR_MALFUNCTION();
        }
    }

  if (made_changes)
    {
      err = mtcc_commit(mtcc, pool);
      if (!err)
        err = store_repos_info(repos, pool);
    }
  else
    {
      err = svn_editor3_abort(mtcc->editor);
    }

  svn_pool_destroy(mtcc->pool);

  svn_pool_destroy(iterpool);
  return svn_error_trace(err);
}

/* Perform the typical suite of manipulations for user-provided URLs
   on URL, returning the result (allocated from POOL): IRI-to-URI
   conversion, auto-escaping, and canonicalization. */
static const char *
sanitize_url(const char *url,
             apr_pool_t *pool)
{
  url = svn_path_uri_from_iri(url, pool);
  url = svn_path_uri_autoescape(url, pool);
  return svn_uri_canonicalize(url, pool);
}

/* Print a usage message on STREAM. */
static void
usage(FILE *stream, apr_pool_t *pool)
{
  svn_error_clear(svn_cmdline_fputs(
    _("usage: svnmover ACTION...\n"
      "Subversion mover command client.\n"
      "Type 'svnmover --version' to see the program version.\n"
      "\n"
      "  Perform one or more Subversion repository URL-based ACTIONs, committing\n"
      "  the result as a (single) new revision.\n"
      "\n"
      "Actions:\n"
      "  ls-br                  : list all branches\n"
      "  branch SRC DST         : branch the (sub)branch at SRC to make a new branch\n"
      "                           at DST (presently, SRC must be a branch root)\n"
      "  branchify BR-ROOT      : change the existing simple sub-tree at SRC into\n"
      "                           a sub-branch (presently, in a new branch family)\n"
      "  dissolve BR-ROOT       : change the existing sub-branch at SRC into a\n"
      "                           simple sub-tree of its parent branch\n"
      "  cp REV SRC-URL DST-URL : copy SRC-URL@REV to DST-URL\n"
      "  mv SRC-URL DST-URL     : move SRC-URL to DST-URL\n"
      "  rm URL                 : delete URL\n"
      "  mkdir URL              : create new directory URL\n"
      "\n"
      "Valid options:\n"
      "  -h, -? [--help]        : display this text\n"
      "  -b [--branch] ARG      : work in branch of path ARG (default: root)\n"
      "  -m [--message] ARG     : use ARG as a log message\n"
      "  -F [--file] ARG        : read log message from file ARG\n"
      "  -u [--username] ARG    : commit the changes as username ARG\n"
      "  -p [--password] ARG    : use ARG as the password\n"
      "  -U [--root-url] ARG    : interpret all action URLs relative to ARG\n"
      "  -r [--revision] ARG    : use revision ARG as baseline for changes\n"
      "  --with-revprop ARG     : set revision property in the following format:\n"
      "                               NAME[=VALUE]\n"
      "  --non-interactive      : do no interactive prompting (default is to\n"
      "                           prompt only if standard input is a terminal)\n"
      "  --force-interactive    : do interactive prompting even if standard\n"
      "                           input is not a terminal\n"
      "  --trust-server-cert    : accept SSL server certificates from unknown\n"
      "                           certificate authorities without prompting (but\n"
      "                           only with '--non-interactive')\n"
      "  -X [--extra-args] ARG  : append arguments from file ARG (one per line;\n"
      "                           use \"-\" to read from standard input)\n"
      "  --config-dir ARG       : use ARG to override the config directory\n"
      "  --config-option ARG    : use ARG to override a configuration option\n"
      "  --no-auth-cache        : do not cache authentication tokens\n"
      "  --version              : print version information\n"),
                  stream, pool));
}

static svn_error_t *
insufficient(void)
{
  return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                          "insufficient arguments");
}

static svn_error_t *
display_version(apr_getopt_t *os, apr_pool_t *pool)
{
  const char *ra_desc_start
    = "The following repository access (RA) modules are available:\n\n";
  svn_stringbuf_t *version_footer;

  version_footer = svn_stringbuf_create(ra_desc_start, pool);
  SVN_ERR(svn_ra_print_modules(version_footer, pool));

  SVN_ERR(svn_opt_print_help4(os, "svnmover", TRUE, FALSE, FALSE,
                              version_footer->data,
                              NULL, NULL, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}

/* Return an error about the mutual exclusivity of the -m, -F, and
   --with-revprop=svn:log command-line options. */
static svn_error_t *
mutually_exclusive_logs_error(void)
{
  return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                          _("--message (-m), --file (-F), and "
                            "--with-revprop=svn:log are mutually "
                            "exclusive"));
}

/* Obtain the log message from multiple sources, producing an error
   if there are multiple sources. Store the result in *FINAL_MESSAGE.  */
static svn_error_t *
sanitize_log_sources(const char **final_message,
                     const char *message,
                     apr_hash_t *revprops,
                     svn_stringbuf_t *filedata,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_string_t *msg;

  *final_message = NULL;
  /* If we already have a log message in the revprop hash, then just
     make sure the user didn't try to also use -m or -F.  Otherwise,
     we need to consult -m or -F to find a log message, if any. */
  msg = svn_hash_gets(revprops, SVN_PROP_REVISION_LOG);
  if (msg)
    {
      if (filedata || message)
        return mutually_exclusive_logs_error();

      *final_message = apr_pstrdup(result_pool, msg->data);

      /* Will be re-added by libsvn_client */
      svn_hash_sets(revprops, SVN_PROP_REVISION_LOG, NULL);
    }
  else if (filedata)
    {
      if (message)
        return mutually_exclusive_logs_error();

      *final_message = apr_pstrdup(result_pool, filedata->data);
    }
  else if (message)
    {
      *final_message = apr_pstrdup(result_pool, message);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
log_message_func(const char **log_msg,
                 svn_boolean_t non_interactive,
                 const char *log_message,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  if (log_message)
    {
      svn_string_t *message = svn_string_create(log_message, pool);

      SVN_ERR_W(svn_subst_translate_string2(&message, NULL, NULL,
                                            message, NULL, FALSE,
                                            pool, pool),
                _("Error normalizing log message to internal format"));

      *log_msg = message->data;

      return SVN_NO_ERROR;
    }

  if (non_interactive)
    {
      return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                              _("Cannot invoke editor to get log message "
                                "when non-interactive"));
    }
  else
    {
      svn_string_t *msg = svn_string_create("", pool);

      SVN_ERR(svn_cmdline__edit_string_externally(
                      &msg, NULL, NULL, "", msg, "svnmover-commit",
                      ctx->config, TRUE, NULL, pool));

      if (msg && msg->data)
        *log_msg = msg->data;
      else
        *log_msg = NULL;

      return SVN_NO_ERROR;
    }
}

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  apr_array_header_t *actions = apr_array_make(pool, 1,
                                               sizeof(struct action *));
  const char *anchor = NULL;
  svn_error_t *err = SVN_NO_ERROR;
  apr_getopt_t *opts;
  enum {
    config_dir_opt = SVN_OPT_FIRST_LONGOPT_ID,
    config_inline_opt,
    no_auth_cache_opt,
    version_opt,
    with_revprop_opt,
    non_interactive_opt,
    force_interactive_opt,
    trust_server_cert_opt
  };
  static const apr_getopt_option_t options[] = {
    {"branch", 'b', 1, ""},
    {"message", 'm', 1, ""},
    {"file", 'F', 1, ""},
    {"username", 'u', 1, ""},
    {"password", 'p', 1, ""},
    {"root-url", 'U', 1, ""},
    {"revision", 'r', 1, ""},
    {"with-revprop",  with_revprop_opt, 1, ""},
    {"extra-args", 'X', 1, ""},
    {"help", 'h', 0, ""},
    {NULL, '?', 0, ""},
    {"non-interactive", non_interactive_opt, 0, ""},
    {"force-interactive", force_interactive_opt, 0, ""},
    {"trust-server-cert", trust_server_cert_opt, 0, ""},
    {"config-dir", config_dir_opt, 1, ""},
    {"config-option",  config_inline_opt, 1, ""},
    {"no-auth-cache",  no_auth_cache_opt, 0, ""},
    {"version", version_opt, 0, ""},
    {NULL, 0, 0, NULL}
  };
  const char *branch_rrpath = "";
  const char *message = "";
  svn_stringbuf_t *filedata = NULL;
  const char *username = NULL, *password = NULL;
  const char *root_url = NULL, *extra_args_file = NULL;
  const char *config_dir = NULL;
  apr_array_header_t *config_options;
  svn_boolean_t non_interactive = FALSE;
  svn_boolean_t force_interactive = FALSE;
  svn_boolean_t trust_server_cert = FALSE;
  svn_boolean_t no_auth_cache = FALSE;
  svn_revnum_t base_revision = SVN_INVALID_REVNUM;
  apr_array_header_t *action_args;
  apr_hash_t *revprops = apr_hash_make(pool);
  apr_hash_t *cfg_hash;
  svn_config_t *cfg_config;
  svn_client_ctx_t *ctx;
  const char *log_msg;
  int i;

  /* Check library versions */
  SVN_ERR(check_lib_versions());

  config_options = apr_array_make(pool, 0,
                                  sizeof(svn_cmdline__config_argument_t*));

  apr_getopt_init(&opts, pool, argc, argv);
  opts->interleave = 1;
  while (1)
    {
      int opt;
      const char *arg;
      const char *opt_arg;

      apr_status_t status = apr_getopt_long(opts, options, &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        return svn_error_wrap_apr(status, "getopt failure");
      switch(opt)
        {
        case 'b':
          SVN_ERR(svn_utf_cstring_to_utf8(&branch_rrpath, arg, pool));
          break;
        case 'm':
          SVN_ERR(svn_utf_cstring_to_utf8(&message, arg, pool));
          break;
        case 'F':
          {
            const char *arg_utf8;
            SVN_ERR(svn_utf_cstring_to_utf8(&arg_utf8, arg, pool));
            SVN_ERR(svn_stringbuf_from_file2(&filedata, arg, pool));
          }
          break;
        case 'u':
          username = apr_pstrdup(pool, arg);
          break;
        case 'p':
          password = apr_pstrdup(pool, arg);
          break;
        case 'U':
          SVN_ERR(svn_utf_cstring_to_utf8(&root_url, arg, pool));
          if (! svn_path_is_url(root_url))
            return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                     "'%s' is not a URL\n", root_url);
          root_url = sanitize_url(root_url, pool);
          anchor = root_url;
          break;
        case 'r':
          {
            const char *saved_arg = arg;
            char *digits_end = NULL;
            while (*arg == 'r')
              arg++;
            base_revision = strtol(arg, &digits_end, 10);
            if ((! SVN_IS_VALID_REVNUM(base_revision))
                || (! digits_end)
                || *digits_end)
              return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                       _("Invalid revision number '%s'"),
                                       saved_arg);
          }
          break;
        case with_revprop_opt:
          SVN_ERR(svn_opt_parse_revprop(&revprops, arg, pool));
          break;
        case 'X':
          extra_args_file = apr_pstrdup(pool, arg);
          break;
        case non_interactive_opt:
          non_interactive = TRUE;
          break;
        case force_interactive_opt:
          force_interactive = TRUE;
          break;
        case trust_server_cert_opt:
          trust_server_cert = TRUE;
          break;
        case config_dir_opt:
          SVN_ERR(svn_utf_cstring_to_utf8(&config_dir, arg, pool));
          break;
        case config_inline_opt:
          SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, arg, pool));
          SVN_ERR(svn_cmdline__parse_config_option(config_options, opt_arg,
                                                   pool));
          break;
        case no_auth_cache_opt:
          no_auth_cache = TRUE;
          break;
        case version_opt:
          SVN_ERR(display_version(opts, pool));
          return SVN_NO_ERROR;
        case 'h':
        case '?':
          usage(stdout, pool);
          return SVN_NO_ERROR;
        }
    }

  if (non_interactive && force_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--non-interactive and --force-interactive "
                                "are mutually exclusive"));
    }
  else
    non_interactive = !svn_cmdline__be_interactive(non_interactive,
                                                   force_interactive);

  if (trust_server_cert && !non_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--trust-server-cert requires "
                                "--non-interactive"));
    }

  /* Copy the rest of our command-line arguments to an array,
     UTF-8-ing them along the way. */
  action_args = apr_array_make(pool, opts->argc, sizeof(const char *));
  while (opts->ind < opts->argc)
    {
      const char *arg = opts->argv[opts->ind++];
      SVN_ERR(svn_utf_cstring_to_utf8(&APR_ARRAY_PUSH(action_args,
                                                      const char *),
                                      arg, pool));
    }

  /* If there are extra arguments in a supplementary file, tack those
     on, too (again, in UTF8 form). */
  if (extra_args_file)
    {
      const char *extra_args_file_utf8;
      svn_stringbuf_t *contents, *contents_utf8;

      SVN_ERR(svn_utf_cstring_to_utf8(&extra_args_file_utf8,
                                      extra_args_file, pool));
      SVN_ERR(svn_stringbuf_from_file2(&contents, extra_args_file_utf8, pool));
      SVN_ERR(svn_utf_stringbuf_to_utf8(&contents_utf8, contents, pool));
      svn_cstring_split_append(action_args, contents_utf8->data, "\n\r",
                               FALSE, pool);
    }

  /* Now initialize the client context */

  err = svn_config_get_config(&cfg_hash, config_dir, pool);
  if (err)
    {
      /* Fallback to default config if the config directory isn't readable
         or is not a directory. */
      if (APR_STATUS_IS_EACCES(err->apr_err)
          || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err))
        {
          svn_handle_warning2(stderr, err, "svnmover: ");
          svn_error_clear(err);

          SVN_ERR(svn_config__get_default_config(&cfg_hash, pool));
        }
      else
        return err;
    }

  if (config_options)
    {
      svn_error_clear(
          svn_cmdline__apply_config_options(cfg_hash, config_options,
                                            "svnmover: ", "--config-option"));
    }

  SVN_ERR(svn_client_create_context2(&ctx, cfg_hash, pool));

  cfg_config = svn_hash_gets(cfg_hash, SVN_CONFIG_CATEGORY_CONFIG);
  SVN_ERR(svn_cmdline_create_auth_baton(&ctx->auth_baton,
                                        non_interactive,
                                        username,
                                        password,
                                        config_dir,
                                        no_auth_cache,
                                        trust_server_cert,
                                        cfg_config,
                                        ctx->cancel_func,
                                        ctx->cancel_baton,
                                        pool));

  /* Make sure we have a log message to use. */
  SVN_ERR(sanitize_log_sources(&log_msg, message, revprops, filedata,
                               pool, pool));

  /* Get the commit log message */
  SVN_ERR(log_message_func(&log_msg, non_interactive, log_msg, ctx, pool));
  if (! log_msg)
    return SVN_NO_ERROR;

  /* Now, we iterate over the combined set of arguments -- our actions. */
  for (i = 0; i < action_args->nelts; ++i)
    {
      int j, num_url_args;
      const char *action_string = APR_ARRAY_IDX(action_args, i, const char *);
      struct action *action = apr_pcalloc(pool, sizeof(*action));

      /* First, parse the action. */
      if (! strcmp(action_string, "ls-br"))
        action->action = ACTION_LIST_BRANCHES;
      else if (! strcmp(action_string, "ls-br-r"))
        action->action = ACTION_LIST_BRANCHES_R;
      else if (! strcmp(action_string, "branch"))
        action->action = ACTION_BRANCH;
      else if (! strcmp(action_string, "branchify"))
        action->action = ACTION_BRANCHIFY;
      else if (! strcmp(action_string, "dissolve"))
        action->action = ACTION_DISSOLVE;
      else if (! strcmp(action_string, "mv"))
        action->action = ACTION_MV;
      else if (! strcmp(action_string, "cp"))
        action->action = ACTION_CP;
      else if (! strcmp(action_string, "mkdir"))
        action->action = ACTION_MKDIR;
      else if (! strcmp(action_string, "rm"))
        action->action = ACTION_RM;
      else if (! strcmp(action_string, "?") || ! strcmp(action_string, "h")
               || ! strcmp(action_string, "help"))
        {
          usage(stdout, pool);
          return SVN_NO_ERROR;
        }
      else
        return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                 "'%s' is not an action\n",
                                 action_string);

      /* For copies, there should be a revision number next. */
      if (action->action == ACTION_CP)
        {
          const char *rev_str;

          if (++i == action_args->nelts)
            return svn_error_trace(insufficient());
          rev_str = APR_ARRAY_IDX(action_args, i, const char *);
          if (strcmp(rev_str, "head") == 0)
            action->rev = SVN_INVALID_REVNUM;
          else if (strcmp(rev_str, "HEAD") == 0)
            action->rev = SVN_INVALID_REVNUM;
          else
            {
              char *end;

              while (*rev_str == 'r')
                ++rev_str;

              action->rev = strtol(rev_str, &end, 0);
              if (*end)
                return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                         "'%s' is not a revision\n",
                                         rev_str);
            }
        }
      else
        {
          action->rev = SVN_INVALID_REVNUM;
        }

      /* How many URLs does this action expect? */
      if (action->action == ACTION_RM
          || action->action == ACTION_MKDIR
          || action->action == ACTION_BRANCHIFY
          || action->action == ACTION_DISSOLVE)
        num_url_args = 1;
      else if (action->action == ACTION_LIST_BRANCHES
               || action->action == ACTION_LIST_BRANCHES_R)
        num_url_args = 0;
      else
        num_url_args = 2;

      /* Parse the required number of URLs. */
      for (j = 0; j < num_url_args; ++j)
        {
          const char *url;

          if (++i == action_args->nelts)
            return svn_error_trace(insufficient());
          url = APR_ARRAY_IDX(action_args, i, const char *);

          /* If there's a ROOT_URL, we expect URL to be a path
             relative to ROOT_URL (and we build a full url from the
             combination of the two).  Otherwise, it should be a full
             url. */
          if (! svn_path_is_url(url))
            {
              if (! root_url)
                return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                         "'%s' is not a URL, and "
                                         "--root-url (-U) not provided\n",
                                         url);
              /* ### These relpaths are already URI-encoded. */
              url = apr_pstrcat(pool, root_url, "/",
                                svn_relpath_canonicalize(url, pool),
                                SVN_VA_NULL);
            }
          url = sanitize_url(url, pool);
          action->path[j] = url;

          /* The first URL argument to 'cp' could be the anchor,
             but the other URLs should be children of the anchor. */
          if (! (action->action == ACTION_CP && j == 0))
            url = svn_uri_dirname(url, pool);
          if (! anchor)
            anchor = url;
          else
            {
              anchor = svn_uri_get_longest_ancestor(anchor, url, pool);
              if (!anchor || !anchor[0])
                return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                         "URLs in the action list do not "
                                         "share a common ancestor");
            }
        }

      APR_ARRAY_PUSH(actions, struct action *) = action;
    }

  if (! actions->nelts)
    {
      *exit_code = EXIT_FAILURE;
      usage(stderr, pool);
      return SVN_NO_ERROR;
    }

  if ((err = execute(branch_rrpath, actions, anchor, log_msg, revprops,
                     base_revision, ctx, pool)))
    {
      if (err->apr_err == SVN_ERR_AUTHN_FAILED && non_interactive)
        err = svn_error_quick_wrap(err,
                                   _("Authentication failed and interactive"
                                     " prompting is disabled; see the"
                                     " --force-interactive option"));
      return err;
    }

  return SVN_NO_ERROR;
}

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;

  /* Initialize the app. */
  if (svn_cmdline_init("svnmover", stderr) != EXIT_SUCCESS)
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
      svn_cmdline_handle_exit_error(err, NULL, "svnmover: ");
    }

  svn_pool_destroy(pool);
  return exit_code;
}
