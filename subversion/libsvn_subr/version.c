/*
 * version.c:  library version number and utilities
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



#include "svn_version.h"

SVN_VER_GEN_IMPL(subr)


/* These functions check for version compatibility, as per our
   compatilbility guarantees, but require an exact match when linking
   to a non-release library version. A development client is always
   compatible with a previous released library. */


svn_boolean_t svn_ver_compatible (const svn_version_t *versioninfo,
                                  int major, int minor, int micro,
                                  const char *tag)
{
  if (versioninfo->tag[0] != '\0')
    /* Development library; require exact match. */
    return (major == versioninfo->major
            && minor == versioninfo->minor
            && micro == versioninfo->micro
            && 0 == strcmp (tag, versioninfo->tag));
  else if (tag[0] != '\0')
    /* Development client; must be newer than the library. */
    return (major == versioninfo->major
            && (minor > versioninfo->minor
                || (minor == versioninfo->minor
                    && micro > versioninfo->micro)));
  else
    /* General compatibility rules for released versions. */
    return (major == versioninfo->major
            && minor >= versioninfo->minor);
}


/* Callback compatibility rules are as call rules, but inverted. */

svn_boolean_t svn_ver_callback_compatible (const svn_version_t *versioninfo,
                                           int major, int minor, int micro,
                                           const char *tag)
{
  svn_version_t clientinfo;
  clientinfo.major = major;
  clientinfo.minor = minor;
  clientinfo.micro = micro;
  clientinfo.tag = tag;
  return svn_ver_compatible (&clientinfo,
                             versioninfo->major,
                             versioninfo->minor,
                             versioninfo->micro,
                             versioninfo->tag);
}
