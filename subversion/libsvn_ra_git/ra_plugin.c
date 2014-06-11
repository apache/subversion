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
#include "svn_delta.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_path.h"
#include "svn_version.h"
#include "svn_sorts.h"

#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"
#include "private/svn_atomic.h"
#include "private/svn_fspath.h"

#include "ra_git.h"

#define APR_WANT_STRFUNC
#include <apr_want.h>

#define RA_GIT_DEFAULT_REFSPEC      "+refs/heads/master:refs/remotes/origin/master"
#define RA_GIT_DEFAULT_REMOTE_NAME  "origin"
#define RA_GIT_DEFAULT_REF          "refs/remotes/origin/master"

typedef struct svn_ra_git__session_baton_t
{
  /* The URL of the session. */
  const char *session_url;

  /* The user accessing the repository. */
  const char *username;

  /* Git repository data structures. */
  git_repository *repos;
  git_remote *remote;
  git_revwalk *revwalk;

  /* The URL of the remote. */
  const char *remote_url;

  /* The local abspath to the local git repository. */
  const char *repos_abspath;

  /* Wether we did 'git fetch' for this session already. */
  svn_boolean_t fetch_done;

  /* The relative path in the tree the session is rooted at. */
  svn_stringbuf_t *fs_path;  /* URI-decoded, always without leading slash. */

  /* The UUID associated with REPOS above (cached) */
  const char *uuid;

  /* Map revision numbers to git commit IDs. */
  apr_hash_t *revmap;

  /* Callbacks/baton passed to svn_ra_open. */
  const svn_ra_callbacks2_t *callbacks;
  void *callback_baton;

  const char *useragent;

  /* Scratch pool for routines that cannot otherwise get one. */
  apr_pool_t *scratch_pool;

} svn_ra_git__session_baton_t;

/*----------------------------------------------------------------*/

/*** Miscellaneous helper functions ***/

svn_error_t *
svn_ra_git__wrap_git_error(void)
{
  git_error git_err;

  if (giterr_detach(&git_err) == -1)
    SVN_ERR_MALFUNCTION();

  /* ### TODO: map error code */
  return svn_error_createf(SVN_ERR_BASE, NULL,
                           _("git: %s"), git_err.message);
}

static const char *
make_git_url(const char *session_url)
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

static svn_error_t *
split_url(const char **remote_url,
          svn_stringbuf_t *fs_path,
          git_repository *repos,
          const char *session_url,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  svn_boolean_t found_remote = FALSE;
  svn_stringbuf_t *remote_url_buf;
  
  remote_url_buf = svn_stringbuf_create(make_git_url(session_url), scratch_pool);
  while (!found_remote)
    {
      git_remote *remote;
      int git_err;

      SVN_DBG(("trying remote url '%s'", remote_url_buf->data));

      /* Create an in-memory remote... */
      git_err = git_remote_create_inmemory(&remote, repos,
                                           RA_GIT_DEFAULT_REFSPEC,
                                           remote_url_buf->data);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());

      /* ... and try to connect to it. */
      git_err = git_remote_connect(remote, GIT_DIRECTION_FETCH);
      if (git_err)
        {
          apr_size_t slash_pos;
          const char *component;

          giterr_clear();

          slash_pos = svn_stringbuf_find_char_backward(remote_url_buf, '/');
          if (slash_pos >= remote_url_buf->len)
            break;

          if (!svn_stringbuf_isempty(fs_path))
            component = apr_pstrcat(scratch_pool, remote_url_buf->data + slash_pos + 1,
                                   "/", SVN_VA_NULL); 
          else
            component = apr_pstrcat(scratch_pool, remote_url_buf->data + slash_pos + 1,
                                    SVN_VA_NULL); 
          svn_stringbuf_insert(fs_path, 0, component, strlen(component));

          svn_stringbuf_chop(remote_url_buf, remote_url_buf->len - slash_pos); 
        }
      else
        found_remote = TRUE;

      git_remote_free(remote);
    }

  if (found_remote)
    *remote_url = apr_pstrdup(result_pool, remote_url_buf->data);
  else
    return svn_error_compose_create(
             svn_ra_git__wrap_git_error(),
             svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                               _("No git repository found at URL '%s'"),
                               session_url));

  SVN_DBG(("found remote url '%s', fs_path: '%s'\n", *remote_url, fs_path->data));

  return SVN_NO_ERROR;
}

static svn_error_t *
do_git_fetch(svn_ra_git__session_baton_t *sess)
{
  int git_err;

  /* Do one fetch per session.
   * ### mutex? atomic_init? */
  if (sess->fetch_done)
    return SVN_NO_ERROR;

  SVN_DBG(("fetching from %s\n", git_remote_url(sess->remote)));

  git_err = git_remote_fetch(sess->remote);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  sess->fetch_done = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
fill_revmap(git_revwalk *revwalk,
            git_repository *repos,
            apr_hash_t *revmap,
            apr_pool_t *pool)
{
  svn_revnum_t rev;
  int git_err;

  /* If the revmap has already been filled, there is nothing to do. */
  if (apr_hash_count(revmap) > 0)
    return SVN_NO_ERROR;

  git_revwalk_reset(revwalk);
  git_revwalk_push_ref(revwalk, RA_GIT_DEFAULT_REF);
  git_revwalk_simplify_first_parent(revwalk);
  git_revwalk_sorting(revwalk, GIT_SORT_REVERSE);

  SVN_DBG(("scanning git commits...\n"));
  rev = 0;
  do
    {
      git_oid oid;

      git_err = git_revwalk_next(&oid, revwalk);
      if (git_err)
        {
          if (git_err != GIT_ITEROVER)
            return svn_error_trace(svn_ra_git__wrap_git_error());
        }
      else
        {
          git_commit *commit;
          svn_revnum_t *revp;
          git_oid *oidp;
          char rev_str[GIT_OID_HEXSZ + 1];

          git_err = git_commit_lookup(&commit, repos, &oid);
          if (git_err)
            return svn_error_trace(svn_ra_git__wrap_git_error());

          revp = apr_palloc(apr_hash_pool_get(revmap), sizeof(*revp));
          *revp = ++rev;
          oidp = apr_palloc(apr_hash_pool_get(revmap), sizeof(*oidp));
          git_oid_cpy(oidp, &oid);
          apr_hash_set(revmap, revp, sizeof(*revp), oidp);

          git_oid_tostr(rev_str, sizeof(rev_str), oidp);
          SVN_DBG(("r%lu -> %s", rev, rev_str));

          git_commit_free(commit);
        }
    }
  while (git_err != GIT_ITEROVER);

  SVN_DBG(("done scanning git commits (%lu revisions)\n", rev));

  return SVN_NO_ERROR;
}

/* Return the git tree, and the git commit pointing to it, corresponding
 * to Subverswion revision REVISION. If REVISION is SVN_INVALID_REVNUM
 * fetch the HEAD revision and store its revision number in *FETCHED_REV
 * if FETCHED_REV is not NULL.
 *
 * PATH is relative to the session url of SESS. Return the corresponding
 * repository-root-relative path in *REPOS_ROOT_RELPATH if REPOS_ROOT_RELPATH
 * is not NULL.
 *
 * Do all allocations in POOL. */
static svn_error_t *
fetch_revision_root(git_tree **tree,
                    git_commit **commit,
                    const char **repos_root_relpath,
                    svn_revnum_t *fetched_rev,
                    svn_ra_git__session_baton_t *sess,
                    const char *path,
                    svn_revnum_t revision,
                    apr_pool_t *pool)
{
  git_oid *oid;
  int git_err;

  do_git_fetch(sess);
  fill_revmap(sess->revwalk, sess->repos, sess->revmap, pool);

  if (!SVN_IS_VALID_REVNUM(revision))
    revision = apr_hash_count(sess->revmap);

  oid = apr_hash_get(sess->revmap, &revision, sizeof(revision));
  if (oid == NULL)
    return svn_error_create(SVN_ERR_FS_NO_SUCH_REVISION, NULL, NULL);

  git_err = git_commit_lookup(commit, sess->repos, oid);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  if (tree)
    {
      git_err = git_commit_tree(tree, *commit);
      if (git_err)
        {
          git_commit_free(*commit);
          return svn_error_trace(svn_ra_git__wrap_git_error());
        }
    }

  /* Handle reparented sessions and sessions not rooted at the git repos root. */
  if (repos_root_relpath)
    {
      if (!svn_stringbuf_isempty(sess->fs_path))
        *repos_root_relpath = svn_relpath_join(sess->fs_path->data, path, pool);
      else
        *repos_root_relpath = path;
     }

  if (fetched_rev)
    *fetched_rev = revision;

  return SVN_NO_ERROR;
}

/* Fetch a username for use with SESSION, and store it in SESSION->username.
 *
 * Allocate the username in SESSION->pool.  Use SCRATCH_POOL for temporary
 * allocations. */
static svn_error_t *
get_username(svn_ra_session_t *session,
             apr_pool_t *scratch_pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;

  /* If we've already found the username don't ask for it again. */
  if (! sess->username)
    {
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
              sess->username = apr_pstrdup(session->pool,
                                           username_creds->username);
              svn_error_clear(svn_auth_save_credentials(iterstate,
                                                        scratch_pool));
            }
          else
            sess->username = "";
        }
      else
        sess->username = "";
    }

  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------*/

