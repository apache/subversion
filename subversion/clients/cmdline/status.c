/*
 * status.c:  the command-line's portion of the "svn status" command
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
#include <apr_hash.h>
#include <apr_tables.h>
#include "svn_sorts.h"
#include "svn_wc.h"
#include "svn_utf.h"
#include "cl.h"


/* Return the single character representation of STATUS */
static char
generate_status_code (enum svn_wc_status_kind status)
{
  switch (status)
    {
    case svn_wc_status_none:        return ' ';
    case svn_wc_status_normal:      return ' ';
    case svn_wc_status_added:       return 'A';
    case svn_wc_status_absent:      return '!';
    case svn_wc_status_deleted:     return 'D';
    case svn_wc_status_replaced:    return 'R';
    case svn_wc_status_modified:    return 'M';
    case svn_wc_status_merged:      return 'G';
    case svn_wc_status_conflicted:  return 'C';
    case svn_wc_status_obstructed:  return '~';
    case svn_wc_status_unversioned: return '?';
    default:                        return '?';
    }
}

/* Print STATUS and PATH in a format determined by DETAILED and
   SHOW_LAST_COMMITTED */
static void 
print_status (const char *path,
              svn_boolean_t detailed,
              svn_boolean_t show_last_committed,
              svn_wc_status_t *status)
{
  char ood_status;
  char working_rev_buf[21]; /* Enough for 2^64 in base 10 plus '\0' */
  char commit_rev_buf[21];
  const char *working_rev = working_rev_buf;
  const char *commit_rev = commit_rev_buf;
  const char *commit_author = NULL; /* Silence a gcc unitialised warning */

  if (detailed)
    {
      if (! status->entry)
        working_rev = "      ";
      else if (! SVN_IS_VALID_REVNUM (status->entry->revision))
        working_rev = "  ?   ";  /* ### Why the odd alignment? */
      else if (status->copied)
        working_rev = "     -";
      else
        sprintf (working_rev_buf, "%6" SVN_REVNUM_T_FMT,
                 status->entry->revision);

      if (status->repos_text_status != svn_wc_status_none
          || status->repos_prop_status != svn_wc_status_none)
        ood_status = '*';
      else
        ood_status = ' ';

      if (show_last_committed)
        {
          if (status->entry && SVN_IS_VALID_REVNUM (status->entry->cmt_rev))
            sprintf(commit_rev_buf, "%6" SVN_REVNUM_T_FMT,
                    status->entry->cmt_rev);
          else if (status->entry)
            commit_rev = "    ? ";
          else
            commit_rev = "      ";

          if (status->entry && status->entry->cmt_author)
            commit_author = status->entry->cmt_author;
          else if (status->entry)
            commit_author = "      ? ";
          else
            commit_author = "        ";
        }
    }

  if (detailed && show_last_committed)
    printf ("%c%c%c%c   %c   %6s   %6s   %8s   %s\n",
            generate_status_code (status->text_status),
            generate_status_code (status->prop_status),
            status->locked ? 'L' : ' ',
            status->copied ? '+' : ' ',
            ood_status,
            working_rev,
            commit_rev,
            commit_author,
            path);

  else if (detailed)
    printf ("%c%c%c%c   %c   %6s   %s\n",
            generate_status_code (status->text_status),
            generate_status_code (status->prop_status),
            status->locked ? 'L' : ' ',
            status->copied ? '+' : ' ',
            ood_status,
            working_rev,
            path);

  else
    printf ("%c%c%c%c   %s\n",
            generate_status_code (status->text_status),
            generate_status_code (status->prop_status),
            status->locked ? 'L' : ' ',
            status->copied ? '+' : ' ',
            path);
}

/* Called by status-cmd.c */
void
svn_cl__print_status_list (apr_hash_t *statushash,
                           svn_revnum_t youngest,
                           svn_boolean_t detailed,
                           svn_boolean_t show_last_committed,
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
      const svn_item_t *item;
      const char *path;
      svn_error_t *err;

      item = &APR_ARRAY_IDX(statusarray, i, const svn_item_t);
      status = item->value;

      if (! status || (skip_unrecognized && ! status->entry))
        continue;

      err = svn_utf_cstring_from_utf8 (&path, item->key, pool);
      if (err)
        svn_handle_error (err, stderr, FALSE);

      /* Always print some path */
      if (path[0] == '\0')
        path = ".";

      print_status (path, detailed, show_last_committed, status);
    }

  /* If printing in detailed format, we might have a head revision to
     print as well. */
  if (detailed && (youngest != SVN_INVALID_REVNUM))
    printf ("Head revision: %6" SVN_REVNUM_T_FMT "\n", youngest);
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
