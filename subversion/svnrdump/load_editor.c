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
#include "svn_path.h"
#include "svn_ra.h"
#include "svn_io.h"
#include "svn_private_config.h"

#include <apr_network_io.h>

#include "load_editor.h"

#define SVNRDUMP_PROP_LOCK SVN_PROP_PREFIX "rdump-lock"
#define LOCK_RETRIES 10

#if 0
#define LDR_DBG(x) SVN_DBG(x)
#else
#define LDR_DBG(x) while(0)
#endif

static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  /* ### Don't print directly; generate a notification. */
  SVN_ERR(svn_cmdline_printf(pool, "* Loaded revision %ld.\n",
                             commit_info->revision));
  return SVN_NO_ERROR;
}

/* See subversion/svnsync/main.c for docstring */
static svn_boolean_t is_atomicity_error(svn_error_t *err)
{
  return svn_error_has_cause(err, SVN_ERR_FS_PROP_BASEVALUE_MISMATCH);
}

/* Acquire a lock (of sorts) on the repository associated with the
 * given RA SESSION. This lock is just a revprop change attempt in a
 * time-delay loop. This function is duplicated by svnsync in main.c.
 *
 * ### TODO: Make this function more generic and
 * expose it through a header for use by other Subversion
 * applications to avoid duplication.
 */
static svn_error_t *
get_lock(const svn_string_t **lock_string_p,
         svn_ra_session_t *session,
         svn_cancel_func_t cancel_func,
         void *cancel_baton,
         apr_pool_t *pool)
{
  char hostname_str[APRMAXHOSTLEN + 1] = { 0 };
  svn_string_t *mylocktoken, *reposlocktoken;
  apr_status_t apr_err;
  svn_boolean_t be_atomic;
  apr_pool_t *subpool;
  int i;

  SVN_ERR(svn_ra_has_capability(session, &be_atomic,
                                SVN_RA_CAPABILITY_ATOMIC_REVPROPS,
                                pool));
  if (! be_atomic)
    {
      /* Pre-1.7 servers can't lock without a race condition.  (Issue #3546) */
      svn_error_t *err =
        svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                         _("Target server does not support atomic revision "
                           "property edits; consider upgrading it to 1.7."));
      svn_handle_warning2(stderr, err, "svnrdump: ");
      svn_error_clear(err);
    }

  apr_err = apr_gethostname(hostname_str, sizeof(hostname_str), pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get local hostname"));

  mylocktoken = svn_string_createf(pool, "%s:%s", hostname_str,
                                   svn_uuid_generate(pool));

  /* If we succeed, this is what the property will be set to. */
  *lock_string_p = mylocktoken;

  subpool = svn_pool_create(pool);

  for (i = 0; i < LOCK_RETRIES; ++i)
    {
      svn_error_t *err;

      svn_pool_clear(subpool);

      SVN_ERR(cancel_func(cancel_baton));
      SVN_ERR(svn_ra_rev_prop(session, 0, SVNRDUMP_PROP_LOCK, &reposlocktoken,
                              subpool));

      if (reposlocktoken)
        {
          /* Did we get it? If so, we're done. */
          if (strcmp(reposlocktoken->data, mylocktoken->data) == 0)
            return SVN_NO_ERROR;

          /* ...otherwise, tell the user that someone else has the
             lock and sleep before retrying. */
          SVN_ERR(svn_cmdline_printf
                  (pool, _("Failed to get lock on destination "
                           "repos, currently held by '%s'\n"),
                   reposlocktoken->data));
          
          apr_sleep(apr_time_from_sec(1));
        }
      else if (i < LOCK_RETRIES - 1)
        {
          const svn_string_t *unset = NULL;

          /* Except in the very last iteration, try to set the lock. */
          err = svn_ra_change_rev_prop2(session, 0, SVNRDUMP_PROP_LOCK,
                                        be_atomic ? &unset : NULL,
                                        mylocktoken, subpool);

          if (be_atomic && err && is_atomicity_error(err))
            {
              /* Someone else has the lock.  Let's loop. */
              svn_error_clear(err);
            }
          else if (be_atomic && err == SVN_NO_ERROR)
            {
              /* We have the lock.  However, for compatibility with
                 concurrent svnrdumps that don't support atomicity, loop
                 anyway to double-check that they haven't overwritten
                 our lock. */
              continue;
            }
          else
            {
              /* Genuine error, or we aren't atomic and need to loop. */
              SVN_ERR(err);
            }
        }
    }

  return svn_error_createf(APR_EINVAL, NULL,
                           _("Couldn't get lock on destination repos "
                             "after %d attempts"), i);
}

