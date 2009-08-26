/*
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

#include "obliterate.h"

#include "svn_types.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_delta.h"
#include "svn_cmdline.h"

#include "svn_private_config.h"


/*** Obliteration Set Manipulation ***/

void
svnsync_add_obliteration_spec(svnsync_obliteration_set_t **obliteration_set,
                              const char *node_rev,
                              apr_pool_t *pool)
{
  if (! *obliteration_set)
    *obliteration_set = apr_array_make(pool, 1, sizeof(node_rev));
  APR_ARRAY_PUSH(*obliteration_set, const char *) = node_rev;
}

/* Return TRUE iff OBLITERATION_SET says we should obliterate changes to PATH
 * in REVISION. PATH is repository-relative and does not start with "/".
 *
 * ### Impl. note: Presently just uses a string prefix match which is not
 * correct, only suitable for simple experimentation. (Pattern "trunk/a" would
 * match "trunk/afile"; "trunk@30" would match "trunk@300".)
 */
static svn_boolean_t
match_obliteration_spec(const svnsync_obliteration_set_t *obliteration_set,
                        const char *path,
                        svn_revnum_t revision,
                        apr_pool_t *scratch_pool)
{
  int i;
  const char *node_rev = apr_psprintf(scratch_pool, "%s@%ld", path, revision);

  for (i = 0; i < obliteration_set->nelts; i++)
    {
      const char *oblit_spec = APR_ARRAY_IDX(obliteration_set, i,
                                             const char *);
      /* If specified obliteration string matches beginning of this node-rev string */
      if (strncmp(oblit_spec, node_rev, strlen(oblit_spec)) == 0)
        {
          printf("## Omitting changes in '%s'\n", node_rev);
          return TRUE;
        }
    }
  return FALSE;
}


/* A txdelta window handler that throws away the incoming delta. */
static svn_error_t *
oblit_txdelta_window_handler(svn_txdelta_window_t *window,
                             void *baton)
{
  /* Ignore the data. */
  return SVN_NO_ERROR;
}


/*** Obliteration Editor ***/

/* This editor has the following jobs:
 *
 * Pass on all changes to the wrapped editor, except where obliteration is
 * required.
 *
 * Obliterate as follows (for the time being):
 *
 *   * Omit changes to file content and (file and dir) properties of nodes that
 *     are specified by the obliteration set.
 *
 * ### TODO: If we simply omit a change in one revision, but don't want to
 * obliterate changes in subsequent revisions of the same node, those
 * subsequent changes won't apply correctly. We need to remember the changes
 * that we omitted, and apply them in the next revision instead.
 *
 * ### TODO: Obliteration should also affect add_file, add_directory and
 * delete_entry.
 *
 * If we obliterate all of the changes in a revision, we still need to commit
 * the resulting empty revision.
 *
 * If we read an empty revision (perhaps due to a previous obliteration, or due
 * to authz restrictions), we still need to commit the resulting empty
 * revision, and this requires a little extra housekeeping: see
 * called_open_root and close_edit().
 */


/* Edit baton */
typedef struct {
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_edit_baton;
  svnsync_obliteration_set_t *obliteration_set;  /* node-revs to omit */
  svn_boolean_t called_open_root;
  svn_revnum_t base_revision;
  svn_boolean_t quiet;
} edit_baton_t;


/* A dual-purpose baton for files and directories. */
typedef struct {
  void *edit_baton;
  void *wrapped_node_baton;
  svn_boolean_t omit_changes;  /* are we obliterating changes to this node? */
} node_baton_t;


/* Return TRUE iff the obliteration set says we should obliterate changes to
 * the node-rev PATH in the current revision (EB->base_revision + 1). */
static svn_boolean_t
should_omit_changes_in(const char *path,
                       const edit_baton_t *eb,
                       apr_pool_t *scratch_pool)
{
  return match_obliteration_spec(eb->obliteration_set, path,
                                 eb->base_revision + 1, scratch_pool);
}


