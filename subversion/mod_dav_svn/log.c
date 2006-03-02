/*
 * log.c: handle the log-report request and response
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



#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_xml.h>
#include <mod_dav.h>

#include "svn_repos.h"
#include "svn_types.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_dav.h"

#include "dav_svn.h"


struct log_receiver_baton
{
  /* this buffers the output for a bit and is automatically flushed,
     at appropriate times, by the Apache filter system. */
  apr_bucket_brigade *bb;

  /* where to deliver the output */
  ap_filter_t *output;

  /* Whether we've written the <S:log-report> header.  Allows for lazy
     writes to support mod_dav-based error handling. */
  svn_boolean_t needs_header;
};


/* If LRB->needs_header is true, send the "<S:log-report>" start
   element and set LRB->needs_header to zero.  Else do nothing.
   This is basically duplicated in file_revs.c.  Consider factoring if
   duplicating again. */
static svn_error_t * maybe_send_header(struct log_receiver_baton *lrb)
{
  if (lrb->needs_header)
    {
      SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                                DAV_XML_HEADER DEBUG_CR
                                "<S:log-report xmlns:S=\"" SVN_XML_NAMESPACE
                                "\" " "xmlns:D=\"DAV:\">" DEBUG_CR));
      lrb->needs_header = FALSE;
    }
  return SVN_NO_ERROR;
}

/* This implements `svn_log_message_receiver_t'.
   BATON is a `struct log_receiver_baton *'.  */
static svn_error_t * log_receiver(void *baton,
                                  apr_hash_t *changed_paths,
                                  svn_revnum_t rev,
                                  const char *author,
                                  const char *date,
                                  const char *msg,
                                  apr_pool_t *pool)
{
  struct log_receiver_baton *lrb = baton;

  SVN_ERR(maybe_send_header(lrb));

  SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                            "<S:log-item>" DEBUG_CR "<D:version-name>%ld"
                            "</D:version-name>" DEBUG_CR, rev));

  if (author)
    SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                              "<D:creator-displayname>%s"
                              "</D:creator-displayname>" DEBUG_CR,
                              apr_xml_quote_string(pool, author, 0)));

  /* ### this should be DAV:creation-date, but we need to format
     ### that date a bit differently */
  if (date)
    SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                              "<S:date>%s</S:date>" DEBUG_CR,
                              apr_xml_quote_string(pool, date, 0)));

  if (msg)
    SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                              "<D:comment>%s</D:comment>" DEBUG_CR,
                              apr_xml_quote_string
                              (pool, svn_xml_fuzzy_escape(msg, pool), 0)));


  if (changed_paths)
    {
      apr_hash_index_t *hi;
      char *path;

      for (hi = apr_hash_first(pool, changed_paths);
           hi != NULL;
           hi = apr_hash_next(hi))
        {
          void *val;
          svn_log_changed_path_t *log_item;
          
          apr_hash_this(hi, (void *) &path, NULL, &val);
          log_item = val;

          /* ### todo: is there a D: namespace equivalent for
             `changed-path'?  Should use it if so. */
          switch (log_item->action)
            {
            case 'A':
              if (log_item->copyfrom_path 
                  && SVN_IS_VALID_REVNUM(log_item->copyfrom_rev))
                SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output, 
                                          "<S:added-path"
                                          " copyfrom-path=\"%s\"" 
                                          " copyfrom-rev=\"%ld\">"
                                          "%s</S:added-path>" DEBUG_CR,
                                          apr_xml_quote_string
                                          (pool, 
                                           log_item->copyfrom_path,
                                           1), /* escape quotes */
                                          log_item->copyfrom_rev,
                                          apr_xml_quote_string(pool,
                                                               path, 0)));
              else
                SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                                          "<S:added-path>%s</S:added-path>" 
                                          DEBUG_CR, 
                                          apr_xml_quote_string(pool, path,
                                                               0)));
              break;

            case 'R':
              if (log_item->copyfrom_path 
                  && SVN_IS_VALID_REVNUM(log_item->copyfrom_rev))
                SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                                          "<S:replaced-path"
                                          " copyfrom-path=\"%s\"" 
                                          " copyfrom-rev=\"%ld\">"
                                          "%s</S:replaced-path>" DEBUG_CR,
                                          apr_xml_quote_string
                                          (pool, 
                                           log_item->copyfrom_path,
                                           1), /* escape quotes */
                                          log_item->copyfrom_rev,
                                          apr_xml_quote_string(pool,
                                                               path, 0)));
              else
                SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                                          "<S:replaced-path>%s"
                                          "</S:replaced-path>" DEBUG_CR,
                                          apr_xml_quote_string(pool, path,
                                                               0)));
              break;

            case 'D':
              SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                                        "<S:deleted-path>%s</S:deleted-path>" 
                                        DEBUG_CR,
                                        apr_xml_quote_string(pool, path, 0)));
              break;

            case 'M':
              SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output,
                                        "<S:modified-path>%s"
                                        "</S:modified-path>" DEBUG_CR,
                                        apr_xml_quote_string(pool, path, 0)));
              break;
              
            default:
              break;
            }
        }
    }

  SVN_ERR(dav_svn__send_xml(lrb->bb, lrb->output, "</S:log-item>" DEBUG_CR));

  return SVN_NO_ERROR;
}




