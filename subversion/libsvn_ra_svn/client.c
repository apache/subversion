/*
 * client.c :  Functions for repository access via the Subversion protocol
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include <apr_strings.h>
#include <apr_network_io.h>
#include <apr_md5.h>
#include <apr_uri.h>
#include <assert.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_time.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_private_config.h"
#include "svn_ra.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_ra_svn.h"
#include "svn_md5.h"
#include "svn_props.h"

#include "ra_svn.h"

typedef struct {
  apr_pool_t *pool;
  svn_ra_svn_conn_t *conn;
  int protocol_version;
  svn_boolean_t is_tunneled;
  svn_auth_baton_t *auth_baton;
  const char *user;
  const char *realm_prefix;
  const char **tunnel_argv;
} ra_svn_session_baton_t;

typedef struct {
  ra_svn_session_baton_t *sess_baton;
  apr_pool_t *pool;
  svn_revnum_t *new_rev;
  svn_commit_callback2_t callback;
  void *callback_baton;
} ra_svn_commit_callback_baton_t;

typedef struct {
  ra_svn_session_baton_t *sess_baton;
  svn_ra_svn_conn_t *conn;
  apr_pool_t *pool;
  const svn_delta_editor_t *editor;
  void *edit_baton;
} ra_svn_reporter_baton_t;

/* Parse an svn URL's tunnel portion into tunnel, if there is a tunnel
   portion. */

static void parse_tunnel(const char *url, const char **tunnel,
                         apr_pool_t *pool)
{
  const char *p;

  *tunnel = NULL;

  if (strncasecmp(url, "svn", 3) != 0)
    return;
  url += 3;

  /* Get the tunnel specification, if any. */
  if (*url == '+')
    {
      url++;
      p = strchr(url, ':');
      if (!p)
        return;
      *tunnel = apr_pstrmemdup(pool, url, p - url);
      url = p;
    }
}

static svn_error_t *make_connection(const char *hostname, unsigned short port,
                                    apr_socket_t **sock, apr_pool_t *pool)
{
  apr_sockaddr_t *sa;
  apr_status_t status;
  int family = APR_INET;
  
  /* Make sure we have IPV6 support first before giving apr_sockaddr_info_get
     APR_UNSPEC, because it may give us back an IPV6 address even if we can't
     create IPV6 sockets.  */  

#if APR_HAVE_IPV6
#ifdef MAX_SECS_TO_LINGER
  status = apr_socket_create(sock, APR_INET6, SOCK_STREAM, pool);
#else
  status = apr_socket_create(sock, APR_INET6, SOCK_STREAM,
                             APR_PROTO_TCP, pool);
#endif
  if (status == 0)
    {
      apr_socket_close(*sock);
      family = APR_UNSPEC;
    }
#endif

  /* Resolve the hostname. */
  status = apr_sockaddr_info_get(&sa, hostname, family, port, 0, pool);
  if (status)
    return svn_error_createf(status, NULL, _("Unknown hostname '%s'"),
                             hostname);

  /* Create the socket. */
#ifdef MAX_SECS_TO_LINGER
  /* ### old APR interface */
  status = apr_socket_create(sock, sa->family, SOCK_STREAM, pool);
#else
  status = apr_socket_create(sock, sa->family, SOCK_STREAM, APR_PROTO_TCP, 
                             pool);
#endif
  if (status)
    return svn_error_wrap_apr(status, _("Can't create socket"));

  status = apr_socket_connect(*sock, sa);
  if (status)
    return svn_error_wrap_apr(status, _("Can't connect to host '%s'"),
                              hostname);

  return SVN_NO_ERROR;
}

/* Convert a property list received from the server into a hash table. */
static svn_error_t *parse_proplist(apr_array_header_t *list, apr_pool_t *pool,
                                   apr_hash_t **props)
{
  char *name;
  svn_string_t *value;
  svn_ra_svn_item_t *elt;
  int i;

  *props = apr_hash_make(pool);
  for (i = 0; i < list->nelts; i++)
    {
      elt = &((svn_ra_svn_item_t *) list->elts)[i];
      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Proplist element not a list"));
      SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, pool, "cs", &name, &value));
      apr_hash_set(*props, name, APR_HASH_KEY_STRING, value);
    }
  return SVN_NO_ERROR;
}

/* Set *DIFFS to an array of svn_prop_t, allocated in POOL, based on the
   property diffs in LIST, received from the server. */
static svn_error_t *parse_prop_diffs(apr_array_header_t *list,
                                     apr_pool_t *pool,
                                     apr_array_header_t **diffs)
{
  svn_ra_svn_item_t *elt;
  svn_prop_t *prop;
  int i;

  *diffs = apr_array_make(pool, list->nelts, sizeof(svn_prop_t));

  for (i = 0; i < list->nelts; i++)
    {
      elt = &APR_ARRAY_IDX(list, i, svn_ra_svn_item_t);
      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Prop diffs element not a list"));
      prop = apr_array_push(*diffs);
      SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, pool, "c(?s)", &prop->name,
                                     &prop->value));
    }
  return SVN_NO_ERROR;
}

/* Parse a lockdesc, provided in LIST as specified by the protocol into
   LOCK, allocated in POOL. */
static svn_error_t *parse_lock(apr_array_header_t *list, apr_pool_t *pool,
                               svn_lock_t **lock)
{
  const char *cdate, *edate;
  *lock = svn_lock_create(pool);
  SVN_ERR(svn_ra_svn_parse_tuple(list, pool, "ccc(?c)c(?c)", &(*lock)->path,
                                 &(*lock)->token, &(*lock)->owner,
                                 &(*lock)->comment, &cdate, &edate));
  (*lock)->path = svn_path_canonicalize((*lock)->path, pool);
  SVN_ERR(svn_time_from_cstring(&(*lock)->creation_date, cdate, pool));
  if (edate)
    SVN_ERR(svn_time_from_cstring(&(*lock)->expiration_date, edate, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *interpret_kind(const char *str, apr_pool_t *pool,
                                   svn_node_kind_t *kind)
{
  if (strcmp(str, "none") == 0)
    *kind = svn_node_none;
  else if (strcmp(str, "file") == 0)
    *kind = svn_node_file;
  else if (strcmp(str, "dir") == 0)
    *kind = svn_node_dir;
  else if (strcmp(str, "unknown") == 0)
    *kind = svn_node_unknown;
  else
    return svn_error_createf(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                             _("Unrecognized node kind '%s' from server"),
                             str);
  return SVN_NO_ERROR;
}

/* --- AUTHENTICATION ROUTINES --- */

static svn_boolean_t find_mech(apr_array_header_t *mechlist, const char *mech)
{
  int i;
  svn_ra_svn_item_t *elt;

  for (i = 0; i < mechlist->nelts; i++)
    {
      elt = &((svn_ra_svn_item_t *) mechlist->elts)[i];
      if (elt->kind == SVN_RA_SVN_WORD && strcmp(elt->u.word, mech) == 0)
        return TRUE;
    }
  return FALSE;
}

/* Having picked a mechanism, start authentication by writing out an
 * auth response.  If COMPAT is true, also write out a version number
 * and capability list.  MECH_ARG may be NULL for mechanisms with no
 * initial client response. */
static svn_error_t *auth_response(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  const char *mech, const char *mech_arg,
                                  svn_boolean_t compat)
{
  if (compat)
    return svn_ra_svn_write_tuple(conn, pool, "nw(?c)(www)", (apr_uint64_t) 1,
                                  mech, mech_arg,
                                  SVN_RA_SVN_CAP_EDIT_PIPELINE,
                                  SVN_RA_SVN_CAP_SVNDIFF1,
                                  SVN_RA_SVN_CAP_ABSENT_ENTRIES);
  else
    return svn_ra_svn_write_tuple(conn, pool, "w(?c)", mech, mech_arg);
}

/* Read the "success" response to ANONYMOUS or EXTERNAL authentication. */
static svn_error_t *read_success(svn_ra_svn_conn_t *conn, apr_pool_t *pool)
{
  const char *status, *arg;

  SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "w(?c)", &status, &arg));
  if (strcmp(status, "failure") == 0 && arg)
    return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                             _("Authentication error from server: %s"), arg);
  else if (strcmp(status, "success") != 0 || arg)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("Unexpected server response to authentication"));
  return SVN_NO_ERROR;
}

