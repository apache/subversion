/*
 * adm_crawler.c:  report local WC mods to an Editor.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */


#include "apr_pools.h"
#include "apr_file_io.h"
#include "apr_hash.h"
#include "wc.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_sorts.h"
#include "svn_delta.h"

#include <assert.h>


/* The values stored in `affected_targets' hashes are of this type.
 *
 * Ben: I think this is the start of a larger change, in which all
 * entries affected by the commit -- dirs and files alike -- are
 * stored in the affected_targets hash, and their entries are recorded
 * along with the baton that needs to be passed to the editor
 * callbacks.  
 * 
 * At that point, stack_object would hold a (struct target_baton *)
 * instead of an entry and an editor baton, and push_stack() would
 * take a struct (target_baton *).  The other changes follow from
 * there, etc.
 *
 * However, since directory adds/deletes are not supported, I've not
 * started storing directories in the affected_targets hash.
 */
struct target_baton
{
  svn_wc_entry_t *entry;
  void *editor_baton;

  svn_boolean_t text_modified_p;
  svn_boolean_t prop_modified_p;
};


/* Local "stack" objects used by the crawler to keep track of dir
   batons. */
struct stack_object
{
  svn_stringbuf_t *path;         /* A working copy directory */
  void *baton;                /* An associated dir baton, if any exists yet. */
  svn_wc_entry_t *this_dir;   /* All entry info about this directory */

  struct stack_object *next;
  struct stack_object *previous;
};




/* Create a new stack object containing {PATH, BATON, ENTRY} and push
   it on top of STACK. */
static void
push_stack (struct stack_object **stack,
            svn_stringbuf_t *path,
            void *baton,
            svn_wc_entry_t *entry,
            apr_pool_t *pool)
{
  struct stack_object *new_top = apr_pcalloc (pool, sizeof (*new_top));

  /* Store path and baton in a new stack object */
  new_top->path = svn_string_dup (path, pool);
  new_top->baton = baton;
  new_top->this_dir = entry;
  new_top->next = NULL;
  new_top->previous = NULL;

  if (*stack == NULL)
    /* This will be the very first object on the stack. */
    *stack = new_top;
  
  else 
    {
      /* The stack already exists, so create links both ways, new_top
         becomes the top of the stack.  */

      (*stack)->next = new_top;
      new_top->previous = *stack;
      *stack = new_top;
    }
}


/* Remove youngest stack object from STACK. */
static void
pop_stack (struct stack_object **stack)
{
  struct stack_object *new_top;

  if ((*stack)->previous)
    {
      new_top = (*stack)->previous;
      new_top->next = NULL;
      *stack = new_top;
    }
}



/* Remove administrative-area locks on each path in LOCKS hash */
static svn_error_t *
remove_all_locks (apr_hash_t *locks, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  
  for (hi = apr_hash_first (locks); hi; hi = apr_hash_next (hi))
    {
      svn_error_t *err;
      const void *key;
      void *val;
      apr_size_t klen;
      svn_stringbuf_t *unlock_path;
      
      apr_hash_this (hi, &key, &klen, &val);
      unlock_path = svn_string_create ((char *)key, pool);
      
      err = svn_wc__unlock (unlock_path, pool);
      if (err) 
        {
          char *message =
            apr_psprintf (pool,
                          "remove_all_locks:  couldn't unlock %s",
                          unlock_path->data);
          return svn_error_quick_wrap (err, message);
        }          
    }

  return SVN_NO_ERROR;
}



/* Attempt to grab a lock in PATH.  If we succeed, store PATH in LOCKS
   and return success.  If we fail to grab a lock, remove all locks in
   LOCKS and return error. */
static svn_error_t *
do_lock (svn_stringbuf_t *path, apr_hash_t *locks, apr_pool_t *pool)
{
  svn_error_t *err, *err2;
      
  err = svn_wc__lock (path, 0, pool);
  if (err)
    {
      /* Couldn't lock: */
      
      /* Remove _all_ previous commit locks */
      err2 = remove_all_locks (locks, pool);
      if (err2) 
        {
          /* If this also errored, put the original error inside it. */
          err2->child = err;
          return err2;
        }
      
      return err;
    }
  
  /* Lock succeeded */
  apr_hash_set (locks, path->data, APR_HASH_KEY_STRING, "(locked)");

  return SVN_NO_ERROR;
}






/* Given the path on the top of STACK, store (and return) NEWEST_BATON
   -- which allows one to edit entries there.  Fetch and store (in
   STACK) any previous directory batons necessary to create the one
   for path (..using calls from EDITOR.)  For every directory baton
   generated, lock the directory as well and store in LOCKS using
   TOP_POOL.  */
static svn_error_t *
do_dir_replaces (void **newest_baton,
                 struct stack_object *stack,
                 const svn_delta_edit_fns_t *editor,
                 void *edit_baton,
                 apr_hash_t *locks,
                 apr_pool_t *top_pool,
                 apr_pool_t *pool)

{
  svn_error_t *err;
  struct stack_object *stackptr;  /* The current stack object we're
                                     examining */

  stackptr = stack;   /* Start at the top of the stack */
  
  while (1)  /* Walk down the stack until we find a non-NULL dir baton. */
    {
      if (stackptr->baton != NULL) 
        /* Found an existing directory baton! */
        break;
      
      if (stackptr->previous)  
        stackptr = stackptr->previous;  /* descend. */

      else
        {
          /* Can't descend?  We must be at stack bottom, fetch the
             root baton. */
          void *root_baton;

          err = editor->replace_root (edit_baton,
                                      stackptr->this_dir->revision, 
                                      &root_baton);  
          if (err) return err;
          
          /* Store it */
          stackptr->baton = root_baton;
          break;
        }
    }

  /* Now that we're outside the while() loop, our stackptr is pointing
     to the frame with the "youngest" directory baton. */

  while (1)  /* Now walk _up_ the stack, creating & storing new batons. */
    {
      if (stackptr->next)
        {
          svn_stringbuf_t *dirname;
          void *dir_baton;

          /* Move up the stack */
          stackptr = stackptr->next;

          /* We only want the last component of the path; that's what
             the editor's replace_directory() expects from us. */
          dirname = svn_path_last_component (stackptr->path,
                                             svn_path_local_style, pool);

          /* Get a baton for this directory */
          err = 
            editor->replace_directory (dirname, /* current dir */
                                       stackptr->previous->baton, /* parent */
                                       stackptr->this_dir->revision,
                                       &dir_baton);
          if (err) return err;

          /* Store it */
          stackptr->baton = dir_baton;
        }
      
      else 
        /* Can't move up the stack anymore?  We must be at the top
           of the stack.  We're all done. */
        break;
    }

  /* Return (by reference) the youngest directory baton, the one that
     goes with our youngest PATH */
  *newest_baton = stackptr->baton;

  /* Lock this youngest directory */
  err = do_lock (svn_string_dup (stackptr->path, top_pool), locks, top_pool);
  if (err) return err;
  
  return SVN_NO_ERROR;
}




