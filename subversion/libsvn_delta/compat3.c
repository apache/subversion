/*
 * compat3.c : Ev3-to-Ev1 compatibility.
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

#include <stddef.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "private/svn_delta_private.h"
#include "svn_private_config.h"


/* Verify EXPR is true; raise an error if not. */
#define VERIFY(expr) SVN_ERR_ASSERT(expr);

/* Features that are not wanted for this commit editor shim but may be
 * wanted in a similar but different shim such as for an update editor. */
/* #define SHIM_WITH_ADD_ABSENT */
/* #define SHIM_WITH_UNLOCK */


/*
 * ===================================================================
 * Commit Editor converter to join a v3 driver to a v1 consumer
 * ===================================================================
 *
 * This editor buffers all the changes before driving the Ev1 at the end,
 * since it needs to do a single depth-first traversal of the path space
 * and this cannot be started until all moves are known.
 *
 * Moves are converted to copy-and-delete, with the copy being from
 * the source peg rev. (### Should it request copy-from revision "-1"?)
 */

/* TODO
 *
 * ### Have we got our rel-paths in order? Ev1, Ev3 and callbacks may
 *     all expect different paths. 'repos_relpath' or relative to
 *     eb->base_relpath? Leading slash (unimplemented 'send_abs_paths'
 *     feature), etc.
 *
 * ### Prop-fetch callback fails to use the copy-from source for a child
 *     of a copy: see drive_ev1_props().
 *
 * ### May be tidier for OPEN_ROOT_FUNC callback (see open_root_ev3())
 *     not to actually open the root in advance, but instead just to
 *     remember the base revision that the driver wants us to specify
 *     when we do open it.
 */


/*
 * ========================================================================
 * Buffering the Delta Editor Changes
 * ========================================================================
 */

/* The kind of Ev1 restructuring operation on a particular path. For each
 * visited path we use exactly one restructuring action. */
enum restructure_action_t
{
  RESTRUCTURE_NONE = 0,
  RESTRUCTURE_ADD,         /* add the node, maybe replacing. maybe copy  */
#ifdef SHIM_WITH_ADD_ABSENT
  RESTRUCTURE_ADD_ABSENT,  /* add an absent node, possibly replacing  */
#endif
  RESTRUCTURE_DELETE       /* delete this node  */
};

/* Records everything about how this node is to be changed, from an Ev1
 * point of view.  */
typedef struct change_node_t
{
  /* what kind of (tree) restructure is occurring at this node?  */
  enum restructure_action_t action;

  svn_node_kind_t kind;  /* the NEW kind of this node  */

  /* We need two revisions: one to specify the revision we are altering,
     and a second to specify the revision to delete/replace. These are
     mutually exclusive, but they need to be separate to ensure we don't
     confuse the operation on this node. For example, we may delete a
     node and replace it we use DELETING for REPLACES_REV, and ignore
     the value placed into CHANGING when properties were set/changed
     on the new node. Or we simply change a node (setting CHANGING),
     and DELETING remains SVN_INVALID_REVNUM, indicating we are not
     attempting to replace a node.  */
  svn_revnum_t changing_rev;
  svn_revnum_t deleting_rev;

  /* new/final set of props to apply; null => no *change*, not no props */
  apr_hash_t *props;

  svn_stream_t *text_stream;  /* new fulltext; null => no change  */
  const svn_checksum_t *text_checksum;  /* checksum of new fulltext  */

  /* If COPYFROM_PATH is not NULL, then copy PATH@REV to this node.
     RESTRUCTURE must be RESTRUCTURE_ADD.  */
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;

#ifdef SHIM_WITH_UNLOCK
  /* Record whether an incoming propchange unlocked this node.  */
  svn_boolean_t unlock;
#endif
} change_node_t;

/* Find the change in CHANGES whose path is the longest out of TARGET_PATH
 * and its ancestors.
 *
 * Always return a non-null change: if no change is found, return a dummy
 * no-tree-change with no other information.
 */
