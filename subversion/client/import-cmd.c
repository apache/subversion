/*
 * import-cmd.c -- Import a file or tree into the repository.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
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
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__import (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_string_t *message;

  svn_string_t *path;
  svn_string_t *url;
  svn_string_t *new_entry;

  const svn_delta_edit_fns_t *trace_editor;
  void *trace_edit_baton;

  /* Take our message from ARGV or a FILE */
  if (opt_state->filedata) 
    message = opt_state->filedata;
  else
    message = opt_state->message;
  
  /* Import takes up to three arguments, for example
   *
   *   $ svn import  file:///home/jrandom/repos  ./myproj  myproj
   *                 ^^^^^^^^^^^^^^^^^^^^^^^^^^  ^^^^^^^^  ^^^^^^
   *                        (repository)          (source)  (dest)
   *
   * or
   *
   *   $ svn import  file:///home/jrandom/repos/some/subdir  .  myproj
   *
   * What is the nicest behavior for merge, from the user's point of
   * view?  This is a subtle question.  Seemingly intuitive answers
   * can lead to weird situations, such never being able to create
   * non-directories in the top-level of the repository.
   *
   * For now, let's keep things simple:
   *
   * If the third arg is present, it is the name of the new entry in
   * the repository target dir (the latter may or may not be the root
   * dir).  If it is absent, then the import happens directly in the
   * repository target dir, creating however many new entries are
   * necessary.
   *
   * If the second arg is also omitted, then "." is implied.
   *
   * The first arg cannot be omitted, of course.
   *
   * ### kff todo: review above behaviors.
   */

  targets = svn_cl__args_to_target_array (os, pool);

  /* Get a repository url. */
  if (targets->nelts < 1)
    return svn_error_create
      (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL, pool,
       "repository url required when importing");
  else
    url = ((svn_string_t **) (targets->elts))[0];

  /* Get a local path. */
  if (targets->nelts < 2)
    path = svn_string_create (".", pool);
  else
    path = ((svn_string_t **) (targets->elts))[1];

  /* Optionally get the dest entry name. */
  if (targets->nelts < 3)
    {
      /* If no entry name is supplied, try to derive it from the local
         path. */
      if (svn_path_is_empty (path, svn_path_local_style))
        return svn_error_create
          (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL, pool,
           "unable to determine repository entry name from local path");
      else
        new_entry = svn_path_last_component (path,
                                             svn_path_local_style,
                                             pool);
    }
  else if (targets->nelts == 3)
    new_entry = ((svn_string_t **) (targets->elts))[2];
  else
    return svn_error_create
      (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL, pool,
       "too many arguments to import command");
  
  SVN_ERR (svn_cl__get_trace_commit_editor (&trace_editor,
                                            &trace_edit_baton,
                                            path,
                                            pool));

  SVN_ERR (svn_client_import (NULL, NULL,
                              trace_editor, trace_edit_baton,
                              path,
                              url,
                              new_entry,
                              message,
                              opt_state->xml_file,
                              opt_state->revision,
                              pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