/* Remove stackframes from STACK until the top points to DESIRED_PATH.
   Before stack frames are popped, call EDITOR->close_directory() on
   any non-null batons. */
static svn_error_t *
do_dir_closures (svn_stringbuf_t *desired_path,
                 struct stack_object **stack,
                 const svn_delta_edit_fns_t *editor)
{
   while (svn_path_compare_paths (desired_path, (*stack)->path,
                                 svn_path_local_style))
    {
      if ((*stack)->baton)
        SVN_ERR (editor->close_directory ((*stack)->baton));
      
      pop_stack (stack);
    }

  return SVN_NO_ERROR;
}





/* Examine both the local and text-base copies of a file FILENAME, and
   push a text-delta to EDITOR using the already-opened FILE_BATON.
   (FILENAME is presumed to be a full path ending with a filename.) */
static svn_error_t *
do_apply_textdelta (svn_stringbuf_t *filename,
                    const svn_delta_edit_fns_t *editor,
                    struct target_baton *tb,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t status;

  svn_txdelta_window_handler_t window_handler;
  void *window_handler_baton;

  svn_txdelta_stream_t *txdelta_stream;
  svn_txdelta_window_t *txdelta_window;

  apr_file_t *localfile = NULL;
  apr_file_t *textbasefile = NULL;

  svn_stringbuf_t *local_tmp_path;

  /* Tell the editor that we're about to apply a textdelta to the file
     baton; the editor returns to us a window consumer routine and
     baton. */
  err = editor->apply_textdelta (tb->editor_baton,
                                 &window_handler,
                                 &window_handler_baton);
  if (err) return err;

  /* Copy the local file to the administrative temp area. */
  local_tmp_path = svn_wc__text_base_path (filename, TRUE, pool);
  err = svn_io_copy_file (filename, local_tmp_path, pool);
  if (err) return err;

  /* Open a filehandle for tmp local file, and one for text-base if
     applicable. */
  status = apr_file_open (&localfile, local_tmp_path->data,
                     APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return
      svn_error_createf (status, 0, NULL, pool,
                         "do_apply_textdelta: error opening '%s'",
                         local_tmp_path->data);

  textbasefile = NULL; /* paranoia! */
  if ((! (tb->entry->schedule == svn_wc_schedule_add))
      && (! (tb->entry->schedule == svn_wc_schedule_replace)))
    {
      err = svn_wc__open_text_base (&textbasefile, filename, APR_READ, pool);
      if (err)
        return err;
    }
                                
  /* Create a text-delta stream object that pulls data out of the two
     files. */
  svn_txdelta (&txdelta_stream,
               svn_stream_from_aprfile (textbasefile, pool),
               svn_stream_from_aprfile (localfile, pool),
               pool);
  
  /* Grab a window from the stream, "push" it at the consumer routine,
     then free it.  (When we run out of windows, TXDELTA_WINDOW will
     be set to NULL, and then still passed to window_handler(),
     thereby notifying window_handler that we're all done.)  */
  do
    {
      err = svn_txdelta_next_window (&txdelta_window, txdelta_stream);
      if (err) return err;
      
      err = (* (window_handler)) (txdelta_window, window_handler_baton);
      if (err) return err;
      
      svn_txdelta_free_window (txdelta_window);

    } while (txdelta_window);


  /* Free the stream */
  svn_txdelta_free (txdelta_stream);

  /* Close the two files */
  status = apr_file_close (localfile);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "do_apply_textdelta: error closing local file");

  if (textbasefile)
    {
      err = svn_wc__close_text_base (textbasefile, filename, 0, pool);
      if (err) return err;
    }

  return SVN_NO_ERROR;
}


/* Loop over AFFECTED_TARGETS, calling do_apply_textdelta().
   AFFECTED_TARGETS, if non-empty, contains a mapping of full file
   paths to still-open file_batons.  After sending each text-delta,
   close each file_baton. */ 
static svn_error_t *
do_postfix_text_deltas (apr_hash_t *affected_targets,
                        const svn_delta_edit_fns_t *editor,
                        apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_index_t *hi;
  svn_stringbuf_t *entrypath;
  struct target_baton *tb;
  const void *key;
  void *val;
  size_t keylen;

  for (hi = apr_hash_first (affected_targets); hi; hi = apr_hash_next (hi))
    {
      apr_hash_this (hi, &key, &keylen, &val);
      tb = val;

      if (tb->text_modified_p)
        {
          entrypath = svn_string_create ((char *) key, pool);
          
          err = do_apply_textdelta (entrypath, editor, tb, pool);
          if (err) return err;
          
          err = editor->close_file (tb->editor_baton);
          if (err) return err;
        }
    }

  return SVN_NO_ERROR;
}



/* Given a full PATH to a particular ENTRY, apply all local property
   changes via EDITOR callbacks with the appropriate file or directory
   BATON. */
