/*
 * main.c :  Main control function for svnserve
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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
#include <apr_getopt.h>
#include <apr_network_io.h>
#include <apr_signal.h>
#include <apr_thread_proc.h>
#include <apr_portable.h>

#include <locale.h>

#include "svn_cmdline.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_ra_svn.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "svn_opt.h"
#include "svn_repos.h"
#include "svn_fs.h"
#include "svn_version.h"
#include "svn_io.h"

#include "svn_private_config.h"
#include "winservice.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>   /* For getpid() */
#endif

#include "server.h"

/* The strategy for handling incoming connections.  Some of these may be
   unavailable due to platform limitations. */
enum connection_handling_mode {
  connection_mode_fork,   /* Create a process per connection */
  connection_mode_thread, /* Create a thread per connection */
  connection_mode_single  /* One connection at a time in this process */
};

/* The mode in which to run svnserve */
enum run_mode {
  run_mode_unspecified,
  run_mode_inetd,
  run_mode_daemon,
  run_mode_tunnel,
  run_mode_listen_once,
  run_mode_service
};

#if APR_HAS_FORK
#if APR_HAS_THREADS

#define CONNECTION_DEFAULT connection_mode_fork
#define CONNECTION_HAVE_THREAD_OPTION

#else /* ! APR_HAS_THREADS */

#define CONNECTION_DEFAULT connection_mode_fork

#endif /* ! APR_HAS_THREADS */
#elif APR_HAS_THREADS /* and ! APR_HAS_FORK */

#define CONNECTION_DEFAULT connection_mode_thread

#else /* ! APR_HAS_THREADS and ! APR_HAS_FORK */

#define CONNECTION_DEFAULT connection_mode_single

#endif


#ifdef WIN32
static apr_os_sock_t winservice_svnserve_accept_socket = INVALID_SOCKET;

/* The SCM calls this function (on an arbitrary thread, not the main()
   thread!) when it wants to stop the service.

   For now, our strategy is to close the listener socket, in order to
   unblock main() and cause it to exit its accept loop.  We cannot use
   apr_socket_close, because that function deletes the apr_socket_t
   structure, as well as closing the socket handle.  If we called
   apr_socket_close here, then main() will also call apr_socket_close,
   resulting in a double-free.  This way, we just close the kernel
   socket handle, which causes the accept() function call to fail,
   which causes main() to clean up the socket.  So, memory gets freed
   only once.

   This isn't pretty, but it's better than a lot of other options.
   Currently, there is no "right" way to shut down svnserve.

   We store the OS handle rather than a pointer to the apr_socket_t
   structure in order to eliminate any possibility of illegal memory
   access. */
void winservice_notify_stop(void)
{
  if (winservice_svnserve_accept_socket != INVALID_SOCKET)
    closesocket(winservice_svnserve_accept_socket);
}
#endif /* _WIN32 */


/* Option codes and descriptions for svnserve.
 *
 * The entire list must be terminated with an entry of nulls.
 *
 * APR requires that options without abbreviations
 * have codes greater than 255.
 */
#define SVNSERVE_OPT_LISTEN_PORT 256
#define SVNSERVE_OPT_LISTEN_HOST 257
#define SVNSERVE_OPT_FOREGROUND  258
#define SVNSERVE_OPT_TUNNEL_USER 259
#define SVNSERVE_OPT_VERSION     260
#define SVNSERVE_OPT_PID_FILE    261
#define SVNSERVE_OPT_SERVICE     262
#define SVNSERVE_OPT_CONFIG_FILE 263
#define SVNSERVE_OPT_LOG_FILE 264

static const apr_getopt_option_t svnserve__options[] =
  {
    {"daemon",           'd', 0, N_("daemon mode")},
    {"inetd",            'i', 0, N_("inetd mode")},
    {"tunnel",           't', 0, N_("tunnel mode")},
    {"listen-once",      'X', 0, N_("listen-once mode (useful for debugging)")},
#ifdef WIN32
    {"service",          SVNSERVE_OPT_SERVICE, 0,
     N_("Windows service mode (Service Control Manager)")},
#endif
    {"root",             'r', 1, N_("root of directory to serve")},
    {"read-only",        'R', 0,
     N_("force read only, overriding repository config file")},
    {"config-file",      SVNSERVE_OPT_CONFIG_FILE, 1,
     N_("read configuration from file ARG")},
    {"listen-port",       SVNSERVE_OPT_LISTEN_PORT, 1,
#ifdef WIN32
     N_("listen port\n"
        "                             "
        "[mode: daemon, service, listen-once]")},
#else
     N_("listen port\n"
        "                             "
        "[mode: daemon, listen-once]")},
