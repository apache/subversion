/*
 * main.c :  Main control function for svnserve
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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




#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_getopt.h>
#include <apr_network_io.h>
#include <apr_signal.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_ra_svn.h"
#include "svn_utf.h"
#include "svn_path.h"

#include "server.h"

static void usage(const char *progname)
{
  if (!progname)
    progname = "svn-server";
  fprintf(stderr, "Usage: %s [-X|-d|-t] [-r root]\n", progname);
  exit(1);
}

static void sigchld_handler(int signo)
{
  /* Nothing to do; we just need to interrupt the accept(). */
}

int main(int argc, const char *const *argv)
{
  svn_boolean_t listen_once = FALSE, daemon_mode = FALSE, tunnel_mode = FALSE;
  apr_socket_t *sock, *usock;
  apr_file_t *in_file, *out_file;
  apr_sockaddr_t *sa;
  apr_pool_t *pool;
  apr_pool_t *connection_pool;
  svn_error_t *err;
  apr_getopt_t *os;
  char opt, errbuf[256];
  const char *arg, *root = "/";
  apr_status_t status;
  svn_ra_svn_conn_t *conn;
#if APR_HAS_FORK
  apr_proc_t proc;
#endif

  apr_initialize();
  atexit(apr_terminate);
  pool = svn_pool_create(NULL);
  apr_getopt_init(&os, pool, argc, argv);

  while (1)
    {
      status = apr_getopt(os, "dtXr:", &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        usage(argv[0]);
      switch (opt)
        {
        case 'd':
          daemon_mode = TRUE;
          break;

        case 't':
          tunnel_mode = TRUE;
          break;

        case 'X':
          listen_once = TRUE;
          break;

        case 'r':
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&root, arg, NULL, pool));
          SVN_INT_ERR(svn_path_get_absolute(&root, root, pool));
          break;
        }
    }
  if (os->ind != argc)
    usage(argv[0]);

  if (!daemon_mode && !listen_once)
    {
      apr_file_open_stdin(&in_file, pool);
      apr_file_open_stdout(&out_file, pool);
      conn = svn_ra_svn_create_conn(NULL, in_file, out_file, pool);
      svn_error_clear(serve(conn, root, tunnel_mode, pool));
      exit(0);
    }

  status = apr_socket_create(&sock, APR_INET, SOCK_STREAM, pool);
  if (status)
    {
      fprintf(stderr, "Can't create server socket: %s\n",
              apr_strerror(status, errbuf, sizeof(errbuf)));
      exit(1);
    }

  /* Prevents "socket in use" errors when server is killed and quickly
   * restarted. */
  apr_socket_opt_set(sock, APR_SO_REUSEADDR, 1);

  apr_sockaddr_info_get(&sa, NULL, APR_INET, SVN_RA_SVN_PORT, 0, pool);
  status = apr_bind(sock, sa);
  if (status)
    {
      fprintf(stderr, "Can't bind server socket: %s\n",
              apr_strerror(status, errbuf, sizeof(errbuf)));
      exit(1);
    }

  apr_listen(sock, 7);

#if APR_HAS_FORK
  if (!listen_once)
    apr_proc_detach(APR_PROC_DETACH_DAEMONIZE);

  apr_signal(SIGCHLD, sigchld_handler);
#endif

  connection_pool = svn_pool_create(pool);
  while (1)
    {
      /* Clear the pool for each iteration. */
      apr_pool_clear(connection_pool);

      status = apr_accept(&usock, sock, connection_pool);
#if APR_HAS_FORK
      /* Collect any zombie child processes. */
      while (apr_proc_wait_all_procs(&proc, NULL, NULL, APR_NOWAIT,
                                     connection_pool) == APR_CHILD_DONE)
        ;
#endif
      if (APR_STATUS_IS_EINTR(status))
        continue;
      if (status)
        {
          fprintf(stderr, "Can't accept client connection: %s\n",
                  apr_strerror(status, errbuf, sizeof(errbuf)));
          exit(1);
        }

      conn = svn_ra_svn_create_conn(usock, NULL, NULL, connection_pool);

      if (listen_once)
        {
          err = serve(conn, root, FALSE, connection_pool);

          if (listen_once && err
              && err->apr_err != SVN_ERR_RA_SVN_CONNECTION_CLOSED)
            svn_handle_error(err, stdout, FALSE);

          apr_socket_close(usock);
          apr_socket_close(sock);
          exit(0);
        }

#if APR_HAS_FORK
      /* This definitely won't work on Windows, which doesn't have the
       * concept of forking a process at all.  I'm not sure how to
       * structure a forking daemon process under Windows.  (Threads?
       * Our library code isn't perfectly thread-safe at the
       * moment.) -ghudson */
      status = apr_proc_fork(&proc, connection_pool);
      if (status == APR_INCHILD)
        {
          svn_error_clear(serve(conn, root, FALSE, connection_pool));
          apr_socket_close(usock);
          exit(0);
        }
      else if (status == APR_INPARENT)
        {
          apr_socket_close(usock);
        }
      else
        {
          /* Log an error, when we support logging. */
          apr_socket_close(usock);
        }
#else
      /* Serve one connection at a time. */
      svn_error_clear(serve(conn, root, FALSE, connection_pool));
#endif
    }

  return 0;
}
