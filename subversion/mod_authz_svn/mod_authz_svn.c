/*
 * mod_authz_svn.c: an Apache mod_dav_svn sub-module to provide path
 *                  based authorization for a Subversion repository.
 *
 * ====================================================================
 * Copyright (c) 2003-2008 CollabNet.  All rights reserved.
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



#include <httpd.h>
#include <http_config.h>
#include <http_core.h>
#include <http_request.h>
#include <http_protocol.h>
#include <http_log.h>
#include <ap_config.h>
#include <ap_provider.h>
#include <apr_uri.h>
#include <apr_lib.h>
#include <mod_dav.h>

#include "mod_dav_svn.h"
#include "mod_authz_svn.h"
#include "svn_path.h"
#include "svn_config.h"
#include "svn_string.h"
#include "svn_repos.h"


extern module AP_MODULE_DECLARE_DATA authz_svn_module;

typedef struct {
  int authoritative;
  int anonymous;
  int no_auth_when_anon_ok;
  const char *base_path;
  const char *access_file;
  const char *force_username_case;
} authz_svn_config_rec;

/*
 * Configuration
 */

static void *
create_authz_svn_dir_config(apr_pool_t *p, char *d)
{
  authz_svn_config_rec *conf = apr_pcalloc(p, sizeof(*conf));
  conf->base_path = d;

  /* By default keep the fortress secure */
  conf->authoritative = 1;
  conf->anonymous = 1;

  return conf;
}

