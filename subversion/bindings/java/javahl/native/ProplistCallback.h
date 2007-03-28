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
 * @file ProplistCallback.h
 * @brief Interface of the class ProplistCallback
 */

#if !defined(_PROPLISTCALLBACK_H__INCLUDED_)
#define _PROPLISTCALLBACK_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_client.h"

/**
 * this class holds a java callback object, which will receive every line of 
 * the file for which the callback information is requested.
 */
class ProplistCallback
{
public:
    ProplistCallback(jobject jcallback);
    ~ProplistCallback();
    svn_error_t *callback(svn_stringbuf_t *path,
                          apr_hash_t *prop_hash,
                          apr_pool_t *pool);
private:
    /**
     * this a local reference to the java object.
     */
    jobject m_callback;

    jobject makeMapFromHash(apr_hash_t *prop_hash, apr_pool_t *pool);
};
// !defined(_PROPLISTCALLBACK_H__INCLUDED_)
#endif
