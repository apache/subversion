/*
 * client.c :  Functions for repository access via the Subversion protocol
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
#include <apr_network_io.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_time.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_ra.h"
#include "svn_ra_svn.h"

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

/* Parse an svn URL's authority section into user, host, and port components.
 * Return 0 on success, -1 on failure.  *user may be set to NULL. */
static int parse_url(const char *url, const char **user, unsigned short *port,
                     const char **hostname, apr_pool_t *pool)
{
  const char *p;

  *user = NULL;
  *port = SVN_RA_SVN_PORT;
  *hostname = NULL;

  if (strncmp(url, "svn://", 6) != 0)
    return -1;
  url += 6;
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
    return svn_error_createf(status, 0, NULL, "Unknown hostname %s", hostname);

  /* Create the socket. */
  status = apr_socket_create(sock, APR_INET, SOCK_STREAM, pool);
  if (status)
    return svn_error_create(status, 0, NULL, "Can't create socket");

  status = apr_connect(*sock, sa);
  if (status)
    return svn_error_createf(status, 0, NULL, "Can't connect to host %s",
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
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
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
    return svn_error_createf(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                             "Unrecognized node kind '%s' from server", str);
  return SVN_NO_ERROR;
}

/* --- REPORTER IMPLEMENTATION --- */

static svn_error_t *ra_svn_set_path(void *baton, const char *path,
                                    svn_revnum_t rev)
{
  ra_svn_reporter_baton_t *b = baton;
  apr_pool_t *pool = b->pool;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "set-path", "cr", path, rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_delete_path(void *baton, const char *path)
{
  ra_svn_reporter_baton_t *b = baton;
  apr_pool_t *pool = b->pool;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "delete-path", "c", path));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  return SVN_NO_ERROR;
}
    
static svn_error_t *ra_svn_link_path(void *baton, const char *path,
                                     const char *url, svn_revnum_t rev)
{
  ra_svn_reporter_baton_t *b = baton;
  apr_pool_t *pool = b->pool;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "link-path", "ccr",
                               path, url, rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_finish_report(void *baton)
{
  ra_svn_reporter_baton_t *b = baton;
  apr_pool_t *pool = b->pool;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "finish-report", ""));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  SVN_ERR(svn_ra_svn_drive_editor(b->conn, pool, b->editor, b->edit_baton,
                                  TRUE, NULL));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_abort_report(void *baton)
{
  ra_svn_reporter_baton_t *b = baton;
  apr_pool_t *pool = b->pool;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "abort-report", ""));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
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

static const char *find_tunnel_agent(const char *hostname, apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *server_group, *agent = NULL;
  svn_error_t *err;

  err = svn_config_read_servers(&cfg, pool);
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }
  server_group = svn_config_find_group(cfg, hostname, "groups", pool);
  if (server_group)
    svn_config_get(cfg, &agent, server_group, "svn-tunnel-agent", NULL);
  return agent;
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

