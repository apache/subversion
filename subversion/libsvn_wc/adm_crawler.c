/*
 * adm_crawler.c:  report local WC mods to an Editor.
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_delta.h"



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
  svn_string_t *path;         /* A working copy directory */
  void *baton;                /* An associated dir baton, if any exists yet. */
  svn_wc_entry_t *this_dir;   /* All entry info about this directory */

  struct stack_object *next;
  struct stack_object *previous;
};




/* Create a new stack object containing {PATH, BATON, ENTRY} and push
   it on top of STACK. */
static void
push_stack (struct stack_object **stack,
            svn_string_t *path,
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
      svn_string_t *unlock_path;
      
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
do_lock (svn_string_t *path, apr_hash_t *locks, apr_pool_t *pool)
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
          svn_string_t *dirname;
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




/* Examine both the local and text-base copies of a file FILENAME, and
   push a text-delta to EDITOR using the already-opened FILE_BATON.
   (FILENAME is presumed to be a full path ending with a filename.) */
static svn_error_t *
do_apply_textdelta (svn_string_t *filename,
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

  svn_string_t *local_tmp_path;

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
  if (! (tb->entry->state & SVN_WC_ENTRY_ADDED))
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
  svn_string_t *entrypath;
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
do_prop_deltas (svn_string_t *path,
                svn_wc_entry_t *entry,
                const svn_delta_edit_fns_t *editor,
                void *baton,
                apr_pool_t *pool)
{
  svn_error_t *err;
  int i;
  svn_string_t *prop_path, *prop_base_path, *tmp_prop_path;
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
bail_if_unresolved_conflict (svn_string_t *full_path,
                             svn_wc_entry_t *entry,
                             apr_hash_t *locks,
                             apr_pool_t *pool)
{
  svn_error_t *err;

  if (entry->state & SVN_WC_ENTRY_CONFLICTED)
    {
      /* We must decide if either component is "conflicted", based
         on whether reject files are mentioned and/or continue to
         exist.  Luckily, we have a function to do this.  :) */
      svn_boolean_t text_conflict_p, prop_conflict_p;
      svn_string_t *parent_dir;
      
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


/* ### not actually used for right now. remove when this func is used. */
#if 0

/* Given an array of starting PATHS (svn_string_t's), return the
   longest common path that they all contain.  Return NULL if so such
   beast exists.  */
static svn_string_t *
get_common_path (const apr_array_header_t *paths,
                 apr_pool_t *pool)
{
  int i;
  svn_string_t *longest_common_path;

  if (paths->nelts <= 0)
    return NULL;

  longest_common_path = (((svn_string_t **)(paths)->elts)[0]);

  for (i = 1; i < paths->nelts; i++)
    {
      svn_string_t *next_path, *the_longer;            

      next_path = (((svn_string_t **)(paths)->elts)[i]);

      /* might return NULL if no common base path: */
      the_longer = svn_path_get_longest_ancestor (next_path,
                                                  longest_common_path,
                                                  pool);

      if (! the_longer) /* at least two paths have NO common base path */
        return NULL;

      longest_common_path = the_longer;
    }

  return longest_common_path;
}

#endif


/* A recursive working-copy "crawler", used for commits.

   Enter PATH and report any local changes to EDITOR.  

   The DIR_BATON argument holds the current baton used to commit
   changes from PATH.  It may be NULL.  If it is NULL and a local
   change is discovered, then it (and all parent batons) will be
   automatically generated by do_dir_replaces(). 

   All allocations will be made in POOL, and open file-batons will be
   stored in AFFECTED_TARGETS using the never-changing top-level pool
   TOP_POOL (for submitting postfix text-deltas later.)  

   STACK begins life as NULL, and is automatically allocated to store
   directory batons returned by the editor.  */

static svn_error_t *
report_local_mods (svn_string_t *path,
                   void *dir_baton,
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

  /**                                                   **/
  /** Setup -- arrival in a new subdir of working copy. **/
  /**                                                   **/

  /* First thing to do is create a new subpool to hold all temporary
     work at this level of the working copy. */
  apr_pool_t *subpool = svn_pool_create (top_pool);

  /* Retrieve _all_ the entries in this subdir into subpool. */
  err = svn_wc_entries_read (&entries, path, subpool);
  if (err)
    return err;

  /* Grab the entry representing "." */
  this_dir = (svn_wc_entry_t *) 
    apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
  if (! this_dir)
    return
      svn_error_createf (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, subpool,
                         "Can't find `.' entry in %s", path->data);
                              
  /* Push the current {path, baton, this_dir} to the top of the stack */
  push_stack (stack, path, dir_baton, this_dir, subpool);


  /**                           **/
  /** Main Logic                **/
  /**                           **/

  /* Loop over each entry */
  for (entry_index = apr_hash_first (entries); entry_index;
       entry_index = apr_hash_next (entry_index))
    {
      const void *key;
      const char *keystring;
      apr_size_t klen;
      void *val;
      svn_string_t *current_entry_name;
      svn_wc_entry_t *current_entry; 
      svn_string_t *full_path_to_entry;

      /* Get the next entry name (and structure) from the hash */
      apr_hash_this (entry_index, &key, &klen, &val);
      keystring = (const char *) key;

      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
        current_entry_name = NULL;
      else
        current_entry_name = svn_string_create (keystring, subpool);
      current_entry = (svn_wc_entry_t *) val;

      /* Construct a full path to the current entry */
      full_path_to_entry = svn_string_dup (path, subpool);
      if (current_entry_name != NULL)
        svn_path_add_component (full_path_to_entry, current_entry_name,
                                svn_path_local_style);


      /* Start examining the current_entry: */
      
      /* Preemptive strike:  if the current entry is a file in a state
         of conflict that has NOT yet been resolved, we abort the
         entire commit.  */
      err = bail_if_unresolved_conflict (full_path_to_entry,
                                         current_entry,
                                         locks, subpool);
      if (err) return err;

      /* Is the entry marked for deletion? */
      if (current_entry->state & SVN_WC_ENTRY_DELETED)
        {
          /* Do what's necessary to get a baton for current directory */
          if (! dir_baton)
            {
              err = do_dir_replaces (&dir_baton,
                                     *stack, editor, edit_baton,
                                     locks, top_pool, subpool);
              if (err) return err;
            }
          
          /* Delete the entry */
          err = editor->delete_entry (current_entry_name, dir_baton);
          if (err) return err;

          /* Remember that it was affected. */
          {
            svn_string_t *longpath;
            struct target_baton *tb = apr_pcalloc (top_pool, sizeof (*tb));
            
            tb->entry = svn_wc__entry_dup (current_entry, top_pool);
            longpath = svn_string_dup (path, top_pool);
            if (current_entry_name != NULL)
              svn_path_add_component (longpath, current_entry_name,
                                      svn_path_local_style);
            apr_hash_set (affected_targets,
                          longpath->data, longpath->len, tb);
          }
        }

      /* Is this entry marked for addition only? */
      else if (current_entry->state & SVN_WC_ENTRY_ADDED)
        {
          /* Create an affected-target object */
          svn_string_t *longpath;
          struct target_baton *tb = apr_pcalloc (top_pool, sizeof (*tb));
          tb->entry = svn_wc__entry_dup (current_entry, top_pool);          

          /* Do what's necesary to get a baton for current directory */
          if (! dir_baton)
            {
              err = do_dir_replaces (&dir_baton,
                                     *stack, editor, edit_baton,
                                     locks, top_pool, subpool);
              if (err) return err;
            }

          /* Adding a new directory: */
          if (current_entry->kind == svn_node_dir)
            {
              /* Add the new directory, getting a new dir baton.  */
              err = editor->add_directory (current_entry_name,
                                           dir_baton, /* current dir
                                                         is parent */
                                           current_entry->ancestor,
                                           current_entry->revision,
                                           &new_dir_baton); /* get child */
              if (err) return err;
            }
      
          /* Adding a new file: */
          else if (current_entry->kind == svn_node_file)
            {
              /* Add a new file, getting a file baton */
              err = editor->add_file (current_entry_name,
                                      dir_baton,             /* parent */
                                      current_entry->ancestor,
                                      current_entry->revision,
                                      &(tb->editor_baton));  /* child */
              if (err) return err;

              /* Of course, this is a new file, so we would should
                 definitely mark the target as having new text! */
              tb->text_modified_p = TRUE;
            }

          /* Check for local property changes to send, whether we're
             looking a file or a dir.  */
          {
            svn_boolean_t prop_modified_p;

            err = svn_wc_props_modified_p (&prop_modified_p,
                                           full_path_to_entry,
                                           subpool);
            if (err) return err;

            if (prop_modified_p)
              {
                void *baton = 
                  (current_entry->kind == svn_node_file) ?
                  tb->editor_baton : new_dir_baton;

                err = do_prop_deltas (full_path_to_entry,
                                      current_entry,
                                      editor,
                                      baton,
                                      subpool);
                if (err) return err;

                tb->prop_modified_p = TRUE;
              }            
          }          

          /* Store the affected-target for safe keeping (possibly to
             be used later for postfix text-deltas) */
          longpath = svn_string_dup (path, top_pool);
          if (current_entry_name != NULL)
            svn_path_add_component (longpath, current_entry_name,
                                    svn_path_local_style);
          apr_hash_set (affected_targets,
                        longpath->data, longpath->len, tb);
        }

      /* We're done looking for ADD and DELETE flags.  If we're not
         adding or deleting the entry, look for LOCAL MODS of any
         sort. */
      else
        {
          svn_boolean_t text_modified_p, prop_modified_p;
          
          err = svn_wc_text_modified_p (&text_modified_p,
                                        full_path_to_entry,
                                        subpool);
          if (err) return err;
          
          /* Only check for local propchanges if we're looking at a
             file, or if we're looking at SVN_WC_ENTRY_THIS_DIR.
             Otherwise, each directory will end up being checked
             twice! */
          if ((current_entry->kind == svn_node_dir)
              && (current_entry_name != NULL))
            /* always assume there's no propchange on `PATH/DIR_NAME' */
            prop_modified_p = FALSE;
          else
            {
              /* but do a real check on `PATH/.' */
              err = svn_wc_props_modified_p (&prop_modified_p,
                                             full_path_to_entry,
                                             subpool);
              if (err) return err;
            }
          
          if (text_modified_p || prop_modified_p)
            {
              svn_string_t *longpath;
              struct target_baton *tb;

              /* There was a local change.  Build an affected-target
                 object in the top-most pool. */
              tb = apr_pcalloc (top_pool, sizeof (*tb));
              tb->entry = svn_wc__entry_dup (current_entry, top_pool);
              tb->text_modified_p = text_modified_p;
              tb->prop_modified_p = prop_modified_p;

              /* Build the full path to this entry, also from the
                 top-pool. */
              longpath = svn_string_dup (path, top_pool);
              if (current_entry_name != NULL)
                svn_path_add_component (longpath, current_entry_name,
                                        svn_path_local_style);
              
              /* Do what's necesary to get a baton for current directory */
              if (! dir_baton)
                {
                  err = do_dir_replaces (&dir_baton,
                                         *stack, editor, edit_baton,
                                         locks, top_pool, subpool);
                  if (err) return err;
                }

              if (current_entry->kind == svn_node_file)
                {
                  /* Replace the file's text, getting a file baton */
                  err = editor->replace_file (current_entry_name,
                                              dir_baton,          /* parent */
                                              current_entry->revision,
                                              &(tb->editor_baton)); /* child */
                  if (err) return err;
                }
              
              if (prop_modified_p)
                {
                  void *baton = 
                    (current_entry->kind == svn_node_file) ?
                    tb->editor_baton : dir_baton;
                  
                  err = do_prop_deltas (longpath,
                                        current_entry,
                                        editor,
                                        baton,
                                        subpool);
                  if (err) return err;

                  /* Very important logic!! */
                  if ((current_entry->kind == svn_node_file )
                      && (! text_modified_p))
                    {
                      err = editor->close_file (tb->editor_baton); 
                    }
                }
              
              /* Store the affected-target for safe keeping (possibly
                 to be used later for postfix text-deltas) */
              apr_hash_set (affected_targets,
                            longpath->data, longpath->len, tb);
            }
        }
      
      /* Finally: if the current entry is a directory (and not `.'),
         we must recurse! */
      if ((current_entry->kind == svn_node_dir) 
          && (current_entry_name != NULL))
        {
          /* Recurse, using new_dir_baton, which will most often be
             NULL (unless the entry is a newly added directory.)  Why
             NULL?  Because that will later force a call to
             do_dir_replaces() and get the _correct_ dir baton for the
             child directory. */
          err = report_local_mods (full_path_to_entry, new_dir_baton,
                                      editor, edit_baton,
                                      stack, affected_targets, locks,
                                      top_pool);
          if (err) return err;
        }
     
      /* Done examining the current entry. */      
    }  

  /* Done examining _all_ entries in this subdir. */

  /**                                                           **/
  /** Cleanup -- ready to "pop up" a level in the working copy. **/
  /**                                                           **/
  
  /* If the current dir (or any of its children) reported changes to
     the editor, then we must remember to close the current dir baton. */
  if ((*stack)->baton)
    {
      err = editor->close_directory ((*stack)->baton);
      if (err) return err;
    }

  /* If stack has no previous pointer, then we'd be removing the final
     stackframe.  We don't want to do this, however;
     svn_wc_crawl_local_mods() needs to examine it to determine if any
     changes were ever made. */
  if (! (*stack)->previous)
    return SVN_NO_ERROR;

  /* Discard top of stack */
  pop_stack (stack);

  /* Free all memory used when processing this subdir. */
  apr_pool_destroy (subpool);

  return SVN_NO_ERROR;
}






/*------------------------------------------------------------------*/
/*** Public Interfaces ***/

svn_error_t *
svn_wc_crawl_local_mods (apr_hash_t **targets,
                         svn_string_t *root_directory,
                         const svn_delta_edit_fns_t *edit_fns,
                         void *edit_baton,
                         apr_pool_t *pool)
{
  svn_error_t *err;

  struct stack_object *stack = NULL;
  apr_hash_t *affected_targets = apr_hash_make (pool);
  apr_hash_t *locked_dirs = apr_hash_make (pool);

  /* Start the crawler! 

     Note that the first thing the crawler will do is push a new stack
     object onto the stack with PATH="root_directory" and BATON=NULL.  */
  SVN_ERR (report_local_mods (root_directory, NULL,
                              edit_fns, edit_baton,
                              &stack, affected_targets, locked_dirs,
                              pool));

  /* The crawler has returned, so affected_targets potentially has some
     still-open file batons.  */

  /* Loop through affected_targets, and fire off any postfix text-deltas that
     may be needed. */
  SVN_ERR (do_postfix_text_deltas (affected_targets, edit_fns, pool));

  /* Have any edits been made at all?  We can tell by looking at the
     top-level stackframe left over from the crawl; it might still
     contain the root-dir baton.  If so, close the editor. */
  if (stack->baton)
    {
      err = edit_fns->close_edit (edit_baton);
      if (err) return err;
    }

  *targets = affected_targets;

  /* The commit is complete, and revisions have been bumped. */

  /* Remove all the lockfiles. */
  SVN_ERR (remove_all_locks (locked_dirs, pool));
  
  return SVN_NO_ERROR;
}





svn_error_t *
svn_wc_crawl_revisions (svn_string_t *root_directory,
                        const svn_delta_edit_fns_t *edit_fns,
                        void *edit_baton,
                        apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_entry_t *root_entry;
  svn_revnum_t master_revnum = SVN_INVALID_REVNUM;

  /* The first thing we do is get the master_revnum from the
     working copy's ROOT_DIRECTORY.  This is the revnum that all
     entries will be compared to. */
  err = svn_wc_entry (&root_entry, root_directory, pool);
  if (err)
    return 
      svn_error_quick_wrap 
      (err, "svn_wc_crawl_revisions: couldn't find wc's master revnum");
                            
  master_revnum = root_entry->revision;

  /* Next, call replace_root() and push as first item on stack...? */


  /* Start the mini-crawler. 
     err = report_revisions (root_directory, NULL,
     edit_fns, edit_baton,
     &stack, master_revnum,
     pool); */
  if (err) return err;


  /* Close the edit, causing the update_editor to be driven. */
  err = edit_fns->close_edit (edit_baton);
  if (err) return err; /* pay attention to this return value!! */

  return SVN_NO_ERROR;

}












/* 

   Ben's notes (temporary):  re:  how to make svn_wc_crawl_local_mods
   take *multiple* start-path arguments in an apr_array_header_t.

   Assumption:  no start-path is contained within another.

   Consider this diagram:

           P
         /  \
        Q
      /   \
     C     B
    / \   / \
   A
  / \

   Pretend we receive an array with two start-paths to traverse:  `A'
   and `B'.  (Obviously, there's no intersection between these two
   sub-trees.) 

   We can't simply call report_local_mods (A), followed by
   process_subdirecotry (B).  Why?  Because do_dir_replaces() crawls
   up the tree trying to create dir_batons, and when it reaches `A' or
   `B', it calls replace_root().  We can't call replace_root() more
   than once during the entire commit!

   Here's the solution:

   1.  Look at our array of start-paths, and find the *nearest* common
   ancestor dir of them.  (In our case, `Q'.)

   2.  Place Q on the *bottom* of our stack, sans dir_baton.

   3.  Before the first crawl, push stackframes containing each path
   going from `Q' down to `A'.  Again, no dir_batons.

   4.  Start the first crawl at `A'.  do_dir_replaces() will
   automatically walk all the way up to `Q' and call replace_root().

   5.  When the first crawl is done, remove all the stackframes until
   there's only the lone `Q' frame left.  (This frame now contains the
   root_baton!)

   6.  For each subsequent crawl, repeats steps 3, 4, 5.  Each
   subsequent crawl shares the same stack, the same affected_targets
   hash, and the same locks hash.  (If any crawl fails, then *all*
   locks are cleaned up.)

   7.  When all crawls are done, do as we're already doing:  check the
   oldest stackframe (`Q') and see if it has a root_baton.  If so,
   call close_edit().  If not, then no commit was ever made.

 */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
