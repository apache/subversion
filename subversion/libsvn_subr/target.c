/*
 * target.c:  functions which operate on a list of targets supplied to 
 *              a subversion subcommand.
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



/*** Includes. ***/

#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "apr_file_info.h"


/*** Code. ***/

svn_error_t *
svn_path_get_absolute(const char **pabsolute,
                      const char *relative,
                      apr_pool_t *pool)
{
  /* ### This belongs in path.c! */

  /* We call svn_path_canonicalize_nts() on the input data, rather
     than the output, so that `buffer' can be returned directly
     without const vs non-const issues. */

  char * buffer;
  apr_status_t apr_err;
  const char *path_native;

  SVN_ERR (svn_utf_cstring_from_utf8
           (svn_path_canonicalize_nts (relative, pool), &path_native, pool));

  apr_err = apr_filepath_merge(&buffer, NULL,
                               path_native,
                               (APR_FILEPATH_NOTRELATIVE
                                | APR_FILEPATH_TRUENAME),
                               pool);

  if (apr_err)
    return svn_error_createf(SVN_ERR_BAD_FILENAME, apr_err, NULL, pool,
                             "Couldn't determine absolute path of %s.", 
                             relative);

  return svn_utf_cstring_to_utf8 (buffer, pabsolute, pool);
}


