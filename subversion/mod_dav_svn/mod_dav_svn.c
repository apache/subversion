/*
 * mod_dav_svn.c: an Apache mod_dav sub-module to provide a Subversion
 *                repository.
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

#include <stdlib.h>

#include <apr_strings.h>
#include <apr_hash.h>

#include <httpd.h>
#include <http_config.h>
#include <http_request.h>
#include <http_log.h>
#include <ap_provider.h>
#include <mod_dav.h>

#include "svn_hash.h"
#include "svn_version.h"
#include "svn_cache_config.h"
#include "svn_utf.h"
#include "svn_ctype.h"
#include "svn_dso.h"
#include "mod_dav_svn.h"

#include "private/svn_fspath.h"
#include "private/svn_subr_private.h"

#include "dav_svn.h"
#include "mod_authz_svn.h"


/* This is the default "special uri" used for SVN's special resources
   (e.g. working resources, activities) */
#define SVN_DEFAULT_SPECIAL_URI "!svn"

/* This is the value to be given to SVNPathAuthz to bypass the apache
 * subreq mechanism and make a call directly to mod_authz_svn. */
#define PATHAUTHZ_BYPASS_ARG "short_circuit"

/* per-server configuration */
typedef struct server_conf_t {
  const char *special_uri;
  svn_boolean_t use_utf8;

  /* The compression level we will pass to svn_txdelta_to_svndiff3()
   * for wire-compression. Negative value used to specify default
     compression level. */
  int compression_level;

} server_conf_t;


/* A tri-state enum used for per directory on/off flags.  Note that
   it's important that CONF_FLAG_DEFAULT is 0 to make
   merge_dir_config in mod_dav_svn do the right thing. */
enum conf_flag {
  CONF_FLAG_DEFAULT,
  CONF_FLAG_ON,
  CONF_FLAG_OFF
};

/* An enum used for the per directory configuration path_authz_method. */
enum path_authz_conf {
  CONF_PATHAUTHZ_DEFAULT,
  CONF_PATHAUTHZ_ON,
  CONF_PATHAUTHZ_OFF,
  CONF_PATHAUTHZ_BYPASS
};

/* per-dir configuration */
typedef struct dir_conf_t {
  const char *fs_path;               /* path to the SVN FS */
  const char *repo_name;             /* repository name */
  const char *xslt_uri;              /* XSL transform URI */
  const char *fs_parent_path;        /* path to parent of SVN FS'es  */
  enum conf_flag autoversioning;     /* whether autoversioning is active */
  dav_svn__bulk_upd_conf bulk_updates; /* whether bulk updates are allowed */
  enum conf_flag v2_protocol;        /* whether HTTP v2 is advertised */
  enum path_authz_conf path_authz_method; /* how GET subrequests are handled */
  enum conf_flag list_parentpath;    /* whether to allow GET of parentpath */
  const char *root_dir;              /* our top-level directory */
  const char *master_uri;            /* URI to the master SVN repos */
  svn_version_t *master_version;     /* version of master server */
  const char *activities_db;         /* path to activities database(s) */
  enum conf_flag txdelta_cache;      /* whether to enable txdelta caching */
  enum conf_flag fulltext_cache;     /* whether to enable fulltext caching */
  enum conf_flag revprop_cache;      /* whether to enable revprop caching */
  enum conf_flag nodeprop_cache;     /* whether to enable nodeprop caching */
  enum conf_flag block_read;         /* whether to enable block read mode */
  const char *hooks_env;             /* path to hook script env config file */
} dir_conf_t;


#define INHERIT_VALUE(parent, child, field) \
                ((child)->field ? (child)->field : (parent)->field)


extern module AP_MODULE_DECLARE_DATA dav_svn_module;

/* The authz_svn provider for bypassing path authz. */
static authz_svn__subreq_bypass_func_t pathauthz_bypass_func = NULL;

static int
init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
{
  svn_error_t *serr;
  server_conf_t *conf;

  ap_add_version_component(p, "SVN/" SVN_VER_NUMBER);

  serr = svn_fs_initialize(p);
  if (serr)
    {
      ap_log_perror(APLOG_MARK, APLOG_ERR, serr->apr_err, p,
                    "mod_dav_svn: error calling svn_fs_initialize: '%s'",
                    serr->message ? serr->message : "(no more info)");
      return HTTP_INTERNAL_SERVER_ERROR;
    }

  serr = svn_repos_authz_initialize(p);
  if (serr)
    {
      ap_log_perror(APLOG_MARK, APLOG_ERR, serr->apr_err, p,
               "mod_dav_svn: error calling svn_repos_authz_initialize: '%s'",
                    serr->message ? serr->message : "(no more info)");
      return HTTP_INTERNAL_SERVER_ERROR;
    }

  /* This returns void, so we can't check for error. */
  conf = ap_get_module_config(s->module_config, &dav_svn_module);
  svn_utf_initialize2(conf->use_utf8, p);

  return OK;
}

static svn_error_t *
malfunction_handler(svn_boolean_t can_return,
                    const char *file, int line,
                    const char *expr)
{
  if (expr)
    ap_log_error(APLOG_MARK, APLOG_CRIT, 0, NULL,
                 "mod_dav_svn: file '%s', line %d, assertion \"%s\" failed",
                 file, line, expr);
  else
    ap_log_error(APLOG_MARK, APLOG_CRIT, 0, NULL,
                 "mod_dav_svn: file '%s', line %d, internal malfunction",
                 file, line);
  abort();

  /* Should not be reached. */
  return SVN_NO_ERROR;
}