static svn_error_t *ra_svn_open(void **sess, const char *url,
                                const svn_ra_callbacks_t *callbacks,
                                void *callback_baton,
                                apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn;
  apr_socket_t *sock;
  const char *hostname, *user, *status, *tunnel_agent, *args[5];
  unsigned short port;
  apr_uint64_t minver, maxver;
  apr_array_header_t *mechlist, *caplist, *status_param;
      apr_procattr_t *attr;
      apr_proc_t *proc;

  if (parse_url(url, &user, &port, &hostname, pool) != 0)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, 0, NULL,
                             "Illegal svn repository URL %s", url);

  tunnel_agent = find_tunnel_agent(hostname, pool);
  if (tunnel_agent)
    {
      /* ### It should be nice if tunnel_agent could contain flags. */
      args[0] = tunnel_agent;
      args[1] = hostname;
      args[2] = "svnserve";
      args[3] = "-t";
      args[4] = NULL;
      apr_procattr_create(&attr, pool);
      apr_procattr_io_set(attr, 1, 1, 0);
      apr_procattr_cmdtype_set(attr, APR_PROGRAM_PATH);
      proc = apr_palloc(pool, sizeof(*proc));
      apr_proc_create(proc, tunnel_agent, args, NULL, attr, pool);
      conn = svn_ra_svn_create_conn(NULL, proc->out, proc->in, pool);
      conn->proc = proc;
    }
  else
    {
      SVN_ERR(make_connection(hostname, port, &sock, pool));
      conn = svn_ra_svn_create_conn(sock, NULL, NULL, pool);
    }

  /* Read server's greeting. */
  SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "nnll", &minver, &maxver,
                                &mechlist, &caplist));
  /* We only support protocol version 1. */
  if (minver > 1)
    return svn_error_createf(SVN_ERR_RA_SVN_BAD_VERSION, 0, NULL,
                             "Server requires minimum version %d",
                             (int) minver);
  if (tunnel_agent && find_mech(mechlist, "EXTERNAL"))
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
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, 0, NULL,
                            "Cannot negotiate authentication mechanism");

  /* Write client response to greeting, picking version 1 and the
   * anonymous authentication mechanism with an empty argument. */

  /* Read the server's challenge.  Since we're only doing anonymous or
   * external authentication, the only expected answer is a success
   * notification with no parameter. */
  SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "wl", &status, &status_param));
  if (strcmp(status, "success") != 0)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, 0, NULL,
                            "Unexpected server response to authentication");

  /* This is where the security layer would go into effect if we
   * supported security layers, which is a ways off. */

  /* Send URL to server and read empty response. */
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "c", url));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  *sess = conn;
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_close(void *sess)
{
  svn_ra_svn_conn_t *conn = sess;

  if (conn->sock)
    apr_socket_close(conn->sock);
  else
    {
      apr_file_close(conn->in_file);
      apr_file_close(conn->out_file);
      /* ### Perhaps a cleanup handler should make sure this gets done? */
      while (apr_proc_wait(conn->proc, NULL, NULL,
                           APR_WAIT) == APR_CHILD_NOTDONE)
        ;
    }
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_latest_rev(void *sess, svn_revnum_t *rev)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-latest-rev", ""));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_dated_rev(void *sess, svn_revnum_t *rev,
                                         apr_time_t tm)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-dated-rev", "c",
                               svn_time_to_cstring(tm, pool)));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_change_rev_prop(void *sess, svn_revnum_t rev,
                                           const char *name,
                                           const svn_string_t *value)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "change-rev-prop", "ncs",
                               rev, name, value));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_rev_proplist(void *sess, svn_revnum_t rev,
                                        apr_hash_t **props)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;
  apr_array_header_t *proplist;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "rev-proplist", "r", rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "l", &proplist));
  SVN_ERR(parse_proplist(proplist, pool, props));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_rev_prop(void *sess, svn_revnum_t rev,
                                    const char *name,
                                    svn_string_t **value)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "rev-prop", "rc", rev, name));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "[s]", value));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_end_commit(void *baton)
{
  ra_svn_commit_callback_baton_t *ccb = baton;

  return svn_ra_svn_read_tuple(ccb->conn, ccb->pool, "rcc", ccb->new_rev,
                               ccb->committed_date,
                               ccb->committed_author);
}

