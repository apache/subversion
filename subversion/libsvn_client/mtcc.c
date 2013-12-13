/*
 * mtcc.c -- Multi Command Context implementation. This allows
 *           performing many operations without a working copy.
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

#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_pools.h"
#include "svn_subst.h"

#include "svn_private_config.h"

#include "client.h"
#include "mtcc.h"

#include <assert.h>

static svn_client_mtcc_op_t *
mtcc_op_create(const char *name,
               svn_boolean_t add,
               svn_boolean_t directory,
               apr_pool_t *result_pool)
{
  svn_client_mtcc_op_t *op;

  op = apr_pcalloc(result_pool, sizeof(*op));
  op->name = name ? apr_pstrdup(result_pool, name) : "";

  if (add)
    op->kind = directory ? OP_ADD_DIR : OP_ADD_FILE;
  else
    op->kind = directory ? OP_OPEN_DIR : OP_OPEN_FILE;

  if (directory)
    op->children = apr_array_make(result_pool, 4,
                                  sizeof(svn_client_mtcc_op_t *));

  return op;
}

static svn_error_t *
mtcc_op_find(svn_client_mtcc_op_t **op,
             svn_boolean_t *created,
             const char *relpath,
             svn_client_mtcc_op_t *base_op,
             svn_boolean_t find_existing,
             svn_boolean_t find_deletes,
             svn_boolean_t create_file,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  const char *name;
  const char *child;
  int i;

  assert(svn_relpath_is_canonical(relpath));
  *created = FALSE;

  if (!*relpath)
    {
      if (find_existing)
        *op = base_op;
      else
        *op = NULL;

      return SVN_NO_ERROR;
    }

  child = strchr(relpath, '/');

  if (child)
    {
      name = apr_pstrmemdup(scratch_pool, relpath, (child-relpath));
      child++; /* Skip '/' */
    }
  else
    name = relpath;

  if (child && !base_op->children)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("Can't operate on '%s' because '%s' is not a "
                               "directory"),
                             child, base_op->name);

  for (i = 0; i < base_op->children->nelts; i++)
    {
      svn_client_mtcc_op_t *cop;

      cop = APR_ARRAY_IDX(base_op->children, i, svn_client_mtcc_op_t *);

      if (! strcmp(cop->name, name)
          && (find_deletes || cop->kind != OP_DELETE))
        {
          return svn_error_trace(
                        mtcc_op_find(op, created, child ? child : "", cop,
                                     find_existing, find_deletes, create_file,
                                     result_pool, scratch_pool));
        }
    }

  if (!created)
    {
      *op = NULL;
      return SVN_NO_ERROR;
    }

  {
    svn_client_mtcc_op_t *cop;

    cop = mtcc_op_create(name, FALSE, child || !create_file, result_pool);

    APR_ARRAY_PUSH(base_op->children, svn_client_mtcc_op_t *) = cop;

    if (!child)
      {
        *op = cop;
        *created = TRUE;
        return SVN_NO_ERROR;
      }

    return svn_error_trace(
                mtcc_op_find(op, created, child, cop, find_existing,
                             find_deletes, create_file,
                             result_pool, scratch_pool));
  }
}

