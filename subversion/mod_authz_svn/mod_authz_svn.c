/*
 * mod_authz_svn.c: an Apache mod_dav_svn sub-module to provide path
 *                  based authorization for a Subversion repository.
 *
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
#include <apr_uri.h>
#include <mod_dav.h>

#include "mod_dav_svn.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_config.h"
#include "svn_string.h"


module AP_MODULE_DECLARE_DATA authz_svn_module;

enum {
    AUTHZ_SVN_NONE = 0,
    AUTHZ_SVN_READ = 1,
    AUTHZ_SVN_WRITE = 2,
    AUTHZ_SVN_RECURSIVE = 4
};

typedef struct {
    int authoritative;
    int anonymous;
    const char *base_path;
    const char *access_file;
} authz_svn_config_rec;

struct parse_authz_baton {
    apr_pool_t *pool;
    svn_config_t *config;
    const char *user;
    int allow;
    int deny;

    int required_access;
    const char *repos_path;
    const char *qualified_repos_path;

    int access;
};

/*
 * Configuration
 */

static void *create_authz_svn_dir_config(apr_pool_t *p, char *d)
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
                 "Set to 'Off' to skip access control when no authenticated "
                 "user is required. (default is On.)"),
    { NULL }
};


/*
 * Access checking
 */

static int group_contains_user(svn_config_t *cfg,
    const char *group, const char *user, apr_pool_t *pool)
{
    const char *value;
    apr_array_header_t *list;
    int i;

    svn_config_get(cfg, &value, "groups", group, "");
    list = svn_cstring_split(value, ",", TRUE, pool);

    for (i = 0; i < list->nelts; i++) {
       const char *group_user = APR_ARRAY_IDX(list, i, char *);
       if (!strcmp(user, group_user))
           return 1;
    }

    return 0;
}

static svn_boolean_t parse_authz_line(const char *name, const char *value,
                                      void *baton)
{
    struct parse_authz_baton *b = baton;

    if (strcmp(name, "*")) {
        if (!b->user) {
            return TRUE;
        }

        if (*name == '@') {
            if (!group_contains_user(b->config, &name[1], b->user, b->pool))
                return TRUE;
        }
        else if (strcmp(name, b->user)) {
            return TRUE;
        }
    }

    if (ap_strchr_c(value, 'r')) {
        b->allow |= AUTHZ_SVN_READ;
    }
    else {
        b->deny |= AUTHZ_SVN_READ;
    }

    if (ap_strchr_c(value, 'w')) {
        b->allow |= AUTHZ_SVN_WRITE;
    }
    else {
        b->deny |= AUTHZ_SVN_WRITE;
    }

    ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, b->pool,
                  "%s = %s => allow = %i, deny = %i",
                  name, value, b->allow, b->deny);

    return TRUE;
}

/*
 * Return TRUE when ACCESS has been determined.
 */
static int parse_authz_lines(svn_config_t *cfg,
                             const char *repos_name, const char *repos_path,
                             const char *user,
                             int required_access, int *access,
                             apr_pool_t *pool)
{
    const char *qualified_repos_path;
    struct parse_authz_baton baton = { 0 };

    baton.pool = pool;
    baton.config = cfg;
    baton.user = user;

    /* First try repos specific */
    qualified_repos_path = apr_pstrcat(pool, repos_name, ":", repos_path,
                                       NULL);
    svn_config_enumerate(cfg, qualified_repos_path,
                         parse_authz_line, &baton);
    *access = !(baton.deny & required_access)
              || (baton.allow & required_access);

    if ((baton.deny & required_access)
        || (baton.allow & required_access))
        return TRUE;

    svn_config_enumerate(cfg, repos_path,
                         parse_authz_line, &baton);
    *access = !(baton.deny & required_access)
              || (baton.allow & required_access);

    return (baton.deny & required_access)
           || (baton.allow & required_access);
}

static svn_boolean_t parse_authz_section(const char *section_name,
                                         void *baton)
{
  struct parse_authz_baton *b = baton;
  int conclusive;

  if (strncmp(section_name, b->qualified_repos_path,
              strlen(b->qualified_repos_path))
      && strncmp(section_name, b->repos_path,
                 strlen(b->repos_path))) {
      /* No match, move on to the next section. */
      return TRUE;
  }

  b->allow = b->deny = 0;
  svn_config_enumerate(b->config, section_name,
                       parse_authz_line, b);

  conclusive = (b->deny & b->required_access)
               || (b->allow & b->required_access);

  b->access = !(b->deny & b->required_access)
              || (b->allow & b->required_access)
              || !conclusive;
  
  /* If access isn't denied, move on to check the next section. */
  return b->access;
}

