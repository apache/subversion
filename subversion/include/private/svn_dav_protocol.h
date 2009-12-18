/*
 * svn_dav_protocol.h: Declarations of the protocol shared by the
 * mod_dav_svn backend for httpd's mod_dav, and its ra_neon and
 * ra_serf RA DAV clients.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#ifndef SVN_DAV_PROTOCOL_H
#define SVN_DAV_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Names for the custom HTTP REPORTs understood by mod_dav_svn, sans
    namespace. */
#define SVN_DAV__MERGEINFO_REPORT "mergeinfo-report"

/** Names for XML child elements of the custom HTTP REPORTs understood
    by mod_dav_svn, sans namespace. */
#define SVN_DAV__CREATIONDATE "creationdate"
#define SVN_DAV__MERGEINFO_ITEM "mergeinfo-item"
#define SVN_DAV__MERGEINFO_PATH "mergeinfo-path"
#define SVN_DAV__MERGEINFO_INFO "mergeinfo-info"
#define SVN_DAV__PATH "path"
#define SVN_DAV__INHERIT "inherit"
#define SVN_DAV__REVISION "revision"
#define SVN_DAV__INCLUDE_DESCENDANTS "include-descendants"
#define SVN_DAV__VERSION_NAME "version-name"

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DAV_PROTOCOL_H */
