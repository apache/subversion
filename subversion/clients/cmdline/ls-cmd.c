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
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "cl.h"


/*** Code. ***/


static svn_error_t *
print_dirents (const char *url,
               apr_hash_t *dirents,
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  
  printf ("%s:\n", url);

  for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      const char *utf8_entryname, *native_entryname;
      const char *native_author, *timestr;
      svn_dirent_t *dirent;
      
      apr_hash_this (hi, &key, NULL, &val);
      utf8_entryname = (const char *) key;
      dirent = (svn_dirent_t *) val;

      SVN_ERR (svn_utf_cstring_from_utf8 (&native_entryname,
                                          utf8_entryname, pool));      
      SVN_ERR (svn_utf_cstring_from_utf8 (&native_author,
                                          dirent->last_author, pool));      
      timestr =  svn_time_to_human_nts (dirent->time, pool);

      printf ("%"SVN_REVNUM_T_FMT" %s %d %ld %s %s%s\n", 
              dirent->created_rev,
              dirent->last_author,
              dirent->has_props,
              (long int) dirent->size,
              timestr,
              native_entryname,
              (dirent->kind == svn_node_dir) ? "/" : "");
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

      SVN_ERR (print_dirents (target_native, dirents, subpool));

      svn_pool_clear (subpool);
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