#endif
    {"listen-host",       SVNSERVE_OPT_LISTEN_HOST, 1,
#ifdef WIN32
     N_("listen hostname or IP address\n"
        "                             "
        "[mode: daemon, service, listen-once]")},
#else
     N_("listen hostname or IP address\n"
        "                             "
        "[mode: daemon, listen-once]")},
#endif
#ifdef CONNECTION_HAVE_THREAD_OPTION
    /* ### Making the assumption here that WIN32 never has fork and so
     * ### this option never exists when --service exists. */
    {"threads",          'T', 0, N_("use threads instead of fork "
                                    "[mode: daemon]")},
#endif
    {"foreground",        SVNSERVE_OPT_FOREGROUND, 0,
     N_("run in foreground (useful for debugging)\n"
        "                             "
        "[mode: daemon]")},
    {"log-file",         SVNSERVE_OPT_LOG_FILE, 1,
     N_("svnserve log file")},
    {"pid-file",         SVNSERVE_OPT_PID_FILE, 1,
#ifdef WIN32
     N_("write server process ID to file ARG\n"
        "                             "
        "[mode: daemon, listen-once, service]")},
#else
     N_("write server process ID to file ARG\n"
        "                             "
        "[mode: daemon, listen-once]")},
#endif
    {"tunnel-user",      SVNSERVE_OPT_TUNNEL_USER, 1,
     N_("tunnel username (default is current uid's name)\n"
        "                             "
        "[mode: tunnel]")},
    {"help",             'h', 0, N_("display this help")},
    {"version",           SVNSERVE_OPT_VERSION, 0,
     N_("show program version information")},
    {0,                  0,   0, 0}
  };


static void usage(const char *progname, apr_pool_t *pool)
{
  if (!progname)
    progname = "svnserve";

  svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                                      _("Type '%s --help' for usage.\n"),
                                      progname));
  exit(1);
}

static void help(apr_pool_t *pool)
{
  apr_size_t i;

#ifdef WIN32
  svn_error_clear(svn_cmdline_fputs(_("usage: svnserve [-d | -i | -t | -X "
                                      "| --service] [options]\n"
                                      "\n"
                                      "Valid options:\n"),
                                    stdout, pool));
#else
  svn_error_clear(svn_cmdline_fputs(_("usage: svnserve [-d | -i | -t | -X] "
                                      "[options]\n"
                                      "\n"
                                      "Valid options:\n"),
                                    stdout, pool));
#endif
  for (i = 0; svnserve__options[i].name && svnserve__options[i].optch; i++)
    {
      const char *optstr;
      svn_opt_format_option(&optstr, svnserve__options + i, TRUE, pool);
      svn_error_clear(svn_cmdline_fprintf(stdout, pool, "  %s\n", optstr));
    }
  svn_error_clear(svn_cmdline_fprintf(stdout, pool, "\n"));
  exit(0);
}

static svn_error_t * version(apr_pool_t *pool)
{
  const char *fs_desc_start
    = _("The following repository back-end (FS) modules are available:\n\n");

  svn_stringbuf_t *version_footer;

  version_footer = svn_stringbuf_create(fs_desc_start, pool);
  SVN_ERR(svn_fs_print_modules(version_footer, pool));

#ifdef SVN_HAVE_SASL
  svn_stringbuf_appendcstr(version_footer,
                           _("\nCyrus SASL authentication is available.\n"));
#endif

  return svn_opt_print_help3(NULL, "svnserve", TRUE, FALSE, version_footer->data,
                             NULL, NULL, NULL, NULL, NULL, pool);
}


#if APR_HAS_FORK
static void sigchld_handler(int signo)
{
  /* Nothing to do; we just need to interrupt the accept(). */
}
#endif

/* In tunnel or inetd mode, we don't want hook scripts corrupting the
 * data stream by sending data to stdout, so we need to redirect
 * stdout somewhere else.  Sending it to stderr is acceptable; sending
 * it to /dev/null is another option, but apr doesn't provide a way to
 * do that without also detaching from the controlling terminal.
 */