/*** The reporter vtable needed by do_update() and friends ***/

typedef struct reporter_baton_t
{
  svn_ra_git__session_baton_t *sess;
  void *report_baton;

} reporter_baton_t;

static svn_error_t *
reporter_set_path(void *reporter_baton,
                  const char *path,
                  svn_revnum_t revision,
                  svn_depth_t depth,
                  svn_boolean_t start_empty,
                  const char *lock_token,
                  apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_error_trace(svn_ra_git__reporter_set_path(rbaton->report_baton,
                                                       path, revision, depth,
                                                       start_empty, lock_token,
                                                       pool));
}

static svn_error_t *
reporter_delete_path(void *reporter_baton,
                     const char *path,
                     apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_error_trace(svn_ra_git__reporter_delete_path(rbaton->report_baton,
                                                          path, pool));
}


static svn_error_t *
reporter_link_path(void *reporter_baton,
                   const char *path,
                   const char *url,
                   svn_revnum_t revision,
                   svn_depth_t depth,
                   svn_boolean_t start_empty,
                   const char *lock_token,
                   apr_pool_t *pool)
{
  reporter_baton_t *rb = reporter_baton;
  const char *linked_path;

  linked_path = svn_uri_skip_ancestor(rb->sess->remote_url,
                                      make_git_url(url), pool);
  if (!linked_path)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("'%s'\n"
                               "is not the same repository as\n"
                               "'%s'"), url, rb->sess->session_url);

  if (!svn_stringbuf_isempty(rb->sess->fs_path))
    {
      path = svn_relpath_join(rb->sess->fs_path->data, path, pool);
      linked_path = svn_relpath_join(rb->sess->fs_path->data,
                                        linked_path, pool);
    }

  return svn_error_trace(svn_ra_git__reporter_link_path(rb->report_baton,
                                                        path, linked_path,
                                                        revision,
                                                        depth, start_empty,
                                                        lock_token, pool));
}

static svn_error_t *
reporter_finish_report(void *reporter_baton,
                       apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_error_trace(svn_ra_git__reporter_finish_report(
                           rbaton->report_baton, pool));
}

static svn_error_t *
reporter_abort_report(void *reporter_baton,
                      apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_error_trace(svn_ra_git__reporter_abort_report(
                           rbaton->report_baton, pool));
}


static const svn_ra_reporter3_t ra_git_reporter =
{
  reporter_set_path,
  reporter_delete_path,
  reporter_link_path,
  reporter_finish_report,
  reporter_abort_report
};


/* ...
 *
 * Allocate @a *reporter and @a *report_baton in @a result_pool.  Use
 * @a scratch_pool for temporary allocations.
 */
static svn_error_t *
make_reporter(svn_ra_session_t *session,
              const svn_ra_reporter3_t **reporter,
              void **report_baton,
              svn_revnum_t revision,
              const char *target,
              const char *other_url,
              svn_boolean_t text_deltas,
              svn_depth_t depth,
              svn_boolean_t send_copyfrom_args,
              svn_boolean_t ignore_ancestry,
              const svn_delta_editor_t *editor,
              void *edit_baton,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  struct reporter_baton_t *rb;
  const char *other_fs_path = NULL;
  void *wrapped_rb;

  /* Get the HEAD revision if one is not supplied. */
  if (! SVN_IS_VALID_REVNUM(revision))
    revision = apr_hash_count(sess->revmap);

  /* If OTHER_URL was provided, validate it and convert it into a
     regular filesystem path. */
  if (other_url)
    {
      const char *other_relpath
        = svn_uri_skip_ancestor(sess->remote_url, make_git_url(other_url),
                                scratch_pool);

      /* Sanity check:  the other_url better be in the same repository as
         the original session url! */
      if (! other_relpath)
        return svn_error_createf
          (SVN_ERR_RA_ILLEGAL_URL, NULL,
           _("'%s'\n"
             "is not the same repository as\n"
             "'%s'"), other_url, sess->session_url);

      other_fs_path = other_relpath;
    }

  if (sess->callbacks)
    SVN_ERR(svn_delta_get_cancellation_editor(sess->callbacks->cancel_func,
                                              sess->callback_baton,
                                              editor,
                                              edit_baton,
                                              &editor,
                                              &edit_baton,
                                              result_pool));

  /* Build a reporter baton. */
  SVN_ERR(svn_ra_git__reporter_begin_report(&wrapped_rb,
                                            revision,
                                            sess->repos,
                                            sess->revmap,
                                            sess->fs_path->data,
                                            target,
                                            other_fs_path,
                                            text_deltas,
                                            depth,
                                            ignore_ancestry,
                                            send_copyfrom_args,
                                            editor,
                                            edit_baton,
                                            1024 * 1024,
                                            result_pool));

  /* Pass back our reporter */
  *reporter = &ra_git_reporter;
  rb = apr_palloc(result_pool, sizeof(*rb));
  rb->sess = sess;
  rb->report_baton = wrapped_rb;
  *report_baton = rb;

  return SVN_NO_ERROR;
}

static apr_status_t
cleanup_temporary_repos(void *data)
{
  svn_ra_session_t *session = data;
  svn_ra_git__session_baton_t *sess = session->priv;
  svn_error_t *err;

  err = svn_io_remove_dir2(sess->repos_abspath, TRUE, NULL, NULL, session->pool);
  if (err)
    {
      apr_status_t apr_err = err->apr_err;
      svn_error_clear(err);
      return apr_err;
    }

  return APR_SUCCESS;
}


static void
check_cancel_stop_remote(svn_ra_git__session_baton_t *sess)
{
  svn_error_t *err;

  if (sess->callbacks->cancel_func == NULL)
    return;

  err = (sess->callbacks->cancel_func)(sess->callback_baton);
  if (err)
    {
      if (err->apr_err == SVN_ERR_CANCELLED)
        git_remote_stop(sess->remote);
      svn_error_clear(err);
    }
}

static int remote_progress_cb(const char *str, int len, void *data)
{
  svn_ra_git__session_baton_t *sess = data;
  svn_string_t *s;

  if (len)
    {
      svn_pool_clear(sess->scratch_pool);
      s = svn_string_ncreate(str, len, sess->scratch_pool);
      SVN_DBG(("%s\n", s->data));
    }

  check_cancel_stop_remote(sess);
  return 0;
}

static int remote_transfer_progress_cb(const git_transfer_progress *stats,
                                       void *data)
{
  svn_ra_git__session_baton_t *sess = data;

  SVN_DBG(("objects: %u total %u indexed %u received %u local, "
           "deltas: %u total %u indexed, %ld bytes received\n", 
           stats->total_objects,
           stats->indexed_objects,
           stats->received_objects,
           stats->local_objects,
           stats->total_deltas,
           stats->indexed_deltas,
           (long)stats->received_bytes));

  check_cancel_stop_remote(sess);
  return 0;
}

static int remote_update_tips_cb(const char *refname,
                                 const git_oid *a,
                                 const git_oid *b,
                                 void *data)
{
  svn_ra_git__session_baton_t *sess = data;

  SVN_DBG(("update %s\n", refname));

  check_cancel_stop_remote(sess);
  return 0;
}

