/*
 * fetch.c : Handles git repository url calculations and mirroring
 *           git repositories into a libsvn_fs_git backend.
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

#include "../libsvn_fs_git/svn_git.h"

#include "svn_hash.h"
#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_path.h"
#include "svn_version.h"
#include "svn_sorts.h"
#include "svn_repos.h"

#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"
#include "private/svn_atomic.h"
#include "private/svn_fspath.h"

#include "ra_git.h"

#define APR_WANT_STRFUNC
#include <apr_want.h>

#define RA_GIT_DEFAULT_REFSPEC      "+refs/*:refs/*"
#define RA_GIT_DEFAULT_REMOTE_NAME  "origin"

/*----------------------------------------------------------------*/

static volatile apr_uint32_t do_libgit2_init_called = 0;

static svn_error_t *
do_libgit2_init(void *baton, apr_pool_t *pool)
{
  git_libgit2_init();
  return SVN_NO_ERROR;
}

/*** Miscellaneous helper functions ***/

static apr_status_t
cleanup_git_remote(void *baton)
{
  git_remote_free(baton);
  return APR_SUCCESS;
}

static svn_error_t *
open_git_repos(git_repository **repos,
               git_remote **remote,
               git_remote_callbacks **callbacks,
               svn_ra_git__session_t *session,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool);

static const char *
make_git_url(const char *session_url,
             apr_pool_t *result_pool)
{
  if (strncmp(session_url, "git+", 4) == 0) /* git+file://, git+http://, git+https:// */
    return session_url + 4;
  else
    /* git:// */
    return session_url;
}

static const char *
make_svn_url(const char *git_url, apr_pool_t *result_pool)
{
  if (strncmp(git_url, "git:", 4) == 0) /* git:// */
    return git_url;
  else /* git+file://, git+http://, git+https:// */
    return apr_pstrcat(result_pool, "git+", git_url, SVN_VA_NULL);
}

