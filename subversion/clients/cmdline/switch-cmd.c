/*
 * switch-cmd.c -- Bring work tree in sync with a different URL
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


svn_error_t *
svn_cl__switch (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  const char *target = NULL, *switch_url = NULL;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_client_auth_baton_t *auth_baton;
  const char *parent_dir, *base_tgt;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton = NULL;

  /* This command should discover (or derive) exactly two cmdline
     arguments: a local path to update ("target"), and a new url to
     switch to ("switch_url"). */
  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));
  if ((targets->nelts < 1) || (targets->nelts > 2))
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");

  /* Get the required SWITCH_URL and the optional TARGET arguments. */
  if (targets->nelts == 1)
    {
      switch_url = ((const char **) (targets->elts))[0];
      target = "";
    }
  else
    {
      switch_url = ((const char **) (targets->elts))[0];
      target = ((const char **) (targets->elts))[1];
    }

  /* Validate the switch_url */
  if (! svn_path_is_url (switch_url))
    return svn_error_createf 
      (SVN_ERR_BAD_URL, 0, NULL, pool, 
       "`%s' does not appear to be a URL", switch_url);

  /* Canonicalize the URL. */
  switch_url = svn_path_canonicalize_nts (switch_url, pool);

  /* Validate the target */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target, FALSE, FALSE,
                                  pool));
  SVN_ERR (svn_wc_entry (&entry, target, adm_access, FALSE, pool));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, 
       "`%s' does not appear to be a working copy path", target);
  
  /* Build an authentication baton to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* We want the switch to print the same letters as a regular update. */
  if (entry->kind == svn_node_file)
    SVN_ERR (svn_wc_get_actual_target (target, &parent_dir, &base_tgt, pool));
  else if (entry->kind == svn_node_dir)
    parent_dir = target;

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, FALSE, FALSE, pool);

  /* Do the 'switch' update. */
  SVN_ERR (svn_client_switch
           (auth_baton,
            target,
            switch_url,
            &(opt_state->start_revision),
            opt_state->nonrecursive ? FALSE : TRUE,
            notify_func, notify_baton,
            pool));

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
