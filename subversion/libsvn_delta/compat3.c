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
#include "private/svn_sorts_private.h"
#include "../libsvn_delta/debug_editor.h"
#include "svn_private_config.h"


/* Verify EXPR is true; raise an error if not. */
#define VERIFY(expr) SVN_ERR_ASSERT(expr)

/*
 * ========================================================================
 * Configuration Options
 * ========================================================================
 */

/* Features that are not wanted for this commit editor shim but may be
 * wanted in a similar but different shim such as for an update editor. */
/* #define SHIM_WITH_ADD_ABSENT */
/* #define SHIM_WITH_UNLOCK */

/* The Ev2 shim ran the accumulated actions during abort... But why?
 * If we don't, then aborting and re-opening a commit txn doesn't find
 * all the previous changes, so tests/libsvn_repos/repos-test 12 fails.
 */
#define SHIM_WITH_ACTIONS_DURING_ABORT

/* Whether to support switching from relative to absolute paths in the
 * Ev1 methods. */
/* #define SHIM_WITH_ABS_PATHS */


/*
 * ========================================================================
 * Shim Connector
 * ========================================================================
 *
 * The shim connector enables a more exact round-trip conversion from an
 * Ev1 drive to Ev3 and back to Ev1.
 */
struct svn_editor3__shim_connector_t
{
  /* Set to true if and when an Ev1 receiving shim receives an absolute
   * path (prefixed with '/') from the delta edit, and causes the Ev1
   * sending shim to send absolute paths.
   * ### NOT IMPLEMENTED
   */
#ifdef SHIM_WITH_ABS_PATHS
  svn_boolean_t *ev1_absolute_paths;
#endif

  /* The Ev1 set_target_revision and start_edit methods, respectively, will
   * call the TARGET_REVISION_FUNC and START_EDIT_FUNC callbacks, if non-null.
   * Otherwise, default calls will be used.
   *
   * (Possibly more useful for update editors than for commit editors?) */
  svn_editor3__set_target_revision_func_t target_revision_func;

  /* If not null, a callback that the Ev3 driver may call to
   * provide the "base revision" of the root directory, even if it is not
   * going to modify that directory. (If it does modify it, then it will
   * pass in the appropriate base revision at that time.) If null
   * or if the driver does not call it, then the Ev1
   * open_root() method will be called with SVN_INVALID_REVNUM as the base
   * revision parameter.
   */
  svn_delta__start_edit_func_t start_edit_func;

#ifdef SHIM_WITH_UNLOCK
  /* A callback which will be called when an unlocking action is received.
     (For update editors?) */
  svn_delta__unlock_func_t unlock_func;
#endif

  void *baton;
};

svn_error_t *
svn_editor3__insert_shims(
                        const svn_delta_editor_t **new_deditor,
                        void **new_dedit_baton,
                        const svn_delta_editor_t *old_deditor,
                        void *old_dedit_baton,
                        const char *repos_root,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_editor3_t *editor3;
  svn_editor3__shim_connector_t *shim_connector;

#ifdef SVN_DEBUG
  /*SVN_ERR(svn_delta__get_debug_editor(&old_deditor, &old_dedit_baton,
                                      old_deditor, old_dedit_baton,
                                      "[OUT] ", result_pool));*/
#endif
  SVN_ERR(svn_delta__ev3_from_delta_for_commit(
                        &editor3,
                        &shim_connector,
                        old_deditor, old_dedit_baton,
                        repos_root, base_relpath,
                        fetch_func, fetch_baton,
                        NULL, NULL /*cancel*/,
                        result_pool, scratch_pool));
#ifdef SVN_DEBUG
  /*SVN_ERR(svn_editor3__get_debug_editor(&editor3, editor3, result_pool));*/
#endif
  SVN_ERR(svn_delta__delta_from_ev3_for_commit(
                        new_deditor, new_dedit_baton,
                        editor3,
                        repos_root, base_relpath,
                        fetch_func, fetch_baton,
                        shim_connector,
                        result_pool, scratch_pool));
#ifdef SVN_DEBUG
  /*SVN_ERR(svn_delta__get_debug_editor(new_deditor, new_dedit_baton,
                                      *new_deditor, *new_dedit_baton,
                                      "[IN]  ", result_pool));*/
#endif
  return SVN_NO_ERROR;
}


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

  /* We may need to specify the revision we are altering or the revision
     to delete or replace. These are mutually exclusive, but are separate
     for clarity. */
  /* CHANGING_REV is the base revision of the change if ACTION is 'none',
     else is SVN_INVALID_REVNUM. (If ACTION is 'add' and COPYFROM_PATH
     is non-null, then COPYFROM_REV serves the equivalent purpose for the
     copied node.) */
  /* ### Can also be SVN_INVALID_REVNUM for a pre-existing file/dir,
         meaning the base is the youngest revision. This is probably not
         a good idea -- it is at least confusing -- and we should instead
         resolve to a real revnum when Ev1 passes in SVN_INVALID_REVNUM
         in such cases. */
  svn_revnum_t changing_rev;
  /* If ACTION is 'delete' or if ACTION is 'add' and it is a replacement,
     DELETING is TRUE and DELETING_REV is the revision to delete. */
  /* ### Can also be SVN_INVALID_REVNUM for a pre-existing file/dir,
         meaning the base is the youngest revision. This is probably not
         a good idea -- it is at least confusing -- and we should instead
         resolve to a real revnum when Ev1 passes in SVN_INVALID_REVNUM
         in such cases. */
  svn_boolean_t deleting;
  svn_revnum_t deleting_rev;

  /* new/final set of props to apply; null => no *change*, not no props */
  apr_hash_t *props;

  /* new fulltext; null => no change */
  svn_boolean_t contents_changed;
  svn_stringbuf_t *contents_text;

  /* If COPYFROM_PATH is not NULL, then copy PATH@REV to this node.
     RESTRUCTURE must be RESTRUCTURE_ADD.  */
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;

#ifdef SHIM_WITH_UNLOCK
  /* Record whether an incoming propchange unlocked this node.  */
  svn_boolean_t unlock;
#endif
} change_node_t;

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
      const char *parent_path = relpath;

      /* Find the nearest parent change. If that's a delete or a simple
         (non-recursive) add, this path cannot exist, else we don't know. */
      while ((parent_path = svn_relpath_dirname(parent_path, scratch_pool)),
             *parent_path)
        {
          change = svn_hash_gets(changes, parent_path);
          if (change)
            {
              if ((change->action == RESTRUCTURE_ADD && !change->copyfrom_path)
                  || change->action == RESTRUCTURE_DELETE)
                exists = svn_tristate_false;
              break;
            }
        }
    }

  return exists;
}

/* Insert a new Ev1-style change for RELPATH, or return an existing one.
 *
 * Verify Ev3 rules. Primary differences from Ev1 rules are ...
 *
 * If ACTION is 'delete', elide any previous explicit deletes inside
 * that subtree. (Other changes inside that subtree are not allowed.) We
 * do not store multiple change records per path even with nested moves
 * -- the most complex change is delete + copy which all fits in one
 * record with action='add'.
 */
static svn_error_t *
insert_change(change_node_t **change_p, apr_hash_t *changes,
              const char *relpath,
              enum restructure_action_t action)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  change_node_t *change = svn_hash_gets(changes, relpath);

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
      break;

    case RESTRUCTURE_ADD:
      if (change)
        {
          /* Add or copy is allowed after delete (and replaces the delete),
           * but not allowed after an add or a no-restructure change. */
          VERIFY(change->action == RESTRUCTURE_DELETE);
          change->action = action;
        }
      break;

#ifdef SHIM_WITH_ADD_ABSENT
    case RESTRUCTURE_ADD_ABSENT:
      /* ### */
      break;
