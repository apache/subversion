/*
 * serve.c :  Functions for serving the Subversion protocol
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
#include <apr_user.h>
#include <apr_file_info.h>
#include <apr_md5.h>

#include "svn_private_config.h"  /* For SVN_PATH_LOCAL_SEPARATOR */
#include <svn_types.h>
#include <svn_string.h>
#include <svn_pools.h>
#include <svn_error.h>
#include <svn_ra.h>
#include <svn_ra_svn.h>
#include <svn_repos.h>
#include <svn_path.h>
#include <svn_time.h>
#include <svn_utf.h>
#include <svn_md5.h>
#include <svn_config.h>

#include "server.h"

typedef struct {
  svn_repos_t *repos;
  svn_fs_t *fs;            /* For convenience; same as svn_repos_fs(repos) */
  svn_config_t *cfg;       /* Parsed repository svnserve.conf */
  svn_config_t *pwdb;      /* Parsed password database */
  const char *realm;       /* Authentication realm */
  const char *repos_url;   /* URL to base of repository */
  const char *fs_path;     /* Decoded base path inside repository */
  const char *user;
  svn_boolean_t tunnel;    /* Tunneled through login agent */
  const char *tunnel_user; /* Allow EXTERNAL to authenticate as this */
  svn_boolean_t read_only; /* Disallow write access (global flag) */
  int protocol_version;
} server_baton_t;

typedef struct {
  svn_revnum_t *new_rev;
  const char **date;
  const char **author;
} commit_callback_baton_t;

typedef struct {
  server_baton_t *sb;
  const char *repos_url;  /* Decoded repository URL. */
  void *report_baton;
  svn_error_t *err;
} report_driver_baton_t;

typedef struct {
  const char *fs_path;
  svn_ra_svn_conn_t *conn;
} log_baton_t;

typedef struct {
  svn_ra_svn_conn_t *conn;
  apr_pool_t *pool;  /* Pool provided in the handler call. */
} file_revs_baton_t;

enum authn_type { UNAUTHENTICATED, AUTHENTICATED };
enum access_type { NO_ACCESS, READ_ACCESS, WRITE_ACCESS };

/* Verify that URL is inside REPOS_URL and get its fs path. Assume that 
   REPOS_URL and URL are already URI-decoded. */
static svn_error_t *get_fs_path(const char *repos_url, const char *url,
                                const char **fs_path, apr_pool_t *pool)
{
  apr_size_t len;

  len = strlen(repos_url);
  if (strncmp(url, repos_url, len) != 0)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             "'%s' is not the same repository as '%s'",
                             url, repos_url);
  *fs_path = url + len;
  return SVN_NO_ERROR;
}

/* --- AUTHENTICATION AND AUTHORIZATION FUNCTIONS --- */

static enum access_type get_access(server_baton_t *b, enum authn_type auth)
{
  const char *var = (auth == AUTHENTICATED) ? SVN_CONFIG_OPTION_AUTH_ACCESS :
    SVN_CONFIG_OPTION_ANON_ACCESS;
  const char *val, *def = (auth == AUTHENTICATED) ? "write" : "read";
  enum access_type result;

  svn_config_get(b->cfg, &val, SVN_CONFIG_SECTION_GENERAL, var, def);
  result = (strcmp(val, "write") == 0 ? WRITE_ACCESS :
            strcmp(val, "read") == 0 ? READ_ACCESS : NO_ACCESS);
  return (result == WRITE_ACCESS && b->read_only) ? READ_ACCESS : result;
}

static enum access_type current_access(server_baton_t *b)
{
  return get_access(b, (b->user) ? AUTHENTICATED : UNAUTHENTICATED);
}

static svn_error_t *send_mechs(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                               server_baton_t *b, enum access_type required)
{
  if (get_access(b, UNAUTHENTICATED) >= required)
    SVN_ERR(svn_ra_svn_write_word(conn, pool, "ANONYMOUS"));
  if (b->tunnel_user && get_access(b, AUTHENTICATED) >= required)
    SVN_ERR(svn_ra_svn_write_word(conn, pool, "EXTERNAL"));
  if (b->pwdb && get_access(b, AUTHENTICATED) >= required)
    SVN_ERR(svn_ra_svn_write_word(conn, pool, "CRAM-MD5"));
  return SVN_NO_ERROR;
}

/* Authenticate, once the client has chosen a mechanism and possibly
 * sent an initial mechanism token.  On success, set *success to true
 * and b->user to the authenticated username (or NULL for anonymous).
 * On authentication failure, report failure to the client and set
 * *success to FALSE.  On communications failure, return an error. */
static svn_error_t *auth(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                         const char *mech, const char *mecharg,
                         server_baton_t *b, enum access_type required,
                         svn_boolean_t *success)
{
  *success = FALSE;

  if (get_access(b, AUTHENTICATED) >= required
      && b->tunnel_user && strcmp(mech, "EXTERNAL") == 0)
    {
      b->user = b->tunnel_user;
      if (*mecharg && strcmp(mecharg, b->user) != 0)
        return svn_ra_svn_write_tuple(conn, pool, "w(c)", "failure",
                                      "Requested username does not match");
      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w()", "success"));
      *success = TRUE;
      return SVN_NO_ERROR;
    }

  if (get_access(b, UNAUTHENTICATED) >= required
      && strcmp(mech, "ANONYMOUS") == 0)
    {
      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w()", "success"));
      *success = TRUE;
      return SVN_NO_ERROR;
    }

  if (get_access(b, AUTHENTICATED) >= required
      && b->pwdb && strcmp(mech, "CRAM-MD5") == 0)
    return svn_ra_svn_cram_server(conn, pool, b->pwdb, &b->user, success);

  return svn_ra_svn_write_tuple(conn, pool, "w(c)", "failure",
                                "Must authenticate with listed mechanism");
}

/* Perform an authentication request in order to get an access level of
 * REQUIRED or higher.  Since the client may escape the authentication
 * exchange, the caller should check current_access(b) to see if
 * authentication succeeded. */
static svn_error_t *auth_request(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                 server_baton_t *b, enum access_type required)
{
  svn_boolean_t success;
  const char *mech, *mecharg;

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w((!", "success"));
  SVN_ERR(send_mechs(conn, pool, b, required));
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!)c)", b->realm));
  do
    {
      SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "w(?c)", &mech, &mecharg));
      if (!*mech)
        break;
      SVN_ERR(auth(conn, pool, mech, mecharg, b, required, &success));
    }
  while (!success);
  return SVN_NO_ERROR;
}

/* Send a trivial auth request, listing no mechanisms. */
static svn_error_t *trivial_auth_request(svn_ra_svn_conn_t *conn,
                                         apr_pool_t *pool, server_baton_t *b)
{
  if (b->protocol_version < 2)
    return SVN_NO_ERROR;
  return svn_ra_svn_write_cmd_response(conn, pool, "()c", "");
}

static svn_error_t *must_have_write_access(svn_ra_svn_conn_t *conn,
                                           apr_pool_t *pool, server_baton_t *b)
{
  if (current_access(b) == WRITE_ACCESS)
    return trivial_auth_request(conn, pool, b);

  /* If we can get write access by authenticating, try that. */
  if (b->user == NULL && get_access(b, AUTHENTICATED) == WRITE_ACCESS
      && (b->tunnel_user || b->pwdb) && b->protocol_version >= 2)
    SVN_ERR(auth_request(conn, pool, b, WRITE_ACCESS));

  if (current_access(b) != WRITE_ACCESS)
    return svn_error_create(SVN_ERR_RA_SVN_CMD_ERR,
                            svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                             "Connection is read-only"), NULL);

  return SVN_NO_ERROR;
}

/* --- REPORTER COMMAND SET --- */

/* To allow for pipelining, reporter commands have no reponses.  If we
 * get an error, we ignore all subsequent reporter commands and return
 * the error finish_report, to be handled by the calling command.
 */

static svn_error_t *set_path(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                             apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;
  const char *path;
  svn_revnum_t rev;
  svn_boolean_t start_empty;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "crb",
                                 &path, &rev, &start_empty));
  if (!b->err)
    b->err = svn_repos_set_path(b->report_baton, path, rev, start_empty, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *delete_path(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;
  const char *path;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c", &path));
  if (!b->err)
    b->err = svn_repos_delete_path(b->report_baton, path, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *link_path(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                              apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;
  const char *path, *url, *fs_path;
  svn_revnum_t rev;
  svn_boolean_t start_empty;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "ccrb",
                                 &path, &url, &rev, &start_empty));
  url = svn_path_uri_decode(url, pool);
  if (!b->err)
    b->err = get_fs_path(b->repos_url, url, &fs_path, pool);
  if (!b->err)
    b->err = svn_repos_link_path(b->report_baton, path, fs_path, rev,
                                 start_empty, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *finish_report(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;

  /* No arguments to parse. */
  SVN_ERR(trivial_auth_request(conn, pool, b->sb));
  if (!b->err)
    b->err = svn_repos_finish_report(b->report_baton, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *abort_report(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   apr_array_header_t *params, void *baton)
{
  report_driver_baton_t *b = baton;

  /* No arguments to parse. */
  svn_error_clear(svn_repos_abort_report(b->report_baton, pool));
  return SVN_NO_ERROR;
}

static const svn_ra_svn_cmd_entry_t report_commands[] = {
  { "set-path",      set_path },
  { "delete-path",   delete_path },
  { "link-path",     link_path },
  { "finish-report", finish_report, TRUE },
  { "abort-report",  abort_report,  TRUE },
  { NULL }
};

/* Accept a report from the client, drive the network editor with the
 * result, and then write an empty command response.  If there is a
 * non-protocol failure, accept_report will abort the edit and return
 * a command error to be reported by handle_commands(). */
static svn_error_t *accept_report(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  server_baton_t *b, svn_revnum_t rev,
                                  const char *target, const char *tgt_path,
                                  svn_boolean_t text_deltas,
                                  svn_boolean_t recurse,
                                  svn_boolean_t ignore_ancestry)
{
  const svn_delta_editor_t *editor;
  void *edit_baton, *report_baton;
  report_driver_baton_t rb;
  svn_error_t *err;

  /* Make an svn_repos report baton.  Tell it to drive the network editor
   * when the report is complete. */
  svn_ra_svn_get_editor(&editor, &edit_baton, conn, pool, NULL, NULL);
  SVN_CMD_ERR(svn_repos_begin_report(&report_baton, rev, b->user, b->repos,
                                     b->fs_path, target, tgt_path, text_deltas,
                                     recurse, ignore_ancestry, editor,
                                     edit_baton, NULL, NULL, pool));

  rb.sb = b;
  rb.repos_url = svn_path_uri_decode(b->repos_url, pool);
  rb.report_baton = report_baton;
  rb.err = NULL;
  err = svn_ra_svn_handle_commands(conn, pool, report_commands, &rb);
  if (err)
    {
      /* Network or protocol error while handling commands. */
      svn_error_clear(rb.err);
      return err;
    }
  else if (rb.err)
    {
      /* Some failure during the reporting or editing operations. */
      svn_error_clear(editor->abort_edit(edit_baton, pool));
      SVN_CMD_ERR(rb.err);
    }
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

/* --- MAIN COMMAND SET --- */

/* Write out a property list.  PROPS is allowed to be NULL, in which case
 * an empty list will be written out; this happens if the client could
 * have asked for props but didn't. */
static svn_error_t *write_proplist(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   apr_hash_t *props)
{
  apr_hash_index_t *hi;
  const void *namevar;
  void *valuevar;
  const char *name;
  svn_string_t *value;

  if (props)
    {
      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &namevar, NULL, &valuevar);
          name = namevar;
          value = valuevar;
          SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "cs", name, value));
        }
    }
  return SVN_NO_ERROR;
}

/* Write out a list of property diffs.  PROPDIFFS is an array of svn_prop_t
 * values. */
static svn_error_t *write_prop_diffs(svn_ra_svn_conn_t *conn,
                                     apr_pool_t *pool,
                                     apr_array_header_t *propdiffs)
{
  int i;

  for (i = 0; i < propdiffs->nelts; ++i)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(propdiffs, i, svn_prop_t);

      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "c(?s)",
                                     prop->name, prop->value));
    }

  return SVN_NO_ERROR;
}