svn_error_t *
svn_client_mtcc_create(svn_client_mtcc_t **mtcc,
                       const char *anchor_url,
                       svn_revnum_t base_revision,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_pool_t *mtcc_pool;

  mtcc_pool = svn_pool_create(result_pool);

  *mtcc = apr_pcalloc(mtcc_pool, sizeof(**mtcc));
  (*mtcc)->pool = mtcc_pool;
  (*mtcc)->base_revision = base_revision;

  (*mtcc)->root_op = mtcc_op_create(NULL, FALSE, TRUE, mtcc_pool);

  (*mtcc)->ctx = ctx;

  SVN_ERR(svn_client_open_ra_session2(&(*mtcc)->ra_session, anchor_url,
                                      NULL /* wri_abspath */, ctx,
                                      mtcc_pool, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
update_copy_src(svn_client_mtcc_op_t *op,
                const char *add_relpath,
                apr_pool_t *result_pool)
{
  int i;

  if (op->src_relpath)
    op->src_relpath = svn_relpath_join(add_relpath, op->src_relpath,
                                       result_pool);

  if (!op->children)
    return SVN_NO_ERROR;

  for (i = 0; i < op->children->nelts; i++)
    {
      svn_client_mtcc_op_t *cop;

      cop = APR_ARRAY_IDX(op->children, i, svn_client_mtcc_op_t *);

      SVN_ERR(update_copy_src(cop, add_relpath, result_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_mtcc_get_relpath(const char **relpath,
                            const char *url,
                            svn_client_mtcc_t *mtcc,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *new_anchor;
  const char *session_url;
  const char *up;

  err = svn_ra_get_path_relative_to_session(mtcc->ra_session, relpath, url,
                                            result_pool);

  if (! err || err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_trace(err);

  svn_error_clear(err);

  SVN_ERR(svn_ra_get_session_url(mtcc->ra_session, &session_url,
                                 scratch_pool));

  new_anchor = svn_uri_get_longest_ancestor(url, session_url, scratch_pool);

  if (svn_path_is_empty(new_anchor))
    {
      return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                               _("'%s' is not in the same repository as '%s'"),
                               url, session_url);
    }

  up = svn_uri_skip_ancestor(new_anchor, session_url, scratch_pool);

  /* Update copy origins recursively...:( */
  SVN_ERR(update_copy_src(mtcc->root_op, up, mtcc->pool));

  SVN_ERR(svn_ra_reparent(mtcc->ra_session, new_anchor, scratch_pool));

  /* Create directory open operations for new ancestors */
  while (*up)
    {
      svn_client_mtcc_op_t *root_op;

      mtcc->root_op->name = svn_relpath_basename(up, mtcc->pool);
      up = svn_relpath_dirname(up, scratch_pool);

      root_op = mtcc_op_create(NULL, FALSE, TRUE, mtcc->pool);

      APR_ARRAY_PUSH(root_op->children,
                     svn_client_mtcc_op_t *) = mtcc->root_op;

      mtcc->root_op = root_op;
    }

  return svn_error_trace(
            svn_ra_get_path_relative_to_session(mtcc->ra_session, relpath, url,
                                                result_pool));
}


svn_error_t *
svn_client_mtcc_add_add_file(const char *relpath,
                             svn_stream_t *src_stream,
                             const svn_checksum_t *src_checksum,
                             svn_client_mtcc_t *mtcc,
                             apr_pool_t *scratch_pool)
{
  svn_client_mtcc_op_t *op;
  svn_boolean_t created;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath) && src_stream);

  SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, FALSE, FALSE,
                       TRUE, mtcc->pool, scratch_pool));

  if (!op || !created)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't add file at '%s'"),
                               relpath);
    }

  op->kind = OP_ADD_FILE;
  op->src_stream = src_stream;
  op->src_checksum = src_checksum ? svn_checksum_dup(src_checksum, mtcc->pool)
                                  : NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_mtcc_add_copy(const char *src_relpath,
                         svn_revnum_t revision,
                         const char *dst_relpath,
                         svn_client_mtcc_t *mtcc,
                         apr_pool_t *scratch_pool)
{
  svn_client_mtcc_op_t *op;
  svn_boolean_t created;
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(src_relpath)
                 && svn_relpath_is_canonical(dst_relpath)
                 && SVN_IS_VALID_REVNUM(revision));

  /* Subversion requires the kind of a copy */
  SVN_ERR(svn_ra_check_path(mtcc->ra_session, src_relpath, revision, &kind,
                            scratch_pool));

  if (kind != svn_node_dir && kind != svn_node_file)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't create a copy of '%s' at revision %ld "
                                 "as it does not exist"),
                               src_relpath, revision);
    }

  SVN_ERR(mtcc_op_find(&op, &created, dst_relpath, mtcc->root_op, FALSE, FALSE,
                       (kind == svn_node_file), mtcc->pool, scratch_pool));

  if (!op || !created)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't add node at '%s'"),
                               dst_relpath);
    }

  op->kind = (kind == svn_node_file) ? OP_ADD_FILE : OP_ADD_DIR;
  op->src_relpath = apr_pstrdup(mtcc->pool, src_relpath);
  op->src_rev = revision;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_mtcc_add_delete(const char *relpath,
                          svn_client_mtcc_t *mtcc,
                          apr_pool_t *scratch_pool)
{
  svn_client_mtcc_op_t *op;
  svn_boolean_t created;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));

  SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, FALSE, TRUE,
                       TRUE, mtcc->pool, scratch_pool));

  if (!op || !created)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't delete node at '%s'"),
                               relpath);
    }

  op->kind = OP_DELETE;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_mtcc_add_mkdir(const char *relpath,
                          svn_client_mtcc_t *mtcc,
                          apr_pool_t *scratch_pool)
{
  svn_client_mtcc_op_t *op;
  svn_boolean_t created;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));

  SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, FALSE, FALSE,
                       FALSE, mtcc->pool, scratch_pool));

  if (!op || !created)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't create directory at '%s'"),
                               relpath);
    }

  op->kind = OP_ADD_DIR;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_mtcc_add_move(const char *src_relpath,
                         const char *dst_relpath,
                         svn_client_mtcc_t *mtcc,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_client_mtcc_add_delete(src_relpath, mtcc, scratch_pool));
  SVN_ERR(svn_client_mtcc_add_copy(src_relpath, mtcc->base_revision,
                                   dst_relpath, mtcc, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_mtcc_add_propset(const char *relpath,
                            const char *propname,
                            const svn_string_t *propval,
                            svn_boolean_t skip_checks,
                            svn_client_mtcc_t *mtcc,
                            apr_pool_t *scratch_pool)
{
  svn_client_mtcc_op_t *op;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath)
                 && svn_property_kind2(propname) == svn_prop_regular_kind);

  if (!skip_checks && svn_prop_needs_translation(propname))
    {
      svn_string_t *translated_value;
       SVN_ERR_W(svn_subst_translate_string2(&translated_value, NULL,
                                             NULL, propval,
                                             NULL, FALSE,
                                             scratch_pool, scratch_pool),
                 _("Error normalizing property value"));
    }

  SVN_ERR(mtcc_op_find(&op, NULL, relpath, mtcc->root_op, TRUE, FALSE,
                       FALSE, mtcc->pool, scratch_pool));

  if (!op)
    {
      svn_node_kind_t kind;
      svn_boolean_t created;
      const char *origin_relpath = relpath;
      svn_revnum_t origin_revnum = mtcc->base_revision;

      /* ### TODO: Check if this node is within a newly copied directory,
                   and update origin values accordingly */

      SVN_ERR(svn_ra_check_path(mtcc->ra_session, origin_relpath,
                                origin_revnum, &kind, scratch_pool));

      if (kind != svn_node_file && kind != svn_node_dir)
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("Can't set properties at not existing '%s'"),
                                 relpath);

      SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, TRUE, FALSE,
                           (kind != svn_node_dir),
                           mtcc->pool, scratch_pool));

      SVN_ERR_ASSERT(op != NULL);
    }

  if (!op->prop_mods)
      op->prop_mods = apr_array_make(mtcc->pool, 4, sizeof(svn_prop_t));

  {
    svn_prop_t propchange;
    propchange.name = apr_pstrdup(mtcc->pool, propname);

    if (propval)
      propchange.value = svn_string_dup(propval, mtcc->pool);
    else
      propchange.value = NULL;

    APR_ARRAY_PUSH(op->prop_mods, svn_prop_t) = propchange;
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_mtcc_add_update_file(const char *relpath,
                                svn_stream_t *src_stream,
                                const svn_checksum_t *src_checksum,
                                svn_stream_t *base_stream,
                                const svn_checksum_t *base_checksum,
                                svn_client_mtcc_t *mtcc,
                                apr_pool_t *scratch_pool)
{
  svn_client_mtcc_op_t *op;
  svn_boolean_t created;
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath) && src_stream);

  SVN_ERR(mtcc_op_find(&op, &created, relpath, mtcc->root_op, TRUE, FALSE,
                       TRUE, mtcc->pool, scratch_pool));

  if (!op
      || (op->kind != OP_OPEN_FILE && op->kind != OP_ADD_FILE)
      || (op->src_stream != NULL))
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Can't update file at '%s'"), relpath);
    }

  op->src_stream = src_stream;
  op->src_checksum = src_checksum ? svn_checksum_dup(src_checksum, mtcc->pool)
                                  : NULL;

  op->base_stream = base_stream;
  op->base_checksum = base_checksum ? svn_checksum_dup(base_checksum,
                                                       mtcc->pool)
                                    : NULL;

  return SVN_NO_ERROR;
}

