/*
 * diff.c:  the command-line's portion of the "svn diff" command
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
#define APR_WANT_STRFUNC  /* for strcmp */
#include <apr_want.h>

#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>

#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_private_config.h"         /* for SVN_CLIENT_DIFF */

#include "cl.h"

/* todo: correctly handle entries which are being added or removed. */

svn_error_t *
svn_cl__print_dir_diff (svn_stringbuf_t *path,
                        apr_array_header_t *options,
                        svn_boolean_t recurse,
                        apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_error_t *err = SVN_NO_ERROR;
  svn_boolean_t iswc;

  /* Check that this is a real working copy. */
  SVN_ERR (svn_wc_check_wc (path, &iswc, pool));

  if (!iswc)
    return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, 0, NULL, pool,
                              "Directory `%s' is not under version control.",
                              path->data);

  SVN_ERR (svn_wc_entries_read (&entries, path, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      svn_wc_entry_t *ent;
      const char *key;

      apr_hash_this(hi, (const void **)&key, NULL, (void **)&ent);

      /* skip entry for the directory itself */
      if (strcmp (key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* Construct the full path for this entry: the original 'path'
       * is restored again after we're done with this entry. */
      svn_path_add_component_nts (path, key, svn_path_local_style);

      switch (ent->kind)
        {
        case svn_node_file:
          err = svn_cl__print_file_diff (path, options, pool);
          break;
        case svn_node_dir:
          if (recurse)
            {
              err = svn_cl__print_dir_diff (path, options, recurse, pool);
            }
          break;
        default:
          break;
        }
      
      svn_path_remove_component (path, svn_path_local_style);

      if (err) return err;

    }
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__print_file_diff (svn_stringbuf_t *path,
                         apr_array_header_t *options,
                         apr_pool_t *pool)
{
  apr_status_t status;
  svn_stringbuf_t *pristine_copy_path;
  svn_boolean_t text_is_modified = FALSE;
  const char **args;
  int i = 0;

  apr_file_t *outhandle = NULL;

  /* We already have a path to the working version of our file, that's
     PATH. */

  /* We don't want to run the external diff process unless we can
     intelligently determine this will be worth our effort.  In other
     words, if we *know* that the text hasn't been modified, the
     external display of those (non-existant) changes is a NOOP. */
  SVN_ERR (svn_wc_text_modified_p (&text_is_modified, path, pool));
  if (!text_is_modified) 
    return SVN_NO_ERROR;

  /* Get a PRISTINE_COPY_PATH to compare against.  */
  SVN_ERR (svn_client_file_diff (path, &pristine_copy_path, pool));

  /* Get an apr_file_t representing stdout, which is where we'll have
     the diff program print to. */
  status = apr_file_open_stdout (&outhandle, pool);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "error: can't open handle to stdout");

  /* Execute local diff command on these two paths, print to stdout. */

  if (options->nelts == 0)
    args = apr_palloc(pool, 5*sizeof(char*));
  else 
    args = apr_palloc(pool, (options->nelts + 4)*sizeof(char*));

  args[i++] = SVN_CLIENT_DIFF;  /* the autoconfiscated system diff program */

  if (options->nelts == 0)
    args[i++] = "-u";
  else
    {
      for (; i <= options->nelts; i++)
        args[i] = ((svn_stringbuf_t **) (options->elts))[i-1]->data;
    }
  args[i++] = pristine_copy_path->data;
  args[i++] = path->data;
  args[i++] = NULL;

  /* todo: This printf is NOT "my final answer" -- placeholder for
     real work to be done. */ 
  apr_file_printf (outhandle, "Index: %s\n", path->data);
  apr_file_printf (outhandle, "===================================================================\n");

  SVN_ERR(svn_io_run_cmd (".", SVN_CLIENT_DIFF, args, NULL, NULL,
                        NULL, outhandle, NULL, pool));

  /* TODO: Handle exit code == 2 (i.e. errors with diff) here */
  
  /* TODO:  someday we'll need to worry about two things here:

     1.  svn_client_file_diff may be returning a file from RA instead
     of the WC's text-base.  If this is so, it will need to provide a
     "clean up" routine to remove the temporary file created by RA.

     2.  we're going to need to write a diff plug-in mechanism that
     makes use of the two paths, instead of just blindly running
     SVN_CLIENT_DIFF. 
  */

  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 * vim:ts=4:sw=4:expandtab:tw=80:fo=tcroq 
 * vim:isk=a-z,A-Z,48-57,_,.,-,> 
 * vim:cino=>1s,e0,n0,f0,{.5s,}0,^-.5s,=.5s,t0,+1s,c3,(0,u0,\:0 
 */



