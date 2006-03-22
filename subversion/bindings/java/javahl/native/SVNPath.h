/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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
 * @file SVNPath.h
 * @brief Interface of the C++ class SVNPath.
 */

#if !defined(AFX_SVNPATH_H__9AD95B26_47BF_4430_8217_20B87ACCE87B__INCLUDED_)
#define AFX_SVNPATH_H__9AD95B26_47BF_4430_8217_20B87ACCE87B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>

/**
 * @since 1.4.0
 */
class SVNPath
{
public:
    /**
     * Returns whether @a path is non-NULL and passes the @c
     * svn_path_check_valid() test.
     */
    static jboolean isValid(const char *path);

protected:
    SVNPath();
    virtual ~SVNPath();
};
// !defined(AFX_SVNPATH_H__9AD95B26_47BF_4430_8217_20B87ACCE87B__INCLUDED_)
#endif
