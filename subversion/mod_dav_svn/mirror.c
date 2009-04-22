/*
 * mirror.c: Use a transparent proxy to mirror Subversion instances.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include <assert.h>

#include <apr_strmatch.h>

#include <httpd.h>
#include <http_core.h>

#include "dav_svn.h"


/* Tweak the request record R, and add the necessary filters, so that
   the request is ready to be proxied away.  MASTER_URI is the URI
   specified in the SVNMasterURI Apache configuration value.
   URI_SEGMENT is the URI bits relative to the repository root (but if
   non-empty, *does* have a leading slash delimiter).  */
static void proxy_request_fixup(request_rec *r,
                                const char *master_uri,
                                const char *uri_segment)
{
    assert((uri_segment[0] == '\0')
           || (uri_segment[0] == '/'));

    r->proxyreq = PROXYREQ_REVERSE;
    r->uri = r->unparsed_uri;
    r->filename = apr_pstrcat(r->pool, "proxy:", master_uri,
                              uri_segment, NULL);
    r->handler = "proxy-server";
    ap_add_output_filter("LocationRewrite", NULL, r, r->connection);
    ap_add_output_filter("ReposRewrite", NULL, r, r->connection);
    ap_add_input_filter("IncomingRewrite", NULL, r, r->connection);
}


int dav_svn__proxy_merge_fixup(request_rec *r)
{
    const char *root_dir, *master_uri, *special_uri;

    root_dir = dav_svn__get_root_dir(r);
    master_uri = dav_svn__get_master_uri(r);
    special_uri = dav_svn__get_special_uri(r);

    if (root_dir && master_uri) {
        const char *seg;

        /* We know we can always safely handle these. */
        if (r->method_number == M_REPORT ||
            r->method_number == M_OPTIONS) {
            return OK;
        }

        /* These are read-only requests -- the kind we like to handle
           ourselves -- but we need to make sure they aren't aimed at
           working resource URIs before trying to field them.  Why?
           Because working resource URIs are modeled in Subversion
           using uncommitted Subversion transactions -- stuff our copy
           of the repository isn't guaranteed to have on hand. */
        if (r->method_number == M_PROPFIND ||
            r->method_number == M_GET) {
            seg = ap_strstr(r->unparsed_uri, root_dir);
            if (seg && ap_strstr_c(seg,
                                   apr_pstrcat(r->pool, special_uri,
                                               "/wrk/", NULL))) {
                seg += strlen(root_dir);
                proxy_request_fixup(r, master_uri, seg);
            }
            return OK;
        }

        /* If this is a write request aimed at a public URI (such as
           MERGE, LOCK, UNLOCK, etc.) or any as-yet-unhandled request
           using a "special URI", we have to doctor it a bit for proxying. */
        seg = ap_strstr(r->unparsed_uri, root_dir);
        if (seg && (r->method_number == M_MERGE ||
                    r->method_number == M_LOCK ||
                    r->method_number == M_UNLOCK ||
                    ap_strstr_c(seg, special_uri))) {
            seg += strlen(root_dir);
            proxy_request_fixup(r, master_uri, seg);
            return OK;
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

apr_status_t dav_svn__location_in_filter(ap_filter_t *f,
                                         apr_bucket_brigade *bb,
                                         ap_input_mode_t mode,
                                         apr_read_type_e block,
                                         apr_off_t readbytes)
{
    request_rec *r = f->r;
    locate_ctx_t *ctx = f->ctx;
    apr_status_t rv;
    apr_bucket *bkt;
    const char *master_uri;

    master_uri = dav_svn__get_master_uri(r);

    if (r->main || !master_uri) {
        ap_remove_input_filter(f);
        return ap_get_brigade(f->next, bb, mode, block, readbytes);
    }

    if (!f->ctx) {
        ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));

        apr_uri_parse(r->pool, master_uri, &ctx->uri);
        ctx->remotepath = ctx->uri.path;
        ctx->remotepath_len = strlen(ctx->remotepath);
        ctx->localpath = dav_svn__get_root_dir(r);
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

apr_status_t dav_svn__location_header_filter(ap_filter_t *f,
                                             apr_bucket_brigade *bb)
{
    request_rec *r = f->r;
    const char *master_uri;

    master_uri = dav_svn__get_master_uri(r);

    if (!r->main && master_uri) {
        const char *location, *start_foo = NULL;

        location = apr_table_get(r->headers_out, "Location");
        if (location) {
            start_foo = ap_strstr_c(location, master_uri);
        }
        if (start_foo) {
            const char *new_uri;
            start_foo += strlen(master_uri);
            new_uri = ap_construct_url(r->pool,
                                       apr_pstrcat(r->pool,
                                                   dav_svn__get_root_dir(r),
                                                   start_foo, NULL),
                                       r);
            apr_table_set(r->headers_out, "Location", new_uri);
        }
    }
    ap_remove_output_filter(f);
    return ap_pass_brigade(f->next, bb);
}

apr_status_t dav_svn__location_body_filter(ap_filter_t *f,
                                           apr_bucket_brigade *bb)
{
    request_rec *r = f->r;
    locate_ctx_t *ctx = f->ctx;
    apr_bucket *bkt;
    const char *master_uri;

    master_uri = dav_svn__get_master_uri(r);

    if (r->main || !master_uri) {
        ap_remove_output_filter(f);
        return ap_pass_brigade(f->next, bb);
    }

    if (!f->ctx) {
        ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));

        apr_uri_parse(r->pool, master_uri, &ctx->uri);
        ctx->remotepath = ctx->uri.path;
        ctx->remotepath_len = strlen(ctx->remotepath);
        ctx->localpath = dav_svn__get_root_dir(r);
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
