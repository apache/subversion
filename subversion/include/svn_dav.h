/*
 * svn_dav.h :  code related to WebDAV/DeltaV usage in Subversion
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




#ifndef SVN_DAV_H
#define SVN_DAV_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* This is the MIME type that Subversion uses for its "svndiff" format.

   It is an application type, for the "svn" vendor. The specific subtype
   is "svndiff". */
#define SVN_SVNDIFF_MIME_TYPE "application/vnd.svn-svndiff"


/* This header is *TEMPORARILY* used to transmit the delta base to the
   server. It contains a version resource URL for what is on the client. */
#define SVN_DAV_DELTA_BASE_HEADER "X-SVN-VR-Base"

/* This header is used when an svn client wants to trigger specific
   svn server behaviors.  Normal WebDAV or DeltaV clients won't use it. */
#define SVN_DAV_OPTIONS_HEADER "X-SVN-Options"

/* Specific options that can appear in the options-header: */
#define SVN_DAV_OPTION_NO_MERGE_RESPONSE "no-merge-response"

/* ### should add strings for the various XML elements in the reports
   ### and things. also the custom prop names. etc.
*/

/* The svn-specific object that is placed within a <D:error> response.  */
#define SVN_DAV_ERROR_NAMESPACE "svn:"
#define SVN_DAV_ERROR_TAG       "error"



/* General property (xml) namespaces that will be used by both ra_dav
   and mod_dav_svn for marshalling properties. */

/* A property stored in the fs and wc, begins with 'svn:', and is
   interpreted either by client or server.  */
#define SVN_DAV_PROP_NS_SVN "http://subversion.tigris.org/xmlns/svn/"

/* A property stored in the fs and wc, but totally ignored by svn
   client and server.  (Simply invented by the users.) */
#define SVN_DAV_PROP_NS_CUSTOM "http://subversion.tigris.org/xmlns/custom/"

/* A property purely generated and consumed by the network layer, not
   seen by either fs or wc. */
#define SVN_DAV_PROP_NS_DAV "http://subversion.tigris.org/xmlns/dav/"


/* Remove this #define to disable support for older (broken) svn_dav
   property namespaces (like "svn:" and "svn:custom:").  Once this
   #define is removed, please remove the code that it enabled in
   mod_dav_svn and libsvn_ra_dav.  Thank you.  */
#define SVN_DAV_FEATURE_USE_OLD_NAMESPACES


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_DAV_H */
