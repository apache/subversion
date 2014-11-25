/*
 * compat3b.c : Ev3-to-Ev1 compatibility via element-based branching.
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
#include "../libsvn_delta/debug_editor.h"
#include "svn_private_config.h"


/* Verify EXPR is true; raise an error if not. */
#define VERIFY(expr) SVN_ERR_ASSERT(expr)

#undef SVN_DBG
#define SVN_DBG(ARGS) (svn__is_verbose() \
                       ? (svn_dbg__preamble(__FILE__, __LINE__, SVN_DBG_OUTPUT), \
                          svn_dbg__printf ARGS) : (void)0)

static svn_boolean_t _verbose = FALSE;

void svn__set_verbose(svn_boolean_t verbose)
{
  _verbose = verbose;
}

svn_boolean_t svn__is_verbose(void)
{
  return _verbose;
}


#ifdef SVN_DEBUG
/* Return a human-readable string representation of LOC. */
static const char *
peg_path_str(svn_editor3_peg_path_t loc,
             apr_pool_t *result_pool)
{
  return apr_psprintf(result_pool, "%s@%ld",
                      loc.relpath, loc.rev);
}
#endif


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
         a replacement or a modification, it now becomes a delete.
         (If it was a delete, this attempt to delete is an error.) */
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

  /* The branching state on which the per-element API is working */
  svn_branch_revision_root_t *edited_rev_root;

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

#if 0 /* needed only for shim connector */
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
#endif

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
          const change_node_t *enclosing_copy
            = svn_hash_gets(changes, copy_path);
          const char *remainder
            = svn_relpath_skip_ancestor(copy_path, repos_relpath);

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

/*
 * ========================================================================
 * Editor for Commit (independent per-node changes; node-id addressing)
 * ========================================================================
 */