static int
init_dso(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp)
{
  /* This isn't ideal, we're not actually being called before any
     pool is created, but we are being called before the server or
     request pools are created, which is probably good enough for
     98% of cases. */

  svn_error_t *serr = svn_dso_initialize2();

  if (serr)
    {
      ap_log_perror(APLOG_MARK, APLOG_ERR, serr->apr_err, plog,
                    "mod_dav_svn: error calling svn_dso_initialize2: '%s'",
                    serr->message ? serr->message : "(no more info)");
      svn_error_clear(serr);
      return HTTP_INTERNAL_SERVER_ERROR;
    }

  svn_error_set_malfunction_handler(malfunction_handler);

  return OK;
}

/* Implements the #create_server_config method of Apache's #module vtable. */
static void *
create_server_config(apr_pool_t *p, server_rec *s)
{
  server_conf_t *conf = apr_pcalloc(p, sizeof(server_conf_t));

  conf->compression_level = -1;

  return conf;
}


/* Implements the #merge_server_config method of Apache's #module vtable. */
static void *
merge_server_config(apr_pool_t *p, void *base, void *overrides)
{
  server_conf_t *parent = base;
  server_conf_t *child = overrides;
  server_conf_t *newconf;

  newconf = apr_pcalloc(p, sizeof(*newconf));

  newconf->special_uri = INHERIT_VALUE(parent, child, special_uri);

  if (child->compression_level < 0)
    {
      /* Inherit compression level from parent if not configured for this
         VirtualHost. */
      newconf->compression_level = parent->compression_level;
    }
  else
    {
      newconf->compression_level = child->compression_level;
    }

  return newconf;
}


/* Implements the #create_dir_config method of Apache's #module vtable. */
static void *
create_dir_config(apr_pool_t *p, char *dir)
{
  /* NOTE: dir==NULL creates the default per-dir config */
  dir_conf_t *conf = apr_pcalloc(p, sizeof(*conf));

  /* In subversion context dir is always considered to be coming from
     <Location /blah> directive. So we treat it as a urlpath. */
  if (dir)
    conf->root_dir = svn_urlpath__canonicalize(dir, p);
  conf->bulk_updates = CONF_BULKUPD_DEFAULT;
  conf->v2_protocol = CONF_FLAG_DEFAULT;
  conf->hooks_env = NULL;
  conf->txdelta_cache = CONF_FLAG_DEFAULT;
  conf->nodeprop_cache = CONF_FLAG_DEFAULT;

  return conf;
}


/* Implements the #merge_dir_config method of Apache's #module vtable. */
static void *
merge_dir_config(apr_pool_t *p, void *base, void *overrides)
{
  dir_conf_t *parent = base;
  dir_conf_t *child = overrides;
  dir_conf_t *newconf;

  newconf = apr_pcalloc(p, sizeof(*newconf));

  newconf->fs_path = INHERIT_VALUE(parent, child, fs_path);
  newconf->master_uri = INHERIT_VALUE(parent, child, master_uri);
  newconf->master_version = INHERIT_VALUE(parent, child, master_version);
  newconf->activities_db = INHERIT_VALUE(parent, child, activities_db);
  newconf->repo_name = INHERIT_VALUE(parent, child, repo_name);
  newconf->xslt_uri = INHERIT_VALUE(parent, child, xslt_uri);
  newconf->fs_parent_path = INHERIT_VALUE(parent, child, fs_parent_path);
  newconf->autoversioning = INHERIT_VALUE(parent, child, autoversioning);
  newconf->bulk_updates = INHERIT_VALUE(parent, child, bulk_updates);
  newconf->v2_protocol = INHERIT_VALUE(parent, child, v2_protocol);
  newconf->path_authz_method = INHERIT_VALUE(parent, child, path_authz_method);
  newconf->list_parentpath = INHERIT_VALUE(parent, child, list_parentpath);
  newconf->txdelta_cache = INHERIT_VALUE(parent, child, txdelta_cache);
  newconf->fulltext_cache = INHERIT_VALUE(parent, child, fulltext_cache);
  newconf->revprop_cache = INHERIT_VALUE(parent, child, revprop_cache);
  newconf->nodeprop_cache = INHERIT_VALUE(parent, child, nodeprop_cache);
  newconf->block_read = INHERIT_VALUE(parent, child, block_read);
  newconf->root_dir = INHERIT_VALUE(parent, child, root_dir);
  newconf->hooks_env = INHERIT_VALUE(parent, child, hooks_env);

  if (parent->fs_path)
    ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL,
                 "mod_dav_svn: Location '%s' hinders access to '%s' "
                 "in parent SVNPath Location '%s'",
                 child->root_dir,
                 svn_urlpath__skip_ancestor(parent->root_dir, child->root_dir),
                 parent->root_dir);

  return newconf;
}


static const char *
SVNReposName_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;

  conf->repo_name = apr_pstrdup(cmd->pool, arg1);

  return NULL;
}


static const char *
SVNMasterURI_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;
  apr_uri_t parsed_uri;
  const char *uri_base_name = "";

  /* SVNMasterURI requires mod_proxy and mod_proxy_http
   * (r->handler = "proxy-server" in mirror.c), make sure
   * they are present. */
  if (ap_find_linked_module("mod_proxy.c") == NULL)
    return "module mod_proxy not loaded, required for SVNMasterURI";
  if (ap_find_linked_module("mod_proxy_http.c") == NULL)
    return "module mod_proxy_http not loaded, required for SVNMasterURI";
  if (APR_SUCCESS != apr_uri_parse(cmd->pool, arg1, &parsed_uri))
    return "unable to parse SVNMasterURI value";
  if (parsed_uri.path)
    uri_base_name = svn_urlpath__basename(
                        svn_urlpath__canonicalize(parsed_uri.path, cmd->pool),
                        cmd->pool);
  if (! *uri_base_name)
    return "SVNMasterURI value must not be a server root";

  conf->master_uri = apr_pstrdup(cmd->pool, arg1);

  return NULL;
}


static const char *
SVNMasterVersion_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;
  svn_error_t *err;
  svn_version_t *version;

  err = svn_version__parse_version_string(&version, arg1, cmd->pool);
  if (err)
    {
      svn_error_clear(err);
      return "Malformed master server version string.";
    }

  conf->master_version = version;
  return NULL;
}


static const char *
SVNActivitiesDB_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;

  conf->activities_db = apr_pstrdup(cmd->pool, arg1);

  return NULL;
}


static const char *
SVNIndexXSLT_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;

  conf->xslt_uri = apr_pstrdup(cmd->pool, arg1);

  return NULL;
}


static const char *
SVNAutoversioning_cmd(cmd_parms *cmd, void *config, int arg)
{
  dir_conf_t *conf = config;

  if (arg)
    conf->autoversioning = CONF_FLAG_ON;
  else
    conf->autoversioning = CONF_FLAG_OFF;

  return NULL;
}


static const char *
SVNAllowBulkUpdates_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;

  if (apr_strnatcasecmp("on", arg1) == 0)
    {
      conf->bulk_updates = CONF_BULKUPD_ON;
    }
  else if (apr_strnatcasecmp("off", arg1) == 0)
    {
      conf->bulk_updates = CONF_BULKUPD_OFF;
    }
  else if (apr_strnatcasecmp("prefer", arg1) == 0)
    {
      conf->bulk_updates = CONF_BULKUPD_PREFER;
    }
  else
    {
      return "Unrecognized value for SVNAllowBulkUpdates directive";
    }

  return NULL;
}


static const char *
SVNAdvertiseV2Protocol_cmd(cmd_parms *cmd, void *config, int arg)
{
  dir_conf_t *conf = config;

  if (arg)
    conf->v2_protocol = CONF_FLAG_ON;
  else
    conf->v2_protocol = CONF_FLAG_OFF;

  return NULL;
}


static const char *
SVNPathAuthz_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;

  if (apr_strnatcasecmp("off", arg1) == 0)
    {
      conf->path_authz_method = CONF_PATHAUTHZ_OFF;
    }
  else if (apr_strnatcasecmp(PATHAUTHZ_BYPASS_ARG, arg1) == 0)
    {
      conf->path_authz_method = CONF_PATHAUTHZ_BYPASS;
      if (pathauthz_bypass_func == NULL)
        {
          pathauthz_bypass_func =
            ap_lookup_provider(AUTHZ_SVN__SUBREQ_BYPASS_PROV_GRP,
                               AUTHZ_SVN__SUBREQ_BYPASS_PROV_NAME,
                               AUTHZ_SVN__SUBREQ_BYPASS_PROV_VER);
        }
    }
  else if (apr_strnatcasecmp("on", arg1) == 0)
    {
      conf->path_authz_method = CONF_PATHAUTHZ_ON;
    }
  else
    {
      return "Unrecognized value for SVNPathAuthz directive";
    }

  return NULL;
}


static const char *
SVNListParentPath_cmd(cmd_parms *cmd, void *config, int arg)
{
  dir_conf_t *conf = config;

  if (arg)
    conf->list_parentpath = CONF_FLAG_ON;
  else
    conf->list_parentpath = CONF_FLAG_OFF;

  return NULL;
}


static const char *
SVNPath_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;

  if (conf->fs_parent_path != NULL)
    return "SVNPath cannot be defined at same time as SVNParentPath.";

  conf->fs_path = svn_dirent_internal_style(arg1, cmd->pool);

  return NULL;
}


static const char *
SVNParentPath_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;

  if (conf->fs_path != NULL)
    return "SVNParentPath cannot be defined at same time as SVNPath.";

  conf->fs_parent_path = svn_dirent_internal_style(arg1, cmd->pool);

  return NULL;
}


static const char *
SVNSpecialURI_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  server_conf_t *conf;
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

static const char *
SVNCacheTextDeltas_cmd(cmd_parms *cmd, void *config, int arg)
{
  dir_conf_t *conf = config;

  if (arg)
    conf->txdelta_cache = CONF_FLAG_ON;
  else
    conf->txdelta_cache = CONF_FLAG_OFF;

  return NULL;
}

static const char *
SVNCacheFullTexts_cmd(cmd_parms *cmd, void *config, int arg)
{
  dir_conf_t *conf = config;

  if (arg)
    conf->fulltext_cache = CONF_FLAG_ON;
  else
    conf->fulltext_cache = CONF_FLAG_OFF;

  return NULL;
}

