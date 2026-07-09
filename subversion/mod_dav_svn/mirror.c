/*
 * mirror.c: Use a transparent proxy to mirror Subversion instances.
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

#include <assert.h>

#include <apr_strmatch.h>

#include <httpd.h>
#include <http_core.h>

#include "private/svn_fspath.h"

#include "dav_svn.h"


/* If the request carries a Destination header (as COPY and MOVE do), rewrite
   it to target the master server instead of this slave. MASTER_URI is the
   SVNMasterURI configuration value (canonical and URI-encoded). This is the
   request-side counterpart to the Location-header rewrite performed by
   dav_svn__location_header_filter(); the two are conceptual inverses. */
static void
proxy_request_fixup_destination(request_rec *r, const char *master_uri)
{
    const char *destination = apr_table_get(r->headers_in, "Destination");
    apr_uri_t dest_uri;
    const char *dest_path, *root_dir, *rel;

    if (destination == NULL
        || apr_uri_parse(r->pool, destination, &dest_uri) != APR_SUCCESS
        || dest_uri.path == NULL)
      {
        return;
      }

    /* apr_uri_parse() leaves dest_uri.path URI-encoded, and
       dav_svn__get_root_dir() is also stored URI-encoded and canonical
       (create_dir_config() runs it through svn_urlpath__canonicalize(),
       which encodes; see mod_dav_svn.c).  Canonicalizing DEST_PATH
       normalizes its hex encoding to the same rules, so the ancestor
       check compares like with like.  REL comes out still encoded and
       must not be re-encoded. */
    dest_path = svn_urlpath__canonicalize(dest_uri.path, r->pool);
    root_dir = dav_svn__get_root_dir(r);

    rel = svn_urlpath__skip_ancestor(root_dir, dest_path);
    if (rel == NULL)
      {
        /* The Destination isn't under this slave's location, so we have no
           way to translate it to the master. Leave it untouched (the master
           will reject it) but log for diagnosability. */
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "Not rewriting Destination '%s': outside slave root '%s'",
                      dest_path, root_dir);
        return;
      }

    apr_table_set(r->headers_in, "Destination",
                  apr_pstrcat(r->pool, master_uri, "/", rel, SVN_VA_NULL));
}


/* Tweak the request record R, and add the necessary filters, so that
   the request is ready to be proxied away.  MASTER_URI is the URI
   specified in the SVNMasterURI Apache configuration value.
   URI_SEGMENT is the URI bits relative to the repository root (but if
   non-empty, *does* have a leading slash delimiter).
   MASTER_URI is canonical and URI-encoded (stored by SVNMasterURI_cmd);
   URI_SEGMENT is not URI-encoded. */
static int proxy_request_fixup(request_rec *r,
                               const char *master_uri,
                               const char *uri_segment)
{
    if (uri_segment[0] != '\0' && uri_segment[0] != '/')
      {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, SVN_ERR_BAD_CONFIG_VALUE, r,
                     "Invalid URI segment '%s' in slave fixup",
                      uri_segment);
        return HTTP_INTERNAL_SERVER_ERROR;
      }

    r->proxyreq = PROXYREQ_REVERSE;
    r->uri = r->unparsed_uri;
    r->filename = (char *) apr_pstrcat(r->pool, "proxy:", master_uri,
                                       svn_path_uri_encode(uri_segment,
                                                           r->pool),
                                       SVN_VA_NULL);
    r->handler = "proxy-server";

    proxy_request_fixup_destination(r, master_uri);

    /* ### FIXME: Seems we could avoid adding some or all of these
           filters altogether when the root_dir (that is, the slave's
           location, relative to the server root) and path portion of
           the master_uri (the master's location, relative to the
           server root) are identical, rather than adding them here
           and then trying to remove them later.  (See the filter
           removal logic in dav_svn__location_in_filter() and
           dav_svn__location_body_filter().  -- cmpilato */

    ap_add_output_filter("LocationRewrite", NULL, r, r->connection);

    /* The body-rewrite filters are attached by whitelist, only bodies
       that carry protocol hrefs to translate get filtered.
       Responses: MERGE (merge-response hrefs, which the client
       validates) and PROPFIND (multistatus hrefs on transaction
       resources).  
       Requests: MERGE and CHECKOUT (v1), whose bodies
       carry the activity/txn source href the master must resolve.
       Everything else is user or versioned payload which a rewrite
       could corrupt (issue #3445).  A method missing from the
       whitelist fails loudly instead of silently corrupting data. */
    if (r->method_number == M_MERGE || r->method_number == M_PROPFIND)
        ap_add_output_filter("ReposRewrite", NULL, r, r->connection);

    /* Request-body whitelist, see above. */
    if (r->method_number == M_MERGE || r->method_number == M_CHECKOUT)
        ap_add_input_filter("IncomingRewrite", NULL, r, r->connection);

    return OK;
}


