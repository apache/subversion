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
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_revert (const char *path,
                   svn_boolean_t recursive,
                   svn_wc_notify_func_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t wc_root;
  svn_error_t *err;

  /* We need to open the parent of PATH, if PATH is not a wc root, but we
     don't know if path is a directory.  It gets a bit messy. */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, TRUE, recursive,
                                  pool));
  SVN_ERR (svn_wc_is_wc_root (&wc_root, path, adm_access, pool));
  if (! wc_root)
    {
      const svn_wc_entry_t *entry;
      SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));

      if (entry->kind == svn_node_dir)
        {
          svn_node_kind_t kind;

          SVN_ERR (svn_io_check_path (path, &kind, pool));
          if (kind == svn_node_dir)
            {
              /* While we could add the parent to the access baton set, there
                 is no way to close such a set. */
              svn_wc_adm_access_t *dir_access;
              SVN_ERR (svn_wc_adm_close (adm_access));
              SVN_ERR (svn_wc_adm_open (&adm_access, NULL,
                                        svn_path_dirname (path, pool),
                                        TRUE, FALSE, pool));
              SVN_ERR (svn_wc_adm_open (&dir_access, adm_access, path,
                                        TRUE, recursive, pool));
            }
        }
    }

  err = svn_wc_revert (path, adm_access, recursive,
                       notify_func, notify_baton,
                       pool);

  SVN_ERR (svn_wc_adm_close (adm_access));

  /* Sleep for one second to ensure timestamp integrity. */
  apr_sleep (apr_time_from_sec(1));

  return err;
}
