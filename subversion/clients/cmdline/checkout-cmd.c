/*
 * checkout-cmd.c -- Subversion checkout command
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
#include "cl.h"


/*** Code. ***/

/* FITZ's WISHLIST : A WORD ABOUT CHECKOUT

   This only works for one repo checkout at a shot.  In CVS, when we
   checkout one project and give it a destination directory, it dumps
   it in the directory. If you check out more than one, it dumps each
   project into its own directory *inside* the one that you specified
   with the -d flag. So, for example, we have project A:

       A/one_mississippi.txt
       A/two_mississippi.txt
       A/three_mississippi.txt
     
   And project B:

       B/cat
       B/dog
       B/pig

   If I do 'cvs -d :pserver:fitz@subversion.tigris.org:/cvs co -d foo A', 
   I get the following:

       foo/one_mississippi.txt
       foo/two_mississippi.txt
       foo/three_mississippi.txt

   But if I do this 'cvs -d :pserver:fitz@subversion.tigris.org:/cvs
   co -d foo A B', I get the following:

       foo/A/one_mississippi.txt
       foo/A/two_mississippi.txt
       foo/A/three_mississippi.txt
       foo/B/cat
       foo/B/dog
       foo/B/pig
      
  Makes sense, right? Right. Note that we have no provision for this
  right now and we need to support it. My vote is that we stop
  iterating over targets here and just pass the args into
  svn_client_checkout and let it decide what to do based on
  (args->nelts == 1) or (args->nelts > 1). -Fitz */


svn_error_t *
svn_cl__checkout (apr_getopt_t *os,
                  svn_cl__opt_state_t *opt_state,
                  apr_pool_t *pool)
{
  svn_client_auth_baton_t *auth_baton;
  apr_array_header_t *targets;
  const char *local_dir;
  const char *repos_url;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton = NULL;

  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));

  /* If there are no targets at all, then let's just give the user a
     friendly help message, rather than silently exiting.  */
  if ((targets->nelts < 1) || (targets->nelts > 2))
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool,
                             "" /* message is unused */);
  
  /* Get the required REPOS_URL, and the optional LOCAL_DIR arguments. */
  if (targets->nelts == 1)
    {
      repos_url = ((const char **) (targets->elts))[0];
      local_dir = svn_path_basename (repos_url, pool);
    }
  else
    {
      repos_url = ((const char **) (targets->elts))[0];
      local_dir = ((const char **) (targets->elts))[1];
      if (svn_path_is_url (local_dir) && (! opt_state->force))
        return svn_error_create 
          (SVN_ERR_WC_NOT_DIRECTORY, 0, 0, pool,
           "Checkout directory appears to be a URL.  If this what you "
           "really wanted to do, please use the --force flag.");
    }

  /* Validate the REPOS_URL */
  if (! svn_path_is_url (repos_url))
    return svn_error_createf 
      (SVN_ERR_BAD_URL, 0, NULL, pool, 
       "`%s' does not appear to be a URL", repos_url);

  /* Canonicalize the URL. */
  repos_url = svn_path_canonicalize_nts (repos_url, pool);

  /* Put commandline auth info into a baton for libsvn_client.  */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, TRUE, FALSE, pool); 

  SVN_ERR (svn_client_checkout (notify_func,
                                notify_baton,
                                auth_baton,
                                repos_url,
                                local_dir,
                                &(opt_state->start_revision),
                                opt_state->nonrecursive ? FALSE : TRUE,
                                opt_state->xml_file,
                                pool));
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
