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
 * @file Revision.h
 * @brief Interface of the class Revision
 */

#ifndef REVISION_H
#define REVISION_H

#include <jni.h>
#include "svn_opt.h"

class Revision
{
 private:
  svn_opt_revision_t m_revision;

 public:
  static const svn_opt_revision_kind START;
  static const svn_opt_revision_kind HEAD;

  Revision(jobject jthis, bool headIfUnspecified = false,
           bool oneIfUnspecified = false);
  Revision(const svn_opt_revision_kind kind = svn_opt_revision_unspecified);
  ~Revision();

  const svn_opt_revision_t *revision() const;

  /**
   * Make a Revision Java object.
   */
  static jobject makeJRevision(svn_revnum_t rev);
};

#endif // REVISION_H
