/*
 * adm_crawler.c:  report local WC mods to an Editor.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */

/* ==================================================================== */


#include "apr_pools.h"
#include "apr_file_io.h"
#include "apr_hash.h"
#include "wc.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_delta.h"



/* Local "stack" objects used by the crawler to keep track of dir
   batons. */
struct stack_object
{
  svn_string_t *path;         /* A working copy directory */
  void *baton;                /* An associated dir baton, if any exists yet. */

  svn_wc__entry_t *this_dir;  /* All entry info about this directory */

  struct stack_object *next;
  struct stack_object *previous;
};




/* Create a new stack object containing PATH and BATON and push it on
   top of STACK. */
static void
push_stack (struct stack_object **stack,
            svn_string_t *path,
            void *baton,
            svn_wc__entry_t *entry,
            apr_pool_t *pool)
{
  struct stack_object *new_top =
    apr_pcalloc (pool, sizeof(struct stack_object));

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
      *stack = new_top;
    }

  else
    *stack = NULL;  /* remove the last stackframe */
}



/* A posix-like read function of type svn_read_fn_t (see svn_io.h).

   Given an already-open APR FILEHANDLE, read LEN bytes into BUFFER.  */
static svn_error_t *
posix_file_reader (void *filehandle,
                   char *buffer,
                   apr_size_t *len,
                   apr_pool_t *pool)
{
  apr_status_t stat;

  /* Recover our filehandle */
  apr_file_t *the_file = (apr_file_t *) filehandle;

  stat = apr_full_read (the_file, buffer,
                        (apr_size_t) *len,
                        (apr_size_t *) len);
  
  if (stat && (stat != APR_EOF)) 
    return
      svn_error_create (stat, 0, NULL, pool,
                        "adm_crawler.c (posix_file_reader): file read error");
  
  return SVN_NO_ERROR;  
}




/* Given the path on the top of STACK, store (and return) NEWEST_BATON
   -- which allows one to edit entries there.  Fetch and store (in
   STACK) any previous directory batons necessary to create the one
   for path (..using calls from EDITOR.)  */
static svn_error_t *
do_dir_replaces (void **newest_baton,
                 struct stack_object *stack,
                 svn_delta_edit_fns_t *editor,
                 void *edit_baton,
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
          /* Can't descend?  We must be at stack bottom.  Fetch the
             root baton here. */
          void *root_baton;
          
          err = editor->replace_root (edit_baton, &root_baton);  
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
                                       stackptr->this_dir->ancestor,
                                       stackptr->this_dir->version,
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

  return SVN_NO_ERROR;
}




/* Examine both the local and text-base copies of a file FILENAME, and
   push a text-delta to EDITOR using the already-opened FILE_BATON.
   (FILENAME is presumed to be a full path ending with a filename. ) */
static svn_error_t *
do_apply_textdelta (svn_string_t *filename,
                    svn_delta_edit_fns_t *editor,
                    void *file_baton,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t status;

  svn_txdelta_window_handler_t *window_handler;
  void *window_handler_baton;

  svn_txdelta_stream_t *txdelta_stream;
  svn_txdelta_window_t *txdelta_window;

  apr_file_t *localfile = NULL;
  apr_file_t *textbasefile = NULL;

  /* Apply a textdelta to the file baton, getting a window
     consumer routine and baton */
  err = editor->apply_textdelta (file_baton,
                                 &window_handler,
                                 &window_handler_baton);
  if (err) return err;

  /* Open two filehandles, one for local file and one for text-base file. */
  status = apr_open (&localfile, filename->data,
                     APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "do_apply_textdelta: error opening local file");

  err = svn_wc__open_text_base (&textbasefile, filename, APR_READ, pool);
  if (err) return err;
                                
  /* Create a text-delta stream object that pulls data out of the two
     files. */
  svn_txdelta (&txdelta_stream, 
               posix_file_reader, localfile,
               posix_file_reader, textbasefile,
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
  status = apr_close (localfile);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "do_apply_textdelta: error closing local file");

  err = svn_wc__close_text_base (textbasefile, filename, 0, pool);
  if (err) return err;

  return SVN_NO_ERROR;
}




