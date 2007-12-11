/*
 * mergeinfo.c :  routines for getting mergeinfo
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

#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_xml.h>
#include <apr_md5.h>

#include <http_request.h>
#include <http_log.h>
#include <mod_dav.h>

#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_dav.h"
#include "svn_mergeinfo.h"
#include "private/svn_dav_protocol.h"
#include "private/svn_mergeinfo_private.h"

#include "../dav_svn.h"


dav_error *
dav_svn__get_mergeinfo_report(const dav_resource *resource,
                              const apr_xml_doc *doc,
                              ap_filter_t *output)
{
  svn_error_t *serr;
  apr_status_t apr_err;
  dav_error *derr = NULL;
  apr_xml_elem *child;
  apr_hash_t *mergeinfo;
  dav_svn__authz_read_baton arb;
  const dav_svn_repos *repos = resource->info->repos;
  const char *action;
  int ns;
  apr_bucket_brigade *bb;

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
      /* else unknown element; skip it */
    }

  /* Build authz read baton */
  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  /* Build mergeinfo brigade */
  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

  serr = svn_repos_fs_get_mergeinfo(&mergeinfo, repos->repos, paths, rev,
                                    inherit, dav_svn__authz_read_func(&arb),
                                    &arb, resource->pool);
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

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

  if (mergeinfo != NULL && apr_hash_count (mergeinfo) > 0)
    {
      const void *key;
      void *value;
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(resource->pool, mergeinfo); hi;
           hi = apr_hash_next(hi))
        {
          const char *path, *info;
          const char itemformat[] = "<S:" SVN_DAV__MERGEINFO_ITEM ">"
            DEBUG_CR
            "<S:" SVN_DAV__MERGEINFO_PATH ">%s</S:" SVN_DAV__MERGEINFO_PATH ">"
            DEBUG_CR
            "<S:" SVN_DAV__MERGEINFO_INFO ">%s</S:" SVN_DAV__MERGEINFO_INFO ">"
            DEBUG_CR
            "</S:" SVN_DAV__MERGEINFO_ITEM ">";

          apr_hash_this(hi, &key, NULL, &value);
          path = (const char *)key + strlen(resource->info->repos_path);
          info = value;
          serr = dav_svn__send_xml(bb, output, itemformat,
                                   apr_xml_quote_string(resource->pool,
                                                        path, 0),
                                   apr_xml_quote_string(resource->pool,
                                                        info, 0));
          if (serr)
            {
              derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                          "Error ending REPORT response.",
                                          resource->pool);
              goto cleanup;
            }
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
  if (paths->nelts == 0)
    action = "get-mergeinfo";
  else if (paths->nelts == 1)
    action = apr_psprintf(resource->pool, "get-mergeinfo '%s'",
                          svn_path_uri_encode(APR_ARRAY_IDX
                                              (paths, 0, const char *),
                                              resource->pool));
  else
    action = apr_psprintf(resource->pool, "get-mergeinfo-partial '%s'",
                          svn_path_uri_encode(APR_ARRAY_IDX
                                              (paths, 0, const char *),
                                              resource->pool));

  apr_table_set(resource->info->r->subprocess_env, "SVN-ACTION", action);


  /* Flush the contents of the brigade (returning an error only if we
     don't already have one). */
  if ((apr_err = ap_fflush(output, bb)) && !derr)
    derr = dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                HTTP_INTERNAL_SERVER_ERROR,
                                "Error flushing brigade.",
                                resource->pool);
  return derr;
}

