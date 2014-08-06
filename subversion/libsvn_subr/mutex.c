/*
 * svn_mutex.c: routines for mutual exclusion.
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

#include <apr_portable.h>

#include "svn_private_config.h"
#include "private/svn_atomic.h"
#include "private/svn_mutex.h"

/* With CHECKED set to TRUE, LOCKED and OWNER must be set *after* acquiring
 * the MUTEX and be reset *before* releasing it again.  This is sufficient
 * because we only want to check whether the current thread already holds
 * the lock.  And the current thread cannot be acquiring / releasing a lock
 * *while* checking for recursion at the same time.
 */
struct svn_mutex__t
{
  /* If TRUE, perform extra checks to detect attempts at recursive locking. */
  svn_boolean_t checked;

#if APR_HAS_THREADS

  apr_thread_mutex_t *mutex;

  /* The owner of this lock, if locked or NULL, otherwise.  Since NULL might
   * be a valid owner ID on some systems, checking for NULL may not be 100%
   * accurate.  Be sure to only produce false negatives in that case.
   * We can't use apr_os_thread_t directly here as there is no portable way
   * to access them atomically.  Instead, we assume that it can always be
   * cast safely to a pointer.
   * This value will only be modified while the lock is being held.  So,
   * setting and resetting it is never racy (but reading it may be).
   * Only used when CHECKED is set. */
  volatile void *owner;

#else

  /* If there is no multi-threading support, simply count lock attempts. */
  int count;

#endif

};

svn_error_t *
svn_mutex__init(svn_mutex__t **mutex_p,
                svn_boolean_t mutex_required,
                svn_boolean_t checked,
                apr_pool_t *result_pool)
{
  /* always initialize the mutex pointer, even though it is not
     strictly necessary if APR_HAS_THREADS has not been set */
  *mutex_p = NULL;

  if (mutex_required)
    {
      svn_mutex__t *mutex = apr_pcalloc(result_pool, sizeof(*mutex));

#if APR_HAS_THREADS
      apr_status_t status =
          apr_thread_mutex_create(&mutex->mutex,
                                  APR_THREAD_MUTEX_DEFAULT,
                                  result_pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't create mutex"));
#endif

      mutex->checked = checked;
      *mutex_p = mutex;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mutex__lock(svn_mutex__t *mutex)
{
  if (mutex)
    {
#if APR_HAS_THREADS
      apr_status_t status;
      void *current_thread;
      void *lock_owner;

      /* Detect recursive locking attempts. */
      if (mutex->checked)
        {
          /* "us" */
          current_thread = (void *)apr_os_thread_current();

          /* Get the current owner value without actually modifying it
             (idempotent replacement of NULL by NULL).  We need the atomic
             operation here since other threads may be writing to this
             variable while we read it (in which case LOCK_OWNER and
             CURRENT_THREAD will differ). */
          lock_owner = apr_atomic_casptr(&mutex->owner, NULL, NULL);

          /* If this matches, svn_mutex__unlock did not reset the owner
             since this thread acquired the lock:  Because there is no
             exit condition between that reset and the actual mutex unlock,
             and because no other thread would set the owner to this value,
             this thread has simply not released the mutex. */
          if (lock_owner &&
              apr_os_thread_equal((apr_os_thread_t)lock_owner,
                                  (apr_os_thread_t)current_thread))
            return svn_error_create(SVN_ERR_RECURSIVE_LOCK, NULL, 
                                    _("Recursive locks are not supported"));
        }

      /* Acquire the mutex.  In the meantime, other threads may acquire and
         release the same lock.  Once we got the lock, however, it is in a
         defined state. */
      status = apr_thread_mutex_lock(mutex->mutex);
      if (status)
        return svn_error_wrap_apr(status, _("Can't lock mutex"));

      /* We own the lock now. */
      if (mutex->checked)
        {
          /* It must have been released by the previous owner as part of
             the mutex unlock. */
          SVN_ERR_ASSERT(apr_atomic_casptr(&mutex->owner, NULL, NULL) == NULL);

          /* Set "us" as the new owner. */
          apr_atomic_casptr(&mutex->owner, current_thread, NULL);
        }
#else
      if (mutex->checked)
        {
          /* We want non-threaded systems to detect the same coding errors
             as threaded systems.  No further sync required. */
          if (mutex->count)
            return svn_error_create(SVN_ERR_RECURSIVE_LOCK, NULL, 
                                    _("Recursive locks are not supported"));

          /* Update lock counter. */
          ++mutex->count;
        }
#endif
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mutex__unlock(svn_mutex__t *mutex,
                  svn_error_t *err)
{
  if (mutex)
    {
#if APR_HAS_THREADS
      apr_status_t status;

      /* We will soon no longer be the owner of this lock.  So, reset the
         OWNER value.  This makes no difference to the recursion check in
         *other* threads; they are known not to hold this mutex and will
         not assume that they do after we set the OWNER to NULL.  And the
         current thread is known not to attempt a recursive lock right now;
         it cannot be in two places at the same time. */
      if (mutex->checked)
        {
          /* Reading the current OWNER value is faster and more reliable
             than asking APR for the current thread id (APR might return
             different but equivalent IDs for the same thread). */
          void *lock_owner = apr_atomic_casptr(&mutex->owner, NULL, NULL);

          /* Check for double unlock. */
          if (lock_owner == NULL)
            {
              /* There seems to be no guarantee that NULL is _not_ a valid
                 thread ID.  Double check to be sure. */
              if (!apr_os_thread_equal((apr_os_thread_t)lock_owner,
                                       apr_os_thread_current()))
                return svn_error_create(SVN_ERR_INVALID_UNLOCK, NULL, 
                                  _("Tried to release a non-locked mutex"));
            }

          /* Now, set it to NULL. */
          apr_atomic_casptr(&mutex->owner, NULL,  lock_owner);
        }

      /* Release the actual mutex. */
      status = apr_thread_mutex_unlock(mutex->mutex);
      if (status && !err)
        return svn_error_wrap_apr(status, _("Can't unlock mutex"));
#else
      /* Update lock counter. */
      if (mutex->checked)
        {
          if (mutex->count <= 0)
            return svn_error_create(SVN_ERR_INVALID_UNLOCK, NULL, 
                                    _("Tried to release a non-locked mutex"));

          --mutex->count;
        }
#endif
    }

  return err;
}