static const char *kind_word(svn_node_kind_t kind)
{
  switch (kind)
    {
    case svn_node_none:
      return "none";
    case svn_node_file:
      return "file";
    case svn_node_dir:
      return "dir";
    case svn_node_unknown:
      return "unknown";
    default:
      abort();
    }
}

/* ### This really belongs in libsvn_repos. */
/* Get the properties for a path, with hardcoded committed-info values. */
static svn_error_t *get_props(apr_hash_t **props, svn_fs_root_t *root,
                              const char *path, apr_pool_t *pool)
{
  svn_string_t *str;
  svn_revnum_t crev;
  const char *cdate, *cauthor, *uuid;

  /* Get the properties. */
  SVN_ERR(svn_fs_node_proplist(props, root, path, pool));

  /* Hardcode the values for the committed revision, date, and author. */
  SVN_ERR(svn_repos_get_committed_info(&crev, &cdate, &cauthor, root,
                                       path, pool));
  str = svn_string_create(apr_psprintf(pool, "%ld", crev),
                          pool);
  apr_hash_set(*props, SVN_PROP_ENTRY_COMMITTED_REV, APR_HASH_KEY_STRING, str);
  str = (cdate) ? svn_string_create(cdate, pool) : NULL;
  apr_hash_set(*props, SVN_PROP_ENTRY_COMMITTED_DATE, APR_HASH_KEY_STRING,
               str);
  str = (cauthor) ? svn_string_create(cauthor, pool) : NULL;
  apr_hash_set(*props, SVN_PROP_ENTRY_LAST_AUTHOR, APR_HASH_KEY_STRING, str);

  /* Hardcode the values for the UUID. */
  SVN_ERR(svn_fs_get_uuid(svn_fs_root_fs(root), &uuid, pool));
  str = (uuid) ? svn_string_create(uuid, pool) : NULL;
  apr_hash_set(*props, SVN_PROP_ENTRY_UUID, APR_HASH_KEY_STRING, str);

  return SVN_NO_ERROR;
}

