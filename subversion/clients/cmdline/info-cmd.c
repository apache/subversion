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
#include "svn_time.h"
#include "cl.h"


/*** Code. ***/

static void
svn_cl__info_print_time (apr_time_t atime,
                         const char *desc,
                         apr_pool_t *pool)
{
  printf ("%s: %s\n", desc, svn_time_to_human_nts (atime,pool));
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
      const char *target = ((const char **) (targets->elts))[i];
      svn_wc_entry_t *entry;
      svn_boolean_t text_conflict = FALSE, props_conflict = FALSE;

      printf ("Path: %s\n", target);

      SVN_ERR (svn_wc_entry (&entry, target, FALSE, pool));
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
          && strcmp (entry->name, SVN_WC_ENTRY_THIS_DIR))
        printf ("Name: %s\n", entry->name);
      
      if (entry->url)
        printf ("Url: %s\n", entry->url);
          
      if (entry->repos)
        printf ("Repository: %s\n", entry->repos);

      if (SVN_IS_VALID_REVNUM (entry->revision))
        printf ("Revision: %" SVN_REVNUM_T_FMT "\n", entry->revision);

      switch (entry->kind) 
        {
        case svn_node_file:
          printf ("Node Kind: file\n");
          {
            const char *dir_name;
            svn_path_split_nts (target, &dir_name, NULL, pool);
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
            printf ("Copied From Url: %s\n", entry->copyfrom_url);

          if (SVN_IS_VALID_REVNUM (entry->copyfrom_rev))
            printf ("Copied From Rev: %" SVN_REVNUM_T_FMT "\n",
                    entry->copyfrom_rev);
        }

      if (entry->cmt_author)
        printf ("Last Changed Author: %s\n", entry->cmt_author);

      if (SVN_IS_VALID_REVNUM (entry->cmt_rev))
        printf ("Last Changed Rev: %" SVN_REVNUM_T_FMT "\n", entry->cmt_rev);

      if (entry->cmt_date)
        svn_cl__info_print_time (entry->cmt_date, "Last Changed Date", pool);

      if (entry->text_time)
        svn_cl__info_print_time (entry->text_time, "Text Last Updated", pool);

      if (entry->prop_time)
        svn_cl__info_print_time (entry->prop_time, "Properties Last Updated",
                                 pool);

      if (entry->checksum)
        printf ("Checksum: %s\n", entry->checksum);

      if (text_conflict && entry->conflict_old)
        printf ("Conflict Previous Base File: %s\n", entry->conflict_old);

      if (text_conflict && entry->conflict_wrk)
        printf ("Conflict Previous Working File: %s\n",
                entry->conflict_wrk);

      if (text_conflict && entry->conflict_new)
        printf ("Conflict Current Base File: %s\n", entry->conflict_new);

      if (props_conflict && entry->prejfile)
        printf ("Conflict Properties File: %s\n", entry->prejfile);

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