static svn_error_t *ra_svn_commit(void *sess,
                                  const svn_delta_editor_t **editor,
                                  void **edit_baton,
                                  svn_revnum_t *new_rev,
                                  const char **committed_date,
                                  const char **committed_author,
                                  const char *log_msg)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;
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
                                    apr_hash_t **props)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;
  svn_ra_svn_item_t *item;
  apr_array_header_t *proplist;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-file", "c[r]bb", path,
                               rev, (props != NULL), (stream != NULL)));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "rl", &rev, &proplist));

  if (fetched_rev)
    *fetched_rev = rev;
  if (props)
    SVN_ERR(parse_proplist(proplist, pool, props));

  /* We're done if the contents weren't wanted. */
  if (!stream)
    return SVN_NO_ERROR;

  /* Read the file's contents. */
  while (1)
    {
      SVN_ERR(svn_ra_svn_read_item(conn, pool, &item));
      if (item->kind != SVN_RA_SVN_STRING)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                                "Non-string as part of file contents");
      if (item->u.string->len == 0)
        break;
      if (stream)
        SVN_ERR(svn_stream_write(stream, item->u.string->data,
                                 &item->u.string->len));
    }
  SVN_ERR(svn_stream_close(stream));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_dir(void *sess, const char *path,
                                   svn_revnum_t rev, apr_hash_t **dirents,
                                   svn_revnum_t *fetched_rev,
                                   apr_hash_t **props)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;
  svn_revnum_t crev;
  apr_array_header_t *proplist, *dirlist;
  int i;
  svn_ra_svn_item_t *elt;
  const char *name, *kind, *cdate, *cauthor;
  svn_boolean_t has_props;
  apr_uint64_t size;
  svn_dirent_t *dirent;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "get-dir", "c[r]bb", path,
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
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                                "Dirlist element not a list");
      SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, pool, "cwnbr[c][c]",
                                     &name, &kind, &size, &has_props,
                                     &crev, &cdate, &cauthor));
      dirent = apr_palloc(pool, sizeof(*dirent));
      SVN_ERR(interpret_kind(kind, pool, &dirent->kind));
      dirent->size = size;
      dirent->has_props = has_props;
      dirent->created_rev = crev;
      SVN_ERR(svn_time_from_cstring(&dirent->time, cdate, pool));
      dirent->last_author = cauthor;
      apr_hash_set(*dirents, name, APR_HASH_KEY_STRING, dirent);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_checkout(void *sess, svn_revnum_t rev,
                                    svn_boolean_t recurse,
                                    const svn_delta_editor_t *editor,
                                    void *edit_baton)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  /* Tell the server to start a checkout. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "checkout", "[r]b", rev, recurse));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  /* Let the server drive the checkout editor. */
  SVN_ERR(svn_ra_svn_drive_editor(conn, pool, editor, edit_baton, TRUE, NULL));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_update(void *sess,
                                  const svn_ra_reporter_t **reporter,
                                  void **report_baton, svn_revnum_t rev,
                                  const char *target, svn_boolean_t recurse,
                                  const svn_delta_editor_t *update_editor,
                                  void *update_baton)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  if (target == NULL)
    target = "";

  /* Tell the server we want to start an update. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "update", "[r]cb", rev, target,
                               recurse));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

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
                                  void *update_baton)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  if (target == NULL)
    target = "";

  /* Tell the server we want to start a switch. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "switch", "[r]cbc", rev, target,
                               recurse, switch_url));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * update_editor upon finish_report(). */
  ra_svn_get_reporter(conn, pool, update_editor, update_baton,
                      reporter, report_baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_status(void *sess,
                                  const svn_ra_reporter_t **reporter,
                                  void **report_baton,
                                  const char *target, svn_boolean_t recurse,
                                  const svn_delta_editor_t *status_editor,
                                  void *status_baton)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  if (target == NULL)
    target = "";

  /* Tell the server we want to start a status operation. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "status", "cb", target, recurse));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

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
                                svn_boolean_t recurse, const char *versus_url,
                                const svn_delta_editor_t *diff_editor,
                                void *diff_baton)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;

  if (target == NULL)
    target = "";

  /* Tell the server we want to start a diff. */
  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "switch", "[r]cbc", rev, target,
                               recurse, versus_url));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

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
                               void *receiver_baton)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool, *subpool;
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
  if (SVN_IS_VALID_REVNUM(start))
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

  /* Read the response. */
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, ""));

  /* Read the log messages. */
  subpool = svn_pool_create(pool);
  while (1)
    {
      SVN_ERR(svn_ra_svn_read_item(conn, subpool, &item));
      if (item->kind == SVN_RA_SVN_WORD && strcmp(item->u.word, "done") == 0)
        break;
      if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                                "Log entry not a list");
      SVN_ERR(svn_ra_svn_parse_tuple(item->u.list, subpool, "lr[c][c][c]",
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
                return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, 0, NULL,
                                        "Changed-path entry not a list");
              SVN_ERR(svn_ra_svn_parse_tuple(elt->u.list, subpool, "cw[cr]",
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
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_check_path(svn_node_kind_t *kind, void *sess,
                                      const char *path, svn_revnum_t rev)
{
  svn_ra_svn_conn_t *conn = sess;
  apr_pool_t *pool = conn->pool;
  const char *kind_word;

  SVN_ERR(svn_ra_svn_write_cmd(conn, pool, "check-path", "c[r]", path, rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(conn, pool, "w", &kind_word));
  SVN_ERR(interpret_kind(kind_word, pool, kind));
  return SVN_NO_ERROR;
}

static const svn_ra_plugin_t ra_svn_plugin = {
  "ra_svn",
  "Module for accessing a repository using the svn network protocol.",
  ra_svn_open,
  ra_svn_close,
  ra_svn_get_latest_rev,
  ra_svn_get_dated_rev,
  ra_svn_change_rev_prop,
  ra_svn_rev_proplist,
  ra_svn_rev_prop,
  ra_svn_commit,
  ra_svn_get_file,
  ra_svn_get_dir,
  ra_svn_checkout,
  ra_svn_update,
  ra_svn_switch,
  ra_svn_status,
  ra_svn_diff,
  ra_svn_log,
  ra_svn_check_path
};

svn_error_t *svn_ra_svn_init(int abi_version, apr_pool_t *pool,
                             apr_hash_t *hash)
{
  apr_hash_set(hash, "svn", APR_HASH_KEY_STRING, &ra_svn_plugin);
  return SVN_NO_ERROR;
}