/* Respond to an auth request and perform authentication.  REALM may
 * be NULL for the initial authentication exchange of protocol version
 * 1. */
static svn_error_t *do_auth(ra_svn_session_baton_t *sess,
                            apr_array_header_t *mechlist,
                            const char *realm, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess->conn;
  const char *realmstring, *user, *password, *msg;
  svn_auth_iterstate_t *iterstate;
  void *creds;
  svn_boolean_t compat = (realm == NULL);

  realmstring = realm ? apr_psprintf(pool, "%s %s", sess->realm_prefix, realm)
    : sess->realm_prefix;
  if (sess->is_tunneled && find_mech(mechlist, "EXTERNAL"))
    {
      /* Ask the server to use the tunnel connection environment (on
       * Unix, that means uid) to determine the authentication name. */
      SVN_ERR(auth_response(conn, pool, "EXTERNAL", "", compat));
      return read_success(conn, pool);
    }
  else if (find_mech(mechlist, "ANONYMOUS"))
    {
      SVN_ERR(auth_response(conn, pool, "ANONYMOUS", "", compat));
      return read_success(conn, pool);
    }
  else if (find_mech(mechlist, "CRAM-MD5"))
    {
      SVN_ERR(svn_auth_first_credentials(&creds, &iterstate,
                                         SVN_AUTH_CRED_SIMPLE, realmstring,
                                         sess->auth_baton, pool));
      if (!creds)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Can't get password"));
      while (creds)
        {
          user = ((svn_auth_cred_simple_t *) creds)->username;
          password = ((svn_auth_cred_simple_t *) creds)->password;
          SVN_ERR(auth_response(conn, pool, "CRAM-MD5", NULL, compat));
          SVN_ERR(svn_ra_svn__cram_client(conn, pool, user, password, &msg));
          if (!msg)
            break;
          SVN_ERR(svn_auth_next_credentials(&creds, iterstate, pool));
        }
      if (!creds)
        return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                 _("Authentication error from server: %s"),
                                 msg);
      SVN_ERR(svn_auth_save_credentials(iterstate, pool));
      return SVN_NO_ERROR;
    }
  else
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("Cannot negotiate authentication mechanism"));
}

static svn_error_t *handle_auth_request(ra_svn_session_baton_t *sess,
                                        apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess->conn;
  apr_array_header_t *mechlist;
  const char *realm;

  if (sess->protocol_version < 2)
    return SVN_NO_ERROR;
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "lc", &mechlist, &realm));
  if (mechlist->nelts == 0)
    return SVN_NO_ERROR;
  return do_auth(sess, mechlist, realm, pool);
}

/* --- REPORTER IMPLEMENTATION --- */

static svn_error_t *ra_svn_set_path(void *baton, const char *path,
                                    svn_revnum_t rev,
                                    svn_boolean_t start_empty,
                                    const char *lock_token,
                                    apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "set-path", "crb(?c)", path, rev,
                               start_empty, lock_token));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_delete_path(void *baton, const char *path,
                                       apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "delete-path", "c", path));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_link_path(void *baton, const char *path,
                                     const char *url,
                                     svn_revnum_t rev,
                                     svn_boolean_t start_empty,
                                     const char *lock_token,
                                     apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "link-path", "ccrb(?c)",
                               path, url, rev, start_empty, lock_token));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_finish_report(void *baton,
                                         apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, b->pool, "finish-report", ""));
  SVN_ERR(handle_auth_request(b->sess_baton, b->pool));
  SVN_ERR(svn_ra_svn_drive_editor(b->conn, b->pool, b->editor, b->edit_baton,
                                  NULL));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, b->pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_abort_report(void *baton,
                                        apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, b->pool, "abort-report", ""));
  return SVN_NO_ERROR;
}

static svn_ra_reporter2_t ra_svn_reporter = {
  ra_svn_set_path,
  ra_svn_delete_path,
  ra_svn_link_path,
  ra_svn_finish_report,
  ra_svn_abort_report
};

static void ra_svn_get_reporter(ra_svn_session_baton_t *sess_baton,
                                apr_pool_t *pool,
                                const svn_delta_editor_t *editor,
                                void *edit_baton,
                                const svn_ra_reporter2_t **reporter,
                                void **report_baton)
{
  ra_svn_reporter_baton_t *b;

  b = apr_palloc(pool, sizeof(*b));
  b->sess_baton = sess_baton;
  b->conn = sess_baton->conn;
  b->pool = pool;
  b->editor = editor;
  b->edit_baton = edit_baton;

  *reporter = &ra_svn_reporter;
  *report_baton = b;
}

/* --- RA LAYER IMPLEMENTATION --- */

/* (Note: *ARGV is an output parameter.) */
static svn_error_t *find_tunnel_agent(const char *tunnel, 
                                      const char *hostinfo,
                                      const char ***argv,
                                      apr_hash_t *config, apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *val, *var, *cmd;
  char **cmd_argv;
  apr_size_t len;
  apr_status_t status;
  int n;

  /* Look up the tunnel specification in config. */
  cfg = config ? apr_hash_get(config, SVN_CONFIG_CATEGORY_CONFIG,
                              APR_HASH_KEY_STRING) : NULL;
  svn_config_get(cfg, &val, SVN_CONFIG_SECTION_TUNNELS, tunnel, NULL);

  /* We have one predefined tunnel scheme, if it isn't overridden by config. */
  if (!val && strcmp(tunnel, "ssh") == 0)
    val = "$SVN_SSH ssh";

  if (!val || !*val)
    return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                             _("Undefined tunnel scheme '%s'"), tunnel);

  /* If the scheme definition begins with "$varname", it means there
   * is an environment variable which can override the command. */
  if (*val == '$')
    {
      val++;
      len = strcspn(val, " ");
      var = apr_pstrmemdup(pool, val, len);
      cmd = getenv(var);
      if (!cmd)
        {
          cmd = val + len;
          while (*cmd == ' ')
            cmd++;
          if (!*cmd)
            return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                                     _("Tunnel scheme %s requires environment "
                                       "variable %s to be defined"), tunnel,
                                     var);
        }
    }
  else
    cmd = val;

  /* Tokenize the command into a list of arguments. */
  status = apr_tokenize_to_argv(cmd, &cmd_argv, pool);
  if (status != APR_SUCCESS)
    return svn_error_wrap_apr(status, _("Can't tokenize command '%s'"), cmd);

  /* Append the fixed arguments to the result. */
  for (n = 0; cmd_argv[n] != NULL; n++)
    ;
  *argv = apr_palloc(pool, (n + 4) * sizeof(char *));
  memcpy(*argv, cmd_argv, n * sizeof(char *));
  (*argv)[n++] = svn_path_uri_decode(hostinfo, pool);
  (*argv)[n++] = "svnserve";
  (*argv)[n++] = "-t";
  (*argv)[n] = NULL;

  return SVN_NO_ERROR;
}

