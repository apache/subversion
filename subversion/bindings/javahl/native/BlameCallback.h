/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2007 CollabNet.  All rights reserved.
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
 * @file BlameCallback.h
 * @brief Interface of the class BlameCallback
 */

#ifndef BLAMECALLBACK_H
#define BLAMECALLBACK_H

#include <jni.h>
#include "svn_client.h"

/**
 * This class holds a Java callback object, which will receive every
 * line of the file for which the callback information is requested.
 */
class BlameCallback
{
 public:
  BlameCallback(jobject jcallback);
  ~BlameCallback();

  static svn_error_t *callback(void *baton,
                               apr_int64_t line_no,
                               svn_revnum_t revision,
                               const char *author,
                               const char *date,
                               svn_revnum_t merged_revision,
                               const char *merged_author,
                               const char *merged_date,
                               const char *merged_path,
                               const char *line,
                               apr_pool_t *pool);

 protected:
  svn_error_t *singleLine(svn_revnum_t revision, const char *author,
                          const char *date, svn_revnum_t mergedRevision,
                          const char *mergedAuthor, const char *mergedDate,
                          const char *mergedPath, const char *line,
                          apr_pool_t *pool);

 private:
  /**
   * This a local reference to the Java object.
   */
  jobject m_callback;
};

#endif  // BLAMECALLBACK_H
