/*
 * target.c:  functions which operate on a list of targets supplied to 
 *              a subversion subcommand.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"


/*** Code. ***/


static svn_boolean_t
redundancy_check (const char *ancestor,
                  const char *path1,
                  const char *path2,
                  svn_depth_t depth,
                  apr_pool_t *pool)
{
  int path1_len;
  svn_node_kind_t kind;
  svn_error_t *err;

  /* For depth zero, there is no redundancy check. */
  if (depth == svn_depth_zero)
    return FALSE;

  /* See if PATH1 is an ancestor of PATH2 */
  if (strcmp (ancestor, path1) != 0)
    return FALSE;

  /* For Depth Infinity, it's enough just to know that PATH1 is an
     ancestor of PATH2. */
  if (depth == svn_depth_infinity)
    return TRUE;
  
  /* For Depth 1, we only care if PATH2 is a file immediately inside
     PATH1.  So if the difference of PATH2 and PATH1 contains a
     directory separator, we know PATH2 is not an immediate child of
     PATH1. */
  path1_len = strlen (path1);
  if (strchr (path2 + path1_len + 1, '/'))
    return FALSE;

  /* We now know that PATH2 is an immediate child of PATH1.  All that
     remains is to see if PATH2 is a file or not.  If we can't answer
     that, we'll go the safe route and assume the path is not
     redundant.  */
  if (! svn_path_is_url (path2))
    {
      err = svn_io_check_path (path2, &kind, pool);
      if ((err == SVN_NO_ERROR) && (kind == svn_node_file))
        return TRUE;
      if (err)
        svn_error_clear (err);
    }

  return FALSE;
}

svn_error_t *
svn_path_condense_targets (const char **pbasedir,
                           apr_array_header_t **pcondensed_targets,
                           const apr_array_header_t *targets,
                           svn_depth_t depth,
                           apr_pool_t *pool)
{
  int i, j, count = targets->nelts;
  apr_array_header_t *abs_targets;

  /* The game plan here is to handle path condensation in light of the
     DEPTH parameter, optionally removing duplicate paths.  For depth
     0, this means that, duplicates aside, there will be a one-to-one
     mapping of input targets to output targets.  For depth 1, the
     output targets will be reduced by any file targets whose parent
     directory is also listed as a target.  For depth infinity, the
     output targets will be reduced by any targets which are children
     of another target. */

  /* Early exit for the trivial (target-less) case. */
  if (targets->nelts <= 0)
    {
      *pbasedir = NULL;
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
      return SVN_NO_ERROR;
    }

  /* Let's start off with an absolute path of our first (perhaps only)
     target. */
  SVN_ERR (svn_path_get_absolute (pbasedir, 
                                  APR_ARRAY_IDX (targets, 0, const char *),
                                  pool));

  /* Copy the targets array, but with absolute paths instead of
     relative.  Also, find the pbasedir argument by finding what is
     common in all of the absolute paths. NOTE: This is not as
     efficient as it could be.  The calculation of the basedir could
     be done in the loop below, which would save some calls to
     svn_path_get_longest_ancestor.  I decided to do it this way
     because I thought it would simpler, since this way, we don't even
     do the loop if we don't need to condense the targets. */
      
  abs_targets = apr_array_make (pool, targets->nelts, sizeof (const char *));
  APR_ARRAY_PUSH (abs_targets, const char *) = *pbasedir;
      
  for (i = 1; i < targets->nelts; ++i)
    {
      const char *rel = APR_ARRAY_IDX (targets, i, const char *);
      const char *absolute;
      SVN_ERR (svn_path_get_absolute (&absolute, rel, pool));
      APR_ARRAY_PUSH (abs_targets, const char *) = absolute;
      *pbasedir = svn_path_get_longest_ancestor (*pbasedir, absolute, pool);
    }

  if (pcondensed_targets != NULL)
    {
      int basedir_len;
      apr_array_header_t *cond;
      svn_boolean_t *removed = 
        apr_pcalloc (pool, (targets->nelts * sizeof (svn_boolean_t)));

      /*** Step 1:  Condense the targets based on the DEPTH parameter. ***/

      for (i = 0; i < abs_targets->nelts; ++i)
        {
          if (removed[i])
            continue;

          for (j = i + 1; j < abs_targets->nelts; ++j)
            {
              const char *abs_i = 
                APR_ARRAY_IDX (abs_targets, i, const char *);
              const char *abs_j =
                APR_ARRAY_IDX (abs_targets, j, const char *);

              if (removed[j])
                continue;

              /* For Depths 1 and Infinity, we will be removing
                 targets that are redundant based on their
                 relationship as children of other targets. */
              if (depth != svn_depth_zero)
                {
                  const char *ancestor =
                    svn_path_get_longest_ancestor (abs_i, abs_j, pool);

                  if (*ancestor == '\0')
                    continue;
                  
                  if (redundancy_check (ancestor, abs_i, abs_j, 
                                        depth, pool))
                    {
                      removed[j] = TRUE;
                      count--;
                    }
                  else if (redundancy_check (ancestor, abs_j, abs_i, 
                                             depth, pool))
                    {
                      removed[i] = TRUE;
                      count--;
                    }
                }

              /* Regardless of the DEPTH, remove duplicates. */
              if (strcmp (abs_i, abs_j) == 0)
                {
                  removed[j] = TRUE;
                  count--;
                }
            }
        }

      /*** Step 2: Now create the return array, and copy the
           non-removed items */

      basedir_len = strlen (*pbasedir);
      cond = apr_array_make (pool, count, sizeof (const char *));
      for (i = 0; i < abs_targets->nelts; ++i)
        {
          const char *item = APR_ARRAY_IDX (abs_targets, i, const char *) +
                             (basedir_len ? basedir_len + 1 : 0);

          if (removed[i])
            continue;
          
          APR_ARRAY_PUSH (cond, const char *) = apr_pstrdup (pool, item);
        }
      *pcondensed_targets = cond;
    }

  /* Finally check if pbasedir is a dir or a file (or a URL). */
  if (! svn_path_is_url (*pbasedir))
    {
      const char *file;

      /* The only way our basedir could be a file is if there was
         (after duplicates removal) only a single file path passed to
         this function. */
      SVN_ERR (svn_path_split_if_file (*pbasedir, pbasedir, &file, pool));
      
      /* If there was just one target, and it was a file, then
         return it as the sole condensed target. */
      if ((pcondensed_targets != NULL) && (! svn_path_is_empty (file)))
        {
          *pcondensed_targets = apr_array_make (pool, 1, sizeof (file));
          APR_ARRAY_PUSH (*pcondensed_targets, const char *) = file;
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
