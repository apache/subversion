/*
 * copy_foreign.c:  copy from other repository support.
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

#include <string.h>
#include "svn_hash.h"
#include "svn_client.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_wc.h"

#include <apr_md5.h>

#include "client.h"
#include "private/svn_subr_private.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"


/* ------------------------------------------------------------------ */

/* WC Modifications Editor.
 *
 * TODO:
 *   - tests
 *   - use for all existing scenarios ('svn add', 'svn propset', etc.)
 *   - copy-from (half done: in dir_add only, untested)
 *   - text-delta
 *   - Instead of 'root_dir_add' option, probably the driver should anchor
 *     at the parent dir.
 *   - Instead of 'ignore_mergeinfo' option, implement that as a wrapper.
 */

struct edit_baton_t
{
  apr_pool_t *pool;
  const char *anchor_abspath;

  /* True => 'open_root' method will act as 'add_directory' */
  svn_boolean_t root_dir_add;
  /* True => filter out any incoming svn:mergeinfo property changes */
  svn_boolean_t ignore_mergeinfo_changes;

  svn_ra_session_t *ra_session;

  svn_wc_context_t *wc_ctx;
  svn_client_ctx_t *ctx;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};

struct dir_baton_t
{
  apr_pool_t *pool;

  struct dir_baton_t *pb;
  struct edit_baton_t *eb;

  const char *local_abspath;

  svn_boolean_t created;  /* already under version control in the WC */
  apr_hash_t *properties;

  int users;
};

