/*
 * mod_dav_svn.c: an Apache mod_dav sub-module to provide a Subversion
 *                repository.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <http_core.h>
#include <http_config.h>
#include <mod_dav.h>

#include <apr_strings.h>
#include <apr_strmatch.h>

#include "svn_version.h"
#include "svn_pools.h"

#include "dav_svn.h"


/* This is the default "special uri" used for SVN's special resources
   (e.g. working resources, activities) */
#define SVN_DEFAULT_SPECIAL_URI "!svn"

/* per-server configuration */
typedef struct {
  const char *special_uri;

} dav_svn_server_conf;

/* per-dir configuration */
typedef struct {
  const char *fs_path;          /* path to the SVN FS */
  const char *master_uri;       /* URI to the master SVN repos */
  const char *repo_name;        /* repository name */
  const char *xslt_uri;         /* XSL transform URI */
  const char *fs_parent_path;   /* path to parent of of SVN FS'es  */
  const char *root_dir;         /* our top-level */
} dav_svn_dir_conf;

#define INHERIT_VALUE(parent, child, field) \
		((child)->field ? (child)->field : (parent)->field)

/* Note: the "dav_svn" prefix is mandatory */
extern module AP_MODULE_DECLARE_DATA dav_svn_module;


static int dav_svn_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp,
                        server_rec *s)
{
    ap_add_version_component(p, "SVN/" SVN_VERSION);
    return OK;
}

static void *dav_svn_create_server_config(apr_pool_t *p, server_rec *s)
{
    return apr_pcalloc(p, sizeof(dav_svn_server_conf));
}

static void *dav_svn_merge_server_config(apr_pool_t *p,
                                         void *base, void *overrides)
{
    dav_svn_server_conf *parent = base;
    dav_svn_server_conf *child = overrides;
    dav_svn_server_conf *newconf;

    newconf = apr_pcalloc(p, sizeof(*newconf));

    newconf->special_uri = INHERIT_VALUE(parent, child, special_uri);

    return newconf;
}

static void *dav_svn_create_dir_config(apr_pool_t *p, char *dir)
{
    /* NOTE: dir==NULL creates the default per-dir config */
    dav_svn_dir_conf *newconf;

    newconf = apr_pcalloc(p, sizeof(dav_svn_dir_conf));
    newconf->root_dir = dir; 

    return newconf;
}

static void *dav_svn_merge_dir_config(apr_pool_t *p,
                                      void *base, void *overrides)
{
    dav_svn_dir_conf *parent = base;
    dav_svn_dir_conf *child = overrides;
    dav_svn_dir_conf *newconf;

    newconf = apr_pcalloc(p, sizeof(*newconf));

    newconf->fs_path = INHERIT_VALUE(parent, child, fs_path);
    newconf->repo_name = INHERIT_VALUE(parent, child, repo_name);
    newconf->master_uri = INHERIT_VALUE(parent, child, master_uri);
    newconf->xslt_uri = INHERIT_VALUE(parent, child, xslt_uri);
    newconf->fs_parent_path = INHERIT_VALUE(parent, child, fs_parent_path);
    if (parent->root_dir) {
        newconf->root_dir = parent->root_dir; 
    }
    else {
        newconf->root_dir = child->root_dir; 
    }

    return newconf;
}

static const char *dav_svn_repo_name(cmd_parms *cmd, void *config,
                                     const char *arg1)
{
  dav_svn_dir_conf *conf = config;

  conf->repo_name = apr_pstrdup(cmd->pool, arg1);

  return NULL;
}

static const char *dav_svn_master_uri(cmd_parms *cmd, void *config,
                                      const char *arg1)
{
  dav_svn_dir_conf *conf = config;

  conf->master_uri = apr_pstrdup(cmd->pool, arg1);

  return NULL;
}

static const char *dav_svn_xslt_uri(cmd_parms *cmd, void *config,
                                    const char *arg1)
{
  dav_svn_dir_conf *conf = config;

  conf->xslt_uri = apr_pstrdup(cmd->pool, arg1);

  return NULL;
}

static const char *dav_svn_path_cmd(cmd_parms *cmd, void *config,
                                    const char *arg1)
{
    dav_svn_dir_conf *conf = config;

    if (conf->fs_parent_path != NULL)
      return "SVNPath cannot be defined at same time as SVNParentPath.";

    conf->fs_path
      = svn_path_canonicalize(apr_pstrdup(cmd->pool, arg1), cmd->pool);

    return NULL;
}


