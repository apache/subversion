/*
 * mergeinfo.c: mod_dav_svn REPORT handler for querying mergeinfo
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

#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_xml.h>

#include <http_request.h>
#include <http_log.h>
#include <mod_dav.h>

#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_dav.h"

#include "private/svn_fspath.h"
#include "private/svn_dav_protocol.h"
#include "private/svn_log.h"
#include "private/svn_mergeinfo_private.h"

#include "../dav_svn.h"

/* Baton type to be used with mergeinfo_receiver. */
typedef struct mergeinfo_receiver_baton_t
{
  /* Start path of the query; report paths relative to this one. */
  const char *fs_path;

  /* Allocate the BRIGADE contents here. */
  apr_pool_t *resource_pool;

  /* Attach the response to this brigade. */
  apr_bucket_brigade *brigade;

  /* Send the response out here. */
  dav_svn__output *output;

  /* Did we already send the opening sequence? */
  svn_boolean_t starting_tuple_sent;
} mergeinfo_receiver_baton_t;

/* Utility method sending the start of the "get m/i" response once
   over BATON. */
static svn_error_t *
send_mergeinfo_starting_sequence(mergeinfo_receiver_baton_t *baton,
                                apr_pool_t *scratch_pool)
{
  if (baton->starting_tuple_sent)
    return SVN_NO_ERROR;

  /* Ideally, dav_svn__brigade_printf() would set a flag in bb (or rather,
     in r->sent_bodyct, see dav_method_report()), and ap_fflush()
     would not set that flag unless it actually sent something.  But
     we are condemned to live in another universe, so we must keep
     track ourselves of whether we've sent anything or not.  See the
     long comment after the 'cleanup' label for more details. */
  SVN_ERR(dav_svn__brigade_puts(baton->brigade, baton->output,
                                DAV_XML_HEADER DEBUG_CR
                                "<S:" SVN_DAV__MERGEINFO_REPORT " "
                                "xmlns:S=\"" SVN_XML_NAMESPACE "\" "
                                "xmlns:D=\"DAV:\">" DEBUG_CR));
  baton->starting_tuple_sent = TRUE;

  return SVN_NO_ERROR;
}

/* Implements svn_repos_mergeinfo_receiver_t, sending the MERGEINFO
 * out over the connection in the mergeinfo_receiver_baton_t * BATON. */
static svn_error_t *
mergeinfo_receiver(const char *path,
                   svn_mergeinfo_t mergeinfo,
                   void *baton,
                   apr_pool_t *scratch_pool)
{
  mergeinfo_receiver_baton_t *b = baton;
  svn_string_t *mergeinfo_string;

  /* Delay starting the response until we checked that the initial
     request went through.  We are at that point now b/c we've got
     the first results in. */
  SVN_ERR(send_mergeinfo_starting_sequence(b, scratch_pool));

  /* Adjust the path info and send the m/i. */
  path = svn_fspath__skip_ancestor(b->fs_path, path);
  SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_string, mergeinfo,
                                  scratch_pool));

  SVN_ERR(dav_svn__brigade_printf
        (b->brigade, b->output,
         "<S:" SVN_DAV__MERGEINFO_ITEM ">"
         DEBUG_CR
         "<S:" SVN_DAV__MERGEINFO_PATH ">%s</S:" SVN_DAV__MERGEINFO_PATH ">"
         DEBUG_CR
         "<S:" SVN_DAV__MERGEINFO_INFO ">%s</S:" SVN_DAV__MERGEINFO_INFO ">"
         DEBUG_CR
         "</S:" SVN_DAV__MERGEINFO_ITEM ">",
         apr_xml_quote_string(b->resource_pool, path, 0),
         apr_xml_quote_string(b->resource_pool, mergeinfo_string->data, 0)));

  return SVN_NO_ERROR;
}