/*  */
static svn_error_t *
content_fetch(svn_editor3_node_content_t **content_p,
              apr_hash_t **children_names,
              ev3_from_delta_baton_t *eb,
              const svn_editor3_peg_path_t *path_rev,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_editor3_node_content_t *content
    = apr_pcalloc(result_pool, sizeof (*content));

  SVN_ERR(eb->fetch_func(&content->kind,
                         &content->props,
                         &content->text,
                         children_names,
                         eb->fetch_baton,
                         path_rev->relpath, path_rev->rev,
                         result_pool, scratch_pool));

  SVN_ERR_ASSERT(content->kind == svn_node_dir
                 || content->kind == svn_node_file);
  if (content_p)
    *content_p = content;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_el_rev_get(svn_branch_el_rev_content_t **node_p,
                      svn_editor3_t *editor,
                      svn_branch_instance_t *branch,
                      int eid,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = svn_editor3__get_baton(editor);
  svn_branch_el_rev_content_t *node = svn_branch_map_get(branch, eid);

  /* Node content is null iff node is a subbranch root, but we shouldn't
     be querying a subbranch root. */
  SVN_ERR_ASSERT(!node || node->content);

  node = node ? svn_branch_el_rev_content_dup(node, result_pool) : NULL;

  /* If content is by reference, fetch full content. */
  if (node && (node->content->ref.relpath))
    {
      SVN_ERR(content_fetch(&node->content, NULL,
                            eb, &node->content->ref,
                            result_pool, scratch_pool));
    }

  *node_p = node;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_editor3_find_el_rev_by_path_rev(svn_branch_el_rev_id_t **el_rev_p,
                                    svn_editor3_t *editor,
                                    const char *rrpath,
                                    svn_revnum_t revnum,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = svn_editor3__get_baton(editor);
  const svn_branch_repos_t *repos = eb->edited_rev_root->repos;

  SVN_ERR(svn_branch_repos_find_el_rev_by_path_rev(el_rev_p,
                                                   rrpath, revnum, repos,
                                                   result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

void
svn_editor3_find_branch_element_by_rrpath(svn_branch_instance_t **branch_p,
                                          int *eid_p,
                                          svn_editor3_t *editor,
                                          const char *rrpath,
                                          apr_pool_t *scratch_pool)
{
  ev3_from_delta_baton_t *eb = svn_editor3__get_baton(editor);

  svn_branch_find_nested_branch_element_by_rrpath(branch_p, eid_p,
                                                  eb->edited_rev_root->root_branch,
                                                  rrpath, scratch_pool);
}

svn_error_t *
svn_branch_branch(svn_editor3_t *editor,
                  svn_branch_instance_t *from_branch,
                  int from_eid,
                  svn_branch_instance_t *to_outer_branch,
                  svn_editor3_eid_t to_outer_parent_eid,
                  const char *new_name,
                  apr_pool_t *scratch_pool)
{
  if (! svn_branch_get_path_by_eid(from_branch, from_eid, scratch_pool))
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("cannot branch from b%d e%d: "
                                 "does not exist"),
                               from_branch->sibling_defn->bid, from_eid);
    }

  SVN_ERR(svn_branch_branch_subtree_r(NULL,
                                      from_branch, from_eid,
                                      to_outer_branch, to_outer_parent_eid,
                                      new_name,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_branchify(svn_editor3_t *editor,
                     svn_branch_instance_t *outer_branch,
                     svn_editor3_eid_t outer_eid,
                     apr_pool_t *scratch_pool)
{
  /* ### TODO: First check the element is not already a branch root
         and its subtree does not contain any branch roots. */

  svn_branch_family_t *new_family
    = svn_branch_family_add_new_subfamily(outer_branch->sibling_defn->family);
  int new_root_eid = svn_branch_family_add_new_element(new_family);
  svn_branch_sibling_t *
    new_branch_def = svn_branch_family_add_new_branch_sibling(new_family,
                                                              new_root_eid);
  int new_outer_eid
    = svn_branch_family_add_new_element(outer_branch->sibling_defn->family);
  svn_branch_instance_t *
    new_branch = svn_branch_add_new_branch_instance(outer_branch, new_outer_eid,
                                                    new_branch_def,
                                                    scratch_pool);
  svn_branch_el_rev_content_t *old_content;

  SVN_DBG(("branchify(b%d e%d at ^/%s): new f%d b%d e%d",
           outer_branch->sibling_defn->bid, outer_eid,
           svn_branch_get_rrpath_by_eid(outer_branch, outer_eid, scratch_pool),
           new_family->fid, new_branch_def->bid, new_branch_def->root_eid));

  /* create the new root element */
  old_content = svn_branch_map_get(outer_branch, outer_eid);
  svn_branch_map_update(new_branch, new_branch_def->root_eid,
                        -1, "", old_content->content);

  /* copy all the children into the new branch, assigning new EIDs */
  SVN_ERR(svn_branch_map_copy_children(outer_branch, outer_eid,
                                       new_branch, new_branch_def->root_eid,
                                       scratch_pool));

  /* delete the old subtree-root element (which implicitly deletes all its
     children from the old branch, if nothing further touches them) */
  svn_branch_map_delete(outer_branch, outer_eid);

  /* replace the old subtree-root element with a new subbranch-root element */
  svn_branch_map_update_as_subbranch_root(outer_branch, new_outer_eid,
                                          old_content->parent_eid,
                                          old_content->name);

  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_add(void *baton,
            svn_editor3_eid_t *eid_p,
            svn_node_kind_t new_kind,
            svn_branch_instance_t *branch,
            svn_editor3_eid_t new_parent_eid,
            const char *new_name,
            const svn_editor3_node_content_t *new_content,
            apr_pool_t *scratch_pool)
{
  int eid;

  eid = svn_branch_family_add_new_element(branch->sibling_defn->family);

  SVN_DBG(("add(e%d): parent e%d, name '%s', kind %s",
           /*branch->sibling->bid,*/ eid, new_parent_eid,
           new_name, svn_node_kind_to_word(new_content->kind)));

  svn_branch_map_update(branch, eid, new_parent_eid, new_name, new_content);

  *eid_p = eid;
  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_instantiate(void *baton,
                    svn_branch_instance_t *branch,
                    svn_editor3_eid_t eid,
                    svn_editor3_eid_t new_parent_eid,
                    const char *new_name,
                    const svn_editor3_node_content_t *new_content,
                    apr_pool_t *scratch_pool)
{
  SVN_DBG(("add(e%d): parent e%d, name '%s', kind %s",
           /*branch->sibling->bid,*/ eid, new_parent_eid,
           new_name, svn_node_kind_to_word(new_content->kind)));

  svn_branch_map_update(branch, eid, new_parent_eid, new_name, new_content);
  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_copy_one(void *baton,
                 const svn_branch_el_rev_id_t *src_el_rev,
                 svn_branch_instance_t *branch,
                 svn_editor3_eid_t eid,
                 svn_editor3_eid_t new_parent_eid,
                 const char *new_name,
                 const svn_editor3_node_content_t *new_content,
                 apr_pool_t *scratch_pool)
{
  /* New content shall be the same as the source if NEW_CONTENT is null. */
  /* ### if (! new_content)
    {
      new_content = branch_map_get(branch, eid)->content;
    }
   */

  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_copy_tree(void *baton,
                  const svn_branch_el_rev_id_t *src_el_rev,
                  svn_branch_instance_t *to_branch,
                  svn_editor3_eid_t new_parent_eid,
                  const char *new_name,
                  apr_pool_t *scratch_pool)
{
  SVN_DBG(("copy_tree(e%d -> e%d/%s)",
           src_el_rev->eid, new_parent_eid, new_name));

  SVN_ERR(svn_branch_copy_subtree_r(src_el_rev,
                                    to_branch, new_parent_eid, new_name,
                                    scratch_pool));

  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_delete(void *baton,
                   svn_revnum_t since_rev,
                   svn_branch_instance_t *branch,
                   svn_editor3_eid_t eid,
                   apr_pool_t *scratch_pool)
{
  SVN_DBG(("delete(e%d)",
           /*branch->sibling_defn->bid,*/ eid));

  svn_branch_map_delete(branch, eid /* ### , since_rev? */);

  /* ### TODO: Delete nested branches. */

  return SVN_NO_ERROR;
}

/* An #svn_editor3_t method. */
static svn_error_t *
editor3_alter(void *baton,
              svn_revnum_t since_rev,
              svn_branch_instance_t *branch,
              svn_editor3_eid_t eid,
              svn_editor3_eid_t new_parent_eid,
              const char *new_name,
              const svn_editor3_node_content_t *new_content,
              apr_pool_t *scratch_pool)
{
  SVN_DBG(("alter(e%d): parent e%d, name '%s', kind %s",
           /*branch->sibling_defn->bid,*/ eid,
           new_parent_eid,
           new_name ? new_name : "(same)",
           new_content ? svn_node_kind_to_word(new_content->kind) : "(same)"));

  /* New content shall be the same as the before if NEW_CONTENT is null. */
  if (! new_content)
    {
      new_content = svn_branch_map_get(branch, eid)->content;
    }

  svn_branch_map_update(branch, eid, new_parent_eid, new_name, new_content);

  return SVN_NO_ERROR;
}

/* Update *PATHS, a hash of (rrpath -> svn_branch_el_rev_id_t),
 * creating or filling in entries for all elements in BRANCH.
 */
static void
convert_branch_to_paths(apr_hash_t *paths,
                        svn_branch_instance_t *branch,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  svn_branch_map_purge_orphans(branch, scratch_pool);
  for (hi = apr_hash_first(scratch_pool, branch->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int eid = *(const int *)apr_hash_this_key(hi);
      const char *relpath = svn_branch_get_path_by_eid(branch, eid,
                                                       result_pool);
      const char *rrpath
        = svn_relpath_join(svn_branch_get_root_rrpath(branch, scratch_pool),
                           relpath, result_pool);
      svn_branch_el_rev_id_t *ba = svn_hash_gets(paths, rrpath);

      /* Fill in the details. If it's already been filled in, then let a
         branch-root element override a sub-branch element of an outer
         branch, because the branch-root element is the one that should
         be specifying the element's content.
       */
      if (! ba
          || eid == branch->sibling_defn->root_eid)
        {
          ba = svn_branch_el_rev_id_create(branch, eid, branch->rev_root->rev,
                                           result_pool);
          svn_hash_sets(paths, rrpath, ba);
          /*SVN_DBG(("branch-to-path[%d]: b%d e%d -> %s",
                   i, branch->sibling_defn->bid, eid, rrpath));*/
        }
      else
        {
          SVN_DBG(("branch-to-path: b%d e%d -> <already present; not overwriting> (%s)",
                   branch->sibling_defn->bid, eid, rrpath));
        }
    }
}

/* Produce a mapping from paths to element ids, covering all elements in
 * BRANCH and all its sub-branches, recursively.
 *
 * Update *PATHS_UNION, a hash of (rrpath -> svn_branch_el_rev_id_t),
 * creating or filling in entries for all elements in all branches at and
 * under BRANCH, recursively.
 */
static void
convert_branch_to_paths_r(apr_hash_t *paths_union,
                          svn_branch_instance_t *branch,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_array_header_t *sub_branches;
  int i;

  /*SVN_DBG(("[%d] branch={b%de%d at '%s'}", idx,
           branch->sibling_defn->bid, branch->sibling_defn->root_eid, branch->branch_root_rrpath));*/
  convert_branch_to_paths(paths_union, branch,
                          result_pool, scratch_pool);

  /* Rercurse into sub-branches */
  sub_branches = svn_branch_get_all_sub_branches(branch,
                                                 scratch_pool, scratch_pool);
  for (i = 0; i < sub_branches->nelts; i++)
    {
      svn_branch_instance_t *b = APR_ARRAY_IDX(sub_branches, i, void *);

      convert_branch_to_paths_r(paths_union, b, result_pool, scratch_pool);
    }
}

/* Return TRUE iff INITIAL_CONTENT and FINAL_CONTENT are both non-null
 * and have the same properties.
 */
static svn_boolean_t
props_equal(svn_editor3_node_content_t *initial_content,
            svn_editor3_node_content_t *final_content,
            apr_pool_t *scratch_pool)
{
  apr_array_header_t *prop_diffs;

  if (!initial_content || !final_content)
    return FALSE;

  svn_error_clear(svn_prop_diffs(&prop_diffs,
                                 initial_content->props,
                                 final_content->props,
                                 scratch_pool));
  return (prop_diffs->nelts == 0);
}

/* Return TRUE iff INITIAL_CONTENT and FINAL_CONTENT are both file content
 * and have the same text.
 */
static svn_boolean_t
text_equal(svn_editor3_node_content_t *initial_content,
           svn_editor3_node_content_t *final_content)
{
  if (!initial_content || !final_content
      || initial_content->kind != svn_node_file
      || final_content->kind != svn_node_file)
    {
      return FALSE;
    }

  return svn_stringbuf_compare(initial_content->text,
                               final_content->text);
}

/* Return the copy-from location to be used if this is to be a copy;
 * otherwise return NULL.
 *
 * ### Currently this is indicated by content-by-reference, which is
 *     an inadequate indication.
 */
static svn_editor3_peg_path_t *
get_copy_from(svn_editor3_node_content_t *final_content)
{
  if (final_content->ref.relpath)
    {
      return &final_content->ref;
    }
  return NULL;
}

/* Return a hash whose keys are the names of the immediate children of
 * RRPATH in PATHS.
 */
static apr_hash_t *
get_immediate_children_names(apr_hash_t *paths,
                             const char *parent_rrpath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  apr_hash_t *children = apr_hash_make(result_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, paths); hi; hi = apr_hash_next(hi))
    {
      const char *this_rrpath = apr_hash_this_key(hi);

      if (this_rrpath[0]
          && strcmp(parent_rrpath, svn_relpath_dirname(this_rrpath,
                                                       scratch_pool)) == 0)
        {
          svn_hash_sets(children,
                        svn_relpath_basename(this_rrpath, result_pool), "");
        }
    }

  return children;
}

/* Return true iff EL_REV1 and EL_REV2 identify the same branch-family
 * and element.
 */
static svn_boolean_t
same_family_and_element(const svn_branch_el_rev_id_t *el_rev1,
                        const svn_branch_el_rev_id_t *el_rev2)
{
  if (el_rev1->branch->sibling_defn->family->fid
      != el_rev2->branch->sibling_defn->family->fid)
    return FALSE;
  if (el_rev1->eid != el_rev2->eid)
    return FALSE;

  return TRUE;
}

/* Generate Ev1 instructions to edit from a current state to a final state
 * at RRPATH, recursing for child paths of RRPATH.
 *
 * The current state at RRPATH might not be the initial state because,
 * although neither RRPATH nor any sub-paths have been explicitly visited
 * before, the current state at RRPATH and its sub-paths might be the
 * result of a copy.
 *
 * PRED_LOC is the predecessor location of the node currently at RRPATH in
 * the Ev1 transaction, or NULL if there is no node currently at RRPATH.
 * If the node is copied, including a child of a copy, this is its copy-from
 * location, otherwise this is its location in the txn base revision.
 * (The current node cannot be a plain added node on entry to this function,
 * as the function must be called only once for each path and there is no
 * recursive add operation.) PRED_LOC identifies the node content that the
 * that the Ev1 edit needs to delete, replace, update or leave unchanged.
 *
 */
static svn_error_t *
drive_changes_r(const char *rrpath,
                svn_editor3_peg_path_t *pred_loc,
                apr_hash_t *paths_final,
                ev3_from_delta_baton_t *eb,
                apr_pool_t *scratch_pool)
{
  /* The el-rev-id of the element that will finally exist at RRPATH. */
  svn_branch_el_rev_id_t *final_el_rev = svn_hash_gets(paths_final, rrpath);
  svn_editor3_node_content_t *final_content;
  svn_editor3_peg_path_t *final_copy_from;
  svn_boolean_t succession;

  SVN_DBG(("rrpath '%s' current=%s, final=e%d)",
           rrpath,
           pred_loc ? peg_path_str(*pred_loc, scratch_pool) : "<nil>",
           final_el_rev ? final_el_rev->eid : -1));

  SVN_ERR_ASSERT(!pred_loc
                 || (pred_loc->relpath && SVN_IS_VALID_REVNUM(pred_loc->rev)));
  /* A non-null FINAL address means an element exists there. */
  SVN_ERR_ASSERT(!final_el_rev
                 || svn_branch_map_get(final_el_rev->branch, final_el_rev->eid));

  if (final_el_rev)
    {
      final_content
        = svn_branch_map_get(final_el_rev->branch, final_el_rev->eid)->content;

      /* Decide whether the state at this path should be a copy (incl. a
         copy-child) */
      final_copy_from = get_copy_from(final_content);
      /* It doesn't make sense to have a non-copy inside a copy */
      /*SVN_ERR_ASSERT(!(parent_is_copy && !final_copy_from));*/
   }
  else
    {
      final_content = NULL;
      final_copy_from = NULL;
    }

  /* Succession means:
       for a copy (inc. child)  -- copy-from same place as natural predecessor
       otherwise                -- it's succession if it's the same element
                                   (which also implies the same kind) */
  if (pred_loc && final_copy_from)
    {
      succession = svn_editor3_peg_path_equal(pred_loc, final_copy_from);
    }
  else if (pred_loc && final_el_rev)
    {
      svn_branch_el_rev_id_t *pred_el_rev;

      SVN_ERR(svn_branch_repos_find_el_rev_by_path_rev(
                                            &pred_el_rev,
                                            pred_loc->relpath, pred_loc->rev,
                                            eb->edited_rev_root->repos,
                                            scratch_pool, scratch_pool));

      succession = same_family_and_element(pred_el_rev, final_el_rev);
    }
  else
    {
      succession = FALSE;
    }

  /* If there's an initial node that isn't also going to be the final
     node at this path, then it's being deleted or replaced: delete it. */
  if (pred_loc && !succession)
    {
      /* Issue an Ev1 delete unless this path is inside a path at which
         we've already issued a delete. */
      if (check_existence(eb->changes, rrpath) != svn_tristate_false)
        {
          SVN_DBG(("ev1:del(%s)", rrpath));
          /* ### We don't need "delete_subtree", we only need to insert a
             single delete operation, as we know we haven't
             inserted any changes inside this subtree. */
          SVN_ERR(delete_subtree(eb->changes, rrpath, pred_loc->rev));
        }
      else
        SVN_DBG(("ev1:del(%s): parent is already deleted", rrpath));
    }

  /* If there's a final node, it's being added or modified.
     Or it's unchanged -- we do nothing in that case. */
  if (final_el_rev)
    {
      svn_editor3_node_content_t *current_content = NULL;
      apr_hash_t *current_children = NULL;
      change_node_t *change = NULL;

      /* Get the full content of the final node. If we have
         only a reference to the content, fetch it in full. */
      SVN_ERR_ASSERT(final_content);
      if (final_content->ref.relpath)
        {
          /* Get content by reference. */
          SVN_ERR(content_fetch(&final_content, NULL,
                                eb, &final_content->ref,
                                scratch_pool, scratch_pool));
        }

      /* If the final node was also the initial node, it's being
         modified, otherwise it's being added (perhaps a replacement). */
      if (succession)
        {
          /* Get full content of the current node */
          SVN_ERR(content_fetch(&current_content, &current_children,
                                eb, pred_loc,
                                scratch_pool, scratch_pool));

          /* If no changes to make, then skip this path */
          if (svn_editor3_node_content_equal(current_content,
                                             final_content, scratch_pool))
            {
              SVN_DBG(("ev1:no-op(%s)", rrpath));
            }
          else
            {
              SVN_DBG(("ev1:mod(%s)", rrpath));
              SVN_ERR(insert_change(&change, eb->changes, rrpath,
                                    RESTRUCTURE_NONE));
              change->changing_rev = pred_loc->rev;
            }
        }
      else /* add or copy/move */
        {
          SVN_DBG(("ev1:add(%s)", rrpath));
          SVN_ERR(insert_change(&change, eb->changes, rrpath,
                                RESTRUCTURE_ADD));

          /* If content is to be copied (and possibly modified) ... */
          if (final_copy_from)
            {
              change->copyfrom_rev = final_copy_from->rev;
              change->copyfrom_path = final_copy_from->relpath;

              /* Get full content of the copy source node */
              SVN_ERR(content_fetch(&current_content, &current_children,
                                    eb, final_copy_from,
                                    scratch_pool, scratch_pool));
            }
        }

      if (change)
        {
          /* Copy the required content into the change record. Avoid no-op
             changes of props / text, not least to minimize clutter when
             debugging Ev1 operations. */
          SVN_ERR_ASSERT(final_content->kind == svn_node_dir
                         || final_content->kind == svn_node_file);
          change->kind = final_content->kind;
          if (!props_equal(current_content, final_content, scratch_pool))
            {
              change->props = final_content->props;
            }
          if (final_content->kind == svn_node_file
              && !text_equal(current_content, final_content))
            {
              change->contents_text = final_content->text;
            }
        }

      /* Recurse to process this directory's children */
      if (final_content->kind == svn_node_dir)
        {
          apr_hash_t *final_children;
          apr_hash_t *union_children;
          apr_hash_index_t *hi;

          final_children = get_immediate_children_names(paths_final, rrpath,
                                                        scratch_pool,
                                                        scratch_pool);
          union_children = (current_children
                            ? apr_hash_overlay(scratch_pool, current_children,
                                               final_children)
                            : final_children);
          for (hi = apr_hash_first(scratch_pool, union_children);
               hi; hi = apr_hash_next(hi))
            {
              const char *name = apr_hash_this_key(hi);
              const char *this_rrpath = svn_relpath_join(rrpath, name,
                                                         scratch_pool);
              svn_boolean_t child_in_current
                = current_children && svn_hash_gets(current_children, name);
              svn_editor3_peg_path_t *child_pred = NULL;

              if (child_in_current)
                {
                 /* If the parent dir is copied, then this child has been
                    copied along with it: predecessor is parent's copy-from
                    location extended by the child's name. */
                  child_pred = apr_palloc(scratch_pool, sizeof(*child_pred));
                  if (final_copy_from)
                    {
                      child_pred->rev = final_copy_from->rev;
                      child_pred->relpath
                        = svn_relpath_join(final_copy_from->relpath, name,
                                           scratch_pool);
                    }
                  else
                    {
                      child_pred->rev = pred_loc->rev;
                      child_pred->relpath = this_rrpath;
                    }
               }
              SVN_DBG(("child '%s' current=%s final? %d%s",
                       name,
                       child_pred ? peg_path_str(*child_pred, scratch_pool)
                                  : "<nil>",
                       (svn_hash_gets(final_children, name) != NULL),
                       final_copy_from
                         ? apr_psprintf(scratch_pool, " parent-cp-from=%s@%ld",
                                        final_copy_from->relpath,
                                        final_copy_from->rev) : ""));

              SVN_ERR(drive_changes_r(this_rrpath,
                                      child_pred,
                                      paths_final, eb, scratch_pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/*
 * Drive svn_delta_editor_t (actions: add/copy/delete/modify) from
 * a before-and-after element mapping.
 */
static svn_error_t *
drive_changes_branch(ev3_from_delta_baton_t *eb,
                     apr_pool_t *scratch_pool)
{
  apr_hash_t *paths_final;
  const apr_array_header_t *paths;

  /* Convert the element mappings to an svn_delta_editor_t traversal.

        1. find union of paths in initial and final states, across all branches.
        2. traverse paths in depth-first order.
        3. modify/delete/add/replace as needed at each path.
   */
  paths_final = apr_hash_make(scratch_pool);
  convert_branch_to_paths_r(paths_final, eb->edited_rev_root->root_branch,
                            scratch_pool, scratch_pool);

  {
    svn_editor3_peg_path_t current = { -1, "" };

    /* ### For now, assume based on youngest known rev. */
    current.rev = eb->edited_rev_root->repos->rev_roots->nelts - 1;
    SVN_ERR(drive_changes_r("", &current,
                            paths_final, eb, scratch_pool));
  }

  /* If the driver has not explicitly opened the root directory via the
     start_edit (aka open_root) callback, do so now. */
  if (eb->ev1_root_dir_baton == NULL)
    SVN_ERR(open_root_ev3(eb, SVN_INVALID_REVNUM));

  /* Make the path driver visit the root dir of the edit. Otherwise, it
     will attempt an open_root() instead, which we already did. */
  if (! svn_hash_gets(eb->changes, eb->base_relpath))
    {
      change_node_t *change;

      SVN_ERR(insert_change(&change, eb->changes, eb->base_relpath,
                            RESTRUCTURE_NONE));
      change->kind = svn_node_dir;
    }

  /* Apply the appropriate Ev1 change to each Ev1-relative path. */
  paths = get_unsorted_paths(eb->changes, eb->base_relpath, scratch_pool);
  SVN_ERR(svn_delta_path_driver2(eb->deditor, eb->dedit_baton,
                                 paths, TRUE /*sort*/,
                                 apply_change, (void *)eb,
                                 scratch_pool));

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
  err = drive_changes_branch(eb, scratch_pool);
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
  err = drive_changes_branch(eb, scratch_pool);
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
svn_delta__ev3_from_delta_for_commit2(
                        svn_editor3_t **editor_p,
                        svn_editor3__shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        svn_branch_revision_root_t *branching_txn,
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
    NULL,
    NULL,
    NULL,
#ifdef SVN_EDITOR3_WITH_RESURRECTION
    NULL,
#endif
    NULL,
    NULL,
    editor3_add,
    editor3_instantiate,
    editor3_copy_one,
    editor3_copy_tree,
    editor3_delete,
    editor3_alter,
    editor3_complete,
    editor3_abort
  };
  ev3_from_delta_baton_t *eb = apr_pcalloc(result_pool, sizeof(*eb));

  eb->deditor = deditor;
  eb->dedit_baton = dedit_baton;

  eb->repos_root_url = apr_pstrdup(result_pool, repos_root_url);
  eb->base_relpath = apr_pstrdup(result_pool, base_relpath);

  eb->changes = apr_hash_make(result_pool);

  eb->fetch_func = fetch_func;
  eb->fetch_baton = fetch_baton;

  eb->edit_pool = result_pool;

  *editor_p = svn_editor3_create(&editor_funcs, eb,
                                 cancel_func, cancel_baton, result_pool);

  /* Find what branch we are editing, based on BASE_RELPATH, and capture
     its initial state.
     ### TODO: Instead, have edit operations specify the branch(es) they
         are operating on, since operations such as "branch", "branchify",
         and those that recurse into sub-branches operate on more than one.
   */
  eb->edited_rev_root = branching_txn;

  if (shim_connector)
    {
#if 0
      *shim_connector = apr_palloc(result_pool, sizeof(**shim_connector));
#ifdef SHIM_WITH_ABS_PATHS
      (*shim_connector)->ev1_absolute_paths
        = apr_palloc(result_pool, sizeof(svn_boolean_t));
      eb->make_abs_paths = (*shim_connector)->ev1_absolute_paths;
#endif
      (*shim_connector)->target_revision_func = set_target_revision_ev3;
      (*shim_connector)->start_edit_func = open_root_ev3;
#ifdef SHIM_WITH_UNLOCK
      (*shim_connector)->unlock_func = do_unlock;
#endif
      (*shim_connector)->baton = eb;
#endif
    }

  return SVN_NO_ERROR;
}
