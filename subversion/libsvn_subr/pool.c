/* pool.c:  pool wrappers for Subversion
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



#include <stdarg.h>
#include <assert.h>

#include <apr_lib.h>
#include <apr_general.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_hash.h>

#include "svn_pools.h"

/* Key for a boolean signifying that this is a top-level Subversion pool. */
static const char SVN_POOL_ROOTED_HERE[] = "svn-pool-rooted-here";

/* file_line for the non-debug case. */
static const char SVN_FILE_LINE_UNDEFINED[] = "svn:<undefined>";



/*-----------------------------------------------------------------*/

#if APR_HAS_THREADS
/* Cleanup function to reset the allocator mutex so that apr_allocator_free
   doesn't try to lock a destroyed mutex during pool cleanup or destruction.
 */
static apr_status_t
allocator_reset_mutex (void *allocator)
{
  apr_allocator_mutex_set(allocator, NULL);
  return APR_SUCCESS;
}
#endif /* APR_HAS_THREADS */


/* Pool allocation handler which just aborts, since we aren't generally
   prepared to deal with out-of-memory rerors.
 */
static int
abort_on_pool_failure (int retcode)
{
  abort ();
  return -1; /* prevent compiler warnings */
}

/*
   Macros to make the preprocessor logic less confusing.
   We need to always have svn_pool_xxx aswell as
   svn_pool_xxx_debug, where one of the two is a simple
   wrapper calling the other.
 */
#if !APR_POOL_DEBUG
#define SVN_POOL_FUNC_DEFINE(rettype, name) \
  rettype name(apr_pool_t *pool)

#else /* APR_POOL_DEBUG */
#undef svn_pool_create
#undef svn_pool_clear

#define SVN_POOL_FUNC_DEFINE(rettype, name) \
  rettype name##_debug(apr_pool_t *pool, const char *file_line)
   
#endif /* APR_POOL_DEBUG */


/* The maximum amount of memory to keep in the freelist: 4MB */
#define SVN_POOL_MAX_FREE (4096 * 1024)

SVN_POOL_FUNC_DEFINE(apr_pool_t *, svn_pool_create)
{
  apr_pool_t *ret_pool;
  apr_allocator_t *allocator = NULL;

  /* For the top level pool we want a seperate allocator */
  if (pool == NULL)
    {
      if (apr_allocator_create (&allocator))
        abort ();

      apr_allocator_set_max_free (allocator, SVN_POOL_MAX_FREE);
    }

#if !APR_POOL_DEBUG
  apr_pool_create_ex (&ret_pool, pool, abort_on_pool_failure, allocator);
#else /* APR_POOL_DEBUG */
  apr_pool_create_ex_debug (&ret_pool, pool, abort_on_pool_failure,
                            allocator, file_line);
#endif /* APR_POOL_DEBUG */

  /* If there is no parent, then initialize ret_pool as the "top". */
  if (pool == NULL)
    {
#if APR_HAS_THREADS
      {
        apr_thread_mutex_t *mutex;

        if (apr_thread_mutex_create (&mutex, APR_THREAD_MUTEX_DEFAULT,
                                     ret_pool))
          abort ();

        apr_allocator_mutex_set (allocator, mutex);
        apr_pool_cleanup_register (ret_pool, allocator,
                                   allocator_reset_mutex,
                                   apr_pool_cleanup_null);

        if (apr_pool_userdata_set ((void *) 1, SVN_POOL_ROOTED_HERE,
                                   apr_pool_cleanup_null, ret_pool))
          abort ();
      }
#endif /* APR_HAS_THREADS */

      apr_allocator_owner_set (allocator, ret_pool);
    }

  return ret_pool;
}


SVN_POOL_FUNC_DEFINE(void, svn_pool_clear)
{
  void *is_root;

  /* Clear the pool.  All userdata of this pool is now invalid. */
#if !APR_POOL_DEBUG
  apr_pool_clear (pool);
#else /* APR_POOL_DEBUG */
  apr_pool_clear_debug (pool, file_line);
#endif /* APR_POOL_DEBUG */

  apr_pool_userdata_get (&is_root, SVN_POOL_ROOTED_HERE, pool);
  if (is_root)
    {
#if APR_HAS_THREADS
      /* At this point, the mutex we set on our own allocator will have
         been destroyed.  Better create a new one.
       */
      apr_allocator_t *allocator;
      apr_thread_mutex_t *mutex;

      allocator = apr_pool_allocator_get (pool);
      if (apr_thread_mutex_create (&mutex, APR_THREAD_MUTEX_DEFAULT, pool))
        abort ();

      apr_allocator_mutex_set (allocator, mutex);
      apr_pool_cleanup_register (pool, allocator,
                                 allocator_reset_mutex, apr_pool_cleanup_null);
#endif /* APR_HAS_THREADS */
    }
}


/* Wrappers that ensure binary compatibility */
#if !APR_POOL_DEBUG
apr_pool_t *
svn_pool_create_debug (apr_pool_t *pool, const char *file_line)
{
  return svn_pool_create (pool);
}

void
svn_pool_clear_debug (apr_pool_t *pool, const char *file_line)
{
  svn_pool_clear (pool);
}

#else /* APR_POOL_DEBUG */
apr_pool_t *
svn_pool_create (apr_pool_t *pool)
{
  return svn_pool_create_debug (pool, SVN_FILE_LINE_UNDEFINED);
}

void
svn_pool_clear (apr_pool_t *pool)
{
  svn_pool_clear_debug (pool, SVN_FILE_LINE_UNDEFINED);
}
#endif /* APR_POOL_DEBUG */



/*
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */
