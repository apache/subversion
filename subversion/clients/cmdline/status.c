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
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/
#include <apr_hash.h>
#include <apr_tables.h>
#include "svn_sorts.h"
#include "svn_wc.h"
#include "svn_string.h"
#include "cl.h"



/* Fill in the first three characters of STR_STATUS with status code
   characters, based on TEXT_STATUS, PROP_STATUS, and LOCKED.
   PROP_TIME is used to determine if properties exist in the first
   place (when prop_status is 'none').  */
static void
generate_status_codes (char *str_status,
                       enum svn_wc_status_kind text_status,
                       enum svn_wc_status_kind prop_status,
                       apr_time_t prop_time,
                       svn_boolean_t locked)
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
    case svn_wc_status_absent:
      text_statuschar = '?';
      break;
    case svn_wc_status_deleted:
      text_statuschar = 'D';
      break;
    case svn_wc_status_replaced:
      text_statuschar = 'R';
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

  /* If a properties exist, show an underscore.  If not, show a
     space. */
  if (prop_time)
    prop_statuschar = '_';
  else
    prop_statuschar = ' ';

  /* Addendum:  if properties are modified, merged, or conflicted,
     show that instead. */
  switch (prop_status)
    {
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
      break;
    }
  
  sprintf (str_status, "%c%c%c", 
           text_statuschar, 
           prop_statuschar,
           locked ? 'L' : ' ');
}


/* Print a single status structure in the short format */
static void 
print_short_format (const char *path,
                    svn_wc_status_t *status)
{
  char str_status[4];

  if (! status)
    return;

  /* Create local-mod status code block. */
  generate_status_codes (str_status,
                         status->text_status,
                         status->prop_status,
                         status->entry->prop_time,
                         status->locked);

  printf ("%s   %s\n", str_status, path);
}


/* Print a single status structure in the short format */
static void 
print_long_format (const char *path,
                   svn_wc_status_t *status)
{
  char str_status[4];
  char update_char;
  svn_revnum_t local_rev;

  if (! status)
    return;

  /* Create local-mod status code block. */
  generate_status_codes (str_status,
                         status->text_status,
                         status->prop_status,
                         status->entry->prop_time,
                         status->locked);

  /* Create update indicator. */
  if ((status->repos_text_status != svn_wc_status_none)
      || (status->repos_prop_status != svn_wc_status_none))
    update_char = '*';
  else
    update_char = ' ';

  /* Get local revision number */
  if (status->entry)
    local_rev = status->entry->revision;
  else
    local_rev = SVN_INVALID_REVNUM;

  /* Print */


  /* If the item is on the repository, but not in the working copy, we
     do complex things: */
  if (status->repos_text_status == svn_wc_status_added)
    {
      if (status->repos_prop_status == svn_wc_status_added) 
        printf ("__     %c         -    %s\n", update_char, path);
      else
        printf ("_      %c         -    %s\n", update_char, path);
    }

  /* Otherwise, go ahead and show the local revision number. */
  else if (local_rev == SVN_INVALID_REVNUM)
    printf ("%s    %c      ?       %s\n", str_status, update_char, path);
  else
    printf ("%s    %c    %6ld    %s\n", str_status, update_char,
            local_rev, path);
}


/* Called by status-cmd.c */
void
svn_cl__print_status_list (apr_hash_t *statushash, 
                           svn_boolean_t detailed,
                           apr_pool_t *pool)
{
  int i;
  apr_array_header_t *statusarray;
  svn_wc_status_t *status = NULL;

  /* Convert the unordered hash to an ordered, sorted array */
  statusarray = apr_hash_sorted_keys (statushash,
                                      svn_sort_compare_items_as_paths,
                                      pool);

  /* Loop over array, printing each name/status-structure */
  for (i = 0; i < statusarray->nelts; i++)
    {
      svn_item_t *item;
      const char *path;
      
      item = (((svn_item_t **)(statusarray)->elts)[i]);
      path = item->key;
      status = item->data;

      if (detailed)
        print_long_format (path, status);
      else
        print_short_format (path, status);
    }

  /* Addendum:  if we printed in detailed format, we *might* have a
     head revision to print as well. */
  if (detailed)
    {
      /* look at the last structure we printed */
      if (status && (status->repos_rev != SVN_INVALID_REVNUM))
        printf ("Head revision: %6ld\n", status->repos_rev);
    }

}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
