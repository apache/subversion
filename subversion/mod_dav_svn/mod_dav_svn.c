/*
 * mod_dav_svn.c: an Apache mod_dav sub-module to provide a Subversion
 *                repository.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 *
 * Portions of this software were originally written by Greg Stein as a
 * sourceXchange project sponsored by SapphireCreek.
 */




#include <httpd.h>
#include <http_config.h>
#include <mod_dav.h>

#include "config.h"
#include "dav_svn.h"


/* per-server configuration */
typedef struct {
    int not_yet_used;

} dav_svn_server_conf;

/* Note: the "dav_svn" prefix is mandatory */
extern module MODULE_VAR_EXPORT dav_svn_module;


static void dav_svn_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp,
                         server_rec *s)
{
    ap_add_version_component(p, "SVN/" SVN_VERSION);
}

static void *dav_svn_create_server_config(apr_pool_t *p, server_rec *s)
{
    return apr_pcalloc(p, sizeof(dav_svn_server_conf));
}

static void *dav_svn_merge_server_config(apr_pool_t *p,
                                         void *base, void *overrides)
{
/*    dav_svn_server_conf *parent = base;
      dav_svn_server_conf *child = overrides; */
    dav_svn_server_conf *newconf;

    newconf = apr_pcalloc(p, sizeof(*newconf));

    return newconf;
}

static const command_rec dav_svn_cmds[] =
{
    { NULL }
};

static const dav_provider dav_svn_provider =
{
    &dav_svn_hooks_repos,
    &dav_svn_hooks_propdb,
    NULL,                       /* locks */
    &dav_svn_hooks_liveprop,
    &dav_svn_hooks_vsn
};

static void register_hooks(void)
{
    ap_hook_post_config(dav_svn_init, NULL, NULL, AP_HOOK_MIDDLE);

    /* our provider */
    dav_register_provider(NULL /* ### pconf */, "svn", &dav_svn_provider);

    /* live property handling */
    ap_hook_gather_propsets(dav_svn_gather_propsets, NULL, NULL,
                            AP_HOOK_MIDDLE);
    ap_hook_find_liveprop(dav_svn_find_liveprop, NULL, NULL, AP_HOOK_MIDDLE);
    ap_hook_insert_all_liveprops(dav_svn_insert_all_liveprops, NULL, NULL,
                                 AP_HOOK_MIDDLE);
    dav_svn_register_uris(NULL /* ### pconf */);
}

module MODULE_VAR_EXPORT dav_svn_module =
{
    STANDARD20_MODULE_STUFF,
    NULL,			/* dir config creater */
    NULL,			/* dir merger --- default is to override */
    dav_svn_create_server_config,	/* server config */
    dav_svn_merge_server_config,	/* merge server config */
    dav_svn_cmds,		/* command table */
    NULL,                       /* handlers */
    register_hooks,             /* register hooks */
};


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
