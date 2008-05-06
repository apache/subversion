/*
 * svn_server.h :  declarations for the svn server
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



#ifndef SERVER_H
#define SERVER_H

#include <apr_network_io.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "svn_config.h"
#include "svn_repos.h"
#include "svn_ra_svn.h"

typedef struct server_baton_t {
  svn_repos_t *repos;
  const char *repos_name;  /* URI-encoded name of repository (not for authz) */
  svn_fs_t *fs;            /* For convenience; same as svn_repos_fs(repos) */
  svn_config_t *cfg;       /* Parsed repository svnserve.conf */
  svn_config_t *pwdb;      /* Parsed password database */
  svn_authz_t *authzdb;    /* Parsed authz rules */
  const char *authz_repos_name; /* The name of the repository for authz */
  const char *realm;       /* Authentication realm */
  const char *repos_url;   /* URL to base of repository */
  svn_stringbuf_t *fs_path;/* Decoded base in-repos path (w/ leading slash) */
  const char *user;
  svn_boolean_t tunnel;    /* Tunneled through login agent */
  const char *tunnel_user; /* Allow EXTERNAL to authenticate as this */
  svn_boolean_t read_only; /* Disallow write access (global flag) */
  svn_boolean_t use_sasl;  /* Use Cyrus SASL for authentication;
                              always false if SVN_HAVE_SASL not defined */
  apr_file_t *log_file;    /* Log filehandle. */
  apr_pool_t *pool;
} server_baton_t;

enum authn_type { UNAUTHENTICATED, AUTHENTICATED };
enum access_type { NO_ACCESS, READ_ACCESS, WRITE_ACCESS };

enum access_type get_access(server_baton_t *b, enum authn_type auth);

typedef struct serve_params_t {
  /* The virtual root of the repositories to serve.  The client URL
     path is interpreted relative to this root and is not allowed to
     escape it. */
  const char *root;

  /* True if the connection is tunneled over an ssh-like transport,
     such that the client may use EXTERNAL to authenticate as the
     current uid's username. */
  svn_boolean_t tunnel;

  /* If tunnel is true, overrides the current uid's username as the
     identity EXTERNAL authenticates as. */
  const char *tunnel_user;

  /* True if the read-only flag was specified on the command-line,
     which forces all connections to be read-only. */
  svn_boolean_t read_only;

  /* A parsed repository svnserve configuration file, ala
     svnserve.conf.  If this is NULL, then no configuration file was
     specified on the command line.  If this is non-NULL, then
     per-repository svnserve.conf are not read. */
  svn_config_t *cfg;

  /* A parsed repository password database.  If this is NULL, then
     either no svnserve configuration file was specified on the
     command line, or it was specified and it did not refer to a
     password database. */
  svn_config_t *pwdb;

  /* A parsed repository authorization database.  If this is NULL,
     then either no svnserve configuration file was specified on the
     command line, or it was specified and it did not refer to a
     authorization database. */
  svn_authz_t *authzdb;

  /* A filehandle open for writing logs to; possibly NULL. */
  apr_file_t *log_file;
} serve_params_t;

/* Serve the connection CONN according to the parameters PARAMS. */
svn_error_t *serve(svn_ra_svn_conn_t *conn, serve_params_t *params,
                   apr_pool_t *pool);

/* Load a svnserve configuration file located at FILENAME into CFG,
   any referenced password database into PWDB and any referenced
   authorization database into AUTHZDB.  If MUST_EXIST is true and
   FILENAME does not exist, then this returns an error.  BASE may be
   specified as the base path to any referenced password and
   authorization files found in FILENAME.

   If SERVER is not NULL, log the real errors with SERVER and CONN but
   return generic errors to the client.  CONN must not be NULL if SERVER
   is not NULL. */
svn_error_t *load_configs(svn_config_t **cfg,
                          svn_config_t **pwdb,
                          svn_authz_t **authzdb,
                          const char *filename,
                          svn_boolean_t must_exist,
                          const char *base,
                          server_baton_t *server,
                          svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool);

/* Initialize the Cyrus SASL library. POOL is used for allocations. */
svn_error_t *cyrus_init(apr_pool_t *pool);

/* Authenticate using Cyrus SASL. */
svn_error_t *cyrus_auth_request(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                server_baton_t *b,
                                enum access_type required,
                                svn_boolean_t needs_username);

/* Escape SOURCE into DEST where SOURCE is null-terminated and DEST is
   size BUFLEN DEST will be null-terminated.  Returns number of bytes
   written, including terminating null byte. */
apr_size_t escape_errorlog_item(char *dest, const char *source,
                                apr_size_t buflen);

/* Log ERR to LOG_FILE if LOG_FILE is not NULL.  Include REMOTE_HOST,
   USER, and REPOS in the log if they are not NULL.  Allocate temporary
   char buffers in POOL (which caller can then clear or dispose of). */
void
log_error(svn_error_t *err, apr_file_t *log_file, const char *remote_host,
          const char *user, const char *repos, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SERVER_H */