/* Loop over FILEHASH, calling do_apply_textdelta().  FILEHASH, if
non-empty, contains a mapping of full file paths to still-open
file_batons.  After sending each text-delta, close each file_baton. */
static svn_error_t *
do_postfix_text_deltas (apr_hash_t *filehash,
                        svn_delta_edit_fns_t *editor,
                        apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_index_t *hi;
  svn_string_t *filepath;
  void *filebaton;
  const void *key;
  size_t keylen;

  for (hi = apr_hash_first (filehash); hi; hi = apr_hash_next (hi))
    {
      apr_hash_this (hi, &key, &keylen, &filebaton);

      filepath = svn_string_create ((char *) key, pool);

      err = do_apply_textdelta (filepath, editor, filebaton, pool);
      if (err) return err;

      err = editor->close_file (filebaton);
      if (err) return err;
    }

  return SVN_NO_ERROR;
}





/* The recursive working-copy crawler.

   Enter PATH and report any local changes to EDITOR.  All allocations
   will be made in POOL, and open file-batons will be stored in
   FILEHASH (for submitting postfix text-deltas later.)  STACK begins
   life as NULL, and is automatically allocated to store directory
   batons returned by the editor.
   
   The DIR_BATON argument holds the current baton used to commit
   changes from PATH.  It may be NULL.  If it is NULL and a local
   change is discovered, then it (and all parent batons) will be
   automatically generated by do_dir_replaces().  */

