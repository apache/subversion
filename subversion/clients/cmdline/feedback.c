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


struct notify_baton
{
  apr_pool_t *pool;
  svn_boolean_t sent_first_txdelta;
};


static void 
notify_added (void *baton, const char *path)
{
  struct notify_baton *nb = (struct notify_baton *) baton;

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


static void
notify_commit_postfix_txdelta (void *baton,
                               const char *path)
{
  struct notify_baton *nb = (struct notify_baton *) baton;
  
  if (! nb->sent_first_txdelta)
    {
      printf ("Transmitting file data ");
      nb->sent_first_txdelta = TRUE;
    }
  
  printf (".");
  fflush (stdout);
}


void 
svn_cl__notify_func (void *baton, 
                     const char *path,
                     svn_wc_notify_action_t action, 
                     svn_node_kind_t kind,
                     svn_wc_notify_state_t text_state,
                     svn_wc_notify_state_t prop_state,
                     svn_revnum_t revision)
{
  /* Note that KIND, TEXT_STATE, PROP_STATE, and REVISION are ignored
     by this implementation. */

  switch (action)
    {
    case svn_wc_notify_add:
      notify_added (baton, path);
      return;

    case svn_wc_notify_delete:
      printf ("D  %s\n", path);
      return;

    case svn_wc_notify_restore:
      printf ("Restored %s\n", path);
      return;

    case svn_wc_notify_revert:
      printf ("Reverted %s\n", path);
      return;

    case svn_wc_notify_resolve:
      printf ("Resolved conflicted state of %s\n", path);
      return;

    case svn_wc_notify_update:
      printf ("U   %s\n", path);
      return;

    case svn_wc_notify_commit_modified:
      printf ("Sending   %s\n", path);
      return;

    case svn_wc_notify_commit_added:
      printf ("Adding    %s\n", path);
      return;

    case svn_wc_notify_commit_deleted:
      printf ("Deleting  %s\n", path);
      return;

    case svn_wc_notify_commit_replaced:
      printf ("Replacing %s\n", path);
      return;

    case svn_wc_notify_commit_postfix_txdelta:
      notify_commit_postfix_txdelta (baton, path);
      return;

    default:
      break;
    }
}


void *
svn_cl__make_notify_baton (apr_pool_t *pool)
{
  struct notify_baton *nb = apr_palloc (pool, sizeof(*nb));

  nb->pool = pool;
  nb->sent_first_txdelta = 0;

  return nb;
}


/*** Notifiers for checkout. ***/

/* ### This should handle update/switch, as well as checkouts;
   see http://subversion.tigris.org/issues/show_bug.cgi?id=662. */

/* Baton for checkout/update/switche notification. */
struct update_notify_baton
{
  svn_boolean_t received_some_change;
  svn_boolean_t is_checkout;
  svn_boolean_t suppress_final_line;
  apr_pool_t *pool;
};


/* This implements `svn_wc_notify_func_t'. */
static void
update_notify (void *baton,
               const char *path,
               svn_wc_notify_action_t action,
               svn_node_kind_t kind,
               svn_wc_notify_state_t text_state,
               svn_wc_notify_state_t prop_state,
               svn_revnum_t revision)
{
  struct update_notify_baton *ub = baton;
  char statchar_buf[3] = "_ ";

  switch (action)
    {
    case svn_wc_notify_delete:
      ub->received_some_change = TRUE;
      printf ("D  %s\n", path);
      break;

    case svn_wc_notify_add:
      ub->received_some_change = TRUE;
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

      ub->received_some_change = TRUE;
      
      if ((kind == svn_node_file) && (action == svn_wc_notify_update))
        {
          if (text_state == svn_wc_notify_state_conflicted)
            statchar_buf[0] = 'C';
          else if (text_state == svn_wc_notify_state_merged)
            statchar_buf[0] = 'G';
          else if (text_state == svn_wc_notify_state_modified)
            {
              statchar_buf[0] = 'U';
            }
        }
      
      if (prop_state == svn_wc_notify_state_conflicted)
        statchar_buf[1] = 'C';
      else if (prop_state == svn_wc_notify_state_merged)
        statchar_buf[1] = 'G';
      else if (prop_state == svn_wc_notify_state_modified)
        statchar_buf[1] = 'U';
      
      if (! ((kind == svn_node_dir)
             && (prop_state == svn_wc_notify_state_unknown)
             && (prop_state == svn_wc_notify_state_unchanged)))
        printf ("%s %s\n", statchar_buf, path);
      
      break;

    case svn_wc_notify_update_completed:
      {
        if (! ub->suppress_final_line)
          {
            if (SVN_IS_VALID_REVNUM (revision))
              {
                if (ub->is_checkout)
                  printf ("Checked out revision %" SVN_REVNUM_T_FMT ".\n",
                          revision);
                else
                  {
                    if (ub->received_some_change)
                      printf ("Updated to revision %" SVN_REVNUM_T_FMT ".\n",
                              revision);
                    else
                      printf ("At revision %" SVN_REVNUM_T_FMT ".\n",
                              revision);
                  }
              }
            else  /* no revision */
              {
                if (ub->is_checkout)
                  printf ("Checkout complete.\n");
                else
                  printf ("Update complete\n");
              }
          }
      }

      break;

    default:
      break;
    }
}


void
svn_cl__get_checkout_notifier (svn_wc_notify_func_t *notify_func_p,
                               void **notify_baton_p,
                               svn_boolean_t is_checkout,
                               svn_boolean_t suppress_final_line,
                               apr_pool_t *pool)
{
  struct update_notify_baton *ub = apr_palloc (pool, sizeof (*ub));

  ub->received_some_change = FALSE;
  ub->is_checkout = is_checkout;
  ub->suppress_final_line = suppress_final_line;
  ub->pool = pool;

  *notify_func_p = update_notify;
  *notify_baton_p = ub;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