/* This function handles any errors which occur in the child process
 * created for a tunnel agent.  We write the error out as a command
 * failure; the code in ra_svn_open() to read the server's greeting
 * will see the error and return it to the caller. */
static void handle_child_process_error(apr_pool_t *pool, apr_status_t status,
                                       const char *desc)
{
  svn_ra_svn_conn_t *conn;
  apr_file_t *in_file, *out_file;
  svn_error_t *err;

  if (apr_file_open_stdin(&in_file, pool)
      || apr_file_open_stdout(&out_file, pool))
    return;

  conn = svn_ra_svn_create_conn(NULL, in_file, out_file, pool);
  err = svn_error_wrap_apr(status, _("Error in child process: %s"), desc);
  svn_error_clear(svn_ra_svn_write_cmd_failure(conn, pool, err));
  svn_error_clear(svn_ra_svn_flush(conn, pool));
}

/* (Note: *CONN is an output parameter.) */
static svn_error_t *make_tunnel(const char **args, svn_ra_svn_conn_t **conn,
                                apr_pool_t *pool)
{
  apr_status_t status;
  apr_proc_t *proc;
  apr_procattr_t *attr;

  status = apr_procattr_create(&attr, pool);
  if (status == APR_SUCCESS)
    status = apr_procattr_io_set(attr, 1, 1, 0);
  if (status == APR_SUCCESS)
    status = apr_procattr_cmdtype_set(attr, APR_PROGRAM_PATH);
  if (status == APR_SUCCESS)
    status = apr_procattr_child_errfn_set(attr, handle_child_process_error);
  proc = apr_palloc(pool, sizeof(*proc));
  if (status == APR_SUCCESS)
    status = apr_proc_create(proc, *args, args, NULL, attr, pool);
  if (status != APR_SUCCESS)
    return svn_error_wrap_apr(status, _("Can't create tunnel"));

  /* Arrange for the tunnel agent to get a SIGKILL on pool
   * cleanup.  This is a little extreme, but the alternatives
   * weren't working out:
   *   - Closing the pipes and waiting for the process to die
   *     was prone to mysterious hangs which are difficult to
   *     diagnose (e.g. svnserve dumps core due to unrelated bug;
   *     sshd goes into zombie state; ssh connection is never
   *     closed; ssh never terminates).
   *   - Killing the tunnel agent with SIGTERM leads to unsightly
   *     stderr output from ssh.
   */
  apr_pool_note_subprocess(pool, proc, APR_KILL_ALWAYS);

  /* APR pipe objects inherit by default.  But we don't want the
   * tunnel agent's pipes held open by future child processes
   * (such as other ra_svn sessions), so turn that off. */
  apr_file_inherit_unset(proc->in);
  apr_file_inherit_unset(proc->out);

  /* Guard against dotfile output to stdout on the server. */
  *conn = svn_ra_svn_create_conn(NULL, proc->out, proc->in, pool);
  (*conn)->proc = proc;
  SVN_ERR(svn_ra_svn_skip_leading_garbage(*conn, pool));
  return SVN_NO_ERROR;
}

/* Parse URL inot URI, validating it and setting the default port if none
   was given.  Allocate the URI fileds out of POOL. */
static svn_error_t *parse_url(const char *url, apr_uri_t *uri,
                              apr_pool_t *pool)
{
  apr_status_t apr_err;

  apr_err = apr_uri_parse(pool, url, uri);
  
  if (apr_err != 0)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("Illegal svn repository URL '%s'"), url);
  
  if (! uri->port)
    uri->port = SVN_RA_SVN_PORT;

  return SVN_NO_ERROR;
}

/* Open a session to URL, returning it in *SESS_P, allocating it in POOL.
   URI is a parsed version of URL.  AUTH_BATON is provided by the caller of
   ra_svn_open.  If tunnel_argv is non-null, it points to a program
   argument list to use when invoking the tunnel agent. */
static svn_error_t *open_session(ra_svn_session_baton_t **sess_p,
                                 const char *url,
                                 const apr_uri_t *uri,
                                 svn_auth_baton_t *auth_baton,
                                 const char **tunnel_argv,
                                 apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess;
  svn_ra_svn_conn_t *conn;
  apr_socket_t *sock;
  apr_uint64_t minver, maxver;
  apr_array_header_t *mechlist, *caplist;
  
  if (tunnel_argv)
    SVN_ERR(make_tunnel(tunnel_argv, &conn, pool));
  else
    {
      SVN_ERR(make_connection(uri->hostname, uri->port, &sock, pool));
      conn = svn_ra_svn_create_conn(sock, NULL, NULL, pool);
    }

  /* Read server's greeting. */
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "nnll", &minver, &maxver,
                                       &mechlist, &caplist));
  /* We support protocol versions 1 and 2. */
  if (minver > 2)
    return svn_error_createf(SVN_ERR_RA_SVN_BAD_VERSION, NULL,
                             _("Server requires minimum version %d"),
                             (int) minver);
  SVN_ERR(svn_ra_svn_set_capabilities(conn, caplist));

  sess = apr_palloc(pool, sizeof(*sess));
  sess->pool = pool;
  sess->conn = conn;
  sess->protocol_version = (maxver > 2) ? 2 : maxver;
  sess->is_tunneled = (tunnel_argv != NULL);
  sess->auth_baton = auth_baton;
  sess->user = uri->user;
  sess->realm_prefix = apr_psprintf(pool, "<svn://%s:%d>", uri->hostname,
                                    uri->port);
  sess->tunnel_argv = tunnel_argv;

  /* In protocol version 2, we send back our protocol version, our
   * capability list, and the URL, and subsequently there is an auth
   * request.  In version 1, we send back the protocol version, auth
   * mechanism, mechanism initial response, and capability list, and;
   * then send the URL after authentication.  do_auth temporarily has
   * support for the mixed-style response. */
  /* When we punt support for protocol version 1, we should:
   * - Eliminate this conditional and the similar one below
   * - Remove v1 support from auth_response and inline it into do_auth
   * - Remove the (realm == NULL) support from do_auth
   * - Inline do_auth into handle_auth_request
   * - Remove the protocol version check from handle_auth_request */
  if (sess->protocol_version == 1)
    {
      SVN_ERR(do_auth(sess, mechlist, NULL, pool));
      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "c", url));
    }
  else
    {
      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "n(www)c", (apr_uint64_t) 2,
                                     SVN_RA_SVN_CAP_EDIT_PIPELINE,
                                     SVN_RA_SVN_CAP_SVNDIFF1,
                                     SVN_RA_SVN_CAP_ABSENT_ENTRIES, url));
      SVN_ERR(handle_auth_request(sess, pool));
    }

  /* This is where the security layer would go into effect if we
   * supported security layers, which is a ways off. */

  /* Read the repository's uuid and root URL. */
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "c?c", &conn->uuid,
                                       &conn->repos_root));
  if (conn->repos_root)
    {
      conn->repos_root = svn_path_canonicalize(conn->repos_root, pool);
      /* We should check that the returned string is a prefix of url, since
         that's the API guarantee, but this isn't true for 1.0 servers.
         Checking the length prevents client crashes. */
      if (strlen(conn->repos_root) > strlen(url))
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Impossibly long repository root from "
                                  "server"));
    }

  *sess_p = sess;

  return SVN_NO_ERROR;
}