static svn_error_t *
do_libgit_init(void *baton, apr_pool_t *pool)
{
  git_threads_init();
  return SVN_NO_ERROR;
}

/* Return the last-changed revision of the repos-root-relative
 * PATH@PEGREV in *LAST_CHANGED. */
svn_error_t *
svn_ra_git__find_last_changed(svn_revnum_t *last_changed,
                              apr_hash_t *revmap,
                              const char *path,
                              svn_revnum_t pegrev,
                              git_repository *repos,
                              apr_pool_t *pool)
{
  int git_err;
  const git_oid *oid;
  git_oid last_oid;
  git_commit *commit;
  git_tree *tree;
  git_tree_entry *entry;
  svn_revnum_t rev;

  oid = apr_hash_get(revmap, &pegrev, sizeof(pegrev));
  if (oid == NULL)
    return svn_error_create(SVN_ERR_FS_NO_SUCH_REVISION, NULL, NULL);

  /* PATH has already been made relative to repos root by caller. */
  if (path[0] == '\0')
    {
      /* The root directory of the repository was last changed in HEAD. */
      *last_changed = apr_hash_count(revmap);
      return SVN_NO_ERROR;
    }

  git_err = git_commit_lookup(&commit, repos, oid);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());
  git_err = git_commit_tree(&tree, commit);
  if (git_err)
    {
      git_commit_free(commit);
      return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  git_err = git_tree_entry_bypath(&entry, tree, path);
  if (git_err)
    {
      git_tree_free(tree);
      git_commit_free(commit);

      if (git_err == GIT_ENOTFOUND)
        return svn_error_createf(SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
                                 _("No entry for %s@%lu\n"), path, pegrev);

      return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  git_oid_cpy(&last_oid, git_tree_entry_id(entry));
  rev = apr_hash_count(revmap);

  git_tree_free(tree);
  git_commit_free(commit);

  while (rev >= 2)
    {
      oid = apr_hash_get(revmap, &rev, sizeof(rev));
      if (oid == NULL)
        return svn_error_create(SVN_ERR_FS_NO_SUCH_REVISION, NULL, NULL);
      git_err = git_commit_lookup(&commit, repos, oid);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());
      git_err = git_commit_tree(&tree, commit);
      if (git_err)
        {
          git_commit_free(commit);
          return svn_error_trace(svn_ra_git__wrap_git_error());
        }
      git_err = git_tree_entry_bypath(&entry, tree, path);
      if (git_err)
        {
          git_tree_free(tree);
          git_commit_free(commit);

          if (git_err == GIT_ENOTFOUND)
            {
              *last_changed = rev;
              return SVN_NO_ERROR;
            }

          return svn_error_trace(svn_ra_git__wrap_git_error());
        }

      git_tree_free(tree);
      git_commit_free(commit);

      oid = git_tree_entry_id(entry);
      if (git_oid_cmp(oid, &last_oid) != 0)
        {
          git_tree_entry_free(entry);
          break;
        }

      git_oid_cpy(&last_oid, git_tree_entry_id(entry));
      git_tree_entry_free(entry);
      rev--;
    }

  *last_changed = rev;

  return SVN_NO_ERROR;
}