#endif

    case RESTRUCTURE_DELETE:
      SVN_ERR_MALFUNCTION();
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

      svn_hash_sets(changes, apr_pstrdup(changes_pool, relpath), change);
    }

  *change_p = change;
  return SVN_NO_ERROR;
}

/* Modify CHANGES so as to delete the subtree at RELPATH.
 *
 * Insert a new Ev1-style change record for RELPATH (or perhaps remove
 * the existing record if this would have the same effect), and remove
 * any change records for sub-paths under RELPATH.
 *
 * Follow Ev3 rules, although without knowing whether this delete is
 * part of a move. Ev3 (incremental) "rm" operation says each node to
 * be removed "MAY be a child of a copy but otherwise SHOULD NOT have
 * been created or modified in this edit". "mv" operation says ...
 */
static svn_error_t *
delete_subtree(apr_hash_t *changes,
               const char *relpath,
               svn_revnum_t deleting_rev)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  apr_pool_t *scratch_pool = changes_pool;
  change_node_t *change = svn_hash_gets(changes, relpath);

  if (change)
    {
      /* If this previous change was a non-replacing addition, there
         is no longer any change to be made at this path. If it was
         a replacement or a modification, it now becomes a delete. (If it was a delete,
         this attempt to delete is an error.) */
       VERIFY(change->action != RESTRUCTURE_DELETE);
       if (change->action == RESTRUCTURE_ADD && !change->deleting)
         {
           svn_hash_sets(changes, relpath, NULL);
           change = NULL;
         }
       else
         {
           change->action = RESTRUCTURE_DELETE;
         }
    }
  else
    {
      /* There was no change recorded at this path. Record a delete. */
      change = apr_pcalloc(changes_pool, sizeof(*change));
      change->action = RESTRUCTURE_DELETE;
      change->changing_rev = SVN_INVALID_REVNUM;
      change->deleting = TRUE;
      change->deleting_rev = deleting_rev;

      svn_hash_sets(changes, apr_pstrdup(changes_pool, relpath), change);
    }

  /* Elide all child ops. */
  {
    apr_hash_index_t *hi;

    for (hi = apr_hash_first(scratch_pool, changes);
         hi; hi = apr_hash_next(hi))
      {
        const char *this_relpath = apr_hash_this_key(hi);
        const char *r = svn_relpath_skip_ancestor(relpath, this_relpath);

        if (r && r[0])
          {
            svn_hash_sets(changes, this_relpath, NULL);
          }
      }
  }

  return SVN_NO_ERROR;
}

/* Insert a new change for RELPATH, or return an existing one.
 *
 * Verify Ev1 ordering.
 *
 * RELPATH is relative to the repository root.
 */