static svn_error_t *
process_subdirectory (svn_string_t *path, void *dir_baton,
                      svn_delta_edit_fns_t *editor, void *edit_baton,
                      struct stack_object *stack,
                      apr_hash_t *filehash,
                      apr_pool_t *pool)                      
{
  svn_error_t *err;
  apr_pool_t *subpool;

  apr_hash_t *entries;            /* _all_ of the entries in in
                                     current directory */
  apr_hash_index_t *entry_index;  /* holds loop-state */
  svn_wc__entry_t *this_dir;      /* represents current working dir */

  /**                                                   **/
  /** Setup -- arrival in a new subdir of working copy. **/
  /**                                                   **/

  /* First thing to do is create a new subpool */
  subpool = svn_pool_create (pool);

  /* Retrieve _all_ the entries in this subdir. */
  err = svn_wc__entries_read (&entries, path, subpool);

  /* Grab the entry representing "." */
  this_dir = (svn_wc__entry_t *) 
    apr_hash_get (entries, SVN_WC__ENTRIES_THIS_DIR,
                  sizeof(SVN_WC__ENTRIES_THIS_DIR));
  if (! this_dir)
    return
      svn_error_createf (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, subpool,
                         "Can't find `.' entry in %s", path->data);
                              
  /* Push the current {path, baton, this_dir} to the top of the stack */
  push_stack (&stack, path, dir_baton, this_dir, subpool);


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
      svn_wc__entry_t *current_entry; 
      svn_string_t *full_path_to_entry;

      /* Get the next entry name (and structure) from the hash */
      apr_hash_this (entry_index, &key, &klen, &val);
      keystring = (const char *) key;

      if (! strcmp (keystring, SVN_WC__ENTRIES_THIS_DIR))
        current_entry_name = NULL;
      else
        current_entry_name = svn_string_create (keystring, subpool);
      current_entry = (svn_wc__entry_t *) val;

      /* Construct a full path to the current entry */
      full_path_to_entry = svn_string_dup (path, subpool);
      if (current_entry_name != NULL)
        svn_path_add_component (full_path_to_entry, current_entry_name,
                                svn_path_local_style, subpool);


      /* Start examining the current_entry: */

      /* Is the entry marked for both deletion AND addition? */
      if ((current_entry->flags) & (SVN_WC__ENTRY_DELETE | SVN_WC__ENTRY_ADD))
        {
          /* Do what's necesary to get a baton for current directory */
          if (! dir_baton)
            {
              err = do_dir_replaces (&dir_baton,
                                     stack, editor, edit_baton, subpool);
              if (err) return err;
            }
          
          /* Delete the old entry FIRST.  This is IMPORTANT.  :) */
          err = editor->delete (current_entry_name, dir_baton);
          if (err) return err;
          
          /* Now replace the entry, either by calling replace_file()
             or replace_dir(). */

          if (current_entry->kind == svn_file_kind)
            {
              void *file_baton;
              svn_string_t *longpath;

              /* Replace the file, getting a file baton */
              err = editor->replace_file (current_entry_name,
                                          dir_baton,          /* parent */
                                          current_entry->ancestor,
                                          current_entry->version,
                                          &file_baton);       /* get child */
              if (err) return err;
              
              /* Store the file's full pathname and baton for safe keeping (to
                 be used later for postfix text-deltas) */
              longpath = svn_string_dup (path, subpool);
              if (current_entry_name != NULL)
                svn_path_add_component (longpath, current_entry_name,
                                        svn_path_local_style, subpool);
              apr_hash_set (filehash, longpath->data, longpath->len,
                            file_baton);              
            }

          else if (current_entry->kind == svn_dir_kind)
            {
              void *new_dir_baton;
              svn_string_t *new_path = svn_string_dup (path, subpool);
              
              err = 
                editor->replace_directory (current_entry_name, 
                                           dir_baton,        /* parent */
                                           current_entry->ancestor,
                                           current_entry->version,
                                           &new_dir_baton);
              if (err) return err;
              
              /* Recurse, using the new, extended path and new dir_baton. */
              if (current_entry_name != NULL)
                svn_path_add_component (new_path,
                                        current_entry_name,
                                        svn_path_local_style, subpool);
              
              err = process_subdirectory (new_path, new_dir_baton,
                                          editor, edit_baton,
                                          stack, filehash, subpool);
              if (err) return err;              
            }
        }

      /* Is the entry marked for deletion only? */
      else if ((current_entry->flags) & SVN_WC__ENTRY_DELETE)
        {
          /* Do what's necesary to get a baton for current directory */
          if (! dir_baton)
            {
              err = do_dir_replaces (&dir_baton,
                                     stack, editor, edit_baton, subpool);
              if (err) return err;
            }
          
          /* Delete the entry */
          err = editor->delete (current_entry_name, dir_baton);
          if (err) return err;
        }


      /* Is this entry marked for addition only? */
      else if ((current_entry->flags) & SVN_WC__ENTRY_ADD)
        {
          /* Adding a new directory: */
          if (current_entry->kind == svn_dir_kind)
            {
              void *new_dir_baton;
              svn_string_t *new_path = svn_string_dup (path, subpool);
              
              /* Do what's necesary to get a baton for current directory */
              if (! dir_baton)
                {
                  err = do_dir_replaces (&dir_baton,
                                         stack, editor, edit_baton, subpool);
                  if (err) return err;
                }
              
              /* Add the new directory, getting a new dir baton.  */
              err = editor->add_directory (current_entry_name,
                                           dir_baton, /* current dir
                                                         is parent */
                                           current_entry->ancestor,
                                           current_entry->version,
                                           &new_dir_baton); /* get child */
              if (err) return err;
          
              /* Recurse, using the new, extended path and new dir_baton. */
              if (current_entry_name != NULL)
                svn_path_add_component (new_path,
                                        current_entry_name,
                                        svn_path_local_style, subpool);
              
              err = process_subdirectory (new_path, new_dir_baton,
                                          editor, edit_baton,
                                          stack, filehash, subpool);
              if (err) return err;
            }
      
          /* Adding a new file: */
          else if (current_entry->kind == svn_file_kind)
            {
              void *file_baton;
              svn_string_t *longpath;

              /* Do what's necesary to get a baton for current directory */
              if (! dir_baton)
                {
                  err = do_dir_replaces (&dir_baton,
                                         stack, editor, edit_baton, subpool);
                  if (err) return err;
                }
              
              /* Add a new file, getting a file baton */
              err = editor->add_file (current_entry_name,
                                      dir_baton,             /* parent */
                                      current_entry->ancestor,
                                      current_entry->version,
                                      &file_baton);          /* get file */
              if (err) return err;
              
              /* Store the file's full pathname and baton for safe keeping
                 (to be used later for postfix text-deltas) */
              longpath = svn_string_dup (path, subpool);
              if (current_entry_name != NULL)
                svn_path_add_component (longpath, current_entry_name,
                                        svn_path_local_style, subpool);
              apr_hash_set (filehash, longpath->data, longpath->len,
                            file_baton);

              /* Don't close the file yet!  That comes much later,
                 after we send text-deltas. */
            }
        }

      /* Is this entry a modified file? */      
      else if (current_entry->kind == svn_file_kind)      
        {
          void *file_baton;
          svn_string_t *longpath;
          svn_boolean_t modified_p;

          err = svn_wc__file_modified_p (&modified_p,
                                         full_path_to_entry,
                                         subpool);
          if (err) return err;

          if (modified_p)
            {
              /* Do what's necesary to get a baton for current directory */
              if (! dir_baton)
                {
                  err = do_dir_replaces (&dir_baton,
                                         stack, editor, edit_baton, subpool);
                  if (err) return err;
                }
          
              /* Replace the file, getting a file baton */
              err = editor->replace_file (current_entry_name,
                                          dir_baton,          /* parent */
                                          current_entry->ancestor,
                                          current_entry->version,
                                          &file_baton);       /* get child */
              if (err) return err;
              
              /* Store the file's full pathname and baton for safe keeping (to
                 be used later for postfix text-deltas) */
              longpath = svn_string_dup (path, subpool);
              if (current_entry_name != NULL)
                svn_path_add_component (longpath, current_entry_name,
                                        svn_path_local_style, subpool);
              apr_hash_set (filehash, longpath->data, longpath->len,
                            file_baton);
            }
        }
      
      /* Okay, we're not adding or deleting anything, nor is this a
         modified file.  However, if the this entry is a directory, we
         must recurse! */
      else if ((current_entry->kind == svn_dir_kind) 
               && (current_entry_name != NULL))
        {
          /* Recurse, using a NULL dir_baton.  Why NULL?  Because that
             will later force a call to do_dir_replaces() and get the
             _correct_ dir baton for the child directory. */
          err = process_subdirectory (path, NULL,
                                      editor, edit_baton,
                                      stack, filehash, subpool);
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
  if (stack->baton)
    {
      err = editor->close_directory (stack->baton);
      if (err) return err;
    }

  /* Discard top of stack */
  pop_stack (&stack);

  /* Free all memory used when processing this subdir. */
  apr_destroy_pool (subpool);

  return SVN_NO_ERROR;
}






/*------------------------------------------------------------------*/
/*** Public Interface:  svn_wc_crawl_local_mods() ***/


svn_error_t *
svn_wc_crawl_local_mods (svn_string_t *root_directory,
                         svn_delta_edit_fns_t *edit_fns,
                         void *edit_baton,
                         apr_pool_t *pool)
{
  svn_error_t *err;

  struct stack_object *stack = NULL;
  apr_hash_t *filehash = apr_make_hash (pool);

  /* Start the crawler! 

     Note that the first thing the crawler will do is push a new stack
     object onto the stack with PATH="root_directory" and BATON=NULL.  */
  err = process_subdirectory (root_directory, NULL,
                              edit_fns, edit_baton,
                              stack, filehash, pool);
  if (err) return err;

  /* The crawler has returned, so filehash potentially has some
     still-open file batons.  */

  /* Loop through filehash, and fire off any postfix text-deltas that
     may be needed. */
  err = do_postfix_text_deltas (filehash, edit_fns, pool);
  if (err) return err;
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
