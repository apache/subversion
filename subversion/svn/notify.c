/*
 * notify.c:  feedback handlers for cmdline client.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"


/* Baton for notify and friends. */
struct notify_baton
{
  svn_boolean_t received_some_change;
  svn_boolean_t is_checkout;
  svn_boolean_t is_export;
  svn_boolean_t suppress_final_line;
  svn_boolean_t sent_first_txdelta;
  svn_boolean_t in_external;
  svn_boolean_t had_print_error; /* Used to not keep printing error messages
                                    when we've already had one print error. */
};


/* This implements `svn_wc_notify_func2_t'.
 * NOTE: This function can't fail, so we just ignore any print errors. */
static void
notify(void *baton, const svn_wc_notify_t *n, apr_pool_t *pool)
{
  struct notify_baton *nb = baton;
  char statchar_buf[5] = "    ";
  const char *path_local;
  svn_error_t *err;

  path_local = svn_path_local_style(n->path, pool);

  switch (n->action)
    {
    case svn_wc_notify_skip:
      if (n->content_state == svn_wc_notify_state_missing)
        {
          if ((err = svn_cmdline_printf
               (pool, _("Skipped missing target: '%s'\n"),
                path_local)))
            goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf
               (pool, _("Skipped '%s'\n"), path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_update_delete:
      nb->received_some_change = TRUE;
      if ((err = svn_cmdline_printf(pool, "D    %s\n", path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_add:
      nb->received_some_change = TRUE;
      if (n->content_state == svn_wc_notify_state_conflicted)
        {
          if ((err = svn_cmdline_printf(pool, "C    %s\n", path_local)))
            goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf(pool, "A    %s\n", path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_restore:
      if ((err = svn_cmdline_printf(pool, _("Restored '%s'\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_revert:
      if ((err = svn_cmdline_printf(pool, _("Reverted '%s'\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_failed_revert:
      if (( err = svn_cmdline_printf(pool, _("Failed to revert '%s' -- "
                                             "try updating instead.\n"), 
                                     path_local)))
        goto print_error;
      break;

    case svn_wc_notify_resolved:
      if ((err = svn_cmdline_printf(pool,
                                    _("Resolved conflicted state of '%s'\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_add:
      /* We *should* only get the MIME_TYPE if PATH is a file.  If we
         do get it, and the mime-type is not textual, note that this
         is a binary addition. */
      if (n->mime_type && (svn_mime_type_is_binary(n->mime_type)))
        {
          if ((err = svn_cmdline_printf(pool, "A  (bin)  %s\n",
                                        path_local)))
            goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf(pool, "A         %s\n",
                                        path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_delete:
      nb->received_some_change = TRUE;
      if ((err = svn_cmdline_printf(pool, "D         %s\n",
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_update:
      {
        /* If this is an inoperative dir change, do no notification.
           An inoperative dir change is when a directory gets closed
           without any props having been changed. */
        if (! ((n->kind == svn_node_dir)
               && ((n->prop_state == svn_wc_notify_state_inapplicable)
                   || (n->prop_state == svn_wc_notify_state_unknown)
                   || (n->prop_state == svn_wc_notify_state_unchanged))))
          {
            if (n->kind == svn_node_file)
              {
                if (n->content_state == svn_wc_notify_state_conflicted)
                  statchar_buf[0] = 'C';
                else if (n->content_state == svn_wc_notify_state_merged)
                  statchar_buf[0] = 'G';
                else if (n->content_state == svn_wc_notify_state_changed)
                  statchar_buf[0] = 'U';
              }
            
            if (n->prop_state == svn_wc_notify_state_conflicted)
              statchar_buf[1] = 'C';
            else if (n->prop_state == svn_wc_notify_state_merged)
              statchar_buf[1] = 'G';
            else if (n->prop_state == svn_wc_notify_state_changed)
              statchar_buf[1] = 'U';

            if (n->lock_state == svn_wc_notify_lock_state_unlocked)
              statchar_buf[2] = 'B';

            if (statchar_buf[0] != ' ' || statchar_buf[1] != ' ')
              nb->received_some_change = TRUE;

            if (statchar_buf[0] != ' ' || statchar_buf[1] != ' '
                || statchar_buf[2] != ' ')
              {
                if ((err = svn_cmdline_printf(pool, "%s %s\n",
                                              statchar_buf, path_local)))
                  goto print_error;
              }
          }
      }
      break;

    case svn_wc_notify_update_external:
      /* Remember that we're now "inside" an externals definition. */
      nb->in_external = TRUE;

      /* Currently this is used for checkouts and switches too.  If we
         want different output, we'll have to add new actions. */
      if ((err = svn_cmdline_printf(pool,
                                    _("\nFetching external item into '%s'\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_completed:
      {
        if (! nb->suppress_final_line)
          {
            if (SVN_IS_VALID_REVNUM(n->revision))
              {
                if (nb->is_export)
                  {
                    if ((err = svn_cmdline_printf
                         (pool, nb->in_external
                          ? _("Exported external at revision %ld.\n")
                          : _("Exported revision %ld.\n"),
                          n->revision)))
                      goto print_error;
                  }
                else if (nb->is_checkout)
                  {
                    if ((err = svn_cmdline_printf
                         (pool, nb->in_external
                          ? _("Checked out external at revision %ld.\n")
                          : _("Checked out revision %ld.\n"),
                          n->revision)))
                      goto print_error;
                  }
                else
                  {
                    if (nb->received_some_change)
                      {
                        if ((err = svn_cmdline_printf
                             (pool, nb->in_external
                              ? _("Updated external to revision %ld.\n")
                              : _("Updated to revision %ld.\n"),
                              n->revision)))
                          goto print_error;
                      }
                    else
                      {
                        if ((err = svn_cmdline_printf
                             (pool, nb->in_external
                              ? _("External at revision %ld.\n")
                              : _("At revision %ld.\n"),
                              n->revision)))
                          goto print_error;
                      }
                  }
              }
            else  /* no revision */
              {
                if (nb->is_export)
                  {
                    if ((err = svn_cmdline_printf
                         (pool, nb->in_external
                          ? _("External export complete.\n")
                          : _("Export complete.\n"))))
                      goto print_error;
                  }
                else if (nb->is_checkout)
                  {
                    if ((err = svn_cmdline_printf
                         (pool, nb->in_external
                          ? _("External checkout complete.\n")
                          : _("Checkout complete.\n"))))
                      goto print_error;
                  }
                else
                  {
                    if ((err = svn_cmdline_printf
                         (pool, nb->in_external
                          ? _("External update complete.\n")
                          : _("Update complete.\n"))))
                      goto print_error;
                  }
              }
          }
      }
      if (nb->in_external)
        {
          nb->in_external = FALSE;
          if ((err = svn_cmdline_printf(pool, "\n")))
            goto print_error;
        }
      break;

    case svn_wc_notify_status_external:
      if ((err = svn_cmdline_printf
           (pool, _("\nPerforming status on external item at '%s'\n"), 
            path_local)))
        goto print_error;
      break;

    case svn_wc_notify_status_completed:
      if (SVN_IS_VALID_REVNUM(n->revision))
        if ((err = svn_cmdline_printf(pool,
                                      _("Status against revision: %6ld\n"),
                                      n->revision)))
          goto print_error;
      break;

    case svn_wc_notify_commit_modified:
      /* xgettext: Align the %s's on this and the following 4 messages */
      if ((err = svn_cmdline_printf(pool,
                                    _("Sending        %s\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_commit_added:
      if (n->mime_type && svn_mime_type_is_binary(n->mime_type))
        {
          if ((err = svn_cmdline_printf(pool,
                                        _("Adding  (bin)  %s\n"),
                                        path_local)))
          goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf(pool,
                                        _("Adding         %s\n"),
                                        path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_commit_deleted:
      if ((err = svn_cmdline_printf(pool, _("Deleting       %s\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_commit_replaced:
      if ((err = svn_cmdline_printf(pool,
                                    _("Replacing      %s\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_commit_postfix_txdelta:
      if (! nb->sent_first_txdelta)
        {
          nb->sent_first_txdelta = TRUE;
          if ((err = svn_cmdline_printf(pool,
                                        _("Transmitting file data "))))
            goto print_error;
        }

      if ((err = svn_cmdline_printf(pool, ".")))
        goto print_error;
      break;

    case svn_wc_notify_locked:
      if ((err = svn_cmdline_printf(pool, _("'%s' locked by user '%s'.\n"),
                                    path_local, n->lock->owner)))
        goto print_error;
      break;

    case svn_wc_notify_unlocked:
      if ((err = svn_cmdline_printf(pool, _("'%s' unlocked.\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_failed_lock:
    case svn_wc_notify_failed_unlock:
      svn_handle_warning(stderr, n->err);
      break;

    default:
      break;
    }

  if ((err = svn_cmdline_fflush(stdout)))
    goto print_error;

  return;

 print_error:
  /* If we had no errors before, print this error to stderr. Else, don't print
     anything.  The user already knows there were some output errors,
     so there is no point in flooding her with an error per notification. */
  if (!nb->had_print_error)
    {
      nb->had_print_error = TRUE;
      svn_handle_error2(err, stderr, FALSE, "svn: ");
    }
  svn_error_clear(err);
}


void
svn_cl__get_notifier(svn_wc_notify_func2_t *notify_func_p,
                     void **notify_baton_p,
                     svn_boolean_t is_checkout,
                     svn_boolean_t is_export,
                     svn_boolean_t suppress_final_line,
                     apr_pool_t *pool)
{
  struct notify_baton *nb = apr_palloc(pool, sizeof(*nb));

  nb->received_some_change = FALSE;
  nb->sent_first_txdelta = FALSE;
  nb->is_checkout = is_checkout;
  nb->is_export = is_export;
  nb->suppress_final_line = suppress_final_line;
  nb->in_external = FALSE;
  nb->had_print_error = FALSE;

  *notify_func_p = notify;
  *notify_baton_p = nb;
}