static const char *dav_svn_parent_path_cmd(cmd_parms *cmd, void *config,
                                           const char *arg1)
{
    dav_svn_dir_conf *conf = config;

    if (conf->fs_path != NULL)
      return "SVNParentPath cannot be defined at same time as SVNPath.";

    conf->fs_parent_path
      = svn_path_canonicalize(apr_pstrdup(cmd->pool, arg1), cmd->pool);

    return NULL;
}

static const char *dav_svn_special_uri_cmd(cmd_parms *cmd, void *config,
                                           const char *arg1)
{
    dav_svn_server_conf *conf;
    char *uri;
    apr_size_t len;

    uri = apr_pstrdup(cmd->pool, arg1);

    /* apply a bit of processing to the thing:
       - eliminate .. and . components
       - eliminate double slashes
       - eliminate leading and trailing slashes
     */
    ap_getparents(uri);
    ap_no2slash(uri);
    if (*uri == '/')
      ++uri;
    len = strlen(uri);
    if (len > 0 && uri[len - 1] == '/')
      uri[--len] = '\0';
    if (len == 0)
      return "The special URI path must have at least one component.";

    conf = ap_get_module_config(cmd->server->module_config,
                                &dav_svn_module);
    conf->special_uri = uri;

    return NULL;
}


/** Accessor functions for the module's configuration state **/

const char *dav_svn_get_fs_path(request_rec *r)
{
    dav_svn_dir_conf *conf;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
    return conf->fs_path;
}

const char *dav_svn_get_fs_parent_path(request_rec *r)
{
    dav_svn_dir_conf *conf;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
    return conf->fs_parent_path;
}


const char *dav_svn_get_repo_name(request_rec *r)
{
    dav_svn_dir_conf *conf;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
    return conf->repo_name;
}

const char *dav_svn_get_master_uri(request_rec *r)
{
    dav_svn_dir_conf *conf;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
    return conf->master_uri;
}

const char *dav_svn_get_xslt_uri(request_rec *r)
{
    dav_svn_dir_conf *conf;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
    return conf->xslt_uri;
}

const char *dav_svn_get_special_uri(request_rec *r)
{
    dav_svn_server_conf *conf;

    conf = ap_get_module_config(r->server->module_config,
                                &dav_svn_module);
    return conf->special_uri ? conf->special_uri : SVN_DEFAULT_SPECIAL_URI;
}

static int proxy_merge_fixup(request_rec *r)
{
    dav_svn_dir_conf *conf;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

    if (conf->root_dir && conf->master_uri) {
        const char *seg;

        /* We know we can always safely handle these. */
        if (r->method_number == M_PROPFIND ||
            r->method_number == M_GET ||
            r->method_number == M_OPTIONS) {
            return OK;
        }

        seg = strstr(r->unparsed_uri, conf->root_dir);
        if (seg && (r->method_number == M_MERGE ||
            strstr(seg, dav_svn_get_special_uri(r)))) {
            seg += strlen(conf->root_dir);

            r->proxyreq = PROXYREQ_PROXY;
            r->uri = r->unparsed_uri;
            r->filename = apr_pstrcat(r->pool, "proxy:", conf->master_uri,
                                      "/", seg, NULL);
            r->handler = "proxy-server";
            ap_add_output_filter("LocationRewrite", NULL, r, r->connection);
            ap_add_output_filter("ReposRewrite", NULL, r, r->connection);
            ap_add_input_filter("IncomingRewrite", NULL, r, r->connection);
        }
    }
    return OK;
}

typedef struct locate_ctx_t
{
    const apr_strmatch_pattern *pattern;
    apr_size_t pattern_len;
    apr_uri_t uri; 
    const char *localpath;
    apr_size_t  localpath_len;
    const char *remotepath;
    apr_size_t  remotepath_len;
} locate_ctx_t;

