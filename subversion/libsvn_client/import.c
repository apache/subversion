/*
 * import.c:  import a local file or tree using an svn_delta_edit_fns_t.
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


#include <assert.h>
#include <apr.h>
#include <apr_errno.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_delta.h"
#include "svn_path.h"


/* Apply PATH's contents (as a delta against the empty string) to
   FILE_BATON in EDITOR.  Use POOL for any temporary allocation.  */
static svn_error_t *
send_file_contents (svn_string_t *path,
                    void *file_baton,
                    const svn_delta_edit_fns_t *editor,
                    apr_pool_t *pool)
{
  svn_stream_t *contents;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  svn_txdelta_stream_t *delta_stream;
  svn_txdelta_window_t *window;
  apr_file_t *f = NULL;
  apr_status_t apr_err;

  /* Get an apr file for PATH. */
  apr_err = apr_file_open (&f, path->data, APR_READ, APR_OS_DEFAULT, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    {
      return svn_error_createf
        (apr_err, 0, NULL, pool, "error opening `%s' for reading", path->data);
    }
  
  /* Get a readable stream of the file's contents. */
  contents = svn_stream_from_aprfile (f, pool);

  /* Create a delta stream which converts an *empty* bytestream into the
     file's contents bytestream. */
  svn_txdelta (&delta_stream, svn_stream_empty (pool), contents, pool);

  /* Get an editor func that wants to consume the delta stream. */
  SVN_ERR (editor->apply_textdelta (file_baton, &handler, &handler_baton));

  /* Pull windows from the delta stream and feed to the consumer. */
  do 
    {
      SVN_ERR (svn_txdelta_next_window (&window, delta_stream));
      SVN_ERR ((*handler) (window, handler_baton));

    } while (window);

  /* Close the file. */
  apr_err = apr_file_close (f);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    {
      return svn_error_createf
        (apr_err, 0, NULL, pool, "error closing `%s'", path->data);
    }

  return SVN_NO_ERROR;
}


/* Import file PATH as NAME in the repository directory indicated by
 * DIR_BATON in EDITOR.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_file (const svn_delta_edit_fns_t *editor,
             void *dir_baton,
             svn_string_t *path,
             svn_string_t *name,
             apr_pool_t *pool)
{
  void *file_baton;
  
  SVN_ERR (editor->add_file (name, dir_baton,
                             NULL, SVN_INVALID_REVNUM, 
                             &file_baton));          
  SVN_ERR (send_file_contents (path, file_baton, editor, pool));
  SVN_ERR (editor->close_file (file_baton));

  return SVN_NO_ERROR;
}
             

/* Import directory PATH into the repository directory indicated by
 * DIR_BATON in EDITOR.  Don't call EDITOR->close_directory(DIR_BATON),
 * that's left for the caller.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_dir (const svn_delta_edit_fns_t *editor, 
            void *dir_baton,
            svn_string_t *path,
            apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_dir_t *dir;
  apr_finfo_t this_entry;
  apr_status_t apr_err;

  apr_err = apr_dir_open (&dir, path->data, subpool);
  for (apr_err = apr_dir_read (&this_entry, APR_FINFO_NORM, dir);
       APR_STATUS_IS_SUCCESS (apr_err);
       apr_err = apr_dir_read (&this_entry, APR_FINFO_NORM, dir))
    {
      svn_string_t *new_path = svn_string_dup (path, subpool);
      svn_path_add_component (new_path,
                              svn_string_create (this_entry.name, subpool),
                              svn_path_local_style);

      if (this_entry.filetype == APR_DIR)
        {
          svn_string_t *name = svn_string_create (this_entry.name, subpool);
          void *this_dir_baton;

          /* Skip entries for this dir and its parent.  
             ### kff todo: APR actually promises that they'll come
             first, so this guard could be moved outside the loop. */
          if ((strcmp (this_entry.name, ".") == 0)
              || (strcmp (this_entry.name, "..") == 0))
            continue;

          /* If someone's trying to import a tree with SVN/ subdirs,
             that's probably not what they wanted to do.  Someday we
             can take an option to make the SVN/ subdirs be silently
             ignored, but for now, seems safest to error. */
          if (strcmp (this_entry.name, SVN_WC_ADM_DIR_NAME) == 0)
            return svn_error_createf
              (SVN_ERR_CL_ADM_DIR_RESERVED, 0, NULL, subpool,
               "cannot import directory named \"%s\" (in `%s')",
               this_entry.name, path->data);

          /* Get descent baton from the editor. */
          SVN_ERR (editor->add_directory (name,
                                          dir_baton,
                                          NULL,
                                          SVN_INVALID_REVNUM, 
                                          &this_dir_baton));

          /* Recurse. */
          SVN_ERR (import_dir (editor, this_dir_baton, new_path, subpool));
        }
      else if (this_entry.filetype == APR_REG)
        {
          SVN_ERR (import_file (editor,
                                dir_baton,
                                new_path,
                                svn_string_create (this_entry.name, subpool),
                                subpool));
        }
      else
        {
          /* It's not a file or dir, so we can't import it (yet).
             No need to error, just ignore the thing. */
        }
    }

  /* Check that the loop exited cleanly. */
  if (! (APR_STATUS_IS_ENOENT (apr_err)))
    {
      return svn_error_createf
        (apr_err, 0, NULL, subpool, "error during import of `%s'", path->data);
    }
  else  /* Yes, it exited cleanly, so close the dir. */
    {
      apr_err = apr_dir_close (dir);
      if (! (APR_STATUS_IS_SUCCESS (apr_err)))
        return svn_error_createf
          (apr_err, 0, NULL, subpool, "error closing dir `%s'", path->data);
    }
      
  apr_pool_destroy (subpool);
  return SVN_NO_ERROR;
}



