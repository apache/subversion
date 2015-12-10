/*
 * ra_loader.c:  logic for loading different RA library implementations
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

/* ==================================================================== */

/*** Includes. ***/
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_uri.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_dirent_uri.h"
#include "svn_props.h"
#include "svn_iter.h"

#include "private/svn_branch_compat.h"
#include "private/svn_branch_repos.h"
#include "private/svn_ra_private.h"
#include "private/svn_delta_private.h"
#include "private/svn_string_private.h"
#include "svnmover.h"
#include "svn_private_config.h"


/* Read the branching info string VALUE belonging to revision REVISION.
 */
static svn_error_t *
read_rev_prop(svn_string_t **value,
              svn_ra_session_t *ra_session,
              const char *branch_info_dir,
              svn_revnum_t revision,
              apr_pool_t *result_pool)
{
  apr_pool_t *scratch_pool = result_pool;

  if (branch_info_dir)
    {
      const char *file_path;
      svn_stream_t *stream;
      svn_error_t *err;

      file_path = svn_dirent_join(branch_info_dir,
                                  apr_psprintf(scratch_pool, "branch-info-r%ld",
                                               revision), scratch_pool);
      err = svn_stream_open_readonly(&stream, file_path, scratch_pool, scratch_pool);
      if (err)
        {
          svn_error_clear(err);
          *value = NULL;
          return SVN_NO_ERROR;
        }
      SVN_ERR(err);
      SVN_ERR(svn_string_from_stream2(value, stream, 0, result_pool));
    }
  else
    {
      SVN_ERR(svn_ra_rev_prop(ra_session, revision, "svn-br-info", value,
                              result_pool));
    }
  return SVN_NO_ERROR;
}

/* Store the branching info string VALUE belonging to revision REVISION.
 */
