/*
 * revert.c:  wrapper around wc revert functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_config.h"
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_revert (const char *path,
                   svn_boolean_t recursive,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t wc_root;
  svn_boolean_t use_commit_times;
  svn_error_t *err;

  /* We need to open the parent of PATH, if PATH is not a wc root, but we
     don't know if path is a directory.  It gets a bit messy. */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, TRUE, recursive,
                                  pool));
  if ((err = svn_wc_is_wc_root (&wc_root, path, adm_access, pool)))
    goto out;
  if (! wc_root)
    {
      const svn_wc_entry_t *entry;
      if ((err = svn_wc_entry (&entry, path, adm_access, FALSE, pool)))
        goto out;

      if (entry->kind == svn_node_dir)
        {
          svn_node_kind_t kind;

          if ((err = svn_io_check_path (path, &kind, pool)))
            goto out;
          if (kind == svn_node_dir)
            {
              /* While we could add the parent to the access baton set, there
                 is no way to close such a set. */
              svn_wc_adm_access_t *dir_access;
              if ((err = svn_wc_adm_close (adm_access))
                  || (err = svn_wc_adm_open (&adm_access, NULL,
                                             svn_path_dirname (path, pool),
                                             TRUE, FALSE, pool))
                  || (err = svn_wc_adm_open (&dir_access, adm_access, path,
                                          TRUE, recursive, pool)))
                goto out;
            }
        }
    }

  /* Look for run-time config variables that affect behavior. */
  {
    const char *commit_time_str;

    svn_config_t *cfg = ctx->config
      ? apr_hash_get (ctx->config, SVN_CONFIG_CATEGORY_CONFIG,  
                      APR_HASH_KEY_STRING)
      : NULL;

    svn_config_get (cfg, &commit_time_str, SVN_CONFIG_SECTION_MISCELLANY,
                    SVN_CONFIG_OPTION_USE_COMMIT_TIMES, NULL);
    if (commit_time_str)
      use_commit_times = (strcasecmp (commit_time_str, "yes") == 0)
                          ? TRUE : FALSE;
    else
      use_commit_times = FALSE;
  }

  err = svn_wc_revert (path, adm_access, recursive, use_commit_times,
                       ctx->cancel_func, ctx->cancel_baton,
                       ctx->notify_func, ctx->notify_baton,
                       pool);

  out:

  SVN_ERR (svn_wc_adm_close (adm_access));

  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps ();

  return err;
}