static int parse_authz_sections(svn_config_t *cfg,
                                const char *repos_name, const char *repos_path,
                                const char *user,
                                int required_access,
                                apr_pool_t *pool)
{
    struct parse_authz_baton baton = { 0 };

    baton.pool = pool;
    baton.config = cfg;
    baton.user = user;
    baton.required_access = required_access;
    baton.repos_path = repos_path;
    baton.qualified_repos_path = apr_pstrcat(pool, repos_name, ":",
                                             repos_path, NULL);
    
    baton.access = 1; /* Allow by default */
    svn_config_enumerate_sections(cfg, parse_authz_section, &baton);

    return baton.access;
}

static int check_access(svn_config_t *cfg, const char *repos_name,
                        const char *repos_path, const char *user,
                        int required_access, apr_pool_t *pool)
{
    const char *base_name;
    const char *original_repos_path = repos_path;
    int access;

    if (!repos_path) {
        /* XXX: Check if the user has 'required_access' _anywhere_ in the
         * XXX: repository.  For now, make this always succeed, until
         * XXX: we come up with a good way of figuring this out.
         */
        return 1;
    }

    base_name = repos_path;
    while (!parse_authz_lines(cfg, repos_name, repos_path,
                              user, required_access, &access,
                              pool)) {
        if (base_name[0] == '/' && base_name[1] == '\0') {
            /* By default, deny access */
            return 0;
        }

        svn_path_split(repos_path, &repos_path, &base_name, pool);
    }

    if (access && (required_access & AUTHZ_SVN_RECURSIVE) != 0) {
        /* Check access on entries below the current repos path */
        access = parse_authz_sections(cfg,
                                      repos_name, original_repos_path,
                                      user, required_access,
                                      pool);
    }

    return access;
}

/* Check if the current request R is allowed.  Upon exit *REPOS_PATH_REF
 * will contain the path and repository name that an operation was requested
 * on in the form 'name:path'.  *DEST_REPOS_PATH_REF will contain the
 * destination path if the requested operation was a MOVE or a COPY.
 * Returns OK when access is allowed, DECLINED when it isn't, or an HTTP_
 * error code when an error occurred.
 */
