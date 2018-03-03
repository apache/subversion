/*
 * mod_authz_svn.c: an Apache mod_dav_svn sub-module to provide path
 *                  based authorization for a Subversion repository.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#include <httpd.h>
#include <http_config.h>
#include <http_core.h>
#include <http_request.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_config.h>
#include <ap_config.h>
#include <ap_provider.h>
#include <ap_mmn.h>
#include <apr_uri.h>
#include <apr_lib.h>
#include <mod_dav.h>

#include "mod_dav_svn.h"
#include "mod_authz_svn.h"
#include "svn_path.h"
#include "svn_config.h"
#include "svn_string.h"
#include "svn_repos.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "private/svn_fspath.h"

/* The apache headers define these and they conflict with our definitions. */
#ifdef PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif
#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif
#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif
#include "svn_private_config.h"

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(authz_svn);
#else
/* This is part of the APLOG_USE_MODULE() macro in httpd-2.3 */
extern module AP_MODULE_DECLARE_DATA authz_svn_module;
#endif

typedef struct authz_svn_config_rec {
  int authoritative;
  int anonymous;
  int no_auth_when_anon_ok;
  const char *base_path;
  const char *access_file;
  const char *repo_relative_access_file;
  const char *groups_file;
  const char *force_username_case;
} authz_svn_config_rec;

/* version where ap_some_auth_required breaks */
#if AP_MODULE_MAGIC_AT_LEAST(20060110,0)
/* first version with force_authn hook and ap_some_authn_required()
   which allows us to work without ap_some_auth_required() */
#  if AP_MODULE_MAGIC_AT_LEAST(20120211,47) || defined(SVN_USE_FORCE_AUTHN)
#    define USE_FORCE_AUTHN 1
#    define IN_SOME_AUTHN_NOTE "authz_svn-in-some-authn"
#    define FORCE_AUTHN_NOTE "authz_svn-force-authn"
#  else 
     /* ap_some_auth_required() is busted and no viable alternative exists */
#    ifndef SVN_ALLOW_BROKEN_HTTPD_AUTH
#      error This Apache httpd has broken auth (CVE-2015-3184)
#    else
       /* user wants to build anyway */
#      define USE_FORCE_AUTHN 0
#    endif
#  endif
#else
   /* old enough that ap_some_auth_required() still works */
#  define USE_FORCE_AUTHN 0
#endif

/*
 * Configuration
 */

/* Implements the #create_dir_config method of Apache's #module vtable. */
static void *
create_authz_svn_dir_config(apr_pool_t *p, char *d)
{
  authz_svn_config_rec *conf = apr_pcalloc(p, sizeof(*conf));
  conf->base_path = d;

  if (d)
    conf->base_path = svn_urlpath__canonicalize(d, p);

  /* By default keep the fortress secure */
  conf->authoritative = 1;
  conf->anonymous = 1;

  return conf;
}

/* canonicalize ACCESS_FILE based on the type of argument.
 * If SERVER_RELATIVE is true, ACCESS_FILE is a relative
 * path then ACCESS_FILE is converted to an absolute
 * path rooted at the server root.
 * Returns NULL if path is not valid.*/
static const char *
canonicalize_access_file(const char *access_file,
                         svn_boolean_t server_relative,
                         apr_pool_t *pool)
{
  if (svn_path_is_url(access_file))
    {
      access_file = svn_uri_canonicalize(access_file, pool);
    }
  else if (!svn_path_is_repos_relative_url(access_file))
    {
      if (server_relative)
        {
          access_file = ap_server_root_relative(pool, access_file);
          if (access_file == NULL)
            return NULL;
        }

      access_file = svn_dirent_internal_style(access_file, pool);
    }

  /* We don't canonicalize repos relative urls since they get
   * canonicalized before calling svn_repos_authz_read3() when they
   * are resolved. */

  return access_file;
}

static const char *
AuthzSVNAccessFile_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  authz_svn_config_rec *conf = config;

  if (conf->repo_relative_access_file != NULL)
    return "AuthzSVNAccessFile and AuthzSVNReposRelativeAccessFile "
           "directives are mutually exclusive.";

  conf->access_file = canonicalize_access_file(arg1, TRUE, cmd->pool);
  if (!conf->access_file)
    return apr_pstrcat(cmd->pool, "Invalid file path ", arg1, SVN_VA_NULL);

  return NULL;
}


