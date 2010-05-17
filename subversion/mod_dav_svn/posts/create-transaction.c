/*
 * create-transaction.c: mod_dav_svn POST handler for creating a new
 *                       commit transaction
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

#include <apr_tables.h>

#include <httpd.h>
#include <mod_dav.h>

#include "svn_dav.h"
#include "../dav_svn.h"


/* Respond to a S:dated-rev-report request. */
int
dav_svn__create_transaction_post(const dav_resource *resource,
                                 const apr_xml_doc *doc,
                                 ap_filter_t *output)
{
  request_rec *r = resource->info->r;
  apr_bucket_brigade *bb;
  apr_status_t apr_err;
  dav_error *derr = NULL;
  const char *txn_name;

  /* Create a Subversion repository transaction based on HEAD, and
     return the new transaction's name in a custom "201 Created"
     response header.  */
  derr = dav_svn__create_txn(resource->info->repos, &txn_name, resource->pool);
  if (derr)
    return dav_svn__error_response_tag(r, derr);

  /* We'll set this header only because some early 1.7-dev client
     expect it. */
  apr_table_set(r->headers_out, SVN_DAV_TXN_NAME_HEADER, txn_name);
  r->status = HTTP_CREATED;

  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);
  apr_err = ap_fprintf(output, bb,
                       DAV_XML_HEADER DEBUG_CR
                       "<S:transaction xmlns:S=\"" SVN_XML_NAMESPACE "\""
                       ">%ld</S:transaction",
                       apr_xml_quote_string(resource->pool, txn_name, 0));
  if (apr_err)
    derr = dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                HTTP_INTERNAL_SERVER_ERROR,
                                "Error writing POST response.",
                                resource->pool);
      
  return dav_svn__final_flush_or_error(r, bb, output, derr, resource->pool);
}
