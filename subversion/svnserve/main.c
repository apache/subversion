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

#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_ra_svn.h"

#include "server.h"

static void usage(const char *progname)
{
  if (!progname)
    progname = "svn-server";
  fprintf(stderr, "Usage: %s [-X] [-r root]\n", progname);
  exit(1);
}

int main(int argc, const char *const *argv)
{
  int debug = 0;
  apr_socket_t *sock, *usock;
  apr_sockaddr_t *sa;
  apr_pool_t *pool;
  svn_error_t *err;
  apr_getopt_t *os;
  char opt, errbuf[256];
  const char *arg, *root = "/";
  apr_status_t status;
  apr_proc_t proc;

  apr_initialize();
  atexit(apr_terminate);
  pool = svn_pool_create(NULL);
  apr_getopt_init(&os, pool, argc, argv);

  while (1)
    {
      status = apr_getopt(os, "Xr:", &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        usage(argv[0]);
      switch (opt)
        {
        case 'X':
          debug = 1;
          break;

        case 'r':
          root = arg;
          break;
        }
    }
  if (os->ind != argc)
    usage(argv[0]);

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

  if (!debug)
    apr_proc_detach(1);

  while (1)
    {
      status = apr_accept(&usock, sock, pool);
      if (status)
        {
          fprintf(stderr, "Can't accept client connection: %s\n",
                  apr_strerror(status, errbuf, sizeof(errbuf)));
          exit(1);
        }

      if (debug)
        {
          err = serve(usock, root, pool);

          if (debug && err && err->apr_err != SVN_ERR_RA_SVN_CONNECTION_CLOSED)
            svn_handle_error(err, stdout, FALSE);

          exit(0);
        }

#ifdef APR_HAS_FORK
      /* This definitely won't work on Windows, which doesn't have the
       * concept of forking a process at all.  I'm not sure how to
       * structure a forking daemon process under Windows.  (Threads?
       * Our library code isn't perfectly thread-safe at the
       * moment.) -ghudson */
      status = apr_proc_fork(&proc, pool);
      if (status == APR_INCHILD)
        {
          svn_error_clear(serve(usock, root, pool));
          exit(0);
        }
      else if (status == APR_INPARENT)
        {
          apr_socket_close(usock);
          while (apr_proc_wait(&proc, NULL, NULL, APR_WAIT) != APR_CHILD_DONE)
            ;
        }
      else
        {
          /* Log an error, when we support logging. */
          apr_socket_close(usock);
        }
#else
      /* Serve one connection at a time. */
      svn_error_clear(serve(usock, root, pool));
#endif
    }

  return 0;
}