static const char *
AuthzSVNReposRelativeAccessFile_cmd(cmd_parms *cmd,
                                    void *config,
                                    const char *arg1)
{
  authz_svn_config_rec *conf = config;

  if (conf->access_file != NULL)
    return "AuthzSVNAccessFile and AuthzSVNReposRelativeAccessFile "
           "directives are mutually exclusive.";

  conf->repo_relative_access_file = canonicalize_access_file(arg1, FALSE,
                                                             cmd->pool);

  if (!conf->repo_relative_access_file)
    return apr_pstrcat(cmd->pool, "Invalid file path ", arg1, SVN_VA_NULL);

  return NULL;
}

static const char *
AuthzSVNGroupsFile_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  authz_svn_config_rec *conf = config;

  conf->groups_file = canonicalize_access_file(arg1, TRUE, cmd->pool);

  if (!conf->groups_file)
    return apr_pstrcat(cmd->pool, "Invalid file path ", arg1, SVN_VA_NULL);

  return NULL;
}

/* Implements the #cmds member of Apache's #module vtable. */
static const command_rec authz_svn_cmds[] =
{
  AP_INIT_FLAG("AuthzSVNAuthoritative", ap_set_flag_slot,
               (void *)APR_OFFSETOF(authz_svn_config_rec, authoritative),
               OR_AUTHCFG,
               "Set to 'Off' to allow access control to be passed along to "
               "lower modules. (default is On.)"),
  AP_INIT_TAKE1("AuthzSVNAccessFile", AuthzSVNAccessFile_cmd,
                NULL,
                OR_AUTHCFG,
                "Path to text file containing permissions of repository "
                "paths.  Path may be an repository relative URL (^/) or "
                "absolute file:// URL to a text file in a Subversion "
                "repository."),
  AP_INIT_TAKE1("AuthzSVNReposRelativeAccessFile",
                AuthzSVNReposRelativeAccessFile_cmd,
                NULL,
                OR_AUTHCFG,
                "Path (relative to repository 'conf' directory) to text "
                "file containing permissions of repository paths. Path may "
                "be an repository relative URL (^/) or absolute file:// URL "
                "to a text file in a Subversion repository."),
  AP_INIT_TAKE1("AuthzSVNGroupsFile",
                AuthzSVNGroupsFile_cmd,
                NULL,
                OR_AUTHCFG,
                "Path to text file containing group definitions for all "
                "repositories.  Path may be an repository relative URL (^/) "
                "or absolute file:// URL to a text file in a Subversion "
                "repository."),
  AP_INIT_FLAG("AuthzSVNAnonymous", ap_set_flag_slot,
               (void *)APR_OFFSETOF(authz_svn_config_rec, anonymous),
               OR_AUTHCFG,
               "Set to 'Off' to disable two special-case behaviours of "
               "this module: (1) interaction with the 'Satisfy Any' "
               "directive, and (2) enforcement of the authorization "
               "policy even when no 'Require' directives are present. "
               "(default is On.)"),
  AP_INIT_FLAG("AuthzSVNNoAuthWhenAnonymousAllowed", ap_set_flag_slot,
               (void *)APR_OFFSETOF(authz_svn_config_rec,
                                    no_auth_when_anon_ok),
               OR_AUTHCFG,
               "Set to 'On' to suppress authentication and authorization "
               "for requests which anonymous users are allowed to perform. "
               "(default is Off.)"),
  AP_INIT_TAKE1("AuthzForceUsernameCase", ap_set_string_slot,
                (void *)APR_OFFSETOF(authz_svn_config_rec,
                                     force_username_case),
                OR_AUTHCFG,
                "Set to 'Upper' or 'Lower' to convert the username before "
                "checking for authorization."),
  { NULL }
};


/* The macros LOG_ARGS_SIGNATURE and LOG_ARGS_CASCADE are expanded as formal
 * and actual parameters to log_access_verdict with respect to HTTPD version.
 */
#if AP_MODULE_MAGIC_AT_LEAST(20100606,0)
#define LOG_ARGS_SIGNATURE const char *file, int line, int module_index
#define LOG_ARGS_CASCADE file, line, module_index
#else
#define LOG_ARGS_SIGNATURE const char *file, int line
#define LOG_ARGS_CASCADE file, line
#endif

/* Log a message indicating the access control decision made about a
 * request.  The macro LOG_ARGS_SIGNATURE expands to FILE, LINE and
 * MODULE_INDEX in HTTPD 2.3 as APLOG_MARK macro has been changed for
 * per-module loglevel configuration.  It expands to FILE and LINE
 * in older server versions.  ALLOWED is boolean.
 * REPOS_PATH and DEST_REPOS_PATH are information
 * about the request.  DEST_REPOS_PATH may be NULL.
 * Non-zero IS_SUBREQ_BYPASS means that this authorization check was
 * implicitly requested using 'subrequest bypass' callback from
 * mod_dav_svn.
 */
