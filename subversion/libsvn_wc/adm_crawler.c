/*
 * adm_crawler.c:  report local WC mods to an Editor.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

/* ==================================================================== */


#include <string.h>

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

  /* This is not an "edit_baton", it is a *file* baton belonging to
     some editor, hence the name "editor_baton".  The compiler
     complained when we tried "editor's_baton". :-) */
  void *editor_baton;

  svn_boolean_t text_modified_p;
};


/* Local "stack" objects used by the crawler to keep track of dir
   batons. */
struct stack_object
{
  svn_stringbuf_t *path;      /* A working copy directory */
  void *baton;                /* An associated dir baton, if any exists yet. */
  svn_wc_entry_t *this_dir;   /* All entry info about this directory */
  apr_pool_t *pool;

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
  struct stack_object *new_top;
  apr_pool_t *my_pool;

  if (*stack == NULL)
    my_pool = svn_pool_create (pool);
  else
    my_pool = svn_pool_create ((*stack)->pool);

  /* Store path and baton in a new stack object */
  new_top = apr_pcalloc (my_pool, sizeof (*new_top));
  new_top->path = svn_stringbuf_dup (path, pool);
  new_top->baton = baton;
  new_top->this_dir = entry;
  new_top->next = NULL;
  new_top->previous = NULL;
  new_top->pool = my_pool;

  if (*stack == NULL)
    {
      /* This will be the very first object on the stack. */
      *stack = new_top;
    }
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
  struct stack_object *old_top;

  old_top = *stack;
  if ((*stack)->previous)
    {
      new_top = (*stack)->previous;
      new_top->next = NULL;
      *stack = new_top;
    }
  svn_pool_destroy (old_top->pool);
}



