/*
 * inherited-props.c: mod_dav_svn REPORT handler for querying inherited props.
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
#include "svn_props.h"
#include "svn_base64.h"

#include "private/svn_fspath.h"
#include "private/svn_dav_protocol.h"
#include "private/svn_log.h"
#include "private/svn_mergeinfo_private.h"

#include "../dav_svn.h"

dav_error *
dav_svn__get_inherited_props_report(const dav_resource *resource,
                                    const apr_xml_doc *doc,
                                    dav_svn__output *output)
{
  svn_error_t *serr;
  dav_error *derr = NULL;
  apr_xml_elem *child;
  apr_array_header_t *inherited_props;
  dav_svn__authz_read_baton arb;
  int ns;
  apr_bucket_brigade *bb;
  const char *path = "/";
  svn_fs_root_t *root;
  int i;
  svn_revnum_t rev = SVN_INVALID_REVNUM;
  apr_pool_t *iterpool;
  svn_node_kind_t kind;

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

  iterpool = svn_pool_create(resource->pool);

  for (child = doc->root->first_child;
       child != NULL;
       child = child->next)
    {
      /* if this element isn't one of ours, then skip it */
      if (child->ns != ns)
        continue;

      if (strcmp(child->name, SVN_DAV__REVISION) == 0)
        {
          rev = SVN_STR_TO_REV(dav_xml_get_cdata(child, iterpool, 1));
        }
      else if (strcmp(child->name, SVN_DAV__PATH) == 0)
        {
          path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(path, iterpool)))
            return derr;
          path = svn_fspath__join(resource->info->repos_path, path,
                                  resource->pool);
        }
      /* else unknown element; skip it */
    }

  /* Build authz read baton */
  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  /* Build inherited property brigade */
  bb = apr_brigade_create(resource->pool,
                          dav_svn__output_get_bucket_alloc(output));

  serr = svn_fs_revision_root(&root, resource->info->repos->fs,
                              rev, resource->pool);
  if (serr != NULL)
    return dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                "couldn't retrieve revision root",
                                resource->pool);

  serr = svn_fs_check_path(&kind, root, path, resource->pool);
  if (!serr && kind == svn_node_none)
    {
      serr = svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                               "'%s' path not found", path);
    }

  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, NULL,
                                  resource->pool);
      goto cleanup;
    }

  serr = svn_repos_fs_get_inherited_props(&inherited_props, root, path, NULL,
                                          dav_svn__authz_read_func(&arb),
                                          &arb, resource->pool, iterpool);
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, NULL,
                                  resource->pool);
      goto cleanup;
    }

  serr = dav_svn__brigade_puts(bb, output,
                               DAV_XML_HEADER DEBUG_CR
                               "<S:" SVN_DAV__INHERITED_PROPS_REPORT " "
                               "xmlns:S=\"" SVN_XML_NAMESPACE "\" "
                               "xmlns:D=\"DAV:\">" DEBUG_CR);
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, NULL,
                                  resource->pool);
      goto cleanup;
    }

  for (i = 0; i < inherited_props->nelts; i++)
    {
      svn_prop_inherited_item_t *elt =
        APR_ARRAY_IDX(inherited_props, i, svn_prop_inherited_item_t *);

      svn_pool_clear(iterpool);

      serr = dav_svn__brigade_printf(
        bb, output,
        "<S:" SVN_DAV__IPROP_ITEM ">"
        DEBUG_CR
        "<S:" SVN_DAV__IPROP_PATH ">%s</S:" SVN_DAV__IPROP_PATH ">"
        DEBUG_CR,
        apr_xml_quote_string(resource->pool, elt->path_or_url, 0));

      if (!serr)
        {
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(resource->pool, elt->prop_hash);
               hi;
               hi = apr_hash_next(hi))
            {
              const char *propname = apr_hash_this_key(hi);
              svn_string_t *propval = apr_hash_this_val(hi);
              const char *xml_safe;

              serr = dav_svn__brigade_printf(
                bb, output,
                "<S:" SVN_DAV__IPROP_PROPNAME ">%s</S:"
                SVN_DAV__IPROP_PROPNAME ">" DEBUG_CR,
                apr_xml_quote_string(iterpool, propname, 0));

              if (!serr)
                {
                  if (svn_xml_is_xml_safe(propval->data, propval->len))
                    {
                      svn_stringbuf_t *tmp = NULL;
                      svn_xml_escape_cdata_string(&tmp, propval,
                                                  iterpool);
                      xml_safe = tmp->data;
                      serr = dav_svn__brigade_printf(
                        bb, output,
                        "<S:" SVN_DAV__IPROP_PROPVAL ">%s</S:"
                        SVN_DAV__IPROP_PROPVAL ">" DEBUG_CR, xml_safe);
                    }
                  else
                    {
                      xml_safe = svn_base64_encode_string2(
                        propval, TRUE, iterpool)->data;
                      serr = dav_svn__brigade_printf(
                        bb, output,
                        "<S:" SVN_DAV__IPROP_PROPVAL
                        " encoding=\"base64\"" ">%s</S:"
                        SVN_DAV__IPROP_PROPVAL ">" DEBUG_CR, xml_safe);
                    }
                }

              if (serr)
                break;
            }
          if (!serr)
            serr = dav_svn__brigade_printf(bb, output,
                                           "</S:" SVN_DAV__IPROP_ITEM ">"
                                           DEBUG_CR);
        }

      if (serr)
        {
          derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                      "Error ending REPORT response.",
                                      resource->pool);
          goto cleanup;
        }
    }

  if ((serr = dav_svn__brigade_puts(bb, output,
                                    "</S:" SVN_DAV__INHERITED_PROPS_REPORT ">"
                                    DEBUG_CR)))
    {
      derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                  "Error ending REPORT response.",
                                  resource->pool);
      goto cleanup;
    }

 cleanup:

  /* Log this 'high level' svn action. */
  dav_svn__operational_log(resource->info,
                           svn_log__get_inherited_props(path, rev,
                                                        resource->pool));
  svn_pool_destroy(iterpool);
  return dav_svn__final_flush_or_error(resource->info->r, bb, output,
                                       derr, resource->pool);
}
