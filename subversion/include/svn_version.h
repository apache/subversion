/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_version.h
 * @brief Version information.
 */

#ifndef SVN_VERSION_H
#define SVN_VERSION_H

/* Hack to prevent the resource compiler from including
   apr_general.h.  It doesn't resolve the include paths
   correctly and blows up without this.
 */
#ifndef APR_STRINGIFY
#include <apr_general.h>
#endif

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Symbols that define the version number. */

/* Version numbers: <major>.<minor>.<micro>
 *
 * The version numbers in this file follow the rules established by:
 *
 *   http://apr.apache.org/versioning.html
 */

/** Major version number.
 *
 * Modify when incompatible changes are made to published interfaces.
 */
#define SVN_VER_MAJOR      1

/** Minor version number.
 *
 * Modify when new functionality is added or new interfaces are
 * defined, but all changes are backward compatible.
 */
#define SVN_VER_MINOR      1

/** Patch number.
 *
 * Modify for every released patch.
 */
#define SVN_VER_MICRO      0

/** Library version number.
 *
 * Modify whenever there's an incompatible change in the library ABI.
 * ### this is semantically equivalent to SVN_VER_MAJOR. fix...
 */
#define SVN_VER_LIBRARY    1


/** Version tag: a string describing the version.
 *
 * This tag remains " (dev build)" in the repository so that we can
 * always see from "svn --version" that the software has been built
 * from the repository rather than a "blessed" distribution.
 *
 * When rolling a tarball, we automatically replace this text with " (r1234)"
 * (where 1234 is the last revision on the branch prior to the release) 
 * for final releases; in prereleases, it becomes " (Alpha)",
 * " (Beta 1)", etc., as appropriate.
 *
 * Always change this at the same time as SVN_VER_NUMTAG.
 */
#define SVN_VER_TAG        " (dev build)"


/** Number tag: a string describing the version.
 *
 * This tag is used to generate a version number string to identify
 * the client and server in HTTP requests, for example. It must not
 * contain any spaces. This value remains "-dev" in the repository.
 *
 * When rolling a tarball, we automatically replace this text with ""
 * for final releases; in prereleases, it becomes "-alpha", "-beta1",
 * etc., as appropriate.
 *
 * Always change this at the same time as SVN_VER_TAG.
 */
#define SVN_VER_NUMTAG     "-dev"


/** Revision number: The repository revision number of this release.
 *
 * This constant is used to generate the build number part of the Windows
 * file version. Its value remains 0 in the repository.
 *
 * When rolling a tarball, we automatically replace it with what we
 * guess to be the correct revision number.
 */
#define SVN_VER_REVISION   0


/* Version strings composed from the above definitions. */

/** Version number */
#define SVN_VER_NUM        APR_STRINGIFY(SVN_VER_MAJOR) \
                           "." APR_STRINGIFY(SVN_VER_MINOR) \
                           "." APR_STRINGIFY(SVN_VER_MICRO)

/** Version number with tag (contains no whitespace) */
#define SVN_VER_NUMBER     SVN_VER_NUM SVN_VER_NUMTAG

/** Complete version string */
#define SVN_VERSION        SVN_VER_NUM SVN_VER_TAG



/* Querying the version number */

/**
 * @since New in 1.1.
 *
 * Version information. */
typedef struct svn_version_t
{
  int major;
  int minor;
  int micro;

  /**
   * The verison tag (@t SVN_VER_NUMTAG). Must always point to a
   * statically allocated string, not something from a pool.
   */
  const char *tag;
} svn_version_t;


/**
 * @since New in 1.1.
 *
 * Generate the prototype for a version-query function. Returns a
 * pointer to a statically allocated @t svn_version_t structure.
 */
#define SVN_VER_GEN_PROTO(name) \
const svn_version_t *svn_##name##_version (void)

/** 
 * @since New in 1.1.
 *
 * Generate the implementation of a version-query function. */
#define SVN_VER_GEN_IMPL(name) \
const svn_version_t *svn_##name##_version (void) \
{ \
  static const svn_version_t versioninfo = \
    { \
      SVN_VER_MAJOR, \
      SVN_VER_MINOR, \
      SVN_VER_MICRO, \
      SVN_VER_NUMTAG \
    }; \
  return &versioninfo; \
}

/**
 * @since New in 1.1.
 *
 * Check version compatibility for calls to the library. Returns @t
 * TRUE if the version info in @a versioninfo is compatible with the
 * one in @a major, @a minor, @a micro and @tag.
 */
svn_boolean_t svn_ver_compatible (const svn_version_t *versioninfo,
                                  int major, int minor, int micro,
                                  const char *tag);

/**
 * @since New in 1.1.
 *
 * Check version compatibility for callbacks from the library. Returns
 * @t TRUE if the version info in @a versioninfo is compatible with
 * the one in @a major, @a minor, @a micro and @tag.
 */
svn_boolean_t svn_ver_callback_compatible (const svn_version_t *versioninfo,
                                           int major, int minor, int micro,
                                           const char *tag);

/** 
 * @since New in 1.1.
 *
 * Shorthand for calling @t svn_ver_compatible. */
#define SVN_VER_COMPATIBLE(name) \
  svn_ver_compatible(svn_##name##_version(), \
                     SVN_VER_MAJOR, SVN_VER_MINOR, SVN_VER_MICRO, \
                     SVN_VER_NUMTAG)

/** 
 * @since New in 1.1.
 *
 * Shorthand for calling @t svn_ver_callback_compatible. */
#define SVN_VER_CALLBACK_COMPATIBLE(name) \
  svn_ver_callback_compatible(svn_##name##_version(), \
                              SVN_VER_MAJOR, SVN_VER_MINOR, SVN_VER_MICRO, \
                              SVN_VER_NUMTAG)


/**
 * @since New in 1.1.
 * (A prototype is being generated here, and the prototype is new in 1.1.)
 *
 * libsvn_subr doesn't have an svn_subr header, so put the prototype here. */
SVN_VER_GEN_PROTO(subr);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_VERSION_H */