/* Remove administrative-area locks on each path in LOCKS hash */
static svn_error_t *
remove_all_locks (apr_hash_t *locks, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  
  for (hi = apr_hash_first (pool, locks); hi; hi = apr_hash_next (hi))
    {
      svn_error_t *err;
      const void *key;
      void *val;
      apr_ssize_t klen;
      svn_stringbuf_t *unlock_path;
      
      apr_hash_this (hi, &key, &klen, &val);
      unlock_path = svn_stringbuf_create ((char *)key, pool);
      
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


/* Remove administrative-area tmpfiles for each wc file in TARGETS hash */
static svn_error_t *
remove_all_tmpfiles (apr_hash_t *targets, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_status_t apr_err;

  for (hi = apr_hash_first (pool, targets); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_ssize_t klen;
      svn_stringbuf_t *tmpfile_path;
      enum svn_node_kind kind;

      apr_hash_this (hi, &key, &klen, &val);
      tmpfile_path = 
        svn_wc__text_base_path (svn_stringbuf_create ((char *)key, pool), 
                                TRUE, pool);
      
      SVN_ERR (svn_io_check_path (tmpfile_path->data, &kind, pool));
      if (kind == svn_node_file)
        {
          apr_err = apr_file_remove (tmpfile_path->data, pool);
          if (apr_err) 
            return svn_error_createf 
              (apr_err, 0, NULL, pool, "Error removing tmpfile '%s'",
               tmpfile_path->data);
        }
    }

  return SVN_NO_ERROR;
}


/* Cleanup after a commit by removing locks and tmpfiles. */
static svn_error_t *
cleanup_commit (apr_hash_t *locked_dirs, 
                apr_hash_t *affected_targets,
                apr_pool_t *pool)
{
  svn_error_t *err, *err2;

  /* Make sure that we always remove the locks that we installed. */
  err = remove_all_locks (locked_dirs, pool);
  
  /* Cleanup the tmp/text-base files that might be left around
     after a failed commit.  We only want to do this if the commit
     failed, though, since*/
  err2 = remove_all_tmpfiles (affected_targets, pool);

  if (err && err2)
    {
      svn_error_t *scan;
     
      err = svn_error_quick_wrap (err, "---- lock cleanup error follows");
      err2 = svn_error_quick_wrap (err2, "---- tmpfile cleanup error follows");

      /* Concatenate the lock cleanup and tmpfile cleanup errors */
      for (scan = err; scan->child != NULL; scan = scan->child)
        continue;
      scan->child = err2;
      return err;
    }
  if (err)
    return err;
  if (err2)
    return err2;
  return SVN_NO_ERROR;
}

/* Attempt to grab a lock in PATH.  If we succeed, store PATH in LOCKS
   and return success.  If we fail to grab a lock, remove all locks in
   LOCKS and return error. */
static svn_error_t *
do_lock (svn_stringbuf_t *path, apr_hash_t *locks, apr_pool_t *pool)
{
  svn_error_t *err, *err2;

  /* See if this directory is locked already.  If so, there's not
     really much to do here.  ### todo: Should we do check the actual
     working copy admin area with a call to svn_wc_locked() here? */
  if (apr_hash_get (locks, path->data, APR_HASH_KEY_STRING) != NULL)
    return SVN_NO_ERROR;

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
                 apr_pool_t *top_pool)
{
  struct stack_object *stackptr;  /* current stack object we're examining */

  /* Start at the top of the stack */
  stackptr = stack;  
  
  /* Walk down the stack until we find a non-NULL dir baton. */
  while (1)  
    {
      if (stackptr->baton != NULL) 
        /* Found an existing directory baton! */
        break;
      
      if (stackptr->previous)
        {
          /* There's a previous stack frame, so descend. */
          stackptr = stackptr->previous;
        }
      else
        {
          /* Can't descend?  We must be at stack bottom, fetch the
             root baton. */
          void *root_baton;

          SVN_ERR (editor->open_root (edit_baton,
                                      stackptr->this_dir->revision, 
                                      &root_baton));  
          /* Store it */
          stackptr->baton = root_baton;
          break;
        }
    }

  /* Now that we're outside the while() loop, our stackptr is pointing
     to the frame with the "youngest" directory baton. */

  /* Now walk _up_ the stack, creating & storing new batons. */
  while (1)  
    {
      if (stackptr->next)
        {
          svn_stringbuf_t *dirname;
          void *dir_baton;

          /* Move up the stack */
          stackptr = stackptr->next;

          /* We only want the last component of the path; that's what
             the editor's open_directory() expects from us. */
          dirname =
            svn_stringbuf_create (svn_path_basename (stackptr->path->data,
                                                     stackptr->pool),
                                  stackptr->pool);

          /* Get a baton for this directory */
          SVN_ERR (editor->open_directory 
                   (dirname, /* current dir */
                    stackptr->previous->baton, /* parent */
                    stackptr->this_dir->revision,
                    &dir_baton));

          /* Store it */
          stackptr->baton = dir_baton;
        }
      else 
        {
          /* Can't move up the stack anymore?  We must be at the top
             of the stack.  We're all done. */
          break;
        }
    }

  /* Return (by reference) the youngest directory baton, the one that
     goes with our youngest PATH */
  *newest_baton = stackptr->baton;

  /* Lock this youngest directory */
  SVN_ERR (do_lock 
           (svn_stringbuf_dup (stackptr->path, top_pool), locks, top_pool));
  
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
   while (svn_path_compare_paths (desired_path, (*stack)->path))
    {
      if ((*stack)->baton)
        SVN_ERR (editor->close_directory ((*stack)->baton));
      
      pop_stack (stack);
    }

  return SVN_NO_ERROR;
}





/* Obtain a window handler by invoking EDITOR->apply_txdelta on
   TB->editor_baton, then feed the handler windows from a txdelta
   stream expressing the difference between FILENAME's text-base and
   FILENAME.  FILENAME is an absolute or relative path ending in a
   filename.

   Caller must have prepared a copy of FILENAME in the appropriate
   tmp text-base administrative area, i.e., the location obtained by
   calling svn_wc__text_base_path(FILENAME).  This tmp file is what
   will actually be used to generate the diff; any eol or keyword
   untranslation should already have been done on the tmp file.
*/
static svn_error_t *
do_apply_textdelta (svn_stringbuf_t *filename,
                    const svn_delta_edit_fns_t *editor,
                    struct target_baton *tb,
                    apr_pool_t *pool)
{
  apr_status_t status;
  svn_txdelta_window_handler_t window_handler;
  void *window_handler_baton;
  svn_txdelta_stream_t *txdelta_stream;
  apr_file_t *localfile = NULL;
  apr_file_t *textbasefile = NULL;
  svn_stringbuf_t *local_tmp_path;

  /* Tell the editor that we're about to apply a textdelta to the file
     baton; the editor returns to us a window consumer routine and
     baton. */
  SVN_ERR (editor->apply_textdelta (tb->editor_baton,
                                    &window_handler,
                                    &window_handler_baton));

  /* Get the path to the administrative temp text-base file.
     Presumably the working file has been copied here by our caller,
     do_postfix_text_deltas(). */
  local_tmp_path = svn_wc__text_base_path (filename, TRUE, pool);

  /* Open a filehandle for tmp local file, and one for text-base if
     applicable. */
  status = apr_file_open (&localfile, local_tmp_path->data,
                          APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                              "do_apply_textdelta: error opening '%s'",
                              local_tmp_path->data);

  if ((! (tb->entry->schedule == svn_wc_schedule_add))
      && (! (tb->entry->schedule == svn_wc_schedule_replace)))
    {
      SVN_ERR (svn_wc__open_text_base (&textbasefile, filename, 
                                       APR_READ, pool));
    }

  /* Create a text-delta stream object that pulls data out of the two
     files. */
  svn_txdelta (&txdelta_stream,
               svn_stream_from_aprfile (textbasefile, pool),
               svn_stream_from_aprfile (localfile, pool),
               pool);
  
  /* Pull windows from the delta stream and feed to the consumer. */
  SVN_ERR (svn_txdelta_send_txstream (txdelta_stream,
                                      window_handler,
                                      window_handler_baton,
                                      pool));

  /* Close the two files */
  status = apr_file_close (localfile);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "do_apply_textdelta: error closing local file");

  if (textbasefile)
    SVN_ERR (svn_wc__close_text_base (textbasefile, filename, 0, pool));

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
  apr_hash_index_t *hi;
  svn_stringbuf_t *entrypath;
  struct target_baton *tb;
  apr_pool_t *subpool = svn_pool_create (pool);

  for (hi = apr_hash_first (pool, affected_targets); 
       hi; 
       hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_stringbuf_t *tmp_wfile, *tmp_text_base;

      apr_hash_this (hi, &key, &keylen, &val);
      entrypath = svn_stringbuf_create ((char *) key, subpool);
      tb = val;
      
      /* Make an untranslated copy of the working file in
         .svn/tmp/text-base, because a) we want this to work even if
         someone changes the working file while we're generating the
         txdelta, b) we need to detranslate eol and keywords anyway,
         and c) after the commit, we're going to copy the tmp file to
         become the new text base anyway.  

         Note that since the translation routine doesn't let you
         choose the filename, we have to do one extra copy.  But what
         the heck, we're about to generate an svndiff anyway. */

      SVN_ERR (svn_wc_translated_file (&tmp_wfile, entrypath, subpool));
      tmp_text_base = svn_wc__text_base_path (entrypath, TRUE, subpool);
      SVN_ERR (svn_io_copy_file (tmp_wfile->data, tmp_text_base->data, FALSE,
                                 subpool));
      if (tmp_wfile != entrypath)
        SVN_ERR (svn_io_remove_file (tmp_wfile->data, subpool));

      /* If there's a local mod, send a text-delta. */
      if (tb->text_modified_p)
        {
          SVN_ERR (do_apply_textdelta (entrypath, editor, tb, subpool));
        }

      /* Close the file baton, whether we sent a text-delta or not! */
      if (tb->entry->kind == svn_node_file)
        SVN_ERR (editor->close_file (tb->editor_baton));

      /* Clear the iteration subpool. */
      svn_pool_clear (subpool);
    }

  /* Destroy the iteration subpool. */
  svn_pool_destroy (subpool);
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
  int i;
  svn_stringbuf_t *prop_path, *prop_base_path, *tmp_prop_path;
  apr_array_header_t *local_propchanges;
  apr_hash_t *localprops = apr_hash_make (pool);
  apr_hash_t *baseprops = apr_hash_make (pool);
  svn_stringbuf_t *propname;
  svn_stringbuf_t *propval;
  
  /* First, get the prop_path from the original path */
  SVN_ERR (svn_wc__prop_path (&prop_path, path, 0, pool));
  
  /* Get the full path of the prop-base `pristine' file */
  SVN_ERR (svn_wc__prop_base_path (&prop_base_path, path, 0, pool));

  /* Copy the local prop file to the administrative temp area */
  SVN_ERR (svn_wc__prop_path (&tmp_prop_path, path, 1, pool));
  SVN_ERR (svn_io_copy_file (prop_path->data, tmp_prop_path->data, FALSE,
                             pool));

  /* Load all properties into hashes */
  SVN_ERR (svn_wc__load_prop_file (tmp_prop_path->data, localprops, pool));
  SVN_ERR (svn_wc__load_prop_file (prop_base_path->data, baseprops, pool));
  
  /* Get an array of local changes by comparing the hashes. */
  SVN_ERR (svn_wc__get_local_propchanges 
           (&local_propchanges, localprops, baseprops, pool));

  /* create some reusable buffers for the prop name and value */
  propname = svn_stringbuf_create ("", pool);
  propval = svn_stringbuf_create ("", pool);

  /* Apply each local change to the baton */
  for (i = 0; i < local_propchanges->nelts; i++)
    {
      const svn_prop_t *change;

      change = &APR_ARRAY_IDX(local_propchanges, i, svn_prop_t);

      svn_stringbuf_set (propname, change->name);

      if (change->value != NULL)
        {
          /*
            svn_stringbuf_nset (propval,
                                change->value->data, change->value->len);
          */
          svn_stringbuf_setempty (propval);
          svn_stringbuf_appendbytes (propval,
                                     change->value->data, change->value->len);
        }

      if (entry->kind == svn_node_file)
        SVN_ERR (editor->change_file_prop (baton, propname,
                                           change->value ? propval : NULL));
      else
        SVN_ERR (editor->change_dir_prop (baton, propname,
                                          change->value ? propval : NULL));
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
  svn_boolean_t text_conflict_p, prop_conflict_p;
  svn_stringbuf_t *parent_dir = NULL;

  /* If there are no reject filenames stored, we'll call ourselves
     conflict-free. */
  if (! (entry->prejfile || entry->conflict_old 
         || entry->conflict_new || entry->conflict_wrk))
    return SVN_NO_ERROR;

  /* We must decide if either component is "conflicted", based
     on whether reject files are mentioned and/or continue to
     exist.  Luckily, we have a function to do this.  :) */
  if (entry->kind == svn_node_dir)
    {
      parent_dir = full_path;
    }
  else  /* non-directory, that's all we need to know */
    {
      parent_dir = svn_stringbuf_dup (full_path, pool);
      svn_path_remove_component (parent_dir);
    }

  /* Defer the conflicted question to someone else.  Not "laziness",
     no ... "modularity"!  */
  SVN_ERR (svn_wc_conflicted_p (&text_conflict_p,
                                &prop_conflict_p,
                                parent_dir,
                                entry,
                                pool));

  if ((! text_conflict_p) && (! prop_conflict_p))
    return SVN_NO_ERROR;

  /* If we get there, that mans a tracked reject file stills exist.
     So, we'll at least return one error describing this problem, and
     that may potentially have a child error that results from some
     failure to clean up locks.  This nested error interface is just
     too cool. */
  return svn_error_createf (SVN_ERR_WC_FOUND_CONFLICT, 0, 
                            remove_all_locks (locks, pool),
                            pool,
                            "Aborting commit: '%s' remains in conflict.",
                            full_path->data);
}


/* Given a directory DIR under revision control with schedule
   SCHEDULE:

   - if SCHEDULE is svn_wc_schedule_delete, all children of this
     directory must have a schedule _delete.

   - else, if SCHEDULE is svn_wc_schedule_replace, all children of
     this directory must have a schedule of either _add or _delete.

   - else, this directory must not be marked for deletion, which is an
     automatic excuse to fail this verification!
*/
static svn_error_t *
verify_tree_deletion (svn_stringbuf_t *dir,
                      enum svn_wc_schedule_t schedule,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_stringbuf_t *fullpath = svn_stringbuf_dup (dir, pool);

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
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_wc_entry_t *entry; 
      int is_this_dir;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      entry = (svn_wc_entry_t *) val;
      is_this_dir = strcmp (key, SVN_WC_ENTRY_THIS_DIR) == 0;

      /* Construct the fullpath of this entry. */
      if (! is_this_dir)
        svn_path_add_component_nts (fullpath, key);

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
      svn_stringbuf_set (fullpath, dir->data);

      /* Clear our per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Destroy our per-iteration pool. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


/* Helper: search down the directory STACK, looking for the nearest
   'copyfrom' url.  Once we've found the root of the copy, derive a
   new copyfrom url by walking back up to ENTRY_NAME.  Return the new
   url in *COPYFROM_URL. */
static svn_error_t *
derive_copyfrom_url (svn_stringbuf_t **copyfrom_url,
                     svn_stringbuf_t *entry_name,
                     struct stack_object *stack)
{
  svn_stringbuf_t *root_copyfrom_url = NULL;

  /* current stack object we're examining */
  struct stack_object *stackptr = stack;
  
  /* Walk down the stack until we find a non-NULL dir baton. */
  while (1)  
    {
      root_copyfrom_url = stackptr->this_dir->copyfrom_url;
      if (root_copyfrom_url != NULL)
        /* found the nearest copy history, so move on. */
        break;
      
      if (stackptr->previous)
        {
          /* There's a previous stack frame, so descend. */
          stackptr = stackptr->previous;
        }
      else
        {
          /* Can't descend?  We must be at stack bottom, and yet found
             no copy history.  This is a bogus working copy! */
          return svn_error_createf 
            (SVN_ERR_WC_CORRUPT, 0, NULL, stack->pool,
             "Can't find any copy history in any parent of copied dir '%s'.",
             stack->path->data);
        }
    }

  /* Dup the root url. */
  root_copyfrom_url = svn_stringbuf_dup (root_copyfrom_url, stack->pool);

  /* Now walk _up_ the stack, augmenting root_copyfrom_url. */
  while (1)  
    {
      if (stackptr->next)
        {
          const char *dirname;

          /* Move up the stack */
          stackptr = stackptr->next;

          /* Fetch the 'name' attribute of this parent directory from
             its URL. */
          /* ### hmm. the url comes from stackptr, but the pool from stack */
          dirname = svn_path_basename (stackptr->this_dir->url->data,
                                       stack->pool);

          /* Add it to the url. */
          svn_path_add_component_nts (root_copyfrom_url, dirname);
        }
      else 
        {
          /* Can't move up the stack anymore?  We must be at the top
             of the stack.  We're all done. */
          break;
        }
    }

  /* Lastly, add the original entry's name to the derived url. */
  svn_path_add_component (root_copyfrom_url, entry_name);

  /* Return the results. */
  *copyfrom_url = root_copyfrom_url;
  
  return SVN_NO_ERROR;
}


/* Forward declaration for co-dependent recursion. */
static svn_error_t *
crawl_dir (svn_stringbuf_t *path,
           void *dir_baton,
           const svn_delta_edit_fns_t *editor,
           void *edit_baton,
           const svn_ra_get_latest_revnum_func_t *revnum_fn,
           void *rev_baton,
           svn_revnum_t *youngest_rev,
           svn_boolean_t adds_only,
           svn_boolean_t copy_mode,
           struct stack_object **stack,
           apr_hash_t *affected_targets,
           apr_hash_t *locks,
           apr_pool_t *top_pool);


/*
  report_single_mod() has the burden of noticing many different entry
  states -- schedules, existences, and so on.  Here are the different
  things it can encounter:

      A. copied && revision is different:

            search upward for root of copy, derive new copyfrom args,
            do another add-with-history.  look for local mods.

      B. scheduled addition (rev=0)

            do a regular add, don't look for local mods.

      C. scheduled addition (copyfrom_args)

            do an add-with-history, using copyfrom args provided.
            look for local mods.

      D. scheduled deletion -OR- existence=deleted

            do a delete, don't look for local mods.

      E. replacement type 1:  (schedule=replace)

            do a delete.
            then do EITHER a regular add (not looking for local mods),
                or an add-with-history (and look for local mods)

      F. replacement type 2:  existence=deleted, schedule=add
         (i.e. a commit has happened between the delete and add)

            same actions as replacement type 1.
         
   
MAIN LOGIC:

   bool do_delete=0, do_add=0;
   string copyfrom_args = NULL;

   // this happens in cases D, E, F
   if (schedule==delete OR schedule==replace OR existence==deleted)
     do_delete = 1;
   
   // this happens in cases B, C, E, F
   if (schedule==add OR schedule==replace)
     do_add = 1;
     copyfrom_args = entry->atts->copyfrom_args // if they exist
   
   // this happens in case A
   if (existence==copied && rev is different than parent && schedule==normal)
     do_add = 1;
     copyfrom_args = derive_from_copy_root(); 

   if (do_delete)
      editor->delete()

   if (do_add)
      if (copyfrom_args)
          editor->add_*(copyfrom_args)
          if file, maybe mark for txdelta
      else
          do_mod_check = 0
          editor->add_*()
          if file, definitely mark for txdelta

   else if (! do_add && ! do_delete)
      ...look for mods, maybe mark for txdelta

   if (dir)
      recurse;
*/

/* Report modifications to file or directory NAME in STACK->path
   (represented by ENTRY).  NAME is NOT SVN_WC_ENTRY_THIS_DIR.
   
   Keep track of modified targets in AFFECTED_TARGETS, and of locked
   directories in LOCKS.

   All reporting is made using calls to EDITOR (using its associated
   EDIT_BATON and a computed DIR_BATON).

   If ADDS_ONLY is set, this function will only pay attention to files
   and directories scheduled for addition.

   If COPY_MODE is set, this function will behave as though the entry
   is marked with the "copied" flag.

   Perform all temporary allocation in TMP_POOL, allocations related
   to the stack in STACK->pool, and any allocation that must outlive
   the reporting process in TOP_POOL.

   Temporary: use REVNUM_FN/REV_BATON/YOUNGEST_REV to determine if a
   dir is up-to-date when it has a propchange.  */
static svn_error_t *
report_single_mod (const char *name,
                   svn_wc_entry_t *entry,
                   struct stack_object **stack,
                   apr_hash_t *affected_targets,
                   apr_hash_t *locks,
                   const svn_delta_edit_fns_t *editor,
                   void *edit_baton,
                   const svn_ra_get_latest_revnum_func_t *revnum_fn,
                   void *rev_baton,
                   svn_revnum_t *youngest_rev,
                   void **dir_baton,
                   svn_boolean_t adds_only,
                   svn_boolean_t copy_mode,
                   apr_pool_t *tmp_pool,
                   apr_pool_t *top_pool)
{
  svn_stringbuf_t *full_path;
  svn_stringbuf_t *entry_name;

  /* If the entry is a directory, and we need to recurse, this is the
     baton we will use to do so.  */
  void *new_dir_baton = NULL;

  /* These will be filled in (if necessary) later on, if we end up
     doing an add-with-history. */
  svn_stringbuf_t *copyfrom_url = NULL;
  svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;

  /* By default, assume we're only going to look for local mods. */
  svn_boolean_t do_add = FALSE, do_delete = FALSE;

  /* Sanity check:  'this_dir' is ignored.  */
  if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
    return SVN_NO_ERROR;
  
  entry_name = svn_stringbuf_create (name, tmp_pool);
  
  /* This entry gets deleted if marked for deletion or replacement. */
  if (! adds_only)
    if ((entry->schedule == svn_wc_schedule_delete)
        || (entry->schedule == svn_wc_schedule_replace))
      {
        do_delete = TRUE;
      }

  /* This entry gets added if marked for addition or replacement. */
  if ((entry->schedule == svn_wc_schedule_add)
      || (entry->schedule == svn_wc_schedule_replace))
    {
      do_add = TRUE;
      
      /* If the entry itself has 'copyfrom' args, find them now. */
      copyfrom_url = entry->copyfrom_url;
      copyfrom_rev = entry->copyfrom_rev;
    }
  
  /* If the entry is part of a 'copied' subtree, and isn't scheduled
     for addition or deletion, we -might- need to do an
     add-with-history if it has a different working rev than its
     parent. */
  if ((entry->copied || copy_mode)
      && (entry->schedule == svn_wc_schedule_normal)
      && (entry->revision != (*stack)->this_dir->revision))
    {
      do_add = TRUE;
      
      /* Derive the copyfrom url by inheriting it from the nearest
         parental 'root' of the copy. */
      SVN_ERR (derive_copyfrom_url (&copyfrom_url, entry_name, *stack));

      /* The copyfrom revision is whatever the 'different' working rev
         is. */
      copyfrom_rev = entry->revision;

      /* If existence is copied, there *better* be some copyfrom args
         above us somewhere!!!   ### take this out later, return error. */
      assert(copyfrom_url != NULL);
    }


  /* Construct a full path to the current entry */
  full_path = svn_stringbuf_dup ((*stack)->path, tmp_pool);
  if (entry_name != NULL)
    svn_path_add_component (full_path, entry_name);

  /* Preemptive strike:  if the current entry is a file in a state
     of conflict that has NOT yet been resolved, we abort the
     entire commit.  */
  SVN_ERR (bail_if_unresolved_conflict (full_path, entry, locks, 
                                        tmp_pool));


  /* Here's a guide to the very long logic that extends below.
   *
   *   if (do_delete)...
   *   if (do_add)...
   *   else if (!do_delete && !do_add)
   *      look for local mods
   *   if (dir)
   *      recurse() */

      
  /* DELETION CHECK */
  if (do_delete)
    {
      /* Do what's necessary to get a baton for current directory */
      if (! *dir_baton)
        SVN_ERR (do_dir_replaces (dir_baton,
                                  *stack, editor, edit_baton,
                                  locks, top_pool));
      
      /* If this entry is a directory, we do a sanity check and make
         sure that all the directory's children are also marked for
         deletion.  If not, we're in a screwy state. */
      if (entry->kind == svn_node_dir) 
        SVN_ERR (verify_tree_deletion (full_path, entry->schedule, 
                                       tmp_pool));

      /* Delete the entry */
      SVN_ERR (editor->delete_entry (entry_name, entry->revision,
                                     *dir_baton));
    }  
  /* END DELETION CHECK */
  

  /* ADDITION CHECK */
  if (do_add)
    {
      svn_stringbuf_t *longpath;
      struct target_baton *tb;
      svn_boolean_t prop_modified_p;        

      /* Create an affected-target object */
      tb = apr_pcalloc (top_pool, sizeof (*tb));
      tb->entry = svn_wc_entry_dup (entry, top_pool);          
      
      /* Do what's necesary to get a baton for current directory */
      if (! *dir_baton)
        SVN_ERR (do_dir_replaces (dir_baton,
                                  *stack, editor, edit_baton,
                                  locks, top_pool));      

      /* Adding a new directory: */
      if (entry->kind == svn_node_dir)
        {             
          svn_wc_entry_t *subdir_entry;
          
          /* A directory's interesting information is stored in
             its own THIS_DIR entry, so read that to get the real
             data for this directory. */
          SVN_ERR (svn_wc_entry (&subdir_entry, full_path, tmp_pool));
          
          if (! copyfrom_url)
            {              
              /* Add the new directory, getting a new dir baton.  */
              SVN_ERR (editor->add_directory (entry_name,
                                              *dir_baton,
                                              NULL, SVN_INVALID_REVNUM,
                                              &new_dir_baton));
            }
          else
            {
              /* Add the new directory WITH HISTORY */
              SVN_ERR (editor->add_directory (entry_name,
                                              *dir_baton,
                                              copyfrom_url, copyfrom_rev,
                                              &new_dir_baton));             
            }
          
          /* History or not, decide if there are props to send. */
          SVN_ERR (svn_wc_props_modified_p (&prop_modified_p, full_path, 
                                            tmp_pool));
          if (prop_modified_p)
            SVN_ERR (do_prop_deltas (full_path, entry, editor, 
                                     new_dir_baton, tmp_pool));
        }
      
      /* Adding a new file: */
      else if (entry->kind == svn_node_file)
        {
          if (! copyfrom_url)
            {
              /* Add a new file, getting a file baton */
              SVN_ERR (editor->add_file (entry_name,
                                         *dir_baton,
                                         NULL, SVN_INVALID_REVNUM,
                                         &(tb->editor_baton)));

              /* If there are no copyfrom args, this must be a totally
                 "new" file.  Assume that text needs to be sent. */
              tb->text_modified_p = TRUE;
            }
          else  
            {
              /* Add a new file WITH HISTORY, getting a file baton */
              SVN_ERR (editor->add_file (entry_name,
                                         *dir_baton,
                                         copyfrom_url, copyfrom_rev,
                                         &(tb->editor_baton)));

              /* This file is added with history; in this case,
                 we only *might* need to send contents.  Do a real
                 local-mod check on it. */
              SVN_ERR (svn_wc_text_modified_p (&(tb->text_modified_p),
                                               full_path, tmp_pool));
            }

          /* History or not, decide if there are props to send. */
          SVN_ERR (svn_wc_props_modified_p (&prop_modified_p, full_path, 
                                            tmp_pool));
          if (prop_modified_p)
            SVN_ERR (do_prop_deltas (full_path, entry, editor, 
                                     tb->editor_baton, tmp_pool));

          /* Store the (added) affected-target for safe keeping (possibly
             to be used later for postfix text-deltas) */
          longpath = svn_stringbuf_dup (full_path, top_pool);
          apr_hash_set (affected_targets, longpath->data, longpath->len, tb);
        }
      
    } 
  /* END ADDITION CHECK */
  

  /* LOCAL MOD CHECK */
  else if (! (do_add || do_delete))
    {
      svn_boolean_t text_modified_p, prop_modified_p;
          
      /* Is text modified? */
      SVN_ERR (svn_wc_text_modified_p (&text_modified_p, full_path, 
                                       tmp_pool));
          
      /* Only check for local propchanges if we're looking at a file. 
         Our caller, crawl_dir(), is looking for propchanges on each
         directory it examines. */
      if (entry->kind == svn_node_dir)
        prop_modified_p = FALSE;
      else
        SVN_ERR (svn_wc_props_modified_p (&prop_modified_p, full_path, 
                                          tmp_pool));
      
      if (text_modified_p || prop_modified_p)
        {
          svn_stringbuf_t *longpath;
          struct target_baton *tb;
          
          /* There was a local change.  Build an affected-target
             object in the top-most pool. */
          tb = apr_pcalloc (top_pool, sizeof (*tb));
          tb->entry = svn_wc_entry_dup (entry, top_pool);
          tb->text_modified_p = text_modified_p;
          
          /* Build the full path to this entry, also from the top-pool. */
          longpath = svn_stringbuf_dup (full_path, top_pool);
          
          /* Do what's necesary to get a baton for current directory */
          if (! *dir_baton)
            SVN_ERR (do_dir_replaces (dir_baton,
                                      *stack, editor, edit_baton,
                                      locks, top_pool));
          
          /* Replace a file, getting a new file baton */
          if (entry->kind == svn_node_file)
            SVN_ERR (editor->open_file (entry_name,
                                        *dir_baton,
                                        entry->revision,
                                        &(tb->editor_baton)));
          
          if (prop_modified_p)
            {
              void *baton = (entry->kind == svn_node_file) ?
                tb->editor_baton : *dir_baton;
              
              /* Send propchanges to editor. */
              SVN_ERR (do_prop_deltas (longpath, entry, editor, baton, 
                                       tmp_pool));
                  
              /* Very important: if there are *only* propchanges, but
                 not textual ones, close the file here and now.
                 (Otherwise the file will be closed after sending
                 postfix text-deltas.)*/
              if ((entry->kind == svn_node_file) && (! text_modified_p))
                SVN_ERR (editor->close_file (tb->editor_baton));
            }
          
          /* Store the affected-target for safe keeping, to be used
             later for postfix text-deltas. */
          if (text_modified_p)
            apr_hash_set (affected_targets, longpath->data, longpath->len, tb);
        }
    }  
  /* END LOCAL MOD CHECK */
  

  /* Finally, decide whether or not to recurse.  Recurse only on
     directories that are not scheduled for deletion (add and replace
     are okay). */
  if ((entry->kind == svn_node_dir) 
      && (entry->schedule != svn_wc_schedule_delete))
    {
      /* Recurse, using new_dir_baton, which will most often be NULL
         (unless the entry is a newly added directory.)  Why NULL?
         Because that will later force a call to do_dir_replaces() and
         get the _correct_ dir baton for the child directory. */

      SVN_ERR (crawl_dir (full_path, 
                          new_dir_baton, 
                          editor, 
                          edit_baton, 
                          revnum_fn,
                          rev_baton,
                          youngest_rev,
                          copyfrom_url ? FALSE : adds_only,
                          copy_mode,
                          stack,
                          affected_targets, 
                          locks, 
                          top_pool));
    }

  return SVN_NO_ERROR;
}




/* A recursive working-copy "crawler", used to drive commits.

   Enter directory PATH and examine its entries for changes that need
   to be reported to EDITOR (using its associated EDIT_BATON and a
   calculated DIR_BATON).

   The DIR_BATON argument holds the current baton used to commit
   changes from PATH.  It may be NULL.  If it is NULL and a local
   change is discovered, then it (and all parent batons) will be
   automatically generated by do_dir_replaces(). 

   Open file-batons will be stored in AFFECTED_TARGETS using the
   never-changing top-level pool TOP_POOL (for submitting postfix
   text-deltas later.)  Any working copy dirs that are locked are
   appended to LOCKS.

   STACK should begin either as NULL, or pointing at the parent of
   PATH.  Stackframes are automatically pushed/popped as the crawl
   proceeds.  When this function returns, the top of stack will be
   exactly where it was.

   ADDS_ONLY and COPY_MODE are simply passed through this function to
   report_single_mod().

   Temporary: use REVNUM_FN/REV_BATON/YOUNGEST_REV to determine if a
   dir is up-to-date when it has a propchange. */
static svn_error_t *
crawl_dir (svn_stringbuf_t *path,
           void *dir_baton,
           const svn_delta_edit_fns_t *editor,
           void *edit_baton,
           const svn_ra_get_latest_revnum_func_t *revnum_fn,
           void *rev_baton,
           svn_revnum_t *youngest_rev,
           svn_boolean_t adds_only,
           svn_boolean_t copy_mode,
           struct stack_object **stack,
           apr_hash_t *affected_targets,
           apr_hash_t *locks,
           apr_pool_t *top_pool)
{
  apr_hash_t *entries;            /* all entries in PATH */
  apr_hash_index_t *entry_index;  /* holds loop-state */
  svn_wc_entry_t *this_dir_entry; /* represents current working dir */
  apr_pool_t *subpool;            /* per-recursion pool */
  apr_pool_t *iterpool;           /* per-iteration pool */
  svn_boolean_t prop_modified_p;

  /* Create the per-recusion subpool. */
  subpool = svn_pool_create (top_pool);

  /* Create the per-iteration subpool. */
  iterpool = svn_pool_create (subpool);

  /* Retrieve _all_ the entries in this subdir into subpool. */
  SVN_ERR (svn_wc_entries_read (&entries, path, subpool));

  /* Grab the entry representing "." */
  this_dir_entry = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                                 APR_HASH_KEY_STRING);
  if (! this_dir_entry)
    return svn_error_createf 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, top_pool,
       "Can't find `.' entry in %s", path->data);

  /* If the `.' entry is marked with ADD, then we *only* want to
     notice child entries that are also added.  It makes no sense to
     look for deletes or local mods in an added directory.
     
     Unless, of course, the add has 'copyfrom' history; when we enter
     a 'copied' subtree, we want to notice all things. */
  if (((this_dir_entry->schedule == svn_wc_schedule_add)
       || (this_dir_entry->schedule == svn_wc_schedule_replace))
      && (! this_dir_entry->copyfrom_url))
    adds_only = TRUE;

  /* Push the current {path, baton, this_dir} to the top of the stack */
  push_stack (stack, path, dir_baton, this_dir_entry, top_pool);

  /* Take care of any property changes this directory might have
     pending. */
  SVN_ERR (svn_wc_props_modified_p (&prop_modified_p, path, 
                                    (*stack)->pool));

  if (prop_modified_p)
    {
      /* Perform the necessary steps to ensure a dir_baton for this
         directory. */
      if (! dir_baton)
        SVN_ERR (do_dir_replaces (&dir_baton,
                                  *stack, editor, edit_baton,
                                  locks, top_pool));
        
      /* Temporary:  we don't allow the committing of prop-changes on
         an out-of-date directory, due to the Greg Hudson Scenarios.
         In the ideal world, this would be enforced server-side;  for
         now, we simply add an extra network turnaround and have the
         -client- enforce the rule instead. */
      if (revnum_fn)
        {
          /* If not already cached, ask the repo for youngest rev. */
          if (! SVN_IS_VALID_REVNUM (*youngest_rev))
            SVN_ERR ((*revnum_fn) (rev_baton, youngest_rev));
          
          /* Is the propchanged-dir out of date? */
          if (this_dir_entry->revision != *youngest_rev)
            return 
              svn_error_createf 
              (SVN_ERR_WC_NOT_UP_TO_DATE, 0, NULL, (*stack)->pool,
               "Directory '%s' is out-of-date;  cannot commit its propchange.",
               path->data);
        }

      /* It's possible that revnum function is NULL.  That's OK;
         commit propchange anyway.  (In the case of XML, it's OK;
         there's no repository to talk to.) */
      SVN_ERR (do_prop_deltas (path, this_dir_entry, editor, dir_baton, 
                               (*stack)->pool));

    }

  /* Loop over each entry */
  for (entry_index = apr_hash_first (subpool, entries); entry_index;
       entry_index = apr_hash_next (entry_index))
    {
      const void *key;
      const char *keystring;
      apr_ssize_t klen;
      void *val;
      svn_wc_entry_t *current_entry; 
      
      /* Get the next entry name (and structure) from the hash */
      apr_hash_this (entry_index, &key, &klen, &val);
      keystring = (const char *) key;

      /* Skip "this dir" */
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
        continue;

      /* Get the entry for this file or directory. */
      current_entry = (svn_wc_entry_t *) val;

      /* If we're looking at a subdir entry, the structure has
         incomplete information.  In particular, the revision is
         undefined (-1).  This is bad, because report_single_mod is
         trying to compare parent & child revisions when within a
         copied subtree.  So:  make sure the entry is _complete_. */
      if (current_entry->kind == svn_node_dir)
        {
          svn_stringbuf_t *full_path = svn_stringbuf_dup (path, iterpool);
          svn_path_add_component_nts (full_path, keystring);
          SVN_ERR (svn_wc_entry (&current_entry, full_path, iterpool));
        }

      /* Report mods for a single entry. */
      SVN_ERR (report_single_mod (keystring,
                                  current_entry,
                                  stack,
                                  affected_targets,
                                  locks,
                                  editor,
                                  edit_baton,
                                  revnum_fn,
                                  rev_baton,
                                  youngest_rev,
                                  &dir_baton,
                                  adds_only,
                                  copy_mode,
                                  iterpool,
                                  top_pool));
      
      /* Clear the iteration subpool. */
      svn_pool_clear (iterpool);
    }

  /* Destroy the iteration subpool. */
  svn_pool_destroy (iterpool);

  /* The presence of a baton in this stack frame means that at some
     point, something was committed in this directory, and means we
     must close that dir baton. */
  if ((*stack)->baton)
    SVN_ERR (editor->close_directory ((*stack)->baton));
  
  /* If stack has no previous pointer, then we'd be removing the base
     stackframe.  We don't want to do this, however;
     svn_wc_crawl_local_mods() needs to examine it to determine if any
     changes were ever made at all. */
  if ((*stack)->previous)
    pop_stack (stack);

  /* Free all memory used when processing this subdir. */
  svn_pool_destroy (subpool);
  
  return SVN_NO_ERROR;
}



/* The main logic of svn_wc_crawl_local_mods(), which is not much more
   than a public wrapper for this routine.  See its docstring.

   The differences between this routine and the public wrapper:

      - assumes that CONDENSED_TARGETS has been sorted (critical!)
      - takes an initialized LOCKED_DIRS hash for storing locked wc dirs.
            
   Temporary:  take a REVNUM_FN/REV_BATON so that we check that
   directories are up-to-date when they have propchanges.
*/
static svn_error_t *
crawl_local_mods (svn_stringbuf_t *parent_dir,
                  apr_array_header_t *condensed_targets,
                  const svn_delta_edit_fns_t *editor,
                  void *edit_baton,
                  const svn_ra_get_latest_revnum_func_t *revnum_fn,
                  void *rev_baton,                          
                  apr_hash_t *locked_dirs,
                  apr_hash_t *affected_targets,
                  apr_pool_t *pool)
{
  svn_error_t *err;
  void *dir_baton = NULL;

  /* A stack that will store all paths and dir_batons as we drive the
     editor depth-first. */
  struct stack_object *stack = NULL;

  /* A cache of the youngest revision in the repository, in case we
     discover any directory propchanges.  An invalid value means that
     this crawl hasn't yet discovered the info.  */
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM;

  /* No targets at all?  This means we are committing the entries in a
     single directory. */
  if (condensed_targets->nelts == 0)
    {
      /* Do a single crawl from parent_dir, that's it.  Parent_dir
         will be automatically pushed to the empty stack, but not
         removed.  This way we can examine the frame to see if there's
         a root_dir_baton, and thus whether we need to call
         close_edit(). */
      err = crawl_dir (parent_dir,
                       NULL,
                       editor, 
                       edit_baton,
                       revnum_fn,
                       rev_baton,
                       &youngest_rev,
                       FALSE,
                       FALSE,
                       &stack, 
                       affected_targets, 
                       locked_dirs,
                       pool);

      if (err)        
        return svn_error_quick_wrap 
          (err, "commit failed: while sending tree-delta to repos.");
    }

  /* This is the "multi-arg" commit processing branch.  That's not to
     say that there is necessarily more than one commit target, but
     whatever..." */
  else 
    {
      svn_wc_entry_t *parent_entry, *tgt_entry;
      int i;

      /* To begin, put the grandaddy parent_dir at the base of the stack. */
      SVN_ERR (svn_wc_entry (&parent_entry, parent_dir, pool));
      push_stack (&stack, parent_dir, NULL, parent_entry, pool);

      /* For each target in our CONDENSED_TARGETS list (which are
         given as paths relative to the PARENT_DIR 'grandaddy
         directory'), we pop or push stackframes until the stack is
         pointing to the immediate parent of the target.  From there,
         we can crawl the target for mods. */
      for (i = 0; i < condensed_targets->nelts; i++)
        {
          svn_stringbuf_t *ptarget;
          svn_stringbuf_t *remainder;
          svn_stringbuf_t *target, *subparent;
          svn_stringbuf_t *tgt_name =
            (((svn_stringbuf_t **) condensed_targets->elts)[i]);

          /* Get the full path of the target. */
          target = svn_stringbuf_dup (parent_dir, pool);
          svn_path_add_component (target, tgt_name);
          
          /* Examine top of stack and target, and get a nearer common
             'subparent'. */

          subparent = svn_path_get_longest_ancestor 
            (target, stack->path, pool);
          
          /* If the current stack path is NOT equal to the subparent,
             it must logically be a child of the subparent.  So... */
          if (svn_path_compare_paths (stack->path, subparent))
            {
              /* ...close directories and remove stackframes until the
                 stack reaches the common parent. */
              err = do_dir_closures (subparent, &stack, editor);         
              if (err)
                return svn_error_quick_wrap 
                  (err, "commit failed: error traversing working copy.");

              /* Reset the dir_baton to NULL; it is of no use to our
                 target (which is not a sibling, or a child of a
                 sibling, to any previous targets we may have
                 processed. */
              dir_baton = NULL;
            }

          /* Push new stackframes to get down to the immediate parent
             of the target PTARGET, which must also be a child of the
             subparent. */
          svn_path_split (target, &ptarget, NULL, pool);
          remainder = svn_path_is_child (stack->path, ptarget, pool);
          
          /* If PTARGET is below the current stackframe, we have to
             push a new stack frame for each directory level between
             them. */
          if (remainder)  
            {
              apr_array_header_t *components;
              int j;
              
              /* Invalidate the dir_baton, because it no longer
                 represents target's immediate parent directory. */
              dir_baton = NULL;

              /* split the remainder into path components. */
              components = svn_path_decompose (remainder, pool);
              
              for (j = 0; j < components->nelts; j++)
                {
                  svn_stringbuf_t *new_path;
                  svn_wc_entry_t *new_entry;
                  svn_stringbuf_t *component = 
                    (((svn_stringbuf_t **) components->elts)[j]);

                  new_path = svn_stringbuf_dup (stack->path, pool);
                  svn_path_add_component (new_path, component);
                  err = svn_wc_entry (&new_entry, new_path, pool);
                  if (err)
                    return svn_error_quick_wrap 
                      (err, "commit failed: looking for next commit target");

                  push_stack (&stack, new_path, NULL, new_entry, pool);
                }
            }
          

          /* NOTE: At this point of processing, the topmost stackframe
           * is GUARANTEED to be the parent of TARGET, regardless of
           * whether TARGET is a file or a directory. 
           */
          

          /* Get the entry for TARGET. */
          err = svn_wc_entry (&tgt_entry, target, pool);
          if (err)
            return svn_error_quick_wrap 
              (err, "commit failed: getting entry of commit target");

          if (tgt_entry)
            {
              apr_pool_t *subpool = svn_pool_create (pool);
              const char *basename = svn_path_basename (target->data, pool);
              
              /* If TARGET is a file, we check that file for mods.  No
                 stackframes will be pushed or popped, since (the file's
                 parent is already on the stack).  No batons will be
                 closed at all (in case we need to commit more files in
                 this parent). */
              err = report_single_mod (basename,
                                       tgt_entry,
                                       &stack,
                                       affected_targets,
                                       locked_dirs,
                                       editor,
                                       edit_baton,
                                       revnum_fn,
                                       rev_baton,
                                       &youngest_rev,
                                       &dir_baton,
                                       FALSE,
                                       FALSE,
                                       subpool,
                                       pool);
              
              svn_pool_destroy (subpool);
              
              if (err)
                return svn_error_quick_wrap 
                  (err, "commit failed: while sending tree-delta.");
            }
          else
            return svn_error_createf
              (SVN_ERR_UNVERSIONED_RESOURCE, 0, NULL, pool,
               "svn_wc_crawl_local_mods: '%s' is not a versioned resource",
               target->data);

        } /*  -- End of main target loop -- */
      
      /* To finish, pop the stack all the way back to the grandaddy
         parent_dir, and call close_dir() on all batons we find. */
      err = do_dir_closures (parent_dir, &stack, editor);
      if (err)
        return svn_error_quick_wrap 
          (err, "commit failed: finishing the crawl");

      /* Don't forget to close the root-dir baton on the bottom
         stackframe, if one exists. */
      if (stack->baton)        
        {
          err = editor->close_directory (stack->baton);
          if (err)
            return svn_error_quick_wrap 
              (err, "commit failed: closing editor's root directory");
        }

    }  /* End of multi-target section */


  /* All crawls are completed, so affected_targets potentially has
     some still-open file batons. Loop through affected_targets, and
     fire off any postfix text-deltas that need to be sent. */
  err = do_postfix_text_deltas (affected_targets, editor, pool);
  if (err)
    return svn_error_quick_wrap 
      (err, "commit failed:  while sending postfix text-deltas.");

  /* Have *any* edits been made at all?  We can tell by looking at the
     foundation stackframe; it might still contain a root-dir baton.
     If so, close the entire edit. */
  if (stack->baton)
    {
      err = editor->close_edit (edit_baton);
      if (err)
        {
          /* Commit failure, though not *necessarily* from the
             repository.  close_edit() does a LOT of things, including
             bumping all working copy revision numbers.  Again, see
             earlier comment.

             The interesting thing here is that the commit might have
             succeeded in the repository, but the WC lib returned a
             revision-bumping or wcprop error. */
          return svn_error_quick_wrap
            (err, "commit failed: while calling close_edit()");
        }
    }

  /* The commit is complete, and revisions have been bumped. */  
  return SVN_NO_ERROR;
}


/* Helper for report_revisions().
   
   Perform an atomic restoration of the file FILE_PATH; that is, copy
   the file's text-base to the administrative tmp area, and then move
   that file to FILE_PATH with possible translations/expansions.  */
static svn_error_t *
restore_file (svn_stringbuf_t *file_path,
              apr_pool_t *pool)
{
  svn_stringbuf_t *text_base_path, *tmp_text_base_path;
  svn_wc_keywords_t *keywords;
  enum svn_wc__eol_style eol_style;
  const char *eol;

  text_base_path = svn_wc__text_base_path (file_path, FALSE, pool);
  tmp_text_base_path = svn_wc__text_base_path (file_path, TRUE, pool);

  SVN_ERR (svn_io_copy_file (text_base_path->data, tmp_text_base_path->data,
                             FALSE, pool));

  SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol,
                                  file_path->data, pool));
  SVN_ERR (svn_wc__get_keywords (&keywords,
                                 file_path->data, NULL, pool));
  
  /* When copying the tmp-text-base out to the working copy, make
     sure to do any eol translations or keyword substitutions,
     as dictated by the property values.  If these properties
     are turned off, then this is just a normal copy. */
  SVN_ERR (svn_wc_copy_and_translate (tmp_text_base_path->data,
                                      file_path->data,
                                      eol, FALSE, /* don't repair */
                                      keywords,
                                      TRUE, /* expand keywords */
                                      pool));
  
  SVN_ERR (svn_io_remove_file (tmp_text_base_path->data, pool));

  return SVN_NO_ERROR;
}