static int req_check_access(request_rec *r,
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
    int authz_svn_type = 0;
    svn_config_t *access_conf = NULL;
    svn_error_t *svn_err;
    const char *cache_key;
    void *user_data;

    switch (r->method_number) {
    /* All methods requiring read access to all subtrees of r->uri */
    case M_COPY:
        authz_svn_type |= AUTHZ_SVN_RECURSIVE;

    /* All methods requiring read access to r->uri */
    case M_OPTIONS:
    case M_GET:
    case M_PROPFIND:
    case M_REPORT:
        authz_svn_type |= AUTHZ_SVN_READ;
        break;

    /* All methods requiring write access to all subtrees of r->uri */
    case M_MOVE:
    case M_DELETE:
        authz_svn_type |= AUTHZ_SVN_RECURSIVE;

    /* All methods requiring write access to r->uri */
    case M_MKCOL:
    case M_PUT:
    case M_PROPPATCH:
    case M_CHECKOUT:
    case M_MERGE:
    case M_MKACTIVITY:
        authz_svn_type |= AUTHZ_SVN_WRITE;
        break;

    default:
        /* Require most strict access for unknown methods */
        authz_svn_type |= AUTHZ_SVN_WRITE|AUTHZ_SVN_RECURSIVE;
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
    if (dav_err) {
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
    if (r->method_number == M_MERGE) {
        repos_path = NULL;
    }

    if (repos_path)
        repos_path = svn_path_join("/", repos_path, r->pool);

    *repos_path_ref = apr_pstrcat(r->pool, repos_name, ":", repos_path, NULL);

    if (r->method_number == M_MOVE || r->method_number == M_COPY) {
        dest_uri = apr_table_get(r->headers_in, "Destination");

        /* Decline MOVE or COPY when there is no Destination uri, this will
         * cause failure.
         */
        if (!dest_uri)
            return DECLINED;

        apr_uri_parse(r->pool, dest_uri, &parsed_dest_uri);

        dest_uri = parsed_dest_uri.path;
        ap_unescape_url((char *)dest_uri);
        if (strncmp(dest_uri, conf->base_path, strlen(conf->base_path))) {
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

        if (dav_err) {
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
    cache_key = apr_pstrcat(r->pool, "mod_authz_svn:", conf->access_file, NULL);
    apr_pool_userdata_get(&user_data, cache_key, r->connection->pool);
    access_conf = user_data;
    if (access_conf == NULL) {
        svn_err = svn_config_read(&access_conf, conf->access_file, FALSE,
                                  r->connection->pool);
        if (svn_err) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, svn_err->apr_err, r,
                          "%s", svn_err->message);

            return DECLINED;
        }

        /* Cache the open repos for the next request on this connection */
        apr_pool_userdata_set(access_conf, cache_key,
                              NULL, r->connection->pool);
    }

    if (!check_access(access_conf,
                      repos_name, repos_path,
                      r->user, authz_svn_type,
                      r->pool)) {
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
    if (r->method_number != M_MOVE
        && r->method_number != M_COPY) {
        return OK;
    }

    /* Check access on the first repos_path */
    if (!check_access(access_conf,
                      dest_repos_name, dest_repos_path,
                      r->user, AUTHZ_SVN_WRITE|AUTHZ_SVN_RECURSIVE,
                      r->pool)) {
        return DECLINED;
    }

    /* XXX: MOVE and COPY, if the path doesn't exist yet, also
     * XXX: require write access to the parent dir of dest_repos_path.
     */

    return OK;
}

/*
 * Hooks
 */

static int access_checker(request_rec *r)
{
    authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                      &authz_svn_module);
    const char *repos_path;
    const char *dest_repos_path = NULL;
    int status;

    /* We are not configured to run */
    if (!conf->anonymous || !conf->access_file)
        return DECLINED;

    if (ap_some_auth_required(r)) {
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
                          ? "Proxy-Authorization" : "Authorization")) {
            /* Given Satisfy Any is in effect, we have to forbid access
             * to let the auth_checker hook have a go at it.
             */
            return HTTP_FORBIDDEN;
        }
    }

    /* If anon access is allowed, return OK */
    status = req_check_access(r, conf, &repos_path, &dest_repos_path);
    if (status == DECLINED) {
        if (!conf->authoritative)
            return DECLINED;

        if (!ap_some_auth_required(r)) {
            if (dest_repos_path) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                    "Access denied: - %s %s %s",
                    r->method, repos_path, dest_repos_path);
            }
            else {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                    "Access denied: - %s %s",
                    r->method, repos_path);
            }

        }

        return HTTP_FORBIDDEN;
    }

    if (status != OK)
        return status;

    if (dest_repos_path) {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
            "Access granted: - %s %s %s",
            r->method, repos_path, dest_repos_path);
    }
    else {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
            "Access granted: - %s %s",
            r->method, repos_path);
    }

    return OK;
}

static int auth_checker(request_rec *r)
{
    authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                      &authz_svn_module);
    const char *repos_path;
    const char *dest_repos_path = NULL;
    int status;

    /* We are not configured to run */
    if (!conf->access_file)
        return DECLINED;

    status = req_check_access(r, conf, &repos_path, &dest_repos_path);
    if (status == DECLINED) {
        if (conf->authoritative) {
            if (dest_repos_path) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                    "Access denied: '%s' %s %s %s",
                    r->user, r->method, repos_path, dest_repos_path);
            }
            else {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                    "Access denied: '%s' %s %s",
                    r->user, r->method, repos_path);
            }
            ap_note_auth_failure(r);
            return HTTP_UNAUTHORIZED;
        }

        return DECLINED;
    }

    if (status != OK)
        return status;

    if (dest_repos_path) {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
            "Access granted: '%s' %s %s %s",
            r->user, r->method, repos_path, dest_repos_path);
    }
    else {
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
            "Access granted: '%s' %s %s",
            r->user, r->method, repos_path);
    }

    return OK;
}

/*
 * Module flesh
 */

static void register_hooks(apr_pool_t *p)
{
    ap_hook_access_checker(access_checker, NULL, NULL, APR_HOOK_LAST);
    ap_hook_auth_checker(auth_checker, NULL, NULL, APR_HOOK_FIRST);
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
