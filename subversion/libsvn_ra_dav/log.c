/*
 * log.c :  routines for requesting and parsing log reports
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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



#define APR_WANT_STRFUNC
#include <apr_want.h> /* for strcmp() */

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_strings.h>
#include <apr_portable.h>

#include <ne_basic.h>
#include <ne_utils.h>
#include <ne_basic.h>
#include <ne_207.h>
#include <ne_props.h>
#include <ne_xml.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_io.h"
#include "svn_ra.h"
#include "svn_path.h"
#include "svn_xml.h"

#include "ra_dav.h"



/*** Code ***/

/* Userdata for the Neon XML element callbacks. */
struct log_baton
{
  /* Allocate log message information.
   * NOTE: this pool may be cleared multiple times as log messages are
   * received.
   */
  apr_pool_t *subpool;

  /* Information about each log item in turn. */
  svn_revnum_t revision;
  const char *author;
  const char *date;
  const char *msg;

  /* Keys are the paths changed in this commit, allocated in SUBPOOL;
     the table itself is also allocated in SUBPOOL.  If this table is
     NULL, no changed paths were indicated -- which doesn't mean no
     paths were changed, just means that this log invocation didn't
     ask for them to be reported. */
  apr_hash_t *changed_paths;

  /* Client's callback, invoked on the above fields when the end of an
     item is seen. */
  svn_log_message_receiver_t receiver;
  void *receiver_baton;

  /* If `receiver' returns error, it is stored here. */
  svn_error_t *err;
};


/* Prepare LB to start accumulating the next log item, by wiping all
 * information related to the previous item and clearing the pool in
 * which they were allocated.  Do not touch any stored error, however.
 */
static void
reset_log_item (struct log_baton *lb)
{
  lb->revision      = SVN_INVALID_REVNUM;
  lb->author        = NULL;
  lb->date          = NULL;
  lb->msg           = NULL;
  lb->changed_paths = NULL;

  svn_pool_clear (lb->subpool);
}


/*
 * This implements the `ne_xml_validate_cb' prototype.
 */
static int
log_validate(void *userdata, ne_xml_elmid parent, ne_xml_elmid child)
{
  /* ### todo */
  return NE_XML_VALID;
}


/*
 * This implements the `ne_xml_startelm_cb' prototype.
 */
static int
log_start_element(void *userdata,
                  const struct ne_xml_elm *elm,
                  const char **atts)
{
  /* ### todo */
  return NE_XML_VALID;
}


/*
 * This implements the `ne_xml_endelm_cb' prototype.
 */
static int
log_end_element(void *userdata,
                const struct ne_xml_elm *elm,
                const char *cdata)
{
  struct log_baton *lb = userdata;

  switch (elm->id)
    {
    case ELEM_version_name:
      lb->revision = SVN_STR_TO_REV (cdata);
      break;
    case ELEM_creator_displayname:
      lb->author = apr_pstrdup (lb->subpool, cdata);
      break;
    case ELEM_log_date:
      lb->date = apr_pstrdup (lb->subpool, cdata);
      break;
    case ELEM_added_path:
    case ELEM_deleted_path:
    case ELEM_changed_path:
      {
        char *path = apr_pstrdup (lb->subpool, cdata);
        char action;

        /* See documentation for `svn_repos_node_t' in svn_repos.h,
           and `svn_log_message_receiver_t' in svn_types.h, for more
           about these action codes. */
        if (elm->id == ELEM_added_path)
          action = 'A';
        else if (elm->id == ELEM_deleted_path)
          action = 'D';
        else
          action = 'U';
        
        if (lb->changed_paths == NULL)
          lb->changed_paths = apr_hash_make(lb->subpool);
        
        apr_hash_set(lb->changed_paths, path, APR_HASH_KEY_STRING,
                     (void *) ((int) action));
      }
      break;
    case ELEM_comment:
      lb->msg = apr_pstrdup (lb->subpool, cdata);
      break;
    case ELEM_log_item:
      {
        /* ### Naive call for now.  We still need to arrange things so
           that last_call gets passed properly, which will
           be... interesting.  Well, not so bad, just need to put an
           attribute on the end element of the last item.  This is a
           change to mod_dav_svn too. */
        
        svn_error_t *err = (*(lb->receiver))(lb->receiver_baton,
                                             lb->changed_paths,
                                             lb->revision,
                                             lb->author,
                                             lb->date,
                                             lb->msg);
        
        reset_log_item (lb);
        
        if (err)
          {
            lb->err = err;         /* ### Wrap an existing error, if any? */
            return NE_XML_INVALID; /* ### Any other way to express an err? */
          }
      }
      break;
    case ELEM_log_report:
      {
        /* ### todo: what to do here?  We're (hopefully) going to handle
           the whole last_call thing another way, so maybe the end of
           the report means nothing... */

        /* Yo --  we're going to take Greg's suggestion of treating
           log_receivers the way we treat delta windows -- last call
           is indicated by a special call with NULL (or, in this case,
           SVN_INVALID_REVNUM), instead of combining the last_call
           indicator with the previous, contentful call. */
      }
      break;
    }

  return NE_XML_VALID;
}


