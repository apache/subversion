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


/* Fill in the first four characters of STR_STATUS with status code
   characters, based on TEXT_STATUS, PROP_STATUS, LOCKED, and COPIED. */
static void
generate_status_codes (char *str_status,
                       enum svn_wc_status_kind text_status,
                       enum svn_wc_status_kind prop_status,
                       svn_boolean_t locked,
                       svn_boolean_t copied)
{
  char text_statuschar, prop_statuschar;

  switch (text_status)
    {
    case svn_wc_status_none:
      text_statuschar = ' ';
      break;
    case svn_wc_status_normal:
      text_statuschar = '_';
      break;
    case svn_wc_status_added:
      text_statuschar = 'A';
      break;
    case svn_wc_status_absent:
      text_statuschar = '!';
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
    case svn_wc_status_unversioned:
    default:
      text_statuschar = '?';
      break;
    }

  switch (prop_status)
    {
    case svn_wc_status_none:
      prop_statuschar = ' ';
      break;
    case svn_wc_status_normal:
      prop_statuschar = '_';
      break;
    case svn_wc_status_added:
      prop_statuschar = 'A';
      break;
    case svn_wc_status_absent:
      prop_statuschar = '!';
      break;
    case svn_wc_status_deleted:
      prop_statuschar = 'D';
      break;
    case svn_wc_status_replaced:
      prop_statuschar = 'R';
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
    case svn_wc_status_unversioned:
    default:
      prop_statuschar = '?';
      break;
    }

  sprintf (str_status, "%c%c%c%c", 
           text_statuschar, 
           prop_statuschar,
           locked ? 'L' : ' ',
           copied ? '+' : ' ');
}


/* Print a single status structure in the short format */
static void 
print_short_format (const char *path,
                    svn_wc_status_t *status)
{
  char str_status[5];

  if (! status)
    return;

  /* Create local-mod status code block. */
  generate_status_codes (str_status,
                         status->text_status,
                         status->prop_status,
                         status->locked,
                         status->copied);

  printf ("%s   %s\n", str_status, path);
}


/* Print a single status structure in the long format */
static void 
print_long_format (const char *path,
                   svn_wc_status_t *status)
{
  char str_status[5];
  char str_rev[7];
  char update_char;
  svn_revnum_t local_rev;

  if (! status)
    return;

  /* Create local-mod status code block. */
  generate_status_codes (str_status,
                         status->text_status,
                         status->prop_status,
                         status->locked,
                         status->copied);

  /* Get local revision number */
  if (status->entry)
    local_rev = status->entry->revision;
  else
    local_rev = SVN_INVALID_REVNUM;

  /* Set the update character. */
  update_char = ' ';
  if ((status->repos_text_status != svn_wc_status_none)
      || (status->repos_prop_status != svn_wc_status_none))
    update_char = '*';

  /* Determine the appropriate local revision string. */
  if (! status->entry)
    strcpy (str_rev, "      ");
  else if (local_rev == SVN_INVALID_REVNUM)
    strcpy (str_rev, "  ?   ");
  else if (status->copied)
    strcpy (str_rev, "     -");
  else
    sprintf (str_rev, "%6ld", local_rev);

  /* One Printf to rule them all, one Printf to bind them..." */
  printf ("%s   %c    %s    %s\n", str_status, update_char, str_rev, path);
}



/* Print a single status structure in the long format */
static void 
print_really_long_format (const char *path,
                          svn_wc_status_t *status)
{
  char str_status[5];
  char str_rev[7], str_cr[7], last_author[9];
  char update_char;
  svn_revnum_t local_rev;
  svn_revnum_t last_changed_rev;

  if (! status)
    return;

  /* Create local-mod status code block. */
  generate_status_codes (str_status,
                         status->text_status,
                         status->prop_status,
                         status->locked,
                         status->copied);

  /* Get local revision number */
  if (status->entry)
    local_rev = status->entry->revision;
  else
    local_rev = SVN_INVALID_REVNUM;

  /* Try to get the CR, if it's in the entry. */
  last_changed_rev = SVN_INVALID_REVNUM;
  if (status->entry)
    {
      svn_stringbuf_t *revstr = apr_hash_get (status->entry->attributes,
                                              SVN_ENTRY_ATTR_COMMITTED_REV,
                                              APR_HASH_KEY_STRING);
      if (revstr)
        last_changed_rev = (svn_revnum_t) atoi (revstr->data);
    }

  /* Try to get the last author, if it's in the entry. */
  strcpy (last_author, "      ? ");
  if (status->entry)
    {
      svn_stringbuf_t *s_author = apr_hash_get (status->entry->attributes,
                                                SVN_ENTRY_ATTR_LAST_AUTHOR,
                                                APR_HASH_KEY_STRING);
      /* Truncate author's name to 8 characters, so output is
         fixed-width and thus more readable. */
      if (s_author)
        strncpy (last_author, s_author->data, 8);
    }

  /* Set the update character. */
  update_char = ' ';
  if ((status->repos_text_status != svn_wc_status_none)
      || (status->repos_prop_status != svn_wc_status_none))
    update_char = '*';

  /* Determine the appropriate local revision string. */
  if (! status->entry)
    strcpy (str_rev, "      ");
  else if (local_rev == SVN_INVALID_REVNUM)
    strcpy (str_rev, "  ?   ");
  else if (status->copied)
    strcpy (str_rev, "     -");
  else
    sprintf (str_rev, "%6ld", local_rev);

  /* Determine the appropriate CR string. */
  if (! status->entry)
    strcpy (str_cr, "      ");
  else if (last_changed_rev == SVN_INVALID_REVNUM)
    strcpy (str_cr, "    ? ");
  else if (status->copied)
    strcpy (str_cr, "     -");
  else
    sprintf (str_cr, "%6ld", last_changed_rev);

  /* One Printf to rule them all, one Printf to bind them..." */
  printf ("%s   %c    %s    [%s : %8s]    %s\n", 
          str_status, update_char, str_rev, str_cr, last_author, path);
}




/* Called by status-cmd.c */
void
svn_cl__print_status_list (apr_hash_t *statushash, 
                           svn_revnum_t youngest,
                           svn_boolean_t detailed,
                           svn_boolean_t very_detailed,
                           svn_boolean_t skip_unrecognized,
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

      if ((skip_unrecognized) && (! status->entry))
        continue;

      if (very_detailed)
        print_really_long_format (path, status);
      else if (detailed)
        print_long_format (path, status);
      else
        print_short_format (path, status);
    }

  /* If printing in detailed format, we might have a head revision to
     print as well. */
  if ((detailed || very_detailed) && (youngest != SVN_INVALID_REVNUM))
    printf ("Head revision: %6ld\n", youngest);
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
