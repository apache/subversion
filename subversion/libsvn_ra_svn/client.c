/*
 * client.c :  Functions for repository access via the Subversion protocol
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



#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_network_io.h>
#include <apr_md5.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_time.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_ra.h"
#include "svn_ra_svn.h"
#include "svn_md5.h"

#include "ra_svn.h"

typedef struct {
  svn_ra_svn_conn_t *conn;
  apr_pool_t *pool;
  svn_revnum_t *new_rev;
  const char **committed_date;
  const char **committed_author;
} ra_svn_commit_callback_baton_t;

typedef struct {
  svn_ra_svn_conn_t *conn;
  apr_pool_t *pool;
  const svn_delta_editor_t *editor;
  void *edit_baton;
} ra_svn_reporter_baton_t;

/* Parse an svn URL's authority section into tunnel, user, host, and
 * port components.  Return 0 on success, -1 on failure.  *tunnel
 * and *user may be set to NULL. */
static int parse_url(const char *url, const char **tunnel, const char **user,
                     unsigned short *port, const char **hostname,
                     apr_pool_t *pool)
{
  const char *p;

  *tunnel = NULL;
  *user = NULL;
  *port = SVN_RA_SVN_PORT;
  *hostname = NULL;

  if (strncasecmp(url, "svn", 3) != 0)
    return -1;
  url += 3;

  /* Get the tunnel specification, if any. */
  if (*url == '+')
    {
      url++;
      p = strchr(url, ':');
      if (!p)
        return -1;
      *tunnel = apr_pstrmemdup(pool, url, p - url);
      url = p;
    }

  if (strncmp(url, "://", 3) != 0)
    return -1;
  url += 3;

  while (1)
    {
      p = url + strcspn(url, "@:/");
      if (*p == '@' && !*user)
        *user = apr_pstrmemdup(pool, url, p - url);
      else if (*p == ':' && !*hostname)
        *hostname = apr_pstrmemdup(pool, url, p - url);
      else if (*p == '/' || *p == '\0')
        {
          if (!*hostname)
            *hostname = apr_pstrmemdup(pool, url, p - url);
          else
            *port = atoi(url);
          break;
        }
      else
        return -1;
      url = p + 1;
    }

  /* Decode any escaped characters in the hostname and user. */
  *hostname = svn_path_uri_decode(*hostname, pool);
  if (*user)
    *user = svn_path_uri_decode(*user, pool);
  return 0;
}

static svn_error_t *make_connection(const char *hostname, unsigned short port,
                                    apr_socket_t **sock, apr_pool_t *pool)
{
  apr_sockaddr_t *sa;
  apr_status_t status;

  /* Resolve the hostname. */
  status = apr_sockaddr_info_get(&sa, hostname, APR_INET, port, 0, pool);
  if (status)
    return svn_error_createf(status, NULL, "Unknown hostname '%s'", hostname);

  /* Create the socket. */
#ifdef MAX_SECS_TO_LINGER
  /* ### old APR interface */
  status = apr_socket_create(sock, APR_INET, SOCK_STREAM, pool);
#else
  status = apr_socket_create(sock, APR_INET, SOCK_STREAM, APR_PROTO_TCP, pool);
#endif
  if (status)
    return svn_error_create(status, NULL, "Can't create socket");

  status = apr_socket_connect(*sock, sa);
  if (status)
    return svn_error_createf(status, NULL, "Can't connect to host '%s'",
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
                                "Proplist element not a list");
      SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, pool, "cs", &name, &value));
      apr_hash_set(*props, name, APR_HASH_KEY_STRING, value);
    }
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
                             "Unrecognized node kind '%s' from server", str);
  return SVN_NO_ERROR;
}

/* --- REPORTER IMPLEMENTATION --- */