static void
log_access_verdict(LOG_ARGS_SIGNATURE,
                   const request_rec *r, int allowed, int is_subreq_bypass,
                   const char *repos_path, const char *dest_repos_path)
{
  int level = allowed ? APLOG_INFO : APLOG_ERR;
  const char *verdict = allowed ? "granted" : "denied";

  /* Use less important log level for implicit sub-request authorization
     checks. */
  if (is_subreq_bypass)
    level = APLOG_INFO;
  else if (r->main && r->method_number == M_GET)
    level = APLOG_INFO;

  if (r->user)
    {
      if (dest_repos_path)
        ap_log_rerror(LOG_ARGS_CASCADE, level, 0, r,
                      "Access %s: '%s' %s %s %s", verdict, r->user,
                      r->method, repos_path, dest_repos_path);
      else
        ap_log_rerror(LOG_ARGS_CASCADE, level, 0, r,
                      "Access %s: '%s' %s %s", verdict, r->user,
                      r->method, repos_path);
    }
  else
    {
      if (dest_repos_path)
        ap_log_rerror(LOG_ARGS_CASCADE, level, 0, r,
                      "Access %s: - %s %s %s", verdict,
                      r->method, repos_path, dest_repos_path);
      else
        ap_log_rerror(LOG_ARGS_CASCADE, level, 0, r,
                      "Access %s: - %s %s", verdict,
                      r->method, repos_path);
    }
}

/* Log a message indiciating the ERR encountered during the request R.
 * LOG_ARGS_SIGNATURE expands as in log_access_verdict() above.
 * PREFIX is inserted at the start of the message.  The rest of the
 * message is generated by combining the message for each error in the
 * chain of ERR, excluding for trace errors.  ERR will be cleared
 * when finished. */
static void
log_svn_error(LOG_ARGS_SIGNATURE,
              request_rec *r, const char *prefix,
              svn_error_t *err, apr_pool_t *scratch_pool)
{
  svn_error_t *err_pos = svn_error_purge_tracing(err);
  svn_stringbuf_t *buff = svn_stringbuf_create(prefix, scratch_pool);

  /* Build the error chain into a space separated stringbuf. */
  while (err_pos)
    {
      svn_stringbuf_appendbyte(buff, ' ');
      if (err_pos->message)
        {
          svn_stringbuf_appendcstr(buff, err_pos->message);
        }
      else
        {
          char strerr[256];

          svn_stringbuf_appendcstr(buff, svn_strerror(err->apr_err, strerr,
                                                       sizeof(strerr)));
        }

      err_pos = err_pos->child;
    }

  ap_log_rerror(LOG_ARGS_CASCADE, APLOG_ERR,
                /* If it is an error code that APR can make sense of, then
                   show it, otherwise, pass zero to avoid putting "APR does
                   not understand this error code" in the error log. */
                ((err->apr_err >= APR_OS_START_USERERR &&
                  err->apr_err < APR_OS_START_CANONERR) ?
                 0 : err->apr_err),
                r, "%s", buff->data);

  svn_error_clear(err);
}

/* Resolve *PATH into an absolute canonical URL iff *PATH is a repos-relative
 * URL.  If *REPOS_URL is NULL convert REPOS_PATH into a file URL stored
 * in *REPOS_URL, if *REPOS_URL is not null REPOS_PATH is ignored.  The
 * resulting *REPOS_URL will be used as the root of the repos-relative URL.
 * The result will be stored in *PATH. */
static svn_error_t *
resolve_repos_relative_url(const char **path, const char **repos_url,
                           const char *repos_path, apr_pool_t *pool)
{
  if (svn_path_is_repos_relative_url(*path))
    {
      if (!*repos_url)
        SVN_ERR(svn_uri_get_file_url_from_dirent(repos_url, repos_path, pool));

      SVN_ERR(svn_path_resolve_repos_relative_url(path, *path,
                                                  *repos_url, pool));
      *path = svn_uri_canonicalize(*path, pool);
    }

  return SVN_NO_ERROR;
}

/*
 * Get the, possibly cached, svn_authz_t for this request.
 */
