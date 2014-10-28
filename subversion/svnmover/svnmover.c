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
  ACTION_MERGE,
  ACTION_MV,
  ACTION_MKDIR,
  ACTION_CP,
  ACTION_RM
} action_code_t;

struct action {
  action_code_t action;

  /* revision (copy-from-rev of path[0] for cp) */
  svn_revnum_t rev;

  /* action    path[0]  path[1]  path[2]
   * ------    -------  -------  -------
   * list_br   path
   * branch    source   target
   * branchify path
   * dissolve  path
   * merge     from     to       yca@rev
   * mv        source   target
   * mkdir     target
   * cp        source   target
   * rm        target
   */
  const char *path[3];
};

/* ====================================================================== */

/* Find the deepest branch in the repository of which RRPATH_REV is
 * either the root element or a normal, non-sub-branch element.
 *
 * RRPATH_REV is a repository-relative path with an optional "@REV" suffix.
 * When "@REV" is not present, find in the current txn.
 *
 * Return the location of the element at RRPATH_REV in that branch, or with
 * EID=-1 if no element exists there.
 *
 * The result will never be NULL, as every path is within at least the root
 * branch.
 */
static svn_error_t *
find_el_rev_by_rrpath_rev(svn_branch_el_rev_id_t **el_rev_p,
                          svn_editor3_t *editor,
                          const char *rrpath_rev,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  const char *rrpath;
  svn_opt_revision_t rev_spec;

  SVN_ERR(svn_opt_parse_path(&rev_spec, &rrpath, rrpath_rev, scratch_pool));
  if (rev_spec.kind == svn_opt_revision_number)
    {
      svn_revnum_t revnum = rev_spec.value.number;

      SVN_ERR(svn_editor3_find_el_rev_by_path_rev(el_rev_p,
                                                  editor, rrpath, revnum,
                                                  result_pool, scratch_pool));
    }
  else
    {
      svn_branch_el_rev_id_t *el_rev = apr_palloc(result_pool, sizeof(*el_rev));

      svn_editor3_find_branch_element_by_rrpath(
        &el_rev->branch, &el_rev->eid,
        editor, rrpath, scratch_pool);
      el_rev->rev = SVN_INVALID_REVNUM;
      *el_rev_p = el_rev;
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

      printf("  branch %d (root element %d -> '/%s')\n",
             branch->sibling_defn->bid, branch->sibling_defn->root_eid,
             svn_branch_get_root_rrpath(branch));
      for (eid = family->first_eid; eid < family->next_eid; eid++)
        {
          const char *rrpath = svn_branch_get_rrpath_by_eid(branch, eid,
                                                            scratch_pool);

          if (rrpath)
            {
              const char *relpath
                = svn_relpath_skip_ancestor(svn_branch_get_root_rrpath(branch),
                                            rrpath);

              printf("    e%d -> %s\n",
                     eid, relpath[0] ? relpath : ".");
            }
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

/* Options to control how strict the merge is about detecting conflicts.
 *
 * The options affect cases that, depending on the user's preference, could
 * either be considered a conflict or be merged to a deterministic result.
 *
 * The set of options is flexible and may be extended in future.
 */
typedef struct merge_conflict_policy_t
{
  /* Whether to merge delete-vs-delete */
  svn_boolean_t merge_double_delete;
  /* Whether to merge add-vs-add (with same parent/name/content) */
  svn_boolean_t merge_double_add;
  /* Whether to merge reparent-vs-reparent (with same parent) */
  svn_boolean_t merge_double_reparent;
  /* Whether to merge rename-vs-rename (with same name) */
  svn_boolean_t merge_double_rename;
  /* Whether to merge modify-vs-modify (with same content) */
  svn_boolean_t merge_double_modify;
  /* Possible additional controls: */
  /* merge (parent, name, props, text) independently or as a group */
  /* merge (parent, name) independently or as a group */
  /* merge (props, text) independently or as a group */
} merge_conflict_policy_t;

/* Merge the content for one element.
 *
 * If there is no conflict, set *CONFLICT_P to FALSE and *RESULT_P to the
 * merged element; otherwise set *CONFLICT_P to TRUE and *RESULT_P to NULL.
 * Note that *RESULT_P can be null, indicating a deletion.
 *
 * This handles any case where at least one of (SIDE1, SIDE2, YCA) exists.
 */
static void
element_merge(svn_branch_el_rev_content_t **result_p,
              svn_boolean_t *conflict_p,
              int eid,
              svn_branch_el_rev_content_t *side1,
              svn_branch_el_rev_content_t *side2,
              svn_branch_el_rev_content_t *yca,
              const merge_conflict_policy_t *policy,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_boolean_t same1 = svn_branch_el_rev_content_equal(eid, yca, side1,
                                                        scratch_pool);
  svn_boolean_t same2 = svn_branch_el_rev_content_equal(eid, yca, side2,
                                                        scratch_pool);
  svn_boolean_t conflict = FALSE;
  svn_branch_el_rev_content_t *result = NULL;

  if (same1)
    {
      result = side2;
    }
  else if (same2)
    {
      result = side1;
    }
  else if (yca && side1 && side2)
    {
      /* All three sides are different, and all exist */
      result = apr_pmemdup(result_pool, yca, sizeof(*result));

      /* merge the parent-eid */
      if (side1->parent_eid == yca->parent_eid)
        {
          result->parent_eid = side2->parent_eid;
        }
      else if (side2->parent_eid == yca->parent_eid)
        {
          result->parent_eid = side1->parent_eid;
        }
      else if (policy->merge_double_reparent
               && side1->parent_eid == side2->parent_eid)
        {
          SVN_DBG(("e%d double reparent: e%d -> { e%d | e%d }",
                   eid, yca->parent_eid, side1->parent_eid, side2->parent_eid));
          result->parent_eid = side1->parent_eid;
        }
      else
        {
          SVN_DBG(("e%d conflict: parent: e%d -> { e%d | e%d }",
                   eid, yca->parent_eid, side1->parent_eid, side2->parent_eid));
          conflict = TRUE;
        }

      /* merge the name */
      if (strcmp(side1->name, yca->name) == 0)
        {
          result->name = side2->name;
        }
      else if (strcmp(side2->name, yca->name) == 0)
        {
          result->name = side1->name;
        }
      else if (policy->merge_double_rename
               && strcmp(side1->name, side2->name) == 0)
        {
          SVN_DBG(("e%d double rename: %s -> { %s | %s }",
                   eid, yca->name, side1->name, side2->name));
          result->name = side1->name;
        }
      else
        {
          SVN_DBG(("e%d conflict: name: %s -> { %s | %s }",
                   eid, yca->name, side1->name, side2->name));
          conflict = TRUE;
        }

      /* merge the content */
      if (svn_editor3_node_content_equal(side1->content, yca->content,
                                         scratch_pool))
        {
          result->content = side2->content;
        }
      else if (svn_editor3_node_content_equal(side2->content, yca->content,
                                              scratch_pool))
        {
          result->content = side1->content;
        }
      else if (policy->merge_double_modify
               && svn_editor3_node_content_equal(side1->content, side2->content,
                                                 scratch_pool))
        {
          SVN_DBG(("e%d double modify: ... -> { ... | ... }",
                   eid));
          result->content = side1->content;
        }
      else
        {
          /* ### Need not conflict if can merge props and text separately. */

          SVN_DBG(("e%d conflict: content: ... -> { ... | ... }",
                   eid));
          conflict = TRUE;
        }
    }
  else if (! side1 && ! side2)
    {
      /* Double delete (as we assume at least one of YCA/SIDE1/SIDE2 exists) */
      if (policy->merge_double_delete)
        {
          SVN_DBG(("e%d double delete",
                   eid));
          result = side1;
        }
      else
        {
          SVN_DBG(("e%d conflict: delete vs. delete",
                   eid));
          conflict = TRUE;
        }
    }
  else if (side1 && side2)
    {
      /* Double add (as we already handled the case where YCA also exists) */
      if (policy->merge_double_add
          && svn_branch_el_rev_content_equal(eid, side1, side2, scratch_pool))
        {
          SVN_DBG(("e%d double add",
                   eid));
          result = side1;
        }
      else
        {
          SVN_DBG(("e%d conflict: add vs. add (%s)",
                   eid,
                   svn_branch_el_rev_content_equal(eid, side1, side2,
                                                   scratch_pool)
                     ? "same content" : "different content"));
          conflict = TRUE;
        }
    }
  else
    {
      /* The remaining cases must be delete vs. modify */
      SVN_DBG(("e%d conflict: delete vs. modify: %d -> { %d | %d }",
               eid, !!yca, !!side1, !!side2));
      conflict = TRUE;
    }

  *result_p = result;
  *conflict_p = conflict;
}

/* Merge ...
 *
 * Merge any sub-branches in the same way, recursively.
 */
static svn_error_t *
branch_merge_subtree_r(svn_editor3_t *editor,
                       const svn_branch_el_rev_id_t *src,
                       const svn_branch_el_rev_id_t *tgt,
                       const svn_branch_el_rev_id_t *yca,
                       apr_pool_t *scratch_pool)
{
  apr_hash_t *diff_yca_src, *diff_yca_tgt;
  svn_boolean_t had_conflict = FALSE;
  int first_eid, next_eid, eid;
  const merge_conflict_policy_t policy = { TRUE, TRUE, TRUE, TRUE, TRUE };

  SVN_ERR_ASSERT(src->branch->sibling_defn->family->fid
                 == tgt->branch->sibling_defn->family->fid);
  SVN_ERR_ASSERT(src->branch->sibling_defn->family->fid
                 == yca->branch->sibling_defn->family->fid);
  SVN_ERR_ASSERT(src->eid == tgt->eid);
  SVN_ERR_ASSERT(src->eid == yca->eid);

  SVN_DBG(("merge src: r%2ld f%d b%2d e%3d",
           src->rev, src->branch->sibling_defn->family->fid,
           src->branch->sibling_defn->bid, src->eid));
  SVN_DBG(("merge tgt: r%2ld f%d b%2d e%3d",
           tgt->rev, tgt->branch->sibling_defn->family->fid,
           tgt->branch->sibling_defn->bid, tgt->eid));
  SVN_DBG(("merge yca: r%2ld f%d b%2d e%3d",
           yca->rev, yca->branch->sibling_defn->family->fid,
           yca->branch->sibling_defn->bid, yca->eid));

  /*
      for (eid, diff1) in element_differences(YCA, FROM):
        diff2 = element_diff(eid, YCA, TO)
        if diff1 and diff2:
          result := element_merge(diff1, diff2)
        elif diff1:
          result := diff1.right
        # else no change
   */
  SVN_ERR(svn_branch_subtree_differences(&diff_yca_src,
                                         editor, yca, src,
                                         scratch_pool, scratch_pool));
  /* ### We only need to query for YCA:TO differences in elements that are
         different in YCA:FROM, but right now we ask for all differences. */
  SVN_ERR(svn_branch_subtree_differences(&diff_yca_tgt,
                                         editor, yca, tgt,
                                         scratch_pool, scratch_pool));

  first_eid = yca->branch->sibling_defn->family->first_eid;
  next_eid = yca->branch->sibling_defn->family->next_eid;
  next_eid = MAX(next_eid, src->branch->sibling_defn->family->next_eid);
  next_eid = MAX(next_eid, tgt->branch->sibling_defn->family->next_eid);

  for (eid = first_eid; eid < next_eid; eid++)
    {
      svn_branch_el_rev_content_t **e_yca_src
        = apr_hash_get(diff_yca_src, &eid, sizeof(eid));
      svn_branch_el_rev_content_t **e_yca_tgt
        = apr_hash_get(diff_yca_tgt, &eid, sizeof(eid));
      svn_branch_el_rev_content_t *e_yca;
      svn_branch_el_rev_content_t *e_src;
      svn_branch_el_rev_content_t *e_tgt;
      svn_branch_el_rev_content_t *result;
      svn_boolean_t conflict;

      /* If an element hasn't changed in the source branch, there is
         no need to do anything with it in the target branch. We could
         use element_merge() for any case where at least one of (SRC,
         TGT, YCA) exists, but we choose to skip it when SRC == YCA. */
      if (! e_yca_src)
        {
          continue;
        }

      e_yca = e_yca_src[0];
      e_src = e_yca_src[1];
      e_tgt = e_yca_tgt ? e_yca_tgt[1] : e_yca_src[0];

      element_merge(&result, &conflict,
                    eid, e_src, e_tgt, e_yca,
                    &policy,
                    scratch_pool, scratch_pool);

      if (conflict)
        {
          SVN_DBG(("merged: e%d => conflict", eid));
          had_conflict = TRUE;
        }
      else if (e_tgt && result)
        {
          SVN_DBG(("merged: e%d => parent=e%d, name=%s, content=...",
                   eid, result->parent_eid, result->name));

          SVN_ERR(svn_editor3_alter(editor, tgt->rev, eid,
                                    result->parent_eid, result->name,
                                    result->content));
        }
      else if (e_tgt)
        {
          SVN_DBG(("merged: e%d => <deleted>", eid));
          SVN_ERR(svn_editor3_delete(editor, tgt->rev, eid));
        }
      else if (result)
        {
          SVN_DBG(("merged: e%d => <added>", eid));

          /* In BRANCH, create an instance of the element EID with new content.
           *
           * Translated to old language, this means create a new node-copy
           * copied (branched) from the source-right version of the merge
           * (which is not specified here, but will need to be),
           * which may be in this branch or in another branch.
           */
          SVN_ERR(svn_editor3_instantiate(editor, eid,
                                          result->parent_eid, result->name,
                                          result->content));
        }
    }

  if (had_conflict)
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("merge failed: conflict(s) occurred"));
    }
  else
    {
      SVN_DBG(("merge completed: no conflicts"));
    }

  /* ### TODO: subbranches */

  return SVN_NO_ERROR;
}

/* Merge SRC into TGT, using the common ancestor YCA.
 *
 * Merge the two sets of changes: YCA -> SRC and YCA -> TGT, applying
 * the result to the transaction at TGT.
 *
 * If conflicts arise, just fail.
 *
 * SRC->BRANCH, TGT->BRANCH and YCA->BRANCH must be in the same family.
 *
 * SRC, TGT and YCA must be existing and corresponding (same EID) elements
 * of the branch family.
 *
 * None of SRC, TGT and YCA is a subbranch root element.
 *
 * ### TODO:
 *     If ... contains nested subbranches, these will also be merged.
 */
static svn_error_t *
svn_branch_merge(svn_editor3_t *editor,
                 svn_branch_el_rev_id_t *src,
                 svn_branch_el_rev_id_t *tgt,
                 svn_branch_el_rev_id_t *yca,
                 apr_pool_t *scratch_pool)
{
  if (src->branch->sibling_defn->family->fid != tgt->branch->sibling_defn->family->fid
      || src->branch->sibling_defn->family->fid != yca->branch->sibling_defn->family->fid)
    return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                             _("Merge branches must all be in same family "
                               "(from: f%d, to: f%d, yca: f%d)"),
                             src->branch->sibling_defn->family->fid,
                             tgt->branch->sibling_defn->family->fid,
                             yca->branch->sibling_defn->family->fid);

  /*SVN_ERR(verify_exists_in_branch(from, scratch_pool));*/
  /*SVN_ERR(verify_exists_in_branch(to, scratch_pool));*/
  /*SVN_ERR(verify_exists_in_branch(yca, scratch_pool));*/
  if (src->eid != tgt->eid || src->eid != yca->eid)
    return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                             _("Merge branches must all be same element "
                               "(from: e%d, to: e%d, yca: e%d)"),
                             src->eid, tgt->eid, yca->eid);
  /*SVN_ERR(verify_not_subbranch_root(from, scratch_pool));*/
  /*SVN_ERR(verify_not_subbranch_root(to, scratch_pool));*/
  /*SVN_ERR(verify_not_subbranch_root(yca, scratch_pool));*/

  SVN_ERR(branch_merge_subtree_r(editor, src, tgt, yca, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
execute(const apr_array_header_t *actions,
        const char *anchor_url,
        const char *log_msg,
        apr_hash_t *revprops,
        svn_revnum_t base_revision,
        svn_client_ctx_t *ctx,
        apr_pool_t *pool)
{
  mtcc_t *mtcc;
  svn_editor3_t *editor;
  const char *base_relpath;
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
  base_relpath = svn_uri_skip_ancestor(mtcc->repos_root_url, anchor_url, pool);
  base_revision = mtcc->base_revision;

  for (i = 0; i < actions->nelts; ++i)
    {
      struct action *action = APR_ARRAY_IDX(actions, i, struct action *);
      int j;
      const char *path_name[3] = { NULL, NULL, NULL };
      svn_branch_el_rev_id_t *el_rev[3], *parent_el_rev[3];

      svn_pool_clear(iterpool);

      for (j = 0; j < 3; j++)
        {
          if (action->path[j])
            {
              const char *rrpath = svn_relpath_join(base_relpath,
                                                    action->path[j], pool);
              const char *parent_rrpath = svn_relpath_dirname(rrpath, pool);

              path_name[j] = svn_relpath_basename(rrpath, NULL);
              SVN_ERR(find_el_rev_by_rrpath_rev(&el_rev[j], editor, rrpath,
                                                pool, pool));
              SVN_ERR(find_el_rev_by_rrpath_rev(&parent_el_rev[j],
                                                editor, parent_rrpath,
                                                pool, pool));
            }
        }
      switch (action->action)
        {
        case ACTION_LIST_BRANCHES:
          {
            svn_branch_family_t *family = svn_branch_get_family(editor);

            SVN_ERR(family_list_branch_instances(family,
                                                 FALSE, iterpool));
          }
          break;
        case ACTION_LIST_BRANCHES_R:
          {
            svn_branch_family_t *family = svn_branch_get_family(editor);

            SVN_ERR(family_list_branch_instances(family,
                                                 TRUE, iterpool));
          }
          break;
        case ACTION_BRANCH:
          SVN_ERR(svn_branch_branch(editor,
                                    el_rev[0]->branch, el_rev[0]->eid,
                                    el_rev[1]->branch, parent_el_rev[1]->eid,
                                    path_name[1],
                                    iterpool));
          made_changes = TRUE;
          break;
        case ACTION_BRANCHIFY:
          SVN_ERR(svn_branch_branchify(editor,
                                       el_rev[0]->eid,
                                       iterpool));
          made_changes = TRUE;
          break;
        case ACTION_DISSOLVE:
          return svn_error_create(SVN_ERR_BRANCHING, NULL,
                                  _("'dissolve' operation not implemented"));
          made_changes = TRUE;
          break;
        case ACTION_MERGE:
          {
            /* Look up path[0] (FROM) and path[2] (YCA) relative to repo
               root, unlike path[1] (TO) which is relative to anchor URL. */
            SVN_ERR(find_el_rev_by_rrpath_rev(
                      &el_rev[0], editor, action->path[0], pool, pool));
            SVN_ERR(find_el_rev_by_rrpath_rev(
                      &el_rev[2], editor, action->path[2], pool, pool));
            SVN_ERR(svn_branch_merge(editor,
                                     el_rev[0] /*from*/,
                                     el_rev[1] /*to*/,
                                     el_rev[2] /*yca*/,
                                     iterpool));
          }
          made_changes = TRUE;
          break;
        case ACTION_MV:
          {
            svn_editor3_node_content_t *content = NULL; /* "no change" */

            SVN_ERR(svn_editor3_alter(editor, el_rev[0]->rev,
                                      el_rev[0]->eid,
                                      parent_el_rev[1]->eid, path_name[1], content));
          }
          made_changes = TRUE;
          break;
        case ACTION_CP:
          SVN_ERR(svn_editor3_copy_tree(editor,
                                        el_rev[0]->rev, el_rev[0]->eid,
                                        parent_el_rev[1]->eid, path_name[1]));
          made_changes = TRUE;
          break;
        case ACTION_RM:
          SVN_ERR_ASSERT(el_rev[0]->rev == SVN_INVALID_REVNUM);
          SVN_ERR(svn_editor3_delete(editor, el_rev[0]->rev,
                                     el_rev[0]->eid));
          made_changes = TRUE;
          break;
        case ACTION_MKDIR:
          {
            apr_hash_t *props = apr_hash_make(iterpool);
            svn_editor3_node_content_t *content
              = svn_editor3_node_content_create_dir(props, iterpool);
            int new_eid;

            SVN_ERR(svn_editor3_add(editor, &new_eid, svn_node_dir,
                                    parent_el_rev[0]->eid, path_name[0], content));
          }
          made_changes = TRUE;
          break;
        default:
          SVN_ERR_MALFUNCTION();
        }
    }

  if (made_changes)
    {
      err = mtcc_commit(mtcc, pool);
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
      "  merge FROM TO YCA@REV  : merge changes YCA->FROM and YCA->TO into TO\n"
      "  cp REV SRC-URL DST-URL : copy SRC-URL@REV to DST-URL\n"
      "  mv SRC-URL DST-URL     : move SRC-URL to DST-URL\n"
      "  rm URL                 : delete URL\n"
      "  mkdir URL              : create new directory URL\n"
      "\n"
      "Valid options:\n"
      "  -h, -? [--help]        : display this text\n"
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
      else if (! strcmp(action_string, "merge"))
        action->action = ACTION_MERGE;
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
      else if (action->action == ACTION_MERGE)
        num_url_args = 3;
      else
        num_url_args = 2;

      /* Parse the required number of URLs. */
      for (j = 0; j < num_url_args; ++j)
        {
          const char *path, *url;

          if (++i == action_args->nelts)
            return svn_error_trace(insufficient());
          path = APR_ARRAY_IDX(action_args, i, const char *);

          /* If there's a ROOT_URL, we expect URL to be a path
             relative to ROOT_URL (and we build a full url from the
             combination of the two).  Otherwise, it should be a full
             url. */
          if (svn_path_is_url(path))
            {
              url = path;
              path = svn_uri_skip_ancestor(root_url, url, pool);
            }
          else
            {
              if (! root_url)
                return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                         "'%s' is not a URL, and "
                                         "--root-url (-U) not provided\n",
                                         path);
              path = svn_path_internal_style(path, pool);
              url = svn_path_url_add_component2(root_url, path, pool);
            }
          url = sanitize_url(url, pool);
          action->path[j] = path;

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

  if ((err = execute(actions, anchor, log_msg, revprops,
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
