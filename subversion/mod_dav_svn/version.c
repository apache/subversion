/*
 * version.c: mod_dav_svn versioning provider functions for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <httpd.h>
#include <mod_dav.h>
#include <apr_tables.h>

#include "dav_svn.h"


static void dav_svn_get_vsn_options(apr_pool_t *p, ap_text_header *phdr)
{
  /* Note: we append pieces with care for Web Folders's 63-char limit
     on the DAV: header */

  ap_text_append(p, phdr,
                 "version-control,checkout,version-history,working-resource");
  ap_text_append(p, phdr,
                 "merge,baseline,activity,version-controlled-collection");

  /* ### fork-control? */
}

static dav_error *dav_svn_get_option(const dav_resource *resource,
                                     const ap_xml_elem *elem,
                                     ap_text_header *option)
{
  /* ### DAV:version-history-collection-set */

  if (elem->ns == AP_XML_NS_DAV_ID)
    {
      if (strcmp(elem->name, "activity-collection-set") == 0)
        {
          ap_text_append(resource->pool, option,
                         "<D:activity-collection-set>");
          ap_text_append(resource->pool, option,
                         dav_svn_build_uri(resource,
                                           DAV_SVN_BUILD_URI_ACT_COLLECTION,
                                           SVN_INVALID_REVNUM, NULL,
                                           1 /* add_href */, resource->pool));
          ap_text_append(resource->pool, option,
                         "</D:activity-collection-set>");
        }
    }

  return NULL;
}

static dav_error *dav_svn_vsn_control(dav_resource *resource,
                                      const char *target)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "VERSION-CONTROL is not yet implemented.");
}

static dav_error *dav_svn_checkout(dav_resource *resource,
                                   int is_unreserved, int is_fork_ok,
                                   int create_activity,
                                   apr_array_header_t *activities,
                                   dav_resource **working_resource)
{
  const char *activity_id;
  const char *txn_name;

  if (resource->type != DAV_RESOURCE_TYPE_VERSION)
    {
      return dav_new_error(resource->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                           "CHECKOUT can only be performed on a version "
                           "resource [at this time].");
    }
  if (create_activity)
    {
      return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                           "CHECKOUT can not create an activity at this "
                           "time. Use MKACTIVITY first.");
    }
  if (is_unreserved)
    {
      return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                           "Unreserved checkouts are not yet available. "
                           "A version history may not be checked out more "
                           "than once, into a specific activity.");
    }
  if (activities == NULL)
    {
      return dav_new_error(resource->pool, HTTP_CONFLICT, 0,
                           "An activity must be provided for the checkout.");
    }
  /* assert: nelts > 0.  the below check effectively means > 1. */
  if (activities->nelts != 1)
    {
      return dav_new_error(resource->pool, HTTP_CONFLICT, 0,
                           "Only one activity may be specified within the "
                           "CHECKOUT.");
    }

  activity_id = ((const char * const *)activities->elts)[0];
  if ((txn_name = dav_svn_get_txn(resource->info->repos, activity_id)) == NULL)
    {
      return dav_new_error(resource->pool, HTTP_CONFLICT, 0,
                           "The specified activity does not exist.");
    }

  *working_resource = dav_svn_create_working_resource(resource,
                                                      activity_id,
                                                      txn_name,
                                                      resource->info->repos_path);
  return NULL;
}

static dav_error *dav_svn_uncheckout(dav_resource *resource)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "UNCHECKOUT is not yet implemented.");
}

static dav_error *dav_svn_checkin(dav_resource *resource,
                                  dav_resource **version_resource)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "CHECKIN is not yet implemented.");
}

static int dav_svn_versionable(const dav_resource *resource)
{
  return 0;
}

static int dav_svn_auto_version_enabled(const dav_resource *resource)
{
  return 0;
}

static dav_error *dav_svn_avail_reports(const dav_resource *resource,
                                        const dav_report_elem **reports)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "REPORT is not yet implemented.");
}

static int dav_svn_report_target_selector_allowed(const ap_xml_doc *doc)
{
  return 0;
}

static dav_error *dav_svn_get_report(request_rec *r,
                                     const dav_resource *resource,
                                     const ap_xml_doc *doc,
                                     ap_text_header *report)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "REPORT is not yet implemented.");
}

static int dav_svn_can_be_activity(const dav_resource *resource)
{
  return resource->type == DAV_RESOURCE_TYPE_ACTIVITY && !resource->exists;
}

static dav_error *dav_svn_make_activity(dav_resource *resource)
{
  const char *activity_id = resource->info->root.activity_id;
  const char *txn_name;
  dav_error *err;

  /* ### need to check some preconditions? */

  err = dav_svn_create_activity(resource->info->repos, &txn_name,
                                resource->pool);
  if (err != NULL)
    return err;

  err = dav_svn_store_activity(resource->info->repos, activity_id, txn_name);
  if (err != NULL)
    return err;

  /* everything is happy. update the resource */
  resource->info->root.txn_name = txn_name;
  resource->exists = 1;
  return NULL;
}


const dav_hooks_vsn dav_svn_hooks_vsn = {
  dav_svn_get_vsn_options,
  dav_svn_get_option,
  dav_svn_vsn_control,
  dav_svn_checkout,
  dav_svn_uncheckout,
  dav_svn_checkin,
  dav_svn_versionable,
  dav_svn_auto_version_enabled,
  dav_svn_avail_reports,
  dav_svn_report_target_selector_allowed,
  dav_svn_get_report,
  NULL,                 /* update */
  NULL,                 /* add_label */
  NULL,                 /* remove_label */
  NULL,                 /* can_be_workspace */
  NULL,                 /* make_workspace */
  dav_svn_can_be_activity,
  dav_svn_make_activity,
};


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
