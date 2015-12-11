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

  apr_array_header_t *extra_builders;

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
  svn_boolean_t added;

  apr_pool_t *pool;

  svn_fs_root_t *root;
  const char *root_path;
  const char *node_path;

  git_treebuilder *dir_builder;

  const char *tmp_abspath;
  svn_checksum_t *result_checksum;
  svn_checksum_t *base_checksum;
  svn_checksum_t *expected_base_checks;

} git_commit_node_baton_t;

static svn_error_t *
setup_change_trees(git_commit_node_baton_t *db,
                   apr_pool_t *scratch_pool)
{
  git_commit_edit_baton_t *eb = db->eb;

  if (db->dir_builder)
    return SVN_NO_ERROR;

  if (!db->pb)
    {
      const char *relpath;

      relpath = svn_relpath_skip_ancestor(db->eb->root_path, db->node_path);

      if (relpath && *relpath)
        {
          const git_tree *tree;

          SVN_ERR(svn_git__commit_tree(&tree, eb->commit, scratch_pool));

          while (*relpath)
            {
              const char *item = svn_relpath_prefix(relpath, 1, scratch_pool);
              git_treebuilder *tb;
              const git_tree_entry *t_entry;

              SVN_ERR(svn_git__treebuilder_new(&tb, eb->repository, tree, eb->pool));
              APR_ARRAY_PUSH(eb->extra_builders, git_treebuilder *) = tb;

              t_entry = git_treebuilder_get(tb, item);
              if (!t_entry)
                return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL, NULL);

              SVN_ERR(svn_git__tree_lookup(&tree, eb->repository,
                                           git_tree_entry_id(t_entry),
                                           eb->pool));

              relpath = svn_relpath_skip_ancestor(item, relpath);
            }

          SVN_ERR(svn_git__treebuilder_new(&db->dir_builder,
                                           eb->repository, tree, db->pool));
        }
      else if (relpath)
        {
          svn_node_kind_t kind;

          /* We are creating or opening the branch */
          SVN_ERR(svn_fs_check_path(&kind, eb->root, db->node_path,
                                    scratch_pool));

          db->root = eb->root;
          db->root_path = db->node_path;

          if (kind == svn_node_none)
            SVN_ERR(svn_git__treebuilder_new(&db->dir_builder, eb->repository,
                                             NULL, db->pool));
          else if (kind == svn_node_dir)
            {
              int root_depth;
              const git_tree *tree;
              const char *root_relpath;

              if (svn_relpath_skip_ancestor("trunk", db->node_path))
                root_depth = 1;
              else
                root_depth = 2;

              root_relpath = svn_relpath_skip_ancestor(
                                svn_relpath_prefix(db->node_path, root_depth,
                                                   scratch_pool),
                                db->node_path);

              if (eb->created_rev == 0)
                tree = NULL;
              else if (*root_relpath != '\0')
                {
                  git_tree_entry *t_entry;

                  SVN_ERR(svn_git__commit_tree_entry(&t_entry, eb->commit,
                                                     root_relpath,
                                                     db->pool, scratch_pool));

                  if (!t_entry)
                    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                             _("'%s' not found in git tree"),
                                             root_relpath);

                  SVN_ERR(svn_git__tree_lookup(&tree, eb->repository,
                                               git_tree_entry_id(t_entry),
                                               db->pool));
                }
              else
                {
                  SVN_ERR(svn_git__commit_tree(&tree, eb->commit, db->pool));
                }

              SVN_ERR(svn_git__treebuilder_new(&db->dir_builder, eb->repository,
                                               tree, db->pool));
            }
        }
    }

  if (db->pb && !db->pb->dir_builder)
    SVN_ERR(setup_change_trees(db->pb, scratch_pool));

  if (db->pb && db->pb->dir_builder)
    {
      const git_tree_entry *entry;
      const git_tree *tree = NULL;
      entry = git_treebuilder_get(db->pb->dir_builder,
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
      db->root = db->eb->root;
      db->root_path = db->node_path;
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

          fs = svn_repos_fs(eb->repos);
          SVN_ERR(svn_fs_youngest_rev(&youngest, fs, scratch_pool));

          SVN_ERR(svn_fs_revision_root(&eb->root, fs, youngest, eb->pool));

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
        }
    }

  if (eb->change_mode)
    {
      if (!svn_relpath_skip_ancestor(eb->root_path, path))
        return svn_error_createf(SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                                 _("Can't commit to '%s' and '%s' in one commit"),
                                 eb->root_path, path);

      SVN_ERR(setup_change_trees(nb, scratch_pool));
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
  git_commit_edit_baton_t *eb = edit_baton;
  svn_node_kind_t kind;
  nb->eb = eb;
  nb->pool = result_pool;

  nb->node_path = svn_uri_skip_ancestor(nb->eb->sess->repos_root_url,
                                        nb->eb->sess->session_url_buf->data,
                                        result_pool);

  nb->root_path = nb->node_path;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    SVN_ERR(eb->session->vtable->get_latest_revnum(eb->session, &base_revision,
                                                   result_pool));

  SVN_ERR(svn_fs_revision_root(&eb->root, svn_repos_fs(eb->repos), base_revision,
                               eb->pool));

  SVN_ERR(svn_fs_check_path(&kind, eb->root, nb->root_path, result_pool));

  if (kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL, NULL);

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
  const git_tree_entry *t_entry;
  const char *name;

  SVN_ERR(ensure_mutable(pb, path, revision, scratch_pool));

  if (!pb->dir_builder)
    return svn_error_create(APR_ENOTIMPL, NULL, NULL);

  name = svn_relpath_basename(path, NULL);

  t_entry = git_treebuilder_get(pb->dir_builder, name);

  if (!t_entry)
    return svn_error_create(SVN_ERR_FS_NOT_FOUND, NULL, NULL);

  GIT2_ERR(git_treebuilder_remove(pb->dir_builder, name));

  return SVN_NO_ERROR;
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
  const char *name;

  SVN_ERR(ensure_mutable(pb, path, SVN_INVALID_REVNUM,
                         result_pool));

  if (!pb->eb->change_mode)
    return svn_error_create(APR_ENOTIMPL, NULL, NULL);

  name = svn_relpath_basename(path, NULL);

  db = apr_pcalloc(result_pool, sizeof(*db));
  db->pb = pb;
  db->eb = eb;
  db->pool = result_pool;
  db->node_path = svn_relpath_join(pb->node_path, name,
                                   result_pool);

  if (copyfrom_path)
    {
      /* TODO: LOOKUP copyfrom in git... setup dir_builder,
               root and root_path */
      return svn_error_create(APR_ENOTIMPL, NULL, NULL);
    }
  else
    {
      db->root = NULL;
      db->root_path = NULL;
    }

  *child_baton = db;

  if (pb->dir_builder)
    {
      const git_tree_entry *t_entry = git_treebuilder_get(pb->dir_builder,
                                                          name);

      if (t_entry)
        return svn_error_create(SVN_ERR_FS_ALREADY_EXISTS, NULL, NULL);
    }
  else if (strcmp(db->node_path, eb->root_path) != 0)
    return svn_error_create(APR_ENOTIMPL, NULL, NULL);

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
  git_commit_edit_baton_t *eb = pb->eb;
  git_commit_node_baton_t *db;
  const char *name;
  svn_node_kind_t kind;

  /* Hmm... not nice, but works for now */
  SVN_ERR(ensure_mutable(pb, path, SVN_INVALID_REVNUM,
                         result_pool));
 
 if (!pb->eb->change_mode)
    return svn_error_create(APR_ENOTIMPL, NULL, NULL);

  name = svn_relpath_basename(path, NULL);

  db = apr_pcalloc(result_pool, sizeof(*db));
  db->pb = pb;
  db->eb = eb;
  db->pool = result_pool;
  db->node_path = svn_relpath_join(pb->node_path, name,
                                   result_pool);
  if (pb->root)
    {
      db->root = pb->root;
      db->root_path = svn_relpath_join(pb->root_path, name,
                                       result_pool);

      SVN_ERR(svn_fs_check_path(&kind, db->root, db->root_path, result_pool));
    }
  else
    kind = svn_node_none;

  if (kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL, NULL);

  *child_baton = db;
  return SVN_NO_ERROR;
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
  git_commit_edit_baton_t *eb = db->eb;

  if (db->dir_builder)
    {
      git_oid oid;

      GIT2_ERR(git_treebuilder_write(&oid, db->dir_builder));

      if (db->pb && db->pb->dir_builder)
        GIT2_ERR(git_treebuilder_insert(NULL, db->pb->dir_builder,
                                        svn_relpath_basename(db->node_path,
                                                             NULL),
                                        &oid, GIT_FILEMODE_TREE));
      else
        {
          const char *relpath = svn_relpath_skip_ancestor(db->eb->root_path,
                                                          db->node_path);

          while (*relpath && eb->extra_builders->nelts)
            {
              git_treebuilder *tb;
              const char *name;

              svn_relpath_split(&relpath, &name, relpath, scratch_pool);
              tb = *(void **)apr_array_pop(eb->extra_builders);

              GIT2_ERR(git_treebuilder_insert(NULL, tb, name, &oid,
                                              GIT_FILEMODE_TREE));

              GIT2_ERR(git_treebuilder_write(&oid, tb));
            }

          SVN_ERR_ASSERT(!*relpath && !eb->extra_builders->nelts);

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
  git_commit_node_baton_t *fb;

  SVN_ERR(ensure_mutable(pb, path, SVN_INVALID_REVNUM,
                         result_pool));

  fb = apr_pcalloc(result_pool, sizeof(*fb));
  fb->pb = pb;
  fb->eb = pb->eb;
  fb->pool = result_pool;
  fb->node_path = svn_relpath_join(pb->node_path,
                                   svn_relpath_basename(path, NULL),
                                   result_pool);

  if (copyfrom_path)
    return svn_error_create(APR_ENOTIMPL, NULL, NULL);

  fb->added = TRUE;
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
git_commit__open_file(const char *path,
                      void *parent_baton,
                      svn_revnum_t base_revision,
                      apr_pool_t *result_pool,
                      void **file_baton)
{
  git_commit_node_baton_t *pb = parent_baton;
  git_commit_node_baton_t *fb;
  svn_node_kind_t kind;
  const char *name;

  SVN_ERR(ensure_mutable(pb, path, SVN_INVALID_REVNUM,
                         result_pool));

  name = svn_relpath_basename(path, NULL);

  fb = apr_pcalloc(result_pool, sizeof(*fb));
  fb->pb = pb;
  fb->eb = pb->eb;
  fb->pool = result_pool;
  fb->node_path = svn_relpath_join(pb->node_path, name,
                                   result_pool);

  if (!pb->root)
    kind = svn_node_none;
  else
    {
      fb->root = pb->root;
      fb->root_path = svn_relpath_join(pb->root_path, name,
                                       result_pool);

      SVN_ERR(svn_fs_check_path(&kind, fb->root, fb->root_path,
                                result_pool));
    }

  if (kind != svn_node_file)
    return svn_error_create(SVN_ERR_FS_NOT_FILE, NULL, NULL);

  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
git_commit__apply_textdelta(void *file_baton,
                            const char *base_checksum,
                            apr_pool_t *result_pool,
                            svn_txdelta_window_handler_t *handler,
                            void **handler_baton)
{
  git_commit_node_baton_t *fb = file_baton;
  apr_file_t *fnew;
  svn_stream_t *base_stream;

  if (base_checksum)
    SVN_ERR(svn_checksum_parse_hex(&fb->expected_base_checks, svn_checksum_md5,
                                   base_checksum, fb->pool));

  SVN_ERR(svn_io_open_unique_file3(&fnew, &fb->tmp_abspath, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   fb->pool, result_pool));

  if (fb->added)
    base_stream = svn_stream_empty(result_pool);
  else
    SVN_ERR(svn_fs_file_contents(&base_stream, fb->root, fb->root_path,
                                 result_pool));

  svn_txdelta_apply(svn_stream_checksummed2(
                        base_stream,
                        &fb->base_checksum, NULL, svn_checksum_md5, TRUE,
                        result_pool),
                    svn_stream_checksummed2(
                        svn_stream_from_aprfile2(fnew, FALSE, result_pool),
                        NULL, &fb->result_checksum, svn_checksum_md5, FALSE,
                        result_pool),
                    NULL, NULL, result_pool,
                    handler, handler_baton);

  return SVN_NO_ERROR;
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
  git_commit_node_baton_t *fb = file_baton;

  /* TODO: Verify checksums! */

  if (fb->pb->dir_builder && fb->tmp_abspath)
    {
      git_oid blob_oid;
      GIT2_ERR(git_blob_create_fromdisk(&blob_oid, fb->eb->repository,
                                        fb->tmp_abspath));

      GIT2_ERR(git_treebuilder_insert(NULL, fb->pb->dir_builder,
                                      svn_relpath_basename(fb->node_path,
                                                           NULL),
                                      &blob_oid,
                                      GIT_FILEMODE_BLOB));

      return SVN_NO_ERROR;
    }

  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
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
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_ra_git__session_t *sess = session->priv;
  git_commit_edit_baton_t *eb = apr_pcalloc(result_pool, sizeof(*eb));
  svn_delta_editor_t *editor = svn_delta_default_editor(result_pool);

  eb->pool = svn_pool_create(result_pool);
  eb->revprops = revprop_table;
  eb->commit_cb = callback;
  eb->commit_baton = callback_baton;
  eb->session = session;
  eb->sess = sess;

  eb->extra_builders = apr_array_make(eb->pool, 16,
                                      sizeof(git_treebuilder *));

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

  SVN_ERR(svn_repos_open3(&eb->repos, eb->sess->local_repos_abspath, NULL,
                          eb->pool, scratch_pool));

  return SVN_NO_ERROR;
}
