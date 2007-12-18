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
 * @file Outputer.h
 * @brief Interface of the class Outputer
 */

#ifndef OUTPUTER_H
#define OUTPUTER_H

#include <jni.h>
#include "svn_io.h"
#include "Pool.h"

/**
 * This class contains a Java objects implementing the interface Outputer and
 * implements the functions write & close of svn_stream_t
 */
class Outputer
{
  /**
   * A local reference to the Java object.
   */
  jobject m_jthis;
  static svn_error_t *write(void *baton,
                            const char *buffer, apr_size_t *len);
  static svn_error_t *close(void *baton);
 public:
  Outputer(jobject jthis);
  ~Outputer();
  svn_stream_t *getStream(const Pool &pool);
};

#endif // OUTPUTER_H