static apr_status_t location_in_filter(ap_filter_t *f,
                                       apr_bucket_brigade *bb,
                                       ap_input_mode_t mode,
                                       apr_read_type_e block,
                                       apr_off_t readbytes)
{
    request_rec *r = f->r;
    locate_ctx_t *ctx = f->ctx;
    dav_svn_dir_conf *conf;
    apr_status_t rv;
    apr_bucket *bkt;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

    if (r->main || !conf->master_uri) {
        ap_remove_output_filter(f);
        return ap_get_brigade(f->next, bb, mode, block, readbytes);
    }

    if (!f->ctx) {
        const char *full_path;
        ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));

        apr_uri_parse(r->pool, conf->master_uri, &ctx->uri);
        ctx->remotepath = apr_pstrcat(r->pool, ctx->uri.path, "/", 
                                      dav_svn_get_special_uri(r), NULL);
        ctx->remotepath_len = strlen(ctx->remotepath);
        ctx->localpath = apr_pstrcat(r->pool, conf->root_dir, "/", 
                                     dav_svn_get_special_uri(r), NULL);
        ctx->localpath_len = strlen(ctx->localpath);
        ctx->pattern = apr_strmatch_precompile(r->pool, ctx->localpath, 0);
        ctx->pattern_len = ctx->localpath_len;
    }

    rv = ap_get_brigade(f->next, bb, mode, block, readbytes);
    if (rv) {
        return rv;
    }

    bkt = APR_BRIGADE_FIRST(bb);
    while (bkt != APR_BRIGADE_SENTINEL(bb)) {

        const char *data, *match;
        apr_size_t len;

        if (APR_BUCKET_IS_METADATA(bkt)) {
            bkt = APR_BUCKET_NEXT(bkt); 
            continue;
        }

        /* read */
        apr_bucket_read(bkt, &data, &len, APR_BLOCK_READ);
        match = apr_strmatch(ctx->pattern, data, len);
        if (match) {
            apr_bucket *next_bucket;
            char *foo;
            apr_bucket_split(bkt, match - data);
            next_bucket = APR_BUCKET_NEXT(bkt); 
            apr_bucket_split(next_bucket, ctx->pattern_len);
            bkt = APR_BUCKET_NEXT(next_bucket);
            apr_bucket_delete(next_bucket);
            next_bucket = apr_bucket_pool_create(ctx->remotepath,
                                                 ctx->remotepath_len,
                                                 r->pool, bb->bucket_alloc);
            APR_BUCKET_INSERT_BEFORE(bkt, next_bucket);
        }
        else {
            bkt = APR_BUCKET_NEXT(bkt); 
        }
    }
    return APR_SUCCESS;
}

static apr_status_t location_header_filter(ap_filter_t *f,
                                           apr_bucket_brigade *bb)
{
    request_rec *r = f->r;
    dav_svn_dir_conf *conf;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

    if (!r->main && conf->master_uri) {
        const char *location, *start_foo = NULL;
        apr_size_t master_len;

        location = apr_table_get(r->headers_out, "Location");
        if (location) {
            start_foo = strstr(location, conf->master_uri);
        }
        if (start_foo) {
            const char *new_uri;
            start_foo += strlen(conf->master_uri);
            new_uri = ap_construct_url(r->pool,
                                       apr_pstrcat(r->pool, conf->root_dir,
                                                   start_foo, NULL),
                                       r);
            apr_table_set(r->headers_out, "Location", new_uri);
        }
    }
    ap_remove_output_filter(f);
    return ap_pass_brigade(f->next, bb);
}

