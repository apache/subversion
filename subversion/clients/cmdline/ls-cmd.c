/*
 * ls-cmd.c -- list a URL
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

#include "svn_client.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "cl.h"


/*** Code. ***/

static int
compare_items_as_paths (const svn_item_t *a, const svn_item_t *b)
{
  return svn_path_compare_paths_nts ((char *)a->key, (char *)b->key);
}

static svn_error_t *
print_dirents (const char *url,
               apr_hash_t *dirents,
               svn_boolean_t verbose,
               apr_pool_t *pool)
{
  apr_array_header_t *array;
  int i;

  array = apr_hash_sorted_keys (dirents, compare_items_as_paths, pool);
  
  printf ("%s:\n", url);

  for (i = 0; i < array->nelts; ++i)
    {
      const char *utf8_entryname, *native_entryname;
      svn_dirent_t *dirent;
      svn_item_t *item;
      char timestr[20];
     
      item = &APR_ARRAY_IDX (array, i, svn_item_t);

      utf8_entryname = item->key;

      dirent = apr_hash_get (dirents, utf8_entryname, item->klen);

      SVN_ERR (svn_utf_cstring_from_utf8 (&native_entryname,
                                          utf8_entryname, pool));      
      if (verbose)
        {
          const char *native_author;

          SVN_ERR (svn_utf_cstring_from_utf8 (&native_author,
                                              dirent->last_author, pool));

          {
            /* svn_time_to_human_nts gives us something *way* to long to use 
               for this, so we have to roll our own. */
            apr_time_exp_t exp_time;
            apr_status_t apr_err;
            apr_size_t size;

            apr_time_exp_lt (&exp_time, dirent->time);

            apr_err = apr_strftime (timestr, &size, sizeof (timestr),
                                    "%b %d %H:%m", &exp_time);

            /* if that failed, just zero out the string and print nothing */
            if (apr_err)
              timestr[0] = '\0';
          }

          printf ("%c %7"SVN_REVNUM_T_FMT" %8.8s %8ld %12s %s%s\n", 
                  dirent->has_props ? 'P' : '_',
                  dirent->created_rev,
                  native_author ? native_author : "      ? ",
                  (long int) dirent->size,
                  timestr,
                  native_entryname,
                  (dirent->kind == svn_node_dir) ? "/" : "");
        }
      else
        {
          printf ("%s%s\n", native_entryname, 
                  (dirent->kind == svn_node_dir) ? "/" : "");
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__ls (apr_getopt_t *os,
            svn_cl__opt_state_t *opt_state,
            apr_pool_t *pool)
{
  apr_array_header_t *targets;
  int i;
  svn_client_auth_baton_t *auth_baton;
  apr_pool_t *subpool = svn_pool_create (pool);

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));

  /* Give me arguments or give me death! */
  if (targets->nelts == 0)
    return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS,
                             0, NULL, pool, "");

  /* For each target, try to list it. */
  for (i = 0; i < targets->nelts; i++)
    {
      apr_hash_t *dirents;
      const char *target_native;
      const char *target = ((const char **) (targets->elts))[i];
      SVN_ERR (svn_utf_cstring_from_utf8 (&target_native, target, subpool));
     
      if (! svn_path_is_url (target))
        {
          printf ("Invalid URL: %s\n", target_native);
          continue;
        }
      
      SVN_ERR (svn_client_ls (&dirents, target, &(opt_state->start_revision),
                              auth_baton, subpool));

      SVN_ERR (print_dirents (target_native, dirents, opt_state->verbose, 
                              subpool));

      svn_pool_clear (subpool);
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