static svn_error_t *
insert_change_ev1_rules(change_node_t **change_p, apr_hash_t *changes,
                        const char *relpath,
                        enum restructure_action_t action,
                        svn_node_kind_t kind)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  apr_pool_t *scratch_pool = changes_pool;
  change_node_t *change = svn_hash_gets(changes, relpath);
  svn_tristate_t exists = check_existence(changes, relpath);

  /* Check whether this op is allowed. */
  switch (action)
    {
    case RESTRUCTURE_NONE:
      VERIFY(kind == svn_node_dir || kind == svn_node_file);
      VERIFY(exists != svn_tristate_false);
      if (change)
        {
          VERIFY(change->kind == kind);
        }
      break;

    case RESTRUCTURE_ADD:
      VERIFY(kind == svn_node_dir || kind == svn_node_file);
      if (change)
        {
          /* Add or copy is allowed after delete (and replaces the delete),
           * but not allowed after an add or a no-restructure change. */
          VERIFY(change->action == RESTRUCTURE_DELETE);
        }
      else
        {
          const char *parent_path = svn_relpath_dirname(relpath, scratch_pool);

          /* Disallow if *parent* path is known to be non-existent
           * (deleted (root or child), or child of a non-copy add). */
          VERIFY(check_existence(changes, parent_path) != svn_tristate_false);
        }
      break;

#ifdef SHIM_WITH_ADD_ABSENT
    case RESTRUCTURE_ADD_ABSENT:
      VERIFY(kind == svn_node_dir || kind == svn_node_file);
      /* ### */
      break;
#endif

    case RESTRUCTURE_DELETE:
      VERIFY(kind == svn_node_none);
      /* Delete is allowed only on a currently existing path. */
      VERIFY(exists != svn_tristate_false);
      /* Remove change records for any child paths inside this delete. */
      {
        apr_hash_index_t *hi;

        for (hi = apr_hash_first(scratch_pool, changes);
             hi; hi = apr_hash_next(hi))
          {
            const char *this_relpath = apr_hash_this_key(hi);
            const char *r = svn_relpath_skip_ancestor(relpath, this_relpath);

            if (r && r[0])
              {
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
      change->kind = kind;
    }
  else
    {
      /* Create a new change. Callers will set the other fields as needed. */
      change = apr_pcalloc(changes_pool, sizeof(*change));
      change->action = action;
      change->kind = kind;
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
      const char *this_path = apr_hash_this_key(hi);
      change_node_t *this_change = apr_hash_this_val(hi);
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


/*
 * ===================================================================
 * Commit Editor converter to join a v1 driver to a v3 consumer
 * ===================================================================
 *
 * The following code maps the calls to a traditional delta editor to an
 * Editor v3.
 *
 * It does not create 'move' operations, neither heuristically nor using
 * out-of-band cues. In fact, the code structure is likely to be
 * unsuitable for processing moves.
 *
 * The design assumes that each Ev1 path maps to a different Ev3 element.
 *
 * It works like this:
 *
 *                +------+--------+
 *                | path | change |
 *      Ev1  -->  +------+--------+  -->  Ev3
 *                | ...  | ...    |
 *                | ...  | ...    |
 *
 *   1. Ev1 changes are accumulated in a per-path table, EB->changes.
 *      Changes are de-duplicated so there is only one change per path.
 *
 *   2. On Ev1 close-edit, walk through the table in a depth-first order,
 *      sending the equivalent Ev3 action for each change.
 *
 * ### This was designed (in its Ev2 form) for both commit and update
 *     editors, but Ev3 is currently only designed as a commit editor.
 *     Therefore 'update' functionality probably doesn't work, including:
 *       - create 'absent' node (currently just omits the 'put', which
 *           Ev3 currently defines will create an empty node)
 *
 * ### Need to review all revisions passed to pathrev()/txn_path()
 *     constructors: are they really the right peg revs?
 *
 * TODO: Fetch the base (kind, props, text) of an opened file or dir
 * right away when it's opened. Delaying the fetch, as we do for the
 * sake of 'optimization', adds complexity & is probably a poor choice.
 */

/* Construct a peg-path-rev */
static svn_editor3_peg_path_t
pathrev(const char *repos_relpath, svn_revnum_t revision)
{
  svn_editor3_peg_path_t p;

  p.rev = revision;
  p.relpath = repos_relpath;
  return p;
}

/* Construct a txn-path-rev */
static svn_editor3_txn_path_t
txn_path(const char *repos_relpath, svn_revnum_t revision,
         const char *created_relpath)
{
  svn_editor3_txn_path_t p;

  p.peg.rev = revision;
  p.peg.relpath = repos_relpath;
  p.relpath = created_relpath;
  return p;
}

struct ev3_edit_baton
{
  svn_editor3_t *editor;

  apr_hash_t *changes;  /* REPOS_RELPATH -> struct change_node  */

  /* (const char *) paths relative to repository root, in path visiting
     order. */
  apr_array_header_t *path_order;
#ifdef SHIM_WITH_ACTIONS_DURING_ABORT
  /* Number of paths in PATH_ORDER processed so far. */
  int paths_processed;
#endif

  /* Repository root URL. */
  const char *repos_root_url;
  /* Base directory of the edit, relative to the repository root. */
  const char *base_relpath;

  const svn_editor3__shim_connector_t *shim_connector;

  svn_editor3__shim_fetch_func_t fetch_func;
  void *fetch_baton;

  svn_boolean_t closed;

  apr_pool_t *edit_pool;
};

struct ev3_dir_baton
{
  struct ev3_edit_baton *eb;

  /* Path of this directory relative to repository root. */
  const char *path;
  /* The base revision if this is a pre-existing directory;
     SVN_INVALID_REVNUM if added/copied.
     ### Can also be SVN_INVALID_REVNUM for a pre-existing dir,
         meaning the base is the youngest revision. */
  svn_revnum_t base_revision;

  /* Copy-from path (relative to repository root) and revision. This is
     set for each dir inside a copy, not just the copy root. */
  const char *copyfrom_relpath;
  svn_revnum_t copyfrom_rev;
};

struct ev3_file_baton
{
  struct ev3_edit_baton *eb;

  /* Path of this file relative to repository root. */
  const char *path;
  /* The base revision if this is a pre-existing file;
     SVN_INVALID_REVNUM if added/copied.
     ### Can also be SVN_INVALID_REVNUM for a pre-existing file,
         meaning the base is the youngest revision. */
  svn_revnum_t base_revision;

  /* Copy-from path (relative to repository root) and revision. This is
     set for each file inside a copy, not just the copy root. */
  const char *copyfrom_relpath;
  svn_revnum_t copyfrom_rev;

  /* Path to a file containing the base text. */
  svn_stringbuf_t *delta_base_text;
};

/* Return the 'txn_path' that addresses the node that is currently at
 * RELPATH according to the info in CHANGES.
 *
 * One way to describe it:
 *
 *   If relpath is a created path:
 *     find_txn_path(its parent)
 *     add (its basename) to the created-path part
 *   elif relpath is an already-existing path:
 *     return txn_path(relpath, base rev, "")
 *   else: # it's deleted
 *     return None
 *
 * Another way:
 *
 *   p := first path component that is add/copy, starting from root
 *   d := dirname(p)
 *   return txn_path(d, changing_rev(d), remainder-relpath)
 */
static svn_editor3_txn_path_t
find_txn_path(apr_hash_t *changes,
              const char *relpath,
              apr_pool_t *result_pool)
{
  const char *existing_path = "", *remainder_path = relpath;
  svn_revnum_t existing_revnum = SVN_INVALID_REVNUM;
  int i;
  svn_editor3_txn_path_t loc;

  /* The root path was necessarily existing. For each further path component,
     if it was existing, add it to the 'existing path', else stop there. */
  for (i = 1; *remainder_path; i++)
    {
      const char *new_prefix_path = svn_relpath_limit(relpath, i, result_pool);
      change_node_t *change = svn_hash_gets(changes, new_prefix_path);

      if (change && change->action == RESTRUCTURE_ADD)
        break;

      existing_path = new_prefix_path;
      remainder_path = svn_relpath_skip_ancestor(existing_path, relpath);
      if (change && change->action == RESTRUCTURE_NONE)
        /* ### This is all well and good when there is a RESTRUCTURE_NONE
               change recorded for this dir, but for an unchanged parent
               dir we don't know what the base revision was ... unless we
               record every 'opened' parent dir. Should we do that? */
        existing_revnum = change->changing_rev;
    }
  loc = txn_path(existing_path, existing_revnum, remainder_path);
  return loc;
}

/* Drive the Ev3 EB->editor to make the Ev1-style edits described by
 * CHANGE for the path REPOS_RELPATH.
 *
 * Note: We do not support converting copy-and-delete to send an Ev3 move.
 * This per-path model of processing is not well suited to doing so.
 */
static svn_error_t *
process_actions(struct ev3_edit_baton *eb,
                const char *repos_relpath,
                const change_node_t *change,
                apr_pool_t *scratch_pool)
{
  svn_editor3_txn_path_t change_loc
    = find_txn_path(eb->changes, repos_relpath, scratch_pool);
  const char *repos_relpath_dirname = svn_relpath_dirname(repos_relpath, scratch_pool);
  const char *repos_relpath_basename = svn_relpath_basename(repos_relpath, scratch_pool);

  SVN_ERR_ASSERT(change != NULL);

#ifdef SHIM_WITH_UNLOCK
  if (eb->shim_connector && change->unlock)
    SVN_ERR(eb->shim_connector->unlock_func(
              eb->shim_connector->baton, repos_relpath, scratch_pool));
#endif

  /* Process any delete, no matter whether it will be replaced. */
  if (change->deleting)
    {
      SVN_ERR(svn_editor3_rm(eb->editor,
                             txn_path(repos_relpath, change->deleting_rev, "")));
    }

#ifdef SHIM_WITH_ADD_ABSENT
  if (change->action == RESTRUCTURE_ADD_ABSENT)
    {
      SVN_ERR(svn_editor3_mk(eb->editor, change->kind,
                             parent_loc, repos_relpath_basename));

      /* No further work possible on this path. */
      return SVN_NO_ERROR;
    }
#endif

  if (change->action == RESTRUCTURE_ADD)
    {
      svn_editor3_txn_path_t parent_loc
        = find_txn_path(eb->changes, repos_relpath_dirname, scratch_pool);

      if (change->copyfrom_path != NULL)
        {
          SVN_ERR(svn_editor3_cp(eb->editor,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
                                 txn_path(change->copyfrom_path, change->copyfrom_rev, ""),
#else
                                 pathrev(change->copyfrom_path, change->copyfrom_rev),
#endif
                                 parent_loc, repos_relpath_basename));
          /* Fall through to possibly make changes post-copy.  */
        }
      else
        {
          SVN_ERR(svn_editor3_mk(eb->editor, change->kind,
                                 parent_loc, repos_relpath_basename));
          /* Fall through to make changes post-add. */
        }
    }

  if (change->props || change->contents_changed)
    {
      svn_editor3_node_content_t *new_content;

      if (change->kind == svn_node_file)
        {
          svn_stringbuf_t *text;

          if (change->contents_text)
            {
              /*SVN_DBG(("contents_changed=%d, contents_text='%.20s...'",
                       change->contents_changed, change->contents_text->data));*/
              text = change->contents_text;
            }
          else
            {
              SVN_DBG(("file '%s', no content, act=%d, cp=%s@%ld",
                       repos_relpath, change->action, change->copyfrom_path, change->copyfrom_rev));
              /*### not: VERIFY(change->action == RESTRUCTURE_ADD && ! change->copyfrom_path);*/

              /* If this file was added, but apply_txdelta() was not called (i.e.
                 CONTENTS_CHANGED is FALSE), we're adding an empty file. */
              text = svn_stringbuf_create_empty(scratch_pool);
            }

          new_content = svn_editor3_node_content_create_file(
                          change->props, text, scratch_pool);
        }
      else if (change->kind == svn_node_dir)
        {
          new_content = svn_editor3_node_content_create_dir(
                          change->props, scratch_pool);
        }
      else
        SVN_ERR_MALFUNCTION();
      SVN_ERR(svn_editor3_put(eb->editor, change_loc, new_content));
    }

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
run_actions(struct ev3_edit_baton *eb,
            apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

#ifdef SHIM_WITH_ACTIONS_DURING_ABORT
  /* Possibly pick up where we left off. Occasionally, we do some of these
     as part of close_edit() and then some more as part of abort_edit()  */
  for (i = eb->paths_processed; i < eb->path_order->nelts; ++i, ++eb->paths_processed)
#else
  for (i = 0; i < eb->path_order->nelts; ++i)
#endif
    {
      const char *repos_relpath
        = APR_ARRAY_IDX(eb->path_order, i, const char *);
      const change_node_t *change = svn_hash_gets(eb->changes, repos_relpath);

      /* Process the change for each path only once, no
         matter how many times the Ev1 driver visited it. When we've
         processed a path successfully, remove it from the queue. */
      if (change)
        {
          svn_pool_clear(iterpool);

          SVN_ERR(process_actions(eb, repos_relpath, change, iterpool));

          /* Remove the action, as we've now processed it. */
          svn_hash_sets(eb->changes, repos_relpath, NULL);
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Return the repository-relative path for a given Ev1 input path (that is,
 * a relpath-within-edit or a URL). */
static const char *
map_to_repos_relpath(struct ev3_edit_baton *eb,
                     const char *path_or_url,
                     apr_pool_t *result_pool)
{
  if (svn_path_is_url(path_or_url))
    {
      return svn_uri_skip_ancestor(eb->repos_root_url, path_or_url, result_pool);
    }
  else
    {
      return svn_relpath_join(eb->base_relpath,
                              path_or_url[0] == '/'
                                    ? path_or_url + 1 : path_or_url,
                              result_pool);
    }
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_set_target_revision(void *edit_baton,
                        svn_revnum_t target_revision,
                        apr_pool_t *scratch_pool)
{
  struct ev3_edit_baton *eb = edit_baton;

  if (eb->shim_connector && eb->shim_connector->target_revision_func)
    SVN_ERR(eb->shim_connector->target_revision_func(
              eb->shim_connector->baton, target_revision, scratch_pool));

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_open_root(void *edit_baton,
              svn_revnum_t base_revision,
              apr_pool_t *result_pool,
              void **root_baton)
{
  struct ev3_dir_baton *db = apr_pcalloc(result_pool, sizeof(*db));
  struct ev3_edit_baton *eb = edit_baton;

  db->eb = eb;
  db->path = apr_pstrdup(eb->edit_pool, eb->base_relpath);
  db->base_revision = base_revision;

  *root_baton = db;

  if (eb->shim_connector && eb->shim_connector->start_edit_func)
    SVN_ERR(eb->shim_connector->start_edit_func(
              eb->shim_connector->baton, base_revision));

  return SVN_NO_ERROR;
}

/* Record a property change in the (existing or new) change record for
 * the node at RELPATH of kind KIND. Change property NAME to value VALUE,
 * or delete the property if VALUE is NULL.
 *
 * Fetch and store the base properties for this node, using the callback
 * EB->fetch_props_func/baton, if we have not yet done so. Then apply the
 * edit to those base properties or to the set of properties resulting
 * from the previous edit.
 *
 * BASE_REVISION is the base revision of the node that is currently at
 * RELPATH, or SVN_INVALID_REVNUM for an added/copied node. COPYFROM_PATH
 * and COPYFROM_REV are the base location for a copied node, including a
 * child of a copy.
 *
 * RELPATH is relative to the repository root.
 */
static svn_error_t *
apply_propedit(struct ev3_edit_baton *eb,
               const char *relpath,
               svn_node_kind_t kind,
               svn_revnum_t base_revision,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_rev,
               const char *name,
               const svn_string_t *value,
               apr_pool_t *scratch_pool)
{
  change_node_t *change;

  SVN_ERR(insert_change_ev1_rules(&change, eb->changes, relpath,
                                  RESTRUCTURE_NONE, kind));

  /* Record the observed order. */
  APR_ARRAY_PUSH(eb->path_order, const char *)
    = apr_pstrdup(eb->edit_pool, relpath);

  /* We're changing the node, so record the base revision in case this is
     the first change. (But we don't need to fill in the copy-from, as a
     change entry would already have been recorded for a copy-root.) */
  VERIFY(!SVN_IS_VALID_REVNUM(change->changing_rev)
         || change->changing_rev == base_revision);
  change->changing_rev = base_revision;

  /* Fetch the original set of properties, if we haven't done so yet. */
  if (change->props == NULL)
    {
      /* If this is a copied/moved node, then the original properties come
         from there. If the node has been added, it starts with empty props.
         Otherwise, we get the properties from BASE.  */
      if (copyfrom_path)
        {
          SVN_ERR(eb->fetch_func(NULL, &change->props, NULL, NULL,
                                 eb->fetch_baton,
                                 copyfrom_path, copyfrom_rev,
                                 eb->edit_pool, scratch_pool));
          SVN_DBG(("apply_propedit('%s@%ld'): fetched %d copy-from props (from %s@%ld)",
                   relpath, base_revision, apr_hash_count(change->props),
                   copyfrom_path, copyfrom_rev));
        }
      else if (change->action == RESTRUCTURE_ADD)
        {
          change->props = apr_hash_make(eb->edit_pool);
        }
      else
        {
          if (! SVN_IS_VALID_REVNUM(base_revision))
            SVN_DBG(("apply_propedit('%s@%ld')  ### need to resolve to HEAD?",
                     relpath, base_revision));
          SVN_ERR(eb->fetch_func(NULL, &change->props, NULL, NULL,
                                 eb->fetch_baton,
                                 relpath, base_revision,
                                 eb->edit_pool, scratch_pool));
          SVN_DBG(("apply_propedit('%s@%ld'): fetched %d original props",
                   relpath, base_revision, apr_hash_count(change->props)));
        }
    }

  if (value == NULL)
    svn_hash_sets(change->props, name, NULL);
  else
    svn_hash_sets(change->props,
                  apr_pstrdup(eb->edit_pool, name),
                  svn_string_dup(value, eb->edit_pool));
  SVN_DBG(("apply_propedit('%s@%ld'): set prop %s=%.50s",
           relpath, base_revision, name, value ? value->data : "<nil>"));

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_delete_entry(const char *path,
                 svn_revnum_t revision,
                 void *parent_baton,
                 apr_pool_t *scratch_pool)
{
  struct ev3_dir_baton *pb = parent_baton;
  svn_revnum_t base_revision;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  change_node_t *change;

  SVN_ERR(insert_change_ev1_rules(&change, pb->eb->changes, relpath,
                                  RESTRUCTURE_DELETE, svn_node_none));

  /* Record the observed order. */
  APR_ARRAY_PUSH(pb->eb->path_order, const char *)
    = apr_pstrdup(pb->eb->edit_pool, relpath);

  if (SVN_IS_VALID_REVNUM(revision))
    base_revision = revision;
  else
    base_revision = pb->base_revision;
  /* ### Shouldn't base_revision be SVN_INVALID_REVNUM instead, if the
         node to delete was created (added/copied) in this edit? */

  /* ### Should these checks be in insert_change()? */
  VERIFY(!change->deleting || change->deleting_rev == base_revision);
  change->deleting = TRUE;
  change->deleting_rev = base_revision;
  if (!SVN_IS_VALID_REVNUM(base_revision))
    SVN_DBG(("ev3_delete_entry('%s'): deleting_rev = base_revision == -1", path));

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_add_directory(const char *path,
                  void *parent_baton,
                  const char *copyfrom_path,
                  svn_revnum_t copyfrom_revision,
                  apr_pool_t *result_pool,
                  void **child_baton)
{
  apr_pool_t *scratch_pool = result_pool;
  struct ev3_dir_baton *pb = parent_baton;
  struct ev3_dir_baton *cb = apr_pcalloc(result_pool, sizeof(*cb));
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  change_node_t *change;

  SVN_ERR(insert_change_ev1_rules(&change, pb->eb->changes, relpath,
                                  RESTRUCTURE_ADD, svn_node_dir));

  /* Record the observed order. */
  APR_ARRAY_PUSH(pb->eb->path_order, const char *)
    = apr_pstrdup(pb->eb->edit_pool, relpath);

  cb->eb = pb->eb;
  cb->path = apr_pstrdup(result_pool, relpath);
  cb->base_revision = SVN_INVALID_REVNUM;
  *child_baton = cb;

  if (!copyfrom_path)
    {
      if (pb->copyfrom_relpath)
        {
          const char *name = svn_relpath_basename(relpath, scratch_pool);
          cb->copyfrom_relpath = svn_relpath_join(pb->copyfrom_relpath, name,
                                                  result_pool);
          cb->copyfrom_rev = pb->copyfrom_rev;
        }
    }
  else
    {
      /* A copy */

      change->copyfrom_path = map_to_repos_relpath(pb->eb, copyfrom_path,
                                                   pb->eb->edit_pool);
      change->copyfrom_rev = copyfrom_revision;

      cb->copyfrom_relpath = change->copyfrom_path;
      cb->copyfrom_rev = change->copyfrom_rev;
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_open_directory(const char *path,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   apr_pool_t *result_pool,
                   void **child_baton)
{
  apr_pool_t *scratch_pool = result_pool;
  struct ev3_dir_baton *pb = parent_baton;
  struct ev3_dir_baton *db = apr_pcalloc(result_pool, sizeof(*db));
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);

  db->eb = pb->eb;
  db->path = apr_pstrdup(result_pool, relpath);
  db->base_revision = base_revision;
  if (! SVN_IS_VALID_REVNUM(base_revision))
    SVN_DBG(("ev3_open_directory('%s', base_revision == -1)  ### need to resolve to HEAD?", path));

  if (pb->copyfrom_relpath)
    {
      /* We are inside a copy. */
      const char *name = svn_relpath_basename(relpath, scratch_pool);

      db->copyfrom_relpath = svn_relpath_join(pb->copyfrom_relpath, name,
                                              result_pool);
      db->copyfrom_rev = pb->copyfrom_rev;
    }

  *child_baton = db;
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_change_dir_prop(void *dir_baton,
                    const char *name,
                    const svn_string_t *value,
                    apr_pool_t *scratch_pool)
{
  struct ev3_dir_baton *db = dir_baton;

  SVN_ERR(apply_propedit(db->eb, db->path, svn_node_dir,
                         db->base_revision, db->copyfrom_relpath, db->copyfrom_rev,
                         name, value, scratch_pool));

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_close_directory(void *dir_baton,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_add_file(const char *path,
             void *parent_baton,
             const char *copyfrom_path,
             svn_revnum_t copyfrom_revision,
             apr_pool_t *result_pool,
             void **file_baton)
{
  apr_pool_t *scratch_pool = result_pool;
  struct ev3_file_baton *fb = apr_pcalloc(result_pool, sizeof(*fb));
  struct ev3_dir_baton *pb = parent_baton;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  change_node_t *change;

  SVN_ERR(insert_change_ev1_rules(&change, pb->eb->changes, relpath,
                                  RESTRUCTURE_ADD, svn_node_file));

  /* Record the observed order. */
  APR_ARRAY_PUSH(pb->eb->path_order, const char *)
    = apr_pstrdup(pb->eb->edit_pool, relpath);

  fb->eb = pb->eb;
  fb->path = apr_pstrdup(result_pool, relpath);
  fb->base_revision = SVN_INVALID_REVNUM;
  *file_baton = fb;

  /* Fetch the base text as a file FB->delta_base, if it's a copy */
  if (copyfrom_path)
    {
      change->copyfrom_path = map_to_repos_relpath(fb->eb, copyfrom_path,
                                                   fb->eb->edit_pool);
      change->copyfrom_rev = copyfrom_revision;

      SVN_ERR(fb->eb->fetch_func(NULL, NULL, &fb->delta_base_text, NULL,
                                 fb->eb->fetch_baton,
                                 change->copyfrom_path,
                                 change->copyfrom_rev,
                                 result_pool, scratch_pool));
      fb->copyfrom_relpath = change->copyfrom_path;
      fb->copyfrom_rev = change->copyfrom_rev;
    }
  else
    {
      /* It's a plain add -- we don't have a base. */
      fb->delta_base_text = NULL;

      if (pb->copyfrom_relpath)
        {
          const char *name = svn_relpath_basename(relpath, scratch_pool);
          fb->copyfrom_relpath = svn_relpath_join(pb->copyfrom_relpath, name,
                                                  result_pool);
          fb->copyfrom_rev = pb->copyfrom_rev;
        }
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_open_file(const char *path,
              void *parent_baton,
              svn_revnum_t base_revision,
              apr_pool_t *result_pool,
              void **file_baton)
{
  apr_pool_t *scratch_pool = result_pool;
  struct ev3_file_baton *fb = apr_pcalloc(result_pool, sizeof(*fb));
  struct ev3_dir_baton *pb = parent_baton;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);

  SVN_DBG(("ev3_open_file(%s@%ld)", path, base_revision));

  fb->eb = pb->eb;
  fb->path = apr_pstrdup(result_pool, relpath);
  fb->base_revision = base_revision;
  if (! SVN_IS_VALID_REVNUM(base_revision))
    SVN_DBG(("ev3_open_file(%s@%ld): base_revision == -1; should we resolve to head?",
             path, base_revision));

  if (pb->copyfrom_relpath)
    {
      /* We're in a copied directory, so delta is based on copy source. */
      const char *name = svn_relpath_basename(relpath, scratch_pool);
      const char *copyfrom_relpath = svn_relpath_join(pb->copyfrom_relpath,
                                                      name, scratch_pool);

      fb->copyfrom_relpath = copyfrom_relpath;
      fb->copyfrom_rev = pb->copyfrom_rev;
      SVN_ERR(fb->eb->fetch_func(NULL, NULL, &fb->delta_base_text, NULL,
                                 fb->eb->fetch_baton,
                                 copyfrom_relpath, pb->copyfrom_rev,
                                 result_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(fb->eb->fetch_func(NULL, NULL, &fb->delta_base_text, NULL,
                                 fb->eb->fetch_baton,
                                 relpath, base_revision,
                                 result_pool, scratch_pool));
    }

  *file_baton = fb;
  return SVN_NO_ERROR;
}

struct ev3_handler_baton
{
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  svn_stream_t *source;

  apr_pool_t *pool;
};

static svn_error_t *
ev3_window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct ev3_handler_baton *hb = baton;
  svn_error_t *err;

  /*
  if (! window)
    SVN_DBG(("ev3 window handler(%s): END",
             hb->fb->path));
  else if (window->new_data)
    SVN_DBG(("ev3 window handler(%s): with new data [%d]'%s'",
             hb->fb->path, (int)window->new_data->len, window->new_data->data));
  else
    SVN_DBG(("ev3 window handler(%s): with no new data",
             hb->fb->path));
  */
  err = hb->apply_handler(window, hb->apply_baton);
  if (window != NULL && !err)
    return SVN_NO_ERROR;

  SVN_ERR(svn_stream_close(hb->source));

  svn_pool_destroy(hb->pool);

  return svn_error_trace(err);
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_apply_textdelta(void *file_baton,
                    const char *base_checksum,
                    apr_pool_t *result_pool,
                    svn_txdelta_window_handler_t *handler,
                    void **handler_baton)
{
  struct ev3_file_baton *fb = file_baton;
  apr_pool_t *handler_pool = svn_pool_create(fb->eb->edit_pool);
  struct ev3_handler_baton *hb = apr_pcalloc(handler_pool, sizeof(*hb));
  change_node_t *change;
  svn_stream_t *target;

  SVN_ERR(insert_change_ev1_rules(&change, fb->eb->changes, fb->path,
                                  RESTRUCTURE_NONE, svn_node_file));

  /* Record the observed order. */
  APR_ARRAY_PUSH(fb->eb->path_order, const char *)
    = apr_pstrdup(fb->eb->edit_pool, fb->path);

  /* The content for this path must be changed only once. (Not explicitly
     mandated by svn_delta_editor, but we'll assume it is mandatory.) */
  VERIFY(!change->contents_changed);
  change->contents_changed = TRUE;

  /* This can come after property changes or no changes or an add. */
  VERIFY(!SVN_IS_VALID_REVNUM(change->changing_rev)
         || change->changing_rev == fb->base_revision);
  change->changing_rev = fb->base_revision;

  if (! fb->delta_base_text)
    {
      /*SVN_DBG(("apply_textdelta(%s): preparing read from delta-base <empty stream>",
               fb->path));*/
      hb->source = svn_stream_empty(handler_pool);
    }
  else
    {
      /*SVN_DBG(("apply_textdelta(%s): preparing read of delta-base '%.20s...'",
               fb->path, fb->delta_base_text));*/
      hb->source = svn_stream_from_stringbuf(fb->delta_base_text, handler_pool);
    }

  change->contents_text = svn_stringbuf_create_empty(fb->eb->edit_pool);
  target = svn_stream_from_stringbuf(change->contents_text, fb->eb->edit_pool);

  svn_txdelta_apply(hb->source, target,
                    NULL, NULL,
                    handler_pool,
                    &hb->apply_handler, &hb->apply_baton);

  hb->pool = handler_pool;

  *handler_baton = hb;
  *handler = ev3_window_handler;
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_change_file_prop(void *file_baton,
                     const char *name,
                     const svn_string_t *value,
                     apr_pool_t *scratch_pool)
{
  struct ev3_file_baton *fb = file_baton;

#ifdef SHIM_WITH_UNLOCK
  if (!strcmp(name, SVN_PROP_ENTRY_LOCK_TOKEN) && value == NULL)
    {
      /* We special case the lock token propery deletion, which is the
         server's way of telling the client to unlock the path. */

      /* ### this duplicates much of apply_propedit(). fix in future.  */
      const char *relpath = map_to_repos_relpath(fb->eb, fb->path,
                                                 scratch_pool);
      change_node_t *change = locate_change(fb->eb, relpath);

      change->unlock = TRUE;
    }
#endif

  SVN_ERR(apply_propedit(fb->eb, fb->path, svn_node_file,
                         fb->base_revision, fb->copyfrom_relpath, fb->copyfrom_rev,
                         name, value, scratch_pool));

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_close_file(void *file_baton,
               const char *text_checksum,
               apr_pool_t *scratch_pool)
{
  struct ev3_file_baton *fb = file_baton;
  change_node_t *change = svn_hash_gets(fb->eb->changes, fb->path);

  /* If this file is being modified, or copied, and apply_txdelta()
     was not called (i.e. CONTENTS_CHANGED is FALSE), then there is
     no change to the content. We must fetch the original content
     in order to tell Ev3 not to change it.
     (### Or we could retract the changing of this file entirely
     if there were no prop changes either.)

     The only exception is for a new, empty file, where we leave
     CONTENTS_CHANGED false for now (and CONTENTS_ABSPATH null), and
     generate an empty stream for it later. */
  /*SVN_DBG(("close_file(%s): action=%d, contents_changed=%d, contents_abspath='%s'",
           fb->path, change->action, change->contents_changed, change->contents_abspath));*/
  if (! change->contents_changed
      && (change->action == RESTRUCTURE_NONE || change->copyfrom_path))
    {
      change->contents_changed = TRUE;
      change->contents_text = svn_stringbuf_dup(fb->delta_base_text,
                                                fb->eb->edit_pool);
      SVN_DBG(("close_file(%s): unchanged => use base text '%.20s...'",
               fb->path, change->contents_text->data));
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_absent_directory(const char *path,
                     void *parent_baton,
                     apr_pool_t *scratch_pool)
{
#ifdef SHIM_WITH_ADD_ABSENT
  struct ev3_dir_baton *pb = parent_baton;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  change_node_t *change;

  SVN_ERR(insert_change_ev1_rules(&change, eb->changes, relpath,
                                  RESTRUCTURE_ADD_ABSENT, svn_node_dir));

  /* Record the observed order. */
  APR_ARRAY_PUSH(pb->eb->path_order, const char *)
    = apr_pstrdup(pb->eb->edit_pool, path);
#endif

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_absent_file(const char *path,
                void *parent_baton,
                apr_pool_t *scratch_pool)
{
#ifdef SHIM_WITH_ADD_ABSENT
  struct ev3_dir_baton *pb = parent_baton;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  change_node_t *change;

  SVN_ERR(insert_change_ev1_rules(&change, eb->changes, relpath,
                                  RESTRUCTURE_ADD_ABSENT, svn_node_file));

  /* Record the observed order. */
  APR_ARRAY_PUSH(pb->eb->path_order, const char *)
    = apr_pstrdup(pb->eb->edit_pool, path);
#endif

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_close_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev3_edit_baton *eb = edit_baton;

  SVN_ERR(run_actions(edit_baton, scratch_pool));
  eb->closed = TRUE;
  SVN_ERR(svn_editor3_complete(eb->editor));
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t method. */
static svn_error_t *
ev3_abort_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev3_edit_baton *eb = edit_baton;

#ifdef SHIM_WITH_ACTIONS_DURING_ABORT
  SVN_ERR(run_actions(edit_baton, scratch_pool));
#endif

  if (!eb->closed)
    SVN_ERR(svn_editor3_abort(eb->editor));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_delta__delta_from_ev3_for_commit(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_editor3_t *editor,
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        const svn_editor3__shim_connector_t *shim_connector,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  static const svn_delta_editor_t delta_editor = {
    ev3_set_target_revision,
    ev3_open_root,
    ev3_delete_entry,
    ev3_add_directory,
    ev3_open_directory,
    ev3_change_dir_prop,
    ev3_close_directory,
    ev3_absent_directory,
    ev3_add_file,
    ev3_open_file,
    ev3_apply_textdelta,
    ev3_change_file_prop,
    ev3_close_file,
    ev3_absent_file,
    ev3_close_edit,
    ev3_abort_edit
  };
  struct ev3_edit_baton *eb = apr_pcalloc(result_pool, sizeof(*eb));

  if (!base_relpath)
    base_relpath = "";
  else if (base_relpath[0] == '/')
    base_relpath += 1;

  eb->editor = editor;
  eb->changes = apr_hash_make(result_pool);
  eb->path_order = apr_array_make(result_pool, 1, sizeof(const char *));
  eb->edit_pool = result_pool;
  eb->shim_connector = shim_connector;
  if (shim_connector)
    {
#ifdef SHIM_WITH_ABS_PATHS
      *eb->shim_connector->ev1_absolute_paths = FALSE;
#endif
    }
  eb->repos_root_url = apr_pstrdup(result_pool, repos_root_url);
  eb->base_relpath = apr_pstrdup(result_pool, base_relpath);

  eb->fetch_func = fetch_func;
  eb->fetch_baton = fetch_baton;

  *dedit_baton = eb;
  *deditor = &delta_editor;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_delta__delta_from_ev3_for_update(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_update_editor3_t *update_editor,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_editor3__shim_connector_t *shim_connector
    = apr_pcalloc(result_pool, sizeof(*shim_connector));

  shim_connector->target_revision_func = update_editor->set_target_revision_func;
  shim_connector->baton = update_editor->set_target_revision_baton;
#ifdef SHIM_WITH_ABS_PATHS
  shim_connector->ev1_absolute_paths /*...*/;
#endif

  SVN_ERR(svn_delta__delta_from_ev3_for_commit(
                        deditor, dedit_baton,
                        update_editor->editor,
                        repos_root_url, base_repos_relpath,
                        fetch_func, fetch_baton,
                        shim_connector,
                        result_pool, scratch_pool));
  SVN_ERR(svn_delta__get_debug_editor(deditor, dedit_baton,
                                      *deditor, *dedit_baton,
                                      "[UP>1] ", result_pool));

  return SVN_NO_ERROR;
}


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
 *
 * It works like this:
 *
 *                +------+--------+
 *                | path | change |
 *      Ev3  -->  +------+--------+  -->  Ev1
 *                | ...  | ...    |
 *                | ...  | ...    |
 *
 *   1. Ev3 changes are accumulated in a per-path table, EB->changes.
 *
 *   2. On Ev3 close-edit, walk through the table in a depth-first order,
 *      sending the equivalent Ev1 action for each change.
 *
 * TODO
 *
 * ### For changes inside a copied subtree, the calls to the "open dir"
 *     and "open file" Ev1 methods may be passing the wrong revision
 *     number: see comment in apply_change().
 *
 * ### Have we got our rel-paths in order? Ev1, Ev3 and callbacks may
 *     all expect different paths. 'repos_relpath' or relative to
 *     eb->base_relpath? Leading slash (unimplemented 'send_abs_paths'
 *     feature), etc.
 *
 * ### May be tidier for OPEN_ROOT_FUNC callback (see open_root_ev3())
 *     not to actually open the root in advance, but instead just to
 *     remember the base revision that the driver wants us to specify
 *     when we do open it.
 */


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
      const char *this_from_relpath = apr_hash_this_key(hi);
      const char *this_to_relpath = apr_hash_this_val(hi);
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
  svn_editor3__shim_fetch_func_t fetch_func;
  void *fetch_baton;

  /* The Ev1 root directory baton if we have opened the root, else null. */
  void *ev1_root_dir_baton;

#ifdef SHIM_WITH_ABS_PATHS
  const svn_boolean_t *make_abs_paths;
#endif

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

/* Get all the (Ev1) paths that have changes. Return only paths at or below
 * BASE_RELPATH, and return them relative to BASE_RELPATH.
 *
 * ### Instead, we should probably avoid adding paths outside BASE_RELPATH
 *     to CHANGES in the first place, and not allow them here.
 */
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
      const char *this_path = apr_hash_this_key(hi);
      const char *this_relpath = svn_relpath_skip_ancestor(base_relpath,
                                                           this_path);

      if (this_relpath)
        {
          APR_ARRAY_PUSH(paths, const char *) = this_relpath;
        }
    }

  return paths;
}

/*  */
static svn_error_t *
set_target_revision_ev3(void *edit_baton,
                        svn_revnum_t target_revision,
                        apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = edit_baton;

  SVN_ERR(eb->deditor->set_target_revision(eb->dedit_baton, target_revision,
                                           scratch_pool));

  return SVN_NO_ERROR;
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

/* If RELPATH is a child of a copy, return the path of the copy root,
 * else return NULL.
 */
static const char *
find_enclosing_copy(apr_hash_t *changes,
                    const char *relpath,
                    apr_pool_t *result_pool)
{
  while (*relpath)
    {
      const change_node_t *change = svn_hash_gets(changes, relpath);

      if (change)
        {
          if (change->copyfrom_path)
            return relpath;
          if (change->action != RESTRUCTURE_NONE)
            return NULL;
        }
      relpath = svn_relpath_dirname(relpath, result_pool);
    }

  return NULL;
}

/* Set *BASE_PROPS to the 'base' properties, against which any changes
 * will be described, for the changed path described in CHANGES at
 * REPOS_RELPATH.
 *
 * For a copied path, including a copy child path, fetch from the copy
 * source path. For a plain add, return an empty set. For a delete,
 * return NULL.
 */
static svn_error_t *
fetch_base_props(apr_hash_t **base_props,
                 apr_hash_t *changes,
                 const char *repos_relpath,
                 svn_editor3__shim_fetch_func_t fetch_func,
                 void *fetch_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  const change_node_t *change = svn_hash_gets(changes, repos_relpath);
  const char *source_path = NULL;
  svn_revnum_t source_rev;

  if (change->action == RESTRUCTURE_DELETE)
    {
      *base_props = NULL;
    }

  else if (change->action == RESTRUCTURE_ADD && ! change->copyfrom_path)
    {
      *base_props = apr_hash_make(result_pool);
    }
  else if (change->copyfrom_path)
    {
      source_path = change->copyfrom_path;
      source_rev = change->copyfrom_rev;
    }
  else /* RESTRUCTURE_NONE */
    {
      /* It's an edit, but possibly to a copy child. Discover if it's a
         copy child, & find the copy-from source. */

      const char *copy_path
        = find_enclosing_copy(changes, repos_relpath, scratch_pool);

      if (copy_path)
        {
          const change_node_t *enclosing_copy = svn_hash_gets(changes, copy_path);
          const char *remainder = svn_relpath_skip_ancestor(copy_path, repos_relpath);

          source_path = svn_relpath_join(enclosing_copy->copyfrom_path,
                                         remainder, scratch_pool);
          source_rev = enclosing_copy->copyfrom_rev;
        }
      else
        {
          /* It's a plain edit (not a copy child path). */
          source_path = repos_relpath;
          source_rev = change->changing_rev;
        }
    }

  if (source_path)
    {
      SVN_ERR(fetch_func(NULL, base_props, NULL, NULL,
                         fetch_baton, source_path, source_rev,
                         result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Send property changes to Ev1 for the CHANGE at REPOS_RELPATH.
 *
 * Ev1 requires exactly one prop-change call for each prop whose value
 * has changed. Therefore we *have* to fetch the original props from the
 * repository, provide them as OLD_PROPS, and calculate the changes.
 */
static svn_error_t *
drive_ev1_props(const char *repos_relpath,
                const change_node_t *change,
                apr_hash_t *old_props,
                const svn_delta_editor_t *deditor,
                void *node_baton,
                apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *propdiffs;
  int i;

  SVN_ERR_ASSERT(change->action != RESTRUCTURE_DELETE);

  /* If there are no property changes, then just exit. */
  if (change->props == NULL)
    return SVN_NO_ERROR;

  SVN_ERR(svn_prop_diffs(&propdiffs, change->props, old_props, scratch_pool));

  /* Apply property changes. These should be changes against the empty set
     for a new node, or changes against the source node for a copied node. */
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
  apr_pool_t *scratch_pool = result_pool;
  const ev3_from_delta_baton_t *eb = callback_baton;
  const char *relpath = svn_relpath_join(eb->base_relpath, ev1_relpath,
                                         scratch_pool);
  const change_node_t *change = svn_hash_gets(eb->changes, relpath);
  void *file_baton = NULL;
  apr_hash_t *base_props;

  /* The callback should only be called for paths in CHANGES.  */
  SVN_ERR_ASSERT(change != NULL);

  /* Typically, we are not creating new directory batons.  */
  *dir_baton = NULL;

  SVN_ERR(fetch_base_props(&base_props, eb->changes, relpath,
                           eb->fetch_func, eb->fetch_baton,
                           scratch_pool, scratch_pool));

  /* Are we editing the root of the tree?  */
  if (parent_baton == NULL)
    {
      /* The root dir was already opened. */
      *dir_baton = eb->ev1_root_dir_baton;

      /* Only property edits are allowed on the root.  */
      SVN_ERR_ASSERT(change->action == RESTRUCTURE_NONE);
      SVN_ERR(drive_ev1_props(relpath, change, base_props,
                              eb->deditor, *dir_baton, scratch_pool));

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
      else if (change->kind == svn_node_file)
        SVN_ERR(eb->deditor->absent_file(ev1_relpath, parent_baton,
                                         scratch_pool));
      else
        SVN_ERR_MALFUNCTION();

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
      if (change->deleting)
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
      else if (change->kind == svn_node_file)
        SVN_ERR(eb->deditor->add_file(ev1_relpath, parent_baton,
                                      copyfrom_url, copyfrom_rev,
                                      result_pool, &file_baton));
      else
        SVN_ERR_MALFUNCTION();
    }
  else /* RESTRUCTURE_NONE */
    {
      /* ### The code that inserts a "plain edit" change record sets
         'changing_rev' to the peg rev of the pegged part of the path,
         even when the full path refers to a child of a copy. Should we
         instead be using the copy source rev here, in that case? (Like
         when we fetch the base properties.) */

      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->open_directory(ev1_relpath, parent_baton,
                                            change->changing_rev,
                                            result_pool, dir_baton));
      else if (change->kind == svn_node_file)
        SVN_ERR(eb->deditor->open_file(ev1_relpath, parent_baton,
                                       change->changing_rev,
                                       result_pool, &file_baton));
      else
        SVN_ERR_MALFUNCTION();
    }

  /* Apply any properties in CHANGE to the node.  */
  if (change->kind == svn_node_dir)
    SVN_ERR(drive_ev1_props(relpath, change, base_props,
                            eb->deditor, *dir_baton, scratch_pool));
  else
    SVN_ERR(drive_ev1_props(relpath, change, base_props,
                            eb->deditor, file_baton, scratch_pool));

  /* Send the text content delta, if new text content is provided. */
  if (change->contents_text)
    {
      svn_stream_t *read_stream;
      svn_txdelta_window_handler_t handler;
      void *handler_baton;

      read_stream = svn_stream_from_stringbuf(change->contents_text,
                                              scratch_pool);
      /* ### would be nice to have a BASE_CHECKSUM, but hey: this is the
         ### shim code...  */
      SVN_ERR(eb->deditor->apply_textdelta(file_baton, NULL, scratch_pool,
                                           &handler, &handler_baton));
      /* ### it would be nice to send a true txdelta here, but whatever.  */
      SVN_ERR(svn_txdelta_send_stream(read_stream, handler, handler_baton,
                                      NULL, scratch_pool));
      SVN_ERR(svn_stream_close(read_stream));
    }

  if (file_baton)
    {
      SVN_ERR(eb->deditor->close_file(file_baton, NULL, scratch_pool));
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
  /* ### Seems clumsy. Is there not a simpler way? */
  if (! svn_hash_gets(eb->changes, eb->base_relpath))
    {
      SVN_ERR(insert_change(&change, eb->changes, eb->base_relpath,
                            RESTRUCTURE_NONE));
      change->kind = svn_node_dir;
    }
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
  SVN_ERR(eb->fetch_func(&change->kind, NULL, NULL, NULL,
                         eb->fetch_baton,
                         from_peg_loc.relpath, from_peg_loc.rev,
                         scratch_pool, scratch_pool));

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
  SVN_ERR(eb->fetch_func(&change->kind, NULL, NULL, NULL, eb->fetch_baton,
                         from_loc.relpath, from_loc.rev,
                         scratch_pool, scratch_pool));

  /* duplicate any child changes into the copy destination */
  SVN_ERR(duplicate_child_changes(eb->changes, from_txnpath, new_txnpath,
                                  scratch_pool));

  /* delete subtree (from_loc in shadow txn) */
  SVN_ERR(delete_subtree(eb->changes, from_txnpath, from_loc.rev));

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

  /* look up old path in shadow txn */
  const char *txnpath
    = e3_general_path_in_txn(eb, loc, scratch_pool);

  /* Precondition: txnpath points to a pre-existing node or a child of
     a copy. This is checked by insert_change(). */

  /* delete subtree (from_loc in shadow txn) */
  /* if we're deleting a pre-existing node (as opposed to a child of a
     copy that we made), give its rev num for out-of-date checking */
  SVN_ERR(delete_subtree(eb->changes, txnpath,
                         loc.relpath[0] ? SVN_INVALID_REVNUM : loc.peg.rev));

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
  apr_pool_t *changes_pool = apr_hash_pool_get(eb->changes);

  /* look up path in shadow txn */
  const char *txnpath
    = e3_general_path_in_txn(eb, loc, scratch_pool);

  /* look up the 'change' record; this may be a new or an existing record */
  SVN_ERR(insert_change(&change, eb->changes, txnpath, RESTRUCTURE_NONE));
  change->kind = new_content->kind;
  /* The revision number that this change is based on is the peg rev for
     a simple change. For a plain add it is unused. For a copy ...

     ### For a copied path, and/or a change inside a copied subtree, should
     we be using the copy-from rev instead? See comment in apply_change().
   */
  change->changing_rev = loc.peg.rev;
  change->props = (new_content->props
                   ? svn_prop_hash_dup(new_content->props, changes_pool)
                   : NULL);

  if (new_content->kind == svn_node_file)
    {
      /* Copy the provided text into the change record. */
      change->contents_text = svn_stringbuf_dup(new_content->text,
                                                changes_pool);
    }

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

#ifdef SHIM_WITH_ACTIONS_DURING_ABORT
  err = drive_changes(eb, scratch_pool);
#else
  err = NULL;
#endif

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

svn_error_t *
svn_delta__ev3_from_delta_for_commit(
                        svn_editor3_t **editor_p,
                        svn_editor3__shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
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
    NULL, NULL, NULL, NULL, NULL, NULL,
    editor3_complete,
    editor3_abort
  };
  ev3_from_delta_baton_t *eb = apr_pcalloc(result_pool, sizeof(*eb));

  eb->deditor = deditor;
  eb->dedit_baton = dedit_baton;

  eb->repos_root_url = apr_pstrdup(result_pool, repos_root_url);
  eb->base_relpath = apr_pstrdup(result_pool, base_relpath);

  eb->changes = apr_hash_make(result_pool);
  eb->moves = apr_hash_make(result_pool);

  eb->fetch_func = fetch_func;
  eb->fetch_baton = fetch_baton;

  eb->edit_pool = result_pool;

  SVN_ERR(svn_editor3_create(editor_p, &editor_funcs, eb,
                             cancel_func, cancel_baton,
                             result_pool, scratch_pool));

  if (shim_connector)
    {
      *shim_connector = apr_palloc(result_pool, sizeof(**shim_connector));
#ifdef SHIM_WITH_ABS_PATHS
      (*shim_connector)->ev1_absolute_paths = apr_palloc(result_pool, sizeof(svn_boolean_t));
      eb->make_abs_paths = (*shim_connector)->ev1_absolute_paths;
#endif
      (*shim_connector)->target_revision_func = set_target_revision_ev3;
      (*shim_connector)->start_edit_func = open_root_ev3;
#ifdef SHIM_WITH_UNLOCK
      (*shim_connector)->unlock_func = do_unlock;
#endif
      (*shim_connector)->baton = eb;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_delta__ev3_from_delta_for_update(
                        svn_update_editor3_t **update_editor_p,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_update_editor3_t *update_editor
    = apr_pcalloc(result_pool, sizeof(*update_editor));
  svn_editor3__shim_connector_t *shim_connector;

  SVN_DBG(("svn_delta__ev3_from_delta_for_update(base='%s')...",
           base_repos_relpath));

  SVN_ERR(svn_delta__get_debug_editor(&deditor, &dedit_baton,
                                      deditor, dedit_baton,
                                      "[1>UP] ", result_pool));
  SVN_ERR(svn_delta__ev3_from_delta_for_commit(
                        &update_editor->editor,
                        &shim_connector,
                        deditor, dedit_baton,
                        repos_root_url, base_repos_relpath,
                        fetch_func, fetch_baton,
                        cancel_func, cancel_baton,
                        result_pool, scratch_pool));

  update_editor->set_target_revision_func = shim_connector->target_revision_func;
  update_editor->set_target_revision_baton = shim_connector->baton;
  /* shim_connector->start_edit_func = open_root_ev3; */
#ifdef SHIM_WITH_ABS_PATHS
  update_editor->ev1_absolute_paths /*...*/;
#endif
#ifdef SHIM_WITH_UNLOCK
  update_editor->unlock_func = do_unlock;
#endif

  *update_editor_p = update_editor;
  return SVN_NO_ERROR;
}
