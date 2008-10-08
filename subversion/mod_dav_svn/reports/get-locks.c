/*
 * version.c: mod_dav_svn versioning provider functions for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include <apr_tables.h>
#include <apr_uuid.h>

#include <httpd.h>
#include <http_log.h>
#include <mod_dav.h>

#include "svn_fs.h"
#include "svn_xml.h"
#include "svn_repos.h"
#include "svn_dav.h"
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_dav.h"
#include "svn_base64.h"

#include "../dav_svn.h"


/* Respond to a get-locks-report request.  See description of this
   report in libsvn_ra_dav/fetch.c.  */
dav_error *
dav_svn__get_locks_report(const dav_resource *resource,
                          const apr_xml_doc *doc,
                          ap_filter_t *output)
{
  apr_bucket_brigade *bb;
  svn_error_t *err;
  apr_status_t apr_err;
  apr_hash_t *locks;
  dav_svn__authz_read_baton arb;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  /* The request URI should be a public one representing an fs path. */
  if ((! resource->info->repos_path)
      || (! resource->info->repos->repos))
    return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
                         "get-locks-report run on resource which doesn't "
                         "represent a path within a repository.");

  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  /* Fetch the locks, but allow authz_read checks to happen on each. */
  if ((err = svn_repos_fs_get_locks(&locks,
                                    resource->info->repos->repos,
                                    resource->info->repos_path,
                                    dav_svn__authz_read_func(&arb), &arb,
                                    resource->pool)) != SVN_NO_ERROR)
    return dav_svn__convert_err(err, HTTP_INTERNAL_SERVER_ERROR,
                                err->message, resource->pool);

  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

  /* start sending report */
  apr_err = ap_fprintf(output, bb,
                       DAV_XML_HEADER DEBUG_CR
                       "<S:get-locks-report xmlns:S=\"" SVN_XML_NAMESPACE "\" "
                       "xmlns:D=\"DAV:\">" DEBUG_CR);
  if (apr_err)
    return dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                HTTP_INTERNAL_SERVER_ERROR,
                                "Error writing REPORT response.",
                                resource->pool);

  /* stream the locks */
  subpool = svn_pool_create(resource->pool);
  for (hi = apr_hash_first(resource->pool, locks); hi; hi = apr_hash_next(hi))
    {
      void *val;
      const svn_lock_t *lock;
      const char *path_quoted, *token_quoted;
      const char *creation_str, *expiration_str;
      const char *owner_to_send, *comment_to_send;
      svn_boolean_t owner_base64 = FALSE, comment_base64 = FALSE;

      svn_pool_clear(subpool);
      apr_hash_this(hi, NULL, NULL, &val);
      lock = val;

      path_quoted = apr_xml_quote_string(subpool, lock->path, 1);
      token_quoted = apr_xml_quote_string(subpool, lock->token, 1);
      creation_str = svn_time_to_cstring(lock->creation_date, subpool);

      apr_err = ap_fprintf(output, bb,
                           "<S:lock>" DEBUG_CR
                           "<S:path>%s</S:path>" DEBUG_CR
                           "<S:token>%s</S:token>" DEBUG_CR
                           "<S:creationdate>%s</S:creationdate>" DEBUG_CR,
                           path_quoted, token_quoted, creation_str);
      if (apr_err)
        return dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                    HTTP_INTERNAL_SERVER_ERROR,
                                    "Error writing REPORT response.",
                                    resource->pool);

      if (lock->expiration_date)
        {
          expiration_str = svn_time_to_cstring(lock->expiration_date, subpool);
          apr_err = ap_fprintf(output, bb,
                               "<S:expirationdate>%s</S:expirationdate>"
                               DEBUG_CR, expiration_str);
          if (apr_err)
            return dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                        HTTP_INTERNAL_SERVER_ERROR,
                                        "Error writing REPORT response.",
                                        resource->pool);
        }

      if (svn_xml_is_xml_safe(lock->owner, strlen(lock->owner)))
        {
          owner_to_send = apr_xml_quote_string(subpool, lock->owner, 1);
        }
      else
        {
          svn_string_t owner_string;
          const svn_string_t *encoded_owner;

          owner_string.data = lock->owner;
          owner_string.len = strlen(lock->owner);
          encoded_owner = svn_base64_encode_string2(&owner_string, TRUE,
                                                    subpool);
          owner_to_send = encoded_owner->data;
          owner_base64 = TRUE;
        }

      apr_err = ap_fprintf(output, bb,
                           "<S:owner %s>%s</S:owner>" DEBUG_CR,
                           owner_base64 ? "encoding=\"base64\"" : "",
                           owner_to_send);
      if (apr_err)
        return dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                    HTTP_INTERNAL_SERVER_ERROR,
                                    "Error writing REPORT response.",
                                    resource->pool);

      if (lock->comment)
        {
          if (svn_xml_is_xml_safe(lock->comment, strlen(lock->comment)))
            {
              comment_to_send = apr_xml_quote_string(subpool,
                                                     lock->comment, 1);
            }
          else
            {
              svn_string_t comment_string;
              const svn_string_t *encoded_comment;

              comment_string.data = lock->comment;
              comment_string.len = strlen(lock->comment);
              encoded_comment = svn_base64_encode_string2(&comment_string,
                                                          TRUE, subpool);
              comment_to_send = encoded_comment->data;
              comment_base64 = TRUE;
            }

          apr_err = ap_fprintf(output, bb,
                               "<S:comment %s>%s</S:comment>" DEBUG_CR,
                               comment_base64 ? "encoding=\"base64\"" : "",
                               comment_to_send);
          if (apr_err)
            return dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                        HTTP_INTERNAL_SERVER_ERROR,
                                        "Error writing REPORT response.",
                                        resource->pool);
        }

      apr_err = ap_fprintf(output, bb, "</S:lock>" DEBUG_CR);
      if (apr_err)
        return dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                    HTTP_INTERNAL_SERVER_ERROR,
                                    "Error writing REPORT response.",
                                    resource->pool);
    } /* end of hash loop */
  svn_pool_destroy(subpool);

  /* finish the report */
  apr_err = ap_fprintf(output, bb, "</S:get-locks-report>" DEBUG_CR);
  if (apr_err)
    return dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                HTTP_INTERNAL_SERVER_ERROR,
                                "Error writing REPORT response.",
                                resource->pool);

  /* Flush the contents of the brigade. */
  if ((apr_err = ap_fflush(output, bb)))
    return dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                HTTP_INTERNAL_SERVER_ERROR,
                                "Error flushing brigade.",
                                resource->pool);

  return NULL;
}
