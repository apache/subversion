/*
 * status.c:  the command-line's portion of the "svn status" command
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
#include <apr_hash.h>
#include <apr_tables.h>
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_string.h"
#include "cl.h"



/* Edit the three-byte string STR_STATUS, based on the contents of
   TEXT_STATUS and PROP_STATUS. 
   
   If VERBOSE is set, STR_STATUS will show the status of both text and
   prop. 

   If VERBOSE is not set, STR_STATUS will contain only one simplified
   status code, based on a logical combination of the two.  */
static void
generate_status_codes (char *str_status,
                       enum svn_wc_status_kind text_status,
                       enum svn_wc_status_kind prop_status)
{
  char text_statuschar, prop_statuschar;

  switch (text_status)
    {
    case svn_wc_status_none:
      text_statuschar = '_';
      break;
    case svn_wc_status_added:
      text_statuschar = 'A';
      break;
    case svn_wc_status_deleted:
      text_statuschar = 'D';
      break;
    case svn_wc_status_modified:
      text_statuschar = 'M';
      break;
    case svn_wc_status_merged:
      text_statuschar = 'G';
      break;
    case svn_wc_status_conflicted:
      text_statuschar = 'C';
      break;
    default:
      text_statuschar = '?';
      break;
    }

  /* Properties stay `invisible' unless they're locally modified,
     merged or conflicted. */
  switch (prop_status)
    {
    case svn_wc_status_none:
      prop_statuschar = ' ';
      break;
    case svn_wc_status_added:
      prop_statuschar = ' ';
      break;
    case svn_wc_status_deleted:
      prop_statuschar = ' ';
      break;
    case svn_wc_status_modified:
      prop_statuschar = 'M';
      break;
    case svn_wc_status_merged:
      prop_statuschar = 'G';
      break;
    case svn_wc_status_conflicted:
      prop_statuschar = 'C';
      break;
    default:
      prop_statuschar = '?';
      break;
    }
  
  sprintf (str_status, "%c%c", text_statuschar, prop_statuschar);
}

void
svn_cl__print_status (svn_string_t *path, svn_wc_status_t *status)
{
  svn_revnum_t entry_rev;
  char str_status[3];

  /* Create either a one or two character status code */
  generate_status_codes (str_status,
                         status->text_status,
                         status->prop_status);
  
  /* Grab the entry revision once, safely. */
  if (status->entry)
    entry_rev = status->entry->revision;
  else
    entry_rev = SVN_INVALID_REVNUM;

  /* Use it. */
  if ((entry_rev == SVN_INVALID_REVNUM)
      && (status->repos_rev == SVN_INVALID_REVNUM))
    printf ("%s  none     ( none )   %s\n",
            str_status, path->data);
  else if (entry_rev == SVN_INVALID_REVNUM)
    printf ("%s  none     (%6ld)   %s\n",
            str_status, status->repos_rev, path->data);
  else if (status->repos_rev == SVN_INVALID_REVNUM)
    printf ("%s  %-6ld  ( none )  %s\n",
            str_status, entry_rev, path->data);
  else
    printf ("%s  %-6ld  (%6ld)  %s\n",
            str_status, entry_rev, status->repos_rev, path->data);
}


void
svn_cl__print_status_list (apr_hash_t *statushash, apr_pool_t *pool)
{
  int i;
  apr_array_header_t *statusarray;

  /* Convert the unordered hash to an ordered, sorted array */
  statusarray = apr_hash_sorted_keys (statushash,
                                      svn_sort_compare_as_paths,
                                      pool);

  /* Loop over array, printing each name/status-structure */
  for (i = 0; i < statusarray->nelts; i++)
    {
      svn_item_t *item;
      const char *path;
      svn_wc_status_t *status;
      
      item = (((svn_item_t **)(statusarray)->elts)[i]);
      path = (const char *) item->key;
      status = (svn_wc_status_t *) item->data;

      svn_cl__print_status (svn_string_create (path, pool), status);
    }
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