#define RA_SVN_DESCRIPTION \
  N_("Module for accessing a repository using the svn network protocol.")

static const char *ra_svn_get_description(void)
{
  return _(RA_SVN_DESCRIPTION);
}

static const char * const *
ra_svn_get_schemes(apr_pool_t *pool)
{
  static const char *schemes[] = { "svn", NULL };

  return schemes;
}



static svn_error_t *ra_svn_open(svn_ra_session_t *session, const char *url,
                                const svn_ra_callbacks2_t *callbacks,
                                void *callback_baton,
                                apr_hash_t *config,
                                apr_pool_t *pool)
{
  apr_pool_t *sess_pool = svn_pool_create(pool);
  ra_svn_session_baton_t *sess;
  const char *tunnel, **tunnel_argv;
  apr_uri_t uri;
  
  SVN_ERR(parse_url(url, &uri, sess_pool));

  parse_tunnel(url, &tunnel, pool);

  if (tunnel)
    SVN_ERR(find_tunnel_agent(tunnel, uri.hostinfo, &tunnel_argv, config,
                              pool));
  else
    tunnel_argv = NULL;

  /* We open the session in a subpool so we can get rid of it if we
     reparent with a server that doesn't support reparenting. */
  SVN_ERR(open_session(&sess, url, &uri, callbacks->auth_baton, tunnel_argv,
                       sess_pool));
  session->priv = sess;

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_reparent(svn_ra_session_t *ra_session,
                                    const char *url,
                                    apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess = ra_session->priv;
  svn_ra_svn_conn_t *conn = sess->conn;
  svn_error_t *err;
  apr_pool_t *sess_pool;
  ra_svn_session_baton_t *new_sess;
  apr_uri_t uri;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "reparent", "c", url));
  err = handle_auth_request(sess, pool);
  if (! err)
    return svn_ra_svn_read_cmd_response(conn, pool, "");
  else if (err->apr_err != SVN_ERR_RA_SVN_UNKNOWN_CMD)
    return err;
  
  /* Servers before 1.4 doesn't support this command; try to reconnect
     instead. */
  svn_error_clear(err);
  /* Create a new subpool of the RA session pool. */
  sess_pool = svn_pool_create(ra_session->pool);
  err = parse_url(url, &uri, sess_pool);
  if (! err)
    err = open_session(&new_sess, url, &uri, sess->auth_baton,
                       sess->tunnel_argv, sess_pool);
  /* We destroy the new session pool on error, since it is allocated in
     the main session pool. */
  if (err)
    {
      svn_pool_destroy(sess_pool);
      return err;
    }

  /* We have a new connection, assign it and destroy the old. */
  ra_session->priv = new_sess;
  svn_pool_destroy(sess->pool);

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_latest_rev(svn_ra_session_t *session,
                                          svn_revnum_t *rev, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-latest-rev", ""));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_dated_rev(svn_ra_session_t *session,
                                         svn_revnum_t *rev, apr_time_t tm,
                                         apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-dated-rev", "c",
                               svn_time_to_cstring(tm, pool)));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_change_rev_prop(svn_ra_session_t *session, svn_revnum_t rev,
                                           const char *name,
                                           const svn_string_t *value,
                                           apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "change-rev-prop", "rc?s",
                               rev, name, value));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_uuid(svn_ra_session_t *session, const char **uuid,
                                    apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  *uuid = conn->uuid;
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_repos_root(svn_ra_session_t *session, const char **url,
                                          apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  if (!conn->repos_root)
    return svn_error_create(SVN_ERR_RA_SVN_BAD_VERSION, NULL,
                            _("Server did not send repository root"));
  *url = conn->repos_root;
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_rev_proplist(svn_ra_session_t *session, svn_revnum_t rev,
                                        apr_hash_t **props, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  apr_array_header_t *proplist;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "rev-proplist", "r", rev));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "l", &proplist));
  SVN_ERR(parse_proplist(proplist, pool, props));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_rev_prop(svn_ra_session_t *session, svn_revnum_t rev,
                                    const char *name,
                                    svn_string_t **value, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "rev-prop", "rc", rev, name));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "(?s)", value));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_end_commit(void *baton)
{
  ra_svn_commit_callback_baton_t *ccb = baton;
  svn_commit_info_t *commit_info = svn_create_commit_info(ccb->pool);

  SVN_ERR(handle_auth_request(ccb->sess_baton, ccb->pool));
  SVN_ERR(svn_ra_svn_read_tuple(ccb->sess_baton->conn, ccb->pool,
                                "r(?c)(?c)?(?c)",
                                 &(commit_info->revision),
                                 &(commit_info->date),
                                 &(commit_info->author),
                                 &(commit_info->post_commit_err)));

  return ccb->callback(commit_info, ccb->callback_baton, ccb->pool);

}