/*** Public interfaces. ***/

/* 
 * Note: the repository directory receiving the import was specified
 * when the editor was fetched.  (I.e, when EDITOR->replace_root() is
 * called, it returns a directory baton for that directory, which is
 * not necessarily the root.)
 */
svn_error_t *
svn_wc_import (svn_string_t *path,
               svn_string_t *new_entry,
               const svn_delta_edit_fns_t *editor,
               void *edit_baton,
               apr_pool_t *pool)
{
  void *root_baton;
  enum svn_node_kind kind;

  /* Basic sanity check. */
  if (new_entry && (strcmp (new_entry->data, "") == 0))
    return svn_error_create
      (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL, pool,
       "new entry name may not be the empty string when importing");

  /* The repository doesn't know about the reserved. */
  if (strcmp (new_entry->data, SVN_WC_ADM_DIR_NAME) == 0)
    return svn_error_createf
      (SVN_ERR_CL_ADM_DIR_RESERVED, 0, NULL, pool,
       "the name \"%s\" is reserved and cannot be imported",
       SVN_WC_ADM_DIR_NAME);

  /* Get a root dir baton. */
  SVN_ERR (editor->replace_root (edit_baton, 0, &root_baton));

  /* Import a file or a directory tree. */
  SVN_ERR (svn_io_check_path (path, &kind, pool));

  /* Note that there is no need to check whether PATH's basename is
     "SVN".  It would be strange but not illegal to import the
     contents of a directory named SVN/, because the directory's own
     name is not part of those contents.  Of course, if something
     underneath it is also named "SVN", then we'll error. */

  if (kind == svn_node_file)
    {
      svn_string_t *filename;

      if (! new_entry)
        {
          return svn_error_create
            (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
             "new entry name required when importing a file");
        }

      SVN_ERR (import_file (editor, root_baton, path, filename, pool));
    }
  else if (kind == svn_node_dir)
    {
      void *new_dir_baton = NULL;

      /* Grab a new baton, making two we'll have to close. */
      if (new_entry)
        SVN_ERR (editor->add_directory (new_entry, root_baton,
                                        NULL, SVN_INVALID_REVNUM,
                                        &new_dir_baton));

      SVN_ERR (import_dir (editor,
                           new_dir_baton ? new_dir_baton : root_baton,
                           path,
                           pool));

      /* Close one baton or two. */
      if (new_dir_baton)
        SVN_ERR (editor->close_directory (new_dir_baton));
      SVN_ERR (editor->close_directory (root_baton));
    }

  SVN_ERR (editor->close_edit (edit_baton));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
