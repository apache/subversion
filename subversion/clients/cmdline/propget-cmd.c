/*
 * propget-cmd.c -- Display status information in current directory
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__propget (apr_getopt_t *os,
                 svn_cl__opt_state_t *opt_state,
                 apr_pool_t *pool)
{
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  svn_boolean_t is_svn_prop;
  int i;

  /* PNAME is first argument (and PNAME_UTF8 will be a UTF-8 version
     thereof) */
  SVN_ERR (svn_cl__parse_num_args (&args, os, 1, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (pname, &pname_utf8, NULL, pool));
  is_svn_prop = svn_prop_is_svn_prop (pname_utf8);
  
  /* suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      apr_hash_t *props;
      apr_hash_index_t *hi;
      svn_boolean_t print_filenames = FALSE;

      SVN_ERR (svn_client_propget (&props, pname_utf8, target,
                                   opt_state->recursive, pool));

      print_filenames = (targets->nelts > 1 || apr_hash_count (props) > 1);

      for (hi = apr_hash_first (pool, props); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *filename; 
          const svn_string_t *propval;
          const char *filename_native;

          apr_hash_this (hi, &key, NULL, &val);
          filename = key;
          propval = val;

          /* If this is a special Subversion property, it is stored as
             UTF8, so convert to the native format. */
          if (is_svn_prop)
            SVN_ERR (svn_utf_string_from_utf8 (propval, &propval, pool));

          /* ### this won't handle binary property values */
          if (print_filenames) 
            {
              SVN_ERR (svn_utf_cstring_from_utf8 (filename,
                                                  &filename_native,
                                                  pool));
              printf ("%s - %s\n", filename_native, propval->data);
            } 
          else 
            {
              printf ("%s\n", propval->data);
            }
        }
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
