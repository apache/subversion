/*
 * feedback.c:  feedback handlers for cmdline client.
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

#include <stdio.h>

#include "svn_pools.h"
#include "cl.h"



/* When the cmd-line client sees an unversioned item during an update,
   print a question mark (`?'), just like CVS does. */
static apr_status_t 
report_unversioned_item (const char *path)
{
  printf ("?  %s\n", path);
              
  return APR_SUCCESS;
}

static apr_status_t 
report_warning (apr_status_t status, const char *warning)
{
  printf ("WARNING: %s\n", warning);

  /* Someday we can examine STATUS and decide if we should return a
     fatal error. */

  return APR_SUCCESS;
}


#if 0
/* We're not overriding the report_progress feedback vtable function
   at this time. */
static apr_status_t 
report_progress (const char *action, int percentage)
{
  return APR_SUCCESS;
}
#endif


void
svn_cl__init_feedback_vtable (apr_pool_t *top_pool)
{
  svn_pool_feedback_t *feedback_vtable =
    svn_pool_get_feedback_vtable (top_pool);

  feedback_vtable->report_unversioned_item = report_unversioned_item;
  feedback_vtable->report_warning = report_warning;
  /* we're -not- overriding report_progress;  we have no need for it
     yet. */
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