static svn_authz_t *
get_access_conf(request_rec *r, authz_svn_config_rec *conf,
                apr_pool_t *scratch_pool)
{
  const char *cache_key = NULL;
  const char *access_file;
  const char *groups_file;
  const char *repos_path;
  const char *repos_url = NULL;
  void *user_data = NULL;
  svn_authz_t *access_conf = NULL;
  svn_error_t *svn_err = SVN_NO_ERROR;
  dav_error *dav_err;

  dav_err = dav_svn_get_repos_path2(r, conf->base_path, &repos_path, scratch_pool);
  if (dav_err)
    {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "%s", dav_err->desc);
      return NULL;
    }

  if (conf->repo_relative_access_file)
    {
      access_file = conf->repo_relative_access_file;
      if (!svn_path_is_repos_relative_url(access_file) &&
          !svn_path_is_url(access_file))
        {
          access_file = svn_dirent_join_many(scratch_pool, repos_path, "conf",
                                             conf->repo_relative_access_file,
                                             SVN_VA_NULL);
        }
    }
  else
    {
      access_file = conf->access_file;
    }
  groups_file = conf->groups_file;

  svn_err = resolve_repos_relative_url(&access_file, &repos_url, repos_path,
                                       scratch_pool);
  if (svn_err)
    {
      log_svn_error(APLOG_MARK, r,
                    conf->repo_relative_access_file ?
                    "Failed to load the AuthzSVNReposRelativeAccessFile:" :
                    "Failed to load the AuthzSVNAccessFile:",
                    svn_err, scratch_pool);
      return NULL;
    }

  ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                "Path to authz file is %s", access_file);

  if (groups_file)
    {
      svn_err = resolve_repos_relative_url(&groups_file, &repos_url, repos_path,
                                           scratch_pool);
      if (svn_err)
        {
          log_svn_error(APLOG_MARK, r,
                        "Failed to load the AuthzSVNGroupsFile:",
                        svn_err, scratch_pool);
          return NULL;
        }

      ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                    "Path to groups file is %s", groups_file);
    }

  cache_key = apr_pstrcat(scratch_pool, "mod_authz_svn:",
                          access_file, groups_file, SVN_VA_NULL);
  apr_pool_userdata_get(&user_data, cache_key, r->connection->pool);
  access_conf = user_data;
  if (access_conf == NULL)
    {
      svn_err = svn_repos_authz_read3(&access_conf, access_file,
                                      groups_file, TRUE, NULL,
                                      r->connection->pool,
                                      scratch_pool);

      if (svn_err)
        {
          log_svn_error(APLOG_MARK, r,
                        "Failed to load the mod_authz_svn config:",
                        svn_err, scratch_pool);
          access_conf = NULL;
        }
      else
        {
          /* Cache the open repos for the next request on this connection */
          apr_pool_userdata_set(access_conf, cache_key,
                                NULL, r->connection->pool);
        }
    }
  return access_conf;
}

/* Convert TEXT to upper case if TO_UPPERCASE is TRUE, else
   converts it to lower case. */
static void
convert_case(char *text, svn_boolean_t to_uppercase)
{
  char *c = text;
  while (*c)
    {
      *c = (char)(to_uppercase ? apr_toupper(*c) : apr_tolower(*c));
      ++c;
    }
}

/* Return the username to authorize, with case-conversion performed if
   CONF->force_username_case is set. */
static char *
get_username_to_authorize(request_rec *r, authz_svn_config_rec *conf,
                          apr_pool_t *pool)
{
  char *username_to_authorize = r->user;
  if (username_to_authorize && conf->force_username_case)
    {
      username_to_authorize = apr_pstrdup(pool, r->user);
      convert_case(username_to_authorize,
                   strcasecmp(conf->force_username_case, "upper") == 0);
    }
  return username_to_authorize;
}

/* Check if the current request R is allowed.  Upon exit *REPOS_PATH_REF
 * will contain the path and repository name that an operation was requested
 * on in the form 'name:path'.  *DEST_REPOS_PATH_REF will contain the
 * destination path if the requested operation was a MOVE or a COPY.
 * Returns OK when access is allowed, DECLINED when it isn't, or an HTTP_
 * error code when an error occurred.
 */
