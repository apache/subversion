/*
 * move-cmd.c -- Subversion move command
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "cl.h"



/*** Code. ***/

svn_error_t *
svn_cl__move (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_stringbuf_t *src_path, *dst_path;

  targets = svn_cl__args_to_target_array (os, pool);

  if (targets->nelts != 2)
    {
      svn_cl__subcommand_help ("move", pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }

  src_path = ((svn_stringbuf_t **) (targets->elts))[0];
  dst_path = ((svn_stringbuf_t **) (targets->elts))[1];
  
  SVN_ERR (svn_client_move (src_path, dst_path, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
