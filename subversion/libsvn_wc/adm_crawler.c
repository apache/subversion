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




/* Local stack used by the crawler */
struct stack_object
{
  svn_string_t *path;   /* A working copy directory */
  void *baton;          /* An associated editor baton, if any exists yet. */

  struct stack_object *next;
  struct stack_object *previous;
}


/* Put a new stack object containing PATH and BATON on top of STACK. */
static void
append_stack (struct stack_object **stack,
              svn_string_t *path,
              void *baton,
              apr_pool_t *pool)
{
  struct stack_object *new_top =
    apr_pcalloc (pool, sizeof(struct stack_object));

  new_top->path = svn_string_dup (path, pool);
  new_top->baton = baton;

  *stack->next = new_top;
  new_top->previous = *stack;

  *stack = new_top;
}


/* Remove youngest stack object from STACK. */
static void
remove_stack (struct stack_object **stack)
{
  struct stack_object *new_top = *stack->previous;

  *stack = new_top;
}


/* A posix-like read function of type svn_read_fn_t.

   Given an open APR FILEHANDLE, read LEN bytes into BUFFER.
*/
static svn_error_t *
posix_file_reader (void *filehandle,
                   char *buffer,
                   apr_size_t *len,
                   apr_pool_t *pool)
{
  apr_status_t stat;

  /* Recover our filehandle */
  apr_file_t *xmlfile = (apr_file_t *) baton;

  stat = apr_full_read (xmlfile, buffer,
                        (apr_size_t) *len,
                        (apr_size_t *) len);
  
  if (stat && (stat != APR_EOF)) 
    return
      svn_error_create (stat, 0, NULL, pool,
                        "my_read_func: error reading xmlfile");
  
  return SVN_NO_ERROR;  
}



/* Decide the entry has been added, deleted, or modified by setting
   the value of NEW_P, MODIFIED_P, or DELETE_P. */
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

  /* Examine the hash for a "new" xml attribute */
  value = apr_hash_get (current_entry_hash, "new", 3);
  *new_p = value ? TRUE : FALSE;

  /* Examine the hash for a  "delete" xml attribute */
  value = apr_hash_get (current_entry_hash, "delete", 6);
  *delete_p = value ? TRUE : FALSE;

  /* Call external routine to decide if this file has been locally
     modified.   The routine is called svn_wc__file_modified_p().  */
  svn_wc__file_modified_p (modified_p, current_entry_name, pool);

  return SVN_NO_ERROR;
}



/* Given a PATH, return an editor-supplied baton which allows one to
   edit entries there. */
static void *
do_dir_replaces (svn_string_t *path, struct stack_object *stack)
{
  /* Logic:

     1.  Walk down the stack until you find a non-NULL baton.  (If you
         reach the bottom of the stack and still can't find one, then
         call replace_root() and store the root baton in the stack
         bottom.)

     2.  Walk *up* the stack now from this non-NULL position, issuing
         multiple "replace_directory()" calls and storing batons in
         the stack.  Stop when you fill in the top of the stack.

     3.  Return the final directory baton at the top of the stack.  */

  return 0;
}



/* Examine both the local and text-base copies of a file FILENAME, and
   push a text-delta to EDITOR using the supplied FILE_BATON. */