static apr_status_t redirect_stdout(void *arg)
{
  apr_pool_t *pool = arg;
  apr_file_t *out_file, *err_file;
  apr_status_t apr_err;

  if ((apr_err = apr_file_open_stdout(&out_file, pool)))
    return apr_err;
  if ((apr_err = apr_file_open_stderr(&err_file, pool)))
    return apr_err;
  return apr_file_dup2(out_file, err_file, pool);
}

/* "Arguments" passed from the main thread to the connection thread */
struct serve_thread_t {
  svn_ra_svn_conn_t *conn;
  serve_params_t *params;
  apr_pool_t *pool;
};

#if APR_HAS_THREADS
static void * APR_THREAD_FUNC serve_thread(apr_thread_t *tid, void *data)
{
  struct serve_thread_t *d = data;

  svn_error_clear(serve(d->conn, d->params, d->pool));
  svn_pool_destroy(d->pool);

  return NULL;
}
#endif

/* Write the PID of the current process as a decimal number, followed by a
   newline to the file FILENAME, using POOL for temporary allocations. */
static svn_error_t *write_pid_file(const char *filename, apr_pool_t *pool)
{
  apr_file_t *file;
  const char *contents = apr_psprintf(pool, "%" APR_PID_T_FMT "\n",
                                             getpid());

  SVN_ERR(svn_io_file_open(&file, filename,
                           APR_WRITE | APR_CREATE | APR_TRUNCATE,
                           APR_OS_DEFAULT, pool));
  SVN_ERR(svn_io_file_write_full(file, contents, strlen(contents), NULL,
                                 pool));

  SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}

/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_repos", svn_repos_version },
      { "svn_fs",    svn_fs_version },
      { "svn_delta", svn_delta_version },
      { "svn_ra_svn", svn_ra_svn_version },
      { NULL, NULL }
    };

  SVN_VERSION_DEFINE(my_version);
  return svn_ver_check_list(&my_version, checklist);
}