static svn_error_t *ra_svn_commit(svn_ra_session_t *session,
                                  const svn_delta_editor_t **editor,
                                  void **edit_baton,
                                   const char *log_msg,
                                  svn_commit_callback2_t callback,
                                  void *callback_baton,
                                  apr_hash_t *lock_tokens,
                                  svn_boolean_t keep_locks,
                                  apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  ra_svn_commit_callback_baton_t *ccb;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  /* Tell the server we're starting the commit. */
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w(c(!", "commit", log_msg));
  if (lock_tokens)
    {
      iterpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, lock_tokens); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *path, *token;
          svn_pool_clear(iterpool);
          apr_hash_this(hi, &key, NULL, &val);
          path = key;
          token = val;          
          SVN_ERR(svn_ra_svn_write_tuple(conn, iterpool, "cc", path, token));
        }
      svn_pool_destroy(iterpool);
    }
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!)b)", keep_locks));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  /* Remember a few arguments for when the commit is over. */
  ccb = apr_palloc(pool, sizeof(*ccb));
  ccb->sess_baton = sess_baton;
  ccb->pool = pool;
  ccb->callback = callback;
  ccb->callback_baton = callback_baton;

  /* Fetch an editor for the caller to drive.  The editor will call
   * ra_svn_end_commit() upon close_edit(), at which point we'll fill
   * in the new_rev, committed_date, and committed_author values. */
  svn_ra_svn_get_editor(editor, edit_baton, conn, pool,
                        ra_svn_end_commit, ccb);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_file(svn_ra_session_t *session, const char *path,
                                    svn_revnum_t rev, svn_stream_t *stream,
                                    svn_revnum_t *fetched_rev,
                                    apr_hash_t **props,
                                    apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_ra_svn_item_t *item;
  apr_array_header_t *proplist;
  unsigned char digest[APR_MD5_DIGESTSIZE];
  const char *expected_checksum, *hex_digest;
  apr_md5_ctx_t md5_context;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-file", "c(?r)bb", path,
                               rev, (props != NULL), (stream != NULL)));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "(?c)rl",
                                       &expected_checksum,
                                       &rev, &proplist));

  if (fetched_rev)
    *fetched_rev = rev;
  if (props)
    SVN_ERR(parse_proplist(proplist, pool, props));

  /* We're done if the contents weren't wanted. */
  if (!stream)
    return SVN_NO_ERROR;

  if (expected_checksum)
    apr_md5_init(&md5_context);

  /* Read the file's contents. */
  while (1)
    {
      SVN_ERR(svn_ra_svn_read_item(conn, pool, &item));
      if (item->kind != SVN_RA_SVN_STRING)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Non-string as part of file contents"));
      if (item->u.string->len == 0)
        break;

      if (expected_checksum)
        apr_md5_update(&md5_context, item->u.string->data,
                       item->u.string->len);

      SVN_ERR(svn_stream_write(stream, item->u.string->data,
                               &item->u.string->len));
    }
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  if (expected_checksum)
    {
      apr_md5_final(digest, &md5_context);
      hex_digest = svn_md5_digest_to_cstring_display(digest, pool);
      if (strcmp(hex_digest, expected_checksum) != 0)
        return svn_error_createf
          (SVN_ERR_CHECKSUM_MISMATCH, NULL,
           _("Checksum mismatch for '%s':\n"
             "   expected checksum:  %s\n"
             "   actual checksum:    %s\n"),
           path, expected_checksum, hex_digest);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_dir(svn_ra_session_t *session,
                                   apr_hash_t **dirents,
                                   svn_revnum_t *fetched_rev,
                                   apr_hash_t **props,
                                   const char *path,
                                   svn_revnum_t rev,
                                   apr_uint32_t dirent_fields,
                                   apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_revnum_t crev;
  apr_array_header_t *proplist, *dirlist;
  int i;
  svn_ra_svn_item_t *elt;
  const char *name, *kind, *cdate, *cauthor;
  svn_boolean_t has_props;
  apr_uint64_t size;
  svn_dirent_t *dirent;

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w(c(?r)bb(!", "get-dir", path,
                                 rev, (props != NULL), (dirents != NULL)));
  if (dirent_fields & SVN_DIRENT_KIND)
    {
      SVN_ERR(svn_ra_svn_write_word(conn, pool, SVN_RA_SVN_DIRENT_KIND));
    }
  if (dirent_fields & SVN_DIRENT_SIZE)
    {
      SVN_ERR(svn_ra_svn_write_word(conn, pool, SVN_RA_SVN_DIRENT_SIZE));
    }
  if (dirent_fields & SVN_DIRENT_HAS_PROPS)
    {
      SVN_ERR(svn_ra_svn_write_word(conn, pool, SVN_RA_SVN_DIRENT_HAS_PROPS));
    }
  if (dirent_fields & SVN_DIRENT_CREATED_REV)
    {
      SVN_ERR(svn_ra_svn_write_word(conn, pool,
                                    SVN_RA_SVN_DIRENT_CREATED_REV));
    }
  if (dirent_fields & SVN_DIRENT_TIME)
    {
      SVN_ERR(svn_ra_svn_write_word(conn, pool, SVN_RA_SVN_DIRENT_TIME));
    }
  if (dirent_fields & SVN_DIRENT_LAST_AUTHOR)
    {
      SVN_ERR(svn_ra_svn_write_word(conn, pool,
                                    SVN_RA_SVN_DIRENT_LAST_AUTHOR));
    }
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!))"));

  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "rll", &rev, &proplist,
                                       &dirlist));

  if (fetched_rev)
    *fetched_rev = rev;
  if (props)
    SVN_ERR(parse_proplist(proplist, pool, props));

  /* We're done if dirents aren't wanted. */
  if (!dirents)
    return SVN_NO_ERROR;

  /* Interpret the directory list. */
  *dirents = apr_hash_make(pool);
  for (i = 0; i < dirlist->nelts; i++)
    {
      elt = &((svn_ra_svn_item_t *) dirlist->elts)[i];
      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Dirlist element not a list"));
      SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, pool, "cwnbr(?c)(?c)",
                                     &name, &kind, &size, &has_props,
                                     &crev, &cdate, &cauthor));
      name = svn_path_canonicalize(name, pool);
      dirent = apr_palloc(pool, sizeof(*dirent));
      SVN_ERR(interpret_kind(kind, pool, &dirent->kind));
      dirent->size = size;/* FIXME: svn_filesize_t */
      dirent->has_props = has_props;
      dirent->created_rev = crev;
      SVN_ERR(svn_time_from_cstring(&dirent->time, cdate, pool));
      dirent->last_author = cauthor;
      apr_hash_set(*dirents, name, APR_HASH_KEY_STRING, dirent);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_update(svn_ra_session_t *session,
                                  const svn_ra_reporter2_t **reporter,
                                  void **report_baton, svn_revnum_t rev,
                                  const char *target, svn_boolean_t recurse,
                                  const svn_delta_editor_t *update_editor,
                                  void *update_baton, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  /* Tell the server we want to start an update. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "update", "(?r)cb", rev, target,
                               recurse));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * update_editor upon finish_report(). */
  ra_svn_get_reporter(sess_baton, pool, update_editor, update_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_switch(svn_ra_session_t *session,
                                  const svn_ra_reporter2_t **reporter,
                                  void **report_baton, svn_revnum_t rev,
                                  const char *target, svn_boolean_t recurse,
                                  const char *switch_url,
                                  const svn_delta_editor_t *update_editor,
                                  void *update_baton, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  /* Tell the server we want to start a switch. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "switch", "(?r)cbc", rev, target,
                               recurse, switch_url));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * update_editor upon finish_report(). */
  ra_svn_get_reporter(sess_baton, pool, update_editor, update_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_status(svn_ra_session_t *session,
                                  const svn_ra_reporter2_t **reporter,
                                  void **report_baton,
                                  const char *target, svn_revnum_t rev,
                                  svn_boolean_t recurse,
                                  const svn_delta_editor_t *status_editor,
                                  void *status_baton, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  /* Tell the server we want to start a status operation. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "status", "cb(?r)",
                               target, recurse, rev));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * status_editor upon finish_report(). */
  ra_svn_get_reporter(sess_baton, pool, status_editor, status_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_diff(svn_ra_session_t *session,
                                const svn_ra_reporter2_t **reporter,
                                void **report_baton,
                                svn_revnum_t rev, const char *target,
                                svn_boolean_t recurse,
                                svn_boolean_t ignore_ancestry,
                                svn_boolean_t text_deltas,
                                const char *versus_url,
                                const svn_delta_editor_t *diff_editor,
                                void *diff_baton, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  /* Tell the server we want to start a diff. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "diff", "(?r)cbbcb", rev,
                               target, recurse, ignore_ancestry,
                               versus_url, text_deltas));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * diff_editor upon finish_report(). */
  ra_svn_get_reporter(sess_baton, pool, diff_editor, diff_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_log(svn_ra_session_t *session,
                               const apr_array_header_t *paths,
                               svn_revnum_t start, svn_revnum_t end,
                               int limit,
                               svn_boolean_t discover_changed_paths,
                               svn_boolean_t strict_node_history,
                               svn_log_message_receiver_t receiver,
                               void *receiver_baton, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  apr_pool_t *subpool;
  int i;
  const char *path, *author, *date, *message, *cpath, *action, *copy_path;
  svn_ra_svn_item_t *item, *elt;
  apr_array_header_t *cplist;
  apr_hash_t *cphash;
  svn_revnum_t rev, copy_rev;
  svn_log_changed_path_t *change;
  int nreceived = 0;

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w((!", "log"));
  if (paths)
    {
      for (i = 0; i < paths->nelts; i++)
        {
          path = ((const char **) paths->elts)[i];
          SVN_ERR(svn_ra_svn_write_cstring(conn, pool, path));
        }
    }
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!)(?r)(?r)bbn)", start, end,
                                 discover_changed_paths, strict_node_history,
                                 (apr_uint64_t) limit));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Read the log messages. */
  subpool = svn_pool_create(pool);
  while (1)
    {
      SVN_ERR(svn_ra_svn_read_item(conn, subpool, &item));
      if (item->kind == SVN_RA_SVN_WORD && strcmp(item->u.word, "done") == 0)
        break;
      if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Log entry not a list"));
      SVN_ERR(svn_ra_svn_parse_tuple(item->u.list, subpool, "lr(?c)(?c)(?c)",
                                     &cplist, &rev, &author, &date,
                                     &message));
      if (cplist->nelts > 0)
        {
          /* Interpret the changed-paths list. */
          cphash = apr_hash_make(subpool);
          for (i = 0; i < cplist->nelts; i++)
            {
              elt = &((svn_ra_svn_item_t *) cplist->elts)[i];
              if (elt->kind != SVN_RA_SVN_LIST)
                return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                        _("Changed-path entry not a list"));
              SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, subpool, "cw(?cr)",
                                             &cpath, &action, &copy_path,
                                             &copy_rev));
              cpath = svn_path_canonicalize(cpath, subpool);
              if (copy_path)
                copy_path = svn_path_canonicalize(copy_path, subpool);
              change = apr_palloc(subpool, sizeof(*change));
              change->action = *action;
              change->copyfrom_path = copy_path;
              change->copyfrom_rev = copy_rev;
              apr_hash_set(cphash, cpath, APR_HASH_KEY_STRING, change);
            }
        }
      else
        cphash = NULL;

      if (! (limit && ++nreceived > limit))
        SVN_ERR(receiver(receiver_baton, cphash, rev, author, date, message,
                         subpool));

      apr_pool_clear(subpool);
    }
  apr_pool_destroy(subpool);

  /* Read the response. */
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  return SVN_NO_ERROR;
}


static svn_error_t *ra_svn_check_path(svn_ra_session_t *session,
                                      const char *path, svn_revnum_t rev,
                                      svn_node_kind_t *kind, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  const char *kind_word;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "check-path", "c(?r)", path, rev));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "w", &kind_word));
  SVN_ERR(interpret_kind(kind_word, pool, kind));
  return SVN_NO_ERROR;
}