static const char *
SVNCacheRevProps_cmd(cmd_parms *cmd, void *config, int arg)
{
  dir_conf_t *conf = config;

  if (arg)
    conf->revprop_cache = CONF_FLAG_ON;
  else
    conf->revprop_cache = CONF_FLAG_OFF;

  return NULL;
}

static const char *
SVNCacheNodeProps_cmd(cmd_parms *cmd, void *config, int arg)
{
  dir_conf_t *conf = config;

  if (arg)
    conf->nodeprop_cache = CONF_FLAG_ON;
  else
    conf->nodeprop_cache = CONF_FLAG_OFF;

  return NULL;
}

static const char *
SVNBlockRead_cmd(cmd_parms *cmd, void *config, int arg)
{
  dir_conf_t *conf = config;

  if (arg)
    conf->block_read = CONF_FLAG_ON;
  else
    conf->block_read = CONF_FLAG_OFF;

  return NULL;
}

static const char *
SVNInMemoryCacheSize_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  svn_cache_config_t settings = *svn_cache_config_get();

  apr_uint64_t value = 0;
  svn_error_t *err = svn_cstring_atoui64(&value, arg1);
  if (err)
    {
      svn_error_clear(err);
      return "Invalid decimal number for the SVN cache size.";
    }

  settings.cache_size = value * 0x400;

  svn_cache_config_set(&settings);

  return NULL;
}

static const char *
SVNCompressionLevel_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  server_conf_t *conf;
  int value = 0;
  svn_error_t *err = svn_cstring_atoi(&value, arg1);
  if (err)
    {
      svn_error_clear(err);
      return "Invalid decimal number for the SVN compression level.";
    }

  if ((value < SVN_DELTA_COMPRESSION_LEVEL_NONE)
      || (value > SVN_DELTA_COMPRESSION_LEVEL_MAX))
    return apr_psprintf(cmd->pool,
                        "%d is not a valid compression level. "
                        "The valid range is %d .. %d.",
                        value,
                        (int)SVN_DELTA_COMPRESSION_LEVEL_NONE,
                        (int)SVN_DELTA_COMPRESSION_LEVEL_MAX);

  conf = ap_get_module_config(cmd->server->module_config,
                              &dav_svn_module);
  conf->compression_level = value;

  return NULL;
}

static const char *
SVNUseUTF8_cmd(cmd_parms *cmd, void *config, int arg)
{
  server_conf_t *conf;

  conf = ap_get_module_config(cmd->server->module_config,
                              &dav_svn_module);
  conf->use_utf8 = arg;

  return NULL;
}

static const char *
SVNHooksEnv_cmd(cmd_parms *cmd, void *config, const char *arg1)
{
  dir_conf_t *conf = config;

  conf->hooks_env = svn_dirent_internal_style(arg1, cmd->pool);

  return NULL;
}

static svn_boolean_t
get_conf_flag(enum conf_flag flag, svn_boolean_t default_value)
{
  if (flag == CONF_FLAG_ON)
    return TRUE;
  else if (flag == CONF_FLAG_OFF)
    return FALSE;
  else /* CONF_FLAG_DEFAULT*/
    return default_value;
}

/** Accessor functions for the module's configuration state **/

const char *
dav_svn__get_fs_path(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->fs_path;
}


const char *
dav_svn__get_fs_parent_path(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->fs_parent_path;
}


AP_MODULE_DECLARE(dav_error *)
dav_svn_get_repos_path2(request_rec *r,
                        const char *root_path,
                        const char **repos_path,
                        apr_pool_t *pool)
{

  const char *fs_path;
  const char *fs_parent_path;
  const char *repos_name;
  const char *ignored_path_in_repos;
  const char *ignored_cleaned_uri;
  const char *ignored_relative;
  int ignored_had_slash;
  dav_error *derr;

  /* Handle the SVNPath case. */
  fs_path = dav_svn__get_fs_path(r);

  if (fs_path != NULL)
    {
      *repos_path = fs_path;
      return NULL;
    }

  /* Handle the SVNParentPath case.  If neither directive was used,
     dav_svn_split_uri will throw a suitable error for us - we do
     not need to check that here. */
  fs_parent_path = dav_svn__get_fs_parent_path(r);

  /* Split the svn URI to get the name of the repository below
     the parent path. */
  derr = dav_svn_split_uri2(r, r->uri, root_path,
                            &ignored_cleaned_uri, &ignored_had_slash,
                            &repos_name,
                            &ignored_relative, &ignored_path_in_repos, pool);
  if (derr)
    return derr;

  /* Construct the full path from the parent path base directory
     and the repository name. */
  *repos_path = svn_dirent_join(fs_parent_path, repos_name, pool);
  return NULL;
}

AP_MODULE_DECLARE(dav_error *)
dav_svn_get_repos_path(request_rec *r,
                       const char *root_path,
                       const char **repos_path)
{
  return dav_svn_get_repos_path2(r, root_path, repos_path, r->pool);
}

const char *
dav_svn__get_repo_name(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->repo_name;
}


const char *
dav_svn__get_root_dir(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->root_dir;
}


const char *
dav_svn__get_master_uri(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->master_uri;
}


svn_version_t *
dav_svn__get_master_version(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->master_uri ? conf->master_version : NULL;
}


