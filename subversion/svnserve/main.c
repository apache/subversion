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




#ifdef SVN_WIN32
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_getopt.h>

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
  int sock, usock, one = 1, len, debug = 0;
  struct sockaddr_in si;
  apr_pool_t *pool;
  svn_error_t *err;
  apr_getopt_t *os;
  char opt;
  const char *arg, *root = "/";
  apr_status_t status;
  pid_t pid;

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

  if (!debug)
    apr_proc_detach(1);

  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1)
    {
      perror("socket");
      exit(1);
    }

  /* This prevents "socket in use" errors when the main server process
   * is killed and quickly restarted.  I'm not sure if it works on
   * Windows. -ghudson */
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  si.sin_family = AF_INET;
  si.sin_port = htons(SVN_RA_SVN_PORT);
  memset(&si.sin_addr, 0, sizeof(si.sin_addr));
  if (bind(sock, (struct sockaddr *) &si, sizeof(si)) != 0)
    {
      perror("bind");
      exit(1);
    }

  listen(sock, 7);

  while (1)
    {
      len = sizeof(si);
      usock = accept(sock, &si, &len);
      if (usock == -1)
        {
          perror("accept");
          exit(1);
        }

      if (debug)
        {
          err = serve(usock, root, pool);

          if (debug && err && err->apr_err != SVN_ERR_RA_SVN_CONNECTION_CLOSED)
            svn_handle_error(err, stdout, FALSE);

          exit(0);
        }

      /* This definitely won't work on Windows, which doesn't have the
       * concept of forking a process at all.  I'm not sure how to
       * structure a forking daemon process under Windows.  (Threads?
       * Our library code isn't perfectly thread-safe at the
       * moment.) -ghudson */
      pid = fork();
      if (pid < 0)
        {
          /* Log something, when we support logging. */
          close(usock);
          continue;
        }
      if (pid == 0)
        {
          svn_error_clear(serve(usock, root, pool));
          exit(0);
        }
      close(usock);
      while (waitpid(pid, NULL, 0) != pid)
        ;
    }


  return 0;
}
