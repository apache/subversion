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

#ifndef PROPLISTCALLBACK_H
#define PROPLISTCALLBACK_H

#include <jni.h>
#include "svn_client.h"

/**
 * This class holds a Java callback object, which will receive every
 * property from a SVNClientInterface.properties call.
 */
class ProplistCallback
{
 public:
  ProplistCallback(jobject jcallback);
  ~ProplistCallback();

  static svn_error_t *callback(void *baton,
                               const char *path,
                               apr_hash_t *prop_hash,
                               apr_pool_t *pool);

  static jobject makeMapFromHash(apr_hash_t *prop_hash, apr_pool_t *pool);

 protected:
  svn_error_t *singlePath(const char *path,
                          apr_hash_t *prop_hash,
                          apr_pool_t *pool);

 private:
  /**
   * This a local reference to the Java object.
   */
  jobject m_callback;
};

#endif  // PROPLISTCALLBACK_H
