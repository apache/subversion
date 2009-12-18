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
 * @file StatusCallback.h
 * @brief Interface of the class StatusCallback
 */

#ifndef STATUSCALLBACK_H
#define STATUSCALLBACK_H

#include <jni.h>
#include "svn_client.h"

/**
 * This class holds a Java callback object, each status item
 * for which the callback information is requested.
 */
class StatusCallback
{
 public:
  StatusCallback(jobject jcallback);
  ~StatusCallback();

  static svn_error_t* callback(void *baton,
                               const char *path,
                               svn_wc_status2_t *status,
                               apr_pool_t *pool);

 protected:
  svn_error_t *doStatus(const char *path, svn_wc_status2_t *status);

 private:
  /**
   * This a local reference to the Java object.
   */
  jobject m_callback;

  jobject createJavaStatus(const char *path,
                           svn_wc_status2_t *status);
};

#endif // STATUSCALLBACK_H