static svn_error_t *
map_obj_to_dirent(svn_dirent_t **out,
                  apr_hash_t *revmap, const char *path, svn_revnum_t pegrev,
                  apr_uint32_t dirent_fields,
                  git_repository *repos, git_commit *commit, git_object *obj,
                  apr_pool_t *pool)
{
  svn_dirent_t *dirent = svn_dirent_create(pool);
  git_otype type = git_object_type(obj);
  svn_revnum_t last_changed_rev = SVN_INVALID_REVNUM;
  git_commit *last_changed_commit = NULL;

  if (dirent_fields & (SVN_DIRENT_CREATED_REV | SVN_DIRENT_TIME | SVN_DIRENT_LAST_AUTHOR))
    {
      SVN_ERR(svn_ra_git__find_last_changed(&last_changed_rev, revmap, path,
                                            pegrev, repos, pool));

      if (dirent_fields & (SVN_DIRENT_TIME | SVN_DIRENT_LAST_AUTHOR))
        {
          git_oid *oid;
          int git_err;

          oid = apr_hash_get(revmap, &last_changed_rev, sizeof(last_changed_rev));
          if (oid == NULL)
            return svn_error_create(SVN_ERR_FS_NO_SUCH_REVISION, NULL, NULL);

          git_err = git_commit_lookup(&last_changed_commit, repos, oid);
          if (git_err)
            return svn_error_trace(svn_ra_git__wrap_git_error());
       } 
    }

  if (dirent_fields & SVN_DIRENT_KIND)
    {
      if (type == GIT_OBJ_TREE)
        dirent->kind = svn_node_dir;
      else if (type == GIT_OBJ_BLOB)
        dirent->kind = svn_node_file;
      else
        dirent->kind = svn_node_none;
    }

  if (dirent_fields & SVN_DIRENT_SIZE)
    {
      if (type == GIT_OBJ_BLOB)
        dirent->size = git_blob_rawsize((git_blob *)obj);
      else
        dirent->size = 0;
    }

  if (dirent_fields & SVN_DIRENT_HAS_PROPS)
    dirent->has_props = FALSE; /* ### TODO map svn: properties */

  if (dirent_fields & SVN_DIRENT_CREATED_REV)
    dirent->created_rev = last_changed_rev;

  if (dirent_fields & SVN_DIRENT_TIME)
    dirent->time = git_commit_time(last_changed_commit) * 1000000;

  if (dirent_fields & SVN_DIRENT_LAST_AUTHOR)
    dirent->last_author = apr_pstrdup(pool, git_commit_author(last_changed_commit)->email);

  *out = dirent;
  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------*/

/*** The RA vtable routines ***/

#define RA_GIT_DESCRIPTION \
        N_("Module for accessing a git repository.")

static const char *
svn_ra_git__get_description(apr_pool_t *pool)
{
  return _(RA_GIT_DESCRIPTION);
}

static const char * const *
svn_ra_git__get_schemes(apr_pool_t *pool)
{
  /* TODO: git+ssh requires optional libssh dependency -- do we want that as well? */
  static const char *schemes[] = { "git", "git+file", "git+http", "git+https", NULL };

  return schemes;
}

#define USER_AGENT "SVN/" SVN_VER_NUMBER " (" SVN_BUILD_TARGET ")" \
                   " ra_git"

static svn_error_t *
svn_ra_git__open(svn_ra_session_t *session,
                 const char **corrected_url,
                 const char *repos_url,
                 const svn_ra_callbacks2_t *callbacks,
                 void *callback_baton,
                 apr_hash_t *config,
                 apr_pool_t *pool)
{
  const char *client_string;
  svn_ra_git__session_baton_t *sess;
  static volatile svn_atomic_t libgit_initialized = 0;
  int git_err;
  git_remote_callbacks *remote_callbacks;


  /* We don't support redirections in ra-git. */
  if (corrected_url)
    *corrected_url = NULL;

  /* Allocate and stash the session_sess args we have already. */
  sess = apr_pcalloc(session->pool, sizeof(*sess));
  sess->callbacks = callbacks;
  sess->callback_baton = callback_baton;

  /* Root the session at the root directory. */
  sess->fs_path = svn_stringbuf_create_empty(session->pool);

  /* Fake up the repository UUID. */
   sess->uuid = RA_GIT_UUID;

  /* Be sure username is NULL so we know to look it up / ask for it */
  sess->username = NULL;

  if (sess->callbacks->get_client_string != NULL)
    SVN_ERR(sess->callbacks->get_client_string(sess->callback_baton,
                                               &client_string, session->pool));
  else
    client_string = NULL;

  if (client_string)
    sess->useragent = apr_pstrcat(session->pool, USER_AGENT " ",
                                  client_string, SVN_VA_NULL);
  else
    sess->useragent = USER_AGENT;

  sess->revmap = apr_hash_make(session->pool);
  sess->fetch_done = FALSE;
  sess->scratch_pool = svn_pool_create(session->pool);

  sess->session_url = apr_pstrdup(pool, repos_url);
  session->priv = sess;

  /* Store the git repository within the working copy's admin area,
   * if availble. Otherwise, create a temporary repository. */
  if (sess->callbacks->get_wc_adm_subdir != NULL)
    {
      SVN_ERR(sess->callbacks->get_wc_adm_subdir(sess->callback_baton,
                                                 &sess->repos_abspath,
                                                 "git",
                                                 pool, pool));
    }
  else
    {
      /* Use a temporary git repository. */
      /* ### small race here, should be using mkdtemp() or similar */
      SVN_ERR(svn_io_open_unique_file3(NULL, &sess->repos_abspath, NULL,
                                       svn_io_file_del_none,
                                       session->pool, pool));
      SVN_ERR(svn_io_remove_file2(sess->repos_abspath, TRUE, pool));

      /* Git repository is removed when the session pool gets destroyed. */
      apr_pool_cleanup_register(session->pool, session, cleanup_temporary_repos,
                                apr_pool_cleanup_null);
    }

  SVN_ERR(svn_atomic__init_once(&libgit_initialized, do_libgit_init, NULL,
                                NULL));

  SVN_DBG(("creating git repos in '%s'\n", sess->repos_abspath));

  /* Init (or reinit) a bare git repository. */
  git_err = git_repository_init(&sess->repos, sess->repos_abspath,
                                TRUE /* is_bare */);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  /* Split the session URL into a git remote URL and, possibly, a path within
   * the repository (in sess->fs_path). */
  svn_pool_clear(sess->scratch_pool);
  SVN_ERR(split_url(&sess->remote_url, sess->fs_path, sess->repos,
                    sess->session_url, session->pool, sess->scratch_pool));

  /* Check if our remote already exists. */
  git_err = git_remote_load(&sess->remote, sess->repos,
                            RA_GIT_DEFAULT_REMOTE_NAME);
  if (git_err)
    {
      if (git_err == GIT_ENOTFOUND)
        {
          giterr_clear();
          sess->remote = NULL;
        }
      else
        return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  if (sess->remote == NULL)
    {
      git_err = git_remote_create_with_fetchspec(
                  &sess->remote, sess->repos, RA_GIT_DEFAULT_REMOTE_NAME,
                  sess->remote_url, RA_GIT_DEFAULT_REFSPEC);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  remote_callbacks = apr_pcalloc(session->pool, sizeof(*remote_callbacks));
  remote_callbacks->version = GIT_REMOTE_CALLBACKS_VERSION;
  remote_callbacks->progress = remote_progress_cb;
  remote_callbacks->transfer_progress = remote_transfer_progress_cb;
  remote_callbacks->update_tips = remote_update_tips_cb;
  remote_callbacks->payload = sess;
  git_remote_set_callbacks(sess->remote, remote_callbacks);

  git_err = git_revwalk_new(&sess->revwalk, sess->repos);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__dup_session(svn_ra_session_t *new_session,
                        svn_ra_session_t *session,
                        const char *new_session_url,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_ra_git__session_baton_t *old_sess = session->priv;
  svn_ra_git__session_baton_t *new_sess;

  /* Allocate and stash the session_sess args we have already. */
  new_sess = apr_pcalloc(result_pool, sizeof(*new_sess));
  new_sess->callbacks = old_sess->callbacks;
  new_sess->callback_baton = old_sess->callback_baton;

  /* ### Make a deep copy of these? */
  new_sess->repos = old_sess->repos;
  new_sess->remote = old_sess->remote;
  new_sess->revwalk = old_sess->revwalk;
  new_sess->revmap = old_sess->revmap;

  new_sess->fetch_done = old_sess->fetch_done;
  new_sess->session_url = apr_pstrdup(result_pool, old_sess->session_url);
  new_sess->remote_url = apr_pstrdup(result_pool, old_sess->remote_url);
  new_sess->fs_path = svn_stringbuf_dup(old_sess->fs_path, result_pool);

  /* Cache the repository UUID as well */
  new_sess->uuid = apr_pstrdup(result_pool, old_sess->uuid);

  new_sess->username = old_sess->username
                            ? apr_pstrdup(result_pool, old_sess->username)
                            : NULL;

  new_sess->useragent = apr_pstrdup(result_pool, old_sess->useragent);
  new_session->priv = new_sess;

  new_sess->scratch_pool = old_sess->scratch_pool;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__reparent(svn_ra_session_t *session,
                     const char *url,
                     apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  const char *relpath = svn_uri_skip_ancestor(sess->remote_url,
                                              make_git_url(url), pool);

  /* If the new URL isn't the same as our repository root URL, then
     let's ensure that it's some child of it. */
  if (! relpath)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
       _("URL '%s' is not a child of the session's repository root "
         "URL '%s'"), url, sess->session_url);

  if (strcmp(sess->session_url, url) != 0)
    {
      svn_stringbuf_set(sess->fs_path, svn_relpath_canonicalize(relpath, pool));
      sess->session_url = apr_pstrdup(pool, url);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__get_session_url(svn_ra_session_t *session,
                            const char **url,
                            apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  *url = apr_pstrdup(pool, sess->session_url);
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__get_latest_revnum(svn_ra_session_t *session,
                              svn_revnum_t *latest_revnum,
                              apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;

  do_git_fetch(sess);
  fill_revmap(sess->revwalk, sess->repos, sess->revmap, pool);
  *latest_revnum = apr_hash_count(sess->revmap);
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__get_file_revs(svn_ra_session_t *session,
                          const char *path,
                          svn_revnum_t start,
                          svn_revnum_t end,
                          svn_boolean_t include_merged_revisions,
                          svn_file_rev_handler_t handler,
                          void *handler_baton,
                          apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}

static svn_error_t *
svn_ra_git__get_dated_revision(svn_ra_session_t *session,
                               svn_revnum_t *revision,
                               apr_time_t tm,
                               apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_git__change_rev_prop(svn_ra_session_t *session,
                            svn_revnum_t rev,
                            const char *name,
                            const svn_string_t *const *old_value_p,
                            const svn_string_t *value,
                            apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}

static svn_error_t *
svn_ra_git__get_uuid(svn_ra_session_t *session,
                     const char **uuid,
                     apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  *uuid = sess->uuid;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__get_repos_root(svn_ra_session_t *session,
                           const char **url,
                           apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  *url = svn_uri_get_longest_ancestor(make_svn_url(sess->remote_url, pool),
                                      sess->session_url, pool);
  return SVN_NO_ERROR;
}

apr_hash_t *
svn_ra_git__make_revprops_hash(git_commit *commit, apr_pool_t *pool)
{
  apr_hash_t *props = apr_hash_make(pool);
  svn_hash_sets(props, SVN_PROP_REVISION_LOG,
                svn_string_create(git_commit_message(commit), pool));
  svn_hash_sets(props, SVN_PROP_REVISION_AUTHOR,
                svn_string_create(git_commit_author(commit)->email, pool));
  svn_hash_sets(props, SVN_PROP_REVISION_DATE,
                svn_string_create(
                  svn_time_to_cstring(git_commit_time(commit) * 1000000, pool),
                  pool));
  return props;
}

static svn_error_t *
svn_ra_git__rev_proplist(svn_ra_session_t *session,
                         svn_revnum_t rev,
                         apr_hash_t **props,
                         apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  int git_err;
  git_oid *oid;
  git_commit *commit;

  do_git_fetch(sess);
  fill_revmap(sess->revwalk, sess->repos, sess->revmap, pool);

  oid = apr_hash_get(sess->revmap, &rev, sizeof(rev));
  if (oid == NULL)
    return svn_error_create(SVN_ERR_FS_NO_SUCH_REVISION, NULL, NULL);

  git_err = git_commit_lookup(&commit, sess->repos, oid);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  *props = svn_ra_git__make_revprops_hash(commit, pool);
  git_commit_free(commit);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__rev_prop(svn_ra_session_t *session,
                     svn_revnum_t rev,
                     const char *name,
                     svn_string_t **value,
                     apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}

static svn_error_t *
svn_ra_git__get_commit_editor(svn_ra_session_t *session,
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
svn_ra_git__get_mergeinfo(svn_ra_session_t *session,
                          svn_mergeinfo_catalog_t *catalog,
                          const apr_array_header_t *paths,
                          svn_revnum_t revision,
                          svn_mergeinfo_inheritance_t inherit,
                          svn_boolean_t include_descendants,
                          apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_git__do_update(svn_ra_session_t *session,
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
  svn_ra_git__session_baton_t *sess = session->priv;

  do_git_fetch(sess);
  fill_revmap(sess->revwalk, sess->repos, sess->revmap, scratch_pool);
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       NULL,
                       TRUE,
                       depth,
                       send_copyfrom_args,
                       ignore_ancestry,
                       update_editor,
                       update_baton,
                       result_pool, scratch_pool);
}


static svn_error_t *
svn_ra_git__do_switch(svn_ra_session_t *session,
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
  svn_ra_git__session_baton_t *sess = session->priv;

  do_git_fetch(sess);
  fill_revmap(sess->revwalk, sess->repos, sess->revmap, scratch_pool);
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       switch_url,
                       TRUE /* text_deltas */,
                       depth,
                       send_copyfrom_args,
                       ignore_ancestry,
                       update_editor,
                       update_baton,
                       result_pool, scratch_pool);
}


static svn_error_t *
svn_ra_git__do_status(svn_ra_session_t *session,
                        const svn_ra_reporter3_t **reporter,
                        void **report_baton,
                        const char *status_target,
                        svn_revnum_t revision,
                        svn_depth_t depth,
                        const svn_delta_editor_t *status_editor,
                        void *status_baton,
                        apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;

  do_git_fetch(sess);
  fill_revmap(sess->revwalk, sess->repos, sess->revmap, pool);
  return make_reporter(session,
                       reporter,
                       report_baton,
                       revision,
                       status_target,
                       NULL,
                       FALSE,
                       depth,
                       FALSE,
                       FALSE,
                       status_editor,
                       status_baton,
                       pool, pool);
}


static svn_error_t *
svn_ra_git__do_diff(svn_ra_session_t *session,
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
  svn_ra_git__session_baton_t *sess = session->priv;

  do_git_fetch(sess);
  fill_revmap(sess->revwalk, sess->repos, sess->revmap, pool);
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       switch_url,
                       text_deltas,
                       depth,
                       FALSE,
                       ignore_ancestry,
                       update_editor,
                       update_baton,
                       pool, pool);
}


struct walk_added_tree_baton {
  apr_hash_t *changed_paths;
  const char *root_relpath;
  apr_pool_t *pool;
} walk_added_tree_baton;

/* Implements git_treewalk_cb */
static int
walk_added_tree_cb(const char *root,
                   const git_tree_entry *entry,
                   void *payload)
{
  struct walk_added_tree_baton *b = payload;
  svn_log_changed_path2_t *changed_path;
  const char *entry_relpath;

  changed_path = svn_log_changed_path2_create(b->pool);
  changed_path->action = 'A';
  root = svn_relpath_canonicalize(root, b->pool);
  entry_relpath = svn_relpath_join(b->root_relpath,
                                   svn_relpath_canonicalize(root, b->pool),
                                   b->pool);
  entry_relpath = svn_relpath_join(entry_relpath, git_tree_entry_name(entry),
                                   b->pool);
  svn_hash_sets(b->changed_paths, entry_relpath, changed_path);

  return 0;
}

static svn_error_t *
walk_added_tree(apr_hash_t *changed_paths,
                const char *root_relpath,
                git_tree *tree,
                apr_pool_t *pool)
{
  int git_err;
  struct walk_added_tree_baton b;

  b.changed_paths = changed_paths;
  b.root_relpath = root_relpath;
  b.pool = pool;

  /* Walk tree entries to compare children. */
  git_err = git_tree_walk(tree, GIT_TREEWALK_PRE,
                          walk_added_tree_cb, &b);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  return SVN_NO_ERROR;
}


static svn_error_t *
compare_git_tree_entries(apr_hash_t *changed_paths,
                         git_repository *repos,
                         git_tree *tree,
                         git_tree *other_tree,
                         const char *tree_relpath,
                         apr_pool_t *pool)
{
  svn_log_changed_path2_t *changed_path;
  apr_hash_t *other_entries;
  int git_err;
  int i;

  /* Get the other tree's entries so we can compare entries of
   * both tree objects. */
  other_entries = apr_hash_make(pool);
  for (i = 0; i < git_tree_entrycount(other_tree); i++)
    {
      const git_tree_entry *e;

      /* Remember the entry's name and its oid. */
      e = git_tree_entry_byindex(other_tree, i);
      svn_hash_sets(other_entries, git_tree_entry_name(e),
                    git_tree_entry_id(e));
    }

  /* Compare the trees' entries, pruning the other entries list
   * of entries which exist in both trees or don't exist in the
   * other tree. */
  for (i = 0; i < git_tree_entrycount(tree); i++)
    {
      const git_tree_entry *e;
      const git_oid *oid;
      const git_oid *other_oid;

      e = git_tree_entry_byindex(tree, i);
      oid = git_tree_entry_id(e);
      other_oid = svn_hash_gets(other_entries, git_tree_entry_name(e));

      if (other_oid == NULL)
        {
          /* This entry was deleted in the other tree.
           * Mark it as deleted. */
          changed_path = svn_log_changed_path2_create(pool);
          changed_path->action = 'D';
          if (git_tree_entry_type(e) == GIT_OBJ_BLOB)
            changed_path->node_kind = svn_node_file;
          else if (git_tree_entry_type(e) == GIT_OBJ_TREE)
            changed_path->node_kind = svn_node_dir;
          else
            changed_path->node_kind = svn_node_unknown;
          svn_hash_sets(changed_paths,
                        svn_relpath_join(svn_relpath_canonicalize(
                                           tree_relpath, pool),
                                         git_tree_entry_name(e), pool),
                          changed_path);
        }
      else if (!git_oid_equal(oid, other_oid))
        {
          /* The entries differ.
           * If it's a blob, mark it as modified if the other entry is
           * also a blob, or mark it as replaced if the other entry is not
           * a blob. If it's a tree object we'll deal with it later instead,
           * while traversing it. */
          if (git_tree_entry_type(e) == GIT_OBJ_BLOB)
            {
              const git_tree_entry *other_entry;
              const char *entry_relpath;

              changed_path = svn_log_changed_path2_create(pool);
              other_entry = git_tree_entry_byoid(other_tree, other_oid);
              if (git_tree_entry_type(other_entry) == GIT_OBJ_BLOB)
                changed_path->action = 'M';
              else
                changed_path->action = 'R';
              entry_relpath = svn_relpath_join(svn_relpath_canonicalize(
                                                 tree_relpath, pool),
                                               git_tree_entry_name(e), pool),
              svn_hash_sets(changed_paths, entry_relpath, changed_path);

              if (changed_path->action == 'R' &&
                  git_tree_entry_type(other_entry) == GIT_OBJ_TREE)
                {
                  git_tree *added_tree;

                  git_err = git_tree_entry_to_object(
                              (git_object **)&added_tree, repos,
                              other_entry);
                  if (git_err)
                    return svn_error_trace(svn_ra_git__wrap_git_error());
                  SVN_ERR(walk_added_tree(changed_paths, entry_relpath, added_tree, pool));
                  git_tree_free(added_tree);
                }
            }
        }

      /* This other entry has been dealt with. */
      svn_hash_sets(other_entries, git_tree_entry_name(e), NULL);
    }

  /* Mark any remaining other entries as newly added. */
  if (apr_hash_count(other_entries))
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(pool, other_entries); hi;
           hi = apr_hash_next(hi))
        {
          const char *other_entry_name = svn__apr_hash_index_key(hi);
          const git_oid *other_entry_id = svn__apr_hash_index_val(hi);
          const git_tree_entry *other_entry;
          const char *other_entry_relpath;

          changed_path = svn_log_changed_path2_create(pool);
          changed_path->action = 'A';
          other_entry_relpath = svn_relpath_join(
                                  svn_relpath_canonicalize(tree_relpath,
                                                           pool),
                                  other_entry_name, pool),
          svn_hash_sets(changed_paths, other_entry_relpath, changed_path);

          other_entry = git_tree_entry_byoid(other_tree, other_entry_id);
          if (git_tree_entry_type(other_entry) == GIT_OBJ_TREE)
            {
              git_tree *added_tree;

              git_err = git_tree_entry_to_object(
                          (git_object **)&added_tree, repos, other_entry);
              if (git_err)
                return svn_error_trace(svn_ra_git__wrap_git_error());

              SVN_ERR(walk_added_tree(changed_paths, other_entry_relpath,
                                      added_tree, pool));
              git_tree_free(added_tree);
            }
        }
    }

  return SVN_NO_ERROR;
}

struct find_changed_paths_walk_baton {
  apr_hash_t *changed_paths;
  git_repository *repos;
  git_tree *other_tree;
  apr_pool_t *pool;
  svn_error_t *err;
} find_changed_paths_walk_baton;

/* Implements git_treewalk_cb */
static int
find_changed_paths_walk_cb(const char *root,
                           const git_tree_entry *entry,
                           void *payload)
{
  struct find_changed_paths_walk_baton *b = payload;
  int git_err;
  git_tree_entry *other_root_entry;
  git_tree_entry *other_entry;
  git_otype other_type;
  git_tree *tree;
  git_tree *other_tree;
  const char *entry_relpath;
  svn_log_changed_path2_t *changed_path;

  /* If this entry is not a tree object, we're not interested. */
  if (git_tree_entry_type(entry) != GIT_OBJ_TREE)
    return 0;

  /* If this entry's root doesn't exist in the other tree,
   * this entry was deleted along with the root. */
  git_err = git_tree_entry_bypath(&other_root_entry, b->other_tree, root);
  if (git_err)
    {
      if (git_err == GIT_ENOTFOUND)
        {
          giterr_clear();
          return 0;
        }

      b->err = svn_error_trace(svn_ra_git__wrap_git_error());
      return -1;
    }
  git_tree_entry_free(other_root_entry);

  /* Look up the corresponding entry in the other tree. */
  root = svn_relpath_canonicalize(root, b->pool);
  entry_relpath = svn_relpath_join(root, git_tree_entry_name(entry), b->pool);
  git_err = git_tree_entry_bypath(&other_entry, b->other_tree, entry_relpath);
  if (git_err)
    {
      if (git_err == GIT_ENOTFOUND)
        {
          /* The entry has been deleted in the other tree. */
          giterr_clear();
          changed_path = svn_log_changed_path2_create(b->pool);
          changed_path->action = 'D';
          svn_hash_sets(b->changed_paths, entry_relpath, changed_path);
          return 0;
        }

      b->err = svn_error_trace(svn_ra_git__wrap_git_error());
      return -1;
    }

  other_type = git_tree_entry_type(other_entry);
  if (other_type != GIT_OBJ_TREE)
    {
      /* The tree object has been replaced in the other tree
       * by an object of a different type, most likely a blob. */
      changed_path = svn_log_changed_path2_create(b->pool);
      changed_path->action = 'R';
      svn_hash_sets(b->changed_paths, entry_relpath, changed_path);
      return 0;
    }

  /* Fetch the entry's tree object... */
  git_err = git_tree_entry_to_object(((git_object **)&tree), b->repos, entry);
  if (git_err)
    {
      b->err = svn_error_trace(svn_ra_git__wrap_git_error());
      return -1;
    }

  /* .. and fetch the other entry's tree object .. */
  git_err = git_tree_entry_to_object(((git_object **)&other_tree), b->repos,
                                     other_entry);
  git_tree_entry_free(other_entry);
  if (git_err)
    {
      b->err = svn_error_trace(svn_ra_git__wrap_git_error());
      return -1;
    }

  /* .. and compare the entries of both trees. */
  b->err = svn_error_trace(compare_git_tree_entries(b->changed_paths,
                                                    b->repos, tree, other_tree,
                                                    entry_relpath, b->pool));
  if (b->err)
    return -1;
    
  return 0;
}

static svn_error_t *
find_changed_paths(apr_hash_t **changed_paths,
                   git_repository *repos,
                   git_tree *tree,
                   git_tree *other_tree,
                   apr_pool_t *pool)
{
  int git_err;
  struct find_changed_paths_walk_baton b;

  b.changed_paths = apr_hash_make(pool);
  b.repos = repos;
  b.other_tree = other_tree;
  b.pool = pool;
  b.err = SVN_NO_ERROR;

  if (tree == NULL)
    {
      SVN_ERR(walk_added_tree(b.changed_paths, "", other_tree, pool));
    }
  else
    {
      /* Compare the root entries. */
      SVN_ERR(compare_git_tree_entries(b.changed_paths, repos, tree, other_tree,
                                      "", pool));

      /* Walk tree entries to compare children. */
      git_err = git_tree_walk(tree, GIT_TREEWALK_PRE,
                              find_changed_paths_walk_cb, &b);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());
      if (b.err)
        return svn_error_trace(b.err);
    }

  *changed_paths = b.changed_paths;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__get_log(svn_ra_session_t *session,
                    const apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    int limit,
                    svn_boolean_t discover_changed_paths,
                    svn_boolean_t strict_node_history,
                    svn_boolean_t include_merged_revisions,
                    svn_move_behavior_t move_behavior,
                    const apr_array_header_t *revprops,
                    svn_log_entry_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  int git_err;
  git_commit *commit;
  git_tree *tree;
  svn_revnum_t revision;
  int step;
  apr_pool_t *iterpool;

  if (!SVN_IS_VALID_REVNUM(start))
    SVN_ERR(svn_ra_git__get_latest_revnum(session, &start, pool));
  if (!SVN_IS_VALID_REVNUM(end))
    SVN_ERR(svn_ra_git__get_latest_revnum(session, &end, pool));

  step = (start < end) ? 1 : -1;
  revision = start;
  if (step == 1)
    end++;
  if (start == 0 && revision != end)
    revision++;
  if (revision == end)
    end += step;
  iterpool = svn_pool_create(sess->scratch_pool);
  while (revision != end)
    {
      apr_hash_t *changed_paths;
      git_tree *parent_tree;

      svn_pool_clear(iterpool);

      SVN_ERR(fetch_revision_root(&tree, &commit, NULL, &revision, sess, "",
                                  revision, pool));
      if (git_commit_parentcount(commit) == 0)
        {
          /* First commit. All tree entries were added. */
          parent_tree = NULL;
        }
      else
        {
          git_commit *parent_commit;

          git_err = git_commit_parent(&parent_commit, commit, 0);
          if (git_err)
            return svn_error_trace(svn_ra_git__wrap_git_error());

          git_err = git_commit_tree(&parent_tree, parent_commit);
          if (git_err)
            {
              git_commit_free(parent_commit);
              return svn_error_trace(svn_ra_git__wrap_git_error());
            }

          git_commit_free(parent_commit);
        }

      SVN_ERR((find_changed_paths(&changed_paths, sess->repos,
                                  parent_tree, tree, iterpool)));
      if (parent_tree)
        git_tree_free(parent_tree);

      if (apr_hash_count(changed_paths) > 0)
        {
          svn_boolean_t show_log = FALSE;

          if (paths)
            {
              int i;

              /* Check if a desired path is among the changed paths. */
              for (i = 0; i < paths->nelts; i++)
                {
                  const char *path = APR_ARRAY_IDX(paths, i, const char *);

                  if (!svn_stringbuf_isempty(sess->fs_path))
                    path = svn_relpath_join(sess->fs_path->data, path,
                                            iterpool);
                  
                  show_log = (path[0] == '\0' ||
                              svn_hash_gets(changed_paths, path) != NULL);
                  if (show_log)
                    break;
                }
            }
          else
            show_log = TRUE;

          if (show_log)
            {
              svn_log_entry_t *log_entry = svn_log_entry_create(iterpool);

              if (discover_changed_paths)
                {
                  /* ### Some callers expect svn_fspath style keys...
                   * ### convert all keys. */
                  apr_hash_index_t *hi;

                  log_entry->changed_paths2 = apr_hash_make(iterpool);

                  for (hi = apr_hash_first(pool, changed_paths); hi;
                       hi = apr_hash_next(hi))
                    {
                      const char *relpath_key = svn__apr_hash_index_key(hi);
                      void *val = svn__apr_hash_index_val(hi);
                      const char *fspath_key;

                      fspath_key = apr_pstrcat(iterpool, "/", relpath_key,
                                               SVN_VA_NULL);
                      svn_hash_sets(log_entry->changed_paths2, fspath_key, val);
                    }

                  log_entry->changed_paths = log_entry->changed_paths2;
                }

              log_entry->revision = revision;

              if (revprops)
                {
                  apr_hash_t *revprops_hash = NULL;
                  int i;

                  if (revprops->nelts > 0)
                    revprops_hash = svn_ra_git__make_revprops_hash(commit,
                                                                   iterpool);

                  log_entry->revprops = apr_hash_make(pool);
                  for (i = 0; i < revprops->nelts; i++)
                    {
                      const char *revprop_name = APR_ARRAY_IDX(revprops, i,
                                                               const char *);
                      svn_string_t *val = svn_hash_gets(revprops_hash,
                                                        revprop_name);
                      if (val)
                        svn_hash_sets(log_entry->revprops, revprop_name, val);
                    }
                }
              else
                log_entry->revprops = svn_ra_git__make_revprops_hash(commit,
                                                                     iterpool);

              SVN_ERR(receiver(receiver_baton, log_entry, iterpool));

              if (limit > 0)
                if (--limit == 0)
                  break;
            }
        }

      revision += step;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_git__check_path(svn_node_kind_t *kind, git_tree *tree, const char *path)
{
  git_tree_entry *entry;
  int git_err;

  if (path[0] == '\0')
    {
      /* The root directory of the repository. */
      *kind = svn_node_dir;
      return SVN_NO_ERROR;
    }

  git_err = git_tree_entry_bypath(&entry, tree, path);
  if (git_err)
    {
      git_tree_free(tree);

      if (git_err == GIT_ENOTFOUND)
        {
          *kind = svn_node_none;
          return SVN_NO_ERROR;
        }

      return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  if (git_tree_entry_filemode(entry) == GIT_FILEMODE_COMMIT)
    *kind = svn_node_none; /* ### submodule, map to external */
  else
    {
      git_otype type = git_tree_entry_type(entry);

      if (type == GIT_OBJ_TREE) 
        *kind = svn_node_dir;
      else if (type == GIT_OBJ_BLOB)
        *kind = svn_node_file;
      else
        *kind = svn_node_unknown;
    }

  git_tree_entry_free(entry);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__do_check_path(svn_ra_session_t *session,
                          const char *path,
                          svn_revnum_t revision,
                          svn_node_kind_t *kind,
                          apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  git_commit *commit;
  git_tree *tree;

  SVN_ERR(fetch_revision_root(&tree, &commit, &path, &revision,
                              sess, path, revision, pool));

  SVN_ERR(svn_ra_git__check_path(kind, tree, path));

  git_tree_free(tree);
  git_commit_free(commit);
  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_git__stat(svn_ra_session_t *session,
                 const char *path,
                 svn_revnum_t revision,
                 svn_dirent_t **dirent,
                 apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  int git_err;
  git_commit *commit;
  git_tree *tree;
  git_otype type;

  SVN_ERR(fetch_revision_root(&tree, &commit, &path, &revision,
                              sess, path, revision, pool));

  if (path[0] == '\0')
    {
      /* The root directory of the repository. */
      type = GIT_OBJ_TREE;
      SVN_ERR(map_obj_to_dirent(dirent, sess->revmap, path, revision, SVN_DIRENT_ALL,
                                sess->repos, commit, (git_object *)tree, pool));
    }
  else
    {
      git_tree_entry *entry;

      git_err = git_tree_entry_bypath(&entry, tree, path);
      if (git_err)
        {
          git_tree_free(tree);
          git_commit_free(commit);

          if (git_err == GIT_ENOTFOUND)
            {
              *dirent = NULL;
              return SVN_NO_ERROR;
            }

          return svn_error_trace(svn_ra_git__wrap_git_error());
        }

      type = git_tree_entry_type(entry);
      if (type == GIT_OBJ_TREE || type == GIT_OBJ_BLOB)
        {
          git_object *object;

          git_err = git_object_lookup(&object, sess->repos, git_tree_entry_id(entry), type);
          if (git_err)
            {
              git_tree_entry_free(entry);
              git_tree_free(tree);
              git_commit_free(commit);

              return svn_error_trace(svn_ra_git__wrap_git_error());
            }

          SVN_ERR(map_obj_to_dirent(dirent, sess->revmap, path, revision, SVN_DIRENT_ALL,
                                    sess->repos, commit, object, pool));
          git_object_free(object);
        }
      else
        {
          git_tree_entry_free(entry);
          return svn_error_trace(svn_error_create(SVN_ERR_FS_NO_SUCH_ENTRY, NULL, NULL));
        }

      git_tree_entry_free(entry);
    }

  git_tree_free(tree);
  git_commit_free(commit);

  return SVN_NO_ERROR;
}



/* Obtain the properties for a node, including its 'entry props */
static svn_error_t *
get_node_props(apr_hash_t **props,
               svn_fs_root_t *root,
               const char *path,
               const char *uuid,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  /* We have no 'wcprops' in ra_git, but might someday. */
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


/* Getting just one file. */
static svn_error_t *
svn_ra_git__get_file(svn_ra_session_t *session,
                     const char *path,
                     svn_revnum_t revision,
                     svn_stream_t *stream,
                     svn_revnum_t *fetched_rev,
                     apr_hash_t **props,
                     apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  int git_err;
  git_commit *commit;
  git_tree *tree;
  git_tree_entry *entry;
  git_otype type;
  git_blob *blob;

  SVN_ERR(fetch_revision_root(&tree, &commit, &path, &revision,
                              sess, path, revision, pool));

  if (path[0] == '\0')
    {
      git_tree_free(tree);
      git_commit_free(commit);
      return svn_error_create(SVN_ERR_FS_NOT_FILE, NULL, NULL);
    }

  git_err = git_tree_entry_bypath(&entry, tree, path);
  if (git_err)
    {
      git_tree_free(tree);
      git_commit_free(commit);

      if (git_err == GIT_ENOTFOUND)
        return svn_error_create(SVN_ERR_FS_NO_SUCH_ENTRY, NULL, NULL);

      return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  type = git_tree_entry_type(entry);
  if (type != GIT_OBJ_BLOB)
    {
      git_tree_entry_free(entry);
      git_tree_free(tree);
      git_commit_free(commit);
      return svn_error_create(SVN_ERR_FS_NOT_FILE, NULL, NULL);
    }

  if (stream)
    {
      apr_size_t total_size;
      const char *data;
      apr_size_t bytes_copied;

      git_err = git_blob_lookup(&blob, sess->repos, git_tree_entry_id(entry));
      if (git_err)
        {
          git_tree_free(tree);
          git_commit_free(commit);
          return svn_error_trace(svn_ra_git__wrap_git_error());
        }

      total_size = git_blob_rawsize(blob);
      data = git_blob_rawcontent(blob);
      bytes_copied = 0;

      while (bytes_copied < total_size)
        {
          apr_size_t chunk_size = 1024;
          apr_size_t len;

          if (total_size - bytes_copied < chunk_size)
            chunk_size = total_size - bytes_copied;

          len = chunk_size;
          SVN_ERR(svn_stream_write(stream, data, &len));
          if (len != chunk_size)
            {
              git_tree_entry_free(entry);
              git_tree_free(tree);
              git_commit_free(commit);
              return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL, NULL);
            }

          data += chunk_size;
          bytes_copied += chunk_size;
        }
    }

  if (fetched_rev)
    *fetched_rev = revision;

  if (props)
    *props = apr_hash_make(pool);

  git_tree_entry_free(entry);
  git_tree_free(tree);
  git_commit_free(commit);

  return SVN_NO_ERROR;
}

/* Getting a directory's entries */
static svn_error_t *
svn_ra_git__get_dir(svn_ra_session_t *session,
                    apr_hash_t **dirents,
                    svn_revnum_t *fetched_rev,
                    apr_hash_t **props,
                    const char *path,
                    svn_revnum_t revision,
                    apr_uint32_t dirent_fields,
                    apr_pool_t *pool)
{
  svn_ra_git__session_baton_t *sess = session->priv;
  int git_err;
  git_commit *commit;
  git_tree *tree;

  SVN_ERR(fetch_revision_root(&tree, &commit, &path, &revision,
                              sess, path, revision, pool));

  if (path[0] != '\0')
    {
      git_tree_entry *entry;
      git_otype type;
      git_object *subtree;

      git_err = git_tree_entry_bypath(&entry, tree, path);
      if (git_err)
        {
          git_tree_free(tree);
          git_commit_free(commit);

          if (git_err == GIT_ENOTFOUND)
            return svn_error_create(SVN_ERR_FS_NO_SUCH_ENTRY, NULL, NULL);

          return svn_error_trace(svn_ra_git__wrap_git_error());
        }

      /* ### Ignore git submodules for now.
       * ### Eventually we'll map them to svn:externals. */ 
      if (git_tree_entry_filemode(entry) == GIT_FILEMODE_COMMIT)
        {
          git_tree_entry_free(entry);
          return svn_error_createf(SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
                                   _("'%s' is a git submodule but submodules are not "
                                     "yet supported"), path);
        }

      type = git_tree_entry_type(entry);
      if (type != GIT_OBJ_TREE)
        {
          git_tree_entry_free(entry);
          git_tree_free(tree);
          git_commit_free(commit);
          return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL, NULL);
        }

      git_err = git_tree_entry_to_object(&subtree, sess->repos, entry);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());

      git_tree_free(tree);
      tree = (git_tree *)subtree;
      git_tree_entry_free(entry);
    }

  if (dirents)
    {
      apr_size_t idx;
      apr_pool_t *iterpool;
      
      *dirents = apr_hash_make(pool);

      iterpool = svn_pool_create(sess->scratch_pool);
      for (idx = 0; idx < git_tree_entrycount(tree); idx++)
        {
          const git_tree_entry *entry;
          git_object *obj;
          svn_dirent_t *dirent;

          svn_pool_clear(iterpool);

          entry = git_tree_entry_byindex(tree, idx);
          SVN_ERR_ASSERT(entry);

          /* Ignore git submodules for now. Eventually we'll map them to svn:externals. */ 
          if (git_tree_entry_filemode(entry) == GIT_FILEMODE_COMMIT)
            continue;

          git_err = git_tree_entry_to_object(&obj, sess->repos, entry);
          if (git_err)
            {
              git_tree_free(tree);
              git_commit_free(commit);
              return svn_error_trace(svn_ra_git__wrap_git_error());
            }

          SVN_ERR(map_obj_to_dirent(&dirent, sess->revmap,
                                    svn_relpath_join(path,
                                                     git_tree_entry_name(entry),
                                                     iterpool),
                                    revision, dirent_fields, sess->repos, commit,
                                    obj, pool));
          svn_hash_sets(*dirents, apr_pstrdup(pool, git_tree_entry_name(entry)), dirent);
          git_object_free(obj);
        }
      svn_pool_destroy(iterpool);
    }

  if (fetched_rev)
    *fetched_rev = revision;

  if (props)
    *props = apr_hash_make(pool);

  git_tree_free(tree);
  git_commit_free(commit);

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_git__get_locations(svn_ra_session_t *session,
                          apr_hash_t **locations,
                          const char *path,
                          svn_revnum_t peg_revision,
                          const apr_array_header_t *location_revisions,
                          apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_git__get_location_segments(svn_ra_session_t *session,
                                  const char *path,
                                  svn_revnum_t peg_revision,
                                  svn_revnum_t start_rev,
                                  svn_revnum_t end_rev,
                                  svn_location_segment_receiver_t receiver,
                                  void *receiver_baton,
                                  apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}

static svn_error_t *
svn_ra_git__lock(svn_ra_session_t *session,
                 apr_hash_t *path_revs,
                 const char *comment,
                 svn_boolean_t force,
                 svn_ra_lock_callback_t lock_func,
                 void *lock_baton,
                 apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_git__unlock(svn_ra_session_t *session,
                   apr_hash_t *path_tokens,
                   svn_boolean_t force,
                   svn_ra_lock_callback_t lock_func,
                   void *lock_baton,
                   apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}



static svn_error_t *
svn_ra_git__get_lock(svn_ra_session_t *session,
                     svn_lock_t **lock,
                     const char *path,
                     apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}



static svn_error_t *
svn_ra_git__get_locks(svn_ra_session_t *session,
                      apr_hash_t **locks,
                      const char *path,
                      svn_depth_t depth,
                      apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_git__replay(svn_ra_session_t *session,
                   svn_revnum_t revision,
                   svn_revnum_t low_water_mark,
                   svn_boolean_t send_deltas,
                   const svn_delta_editor_t *editor,
                   void *edit_baton,
                   apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_git__replay_range(svn_ra_session_t *session,
                           svn_revnum_t start_revision,
                           svn_revnum_t end_revision,
                           svn_revnum_t low_water_mark,
                           svn_boolean_t send_deltas,
                           svn_ra_replay_revstart_callback_t revstart_func,
                           svn_ra_replay_revfinish_callback_t revfinish_func,
                           void *replay_baton,
                           apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_git__has_capability(svn_ra_session_t *session,
                             svn_boolean_t *has,
                             const char *capability,
                             apr_pool_t *pool)
{

  if (strcmp(capability, SVN_RA_CAPABILITY_LOG_REVPROPS) == 0)
    {
      *has = TRUE;
    }
  else if (strcmp(capability, SVN_RA_CAPABILITY_DEPTH) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_PARTIAL_REPLAY) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_COMMIT_REVPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_ATOMIC_REVPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_INHERITED_PROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_EPHEMERAL_TXNPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_GET_FILE_REVS_REVERSE) == 0
      )
    {
      /* ### These features are not yet implemented. */
      *has = FALSE;
    }
  else if (strcmp(capability, SVN_RA_CAPABILITY_MERGEINFO) == 0)
    {
      /* Mergeinfo is unsupported by this RA layer.
       * We can simply rely on git's native merge capabilities instead. */
      *has = FALSE;
    }
  else  /* Don't know any other capabilities, so error. */
    {
      return svn_error_createf
        (SVN_ERR_UNKNOWN_CAPABILITY, NULL,
         _("Don't know anything about capability '%s'"), capability);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_git__get_deleted_rev(svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t peg_revision,
                            svn_revnum_t end_revision,
                            svn_revnum_t *revision_deleted,
                            apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}

static svn_error_t *
svn_ra_git__get_inherited_props(svn_ra_session_t *session,
                                  apr_array_header_t **iprops,
                                  const char *path,
                                  svn_revnum_t revision,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}

static svn_error_t *
svn_ra_git__register_editor_shim_callbacks(svn_ra_session_t *session,
                                    svn_delta_shim_callbacks_t *callbacks)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_git__get_commit_ev2(svn_editor_t **editor,
                           svn_ra_session_t *session,
                           apr_hash_t *revprops,
                           svn_commit_callback2_t commit_cb,
                           void *commit_baton,
                           apr_hash_t *lock_tokens,
                           svn_boolean_t keep_locks,
                           svn_ra__provide_base_cb_t provide_base_cb,
                           svn_ra__provide_props_cb_t provide_props_cb,
                           svn_ra__get_copysrc_kind_cb_t get_copysrc_kind_cb,
                           void *cb_baton,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
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
  svn_ra_git__get_description,
  svn_ra_git__get_schemes,
  svn_ra_git__open,
  svn_ra_git__dup_session,
  svn_ra_git__reparent,
  svn_ra_git__get_session_url,
  svn_ra_git__get_latest_revnum,
  svn_ra_git__get_dated_revision,
  svn_ra_git__change_rev_prop,
  svn_ra_git__rev_proplist,
  svn_ra_git__rev_prop,
  svn_ra_git__get_commit_editor,
  svn_ra_git__get_file,
  svn_ra_git__get_dir,
  svn_ra_git__get_mergeinfo,
  svn_ra_git__do_update,
  svn_ra_git__do_switch,
  svn_ra_git__do_status,
  svn_ra_git__do_diff,
  svn_ra_git__get_log,
  svn_ra_git__do_check_path,
  svn_ra_git__stat,
  svn_ra_git__get_uuid,
  svn_ra_git__get_repos_root,
  svn_ra_git__get_locations,
  svn_ra_git__get_location_segments,
  svn_ra_git__get_file_revs,
  svn_ra_git__lock,
  svn_ra_git__unlock,
  svn_ra_git__get_lock,
  svn_ra_git__get_locks,
  svn_ra_git__replay,
  svn_ra_git__has_capability,
  svn_ra_git__replay_range,
  svn_ra_git__get_deleted_rev,
  svn_ra_git__register_editor_shim_callbacks,
  svn_ra_git__get_inherited_props,
  svn_ra_git__get_commit_ev2
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

