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



#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_VERSION_H
#define SVN_VERSION_H


/* Symbols that define the version number.
   XXX FIXME: Modify before each snapshot, milestone or release. */

/* Version numbers: <major>.<minor>.<micro> */

/* Major version number.
   Modify when incompatible changes are made to published interfaces. */
#define SVN_VER_MAJOR      0

/* Minor version number.
   Modify when new functionality is added or new interfaces are
   defined, but all changes are backward compatible. */
#define SVN_VER_MINOR      9

/* Patch number.
   Modify for every released patch. */
#define SVN_VER_MICRO      0


/* Descriptive name: If a version number isn't appropriate (e.g., for
   pre-1.0 milestone versions), use this string for the version name
   instead. Otherwise, this symbol should be #undef'd. */
#undef SVN_VER_NAME
/* #define SVN_VER_NAME       "M6" */


/* Version tag: A string describing the of the version.
   Typical values are "alpha", "beta", or "rcN", for release candidate N.
   This symbol may be #undef'd for major releases; for development
   snapshots, it should be the number of the snapshot's revision. */
#undef SVN_VER_TAG
/* #define SVN_VER_TAG        "rc2" */



/* Version strings composed from the above definitions. */


/* Yet another stringification macro. */
#define SVN_VER_STRINGIFY(X) SVN_VER_REALLY_STRINGIFY(X)
#define SVN_VER_REALLY_STRINGIFY(X) #X


/* Version number */
#ifndef SVN_VER_NAME
#  define SVN_VER_NUMBER   SVN_VER_STRINGIFY(SVN_VER_MAJOR) \
                           "." SVN_VER_STRINGIFY(SVN_VER_MINOR) \
                           "." SVN_VER_STRINGIFY(SVN_VER_MICRO)
#else
#  define SVN_VER_NUMBER   SVN_VER_NAME
#endif /* SVN_VER_NAME */


/* Complete version string */
#ifdef SVN_VER_TAG
#  define SVN_VERSION      SVN_VER_NUMBER " (" SVN_VER_TAG ")"
#else
#  define SVN_VERSION      SVN_VER_NUMBER
#endif /* SVN_VER_TAG */



#endif /* SVN_VERSION_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