/* Remove the lock on SESSION iff the lock is owned by MYLOCKTOKEN. */
static svn_error_t *
maybe_unlock(svn_ra_session_t *session,
             const svn_string_t *mylocktoken,
             apr_pool_t *scratch_pool)
{
  svn_string_t *reposlocktoken;
  svn_boolean_t be_atomic;

  SVN_ERR(svn_ra_has_capability(session, &be_atomic,
                                SVN_RA_CAPABILITY_ATOMIC_REVPROPS,
                                scratch_pool));
  SVN_ERR(svn_ra_rev_prop(session, 0, SVNRDUMP_PROP_LOCK,
                          &reposlocktoken, scratch_pool));
  if (reposlocktoken && strcmp(reposlocktoken->data, mylocktoken->data) == 0)
    {
      svn_error_t *err =
        svn_ra_change_rev_prop2(session, 0, SVNRDUMP_PROP_LOCK,
                                be_atomic ? &mylocktoken : NULL, NULL,
                                scratch_pool);
      if (is_atomicity_error(err))
        return svn_error_quick_wrap(err, _("svnrdump's lock was stolen; "
                                           "can't remove it"));
    }
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
  rb->pool = svn_pool_create(pool);
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
  rb->revprop_table = apr_hash_make(rb->pool);

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
  const struct svn_delta_editor_t *commit_editor;
  struct node_baton *nb;
  struct revision_baton *rb;
  struct directory_baton *child_db;
  apr_hash_index_t *hi;
  void *child_baton;
  void *commit_edit_baton;
  char *ancestor_path;
  apr_array_header_t *residual_open_path;
  char *relpath_compose;
  const char *nb_dirname;
  apr_size_t residual_close_count;
  int i;

  rb = revision_baton;
  nb = apr_pcalloc(rb->pool, sizeof(*nb));
  nb->rb = rb;

  nb->copyfrom_path = NULL;
  nb->copyfrom_rev = SVN_INVALID_REVNUM;

  commit_editor = rb->pb->commit_editor;
  commit_edit_baton = rb->pb->commit_edit_baton;

  /* If the creation of commit_editor is pending, create it now and
     open_root on it; also create a top-level directory baton. */

  if (!commit_editor)
    {
      /* The revprop_table should have been filled in with important
         information like svn:log in set_revision_property. We can now
         use it all this information to create our commit_editor. But
         first, clear revprops that we aren't allowed to set with the
         commit_editor. We'll set them separately using the RA API
         after closing the editor (see close_revision). */

      apr_hash_set(rb->revprop_table, SVN_PROP_REVISION_AUTHOR,
                   APR_HASH_KEY_STRING, NULL);
      apr_hash_set(rb->revprop_table, SVN_PROP_REVISION_DATE,
                   APR_HASH_KEY_STRING, NULL);

      SVN_ERR(svn_ra_get_commit_editor3(rb->pb->session, &commit_editor,
                                        &commit_edit_baton, rb->revprop_table,
                                        commit_callback, NULL, NULL, FALSE,
                                        rb->pool));

      rb->pb->commit_editor = commit_editor;
      rb->pb->commit_edit_baton = commit_edit_baton;

      SVN_ERR(commit_editor->open_root(commit_edit_baton, rb->rev - 1,
                                       rb->pool, &child_baton));

      LDR_DBG(("Opened root %p\n", child_baton));

      /* child_db corresponds to the root directory baton here */
      child_db = apr_pcalloc(rb->pool, sizeof(*child_db));
      child_db->baton = child_baton;
      child_db->depth = 0;
      child_db->relpath = "";
      child_db->parent = NULL;
      rb->db = child_db;
    }

  for (hi = apr_hash_first(rb->pool, headers); hi; hi = apr_hash_next(hi))
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
        nb->path = apr_pstrdup(rb->pool, hval);
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
      if (strcmp(hname, SVN_REPOS_DUMPFILE_TEXT_DELTA_BASE_MD5) == 0)
        nb->base_checksum = apr_pstrdup(rb->pool, hval);
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV) == 0)
        nb->copyfrom_rev = atoi(hval);
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH) == 0)
        nb->copyfrom_path =
          svn_path_url_add_component2(rb->pb->root_url,
                                      apr_pstrdup(rb->pool, hval),
                                      rb->pool);
    }

  nb_dirname = svn_relpath_dirname(nb->path, pool);
  if (svn_path_compare_paths(nb_dirname,
                             rb->db->relpath) != 0)
    {
      /* Before attempting to handle the action, call open_directory
         for all the path components and set the directory baton
         accordingly */
      ancestor_path =
        svn_relpath_get_longest_ancestor(nb_dirname,
                                         rb->db->relpath, pool);
      residual_close_count =
        svn_path_component_count(svn_relpath_skip_ancestor(ancestor_path,
                                                           rb->db->relpath));
      residual_open_path =
        svn_path_decompose(svn_relpath_skip_ancestor(ancestor_path,
                                                     nb_dirname), pool);

      /* First close all as many directories as there are after
         skip_ancestor, and then open fresh directories */
      for (i = 0; i < residual_close_count; i ++)
        {
          /* Don't worry about destroying the actual rb->db object,
             since the pool we're using has the lifetime of one
             revision anyway */
          LDR_DBG(("Closing dir %p\n", rb->db->baton));
          SVN_ERR(commit_editor->close_directory(rb->db->baton, rb->pool));
          rb->db = rb->db->parent;
        }
        
      for (i = 0; i < residual_open_path->nelts; i ++)
        {
          relpath_compose =
            svn_relpath_join(rb->db->relpath,
                             APR_ARRAY_IDX(residual_open_path, i, const char *),
                             rb->pool);
          SVN_ERR(commit_editor->open_directory(relpath_compose,
                                                rb->db->baton,
                                                rb->rev - 1,
                                                rb->pool, &child_baton));
          LDR_DBG(("Opened dir %p\n", child_baton));
          child_db = apr_pcalloc(rb->pool, sizeof(*child_db));
          child_db->baton = child_baton;
          child_db->depth = rb->db->depth + 1;
          child_db->relpath = relpath_compose;
          child_db->parent = rb->db;
          rb->db = child_db;
        }
    }

  switch (nb->action)
    {
    case svn_node_action_add:
      switch (nb->kind)
        {
        case svn_node_file:
          SVN_ERR(commit_editor->add_file(nb->path, rb->db->baton,
                                          nb->copyfrom_path,
                                          nb->copyfrom_rev,
                                          rb->pool, &(nb->file_baton)));
          LDR_DBG(("Added file %s to dir %p as %p\n", nb->path, rb->db->baton, nb->file_baton));
          break;
        case svn_node_dir:
          SVN_ERR(commit_editor->add_directory(nb->path, rb->db->baton,
                                               nb->copyfrom_path,
                                               nb->copyfrom_rev,
                                               rb->pool, &child_baton));
          LDR_DBG(("Added dir %s to dir %p as %p\n", nb->path, rb->db->baton, child_baton));
          child_db = apr_pcalloc(rb->pool, sizeof(*child_db));
          child_db->baton = child_baton;
          child_db->depth = rb->db->depth + 1;
          child_db->relpath = apr_pstrdup(rb->pool, nb->path);
          child_db->parent = rb->db;
          rb->db = child_db;
          break;
        default:
          break;
        }
      break;
    case svn_node_action_change:
      switch (nb->kind)
        {
        case svn_node_file:
          /* open_file to set the file_baton so we can apply props,
             txdelta to it */
          SVN_ERR(commit_editor->open_file(nb->path, rb->db->baton,
                                           SVN_INVALID_REVNUM, rb->pool,
                                           &(nb->file_baton)));
          break;
        default:
          /* The directory baton has already been set */
          break;
        }
      break;
    case svn_node_action_delete:
      LDR_DBG(("Deleting entry %s in %p\n", nb->path, rb->db->baton));
      SVN_ERR(commit_editor->delete_entry(nb->path, rb->rev,
                                          rb->db->baton, rb->pool));
      break;
    case svn_node_action_replace:
      /* Absent in dumpstream; represented as a delete + add */
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
    apr_hash_set(rb->revprop_table, apr_pstrdup(rb->pool, name),
                 APR_HASH_KEY_STRING, svn_string_dup(value, rb->pool));
  else
    /* Special handling for revision 0; this is safe because the
       commit_editor hasn't been created yet. */
    SVN_ERR(svn_ra_change_rev_prop2(rb->pb->session, rb->rev,
                                    name, NULL, value, rb->pool));

  /* Remember any datestamp/ author that passes through (see comment
     in close_revision). */
  if (!strcmp(name, SVN_PROP_REVISION_DATE))
    rb->datestamp = svn_string_dup(value, rb->pool);
  if (!strcmp(name, SVN_PROP_REVISION_AUTHOR))
    rb->author = svn_string_dup(value, rb->pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property(void *baton,
                  const char *name,
                  const svn_string_t *value)
{
  struct node_baton *nb;
  const struct svn_delta_editor_t *commit_editor;
  apr_pool_t *pool;
  nb = baton;
  commit_editor = nb->rb->pb->commit_editor;
  pool = nb->rb->pool;

  switch (nb->kind)
    {
    case svn_node_file:
      LDR_DBG(("Applying properties on %p\n", nb->file_baton));
      SVN_ERR(commit_editor->change_file_prop(nb->file_baton, name,
                                              value, pool));
      break;
    case svn_node_dir:
      LDR_DBG(("Applying properties on %p\n", nb->rb->db->baton));
      SVN_ERR(commit_editor->change_dir_prop(nb->rb->db->baton, name,
                                             value, pool));
      break;
    default:
      break;
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_node_property(void *baton,
                     const char *name)
{
  struct node_baton *nb;
  const struct svn_delta_editor_t *commit_editor;
  apr_pool_t *pool;
  nb = baton;
  commit_editor = nb->rb->pb->commit_editor;
  pool = nb->rb->pool;

  if (nb->kind == svn_node_file)
    SVN_ERR(commit_editor->change_file_prop(nb->file_baton, name,
                                            NULL, pool));
  else
    SVN_ERR(commit_editor->change_dir_prop(nb->rb->db->baton, name,
                                           NULL, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_node_props(void *baton)
{
  /* ### Not implemented */
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
  pool = nb->rb->pool;
  LDR_DBG(("Applying textdelta to %p\n", nb->file_baton));
  SVN_ERR(commit_editor->apply_textdelta(nb->file_baton, nb->base_checksum,
                                         pool, handler, handler_baton));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_node(void *baton)
{
  struct node_baton *nb;
  const struct svn_delta_editor_t *commit_editor;

  nb = baton;
  commit_editor = nb->rb->pb->commit_editor;

  if (nb->kind == svn_node_file)
    {
      LDR_DBG(("Closing file %p\n", nb->file_baton));
      SVN_ERR(commit_editor->close_file(nb->file_baton, NULL, nb->rb->pool));
    }

  /* The svn_node_dir case is handled in close_revision */

  return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *baton)
{
  struct revision_baton *rb;
  const svn_delta_editor_t *commit_editor;
  void *commit_edit_baton;
  void *child_baton;
  rb = baton;

  commit_editor = rb->pb->commit_editor;
  commit_edit_baton = rb->pb->commit_edit_baton;

  /* Fake revision 0 */
  if (rb->rev == 0)
    /* ### Don't print directly; generate a notification. */
    SVN_ERR(svn_cmdline_printf(rb->pool, "* Loaded revision 0.\n"));
  else if (commit_editor)
    {
      /* Close all pending open directories, and then close the edit
         session itself */
      while (rb->db && rb->db->parent)
        {
          LDR_DBG(("Closing dir %p\n", rb->db->baton));
          SVN_ERR(commit_editor->close_directory(rb->db->baton, rb->pool));
          rb->db = rb->db->parent;
        }
      /* root dir's baton */
      LDR_DBG(("Closing edit on %p\n", commit_edit_baton));
      SVN_ERR(commit_editor->close_directory(rb->db->baton, rb->pool));
      SVN_ERR(commit_editor->close_edit(commit_edit_baton, rb->pool));
    }
  else
    {
      /* Legitimate revision with no node information */
      SVN_ERR(svn_ra_get_commit_editor3(rb->pb->session, &commit_editor,
                                        &commit_edit_baton, rb->revprop_table,
                                        commit_callback, NULL, NULL, FALSE,
                                        rb->pool));

      SVN_ERR(commit_editor->open_root(commit_edit_baton, rb->rev - 1,
                                       rb->pool, &child_baton));

      LDR_DBG(("Opened root %p\n", child_baton));
      LDR_DBG(("Closing edit on %p\n", commit_edit_baton));
      SVN_ERR(commit_editor->close_directory(child_baton, rb->pool));
      SVN_ERR(commit_editor->close_edit(commit_edit_baton, rb->pool));
    }

  /* svn_fs_commit_txn rewrites the datestamp/ author property-
     rewrite it by hand after closing the commit_editor. */
  SVN_ERR(svn_ra_change_rev_prop2(rb->pb->session, rb->rev,
                                  SVN_PROP_REVISION_DATE,
                                  NULL, rb->datestamp, rb->pool));
  SVN_ERR(svn_ra_change_rev_prop2(rb->pb->session, rb->rev,
                                  SVN_PROP_REVISION_AUTHOR,
                                  NULL, rb->author, rb->pool));

  svn_pool_destroy(rb->pool);

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
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *pool)
{
  struct parse_baton *pb = parse_baton;
  const svn_string_t *lock_string;
  svn_boolean_t be_atomic;
  svn_error_t *err;

  SVN_ERR(svn_ra_has_capability(session, &be_atomic,
                                SVN_RA_CAPABILITY_ATOMIC_REVPROPS,
                                pool));
  SVN_ERR(get_lock(&lock_string, session, cancel_func, cancel_baton, pool));
  SVN_ERR(svn_ra_get_repos_root2(session, &(pb->root_url), pool));
  err = svn_repos_parse_dumpstream2(stream, parser, parse_baton,
                                    cancel_func, cancel_baton, pool);

  /* If all goes well, or if we're cancelled cleanly, don't leave a
     stray lock behind. */
  if ((! err) 
      || (err && (err->apr_err == SVN_ERR_CANCELLED)))
    err = svn_error_compose_create(maybe_unlock(session, lock_string, pool),
                                   err);
  return err;
}