static change_node_t *
find_nearest_change(apr_hash_t *changes,
                    const char *target_path)
{
  apr_pool_t *pool = apr_hash_pool_get(changes);
  const char *nearest_change_path = "";
  change_node_t *nearest_change;
  apr_hash_index_t *hi;

  /* find the change with the longest matching prefix of TARGET_PATH */
  for (hi = apr_hash_first(pool, changes);
       hi; hi = apr_hash_next(hi))
    {
      const char *this_relpath = svn__apr_hash_index_key(hi);
      const char *r = svn_relpath_skip_ancestor(this_relpath, target_path);

      if (r && strlen(this_relpath) > strlen(nearest_change_path))
        {
          nearest_change_path = apr_pstrdup(pool, this_relpath);
          /* stop looking for a longer match if this is an exact match */
          if (r[0] == '\0')
            break;
        }
    }
  nearest_change = svn_hash_gets(changes, nearest_change_path);
  if (! nearest_change)
    {
      /* Create a new, dummy change. */
      nearest_change = apr_pcalloc(pool, sizeof(*nearest_change));
      nearest_change->action = RESTRUCTURE_NONE;
      nearest_change->changing_rev = SVN_INVALID_REVNUM;
      nearest_change->deleting_rev = SVN_INVALID_REVNUM;
    }
  return nearest_change;
}

/* Check whether RELPATH is known to exist, known to not exist, or unknown. */
static svn_tristate_t
check_existence(apr_hash_t *changes,
                const char *relpath)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  apr_pool_t *scratch_pool = changes_pool;
  change_node_t *change = svn_hash_gets(changes, relpath);
  svn_tristate_t exists = svn_tristate_unknown;

  if (change && change->action != RESTRUCTURE_DELETE)
    exists = svn_tristate_true;
  else if (change && change->action == RESTRUCTURE_DELETE)
    exists = svn_tristate_false;
  else
    {
      const char *parent_path = svn_relpath_dirname(relpath, scratch_pool);

      change = find_nearest_change(changes, parent_path);
      if (change->action == RESTRUCTURE_DELETE)
        exists = svn_tristate_false;
      else if (change->action == RESTRUCTURE_ADD && !change->copyfrom_path)
        exists = svn_tristate_false;
    }

  return exists;
}

/* Insert a new change for RELPATH, or return an existing one.
 *
 * Check that this change makes sense on top of previous changes; return
 * an error if not.
 *
 * If the new op is a delete, then any previous explicit deletes inside
 * that subtree are elided. (Other changes inside that subtree are not
 * allowed.) The result of this we do not need to store multiple
 * restructure ops per path even with nested moves.
 */
static svn_error_t *
insert_change(change_node_t **change_p, apr_hash_t *changes,
              const char *relpath,
              enum restructure_action_t action)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  apr_pool_t *scratch_pool = changes_pool;
  change_node_t *change = svn_hash_gets(changes, relpath);
  const char *parent_path = svn_relpath_dirname(relpath, scratch_pool);
  svn_tristate_t exists = check_existence(changes, relpath);

  /* Check whether this op is allowed. */
  switch (action)
    {
    case RESTRUCTURE_NONE:
      if (change)
        {
          /* A no-restructure change is allowed after add, but not allowed
           * (in Ev3) after another no-restructure change, nor a delete. */
          VERIFY(change->action == RESTRUCTURE_ADD);
        }
      else
        {
          /* Disallow if path is known to be non-existent. */
          VERIFY(exists != svn_tristate_false);
        }
      break;

    case RESTRUCTURE_ADD:
      if (change)
        {
          /* Add or copy is allowed after delete (and replaces the delete),
           * but not allowed after an add or a no-restructure change. */
          VERIFY(change->action == RESTRUCTURE_DELETE);
          change->action = action;
        }
      else
        {
          /* Disallow if *parent* path is known to be non-existent
           * (deleted (root or child), or child of a non-copy add). */
          VERIFY(check_existence(changes, parent_path) != svn_tristate_false);
        }
      break;

#ifdef SHIM_WITH_ADD_ABSENT
    case RESTRUCTURE_ADD_ABSENT:
      /* ### */
      break;
#endif

    case RESTRUCTURE_DELETE:
      /* Delete is allowed on a copy-child, and also after add (if that
       * was a move and this is a move), but not after delete. Delete
       * is not allowed if there are non-delete changes to child paths;
       * if there are such changes it elides them.
       */
      if (change)
        {
          VERIFY(change->action != RESTRUCTURE_DELETE);
        }
      else
        {
          /* Disallow if path is known to be non-existent. */
          VERIFY(exists != svn_tristate_false);
        }
      /* Validate that all child ops are deletes, and elide them. */
      {
        apr_hash_index_t *hi;

        for (hi = apr_hash_first(scratch_pool, changes);
             hi; hi = apr_hash_next(hi))
          {
            const char *this_relpath = svn__apr_hash_index_key(hi);
            change_node_t *this_change = svn__apr_hash_index_val(hi);
            const char *r = svn_relpath_skip_ancestor(relpath, this_relpath);

            if (r && r[0])
              {
                VERIFY(this_change->action == RESTRUCTURE_DELETE);
                svn_hash_sets(changes, this_relpath, NULL);
              }
          }
      }

      break;
    }

  if (change)
    {
      if (action != RESTRUCTURE_NONE)
        {
          change->action = action;
        }
    }
  else
    {
      /* Create a new change. Callers will set the other fields as needed. */
      change = apr_pcalloc(changes_pool, sizeof(*change));
      change->action = action;
      change->changing_rev = SVN_INVALID_REVNUM;
      change->deleting_rev = SVN_INVALID_REVNUM;

      svn_hash_sets(changes, apr_pstrdup(changes_pool, relpath), change);
    }

  *change_p = change;
  return SVN_NO_ERROR;
}