static svn_error_t *
commit_properties(const svn_delta_editor_t *editor,
                  const svn_client_mtcc_op_t *op,
                  void *node_baton,
                  apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool;

  if (!op->prop_mods || op->prop_mods->nelts == 0)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < op->prop_mods->nelts; i++)
    {
      const svn_prop_t *mod = &APR_ARRAY_IDX(op->prop_mods, i, svn_prop_t);

      svn_pool_clear(iterpool);

      if (op->kind == OP_ADD_DIR || op->kind == OP_OPEN_DIR)
        SVN_ERR(editor->change_dir_prop(node_baton, mod->name, mod->value,
                                        iterpool));
      else if (op->kind == OP_ADD_FILE || op->kind == OP_OPEN_FILE)
        SVN_ERR(editor->change_file_prop(node_baton, mod->name, mod->value,
                                         iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
commit_file(const svn_delta_editor_t *editor,
            svn_client_mtcc_op_t *op,
            void *file_baton,
            svn_client_ctx_t *ctx,
            apr_pool_t *scratch_pool)
{
  const char *text_checksum = NULL;
  svn_checksum_t *src_checksum = op->src_checksum;
  SVN_ERR(commit_properties(editor, op, file_baton, scratch_pool));

  if (op->src_stream)
    {
      const char *base_checksum = NULL;
      apr_pool_t *txdelta_pool = scratch_pool;
      svn_txdelta_window_handler_t window_handler;
      void *handler_baton;
      svn_stream_t *src_stream = op->src_stream;

      if (op->base_checksum && op->base_checksum->kind == svn_checksum_md5)
        base_checksum = svn_checksum_to_cstring(op->base_checksum, scratch_pool);

      /* ### TODO: Future enhancement: Allocate in special pool and send
                   files after the true edit operation, like a wc commit */
      SVN_ERR(editor->apply_textdelta(file_baton, base_checksum, txdelta_pool,
                                      &window_handler, &handler_baton));

      if (window_handler != svn_delta_noop_window_handler)
        {
          if (!src_checksum || src_checksum->kind != svn_checksum_md5)
            src_stream = svn_stream_checksummed2(src_stream, NULL, &src_checksum,
                                                 svn_checksum_md5,
                                                 TRUE, scratch_pool);

          if (!op->base_stream)
            SVN_ERR(svn_txdelta_send_stream(src_stream,
                                            window_handler, handler_baton, NULL,
                                            scratch_pool));
          else
            SVN_ERR(svn_txdelta_run(op->base_stream, src_stream,
                                    window_handler, handler_baton,
                                    svn_checksum_md5, NULL,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    scratch_pool, scratch_pool));
        }
      else
        {
          if (op->src_stream)
            SVN_ERR(svn_stream_close(op->src_stream));
          if (op->base_stream)
            SVN_ERR(svn_stream_close(op->base_stream));
        }
    }

  if (op->src_checksum && op->src_checksum->kind == svn_checksum_md5)
    text_checksum = svn_checksum_to_cstring(op->src_checksum, scratch_pool);

  return svn_error_trace(editor->close_file(file_baton, text_checksum,
                                            scratch_pool));
}

static svn_error_t *
commit_directory(const svn_delta_editor_t *editor,
                 svn_client_mtcc_op_t *op,
                 const char *relpath,
                 svn_revnum_t base_rev,
                 void *dir_baton,
                 const char *session_url,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  SVN_ERR(commit_properties(editor, op, dir_baton, scratch_pool));

  if (op->children && op->children->nelts > 0)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      for (i = 0; i < op->children->nelts; i++)
        {
          svn_client_mtcc_op_t *cop;
          const char * child_relpath;
          void *child_baton;

          cop = APR_ARRAY_IDX(op->children, i, svn_client_mtcc_op_t *);

          svn_pool_clear(iterpool);

          child_relpath = svn_relpath_join(relpath, cop->name, iterpool);

          switch (cop->kind)
            {
              case OP_DELETE:
                SVN_ERR(editor->delete_entry(child_relpath, base_rev,
                                             dir_baton, iterpool));
                break;

              case OP_ADD_DIR:
                SVN_ERR(editor->add_directory(child_relpath, dir_baton,
                                              cop->src_relpath
                                                ? svn_path_url_add_component2(
                                                              session_url,
                                                              cop->src_relpath,
                                                              iterpool)
                                                : NULL,
                                              cop->src_rev,
                                              iterpool, &child_baton));
                SVN_ERR(commit_directory(editor, cop, child_relpath,
                                         SVN_INVALID_REVNUM, child_baton,
                                         session_url, ctx, iterpool));
                break;
              case OP_OPEN_DIR:
                SVN_ERR(editor->open_directory(child_relpath, dir_baton,
                                               base_rev, iterpool, &child_baton));
                SVN_ERR(commit_directory(editor, cop, child_relpath,
                                         base_rev, child_baton,
                                         session_url, ctx, iterpool));
                break;

              case OP_ADD_FILE:
                SVN_ERR(editor->add_file(child_relpath, dir_baton,
                                         cop->src_relpath
                                            ? svn_path_url_add_component2(
                                                            session_url,
                                                            cop->src_relpath,
                                                            iterpool)
                                            : NULL,
                                         cop->src_rev,
                                         iterpool, &child_baton));
                SVN_ERR(commit_file(editor, cop, child_baton, ctx, iterpool));
                break;
              case OP_OPEN_FILE:
                SVN_ERR(editor->open_file(child_relpath, dir_baton, base_rev,
                                          iterpool, &child_baton));
                SVN_ERR(commit_file(editor, cop, child_baton, ctx, iterpool));
                break;

              default:
                SVN_ERR_MALFUNCTION();
            }
        }
    }

  return svn_error_trace(editor->close_directory(dir_baton, scratch_pool));
}


/* Helper function to recursively create svn_client_commit_item3_t items
   to provide to the log message callback */
static svn_error_t *
add_commit_items(svn_client_mtcc_op_t *op,
                 const char *session_url,
                 const char *url,
                 apr_array_header_t *commit_items,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  if ((op->kind != OP_OPEN_DIR && op->kind != OP_OPEN_FILE)
      || (op->prop_mods && op->prop_mods->nelts)
      || (op->src_stream))
    {
      svn_client_commit_item3_t *item;

      item = svn_client_commit_item3_create(result_pool);

      item->path = NULL;
      if (op->kind == OP_OPEN_DIR || op->kind == OP_ADD_DIR)
        item->kind = svn_node_dir;
      else if (op->kind == OP_OPEN_FILE || op->kind == OP_ADD_FILE)
        item->kind = svn_node_file;
      else
        item->kind = svn_node_unknown;

      item->url = apr_pstrdup(result_pool, url);
      item->session_relpath = svn_relpath_skip_ancestor(session_url, item->url,
                                                        result_pool);

      if (op->src_relpath)
        {
          item->copyfrom_url = svn_path_url_add_component2(session_url,
                                                           op->src_relpath,
                                                           result_pool);
          item->copyfrom_rev = op->src_rev;
        }
      else
        item->copyfrom_rev = SVN_INVALID_REVNUM;

      if (op->kind == OP_ADD_DIR || op->kind == OP_ADD_FILE)
        item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      else if (op->kind == OP_DELETE)
        item->state_flags = SVN_CLIENT_COMMIT_ITEM_DELETE;
      /* else item->state_flags = 0; */

      if (op->prop_mods && op->prop_mods->nelts)
        item->state_flags |= SVN_CLIENT_COMMIT_ITEM_PROP_MODS;

      if (op->src_stream)
        item->state_flags |= SVN_CLIENT_COMMIT_ITEM_TEXT_MODS;

      APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
    }

  if (op->children && op->children->nelts)
    {
      int i;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      for (i = 0; i < op->children->nelts; i++)
        {
          svn_client_mtcc_op_t *cop;
          const char * child_url;

          cop = APR_ARRAY_IDX(op->children, i, svn_client_mtcc_op_t *);

          svn_pool_clear(iterpool);

          child_url = svn_path_url_add_component2(url, cop->name, iterpool);

          SVN_ERR(add_commit_items(cop, session_url, child_url, commit_items,
                                   result_pool, iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_mtcc_commit(apr_hash_t *revprop_table,
                       svn_commit_callback2_t commit_callback,
                       void *commit_baton,
                       svn_client_mtcc_t *mtcc,
                       apr_pool_t *scratch_pool)
{
  const svn_delta_editor_t *editor;
  apr_hash_t *commit_revprops;
  void *edit_baton;
  svn_error_t *err;
  void *root_baton;
  const char *session_url;
  const char *log_msg;

  SVN_ERR(svn_ra_get_session_url(mtcc->ra_session, &session_url, scratch_pool));

    /* Create new commit items and add them to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(mtcc->ctx))
    {
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
                = apr_array_make(scratch_pool, 32, sizeof(item));

      SVN_ERR(add_commit_items(mtcc->root_op, session_url, commit_items,
                               scratch_pool, scratch_pool));

      SVN_ERR(svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                      mtcc->ctx, scratch_pool));

      if (! log_msg)
        return SVN_NO_ERROR;
    }
  else
    log_msg = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, mtcc->ctx, scratch_pool));

  SVN_ERR(svn_ra_get_commit_editor3(mtcc->ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    commit_callback, commit_baton,
                                    NULL /* lock_tokens */,
                                    FALSE /* keep_locks */,
                                    scratch_pool));

  err = editor->open_root(edit_baton, mtcc->base_revision, scratch_pool, &root_baton);

  if (!err)
    err = commit_directory(editor, mtcc->root_op, "", mtcc->base_revision,
                           root_baton, session_url, mtcc->ctx, scratch_pool);

  if (!err)
    SVN_ERR(editor->close_edit(edit_baton, scratch_pool));
  else
    err = svn_error_compose_create(err,
                                   editor->abort_edit(edit_baton, scratch_pool));

  svn_pool_destroy(mtcc->pool);

  return svn_error_trace(err);
}