static int
req_check_access(request_rec *r,
                 authz_svn_config_rec *conf,
                 const char **repos_path_ref,
                 const char **dest_repos_path_ref)
{
  const char *dest_uri;
  apr_uri_t parsed_dest_uri;
  const char *cleaned_uri;
  int trailing_slash;
  const char *repos_name;
  const char *dest_repos_name;
  const char *relative_path;
  const char *repos_path;
  const char *dest_repos_path = NULL;
  dav_error *dav_err;
  svn_repos_authz_access_t authz_svn_type = svn_authz_none;
  svn_boolean_t authz_access_granted = FALSE;
  svn_authz_t *access_conf = NULL;
  svn_error_t *svn_err;
  const char *username_to_authorize = get_username_to_authorize(r, conf,
                                                                r->pool);

  switch (r->method_number)
    {
      /* All methods requiring read access to all subtrees of r->uri */
      case M_COPY:
        authz_svn_type |= svn_authz_recursive;

      /* All methods requiring read access to r->uri */
      case M_OPTIONS:
      case M_GET:
      case M_PROPFIND:
      case M_REPORT:
        authz_svn_type |= svn_authz_read;
        break;

      /* All methods requiring write access to all subtrees of r->uri */
      case M_MOVE:
      case M_DELETE:
        authz_svn_type |= svn_authz_recursive;

      /* All methods requiring write access to r->uri */
      case M_MKCOL:
      case M_PUT:
      case M_PROPPATCH:
      case M_CHECKOUT:
      case M_MERGE:
      case M_MKACTIVITY:
      case M_LOCK:
      case M_UNLOCK:
        authz_svn_type |= svn_authz_write;
        break;

      default:
        /* Require most strict access for unknown methods */
        authz_svn_type |= svn_authz_write | svn_authz_recursive;
        break;
    }

  if (strcmp(svn_urlpath__canonicalize(r->uri, r->pool), conf->base_path) == 0)
    {
      /* Do no access control when conf->base_path(as configured in <Location>)
       * and given uri are same. The reason for such relaxation of access
       * control is "This module is meant to control access inside the
       * repository path, in this case inside PATH is empty and hence
       * dav_svn_split_uri fails saying no repository name present".
       * One may ask it will allow access to '/' inside the repository if
       * repository is served via SVNPath instead of SVNParentPath.
       * It does not, The other methods(PROPFIND, MKACTIVITY) for
       * accomplishing the operation takes care of making a request to
       * proper URL */
      return OK;
    }

  dav_err = dav_svn_split_uri(r,
                              r->uri,
                              conf->base_path,
                              &cleaned_uri,
                              &trailing_slash,
                              &repos_name,
                              &relative_path,
                              &repos_path);
  if (dav_err)
    {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                    "%s  [%d, #%d]",
                    dav_err->desc, dav_err->status, dav_err->error_id);
      /* Ensure that we never allow access by dav_err->status */
      return (dav_err->status != OK && dav_err->status != DECLINED) ?
              dav_err->status : HTTP_INTERNAL_SERVER_ERROR;
    }

  /* Ignore the URI passed to MERGE, like mod_dav_svn does.
   * See issue #1821.
   * XXX: When we start accepting a broader range of DeltaV MERGE
   * XXX: requests, this should be revisited.
   */
  if (r->method_number == M_MERGE)
    repos_path = NULL;

  if (repos_path)
    repos_path = svn_fspath__canonicalize(repos_path, r->pool);

  *repos_path_ref = apr_pstrcat(r->pool, repos_name, ":", repos_path,
                                SVN_VA_NULL);

  if (r->method_number == M_MOVE || r->method_number == M_COPY)
    {
      apr_status_t status;

      dest_uri = apr_table_get(r->headers_in, "Destination");

      /* Decline MOVE or COPY when there is no Destination uri, this will
       * cause failure.
       */
      if (!dest_uri)
        return DECLINED;

      status = apr_uri_parse(r->pool, dest_uri, &parsed_dest_uri);
      if (status)
        {
          ap_log_rerror(APLOG_MARK, APLOG_ERR, status, r,
                        "Invalid URI in Destination header");
          return HTTP_BAD_REQUEST;
        }
      if (!parsed_dest_uri.path)
        {
          ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                        "Invalid URI in Destination header");
          return HTTP_BAD_REQUEST;
        }

      ap_unescape_url(parsed_dest_uri.path);
      dest_uri = parsed_dest_uri.path;
      if (strncmp(dest_uri, conf->base_path, strlen(conf->base_path)))
        {
          /* If it is not the same location, then we don't allow it.
           * XXX: Instead we could compare repository uuids, but that
           * XXX: seems a bit over the top.
           */
          return HTTP_BAD_REQUEST;
        }

      dav_err = dav_svn_split_uri(r,
                                  dest_uri,
                                  conf->base_path,
                                  &cleaned_uri,
                                  &trailing_slash,
                                  &dest_repos_name,
                                  &relative_path,
                                  &dest_repos_path);

      if (dav_err)
        {
          ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                        "%s  [%d, #%d]",
                        dav_err->desc, dav_err->status, dav_err->error_id);
          /* Ensure that we never allow access by dav_err->status */
          return (dav_err->status != OK && dav_err->status != DECLINED) ?
                  dav_err->status : HTTP_INTERNAL_SERVER_ERROR;
        }

      if (dest_repos_path)
        dest_repos_path = svn_fspath__canonicalize(dest_repos_path, r->pool);

      *dest_repos_path_ref = apr_pstrcat(r->pool, dest_repos_name, ":",
                                         dest_repos_path, SVN_VA_NULL);
    }

  /* Retrieve/cache authorization file */
  access_conf = get_access_conf(r,conf, r->pool);
  if (access_conf == NULL)
    return DECLINED;

  /* Perform authz access control.
   *
   * First test the special case where repos_path == NULL, and skip
   * calling the authz routines in that case.  This is an oddity of
   * the DAV RA method: some requests have no repos_path, but apache
   * still triggers an authz lookup for the URI.
   *
   * However, if repos_path == NULL and the request requires write
   * access, then perform a global authz lookup.  The request is
   * denied if the user commiting isn't granted any access anywhere
   * in the repository.  This is to avoid operations that involve no
   * paths (commiting an empty revision, leaving a dangling
   * transaction in the FS) being granted by default, letting
   * unauthenticated users write some changes to the repository.
   * This was issue #2388.
   *
   * XXX: For now, requesting access to the entire repository always
   * XXX: succeeds, until we come up with a good way of figuring
   * XXX: this out.
   */
  if (repos_path
      || (!repos_path && (authz_svn_type & svn_authz_write)))
    {
      svn_err = svn_repos_authz_check_access(access_conf, repos_name,
                                             repos_path,
                                             username_to_authorize,
                                             authz_svn_type,
                                             &authz_access_granted,
                                             r->pool);
      if (svn_err)
        {
          log_svn_error(APLOG_MARK, r,
                        "Failed to perform access control:",
                        svn_err, r->pool);

          return DECLINED;
        }
        if (!authz_access_granted)
          return DECLINED;
    }

  /* XXX: MKCOL, MOVE, DELETE
   * XXX: Require write access to the parent dir of repos_path.
   */

  /* XXX: PUT
   * XXX: If the path doesn't exist, require write access to the
   * XXX: parent dir of repos_path.
   */

  /* Only MOVE and COPY have a second uri we have to check access to. */
  if (r->method_number != M_MOVE && r->method_number != M_COPY)
    return OK;

  /* Check access on the destination repos_path.  Again, skip this if
     repos_path == NULL (see above for explanations) */
  if (repos_path)
    {
      svn_err = svn_repos_authz_check_access(access_conf,
                                             dest_repos_name,
                                             dest_repos_path,
                                             username_to_authorize,
                                             svn_authz_write
                                             |svn_authz_recursive,
                                             &authz_access_granted,
                                             r->pool);
      if (svn_err)
        {
          log_svn_error(APLOG_MARK, r,
                        "Failed to perform access control:",
                        svn_err, r->pool);

          return DECLINED;
        }
      if (!authz_access_granted)
        return DECLINED;
    }

  /* XXX: MOVE and COPY, if the path doesn't exist yet, also
   * XXX: require write access to the parent dir of dest_repos_path.
   */

  return OK;
}

