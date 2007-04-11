/**
 * @copyright
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
 * @endcopyright
 *
 * @file InfoCallback.h
 * @brief Interface of the class InfoCallback
 */

#if !defined(_INFOCALLBACK_H__INCLUDED_)
#define _INFOCALLBACK_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_client.h"

struct info_entry;

/**
 * this class holds a java callback object, which will receive every line of
 * the file for which the callback information is requested.
 */
class InfoCallback
{
public:
    InfoCallback(jobject jcallback);
    ~InfoCallback();

    static svn_error_t *callback(void *baton,
                                 const char *path,
                                 const svn_info_t *info,
                                 apr_pool_t *pool);

protected:
    svn_error_t *singleInfo(const char *path,
                            const svn_info_t *info,
                            apr_pool_t *pool);

private:
    /**
     * A local reference to the corresponding Java object.
     */
    jobject m_callback;

    jobject createJavaInfo2(const char *path,
                            const svn_info_t *info,
                            apr_pool_t *pool);
};
// !defined(_INFOCALLBACK_H__INCLUDED_)
#endif
