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
#include <http_config.h>
#include <mod_dav.h>

#include <apr_strings.h>

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
  const char *repo_name;        /* repository name */
  const char *xslt_uri;         /* XSL transform URI */
  const char *fs_parent_path;   /* path to parent of of SVN FS'es  */
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

    return apr_pcalloc(p, sizeof(dav_svn_dir_conf));
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
    newconf->xslt_uri = INHERIT_VALUE(parent, child, xslt_uri);
    newconf->fs_parent_path = INHERIT_VALUE(parent, child, fs_parent_path);

    return newconf;
}

static const char *dav_svn_repo_name(cmd_parms *cmd, void *config,
                                     const char *arg1)
{
  dav_svn_dir_conf *conf = config;

  conf->repo_name = apr_pstrdup(cmd->pool, arg1);

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
      = svn_path_canonicalize_nts(apr_pstrdup(cmd->pool, arg1), cmd->pool);

    return NULL;
}


static const char *dav_svn_parent_path_cmd(cmd_parms *cmd, void *config,
                                           const char *arg1)
{
    dav_svn_dir_conf *conf = config;

    if (conf->fs_path != NULL)
      return "SVNParentPath cannot be defined at same time as SVNPath.";

    conf->fs_parent_path
      = svn_path_canonicalize_nts(apr_pstrdup(cmd->pool, arg1), cmd->pool);

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