dav_error *
dav_svn__get_mergeinfo_report(const dav_resource *resource,
                              const apr_xml_doc *doc,
                              dav_svn__output *output)
{
  svn_error_t *serr;
  dav_error *derr = NULL;
  apr_xml_elem *child;
  svn_boolean_t include_descendants = FALSE;
  dav_svn__authz_read_baton arb;
  const dav_svn_repos *repos = resource->info->repos;
  int ns;
  apr_bucket_brigade *bb;
  mergeinfo_receiver_baton_t receiver_baton;

  /* These get determined from the request document. */
  svn_revnum_t rev = SVN_INVALID_REVNUM;
  /* By default look for explicit mergeinfo only. */
  svn_mergeinfo_inheritance_t inherit = svn_mergeinfo_explicit;
  apr_array_header_t *paths
    = apr_array_make(resource->pool, 0, sizeof(const char *));

  /* Sanity check. */
  if (!resource->info->repos_path)
    return dav_svn__new_error(resource->pool, HTTP_BAD_REQUEST, 0, 0,
                              "The request does not specify a repository path");
  ns = dav_svn__find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      return dav_svn__new_error_svn(resource->pool, HTTP_BAD_REQUEST, 0, 0,
                                    "The request does not contain the 'svn:' "
                                    "namespace, so it is not going to have "
                                    "certain required elements");
    }

  for (child = doc->root->first_child; child != NULL; child = child->next)
    {
      /* if this element isn't one of ours, then skip it */
      if (child->ns != ns)
        continue;

      if (strcmp(child->name, SVN_DAV__REVISION) == 0)
        rev = SVN_STR_TO_REV(dav_xml_get_cdata(child, resource->pool, 1));
      else if (strcmp(child->name, SVN_DAV__INHERIT) == 0)
        {
          inherit = svn_inheritance_from_word(
            dav_xml_get_cdata(child, resource->pool, 1));
        }
      else if (strcmp(child->name, SVN_DAV__PATH) == 0)
        {
          const char *target;
          const char *rel_path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(rel_path, resource->pool)))
            return derr;

          /* Force REL_PATH to be a relative path, not an fspath. */
          rel_path = svn_relpath_canonicalize(rel_path, resource->pool);

          /* Append the REL_PATH to the base FS path to get an
             absolute repository path. */
          target = svn_fspath__join(resource->info->repos_path, rel_path,
                                    resource->pool);
          (*((const char **)(apr_array_push(paths)))) = target;
        }
      else if (strcmp(child->name, SVN_DAV__INCLUDE_DESCENDANTS) == 0)
        {
          const char *word = dav_xml_get_cdata(child, resource->pool, 1);
          if (strcmp(word, "yes") == 0)
            include_descendants = TRUE;
          /* Else the client isn't supposed to send anyway, so just
             leave it false. */
        }
      /* else unknown element; skip it */
    }

  /* Build authz read baton */
  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  /* Build mergeinfo brigade */
  bb = apr_brigade_create(resource->pool,
                          dav_svn__output_get_bucket_alloc(output));

  receiver_baton.brigade = bb;
  receiver_baton.output = output;
  receiver_baton.fs_path = resource->info->repos_path;
  receiver_baton.resource_pool = resource->pool;
  receiver_baton.starting_tuple_sent = FALSE;

  serr = svn_repos_fs_get_mergeinfo2(repos->repos, paths, rev,
                                     inherit, include_descendants,
                                     dav_svn__authz_read_func(&arb), &arb,
                                     mergeinfo_receiver, &receiver_baton,
                                     resource->pool);
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, NULL,
                                  resource->pool);
      goto cleanup;
    }

  /* We might not have sent anything
     => ensure to begin the response in any case. */
  serr = send_mergeinfo_starting_sequence(&receiver_baton, resource->pool);
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, NULL,
                                  resource->pool);
      goto cleanup;
    }


  if ((serr = dav_svn__brigade_puts(bb, output,
                                    "</S:" SVN_DAV__MERGEINFO_REPORT ">"
                                    DEBUG_CR)))
    {
      derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                  "Error ending REPORT response.",
                                  resource->pool);
      goto cleanup;
    }

 cleanup:

  /* We've detected a 'high level' svn action to log. */
  dav_svn__operational_log(resource->info,
                           svn_log__get_mergeinfo(paths, inherit,
                                                  include_descendants,
                                                  resource->pool));

  return dav_svn__final_flush_or_error(resource->info->r, bb, output,
                                       derr, resource->pool);
}
