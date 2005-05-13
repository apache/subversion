/*
 * svn_private_config.hw : Template for svn_private_config.h on iSeries.
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

/* ==================================================================== */




#ifndef CONFIG_HW
#define CONFIG_HW

#if APR_CHARSET_EBCDIC
#pragma convert (1208)
#endif

/* The version of Berkeley DB we want */
#define SVN_FS_WANT_DB_MAJOR	4
#define SVN_FS_WANT_DB_MINOR	0
#define SVN_FS_WANT_DB_PATCH	14


/* Path separator for local filesystem */
#define SVN_PATH_LOCAL_SEPARATOR SVN_UTF8_FSLASH
/*  #define SVN_PATH_LOCAL_SEPARATOR '/'  */

/* Name of system's null device */
#define SVN_NULL_DEVICE_NAME "/dev/null"

/* Link fs base library into the fs library */
//#define SVN_LIBSVN_FS_LINKS_FS_BASE

/* Link fs fs library into the fs library */
#define SVN_LIBSVN_FS_LINKS_FS_FS

/* Link local repos access library to client */
#define SVN_LIBSVN_CLIENT_LINKS_RA_LOCAL

/* Link DAV repos access library to client */
//#define SVN_LIBSVN_CLIENT_LINKS_RA_DAV

/* Link pipe repos access library to client */
#define SVN_LIBSVN_CLIENT_LINKS_RA_SVN

/* Defined to be the path to the installed binaries */
#define SVN_BINARY_DIR "\x2f\x53\x75\x62\x76\x65\x72\x73\x69\x6f\x6e\x2f" \
                       "\x62\x69\x6e" /* "/Subversion/bin" */

/* Setup gettext macros */
#define N_(x) (x)
#define PACKAGE_NAME "subversion"

#ifdef ENABLE_NLS
#define SVN_LOCALE_RELATIVE_PATH "\x2e\x2e\x2f\x73\x68\x61\x72\x65\x2f\x6c" \
                                 "\x6f\x63\x61\x6c\x65" /* "../share/locale" */
#include <locale.h>
#include <libintl.h>
#define _(x) dgettext(PACKAGE_NAME, x)
#else
#define _(x) (x)
#define gettext(x) (x)
#define dgettext(domain,x) (x)
#endif

#endif /* CONFIG_HW */
