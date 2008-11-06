/*
 * target.c:  functions which operate on a list of targets supplied to
 *              a subversion subcommand.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"


/*** Code. ***/

svn_error_t *
svn_path_condense_targets(const char **pcommon,
                          apr_array_header_t **pcondensed_targets,
                          const apr_array_header_t *targets,
                          svn_boolean_t remove_redundancies,
                          apr_pool_t *pool)
{
  int i, j, num_condensed = targets->nelts;
  svn_boolean_t *removed;
  apr_array_header_t *abs_targets;
  int basedir_len;

  /* Early exit when there's no data to work on. */
  if (targets->nelts <= 0)
    {
      *pcommon = NULL;
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
      return SVN_NO_ERROR;
    }

  /* Get the absolute path of the first target. */
  SVN_ERR(svn_path_get_absolute(pcommon,
                                APR_ARRAY_IDX(targets, 0, const char *),
                                pool));

  /* Early exit when there's only one path to work on. */
  if (targets->nelts == 1)
    {
      if (pcondensed_targets)
        *pcondensed_targets = apr_array_make(pool, 0, sizeof(const char *));
      return SVN_NO_ERROR;
    }

  /* Copy the targets array, but with absolute paths instead of
     relative.  Also, find the pcommon argument by finding what is
     common in all of the absolute paths. NOTE: This is not as
     efficient as it could be.  The calculation of the basedir could
     be done in the loop below, which would save some calls to
     svn_path_get_longest_ancestor.  I decided to do it this way
     because I thought it would be simpler, since this way, we don't
     even do the loop if we don't need to condense the targets. */

  removed = apr_pcalloc(pool, (targets->nelts * sizeof(svn_boolean_t)));
  abs_targets = apr_array_make(pool, targets->nelts, sizeof(const char *));

  APR_ARRAY_PUSH(abs_targets, const char *) = *pcommon;

  for (i = 1; i < targets->nelts; ++i)
    {
      const char *rel = APR_ARRAY_IDX(targets, i, const char *);
      const char *absolute;
      SVN_ERR(svn_path_get_absolute(&absolute, rel, pool));
      APR_ARRAY_PUSH(abs_targets, const char *) = absolute;
      *pcommon = svn_path_get_longest_ancestor(*pcommon, absolute, pool);
    }

  if (pcondensed_targets != NULL)
    {
      if (remove_redundancies)
        {
          /* Find the common part of each pair of targets.  If
             common part is equal to one of the paths, the other
             is a child of it, and can be removed.  If a target is
             equal to *pcommon, it can also be removed. */

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

                  abs_targets_i = APR_ARRAY_IDX(abs_targets, i, const char *);
                  abs_targets_j = APR_ARRAY_IDX(abs_targets, j, const char *);

                  ancestor = svn_path_get_longest_ancestor
                    (abs_targets_i, abs_targets_j, pool);

                  if (*ancestor == '\0')
                    continue;

                  if (strcmp(ancestor, abs_targets_i) == 0)
                    {
                      removed[j] = TRUE;
                      num_condensed--;
                    }
                  else if (strcmp(ancestor, abs_targets_j) == 0)
                    {
                      removed[i] = TRUE;
                      num_condensed--;
                    }
                }
            }

          /* Second pass: when a target is the same as *pcommon,
             remove the target. */
          for (i = 0; i < abs_targets->nelts; ++i)
            {
              const char *abs_targets_i = APR_ARRAY_IDX(abs_targets, i,
                                                        const char *);

              if ((strcmp(abs_targets_i, *pcommon) == 0) && (! removed[i]))
                {
                  removed[i] = TRUE;
                  num_condensed--;
                }
            }
        }

      /* Now create the return array, and copy the non-removed items */
      basedir_len = strlen(*pcommon);
      *pcondensed_targets = apr_array_make(pool, num_condensed,
                                           sizeof(const char *));

      for (i = 0; i < abs_targets->nelts; ++i)
        {
          const char *rel_item = APR_ARRAY_IDX(abs_targets, i, const char *);

          /* Skip this if it's been removed. */
          if (removed[i])
            continue;

          /* If a common prefix was found, condensed_targets are given
             relative to that prefix.  */
          if (basedir_len > 0)
            {
              /* Only advance our pointer past a path separator if
                 REL_ITEM isn't the same as *PCOMMON.

                 If *PCOMMON is a root path, basedir_len will already
                 include the closing '/', so never advance the pointer
                 here.
                 */
              rel_item += basedir_len;
              if (rel_item[0] &&
                  ! svn_dirent_is_root(*pcommon, basedir_len))
                rel_item++;
            }

          APR_ARRAY_PUSH(*pcondensed_targets, const char *)
            = apr_pstrdup(pool, rel_item);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_path_remove_redundancies(apr_array_header_t **pcondensed_targets,
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
  temp_pool = svn_pool_create(pool);

  /* Create our list of absolute paths for our "keepers" */
  abs_targets = apr_array_make(temp_pool, targets->nelts,
                               sizeof(const char *));

  /* Create our list of untainted paths for our "keepers" */
  rel_targets = apr_array_make(pool, targets->nelts,
                               sizeof(const char *));

  /* For each target in our list we do the following:

     1.  Calculate its absolute path (ABS_PATH).
     2.  See if any of the keepers in ABS_TARGETS is a parent of, or
         is the same path as, ABS_PATH.  If so, we ignore this
         target.  If not, however, add this target's absolute path to
         ABS_TARGETS and its original path to REL_TARGETS.
  */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *rel_path = APR_ARRAY_IDX(targets, i, const char *);
      const char *abs_path;
      int j;
      svn_boolean_t keep_me;

      /* Get the absolute path for this target. */
      SVN_ERR(svn_path_get_absolute(&abs_path, rel_path, temp_pool));

      /* For each keeper in ABS_TARGETS, see if this target is the
         same as or a child of that keeper. */
      keep_me = TRUE;
      for (j = 0; j < abs_targets->nelts; j++)
        {
          const char *keeper = APR_ARRAY_IDX(abs_targets, j, const char *);

          /* Quit here if we find this path already in the keepers. */
          if (strcmp(keeper, abs_path) == 0)
            {
              keep_me = FALSE;
              break;
            }

          /* Quit here if this path is a child of one of the keepers. */
          if (svn_path_is_child(keeper, abs_path, temp_pool))
            {
              keep_me = FALSE;
              break;
            }
        }

      /* If this is a new keeper, add its absolute path to ABS_TARGETS
         and its original path to REL_TARGETS. */
      if (keep_me)
        {
          APR_ARRAY_PUSH(abs_targets, const char *) = abs_path;
          APR_ARRAY_PUSH(rel_targets, const char *) = rel_path;
        }
    }

  /* Destroy our temporary pool. */
  svn_pool_destroy(temp_pool);

  /* Make sure we return the list of untainted keeper paths. */
  *pcondensed_targets = rel_targets;

  return SVN_NO_ERROR;
}