const char *
dav_svn__get_xslt_uri(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->xslt_uri;
}


const char *
dav_svn__get_special_uri(request_rec *r)
{
  server_conf_t *conf;

  conf = ap_get_module_config(r->server->module_config,
                              &dav_svn_module);
  return conf->special_uri ? conf->special_uri : SVN_DEFAULT_SPECIAL_URI;
}


const char *
dav_svn__get_me_resource_uri(request_rec *r)
{
  return apr_pstrcat(r->pool, dav_svn__get_special_uri(r), "/me",
                     SVN_VA_NULL);
}


const char *
dav_svn__get_rev_stub(request_rec *r)
{
  return apr_pstrcat(r->pool, dav_svn__get_special_uri(r), "/rev",
                     SVN_VA_NULL);
}


const char *
dav_svn__get_rev_root_stub(request_rec *r)
{
  return apr_pstrcat(r->pool, dav_svn__get_special_uri(r), "/rvr",
                     SVN_VA_NULL);
}


const char *
dav_svn__get_txn_stub(request_rec *r)
{
  return apr_pstrcat(r->pool, dav_svn__get_special_uri(r), "/txn",
                     SVN_VA_NULL);
}


const char *
dav_svn__get_txn_root_stub(request_rec *r)
{
  return apr_pstrcat(r->pool, dav_svn__get_special_uri(r), "/txr", SVN_VA_NULL);
}


const char *
dav_svn__get_vtxn_stub(request_rec *r)
{
  return apr_pstrcat(r->pool, dav_svn__get_special_uri(r), "/vtxn",
                     SVN_VA_NULL);
}


const char *
dav_svn__get_vtxn_root_stub(request_rec *r)
{
  return apr_pstrcat(r->pool, dav_svn__get_special_uri(r), "/vtxr",
                     SVN_VA_NULL);
}


svn_boolean_t
dav_svn__get_autoversioning_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->autoversioning == CONF_FLAG_ON;
}


dav_svn__bulk_upd_conf
dav_svn__get_bulk_updates_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  /* SVNAllowBulkUpdates is 'on' by default. */
  if (conf->bulk_updates == CONF_BULKUPD_DEFAULT)
    return CONF_BULKUPD_ON;
  else
    return conf->bulk_updates;
}


svn_boolean_t
dav_svn__check_httpv2_support(request_rec *r)
{
  dir_conf_t *conf;
  svn_boolean_t available;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  available = get_conf_flag(conf->v2_protocol, TRUE);

  /* If our configuration says that HTTPv2 is available, but we are
     proxying requests to a master Subversion server which lacks
     support for HTTPv2, we dumb ourselves down. */
  if (available)
    {
      svn_version_t *version = dav_svn__get_master_version(r);
      if (version && (! svn_version__at_least(version, 1, 7, 0)))
        available = FALSE;
    }
  return available;
}


/* FALSE if path authorization should be skipped.
 * TRUE if either the bypass or the apache subrequest methods should be used.
 */
svn_boolean_t
dav_svn__get_pathauthz_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->path_authz_method != CONF_PATHAUTHZ_OFF;
}

/* Function pointer if we should use the bypass directly to mod_authz_svn.
 * NULL otherwise. */
authz_svn__subreq_bypass_func_t
dav_svn__get_pathauthz_bypass(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  if (conf->path_authz_method == CONF_PATHAUTHZ_BYPASS)
    return pathauthz_bypass_func;
  return NULL;
}


svn_boolean_t
dav_svn__get_list_parentpath_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->list_parentpath == CONF_FLAG_ON;
}


const char *
dav_svn__get_activities_db(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->activities_db;
}


svn_boolean_t
dav_svn__get_txdelta_cache_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  /* txdelta caching is enabled by default. */
  return get_conf_flag(conf->txdelta_cache, TRUE);
}


svn_boolean_t
dav_svn__get_fulltext_cache_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  /* fulltext caching is enabled by default. */
  return get_conf_flag(conf->fulltext_cache, TRUE);
}


svn_boolean_t
dav_svn__get_revprop_cache_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  /* revprop caching is enabled by default. */
  return get_conf_flag(conf->revprop_cache, TRUE);
}

svn_boolean_t
dav_svn__get_nodeprop_cache_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  /* node properties caching is enabled by default. */
  return get_conf_flag(conf->nodeprop_cache, TRUE);
}

svn_boolean_t
dav_svn__get_block_read_flag(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  /* the block-read feature is disabled by default. */
  return get_conf_flag(conf->block_read, FALSE);
}

int
dav_svn__get_compression_level(request_rec *r)
{
  server_conf_t *conf;

  conf = ap_get_module_config(r->server->module_config,
                              &dav_svn_module);

  if (conf->compression_level < 0)
    {
      return SVN_DELTA_COMPRESSION_LEVEL_DEFAULT;
    }
  else
    {
      return conf->compression_level;
    }
}

const char *
dav_svn__get_hooks_env(request_rec *r)
{
  dir_conf_t *conf;

  conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);
  return conf->hooks_env;
}

static void
merge_xml_filter_insert(request_rec *r)
{
  /* We only care about MERGE and DELETE requests. */
  if ((r->method_number == M_MERGE)
      || (r->method_number == M_DELETE))
    {
      dir_conf_t *conf;
      conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

      /* We only care if we are configured. */
      if (conf->fs_path || conf->fs_parent_path)
        {
          ap_add_input_filter("SVN-MERGE", NULL, r, r->connection);
        }
    }
}