dav_error * dav_svn__log_report(const dav_resource *resource,
                                const apr_xml_doc *doc,
                                ap_filter_t *output)
{
  svn_error_t *serr;
  apr_status_t apr_err;
  dav_error *derr = NULL;
  apr_xml_elem *child;
  struct log_receiver_baton lrb;
  dav_svn_authz_read_baton arb;
  const dav_svn_repos *repos = resource->info->repos;
  const char *action;
  const char *target = NULL;
  int limit = 0;
  int ns;

  /* These get determined from the request document. */
  svn_revnum_t start = SVN_INVALID_REVNUM;   /* defaults to HEAD */
  svn_revnum_t end = SVN_INVALID_REVNUM;     /* defaults to HEAD */
  svn_boolean_t discover_changed_paths = 0;  /* off by default */
  svn_boolean_t strict_node_history = 0;     /* off by default */
  apr_array_header_t *paths
    = apr_array_make(resource->pool, 0, sizeof(const char *));

  /* Sanity check. */
  ns = dav_svn_find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                    "The request does not contain the 'svn:' "
                                    "namespace, so it is not going to have "
                                    "certain required elements.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }
  
  /* ### todo: okay, now go fill in svn_ra_dav__get_log() based on the
     syntax implied below... */
  for (child = doc->root->first_child; child != NULL; child = child->next)
    {
      /* if this element isn't one of ours, then skip it */
      if (child->ns != ns)
        continue;

      if (strcmp(child->name, "start-revision") == 0)
        start = SVN_STR_TO_REV(dav_xml_get_cdata(child, resource->pool, 1));
      else if (strcmp(child->name, "end-revision") == 0)
        end = SVN_STR_TO_REV(dav_xml_get_cdata(child, resource->pool, 1));
      else if (strcmp(child->name, "limit") == 0)
        limit = atoi(dav_xml_get_cdata(child, resource->pool, 1));
      else if (strcmp(child->name, "discover-changed-paths") == 0)
        discover_changed_paths = 1; /* presence indicates positivity */
      else if (strcmp(child->name, "strict-node-history") == 0)
        strict_node_history = 1; /* presence indicates positivity */
      else if (strcmp(child->name, "path") == 0)
        {
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

  /* Build log receiver baton */
  lrb.bb = apr_brigade_create(resource->pool,  /* not the subpool! */
                              output->c->bucket_alloc);
  lrb.output = output;
  lrb.needs_header = TRUE;

  /* Our svn_log_message_receiver_t sends the <S:log-report> header in
     a lazy fashion.  Before writing the first log message, it assures
     that the header has already been sent (checking the needs_header
     flag in our log_receiver_baton structure). */

  /* Send zero or more log items. */
  serr = svn_repos_get_logs3(repos->repos,
                             paths,
                             start,
                             end,
                             limit,
                             discover_changed_paths,
                             strict_node_history,
                             dav_svn_authz_read_func(&arb),
                             &arb,
                             log_receiver,
                             &lrb,
                             resource->pool);
  if (serr)
    {
      derr = dav_svn_convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                 resource->pool);
      goto cleanup;
    }
  
  if ((serr = maybe_send_header(&lrb)))
    {
      derr = dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Error beginning REPORT response.",
                                 resource->pool);
      goto cleanup;
    }
    
  if ((serr = dav_svn__send_xml(lrb.bb, lrb.output, "</S:log-report>"
                                DEBUG_CR)))
    {
      derr = dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Error ending REPORT response.",
                                 resource->pool);
      goto cleanup;
    }

 cleanup:

  /* We've detected a 'high level' svn action to log. */
  if (paths->nelts == 0)
    action = "log";
  else if (paths->nelts == 1)
    action = apr_psprintf(resource->pool, "log-all '%s'",
                          svn_path_uri_encode(APR_ARRAY_IDX
                                              (paths, 0, const char *),
                                              resource->pool));
  else
    action = apr_psprintf(resource->pool, "log-partial '%s'",
                          svn_path_uri_encode(APR_ARRAY_IDX
                                              (paths, 0, const char *),
                                              resource->pool));

  apr_table_set(resource->info->r->subprocess_env, "SVN-ACTION", action);


  /* Flush the contents of the brigade (returning an error only if we
     don't already have one). */
  if (((apr_err = ap_fflush(output, lrb.bb))) && (! derr))
    derr = dav_svn_convert_err(svn_error_create(apr_err, 0, NULL),
                               HTTP_INTERNAL_SERVER_ERROR,
                               "Error flushing brigade.",
                               resource->pool);
  return derr;
}