static svn_error_t *
do_prop_deltas (svn_stringbuf_t *path,
                svn_wc_entry_t *entry,
                const svn_delta_edit_fns_t *editor,
                void *baton,
                apr_pool_t *pool)
{
  svn_error_t *err;
  int i;
  svn_stringbuf_t *prop_path, *prop_base_path, *tmp_prop_path;
  apr_array_header_t *local_propchanges;
  apr_hash_t *localprops = apr_hash_make (pool);
  apr_hash_t *baseprops = apr_hash_make (pool);
  
  /* First, get the prop_path from the original path */
  err = svn_wc__prop_path (&prop_path, path, 0, pool);
  if (err) return err;
  
  /* Get the full path of the prop-base `pristine' file */
  err = svn_wc__prop_base_path (&prop_base_path, path, 0, pool);
  if (err) return err;

  /* Copy the local prop file to the administrative temp area */
  err = svn_wc__prop_path (&tmp_prop_path, path, 1, pool);
  if (err) return err;
  err = svn_io_copy_file (prop_path, tmp_prop_path, pool);
  if (err) return err;

  /* Load all properties into hashes */
  err = svn_wc__load_prop_file (tmp_prop_path,
                                localprops, pool);
  if (err) return err;
  
  err = svn_wc__load_prop_file (prop_base_path,
                                baseprops, pool);
  if (err) return err;
  
  /* Get an array of local changes by comparing the hashes. */
  err = svn_wc__get_local_propchanges (&local_propchanges,
                                       localprops,
                                       baseprops,
                                       pool);
  if (err) return err;
  
  /* Apply each local change to the baton */
  for (i = 0; i < local_propchanges->nelts; i++)
    {
      svn_prop_t *change;

      change = (((svn_prop_t **)(local_propchanges)->elts)[i]);
      
      if (entry->kind == svn_node_file)
        err = editor->change_file_prop (baton,
                                        change->name,
                                        change->value);
      else
        err = editor->change_dir_prop (baton,
                                       change->name,
                                       change->value);
      if (err) return err;
    }

  return SVN_NO_ERROR;
}




/* Decide if the file or dir represented by ENTRY continues to exist
   in a state of conflict.  If so, aid in the bailout of the current
   commit by unlocking all admin-area locks in LOCKS and returning an
   error.
   
   Obviously, this routine should only be called on entries who have
   the `conflicted' flag bit set.  */
static svn_error_t *
bail_if_unresolved_conflict (svn_stringbuf_t *full_path,
                             svn_wc_entry_t *entry,
                             apr_hash_t *locks,
                             apr_pool_t *pool)
{
  svn_error_t *err;

  if (entry->conflicted)
    {
      /* We must decide if either component is "conflicted", based
         on whether reject files are mentioned and/or continue to
         exist.  Luckily, we have a function to do this.  :) */
      svn_boolean_t text_conflict_p, prop_conflict_p;
      svn_stringbuf_t *parent_dir;
      
      if (entry->kind == svn_node_file)
        {
          parent_dir = svn_string_dup (full_path, pool);
          svn_path_remove_component (parent_dir, svn_path_local_style);
        }
      else if (entry->kind == svn_node_dir)
        parent_dir = full_path;
      
      err = svn_wc_conflicted_p (&text_conflict_p,
                                 &prop_conflict_p,
                                 parent_dir,
                                 entry,
                                 pool);
      if (err) return err;

      if ((! text_conflict_p) && (! prop_conflict_p))
        return SVN_NO_ERROR;

      else /* a tracked .rej or .prej file still exists */
        {
          svn_error_t *final_err;
          
          final_err =
            svn_error_createf (SVN_ERR_WC_FOUND_CONFLICT, 0, NULL, pool,
                               "Aborting commit: '%s' remains in conflict.",
                               full_path->data);
          
          err = remove_all_locks (locks, pool);
          if (err)
            final_err->child = err; /* nestle them */
          
          return final_err;
        }
    }
  
  return SVN_NO_ERROR;
}