/*
 * Implementation of subreq_bypass with scratch_pool parameter.
 */
static int
subreq_bypass2(request_rec *r,
               const char *repos_path,
               const char *repos_name,
               apr_pool_t *scratch_pool)
{
  svn_error_t *svn_err = NULL;
  svn_authz_t *access_conf = NULL;
  authz_svn_config_rec *conf = NULL;
  svn_boolean_t authz_access_granted = FALSE;
  const char *username_to_authorize;

  conf = ap_get_module_config(r->per_dir_config,
                              &authz_svn_module);
  username_to_authorize = get_username_to_authorize(r, conf, scratch_pool);

  /* If configured properly, this should never be true, but just in case. */
  if (!conf->anonymous
      || (! (conf->access_file || conf->repo_relative_access_file)))
    {
      log_access_verdict(APLOG_MARK, r, 0, TRUE, repos_path, NULL);
      return HTTP_FORBIDDEN;
    }

  /* Retrieve authorization file */
  access_conf = get_access_conf(r, conf, scratch_pool);
  if (access_conf == NULL)
    return HTTP_FORBIDDEN;

  /* Perform authz access control.
   * See similarly labeled comment in req_check_access.
   */
  if (repos_path)
    {
      svn_err = svn_repos_authz_check_access(access_conf, repos_name,
                                             repos_path,
                                             username_to_authorize,
                                             svn_authz_none|svn_authz_read,
                                             &authz_access_granted,
                                             scratch_pool);
      if (svn_err)
        {
          log_svn_error(APLOG_MARK, r,
                        "Failed to perform access control:",
                        svn_err, scratch_pool);
          return HTTP_FORBIDDEN;
        }
      if (!authz_access_granted)
        {
          log_access_verdict(APLOG_MARK, r, 0, TRUE, repos_path, NULL);
          return HTTP_FORBIDDEN;
        }
    }

  log_access_verdict(APLOG_MARK, r, 1, TRUE, repos_path, NULL);

  return OK;
}