/*  */
static svn_error_t *
get_path(const char **local_abspath_p,
         const char *anchor_abspath,
         const char *path,
         apr_pool_t *result_pool)
{
  svn_boolean_t under_root;

  SVN_ERR(svn_dirent_is_under_root(&under_root, local_abspath_p,
                                   anchor_abspath, path, result_pool));
  if (! under_root)
    {
      return svn_error_createf(
                    SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                    _("Path '%s' is not in the working copy"),
                    svn_dirent_local_style(path, result_pool));
    }
  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function */
static svn_error_t *
edit_open(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *result_pool,
          void **root_baton)
{
  struct edit_baton_t *eb = edit_baton;
  apr_pool_t *dir_pool = svn_pool_create(eb->pool);
  struct dir_baton_t *db = apr_pcalloc(dir_pool, sizeof(*db));

  db->pool = dir_pool;
  db->eb = eb;
  db->users = 1;
  db->local_abspath = eb->anchor_abspath;

  db->created = !(eb->root_dir_add);
  if (eb->root_dir_add)
    SVN_ERR(svn_io_make_dir_recursively(eb->anchor_abspath, dir_pool));

  *root_baton = db;

  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function */
static svn_error_t *
edit_close(void *edit_baton,
           apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *scratch_pool)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  const char *local_abspath;

  SVN_ERR(get_path(&local_abspath,
                   eb->anchor_abspath, path, scratch_pool));
  SVN_ERR(svn_wc_delete4(eb->wc_ctx, local_abspath,
                         FALSE /*keep_local*/,
                         TRUE /*delete_unversioned*/,
                         NULL, NULL, /*cancellation*/
                         eb->notify_func, eb->notify_baton, scratch_pool));

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
dir_open_or_add(const char *path,
                void *parent_baton,
                struct dir_baton_t **child_baton)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  apr_pool_t *dir_pool = svn_pool_create(pb->pool);
  struct dir_baton_t *db = apr_pcalloc(dir_pool, sizeof(*db));

  pb->users++;

  db->pb = pb;
  db->eb = pb->eb;
  db->pool = dir_pool;
  db->users = 1;

  SVN_ERR(get_path(&db->local_abspath,
                   eb->anchor_abspath, path, db->pool));

  *child_baton = db;
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
dir_open(const char *path,
         void *parent_baton,
         svn_revnum_t base_revision,
         apr_pool_t *result_pool,
         void **child_baton)
{
  struct dir_baton_t *db;

  SVN_ERR(dir_open_or_add(path, parent_baton, &db));
  db->created = TRUE;

  *child_baton = db;
  return SVN_NO_ERROR;
}

/* Are RA_SESSION and the versioned *parent* dir of WC_TARGET_ABSPATH in
 * the same repository?
 */
static svn_error_t *
is_same_repository(svn_boolean_t *same_repository,
                   svn_ra_session_t *ra_session,
                   const char *wc_target_abspath,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  const char *src_uuid, *dst_uuid;

  /* Get the repository UUIDs of copy source URL and WC parent path */
  SVN_ERR(svn_ra_get_uuid2(ra_session, &src_uuid, scratch_pool));
  SVN_ERR(svn_client_get_repos_root(NULL /*root_url*/, &dst_uuid,
                                    svn_dirent_dirname(wc_target_abspath,
                                                       scratch_pool),
                                    ctx, scratch_pool, scratch_pool));
  *same_repository = (strcmp(src_uuid, dst_uuid) == 0);
  return SVN_NO_ERROR;
}

static svn_error_t *
dir_add(const char *path,
        void *parent_baton,
        const char *copyfrom_path,
        svn_revnum_t copyfrom_revision,
        apr_pool_t *result_pool,
        void **child_baton)
{
  struct dir_baton_t *db;

  SVN_ERR(dir_open_or_add(path, parent_baton, &db));
  SVN_ERR(svn_io_make_dir_recursively(db->local_abspath, db->pool));

  if (copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_revision))
    {
      svn_boolean_t same_repository;
      svn_boolean_t timestamp_sleep;

      SVN_ERR(is_same_repository(&same_repository,
                                 db->eb->ra_session, db->local_abspath,
                                 db->eb->ctx, db->pool));

      SVN_ERR(svn_client__repos_to_wc_copy_dir(&timestamp_sleep,
                                               copyfrom_path,
                                               copyfrom_revision,
                                               db->local_abspath,
                                               TRUE /*ignore_externals*/,
                                               same_repository,
                                               db->eb->ra_session,
                                               db->eb->ctx, db->pool));
    }

  *child_baton = db;
  return SVN_NO_ERROR;
}

static svn_error_t *
dir_change_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *scratch_pool)
{
  struct dir_baton_t *db = dir_baton;
  struct edit_baton_t *eb = db->eb;
  svn_prop_kind_t prop_kind;

  prop_kind = svn_property_kind2(name);

  if (prop_kind != svn_prop_regular_kind
      || (eb->ignore_mergeinfo_changes && ! strcmp(name, SVN_PROP_MERGEINFO)))
    {
      /* We can't handle DAV, ENTRY and merge specific props here */
      return SVN_NO_ERROR;
    }

  if (! db->created)
    {
      /* Store properties to be added later in svn_wc_add_from_disk3() */
      if (! db->properties)
        db->properties = apr_hash_make(db->pool);

      if (value != NULL)
        svn_hash_sets(db->properties, apr_pstrdup(db->pool, name),
                      svn_string_dup(value, db->pool));
    }
  else
    {
      SVN_ERR(svn_wc_prop_set4(eb->wc_ctx, db->local_abspath, name, value,
                               svn_depth_empty, FALSE, NULL,
                               NULL, NULL, /* Cancellation */
                               NULL, NULL, /* Notification */
                               scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Releases the directory baton if there are no more users */
static svn_error_t *
maybe_done(struct dir_baton_t *db)
{
  db->users--;

  if (db->users == 0)
    {
      struct dir_baton_t *pb = db->pb;

      svn_pool_clear(db->pool);

      if (pb)
        SVN_ERR(maybe_done(pb));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ensure_added(struct dir_baton_t *db,
             apr_pool_t *scratch_pool)
{
  if (db->created)
    return SVN_NO_ERROR;

  if (db->pb)
    SVN_ERR(ensure_added(db->pb, scratch_pool));

  db->created = TRUE;

  /* Add the directory with all the already collected properties */
  SVN_ERR(svn_wc_add_from_disk3(db->eb->wc_ctx,
                                db->local_abspath,
                                db->properties,
                                TRUE /* skip checks */,
                                db->eb->notify_func,
                                db->eb->notify_baton,
                                scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
dir_close(void *dir_baton,
          apr_pool_t *scratch_pool)
{
  struct dir_baton_t *db = dir_baton;
  /*struct edit_baton_t *eb = db->eb;*/

  SVN_ERR(ensure_added(db, scratch_pool));

  SVN_ERR(maybe_done(db));

  return SVN_NO_ERROR;
}

struct file_baton_t
{
  apr_pool_t *pool;

  struct dir_baton_t *pb;
  struct edit_baton_t *eb;

  const char *local_abspath;
  svn_boolean_t created;  /* already under version control in the WC */
  apr_hash_t *properties;

  svn_boolean_t writing;
  unsigned char digest[APR_MD5_DIGESTSIZE];

  const char *tmp_path;
};

/*  */
static svn_error_t *
file_open_or_add(const char *path,
                 void *parent_baton,
                 struct file_baton_t **file_baton)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  apr_pool_t *file_pool = svn_pool_create(pb->pool);
  struct file_baton_t *fb = apr_pcalloc(file_pool, sizeof(*fb));

  pb->users++;

  fb->pool = file_pool;
  fb->eb = eb;
  fb->pb = pb;

  SVN_ERR(get_path(&fb->local_abspath,
                   eb->anchor_abspath, path, fb->pool));

  *file_baton = fb;
  return SVN_NO_ERROR;
}

static svn_error_t *
file_open(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *result_pool,
          void **file_baton)
{
  struct file_baton_t *fb;

  SVN_ERR(file_open_or_add(path, parent_baton, &fb));
  fb->created = TRUE;

  *file_baton = fb;
  return SVN_NO_ERROR;
}

static svn_error_t *
file_add(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *result_pool,
         void **file_baton)
{
  struct file_baton_t *fb;

  SVN_ERR(file_open_or_add(path, parent_baton, &fb));

  if (copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_revision))
    {
      svn_boolean_t same_repository;
      svn_boolean_t timestamp_sleep;

      SVN_ERR(is_same_repository(&same_repository,
                                 fb->eb->ra_session, fb->local_abspath,
                                 fb->eb->ctx, fb->pool));

      SVN_ERR(svn_client__repos_to_wc_copy_file(&timestamp_sleep,
                                                copyfrom_path,
                                                copyfrom_revision,
                                                fb->local_abspath,
                                                same_repository,
                                                fb->eb->ra_session,
                                                fb->eb->ctx, fb->pool));
    }

  *file_baton = fb;
  return SVN_NO_ERROR;
}

static svn_error_t *
file_change_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *scratch_pool)
{
  struct file_baton_t *fb = file_baton;
  struct edit_baton_t *eb = fb->eb;
  svn_prop_kind_t prop_kind;

  prop_kind = svn_property_kind2(name);

  if (prop_kind != svn_prop_regular_kind
      || (eb->ignore_mergeinfo_changes && ! strcmp(name, SVN_PROP_MERGEINFO)))
    {
      /* We can't handle DAV, ENTRY and merge specific props here */
      return SVN_NO_ERROR;
    }

  if (! fb->created)
    {
      /* Store properties to be added later in svn_wc_add_from_disk3() */
      if (! fb->properties)
        fb->properties = apr_hash_make(fb->pool);

      if (value != NULL)
        svn_hash_sets(fb->properties, apr_pstrdup(fb->pool, name),
                      svn_string_dup(value, fb->pool));
    }
  else
    {
      SVN_ERR(svn_wc_prop_set4(eb->wc_ctx, fb->local_abspath, name, value,
                               svn_depth_empty, FALSE, NULL,
                               NULL, NULL, /* Cancellation */
                               NULL, NULL, /* Notification */
                               scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
file_textdelta(void *file_baton,
               const char *base_checksum,
               apr_pool_t *result_pool,
               svn_txdelta_window_handler_t *handler,
               void **handler_baton)
{
  struct file_baton_t *fb = file_baton;
  svn_stream_t *target;

  SVN_ERR_ASSERT(! fb->writing);

  SVN_ERR(svn_stream_open_writable(&target, fb->local_abspath, fb->pool,
                                   fb->pool));

  fb->writing = TRUE;
  svn_txdelta_apply(svn_stream_empty(fb->pool) /* source */,
                    target,
                    fb->digest,
                    fb->local_abspath,
                    fb->pool,
                    /* Provide the handler directly */
                    handler, handler_baton);

  return SVN_NO_ERROR;
}

static svn_error_t *
ensure_added_file(struct file_baton_t *fb,
                  apr_pool_t *scratch_pool)
{
  struct edit_baton_t *eb = fb->eb;

  if (fb->created)
    return SVN_NO_ERROR;

  if (fb->pb)
    SVN_ERR(ensure_added(fb->pb, scratch_pool));

  fb->created = TRUE;

  /* Add the file with all the already collected properties */
  SVN_ERR(svn_wc_add_from_disk3(eb->wc_ctx, fb->local_abspath, fb->properties,
                                TRUE /* skip checks */,
                                eb->notify_func, eb->notify_baton,
                                fb->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
file_close(void *file_baton,
           const char *text_checksum,
           apr_pool_t *scratch_pool)
{
  struct file_baton_t *fb = file_baton;
  struct dir_baton_t *pb = fb->pb;

  if (text_checksum)
    {
      svn_checksum_t *expected_checksum;
      svn_checksum_t *actual_checksum;

      SVN_ERR(svn_checksum_parse_hex(&expected_checksum, svn_checksum_md5,
                                     text_checksum, fb->pool));
      actual_checksum = svn_checksum__from_digest_md5(fb->digest, fb->pool);

      if (! svn_checksum_match(expected_checksum, actual_checksum))
        return svn_error_trace(
                    svn_checksum_mismatch_err(expected_checksum,
                                              actual_checksum,
                                              fb->pool,
                                         _("Checksum mismatch for '%s'"),
                                              svn_dirent_local_style(
                                                    fb->local_abspath,
                                                    fb->pool)));
    }

  SVN_ERR(ensure_added_file(fb, fb->pool));

  svn_pool_destroy(fb->pool);
  SVN_ERR(maybe_done(pb));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__wc_editor_internal(const svn_delta_editor_t **editor_p,
                               void **edit_baton_p,
                               const char *dst_abspath,
                               svn_boolean_t root_dir_add,
                               svn_boolean_t ignore_mergeinfo_changes,
                               svn_wc_notify_func2_t notify_func,
                               void *notify_baton,
                               svn_ra_session_t *ra_session,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool)
{
  svn_delta_editor_t *editor = svn_delta_default_editor(result_pool);
  struct edit_baton_t *eb = apr_pcalloc(result_pool, sizeof(*eb));

  eb->pool = result_pool;
  eb->anchor_abspath = apr_pstrdup(result_pool, dst_abspath);
  eb->root_dir_add = root_dir_add;
  eb->ignore_mergeinfo_changes = ignore_mergeinfo_changes;

  eb->ra_session = ra_session;
  eb->wc_ctx = ctx->wc_ctx;
  eb->ctx = ctx;
  eb->notify_func = notify_func;
  eb->notify_baton  = notify_baton;

  editor->open_root = edit_open;
  editor->close_edit = edit_close;

  editor->delete_entry = delete_entry;

  editor->open_directory = dir_open;
  editor->add_directory = dir_add;
  editor->change_dir_prop = dir_change_prop;
  editor->close_directory = dir_close;

  editor->open_file = file_open;
  editor->add_file = file_add;
  editor->change_file_prop = file_change_prop;
  editor->apply_textdelta = file_textdelta;
  editor->close_file = file_close;

  *editor_p = editor;
  *edit_baton_p = eb;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__wc_editor(const svn_delta_editor_t **editor_p,
                      void **edit_baton_p,
                      const char *dst_abspath,
                      svn_wc_notify_func2_t notify_func,
                      void *notify_baton,
                      svn_ra_session_t *ra_session,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool)
{
  SVN_ERR(svn_client__wc_editor_internal(editor_p, edit_baton_p,
                                         dst_abspath,
                                         FALSE /*root_dir_add*/,
                                         FALSE /*ignore_mergeinfo_changes*/,
                                         notify_func, notify_baton,
                                         ra_session,
                                         ctx, result_pool));
  return SVN_NO_ERROR;
}