/* Duplicate any child changes from the subtree under (but excluding)
 * FROM_PATH into the subtree under (but excluding) NEW_PATH. */
static svn_error_t *
duplicate_child_changes(apr_hash_t *changes,
                        const char *from_path,
                        const char *new_path,
                        apr_pool_t *scratch_pool)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  apr_hash_index_t *hi;

  /* for each change ... */
  for (hi = apr_hash_first(scratch_pool, changes);
       hi; hi = apr_hash_next(hi))
    {
      const char *this_path = svn__apr_hash_index_key(hi);
      change_node_t *this_change = svn__apr_hash_index_val(hi);
      const char *r = svn_relpath_skip_ancestor(from_path, this_path);

      /* ... at a child path strictly below FROM_PATH ... */
      if (r && r[0])
        {
          const char *new_child_path
            = svn_relpath_join(new_path, r, changes_pool);

          /* ... duplicate that change as a child of NEW_PATH */
          svn_hash_sets(changes, new_child_path, this_change);
        }
    }

  return SVN_NO_ERROR;
}

/* Record a move of a subtree from INITIAL_RELPATH to CURRENT_RELPATH.
 */
static void
record_move(apr_hash_t *moves,
            const char *initial_relpath,
            const char *current_relpath)
{
  apr_pool_t *pool = apr_hash_pool_get(moves);

  svn_hash_sets(moves, apr_pstrdup(pool, initial_relpath),
                apr_pstrdup(pool, current_relpath));
}

/* Return the path to which INITIAL_RELPATH would be moved, according to
 * the information in MOVES. Return INITIAL_RELPATH unchanged if it would
 * not be moved.
 */
static const char *
find_move(apr_hash_t *moves,
          const char *initial_relpath,
          apr_pool_t *scratch_pool)
{
  const char *p = initial_relpath;
  apr_hash_index_t *hi;

  /* follow moves: find the longest matching prefix */
  for (hi = apr_hash_first(scratch_pool, moves);
       hi; hi = apr_hash_next(hi))
    {
      const char *this_from_relpath = svn__apr_hash_index_key(hi);
      const char *this_to_relpath = svn__apr_hash_index_val(hi);
      const char *r
        = svn_relpath_skip_ancestor(this_from_relpath, initial_relpath);

      if (r)
        {
          p = svn_relpath_join(this_to_relpath, r, scratch_pool);
          /* look for a longer match unless we found an exact match */
          if (r[0] == '\0')
            break;
        }
    }

  return p;
}


/*
 * ========================================================================
 * Driving the Delta Editor
 * ========================================================================
 */

/* Information needed for driving the delta editor. */
typedef struct ev3_from_delta_baton_t
{
  /* The Ev1 "delta editor" */
  const svn_delta_editor_t *deditor;
  void *dedit_baton;

  /* Callbacks */
  svn_delta_fetch_kind_func_t fetch_kind_func;
  void *fetch_kind_baton;
  svn_delta_fetch_props_func_t fetch_props_func;
  void *fetch_props_baton;

  /* The Ev1 root directory baton if we have opened the root, else null. */
  void *ev1_root_dir_baton;

  /*svn_boolean_t *make_abs_paths;*/

  /* Repository root URL
     ### Some code allows this to be null -- but is that valid? */
  const char *repos_root_url;
  /* Path of the root of the edit, relative to the repository root. */
  const char *base_relpath;

  /* Ev1 changes recorded so far: REPOS_RELPATH -> change_node_ev3_t */
  apr_hash_t *changes;

  /* Moves recorded so far: from_relpath -> (char *)to_relpath. */
  apr_hash_t *moves;

  apr_pool_t *edit_pool;
} ev3_from_delta_baton_t;