/*
 * This function is used as a provider to allow mod_dav_svn to bypass the
 * generation of an apache request when checking GET access from
 * "mod_dav_svn/authz.c" .
 */
static int
subreq_bypass(request_rec *r,
              const char *repos_path,
              const char *repos_name)
{
  int status;
  apr_pool_t *scratch_pool;

  scratch_pool = svn_pool_create(r->pool);
  status = subreq_bypass2(r, repos_path, repos_name, scratch_pool);
  svn_pool_destroy(scratch_pool);

  return status;
}

/*
 * Hooks
 */

static int
access_checker(request_rec *r)
{
  authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                    &authz_svn_module);
  const char *repos_path = NULL;
  const char *dest_repos_path = NULL;
  int status, authn_required;

#if USE_FORCE_AUTHN
  /* Use the force_authn() hook available in 2.4.x to work securely
   * given that ap_some_auth_required() is no longer functional for our
   * purposes in 2.4.x.
   */
  int authn_configured;

  /* We are not configured to run */
  if (!conf->anonymous || apr_table_get(r->notes, IN_SOME_AUTHN_NOTE)
      || (! (conf->access_file || conf->repo_relative_access_file)))
    return DECLINED;

  /* Authentication is configured */
  authn_configured = ap_auth_type(r) != NULL;
  if (authn_configured)
    {
      /* If the user is trying to authenticate, let him.  It doesn't
       * make much sense to grant anonymous access but deny authenticated
       * users access, even though you can do that with '$anon' in the
       * access file.
       */
      if (apr_table_get(r->headers_in,
                        (PROXYREQ_PROXY == r->proxyreq)
                        ? "Proxy-Authorization" : "Authorization"))
        {
          /* Set the note to force authn regardless of what access_checker_ex
             hook requires */
          apr_table_setn(r->notes, FORCE_AUTHN_NOTE, (const char*)1);

          /* provide the proper return so the access_checker hook doesn't
           * prevent the code from continuing on to the other auth hooks */
          if (ap_satisfies(r) != SATISFY_ANY)
            return OK;
          else
            return HTTP_FORBIDDEN;
        }
    }    

#else
  /* Support for older versions of httpd that have a working
   * ap_some_auth_required() */

  /* We are not configured to run */
  if (!conf->anonymous
      || (! (conf->access_file || conf->repo_relative_access_file)))
    return DECLINED;

  authn_required = ap_some_auth_required(r);
  if (authn_required)
    {
      /* It makes no sense to check if a location is both accessible
       * anonymous and by an authenticated user (in the same request!).
       */
      if (ap_satisfies(r) != SATISFY_ANY)
        return DECLINED;

      /* If the user is trying to authenticate, let him.  It doesn't
       * make much sense to grant anonymous access but deny authenticated
       * users access, even though you can do that with '$anon' in the
       * access file.
       */
      if (apr_table_get(r->headers_in,
                        (PROXYREQ_PROXY == r->proxyreq)
                        ? "Proxy-Authorization" : "Authorization"))
        {
          /* Given Satisfy Any is in effect, we have to forbid access
           * to let the auth_checker hook have a go at it.
           */
          return HTTP_FORBIDDEN;
        }
    }
#endif

  /* If anon access is allowed, return OK */
  status = req_check_access(r, conf, &repos_path, &dest_repos_path);
  if (status == DECLINED)
    {
      if (!conf->authoritative)
        return DECLINED;

#if USE_FORCE_AUTHN
      if (authn_configured) {
          /* We have to check to see if authn is required because if so we must
           * return DECLINED rather than FORBIDDEN (403) since returning
           * the 403 leaks information about what paths may exist to
           * unauthenticated users.  Returning DECLINED means apache's request
           * handling will continue until the authn module itself generates
           * UNAUTHORIZED (401).

           * We must set a note here in order to use
           * ap_some_authn_rquired() without triggering an infinite
           * loop since the call will trigger this function to be
           * called again. */
          apr_table_setn(r->notes, IN_SOME_AUTHN_NOTE, (const char*)1);
          authn_required = ap_some_authn_required(r);
          apr_table_unset(r->notes, IN_SOME_AUTHN_NOTE);
          if (authn_required)
            return DECLINED;
      }
#else
      if (!authn_required)
#endif
        log_access_verdict(APLOG_MARK, r, 0, FALSE, repos_path, dest_repos_path);

      return HTTP_FORBIDDEN;
    }

  if (status != OK)
    return status;

  log_access_verdict(APLOG_MARK, r, 1, FALSE, repos_path, dest_repos_path);

  return OK;
}