typedef struct merge_ctx_t {
  apr_bucket_brigade *bb;
  apr_xml_parser *parser;
} merge_ctx_t;


static apr_status_t
merge_xml_in_filter(ap_filter_t *f,
                    apr_bucket_brigade *bb,
                    ap_input_mode_t mode,
                    apr_read_type_e block,
                    apr_off_t readbytes)
{
  apr_status_t rv;
  request_rec *r = f->r;
  merge_ctx_t *ctx = f->ctx;
  apr_bucket *bucket;
  int seen_eos = 0;

  /* We shouldn't be added if we're not a MERGE/DELETE, but double check. */
  if ((r->method_number != M_MERGE)
      && (r->method_number != M_DELETE))
    {
      ap_remove_input_filter(f);
      return ap_get_brigade(f->next, bb, mode, block, readbytes);
    }

  if (!ctx)
    {
      f->ctx = ctx = apr_palloc(r->pool, sizeof(*ctx));
      ctx->parser = apr_xml_parser_create(r->pool);
      ctx->bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
    }

  rv = ap_get_brigade(f->next, ctx->bb, mode, block, readbytes);

  if (rv != APR_SUCCESS)
    return rv;

  for (bucket = APR_BRIGADE_FIRST(ctx->bb);
       bucket != APR_BRIGADE_SENTINEL(ctx->bb);
       bucket = APR_BUCKET_NEXT(bucket))
    {
      const char *data;
      apr_size_t len;

      if (APR_BUCKET_IS_EOS(bucket))
        {
          seen_eos = 1;
          break;
        }

      if (APR_BUCKET_IS_METADATA(bucket))
        continue;

      rv = apr_bucket_read(bucket, &data, &len, APR_BLOCK_READ);
      if (rv != APR_SUCCESS)
        return rv;

      rv = apr_xml_parser_feed(ctx->parser, data, len);
      if (rv != APR_SUCCESS)
        {
          /* Clean up the parser. */
          (void) apr_xml_parser_done(ctx->parser, NULL);
          break;
        }
    }

  /* This will clear-out the ctx->bb as well. */
  APR_BRIGADE_CONCAT(bb, ctx->bb);

  if (seen_eos)
    {
      apr_xml_doc *pdoc;

      /* Remove ourselves now. */
      ap_remove_input_filter(f);

      /* tell the parser that we're done */
      rv = apr_xml_parser_done(ctx->parser, &pdoc);
      if (rv == APR_SUCCESS)
        {
#if APR_CHARSET_EBCDIC
          apr_xml_parser_convert_doc(r->pool, pdoc, ap_hdrs_from_ascii);
#endif
          /* stash the doc away for mod_dav_svn's later use. */
          rv = apr_pool_userdata_set(pdoc, "svn-request-body",
                                     NULL, r->pool);
          if (rv != APR_SUCCESS)
            return rv;

        }
    }

  return APR_SUCCESS;
}


/* Response handler for POST requests (protocol-v2 commits).  */
static int dav_svn__handler(request_rec *r)
{
  dir_conf_t *conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  if (conf->fs_path || conf->fs_parent_path)
    {
      /* HTTP-defined Methods we handle */
      r->allowed = 0
        | (AP_METHOD_BIT << M_POST);

      if (r->method_number == M_POST)
        return dav_svn__method_post(r);
    }

  return DECLINED;
}

#define NO_MAP_TO_STORAGE_NOTE "dav_svn-no-map-to-storage"

/* Fill the filename on the request with a bogus path since we aren't serving
 * a file off the disk.  This means that <Directory> blocks will not match and
 * %f in logging formats will show as "dav_svn:/path/to/repo/path/in/repo".
 */
static int dav_svn__translate_name(request_rec *r)
{
  const char *fs_path, *repos_basename, *repos_path;
  const char *ignore_cleaned_uri, *ignore_relative_path;
  int ignore_had_slash;
  dir_conf_t *conf = ap_get_module_config(r->per_dir_config, &dav_svn_module);

  /* module is not configured, bail out early */
  if (!conf->fs_path && !conf->fs_parent_path)
    return DECLINED;

  if (dav_svn__is_parentpath_list(r))
    {
      /* SVNListParentPath is on and the request is for the conf->root_dir,
       * so just set the repos_basename to an empty string and the repos_path
       * to NULL so we end up just reporting our parent path as the bogus
       * path. */
      repos_basename = "";
      repos_path = NULL;
    }
  else
    {
      /* Retrieve path to repo and within repo for the request */
      dav_error *err = dav_svn_split_uri(r, r->uri, conf->root_dir,
                                         &ignore_cleaned_uri,
                                         &ignore_had_slash, &repos_basename,
                                         &ignore_relative_path, &repos_path);
      if (err)
        {
          dav_svn__log_err(r, err, APLOG_ERR);
          return err->status;
        }
    }

  if (conf->fs_parent_path)
    {
      fs_path = svn_dirent_join(conf->fs_parent_path, repos_basename,
                                r->pool);
    }
  else
    {
      fs_path = conf->fs_path;
    }

  /* Avoid a trailing slash on the bogus path when repos_path is just "/" */
  if (repos_path && '/' == repos_path[0] && '\0' == repos_path[1])
    repos_path = NULL;

  /* Combine 'dav_svn:', fs_path and repos_path to produce the bogus path we're
   * placing in r->filename.  We can't use our standard join helpers such
   * as svn_dirent_join.  fs_path is a dirent and repos_path is a fspath
   * (that can be trivially converted to a relpath by skipping the leading
   * slash).  In general it is safe to join these, but when a path in a
   * repository is 'trunk/c:hi' this results in a non canonical dirent on
   * Windows. Instead we just cat them together. */
  r->filename = apr_pstrcat(r->pool,
                            "dav_svn:", fs_path, repos_path, SVN_VA_NULL);

  /* Leave a note to ourselves so that we know not to decline in the
   * map_to_storage hook. */
  apr_table_setn(r->notes, NO_MAP_TO_STORAGE_NOTE, (const char*)1);
  return OK;
}

