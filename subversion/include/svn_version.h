/* svn_version.h:  Version information.
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

#ifndef SVN_VERSION_H
#define SVN_VERSION_H

#ifndef APR_STRINGIFY
#include <apr_version.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Symbols that define the version number.
   XXX FIXME: Modify before each snapshot, milestone or release. */

/* Version numbers: <major>.<minor>.<micro> */

/* Major version number.
   Modify when incompatible changes are made to published interfaces. */
#define SVN_VER_MAJOR      0

/* Minor version number.
   Modify when new functionality is added or new interfaces are
   defined, but all changes are backward compatible. */
#define SVN_VER_MINOR      10

/* Patch number.
   Modify for every released patch. */
#define SVN_VER_MICRO      1


/* Version tag: a string describing the version.
   This tag remains "dev build" in the repository so that we can always
   see from "svn --version" that the software has been built from the
   repository rather than a "blessed" distribution.

   During the distribution process, we automatically replace this text
   with something like "r1504".  */
#define SVN_VER_TAG        "dev build"



/* Version strings composed from the above definitions. */


/* Version number */
#define SVN_VER_NUMBER   APR_STRINGIFY(SVN_VER_MAJOR) \
                         "." APR_STRINGIFY(SVN_VER_MINOR) \
                         "." APR_STRINGIFY(SVN_VER_MICRO)

/* Complete version string */
#define SVN_VERSION      SVN_VER_NUMBER " (" SVN_VER_TAG ")"


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_VERSION_H */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