dav_error *
dav_svn__get_commit_and_merge_ranges_report(const dav_resource *resource,
                                            const apr_xml_doc *doc,
                                            ap_filter_t *output)
{
  apr_status_t apr_err;
  svn_error_t *serr;
  dav_error *derr = NULL;
  int ns;
  const char *action = "get-commit-and-merge-ranges";
  apr_array_header_t *commit_rangelist;
  apr_xml_elem *child;
  /* These get determined from the request document. */
  svn_revnum_t max_commit_rev = SVN_INVALID_REVNUM;
  svn_revnum_t min_commit_rev = SVN_INVALID_REVNUM;
  const char *merge_target = NULL;
  const char *merge_source = NULL;
  apr_array_header_t *merge_rangelist;
  svn_stringbuf_t *merge_rangelist_string, *commit_rangelist_string;
  /* By default look for explicit mergeinfo only. */
  svn_mergeinfo_inheritance_t inherit = svn_mergeinfo_explicit;
  const dav_svn_repos *repos = resource->info->repos;
  apr_hash_t *mergeinfo = apr_hash_make(resource->pool);
  svn_stringbuf_t *commit_rev_mergeinfo;
  apr_bucket_brigade *bb;
  dav_svn__authz_read_baton arb;

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

      if (strcmp(child->name, SVN_DAV__MAX_COMMIT_REVISION) == 0)
        /* ### Check for boundary cases, errors?  -Karl */
        max_commit_rev = SVN_STR_TO_REV(dav_xml_get_cdata(child, 
                                                          resource->pool, 1));
      else if (strcmp(child->name, SVN_DAV__MIN_COMMIT_REVISION) == 0)
        /* ### Check for boundary cases, errors?  -Karl */
        min_commit_rev = SVN_STR_TO_REV(dav_xml_get_cdata(child, 
                                                          resource->pool, 1));
      else if (strcmp(child->name, SVN_DAV__INHERIT) == 0)
        /* ### Check for boundary cases, errors?  -Karl */
        inherit = svn_inheritance_from_word(
                              dav_xml_get_cdata(child, resource->pool, 1));
      else if (strcmp(child->name, SVN_DAV__MERGE_SOURCE) == 0)
        {
          /* ### Big code duplication between this case and next. -Karl */
          const char *rel_path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(rel_path, resource->pool)))
            return derr;
          merge_source = svn_path_join(resource->info->repos_path, rel_path,
                                       resource->pool);
        }
      else if (strcmp(child->name, SVN_DAV__MERGE_TARGET) == 0)
        {
          /* ### Big code duplication between this case and prev. -Karl */
          const char *rel_path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(rel_path, resource->pool)))
            return derr;
          merge_target = svn_path_join(resource->info->repos_path, rel_path,
                                       resource->pool);
        }
      /* else unknown element; skip it */
    }

  /* Build authz read baton */
  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  /* Build mergeinfo brigade */
  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

  serr = svn_repos_get_commit_and_merge_ranges(&merge_rangelist,
                                               &commit_rangelist,
                                               repos->repos, merge_target,
                                               merge_source,
                                               min_commit_rev,
                                               max_commit_rev,
                                               inherit, 
                                               dav_svn__authz_read_func(&arb),
                                               &arb,
                                               resource->pool);
  /* ### Same error-handling code appears elsewhere.  -Karl */
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

  serr = svn_rangelist_to_stringbuf(&merge_rangelist_string, merge_rangelist,
                                    resource->pool);

  /* ### Same error-handling code appears elsewhere.  -Karl */
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

  serr = svn_rangelist_to_stringbuf(&commit_rangelist_string, commit_rangelist,
                                    resource->pool);

  /* ### Same error-handling code appears elsewhere.  -Karl */
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }


  serr = dav_svn__send_xml(bb, output,
                           DAV_XML_HEADER DEBUG_CR
                           "<S:" SVN_DAV__COMMIT_AND_MERGE_RANGES_REPORT " "
                           "xmlns:S=\"" SVN_XML_NAMESPACE "\" "
                           "xmlns:D=\"DAV:\">" DEBUG_CR);
  /* ### Same error-handling code appears elsewhere.  -Karl */
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

  serr = dav_svn__send_xml(bb, output,
                           "<S:" SVN_DAV__MERGE_RANGES ">%s</S:"
                           SVN_DAV__MERGE_RANGES ">" DEBUG_CR,
                           merge_rangelist_string->data);
  /* ### Same error-handling code appears elsewhere.  -Karl */
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }


  serr = dav_svn__send_xml(bb, output,
                           "<S:" SVN_DAV__COMMIT_RANGES ">%s</S:"
                           SVN_DAV__COMMIT_RANGES ">" DEBUG_CR,
                           commit_rangelist_string->data);
  /* ### Same error-handling code appears elsewhere.  -Karl */
  if (serr)
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

  serr = dav_svn__send_xml(bb, output, "</S:"
                           SVN_DAV__COMMIT_AND_MERGE_RANGES_REPORT
                           ">" DEBUG_CR);
  if (serr)
    {
      /* ### Same error-handling code appears elsewhere.  -Karl */
      derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                  "Error ending REPORT response.",
                                  resource->pool);
      goto cleanup;
    }

 cleanup:
  apr_table_set(resource->info->r->subprocess_env, "SVN-ACTION", action);


  /* Flush the contents of the brigade (returning an error only if we
     don't already have one). */
  if ((apr_err = ap_fflush(output, bb)) && !derr)
    derr = dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                HTTP_INTERNAL_SERVER_ERROR,
                                "Error flushing brigade.", resource->pool);
  return derr;
}
