/*
 * feedback.c:  feedback handlers for cmdline client.
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

#include <stdio.h>

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_pools.h"
#include "cl.h"


/* Baton for notify and friends. */
struct notify_baton
{
  svn_boolean_t received_some_change;
  svn_boolean_t is_checkout;
  svn_boolean_t suppress_final_line;
  svn_boolean_t sent_first_txdelta;
  apr_pool_t *pool;
};



#if 0  /* left for reference, soon to toss */
static void
notify_added (struct notify_baton *nb, const char *path)
{
  /* the pool (BATON) is typically the global pool; don't keep filling it */
  apr_pool_t *subpool = svn_pool_create (nb->pool);
  svn_wc_entry_t *entry;
  svn_error_t *err;
  const char *type = "      ";  /* fill with "binary" if binary, etc */

  /* ### this sucks. we have to open/parse the entries file to get this
     ### information. when adding thousands of files, this blows... */
  err = svn_wc_entry (&entry, path, FALSE, subpool);
  if (err)
    {
      printf ("WARNING: error fetching entry for %s\n", path);
      goto done;
    }
  else if (! entry)
    {
      printf ("WARNING: apparently failed to add %s\n", path);
      goto done;
    }

  if (entry->kind == svn_node_file)
    {
      const svn_string_t *value;

      /* ### and again: open/parse a properties file. urk... */
      err = svn_wc_prop_get (&value, SVN_PROP_MIME_TYPE, path, subpool);
      if (err)
        {
          printf ("WARNING: error fetching %s property for %s\n",
                  SVN_PROP_MIME_TYPE, path);
          goto done;
        }

      /* If the property exists and it doesn't start with `text/', we'll
         call it binary. */
      if ((value) && (value->len > 5) && (strncmp (value->data, "text/", 5)))
        type = "binary";
    }

  printf ("A  %s  %s\n", type, path);

 done:
  svn_pool_destroy (subpool);
}
#endif /* 0 */


/* This implements `svn_wc_notify_func_t'. */
static void
notify (void *baton,
        const char *path,
        svn_wc_notify_action_t action,
        svn_node_kind_t kind,
        const char *mime_type,
        svn_wc_notify_state_t content_state,
        svn_wc_notify_state_t prop_state,
        svn_revnum_t revision)
{
  struct notify_baton *nb = baton;
  char statchar_buf[3] = "_ ";

  switch (action)
    {
    case svn_wc_notify_delete:
      nb->received_some_change = TRUE;
      printf ("D  %s\n", path);
      break;

    case svn_wc_notify_restore:
      printf ("Restored %s\n", path);
      break;

    case svn_wc_notify_revert:
      printf ("Reverted %s\n", path);
      break;

    case svn_wc_notify_resolve:
      printf ("Resolved conflicted state of %s\n", path);
      break;

    case svn_wc_notify_add:
      nb->received_some_change = TRUE;
      if (kind == svn_node_dir)
        {
          printf ("A  %s\n", path);
          break;
        }
      else if (kind == svn_node_file)
        statchar_buf[0] = 'A';

      /* fall thru to more file case */

    case svn_wc_notify_update:
      /* note: maybe fell thru from above case */

      /* ### printf ("U   %s\n", path); */

      nb->received_some_change = TRUE;

      if ((kind == svn_node_file) && (action == svn_wc_notify_update))
        {
          if (content_state == svn_wc_notify_state_conflicted)
            statchar_buf[0] = 'C';
          else if (content_state == svn_wc_notify_state_merged)
            statchar_buf[0] = 'G';
          else if (content_state == svn_wc_notify_state_modified)
            statchar_buf[0] = 'U';
        }

      if (prop_state == svn_wc_notify_state_conflicted)
        statchar_buf[1] = 'C';
      else if (prop_state == svn_wc_notify_state_merged)
        statchar_buf[1] = 'G';
      else if (prop_state == svn_wc_notify_state_modified)
        statchar_buf[1] = 'U';

      if (! ((kind == svn_node_dir)
             && ((prop_state == svn_wc_notify_state_unknown)
                 || (prop_state == svn_wc_notify_state_unchanged))))
        printf ("%s %s\n", statchar_buf, path);

      break;

    case svn_wc_notify_update_completed:
      {
        if (! nb->suppress_final_line)
          {
            if (SVN_IS_VALID_REVNUM (revision))
              {
                if (nb->is_checkout)
                  printf ("Checked out revision %" SVN_REVNUM_T_FMT ".\n",
                          revision);
                else
                  {
                    if (nb->received_some_change)
                      printf ("Updated to revision %" SVN_REVNUM_T_FMT ".\n",
                              revision);
                    else
                      printf ("At revision %" SVN_REVNUM_T_FMT ".\n",
                              revision);
                  }
              }
            else  /* no revision */
              {
                if (nb->is_checkout)
                  printf ("Checkout complete.\n");
                else
                  printf ("Update complete\n");
              }
          }
      }

      break;

    case svn_wc_notify_commit_modified:
      printf ("Sending         %s\n", path);
      break;

    case svn_wc_notify_commit_added:
      if (mime_type
          && ((strlen (mime_type)) > 5)
          && ((strncmp (mime_type, "text/", 5)) != 0))
        printf ("Adding  (bin)  %s\n", path);
      else
        printf ("Adding         %s\n", path);
      break;

    case svn_wc_notify_commit_deleted:
      printf ("Deleting        %s\n", path);
      break;

    case svn_wc_notify_commit_replaced:
      printf ("Replacing       %s\n", path);
      break;

    case svn_wc_notify_commit_postfix_txdelta:
      if (! nb->sent_first_txdelta)
        {
          printf ("Transmitting file data ");
          nb->sent_first_txdelta = TRUE;
        }

      printf (".");
      fflush (stdout);
      break;

    default:
      break;
    }
}


void
svn_cl__get_notifier (svn_wc_notify_func_t *notify_func_p,
                      void **notify_baton_p,
                      svn_boolean_t is_checkout,
                      svn_boolean_t suppress_final_line,
                      apr_pool_t *pool)
{
  struct notify_baton *nb = apr_palloc (pool, sizeof (*nb));

  nb->received_some_change = FALSE;
  nb->sent_first_txdelta = FALSE;
  nb->is_checkout = is_checkout;
  nb->suppress_final_line = suppress_final_line;
  nb->pool = pool;

  *notify_func_p = notify;
  *notify_baton_p = nb;
}



/*
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
