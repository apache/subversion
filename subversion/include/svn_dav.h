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


/* ### should add strings for the various XML elements in the reports
   ### and things. also the custom prop names. etc.
*/

/* The svn-specific object that is placed within a <D:error> response.  */
#define SVN_DAV_ERROR_NAMESPACE "svn:"
#define SVN_DAV_ERROR_TAG       "error"


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_DAV_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