/* Given a directory DIR under revision control with schedule
   SCHEDULE:

   - if SCHEDULE is svn_wc_schedule_delete, all children of this
     directory must have a schedule _delete.

   - else, if SCHEDULE is svn_wc_schedule_replace, all children of
     this directory must have a schedule of either _add or _delete.

   - else, this directory must not be marked for deletion, which is an
     automatic to fail this verifation!
*/
static svn_error_t *
verify_tree_deletion (svn_stringbuf_t *dir,
                      enum svn_wc_schedule_t schedule,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_stringbuf_t *fullpath = svn_string_dup (dir, pool);

  if ((schedule != svn_wc_schedule_delete) 
      && (schedule != svn_wc_schedule_replace))
    {
      return svn_error_createf 
        (SVN_ERR_WC_FOUND_CONFLICT, 0, NULL, pool,
         "Aborting commit: '%s' not scheduled for deletion as expected.",
         dir->data);
    }

  /* Read the entries file for this directory. */
  SVN_ERR (svn_wc_entries_read (&entries, dir, pool));

  /* Delete each entry in the entries file. */
  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t klen;
      void *val;
      svn_wc_entry_t *entry; 
      int is_this_dir;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      entry = (svn_wc_entry_t *) val;
      is_this_dir = strcmp (key, SVN_WC_ENTRY_THIS_DIR) == 0;

      /* Construct the fullpath of this entry. */
      if (! is_this_dir)
        svn_path_add_component_nts (fullpath, key, svn_path_local_style);

      /* If parent is marked for deletion only, this entry must be
         marked the same way. */
      if ((schedule == svn_wc_schedule_delete)
          && (entry->schedule != svn_wc_schedule_delete))
        {
          return svn_error_createf 
            (SVN_ERR_WC_FOUND_CONFLICT, 0, NULL, pool,
             "Aborting commit: '%s' dangling in deleted directory.",
             fullpath->data);
        }
      /* If parent is marked for both deletion and addition, this
         entry must be marked for either deletion, addition, or
         replacement. */
      if ((schedule == svn_wc_schedule_replace)
          && (! ((entry->schedule == svn_wc_schedule_delete)
                 || (entry->schedule == svn_wc_schedule_add)
                 || (entry->schedule == svn_wc_schedule_replace))))
        {
          return svn_error_createf 
            (SVN_ERR_WC_FOUND_CONFLICT, 0, NULL, pool,
             "Aborting commit: '%s' dangling in replaced directory.",
             fullpath->data);
        }

      /* Recurse on subdirectories. */
      if ((entry->kind == svn_node_dir) && (! is_this_dir))
        SVN_ERR (verify_tree_deletion (fullpath, entry->schedule, subpool));

      /* Reset FULLPATH to just hold this dir's name. */
      svn_string_set (fullpath, dir->data);

      /* Clear our per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Destroy our per-iteration pool. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}








/* A recursive working-copy "crawler", used to drive commits.

   Enter directory PATH and recursively report any local changes to
   EDITOR.

   The DIR_BATON argument holds the current baton used to commit
   changes from PATH.  It may be NULL.  If it is NULL and a local
   change is discovered, then it (and all parent batons) will be
   automatically generated by do_dir_replaces(). 

   FILENAME is ordinarily NULL;  if not, this routine will *not* be
   recursive.  Instead, it will (potentially) report local mods on the
   entry named FILENAME within PATH and return.

   Open file-batons will be stored in AFFECTED_TARGETS using the
   never-changing top-level pool TOP_POOL (for submitting postfix
   text-deltas later.)  Any working copy dirs that are locked are
   appended to LOCKS.

   STACK should begin either as NULL, or pointing at the parent of
   PATH.  Stackframes are automatically pushed/popped as the crawl
   proceeds.  When this function returns, the top of stack will be
   exactly where it was. */

static svn_error_t *
report_local_mods (svn_stringbuf_t *path,
                   void *dir_baton,
                   svn_stringbuf_t *filename,
                   const svn_delta_edit_fns_t *editor,
                   void *edit_baton,
                   struct stack_object **stack,
                   apr_hash_t *affected_targets,
                   apr_hash_t *locks,
                   apr_pool_t *top_pool)
{
  svn_error_t *err;
  apr_hash_t *entries;            /* _all_ of the entries in in
                                     current directory */
  apr_hash_index_t *entry_index;  /* holds loop-state */
  svn_wc_entry_t *this_dir;       /* represents current working dir */
  void *new_dir_baton = NULL;     /* potential child dir baton */
  int only_add_entries = FALSE;   /* whether we should only notice
                                     added things. */

  /**                                                   **/
  /** Setup -- arrival in a new subdir of working copy. **/
  /**                                                   **/

  /* First thing to do is create a new subpool to hold all temporary
     work at this level of the working copy. */
  /* ### this pool is not properly cleared/destroyed on exit */
  apr_pool_t *subpool = svn_pool_create (top_pool);

  /* Retrieve _all_ the entries in this subdir into subpool. */
  SVN_ERR (svn_wc_entries_read (&entries, path, subpool));

  /* Grab the entry representing "." */
  this_dir = (svn_wc_entry_t *) 
    apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
  if (! this_dir)
    return
      svn_error_createf (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, subpool,
                         "Can't find `.' entry in %s", path->data);

  /* If the `.' entry is marked with ADD, then we *only* want to
     notice child entries that are also added.  It makes no sense to
     look for deletes or local mods in an added directory. */
  if ((this_dir->schedule == svn_wc_schedule_add)
      || (this_dir->schedule == svn_wc_schedule_replace))
    only_add_entries = TRUE;
                             
  /**                           **/
  /** Main Logic                **/
  /**                           **/

  /* Else do real recursion on PATH. */

  /* Push the current {path, baton, this_dir} to the top of the stack
     -- unless we're committing a single file.  If we're only
     committing one file, then then PATH is the file's parent and is
     *already* on the stack. */
  if (! filename)
    push_stack (stack, path, dir_baton, this_dir, subpool);
  
  /* Loop over each entry */
  for (entry_index = apr_hash_first (entries); entry_index;
       entry_index = apr_hash_next (entry_index))
    {
      const void *key;
      const char *keystring;
      apr_size_t klen;
      void *val;
      svn_stringbuf_t *current_entry_name;
      svn_wc_entry_t *current_entry; 
      svn_stringbuf_t *full_path_to_entry;
      svn_boolean_t do_add;
      svn_boolean_t do_delete;
      
      /* Get the next entry name (and structure) from the hash */
      apr_hash_this (entry_index, &key, &klen, &val);
      keystring = (const char *) key;
      
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
        current_entry_name = NULL;
      else
        current_entry_name = svn_string_create (keystring, subpool);
      current_entry = (svn_wc_entry_t *) val;

      /* This entry gets deleted if all of the following hold true:

         a) the algorithm is not in "adds only" mode
         b) the entry is marked for deletion or replacement
         c) the entry is NOT the "this dir" entry
      */
      do_delete = ((! only_add_entries)
                   && current_entry_name
                   && ((current_entry->schedule == svn_wc_schedule_delete)
                       || (current_entry->schedule ==
                           svn_wc_schedule_replace))) ? TRUE : FALSE;

      /* This entry gets added if all of the following hold true:

         a) the entry is marked for addition or replacement
         b) the entry is NOT the "this dir" entry
      */
      do_add = (current_entry_name
                && ((current_entry->schedule == svn_wc_schedule_add)
                    || (current_entry->schedule ==
                        svn_wc_schedule_replace))) ? TRUE : FALSE;

      /* If we're only looking to commit a single file in this dir,
         then skip past all irrelevant entries. */
      if (filename)
        {
          if (! current_entry_name)
            continue;                   /* skip THIS_DIR */
          if (! svn_string_compare (filename, current_entry_name))
            continue;                   /* skip differing entryname */
        }

      /* Construct a full path to the current entry */
      full_path_to_entry = svn_string_dup (path, subpool);
      if (current_entry_name != NULL)
        svn_path_add_component (full_path_to_entry, current_entry_name,
                                svn_path_local_style);

      /* Preemptive strike:  if the current entry is a file in a state
         of conflict that has NOT yet been resolved, we abort the
         entire commit.  */
      SVN_ERR(bail_if_unresolved_conflict (full_path_to_entry,
                                           current_entry,
                                           locks, subpool));

      /* -- Start examining current_entry -- */

      /* Here's a guide to the very long logic that extends below.
         For each entry in the current dir (PATH), the examination
         looks like this:

              if (deleted)...
              if (added)...
              else if (local mods)...
              if (dir)
                recurse()
       */
      
      /* DELETION CHECK */
      if (do_delete)
        {
          /* Do what's necessary to get a baton for current directory */
          if (! dir_baton)
            SVN_ERR(do_dir_replaces (&dir_baton,
                                     *stack, editor, edit_baton,
                                     locks, top_pool, subpool));
      
          /* If this is a directory, we do a sanity check and make sure
             that all the directory's children are also marked for
             deletion.  If not, we're in a screwy state. */
          if (current_entry->kind == svn_node_dir)
            SVN_ERR (verify_tree_deletion (full_path_to_entry, 
                                           current_entry->schedule,
                                           subpool));

          /* Delete the entry */
          err = editor->delete_entry (current_entry_name, dir_baton);
          if (err) return err;
          
          /* Remember that it was affected. */
          {
            svn_stringbuf_t *longpath;
            struct target_baton *tb = apr_pcalloc (top_pool, sizeof (*tb));
            
            tb->entry = svn_wc__entry_dup (current_entry, top_pool);
            longpath = svn_string_dup (full_path_to_entry, top_pool);
            apr_hash_set (affected_targets, longpath->data, longpath->len, tb);
          }
        }  /* END DELETION CHECK */
  

      /* ADDITION CHECK */
      if (do_add)
        {
          /* Create an affected-target object */
          svn_stringbuf_t *longpath;
          struct target_baton *tb = apr_pcalloc (top_pool, sizeof (*tb));
          tb->entry = svn_wc__entry_dup (current_entry, top_pool);          
      
          /* Do what's necesary to get a baton for current directory */
          if (! dir_baton)
            SVN_ERR(do_dir_replaces (&dir_baton,
                                     *stack, editor, edit_baton,
                                     locks, top_pool, subpool));
      
          /* Adding a new directory: */
          if (current_entry->kind == svn_node_dir)
            {             
              svn_stringbuf_t *copyfrom_URL = NULL;
              svn_wc_entry_t *subdir_entry;
                    
              /* A directory's interesting information is stored in
                 its own THIS_DIR entry, so read that to get the real
                 data for this directory. */
              SVN_ERR (svn_wc_entry (&subdir_entry, 
                                     full_path_to_entry, subpool));
              
              /* If the directory is completely new, the wc records
                 its pre-committed revision as "0", even though it may
                 have a "default" URL listed.  But the delta.h
                 docstring for add_directory() says that the copyfrom
                 args must be either both valid or both invalid. */
              if (subdir_entry->revision > 0)
                copyfrom_URL = subdir_entry->ancestor;
              
              /* Add the new directory, getting a new dir baton.  */
              SVN_ERR(editor->add_directory (current_entry_name,
                                             dir_baton, /* parent baton */
                                             copyfrom_URL,
                                             subdir_entry->revision,
                                             &new_dir_baton)); /* get child */
            }
      
          /* Adding a new file: */
          else if (current_entry->kind == svn_node_file)
            {
              /* Add a new file, getting a file baton */
              SVN_ERR(editor->add_file (current_entry_name,
                                        dir_baton,         /* parent baton */
                                        current_entry->ancestor,
                                        current_entry->revision,
                                        &(tb->editor_baton))); /* get child */
          
              /* This might be a *newly* added file, in which case the
                 revision is 0 or invalid; assume that the contents
                 need to be sent. */
              if ((current_entry->revision == 0) 
                  || (! SVN_IS_VALID_REVNUM(current_entry->revision)))
                tb->text_modified_p = TRUE;
              else
                /* This file might be added with history;  in this
                   case, we only *might* need to send contents.  Do a
                   real local-mod check on it. */
                SVN_ERR (svn_wc_text_modified_p (&(tb->text_modified_p),
                                                 full_path_to_entry,
                                                 subpool));
            }
      
          /* Check for local property changes to send, whether we're
             looking a file or a dir.  */
          {
            svn_boolean_t prop_modified_p;        
            SVN_ERR(svn_wc_props_modified_p (&prop_modified_p,
                                             full_path_to_entry,
                                             subpool));
            if (prop_modified_p)
              {
                void *baton = (current_entry->kind == svn_node_file) ?
                  tb->editor_baton : new_dir_baton;
                
                /* Send propchanges to the editor. */
                SVN_ERR(do_prop_deltas (full_path_to_entry, current_entry,
                                        editor, baton, subpool));
            
                tb->prop_modified_p = TRUE;
              }            
          }          
      
          /* Store the (added) affected-target for safe keeping
             (possibly to be used later for postfix text-deltas) */
          longpath = svn_string_dup (full_path_to_entry, top_pool);
          apr_hash_set (affected_targets, longpath->data, longpath->len, tb);

        } /* END ADDITION CHECK */
  

      /* LOCAL MOD CHECK */
      else if (! only_add_entries)
        {
          svn_boolean_t text_modified_p, prop_modified_p;
          
          /* Is text modified? */
          SVN_ERR(svn_wc_text_modified_p (&text_modified_p, full_path_to_entry,
                                          subpool));
          
          /* Only check for local propchanges if we're looking at a
             file, or if we're looking at SVN_WC_ENTRY_THIS_DIR.
             Otherwise, each directory will end up being checked
             twice! */
          if ((current_entry->kind == svn_node_dir)
              && (current_entry_name != NULL))
            /* always assume there's no propchange on `PATH/DIR_NAME' */
            prop_modified_p = FALSE;
          else
            /* but do a real check on `PATH/.' */
            SVN_ERR(svn_wc_props_modified_p (&prop_modified_p,
                                             full_path_to_entry, subpool));
      
          if (text_modified_p || prop_modified_p)
            {
              svn_stringbuf_t *longpath;
              struct target_baton *tb;
          
              /* There was a local change.  Build an affected-target
                 object in the top-most pool. */
              tb = apr_pcalloc (top_pool, sizeof (*tb));
              tb->entry = svn_wc__entry_dup (current_entry, top_pool);
              tb->text_modified_p = text_modified_p;
              tb->prop_modified_p = prop_modified_p;
          
              /* Build the full path to this entry, also from the
                 top-pool. */
              longpath = svn_string_dup (full_path_to_entry, top_pool);
          
              /* Do what's necesary to get a baton for current directory */
              if (! dir_baton)
                SVN_ERR(do_dir_replaces (&dir_baton,
                                         *stack, editor, edit_baton,
                                         locks, top_pool, subpool));
          
              if (current_entry->kind == svn_node_file)
                /* Replace the file's text, getting a new file baton */
                SVN_ERR(editor->replace_file (current_entry_name,
                                              dir_baton,          /* parent */
                                              current_entry->revision,
                                              &(tb->editor_baton)));/* child */
          
              if (prop_modified_p)
                {
                  void *baton = (current_entry->kind == svn_node_file) ?
                    tb->editor_baton : dir_baton;
              
                  /* Send propchanges to editor. */
                  SVN_ERR(do_prop_deltas (longpath, current_entry,
                                          editor, baton, subpool));
                  
                  /* Very important: if there are *only* propchanges,
                     but not textual ones, close the file here and
                     now.  (Otherwise the file will be closed after
                     sending postfix text-deltas.)*/
                  if ((current_entry->kind == svn_node_file)
                      && (! text_modified_p))
                    SVN_ERR (editor->close_file (tb->editor_baton));
                }
          
              /* Store the affected-target for safe keeping (possibly
                 to be used later for postfix text-deltas) */
              apr_hash_set (affected_targets, longpath->data,
                            longpath->len, tb);
            }

        }  /* END LOCAL MOD CHECK */
  
      /* Finally, decide whether or not to recurse. Recurse only if
         all these things are true:

           * current_entry is a directory and not `.'
           * current_entry is not *only* marked for deletion
             (if marked for deletion and addition both, we want to recurse.)
      */
      if ((current_entry->kind == svn_node_dir) 
          && (current_entry_name)
          && (current_entry->schedule != svn_wc_schedule_delete))
        {
          /* Recurse, using new_dir_baton, which will most often be
             NULL (unless the entry is a newly added directory.)  Why
             NULL?  Because that will later force a call to
             do_dir_replaces() and get the _correct_ dir baton for the
             child directory. */
          SVN_ERR (report_local_mods (full_path_to_entry, new_dir_baton, 
                                      filename, editor, edit_baton, stack,
                                      affected_targets, locks, top_pool));
        }
      /* -- Done examining current_entry --  */      
    }

  
  /**                                                           **/
  /** Cleanup -- ready to "pop up" a level in the working copy. **/
  /**                                                           **/

  /* If we only committed one file in this dir, do *not* close the
     directory baton or pop the current stackframe.  We may be
     entering in here again in just a minute, attempting to commit a
     sibling file.  */
  if (filename)
    {
      svn_pool_destroy (subpool);
      return SVN_NO_ERROR;
    }
  
  else  /* We're finishing a level of recursive descent. */ 
    {
      /* If the current dir (or any of its children) reported changes to
         the editor, then we must close the current dir baton. */
      if ((*stack)->baton)
        SVN_ERR (editor->close_directory ((*stack)->baton));
      
      /* If stack has no previous pointer, then we'd be removing the base
         stackframe.  We don't want to do this, however;
         svn_wc_crawl_local_mods() needs to examine it to determine if any
         changes were ever made at all. */
      if (! (*stack)->previous)
        return SVN_NO_ERROR;
  
      /* Discard top of stack */
      pop_stack (stack);
      
      /* Free all memory used when processing this subdir. */
      svn_pool_destroy (subpool);

      return SVN_NO_ERROR;
    }
}




/* The recursive crawler that describes a mixed-revision working
   copy.  Used for updates.

   This is a depth-first recursive walk of DIR_PATH under WC_PATH.
   Look at each entry and check if its revision is different than
   DIR_REV.  If so, report this fact to REPORTER.

   Note: we're conspicuously creating a subpool in POOL and freeing it
   at each level of subdir recursion; this is a safety measure that
   protects us when reporting info on outrageously large or deep
   trees. */
static svn_error_t *
report_revisions (svn_stringbuf_t *wc_path,
                  svn_stringbuf_t *dir_path,
                  svn_revnum_t dir_rev,
                  const svn_ra_reporter_t *reporter,
                  void *report_baton,
                  apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);

  svn_stringbuf_t *full_path = svn_string_dup (wc_path, subpool);
  svn_path_add_component (full_path, dir_path, svn_path_local_style);
  
  SVN_ERR (svn_wc_entries_read (&entries, full_path, subpool));

  /* Loop over this directory's entries: */
  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      apr_size_t klen;
      void *val;
      svn_stringbuf_t *current_entry_name;
      svn_wc_entry_t *current_entry; 
      svn_stringbuf_t *full_entry_path;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      keystring = (const char *) key;
      current_entry = (svn_wc_entry_t *) val;

      /* Compute the name of the entry */
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
        current_entry_name = NULL;
      else
        current_entry_name = svn_string_create (keystring, subpool);

      /* Compute the complete path of the entry, relative to dir_path. */
      full_entry_path = svn_string_dup (dir_path, subpool);
      if (current_entry_name)
        svn_path_add_component (full_entry_path, current_entry_name,
                                svn_path_local_style);

      /* The Big Tests: */
      
      /* If it's a file with a different rev than its parent, report. */
      if ((current_entry->kind == svn_node_file) 
          && (current_entry->revision != dir_rev))
        SVN_ERR (reporter->set_path (report_baton,
                                     full_entry_path,
                                     current_entry->revision));
      
      /* If entry is a dir (and not `.')... */
      if ((current_entry->kind == svn_node_dir) && current_entry_name)
        {
          /* First check to see if it has a different rev than its
             parent.  It might need to be reported. */
          svn_wc_entry_t *subdir_entry;
          svn_stringbuf_t *megalong_path = svn_string_dup (wc_path, subpool);
          svn_path_add_component (megalong_path, full_entry_path,
                                  svn_path_local_style);
          SVN_ERR (svn_wc_entry (&subdir_entry, megalong_path, subpool));

          if (subdir_entry->revision != dir_rev)
            SVN_ERR (reporter->set_path (report_baton,
                                         full_entry_path,
                                         subdir_entry->revision));          
          /* Recurse. */
          SVN_ERR (report_revisions (wc_path,
                                     full_entry_path,
                                     subdir_entry->revision,
                                     reporter, report_baton,
                                     subpool));
        }
    }

  /* We're done examining this dir's entries, so free them. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}






/*------------------------------------------------------------------*/
/*** Public Interfaces ***/