static svn_error_t *get_latest_rev(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;

  SVN_ERR(trivial_auth_request(conn, pool, b));
  SVN_CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *get_dated_rev(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  apr_time_t tm;
  const char *timestr;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c", &timestr));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  SVN_CMD_ERR(svn_time_from_cstring(&tm, timestr, pool));
  SVN_CMD_ERR(svn_repos_dated_revision(&rev, b->repos, tm, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *change_rev_prop(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                    apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *name;
  svn_string_t *value;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "rc?s", &rev, &name, &value));
  SVN_ERR(must_have_write_access(conn, pool, b));
  SVN_CMD_ERR(svn_repos_fs_change_rev_prop2(b->repos, rev, b->user,
                                            name, value, NULL, NULL, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *rev_proplist(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                 apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  apr_hash_t *props;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "r", &rev));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  SVN_CMD_ERR(svn_repos_fs_revision_proplist(&props, b->repos, rev,
                                              NULL, NULL, pool));
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w((!", "success"));
  SVN_ERR(write_proplist(conn, pool, props));
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!))"));
  return SVN_NO_ERROR;
}

static svn_error_t *rev_prop(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                             apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *name;
  svn_string_t *value;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "rc", &rev, &name));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  SVN_CMD_ERR(svn_repos_fs_revision_prop(&value, b->repos, rev, name,
                                         NULL, NULL, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "(?s)", value));
  return SVN_NO_ERROR;
}

static svn_error_t *commit_done(svn_revnum_t new_rev, const char *date,
                                const char *author, void *baton)
{
  commit_callback_baton_t *ccb = baton;

  *ccb->new_rev = new_rev;
  *ccb->date = date;
  *ccb->author = author;
  return SVN_NO_ERROR;
}

static svn_error_t *commit(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                           apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  const char *log_msg, *date, *author;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_boolean_t aborted;
  commit_callback_baton_t ccb;
  svn_revnum_t new_rev;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c", &log_msg));
  SVN_ERR(must_have_write_access(conn, pool, b));
  ccb.new_rev = &new_rev;
  ccb.date = &date;
  ccb.author = &author;
  /* ### Note that svn_repos_get_commit_editor actually wants a decoded URL. */
  SVN_CMD_ERR(svn_repos_get_commit_editor(&editor, &edit_baton, b->repos,
                                          svn_path_uri_decode(b->repos_url,
                                                              pool),
                                          b->fs_path, b->user,
                                          log_msg, commit_done, &ccb, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  SVN_ERR(svn_ra_svn_drive_editor(conn, pool, editor, edit_baton, &aborted));
  if (!aborted)
    {
      SVN_ERR(trivial_auth_request(conn, pool, b));

      /* In tunnel mode, deltify before answering the client, because
         answering may cause the client to terminate the connection
         and thus kill the server.  But otherwise, deltify after
         answering the client, to avoid user-visible delay. */

      if (b->tunnel)
        SVN_ERR(svn_fs_deltify_revision(b->fs, new_rev, pool));

      SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "r(?c)(?c)",
                                     new_rev, date, author));

      if (! b->tunnel)
        SVN_ERR(svn_fs_deltify_revision(b->fs, new_rev, pool));
    }
  return SVN_NO_ERROR;
}

static svn_error_t *get_file(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                             apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  const char *path, *full_path, *hex_digest;
  svn_revnum_t rev;
  svn_fs_root_t *root;
  svn_stream_t *contents;
  apr_hash_t *props = NULL;
  svn_string_t write_str;
  char buf[4096];
  apr_size_t len;
  svn_boolean_t want_props, want_contents;
  unsigned char digest[APR_MD5_DIGESTSIZE];
  svn_error_t *err, *write_err;

  /* Parse arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c(?r)bb", &path, &rev,
                                 &want_props, &want_contents));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  full_path = svn_path_join(b->fs_path, path, pool);

  /* Fetch the properties and a stream for the contents. */
  SVN_CMD_ERR(svn_fs_revision_root(&root, b->fs, rev, pool));
  SVN_CMD_ERR(svn_fs_file_md5_checksum(digest, root, full_path, pool));
  hex_digest = svn_md5_digest_to_cstring(digest, pool);
  if (want_props)
    SVN_CMD_ERR(get_props(&props, root, full_path, pool));
  if (want_contents)
    SVN_CMD_ERR(svn_fs_file_contents(&contents, root, full_path, pool));

  /* Send successful command response with revision and props. */
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w((?c)r(!", "success",
                                 hex_digest, rev));
  SVN_ERR(write_proplist(conn, pool, props));
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!))"));

  /* Now send the file's contents. */
  if (want_contents)
    {
      err = SVN_NO_ERROR;
      while (1)
        {
          len = sizeof(buf);
          err = svn_stream_read(contents, buf, &len);
          if (err)
            break;
          if (len > 0)
            {
              write_str.data = buf;
              write_str.len = len;
              SVN_ERR(svn_ra_svn_write_string(conn, pool, &write_str));
            }
          if (len < sizeof(buf))
            {
              err = svn_stream_close(contents);
              break;
            }
        }
      write_err = svn_ra_svn_write_cstring(conn, pool, "");
      if (write_err)
        {
          svn_error_clear(err);
          return write_err;
        }
      SVN_CMD_ERR(err);
      SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *get_dir(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                            apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  const char *path, *full_path, *file_path, *name, *cauthor, *cdate;
  svn_revnum_t rev;
  apr_hash_t *entries, *props = NULL, *file_props;
  apr_hash_index_t *hi;
  svn_fs_dirent_t *fsent;
  svn_dirent_t *entry;
  const void *key;
  void *val;
  svn_fs_root_t *root;
  apr_pool_t *subpool;
  svn_boolean_t want_props, want_contents;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c(?r)bb", &path, &rev,
                                 &want_props, &want_contents));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  full_path = svn_path_join(b->fs_path, path, pool);

  /* Fetch the root of the appropriate revision. */
  SVN_CMD_ERR(svn_fs_revision_root(&root, b->fs, rev, pool));

  /* Fetch the directory properties if requested. */
  if (want_props)
    SVN_CMD_ERR(get_props(&props, root, full_path, pool));

  /* Fetch the directory entries if requested. */
  if (want_contents)
    {
      SVN_CMD_ERR(svn_fs_dir_entries(&entries, root, full_path, pool));

      /* Transform the hash table's FS entries into dirents.  This probably
       * belongs in libsvn_repos. */
      subpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &key, NULL, &val);
          name = key;
          fsent = val;

          file_path = svn_path_join(full_path, name, subpool);
          entry = apr_pcalloc(pool, sizeof(*entry));

          /* kind */
          entry->kind = fsent->kind;

          /* size */
          if (entry->kind == svn_node_dir)
            entry->size = 0;
          else
            SVN_CMD_ERR(svn_fs_file_length(&entry->size, root, file_path,
                                           subpool));

          /* has_props */
          SVN_CMD_ERR(svn_fs_node_proplist(&file_props, root, file_path,
                                           subpool));
          entry->has_props = (apr_hash_count(file_props) > 0) ? TRUE : FALSE;

          /* created_rev, last_author, time */
          SVN_CMD_ERR(svn_repos_get_committed_info(&entry->created_rev, &cdate,
                                                   &cauthor, root, file_path,
                                                   subpool));
          entry->last_author = apr_pstrdup(pool, cauthor);
          if (cdate)
            SVN_CMD_ERR(svn_time_from_cstring(&entry->time, cdate, subpool));
          else
            entry->time = (time_t) -1;

          /* Store the entry. */
          apr_hash_set(entries, name, APR_HASH_KEY_STRING, entry);
          svn_pool_clear(subpool);
        }
      svn_pool_destroy(subpool);
    }

  /* Write out response. */
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w(r(!", "success", rev));
  SVN_ERR(write_proplist(conn, pool, props));
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!)(!"));
  if (want_contents)
    {
      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &key, NULL, &val);
          name = key;
          entry = val;
          cdate = (entry->time == (time_t) -1) ? NULL
            : svn_time_to_cstring(entry->time, pool);
          SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "cwnbr(?c)(?c)", name,
                                         kind_word(entry->kind),
                                         (apr_uint64_t) entry->size,
                                         entry->has_props, entry->created_rev,
                                         cdate, entry->last_author));
        }
    }
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!))"));
  return SVN_NO_ERROR;
}


