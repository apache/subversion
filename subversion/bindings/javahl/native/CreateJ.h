/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007-2009 CollabNet.  All rights reserved.
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
 * @file CreateJ.h
 * @brief Interface of the class CreateJ
 */

#ifndef CREATEJ_H
#define CREATEJ_H

#include <jni.h>
#include "svn_wc.h"

/**
 * This class passes centralizes the creating of Java objects from
 * Subversion's C structures.
 * @since 1.6
 */
class CreateJ
{
 public:
  static jobject
  ConflictDescriptor(const svn_wc_conflict_description_t *desc);

  static jobject
  Info(const svn_wc_entry_t *entry);

  static jobject
  Lock(const svn_lock_t *lock);

  static jobject
  Property(jobject jthis, const char *path, const char *name,
           svn_string_t *value);

  static jobjectArray
  RevisionRangeArray(apr_array_header_t *ranges);

 protected:
  static jobject
  ConflictVersion(const svn_wc_conflict_version_t *version);
};

#endif  // CREATEJ_H
