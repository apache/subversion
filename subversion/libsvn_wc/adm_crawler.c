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

  struct stack_object *next;
  struct stack_object *previous;
};


/* A crawler baton structure, used to encapsulate data needed at each
   step of recursion.  This is used by process_subdir, entry_callback,
   and even passes through expat via userdata.  */

typedef struct svn_wc__crawler_baton_t
{
  svn_string_t *path;     /* The current directory we're examining */
  void *dir_baton;        /* The dir_baton for cwd, if it yet exists */

  svn_delta_edit_fns_t *editor;
  void *edit_baton;

  struct stack_object *stack;  /* A stack of stack_objects */
  apr_pool_t *pool;

  apr_hash_t *filehash; /* A place to store unclosed file batons (for
                           postfix text-deltas); this field is always
                           inherited as the stack grows. */

} svn_wc__crawler_baton_t;



/* Create a new stack object containing PATH and BATON and push it on
   top of STACK. */
static void
append_stack (svn_wc__crawler_baton_t *crawlbaton,
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

  if (crawlbaton->stack == NULL)
    {
      /* This will be the very first object on the stack. */

      /* Create a hash that will hold pathnames that map to unclosed file
         batons.  This hash will be available within every `stackframe' of
         the crawl and will be used *after* the crawl to send postfix
         text-deltas. */      

      crawlbaton->stack = new_top;
    }

  else 
    {
      /* The stack already exists, so create links both ways, new_top
         becomes the top of the stack.  */

      crawlbaton->stack->next = new_top;
      new_top->previous = crawlbaton->stack;
      crawlbaton->stack = new_top;
    }
}


/* Remove youngest stack object from STACK. */
static void
remove_stack (svn_wc__crawler_baton_t *crawlbaton)
{
  struct stack_object *new_top;

  if (crawlbaton->stack->previous)
    {
      new_top = crawlbaton->stack->previous;
      crawlbaton->stack = new_top;
    }

  else
    crawlbaton->stack = NULL;  /* remove the last stackframe */
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
set_entry_flags (svn_string_t *filename,
                 enum svn_node_kind current_entry_type,
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
                        SVN_WC__ENTRIES_ATTR_ADD,
                        strlen(SVN_WC__ENTRIES_ATTR_ADD));
  *new_p = value ? TRUE : FALSE;

  /* Examine the hash for a "delete" xml attribute in the same
     manner. */
  value = apr_hash_get (current_entry_hash,
                        SVN_WC__ENTRIES_ATTR_DELETE,
                        strlen(SVN_WC__ENTRIES_ATTR_DELETE));
  *delete_p = value ? TRUE : FALSE;

  /* Call external routine to decide if this file has been locally
     modified.   The routine is called svn_wc__file_modified_p().  */
  if (current_entry_type == svn_file_kind)
    svn_wc__file_modified_p (modified_p, filename, pool);
  else
    *modified_p = FALSE;

  return SVN_NO_ERROR;
}



/* Given a PATH, return NEWEST_BATON which allows one to edit entries
   there.  Fetch and store (in STACK) any previous directory batons
   necessary to create the one for PATH (..using calls from EDITOR.)  */
