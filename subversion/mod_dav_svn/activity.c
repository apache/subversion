/*
 * activity.c: DeltaV activity handling
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



#include <httpd.h>
#include <mod_dav.h>

#include <apr_dbm.h>

#include "svn_string.h"
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"

#include "dav_svn.h"

#define ACTIVITY_DB "dav/activities"


const char *dav_svn_get_txn(const dav_svn_repos *repos,
                            const char *activity_id)
{
  apr_dbm_t *dbm;
  apr_status_t status;
  const char *pathname;
  apr_datum_t key;
  apr_datum_t value;
  const char *txn_name;

  pathname = svn_path_join(repos->fs_path, ACTIVITY_DB, repos->pool);
  status = apr_dbm_open(&dbm, pathname, APR_DBM_READONLY, 
                        APR_OS_DEFAULT, repos->pool);
  if (status != APR_SUCCESS)
    {
      /* ### let's just assume that any error means the DB doesn't exist,
         ### therefore, the activity/transaction doesn't exist */
      return NULL;
    }

  key.dptr = (char *)activity_id;
  key.dsize = strlen(activity_id) + 1;  /* null-term'd */
  status = apr_dbm_fetch(dbm, key, &value);
  if (status != APR_SUCCESS)
    {
      /* ### again: assume failure means it doesn't exist */
      apr_dbm_close(dbm);
      return NULL;
    }

  txn_name = apr_pstrdup(repos->pool, value.dptr);   /* null-term'd */
  apr_dbm_freedatum(dbm, value);

  apr_dbm_close(dbm);

  return txn_name;
}

dav_error *dav_svn_store_activity(const dav_svn_repos *repos,
                                  const char *activity_id,
                                  const char *txn_name)
{
  apr_dbm_t *dbm;
  apr_status_t status;
  const char *pathname;
  apr_datum_t key;
  apr_datum_t value;

  pathname = svn_path_join(repos->fs_path, ACTIVITY_DB, repos->pool);
  status = apr_dbm_open(&dbm, pathname, APR_DBM_RWCREATE, 
                        APR_OS_DEFAULT, repos->pool);
  if (status != APR_SUCCESS)
    {
      svn_error_t *serr = 
        svn_error_create(status, NULL,
                         "failed to open activity db;  check repos perms.");

      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not open dbm files.");
    }

  key.dptr = (char *)activity_id;
  key.dsize = strlen(activity_id) + 1;  /* null-term'd */
  value.dptr = (char *)txn_name;
  value.dsize = strlen(txn_name) + 1;   /* null-term'd */
  status = apr_dbm_store(dbm, key, value);
  apr_dbm_close(dbm);
  if (status != APR_SUCCESS)
    {
      svn_error_t *serr = 
        svn_error_create(status, NULL,
                         "failed to close activity db; check repos perms.");

      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not close dbm files.");
    }

  return NULL;
}

dav_error *dav_svn_create_activity(const dav_svn_repos *repos,
                                   const char **ptxn_name,
                                   apr_pool_t *pool)
{
  svn_revnum_t rev;
  svn_fs_txn_t *txn;
  svn_error_t *serr;

  serr = svn_fs_youngest_rev(&rev, repos->fs, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not determine youngest revision");
    }

  serr = svn_repos_fs_begin_txn_for_commit(&txn, repos->repos, rev,
                                           repos->username, NULL, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not begin a transaction");
    }

  serr = svn_fs_txn_name(ptxn_name, txn, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not fetch transaction name");
    }

  return NULL;
}
