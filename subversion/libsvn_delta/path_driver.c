/* 
 * path_driver.c -- drive an editor across a set of paths
 * 
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#include <assert.h>
#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_delta.h"
#include "svn_pools.h"
#include "svn_path.h"


/*** Helper functions. ***/


/* Push a new directory onto the dir baton stack, and update the stack
 * pointer.
 */
static void
push_stack (apr_array_header_t *db_stack,
            int *stack_ptr,
            void *dir_baton)
{
  /* Assert that we are in a stable state. */
  assert (db_stack && dir_baton);

  /* If all our current stack space is in use, push the DB onto the
     end of the array (which will allocate more space).  Else, we will
     just re-use a previously allocated slot.  */
  if (*stack_ptr == db_stack->nelts)
    APR_ARRAY_PUSH (db_stack, void *) = dir_baton;
  else
    APR_ARRAY_IDX (db_stack, *stack_ptr, void *) = dir_baton;

  /* Increment our stack pointer and get outta here. */
  (*stack_ptr)++;
}


/* Call EDITOR's open_directory() function with the PATH and REVISION
 * arguments, and then add the resulting dir baton to the dir baton
 * stack. 
 */
static svn_error_t *
open_dir (apr_array_header_t *db_stack,
          int *stack_ptr,
          const svn_delta_editor_t *editor,
          const char *path,
          svn_revnum_t revision,
          apr_pool_t *pool)
{
  void *parent_db, *db;

  /* Assert that we are in a stable state. */
  assert (db_stack && db_stack->nelts && *stack_ptr);

  /* Get the parent dir baton. */
  parent_db = APR_ARRAY_IDX (db_stack, (*stack_ptr - 1), void *);

  /* Call the EDITOR's open_directory function to get a new directory
     baton. */
  SVN_ERR (editor->open_directory (path, parent_db, revision, pool, &db));

  /* Now add the dir baton to the stack. */
  push_stack (db_stack, stack_ptr, db);

  return SVN_NO_ERROR;
}


/* Pop a directory from the dir baton stack and update the stack
 * pointer.
 *
 * This function calls the EDITOR's close_directory() function.
 */
static svn_error_t *
pop_stack (apr_array_header_t *db_stack,
           int *stack_ptr,
           const svn_delta_editor_t *editor,
           apr_pool_t *pool)
{
  void *db;

  /* Decrement our stack pointer. */
  (*stack_ptr)--;

  /* Close the most recent directory pushed to the stack. */
  db = APR_ARRAY_IDX (db_stack, *stack_ptr, void *);
  SVN_ERR (editor->close_directory (db, pool));

  return SVN_NO_ERROR;
}


/* Count the number of path components in PATH. */
static int
count_components (const char *path)
{
  int count = 1;
  const char *instance = path;

  if ((strlen (path) == 1) && (path[0] == '/'))
    return 0;

  do
    {
      instance++;
      instance = strchr (instance, '/');
      if (instance)
        count++;
    }
  while (instance);

  return count;
}


/* qsort-ready comparison function. */
static int compare_paths (const void *a, const void *b)
{
  const char *item1 = *((const char * const *) a);
  const char *item2 = *((const char * const *) b);
  return svn_path_compare_paths (item1, item2);
}



/*** Public interfaces ***/
svn_error_t *
svn_delta_path_driver (const svn_delta_editor_t *editor,
                       void *edit_baton,
                       svn_revnum_t revision,
                       apr_array_header_t *paths,
                       svn_delta_path_driver_cb_func_t callback_func,
                       void *callback_baton,
                       apr_pool_t *pool)
{
  apr_array_header_t *db_stack = apr_array_make (pool, 4, sizeof (void *));
  const char *last_path = NULL;
  int i = 0, stack_ptr = 0;
  void *parent_db = NULL, *db = NULL;
  const char *path;

  /* Do nothing if there are no paths. */
  if (! paths->nelts)
    return SVN_NO_ERROR;

  /* Sort the paths in a depth-first directory-ish order. */
  qsort (paths->elts, paths->nelts, paths->elt_size, compare_paths);

  /* If the root of the edit is also a target path, we want to call
     the callback function to let the user open the root directory and
     do what needs to be done.  Otherwise, we'll do the open_root()
     ourselves. */
  path = APR_ARRAY_IDX (paths, 0, const char *);
  if (svn_path_is_empty (path))
    {
      SVN_ERR (callback_func (&db, NULL, callback_baton, path, pool));
      last_path = path;
      i++;
    }
  else
    {
      SVN_ERR (editor->open_root (edit_baton, revision, pool, &db));
    }
  push_stack (db_stack, &stack_ptr, db);

  /* Now, loop over the commit items, traversing the URL tree and
     driving the editor. */
  for (; i < paths->nelts; i++)
    {
      const char *pdir, *bname;
      const char *common = "";
      size_t common_len;
      
      /* Get the next path. */
      path = APR_ARRAY_IDX (paths, i, const char *);

      /*** Step A - Find the common ancestor of the last path and the
           current one.  For the first iteration, this is just the
           empty string. ***/
      if (i > 0)
        common = svn_path_get_longest_ancestor (last_path, path, pool);
      common_len = strlen (common);

      /*** Step B - Close any directories between the last path and
           the new common ancestor, if any need to be closed.
           Sometimes there is nothing to do here (like, for the first
           iteration, or when the last path was an ancestor of the
           current one). ***/
      if ((i > 0) && (strlen (last_path) > common_len))
        {
          const char *rel = last_path + (common_len ? (common_len + 1) : 0);
          int count = count_components (rel);
          while (count--)
            {
              SVN_ERR (pop_stack (db_stack, &stack_ptr, editor, pool));
            }
        }

      /*** Step C - Open any directories between the common ancestor
           and the parent of the current path. ***/
      svn_path_split (path, &pdir, &bname, pool);
      if (strlen (pdir) > common_len)
        {
          char *rel = apr_pstrdup (pool, pdir);
          char *piece = rel + common_len + 1;

          while (1)
            {
              /* Find the first separator. */
              piece = strchr (piece, '/');

              /* Temporarily replace it with a NULL terminator. */
              if (piece)
                *piece = 0;

              /* Open the subdirectory. */
              SVN_ERR (open_dir (db_stack, &stack_ptr, editor, 
                                 rel, revision, pool));
              
              /* If we temporarily replaced a '/' with a NULL,
                 un-replace it and move our piece pointer to the
                 character after the '/' we found.  If there was no
                 piece found, though, we're done.  */
              if (piece)
                {
                  *piece = '/';
                  piece++;    
                }
              else
                break;
            }
        }

      /*** Step D - Tell our caller to handle the current path. ***/
      parent_db = APR_ARRAY_IDX (db_stack, (stack_ptr - 1), void *);
      SVN_ERR (callback_func (&db, parent_db, callback_baton, path, pool));
      if (db)
        push_stack (db_stack, &stack_ptr, db);

      /*** Step E - Save our state for the next iteration.  If our
           caller opened or added PATH as a directory, that becomes
           our LAST_PATH.  Otherwise, we use PATH's parent
           directory. ***/
      if (db)
        last_path = path;
      else
        last_path = pdir;
    }

  /* Close down any remaining open directory batons. */
  while (stack_ptr)
    {
      SVN_ERR (pop_stack (db_stack, &stack_ptr, editor, pool));
    }

  return SVN_NO_ERROR;
}