/* If ERR is a command not supported error, wrap it in a
   SVN_ERR_RA_NOT_IMPLEMENTED with error message MSG.  Else, return err. */
static svn_error_t *handle_unsupported_cmd(svn_error_t *err,
                                           const char *msg)
{
  if (err && err->apr_err == SVN_ERR_RA_SVN_UNKNOWN_CMD)
    return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, err,
                            msg);
  return err;
}


static svn_error_t *ra_svn_stat(svn_ra_session_t *session,
                                const char *path, svn_revnum_t rev,
                                svn_dirent_t **dirent, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  apr_array_header_t *list = NULL;
  const char *kind, *cdate, *cauthor;
  svn_revnum_t crev;
  svn_boolean_t has_props;
  apr_uint64_t size;
  svn_dirent_t *the_dirent;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "stat", "c(?r)", path, rev));

  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess_baton, pool),
                                 _("'stat' not implemented")));

  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "(?l)", &list));

  if (! list)
    {
      *dirent = NULL;
    }
  else
    {
      SVN_ERR(svn_ra_svn_parse_tuple(list, pool, "wnbr(?c)(?c)",
                                     &kind, &size, &has_props,
                                     &crev, &cdate, &cauthor));
      
      the_dirent = apr_palloc(pool, sizeof(*the_dirent));
      SVN_ERR(interpret_kind(kind, pool, &the_dirent->kind));
      the_dirent->size = size;/* FIXME: svn_filesize_t */
      the_dirent->has_props = has_props;
      the_dirent->created_rev = crev;
      SVN_ERR(svn_time_from_cstring(&the_dirent->time, cdate, pool));
      the_dirent->last_author = cauthor;

      *dirent = the_dirent;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *ra_svn_get_locations(svn_ra_session_t *session,
                                         apr_hash_t **locations,
                                         const char *path,
                                         svn_revnum_t peg_revision,
                                         apr_array_header_t *location_revisions,
                                         apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_revnum_t revision;
  svn_ra_svn_item_t *item;
  svn_boolean_t is_done;
  int i;
  const char *ret_path;

  /* Transmit the parameters. */
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w(cr(!",
                                 "get-locations", path, peg_revision));
  for (i = 0; i < location_revisions->nelts; i++)
    {
      revision = ((svn_revnum_t *)location_revisions->elts)[i];
      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!r!", revision));
    }

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!))"));

  /* Servers before 1.1 don't support this command. Check for this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess_baton, pool),
                                 _("'get-locations' not implemented")));

  /* Read the hash items. */
  is_done = FALSE;
  *locations = apr_hash_make(pool);
  while (!is_done)
    {
      SVN_ERR(svn_ra_svn_read_item(conn, pool, &item));
      if (item->kind == SVN_RA_SVN_WORD && strcmp(item->u.word, "done") == 0)
        is_done = 1;
      else if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Location entry not a list"));
      else
        {
          SVN_ERR(svn_ra_svn_parse_tuple(item->u.list, pool, "rc",
                                         &revision, &ret_path));
          ret_path = svn_path_canonicalize(ret_path, pool);
          apr_hash_set(*locations, apr_pmemdup(pool, &revision,
                                               sizeof(revision)),
                       sizeof(revision), ret_path);
        }
    }

  /* Read the response. This is so the server would have a chance to
   * report an error. */
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_file_revs(svn_ra_session_t *session,
                                         const char *path,
                                         svn_revnum_t start, svn_revnum_t end,
                                         svn_ra_file_rev_handler_t handler,
                                         void *handler_baton, apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess_baton = session->priv;
  apr_pool_t *rev_pool, *chunk_pool;
  svn_ra_svn_item_t *item;
  const char *p;
  svn_revnum_t rev;
  apr_array_header_t *rev_proplist, *proplist;
  apr_hash_t *rev_props;
  apr_array_header_t *props;
  svn_boolean_t has_txdelta;
  svn_boolean_t had_revision = FALSE;
  svn_stream_t *stream;
  svn_txdelta_window_handler_t d_handler;
  void *d_baton;
  apr_size_t size;

  /* One sub-pool for each revision and one for each txdelta chunk.
     Note that the rev_pool must live during the following txdelta. */
  rev_pool = svn_pool_create(pool);
  chunk_pool = svn_pool_create(pool);

  SVN_ERR(svn_ra_svn_write_cmd(sess_baton->conn, pool, "get-file-revs",
                               "c(?r)(?r)", path, start, end));

  /* Servers before 1.1 don't support this command.  Check for this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess_baton, pool),
                                 _("'get-file-revs' not implemented")));

  while (1)
    {
      svn_pool_clear(rev_pool);
      svn_pool_clear(chunk_pool);
      SVN_ERR(svn_ra_svn_read_item(sess_baton->conn, rev_pool, &item));
      if (item->kind == SVN_RA_SVN_WORD && strcmp(item->u.word, "done") == 0)
        break;
      /* Either we've got a correct revision or we will error out below. */
      had_revision = TRUE;
      if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Revision entry not a list"));

      SVN_ERR(svn_ra_svn_parse_tuple(item->u.list, rev_pool,
                                     "crll", &p, &rev, &rev_proplist,
                                     &proplist));
      p = svn_path_canonicalize(p, rev_pool);
      SVN_ERR(parse_proplist(rev_proplist, rev_pool, &rev_props));
      SVN_ERR(parse_prop_diffs(proplist, rev_pool, &props));

      /* Get the first delta chunk so we know if there is a delta. */
      SVN_ERR(svn_ra_svn_read_item(sess_baton->conn, chunk_pool, &item));
      if (item->kind != SVN_RA_SVN_STRING)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Text delta chunk not a string"));
      has_txdelta = item->u.string->len > 0;

      SVN_ERR(handler(handler_baton, p, rev, rev_props,
                      has_txdelta ? &d_handler : NULL, &d_baton,
                      props, rev_pool));

      /* Process the text delta if any. */
      if (has_txdelta)
        {
          if (d_handler)
            stream = svn_txdelta_parse_svndiff(d_handler, d_baton, TRUE,
                                               rev_pool);
          else
            stream = NULL;
          while (item->u.string->len > 0)
            {
              size = item->u.string->len;
              if (stream)
                SVN_ERR(svn_stream_write(stream, item->u.string->data, &size));
              svn_pool_clear(chunk_pool);

              SVN_ERR(svn_ra_svn_read_item(sess_baton->conn, chunk_pool, &item));
              if (item->kind != SVN_RA_SVN_STRING)
                return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                        _("Text delta chunk not a string"));
            }
          if (stream)
            SVN_ERR(svn_stream_close(stream));
        }
    }
 
  SVN_ERR(svn_ra_svn_read_cmd_response(sess_baton->conn, pool, ""));

  /* Return error if we didn't get any revisions. */
  if (!had_revision)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("The get-file-revs command didn't return "
                              "any revisions"));

  svn_pool_destroy(chunk_pool);
  svn_pool_destroy(rev_pool);

  return SVN_NO_ERROR;
}

