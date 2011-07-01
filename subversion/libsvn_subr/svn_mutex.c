/*
 * svn_mutex.c: in-memory caching for Subversion
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

#include "svn_private_config.h"
#include "private/svn_mutex.h"

svn_error_t *
svn_mutex__init(svn_mutex__t *mutex, 
                svn_boolean_t enable_mutex, 
                apr_pool_t *pool)
{
#if APR_HAS_THREADS
  mutex->mutex = NULL;
  if (enable_mutex)
    {
      apr_status_t status =
          apr_thread_mutex_create(&mutex->mutex,
                                  APR_THREAD_MUTEX_DEFAULT,
                                  pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't create mutex"));
    }
#else
  if (enable_mutex)
    return svn_error_wrap_apr(APR_ENOTIMPL, _("APR doesn't support threads"));
#endif
    
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mutex__lock(svn_mutex__t mutex)
{
#if APR_HAS_THREADS
  if (mutex.mutex)
    {
      apr_status_t status = apr_thread_mutex_lock(mutex.mutex);
      if (status)
        return svn_error_wrap_apr(status, _("Can't lock mutex"));
    }
#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mutex__unlock(svn_mutex__t mutex, 
                  svn_error_t *err)
{
#if APR_HAS_THREADS
  if (mutex.mutex)
    {
      apr_status_t status = apr_thread_mutex_unlock(mutex.mutex);
      if (status && !err)
        return svn_error_wrap_apr(status, _("Can't unlock mutex"));
    }
#endif

  return err;
}