static svn_error_t *update(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                           apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *target;
  svn_boolean_t recurse;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "(?r)cb", &rev, &target,
                                 &recurse));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));

  return accept_report(conn, pool, b, rev, target, NULL, TRUE, recurse, FALSE);
}

static svn_error_t *switch_cmd(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                               apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *target;
  const char *switch_url, *switch_path;
  svn_boolean_t recurse;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "(?r)cbc", &rev, &target,
                                 &recurse, &switch_url));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  SVN_CMD_ERR(get_fs_path(svn_path_uri_decode(b->repos_url, pool),
                          svn_path_uri_decode(switch_url, pool),
                          &switch_path, pool));

  return accept_report(conn, pool, b, rev, target, switch_path, TRUE, recurse,
                       TRUE);
}

static svn_error_t *status(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                           apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *target;
  svn_boolean_t recurse;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "cb?(?r)",
                                 &target, &recurse, &rev));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  return accept_report(conn, pool, b, rev, target, NULL, FALSE, recurse,
                       FALSE);
}

static svn_error_t *diff(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                         apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *target, *versus_url, *versus_path;
  svn_boolean_t recurse, ignore_ancestry;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "(?r)cbbc", &rev, &target,
                                 &recurse, &ignore_ancestry, &versus_url));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  SVN_CMD_ERR(get_fs_path(svn_path_uri_decode(b->repos_url, pool),
                          svn_path_uri_decode(versus_url, pool),
                          &versus_path, pool));

  return accept_report(conn, pool, b, rev, target, versus_path, TRUE, recurse,
                       ignore_ancestry);
}

/* Send a log entry to the client. */
static svn_error_t *log_receiver(void *baton, apr_hash_t *changed_paths,
                                 svn_revnum_t rev, const char *author,
                                 const char *date, const char *message,
                                 apr_pool_t *pool)
{
  log_baton_t *b = baton;
  svn_ra_svn_conn_t *conn = b->conn;
  apr_hash_index_t *h;
  const void *key;
  void *val;
  const char *path;
  svn_log_changed_path_t *change;
  char action[2];

  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "(!"));
  if (changed_paths)
    {
      for (h = apr_hash_first(pool, changed_paths); h; h = apr_hash_next(h))
        {
          apr_hash_this(h, &key, NULL, &val);
          path = key;
          change = val;
          action[0] = change->action;
          action[1] = '\0';
          SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "cw(?cr)", path, action,
                                         change->copyfrom_path,
                                         change->copyfrom_rev));
        }
    }
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!)r(?c)(?c)(?c)", rev, author,
                                 date, message));
  return SVN_NO_ERROR;
}

static svn_error_t *log_cmd(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                            apr_array_header_t *params, void *baton)
{
  svn_error_t *err, *write_err;
  server_baton_t *b = baton;
  svn_revnum_t start_rev, end_rev;
  const char *full_path;
  svn_boolean_t changed_paths, strict_node;
  apr_array_header_t *paths, *full_paths;
  svn_ra_svn_item_t *elt;
  int i;
  apr_uint64_t limit;
  log_baton_t lb;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "l(?r)(?r)bb?n", &paths,
                                 &start_rev, &end_rev, &changed_paths,
                                 &strict_node, &limit));

  /* if we got an unspecified number then the user didn't send us anything,
     so we assume no limit.  if it's larger than INT_MAX then someone is 
     messing with us, since we know the svn client libraries will never send
     us anything that big, so play it safe and default to no limit. */
  if (limit == SVN_RA_SVN_UNSPECIFIED_NUMBER || limit > INT_MAX)
    limit = 0;

  full_paths = apr_array_make(pool, paths->nelts, sizeof(const char *));
  for (i = 0; i < paths->nelts; i++)
    {
      elt = &((svn_ra_svn_item_t *) paths->elts)[i];
      if (elt->kind != SVN_RA_SVN_STRING)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                "Log path entry not a string");
      full_path = svn_path_join(b->fs_path, elt->u.string->data, pool);
      *((const char **) apr_array_push(full_paths)) = full_path;
    }
  SVN_ERR(trivial_auth_request(conn, pool, b));

  /* Get logs.  (Can't report errors back to the client at this point.) */
  lb.fs_path = b->fs_path;
  lb.conn = conn;
  err = svn_repos_get_logs3(b->repos, full_paths, start_rev, end_rev,
                            (int) limit, changed_paths, strict_node,
                            NULL, NULL, log_receiver, &lb, pool);

  write_err = svn_ra_svn_write_word(conn, pool, "done");
  if (write_err)
    {
      svn_error_clear(err);
      return write_err;
    }
  SVN_CMD_ERR(err);
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *check_path(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                               apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_revnum_t rev;
  const char *path, *full_path;
  svn_fs_root_t *root;
  svn_node_kind_t kind;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c(?r)", &path, &rev));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_CMD_ERR(svn_fs_youngest_rev(&rev, b->fs, pool));
  full_path = svn_path_join(b->fs_path, path, pool);
  SVN_CMD_ERR(svn_fs_revision_root(&root, b->fs, rev, pool));
  SVN_CMD_ERR(svn_fs_check_path(&kind, root, full_path, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "w", kind_word(kind)));
  return SVN_NO_ERROR;
}

