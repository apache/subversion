/*
 * info-cmd.c -- Display information about a resource
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
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "cl.h"


/*** Code. ***/

static void
svn_cl__info_print_time (apr_time_t atime, const char * desc)
{
  apr_time_exp_t extime;
  apr_status_t apr_err;

  /* if this returns an error, just don't print anything out */
  apr_err = apr_time_exp_tz (&extime, atime, 0);
  if (! apr_err)
    printf ("%s: %04lu-%02lu-%02lu %02lu:%02lu GMT\n", desc,
            (unsigned long)(extime.tm_year + 1900),
            (unsigned long)(extime.tm_mon + 1),
            (unsigned long)(extime.tm_mday),
            (unsigned long)(extime.tm_hour),
            (unsigned long)(extime.tm_min));
}

svn_error_t *
svn_cl__info (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  apr_array_header_t *targets;
  int i;

  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);

  /* Add "." if user passed 0 arguments. */
  svn_cl__push_implicit_dot_target (targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];
      svn_wc_entry_t *entry;
      svn_boolean_t text_conflict = FALSE, props_conflict = FALSE;

      printf ("Path: %s\n", target->data);

      SVN_ERR (svn_wc_entry (&entry, target, pool));
      if (! entry)
        {
          /* Print non-versioned message and extra newline separator. */
          printf ("(Not a versioned resource)\n\n");
          continue;
        }

      /* Note: we have to be paranoid about checking that these are
         valid, since svn_wc_entry() doesn't fill them in if they
         aren't in the entries file. */

      if ((entry->name) 
          && strcmp (entry->name->data, SVN_WC_ENTRY_THIS_DIR))
        printf ("Name: %s\n", entry->name->data);
      
      if (entry->url)
        printf ("Url: %s\n", entry->url->data);
          
      if (entry->repos)
        printf ("Repository: %s\n", entry->repos->data);

      if (SVN_IS_VALID_REVNUM (entry->revision))
        printf ("Revision: %" SVN_REVNUM_T_FMT "\n", entry->revision);

      switch (entry->kind) 
        {
        case svn_node_file:
          printf ("Node Kind: file\n");
          {
            svn_stringbuf_t *dir_name;
            svn_path_split (target, &dir_name, NULL, pool);
            SVN_ERR (svn_wc_conflicted_p (&text_conflict, &props_conflict,
                                          dir_name, entry, pool));
          }
          break;
          
        case svn_node_dir:
          printf ("Node Kind: directory\n");
          SVN_ERR (svn_wc_conflicted_p (&text_conflict, &props_conflict,
                                        target, entry, pool));
          break;
          
        case svn_node_none:
          printf ("Node Kind: none\n");
          break;
          
        case svn_node_unknown:
        default:
          printf ("Node Kind: unknown\n");
          break;
        }

      switch (entry->schedule) 
        {
        case svn_wc_schedule_normal:
          printf ("Schedule: normal\n");
          break;
          
        case svn_wc_schedule_add:
          printf ("Schedule: add\n");
          break;
          
        case svn_wc_schedule_delete:
          printf ("Schedule: delete\n");
          break;
          
        case svn_wc_schedule_replace:
          printf ("Schedule: replace\n");
          break;
          
        default:
          break;
        }

      if (entry->copied)
        {
          if (entry->copyfrom_url)
            printf ("Copied From Url: %s\n", entry->copyfrom_url->data);

          if (SVN_IS_VALID_REVNUM (entry->copyfrom_rev))
            printf ("Copied From Rev: %" SVN_REVNUM_T_FMT "\n",
                    entry->copyfrom_rev);
        }

      if (entry->cmt_author)
        printf ("Last Changed Author: %s\n", entry->cmt_author->data);

      if (SVN_IS_VALID_REVNUM (entry->cmt_rev))
        printf ("Last Changed Rev: %" SVN_REVNUM_T_FMT "\n", entry->cmt_rev);

      if (entry->cmt_date)
        svn_cl__info_print_time (entry->cmt_date, "Last Changed Date");

      if (entry->text_time)
        svn_cl__info_print_time (entry->text_time, "Text Last Updated");

      if (entry->prop_time)
        svn_cl__info_print_time (entry->prop_time, "Properties Last Updated");

      if (entry->checksum)
        printf ("Checksum: %s\n", entry->checksum->data);

      if (text_conflict && entry->conflict_old)
        printf ("Conflict Previous Base File: %s\n", entry->conflict_old->data);

      if (text_conflict && entry->conflict_wrk)
        printf ("Conflict Previous Working File: %s\n",
                entry->conflict_wrk->data);

      if (text_conflict && entry->conflict_new)
        printf ("Conflict Current Base File: %s\n", entry->conflict_new->data);

      if (props_conflict && entry->prejfile)
        printf ("Conflict Properties File: %s\n", entry->prejfile->data);

      /* Print extra newline separator. */
      printf ("\n");
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