/* Prevent core_map_to_storage from running if we prevented the r->filename
 * from being set since core_map_to_storage doesn't like r->filename being
 * bogus. */
static int dav_svn__map_to_storage(request_rec *r)
{
  /* Check a note we left in translate_name since map_to_storage doesn't
   * have access to our configuration. */
  if (apr_table_get(r->notes, NO_MAP_TO_STORAGE_NOTE))
    return OK;

  return DECLINED;
}



/** Module framework stuff **/

/* Implements the #cmds member of Apache's #module vtable. */
static const command_rec cmds[] =
{
  /* per directory/location */
  AP_INIT_TAKE1("SVNPath", SVNPath_cmd, NULL, ACCESS_CONF,
                "specifies the location in the filesystem for a Subversion "
                "repository's files."),

  /* per server */
  AP_INIT_TAKE1("SVNSpecialURI", SVNSpecialURI_cmd, NULL, RSRC_CONF,
                "specify the URI component for special Subversion "
                "resources"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNReposName", SVNReposName_cmd, NULL, ACCESS_CONF,
                "specify the name of a Subversion repository"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNIndexXSLT", SVNIndexXSLT_cmd, NULL, ACCESS_CONF,
                "specify the URI of an XSL transformation for "
                "directory indexes"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNParentPath", SVNParentPath_cmd, NULL, ACCESS_CONF,
                "specifies the location in the filesystem whose "
                "subdirectories are assumed to be Subversion repositories."),

  /* per directory/location */
  AP_INIT_FLAG("SVNAutoversioning", SVNAutoversioning_cmd, NULL,
               ACCESS_CONF|RSRC_CONF, "turn on deltaV autoversioning."),

  /* per directory/location */
  AP_INIT_TAKE1("SVNPathAuthz", SVNPathAuthz_cmd, NULL,
               ACCESS_CONF|RSRC_CONF,
               "control path-based authz by enabling subrequests(On,default), "
               "disabling subrequests(Off), or"
               "querying mod_authz_svn directly(" PATHAUTHZ_BYPASS_ARG ")"),

  /* per directory/location */
  AP_INIT_FLAG("SVNListParentPath", SVNListParentPath_cmd, NULL,
               ACCESS_CONF|RSRC_CONF, "allow GET of SVNParentPath."),

  /* per directory/location */
  AP_INIT_TAKE1("SVNMasterURI", SVNMasterURI_cmd, NULL, ACCESS_CONF,
                "specifies a URI to access a master Subversion repository"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNMasterVersion", SVNMasterVersion_cmd, NULL, ACCESS_CONF,
                "specifies the Subversion release version of a master "
                "Subversion server "),

  /* per directory/location */
  AP_INIT_TAKE1("SVNActivitiesDB", SVNActivitiesDB_cmd, NULL, ACCESS_CONF,
                "specifies the location in the filesystem in which the "
                "activities database(s) should be stored"),

  /* per directory/location */
  AP_INIT_TAKE1("SVNAllowBulkUpdates", SVNAllowBulkUpdates_cmd, NULL,
                ACCESS_CONF|RSRC_CONF,
                "enables support for bulk update-style requests (On, default), "
                "as opposed to only skeletal reports that require additional "
                "per-file downloads (Off). Use Prefer to tell the svn client "
                "to always use bulk update requests, if supported."),

  /* per directory/location */
  AP_INIT_FLAG("SVNAdvertiseV2Protocol", SVNAdvertiseV2Protocol_cmd, NULL,
               ACCESS_CONF|RSRC_CONF,
               "enables server advertising of support for version 2 of "
               "Subversion's HTTP protocol (default values is On)."),

  /* per directory/location */
  AP_INIT_FLAG("SVNCacheTextDeltas", SVNCacheTextDeltas_cmd, NULL,
               ACCESS_CONF|RSRC_CONF,
               "speeds up data access to older revisions by caching "
               "delta information if sufficient in-memory cache is "
               "available (default is On)."),

  /* per directory/location */
  AP_INIT_FLAG("SVNCacheFullTexts", SVNCacheFullTexts_cmd, NULL,
               ACCESS_CONF|RSRC_CONF,
               "speeds up data access by caching full file content "
               "if sufficient in-memory cache is available "
               "(default is Off)."),

  /* per directory/location */
  AP_INIT_FLAG("SVNCacheRevProps", SVNCacheRevProps_cmd, NULL,
               ACCESS_CONF|RSRC_CONF,
               "speeds up 'svn ls -v', export and checkout operations"
               "but should only be enabled under the conditions described"
               "in the documentation"
               "(default is Off)."),

  /* per directory/location */
  AP_INIT_FLAG("SVNCacheNodeProps", SVNCacheNodeProps_cmd, NULL,
               ACCESS_CONF|RSRC_CONF,
               "speeds up data access by caching node properties "
               "if sufficient in-memory cache is available"
               "(default is On)."),

  /* per directory/location */
  AP_INIT_FLAG("SVNBlockRead", SVNBlockRead_cmd, NULL,
               ACCESS_CONF|RSRC_CONF,
               "speeds up operations of FSFS 1.9+ repositories if large"
               "caches (see SVNInMemoryCacheSize) have been configured."
               "(default is Off)."),

  /* per server */
  AP_INIT_TAKE1("SVNInMemoryCacheSize", SVNInMemoryCacheSize_cmd, NULL,
                RSRC_CONF,
                "specifies the maximum size in kB per process of Subversion's "
                "in-memory object cache (default value is 16384; 0 switches "
                "to dynamically sized caches)."),
  /* per server */
  AP_INIT_TAKE1("SVNCompressionLevel", SVNCompressionLevel_cmd, NULL,
                RSRC_CONF,
                "specifies the compression level used before sending file "
                "content over the network (0 for no compression, 9 for "
                "maximum, 5 is default)."),

  /* per server */
  AP_INIT_FLAG("SVNUseUTF8",
               SVNUseUTF8_cmd, NULL,
               RSRC_CONF,
               "use UTF-8 as native character encoding (default is ASCII)."),

  /* per directory/location */
  AP_INIT_TAKE1("SVNHooksEnv", SVNHooksEnv_cmd, NULL,
                ACCESS_CONF|RSRC_CONF,
                "Sets the path to the configuration file for the environment "
                "of hook scripts. If not absolute, the path is relative to "
                "the repository's conf directory (by default the hooks-env "
                "file in the repository is used)."),
  { NULL }
};