static svn_error_t *get_locations(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  apr_array_header_t *params, void *baton)
{
  svn_error_t *err, *write_err;
  server_baton_t *b = baton;
  svn_revnum_t revision;
  apr_array_header_t *location_revisions, *loc_revs_proto;
  svn_ra_svn_item_t *elt;
  int i;
  const char *relative_path;
  svn_revnum_t peg_revision;
  apr_hash_t *fs_locations;
  apr_hash_index_t *iter;
  const char *abs_path;
  const void *iter_key;
  void *iter_value;

  /* Parse the arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "crl", &relative_path,
                                 &peg_revision,
                                 &loc_revs_proto));

  abs_path = svn_path_join(b->fs_path, relative_path, pool);

  location_revisions = apr_array_make(pool, loc_revs_proto->nelts,
                                      sizeof(svn_revnum_t));
  for (i = 0; i < loc_revs_proto->nelts; i++)
    {
      elt = &APR_ARRAY_IDX(loc_revs_proto, i, svn_ra_svn_item_t);
      if (elt->kind != SVN_RA_SVN_NUMBER)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                "Get-locations location revisions entry "
                                "not a revision number");
      revision = (svn_revnum_t)(elt->u.number);
      APR_ARRAY_PUSH(location_revisions, svn_revnum_t) = revision;
    }
  SVN_ERR(trivial_auth_request(conn, pool, b));

  /* All the parameters are fine - let's perform the query against the
   * repository. */

  /* We store both err and write_err here, so the client will get
   * the "done" even if there was an error in fetching the results. */

  err = svn_repos_trace_node_locations(b->fs, &fs_locations, abs_path,
                                       peg_revision, location_revisions,
                                       NULL, NULL, pool);

  /* Now, write the results to the connection. */
  if (!err)
    {
      if (fs_locations)
        {
          for (iter = apr_hash_first(pool, fs_locations); iter;
              iter = apr_hash_next(iter))
            {
              apr_hash_this(iter, &iter_key, NULL, &iter_value);
              SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "rc",
                                             *(const svn_revnum_t *)iter_key,
                                             (const char *)iter_value));
            }
        }
    }

  write_err = svn_ra_svn_write_word(conn, pool, "done");
  if (write_err)
    {
      svn_error_clear(err);
      return write_err;
    }
  SVN_CMD_ERR(err);

  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));

  return SVN_NO_ERROR;
}

/* This implements svn_write_fn_t.  Write LEN bytes starting at DATA to the
   client as a string. */
static svn_error_t *svndiff_handler(void *baton, const char *data,
                                    apr_size_t *len)
{
  file_revs_baton_t *b = baton;
  svn_string_t str;

  str.data = data;
  str.len = *len;
  return svn_ra_svn_write_string(b->conn, b->pool, &str);
}

/* This implements svn_close_fn_t.  Mark the end of the data by writing an
   empty string to the client. */
static svn_error_t *svndiff_close_handler(void *baton)
{
  file_revs_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cstring(b->conn, b->pool, ""));
  return SVN_NO_ERROR;
}

/* This implements the svn_repos_file_rev_handler_t interface. */
static svn_error_t *file_rev_handler(void *baton, const char *path,
                                     svn_revnum_t rev, apr_hash_t *rev_props,
                                     svn_txdelta_window_handler_t *d_handler,
                                     void **d_baton,
                                     apr_array_header_t *prop_diffs,
                                     apr_pool_t *pool)
{
  file_revs_baton_t *frb = baton;
  svn_stream_t *stream;

  SVN_ERR(svn_ra_svn_write_tuple(frb->conn, pool, "cr(!",
                                 path, rev));
  SVN_ERR(write_proplist(frb->conn, pool, rev_props));
  SVN_ERR(svn_ra_svn_write_tuple(frb->conn, pool, "!)(!"));
  SVN_ERR(write_prop_diffs(frb->conn, pool, prop_diffs));
  SVN_ERR(svn_ra_svn_write_tuple(frb->conn, pool, "!)"));

  /* Store the pool for the delta stream. */
  frb->pool = pool;

  /* Prepare for the delta or just write an empty string. */
  if (d_handler)
    {
      stream = svn_stream_create(baton, pool);
      svn_stream_set_write(stream, svndiff_handler);
      svn_stream_set_close(stream, svndiff_close_handler);

      svn_txdelta_to_svndiff(stream, pool, d_handler, d_baton);
    }
  else
    SVN_ERR(svn_ra_svn_write_cstring(frb->conn, pool, ""));
      
  return SVN_NO_ERROR;
}

