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
 * @file ListCallback.h
 * @brief Interface of the class ListCallback
 */

#ifndef LISTCALLBACK_H
#define LISTCALLBACK_H

#include <jni.h>
#include "svn_client.h"

/**
 * This class holds a Java callback object, which will receive every
 * directory entry for which the callback information is requested.
 */
class ListCallback
{
public:
  ListCallback(jobject jcallback);
  ~ListCallback();

  static svn_error_t *callback(void *baton,
                               const char *path,
                               const svn_dirent_t *dirent,
                               const svn_lock_t *lock,
                               const char *abs_path,
                               apr_pool_t *pool);

protected:
  svn_error_t *doList(const char *path,
                      const svn_dirent_t *dirent,
                      const svn_lock_t *lock,
                      const char *abs_path,
                      apr_pool_t *pool);

private:
  /**
   * This a local reference to the Java object.
   */
  jobject m_callback;

  jobject createJavaDirEntry(const char *path,
                             const char *absPath,
                             const svn_dirent_t *dirent);
};

#endif // LISTCALLBACK_H