/* Get all the (Ev1) paths that have changes. */
static const apr_array_header_t *
get_unsorted_paths(apr_hash_t *changes,
                   const char *base_relpath,
                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *paths
    = apr_array_make(scratch_pool, apr_hash_count(changes), sizeof(char *));
  apr_hash_index_t *hi;

  /* Build a new array with just the paths, trimmed to relative paths for
     the Ev1 drive.  */
  for (hi = apr_hash_first(scratch_pool, changes); hi; hi = apr_hash_next(hi))
    {
      const char *this_path = svn__apr_hash_index_key(hi);

      APR_ARRAY_PUSH(paths, const char *)
        = svn_relpath_skip_ancestor(base_relpath, this_path);
    }

  return paths;
}

/*  */
static svn_error_t *
open_root_ev3(void *baton,
              svn_revnum_t base_revision)
{
  ev3_from_delta_baton_t *eb = baton;

  SVN_ERR(eb->deditor->open_root(eb->dedit_baton, base_revision,
                                 eb->edit_pool, &eb->ev1_root_dir_baton));
  return SVN_NO_ERROR;
}

/* Send property changes to Ev1 for the CHANGE at REPOS_RELPATH.
 *
 * Ev1 requires exactly one prop-change call for each prop whose value
 * has changed. Therefore we *have* to fetch the original props from the
 * peg revision and calculate the changes.
 *
 * ### BUG: For a child-of-copy, the 'copyfrom_*' fields are not currently
 *     set, and so we will diff against the wrong source. Either set the
 *     fields whenever recording a change inside a copy, but that would
 *     trigger an unnecessary nested copy; or here go looking for if we're
 *     inside a copy whenever we need to locate the 'source' properties.
 */
static svn_error_t *
drive_ev1_props(const char *repos_relpath,
                const change_node_t *change,
                const svn_delta_editor_t *deditor,
                void *node_baton,
                svn_delta_fetch_props_func_t fetch_props_func,
                void *fetch_props_baton,
                apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *old_props;
  apr_array_header_t *propdiffs;
  int i;

  /* If there are no properties to install, then just exit.  */
  if (change->props == NULL)
    return SVN_NO_ERROR;

  if (change->copyfrom_path)
    {
      /* The pristine properties are from the copy/move source.  */
      SVN_ERR(fetch_props_func(&old_props, fetch_props_baton,
                               change->copyfrom_path,
                               change->copyfrom_rev,
                               scratch_pool, iterpool));
    }
  else if (change->action == RESTRUCTURE_ADD)
    {
      /* Locally-added nodes have no pristine properties.

         Note: we can use iterpool; this hash only needs to survive to
         the propdiffs call, and there are no contents to preserve.  */
      old_props = apr_hash_make(iterpool);
    }
  else
    {
      /* Fetch the pristine properties for whatever we're editing.  */
      SVN_ERR(fetch_props_func(&old_props, fetch_props_baton,
                               repos_relpath, change->changing_rev,
                               scratch_pool, iterpool));
    }

  SVN_ERR(svn_prop_diffs(&propdiffs, change->props, old_props, scratch_pool));

  /* Apply property changes. These should be changes against the empty set
     for a new node, or changes against the source node for a copied node.

     ### Are they in fact changes against the appropriate base?
   */
  for (i = 0; i < propdiffs->nelts; i++)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(propdiffs, i, svn_prop_t);

      svn_pool_clear(iterpool);

      if (change->kind == svn_node_dir)
        SVN_ERR(deditor->change_dir_prop(node_baton,
                                         prop->name, prop->value,
                                         iterpool));
      else
        SVN_ERR(deditor->change_file_prop(node_baton,
                                          prop->name, prop->value,
                                          iterpool));
    }

#ifdef SHIM_WITH_UNLOCK
  /* Handle the funky unlock protocol. Note: only possibly on files.  */
  if (change->unlock)
    {
      SVN_ERR_ASSERT(change->kind == svn_node_file);
      SVN_ERR(deditor->change_file_prop(node_baton,
                                            SVN_PROP_ENTRY_LOCK_TOKEN, NULL,
                                            iterpool));
    }
#endif

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Drive the Ev1 editor with the change recorded in EB->changes for the
 * path EV1_RELPATH (which is relative to EB->base_relpath).
 *
 * Conforms to svn_delta_path_driver_cb_func_t.
 */
