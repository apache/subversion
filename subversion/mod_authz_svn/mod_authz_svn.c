/*
 * mod_authz_svn.c: an Apache mod_dav_svn sub-module to provide path
 *                  based authorization for a Subversion repository.
 *
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
    AUTHZ_SVN_READ,
    AUTHZ_SVN_WRITE
};

typedef struct {
    int authoritative;
    const char *base_path;
    const char *access_file;
} authz_svn_config_rec;

struct parse_authz_line_baton {
    apr_pool_t *pool;
    svn_config_t *config;
    const char *user;
    int allow;
    int deny;
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
    struct parse_authz_line_baton *b = baton;

    if (*name == '@') {
        if (!group_contains_user(b->config, &name[1], b->user, b->pool))
            return TRUE;
    }
    else if (strcmp(name, b->user) && strcmp(name, "*")) {
        return TRUE;
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

static int check_access(svn_config_t *cfg,
                        const char *repos_path, const char *user,
                        int required_access, apr_pool_t *pool)
{
    const char *base_name;
    struct parse_authz_line_baton baton = { 0 };

    if (!repos_path) {
        /* XXX: Check if the user has 'required_access' _anywhere_ in the
	 * XXX: repository.  For now, make this always succeed, until
	 * XXX: we come up with a good way of figuring this out.
	 */
        return 1;
    }

    baton.pool = pool;
    baton.config = cfg;
    baton.user = user;

    svn_config_enumerate(cfg, repos_path,
                         parse_authz_line, &baton);

    base_name = repos_path;
    while (!(baton.deny & required_access)
           && !(baton.allow & required_access)) {
        if (base_name[0] == '/' && base_name[1] == '\0') {
            /* By default, deny access */
            return 0;
        }

        svn_path_split(repos_path, &repos_path, &base_name, pool);
        svn_config_enumerate(cfg, repos_path,
                             parse_authz_line, &baton);
    }

    /* XXX: We could use an 'order = deny,allow' option to make this more
     * XXX: configurable.
     */
    return !(baton.deny & required_access)
           || (baton.allow & required_access) != 0;
}


/*
 * Hooks
 */

static int auth_checker(request_rec *r)
{
    authz_svn_config_rec *conf = ap_get_module_config(r->per_dir_config,
                                                      &authz_svn_module);
    const char *dest_uri;
    apr_uri_t   parsed_dest_uri;
    const char *cleaned_uri;
    int trailing_slash;
    const char *repos_name;
    const char *relative_path;
    const char *repos_path;
    const char *dest_repos_path = NULL;
    dav_error *dav_err;
    int authz_svn_type;
    svn_config_t *access_conf = NULL;
    svn_error_t *svn_err;
    int status = OK;

    if (!conf->access_file)
        return DECLINED;

    switch (r->method_number) {
    /* All methods requiring read access to r->uri */
    case M_OPTIONS:
    case M_GET:
    case M_COPY:
    case M_PROPFIND:
    case M_REPORT:
        authz_svn_type = AUTHZ_SVN_READ;
        break;

    /* All methods requiring write access to r->uri */
    case M_MOVE:
    case M_MKCOL:
    case M_DELETE:
    case M_PUT:
    case M_PROPPATCH:
    case M_CHECKOUT:
    case M_MERGE:
    case M_MKACTIVITY:
        authz_svn_type = AUTHZ_SVN_WRITE;
        break;

    default:
        /* Require most strict access for unknown methods */
        authz_svn_type = AUTHZ_SVN_WRITE;
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
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (repos_path)
        repos_path = svn_path_join("/", repos_path, r->pool);

    svn_err = svn_config_read(&access_conf, conf->access_file, FALSE, r->pool);
    if (svn_err) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, svn_err->apr_err, r,
            "%s", svn_err->message);

        return conf->authoritative ? HTTP_UNAUTHORIZED: DECLINED;
    }

    if (!check_access(access_conf,
                      repos_path, r->user, authz_svn_type,
                      r->pool)) {
        if (conf->authoritative) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "Access denied: '%s' %s %s",
                r->user, r->method, repos_path);
            ap_note_auth_failure(r);
            return HTTP_UNAUTHORIZED;
        }

        status = DECLINED;
    }
    
    /* XXX: DELETE, MOVE, MKCOL and PUT, if the path doesn't exist yet, also
     * XXX: require write access to the parent dir of repos_path.
     */

    /* Only MOVE and COPY have a second uri we have to check access to. */
    if (r->method_number != M_MOVE
        && r->method_number != M_COPY) {
        if (status == DECLINED)
            return DECLINED;

        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
            "Access granted: '%s' %s %s", r->user, r->method, repos_path);
        return OK;
    }

    dest_uri = apr_table_get(r->headers_in, "Destination");

    /* Decline MOVE or COPY when there is no Destination uri, this will
     * cause failure.
     */
    if (!dest_uri) {
        return DECLINED;
    }

    apr_uri_parse(r->pool, dest_uri, &parsed_dest_uri);

    if (strcmp(parsed_dest_uri.hostname, r->parsed_uri.hostname)
        || strcmp(parsed_dest_uri.scheme, r->parsed_uri.scheme)) {
        /* Don't allow this, operation between different hosts/schemes.
         * XXX: Maybe we should DECLINE instead and rely on mod_dav to
         * XXX: throw an error.
         */
        return HTTP_BAD_REQUEST;
    }

    dest_uri = parsed_dest_uri.path;
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
                                &repos_name,
                                &relative_path,
                                &dest_repos_path);

    if (dav_err) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "%s  [%d, #%d]",
                      dav_err->desc, dav_err->status, dav_err->error_id);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (dest_repos_path)
        dest_repos_path = svn_path_join("/", dest_repos_path, r->pool);

    /* Check access on the first repos_path */
    if (!check_access(access_conf,
                      dest_repos_path, r->user, AUTHZ_SVN_WRITE,
                      r->pool)) {
        if (!conf->authoritative)
            return DECLINED;

        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
            "Access denied: '%s' %s %s %s",
            r->user, r->method, repos_path, dest_repos_path);
        ap_note_auth_failure(r);
        return HTTP_UNAUTHORIZED;
    }

    /* XXX: MOVE and COPY, if the path doesn't exist yet, also
     * XXX: require write access to the parent dir of dest_repos_path.
     */

    if (status == DECLINED)
        return DECLINED;

    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
        "Access granted: '%s' %s %s %s",
        r->user, r->method, repos_path, dest_repos_path);
    return OK;
}

/*
 * Module flesh
 */

static void register_hooks(apr_pool_t *p)
{
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
