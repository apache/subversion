/*
 * wc_db.c :  manipulating the administrative database
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#define SVN_WC__I_AM_WC_DB

#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"

#include "wc_db.h"
#include "wc_db_private.h"

#include "svn_private_config.h"
#include "private/svn_sqlite.h"


/* Some helpful transaction helpers. 

   Instead of directly using SQLite transactions, these wrappers take care of
   simple cases by allowing consumers to worry about wrapping the wcroot and
   local_relpath, which are almost always used within the transaction.
   
   This also means if we later want to implement some wc_db-specific txn
   handling, we have a convenient place to do it.
   */

/* A callback which supplies WCROOTs and LOCAL_RELPATHs. */
typedef svn_error_t *(*db_txn_callback_t)(void *baton,
                                          svn_wc__db_wcroot_t *wcroot,
                                          const char *local_relpath,
                                          apr_pool_t *scratch_pool);

/* Baton for use with run_txn() and with_db_txn(). */
struct txn_baton_t
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  db_txn_callback_t cb_func;
  void *cb_baton;
};


/* Unwrap the sqlite transaction into a wc_db txn.
   Implements svn_sqlite__transaction_callback_t. */
static svn_error_t *
run_txn(void *baton, svn_sqlite__db_t *db, apr_pool_t *scratch_pool)
{
  struct txn_baton_t *tb = baton;

  return svn_error_return(
    tb->cb_func(tb->cb_baton, tb->wcroot, tb->local_relpath, scratch_pool));
}


/* Run CB_FUNC in a SQLite transaction with CB_BATON, using WCROOT and
   LOCAL_RELPATH.  If callbacks require additional information, they may
   provide it using CB_BATON. */
svn_error_t *
svn_wc__db_with_txn(svn_wc__db_wcroot_t *wcroot,
                    const char *local_relpath,
                    svn_wc__db_txn_callback_t cb_func,
                    void *cb_baton,
                    apr_pool_t *scratch_pool)
{
  struct txn_baton_t tb = { wcroot, local_relpath, cb_func, cb_baton };

  return svn_error_return(
    svn_sqlite__with_lock(wcroot->sdb, run_txn, &tb, scratch_pool));
}
