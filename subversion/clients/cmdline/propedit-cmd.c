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

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propedit (apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  int i;

  /* Validate the input and get the property's name (and a UTF-8
     version of that name). */
  SVN_ERR (svn_opt_parse_num_args (&args, os, 1, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (&pname_utf8, pname, NULL, pool));

  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target (targets, pool);

  /* Decide if we're making a local mod to a versioned working copy
     prop, or making a permanent change to an unversioned repository
     revision prop.  The existence of the '-r' flag is the key. */
  if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      svn_revnum_t rev;
      const char *URL, *target;
      svn_client_auth_baton_t *auth_baton;
      svn_string_t *propval;
      const char *new_propval;

      auth_baton = svn_cl__make_auth_baton (opt_state, pool);

      /* Either we have a URL target, or an implicit wc-path ('.')
         which needs to be converted to a URL. */
      if (targets->nelts <= 0)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL, pool,
                                "No URL target available.");
      target = ((const char **) (targets->elts))[0];
      SVN_ERR (svn_cl__get_url_from_target (&URL, target, pool));  
      if (URL == NULL)
        return svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, 0, NULL,
                                pool,
                                "Either a URL or versioned item is required.");

      /* Fetch the current property. */
      SVN_ERR (svn_client_revprop_get (pname_utf8, &propval,
                                       URL, &(opt_state->start_revision),
                                       auth_baton, &rev, pool));
      if (! propval)
        propval = svn_string_create ("", pool);
      
      /* Run the editor on a temporary file in '.' which contains the
         original property value... */
      SVN_ERR (svn_cl__edit_externally (&new_propval, ".",
                                        propval->data,
                                        pool));
      
      /* ...and re-set the property's value accordingly. */
      if (new_propval)
        {
          propval->data = new_propval;
          propval->len = strlen (new_propval);
          SVN_ERR (svn_client_revprop_set (pname_utf8, propval,
                                           URL, &(opt_state->start_revision),
                                           auth_baton, &rev, pool));

          printf ("Set new value for property `%s' on revision %"
                  SVN_REVNUM_T_FMT"\n", pname, rev);
        }
      else
        {
          printf ("No changes to property `%s' on revision %"
                  SVN_REVNUM_T_FMT"\n", pname, rev);
        }
    }

  else
    {
      /* For each target, edit the property PNAME. */
      for (i = 0; i < targets->nelts; i++)
        {
          apr_hash_t *props;
          const char *target = ((const char **) (targets->elts))[i];
          svn_string_t *propval;
          const char *new_propval;
          const char *base_dir = target;
          const char *target_native;
          svn_wc_adm_access_t *adm_access;
          const svn_wc_entry_t *entry;
          
          /* Fetch the current property. */
          SVN_ERR (svn_client_propget (&props, pname_utf8, target,
                                       FALSE, pool));
          
          /* Get the property value. */
          propval = apr_hash_get (props, target, APR_HASH_KEY_STRING);
          if (! propval)
            propval = svn_string_create ("", pool);
          
          /* Split the path if it is a file path. */
          SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target,
                                          FALSE, FALSE, pool));
          SVN_ERR (svn_wc_entry (&entry, target, adm_access, FALSE, pool));
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
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