static svn_error_t *ra_svn_set_path(void *baton, const char *path,
                                    svn_revnum_t rev,
                                    svn_boolean_t start_empty,
                                    apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "set-path", "crb", path, rev,
                               start_empty));
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
                                     apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "link-path", "ccrb", path, url,
                               rev, start_empty));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_finish_report(void *baton)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, b->pool, "finish-report", ""));
  SVN_ERR(svn_ra_svn_drive_editor(b->conn, b->pool, b->editor, b->edit_baton,
                                  NULL));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, b->pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_abort_report(void *baton)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, b->pool, "abort-report", ""));
  return SVN_NO_ERROR;
}

static svn_ra_reporter_t ra_svn_reporter = {
  ra_svn_set_path,
  ra_svn_delete_path,
  ra_svn_link_path,
  ra_svn_finish_report,
  ra_svn_abort_report
};

static void ra_svn_get_reporter(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                const svn_delta_editor_t *editor,
                                void *edit_baton,
                                const svn_ra_reporter_t **reporter,
                                void **report_baton)
{
  ra_svn_reporter_baton_t *b;

  b = apr_palloc(pool, sizeof(*b));
  b->conn = conn;
  b->pool = pool;
  b->editor = editor;
  b->edit_baton = edit_baton;

  *reporter = &ra_svn_reporter;
  *report_baton = b;
}

/* --- RA LAYER IMPLEMENTATION --- */

static svn_error_t *find_tunnel_agent(const char *tunnel, const char *hostname,
                                      const char ***argv, apr_hash_t *config,
                                      apr_pool_t *pool)
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
                             "Undefined tunnel scheme %s", tunnel);

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
                                     "Tunnel scheme %s requires environment "
                                     "variable %s to be defined", tunnel, var);
        }
    }
  else
    cmd = val;

  /* Tokenize the command into a list of arguments. */
  status = apr_tokenize_to_argv(cmd, &cmd_argv, pool);
  if (status != APR_SUCCESS)
    return svn_error_createf(status, NULL, "Can't tokenize command %s", cmd);

  /* Append the fixed arguments to the result. */
  for (n = 0; cmd_argv[n] != NULL; n++)
    ;
  *argv = apr_palloc(pool, (n + 4) * sizeof(char *));
  memcpy(*argv, cmd_argv, n * sizeof(char *));
  (*argv)[n++] = hostname;
  (*argv)[n++] = "svnserve";
  (*argv)[n++] = "-t";
  (*argv)[n] = NULL;

  return SVN_NO_ERROR;
}

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

  apr_file_open_stdin(&in_file, pool);
  apr_file_open_stdout(&out_file, pool);
  conn = svn_ra_svn_create_conn(NULL, in_file, out_file, pool);
  err = svn_error_create(status, NULL, desc);
  svn_error_clear(svn_ra_svn_write_cmd_failure(conn, pool, err));
  svn_error_clear(svn_ra_svn_flush(conn, pool));
}