static svn_error_t *
do_dir_replaces (svn_wc__crawler_baton_t *crawlbaton,
                 void **newest_baton)
{
  svn_error_t *err;
  struct stack_object *stackptr;  /* The current stack object we're
                                     examining */

  apr_pool_t *pool = crawlbaton->pool;
  struct stack_object *stack = crawlbaton->stack;
  svn_delta_edit_fns_t *editor = crawlbaton->editor;

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
          
          err = editor->replace_root (crawlbaton->edit_baton, &root_baton);  
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
          svn_string_t *ancestor_path;
          svn_vernum_t ancestor_ver;
          svn_string_t *dirname;
          void *dir_baton;

          /* Move up the stack */
          stackptr = stackptr->next;

          /* Get the ancestry for this directory */
          err = svn_wc__entry_get_ancestry (stackptr->path, NULL,
                                            &ancestor_path, &ancestor_ver,
                                            pool);
          if (err) return err;

          /* We only want the last component of the path; that's what
             the editor's replace_directory() expects from us. */
          dirname = svn_path_last_component (stackptr->path,
                                             svn_path_local_style, pool);

          /* Get a baton for this directory */
          err = 
            editor->replace_directory (dirname, /* current dir */
                                       stackptr->previous->baton, /* parent */
                                       ancestor_path,
                                       ancestor_ver,
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



/* Need to declare this routine here, since we have two static
   routines both calling one another. */
static svn_error_t *entry_callback (void *loop_baton,
                                    struct svn_wc__entry_baton_t *entrybaton);




/* Start-point of the recursive working-copy crawler.
   
   The CRAWLBATON contains all state and stack info about the
   crawler's current position in the working copy.  For each working
   copy subdir, call an xml parser on the entries file.  For each
   entry found by the xml parser, entry_callback() is called (see
   below).  */
static svn_error_t *
process_subdirectory (svn_wc__crawler_baton_t *crawlbaton)
{
  svn_error_t *err;
  svn_wc__entry_baton_t *entrybaton;
  apr_file_t *infile;
  apr_hash_t *att_hash;

  
  /* We've just arrived in a new subdirectory.  First thing to do is
     push the "current" path and baton to the top of the crawlbaton's
     stack. */
  append_stack (crawlbaton,
                crawlbaton->path,
                crawlbaton->dir_baton,
                crawlbaton->pool);

  /* Build an entrybaton that will be sent as "userdata" to the xml
     parser. */
  entrybaton = apr_pcalloc (crawlbaton->pool,
                            sizeof (struct svn_wc__entry_baton_t));

  /* Open entries file in the current directory for reading */
  infile = NULL;
  err = svn_wc__open_adm_file (&infile, crawlbaton->stack->path,
                               SVN_WC__ADM_ENTRIES,
                               APR_READ, crawlbaton->pool);
  if (err) return err;
  
  att_hash = apr_make_hash (crawlbaton->pool);

  /* Fill in the fields we care about. */
  entrybaton->pool             = crawlbaton->pool;
  entrybaton->infile           = infile;
  entrybaton->version          = SVN_INVALID_VERNUM;
  entrybaton->kind             = 0;
  entrybaton->attributes       = att_hash;
  entrybaton->looping          = TRUE;
  entrybaton->looper_callback  = entry_callback;  /* defined below */
  entrybaton->callback_baton   = crawlbaton;

  /* Parse the `entries' file.  Every time the parser finds a new
     entry, it will call entry_callback(), which will in turn
     (possibly) call this routine again, completing the recursion.  */
  err = do_parse (entrybaton);
  if (err) return err;

  /* When we arrive here, we're now done looping over this directory's
     entries, and we're done descending into its children.  Close the
     entries file. */
  err = svn_wc__close_adm_file (infile, crawlbaton->stack->path,
                                SVN_WC__ADM_ENTRIES, 0, crawlbaton->pool);
  if (err) return err;

  /* If the current stackframe has a non-NULL directory baton, then we
     must have issued an add_dir() or replace_dir() call already.
     Before we exit the function and move "up" the tree, we need to
     close this directory baton. */
  if (crawlbaton->stack->baton)
    {
      err = crawlbaton->editor->close_directory (crawlbaton->stack->baton);
      if (err) return err;
    }

  /* Special case: watch out for the *root* stack frame -- don't
     remove it.  It contains our filehash, chok' full of open file
     batons waiting for text deltas. */
  if (crawlbaton->stack->previous == NULL)
    /* This is the top-level frame, we're all done, return to
       svn_wc_crawl_local_mods now!  */
    return SVN_NO_ERROR;

  /* Otherwise, discard top of stack */
  remove_stack (crawlbaton);

  /* Pop "up" a directory in the working copy. */
  return SVN_NO_ERROR;
}



/* Main logic for parsing each entry.  

   Called by expat, whenever it finds a new entry in the `entries'
   file.  This routine then makes calls to the editor and recurses by
   calling process_subdirectory. 

   The CRAWLBATON structure contains all the state and stack relevant
   to the crawler's current location in the working copy.  The
   ENTRYBATON structure contains everything there is know about the
   "current" entry found by expat.  */
static svn_error_t *
entry_callback (void *loop_baton,
                struct svn_wc__entry_baton_t *entrybaton)
{
  svn_error_t *err;

  /* Vars that will be deduced for us */
  svn_boolean_t new_p;
  svn_boolean_t modified_p;
  svn_boolean_t delete_p;

  /* Reusable vars */
  svn_string_t *ancestor_path;
  svn_vernum_t ancestor_ver;

  /* Recover our crawlbaton */
  svn_wc__crawler_baton_t *crawlbaton
    = (svn_wc__crawler_baton_t *) loop_baton;

  /* Vars we steal from the crawlbaton */
  svn_string_t *path = crawlbaton->stack->path;
  svn_delta_edit_fns_t *editor = crawlbaton->editor;
  apr_pool_t *pool = crawlbaton->pool;

  /* Suck data from entrybaton into local vars, too */
  svn_string_t *current_entry_name      = entrybaton->entryname;
  enum svn_node_kind current_entry_type = entrybaton->kind;
  apr_hash_t *current_entry_hash        = entrybaton->attributes;

  /* Find out if this entry has been added, deleted, or modified. */
  svn_string_t *full_path_to_entry = svn_string_dup (path, pool);

  /* We don't want to call add_component on a NULL entry name. */
  svn_string_t *emptyname = svn_string_create ("", crawlbaton->pool);
  if (current_entry_name == NULL)
    current_entry_name = emptyname;

  svn_path_add_component (full_path_to_entry, current_entry_name,
                          svn_path_local_style, pool);
  err = set_entry_flags (full_path_to_entry,
                         current_entry_type,
                         current_entry_hash,
                         pool,
                         &new_p, &modified_p, &delete_p);
  if (err) return err;

  /* Main Logic */
  
  if (new_p)
    {
      /* Adding a new directory: */
      if (current_entry_type == svn_dir_kind)
        {
          void *new_dir_baton;
          
          /* Do what's necesary to get a baton for current directory */
          if (! crawlbaton->dir_baton)
            {
              err = do_dir_replaces (crawlbaton,
                                     &(crawlbaton->dir_baton));
              if (err) return err;
            }
          
          /* Get the ancestry for this new directory */
          err = svn_wc__entry_get_ancestry (path, NULL,
                                            &ancestor_path, &ancestor_ver,
                                            pool);
          if (err) return err;
          
          /* Add the new directory, getting a new dir baton.  */
          err = editor->add_directory (current_entry_name,
                                       crawlbaton->dir_baton, /* give parent */
                                       ancestor_path,
                                       ancestor_ver,
                                       &new_dir_baton);  /* get new child */
          if (err) return err;
          
          /* Recurse, using the new, extended path and new dir_baton. */
          svn_path_add_component (crawlbaton->path,
                                  current_entry_name,
                                  svn_path_local_style, pool);
          crawlbaton->dir_baton = new_dir_baton;

          err = process_subdirectory (crawlbaton);
          if (err) return err;
        }
      
      /* Adding a new file: */
      else if (current_entry_type == svn_file_kind)
        {
          void *file_baton;
          svn_string_t *longpath;
          
              /* Do what's necesary to get a baton for current directory */
          if (! crawlbaton->dir_baton)
            {
              err = do_dir_replaces (crawlbaton,
                                     &(crawlbaton->dir_baton));
              if (err) return err;
            }
          
          /* Get the ancestry for this new file */
          err = svn_wc__entry_get_ancestry (crawlbaton->path,
                                            current_entry_name,
                                            &ancestor_path, &ancestor_ver,
                                            pool);
          if (err) return err;
          
          /* Add a new file, getting a file baton */
          err = editor->add_file (current_entry_name,
                                  crawlbaton->dir_baton, /* parent */
                                  ancestor_path,
                                  ancestor_ver,
                                  &file_baton);          /* get file */
          if (err) return err;
          
          /* Store the file's full pathname and baton for safe keeping
             (to be used later for postfix text-deltas) */
          longpath = svn_string_dup (path, pool);
          svn_path_add_component (longpath, current_entry_name,
                                  svn_path_local_style, pool);
          apr_hash_set (crawlbaton->filehash, longpath->data, longpath->len,
                        file_baton);
        }
    }
      
  /* Delete a file or dir: */
  else if (delete_p)
    {
      /* Do what's necesary to get a baton for current directory */
      if (! crawlbaton->dir_baton)
        {
          err = do_dir_replaces (crawlbaton,
                                 &(crawlbaton->dir_baton));
          if (err) return err;
        }

      /* Delete the entry */
      err = editor->delete (current_entry_name,
                            crawlbaton->dir_baton);  /* parent */
      if (err) return err;
    }
  
  /* Replace a modified file: */
  else if (modified_p)
    {
      void *file_baton;
      svn_string_t *longpath;
      
      /* Do what's necesary to get a baton for current directory */
      if (! crawlbaton->dir_baton)
        {
          err = do_dir_replaces (crawlbaton,
                                 &(crawlbaton->dir_baton));
          if (err) return err;
        }
      
      /* Get the ancestry for this new file */
      err = svn_wc__entry_get_ancestry (path, current_entry_name,
                                        &ancestor_path, &ancestor_ver,
                                        pool);
      if (err) return err;
      
      /* Replace the file, getting a file baton */
      err = editor->replace_file (current_entry_name,
                                  crawlbaton->dir_baton, /* parent */
                                  ancestor_path,
                                  ancestor_ver,
                                  &file_baton);          /* get child */
      if (err) return err;
      
      /* Store the file's full pathname and baton for safe keeping (to
         be used later for postfix text-deltas) */
      longpath = svn_string_dup (path, pool);
      svn_path_add_component (longpath, current_entry_name,
                              svn_path_local_style, pool);
      apr_hash_set (crawlbaton->filehash, longpath->data, longpath->len,
                    file_baton);
    }

  /* Okay, we're not adding or deleting anything, nor is this a
     modified file.  However, if the this entry is a directory, we
     must recurse! */
  else if ((current_entry_type == svn_dir_kind) 
           && (! svn_string_isempty (current_entry_name)))
    {
      /* Recurse, using a NULL dir_baton.  Why NULL?  Because that
         will later force a call to do_dir_replaces() and get the
         _correct_ dir baton for the child directory. */
      svn_path_add_component (crawlbaton->path,
                              current_entry_name,
                              svn_path_local_style, pool);
      crawlbaton->dir_baton = NULL;

      err = process_subdirectory (crawlbaton);
      if (err) return err;
    }
  

  /* Done examining the current entry. */

  return SVN_NO_ERROR;
}




/*------------------------------------------------------------------*/
/*** Public Interface:  svn_wc_crawl_local_mods() ***/


svn_error_t *
svn_wc_crawl_local_mods (svn_string_t *root_directory,
                         svn_delta_edit_fns_t *edit_fns,
                         void *edit_baton,
                         svn_string_t *tok,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc__crawler_baton_t *crawlbaton;

  /* todo: Ben, use `tok'. */
  
  /* Create a crawlbaton for this commit, which will store all state
     and stack as the crawler moves through the working copy. */
  crawlbaton = apr_pcalloc (pool, sizeof(struct svn_wc__crawler_baton_t));

  crawlbaton->path       = svn_string_dup (root_directory, pool);
  crawlbaton->dir_baton  = NULL;
  crawlbaton->editor     = edit_fns;
  crawlbaton->edit_baton = edit_baton;
  crawlbaton->stack      = NULL;
  crawlbaton->pool       = pool;
  crawlbaton->filehash   = apr_make_hash (pool);

  /* Start the crawler! 

     Note that the first thing the crawler will do is push a new stack
     object onto the stack with PATH="root_directory" and BATON=NULL.  */
  err = process_subdirectory (crawlbaton);
  if (err) return err;

  /* The crawler has returned, and crawlbaton->filehash potentially
     has some still-open file batons.  */

  /* Loop through crawlbaton->filehash, and fire off any postfix
     text-deltas that may be needed. */
  err = do_postfix_text_deltas (crawlbaton->filehash, edit_fns, pool);
  if (err) return err;

  /* Finally, finish all the edits. */
  err = edit_fns->close_edit (edit_baton);
  if (err) return err;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
