/*
 * win32_crashrpt.h : shares the win32 crashhandler functions in libsvn_subr.
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

#ifndef SVN_LIBSVN_SUBR_WIN32_CRASHRPT_H
#define SVN_LIBSVN_SUBR_WIN32_CRASHRPT_H

#ifdef WIN32
#ifdef SVN_USE_WIN32_CRASHHANDLER

LONG WINAPI svn__unhandled_exception_filter(PEXCEPTION_POINTERS ptrs);

#endif /* SVN_USE_WIN32_CRASHHANDLER */
#endif /* WIN32 */

#endif /* SVN_LIBSVN_SUBR_WIN32_CRASHRPT_H */