static svn_error_t *get_file_revs(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  apr_array_header_t *params, void *baton)
{
  server_baton_t *b = baton;
  svn_error_t *err, *write_err;
  file_revs_baton_t frb;
  svn_revnum_t start_rev, end_rev;
  const char *path;
  const char *full_path;
  
  /* Parse arguments. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c(?r)(?r)",
                                 &path, &start_rev, &end_rev));
  SVN_ERR(trivial_auth_request(conn, pool, b));
  full_path = svn_path_join(b->fs_path, path, pool);

  frb.conn = conn;
  frb.pool = NULL;

  err = svn_repos_get_file_revs(b->repos, full_path, start_rev, end_rev, NULL,
                                NULL, file_rev_handler, &frb, pool);
  write_err = svn_ra_svn_write_word(conn, pool, "done");
  if (write_err)
    {
      svn_error_clear(err);
      return write_err;
    }
  SVN_CMD_ERR(err);
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));

  return SVN_NO_ERROR;
}


static const svn_ra_svn_cmd_entry_t main_commands[] = {
  { "get-latest-rev",  get_latest_rev },
  { "get-dated-rev",   get_dated_rev },
  { "change-rev-prop", change_rev_prop },
  { "rev-proplist",    rev_proplist },
  { "rev-prop",        rev_prop },
  { "commit",          commit },
  { "get-file",        get_file },
  { "get-dir",         get_dir },
  { "update",          update },
  { "switch",          switch_cmd },
  { "status",          status },
  { "diff",            diff },
  { "log",             log_cmd },
  { "check-path",      check_path },
  { "get-locations",   get_locations },
  { "get-file-revs",   get_file_revs },
  { NULL }
};

/* Skip past the scheme part of a URL, including the tunnel specification
 * if present.  Return NULL if the scheme part is invalid for ra_svn. */
static const char *skip_scheme_part(const char *url)
{
  if (strncmp(url, "svn", 3) != 0)
    return NULL;
  url += 3;
  if (*url == '+')
    url += strcspn(url, ":");
  if (strncmp(url, "://", 3) != 0)
    return NULL;
  return url + 3;
}

/* Check that PATH is a valid repository path, meaning it doesn't contain any
   '..' path segments.
   NOTE: This is similar to svn_path_is_backpath_present, but that function
   assumes the path separator is '/'.  This function also checks for
   segments delimited by the local path separator. */
static svn_boolean_t
repos_path_valid(const char *path)
{
  const char *s = path;

  while (*s)
    {
      /* Scan for the end of the segment. */
      while (*path && *path != '/' && *path != SVN_PATH_LOCAL_SEPARATOR)
        ++path;

      /* Check for '..'. */
#if WIN32
      /* On Windows, don't allow sequences of more than one character
         consisting of just dots and spaces.  Win32 functions treat
         paths such as ".. " and "......." inconsistently.  Make sure
         no one can escape out of the root. */
      if (path - s >= 2 && strspn(s, ". ") == path - s)
        return FALSE;
#else  /* ! WIN32 */
      if (path - s == 2 && s[0] == '.' && s[1] == '.')
        return FALSE;
#endif

      /* Skip all separators. */
      while (*path && (*path == '/' || *path == SVN_PATH_LOCAL_SEPARATOR))
        ++path;
      s = path;
    }

  return TRUE;
}
      
/* Look for the repository given by URL, using ROOT as the virtual
 * repository root.  If we find one, fill in the repos, fs, cfg,
 * repos_url, and fs_path fields of B. */
static svn_error_t *find_repos(const char *url, const char *root,
                               server_baton_t *b, apr_pool_t *pool)
{
  const char *path, *full_path, *repos_root, *pwdb_path;
  svn_stringbuf_t *url_buf;

  /* Skip past the scheme and authority part. */
  path = skip_scheme_part(url);
  if (path == NULL)
    return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                             "Non-svn URL passed to svn server: '%s'", url);
  path = strchr(path, '/');
  path = (path == NULL) ? "" : path + 1;

  /* Decode URI escapes from the path. */
  path = svn_path_uri_decode(path, pool);

  /* Ensure that it isn't possible to escape the root by skipping leading
     slashes and not allowing '..' segments. */
  while (*path == '/')
    ++path;
  if (!repos_path_valid(path))
    return svn_error_create(SVN_ERR_BAD_FILENAME, NULL,
                            "Couldn't determine repository path");

  /* Join the server-configured root with the client path. */
  full_path = svn_path_join(svn_path_canonicalize(root, pool),
                            svn_path_canonicalize(path, pool), pool);

  /* Search for a repository in the full path. */
  repos_root = svn_repos_find_root_path(full_path, pool);
  if (!repos_root)
    return svn_error_createf(SVN_ERR_RA_SVN_REPOS_NOT_FOUND, NULL,
                             "No repository found in '%s'", url);

  /* Open the repository and fill in b with the resulting information. */
  SVN_ERR(svn_repos_open(&b->repos, repos_root, pool));
  b->fs = svn_repos_fs(b->repos);
  b->fs_path = apr_pstrdup(pool, full_path + strlen(repos_root));
  url_buf = svn_stringbuf_create(url, pool);
  svn_path_remove_components(url_buf, svn_path_component_count(b->fs_path));
  b->repos_url = url_buf->data;

  /* Read repository configuration. */
  SVN_ERR(svn_config_read(&b->cfg, svn_repos_svnserve_conf(b->repos, pool),
                          FALSE, pool));
  svn_config_get(b->cfg, &pwdb_path, SVN_CONFIG_SECTION_GENERAL,
                 SVN_CONFIG_OPTION_PASSWORD_DB, NULL);
  if (pwdb_path)
    {
      pwdb_path = svn_path_join(svn_repos_conf_dir(b->repos, pool),
                                pwdb_path, pool);
      SVN_ERR(svn_config_read(&b->pwdb, pwdb_path, TRUE, pool));

      /* Use the repository UUID as the default realm. */
      SVN_ERR(svn_fs_get_uuid(b->fs, &b->realm, pool));
      svn_config_get(b->cfg, &b->realm, SVN_CONFIG_SECTION_GENERAL,
                     SVN_CONFIG_OPTION_REALM, b->realm);
    }
  else
    {
      b->pwdb = NULL;
      b->realm = "";
    }

  /* Make sure it's possible for the client to authenticate. */
  if (get_access(b, UNAUTHENTICATED) == NO_ACCESS
      && (get_access(b, AUTHENTICATED) == NO_ACCESS
          || (!b->tunnel_user && !b->pwdb)))
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            "No access allowed to this repository");
  return SVN_NO_ERROR;
}

