/*
 * mergeinfo.c :  routines for getting mergeinfo
 *
 * ====================================================================
 * Copyright (c) 2006-2008 CollabNet.  All rights reserved.
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
#include "private/svn_dav_protocol.h"
#include "private/svn_log.h"
#include "private/svn_mergeinfo_private.h"

#include "../dav_svn.h"


dav_error *
dav_svn__get_mergeinfo_report(const dav_resource *resource,
                              const apr_xml_doc *doc,
                              ap_filter_t *output)
{
  svn_error_t *serr;
  dav_error *derr = NULL;
  apr_xml_elem *child;
  svn_mergeinfo_catalog_t catalog;
  svn_boolean_t include_descendants = FALSE;
  dav_svn__authz_read_baton arb;
  const dav_svn_repos *repos = resource->info->repos;
  int ns;
  apr_bucket_brigade *bb;
  apr_hash_index_t *hi;
  svn_boolean_t sent_anything = FALSE;

  /* These get determined from the request document. */
  svn_revnum_t rev = SVN_INVALID_REVNUM;
  /* By default look for explicit mergeinfo only. */
  svn_mergeinfo_inheritance_t inherit = svn_mergeinfo_explicit;
  apr_array_header_t *paths
    = apr_array_make(resource->pool, 0, sizeof(const char *));

  /* Sanity check. */
  ns = dav_svn__find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                    "The request does not contain the 'svn:' "
                                    "namespace, so it is not going to have "
                                    "certain required elements.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
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
          target = svn_path_join(resource->info->repos_path, rel_path,
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
  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

  serr = svn_repos_fs_get_mergeinfo(&catalog, repos->repos, paths, rev,
                                    inherit, include_descendants,
                                    dav_svn__authz_read_func(&arb),
                                    &arb, resource->pool);
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

  serr = svn_mergeinfo__remove_prefix_from_catalog(&catalog, catalog,
                                                   resource->info->repos_path,
                                                   resource->pool);
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

  /* Ideally, dav_svn__send_xml() would set a flag in bb (or rather,
     in r->sent_bodyct, see dav_method_report()), and ap_fflush()
     would not set that flag unless it actually sent something.  But
     we are condemned to live in another universe, so we must keep
     track ourselves of whether we've sent anything or not.  See the
     long comment after the 'cleanup' label for more details. */
  sent_anything = TRUE;
  serr = dav_svn__send_xml(bb, output,
                           DAV_XML_HEADER DEBUG_CR
                           "<S:" SVN_DAV__MERGEINFO_REPORT " "
                           "xmlns:S=\"" SVN_XML_NAMESPACE "\" "
                           "xmlns:D=\"DAV:\">" DEBUG_CR);
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

  for (hi = apr_hash_first(resource->pool, catalog); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *value;
      const char *path;
      svn_mergeinfo_t mergeinfo;
      svn_string_t *mergeinfo_string;
      const char itemformat[] = "<S:" SVN_DAV__MERGEINFO_ITEM ">"
        DEBUG_CR
        "<S:" SVN_DAV__MERGEINFO_PATH ">%s</S:" SVN_DAV__MERGEINFO_PATH ">"
        DEBUG_CR
        "<S:" SVN_DAV__MERGEINFO_INFO ">%s</S:" SVN_DAV__MERGEINFO_INFO ">"
        DEBUG_CR
        "</S:" SVN_DAV__MERGEINFO_ITEM ">";

      apr_hash_this(hi, &key, NULL, &value);
      path = key;
      mergeinfo = value;
      serr = svn_mergeinfo_to_string(&mergeinfo_string, mergeinfo,
                                     resource->pool);
      if (serr)
        {
          derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                      "Error ending REPORT response.",
                                      resource->pool);
          goto cleanup;
        }
      serr = dav_svn__send_xml(bb, output, itemformat,
                               apr_xml_quote_string(resource->pool,
                                                    path, 0),
                               apr_xml_quote_string(resource->pool,
                                                    mergeinfo_string->data, 0));
      if (serr)
        {
          derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                      "Error ending REPORT response.",
                                      resource->pool);
          goto cleanup;
        }
    }

  if ((serr = dav_svn__send_xml(bb, output,
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