static svn_error_t *ra_svn_open(void **sess, const char *url,
                                const svn_ra_callbacks_t *callbacks,
                                void *callback_baton,
                                apr_hash_t *config,
                                apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn;
  apr_socket_t *sock;
  const char *hostname, *user, *status, *tunnel, **args;
  unsigned short port;
  apr_uint64_t minver, maxver;
  apr_array_header_t *mechlist, *caplist, *status_param;
  apr_procattr_t *attr;
  apr_proc_t *proc;
  apr_status_t apr_status;

  if (parse_url(url, &tunnel, &user, &port, &hostname, pool) != 0)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             "Illegal svn repository URL '%s'", url);

  if (tunnel)
    {
      SVN_ERR(find_tunnel_agent(tunnel, hostname, &args, config, pool));
      apr_status = apr_procattr_create(&attr, pool);
      if (apr_status == APR_SUCCESS)
        apr_status = apr_procattr_io_set(attr, 1, 1, 0);
      if (apr_status == APR_SUCCESS)
        apr_status = apr_procattr_cmdtype_set(attr, APR_PROGRAM_PATH);
      if (apr_status == APR_SUCCESS)
        apr_status = apr_procattr_child_errfn_set(attr,
                                                  handle_child_process_error);
      proc = apr_palloc(pool, sizeof(*proc));
      if (apr_status == APR_SUCCESS)
        apr_status = apr_proc_create(proc, *args, args, NULL, attr, pool);
      if (apr_status != APR_SUCCESS)
        return svn_error_create(apr_status, NULL, "Could not create tunnel.");
      conn = svn_ra_svn_create_conn(NULL, proc->out, proc->in, pool);
      conn->proc = proc;

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
      svn_ra_svn_skip_leading_garbage(conn, pool);
    }
  else
    {
      SVN_ERR(make_connection(hostname, port, &sock, pool));
      conn = svn_ra_svn_create_conn(sock, NULL, NULL, pool);
    }

  /* Read server's greeting. */
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "nnll", &minver, &maxver,
                                       &mechlist, &caplist));
  /* We only support protocol version 1. */
  if (minver > 1)
    return svn_error_createf(SVN_ERR_RA_SVN_BAD_VERSION, NULL,
                             "Server requires minimum version %d",
                             (int) minver);
  if (tunnel && find_mech(mechlist, "EXTERNAL"))
    {
      /* Ask the server to use the ssh connection environment (on
       * Unix, that means uid) to determine the authentication name. */
      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "nw(c)()", (apr_uint64_t) 1,
                                     "EXTERNAL", ""));
    }
  else if (find_mech(mechlist, "ANONYMOUS"))
    {
      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "nw(c)()", (apr_uint64_t) 1,
                                     "ANONYMOUS", ""));
    }
  else
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            "Cannot negotiate authentication mechanism");

  /* Write client response to greeting, picking version 1 and the
   * anonymous authentication mechanism with an empty argument. */

  /* Read the server's challenge.  Since we're only doing anonymous or
   * external authentication, the only expected answer is a success
   * notification with no parameter. */
  SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "wl", &status, &status_param));
  if (strcmp(status, "success") != 0)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            "Unexpected server response to authentication");

  /* This is where the security layer would go into effect if we
   * supported security layers, which is a ways off. */

  /* Send URL to server and read UUID response. */
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "c", url));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "c", &conn->uuid));

  *sess = conn;
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_latest_rev(void *sess, svn_revnum_t *rev,
                                          apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-latest-rev", ""));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_dated_rev(void *sess, svn_revnum_t *rev,
                                         apr_time_t tm, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-dated-rev", "c",
                               svn_time_to_cstring(tm, pool)));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_change_rev_prop(void *sess, svn_revnum_t rev,
                                           const char *name,
                                           const svn_string_t *value,
                                           apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "change-rev-prop", "rcs",
                               rev, name, value));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_uuid(void *sess, const char **uuid,
                                    apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;
  *uuid = conn->uuid;
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_rev_proplist(void *sess, svn_revnum_t rev,
                                        apr_hash_t **props, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_array_header_t *proplist;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "rev-proplist", "r", rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "l", &proplist));
  SVN_ERR(parse_proplist(proplist, pool, props));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_rev_prop(void *sess, svn_revnum_t rev,
                                    const char *name,
                                    svn_string_t **value, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "rev-prop", "rc", rev, name));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "(?s)", value));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_end_commit(void *baton)
{
  ra_svn_commit_callback_baton_t *ccb = baton;

  return svn_ra_svn_read_tuple(ccb->conn, ccb->pool, "r(?c)(?c)",
                               ccb->new_rev,
                               ccb->committed_date,
                               ccb->committed_author);
}

