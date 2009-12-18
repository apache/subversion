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
 * @file CopySources.h
 * @brief Interface of the class CopySources
 */

#ifndef COPY_SOURCES_H
#define COPY_SOURCES_H

#include <jni.h>
#include <apr_tables.h>

class Pool;

/**
 * A container for our copy sources, which can convert them into an
 * array of svn_client_copy_item_t's.
 */
class CopySources
{
 public:
  /**
   * Create a CopySources object.
   * @param jobjectArray An array of CopySource Java objects.
   */
  CopySources(jobjectArray copySources);

  /**
   * Destroy a CopySources object
   */
  ~CopySources();

  /**
   * Converts the array of CopySources to an apr_array_header_t of
   * svn_client_copy_source_t *'s.
   *
   * @param pool The pool from which to perform allocations.
   * @return A pointer to the new array.
   */
  apr_array_header_t *array(Pool &pool);

  /**
   * Make a (single) CopySource Java object.
   */
  static jobject makeJCopySource(const char *path, svn_revnum_t rev,
                                 Pool &pool);

 private:
  /**
   * A local reference to the Java CopySources peer.
   */
  jobjectArray m_copySources;
};

#endif  /* COPY_SOURCES_H */