/* This is the main driver of the commit-editor.   It drives the
   editor in postfix-text-delta style. */


/* Fascinating note about the potential values of {parent_dir,
   condensed_targets} coming into this function:

   There are only three possibilities, as a result of running
   svn_path_condense_targets on the commandline commit targets.

    1. No targets.
       parent = /home/sussman, targets = []

    2. One file target.
       parent = /home/sussman, targets = [foo.c]

    3. Two or more targets of any type.
       parent = /home/sussman, targets = [foo.c, bar, baz, ...]

   It's actually not possible to get a single directory target;  since
   such a directory target would be a child of the parent, the
   condenser routine would reduce the input down to case #1.  */

svn_error_t *
svn_wc_crawl_local_mods (svn_stringbuf_t *parent_dir,
                         apr_array_header_t *condensed_targets,
                         const svn_delta_edit_fns_t *edit_fns,
                         void *edit_baton,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  int i;
  svn_stringbuf_t *filename;
  svn_wc_entry_t *parent_entry, *tgt_entry;

  /* A stack that will store all paths and dir_batons as we drive the
     editor depth-first. */
  struct stack_object *stack = NULL;

  /* All the locally modified files which are waiting to be sent as
     postfix-textdeltas. */
  apr_hash_t *affected_targets = apr_hash_make (pool);

  /* All the wc directories that are "locked" as we commit local
     changes. */
  apr_hash_t *locked_dirs = apr_hash_make (pool);

  /* Sanity check. */
  assert(parent_dir != NULL);
  assert(condensed_targets != NULL);

  /* Sort the condensed targets so that targets which share "common
     sub-parent" directories are all lumped together.  This guarantees
     a depth-first drive of the editor. */
  qsort (condensed_targets->elts,
         condensed_targets->nelts,
         condensed_targets->elt_size,
         svn_sort_compare_strings_as_paths);

  /* No targets at all? */
  if (condensed_targets->nelts == 0)
    {
      /* Do a single crawl from parent_dir, that's it.  Parent_dir
      will be automatically pushed to the empty stack, but not
      removed.  This way we can examine the frame to see if there's a
      root_dir_baton, and thus whether we need to call close_edit(). */
      err = report_local_mods (parent_dir, NULL, NULL,
                               edit_fns, edit_baton,
                               &stack, affected_targets, locked_dirs,
                               pool);
      if (err)
        {
          remove_all_locks (locked_dirs, pool);
          return svn_error_quick_wrap 
            (err, "commit failed: while sending tree-delta to repos.");
        }
    }
      
  else  /* Multi-target commit under parent_dir.  Fasten seatbelts. */
    {
      /* To begin, put the grandaddy parent_dir at the base of the stack. */
      SVN_ERR (svn_wc_entry (&parent_entry, parent_dir, pool));
      push_stack (&stack, parent_dir, NULL, parent_entry, pool);

      /* The Main Loop -- over each commit target. */
      for (i = 0; i < condensed_targets->nelts; i++)
        {
          int j;
          svn_stringbuf_t *ptarget;
          svn_stringbuf_t *remainder_path;
          svn_stringbuf_t *target, *subparent;
          svn_stringbuf_t *relative_target =
            (((svn_stringbuf_t **) condensed_targets->elts)[i]);

          /* The targets come in as paths relative to parent_dir.
             Unfortunately, report_local_mods expects "real" paths
             (i.e. either absolute, or relative to CWD.)  So we prepend
             parent_dir to all the targets here. */
          target = svn_string_dup (parent_dir, pool);
          svn_path_add_component (target, relative_target,
                                  svn_path_local_style);
          
          /* Examine top of stack and target, and get a nearer common
             'subparent'. */
          subparent = svn_path_get_longest_ancestor (target,
                                                     stack->path, 
                                                     svn_path_local_style,
                                                     pool);
          
          /* If the current stack path is NOT equal to the subparent,
             it must logically be a child of the subparent.  So... */
          if (svn_path_compare_paths (stack->path, subparent,
                                      svn_path_local_style))
            /* ...close directories and remove stackframes until the stack
               reaches the common parent. */
            SVN_ERR (do_dir_closures (subparent, &stack, edit_fns));
          
          /* Push new stackframes to get down to the immediate parent of
             the target ("ptarget"), which must also be a child of the
             subparent. */
          svn_path_split (target, &ptarget, NULL,
                          svn_path_local_style, pool);
          remainder_path = svn_path_is_child (stack->path, ptarget, 
                                              svn_path_local_style,
                                              pool);
          
          if (remainder_path)  /* is ptarget "below" the stack-path? */
            {
              apr_array_header_t *components;
              
              /* split the remainder into path components. */
              components = svn_path_decompose (remainder_path,
                                               svn_path_local_style,
                                               pool);
              
              for (j = 0; j < components->nelts; j++)
                {
                  svn_stringbuf_t *new_path;
                  svn_wc_entry_t *new_entry;
                  
                  svn_stringbuf_t *component = 
                    (((svn_stringbuf_t **) components->elts)[j]);
                  new_path = svn_string_dup (stack->path, pool);
                  svn_path_add_component (new_path, component,
                                          svn_path_local_style);
                  
                  SVN_ERR(svn_wc_entry (&new_entry, new_path, pool));
                  
                  push_stack (&stack, new_path, NULL, new_entry, pool);
                }
            }
          
          /* Note:  when we get here, the topmost stackframe is
                    *guaranteed* to be the parent of TARGET, whether
                    TARGET be a file or dir. */
          
          /* Figure out if TARGET is a file or a dir. */
          SVN_ERR(svn_wc_entry (&tgt_entry, target, pool));
          
          if (tgt_entry->kind == svn_node_file)
            /* isolate the name of the file itself */
            filename = svn_path_last_component (target,
                                                svn_path_local_style, pool);
          else
            filename = NULL;

          /* Do a crawl for local mods in TARGET.  If any are found,
             directory batons will be automatically generated as needed
             and stored in the stack.  File batons for postfix textdeltas
             will be continually added to AFFECTED_TARGETS, and locked
             directories will be appended to LOCKED_DIRS. 

             For the perplexed:  here is the behavior that this
             routine believes report_local_mods() will follow:

               IF COMMITTING A DIR TARGET:
                   1. the target dir will be pushed onto the stack
                   2. after recursing, the target dir will be 'closed'.
                   3. the target dir will be popped from the stack.

               IF COMMITTING A FILE TARGET:
                   1. no stackframe will be pushed.  (the file's
                      parent is already on the stack).
                   2. no dir_batons will be closed at all (in case we
                      need to commit more files in this parent.)
                   3. no stackframe will be popped.

          */
          err = report_local_mods (filename ? ptarget : target,
                                   stack->baton, filename,
                                   edit_fns, edit_baton,
                                   &stack, affected_targets, locked_dirs,
                                   pool);
          if (err)
            {
              remove_all_locks (locked_dirs, pool);
              return svn_error_quick_wrap 
                (err, "commit failed: while sending tree-delta.");
            }

        } /*  -- End of main target loop -- */
      
      /* To finish, pop the stack all the way back to the grandaddy
         parent_dir, and call close_dir() on all batons we find. */
      SVN_ERR (do_dir_closures (parent_dir, &stack, edit_fns));

      /* Don't forget to close the root-dir baton on the bottom
         stackframe, if one exists. */
      if (stack->baton)        
        SVN_ERR (edit_fns->close_directory (stack->baton));

    }  /* End of multi-target section */



  /* All crawls are completed, so affected_targets potentially has
     some still-open file batons. Loop through affected_targets, and
     fire off any postfix text-deltas that need to be sent. */
  err = do_postfix_text_deltas (affected_targets, edit_fns, pool);
  if (err)
    {
      remove_all_locks (locked_dirs, pool);
      return svn_error_quick_wrap 
        (err, "commit failed:  while sending postfix text-deltas.");
    }

  /* Have *any* edits been made at all?  We can tell by looking at the
     foundation stackframe; it might still contain a root-dir baton.
     If so, close the entire edit. */
  if (stack->baton)
    {
      err = edit_fns->close_edit (edit_baton);
      if (err)
        {
          /* Commit failure, though not *nececssarily* from the
             repository.  close_edit() does a LOT of things, including
             bumping all working copy revision numbers.  Again, see
             earlier comment. 

             The interesting thing here is that the commit might have
             succeeded in the repository, but the WC lib returned a
             revision-bumping or wcprop error. */
          remove_all_locks (locked_dirs, pool);
          return svn_error_quick_wrap
            (err, "commit failed: while calling close_edit()");
        }
    }

  /* The commit is complete, and revisions have been bumped. */

  /* Successful cleanup:  remove all the lockfiles. */
  SVN_ERR (remove_all_locks (locked_dirs, pool));
  
  return SVN_NO_ERROR;
}