static svn_error_t *ra_svn_commit(void *sess,
                                  const svn_delta_editor_t **editor,
                                  void **edit_baton,
                                  svn_revnum_t *new_rev,
                                  const char **committed_date,
                                  const char **committed_author,
                                  const char *log_msg,
                                  apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;
  ra_svn_commit_callback_baton_t *ccb;

  /* Tell the server we're starting the commit. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "commit", "c", log_msg));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  /* Remember a few arguments for when the commit is over. */
  ccb = apr_palloc(pool, sizeof(*ccb));
  ccb->conn = conn;
  ccb->pool = pool;
  ccb->new_rev = new_rev;
  ccb->committed_date = committed_date;
  ccb->committed_author = committed_author;

  /* Fetch an editor for the caller to drive.  The editor will call
   * ra_svn_end_commit() upon close_edit(), at which point we'll fill
   * in the new_rev, committed_date, and commited_author values. */
  svn_ra_svn_get_editor(editor, edit_baton, conn, pool,
                        ra_svn_end_commit, ccb);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_file(void *sess, const char *path,
                                    svn_revnum_t rev, svn_stream_t *stream,
                                    svn_revnum_t *fetched_rev,
                                    apr_hash_t **props,
                                    apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;
  svn_ra_svn_item_t *item;
  apr_array_header_t *proplist;
  unsigned char digest[MD5_DIGESTSIZE];
  const char *expected_checksum, *hex_digest;
  apr_md5_ctx_t md5_context;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-file", "c(?r)bb", path,
                               rev, (props != NULL), (stream != NULL)));
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
                                "Non-string as part of file contents");
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
      hex_digest = svn_md5_digest_to_cstring(digest, pool);
      if (strcmp(hex_digest, expected_checksum) != 0)
        return svn_error_createf
          (SVN_ERR_CHECKSUM_MISMATCH, NULL,
           "ra_svn_get_file: checksum mismatch for '%s':\n"
           "   expected checksum:  %s\n"
           "   actual checksum:    %s\n",
           path, expected_checksum, hex_digest);
    }

  SVN_ERR(svn_stream_close(stream));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_dir(void *sess, const char *path,
                                   svn_revnum_t rev, apr_hash_t **dirents,
                                   svn_revnum_t *fetched_rev,
                                   apr_hash_t **props,
                                   apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;
  svn_revnum_t crev;
  apr_array_header_t *proplist, *dirlist;
  int i;
  svn_ra_svn_item_t *elt;
  const char *name, *kind, *cdate, *cauthor;
  svn_boolean_t has_props;
  apr_uint64_t size;
  svn_dirent_t *dirent;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-dir", "c(?r)bb", path,
                               rev, (props != NULL), (dirents != NULL)));
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
                                "Dirlist element not a list");
      SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, pool, "cwnbr(?c)(?c)",
                                     &name, &kind, &size, &has_props,
                                     &crev, &cdate, &cauthor));
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


