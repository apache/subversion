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
  svn_string_t *path;   /* A working copy directory */
  void *baton;          /* An associated editor baton, if any exists yet. */

  apr_hash_t *filehash; /* A place to store unclosed file batons (for
                           postfix text-deltas); this field is always
                           inherited as the stack grows. */

  struct stack_object *next;
  struct stack_object *previous;
};


/* Create a new stack object containing PATH and BATON and push it on
   top of STACK. */
static void
append_stack (struct stack_object **stack,
              svn_string_t *path,
              void *baton,
              apr_pool_t *pool)
{
  struct stack_object *new_top =
    apr_pcalloc (pool, sizeof(struct stack_object));

  /* Store path and baton in a new stack object */
  new_top->path = svn_string_dup (path, pool);
  new_top->baton = baton;
  new_top->next = NULL;
  new_top->previous = NULL;

  if (*stack == NULL)
    {
      /* This will be the very first object on the stack. */

      /* Create a hash that will hold pathnames that map to unclosed file
         batons.  This hash will be available within every `stackframe' of
         the crawl and will be used *after* the crawl to send postfix
         text-deltas. */      
      new_top->filehash = apr_make_hash (pool);

      *stack = new_top;
    }

  else 
    {
      /* The stack already exists, so create links both ways, inherit
         the hash, and new_top becomes the top of the stack.  */

      new_top->filehash = (*stack)->filehash; /* directly inherited */
      (*stack)->next = new_top;
      new_top->previous = *stack;
      *stack = new_top;
    }
}


