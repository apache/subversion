/*
 * thread.c : Threaded server implementation
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



#include <apr_time.h>
#include <apr_thread_proc.h>
#include <apr_thread_cond.h>
#include <apr_thread_mutex.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_ra_svn.h"

#include "server.h"

#ifdef APR_HAS_THREADS

/* The structure encapsulating a single request. */
typedef struct thread_req_t thread_req_t;
struct thread_req_t {
  const char *root;
  svn_ra_svn_conn_t *conn;
  svn_boolean_t read_only;
  apr_pool_t *pool;

  /* Next struct in the queue. Used only in the request queue. */
  thread_req_t *next;
};

/* The max number of idle threads.
   TODO: Make this a command-line parameter. */
static int thread_idle_max = 5;

/* The maximum amount of time a thread can be idle before dying.
   TODO: Make this a command-line parameter. */
static apr_interval_time_t thread_idle_timeout = apr_time_from_sec(60);

/* The lock for the request queue and idle thread count. */
static apr_thread_mutex_t *thread_req_lock;

/* The trigger for queue events. Associated with thread_queue_lock. */
static apr_thread_cond_t *thread_req_event;

/* The number of currently running threads. Protected by thread_req_lock. */
static int thread_count = 0;

/* The number of idle threads. Protected by thread_req_lock. */
static int thread_idle_count = 0;

/* The number of requests in queue. Protected by thread_req_lock. */
static int thread_req_count = 0;

/* The request queue. Protected by thread_req_lock. */
static thread_req_t *thread_req_head = NULL;
static thread_req_t *thread_req_tail = NULL;

/* Return TRUE if the request queue is empty.
   The caller must own the queue lock. */
static APR_INLINE svn_boolean_t queue_empty(void)
{
  return (thread_req_head == NULL);
}

/* Insert a new request into the queue. Assumes that REQUEST->next is NULL.
   The caller must own the queue lock. */
static APR_INLINE void queue_put(thread_req_t *request)
{
  if (queue_empty())
    thread_req_head = thread_req_tail = request;
  else
    {
      thread_req_tail->next = request;
      thread_req_tail = request;
    }
  ++thread_req_count;
}

/* Get the next request from the queue. Assumes the queue is not empty.
   The caller must own the queue lock. */
static APR_INLINE thread_req_t *queue_get(void)
{
  thread_req_t *request = thread_req_head;
  thread_req_head = thread_req_head->next;
  --thread_req_count;
  return request;
}


/* The thread main function. DATA is the first request to serve. */
static void *APR_THREAD_FUNC thread_main(apr_thread_t *tid, void *data)
{
  apr_pool_t *const thread_pool = data;

  for (;;)
    {
      apr_status_t status = APR_SUCCESS;
      thread_req_t *request;
      apr_thread_mutex_lock(thread_req_lock);

      /* Poll the request queue. */
      while (queue_empty())
        {
          if (thread_idle_count >= thread_idle_max
              && APR_STATUS_IS_TIMEUP(status))
            {
              /* That's it, time to die. */
              --thread_count;
              apr_thread_mutex_unlock(thread_req_lock);
              svn_pool_destroy(thread_pool);
              return NULL;
            }

          ++thread_idle_count;
          status = apr_thread_cond_timedwait(thread_req_event, thread_req_lock,
                                             thread_idle_timeout);
          --thread_idle_count;
        }

      /* If we got here, the queue is not empty and we own the lock. */
      request = queue_get();
      apr_thread_mutex_unlock(thread_req_lock);

      /* Serve the request. */
      svn_error_clear(serve(request->conn, request->root, FALSE,
                            request->read_only, request->pool));
      svn_pool_destroy(request->pool);
    }
}


/* Create a thread to serve REQUEST. The thread pool is a subpool of POOL. */
static void create_thread(apr_pool_t *pool)
{
  apr_pool_t *thread_pool = svn_pool_create(pool);
  apr_threadattr_t *tattr;
  apr_thread_t *tid;
  apr_status_t status;
  char errbuf[256];

  status = apr_threadattr_create(&tattr, thread_pool);
  if (status)
    {
      fprintf(stderr, "Can't create threadattr: %s\n",
              apr_strerror(status, errbuf, sizeof(errbuf)));
      exit(1);
    }

  status = apr_threadattr_detach_set(tattr, 1);
  if (status)
    {
      fprintf(stderr, "Can't set detached state: %s\n",
              apr_strerror(status, errbuf, sizeof(errbuf)));
      exit(1);
    }

  status = apr_thread_create(&tid, tattr, thread_main,
                             thread_pool, thread_pool);
  if (status)
    {
      fprintf(stderr, "Can't create thread: %s\n",
              apr_strerror(status, errbuf, sizeof(errbuf)));
      exit(1);
    }
}


/* Serve a request in a working thread. */
void serve_thread(svn_ra_svn_conn_t *conn, const char *root,
                  svn_boolean_t read_only, apr_pool_t *pool,
                  apr_pool_t *connection_pool)
{
  thread_req_t *request;
  svn_boolean_t make_new_thread;

  request = apr_palloc(connection_pool, sizeof(*request));
  request->conn = conn;
  request->root = root;
  request->read_only = read_only;
  request->pool = connection_pool;
  request->next = NULL;

  /* Insert the request into the queue and update the counters. */
  apr_thread_mutex_lock(thread_req_lock);
  queue_put(request);
  make_new_thread = (thread_req_count > thread_idle_count);
  apr_thread_mutex_unlock(thread_req_lock);

  /* Create a new thread if there are no idle threads waiting. */
  if (make_new_thread)
    create_thread(pool);
  else
    apr_thread_cond_signal(thread_req_event);
}


/* Initialize the threaded server parameters. */
void init_threads(apr_pool_t *pool)
{
  apr_status_t status;
  char errbuf[256];

  status = apr_thread_mutex_create(&thread_req_lock,
                                   APR_THREAD_MUTEX_DEFAULT, pool);
  if (status)
    {
      fprintf(stderr, "Can't initialize request queue lock: %s\n",
              apr_strerror(status, errbuf, sizeof(errbuf)));
      exit(1);
    }

  status = apr_thread_cond_create(&thread_req_event, pool);
  if (status)
    {
      fprintf(stderr, "Can't initialize request queue event: %s\n",
              apr_strerror(status, errbuf, sizeof(errbuf)));
      exit(1);
    }
}


#endif /* APR_HAS_THREADS */