static svn_error_t *ra_svn_update(void *sess,
                                  const svn_ra_reporter_t **reporter,
                                  void **report_baton, svn_revnum_t rev,
                                  const char *target, svn_boolean_t recurse,
                                  const svn_delta_editor_t *update_editor,
                                  void *update_baton, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;

  if (target == NULL)
    target = "";

  /* Tell the server we want to start an update. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "update", "(?r)cb", rev, target,
                               recurse));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * update_editor upon finish_report(). */
  ra_svn_get_reporter(conn, pool, update_editor, update_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_switch(void *sess,
                                  const svn_ra_reporter_t **reporter,
                                  void **report_baton, svn_revnum_t rev,
                                  const char *target, svn_boolean_t recurse,
                                  const char *switch_url,
                                  const svn_delta_editor_t *update_editor,
                                  void *update_baton, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;

  if (target == NULL)
    target = "";

  /* Tell the server we want to start a switch. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "switch", "(?r)cbc", rev, target,
                               recurse, switch_url));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * update_editor upon finish_report(). */
  ra_svn_get_reporter(conn, pool, update_editor, update_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_status(void *sess,
                                  const svn_ra_reporter_t **reporter,
                                  void **report_baton,
                                  const char *target, svn_revnum_t rev,
                                  svn_boolean_t recurse,
                                  const svn_delta_editor_t *status_editor,
                                  void *status_baton, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;

  if (target == NULL)
    target = "";

  /* Tell the server we want to start a status operation. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "status", "cb(?r)", 
                               target, recurse, rev));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * status_editor upon finish_report(). */
  ra_svn_get_reporter(conn, pool, status_editor, status_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_diff(void *sess,
                                const svn_ra_reporter_t **reporter,
                                void **report_baton,
                                svn_revnum_t rev, const char *target,
                                svn_boolean_t recurse,
                                svn_boolean_t ignore_ancestry,
                                const char *versus_url,
                                const svn_delta_editor_t *diff_editor,
                                void *diff_baton, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;

  if (target == NULL)
    target = "";

  /* Tell the server we want to start a diff. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "diff", "(?r)cbbc", rev, target,
                               recurse, ignore_ancestry, versus_url));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * diff_editor upon finish_report(). */
  ra_svn_get_reporter(conn, pool, diff_editor, diff_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_log(void *sess, const apr_array_header_t *paths,
                               svn_revnum_t start, svn_revnum_t end,
                               svn_boolean_t discover_changed_paths,
                               svn_boolean_t strict_node_history,
                               svn_log_message_receiver_t receiver,
                               void *receiver_baton, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *subpool;
  int i;
  const char *path, *author, *date, *message, *cpath, *action, *copy_path;
  svn_ra_svn_item_t *item, *elt;
  apr_array_header_t *cplist;
  apr_hash_t *cphash;
  svn_revnum_t rev, copy_rev;
  svn_log_changed_path_t *change;

  /* Write out the command manually, since we have to send an array. */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  SVN_ERR(svn_ra_svn_write_word(conn, pool, "log"));
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  /* Parameter 1: paths */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (paths)
    {
      for (i = 0; i < paths->nelts; i++)
        {
          path = ((const char **) paths->elts)[i];
          SVN_ERR(svn_ra_svn_write_cstring(conn, pool, path));
        }
    }
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  /* Parameter 2: start rev (optional) */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (SVN_IS_VALID_REVNUM(start))
    SVN_ERR(svn_ra_svn_write_number(conn, pool, start));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  /* Parameter 3: end rev (optional) */
  SVN_ERR(svn_ra_svn_start_list(conn, pool));
  if (SVN_IS_VALID_REVNUM(end))
    SVN_ERR(svn_ra_svn_write_number(conn, pool, end));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  /* Parameter 4: changed-paths */
  SVN_ERR(svn_ra_svn_write_word(conn, pool,
                                discover_changed_paths ? "true" : "false"));
  /* Parameter 5: strict-node */
  SVN_ERR(svn_ra_svn_write_word(conn, pool,
                                strict_node_history ? "true" : "false"));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));
  SVN_ERR(svn_ra_svn_end_list(conn, pool));

  /* Read the log messages. */
  subpool = svn_pool_create(pool);
  while (1)
    {
      SVN_ERR(svn_ra_svn_read_item(conn, subpool, &item));
      if (item->kind == SVN_RA_SVN_WORD && strcmp(item->u.word, "done") == 0)
        break;
      if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                "Log entry not a list");
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
                                        "Changed-path entry not a list");
              SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, subpool, "cw(?cr)",
                                             &cpath, &action, &copy_path,
                                             &copy_rev));
              change = apr_palloc(subpool, sizeof(*change));
              change->action = *action;
              change->copyfrom_path = copy_path;
              change->copyfrom_rev = copy_rev;
              apr_hash_set(cphash, cpath, APR_HASH_KEY_STRING, change);
            }
        }
      else
        cphash = NULL;
      SVN_ERR(receiver(receiver_baton, cphash, rev, author, date, message,
                       subpool));
      apr_pool_clear(subpool);
    }
  apr_pool_destroy(subpool);

  /* Read the response. */
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_check_path(svn_node_kind_t *kind, void *sess,
                                      const char *path, svn_revnum_t rev,
                                      apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess;
  const char *kind_word;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "check-path", "c(?r)", path, rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "w", &kind_word));
  SVN_ERR(interpret_kind(kind_word, pool, kind));
  return SVN_NO_ERROR;
}

static const svn_ra_plugin_t ra_svn_plugin = {
  "ra_svn",
  "Module for accessing a repository using the svn network protocol.",
  ra_svn_open,
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
  ra_svn_get_uuid
};

svn_error_t *svn_ra_svn_init(int abi_version, apr_pool_t *pool,
                             apr_hash_t *hash)
{
  apr_hash_set(hash, "svn", APR_HASH_KEY_STRING, &ra_svn_plugin);
  return SVN_NO_ERROR;
}
