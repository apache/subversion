/*
 *  load_editor.c: The svn_delta_editor_t editor used by svnrdump to
 *  load revisions.
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_repos.h"
#include "svn_props.h"
#include "svn_ra.h"
#include "svn_io.h"

#include "load_editor.h"

static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, "* Loaded revision %ld\n",
                             commit_info->revision));
  return SVN_NO_ERROR;
}


static svn_error_t *
new_revision_record(void **revision_baton,
		    apr_hash_t *headers,
		    void *parse_baton,
		    apr_pool_t *pool)
{
  struct revision_baton *rb;
  struct parse_baton *pb;
  apr_hash_index_t *hi;

  rb = apr_pcalloc(pool, sizeof(*rb));
  pb = parse_baton;
  pb->pool = svn_pool_create(pool);
  rb->pb = pb;

  for (hi = apr_hash_first(pool, headers); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *hname, *hval;

      apr_hash_this(hi, &key, NULL, &val);
      hname = key;
      hval = val;

      if (strcmp(hname, SVN_REPOS_DUMPFILE_REVISION_NUMBER) == 0)
        rb->rev = atoi(hval);
    }
  /* Set the commit_editor/ commit_edit_baton to NULL and wait for
     them to be created in new_node_record */
  
  rb->pb->commit_editor = NULL;
  rb->pb->commit_edit_baton = NULL;
  rb->revprop_table = apr_hash_make(pb->pool);

  *revision_baton = rb;
  return SVN_NO_ERROR;
}

static svn_error_t *
uuid_record(const char *uuid,
            void *parse_baton,
            apr_pool_t *pool)
{
  struct parse_baton *pb;
  pb = parse_baton;
  pb->uuid = apr_pstrdup(pool, uuid);
  return SVN_NO_ERROR;
}