/* The recursive crawler that describes a mixed-revision working
   copy to an RA layer.  Used to initiate updates.

   This is a depth-first recursive walk of DIR_PATH under WC_PATH.
   Look at each entry and check if its revision is different than
   DIR_REV.  If so, report this fact to REPORTER.  If an entry is
   missing from disk, report its absence to REPORTER.  

   If RESTORE_FILES is set, then unexpectedly missing working files
   will be restored from text-base and NOTIFY_FUNC/NOTIFY_BATON
   will be called to report the restoration. */
static svn_error_t *
report_revisions (svn_stringbuf_t *wc_path,
                  svn_stringbuf_t *dir_path,
                  svn_revnum_t dir_rev,
                  const svn_ra_reporter_t *reporter,
                  void *report_baton,
                  svn_wc_notify_func_t notify_func,
                  void *notify_baton,
                  svn_boolean_t restore_files,
                  svn_boolean_t recurse,
                  apr_pool_t *pool)
{
  apr_hash_t *entries, *dirents;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Construct the actual 'fullpath' = wc_path + dir_path */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (wc_path, subpool);
  svn_path_add_component (full_path, dir_path);

  /* Get both the SVN Entries and the actual on-disk entries. */
  SVN_ERR (svn_wc_entries_read (&entries, full_path, subpool));
  SVN_ERR (svn_io_get_dirents (&dirents, full_path, subpool));
  
  /* Do the real reporting and recursing. */

  /* Looping over current directory's SVN entries: */
  for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      apr_ssize_t klen;
      void *val;
      svn_stringbuf_t *current_entry_name;
      svn_wc_entry_t *current_entry; 
      svn_stringbuf_t *full_entry_path;
      enum svn_node_kind *dirent_kind;
      svn_boolean_t missing = FALSE;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      keystring = (const char *) key;
      current_entry = (svn_wc_entry_t *) val;

      /* Compute the name of the entry.  Skip THIS_DIR altogether. */
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
        continue;
      else
        current_entry_name = svn_stringbuf_create (keystring, subpool);

      /* Compute the complete path of the entry, relative to dir_path. */
      full_entry_path = svn_stringbuf_dup (dir_path, subpool);
      if (current_entry_name)
        svn_path_add_component (full_entry_path, current_entry_name);

      /* The Big Tests: */
      
      /* Is the entry on disk?  Set a flag if not. */
      dirent_kind = (enum svn_node_kind *) apr_hash_get (dirents, key, klen);
      if (! dirent_kind)
        missing = TRUE;
      
      /* From here on out, ignore any entry scheduled for addition
         or deletion */
      if (current_entry->schedule == svn_wc_schedule_normal)
        /* The entry exists on disk, and isn't `deleted'. */
        {
          if (current_entry->kind == svn_node_file) 
            {
              if (dirent_kind && (*dirent_kind != svn_node_file))
                {
                  /* If the dirent changed kind, report it as missing.
                     Later on, the update editor will return an
                     'obstructed update' error.  :)  */
                  SVN_ERR (reporter->delete_path (report_baton,
                                                  full_entry_path->data));
                  continue;  /* move to next entry */
                }

              if (missing && restore_files)
                {
                  svn_stringbuf_t *long_file_path 
                    = svn_stringbuf_dup (full_path, pool);
                  svn_path_add_component (long_file_path, current_entry_name);

                  /* Recreate file from text-base. */
                  SVN_ERR (restore_file (long_file_path, pool));

                  /* Report the restoration to the caller. */
                  if (notify_func != NULL)
                    (*notify_func) (notify_baton, 
                                    svn_wc_notify_restore,
                                    long_file_path->data);
                }

              /* Possibly report a differing revision. */
              if (current_entry->revision !=  dir_rev)                
                SVN_ERR (reporter->set_path (report_baton,
                                             full_entry_path->data,
                                             current_entry->revision));
            }

          else if (current_entry->kind == svn_node_dir && recurse)
            {
              if (missing)
                {
                  /* We can't recreate dirs locally, so report as missing. */
                  SVN_ERR (reporter->delete_path (report_baton,
                                                  full_entry_path->data));   
                  continue;  /* move on to next entry */
                }

              if (dirent_kind && (*dirent_kind != svn_node_dir))
                /* No excuses here.  If the user changed a
                   revision-controlled directory into something else,
                   the working copy is FUBAR.  It can't receive
                   updates within this dir anymore.  Throw a real
                   error. */
                return svn_error_createf
                  (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, subpool,
                   "The entry '%s' is no longer a directory,\n"
                   "which prevents proper updates.\n"
                   "Please remove this entry and try updating again.",
                   full_entry_path->data);
              
              /* Otherwise, possibly report a differing revision, and
                 recurse. */
              {
                svn_wc_entry_t *subdir_entry;
                svn_stringbuf_t *megalong_path = 
                  svn_stringbuf_dup (wc_path, subpool);
                svn_path_add_component (megalong_path, full_entry_path);
                SVN_ERR (svn_wc_entry (&subdir_entry, megalong_path, subpool));
                
                if (subdir_entry->revision != dir_rev)
                  SVN_ERR (reporter->set_path (report_baton,
                                               full_entry_path->data,
                                               subdir_entry->revision));
                /* Recurse. */
                SVN_ERR (report_revisions (wc_path,
                                           full_entry_path,
                                           subdir_entry->revision,
                                           reporter, report_baton,
                                           notify_func, notify_baton,
                                           restore_files, recurse,
                                           subpool));
              }
            } /* end directory case */
        } /* end 'entry exists on disk' */   
    } /* end main entries loop */

  /* We're done examining this dir's entries, so free everything. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
merge_commit_errors (svn_error_t *commit_err, svn_error_t *cleanup_err)
{
  /* Now deal with the errors that may have occurred. */
  if (commit_err && cleanup_err)
    {
      svn_error_t *scan;

      /* This is tricky... wrap the two errors and concatenate them. */
      commit_err = svn_error_quick_wrap 
        (commit_err, "---- commit error follows:");
      
      cleanup_err = svn_error_quick_wrap
        (cleanup_err, "commit failed (see below), and commit cleanup failed:");

      /* Hook the commit error to the end of the unlock error. */
      for (scan = cleanup_err; scan->child != NULL; scan = scan->child)
        continue;
      scan->child = commit_err;

      /* Return the unlock error; the commit error is at the end. */
      return cleanup_err;
    }

  if (commit_err)
    return svn_error_quick_wrap 
      (commit_err, "commit failed: wc locks and tmpfiles have been removed.");

  if (cleanup_err)
    return svn_error_quick_wrap
      (cleanup_err, "commit succeeded, but cleanup failed");
  
  return SVN_NO_ERROR;
}


