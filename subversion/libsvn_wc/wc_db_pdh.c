/*
 * wc_db_pdh.c :  supporting datastructures for the administrative database
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

#include "wc_db_pdh.h"



svn_wc__db_pdh_t *
svn_wc__db_pdh_get_or_create(svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             svn_boolean_t create_allowed,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh = apr_hash_get(db->dir_data,
                                       local_dir_abspath, APR_HASH_KEY_STRING);

  if (pdh == NULL && create_allowed)
    {
      pdh = apr_pcalloc(db->state_pool, sizeof(*pdh));

      /* Copy the path for the proper lifetime.  */
      pdh->local_abspath = apr_pstrdup(db->state_pool, local_dir_abspath);

      /* We don't know anything about this directory, so we cannot construct
         a svn_wc__db_wcroot_t for it (yet).  */

      /* ### parent */

      apr_hash_set(db->dir_data, pdh->local_abspath, APR_HASH_KEY_STRING, pdh);
    }

  return pdh;
}