static svn_error_t *
new_node_record(void **node_baton,
                apr_hash_t *headers,
                void *revision_baton,
                apr_pool_t *pool)
{
  struct node_baton *nb;
  struct revision_baton *rb;
  apr_hash_index_t *hi;
  void *file_baton;
  void *child_baton;
  const struct svn_delta_editor_t *commit_editor;
  void *commit_edit_baton;
  void *root_baton;

  rb = revision_baton;
  nb = apr_pcalloc(rb->pb->pool, sizeof(*nb));
  nb->rb = rb;

  commit_editor = rb->pb->commit_editor;
  commit_edit_baton = rb->pb->commit_edit_baton;

  /* If the creation of commit_editor is pending, create it now and
     open_root on it */
  if (!commit_editor) {
      SVN_ERR(svn_ra_get_commit_editor3(rb->pb->session, &commit_editor,
                                        &commit_edit_baton, rb->revprop_table,
                                        commit_callback, NULL, NULL, FALSE,
                                        rb->pb->pool));

      rb->pb->commit_editor = commit_editor;
      rb->pb->commit_edit_baton = commit_edit_baton;

      SVN_ERR(commit_editor->set_target_revision(commit_edit_baton,
                                                 rb->rev, rb->pb->pool));
      SVN_ERR(commit_editor->open_root(commit_edit_baton, rb->rev,
                                       rb->pb->pool, &root_baton));
      rb->dir_baton = root_baton;

  }

  for (hi = apr_hash_first(rb->pb->pool, headers); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *hname, *hval;

      apr_hash_this(hi, &key, NULL, &val);
      hname = key;
      hval = val;

      /* Parse the different kinds of headers we can encounter and
         stuff them into the node_baton for writing later */
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_PATH) == 0)
        nb->path = apr_pstrdup(rb->pb->pool, hval);
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_KIND) == 0)
        nb->kind = strcmp(hval, "file") == 0 ? svn_node_file : svn_node_dir;
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_ACTION) == 0)
        {
          if (strcmp(hval, "add") == 0)
            nb->action = svn_node_action_add;
          if (strcmp(hval, "change") == 0)
            nb->action = svn_node_action_change;
          if (strcmp(hval, "delete") == 0)
            nb->action = svn_node_action_delete;
          if (strcmp(hval, "replace") == 0)
            nb->action = svn_node_action_replace;
        }
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV) == 0)
        nb->copyfrom_rev = atoi(hval);
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH) == 0)
        nb->copyfrom_path = apr_pstrdup(rb->pb->pool, hval);
    }

  switch (nb->action)
    {
    case svn_node_action_add:
      if (nb->kind == svn_node_file)
        {
          SVN_ERR(commit_editor->add_file(nb->path, nb->rb->dir_baton,
                                          nb->copyfrom_path, nb->copyfrom_rev,
                                          rb->pb->pool, &file_baton));
          nb->file_baton = file_baton;
        }
      else if(nb->kind == svn_node_dir)
        {
          SVN_ERR(commit_editor->add_directory(nb->path, nb->rb->dir_baton,
                                               nb->copyfrom_path, nb->copyfrom_rev,
                                               rb->pb->pool, &child_baton));
          nb->rb->dir_baton = child_baton;
        }
      break;
    default:
      break;
    }

  *node_baton = nb;
  return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property(void *baton,
                      const char *name,
                      const svn_string_t *value)
{
  struct revision_baton *rb;
  rb = baton;

  if (rb->rev > 0)
    {
      apr_hash_set(rb->revprop_table, apr_pstrdup(rb->pb->pool, name),
                   APR_HASH_KEY_STRING, svn_string_dup(value, rb->pb->pool));
    }
  else
    /* Special handling for revision 0; this is safe because the
       commit_editor hasn't been created yet. */
    svn_ra_change_rev_prop(rb->pb->session, rb->rev, name, value,
                           rb->pb->pool);

  /* Remember any datestamp that passes through!  (See comment in
     close_revision() below.) */
  if (! strcmp(name, SVN_PROP_REVISION_DATE))
    rb->datestamp = svn_string_dup(value, rb->pb->pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property(void *baton,
                  const char *name,
                  const svn_string_t *value)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_node_property(void *baton,
                     const char *name)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
remove_node_props(void *baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
set_fulltext(svn_stream_t **stream,
             void *node_baton)
{
  /* ### Not implemented */
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler,
                void **handler_baton,
                void *node_baton)
{
  struct node_baton *nb;
  const struct svn_delta_editor_t *commit_editor;
  apr_pool_t *pool;
  nb = node_baton;
  commit_editor = nb->rb->pb->commit_editor;
  pool = nb->rb->pb->pool;
  return commit_editor->apply_textdelta(nb->file_baton, NULL, pool,
                                        handler, handler_baton);
}

static svn_error_t *
close_node(void *baton)
{
  struct node_baton *nb;
  const struct svn_delta_editor_t *commit_editor;
  apr_pool_t * pool;
  nb = baton;
  pool = nb->rb->pb->pool;
  commit_editor = nb->rb->pb->commit_editor;
  if (nb->kind == svn_node_file)
    SVN_ERR(commit_editor->close_file(nb->file_baton, NULL,
                                      pool));
  else if (nb->kind == svn_node_dir)
    SVN_ERR(commit_editor->close_directory(nb->rb->dir_baton,
                                           pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *baton)
{
  struct revision_baton *rb;
  const svn_delta_editor_t *commit_editor;
  void *commit_edit_baton;
  rb = baton;

  commit_editor = rb->pb->commit_editor;
  commit_edit_baton = rb->pb->commit_edit_baton;

  /* r0 doesn't have a corresponding commit_editor; we fake it */
  if (rb->rev == 0)
    SVN_ERR(svn_cmdline_printf(rb->pb->pool, "* Loaded revision 0\n"));
  else
    SVN_ERR(commit_editor->close_edit(commit_edit_baton, rb->pb->pool));

  /* svn_fs_commit_txn rewrites the datestamp property to the current
     clock-time, and this is not desired. Rewrite it by hand after
     closing the commit_editor. */
  SVN_ERR(svn_ra_change_rev_prop(rb->pb->session, rb->rev,
                                 SVN_PROP_REVISION_DATE,
                                 rb->datestamp, rb->pb->pool));

  svn_pool_destroy(rb->pb->pool);

  return SVN_NO_ERROR;
}

svn_error_t *
get_dumpstream_loader(const svn_repos_parse_fns2_t **parser,
                      void **parse_baton,
                      svn_ra_session_t *session,
                      apr_pool_t *pool)
{
  svn_repos_parse_fns2_t *pf;
  struct parse_baton *pb;

  pf = apr_pcalloc(pool, sizeof(*pf));
  pf->new_revision_record = new_revision_record;
  pf->uuid_record = uuid_record;
  pf->new_node_record = new_node_record;
  pf->set_revision_property = set_revision_property;
  pf->set_node_property = set_node_property;
  pf->delete_node_property = delete_node_property;
  pf->remove_node_props = remove_node_props;
  pf->set_fulltext = set_fulltext;
  pf->apply_textdelta = apply_textdelta;
  pf->close_node = close_node;
  pf->close_revision = close_revision;

  pb = apr_pcalloc(pool, sizeof(*pb));
  pb->session = session;

  *parser = pf;
  *parse_baton = pb;

  return SVN_NO_ERROR;
}

svn_error_t *
drive_dumpstream_loader(svn_stream_t *stream,
                        const svn_repos_parse_fns2_t *parser,
                        void *parse_baton,
                        svn_ra_session_t *session,
                        apr_pool_t *pool)
{
  void *pb;
  pb = parse_baton;

  SVN_ERR(svn_repos_parse_dumpstream2(stream, parser,parse_baton,
                                      NULL, NULL, pool));

  return SVN_NO_ERROR;
}