static svn_error_t *
apply_change(void **dir_baton,
             void *parent_baton,
             void *callback_baton,
             const char *ev1_relpath,
             apr_pool_t *result_pool)
{
  apr_pool_t *scratch_pool = result_pool;  /* ### fix this?  */
  const ev3_from_delta_baton_t *eb = callback_baton;
  const char *relpath = svn_relpath_join(eb->base_relpath, ev1_relpath,
                                         scratch_pool);
  const change_node_t *change = svn_hash_gets(eb->changes, relpath);
  void *file_baton = NULL;

  /* The callback should only be called for paths in CHANGES.  */
  SVN_ERR_ASSERT(change != NULL);

  /* Typically, we are not creating new directory batons.  */
  *dir_baton = NULL;

  /* Are we editing the root of the tree?  */
  if (parent_baton == NULL)
    {
      /* The root dir was already opened. */
      *dir_baton = eb->ev1_root_dir_baton;

      /* Only property edits are allowed on the root.  */
      SVN_ERR_ASSERT(change->action == RESTRUCTURE_NONE);
      SVN_ERR(drive_ev1_props(relpath, change,
                              eb->deditor, *dir_baton,
                              eb->fetch_props_func, eb->fetch_props_baton,
                              scratch_pool));

      /* No further action possible for the root.  */
      return SVN_NO_ERROR;
    }

  if (change->action == RESTRUCTURE_DELETE)
    {
      SVN_ERR(eb->deditor->delete_entry(ev1_relpath, change->deleting_rev,
                                        parent_baton, scratch_pool));

      /* No futher action possible for this node.  */
      return SVN_NO_ERROR;
    }

  /* If we're not deleting this node, then we should know its kind.  */
  SVN_ERR_ASSERT(change->kind != svn_node_unknown);

#ifdef SHIM_WITH_ADD_ABSENT
  if (change->action == RESTRUCTURE_ADD_ABSENT)
    {
      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->absent_directory(ev1_relpath, parent_baton,
                                              scratch_pool));
      else
        SVN_ERR(eb->deditor->absent_file(ev1_relpath, parent_baton,
                                         scratch_pool));

      /* No further action possible for this node.  */
      return SVN_NO_ERROR;
    }