/* Compute the authentication name EXTERNAL should be able to get, if any. */
static const char *get_tunnel_user(serve_params_t *params, apr_pool_t *pool)
{
  apr_uid_t uid;
  apr_gid_t gid;
  char *user;

  /* Only offer EXTERNAL for connections tunneled over a login agent. */
  if (!params->tunnel)
    return NULL;

  /* If a tunnel user was provided on the command line, use that. */
  if (params->tunnel_user)
    return params->tunnel_user;

#if APR_HAS_USER
  /* Use the current uid's name, if we can. */
  if (apr_uid_current(&uid, &gid, pool) == APR_SUCCESS
      && apr_uid_name_get(&user, uid, pool) == APR_SUCCESS)
    return user;
#endif

  /* Give up and don't offer EXTERNAL. */
  return NULL;
}

svn_error_t *serve(svn_ra_svn_conn_t *conn, serve_params_t *params,
                   apr_pool_t *pool)
{
  svn_error_t *err, *io_err;
  apr_uint64_t ver;
  const char *mech, *mecharg, *uuid, *client_url;
  apr_array_header_t *caplist;
  server_baton_t b;
  svn_boolean_t success;
  svn_ra_svn_item_t *item, *first;

  b.tunnel = params->tunnel;
  b.tunnel_user = get_tunnel_user(params, pool);
  b.read_only = params->read_only;
  b.user = NULL;
  b.cfg = NULL;  /* Ugly; can drop when we remove v1 support. */
  b.pwdb = NULL; /* Likewise */

  /* Send greeting.   When we drop support for version 1, we can
   * start sending an empty mechlist. */
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "w(nn(!", "success",
                                 (apr_uint64_t) 1, (apr_uint64_t) 2));
  SVN_ERR(send_mechs(conn, pool, &b, READ_ACCESS));
  SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!)(w))",
                                 SVN_RA_SVN_CAP_EDIT_PIPELINE));

  /* Read client response.  Because the client response form changed
   * between version 1 and version 2, we have to do some of this by
   * hand until we punt support for version 1. */
  SVN_ERR(svn_ra_svn_read_item(conn, pool, &item));
  if (item->kind != SVN_RA_SVN_LIST || item->u.list->nelts < 2)
    return SVN_NO_ERROR;
  first = &APR_ARRAY_IDX(item->u.list, 0, svn_ra_svn_item_t);
  if (first->kind != SVN_RA_SVN_NUMBER)
    return SVN_NO_ERROR;
  b.protocol_version = (int) first->u.number;
  if (b.protocol_version == 1)
    {
      /* Version 1: auth exchange is mixed with client version and
       * capability list, and happens before the client URL is received. */
      SVN_ERR(svn_ra_svn_parse_tuple(item->u.list, pool, "nw(?c)l",
                                     &ver, &mech, &mecharg, &caplist));
      SVN_ERR(svn_ra_svn_set_capabilities(conn, caplist));
      SVN_ERR(auth(conn, pool, mech, mecharg, &b, READ_ACCESS, &success));
      if (!success)
        return svn_ra_svn_flush(conn, pool);
      SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "c", &client_url));
      err = find_repos(client_url, params->root, &b, pool);
      if (!err && current_access(&b) == NO_ACCESS)
        err = svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                               "Not authorized for access");
      if (err)
        {
          io_err = svn_ra_svn_write_cmd_failure(conn, pool, err);
          svn_error_clear(err);
          SVN_ERR(io_err);
          return svn_ra_svn_flush(conn, pool);
        }
    }
  else if (b.protocol_version == 2)
    {
      /* Version 2: client sends version, capability list, and client
       * URL, and then we do an auth request. */
      SVN_ERR(svn_ra_svn_parse_tuple(item->u.list, pool, "nlc", &ver,
                                     &caplist, &client_url));
      SVN_ERR(svn_ra_svn_set_capabilities(conn, caplist));
      err = find_repos(client_url, params->root, &b, pool);
      if (!err)
        {
          SVN_ERR(auth_request(conn, pool, &b, READ_ACCESS));
          if (current_access(&b) == NO_ACCESS)
            err = svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                   "Not authorized for access");
        }
      if (err)
        {
          io_err = svn_ra_svn_write_cmd_failure(conn, pool, err);
          svn_error_clear(err);
          SVN_ERR(io_err);
          return svn_ra_svn_flush(conn, pool);
        }
    }
  else
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_get_uuid(b.fs, &uuid, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, "cc", uuid, b.repos_url));

  return svn_ra_svn_handle_commands(conn, pool, main_commands, &b);
}