static int
check_user_id(request_rec *r)
{
  authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                    &authz_svn_module);
  const char *repos_path = NULL;
  const char *dest_repos_path = NULL;
  int status;

  /* We are not configured to run, or, an earlier module has already
   * authenticated this request. */
  if (!conf->no_auth_when_anon_ok || r->user
      || (! (conf->access_file || conf->repo_relative_access_file)))
    return DECLINED;

  /* If anon access is allowed, return OK, preventing later modules
   * from issuing an HTTP_UNAUTHORIZED.  Also pass a note to our
   * auth_checker hook that access has already been checked. */
  status = req_check_access(r, conf, &repos_path, &dest_repos_path);
  if (status == OK)
    {
      apr_table_setn(r->notes, "authz_svn-anon-ok", (const char*)1);
      log_access_verdict(APLOG_MARK, r, 1, FALSE, repos_path, dest_repos_path);
      return OK;
    }

  return status;
}

static int
auth_checker(request_rec *r)
{
  authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                    &authz_svn_module);
  const char *repos_path = NULL;
  const char *dest_repos_path = NULL;
  int status;

  /* We are not configured to run */
  if (! (conf->access_file || conf->repo_relative_access_file))
    return DECLINED;

  /* Previous hook (check_user_id) already did all the work,
   * and, as a sanity check, r->user hasn't been set since then? */
  if (!r->user && apr_table_get(r->notes, "authz_svn-anon-ok"))
    return OK;

  status = req_check_access(r, conf, &repos_path, &dest_repos_path);
  if (status == DECLINED)
    {
      if (conf->authoritative)
        {
          log_access_verdict(APLOG_MARK, r, 0, FALSE, repos_path, dest_repos_path);
          ap_note_auth_failure(r);
          return HTTP_FORBIDDEN;
        }
      return DECLINED;
    }

  if (status != OK)
    return status;

  log_access_verdict(APLOG_MARK, r, 1, FALSE, repos_path, dest_repos_path);

  return OK;
}

#if USE_FORCE_AUTHN
static int
force_authn(request_rec *r)
{
  if (apr_table_get(r->notes, FORCE_AUTHN_NOTE))
    return OK;

  return DECLINED;
}
#endif

/*
 * Module flesh
 */

/* Implements the #register_hooks method of Apache's #module vtable. */
static void
register_hooks(apr_pool_t *p)
{
  static const char * const mod_ssl[] = { "mod_ssl.c", NULL };

  ap_hook_access_checker(access_checker, NULL, NULL, APR_HOOK_LAST);
  /* Our check_user_id hook must be before any module which will return
   * HTTP_UNAUTHORIZED (mod_auth_basic, etc.), but after mod_ssl, to
   * give SSLOptions +FakeBasicAuth a chance to work. */
  ap_hook_check_user_id(check_user_id, mod_ssl, NULL, APR_HOOK_FIRST);
  ap_hook_auth_checker(auth_checker, NULL, NULL, APR_HOOK_FIRST);
#if USE_FORCE_AUTHN
  ap_hook_force_authn(force_authn, NULL, NULL, APR_HOOK_FIRST);
#endif
  ap_register_provider(p,
                       AUTHZ_SVN__SUBREQ_BYPASS_PROV_GRP,
                       AUTHZ_SVN__SUBREQ_BYPASS_PROV_NAME,
                       AUTHZ_SVN__SUBREQ_BYPASS_PROV_VER,
                       (void*)subreq_bypass);
}

module AP_MODULE_DECLARE_DATA authz_svn_module =
{
  STANDARD20_MODULE_STUFF,
  create_authz_svn_dir_config,     /* dir config creater */
  NULL,                            /* dir merger --- default is to override */
  NULL,                            /* server config */
  NULL,                            /* merge server config */
  authz_svn_cmds,                  /* command apr_table_t */
  register_hooks                   /* register hooks */
};