/* This is the main driver of the working copy state "reporter", used
   for updates. */
svn_error_t *
svn_wc_crawl_revisions (svn_stringbuf_t *path,
                        const svn_ra_reporter_t *reporter,
                        void *report_baton,
                        apr_pool_t *pool)
{
  svn_wc_entry_t *entry;
  svn_revnum_t base_rev = SVN_INVALID_REVNUM;

  /* The first thing we do is get the base_rev from the working copy's
     ROOT_DIRECTORY.  This is the first revnum that entries will be
     compared to. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  base_rev = entry->revision;

  /* The first call to the reporter merely informs it that the
     top-level directory being updated is at BASE_REV.  Its PATH
     argument is ignored. */
  SVN_ERR (reporter->set_path (report_baton,
                               svn_string_create ("", pool),
                               base_rev));

  if (entry->kind == svn_node_dir)
    {
      /* Recursively crawl ROOT_DIRECTORY and report differing
         revisions. */
      SVN_ERR (report_revisions (path,
                                 svn_string_create ("", pool),
                                 base_rev,
                                 reporter, report_baton, pool));
    }
  else if ((entry->kind == svn_node_file) 
           && (entry->revision != base_rev))
    {
      /* If this entry is a file node, we just want to report that
         node's revision.  Since we are looking at the actual target
         of the report (not some file in a subdirectory of a target
         directory), and that target is a file, we need to pass an
         empty string to set_path. */
      reporter->set_path (report_baton, 
                          svn_string_create ("", pool),
                          base_rev);
    }

  /* Finish the report, which causes the update editor to be driven. */
  SVN_ERR (reporter->finish_report (report_baton));

  return SVN_NO_ERROR;
}





/* 

   Status of multi-arg commits:
   
   TODO:

   * the "path analysis" phase needs to happen at a high level in the
   client, along with the alphabetization.  Specifically, when the
   client must open an RA session to the *grandparent* dir of all
   commit targets, and use that ra session to fetch the commit
   editor.  It then needs to pass the canonicalized paths to
   crawl_local_mods.

   * must write some python tests for multi-args.

   * secret worry:  do all the new path routines work -- both Kevin
   P-B's as well as my own?
 
 */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