svn_error_t * svn_ra_dav__get_log(void *session_baton,
                                  const apr_array_header_t *paths,
                                  svn_revnum_t start,
                                  svn_revnum_t end,
                                  svn_boolean_t discover_changed_paths,
                                  svn_log_message_receiver_t receiver,
                                  void *receiver_baton)
{
  /* The Plan: Send a request to the server for a log report.
   * Somewhere in mod_dav_svn, there will be an implementation, R, of
   * the `svn_log_message_receiver_t' function type.  Some other
   * function in mod_dav_svn will use svn_repos_get_logs() to loop R
   * over the log messages, and the successive invocations of R will
   * collectively transmit the report back here, where we parse the
   * report and invoke RECEIVER (which is an entirely separate
   * instance of `svn_log_message_receiver_t') on each individual
   * message in that report.
   */

  int i;
  svn_ra_session_t *ras = session_baton;
  svn_stringbuf_t *request_body = svn_stringbuf_create("", ras->pool);
  struct log_baton lb;

  /* ### todo: I don't understand why the static, file-global
     variables shared by update and status are called `report_head'
     and `report_tail', instead of `request_head' and `request_tail'.
     Maybe Greg can explain?  Meanwhile, I'm tentatively using
     "request_*" for my local vars below. */

  static const char log_request_head[]
    = "<S:log-report xmlns:S=\"" SVN_XML_NAMESPACE "\">" DEBUG_CR;

  static const char log_request_tail[] = "</S:log-report>" DEBUG_CR;
  
  static const struct ne_xml_elm log_report_elements[] =
    {
      { SVN_XML_NAMESPACE, "log-report", ELEM_log_report, 0 },
      { SVN_XML_NAMESPACE, "log-item", ELEM_log_item, 0 },
      { SVN_XML_NAMESPACE, "date", ELEM_log_date, NE_XML_CDATA },
      { SVN_XML_NAMESPACE, "added-path", ELEM_added_path, NE_XML_CDATA },
      { SVN_XML_NAMESPACE, "deleted-path", ELEM_deleted_path, NE_XML_CDATA },
      { SVN_XML_NAMESPACE, "changed-path", ELEM_changed_path, NE_XML_CDATA },
      { "DAV:", "version-name", ELEM_version_name, NE_XML_CDATA },
      { "DAV:", "creator-displayname", ELEM_creator_displayname,
        NE_XML_CDATA },
      { "DAV:", "comment", ELEM_comment, NE_XML_CDATA },
      { NULL }
    };
  

  /* Construct the request body. */
  svn_stringbuf_appendcstr(request_body, log_request_head);
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(ras->pool,
                                        "<S:start-revision>%" SVN_REVNUM_T_FMT
                                        "</S:start-revision>", start));
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(ras->pool,
                                        "<S:end-revision>%" SVN_REVNUM_T_FMT
                                        "</S:end-revision>", end));
  if (discover_changed_paths)
    {
      svn_stringbuf_appendcstr(request_body,
                               apr_psprintf(ras->pool,
                                            "<S:discover-changed-paths/>"));
    }
    
  for (i = 0; i < paths->nelts; i++)
    {
      const char *this_path = (((svn_stringbuf_t **)paths->elts)[i])->data;
      /* ### todo: want to xml-escape the path, but can't use
         apr_xml_quote_string() here because we don't use apr_util
         yet.  Should use svn_xml_escape_blah() instead? */
      svn_stringbuf_appendcstr(request_body,
                               apr_psprintf(ras->pool,
                                            "<S:path>%s</S:path>", this_path));
    }

  svn_stringbuf_appendcstr(request_body, log_request_tail);

  lb.receiver = receiver;
  lb.receiver_baton = receiver_baton;
  lb.subpool = svn_pool_create (ras->pool);
  reset_log_item (&lb);

  SVN_ERR( svn_ra_dav__parsed_request(ras,
                                      "REPORT",
                                      ras->root.path,
                                      request_body->data,
                                      0,  /* ignored */
                                      log_report_elements, 
                                      log_validate,
                                      log_start_element,
                                      log_end_element,
                                      &lb,
                                      ras->pool) );
  
  svn_pool_destroy (lb.subpool);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
