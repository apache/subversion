/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 * @file BlameCall.h
 * @brief Interface of the class BlameCallback
 */

#if !defined(AFX_BLAMECALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_)
#define AFX_BLAMECALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_client.h"

/**
 * this class holds a java callback object, which will receive every line of 
 * the file for which the callback information is requested.
 */
class BlameCallback
{
public:
    BlameCallback(jobject jcallback);
    ~BlameCallback();
    svn_error_t *callback(svn_revnum_t revision, const char *author,
                      const char *date, const char *line, apr_pool_t *pool);
private:
    /**
     * this a local reference to the java object.
     */
    jobject m_callback;
};
// !defined(AFX_BLAMECALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_)
#endif
