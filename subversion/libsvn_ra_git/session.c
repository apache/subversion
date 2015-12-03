/*
 * ra_plugin.c : the main RA module for git repository access
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

#include "svn_hash.h"
#include "svn_ra.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_version.h"
#include "svn_repos.h"

#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"

#include "ra_git.h"

/*----------------------------------------------------------------*/

static apr_status_t
cleanup_temporary_repos(void *data)
{
  svn_ra_session_t *session = data;
  svn_ra_git__session_t *sess = session->priv;

  svn_error_clear(svn_io_remove_dir2(sess->local_repos_abspath, TRUE,
                                     session->cancel_func,
                                     session->cancel_baton,
                                     session->pool));

  return APR_SUCCESS;
}

/*----------------------------------------------------------------*/
typedef struct ra_git_reporter_baton_t
{
  const svn_ra_reporter3_t *reporter;
  void *report_baton;

  svn_ra_session_t *session;
} ra_git_reporter_baton_t;

static svn_error_t *
ra_git_reporter_set_path(void *report_baton,
                         const char *path,
                         svn_revnum_t revision,
                         svn_depth_t depth,
                         svn_boolean_t start_empty,
                         const char *lock_token,
                         apr_pool_t *pool)
{
  ra_git_reporter_baton_t *grb = report_baton;

  return svn_error_trace(
    grb->reporter->set_path(grb->report_baton, path, revision, depth,
                            start_empty, lock_token, pool));
}

static svn_error_t *
ra_git_reporter_delete_path(void *report_baton,
                            const char *path,
                            apr_pool_t *pool)
{
  ra_git_reporter_baton_t *grb = report_baton;

  return svn_error_trace(
    grb->reporter->delete_path(grb->report_baton, path, pool));
}

static svn_error_t *
ra_git_reporter_link_path(void *report_baton,
                          const char *path,
                          const char *url,
                          svn_revnum_t revision,
                          svn_depth_t depth,
                          svn_boolean_t start_empty,
                          const char *lock_token,
                          apr_pool_t *pool)
{
  ra_git_reporter_baton_t *grb = report_baton;

  if (url && svn_path_is_url(url))
    {
      svn_ra_git__session_t *sess = grb->session->priv;
      const char *repos_relpath;

      SVN_ERR(svn_ra_get_path_relative_to_root(grb->session,
                                               &repos_relpath,
                                               url, pool));
      url = svn_path_url_add_component2(sess->local_repos_root_url,
                                        repos_relpath, pool);
    }

  return svn_error_trace(
    grb->reporter->link_path(grb->report_baton, path, url, revision, depth,
                             start_empty, lock_token, pool));
}

static svn_error_t *
ra_git_reporter_finish_report(void *report_baton,
                              apr_pool_t *pool)
{
  ra_git_reporter_baton_t *grb = report_baton;

  return svn_error_trace(
    grb->reporter->finish_report(grb->report_baton, pool));
}

static svn_error_t *
ra_git_reporter_abort_report(void *report_baton,
                             apr_pool_t *pool)
{
  ra_git_reporter_baton_t *grb = report_baton;

  return svn_error_trace(
    grb->reporter->abort_report(grb->report_baton, pool));
}

static const svn_ra_reporter3_t ra_git_reporter_vtable =
{
  ra_git_reporter_set_path,
  ra_git_reporter_delete_path,
  ra_git_reporter_link_path,
  ra_git_reporter_finish_report,
  ra_git_reporter_abort_report
};