static svn_error_t *
do_apply_textdelta (svn_string_t *filename,
                    svn_delta_edit_fns_t *editor,
                    void *file_baton)
{
  svn_txdelta_window_handler_t *window_handler;
  void *window_handler_baton;
  svn_txdelta_stream_t *txdelta_stream;
  svn_txdelta_window_t *txdelta_window;
  apr_status_t status;
  apr_file_t *localfile = NULL;
  apr_file_t *textbasefile = NULL;

  /* Apply a textdelta to the file baton, getting a window
     consumer routine and baton */
  err = (* (editor->apply_textdelta)) (file_baton,
                                       &window_handler,
                                       &window_handler_baton);
  if (err) return err;

  /* Open two filehandles, one for local file and one for text-base file. */
  /* TODO, using *filename and svn_wc__open_ routines. */

  /* Create a text-delta stream object that pulls data out of the two
     files. */
  err= svn_txdelta (&txdelta_stream, 
                    posix_file_reader, (void *) localfile,
                    posix_file_reader, (void *) textbasefile,
                    pool);
  if (err) return err;
  
  /* Grab a window from the stream, "push" it at the consumer routine,
     then free it.  Repeat until there are no more windows. */
  while (txdelta_window)
    {
      err = svn_txdelta_next_window (&txdelta_window, txdelta_stream);
      if (err) return err;
      
      err = (* (window_handler)) (txdelta_window, window_handler_baton);
      if (err) return err;
      
      svn_txdelta_free_window (txdelta_window);
    }

  /* Free the stream */
  svn_txdelta_free (txdelta_stream);

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
                      struct stack_object  *stack,
                      apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc__entries_index *index;
  
  /* Vars that we automatically get when fetching a directory entry */
  svn_string_t *current_entry_name;
  svn_vernum_t current_entry_version;
  int current_entry_type;
  apr_hash_t *current_entry_hash;

  /* Vars that we will deduce ourselves. */
  svn_boolean_t new_p;
  svn_boolean_t modified_p;
  svn_boolean_t delete_p;

  /* Add the current path and baton to the top of the stack. */
  append_stack (&stack, path, dir_baton, pool);

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
                dir_baton = do_dir_replaces (path);

              /* Add the new directory, getting a new dir baton.  */
              err = (* (editor->add_directory)) (current_entry_name,
                                                 dir_baton,
                                                 ancestor_path,
                                                 ancestor_version,
                                                 &new_dir_baton);
              if (err) return err;

              /* Recurse into it, using the new dir_baton. */
              err = process_subdirectory (current_entry_name, new_dir_baton,
                                          editor, stack, pool);
              if (err) return err;
            }

          else if (current_entry_type == svn_file_kind)
            {
              void *file_baton;
              svn_string_t *ancestor_path;
              svn_vernum_t ancestor_ver;
              
              /* Do what's necesary to get a baton for current directory */
              if (! dir_baton)
                dir_baton = do_dir_replaces (path);
              
              /* Add a new file, getting a file baton */
              err = (* (editor->add_file)) (current_entry_name,
                                            dir_baton,
                                            ancestor_path,
                                            ancestor_ver,
                                            &file_baton);
              if (err) return err;
              
              /* Send the text-delta to this file baton */
              err = do_apply_textdelta (current_entry_name,
                                        editor,
                                        file_baton);
              
              /* Close the file baton. */
              err = (* (editor->close_file)) (file_baton);
              if (err) return err;
            }
        }
      
      else if (delete_p)
        {
          /* Do what's necesary to get a baton for current directory */
          if (! dir_baton)
            dir_baton = do_dir_replaces (path);

          /* Delete the entry */
          err = (* (editor->delete)) (current_entry_name, dir_baton);
          if (err) return err;
        }

      else if (modified_p)
        {
          void *file_baton;
          svn_string_t *ancestor_path;
          svn_vernum_t ancestor_ver;

          /* Do what's necesary to get a baton for current directory */
          if (! dir_baton)
            dir_baton = do_dir_replaces (path);
          
          /* Replace the file, getting a file baton */
          err = (* (editor->replace_file)) (current_entry_name,
                                            dir_baton,
                                            ancestor_path,
                                            ancestor_ver,
                                            &file_baton);
          if (err) return err;

          /* Send the text-delta to this file baton */
          err = do_apply_textdelta (current_entry_name, editor, file_baton);

          /* Close the file baton. */
          err = (* (editor->close_file)) (file_baton);
          if (err) return err;
        }

      else if (current_entry_type == svn_dir_kind)
        {
          /* Recurse, using a NULL dir_baton.  Why NULL?  Because that
             will force a call to do_dir_replaces() and get the
             _correct_ dir baton for the child directory.  */
          err = process_subdirectory (current_entry_name, NULL,
                                      editor, stack, pool);
        }

    } while (current_entry_name)
  
  /* If the current stackframe has a real directory baton, then we
  must have issued an add_dir() or replace_dir() call already.  Since
  we're now done looping through this directory's entries, we're going
  to pop "up" our tree and thus need to close the current
  directory. */
  if (stack->baton)
    {
      err = (* (editor->close_dir)) (stack->baton);
      if (err) return err;
    }

  /* Discard top of stack*/
  remove_stack (&stack);

  return SVN_NO_ERROR;
}






/*------------------------------------------------------------------*/
/* Public interface.

   Do a depth-first crawl of the local changes in a working copy,
   beginning at ROOT_DIRECTORY (absolute path).  Communicate all local
   changes (both textual and tree) to the supplied EDIT_FNS object.

   (Presumably, the client library will someday grab EDIT_FNS from
   libsvn_ra, and then pass it to this routine.  This is how local
   changes in the working copy are ultimately translated into network
   requests.)  */

svn_error_t *
svn_wc_crawl_local_mods (svn_string_t *root_directory,
                         svn_delta_edit_fns_t *edit_fns,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  struct stack_object stack_bottom;

  stack_bottom.path = NULL;
  stack_bottom.baton = NULL;
  
  /* Start the crawler! */
  err = process_subdirectory (svn_string_t *root_directory,
                              NULL,             /* No baton to start with. */
                              edit_fns,
                              &stack_bottom,
                              pool);

  if (err) return err;


  /* TODO: if (stack_bottom.baton), that means the editor was actually
     used, and we're looking at the remaining "root" baton.
     Therefore, we must call the editor's `close_edit()' with this
     baton. */

  return SVN_NO_ERROR;
}








/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
