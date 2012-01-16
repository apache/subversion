/*
 * mod_setlocale.c: an Apache module that calls setlocale()
 *
 * THIS IS USEFUL AS A QUICK WORKAROUND, BUT IT CAN'T REALLY BE CONSIDERED
 * SAFE. If your httpd's job is to only serve Subversion, you may decide that
 * this module has little (or no?) adverse effects. BUT THIS IS JUST A HACK.
 *
 * *** WARNING! ***
 * httpd runs in the 'C' locale, with only ASCII characters allowed in the
 * "native" encoding, for good reasons. Allowing non-ASCII characters opens
 * httpd and its modules up to unicode/UTF-8 vulnerabilities, see:
 * http://unicode.org/reports/tr36/#UTF-8_Exploit
 *
 * See the README file for detailed instructions.
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
#include <http_log.h>

#include <apr_strings.h>

#include <locale.h>

module AP_MODULE_DECLARE_DATA setlocale_module;

typedef struct setlocale_config_rec {
  const char *set_ctype;
  const char *old_ctype;
} setlocale_config_rec;

static const char *
cmd_func_ctype(cmd_parms *cmd,
               void *struct_ptr,
               const char *arg)
{
  /* ### TODO What about STRUCT_PTR ? */
  setlocale_config_rec *cfg = ap_get_module_config(cmd->server->module_config,
                                                   &setlocale_module);
  cfg->set_ctype = arg;
  return NULL;
}

static const command_rec setlocale_cmds[] =
{
  /* ### TODO: allow specifying both arguments to setlocale(). */
  /* ### TODO: why doesn't ap_set_string_slot() work? */
  AP_INIT_TAKE1("SetLocaleCTYPE", cmd_func_ctype,
                NULL,
                RSRC_CONF | EXEC_ON_READ,
                "Second argument to setlocale(LC_CTYPE, ...)"),
  { NULL }
};

static int
setlocale_post_config(apr_pool_t *pconf,apr_pool_t *plog,
                      apr_pool_t *ptemp,server_rec *s)
{
  setlocale_config_rec *cfg = ap_get_module_config(s->module_config,
                                                   &setlocale_module);

  if (cfg == NULL)
    /* Perhaps because setlocale_merge_config() was called. Perhaps not. */
    {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, 
                   "%s", "Null config");
      return HTTP_INTERNAL_SERVER_ERROR;
    }

  /* If the user omitted a configuration directive, then set_ctype will be
   * NULL.  Below condition sets it to "" instead, which loads the default as
   * determined by the environment.  httpd's env is typically set by
   * /etc/apache2/envvars, where LANG defaults to 'C', but it can be set to
   * the system default there by sourcing the system's config file (e.g. '.
   * /etc/default/locale'). Then, it suffices to just load this module to
   * obtain the system's default locale. */
  if (cfg->set_ctype == NULL)
    cfg->set_ctype = "";

  cfg->old_ctype = setlocale(LC_CTYPE, cfg->set_ctype);
  if (cfg->old_ctype)
    {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, 
                   "setlocale('%s') success: '%s'", cfg->set_ctype, cfg->old_ctype);
      return DECLINED;
    }
  else
    {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, 
                   "setlocale('%s') failed", cfg->set_ctype);
      return HTTP_INTERNAL_SERVER_ERROR;
    }
}

static void *
setlocale_create_server_config(apr_pool_t *p, server_rec *s)
{
  setlocale_config_rec *cfg = apr_pcalloc(p, sizeof(*cfg));
  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, 
               "create:  0x%08x", cfg);
  return cfg;
}

static void
setlocale_register_hooks(apr_pool_t *pool)
{
  ap_hook_post_config(setlocale_post_config, NULL, NULL, APR_HOOK_REALLY_FIRST);
}

module AP_MODULE_DECLARE_DATA setlocale_module =
{
  STANDARD20_MODULE_STUFF,
  NULL, NULL,
  setlocale_create_server_config, NULL,
  setlocale_cmds,
  setlocale_register_hooks
};