static apr_status_t location_body_filter(ap_filter_t *f,
                                          apr_bucket_brigade *bb)
{
    request_rec *r = f->r;
    locate_ctx_t *ctx = f->ctx;
    dav_svn_dir_conf *conf;
    apr_bucket *bkt;

    conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

    if (r->main || !conf->master_uri) {
        ap_remove_output_filter(f);
        return ap_pass_brigade(f->next, bb);
    }

    if (!f->ctx) {
        const char *full_path;
        ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));

        apr_uri_parse(r->pool, conf->master_uri, &ctx->uri);
        ctx->remotepath = apr_pstrcat(r->pool, ctx->uri.path, "/", 
                                      dav_svn_get_special_uri(r), NULL);
        ctx->remotepath_len = strlen(ctx->remotepath);
        ctx->localpath = apr_pstrcat(r->pool, conf->root_dir, "/", 
                                     dav_svn_get_special_uri(r), NULL);
        ctx->localpath_len = strlen(ctx->localpath);
        ctx->pattern = apr_strmatch_precompile(r->pool, ctx->remotepath, 0);
        ctx->pattern_len = ctx->remotepath_len;
    }

    bkt = APR_BRIGADE_FIRST(bb);
    while (bkt != APR_BRIGADE_SENTINEL(bb)) {

        const char *data, *match;
        apr_size_t len;

        /* read */
        apr_bucket_read(bkt, &data, &len, APR_BLOCK_READ);
        match = apr_strmatch(ctx->pattern, data, len);
        if (match) {
            apr_bucket *next_bucket;
            char *foo;
            apr_bucket_split(bkt, match - data);
            next_bucket = APR_BUCKET_NEXT(bkt); 
            apr_bucket_split(next_bucket, ctx->pattern_len);
            bkt = APR_BUCKET_NEXT(next_bucket);
            apr_bucket_delete(next_bucket);
            next_bucket = apr_bucket_pool_create(ctx->localpath,
                                                 ctx->localpath_len,
                                                 r->pool, bb->bucket_alloc);
            APR_BUCKET_INSERT_BEFORE(bkt, next_bucket);
        }
        else {
            bkt = APR_BUCKET_NEXT(bkt); 
        }
    }
    return ap_pass_brigade(f->next, bb);
}


/** Module framework stuff **/

static const command_rec dav_svn_cmds[] =
{
  /* per directory/location */
  AP_INIT_TAKE1("SVNPath", dav_svn_path_cmd, NULL, ACCESS_CONF,
                "specifies the location in the filesystem for a Subversion "
                "repository's files."),

  /* per server */
  AP_INIT_TAKE1("SVNSpecialURI", dav_svn_special_uri_cmd, NULL, RSRC_CONF,
                "specify the URI component for special Subversion "
                "resources"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNReposName", dav_svn_repo_name, NULL, ACCESS_CONF,
                "specify the name of a Subversion repository"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNMasterURI", dav_svn_master_uri, NULL, ACCESS_CONF,
                "specifies the URI to access the master Subversion repository"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNIndexXSLT", dav_svn_xslt_uri, NULL, ACCESS_CONF,
                "specify the URI of an XSL transformation for "
                "directory indexes"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNParentPath", dav_svn_parent_path_cmd, NULL, ACCESS_CONF,
                "specifies the location in the filesystem whose "
                "subdirectories are assumed to be Subversion repositories."),


  { NULL }
};

static const dav_provider dav_svn_provider =
{
    &dav_svn_hooks_repos,
    &dav_svn_hooks_propdb,
    NULL,                       /* locks */
    &dav_svn_hooks_vsn,
    NULL,                       /* binding */
    NULL                        /* search */
};

static void register_hooks(apr_pool_t *pconf)
{
    ap_hook_post_config(dav_svn_init, NULL, NULL, APR_HOOK_MIDDLE);

    /* our provider */
    dav_register_provider(pconf, "svn", &dav_svn_provider);

    /* live property handling */
    dav_hook_gather_propsets(dav_svn_gather_propsets, NULL, NULL,
                             APR_HOOK_MIDDLE);
    dav_hook_find_liveprop(dav_svn_find_liveprop, NULL, NULL, APR_HOOK_MIDDLE);
    dav_hook_insert_all_liveprops(dav_svn_insert_all_liveprops, NULL, NULL,
                                  APR_HOOK_MIDDLE);
    dav_svn_register_uris(pconf);

    ap_register_output_filter("LocationRewrite", location_header_filter, NULL,
                              AP_FTYPE_CONTENT_SET);
    ap_register_output_filter("ReposRewrite", location_body_filter, NULL,
                              AP_FTYPE_CONTENT_SET);
    ap_register_input_filter("IncomingRewrite", location_in_filter, NULL,
                              AP_FTYPE_CONTENT_SET);
    ap_hook_fixups(proxy_merge_fixup, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA dav_svn_module =
{
    STANDARD20_MODULE_STUFF,
    dav_svn_create_dir_config,	/* dir config creater */
    dav_svn_merge_dir_config,	/* dir merger --- default is to override */
    dav_svn_create_server_config,	/* server config */
    dav_svn_merge_server_config,	/* merge server config */
    dav_svn_cmds,		/* command table */
    register_hooks,             /* register hooks */
};