int main(int argc, const char *argv[])
{
  enum run_mode run_mode = run_mode_unspecified;
  svn_boolean_t foreground = FALSE;
  apr_socket_t *sock, *usock;
  apr_file_t *in_file, *out_file;
  apr_sockaddr_t *sa;
  apr_pool_t *pool;
  apr_pool_t *connection_pool;
  svn_error_t *err;
  apr_getopt_t *os;
  int opt;
  serve_params_t params;
  const char *arg;
  apr_status_t status;
  svn_ra_svn_conn_t *conn;
  apr_proc_t proc;
#if APR_HAS_THREADS
  apr_threadattr_t *tattr;
  apr_thread_t *tid;

  struct serve_thread_t *thread_data;
#endif
  enum connection_handling_mode handling_mode = CONNECTION_DEFAULT;
  apr_uint16_t port = SVN_RA_SVN_PORT;
  const char *host = NULL;
  int family = APR_INET;
  int mode_opt_count = 0;
  const char *config_filename = NULL;
  const char *pid_filename = NULL;
  const char *log_filename = NULL;
  svn_node_kind_t kind;

  /* Initialize the app. */
  if (svn_cmdline_init("svnserve", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool. */
  pool = svn_pool_create(NULL);

#ifdef SVN_HAVE_SASL
  SVN_INT_ERR(cyrus_init(pool));
#endif

  /* Check library versions */
  err = check_lib_versions();
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");

  /* Initialize the FS library. */
  err = svn_fs_initialize(pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");

  err = svn_cmdline__getopt_init(&os, argc, argv, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");

  params.root = "/";
  params.tunnel = FALSE;
  params.tunnel_user = NULL;
  params.read_only = FALSE;
  params.cfg = NULL;
  params.pwdb = NULL;
  params.authzdb = NULL;
  params.log_file = NULL;

  while (1)
    {
      status = apr_getopt_long(os, svnserve__options, &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        usage(argv[0], pool);
      switch (opt)
        {
        case 'h':
          help(pool);
          break;

        case SVNSERVE_OPT_VERSION:
          SVN_INT_ERR(version(pool));
          exit(0);
          break;

        case 'd':
          if (run_mode != run_mode_daemon)
            {
              run_mode = run_mode_daemon;
              mode_opt_count++;
            }
          break;

        case SVNSERVE_OPT_FOREGROUND:
          foreground = TRUE;
          break;

        case 'i':
          if (run_mode != run_mode_inetd)
            {
              run_mode = run_mode_inetd;
              mode_opt_count++;
            }
          break;

        case SVNSERVE_OPT_LISTEN_PORT:
          port = atoi(arg);
          break;

        case SVNSERVE_OPT_LISTEN_HOST:
          host = arg;
          break;

        case 't':
          if (run_mode != run_mode_tunnel)
            {
              run_mode = run_mode_tunnel;
              mode_opt_count++;
            }
          break;

        case SVNSERVE_OPT_TUNNEL_USER:
          params.tunnel_user = arg;
          break;

        case 'X':
          if (run_mode != run_mode_listen_once)
            {
              run_mode = run_mode_listen_once;
              mode_opt_count++;
            }
          break;

        case 'r':
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&params.root, arg, pool));

          err = svn_io_check_resolved_path(params.root, &kind, pool);
          if (err)
            return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");
          if (kind != svn_node_dir)
            {
              svn_error_clear
                (svn_cmdline_fprintf
                   (stderr, pool,
                    _("svnserve: Root path '%s' does not exist "
                      "or is not a directory.\n"), params.root));
              return EXIT_FAILURE;
            }

          params.root = svn_path_internal_style(params.root, pool);
          SVN_INT_ERR(svn_path_get_absolute(&params.root, params.root, pool));
          break;

        case 'R':
          params.read_only = TRUE;
          break;

        case 'T':
          handling_mode = connection_mode_thread;
          break;

#ifdef WIN32
        case SVNSERVE_OPT_SERVICE:
          if (run_mode != run_mode_service)
            {
              run_mode = run_mode_service;
              mode_opt_count++;
            }
          break;
#endif

        case SVNSERVE_OPT_CONFIG_FILE:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&config_filename, arg, pool));
          config_filename = svn_path_internal_style(config_filename, pool);
          SVN_INT_ERR(svn_path_get_absolute(&config_filename, config_filename,
                                            pool));
          break;

        case SVNSERVE_OPT_PID_FILE:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&pid_filename, arg, pool));
          pid_filename = svn_path_internal_style(pid_filename, pool);
          SVN_INT_ERR(svn_path_get_absolute(&pid_filename, pid_filename,
                                            pool));
          break;

        case SVNSERVE_OPT_LOG_FILE:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&log_filename, arg, pool));
          log_filename = svn_path_internal_style(log_filename, pool);
          SVN_INT_ERR(svn_path_get_absolute(&log_filename, log_filename,
                                            pool));
          break;

        }
    }
  if (os->ind != argc)
    usage(argv[0], pool);

  if (mode_opt_count != 1)
    {
      svn_error_clear(svn_cmdline_fputs(
#ifdef WIN32
                      _("You must specify exactly one of -d, -i, -t, "
                        "--service or -X.\n"),
#else
                      _("You must specify exactly one of -d, -i, -t or -X.\n"),
#endif
                       stderr, pool));
      usage(argv[0], pool);
    }

  /* If a configuration file is specified, load it and any referenced
   * password and authorization files. */
  if (config_filename)
      SVN_INT_ERR(load_configs(&params.cfg, &params.pwdb, &params.authzdb,
                               config_filename, TRUE,
                               svn_path_dirname(config_filename, pool),
                               NULL, NULL, /* server baton, conn */
                               pool));

  if (log_filename)
    SVN_INT_ERR(svn_io_file_open(&params.log_file, log_filename,
                                 APR_WRITE | APR_CREATE | APR_APPEND,
                                 APR_OS_DEFAULT, pool));

  if (params.tunnel_user && run_mode != run_mode_tunnel)
    {
      svn_error_clear
        (svn_cmdline_fprintf
           (stderr, pool,
            _("Option --tunnel-user is only valid in tunnel mode.\n")));
      exit(1);
    }

  if (run_mode == run_mode_inetd || run_mode == run_mode_tunnel)
    {
      params.tunnel = (run_mode == run_mode_tunnel);
      apr_pool_cleanup_register(pool, pool, apr_pool_cleanup_null,
                                redirect_stdout);
      status = apr_file_open_stdin(&in_file, pool);
      if (status)
        {
          err = svn_error_wrap_apr(status, _("Can't open stdin"));
          return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");
        }

      status = apr_file_open_stdout(&out_file, pool);
      if (status)
        {
          err = svn_error_wrap_apr(status, _("Can't open stdout"));
          return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");
        }

      conn = svn_ra_svn_create_conn(NULL, in_file, out_file, pool);
      svn_error_clear(serve(conn, &params, pool));
      exit(0);
    }