/* For each path in PATH_REVS, send a 'lock' command to the server.
   Used with 1.2.x series servers which support locking, but of only
   one path at a time.  ra_svn_lock(), which supports 'lock-many'
   is now the default.  See svn_ra_lock() docstring for interface details. */
static svn_error_t *ra_svn_lock_compat(svn_ra_session_t *session,
                                       apr_hash_t *path_revs,
                                       const char *comment,
                                       svn_boolean_t steal_lock,
                                       svn_ra_lock_callback_t lock_func,
                                       void *lock_baton,
                                       apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t* conn = sess->conn;
  apr_array_header_t *list;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      svn_lock_t *lock;
      const void *key;
      const char *path;
      void *val;
      svn_revnum_t *revnum;
      svn_error_t *err, *callback_err = NULL;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      revnum = val;

      SVN_ERR(svn_ra_svn_write_cmd(conn, iterpool, "lock", "c(?c)b(?r)", 
                                   path, comment,
                                   steal_lock, *revnum));

      /* Servers before 1.2 doesn't support locking.  Check this here. */
      SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                     _("Server doesn't support "
                                       "the lock command")));

      err = svn_ra_svn_read_cmd_response(conn, iterpool, "l", &list);

      if (!err)
        SVN_ERR(parse_lock(list, iterpool, &lock));

      if (err && !SVN_ERR_IS_LOCK_ERROR(err))
        return err;

      if (lock_func)
        callback_err = lock_func(lock_baton, path, TRUE, err ? NULL : lock,
                                 err, iterpool);

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* For each path in PATH_TOKENS, send an 'unlock' command to the server.
   Used with 1.2.x series servers which support unlocking, but of only
   one path at a time.  ra_svn_unlock(), which supports 'unlock-many' is
   now the default.  See svn_ra_unlock() docstring for interface details. */
static svn_error_t *ra_svn_unlock_compat(svn_ra_session_t *session,
                                         apr_hash_t *path_tokens,
                                         svn_boolean_t break_lock,
                                         svn_ra_lock_callback_t lock_func, 
                                         void *lock_baton,
                                         apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t* conn = sess->conn;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *path;
      void *val;
      const char *token;
      svn_error_t *err, *callback_err = NULL;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      if (strcmp(val, "") != 0)
        token = val;
      else
        token = NULL;

      SVN_ERR(svn_ra_svn_write_cmd(conn, iterpool, "unlock", "c(?c)b",
                                   path, token, break_lock));

      /* Servers before 1.2 don't support locking.  Check this here. */
      SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, iterpool),
                                     _("Server doesn't support the unlock "
                                       "command")));

      err = svn_ra_svn_read_cmd_response(conn, iterpool, "");

      if (err && !SVN_ERR_IS_UNLOCK_ERROR(err))
        return err;

      if (lock_func)
        callback_err = lock_func(lock_baton, path, FALSE, NULL, err, pool);

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Tell the server to lock all paths in PATH_REVS.
   See svn_ra_lock() for interface details. */
static svn_error_t *ra_svn_lock(svn_ra_session_t *session,
                                apr_hash_t *path_revs,
                                const char *comment,
                                svn_boolean_t steal_lock,
                                svn_ra_lock_callback_t lock_func, 
                                void *lock_baton,
                                apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t *conn = sess->conn;
  apr_hash_index_t *hi;
  svn_ra_svn_item_t *elt;
  svn_error_t *err, *callback_err = SVN_NO_ERROR;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *status;
  svn_lock_t *lock;
  apr_array_header_t *list = NULL;

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w((?c)b(!", "lock-many",
                                 comment, steal_lock));

  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *path;
      void *val;
      svn_revnum_t *revnum;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      revnum = val;

      SVN_ERR(svn_ra_svn_write_tuple(conn, subpool, "c(?r)", path, *revnum));
    }

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!))"));

  err = handle_auth_request(sess, pool);

  /* Pre-1.3 servers don't support 'lock-many'. If that fails, fall back
   * to 'lock'. */
  if (err && err->apr_err == SVN_ERR_RA_SVN_UNKNOWN_CMD)
    {
      svn_error_clear(err);
      return ra_svn_lock_compat(session, path_revs, comment, steal_lock,
                                lock_func, lock_baton, pool);
    }

  if (err)
    return err;

  /* Loop over responses to get lock information. */
  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *path;

      apr_hash_this(hi, &key, NULL, NULL);
      path = key;

      svn_pool_clear(subpool);
      SVN_ERR(svn_ra_svn_read_item(conn, subpool, &elt));

      /* The server might have encountered some sort of fatal error in
         the middle of the request list.  If this happens, it will
         transmit "done" to end the lock-info early, and then the
         overall command response will talk about the fatal error. */
      if (elt->kind == SVN_RA_SVN_WORD && strcmp(elt->u.word, "done") == 0)
        break;

      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Lock response not a list"));

      SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, subpool, "wl", &status,
                                     &list));

      if (strcmp(status, "failure") == 0)
        err = svn_ra_svn__handle_failure_status(list, subpool);
      else if (strcmp(status, "success") == 0)
        {
          SVN_ERR(parse_lock(list, subpool, &lock));
          err = NULL;
        }
      else
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Unknown status for lock command"));

      if (lock_func)
        callback_err = lock_func(lock_baton, path, TRUE,
                                 err ? NULL : lock,
                                 err, subpool);
      else
        callback_err = SVN_NO_ERROR;

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  /* If we didn't break early above, and the whole hash was traversed,
     read the final "done" from the server. */
  if (!hi)
    {
      SVN_ERR(svn_ra_svn_read_item(conn, pool, &elt));
      if (elt->kind != SVN_RA_SVN_WORD || strcmp(elt->u.word, "done") != 0)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Didn't receive end marker for lock "
                                  "responses"));
    }

  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Tell the server to unlock all paths in PATH_TOKENS.
   See svn_ra_unlock() for interface details. */