#endif
  /* RESTRUCTURE_NONE or RESTRUCTURE_ADD  */

  if (change->action == RESTRUCTURE_ADD)
    {
      const char *copyfrom_url = NULL;
      svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;

      /* Do we have an old node to delete first? If so, delete it. */
      if (SVN_IS_VALID_REVNUM(change->deleting_rev))
        SVN_ERR(eb->deditor->delete_entry(ev1_relpath, change->deleting_rev,
                                          parent_baton, scratch_pool));

      /* If it's a copy, determine the copy source location. */
      if (change->copyfrom_path)
        {
          /* ### What's this about URL vs. fspath? REPOS_ROOT_URL isn't
             optional, is it, at least in a commit editor? */
          if (eb->repos_root_url)
            copyfrom_url = svn_path_url_add_component2(eb->repos_root_url,
                                                       change->copyfrom_path,
                                                       scratch_pool);
          else
            {
              copyfrom_url = change->copyfrom_path;

              /* Make this an FS path by prepending "/" */
              if (copyfrom_url[0] != '/')
                copyfrom_url = apr_pstrcat(scratch_pool, "/",
                                           copyfrom_url, SVN_VA_NULL);
            }

          copyfrom_rev = change->copyfrom_rev;
        }

      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->add_directory(ev1_relpath, parent_baton,
                                           copyfrom_url, copyfrom_rev,
                                           result_pool, dir_baton));
      else
        SVN_ERR(eb->deditor->add_file(ev1_relpath, parent_baton,
                                      copyfrom_url, copyfrom_rev,
                                      result_pool, &file_baton));
    }
  else
    {
      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->open_directory(ev1_relpath, parent_baton,
                                            change->changing_rev,
                                            result_pool, dir_baton));
      else
        SVN_ERR(eb->deditor->open_file(ev1_relpath, parent_baton,
                                       change->changing_rev,
                                       result_pool, &file_baton));
    }

  /* Apply any properties in CHANGE to the node.  */
  if (change->kind == svn_node_dir)
    SVN_ERR(drive_ev1_props(relpath, change,
                            eb->deditor, *dir_baton,
                            eb->fetch_props_func, eb->fetch_props_baton,
                            scratch_pool));
  else
    SVN_ERR(drive_ev1_props(relpath, change,
                            eb->deditor, file_baton,
                            eb->fetch_props_func, eb->fetch_props_baton,
                            scratch_pool));

  if (change->text_stream)
    {
      svn_txdelta_window_handler_t handler;
      void *handler_baton;

      /* ### would be nice to have a BASE_CHECKSUM, but hey: this is the
         ### shim code...  */
      SVN_ERR(eb->deditor->apply_textdelta(file_baton, NULL, scratch_pool,
                                           &handler, &handler_baton));
      /* ### it would be nice to send a true txdelta here, but whatever.  */
      SVN_ERR(svn_txdelta_send_stream(change->text_stream,
                                      handler, handler_baton,
                                      NULL, scratch_pool));
      SVN_ERR(svn_stream_close(change->text_stream));
    }

  if (file_baton)
    {
      const char *digest = svn_checksum_to_cstring(change->text_checksum,
                                                   scratch_pool);

      SVN_ERR(eb->deditor->close_file(file_baton, digest, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Drive the Ev1 with all the changes we have accumulated in EB.
 *
 * We visit each path operated on, and any ancestor directories, in an order
 * that is depth first and in lexical order within each directory.
 *
 * ### For an update editor, we want to send all deletes before all adds
 *     to make case-only renames work better on case-insensitive systems.
 *     But for a commit editor that is irrelevant.
 *
 * ### The Ev2-to-Ev1 converter ordered changes such that lone deletes come
 *     before all other changes, but a delete that is part of a replacement
 *     was sent immediately before the replacing add. I don't know why, but
 *     I can't see how that could be right.
 */
static svn_error_t *
drive_changes(ev3_from_delta_baton_t *eb,
              apr_pool_t *scratch_pool)
{
  change_node_t *change;
  const apr_array_header_t *paths;

  /* If the driver has not explicitly opened the root directory, do so now. */
  if (eb->ev1_root_dir_baton == NULL)
    SVN_ERR(open_root_ev3(eb, SVN_INVALID_REVNUM));

  /* Make the path driver visit the root dir of the edit. Otherwise, it
     will attempt an open_root() instead, which we already did. */
  SVN_ERR(insert_change(&change, eb->changes, eb->base_relpath,
                        RESTRUCTURE_NONE));
  change->kind = svn_node_dir;
  /* No property changes (tho they might exist from a real change).  */

  /* Get a list of Ev1-relative paths (unsorted). */
  paths = get_unsorted_paths(eb->changes, eb->base_relpath, scratch_pool);
  SVN_ERR(svn_delta_path_driver2(eb->deditor, eb->dedit_baton,
                                 paths, TRUE /*sort*/,
                                 apply_change, (void *)eb,
                                 scratch_pool));

  return SVN_NO_ERROR;
}


/*
 * ===================================================================
 * Commit Editor v3 (incremental tree changes; path-based addressing)
 * ===================================================================
 */

/* Return the current path in txn corresponding to the given peg location
 * PEG_LOC. Follow moves that have been made so far.
 */
static const char *
e3_pegged_path_in_txn(ev3_from_delta_baton_t *eb,
                      svn_editor3_peg_path_t peg_loc,
                      apr_pool_t *scratch_pool)
{
  const char *p;

  if (SVN_IS_VALID_REVNUM(peg_loc.rev))
    {
      p = find_move(eb->moves, peg_loc.relpath, scratch_pool);
    }
  else
    {
      /* Path in txn is just as specified */
      p = peg_loc.relpath;
    }

  return p;
}

/* Return the current path in txn corresponding to LOC.
 *
 * LOC represents a path pegged to a revision, plus a created path
 * relative to the pegged path. Follow the pegged part of the path
 * through moves that have been made so far.
 */
static const char *
e3_general_path_in_txn(ev3_from_delta_baton_t *eb,
                       svn_editor3_txn_path_t loc,
                       apr_pool_t *result_pool)
{
  return svn_relpath_join(e3_pegged_path_in_txn(eb, loc.peg, result_pool),
                          loc.relpath, result_pool);
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_mk(void *baton,
           svn_node_kind_t new_kind,
           svn_editor3_txn_path_t new_parent_loc,
           const char *new_name,
           apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = baton;
  change_node_t *change;

  /* look up parent_loc in shadow txn */
  const char *new_parent_txnpath
    = e3_general_path_in_txn(eb, new_parent_loc, scratch_pool);
  const char *new_txnpath
    = svn_relpath_join(new_parent_txnpath, new_name, scratch_pool);

  /* Precondition: a child with this name in parent_loc must not exist,
     as far as we know. This is checked by insert_change(). */

  /* create node in shadow txn */
  SVN_ERR(insert_change(&change, eb->changes, new_txnpath, RESTRUCTURE_ADD));
  change->kind = new_kind;

  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_cp(void *baton,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
           svn_editor3_txn_path_t from_txn_loc,
#else
           svn_editor3_peg_path_t from_peg_loc,
#endif
           svn_editor3_txn_path_t new_parent_loc,
           const char *new_name,
           apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = baton;
  change_node_t *change;

  /* look up old path and new parent path in shadow txn */
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
  svn_editor3_peg_path_t from_peg_loc = from_txn_loc.peg;
#endif
  const char *new_parent_txnpath
    = e3_general_path_in_txn(eb, new_parent_loc, scratch_pool);
  const char *new_txnpath
    = svn_relpath_join(new_parent_txnpath, new_name, scratch_pool);

#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
  /* An attempt to copy from this revision isn't supported, even if the
   * possibility of using this feature is compiled in. */
  if (from_txn_loc.relpath[0])
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                            "Ev3-to-Ev1 doesn't support copy-from-this-rev");
#endif

  /* Precondition: a child with this name in parent_loc must not exist,
     as far as we know. This is checked by insert_change(). */

  /* copy subtree (from_loc originally) to (parent_loc, name in shadow txn) */
  SVN_ERR(insert_change(&change, eb->changes, new_txnpath, RESTRUCTURE_ADD));
  change->copyfrom_path = from_peg_loc.relpath;
  change->copyfrom_rev = from_peg_loc.rev;
  /* We need the source's kind to know whether to call add_directory()
     or add_file() later on.  */
  SVN_ERR(eb->fetch_kind_func(&change->kind, eb->fetch_kind_baton,
                              from_peg_loc.relpath, from_peg_loc.rev,
                              scratch_pool));

  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_mv(void *baton,
           svn_editor3_peg_path_t from_loc,
           svn_editor3_txn_path_t new_parent_loc,
           const char *new_name,
           apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = baton;
  change_node_t *change;

  /* look up old path and new parent path in shadow txn */
  const char *from_txnpath
    = e3_pegged_path_in_txn(eb, from_loc, scratch_pool);
  const char *new_parent_txnpath
    = e3_general_path_in_txn(eb, new_parent_loc, scratch_pool);
  const char *new_txnpath
    = svn_relpath_join(new_parent_txnpath, new_name, scratch_pool);

  /* Precondition: a child with this name in parent_loc must not exist,
     as far as we know. This is checked by insert_change(). */

  /* copy subtree (from_loc originally) to (parent_loc, name in shadow txn) */
  SVN_ERR(insert_change(&change, eb->changes, new_txnpath, RESTRUCTURE_ADD));
  change->copyfrom_path = from_loc.relpath;
  change->copyfrom_rev = from_loc.rev; /* ### or "copyfrom_rev = -1"? */
  /* We need the source's kind to know whether to call add_directory()
     or add_file() later on. (If the move source is one for which we have
     already recorded a change -- an earlier move, I suppose -- then the
     'kind' has already been recorded there and we could potentially
     re-use it here. But we have no need yet to optimise that case.) */
  SVN_ERR(eb->fetch_kind_func(&change->kind, eb->fetch_kind_baton,
                              from_loc.relpath, from_loc.rev, scratch_pool));

  /* duplicate any child changes into the copy destination */
  SVN_ERR(duplicate_child_changes(eb->changes, from_txnpath, new_txnpath,
                                  scratch_pool));

  /* delete subtree (from_loc in shadow txn) */
  SVN_ERR(insert_change(&change, eb->changes, from_txnpath, RESTRUCTURE_DELETE));
  change->deleting_rev = from_loc.rev;

  /* Record the move. If we're moving something again that we already moved
     before, just overwrite the previous entry. */
  record_move(eb->moves, from_loc.relpath, new_txnpath);

  return SVN_NO_ERROR;
}

#ifdef SVN_EDITOR3_WITH_RESURRECTION
/* An #svn_editor3_t method. */
static svn_error_t *
editor3_res(void *baton,
            svn_editor3_peg_path_t from_loc,
            svn_editor3_txn_path_t parent_loc,
            const char *new_name,
            apr_pool_t *scratch_pool)
{
  /* ### */

  return SVN_NO_ERROR;
}
#endif

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_rm(void *baton,
           svn_editor3_txn_path_t loc,
           apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = baton;
  change_node_t *change;

  /* look up old path in shadow txn */
  const char *txnpath
    = e3_general_path_in_txn(eb, loc, scratch_pool);

  /* Precondition: txnpath points to a pre-existing node or a child of
     a copy. This is checked by insert_change(). */

  /* delete subtree (from_loc in shadow txn) */
  SVN_ERR(insert_change(&change, eb->changes, txnpath, RESTRUCTURE_DELETE));
  /* if we're deleting a pre-existing node (as opposed to a child of a
     copy that we made), give its rev num for out-of-date checking */
  change->deleting_rev = (loc.relpath[0] ? SVN_INVALID_REVNUM : loc.peg.rev);

  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_put(void *baton,
            svn_editor3_txn_path_t loc,
            const svn_editor3_node_content_t *new_content,
            apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = baton;
  change_node_t *change;

  /* look up path in shadow txn */
  const char *txnpath
    = e3_general_path_in_txn(eb, loc, scratch_pool);

  /* look up the 'change' record; this may be a new or an existing record */
  SVN_ERR(insert_change(&change, eb->changes, txnpath, RESTRUCTURE_NONE));
  change->changing_rev = loc.peg.rev; /* this is unused for a plain-add node */
  change->props = new_content->props;
  change->text_stream = new_content->stream;
  change->text_checksum = new_content->checksum;

  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_complete(void *baton,
                 apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = baton;
  svn_error_t *err;

  /* Drive the tree we've created. */
  err = drive_changes(eb, scratch_pool);
  if (!err)
     {
       err = svn_error_compose_create(err, eb->deditor->close_edit(
                                                            eb->dedit_baton,
                                                            scratch_pool));
     }

  if (err)
    svn_error_clear(eb->deditor->abort_edit(eb->dedit_baton, scratch_pool));

  return err;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_abort(void *baton,
              apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = baton;
  svn_error_t *err;
  svn_error_t *err2;

  /* ### A comment in the Ev2-Ev1 converter said: "We still need to drive
     anything we collected in the editor to this point." But why? Let's
     try not doing so. */
  err = NULL; /*= editor3_drive_changes(eb, scratch_pool);*/

  err2 = eb->deditor->abort_edit(eb->dedit_baton, scratch_pool);

  if (err2)
    {
      if (err)
        svn_error_clear(err2);
      else
        err = err2;
    }

  return err;
}

/*
 * ### SEND_ABS_PATHS is not implemented. When/where is it needed?
 * SEND_ABS_PATHS: A pointer to a flag which must be set prior to driving
 * this edit, but not necessarily at the invocation of this function,
 * that indicates whether Ev1 paths should be sent as absolute paths (with
 * a leading slash) or relative paths.
 *
 * # The Ev2 function on which this is based also had:
 *   - returned a 'target_revision' callback (for update editors)
 *   - returned an 'unlock_func' callback (for update editors)
 */
svn_error_t *
svn_delta__ev3_from_delta_for_commit(
                        svn_editor3_t **editor_p,
                        svn_delta__start_edit_func_t *open_root_func,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        /*svn_boolean_t *send_abs_paths,*/
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_delta_fetch_kind_func_t fetch_kind_func,
                        void *fetch_kind_baton,
                        svn_delta_fetch_props_func_t fetch_props_func,
                        void *fetch_props_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  static const svn_editor3_cb_funcs_t editor_funcs = {
    editor3_mk,
    editor3_cp,
    editor3_mv,
#ifdef SVN_EDITOR3_WITH_RESURRECTION
    editor3_res,
#endif
    editor3_rm,
    editor3_put,
    NULL, NULL, NULL, NULL, NULL,
    editor3_complete,
    editor3_abort
  };
  ev3_from_delta_baton_t *eb = apr_palloc(result_pool, sizeof(*eb));

  eb->deditor = deditor;
  eb->dedit_baton = dedit_baton;

  /*eb->make_abs_paths = send_abs_paths;*/
  eb->repos_root_url = apr_pstrdup(result_pool, repos_root_url);
  eb->base_relpath = apr_pstrdup(result_pool, base_relpath);

  eb->changes = apr_hash_make(result_pool);
  eb->moves = apr_hash_make(result_pool);

  eb->fetch_kind_func = fetch_kind_func;
  eb->fetch_kind_baton = fetch_kind_baton;
  eb->fetch_props_func = fetch_props_func;
  eb->fetch_props_baton = fetch_props_baton;

  eb->edit_pool = result_pool;

  SVN_ERR(svn_editor3_create(editor_p, &editor_funcs, eb,
                             cancel_func, cancel_baton,
                             result_pool, scratch_pool));

  if (open_root_func)
    *open_root_func = open_root_ev3;

#ifdef SHIM_WITH_UNLOCK
  *unlock_func = do_unlock;
  *unlock_baton = eb;
#endif

  return SVN_NO_ERROR;
}

