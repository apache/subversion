/*
 * log.c: handle the log-report request and response
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_fs.h"
#include "svn_xml.h"
#include "svn_path.h"

#include "dav_svn.h"


struct log_receiver_baton
{
  /* True on the first call to receiver. */
  svn_boolean_t first_call;

  /* Where to store the output for sending. */
  /* ### todo: Make this a file instead of an apr_text_header, because
     it can grow quite large.  NO, WAIT, we won't even do that --
     instead, we'll send back the response streamily, as Greg has
     stated he intends to do with update reports as well, now that
     Apache can handle it. */
  apr_text_header *output;

  /* For temporary allocations. */
  apr_pool_t *pool;
};


static void send_xml(struct log_receiver_baton *lrb, const char *fmt, ...)
{
  va_list ap;
  const char *s;

  va_start(ap, fmt);
  s = apr_pvsprintf(lrb->pool, fmt, ap);
  va_end(ap);

  apr_text_append(lrb->pool, lrb->output, s);
}


/* This implements `svn_log_message_receiver_t'. */
static svn_error_t * log_receiver(void *baton,
                                  const apr_hash_t *changed_paths,
                                  svn_revnum_t rev,
                                  const char *author,
                                  const char *date,
                                  const char *msg,
                                  svn_boolean_t last_call)
{
  struct log_receiver_baton *lrb = baton;
  svn_stringbuf_t *escaped_author = NULL;
  svn_stringbuf_t *escaped_date = NULL;
  svn_stringbuf_t *escaped_msg = NULL;

  /* ### todo: we ignore changed_paths for now; libsvn_repos isn't
     yet calculating them anyway. */

  if (lrb->first_call)
    {
      send_xml(lrb,
               "<S:log-report xmlns:S=\"" SVN_XML_NAMESPACE "\" "
               "xmlns:D=\"DAV:\">" DEBUG_CR);
      lrb->first_call = 0;
    }

  svn_xml_escape_nts(&escaped_author, author, lrb->pool);
  svn_xml_escape_nts(&escaped_date, date, lrb->pool);
  svn_xml_escape_nts(&escaped_msg, msg, lrb->pool);

  send_xml(lrb,
           "<S:log-item>" DEBUG_CR
           "<D:version-name>%lu</D:version-name>" DEBUG_CR
           "<D:creator-displayname>%s</D:creator-displayname>" DEBUG_CR
           /* ### this should be DAV:creation-date, but we need to format
              ### that date a bit differently */
           "<S:date>%s</S:date>" DEBUG_CR
           "<D:comment>%s</D:comment>" DEBUG_CR
           "</S:log-item>" DEBUG_CR,
           rev, escaped_author->data, escaped_date->data, escaped_msg->data);

  if (last_call)
    send_xml(lrb, "</S:log-report>" DEBUG_CR);

  return SVN_NO_ERROR;
}




dav_error * dav_svn__log_report(const dav_resource *resource,
                                const apr_xml_doc *doc,
                                apr_text_header *report)
{
  svn_error_t *serr;
  apr_xml_elem *child;
  struct log_receiver_baton lrb;
  const dav_svn_repos *repos = resource->info->repos;
  svn_stringbuf_t *target = NULL; 
  int ns;

  /* These get determined from the request document. */
  svn_revnum_t start = SVN_INVALID_REVNUM;   /* defaults to HEAD */
  svn_revnum_t end = SVN_INVALID_REVNUM;     /* defaults to HEAD */
  svn_boolean_t discover_changed_paths = 0;  /* off by default */

  /* ### why are these paths stringbuf? they aren't going to be changed... */
  apr_array_header_t *paths
    = apr_array_make(resource->pool, 0, sizeof(svn_stringbuf_t *));

  /* Sanity check. */
  ns = dav_svn_find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
                           "The request does not contain the 'svn:' "
                           "namespace, so it is not going to have certain "
                           "required elements.");
    }
  
  /* ### todo: okay, now go fill in svn_ra_dav__get_log() based on the
     syntax implied below... */
  for (child = doc->root->first_child; child != NULL; child = child->next)
    {
      /* if this element isn't one of ours, then skip it */
      if (child->ns != ns)
        continue;

      if (strcmp(child->name, "start-revision") == 0)
        {
          /* ### assume no white space, no child elems, etc */
          start = atol(child->first_cdata.first->text);
        }
      else if (strcmp(child->name, "end-revision") == 0)
        {
          /* ### assume no white space, no child elems, etc */
          end = atol(child->first_cdata.first->text);
        }
      else if (strcmp(child->name, "discover-changed-paths") == 0)
        {
          /* ### todo: value doesn't matter, presence alone is enough?
             (I.e., is that a traditional way to do things here?) */
          discover_changed_paths = 1;
        }
      else if (strcmp(child->name, "path") == 0)
        {
          /* ### Presumably, we can see the "path" element multiple
             times, adding the corresponding value to `paths' each
             time? */

          target = svn_stringbuf_create (child->first_cdata.first->text,
                                         resource->pool);
          (*((svn_stringbuf_t **)(apr_array_push (paths)))) = target;
        }
      /* else unknown element; skip it */
    }

  lrb.first_call = 1;
  lrb.output = report;
  lrb.pool = resource->pool;

  serr = svn_repos_get_logs(repos->fs,
                            paths,
                            start,
                            end,
                            discover_changed_paths,
                            log_receiver,
                            &lrb,
                            resource->pool);

  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "The log receiver or its caller encountered "
                               "an error.");
  
  
  return NULL;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