static dav_provider provider =
{
  &dav_svn__hooks_repository,
  &dav_svn__hooks_propdb,
  &dav_svn__hooks_locks,
  &dav_svn__hooks_vsn,
  NULL,                       /* binding */
  NULL                        /* search */
};


/* Implements the #register_hooks method of Apache's #module vtable. */
static void
register_hooks(apr_pool_t *pconf)
{
  ap_hook_pre_config(init_dso, NULL, NULL, APR_HOOK_REALLY_FIRST);
  ap_hook_post_config(init, NULL, NULL, APR_HOOK_MIDDLE);

  /* our provider */
  dav_register_provider(pconf, "svn", &provider);

  /* input filter to read MERGE bodies. */
  ap_register_input_filter("SVN-MERGE", merge_xml_in_filter, NULL,
                           AP_FTYPE_RESOURCE);
  ap_hook_insert_filter(merge_xml_filter_insert, NULL, NULL,
                        APR_HOOK_MIDDLE);

  /* general request handler for methods which mod_dav DECLINEs. */
  ap_hook_handler(dav_svn__handler, NULL, NULL, APR_HOOK_LAST);

  /* Handler to GET Subversion's FSFS cache stats, a bit like mod_status. */
  ap_hook_handler(dav_svn__status, NULL, NULL, APR_HOOK_MIDDLE);

  /* live property handling */
  dav_hook_gather_propsets(dav_svn__gather_propsets, NULL, NULL,
                           APR_HOOK_MIDDLE);
  dav_hook_find_liveprop(dav_svn__find_liveprop, NULL, NULL, APR_HOOK_MIDDLE);
  dav_hook_insert_all_liveprops(dav_svn__insert_all_liveprops, NULL, NULL,
                                APR_HOOK_MIDDLE);
  dav_register_liveprop_group(pconf, &dav_svn__liveprop_group);

  /* Proxy / mirroring filters and fixups */
  ap_register_output_filter("LocationRewrite", dav_svn__location_header_filter,
                            NULL, AP_FTYPE_CONTENT_SET);
  ap_register_output_filter("ReposRewrite", dav_svn__location_body_filter,
                            NULL, AP_FTYPE_CONTENT_SET);
  ap_register_input_filter("IncomingRewrite", dav_svn__location_in_filter,
                           NULL, AP_FTYPE_CONTENT_SET);
  ap_hook_fixups(dav_svn__proxy_request_fixup, NULL, NULL, APR_HOOK_MIDDLE);
  /* translate_name hook is LAST so that it doesn't interfere with modules
   * like mod_alias that are MIDDLE. */
  ap_hook_translate_name(dav_svn__translate_name, NULL, NULL, APR_HOOK_LAST);
  /* map_to_storage hook is LAST to avoid interferring with mod_http's
   * handling of OPTIONS and TRACE. */
  ap_hook_map_to_storage(dav_svn__map_to_storage, NULL, NULL, APR_HOOK_LAST);
}


module AP_MODULE_DECLARE_DATA dav_svn_module =
{
  STANDARD20_MODULE_STUFF,
  create_dir_config,    /* dir config creater */
  merge_dir_config,     /* dir merger --- default is to override */
  create_server_config, /* server config */
  merge_server_config,  /* merge server config */
  cmds,                 /* command table */
  register_hooks,       /* register hooks */
};