static const command_rec authz_svn_cmds[] =
{
  AP_INIT_FLAG("AuthzSVNAuthoritative", ap_set_flag_slot,
               (void *)APR_OFFSETOF(authz_svn_config_rec, authoritative),
               OR_AUTHCFG,
               "Set to 'Off' to allow access control to be passed along to "
               "lower modules. (default is On.)"),
  AP_INIT_TAKE1("AuthzSVNAccessFile", ap_set_file_slot,
                (void *)APR_OFFSETOF(authz_svn_config_rec, access_file),
                OR_AUTHCFG,
                "Text file containing permissions of repository paths."),
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

/*
 * Get the, possibly cached, svn_authz_t for this request.
 */
static svn_authz_t *
get_access_conf(request_rec *r, authz_svn_config_rec *conf)
{
  const char *cache_key = NULL;
  void *user_data = NULL;
  svn_authz_t *access_conf = NULL;
  svn_error_t *svn_err;
  char errbuf[256];

  cache_key = apr_pstrcat(r->pool, "mod_authz_svn:",
                          conf->access_file, NULL);
  apr_pool_userdata_get(&user_data, cache_key, r->connection->pool);
  access_conf = user_data;
  if (access_conf == NULL)
    {
      svn_err = svn_repos_authz_read(&access_conf, conf->access_file,
                                     TRUE, r->connection->pool);
      if (svn_err)
        {
          ap_log_rerror(APLOG_MARK, APLOG_ERR,
                        /* If it is an error code that APR can make sense
                           of, then show it, otherwise, pass zero to avoid
                           putting "APR does not understand this error code"
                           in the error log. */
                        ((svn_err->apr_err >= APR_OS_START_USERERR &&
                          svn_err->apr_err < APR_OS_START_CANONERR) ?
                         0 : svn_err->apr_err),
                        r, "Failed to load the AuthzSVNAccessFile: %s",
                        svn_err_best_message(svn_err, errbuf, sizeof(errbuf)));
          svn_error_clear(svn_err);
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
      *c = (to_uppercase ? apr_toupper(*c) : apr_tolower(*c));
      ++c;
    }
}

/* Return the username to authorize, with case-conversion performed if
   CONF->force_username_case is set. */
static char *
get_username_to_authorize(request_rec *r, authz_svn_config_rec *conf)
{
  char *username_to_authorize = r->user;
  if (conf->force_username_case)
    {
      username_to_authorize = apr_pstrdup(r->pool, r->user);
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
  char errbuf[256];
  const char *username_to_authorize = get_username_to_authorize(r, conf);

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
    repos_path = svn_path_join("/", repos_path, r->pool);

  *repos_path_ref = apr_pstrcat(r->pool, repos_name, ":", repos_path, NULL);

  if (r->method_number == M_MOVE || r->method_number == M_COPY)
    {
      dest_uri = apr_table_get(r->headers_in, "Destination");

      /* Decline MOVE or COPY when there is no Destination uri, this will
       * cause failure.
       */
      if (!dest_uri)
        return DECLINED;

      apr_uri_parse(r->pool, dest_uri, &parsed_dest_uri);

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
        dest_repos_path = svn_path_join("/", dest_repos_path, r->pool);

      *dest_repos_path_ref = apr_pstrcat(r->pool, dest_repos_name, ":",
                                         dest_repos_path, NULL);
    }

  /* Retrieve/cache authorization file */
  access_conf = get_access_conf(r,conf);
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
          ap_log_rerror(APLOG_MARK, APLOG_ERR,
                        /* If it is an error code that APR can make
                           sense of, then show it, otherwise, pass
                           zero to avoid putting "APR does not
                           understand this error code" in the error
                           log. */
                        ((svn_err->apr_err >= APR_OS_START_USERERR &&
                          svn_err->apr_err < APR_OS_START_CANONERR) ?
                          0 : svn_err->apr_err),
                         r, "Failed to perform access control: %s",
                         svn_err_best_message(svn_err, errbuf,
                                              sizeof(errbuf)));
          svn_error_clear(svn_err);

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
          ap_log_rerror(APLOG_MARK, APLOG_ERR,
                        /* If it is an error code that APR can make sense
                           of, then show it, otherwise, pass zero to avoid
                           putting "APR does not understand this error code"
                           in the error log. */
                        ((svn_err->apr_err >= APR_OS_START_USERERR &&
                          svn_err->apr_err < APR_OS_START_CANONERR) ?
                         0 : svn_err->apr_err),
                        r, "Failed to perform access control: %s",
                        svn_err_best_message(svn_err, errbuf, sizeof(errbuf)));
          svn_error_clear(svn_err);

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

/* Log a message indicating the access control decision made about a
 * request.  FILE and LINE should be supplied via the APLOG_MARK macro.
 * ALLOWED is boolean.  REPOS_PATH and DEST_REPOS_PATH are information
 * about the request.  DEST_REPOS_PATH may be NULL. */
static void
log_access_verdict(const char *file, int line,
                   const request_rec *r, int allowed,
                   const char *repos_path, const char *dest_repos_path)
{
  int level = allowed ? APLOG_INFO : APLOG_ERR;
  const char *verdict = allowed ? "granted" : "denied";

  if (r->user)
    {
      if (dest_repos_path)
        ap_log_rerror(file, line, level, 0, r,
                      "Access %s: '%s' %s %s %s", verdict, r->user,
                      r->method, repos_path, dest_repos_path);
      else
        ap_log_rerror(file, line, level, 0, r,
                      "Access %s: '%s' %s %s", verdict, r->user,
                      r->method, repos_path);
    }
  else
    {
      if (dest_repos_path)
        ap_log_rerror(file, line, level, 0, r,
                      "Access %s: - %s %s %s", verdict,
                      r->method, repos_path, dest_repos_path);
      else
        ap_log_rerror(file, line, level, 0, r,
                      "Access %s: - %s %s", verdict,
                      r->method, repos_path);
    }
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
  svn_error_t *svn_err = NULL;
  svn_authz_t *access_conf = NULL;
  authz_svn_config_rec *conf = NULL;
  svn_boolean_t authz_access_granted = FALSE;
  char errbuf[256];
  const char *username_to_authorize;

  conf = ap_get_module_config(r->per_dir_config,
                              &authz_svn_module);
  username_to_authorize = get_username_to_authorize(r, conf);

  /* If configured properly, this should never be true, but just in case. */
  if (!conf->anonymous || !conf->access_file)
    {
      log_access_verdict(APLOG_MARK, r, 0, repos_path, NULL);
      return HTTP_FORBIDDEN;
    }

  /* Retrieve authorization file */
  access_conf = get_access_conf(r,conf);
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
                                             r->pool);
      if (svn_err)
        {
          ap_log_rerror(APLOG_MARK, APLOG_ERR,
                        /* If it is an error code that APR can make
                           sense of, then show it, otherwise, pass
                           zero to avoid putting "APR does not
                           understand this error code" in the error
                           log. */
                        ((svn_err->apr_err >= APR_OS_START_USERERR &&
                          svn_err->apr_err < APR_OS_START_CANONERR) ?
                         0 : svn_err->apr_err),
                        r, "Failed to perform access control: %s",
                        svn_err_best_message(svn_err, errbuf, sizeof(errbuf)));
          svn_error_clear(svn_err);
          return HTTP_FORBIDDEN;
        }
      if (!authz_access_granted)
        {
          log_access_verdict(APLOG_MARK, r, 0, repos_path, NULL);
          return HTTP_FORBIDDEN;
        }
    }

  log_access_verdict(APLOG_MARK, r, 1, repos_path, NULL);

  return OK;
}

/*
 * Hooks
 */

static int
access_checker(request_rec *r)
{
  authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                    &authz_svn_module);
  const char *repos_path;
  const char *dest_repos_path = NULL;
  int status;

  /* We are not configured to run */
  if (!conf->anonymous || !conf->access_file)
    return DECLINED;

  if (ap_some_auth_required(r))
    {
      /* It makes no sense to check if a location is both accessible
       * anonymous and by an authenticated user (in the same request!).
       */
      if (ap_satisfies(r) != SATISFY_ANY)
        return DECLINED;

      /* If the user is trying to authenticate, let him.  If anonymous
       * access is allowed, so is authenticated access, by definition
       * of the meaning of '*' in the access file.
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

  /* If anon access is allowed, return OK */
  status = req_check_access(r, conf, &repos_path, &dest_repos_path);
  if (status == DECLINED)
    {
      if (!conf->authoritative)
        return DECLINED;

      if (!ap_some_auth_required(r))
        log_access_verdict(APLOG_MARK, r, 0, repos_path, dest_repos_path);

      return HTTP_FORBIDDEN;
    }

  if (status != OK)
    return status;

  log_access_verdict(APLOG_MARK, r, 1, repos_path, dest_repos_path);

  return OK;
}

static int
check_user_id(request_rec *r)
{
  authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                    &authz_svn_module);
  const char *repos_path;
  const char *dest_repos_path = NULL;
  int status;

  /* We are not configured to run, or, an earlier module has already
   * authenticated this request. */
  if (!conf->access_file || !conf->no_auth_when_anon_ok || r->user)
    return DECLINED;

  /* If anon access is allowed, return OK, preventing later modules
   * from issuing an HTTP_UNAUTHORIZED.  Also pass a note to our
   * auth_checker hook that access has already been checked. */
  status = req_check_access(r, conf, &repos_path, &dest_repos_path);
  if (status == OK)
    {
      apr_table_setn(r->notes, "authz_svn-anon-ok", (const char*)1);
      log_access_verdict(APLOG_MARK, r, 1, repos_path, dest_repos_path);
      return OK;
    }

  return status;
}

static int
auth_checker(request_rec *r)
{
  authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                    &authz_svn_module);
  const char *repos_path;
  const char *dest_repos_path = NULL;
  int status;

  /* We are not configured to run */
  if (!conf->access_file)
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
          log_access_verdict(APLOG_MARK, r, 0, repos_path, dest_repos_path);
          ap_note_auth_failure(r);
          return HTTP_FORBIDDEN;
        }
      return DECLINED;
    }

  if (status != OK)
    return status;

  log_access_verdict(APLOG_MARK, r, 1, repos_path, dest_repos_path);

  return OK;
}

/*
 * Module flesh
 */

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
