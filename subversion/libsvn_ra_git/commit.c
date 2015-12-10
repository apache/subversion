/*
 * commit.c : Handles some commit scenarios to a libsvn_fs_git backend.
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
#include "svn_dirent_uri.h"
#include "svn_ra.h"
#include "svn_repos.h"
#include "svn_fs.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_delta.h"

#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"
#include "private/svn_atomic.h"
#include "private/svn_fspath.h"

#include "ra_git.h"

typedef struct git_commit_edit_baton_t
{
  apr_pool_t *pool;
  svn_ra_session_t *session;
  svn_ra_git__session_t *sess;

  svn_repos_t *repos;
  svn_fs_root_t *root;
  svn_revnum_t created_rev;

  git_repository *repository;
  const git_commit *commit;

  svn_boolean_t aborted, done;

  svn_commit_callback2_t commit_cb;
  void *commit_baton;

  apr_hash_t *revprops;

  svn_boolean_t tag_mode;
  svn_boolean_t change_mode;

  const char *root_path;

  svn_boolean_t tree_written;
  git_oid tree_oid;

} git_commit_edit_baton_t;

typedef struct git_commit_node_baton_t
{
  struct git_commit_node_baton_t *pb;
  git_commit_edit_baton_t *eb;

  apr_pool_t *pool;

  svn_fs_root_t *root;
  const char *node_path;

  git_treebuilder *dir_builder;
  svn_boolean_t added;

} git_commit_node_baton_t;

static svn_error_t *
setup_change_trees(git_commit_node_baton_t *db,
                   apr_pool_t *scratch_pool)
{
  if (db->dir_builder)
    return SVN_NO_ERROR;

  if (db->pb && !db->pb->dir_builder)
    SVN_ERR(setup_change_trees(db->pb, scratch_pool));

  if (db->pb && db->pb->dir_builder)
    {
      const git_tree_entry *entry;
      const git_tree *tree = NULL;
      entry = git_treebuilder_get(db->dir_builder,
                                  svn_relpath_basename(db->node_path, NULL));

      if (entry)
        {
          const git_object *obj;

          SVN_ERR(svn_git__tree_entry_to_object(&obj, db->eb->repository,
                                                entry, db->pool));

          tree = (const git_tree*)obj;
        }

      SVN_ERR(svn_git__treebuilder_new(&db->dir_builder, db->eb->repository, tree,
                                       db->pool));
      return SVN_NO_ERROR;
    }

  if (!db->node_path[0])
    return SVN_NO_ERROR; /* Creating 'trunk' */

  if (strcmp(db->eb->root_path, db->node_path))
    {
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ensure_mutable(git_commit_node_baton_t *nb,
               const char *path,
               svn_revnum_t base_rev,
               apr_pool_t *scratch_pool)
{
  git_commit_edit_baton_t *eb = nb->eb;

  if (!path)
    path = nb->node_path;
  else
    path = svn_relpath_join(nb->node_path,
                            svn_relpath_basename(path, NULL),
                            scratch_pool);

  if (!(eb->tag_mode || eb->change_mode))
    {
      if (svn_relpath_skip_ancestor("trunk", path))
        {
          eb->change_mode = TRUE;
          eb->root_path = "trunk";
        }
      else if (svn_relpath_skip_ancestor("branches", path))
        {
          if (strlen(path) > 8)
            {
              eb->change_mode = TRUE;
              eb->root_path = svn_relpath_prefix(path, 2, eb->pool);
            }
          else
            {
              eb->tag_mode = TRUE;
              eb->root_path = "branches";
            }
        }
      else if (!strcmp("tags", path))
        {
          eb->tag_mode = TRUE;
          eb->root_path = "tags";
        }
      else
        return svn_error_createf(SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                                 _("Can't commit directly to '%s' "
                                   "in a git repository"),
                                 path);

      if (eb->change_mode)
        {
          svn_revnum_t youngest;
          svn_fs_t *fs;
          const git_tree *tree = NULL;

          SVN_ERR(svn_repos_open3(&eb->repos, eb->sess->local_repos_abspath, NULL,
                                  eb->pool, scratch_pool));

          fs = svn_repos_fs(eb->repos);
          SVN_ERR(svn_fs_youngest_rev(&youngest, fs, scratch_pool));

          SVN_ERR(svn_fs_revision_root(&eb->root, fs, youngest, scratch_pool));

          SVN_ERR(svn_fs_node_created_rev(&eb->created_rev, eb->root,
                                          eb->root_path, scratch_pool));

          SVN_ERR(svn_git__repository_open(&eb->repository,
                                           svn_dirent_join(
                                             eb->sess->local_repos_abspath,
                                             "db/git", scratch_pool),
                                           eb->pool));

          if (eb->created_rev > 0)
            {
              svn_string_t *oid_value;

              SVN_ERR(svn_fs_revision_prop2(&oid_value, fs, eb->created_rev,
                                            "svn:git-commit-id", FALSE,
                                            scratch_pool, scratch_pool));

              if (oid_value)
                {
                  git_oid oid;

                  GIT2_ERR(git_oid_fromstr(&oid, oid_value->data));

                  SVN_ERR(svn_git__commit_lookup(&eb->commit, eb->repository,
                                                 &oid, eb->pool));

                  if (eb->commit)
                    SVN_ERR(svn_git__commit_tree(&tree, eb->commit, eb->pool));
                }
            }
          else
            tree = NULL;

          SVN_ERR(setup_change_trees(nb, scratch_pool));
        }
    }

  if (eb->change_mode)
    {
      if (!svn_relpath_skip_ancestor(eb->root_path, path))
        return svn_error_createf(SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                                 _("Can't commit to '%s' and '%s' in one commit"),
                                 eb->root_path, path);
    }
  else
    {
      const char *rp = svn_relpath_skip_ancestor(eb->root_path, path);

      if (!rp || !*rp || strchr(rp, '/'))
        return svn_error_createf(SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                                 _("Can't tag to '%s'"), path);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
git_commit__open_root(void *edit_baton,
                      svn_revnum_t base_revision,
                      apr_pool_t *result_pool,
                      void **root_baton)
{
  git_commit_node_baton_t *nb = apr_pcalloc(result_pool, sizeof(*nb));
  nb->eb = edit_baton;
  nb->pool = result_pool;

  nb->node_path = svn_uri_skip_ancestor(nb->eb->sess->repos_root_url,
                                        nb->eb->sess->session_url_buf->data,
                                        result_pool);

  *root_baton = nb;

  return SVN_NO_ERROR;
}

static svn_error_t *
git_commit__delete_entry(const char *path,
                         svn_revnum_t revision,
                         void *parent_baton,
                         apr_pool_t *scratch_pool)
{
  git_commit_node_baton_t *pb = parent_baton;

  SVN_ERR(ensure_mutable(pb, path, revision, scratch_pool));

  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
git_commit__add_directory(const char *path,
                          void *parent_baton,
                          const char *copyfrom_path,
                          svn_revnum_t copyfrom_revision,
                          apr_pool_t *result_pool,
                          void **child_baton)
{
  git_commit_node_baton_t *pb = parent_baton;
  git_commit_edit_baton_t *eb = pb->eb;
  git_commit_node_baton_t *db;
  const char *relpath;

  SVN_ERR(ensure_mutable(pb, path, SVN_INVALID_REVNUM,
                         result_pool));

  if (!pb->eb->change_mode)
    return svn_error_create(APR_ENOTIMPL, NULL, NULL);

  db = apr_pcalloc(result_pool, sizeof(*db));
  db->pb = pb;
  db->eb = eb;
  db->pool = result_pool;
  db->node_path = svn_relpath_join(pb->node_path,
                                   svn_relpath_basename(path, NULL),
                                   result_pool);

  relpath = svn_relpath_skip_ancestor(eb->root_path, db->node_path);

  *child_baton = db;

  SVN_ERR(svn_git__treebuilder_new(&db->dir_builder, db->eb->repository, NULL,
                                   result_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
git_commit__open_directory(const char *path,
                           void *parent_baton,
                           svn_revnum_t base_revision,
                           apr_pool_t *result_pool,
                           void **child_baton)
{
  git_commit_node_baton_t *pb = parent_baton;

  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
git_commit__change_dir_prop(void *dir_baton,
                            const char *name,
                            const svn_string_t *value,
                            apr_pool_t *scratch_pool)
{
  git_commit_node_baton_t *db = dir_baton;

  SVN_ERR(ensure_mutable(db, NULL, SVN_INVALID_REVNUM,
                         scratch_pool));

  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
git_commit__close_directory(void *dir_baton,
                            apr_pool_t *scratch_pool)
{
  git_commit_node_baton_t *db = dir_baton;

  if (db->dir_builder)
    {
      git_oid oid;

      GIT2_ERR(git_treebuilder_write(&oid, db->dir_builder));

      if (db->pb && db->pb->dir_builder)
        GIT2_ERR(git_treebuilder_insert(NULL, db->pb->dir_builder,
                                        svn_relpath_basename(db->node_path,
                                                             NULL),
                                        &oid, GIT_FILEMODE_TREE));
      else if (!strcmp(db->node_path, db->eb->root_path))
        {
          db->eb->tree_oid = oid;
          db->eb->tree_written = TRUE;
        }
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
git_commit__add_file(const char *path,
                     void *parent_baton,
                     const char *copyfrom_path,
                     svn_revnum_t copyfrom_revision,
                     apr_pool_t *result_pool,
                     void **file_baton)
{
  git_commit_node_baton_t *pb = parent_baton;

  SVN_ERR(ensure_mutable(pb, path, SVN_INVALID_REVNUM,
                         result_pool));

  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
git_commit__open_file(const char *path,
                      void *parent_baton,
                      svn_revnum_t base_revision,
                      apr_pool_t *result_pool,
                      void **file_baton)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
git_commit__apply_textdelta(void *file_baton,
                            const char *base_checksum,
                            apr_pool_t *result_pool,
                            svn_txdelta_window_handler_t *handler,
                            void **handler_baton)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
git_commit__change_file_prop(void *file_baton,
                             const char *name,
                             const svn_string_t *value,
                             apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
git_commit__close_file(void *file_baton,
                       const char *text_checksum,
                       apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
git_commit__close_edit(void *edit_baton,
                       apr_pool_t *scratch_pool)
{
  git_commit_edit_baton_t *eb = edit_baton;

  if (eb->done || eb->aborted)
    return SVN_NO_ERROR;

  if (eb->tree_written)
    {
      git_oid commit_oid;
      git_signature *author;
      int git_err;
      svn_string_t *log_value;
      svn_error_t *err;
      const char *ref = svn_relpath_join("refs/tmp",
                                         svn_uuid_generate(scratch_pool),
                                         scratch_pool);
      const git_tree *tree;

      SVN_ERR(svn_git__tree_lookup(&tree, eb->repository, &eb->tree_oid,
                                   scratch_pool));

      git_err = git_signature_default(&author, eb->repository);
      if (git_err != GIT_ENOTFOUND)
        GIT2_ERR(git_err);
      else
        {
          /* ### TODO: Fetch something better */
          GIT2_ERR(git_signature_now(&author, "svn-dummy",
                                     "svn-dummy@subversion.tigris.org"));
        }

      log_value = svn_hash_gets(eb->revprops, SVN_PROP_REVISION_LOG);

      git_err = git_commit_create(&commit_oid, eb->repository, ref,
                                  author, author,
                                  "UTF-8",
                                  log_value ? log_value->data : "",
                                  tree,
                                  eb->commit ? 1 : 0,
                                  eb->commit ? &eb->commit : NULL);

      git_signature_free(author);
      GIT2_ERR(git_err);

      /* Ok, we now have a commit... Let's push it to the actual server.

         We can then fetch it back and return the revision of to the result */
      err = svn_error_trace(
              svn_ra_git__push_commit(eb->session,
                                      ref, eb->root_path, &commit_oid,
                                      eb->commit_cb, eb->commit_baton,
                                      scratch_pool));

      if (err)
        {
          git_reference_remove(eb->repository, ref);
          giterr_clear();
        }

      eb->done = TRUE;
      svn_pool_destroy(eb->pool);

      return err;
    }

  //if (eb->treebuilder)
  //  {
  //    git_oid oid;
  //
  //    GIT2_ERR(git_treebuilder_write(&oid, eb->treebuilder));
  //  }

  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
git_commit__abort_edit(void *edit_baton,
                       apr_pool_t *scratch_pool)
{
  git_commit_edit_baton_t *eb = edit_baton;
  eb->aborted = TRUE;

  if (!eb->done)
    {
      eb->done = TRUE;
      svn_pool_destroy(eb->pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_git__get_commit_editor(const svn_delta_editor_t **editor_p,
                              void **edit_baton_p,
                              svn_ra_session_t *session,
                              apr_hash_t *revprop_table,
                              svn_commit_callback2_t callback,
                              void *callback_baton,
                              apr_pool_t *pool)
{
  svn_ra_git__session_t *sess = session->priv;
  git_commit_edit_baton_t *eb = apr_pcalloc(pool, sizeof(*eb));
  svn_delta_editor_t *editor = svn_delta_default_editor(pool);

  eb->pool = svn_pool_create(pool);
  eb->revprops = revprop_table;
  eb->commit_cb = callback;
  eb->commit_baton = callback_baton;
  eb->session = session;
  eb->sess = sess;

  editor->open_root        = git_commit__open_root;
  editor->delete_entry     = git_commit__delete_entry;
  editor->add_directory    = git_commit__add_directory;
  editor->open_directory   = git_commit__open_directory;
  editor->change_dir_prop  = git_commit__change_dir_prop;
  editor->close_directory  = git_commit__close_directory;
  editor->add_file         = git_commit__add_file;
  editor->open_file        = git_commit__open_file;
  editor->change_file_prop = git_commit__change_file_prop;
  editor->apply_textdelta  = git_commit__apply_textdelta;
  editor->close_file       = git_commit__close_file;

  editor->close_edit       = git_commit__close_edit;
  editor->abort_edit       = git_commit__abort_edit;

  *editor_p = editor;
  *edit_baton_p = eb;
  return SVN_NO_ERROR;
}
