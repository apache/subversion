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
#include "apr_file_info.h"


/*** Code. ***/

svn_error_t *
svn_path_get_absolute(svn_stringbuf_t **pabsolute,
                      const svn_stringbuf_t *relative,
                      apr_pool_t *pool)
{
  char * buffer;
  int apr_err = apr_filepath_merge(&buffer, NULL, relative->data,
                                   APR_FILEPATH_NOTRELATIVE
                                   | APR_FILEPATH_TRUENAME,
                                   pool);
  if (apr_err == APR_SUCCESS)
    {
      *pabsolute = svn_stringbuf_create(buffer, pool);
      svn_path_canonicalize(*pabsolute);
      return SVN_NO_ERROR;
    }
  else
    {
      return svn_error_createf(SVN_ERR_BAD_FILENAME, apr_err, NULL, pool,
                               "Couldn't determine absolute path of %s.", 
                               relative->data);
    }
}

svn_error_t *
svn_path_split_if_file(svn_stringbuf_t *path,
                       svn_stringbuf_t **pdirectory,
                       svn_stringbuf_t **pfile,
                       apr_pool_t * pool)
{
  apr_finfo_t finfo;
  apr_status_t apr_err = apr_stat(&finfo, path->data, APR_FINFO_TYPE, pool);
  if (apr_err != APR_SUCCESS)
    {
      return svn_error_createf(SVN_ERR_BAD_FILENAME, apr_err, NULL,
                              pool, "Couldn't determine if %s was a file or "
                              "directory.", path->data);
    }
  else
    {
      if (finfo.filetype == APR_DIR)
        {
          *pdirectory = path;
          *pfile = svn_stringbuf_create("", pool);
        }
      else if (finfo.filetype == APR_REG)
        {
          svn_path_split(path, pdirectory, pfile, pool);
        }
      else 
        {
          return svn_error_createf(SVN_ERR_BAD_FILENAME, 0, NULL, pool,
                                  "%s is neither a file nor a directory name.",
                                  path->data);
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_path_condense_targets (svn_stringbuf_t **pbasedir,
                           apr_array_header_t **pcondensed_targets,
                           const apr_array_header_t *targets,
                           apr_pool_t *pool)
{
  if (targets->nelts <=0)
    {
      *pbasedir = NULL;
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
    }
  else
    {
      int i, j, num_condensed = targets->nelts;
      svn_stringbuf_t *file;
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
        = apr_array_make (pool, targets->nelts, sizeof (svn_stringbuf_t*));
      
      SVN_ERR (svn_path_get_absolute (pbasedir,
                                      ((svn_stringbuf_t **) targets->elts)[0],
                                      pool));
      
      (*((svn_stringbuf_t**)apr_array_push (abs_targets))) = *pbasedir;
      
      for (i = 1; i < targets->nelts; ++i)
        {
          svn_stringbuf_t *rel = ((svn_stringbuf_t **)targets->elts)[i];
          svn_stringbuf_t *absolute;
          SVN_ERR (svn_path_get_absolute (&absolute, rel, pool));
          (*((svn_stringbuf_t **)apr_array_push (abs_targets))) = absolute;
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
                  svn_stringbuf_t *abs_targets_i;
                  svn_stringbuf_t *abs_targets_j;
                  svn_stringbuf_t *ancestor;

                  if (removed[j])
                    continue;

                  abs_targets_i = 
                    ((svn_stringbuf_t **)abs_targets->elts)[i];

                  abs_targets_j = 
                    ((svn_stringbuf_t **)abs_targets->elts)[j];

                  ancestor = svn_path_get_longest_ancestor 
                    (abs_targets_i, abs_targets_j, pool);

                  if (! ancestor)
                    continue;

                  if (svn_stringbuf_compare (ancestor, abs_targets_i))
                    {
                      removed[j] = TRUE;
                      num_condensed--;
                    }
                  else if (svn_stringbuf_compare (ancestor, abs_targets_j))
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
              svn_stringbuf_t *abs_targets_i = ((svn_stringbuf_t **)
                                             abs_targets->elts)[i];
              if ((svn_stringbuf_compare (abs_targets_i, *pbasedir))
                  && !removed[i])
                {
                  removed[i] = TRUE;
                  num_condensed--;
                }
            }
          
          /* Now create the return array, and copy the non-removed items */
          *pcondensed_targets = apr_array_make (pool, num_condensed,
                                                sizeof (svn_stringbuf_t*));
          
          for (i = 0; i < abs_targets->nelts; ++i)
            {
              char *rel_item;

              if (removed[i])
                continue;

              rel_item = ((svn_stringbuf_t**)abs_targets->elts)[i]->data;

              rel_item += (*pbasedir)->len + 1;

              (*((svn_stringbuf_t**)apr_array_push (*pcondensed_targets)))
                = svn_stringbuf_create (rel_item, pool);
            }
        }
      
      /* Finally check if pbasedir is a dir or a file. */
      if (! svn_path_split_if_file (*pbasedir, pbasedir, &file, pool))
        {
          if ((pcondensed_targets != NULL)
              && (! svn_path_is_empty (file)))
            {
              /* If there was just one target, and it was a file, then
                 return it as the sole condensed target. */
              (*((svn_stringbuf_t**)apr_array_push (*pcondensed_targets))) = file;
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
                                sizeof (svn_stringbuf_t *));

  /* Create our list of untainted paths for our "keepers" */
  rel_targets = apr_array_make (pool, targets->nelts,
                                sizeof (svn_stringbuf_t *));

  /* For each target in our list we do the following:

     1.  Calculate its absolute path (ABS_PATH).
     2.  See if any of the keepers in ABS_TARGETS is a parent of, or
         is the same path as, ABS_PATH.  If so, we ignore this
         target.  If not, however, add this target's absolute path to
         ABS_TARGETS and its original path to REL_TARGETS.
  */
  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *rel_path = ((svn_stringbuf_t **)targets->elts)[i];
      svn_stringbuf_t *abs_path;
      int j;
      svn_boolean_t keep_me;

      /* Get the absolute path for this target. */
      SVN_ERR (svn_path_get_absolute (&abs_path, rel_path, temp_pool));

      /* For each keeper in ABS_TARGETS, see if this target is the
         same as or a child of that keeper. */
      keep_me = TRUE;
      for (j = 0; j < abs_targets->nelts; j++)
        {
          svn_stringbuf_t *keeper = ((svn_stringbuf_t **)abs_targets->elts)[j];
          
          /* Quit here if we find this path already in the keepers. */
          if (svn_stringbuf_compare (keeper, abs_path))
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
          (* ((svn_stringbuf_t **) apr_array_push (abs_targets))) = abs_path;
          (* ((svn_stringbuf_t **) apr_array_push (rel_targets))) = rel_path;
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
