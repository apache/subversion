/* svn_config.h:  Functions for accessing SVN configuration files.
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



#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_VERSION_H
#define SVN_VERSION_H


/* Symbols that define the version number.
   XXX FIXME: Modify before each snapshot, milestone or release. */

/* Version numbers: <major>.<minor>.<micro> */
#define SVN_VER_MAJOR      0    /* Major version number.
                                   Will change when incompatible changes
                                   are made to published interfaces. */
#define SVN_VER_MINOR      0    /* Minor version number.
                                   Will change when new functionality is
                                   added or new interfaces are defined, but
                                   all changes are backward compatible. */
#define SVN_VER_MICRO      0    /* Patch number.
                                   Will change with every released patch. */


/* Milestone version: "M*".
   If this is not a milestone release, this symbol should be #undef'd. */
/* #undef SVN_VER_MILESTONE */
#define SVN_VER_MILESTONE  "M3"


/* Version date: date of release or snapshot. */
#define SVN_VER_DATE       "2001-08-30"


/* Version tag: A string describing the stateus of the version.
   Typical values are "alpha", "beta", "prerelease", ...
   This symbol should be #undef'd for major releases; for development
   snapshots, it should be defined to SVN_VER_DATE.
   This symbol is not used in the milestone version strings. */
/* #undef SVN_VER_TAG */
#define SVN_VER_TAG        "N/A"



/* Version strings composed from the above definitions. */

/* Complete version string */
#ifndef SVN_VER_MILESTONE
#  ifdef SVN_VER_TAG
#    define SVN_VERSION    SVN_VERSION_NUMBER" ("SVN_VER_TAG")"
#  else
#    define SVN_VERSION    SVN_VERSION_NUMBER
#  endif
#else
#  define SVN_VERSION      SVN_VER_MILESTONE
#endif

/* Version number */
#define SVN_VERSION_NUMBER SVN_VER_STRINGIFY(SVN_VER_MAJOR) \
                           "."SVN_VER_STRINGIFY(SVN_VER_MINOR) \
                           "."SVN_VER_STRINGIFY(SVN_VER_MICRO)


/* Yet another stringification macro. */
#define GLOW_VER_STRINGIFY(X) GLOW_VER_REALLY_STRINGIFY(X)
#define GLOW_VER_REALLY_STRINGIFY(X) #X


#endif /* SVN_VERSION_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