/*** Editor vtable functions ***/

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;
  return eb->wrapped_editor->set_target_revision(eb->wrapped_edit_baton,
                                                 target_revision, pool);
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  edit_baton_t *eb = edit_baton;
  node_baton_t *dir_baton = apr_palloc(pool, sizeof(*dir_baton));

  SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                        base_revision, pool,
                                        &dir_baton->wrapped_node_baton));

  eb->called_open_root = TRUE;
  dir_baton->edit_baton = edit_baton;
  *root_baton = dir_baton;

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;

  return eb->wrapped_editor->delete_entry(path, base_revision,
                                          pb->wrapped_node_baton, pool);
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **child_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(eb->wrapped_editor->add_directory(path, pb->wrapped_node_baton,
                                            copyfrom_path,
                                            copyfrom_rev, pool,
                                            &b->wrapped_node_baton));

  b->edit_baton = eb;
  b->omit_changes = should_omit_changes_in(path, eb, pool);
  *child_baton = b;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *db = apr_palloc(pool, sizeof(*db));

  SVN_ERR(eb->wrapped_editor->open_directory(path, pb->wrapped_node_baton,
                                             base_revision, pool,
                                             &db->wrapped_node_baton));

  db->edit_baton = eb;
  db->omit_changes = should_omit_changes_in(path, eb, pool);
  *child_baton = db;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *fb = apr_palloc(pool, sizeof(*fb));

  SVN_ERR(eb->wrapped_editor->add_file(path, pb->wrapped_node_baton,
                                       copyfrom_path, copyfrom_rev,
                                       pool, &fb->wrapped_node_baton));

  fb->edit_baton = eb;
  fb->omit_changes = FALSE /* should_omit_changes_in(path, eb, pool) */;
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *fb = apr_palloc(pool, sizeof(*fb));

  SVN_ERR(eb->wrapped_editor->open_file(path, pb->wrapped_node_baton,
                                        base_revision, pool,
                                        &fb->wrapped_node_baton));

  fb->edit_baton = eb;
  fb->omit_changes = should_omit_changes_in(path, eb, pool);
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

  if (fb->omit_changes && ! eb->quiet)
    {
      /* Assuming that the wrapped editor is printing "." for each file's
       * text delta it transmits, insert an "O" for each one we omit. */
      /* ### If the first one in this edit is "O", it will print before
       * "Transmitting file changes ". */
      SVN_ERR(svn_cmdline_printf(pool, "O"));
      SVN_ERR(svn_cmdline_fflush(stdout));
    }

  if (fb->omit_changes)
    {
      *handler = oblit_txdelta_window_handler;
    }
  else
    SVN_ERR(eb->wrapped_editor->apply_textdelta(fb->wrapped_node_baton,
                                                base_checksum, pool,
                                                handler, handler_baton));
  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

  if (fb->omit_changes)
    {
      /* We altered the content so the checksum won't be as expected. */
      text_checksum = NULL;
    }
  return eb->wrapped_editor->close_file(fb->wrapped_node_baton,
                                        text_checksum, pool);
}

static svn_error_t *
absent_file(const char *path,
            void *file_baton,
            apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;
  return eb->wrapped_editor->absent_file(path, fb->wrapped_node_baton, pool);
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
  return eb->wrapped_editor->close_directory(db->wrapped_node_baton, pool);
}

static svn_error_t *
absent_directory(const char *path,
                 void *dir_baton,
                 apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
  return eb->wrapped_editor->absent_directory(path, db->wrapped_node_baton,
                                              pool);
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

  /* ### Only regular properties can pass over libsvn_ra */
  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  if (fb->omit_changes)
    {
      /* do nothing */
    }
  else
    SVN_ERR(eb->wrapped_editor->change_file_prop(fb->wrapped_node_baton,
                                              name, value, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;

  /* ### Only regular properties can pass over libsvn_ra */
  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  if (db->omit_changes)
    {
      /* do nothing */
    }
  else
    SVN_ERR(eb->wrapped_editor->change_dir_prop(db->wrapped_node_baton,
                                             name, value, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;

  /* If we haven't opened the root yet, that means we're transfering
     an empty revision, probably because we aren't allowed to see the
     contents for some reason.  In any event, we need to open the root
     and close it again, before we can close out the edit, or the
     commit will fail. */

  if (! eb->called_open_root)
    {
      void *baton;
      SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                            eb->base_revision, pool,
                                            &baton));
      SVN_ERR(eb->wrapped_editor->close_directory(baton, pool));
    }

  return eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool);
}

static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;
  return eb->wrapped_editor->abort_edit(eb->wrapped_edit_baton, pool);
}



/*** Editor factory function ***/

svn_error_t *
svnsync_get_obliterate_editor(const svn_delta_editor_t *wrapped_editor,
                              void *wrapped_edit_baton,
                              svn_revnum_t base_revision,
                              svnsync_obliteration_set_t *obliteration_set,
                              svn_boolean_t quiet,
                              const svn_delta_editor_t **editor,
                              void **edit_baton,
                              apr_pool_t *pool)
{
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(pool);
  edit_baton_t *eb = apr_pcalloc(pool, sizeof(*eb));

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->absent_directory = absent_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->absent_file = absent_file;
  tree_editor->close_edit = close_edit;
  tree_editor->abort_edit = abort_edit;

  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->base_revision = base_revision;
  eb->obliteration_set = obliteration_set;
  eb->quiet = quiet;

  *editor = tree_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}

