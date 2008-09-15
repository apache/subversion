/*
 * repos_diff_summarize.c -- The diff summarize editor for summarizing
 * the differences of two repository versions
 *
 * ====================================================================
 * Copyright (c) 2005-2006 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */


#include "svn_props.h"
#include "svn_pools.h"

#include "client.h"


/* Overall crawler editor baton.  */
struct edit_baton {
  /* The target of the diff, relative to the root of the edit */
  const char *target;

  /* The summarize callback passed down from the API */
  svn_client_diff_summarize_func_t summarize_func;

  /* The summarize callback baton */
  void *summarize_func_baton;

  /* An RA session used to check the kind of deleted paths */
  svn_ra_session_t *ra_session;

  /* The start revision for the comparison */
  svn_revnum_t revision;
};


/* Item baton. */
struct item_baton {
  /* The overall crawler editor baton */
  struct edit_baton *edit_baton;

  /* The summarize filled by the editor calls, NULL if this item hasn't
     been modified (yet) */
  svn_client_diff_summarize_t *summarize;

  /* The path of the file or directory within the repository */
  const char *path;

  /* The kind of this item */
  svn_node_kind_t node_kind;

  /* The file/directory pool */
  apr_pool_t *item_pool;
};


/* Create an item baton, with the fields initialized to EDIT_BATON, PATH,
 * NODE_KIND and POOL, respectively.  Allocate the returned structure in POOL.
 */
static struct item_baton *
create_item_baton(struct edit_baton *edit_baton,
                  const char *path,
                  svn_node_kind_t node_kind,
                  apr_pool_t *pool)
{
  struct item_baton *b = apr_pcalloc(pool, sizeof(*b));

  b->edit_baton = edit_baton;
  /* Issue #2765: b->path is supposed to be relative to the target.
     If the target is a file, just use an empty path.  This way the
     receiver can just concatenate this path to the original path
     without doing any extra checks. */
  if (node_kind == svn_node_file && strcmp(path, edit_baton->target) == 0)
    b->path =  "";
  else
    b->path = apr_pstrdup(pool, path);
  b->node_kind = node_kind;
  b->item_pool = pool;

  return b;
}

/* Make sure that this item baton contains a summarize struct.
 * If it doesn't before this call, allocate a new struct in the item's pool,
 * initializing the diff kind to normal.
 * All other fields are also initialized from IB to to NULL/invalid values. */
static void
ensure_summarize(struct item_baton *ib)
{
  svn_client_diff_summarize_t *sum;
  if (ib->summarize)
    return;

  sum = apr_pcalloc(ib->item_pool, sizeof(*sum));

  sum->node_kind = ib->node_kind;
  sum->summarize_kind = svn_client_diff_summarize_kind_normal;
  sum->path = ib->path;

  ib->summarize = sum;
}


/* An editor function. The root of the comparison hierarchy */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct item_baton *ib = create_item_baton(edit_baton, "",
                                            svn_node_dir, pool);

  *root_baton = ib;
  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct item_baton *ib = parent_baton;
  struct edit_baton *eb = ib->edit_baton;
  svn_client_diff_summarize_t *sum;
  svn_node_kind_t kind;

  /* We need to know if this is a directory or a file */
  SVN_ERR(svn_ra_check_path(eb->ra_session,
                            path,
                            eb->revision,
                            &kind,
                            pool));

  sum = apr_pcalloc(pool, sizeof(*sum));
  sum->summarize_kind = svn_client_diff_summarize_kind_deleted;
  sum->path = path;
  sum->node_kind = kind;

  return eb->summarize_func(sum, eb->summarize_func_baton, pool);
}

/* An editor function.  */
static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **child_baton)
{
  struct item_baton *pb = parent_baton;
  struct item_baton *cb;

  cb = create_item_baton(pb->edit_baton, path, svn_node_dir, pool);
  ensure_summarize(cb);
  cb->summarize->summarize_kind = svn_client_diff_summarize_kind_added;

  *child_baton = cb;
  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct item_baton *pb = parent_baton;
  struct item_baton *cb;

  cb = create_item_baton(pb->edit_baton, path, svn_node_dir, pool);

  *child_baton = cb;
  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct item_baton *ib = dir_baton;
  struct edit_baton *eb = ib->edit_baton;

  if (ib->summarize)
    SVN_ERR(eb->summarize_func(ib->summarize, eb->summarize_func_baton,
                               pool));

  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  struct item_baton *pb = parent_baton;
  struct item_baton *cb;

  cb = create_item_baton(pb->edit_baton, path, svn_node_file, pool);
  ensure_summarize(cb);
  cb->summarize->summarize_kind = svn_client_diff_summarize_kind_added;

  *file_baton = cb;
  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct item_baton *pb = parent_baton;
  struct item_baton *cb;

  cb = create_item_baton(pb->edit_baton, path, svn_node_file, pool);

  *file_baton = cb;
  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct item_baton *ib = file_baton;

  ensure_summarize(ib);
  if (ib->summarize->summarize_kind == svn_client_diff_summarize_kind_normal)
    ib->summarize->summarize_kind = svn_client_diff_summarize_kind_modified;

  *handler = svn_delta_noop_window_handler;
  *handler_baton = NULL;

  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct item_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  if (fb->summarize)
    SVN_ERR(eb->summarize_func(fb->summarize, eb->summarize_func_baton,
                               pool));

  return SVN_NO_ERROR;
}


/* An editor function, implementing both change_file_prop and
 * change_dir_prop.  */
static svn_error_t *
change_prop(void *entry_baton,
            const char *name,
            const svn_string_t *value,
            apr_pool_t *pool)
{
  struct item_baton *ib = entry_baton;

  if (svn_property_kind(NULL, name) == svn_prop_regular_kind)
    {
      ensure_summarize(ib);
      ib->summarize->prop_changed = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Create a repository diff summarize editor and baton.  */
svn_error_t *
svn_client__get_diff_summarize_editor(const char *target,
                                      svn_client_diff_summarize_func_t
                                      summarize_func,
                                      void *item_baton,
                                      svn_ra_session_t *ra_session,
                                      svn_revnum_t revision,
                                      svn_cancel_func_t cancel_func,
                                      void *cancel_baton,
                                      const svn_delta_editor_t **editor,
                                      void **edit_baton,
                                      apr_pool_t *pool)
{
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(pool);
  struct edit_baton *eb = apr_palloc(pool, sizeof(*eb));

  eb->target = target;
  eb->summarize_func = summarize_func;
  eb->summarize_func_baton = item_baton;
  eb->ra_session = ra_session;
  eb->revision = revision;

  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_prop;
  tree_editor->close_directory = close_directory;

  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_prop;
  tree_editor->close_file = close_file;

  return svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                           tree_editor, eb, editor, edit_baton,
                                           pool);
}
