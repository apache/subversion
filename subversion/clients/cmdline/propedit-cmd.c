/*
 * propedit-cmd.c -- Edit properties of files/dirs using $EDITOR
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
svn_cl__propedit (apr_getopt_t *os,
                  svn_cl__opt_state_t *opt_state,
                  apr_pool_t *pool)
{
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  int i;

  /* Validate the input and get the property's name (and a UTF-8
     version of that name). */
  SVN_ERR (svn_cl__parse_num_args (&args, os, 1, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (&pname_utf8, pname, NULL, pool));

  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  /* For each target, edit the property PNAME. */
  for (i = 0; i < targets->nelts; i++)
    {
      apr_hash_t *props;
      const char *target = ((const char **) (targets->elts))[i];
      svn_string_t *propval;
      const char *new_propval;
      const char *base_dir = target;
      const char *target_native;
      svn_wc_entry_t *entry;

      /* Fetch the current property. */
      SVN_ERR (svn_client_propget (&props, pname_utf8, target, FALSE, pool));

      /* Get the property value. */
      propval = apr_hash_get (props, target, APR_HASH_KEY_STRING);
      if (! propval)
        propval = svn_string_create ("", pool);

      /* Split the path if it is a file path. */
      SVN_ERR (svn_wc_entry (&entry, target, FALSE, pool));
      if (! entry)
        return svn_error_create (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL,
                                 pool, target);
      if (entry->kind == svn_node_file)
        svn_path_split_nts (target, &base_dir, NULL, pool);
      
      /* Run the editor on a temporary file which contains the
         original property value... */
      SVN_ERR (svn_cl__edit_externally (&new_propval,
                                        base_dir,
                                        propval->data,
                                        pool));

      SVN_ERR (svn_utf_cstring_from_utf8 (&target_native, target, pool));

      /* ...and re-set the property's value accordingly. */
      if (new_propval)
        {
          propval->data = new_propval;
          propval->len = strlen (new_propval);
          SVN_ERR (svn_client_propset (pname_utf8, propval, target, 
                                       FALSE, pool));
          printf ("Set new value for property `%s' on `%s'\n",
                  pname, target_native);
        }
      else
        {
          printf ("No changes to property `%s' on `%s'\n",
                  pname, target_native);
        }
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