#ifdef WIN32
  /* If svnserve needs to run as a Win32 service, then we need to
     coordinate with the Service Control Manager (SCM) before
     continuing.  This function call registers the svnserve.exe
     process with the SCM, waits for the "start" command from the SCM
     (which will come very quickly), and confirms that those steps
     succeeded.

     After this call succeeds, the service is free to run.  At some
     point in the future, the SCM will send a message to the service,
     requesting that it stop.  This is translated into a call to
     winservice_notify_stop().  The service is then responsible for
     cleanly terminating.

     We need to do this before actually starting the service logic
     (opening files, sockets, etc.) because the SCM wants you to
     connect *first*, then do your service-specific logic.  If the
     service process takes too long to connect to the SCM, then the
     SCM will decide that the service is busted, and will give up on
     it.
     */
  if (run_mode == run_mode_service)
    {
      err = winservice_start();
      if (err)
        {
          svn_handle_error2(err, stderr, FALSE, "svnserve: ");

          /* This is the most common error.  It means the user started
             svnserve from a shell, and specified the --service
             argument.  svnserve cannot be started, as a service, in
             this way.  The --service argument is valid only valid if
             svnserve is started by the SCM. */
          if (err->apr_err ==
              APR_FROM_OS_ERROR(ERROR_FAILED_SERVICE_CONTROLLER_CONNECT))
            {
              svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                  _("svnserve: The --service flag is only valid if the"
                    " process is started by the Service Control Manager.\n")));
            }

          svn_error_clear(err);
          exit(1);
        }

      /* The service is now in the "starting" state.  Before the SCM will
         consider the service "started", this thread must call the
         winservice_running() function. */
    }
#endif /* WIN32 */

  /* Make sure we have IPV6 support first before giving apr_sockaddr_info_get
     APR_UNSPEC, because it may give us back an IPV6 address even if we can't
     create IPV6 sockets. */

#if APR_HAVE_IPV6
#ifdef MAX_SECS_TO_LINGER
  /* ### old APR interface */
  status = apr_socket_create(&sock, APR_INET6, SOCK_STREAM, pool);
#else
  status = apr_socket_create(&sock, APR_INET6, SOCK_STREAM, APR_PROTO_TCP,
                             pool);
#endif
  if (status == 0)
    {
      apr_socket_close(sock);
      family = APR_UNSPEC;
    }
#endif

  status = apr_sockaddr_info_get(&sa, host, family, port, 0, pool);
  if (status)
    {
      err = svn_error_wrap_apr(status, _("Can't get address info"));
      return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");
    }


#ifdef MAX_SECS_TO_LINGER
  /* ### old APR interface */
  status = apr_socket_create(&sock, sa->family, SOCK_STREAM, pool);
#else
  status = apr_socket_create(&sock, sa->family, SOCK_STREAM, APR_PROTO_TCP,
                             pool);
#endif
  if (status)
    {
      err = svn_error_wrap_apr(status, _("Can't create server socket"));
      return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");
    }

  /* Prevents "socket in use" errors when server is killed and quickly
   * restarted. */
  apr_socket_opt_set(sock, APR_SO_REUSEADDR, 1);

  status = apr_socket_bind(sock, sa);
  if (status)
    {
      err = svn_error_wrap_apr(status, _("Can't bind server socket"));
      return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");
    }

  apr_socket_listen(sock, 7);

#if APR_HAS_FORK
  if (run_mode != run_mode_listen_once && !foreground)
    apr_proc_detach(APR_PROC_DETACH_DAEMONIZE);

  apr_signal(SIGCHLD, sigchld_handler);
#endif

#ifdef SIGPIPE
  /* Disable SIGPIPE generation for the platforms that have it. */
  apr_signal(SIGPIPE, SIG_IGN);
#endif

#ifdef SIGXFSZ
  /* Disable SIGXFSZ generation for the platforms that have it, otherwise
   * working with large files when compiled against an APR that doesn't have
   * large file support will crash the program, which is uncool. */
  apr_signal(SIGXFSZ, SIG_IGN);