svn_error_t *
svn_ra_git__split_url(const char **repos_root_url,
                      const char **repos_relpath,
                      const char **git_remote_url,
                      apr_array_header_t **branches,
                      svn_ra_git__session_t *session,
                      const char *url,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_boolean_t found_remote = FALSE;
  svn_stringbuf_t *remote_url_buf;
  git_repository *repos;
  git_remote *remote;
  git_remote_callbacks *callbacks;
  const char *remote_url_git;

  if (branches)
    *branches = NULL;

  SVN_ERR(open_git_repos(&repos, &remote, &callbacks,
                         session, scratch_pool, scratch_pool));

  /* ### TODO: Optimize this by checking if there is some "path.git"
               component, before starting at the end and working upwards.

               Perhaps starting at the root first, etc.
               */

  remote_url_git = make_git_url(url, scratch_pool);
  remote_url_buf = svn_stringbuf_create(remote_url_git, scratch_pool);
  while (!found_remote)
    {
      int git_err;

      SVN_DBG(("trying remote url '%s'", remote_url_buf->data));

      /* Create an in-memory remote... */
      GIT2_ERR(git_remote_create_anonymous(&remote, repos,
                                           remote_url_buf->data));

      apr_pool_cleanup_register(scratch_pool, remote, cleanup_git_remote,
                                apr_pool_cleanup_null);

      /* ... and try to connect to it. */
      git_err = git_remote_connect(remote, GIT_DIRECTION_FETCH, callbacks,
                                   NULL /* proxy_opts */,
                                   NULL /* custom_headers */);
      if (!git_err)
        {
          found_remote = TRUE;
          break;
        }

      giterr_clear();

      if (svn_uri_is_root(remote_url_buf->data, remote_url_buf->len))
        break;

      svn_path_remove_component(remote_url_buf);

      apr_pool_cleanup_run(scratch_pool, remote, cleanup_git_remote);
      remote = NULL;
    }

  if (branches && found_remote)
    {
      git_remote_head** heads;
      size_t head_count, i;
    
      /* This may look like it contacts the server, but this data is already
         cached in libgit2 as it is always sent as part of the handshake */
      GIT2_ERR(git_remote_ls(&heads, &head_count, remote));
    
      *branches = apr_array_make(result_pool, head_count,
                                 sizeof(svn_ra_git_branch_t *));
    
      for (i = 0; i < head_count; i++)
        {
          git_remote_head *head = heads[i];
          svn_ra_git_branch_t *br = apr_pcalloc(result_pool, sizeof(*br));

          br->name = apr_pstrdup(result_pool, head->name);
          if (head->symref_target)
            br->symref_target = apr_pstrdup(result_pool, head->symref_target);

          APR_ARRAY_PUSH(*branches, svn_ra_git_branch_t *) = br;
          SVN_DBG(("Noticed: %s -> %s", br->name, br->symref_target));
        }
    }

  apr_pool_cleanup_run(scratch_pool, remote, cleanup_git_remote);
  remote = NULL;

  if (found_remote)
    {
      *repos_root_url = make_svn_url(remote_url_buf->data, result_pool);
      *repos_relpath = svn_uri_skip_ancestor(remote_url_buf->data,
                                             remote_url_git, result_pool);
      *git_remote_url = apr_pstrdup(result_pool, remote_url_buf->data);
    }
  else
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("No git repository found at URL '%s'"),
                             url);

  SVN_DBG(("found remote url '%s', fs_path: '%s'\n",
           *repos_root_url, *repos_relpath));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_git__git_fetch(svn_ra_session_t *session,
                      svn_boolean_t refresh,
                      apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess = session->priv;
  git_repository *repos;
  git_remote *remote;
  git_remote_callbacks *callbacks;
  apr_pool_t *subpool;
#ifdef SVN_DEBUG
  svn_revnum_t youngest;
#endif

  /* Do one fetch per session. */
  if (sess->fetch_done && !refresh)
    return SVN_NO_ERROR;

  /* Create subpool, to allow closing handles early on */
  subpool = svn_pool_create(scratch_pool);

  SVN_ERR(open_git_repos(&repos, &remote, &callbacks, sess,
                         subpool, subpool));

  SVN_DBG(("Fetching from %s\n", sess->git_remote_url));

  {
    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;

    fetch_opts.callbacks = *callbacks;
    fetch_opts.prune = GIT_FETCH_PRUNE;
    fetch_opts.update_fetchhead = TRUE;
    fetch_opts.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_ALL;

    GIT2_ERR(git_remote_fetch(remote, NULL, &fetch_opts, NULL));
  }

  sess->fetch_done = TRUE;

  /* This makes svn_fs_git() add new revisions to the mapping system */
  SVN_ERR(svn_repos_recover4(sess->local_repos_abspath, FALSE,
                             NULL, NULL,
                             session->cancel_func, session->cancel_baton,
                             subpool));
#ifdef SVN_DEBUG
  SVN_ERR(sess->local_session->vtable->get_latest_revnum(
                                   sess->local_session, &youngest,
                                   subpool));

  SVN_DBG(("Latest revision r%ld\n", youngest));
#endif
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_git__push_commit(svn_ra_session_t *session,
                        const char *reference,
                        const char *edit_relpath,
                        const struct git_oid *commit_oid,
                        svn_commit_callback2_t callback,
                        void *callback_baton,
                        apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess = session->priv;
  git_repository *repos;
  git_remote *remote;
  git_remote_callbacks *callbacks;
  apr_pool_t *subpool;
  const char *to_ref = "refs/heads/master";

  if (strcmp(edit_relpath, "trunk") == 0)
    {
      int i;
      for (i = 0; i < sess->branches->nelts; i++)
        {
          svn_ra_git_branch_t *br = APR_ARRAY_IDX(sess->branches, i,
                                                  svn_ra_git_branch_t *);

          if (br->symref_target && !strcasecmp(br->name, "HEAD"))
            to_ref = br->symref_target;
        }
    }
  else
    {
      /* TODO: Determine proper branch reference from edit path */
    }

  /* Create subpool, to allow closing handles early on */
  subpool = svn_pool_create(scratch_pool);

  SVN_ERR(open_git_repos(&repos, &remote, &callbacks, sess,
                         subpool, subpool));

  SVN_DBG(("Pushing to %s\n", sess->git_remote_url));

  {
    git_push_options push_opts = GIT_PUSH_OPTIONS_INIT;
    git_strarray refspecs;
    char *refspec;
    refspecs.count = 1;
    refspecs.strings = &refspec;

    push_opts.callbacks = *callbacks;
    refspec = apr_pstrcat(scratch_pool, "+", reference, ":", to_ref,
                          SVN_VA_NULL);

    GIT2_ERR(git_remote_push(remote, &refspecs, &push_opts));
  }

  svn_pool_clear(subpool);

  SVN_ERR(svn_ra_git__git_fetch(session, TRUE, subpool));

  if (callback)
    {
      apr_hash_t *revprops;
      svn_commit_info_t *info = svn_create_commit_info(subpool);

      /* Should we really handle cases where this isn't the last
         revision? */
      SVN_ERR(sess->local_session->vtable->get_latest_revnum(
        sess->local_session, &info->revision,
        subpool));

      SVN_ERR(sess->local_session->vtable->rev_proplist(
        sess->local_session, info->revision, &revprops, subpool));

      info->author = svn_prop_get_value(revprops, SVN_PROP_REVISION_AUTHOR);
      info->date = svn_prop_get_value(revprops, SVN_PROP_REVISION_DATE);
      info->repos_root = sess->repos_root_url;

      SVN_ERR(callback(info, callback_baton, scratch_pool));
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Fetch a username for use with SESS */
static svn_error_t *
get_username(const char **username,
             svn_ra_git__session_t *sess,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  *username = NULL;

  /* Get a username somehow, so we have some svn:author property to
      attach to a commit. */
  if (sess->callbacks->auth_baton)
    {
      void *creds;
      svn_auth_cred_username_t *username_creds;
      svn_auth_iterstate_t *iterstate;

      SVN_ERR(svn_auth_first_credentials(&creds, &iterstate,
                                         SVN_AUTH_CRED_USERNAME,
                                         sess->uuid, /* realmstring */
                                         sess->callbacks->auth_baton,
                                         scratch_pool));

      /* No point in calling next_creds(), since that assumes that the
          first_creds() somehow failed to authenticate.  But there's no
          challenge going on, so we use whatever creds we get back on
          the first try. */
      username_creds = creds;
      if (username_creds && username_creds->username)
        {
          *username = apr_pstrdup(result_pool,
                                  username_creds->username);
          svn_error_clear(svn_auth_save_credentials(iterstate,
                                                    scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------*/
/* git remote callbacks - Wrapped into Subversion like wrapper to
                          allow using normal apis */

typedef struct ra_git_remote_baton_t
{
  apr_pool_t *remote_pool;
  git_remote *remote;

  svn_error_t *err;

  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  apr_pool_t *scratch_pool;
  svn_auth_iterstate_t *auth_iter;
  unsigned int authtypes_done;

  svn_boolean_t stopped;
  svn_ra_git__session_t *sess;

  apr_size_t last_received_bytes;
} ra_git_remote_baton_t;

static svn_error_t *
remote_sideband_progress(ra_git_remote_baton_t *grb,
                         const char *str, int len,
                         apr_pool_t *scratch_pool)
{
  if (len)
    {
      str = apr_pstrmemdup(scratch_pool, str, len);
    
      SVN_DBG(("%s\n", str));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
remote_completion(ra_git_remote_baton_t *grb,
                  git_remote_completion_type type,
                  apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
remote_credentials_acquire(git_cred **cred,
                           ra_git_remote_baton_t *grb,
                           const char *url,
                           const char *username_from_url,
                           unsigned int allowed_types,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *realm;
  *cred = NULL;

  allowed_types &= ~grb->authtypes_done;

  realm = apr_pstrcat(scratch_pool, "<", url, "> git repository", SVN_VA_NULL);

  if (allowed_types & GIT_CREDTYPE_DEFAULT)
    {
      GIT2_ERR(git_cred_default_new(cred));
      grb->authtypes_done |= GIT_CREDTYPE_DEFAULT; /* Only do this once */
      return SVN_NO_ERROR;
    }

  if (allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT)
    {
      void *svn_creds;
      if (!grb->auth_iter)
        SVN_ERR(svn_auth_first_credentials(&svn_creds, &grb->auth_iter,
                                           SVN_AUTH_CRED_SIMPLE,
                                           apr_pstrdup(grb->remote_pool, realm),
                                           grb->sess->callbacks->auth_baton,
                                           grb->remote_pool));
      else
        SVN_ERR(svn_auth_next_credentials(&svn_creds, grb->auth_iter,
                                          result_pool));

      if (svn_creds)
        {
          svn_auth_cred_simple_t *simple_creds = svn_creds;

          GIT2_ERR(git_cred_userpass_plaintext_new(cred,
                                                   simple_creds->username,
                                                   simple_creds->password));
          return SVN_NO_ERROR;
        }
      else
        {
          grb->auth_iter = NULL; /* Don't save on success */
          grb->authtypes_done |= GIT_CREDTYPE_USERPASS_PLAINTEXT;
          /* And fall through to try other cred types */
        }
    }

  if (allowed_types & GIT_CREDTYPE_USERNAME)
    {
      const char *username;
      SVN_ERR(get_username(&username, grb->sess, scratch_pool,
                           scratch_pool));

      if (username)
        {
          GIT2_ERR(git_cred_username_new(cred, username));
          return SVN_NO_ERROR;
        }
      else
        {
          grb->authtypes_done |= GIT_CREDTYPE_USERNAME;
          /* And fall through */
        }
    }

  /* TODO: GIT_CREDTYPE_SSH_KEY,
           GIT_CREDTYPE_SSH_CUSTOM,
           GIT_CREDTYPE_SSH_INTERACTIVE,
           GIT_CREDTYPE_SSH_MEMORY */

  return SVN_NO_ERROR;
}


static svn_error_t *
remote_transport_certificate_check(ra_git_remote_baton_t *grb,
                                   git_cert *cert,
                                   int valid,
                                   const char *host,
                                   void *payload)
{
  /* ### TODO: */
  return SVN_NO_ERROR;
}

static svn_error_t *
remote_transfer_progress(ra_git_remote_baton_t *grb,
                         const git_transfer_progress *stats,
                         apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess = grb->sess;

  if (stats->received_bytes < grb->last_received_bytes)
    grb->last_received_bytes = 0;

  if (stats->received_bytes > grb->last_received_bytes)
    {
      apr_size_t added = stats->received_bytes - grb->last_received_bytes;

      sess->progress_bytes += added;

      if (sess->callbacks->progress_func)
        sess->callbacks->progress_func(sess->progress_bytes, -1,
                                       sess->callbacks->progress_baton,
                                       scratch_pool);
    }

  /*SVN_DBG(("objects: %u total %u indexed %u received %u local, "
           "deltas: %u total %u indexed\n",
           stats->total_objects, stats->indexed_objects,
           stats->received_objects, stats->local_objects,
           stats->total_deltas, stats->indexed_deltas));*/

  return SVN_NO_ERROR;
}

static svn_error_t *
remote_update_tips(svn_ra_git__session_t *sess,
                   const char *refname,
                   const git_oid *a,
                   const git_oid *b,
                   apr_pool_t *scratch_pool)
{
  SVN_DBG(("Updating: %s", refname));

  return SVN_NO_ERROR;
}

static int
remote_update_tips_cb(const char *refname,
                      const git_oid *a,
                      const git_oid *b,
                      void *data)
{
  ra_git_remote_baton_t *grb = data;

  svn_pool_clear(grb->scratch_pool);

  if (!grb->err && grb->cancel_func)
    grb->err = grb->cancel_func(grb->cancel_baton);

  if (!grb->err)
    grb->err = remote_update_tips(grb->sess, refname,
                                  a, b, grb->scratch_pool);

  return (grb->err ? 1 : 0);
}


static int
git_remote_sideband_progress_cb(const char *str, int len, void *data)
{
  ra_git_remote_baton_t *grb = data;

  svn_pool_clear(grb->scratch_pool);

  if (!grb->err && grb->cancel_func)
    grb->err = grb->cancel_func(grb->cancel_baton);

  if (!grb->err)
    grb->err = remote_sideband_progress(grb, str, len, grb->scratch_pool);

  if (grb->err && !grb->stopped)
    {
      grb->stopped = TRUE;
      git_remote_stop(grb->remote);
    }

  return (grb->err ? 1 : 0);
}

static int
git_remote_completion_cb(git_remote_completion_type type, void *data)
{
  ra_git_remote_baton_t *grb = data;

  svn_pool_clear(grb->scratch_pool);

  if (!grb->err && grb->cancel_func)
    grb->err = grb->cancel_func(grb->cancel_baton);

  if (!grb->err)
    grb->err = remote_completion(grb, type, grb->scratch_pool);

  if (grb->err && !grb->stopped)
    {
      grb->stopped = TRUE;
      git_remote_stop(grb->remote);
    }

  if (!grb->err && grb->auth_iter)
    {
      grb->err = svn_auth_save_credentials(grb->auth_iter, grb->scratch_pool);
      grb->auth_iter = NULL;
    }

  return (grb->err ? 1 : 0);
}

static int
git_remote_credentials_acquire_cb(git_cred **cred,
                                  const char *url,
                                  const char *username_from_url,
                                  unsigned int allowed_types,
                                  void *payload)
{
  ra_git_remote_baton_t *grb = payload;

  svn_pool_clear(grb->scratch_pool);

  if (!grb->err && grb->cancel_func)
    grb->err = grb->cancel_func(grb->cancel_baton);

  if (!grb->err)
    grb->err = remote_credentials_acquire(cred, grb, url, username_from_url,
                                          allowed_types,
                                          grb->scratch_pool, grb->scratch_pool);

  if (grb->err && !grb->stopped)
    {
      grb->stopped = TRUE;
      git_remote_stop(grb->remote);
    }

  return (grb->err ? 1 : 0);
}

static int
remote_transport_certificate_check_cb(git_cert *cert,
                                      int valid,
                                      const char *host,
                                      void *payload)
{
  ra_git_remote_baton_t *grb = payload;

  svn_pool_clear(grb->scratch_pool);

  if (!grb->err && grb->cancel_func)
    grb->err = grb->cancel_func(grb->cancel_baton);

  if (!grb->err)
    grb->err = remote_transport_certificate_check(grb,
                                                  cert, valid, host,
                                                  grb->scratch_pool);

  if (grb->err && !grb->stopped)
    {
      grb->stopped = TRUE;
      git_remote_stop(grb->remote);
    }

  return (grb->err ? 1 : 0);
}

static int remote_transfer_progress_cb(const git_transfer_progress *stats,
                                       void *data)
{
  ra_git_remote_baton_t *grb = data;

  svn_pool_clear(grb->scratch_pool);

  if (!grb->err && grb->cancel_func)
    grb->err = grb->cancel_func(grb->cancel_baton);

  if (!grb->err)
    grb->err = remote_transfer_progress(grb, stats, grb->scratch_pool);

  if (grb->err && !grb->stopped)
    {
      grb->stopped = TRUE;
      git_remote_stop(grb->remote);
    }

  return (grb->err ? 1 : 0);
}

static svn_error_t *
open_git_repos(git_repository **repos,
               git_remote **remote,
               git_remote_callbacks **callbacks,
               svn_ra_git__session_t *sess,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_atomic__init_once(&do_libgit2_init_called, do_libgit2_init,
                                NULL, scratch_pool));

  SVN_ERR(svn_git__repository_open(repos,
                                   svn_dirent_join(sess->local_repos_abspath,
                                                   "db/git", scratch_pool),
                                   result_pool));

  if (remote)
    {
      *remote = NULL;
      if (callbacks)
        {
          git_remote_callbacks cb = GIT_REMOTE_CALLBACKS_INIT;
          ra_git_remote_baton_t *grb = apr_pcalloc(result_pool, sizeof(*grb));

          cb.sideband_progress = git_remote_sideband_progress_cb;
          cb.completion = git_remote_completion_cb;
          cb.credentials = git_remote_credentials_acquire_cb;
          cb.certificate_check = remote_transport_certificate_check_cb;
          cb.transfer_progress = remote_transfer_progress_cb;
          cb.update_tips = remote_update_tips_cb;
          cb.payload = grb;

          *callbacks = apr_pmemdup(result_pool, &cb, sizeof(cb));

          grb->remote = *remote;
          grb->sess = sess;
          grb->remote_pool = result_pool;
          grb->scratch_pool = svn_pool_create(result_pool);
          grb->cancel_func = sess->callbacks->cancel_func;
          grb->cancel_baton = sess->callback_baton;
        }

    /* Check if our remote already exists. */
      GIT2_ERR_NOTFOUND(remote, git_remote_lookup(remote, *repos,
                                                  RA_GIT_DEFAULT_REMOTE_NAME));
      if (!*remote)
        {
          /* No remote yet. Let's setup a remote in a similar way as
             git --mirror would */
          GIT2_ERR(git_remote_create_with_fetchspec(
                    remote, *repos, RA_GIT_DEFAULT_REMOTE_NAME,
                    sess->git_remote_url, RA_GIT_DEFAULT_REFSPEC));
        }

      if (*remote)
        apr_pool_cleanup_register(result_pool, *remote, cleanup_git_remote,
                                  apr_pool_cleanup_null);
    }

  return SVN_NO_ERROR;
}

void
svn_ra_git__libgit2_version(int *major,
                            int *minor,
                            int *rev,
                            const char **compiled)
{
  git_libgit2_version(major, minor, rev);
  *compiled = LIBGIT2_VERSION;
}