/* Remove youngest stack object from STACK. */
static void
remove_stack (struct stack_object **stack)
{
  
  struct stack_object *new_top = (*stack)->previous;

  *stack = new_top;
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



/* Given the NAME, TYPE, and HASH attribute info for a directory
   entry, decide if the entry has been added, deleted, or modified.
   Return these flags by setting the value of NEW_P, MODIFIED_P, or
   DELETE_P. */
static svn_error_t *
set_entry_flags (svn_string_t *current_entry_name,
                 int current_entry_type,
                 apr_hash_t *current_entry_hash,
                 apr_pool_t *pool,
                 svn_boolean_t *new_p,
                 svn_boolean_t *modified_p, 
                 svn_boolean_t *delete_p)
{
  void *value;

  /* Examine the hash for a "new" xml attribute.  If this attribute
     doesn't exists, then the hash value will be NULL.  */
  value = apr_hash_get (current_entry_hash,
                        SVN_WC__ENTRIES_ATTR_NEW,
                        strlen(SVN_WC__ENTRIES_ATTR_NEW));
  *new_p = value ? TRUE : FALSE;

  /* Examine the hash for a "delete" xml attribute in the same
     manner. */
  value = apr_hash_get (current_entry_hash,
                        SVN_WC__ENTRIES_ATTR_DELETE,
                        strlen(SVN_WC__ENTRIES_ATTR_DELETE));
  *delete_p = value ? TRUE : FALSE;

  /* Call external routine to decide if this file has been locally
     modified.   The routine is called svn_wc__file_modified_p().  */
  svn_wc__file_modified_p (modified_p, current_entry_name, pool);

  return SVN_NO_ERROR;
}



/* Given a PATH, return NEWEST_BATON which allows one to edit entries
   there.  Fetch and store (in STACK) any previous directory batons
   necessary to create the one for PATH (..using calls from EDITOR.)  */
static svn_error_t *
do_dir_replaces (svn_string_t *path,
                 apr_pool_t *pool,
                 struct stack_object **stack,
                 svn_delta_edit_fns_t *editor,
                 void *edit_baton,
                 void **newest_baton)
{
  svn_error_t *err;
  struct stack_object *stackptr;  /* The current stack object we're
                                     examining */

  stackptr = *stack;   /* Start at the top of the stack */

  while (1)  /* Walk down the stack until we find a non-NULL dir baton. */
    {
      if (stackptr->baton != NULL) 
        /* Found an existing directory baton! */
        break;
      
      if (stackptr->previous)  
        stackptr = stackptr->previous;  /* descend. */
      else
        {
          /* Can't descend?  We must have reached stack_bottom, which
             is just an empty placeholder.  */

          /* Move up the stack to the "root" stackframe and fetch the
             root baton. */
          void *root_baton;

          err = editor->replace_root (edit_baton, &root_baton);  
          if (err) return err;
          
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
          svn_string_t *ancestor_path;
          svn_vernum_t ancestor_ver;
          void *dir_baton;

          /* Move up the stack */
          stackptr = stackptr->next;

          /* Get the ancestry for this directory */
          err = svn_wc__entry_get_ancestry (stackptr->path, NULL,
                                            &ancestor_path, &ancestor_ver,
                                            pool);
          if (err) return err;
          
          /* Get a baton for this directory */
          err = 
            editor->replace_directory (stackptr->path, /* current dir */
                                       stackptr->previous->path, /* parent */
                                       ancestor_path,
                                       ancestor_ver,
                                       &dir_baton);
          if (err) return err;

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
  err= svn_txdelta (&txdelta_stream, 
                    posix_file_reader, localfile,
                    posix_file_reader, textbasefile,
                    pool);
  if (err) return err;
  
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





/* Recursive working-copy crawler.
   
   Examine each entry in the `entries' file in PATH.  Communicate all
   local changes to EDITOR.  Use DIR_BATON as the editor-supplied
   baton for this directory; if this value is NULL, that's okay: it
   will be automatically set when necessary.  

   STACK is used to keep track of paths and associated directory
   batons, and always represents the top (youngest) stackframe.  
*/
static svn_error_t *
process_subdirectory (svn_string_t *path,
                      void *dir_baton,
                      svn_delta_edit_fns_t *editor,
                      void *edit_baton,
                      struct stack_object **stack,
                      apr_pool_t *pool)
{
  svn_error_t *err;
  struct svn_wc__entries_index *index;

  svn_string_t *ancestor_path;
  svn_vernum_t ancestor_ver;
  
  /* Vars that we automatically get when fetching a directory entry */
  svn_string_t *current_entry_name;
  svn_vernum_t current_entry_version;
  enum svn_node_kind current_entry_type;
  apr_hash_t *current_entry_hash;

  /* Vars that we will deduce ourselves. */
  svn_boolean_t new_p;
  svn_boolean_t modified_p;
  svn_boolean_t delete_p;

  /* Push the current path and baton to the top of the stack. */
  append_stack (stack, path, dir_baton, pool);

  /* Start looping over each entry in this directory */
  err = svn_wc__entries_start (&index, path, pool);
  if (err) return err;

  /* Continue looping over each entry, examining each. */
  do
    {
      /* Get the next entry in current directory. */
      err = svn_wc__entries_next (index, &current_entry_name,
                                  &current_entry_version, &current_entry_type,
                                  &current_entry_hash);
      if (err) return err;
      
      /* Decide the entry has been added, deleted, or modified. */
      err = set_entry_flags (current_entry_name,
                             current_entry_type,
                             current_entry_hash,
                             pool,
                             &new_p, &modified_p, &delete_p);
      if (err) return err;
      
      if (new_p)
        {
          if (current_entry_type == svn_dir_kind)
            {
              void *new_dir_baton;

              /* Do what's necesary to get a baton for current directory */
              if (! dir_baton)
                {
                  err = do_dir_replaces (path, pool, stack,
                                         editor, edit_baton,
                                         &dir_baton);
                  if (err) return err;
                }

              /* Get the ancestry for this new directory */
              err = svn_wc__entry_get_ancestry (path, NULL,
                                                &ancestor_path, &ancestor_ver,
                                                pool);
              if (err) return err;
              
              /* Add the new directory, getting a new dir baton.  */
              err = editor->add_directory (current_entry_name,
                                           dir_baton,
                                           ancestor_path,
                                           ancestor_ver,
                                           &new_dir_baton);
              if (err) return err;

              /* Recurse into it, using the new dir_baton. */
              err = process_subdirectory (current_entry_name, new_dir_baton,
                                          editor, edit_baton, stack, pool);
              if (err) return err;
            }

          else if (current_entry_type == svn_file_kind)
            {
              void *file_baton;
              svn_string_t *longpath = svn_string_dup (path, pool);
              
              /* Do what's necesary to get a baton for current directory */
              if (! dir_baton)
                {
                  err = do_dir_replaces (path, pool, stack,
                                         editor, edit_baton,
                                         &dir_baton);
                  if (err) return err;
                }

              /* Get the ancestry for this new file */
              err = svn_wc__entry_get_ancestry (path, current_entry_name,
                                                &ancestor_path, &ancestor_ver,
                                                pool);
              if (err) return err;

              /* Add a new file, getting a file baton */
              err = editor->add_file (current_entry_name,
                                      dir_baton,
                                      ancestor_path,
                                      ancestor_ver,
                                      &file_baton);
              if (err) return err;
              
              /* Store the file's full pathname and filebaton for safe
                 keeping (and to be used later for postfix
                 text-deltas) */
              svn_path_add_component (longpath, current_entry_name,
                                      svn_path_local_style, pool);
              apr_hash_set ((*stack)->filehash, longpath->data, longpath->len,
                            file_baton);
            }
        }
      
      else if (delete_p)
        {
          /* Do what's necesary to get a baton for current directory */
          if (! dir_baton)
            {
              err = do_dir_replaces (path, pool, stack,
                                     editor, edit_baton,
                                     &dir_baton);
              if (err) return err;
            }

          /* Delete the entry */
          err = editor->delete (current_entry_name, dir_baton);
          if (err) return err;
        }

      else if (modified_p)
        {
          void *file_baton;
          svn_string_t *longpath = svn_string_dup (path, pool);

          /* Do what's necesary to get a baton for current directory */
          if (! dir_baton)
            {
              err = do_dir_replaces (path, pool, stack,
                                     editor, edit_baton,
                                     &dir_baton);
              if (err) return err;
            }

          /* Get the ancestry for this new file */
          err = svn_wc__entry_get_ancestry (path, current_entry_name,
                                            &ancestor_path, &ancestor_ver,
                                            pool);
          if (err) return err;
                    
          /* Replace the file, getting a file baton */
          err = editor->replace_file (current_entry_name,
                                      dir_baton,
                                      ancestor_path,
                                      ancestor_ver,
                                      &file_baton);
          if (err) return err;

          /* Store the file's full pathname and filebaton for safe
             keeping (and to be used later for postfix
             text-deltas) */
          svn_path_add_component (longpath, current_entry_name,
                                  svn_path_local_style, pool);
          apr_hash_set ((*stack)->filehash, longpath->data, longpath->len,
                        file_baton);          
        }
      
      else if (current_entry_type == svn_dir_kind)
        {
          /* Recurse, using a NULL dir_baton.  Why NULL?  Because that
             will force a call to do_dir_replaces() and get the
             _correct_ dir baton for the child directory.  */
          err = process_subdirectory (current_entry_name, NULL,
                                      editor, edit_baton, stack, pool);
        }

    } while (current_entry_name);
  
  /* When we arrive here, we're now done looping over this directory's
     entries, and we're done descending into its children. */

  /* Special case: watch out for the *root* stack frame.  It can only
     be closed by close_edit(), not close_directory().  And we don't
     want to lose this frame anyway, no sir.  This frame contains our
     filehash, chok' full of open file batons waiting for text
     deltas. */
  if ((*stack)->previous == NULL)
    /* This is the top-level frame, we're all done, get out. */
    return SVN_NO_ERROR;

  /* If the current stackframe has a non-NULL directory baton, then we
     must have issued an add_dir() or replace_dir() call already.
     Before we exit the function and move "up" the tree, we need to
     close this directory baton. */
  if ((*stack)->baton)
    {
      err = editor->close_directory ((*stack)->baton);
      if (err) return err;
    }

  /* Discard top of stack */
  remove_stack (stack);

  return SVN_NO_ERROR;
}




/*** Closing commits. ***/

/* Intended as the USER_DATA argument to close_commit(). */
struct close_commit_baton
{
  svn_string_t *root_dir;    /* Top of commit in working copy. */
  int cid;                   /* Working copy's token for this commit. */
  apr_pool_t *pool;          /* What would life be like without a pool? */
};


/* An implementation of `svn_wc_close_commit_fn_t'. */
static svn_error_t *
close_commit (void *user_data, svn_vernum_t new_version)
{
#if 0
  struct close_commit_baton *cb = (struct close_commit_baton *) user_data;
#endif /* 0 */

  /* kff todo: walk down from root, updating adm areas with the
     version number of the successful commit. */ 

  return SVN_NO_ERROR;
}


/*------------------------------------------------------------------*/
svn_error_t *
svn_wc_crawl_local_mods (svn_string_t *root_directory,
                         svn_delta_edit_fns_t *edit_fns,
                         void *edit_baton,
                         svn_wc_close_commit_fn_t **close_commit_fn,
                         void **close_commit_baton,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  struct stack_object *stack_bottom = NULL;
  struct close_commit_baton *close_baton;

  /* Set up a closure function and baton, for close_edit() to call. */
  close_baton = apr_palloc (pool, sizeof (*close_baton));
  close_baton->root_dir = root_directory;
  close_baton->cid = 0;
  close_baton->pool = pool;
  *close_commit_fn = close_commit;
  *close_commit_baton = close_baton;

  /* Start the crawler! */

  /* Note that the first thing the crawler will do is push a new stack
     object onto the stack with PATH="root_directory" and BATON=NULL.  */
  err = process_subdirectory (root_directory,
                              NULL,            /* No baton to start with. */
                              edit_fns,
                              edit_baton,
                              &stack_bottom,   /* NULL stack to start with. */
                              pool);
  if (err) return err;

  /* The crawler has returned, and *stack_bottom now points to a very
     valuable top-level stack object. */

  /* Loop through stack_bottom->filehash, and fire off any postfix
     text-deltas that may be needed. */
  err = do_postfix_text_deltas (stack_bottom->filehash, edit_fns, pool);
  if (err) return err;

  /* If the bottom of the stack contains a non-NULL dir baton, that
     means the editor was actually used at some point, and we're
     looking at the remaining "root" baton.  Therefore, we must call
     the editor's `close_edit()'. */
  if (stack_bottom->baton)
    {
      err = edit_fns->close_edit (edit_baton);
      if (err) return err;
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