#endif

  if (pid_filename)
    SVN_INT_ERR(write_pid_file(pid_filename, pool));

#ifdef WIN32
  status = apr_os_sock_get(&winservice_svnserve_accept_socket, sock);
  if (status)
    winservice_svnserve_accept_socket = INVALID_SOCKET;

  /* At this point, the service is "running".  Notify the SCM. */
  if (run_mode == run_mode_service)
    winservice_running();
#endif

  while (1)
    {
#ifdef WIN32
      if (winservice_is_stopping())
        return ERROR_SUCCESS;
#endif

      /* Non-standard pool handling.  The main thread never blocks to join
         the connection threads so it cannot clean up after each one.  So
         separate pools, that can be cleared at thread exit, are used */
      connection_pool = svn_pool_create(NULL);

      status = apr_socket_accept(&usock, sock, connection_pool);
      if (handling_mode == connection_mode_fork)
        {
          /* Collect any zombie child processes. */
          while (apr_proc_wait_all_procs(&proc, NULL, NULL, APR_NOWAIT,
                                         connection_pool) == APR_CHILD_DONE)
            ;
        }
      if (APR_STATUS_IS_EINTR(status))
        {
          svn_pool_destroy(connection_pool);
          continue;
        }
      if (status)
        {
          err = svn_error_wrap_apr
            (status, _("Can't accept client connection"));
          return svn_cmdline_handle_exit_error(err, pool, "svnserve: ");
        }

      conn = svn_ra_svn_create_conn(usock, NULL, NULL, connection_pool);

      if (run_mode == run_mode_listen_once)
        {
          err = serve(conn, &params, connection_pool);

          if (err)
            svn_handle_error2(err, stdout, FALSE, "svnserve: ");
          svn_error_clear(err);

          apr_socket_close(usock);
          apr_socket_close(sock);
          exit(0);
        }

      switch (handling_mode)
        {
        case connection_mode_fork:
#if APR_HAS_FORK
          status = apr_proc_fork(&proc, connection_pool);
          if (status == APR_INCHILD)
            {
              apr_socket_close(sock);
              err = serve(conn, &params, connection_pool);
              log_error(err, params.log_file,
                        svn_ra_svn_conn_remote_host(conn),
                        NULL, NULL, /* user, repos */
                        connection_pool);
              svn_error_clear(err);
              apr_socket_close(usock);
              exit(0);
            }
          else if (status == APR_INPARENT)
            {
              apr_socket_close(usock);
            }
          else
            {
              err = svn_error_wrap_apr(status, "apr_proc_fork");
              log_error(err, params.log_file,
                        svn_ra_svn_conn_remote_host(conn),
                        NULL, NULL, /* user, repos */
                        connection_pool);
              svn_error_clear(err);
              apr_socket_close(usock);
            }
          svn_pool_destroy(connection_pool);
#endif
          break;

        case connection_mode_thread:
          /* Create a detached thread for each connection.  That's not a
             particularly sophisticated strategy for a threaded server, it's
             little different from forking one process per connection. */
#if APR_HAS_THREADS
          status = apr_threadattr_create(&tattr, connection_pool);
          if (status)
            {
              err = svn_error_wrap_apr(status, _("Can't create threadattr"));
              svn_handle_error2(err, stderr, FALSE, "svnserve: ");
              svn_error_clear(err);
              exit(1);
            }
          status = apr_threadattr_detach_set(tattr, 1);
          if (status)
            {
              err = svn_error_wrap_apr(status, _("Can't set detached state"));
              svn_handle_error2(err, stderr, FALSE, "svnserve: ");
              svn_error_clear(err);
              exit(1);
            }
          thread_data = apr_palloc(connection_pool, sizeof(*thread_data));
          thread_data->conn = conn;
          thread_data->params = &params;
          thread_data->pool = connection_pool;
          status = apr_thread_create(&tid, tattr, serve_thread, thread_data,
                                     connection_pool);
          if (status)
            {
              err = svn_error_wrap_apr(status, _("Can't create thread"));
              svn_handle_error2(err, stderr, FALSE, "svnserve: ");
              svn_error_clear(err);
              exit(1);
            }
#endif
          break;

        case connection_mode_single:
          /* Serve one connection at a time. */
          svn_error_clear(serve(conn, &params, connection_pool));
          svn_pool_destroy(connection_pool);
        }
    }

  /* NOTREACHED */
}