int dav_svn__proxy_request_fixup(request_rec *r)
{
    const char *root_dir, *master_uri, *special_uri;

    root_dir = dav_svn__get_root_dir(r);
    master_uri = dav_svn__get_master_uri(r);
    special_uri = dav_svn__get_special_uri(r);

    if (root_dir && master_uri) {
        const char *seg;
        const char *root_dir_decoded;

        /* ROOT_DIR is stored canonical and URI-encoded, but at fixup time
           R->URI is the decoded path, compare like with like. */
        root_dir_decoded = svn_path_uri_decode(root_dir, r->pool);

        /* We know we can always safely handle these. */
        if (r->method_number == M_REPORT ||
            r->method_number == M_OPTIONS) {
            return OK;
        }

        /* These are read-only requests -- the kind we like to handle
           ourselves -- but we need to make sure they aren't aimed at
           resources that only exist on the master server such as
           working resource URIs or the HTTPv2 transaction root and
           transaction tree resources. */
        if (r->method_number == M_PROPFIND ||
            r->method_number == M_GET) {
            if ((seg = ap_strstr(r->uri, root_dir_decoded))) {
                if (ap_strstr_c(seg, apr_pstrcat(r->pool, special_uri,
                                                 "/wrk/", SVN_VA_NULL))
                    || ap_strstr_c(seg, apr_pstrcat(r->pool, special_uri,
                                                    "/txn/", SVN_VA_NULL))
                    || ap_strstr_c(seg, apr_pstrcat(r->pool, special_uri,
                                                    "/txr/", SVN_VA_NULL))) {
                    int rv;
                    seg += strlen(root_dir_decoded);
                    rv = proxy_request_fixup(r, master_uri, seg);
                    if (rv) return rv;
                }
            }
            return OK;
        }

        /* If this is a write request aimed at a public URI (such as
           MERGE, LOCK, UNLOCK, etc.) or any as-yet-unhandled request
           using a "special URI", we have to doctor it a bit for proxying. */
        seg = ap_strstr(r->uri, root_dir_decoded);
        if (seg && (r->method_number == M_MERGE ||
                    r->method_number == M_LOCK ||
                    r->method_number == M_UNLOCK ||
                    ap_strstr_c(seg, special_uri))) {
            int rv;
            seg += strlen(root_dir_decoded);
            rv = proxy_request_fixup(r, master_uri, seg);
            if (rv) return rv;
            return OK;
        }
    }
    return OK;
}

typedef struct locate_ctx_t
{
    const apr_strmatch_pattern *pattern;
    apr_size_t pattern_len;
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
    const char *master_uri, *root_dir, *canonicalized_uri;
    apr_uri_t uri;

    /* Don't filter if we're in a subrequest or we aren't setup to
       proxy anything. */
    master_uri = dav_svn__get_master_uri(r);
    if (r->main || !master_uri) {
        ap_remove_input_filter(f);
        return ap_get_brigade(f->next, bb, mode, block, readbytes);
    }

    /* And don't filter if our search-n-replace would be a noop anyway
       (that is, if our root path matches that of the master server). */
    apr_uri_parse(r->pool, master_uri, &uri);
    root_dir = dav_svn__get_root_dir(r);
    canonicalized_uri = svn_urlpath__canonicalize(uri.path, r->pool);
    if (strcmp(canonicalized_uri, root_dir) == 0) {
        ap_remove_input_filter(f);
        return ap_get_brigade(f->next, bb, mode, block, readbytes);
    }

    /* Both CANONICALIZED_URI and ROOT_DIR are already canonical and
       URI-encoded (svn_urlpath__canonicalize() output and the stored
       <Location> path, respectively), which is the same domain the
       protocol bodies use on the wire. */
    if (!f->ctx) {
        ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));
        ctx->remotepath = canonicalized_uri;
        ctx->remotepath_len = strlen(ctx->remotepath);
        ctx->localpath = root_dir;
        ctx->localpath_len = strlen(ctx->localpath);
        ctx->pattern = apr_strmatch_precompile(r->pool, ctx->localpath, 1);
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
    const char *location, *start_foo = NULL;

    /* Don't filter if we're in a subrequest or we aren't setup to
       proxy anything. */
    master_uri = dav_svn__get_master_uri(r);
    if (r->main || !master_uri) {
        ap_remove_output_filter(f);
        return ap_pass_brigade(f->next, bb);
    }

    location = apr_table_get(r->headers_out, "Location");
    if (location) {
        start_foo = ap_strstr_c(location, master_uri);
    }
    if (start_foo) {
        const char *new_uri;
        start_foo += strlen(master_uri);
        new_uri = ap_construct_url(r->pool,
                                   apr_pstrcat(r->pool,
                                               dav_svn__get_root_dir(r), "/",
                                               start_foo, SVN_VA_NULL),
                                   r);
        apr_table_set(r->headers_out, "Location", new_uri);
    }
    return ap_pass_brigade(f->next, bb);
}