static svn_error_t *ra_svn_unlock(svn_ra_session_t *session,
                                  apr_hash_t *path_tokens,
                                  svn_boolean_t break_lock,
                                  svn_ra_lock_callback_t lock_func, 
                                  void *lock_baton,
                                  apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t *conn = sess->conn;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err, *callback_err = NULL;
  svn_ra_svn_item_t *elt;
  const char *status = NULL;
  apr_array_header_t *list = NULL;
  const void *key;
  const char *path;

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w(b(!", "unlock-many",
                                 break_lock));

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      void *val;
      const char *token;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);
      path = key;

      if (strcmp(val, "") != 0)
        token = val;
      else
        token = NULL;

      SVN_ERR(svn_ra_svn_write_tuple(conn, subpool, "c(?c)", path, token)); 
    }

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!))"));

  err = handle_auth_request(sess, pool);

  /* Pre-1.3 servers don't support 'unlock-many'. If unknown, fall back
   * to 'unlock'.
   */
  if (err && err->apr_err == SVN_ERR_RA_SVN_UNKNOWN_CMD)
    {
      svn_error_clear(err);
      return ra_svn_unlock_compat(session, path_tokens, break_lock, lock_func,
                                  lock_baton, pool);
    }

  if (err)
    return err;

  /* Loop over responses to unlock files. */
  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      svn_pool_clear(subpool);

      SVN_ERR(svn_ra_svn_read_item(conn, subpool, &elt));

      /* The server might have encountered some sort of fatal error in
         the middle of the request list.  If this happens, it will
         transmit "done" to end the lock-info early, and then the
         overall command response will talk about the fatal error. */
      if (elt->kind == SVN_RA_SVN_WORD && (strcmp(elt->u.word, "done") == 0))
        break;

      apr_hash_this(hi, &key, NULL, NULL);
      path = key;

      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Unlock response not a list"));

      SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, subpool, "wl", &status,
                                     &list));

      if (strcmp(status, "failure") == 0)
        err = svn_ra_svn__handle_failure_status(list, subpool);
      else if (strcmp(status, "success") == 0)
        {
          SVN_ERR(svn_ra_svn_parse_tuple(list, subpool, "c", &path));
          err = SVN_NO_ERROR;
        }
      else
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Unknown status for unlock command"));

      if (lock_func)
        callback_err = lock_func(lock_baton, path, FALSE, NULL, err,
                                 subpool);
      else
        callback_err = SVN_NO_ERROR;

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  /* If we didn't break early above, and the whole hash was traversed,
     read the final "done" from the server. */
  if (!hi)
    {
      SVN_ERR(svn_ra_svn_read_item(conn, pool, &elt));
      if (elt->kind != SVN_RA_SVN_WORD || strcmp(elt->u.word, "done") != 0)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Didn't receive end marker for unlock "
                                  "responses"));
    }

  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_lock(svn_ra_session_t *session,
                                    svn_lock_t **lock,
                                    const char *path,
                                    apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t* conn = sess->conn;
  apr_array_header_t *list;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-lock", "c", path));

  /* Servers before 1.2 doesn't support locking.  Check this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                 _("Server doesn't support the get-lock "
                                   "command")));

  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "(?l)", &list));
  if (list)
    SVN_ERR(parse_lock(list, pool, lock));
  else
    *lock = NULL;

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_locks(svn_ra_session_t *session,
                                     apr_hash_t **locks,
                                     const char *path,
                                     apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t* conn = sess->conn;
  apr_array_header_t *list;
  int i;
  svn_ra_svn_item_t *elt;
  svn_lock_t *lock;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-locks", "c", path));

  /* Servers before 1.2 doesn't support locking.  Check this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                 _("Server doesn't support the get-lock "
                                   "command")));

  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "l", &list));

  *locks = apr_hash_make(pool);

  for (i = 0; i < list->nelts; ++i)
    {
      elt = &APR_ARRAY_IDX(list, i, svn_ra_svn_item_t);

      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Lock element not a list"));
      SVN_ERR(parse_lock(elt->u.list, pool, &lock));
      apr_hash_set(*locks, lock->path, APR_HASH_KEY_STRING, lock);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *ra_svn_replay(svn_ra_session_t *session,
                                  svn_revnum_t revision,
                                  svn_revnum_t low_water_mark,
                                  svn_boolean_t send_deltas,
                                  const svn_delta_editor_t *editor,
                                  void *edit_baton,
                                  apr_pool_t *pool)
{
  ra_svn_session_baton_t *sess = session->priv;

  SVN_ERR(svn_ra_svn_write_cmd(sess->conn, pool, "replay", "rrb", revision,
                               low_water_mark, send_deltas));

  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                 _("Server doesn't support the replay "
                                   "command")));

  SVN_ERR(svn_ra_svn_drive_editor2(sess->conn, pool, editor, edit_baton,
                                   NULL, TRUE));

  SVN_ERR(svn_ra_svn_read_cmd_response(sess->conn, pool, ""));

  return SVN_NO_ERROR;
}


static const svn_ra__vtable_t ra_svn_vtable = {
  svn_ra_svn_version,
  ra_svn_get_description,
  ra_svn_get_schemes,
  ra_svn_open,
  ra_svn_reparent,
  ra_svn_get_latest_rev,
  ra_svn_get_dated_rev,
  ra_svn_change_rev_prop,
  ra_svn_rev_proplist,
  ra_svn_rev_prop,
  ra_svn_commit,
  ra_svn_get_file,
  ra_svn_get_dir,
  ra_svn_update,
  ra_svn_switch,
  ra_svn_status,
  ra_svn_diff,
  ra_svn_log,
  ra_svn_check_path,
  ra_svn_stat,
  ra_svn_get_uuid,
  ra_svn_get_repos_root,
  ra_svn_get_locations,
  ra_svn_get_file_revs,
  ra_svn_lock,
  ra_svn_unlock,
  ra_svn_get_lock,
  ra_svn_get_locks,
  ra_svn_replay,
};

svn_error_t *
svn_ra_svn__init(const svn_version_t *loader_version,
                 const svn_ra__vtable_t **vtable,
                 apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };
  
  SVN_ERR(svn_ver_check_list(svn_ra_svn_version(), checklist));

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    {
      return svn_error_createf
        (SVN_ERR_VERSION_MISMATCH, NULL,
         _("Unsupported RA loader version (%d) for ra_svn"),
         loader_version->major);
    }

  *vtable = &ra_svn_vtable;

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for the 1.1 and before API. */
#define NAME "ra_svn"
#define DESCRIPTION RA_SVN_DESCRIPTION
#define VTBL ra_svn_vtable
#define INITFUNC svn_ra_svn__init
#define COMPAT_INITFUNC svn_ra_svn_init
#include "../libsvn_ra/wrapper_template.h"
