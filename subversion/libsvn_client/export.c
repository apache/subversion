/*
 * export.c:  export a tree.
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

#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "client.h"


/*** Code. ***/

static svn_error_t *
remove_admin_dirs (const char *dir, apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  SVN_ERR (svn_io_get_dirents (&dirents, dir, pool));

  for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
    {
      const svn_node_kind_t *type;
      const char *item;
      const void *key;
      void *val;

      apr_hash_this (hi, &key, NULL, &val);

      item = key;
      type = val;

      if (*type == svn_node_dir)
        {
          const char *dir_path = svn_path_join (dir, key, subpool);

          if (strcmp (item, SVN_WC_ADM_DIR_NAME) == 0)
            {
              SVN_ERR (svn_io_remove_dir (dir_path, subpool));
            }
          else
            {
              SVN_ERR (remove_admin_dirs (dir_path, subpool));
            } 
        }

      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_versioned_files (const char *from,
                      const char *to,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *dirents;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_error_t *err;

  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, from, FALSE, FALSE, pool));
  err = svn_wc_entry (&entry, from, adm_access, FALSE, subpool);
  SVN_ERR (svn_wc_adm_close (adm_access));

  if (err && err->apr_err != SVN_ERR_WC_NOT_DIRECTORY)
    return err;

  /* we don't want to copy some random non-versioned directory. */
  if (entry)
    {
      apr_hash_index_t *hi;
      apr_finfo_t finfo;

      SVN_ERR (svn_io_stat (&finfo, from, APR_FINFO_PROT, subpool));

      SVN_ERR (svn_io_dir_make (to, finfo.protection, subpool));

      SVN_ERR (svn_io_get_dirents (&dirents, from, pool));

      for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
        {
          const svn_node_kind_t *type;
          const char *item;
          const void *key;
          void *val;

          apr_hash_this (hi, &key, NULL, &val);

          item = key;
          type = val;

          if (*type == svn_node_dir)
            {
              if (strcmp (item, SVN_WC_ADM_DIR_NAME) == 0)
                {
                  ; /* skip this, it's an administrative directory. */
                }
              else
                {
                  const char *new_from = svn_path_join (from, key, subpool);
                  const char *new_to = svn_path_join (to, key, subpool);

                  SVN_ERR (copy_versioned_files (new_from, new_to, subpool));
                }
            }
          else if (*type == svn_node_file)
            {
              const char *copy_from = svn_path_join (from, item, subpool);
              const char *copy_to = svn_path_join (to, item, subpool);

              err = svn_wc_entry (&entry, copy_from, adm_access, FALSE,
                                  subpool);

              if (err)
                {
                  if (err->apr_err != SVN_ERR_WC_NOT_FILE)
                    return err;
                  svn_error_clear (err);
                }

              /* don't copy it if it isn't versioned. */
              if (entry)
                {
                  SVN_ERR (svn_io_copy_file (copy_from, copy_to, TRUE,
                                             subpool));
                }
            }

          svn_pool_clear (subpool);
        }
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_export (const char *from,
                   const char *to,
                   svn_opt_revision_t *revision,
                   svn_client_auth_baton_t *auth_baton,
                   svn_wc_notify_func_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  if (svn_path_is_url (from))
    {
      /* export directly from the repository by doing a checkout first. */
      SVN_ERR (svn_client_checkout (notify_func,
                                    notify_baton,
                                    auth_baton,
                                    from,
                                    to,
                                    revision,
                                    TRUE,
                                    pool));

      /* walk over the wc and remove the administrative directories. */
      SVN_ERR (remove_admin_dirs (to, pool));
    }
  else
    {
      /* just copy the contents of the working copy into the target path. */
      SVN_ERR (copy_versioned_files (from, to, pool));
    }

  return SVN_NO_ERROR;
}