svn_error_t *
svn_path_split_if_file(const char *path,
                       const char **pdirectory,
                       const char **pfile,
                       apr_pool_t *pool)
{
  /* ### This belongs in path.c! */

  apr_finfo_t finfo;
  svn_error_t *err;

  err = svn_io_stat(&finfo, path, APR_FINFO_TYPE, pool);

  if (err != SVN_NO_ERROR)
    {
      return svn_error_createf(SVN_ERR_BAD_FILENAME, 0, err, pool,
                               "Couldn't determine if %s was "
                               "a file or directory.",
                               path);
    }
  else
    {
      if (finfo.filetype == APR_DIR)
        {
          *pdirectory = path;
          *pfile = "";
        }
      else if (finfo.filetype == APR_REG)
        {
          svn_path_split_nts(path, pdirectory, pfile, pool);
        }
      else 
        {
          return svn_error_createf(SVN_ERR_BAD_FILENAME, 0, NULL, pool,
                                  "%s is neither a file nor a directory name.",
                                  path);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_path_condense_targets (const char **pbasedir,
                           apr_array_header_t **pcondensed_targets,
                           const apr_array_header_t *targets,
                           apr_pool_t *pool)
{
  if (targets->nelts <= 0)
    {
      *pbasedir = NULL;
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
    }
  else
    {
      int i, j, num_condensed = targets->nelts;
      const char *file;
      svn_boolean_t *removed
        = apr_pcalloc (pool, (targets->nelts * sizeof (svn_boolean_t)));
      
      /* Copy the targets array, but with absolute paths instead of
         relative.  Also, find the pbasedir argument by finding what is
         common in all of the absolute paths. NOTE: This is not as
         efficient as it could be The calculation of the the basedir
         could be done in the loop below, which would save some calls to
         svn_path_get_longest_ancestor.  I decided to do it this way
         because I thought it would simpler, since this way, we don't
         even do the loop if we don't need to condense the targets. */
      
      apr_array_header_t *abs_targets
        = apr_array_make (pool, targets->nelts, sizeof (const char *));
      
      SVN_ERR (svn_path_get_absolute (pbasedir,
                                      ((const char **) targets->elts)[0],
                                      pool));
      
      (*((const char **)apr_array_push (abs_targets))) = *pbasedir;
      
      for (i = 1; i < targets->nelts; ++i)
        {
          const char *rel = ((const char **)targets->elts)[i];
          const char *absolute;
          SVN_ERR (svn_path_get_absolute (&absolute, rel, pool));
          (*((const char **)apr_array_push (abs_targets))) = absolute;
          *pbasedir = svn_path_get_longest_ancestor (*pbasedir, 
                                                     absolute, 
                                                     pool);
        }
      
      /* If we need to find the targets, find the common part of each pair
         of targets.  If common part is equal to one of the paths, the other
         is a child of it, and can be removed.  If a target is equal to
         *pbasedir, it can also be removed. */
      if (pcondensed_targets != NULL)
        {
          /* First pass: when one non-removed target is a child of
             another non-removed target, remove the child. */
          for (i = 0; i < abs_targets->nelts; ++i)
            {
              if (removed[i])
                continue;

              for (j = i + 1; j < abs_targets->nelts; ++j)
                {
                  const char *abs_targets_i;
                  const char *abs_targets_j;
                  const char *ancestor;

                  if (removed[j])
                    continue;

                  abs_targets_i = 
                    ((const char **)abs_targets->elts)[i];

                  abs_targets_j = 
                    ((const char **)abs_targets->elts)[j];

                  ancestor = svn_path_get_longest_ancestor 
                    (abs_targets_i, abs_targets_j, pool);

                  if (! ancestor)
                    continue;

                  if (strcmp (ancestor, abs_targets_i) == 0)
                    {
                      removed[j] = TRUE;
                      num_condensed--;
                    }
                  else if (strcmp (ancestor, abs_targets_j) == 0)
                    {
                      removed[i] = TRUE;
                      num_condensed--;
                    }
                }
            }
          
          /* Second pass: when a target is the same as *pbasedir,
             remove the target. */
          for (i = 0; i < abs_targets->nelts; ++i)
            {
              const char *abs_targets_i
                = ((const char **) abs_targets->elts)[i];
              if ((strcmp (abs_targets_i, *pbasedir) == 0) && (! removed[i]))
                {
                  removed[i] = TRUE;
                  num_condensed--;
                }
            }
          
          /* Now create the return array, and copy the non-removed items */
          {
            int basedir_len = strlen (*pbasedir);

            *pcondensed_targets = apr_array_make (pool, num_condensed,
                                                  sizeof (const char *));
          
            for (i = 0; i < abs_targets->nelts; ++i)
              {
                const char *rel_item;
                
                if (removed[i])
                  continue;
                
                rel_item = ((const char **)abs_targets->elts)[i];
                rel_item += basedir_len + 1;
                
                (*((const char **)apr_array_push (*pcondensed_targets)))
                  = apr_pstrdup (pool, rel_item);
              }
          }
        }
      
      /* Finally check if pbasedir is a dir or a file. */
      if (! svn_path_split_if_file (*pbasedir, pbasedir, &file, pool))
        {
          if ((pcondensed_targets != NULL) && (! svn_path_is_empty_nts (file)))
            {
              /* If there was just one target, and it was a file, then
                 return it as the sole condensed target. */
              (*((const char **)apr_array_push (*pcondensed_targets))) = file;
            }
        }
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_path_remove_redundancies (apr_array_header_t **pcondensed_targets,
                              const apr_array_header_t *targets,
                              apr_pool_t *pool)
{
  apr_pool_t *temp_pool;
  apr_array_header_t *abs_targets;
  apr_array_header_t *rel_targets;
  int i;

  if ((targets->nelts <= 0) || (! pcondensed_targets))
    {
      /* No targets or no place to store our work means this function
         really has nothing to do. */
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
      return SVN_NO_ERROR;
    }

  /* Initialize our temporary pool. */
  temp_pool = svn_pool_create (pool);

  /* Create our list of absolute paths for our "keepers" */
  abs_targets = apr_array_make (temp_pool, targets->nelts, 
                                sizeof (const char *));

  /* Create our list of untainted paths for our "keepers" */
  rel_targets = apr_array_make (pool, targets->nelts,
                                sizeof (const char *));

  /* For each target in our list we do the following:

     1.  Calculate its absolute path (ABS_PATH).
     2.  See if any of the keepers in ABS_TARGETS is a parent of, or
         is the same path as, ABS_PATH.  If so, we ignore this
         target.  If not, however, add this target's absolute path to
         ABS_TARGETS and its original path to REL_TARGETS.
  */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *rel_path = ((const char **)targets->elts)[i];
      const char *abs_path;
      int j;
      svn_boolean_t keep_me;

      /* Get the absolute path for this target. */
      SVN_ERR (svn_path_get_absolute (&abs_path, rel_path, temp_pool));

      /* For each keeper in ABS_TARGETS, see if this target is the
         same as or a child of that keeper. */
      keep_me = TRUE;
      for (j = 0; j < abs_targets->nelts; j++)
        {
          const char *keeper = ((const char **)abs_targets->elts)[j];
          
          /* Quit here if we find this path already in the keepers. */
          if (strcmp (keeper, abs_path) == 0)
            {
              keep_me = FALSE;
              break;
            }
          
          /* Quit here if this path is a child of one of the keepers. */
          if (svn_path_is_child (keeper, abs_path, temp_pool))
            { 
              keep_me = FALSE;
              break;
            }
        }

      /* If this is a new keeper, add its absolute path to ABS_TARGETS
         and its original path to REL_TARGETS. */
      if (keep_me)
        {
          (* ((const char **) apr_array_push (abs_targets))) = abs_path;
          (* ((const char **) apr_array_push (rel_targets))) = rel_path;
        }
    }
  
  /* Destroy our temporary pool. */
  svn_pool_destroy (temp_pool);

  /* Make sure we return the list of untainted keeper paths. */
  *pcondensed_targets = rel_targets;
  
  return SVN_NO_ERROR;
}
      

  



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