/* Only protocol XML (a multistatus, merge-response, etc.) carries hrefs
   the proxy must translate.  Since the body filter is attached only for
   MERGE and PROPFIND, anything non-XML reaching it is an error body
   (an httpd or mod_proxy error page, whose fixed Content-Length the
   rewrite would invalidate) and must pass through untouched. */
static svn_boolean_t
response_is_xml(const request_rec *r)
{
    return r->content_type
           && (ap_cstr_casecmpn(r->content_type, "text/xml",
                                sizeof("text/xml") - 1) == 0
               || ap_cstr_casecmpn(r->content_type, "application/xml",
                                   sizeof("application/xml") - 1) == 0);
}

apr_status_t dav_svn__location_body_filter(ap_filter_t *f,
                                           apr_bucket_brigade *bb)
{
    request_rec *r = f->r;
    locate_ctx_t *ctx = f->ctx;
    apr_bucket *bkt;
    const char *master_uri, *root_dir, *canonicalized_uri;
    apr_uri_t uri;

    /* Don't filter if we're in a subrequest or we aren't setup to
       proxy anything. */
    master_uri = dav_svn__get_master_uri(r);
    if (r->main || !master_uri) {
        ap_remove_output_filter(f);
        return ap_pass_brigade(f->next, bb);
    }

    /* And don't filter if our search-n-replace would be a noop anyway
       (that is, if our root path matches that of the master server). */
    apr_uri_parse(r->pool, master_uri, &uri);
    root_dir = dav_svn__get_root_dir(r);
    canonicalized_uri = svn_urlpath__canonicalize(uri.path, r->pool);
    if (strcmp(canonicalized_uri, root_dir) == 0) {
        ap_remove_output_filter(f);
        return ap_pass_brigade(f->next, bb);
    }

    /* This filter is only attached to MERGE and PROPFIND requests (see
       proxy_request_fixup()), whose responses are protocol XML carrying
       hrefs to translate.
       Anything that is not protocol XML is not ours to touch. */
    if (!response_is_xml(r)) {
        ap_remove_output_filter(f);
        return ap_pass_brigade(f->next, bb);
    }

    /* ### FIXME (SVN-3445, residual): a PROPFIND multistatus is still rewritten
       ### wholesale below, so a dead-property *value* that happens to contain
       ### the master location gets silently rewritten along with the genuine
       ### <D:href>s.  Fixing that safely requires an XML-structure-aware
       ### rewrite (translate hrefs only, leave property values alone) rather
       ### than the blind byte substitution used here. */

    /* Both CANONICALIZED_URI and ROOT_DIR are already canonical and
       URI-encoded (svn_urlpath__canonicalize() output and the stored
       <Location> path, respectively), which is the same domain the
       protocol bodies use on the wire. */
    if (!f->ctx) {
        ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));
        ctx->remotepath = canonicalized_uri;
        ctx->remotepath_len = strlen(ctx->remotepath);
        ctx->localpath = root_dir;
        ctx->localpath_len = strlen(ctx->localpath);
        ctx->pattern = apr_strmatch_precompile(r->pool, ctx->remotepath, 1);
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