/* Perform a commit crawl of a single working copy path (which is a
   PARENT directory plus a NAME'd entry in that directory) as if that
   path was scheduled to be added to the repository as a copy of
   PARENT+NAME's URL (with a new name of COPY_NAME).

   Use EDITOR/EDIT_BATON to accomplish this task, tracking all
   committed things in the AFFECTED_TARGETS hash, and all locked
   directories in the LOCKS hash.

   Use POOL for all necessary allocations.
 */
static svn_error_t *
crawl_as_copy (svn_stringbuf_t *parent,
               svn_stringbuf_t *name,
               svn_stringbuf_t *copy_name,
               const svn_delta_edit_fns_t *editor,
               void *edit_baton,
               apr_hash_t *affected_targets,
               apr_hash_t *locks,
               apr_pool_t *pool)
{
  struct stack_object *stack = NULL;
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM;
  void *dir_baton = NULL, *root_baton = NULL;
  svn_wc_entry_t *entry = NULL, *p_entry = NULL;
  svn_stringbuf_t *fullpath = svn_stringbuf_dup (parent, pool);
  svn_error_t *err;

  /* Assemble the full path of the commit target. */
  svn_path_add_component (fullpath, name);

  /* Get the entry for the parent of the commit target.  This needs to
     have a valid URL so we will know where to copy from. */
  SVN_ERR (svn_wc_entry (&p_entry, parent, pool));
  if (! p_entry)
    return svn_error_create 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, parent->data);
  if (! p_entry->url)
    return svn_error_create 
      (SVN_ERR_ENTRY_MISSING_URL, 0, NULL, pool, parent->data);

  /* Get the entry for the commit target. */
  SVN_ERR (svn_wc_entry (&entry, fullpath, pool));
  if (! entry)
    return svn_error_create 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, parent->data);

  /* Get the root baton. */
  SVN_ERR (editor->open_root (edit_baton, p_entry->revision, &root_baton));

  /* Make our entry look like it's slated to be copied. */
  entry->copyfrom_url = svn_stringbuf_dup (p_entry->url, pool);
  entry->copyfrom_rev = p_entry->revision;

  /* Push the anchor's stackframe onto the stack. */
  push_stack (&stack, parent, root_baton, p_entry, pool);
  
  if (entry->kind == svn_node_file)
    {
      void *file_baton;
      struct target_baton *tb = apr_pcalloc (pool, sizeof (*tb));

      /* Add our target with copyfrom history. */
      SVN_ERR (editor->add_file (copy_name,
                                 root_baton, 
                                 entry->url,
                                 entry->revision, 
                                 &file_baton));

      /* Populate our target baton, and shove it into the
         AFFECTED_TARGETS hash. */
      tb->entry = entry;
      tb->editor_baton = file_baton;
      SVN_ERR (svn_wc_text_modified_p (&(tb->text_modified_p), 
                                       fullpath, pool));
      apr_hash_set (affected_targets, fullpath->data, fullpath->len, tb);
    }
  else if (entry->kind == svn_node_dir)
    {
      /* Add our target with copyfrom history. */
      SVN_ERR (editor->add_directory (copy_name,
                                      root_baton, 
                                      entry->url,
                                      entry->revision, 
                                      &dir_baton));

      /* Crawl this directory in "copy mode".  This will push the
         stackframe with dir_baton, do some work, then close the
         directory and pop the stackframe for us. */
      SVN_ERR (crawl_dir (fullpath, dir_baton, editor, edit_baton,
                          NULL, NULL, &youngest_rev, FALSE, 
                          TRUE /* copy mode! */, &stack,
                          affected_targets, locks, pool));
    }
  else
    return svn_error_create 
      (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool, fullpath->data);

  /* All crawls are completed, so affected_targets potentially has
     some still-open file batons. Loop through affected_targets, and
     fire off any postfix text-deltas that need to be sent. */
  err = do_postfix_text_deltas (affected_targets, editor, pool);
  if (err)
    return svn_error_quick_wrap 
      (err, "commit failed:  while sending postfix text-deltas.");

  /* Close the root directory. */
  err = editor->close_directory (root_baton);
  pop_stack (&stack);

  /* Close the edit. */
  err = editor->close_edit (edit_baton);
  if (err)
    /* Commit failure, though not *necessarily* from the
       repository.  close_edit() does a LOT of things, including
       bumping all working copy revision numbers.  Again, see
       earlier comment.
       
       The interesting thing here is that the commit might have
       succeeded in the repository, but the WC lib returned a
       revision-bumping or wcprop error. */
    return svn_error_quick_wrap
      (err, "commit failed: while calling close_edit()");

  return SVN_NO_ERROR;
}




