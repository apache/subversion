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

#include "dav_svn.h"


static void dav_svn_get_vsn_options(apr_pool_t *p, ap_text_header *phdr)
{
}

static dav_error *dav_svn_get_option(const dav_resource *resource,
                                     const ap_xml_elem *elem,
                                     ap_text_header *option)
{
  return NULL;
}

static dav_error *dav_svn_vsn_control(dav_resource *resource,
                                      const char *target)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "VERSION-CONTROL is not yet implemented.");
}

static dav_error *dav_svn_checkout(dav_resource *resource,
                                   dav_resource **working_resource)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "CHECKOUT is not yet implemented.");
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

static dav_error *dav_svn_set_target(const dav_resource *resource,
                                     const char *target,
                                     int is_label)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "SET-TARGET is not yet implemented.");
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

static dav_error *dav_svn_add_label(const dav_resource *resource,
                                    const char *label,
                                    int replace)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "Adding labels is not yet implemented.");
}

static dav_error *dav_svn_remove_label(const dav_resource *resource,
                                       const char *label)
{
  return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "Removing labels is not yet implemented.");
}

const dav_hooks_vsn dav_svn_hooks_vsn = {
  dav_svn_get_vsn_options,
  dav_svn_get_option,
  dav_svn_vsn_control,
  dav_svn_checkout,
  dav_svn_uncheckout,
  dav_svn_checkin,
  dav_svn_set_target,
  dav_svn_versionable,
  dav_svn_auto_version_enabled,
  dav_svn_avail_reports,
  dav_svn_report_target_selector_allowed,
  dav_svn_get_report,
  dav_svn_add_label,
  dav_svn_remove_label,
  NULL,                 /* can_be_workspace */
  NULL,                 /* make_workspace */
};


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