static svn_error_t *
write_rev_prop(svn_ra_session_t *ra_session,
               const char *branch_info_dir,
               svn_revnum_t revision,
               svn_string_t *value,
               apr_pool_t *scratch_pool)
{
  if (branch_info_dir)
    {
      const char *file_path;
      svn_error_t *err;

     file_path = svn_dirent_join(branch_info_dir,
                                  apr_psprintf(scratch_pool, "branch-info-r%ld",
                                               revision), scratch_pool);
      err = svn_io_file_create(file_path, value->data, scratch_pool);
      if (err)
        {
          svn_error_clear(err);
          SVN_ERR(svn_io_dir_make(branch_info_dir, APR_FPROT_OS_DEFAULT,
                                  scratch_pool));
          err = svn_io_file_create(file_path, value->data, scratch_pool);
        }
      SVN_ERR(err);
    }
  else
    {
      SVN_ERR(svn_ra_change_rev_prop2(ra_session, revision, "svn-br-info",
                                      NULL, value, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Create a new revision-root object and read the move-tracking /
 * branch-tracking metadata from the repository into it.
 */
static svn_error_t *
branch_revision_fetch_info(svn_branch__txn_t **txn_p,
                           svn_branch__repos_t *repos,
                           svn_ra_session_t *ra_session,
                           const char *branch_info_dir,
                           svn_revnum_t revision,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_string_t *value;
  svn_stream_t *stream;
  svn_branch__txn_t *txn;

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));

  /* Read initial state from repository */
  SVN_ERR(read_rev_prop(&value, ra_session, branch_info_dir, revision,
                        scratch_pool));
  if (! value && revision == 0)
    {
      value = svn_branch__get_default_r0_metadata(scratch_pool);
      /*SVN_DBG(("fetch_per_revision_info(r%ld): LOADED DEFAULT INFO:\n%s",
               revision, value->data));*/
      SVN_ERR(write_rev_prop(ra_session, branch_info_dir, revision, value,
                             scratch_pool));
    }
  else if (! value)
    {
      return svn_error_createf(SVN_BRANCH__ERR, NULL,
                               _("Move-tracking metadata not found in r%ld "
                                 "in this repository. Run svnmover on an "
                                 "empty repository to initialize the "
                                 "metadata"), revision);
    }
  stream = svn_stream_from_string(value, scratch_pool);

  SVN_ERR(svn_branch__txn_parse(&txn, repos, stream,
                                result_pool, scratch_pool));

  /* Self-test: writing out the info should produce exactly the same string. */
  {
    svn_stringbuf_t *buf = svn_stringbuf_create_empty(scratch_pool);

    stream = svn_stream_from_stringbuf(buf, scratch_pool);
    SVN_ERR(svn_branch__txn_serialize(txn, stream, scratch_pool));
    SVN_ERR(svn_stream_close(stream));

    SVN_ERR_ASSERT(svn_string_compare(value,
                                      svn_stringbuf__morph_into_string(buf)));
  }

  *txn_p = txn;
  return SVN_NO_ERROR;
}

/* Fetch all element payloads in TXN.
 */
static svn_error_t *
txn_fetch_payloads(svn_branch__txn_t *txn,
                   svn_branch__compat_fetch_func_t fetch_func,
                   void *fetch_baton,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *branches = svn_branch__txn_get_branches(txn, scratch_pool);
  int i;

  /* Read payload of each element.
     (In a real implementation, of course, we'd delay this until demanded.) */
  for (i = 0; i < branches->nelts; i++)
    {
      svn_branch__state_t *branch = APR_ARRAY_IDX(branches, i, void *);
      svn_element__tree_t *element_tree;
      apr_hash_index_t *hi;

      SVN_ERR(svn_branch__state_get_elements(branch, &element_tree,
                                             scratch_pool));
      for (hi = apr_hash_first(scratch_pool, element_tree->e_map);
           hi; hi = apr_hash_next(hi))
        {
          int eid = svn_eid__hash_this_key(hi);
          svn_element__content_t *element /*= apr_hash_this_val(hi)*/;

          SVN_ERR(svn_branch__state_get_element(branch, &element,
                                                eid, scratch_pool));
          if (! element->payload->is_subbranch_root)
            {
              SVN_ERR(svn_branch__compat_fetch(&element->payload,
                                               txn,
                                               element->payload->branch_ref,
                                               fetch_func, fetch_baton,
                                               result_pool, scratch_pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Create a new repository object and read the move-tracking /
 * branch-tracking metadata from the repository into it.
 */
static svn_error_t *
branch_repos_fetch_info(svn_branch__repos_t **repos_p,
                        svn_ra_session_t *ra_session,
                        const char *branch_info_dir,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_branch__repos_t *repos
    = svn_branch__repos_create(result_pool);
  svn_revnum_t base_revision;
  svn_revnum_t r;

  SVN_ERR(svn_ra_get_latest_revnum(ra_session, &base_revision, scratch_pool));

  for (r = 0; r <= base_revision; r++)
    {
      svn_branch__txn_t *txn;

      SVN_ERR(branch_revision_fetch_info(&txn,
                                         repos, ra_session, branch_info_dir,
                                         r,
                                         result_pool, scratch_pool));
      SVN_ERR(svn_branch__repos_add_revision(repos, txn));
      SVN_ERR(txn_fetch_payloads(txn, fetch_func, fetch_baton,
                                 result_pool, scratch_pool));
      }

  *repos_p = repos;
  return SVN_NO_ERROR;
}

/* Return a mutable state based on revision BASE_REVISION in REPOS.
 */
static svn_error_t *
branch_get_mutable_state(svn_branch__txn_t **txn_p,
                         svn_branch__repos_t *repos,
                         svn_ra_session_t *ra_session,
                         const char *branch_info_dir,
                         svn_revnum_t base_revision,
                         svn_branch__compat_fetch_func_t fetch_func,
                         void *fetch_baton,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_branch__txn_t *txn;
  apr_array_header_t *branches;
  int i;

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(base_revision));

  SVN_ERR(branch_revision_fetch_info(&txn,
                                     repos, ra_session, branch_info_dir,
                                     base_revision,
                                     result_pool, scratch_pool));
  SVN_ERR_ASSERT(txn->rev == base_revision);
  SVN_ERR(txn_fetch_payloads(txn, fetch_func, fetch_baton,
                             result_pool, scratch_pool));

  /* Update all the 'predecessor' info to point to the BASE_REVISION instead
     of to that revision's predecessor. */
  txn->base_rev = base_revision;
  txn->rev = SVN_INVALID_REVNUM;

  branches = svn_branch__txn_get_branches(txn, scratch_pool);
  for (i = 0; i < branches->nelts; i++)
    {
      svn_branch__state_t *b = APR_ARRAY_IDX(branches, i, void *);
      svn_branch__history_t *history
        = svn_branch__history_create_empty(result_pool);

      /* Set each branch's parent to the branch in the base rev */
      svn_branch__rev_bid_t *parent
        = svn_branch__rev_bid_create(base_revision,
                                     svn_branch__get_id(b, scratch_pool),
                                     result_pool);

      svn_hash_sets(history->parents,
                    apr_pstrdup(result_pool, b->bid), parent);
      SVN_ERR(svn_branch__state_set_history(b, history, scratch_pool));
    }

  *txn_p = txn;
  return SVN_NO_ERROR;
}

/* Store the move-tracking / branch-tracking metadata from TXN into the
 * repository. TXN->rev is the newly committed revision number.
 */
static svn_error_t *
store_repos_info(svn_branch__txn_t *txn,
                 svn_ra_session_t *ra_session,
                 const char *branch_info_dir,
                 apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(scratch_pool);
  svn_stream_t *stream = svn_stream_from_stringbuf(buf, scratch_pool);

  SVN_ERR(svn_branch__txn_serialize(txn, stream, scratch_pool));

  SVN_ERR(svn_stream_close(stream));
  /*SVN_DBG(("store_repos_info: %s", buf->data));*/
  SVN_ERR(write_rev_prop(ra_session, branch_info_dir, txn->rev,
                         svn_stringbuf__morph_into_string(buf), scratch_pool));

  return SVN_NO_ERROR;
}

struct ccw_baton
{
  svn_commit_callback2_t original_callback;
  void *original_baton;

  svn_ra_session_t *session;
  const char *branch_info_dir;
  svn_branch__txn_t *branching_txn;
};

/* Wrapper which stores the branching/move-tracking info.
 */
static svn_error_t *
commit_callback_wrapper(const svn_commit_info_t *commit_info,
                        void *baton,
                        apr_pool_t *pool)
{
  struct ccw_baton *ccwb = baton;

  /* if this commit used element-branching info, store the new info */
  if (ccwb->branching_txn)
    {
      svn_branch__repos_t *repos = ccwb->branching_txn->repos;

      ccwb->branching_txn->rev = commit_info->revision;
      SVN_ERR(svn_branch__repos_add_revision(repos, ccwb->branching_txn));
      SVN_ERR(store_repos_info(ccwb->branching_txn, ccwb->session,
                               ccwb->branch_info_dir, pool));
    }

  /* call the wrapped callback */
  if (ccwb->original_callback)
    {
      SVN_ERR(ccwb->original_callback(commit_info, ccwb->original_baton, pool));
    }

  return SVN_NO_ERROR;
}


/* Some RA layers do not correctly fill in REPOS_ROOT in commit_info, or
   they are third-party layers conforming to an older commit_info structure.
   Interpose a utility function to ensure the field is valid.  */
static void
remap_commit_callback(svn_commit_callback2_t *callback,
                      void **callback_baton,
                      svn_ra_session_t *session,
                      svn_branch__txn_t *branching_txn,
                      const char *branch_info_dir,
                      svn_commit_callback2_t original_callback,
                      void *original_baton,
                      apr_pool_t *result_pool)
{
  /* Allocate this in RESULT_POOL, since the callback will be called
     long after this function has returned. */
  struct ccw_baton *ccwb = apr_palloc(result_pool, sizeof(*ccwb));

  ccwb->session = session;
  ccwb->branch_info_dir = apr_pstrdup(result_pool, branch_info_dir);
  ccwb->branching_txn = branching_txn;
  ccwb->original_callback = original_callback;
  ccwb->original_baton = original_baton;

  *callback = commit_callback_wrapper;
  *callback_baton = ccwb;
}


/* Ev3 shims */
struct fb_baton {
  /* A session parented at the repository root */
  svn_ra_session_t *session;
  const char *repos_root_url;
  const char *session_path;
};

/* Fetch kind and/or props and/or text.
 *
 * Implements svn_branch__compat_fetch_func_t. */
static svn_error_t *
fetch(svn_node_kind_t *kind_p,
      apr_hash_t **props_p,
      svn_stringbuf_t **file_text,
      apr_hash_t **children_names,
      void *baton,
      const char *repos_relpath,
      svn_revnum_t revision,
      apr_pool_t *result_pool,
      apr_pool_t *scratch_pool)
{
  struct fb_baton *fbb = baton;
  svn_node_kind_t kind;
  apr_hash_index_t *hi;

  if (props_p)
    *props_p = NULL;
  if (file_text)
    *file_text = NULL;
  if (children_names)
    *children_names = NULL;

  SVN_ERR(svn_ra_check_path(fbb->session, repos_relpath, revision,
                            &kind, scratch_pool));
  if (kind_p)
    *kind_p = kind;
  if (kind == svn_node_file && (props_p || file_text))
    {
      svn_stream_t *file_stream = NULL;

      if (file_text)
        {
          *file_text = svn_stringbuf_create_empty(result_pool);
          file_stream = svn_stream_from_stringbuf(*file_text, scratch_pool);
        }
      SVN_ERR(svn_ra_get_file(fbb->session, repos_relpath, revision,
                              file_stream, NULL, props_p, result_pool));
      if (file_text)
        {
          SVN_ERR(svn_stream_close(file_stream));
        }
    }
  else if (kind == svn_node_dir && (props_p || children_names))
    {
      SVN_ERR(svn_ra_get_dir2(fbb->session,
                              children_names, NULL, props_p,
                              repos_relpath, revision,
                              0 /*minimal child info*/,
                              result_pool));
    }

  /* Remove non-regular props */
  if (props_p && *props_p)
    {
      for (hi = apr_hash_first(scratch_pool, *props_p); hi; hi = apr_hash_next(hi))
        {
          const char *name = apr_hash_this_key(hi);

          if (svn_property_kind2(name) != svn_prop_regular_kind)
            svn_hash_sets(*props_p, name, NULL);

        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_load_branching_state(svn_branch__txn_t **branching_txn_p,
                            svn_branch__compat_fetch_func_t *fetch_func,
                            void **fetch_baton,
                            svn_ra_session_t *session,
                            const char *branch_info_dir,
                            svn_revnum_t base_revision,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_branch__repos_t *repos;
  const char *repos_root_url, *session_url, *base_relpath;
  struct fb_baton *fbb = apr_palloc(result_pool, sizeof (*fbb));

  if (base_revision == SVN_INVALID_REVNUM)
    {
      SVN_ERR(svn_ra_get_latest_revnum(session, &base_revision, scratch_pool));
    }

  /* fetcher */
  SVN_ERR(svn_ra_get_repos_root2(session, &repos_root_url, result_pool));
  SVN_ERR(svn_ra_get_session_url(session, &session_url, scratch_pool));
  base_relpath = svn_uri_skip_ancestor(repos_root_url, session_url, result_pool);
  SVN_ERR(svn_ra__dup_session(&fbb->session, session, repos_root_url, result_pool, scratch_pool));
  fbb->session_path = base_relpath;
  fbb->repos_root_url = repos_root_url;
  *fetch_func = fetch;
  *fetch_baton = fbb;

  SVN_ERR(branch_repos_fetch_info(&repos,
                                  session, branch_info_dir,
                                  *fetch_func, *fetch_baton,
                                  result_pool, scratch_pool));
  SVN_ERR(branch_get_mutable_state(branching_txn_p,
                                   repos, session, branch_info_dir,
                                   base_revision,
                                   *fetch_func, *fetch_baton,
                                   result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_get_commit_txn(svn_ra_session_t *session,
                      svn_branch__txn_t **edit_txn_p,
                      apr_hash_t *revprop_table,
                      svn_commit_callback2_t commit_callback,
                      void *commit_baton,
                      apr_hash_t *lock_tokens,
                      svn_boolean_t keep_locks,
                      const char *branch_info_dir,
                      apr_pool_t *pool)
{
  svn_branch__txn_t *branching_txn;
  svn_branch__compat_fetch_func_t fetch_func;
  void *fetch_baton;
  const svn_delta_editor_t *deditor;
  void *dedit_baton;
  svn_branch__compat_shim_connector_t *shim_connector;

  /* load branching info
   * ### Currently we always start from a single base revision, never from
   *     a mixed-rev state */
  SVN_ERR(svn_ra_load_branching_state(&branching_txn, &fetch_func, &fetch_baton,
                                      session, branch_info_dir,
                                      SVN_INVALID_REVNUM /*base_revision*/,
                                      pool, pool));

  /* arrange for branching info to be stored after commit */
  remap_commit_callback(&commit_callback, &commit_baton,
                        session, branching_txn, branch_info_dir,
                        commit_callback, commit_baton, pool);

  SVN_ERR(svn_ra_get_commit_editor3(session, &deditor, &dedit_baton,
                                    revprop_table,
                                    commit_callback, commit_baton,
                                    lock_tokens, keep_locks, pool));

  /* Convert to Ev3 */
  {
    const char *repos_root_url;

    SVN_ERR(svn_ra_get_repos_root2(session, &repos_root_url, pool));

    /*SVN_ERR(svn_delta__get_debug_editor(&deditor, &dedit_baton,
                                          deditor, dedit_baton, "", pool));*/
    SVN_ERR(svn_branch__compat_txn_from_delta_for_commit(
                        edit_txn_p,
                        &shim_connector,
                        deditor, dedit_baton, branching_txn,
                        repos_root_url,
                        fetch_func, fetch_baton,
                        NULL, NULL /*cancel*/,
                        pool, pool));
  }

  return SVN_NO_ERROR;
}

