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
 * @file RevisionRange.h
 * @brief Interface of the class RevisionRange
 */

#ifndef REVISION_RANGE_H
#define REVISION_RANGE_H

#include <jni.h>
#include "svn_types.h"

class Pool;

/**
 * A container for our copy sources, which can convert them into an
 * array of svn_client_copy_item_t's.
 */
class RevisionRange
{
 public:

  /**
   * Create a RevisionRange object from a Java object.
   */
  RevisionRange(jobject jthis);

  /**
   * Destroy a RevisionRange object
   */
  ~RevisionRange();

  /**
   * Return an svn_opt_revision_range_t.
   */
  const svn_opt_revision_range_t *toRange(Pool &pool) const;

  /**
   * Make a (single) RevisionRange Java object.
   */
  static jobject makeJRevisionRange(svn_merge_range_t *range);

 private:
  /**
   * A local reference to the Java RevisionRange peer.
   */
  jobject m_range;
};

#endif  // REVISION_RANGE_H
