/*
 * propfind_buckets.c :  serf bucket for a PROPFIND request
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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



#include <stdlib.h>

#include <apr_pools.h>
#include <apr_strings.h>

#include <serf.h>
#include <serf_bucket_util.h>

#include "ra_serf.h"

typedef struct {
    svn_ra_serf__connection_t *conn;
    const char *path;
    const char *label;
    const char *depth;
    const svn_ra_serf__dav_props_t *find_props;
    serf_bucket_t *request;
} prop_context_t;

const serf_bucket_type_t serf_bucket_type_propfind;

#define PROPFIND_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?><propfind xmlns=\"DAV:\">"
#define PROPFIND_TRAILER "</propfind>"

serf_bucket_t * svn_ra_serf__bucket_propfind_create(
    svn_ra_serf__connection_t *conn,
    const char *path,
    const char *label,
    const char *depth,
    const svn_ra_serf__dav_props_t *find_props,
    serf_bucket_alloc_t *allocator)
{
    prop_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->conn = conn;
    ctx->path = path;
    ctx->label = label;
    ctx->depth = depth;
    ctx->find_props = find_props;

    return serf_bucket_create(&serf_bucket_type_propfind, allocator, ctx);
}

static serf_bucket_t *create_propfind_body(serf_bucket_t *bucket)
{
  prop_context_t *ctx = bucket->data;
  serf_bucket_alloc_t *alloc = bucket->allocator;
  serf_bucket_t *body_bkt, *tmp;
  const svn_ra_serf__dav_props_t *prop;
  svn_boolean_t requested_allprop = FALSE;

  body_bkt = serf_bucket_aggregate_create(alloc);

  prop = ctx->find_props;
  while (prop && prop->namespace)
    {
      /* special case the allprop case. */
      if (strcmp(prop->name, "allprop") == 0)
        {
          requested_allprop = TRUE;
        }

      /* <*propname* xmlns="*propns*" /> */
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<", 1, alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING(prop->name, alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" xmlns=\"",
                                          sizeof(" xmlns=\"")-1,
                                          alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING(prop->namespace, alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"/>", sizeof("\"/>")-1,
                                          alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      prop++;
    }

  /* If we're not doing an allprop, add <prop> tags. */
  if (requested_allprop == FALSE)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<prop>",
                                          sizeof("<prop>")-1,
                                          alloc);
      serf_bucket_aggregate_prepend(body_bkt, tmp);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(PROPFIND_HEADER,
                                      sizeof(PROPFIND_HEADER)-1,
                                      alloc);

  serf_bucket_aggregate_prepend(body_bkt, tmp);

  if (requested_allprop == FALSE)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</prop>",
                                          sizeof("</prop>")-1,
                                          alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(PROPFIND_TRAILER,
                                      sizeof(PROPFIND_TRAILER)-1,
                                      alloc);
  serf_bucket_aggregate_append(body_bkt, tmp);

  return body_bkt;
}

static void become_request(serf_bucket_t *bucket)
{
  prop_context_t *ctx = bucket->data;
  serf_bucket_t *hdrs_bkt, *body_bkt;

  body_bkt = create_propfind_body(bucket);

  serf_bucket_request_become(bucket, "PROPFIND", ctx->path, body_bkt);

  hdrs_bkt = serf_bucket_request_get_headers(bucket);

  serf_bucket_headers_setn(hdrs_bkt, "Host", ctx->conn->hostinfo);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent", "svn/ra_serf");
  if (ctx->conn->using_compression == TRUE)
    {
      serf_bucket_headers_setn(hdrs_bkt, "Accept-Encoding", "gzip");
    }
  serf_bucket_headers_setn(hdrs_bkt, "Content-Type", "text/xml");
  serf_bucket_headers_setn(hdrs_bkt, "Depth", ctx->depth);
  if (ctx->label)
    {
      serf_bucket_headers_setn(hdrs_bkt, "Label", ctx->label);
    }
  if (ctx->conn->auth_header && ctx->conn->auth_value)
    {
      serf_bucket_headers_setn(hdrs_bkt,
                               ctx->conn->auth_header, ctx->conn->auth_value);
    }

  serf_bucket_mem_free(bucket->allocator, ctx);
}

static apr_status_t serf_propfind_read(serf_bucket_t *bucket,
                                       apr_size_t requested,
                                       const char **data, apr_size_t *len)
{
  become_request(bucket);

  /* Delegate to the "new" request bucket to do the readline. */
  return serf_bucket_read(bucket, requested, data, len);
}

static apr_status_t serf_propfind_readline(serf_bucket_t *bucket,
                                           int acceptable, int *found,
                                           const char **data, apr_size_t *len)
{
  become_request(bucket);

  /* Delegate to the "new" request bucket to do the readline. */
  return serf_bucket_readline(bucket, acceptable, found, data, len);
}

static apr_status_t serf_propfind_read_iovec(serf_bucket_t *bucket,
                                             apr_size_t requested,
                                             int vecs_size,
                                             struct iovec *vecs,
                                             int *vecs_used)
{
  become_request(bucket);

  /* Delegate to the "new" request bucket to do the peek. */
  return serf_bucket_read_iovec(bucket, requested, vecs_size, vecs, vecs_used);
}

static apr_status_t serf_propfind_peek(serf_bucket_t *bucket,
                                      const char **data,
                                      apr_size_t *len)
{
  become_request(bucket);

  /* Delegate to the "new" request bucket to do the peek. */
  return serf_bucket_peek(bucket, data, len);
}

SERF_DECLARE_DATA const serf_bucket_type_t serf_bucket_type_propfind = {
  "PROPFIND",
  serf_propfind_read,
  serf_propfind_readline,
  serf_propfind_read_iovec,
  serf_default_read_for_sendfile,
  serf_default_read_bucket,
  serf_propfind_peek,
  serf_default_destroy_and_data,
};

