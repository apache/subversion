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

  if (targets->nelts == 0)
    return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0, 0, pool, "");

  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];
      svn_wc_entry_t *entry;

      SVN_ERR (svn_wc_entry (&entry, target, pool));

      if (! entry)
        {
          printf ("'%s' is not a versioned resource.\n", target->data);
        }
      else
        {
          /* note: we have to be paranoid about checking that these are valid,
             since svn_wc_entry() doesn't fill them in if they aren't in the 
             entries file. */

          if (entry->name)
            printf ("Name: %s\n", entry->name->data);

          if (entry->url)
            printf ("Url: %s\n", entry->url->data);

          if (entry->repos)
            printf ("Repository: %s\n", entry->repos->data);

          if (SVN_IS_VALID_REVNUM (entry->revision))
            printf ("Revision: %ld\n", entry->revision);

          if (entry->kind)
            {
              switch (entry->kind) {
              case svn_node_file:
                printf ("Node Kind: file\n");
                break;

              case svn_node_dir:
                printf ("Node Kind: dIrectory\n");
                break;

              case svn_node_unknown:
                printf ("Node Kind: unknown\n");
                break;

              case svn_node_none:
                printf ("Node Kind: none\n");
                break;

              default:
                break;
              }
            }

          if (entry->schedule)
            {
              switch (entry->schedule) {
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
            }

          if (entry->copied)
            {
              if (entry->copyfrom_url)
                printf ("Copied From Url: %s\n", entry->copyfrom_url->data);

              if (SVN_IS_VALID_REVNUM (entry->copyfrom_rev))
                printf ("Copied From Rev: %ld\n", entry->copyfrom_rev);
            }

          if (entry->cmt_author)
            printf ("Last Changed Author: %s\n", entry->cmt_author->data);

          if (SVN_IS_VALID_REVNUM (entry->cmt_rev))
            printf ("Last Changed Rev: %ld\n", entry->cmt_rev);

          if (entry->cmt_date)
            svn_cl__info_print_time (entry->cmt_date, "Last Changed Date");

          if (entry->text_time)
            svn_cl__info_print_time (entry->text_time, "Text Last Updated");

          if (entry->prop_time)
            svn_cl__info_print_time (entry->prop_time,
                                     "Properties Last Updated");

          if (entry->checksum)
            printf ("Checksum: %s\n", entry->checksum->data);
        }
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