svn_error_t *
ra_git_wrap_reporter(const svn_ra_reporter3_t **reporter_p,
                     void **reporter_baton_p,
                     const svn_ra_reporter3_t *reporter,
                     void *reporter_baton,
                     svn_ra_session_t *session,
                     apr_pool_t *result_pool)
{
  ra_git_reporter_baton_t *grb = apr_pcalloc(result_pool, sizeof(*grb));
  grb->reporter = reporter;
  grb->report_baton = reporter_baton;
  grb->session = session;

  *reporter_p = &ra_git_reporter_vtable;
  *reporter_baton_p = grb;
  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------*/
/*** The RA vtable routines ***/

#define RA_GIT_DESCRIPTION \
        N_("Module for accessing a git repository.")

static const char *
ra_git_get_description(apr_pool_t *pool)
{
  return _(RA_GIT_DESCRIPTION);
}

static const char * const *
ra_git_get_schemes(apr_pool_t *pool)
{
  /* TODO: git+ssh requires optional libssh dependency -- do we want that as well? */
  static const char *schemes[] = { "git", "git+file", "git+http", "git+https", NULL };

  return schemes;
}

static svn_error_t *
ra_git_open(svn_ra_session_t *session,
            const char **corrected_url,
            const char *session_URL,
            const svn_ra_callbacks2_t *callbacks,
            void *callback_baton,
            svn_auth_baton_t *auth_baton,
            apr_hash_t *config,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess;
  svn_error_t *err;
  svn_repos_t *repos;

  /* We don't support redirections in ra-git. */
  if (corrected_url)
    *corrected_url = NULL;

  /* Allocate and stash the session_sess args we have already. */
  sess = apr_pcalloc(session->pool, sizeof(*sess));

  session->priv = sess;

  sess->config = config;
  sess->callbacks = callbacks;
  sess->callback_baton = callback_baton;

  /* Root the session at the root directory. */
  sess->session_url_buf = svn_stringbuf_create_ensure(348, session->pool);
  sess->repos_relpath_buf = svn_stringbuf_create_ensure(256, session->pool);

  /* Fake up the repository UUID. */
  sess->uuid = RA_GIT_UUID;

  sess->fetch_done = FALSE;

  /* Store the git repository within the working copy's admin area,
   * if availble. Otherwise, create a temporary repository. */
  if (sess->callbacks->get_wc_adm_subdir != NULL)
    {
      SVN_ERR(sess->callbacks->get_wc_adm_subdir(sess->callback_baton,
                                                 &sess->local_repos_abspath,
                                                 "git",
                                                 result_pool, scratch_pool));

      SVN_DBG(("Using git repos in '%s'\n", sess->local_repos_abspath));
    }
  else
    {
      /* Use a temporary git repository. */
      /* ### small race here, should be using mkdtemp() or similar */
      SVN_ERR(svn_io_open_unique_file3(NULL, &sess->local_repos_abspath, NULL,
                                       svn_io_file_del_none,
                                       result_pool, scratch_pool));
      SVN_ERR(svn_io_remove_file2(sess->local_repos_abspath, TRUE,
                                  scratch_pool));

      /* Git repository is removed when the session pool gets destroyed. */
      apr_pool_cleanup_register(session->pool, session, cleanup_temporary_repos,
                                apr_pool_cleanup_null);

      SVN_DBG(("Creating git repos in '%s'\n", sess->local_repos_abspath));
    }

  SVN_ERR(svn_uri_get_file_url_from_dirent(&sess->local_repos_root_url,
                                           sess->local_repos_abspath,
                                           result_pool));

  err = svn_repos_open3(&repos, sess->local_repos_abspath, NULL,
                        scratch_pool, scratch_pool);

  if (err)
    {
      apr_hash_t *fs_config;
      if (!APR_STATUS_IS_ENOENT(err->apr_err))
        return svn_error_trace(err);

      svn_error_clear(err);

      fs_config = apr_hash_make(session->pool);
      svn_hash_sets(fs_config, SVN_FS_CONFIG_FS_TYPE, SVN_FS_TYPE_GIT);

      SVN_ERR(svn_repos_create(&repos, sess->local_repos_abspath,
                               NULL, NULL, /* unused */
                               NULL /*config */, fs_config,
                               scratch_pool));
    }

/* Split the session URL into a git remote URL and, possibly, a path within
 * the repository (in sess->fs_path). */
  {
    const char *repos_relpath;
    apr_array_header_t *branches;

    SVN_ERR(svn_ra_git__split_url(&sess->repos_root_url,
                                  &repos_relpath,
                                  &sess->git_remote_url,
                                  &branches,
                                  sess, session_URL,
                                  result_pool, scratch_pool));

    svn_stringbuf_set(sess->repos_relpath_buf, repos_relpath);
    svn_stringbuf_set(sess->session_url_buf, session_URL);
  }

  sess->scratch_pool = svn_pool_create(session->pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
ra_git_set_svn_ra_open(svn_ra_session_t *session,
                       svn_ra__open_func_t func)
{
  svn_ra_git__session_t *sess = session->priv;

  sess->svn_ra_open = func;
  return SVN_NO_ERROR;
}

static svn_error_t *
ensure_local_session(svn_ra_session_t *session,
                     apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess = session->priv;

  /* TODO: Open ra_local session, etc. */
  if (!sess->local_session)
    {
      svn_revnum_t rev;
      SVN_DBG(("Opening inner ra session to: %s", sess->local_repos_root_url));
      SVN_ERR(sess->svn_ra_open(&sess->local_session, NULL,
                                sess->local_repos_root_url,
                                NULL,
                                sess->callbacks, sess->callback_baton,
                                sess->config, session->pool));

      if (sess->repos_relpath_buf->len)
        SVN_ERR(svn_ra_reparent(sess->local_session,
                                svn_path_url_add_component2(
                                  sess->local_repos_root_url,
                                  sess->repos_relpath_buf->data,
                                  scratch_pool),
                                scratch_pool));

      SVN_ERR(svn_ra_get_latest_revnum(sess->local_session, &rev, scratch_pool));

      if (rev <= 0)
        SVN_ERR(svn_ra_git__git_fetch(session, TRUE, scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ra_git_dup_session(svn_ra_session_t *new_session,
                   svn_ra_session_t *session,
                   const char *new_session_url,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *old_s = session->priv;
  svn_ra_git__session_t *new_s;

  /* Allocate and stash the session_sess args we have already. */
  new_s = apr_pcalloc(result_pool, sizeof(*new_s));

  new_s->callbacks = old_s->callbacks;
  new_s->callback_baton = old_s->callback_baton;
  new_s->config = old_s->config; /* ### copy? */

  new_s->repos_root_url = apr_pstrdup(result_pool, old_s->repos_root_url);

  new_s->local_session = NULL;
  new_s->local_repos_abspath = apr_pstrdup(result_pool,
                                           old_s->local_repos_abspath);
  new_s->local_repos_root_url = apr_pstrdup(result_pool, old_s->repos_root_url);

  new_s->git_remote_url = apr_pstrdup(result_pool, old_s->git_remote_url);
  new_s->fetch_done = old_s->fetch_done;

  new_s->session_url_buf = svn_stringbuf_create_ensure(384, result_pool);
  new_s->repos_relpath_buf = svn_stringbuf_create_ensure(256, result_pool);

  svn_stringbuf_set(new_s->session_url_buf, old_s->session_url_buf->data);
  svn_stringbuf_set(new_s->repos_relpath_buf, old_s->repos_relpath_buf->data);

  /* Cache the repository UUID as well */
  new_s->uuid = apr_pstrdup(result_pool, old_s->uuid);

  new_s->svn_ra_open = old_s->svn_ra_open;

  new_s->scratch_pool = svn_pool_create(new_session->pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
ra_git_reparent(svn_ra_session_t *session,
                const char *url,
                apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;
  const char *relpath = svn_uri_skip_ancestor(sess->repos_root_url, url, pool);
  /* If the new URL isn't the same as our repository root URL, then
     let's ensure that it's some child of it. */
  if (!relpath)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("URL '%s' is not a child of the session's repository root "
                               "URL '%s'"), url, sess->session_url_buf->data);

  if (strcmp(sess->session_url_buf->data, url) == 0)
    return NULL;

  SVN_ERR(svn_ra_reparent(sess->local_session,
                          svn_path_url_add_component2(sess->local_repos_root_url,
                                                      relpath, pool),
                          pool));

  svn_stringbuf_set(sess->repos_relpath_buf, relpath);
  svn_stringbuf_set(sess->session_url_buf, url);

  return SVN_NO_ERROR;
}

static svn_error_t *
ra_git_get_session_url(svn_ra_session_t *session,
                       const char **url,
                       apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  *url = sess->session_url_buf->data; /* MUST be in session pool */
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_git_get_latest_revnum(svn_ra_session_t *session,
                         svn_revnum_t *latest_revnum,
                         apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  SVN_ERR(ensure_local_session(session, pool));

  return svn_error_trace(
    svn_ra_get_latest_revnum(sess->local_session, latest_revnum, pool));
}

static svn_error_t *
ra_git_get_file_revs(svn_ra_session_t *session,
                     const char *path,
                     svn_revnum_t start,
                     svn_revnum_t end,
                     svn_boolean_t include_merged_revisions,
                     svn_file_rev_handler_t handler,
                     void *handler_baton,
                     apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  SVN_ERR(ensure_local_session(session, pool));

  return svn_error_trace(
    svn_ra_get_file_revs2(sess->local_session, path, start, end,
                          include_merged_revisions,
                          handler, handler_baton, pool));
}

static svn_error_t *
ra_git_get_dated_revision(svn_ra_session_t *session,
                          svn_revnum_t *revision,
                          apr_time_t tm,
                          apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  SVN_ERR(ensure_local_session(session, pool));

  return svn_error_trace(
    svn_ra_get_dated_revision(sess->local_session, revision, tm, pool));
}

static svn_error_t *
ra_git_change_rev_prop(svn_ra_session_t *session,
                       svn_revnum_t rev,
                       const char *name,
                       const svn_string_t *const *old_value_p,
                       const svn_string_t *value,
                       apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  SVN_ERR(ensure_local_session(session, pool));

  return svn_error_trace(
    svn_ra_change_rev_prop2(sess->local_session, rev, name, old_value_p,
                            value, pool));
}

static svn_error_t *
ra_git_get_uuid(svn_ra_session_t *session,
                const char **uuid,
                apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  *uuid = sess->uuid;
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_git_get_repos_root(svn_ra_session_t *session,
                      const char **url,
                      apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  *url = sess->repos_root_url;
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_git_rev_proplist(svn_ra_session_t *session,
                    svn_revnum_t rev,
                    apr_hash_t **props,
                    apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  SVN_ERR(ensure_local_session(session, pool));

  return svn_error_trace(
    svn_ra_rev_proplist(sess->local_session, rev, props, pool));
}

static svn_error_t *
ra_git_rev_prop(svn_ra_session_t *session,
                svn_revnum_t rev,
                const char *name,
                svn_string_t **value,
                apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  SVN_ERR(ensure_local_session(session, pool));

  return svn_error_trace(
    svn_ra_rev_prop(sess->local_session, rev, name, value, pool));
}

static svn_error_t *
ra_git_get_commit_editor(svn_ra_session_t *session,
                         const svn_delta_editor_t **editor,
                         void **edit_baton,
                         apr_hash_t *revprop_table,
                         svn_commit_callback2_t callback,
                         void *callback_baton,
                         apr_hash_t *lock_tokens,
                         svn_boolean_t keep_locks,
                         apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
ra_git_get_mergeinfo(svn_ra_session_t *session,
                     svn_mergeinfo_catalog_t *catalog,
                     const apr_array_header_t *paths,
                     svn_revnum_t revision,
                     svn_mergeinfo_inheritance_t inherit,
                     svn_boolean_t include_descendants,
                     apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  SVN_ERR(ensure_local_session(session, pool));

  return svn_error_trace(
    svn_ra_get_mergeinfo(sess->local_session, catalog, paths, revision,
                         inherit, include_descendants, pool));
}


static svn_error_t *
ra_git_do_update(svn_ra_session_t *session,
                 const svn_ra_reporter3_t **reporter,
                 void **report_baton,
                 svn_revnum_t update_revision,
                 const char *update_target,
                 svn_depth_t depth,
                 svn_boolean_t send_copyfrom_args,
                 svn_boolean_t ignore_ancestry,
                 const svn_delta_editor_t *update_editor,
                 void *update_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess = session->priv;

  SVN_ERR(ensure_local_session(session, scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, scratch_pool));

  SVN_ERR(svn_ra_do_update3(sess->local_session,
                            reporter, report_baton,
                            update_revision, update_target, depth,
                            send_copyfrom_args, ignore_ancestry,
                            update_editor, update_baton,
                            result_pool, scratch_pool));

  SVN_ERR(ra_git_wrap_reporter(reporter, report_baton,
                               *reporter, *report_baton,
                               session, result_pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
ra_git_do_switch(svn_ra_session_t *session,
                 const svn_ra_reporter3_t **reporter,
                 void **report_baton,
                 svn_revnum_t update_revision,
                 const char *update_target,
                 svn_depth_t depth,
                 const char *switch_url,
                 svn_boolean_t send_copyfrom_args,
                 svn_boolean_t ignore_ancestry,
                 const svn_delta_editor_t *update_editor,
                 void *update_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess = session->priv;
  const char *repos_relpath;

  SVN_ERR(ensure_local_session(session, scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, scratch_pool));

  SVN_ERR(svn_ra_get_path_relative_to_root(session, &repos_relpath,
                                           switch_url, scratch_pool));

  switch_url = svn_path_url_add_component2(sess->local_repos_root_url,
                                           repos_relpath, scratch_pool);

  SVN_ERR(svn_ra_do_switch3(sess->local_session,
                            reporter, report_baton,
                            update_revision, update_target, depth,
                            switch_url, send_copyfrom_args,
                            ignore_ancestry,
                            update_editor, update_baton,
                            result_pool, scratch_pool));

  SVN_ERR(ra_git_wrap_reporter(reporter, report_baton,
                               *reporter, *report_baton,
                               session, result_pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
ra_git_do_status(svn_ra_session_t *session,
                 const svn_ra_reporter3_t **reporter,
                 void **report_baton,
                 const char *status_target,
                 svn_revnum_t revision,
                 svn_depth_t depth,
                 const svn_delta_editor_t *status_editor,
                 void *status_baton,
                 apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);

  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  SVN_ERR(svn_ra_do_status2(sess->local_session,
                            reporter, report_baton,
                            status_target, revision, depth,
                            status_editor, status_baton, pool));

  SVN_ERR(ra_git_wrap_reporter(reporter, report_baton,
                               *reporter, *report_baton,
                               session, pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
ra_git_do_diff(svn_ra_session_t *session,
               const svn_ra_reporter3_t **reporter,
               void **report_baton,
               svn_revnum_t update_revision,
               const char *update_target,
               svn_depth_t depth,
               svn_boolean_t ignore_ancestry,
               svn_boolean_t text_deltas,
               const char *switch_url,
               const svn_delta_editor_t *update_editor,
               void *update_baton,
               apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;
  const char *repos_relpath;

  svn_pool_clear(sess->scratch_pool);

  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, TRUE, sess->scratch_pool));


  SVN_ERR(svn_ra_get_path_relative_to_root(session, &repos_relpath,
                                           switch_url, sess->scratch_pool));

  switch_url = svn_path_url_add_component2(sess->local_repos_root_url,
                                           repos_relpath, sess->scratch_pool);

  SVN_ERR(svn_ra_do_diff3(sess->local_session,
                          reporter, report_baton,
                          update_revision, update_target, depth,
                          ignore_ancestry, text_deltas, switch_url,
                          update_editor, update_baton, pool));

  SVN_ERR(ra_git_wrap_reporter(reporter, report_baton,
                               *reporter, *report_baton,
                               session, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
ra_git_get_log(svn_ra_session_t *session,
               const apr_array_header_t *paths,
               svn_revnum_t start,
               svn_revnum_t end,
               int limit,
               svn_boolean_t discover_changed_paths,
               svn_boolean_t strict_node_history,
               svn_boolean_t include_merged_revisions,
               const apr_array_header_t *revprops,
               svn_log_entry_receiver_t receiver,
               void *receiver_baton,
               apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);

  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_log2(sess->local_session, paths, start, end,
                    limit, discover_changed_paths, strict_node_history,
                    include_merged_revisions, revprops,
                    receiver, receiver_baton, pool));
}

static svn_error_t *
ra_git_do_check_path(svn_ra_session_t *session,
                     const char *path,
                     svn_revnum_t revision,
                     svn_node_kind_t *kind,
                     apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);

  SVN_ERR(ensure_local_session(session, sess->scratch_pool));

  /* ### TODO: Check if we can use the branch name cache for easy result */

  return svn_error_trace(
    svn_ra_check_path(sess->local_session, path, revision, kind, pool));
}


static svn_error_t *
ra_git_stat(svn_ra_session_t *session,
            const char *path,
            svn_revnum_t revision,
            svn_dirent_t **dirent,
            apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_stat(sess->local_session, path, revision, dirent, pool));
}

/* Getting just one file. */
static svn_error_t *
ra_git_get_file(svn_ra_session_t *session,
                const char *path,
                svn_revnum_t revision,
                svn_stream_t *stream,
                svn_revnum_t *fetched_rev,
                apr_hash_t **props,
                apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_file(sess->local_session, path, revision, stream, fetched_rev,
                    props, pool));
}

/* Getting a directory's entries */
static svn_error_t *
ra_git_get_dir(svn_ra_session_t *session,
               apr_hash_t **dirents,
               svn_revnum_t *fetched_rev,
               apr_hash_t **props,
               const char *path,
               svn_revnum_t revision,
               apr_uint32_t dirent_fields,
               apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_dir2(sess->local_session, dirents, fetched_rev, props,
                    path, revision, dirent_fields, pool));
}


static svn_error_t *
ra_git_get_locations(svn_ra_session_t *session,
                     apr_hash_t **locations,
                     const char *path,
                     svn_revnum_t peg_revision,
                     const apr_array_header_t *location_revisions,
                     apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_locations(sess->local_session, locations, path, peg_revision,
                         location_revisions, pool));
}


static svn_error_t *
ra_git_get_location_segments(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t peg_revision,
                             svn_revnum_t start_rev,
                             svn_revnum_t end_rev,
                             svn_location_segment_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_location_segments(sess->local_session, path, peg_revision,
                                 start_rev, end_rev, receiver, receiver_baton,
                                 pool));
}

static svn_error_t *
ra_git_lock(svn_ra_session_t *session,
            apr_hash_t *path_revs,
            const char *comment,
            svn_boolean_t steal_lock,
            svn_ra_lock_callback_t lock_func,
            void *lock_baton,
            apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_lock(sess->local_session, path_revs, comment, steal_lock, lock_func,
                lock_baton, pool));
}


static svn_error_t *
ra_git_unlock(svn_ra_session_t *session,
              apr_hash_t *path_tokens,
              svn_boolean_t break_lock,
              svn_ra_lock_callback_t lock_func,
              void *lock_baton,
              apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_unlock(sess->local_session, path_tokens, break_lock,
                  lock_func, lock_baton, pool));
}

static svn_error_t *
ra_git_get_lock(svn_ra_session_t *session,
                svn_lock_t **lock,
                const char *path,
                apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_lock(sess->local_session, lock, path, pool));
}

static svn_error_t *
ra_git_get_locks(svn_ra_session_t *session,
                 apr_hash_t **locks,
                 const char *path,
                 svn_depth_t depth,
                 apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_locks2(sess->local_session, locks, path, depth, pool));
}


static svn_error_t *
ra_git_replay(svn_ra_session_t *session,
              svn_revnum_t revision,
              svn_revnum_t low_water_mark,
              svn_boolean_t send_deltas,
              const svn_delta_editor_t *editor,
              void *edit_baton,
              apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_replay(sess->local_session, revision, low_water_mark, send_deltas,
                  editor, edit_baton, pool));
}


static svn_error_t *
ra_git_replay_range(svn_ra_session_t *session,
                    svn_revnum_t start_revision,
                    svn_revnum_t end_revision,
                    svn_revnum_t low_water_mark,
                    svn_boolean_t send_deltas,
                    svn_ra_replay_revstart_callback_t revstart_func,
                    svn_ra_replay_revfinish_callback_t revfinish_func,
                    void *replay_baton,
                    apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_replay_range(sess->local_session, start_revision, end_revision,
                        low_water_mark, send_deltas,
                        revstart_func, revfinish_func, replay_baton,
                        pool));
}

static svn_error_t *
ra_git_has_capability(svn_ra_session_t *session,
                      svn_boolean_t *has,
                      const char *capability,
                      apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));

  if (strcmp(capability, SVN_RA_CAPABILITY_COMMIT_REVPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_ATOMIC_REVPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_EPHEMERAL_TXNPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_MERGEINFO) == 0)
  {
    *has = FALSE;
    return SVN_NO_ERROR;
  }

  return svn_error_trace(
    svn_ra_has_capability(sess->local_session, has, capability, pool));
}

static svn_error_t *
ra_git_get_deleted_rev(svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t peg_revision,
                       svn_revnum_t end_revision,
                       svn_revnum_t *revision_deleted,
                       apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_deleted_rev(sess->local_session, path,
                           peg_revision, end_revision,
                           revision_deleted, pool));
}

static svn_error_t *
ra_git_get_inherited_props(svn_ra_session_t *session,
                           apr_array_header_t **iprops,
                           const char *path,
                           svn_revnum_t revision,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));
  SVN_ERR(svn_ra_git__git_fetch(session, FALSE, sess->scratch_pool));

  return svn_error_trace(
    svn_ra_get_inherited_props(sess->local_session, iprops, path, revision,
                               result_pool, scratch_pool));
}

static svn_error_t *
ra_git_register_editor_shim_callbacks(svn_ra_session_t *session,
                                      svn_delta_shim_callbacks_t *callbacks)
{
  svn_ra_git__session_t *sess = session->priv;

  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(ensure_local_session(session, sess->scratch_pool));

  return svn_error_trace(
    svn_ra__register_editor_shim_callbacks(sess->local_session, callbacks));
}



/*----------------------------------------------------------------*/

static const svn_version_t *
ra_git_version(void)
{
  SVN_VERSION_BODY;
}

/** The ra_vtable **/

static const svn_ra__vtable_t ra_git_vtable =
{
  ra_git_version,
  ra_git_get_description,
  ra_git_get_schemes,
  ra_git_open,
  ra_git_dup_session,
  ra_git_reparent,
  ra_git_get_session_url,
  ra_git_get_latest_revnum,
  ra_git_get_dated_revision,
  ra_git_change_rev_prop,
  ra_git_rev_proplist,
  ra_git_rev_prop,
  ra_git_get_commit_editor,
  ra_git_get_file,
  ra_git_get_dir,
  ra_git_get_mergeinfo,
  ra_git_do_update,
  ra_git_do_switch,
  ra_git_do_status,
  ra_git_do_diff,
  ra_git_get_log,
  ra_git_do_check_path,
  ra_git_stat,
  ra_git_get_uuid,
  ra_git_get_repos_root,
  ra_git_get_locations,
  ra_git_get_location_segments,
  ra_git_get_file_revs,
  ra_git_lock,
  ra_git_unlock,
  ra_git_get_lock,
  ra_git_get_locks,
  ra_git_replay,
  ra_git_has_capability,
  ra_git_replay_range,
  ra_git_get_deleted_rev,
  ra_git_get_inherited_props,
  ra_git_set_svn_ra_open,

  ra_git_register_editor_shim_callbacks,
  NULL /* get_commit_ev2 */,
  NULL /* replay_range_ev2 */
};


/*----------------------------------------------------------------*/
/** The One Public Routine, called by libsvn_ra **/

svn_error_t *
svn_ra_git__init(const svn_version_t *loader_version,
                 const svn_ra__vtable_t **vtable,
                 apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
  {
    { "svn_subr",  svn_subr_version },
    { NULL, NULL }
  };


/* Simplified version check to make sure we can safely use the
   VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Unsupported RA loader version (%d) for ra_git"),
                             loader_version->major);

  SVN_ERR(svn_ver_check_list2(ra_git_version(), checklist, svn_ver_equal));

  *vtable = &ra_git_vtable;

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for pre-1.2 subversions.  Needed? */
#define NAME "ra_git"
#define DESCRIPTION RA_GIT_DESCRIPTION
#define VTBL ra_git_vtable
#define INITFUNC svn_ra_git__init
#define COMPAT_INITFUNC svn_ra_git_init
#include "../libsvn_ra/wrapper_template.h"