/*------------------------------------------------------------------*/
/*** Public Interfaces ***/


/* This is the main driver of the commit-editor.   It drives the
   editor in postfix-text-delta style. */


/* Fascinating note about the potential values of {parent_dir,
   condensed_targets} coming into this function:

   There are only four possibilities.

    1. No targets.
       parent = /home/sussman, targets = []

    2. One file target.
       parent = /home/sussman, targets = [foo.c]

    3. One directory target.(*)
       parent = /home/sussman, targets = [bar]

    4. Two or more targets of any type.
       parent = /home/sussman, targets = [foo.c, bar, baz, ...]

   (*) While svn_path_condense_targets does not allow for the
   possibility of a single directory target, the caller should have
   used svn_wc_get_actual_target in this case, which would result in
   the {parent_dir, NULL} combination possibly turning into a
   {parent_dir's parent, parent_dir} combination. */
svn_error_t *
svn_wc_crawl_local_mods (svn_stringbuf_t *parent_dir,
                         apr_array_header_t *condensed_targets,
                         const svn_delta_edit_fns_t *editor,
                         void *edit_baton,
                         const svn_ra_get_latest_revnum_func_t *revnum_fn,
                         void *rev_baton,
                         apr_pool_t *pool)
{
  svn_error_t *err, *err2;

  /* All the locally modified files which are waiting to be sent as
     postfix-textdeltas. */
  apr_hash_t *affected_targets = apr_hash_make (pool);

  /* All the wc directories that are "locked" as we commit local
     changes. */
  apr_hash_t *locked_dirs = apr_hash_make (pool);

  /* Sanity check. */
  assert (parent_dir != NULL);
  assert (condensed_targets != NULL);

  /* Sort the condensed targets so that targets which share "common
     sub-parent" directories are all lumped together.  This guarantees
     a depth-first drive of the editor. */
  qsort (condensed_targets->elts,
         condensed_targets->nelts,
         condensed_targets->elt_size,
         svn_sort_compare_strings_as_paths);

  /* Now pass the locked_dirs hash into the *real* routine that does
     the work. */
  err = crawl_local_mods (parent_dir,
                          condensed_targets,
                          editor, edit_baton,
                          revnum_fn, rev_baton,
                          locked_dirs,
                          affected_targets,
                          pool);

  /* Cleanup after the commit. */
  err2 = cleanup_commit (locked_dirs, affected_targets, pool);

  /* Return the merged commit errors. */
  return merge_commit_errors (err, err2);
}



/* This is the main driver of the working copy state "reporter", used
   for updates. */
svn_error_t *
svn_wc_crawl_revisions (svn_stringbuf_t *path,
                        const svn_ra_reporter_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_boolean_t recurse,
                        svn_wc_notify_func_t notify_func,
                        void *notify_baton,
                        apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_entry_t *entry;
  svn_revnum_t base_rev = SVN_INVALID_REVNUM;
  svn_boolean_t missing = FALSE;

  /* The first thing we do is get the base_rev from the working copy's
     ROOT_DIRECTORY.  This is the first revnum that entries will be
     compared to. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  base_rev = entry->revision;
  if (base_rev == SVN_INVALID_REVNUM)
    {
      svn_stringbuf_t *parent_name = svn_stringbuf_dup (path, pool);
      svn_wc_entry_t *parent_entry;
      svn_path_remove_component (parent_name);
      SVN_ERR (svn_wc_entry (&parent_entry, parent_name, pool));
      base_rev = parent_entry->revision;
    }

  /* The first call to the reporter merely informs it that the
     top-level directory being updated is at BASE_REV.  Its PATH
     argument is ignored. */
  SVN_ERR (reporter->set_path (report_baton, "", base_rev));

  if (entry->schedule != svn_wc_schedule_delete)
    {
      apr_finfo_t info;
      apr_status_t apr_err;
      apr_err = apr_stat (&info, path->data, APR_FINFO_MIN, pool);
      if (APR_STATUS_IS_ENOENT(apr_err))
        missing = TRUE;
    }

  if (entry->kind == svn_node_dir)
    {
      if (missing)
        {
          /* Always report directories as missing;  we can't recreate
             them locally. */
          err = reporter->delete_path (report_baton, "");
          if (err)
            {
              /* Clean up the fs transaction. */
              svn_error_t *fserr;
              fserr = reporter->abort_report (report_baton);
              if (fserr)
                return svn_error_quick_wrap (fserr, "Error aborting report.");
              else
                return err;
            }
        }

      else 
        {
          /* Recursively crawl ROOT_DIRECTORY and report differing
             revisions. */
          err = report_revisions (path,
                                  svn_stringbuf_create ("", pool),
                                  base_rev,
                                  reporter, report_baton,
                                  notify_func, notify_baton,
                                  restore_files, recurse, pool);
          if (err)
            {
              /* Clean up the fs transaction. */
              svn_error_t *fserr;
              fserr = reporter->abort_report (report_baton);
              if (fserr)
                return svn_error_quick_wrap (fserr, "Error aborting report.");
              else
                return err;
            }
        }
    }

  else if (entry->kind == svn_node_file)
    {
      if (missing && restore_files)
        {
          /* Recreate file from text-base. */
          SVN_ERR (restore_file (path, pool));

          /* Report the restoration to the caller. */
          if (notify_func != NULL)
            (*notify_func) (notify_baton, svn_wc_notify_restore, path->data);
        }

      if (entry->revision != base_rev)
        {
          /* If this entry is a file node, we just want to report that
             node's revision.  Since we are looking at the actual target
             of the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to set_path. */
          err = reporter->set_path (report_baton, "", base_rev);
          if (err)
            {
              /* Clean up the fs transaction. */
              svn_error_t *fserr;
              fserr = reporter->abort_report (report_baton);
              if (fserr)
                return svn_error_quick_wrap (fserr, "Error aborting report.");
              else
                return err;
            }
        }
    }

  /* Finish the report, which causes the update editor to be driven. */
  err = reporter->finish_report (report_baton);
  if (err)
    {
      /* Clean up the fs transaction. */
      svn_error_t *fserr;
      fserr = reporter->abort_report (report_baton);
      if (fserr)
        return svn_error_quick_wrap (fserr, "Error aborting report.");
      else
        return err;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_crawl_as_copy (svn_stringbuf_t *parent,
                      svn_stringbuf_t *name,
                      svn_stringbuf_t *copy_name,
                      const svn_delta_edit_fns_t *editor,
                      void *edit_baton,
                      apr_pool_t *pool)
{
  apr_hash_t *affected_targets = apr_hash_make (pool);
  apr_hash_t *locks = apr_hash_make (pool);
  svn_error_t *err, *err2;

  /* Do the actual work of this commit. */
  err = crawl_as_copy (parent, name, copy_name, editor, edit_baton,
                       affected_targets, locks, pool);

  /* Cleanup after the commit. */
  err2 = cleanup_commit (locks, affected_targets, pool);

  /* Return the merged commit errors. */
  return merge_commit_errors (err, err2);
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
