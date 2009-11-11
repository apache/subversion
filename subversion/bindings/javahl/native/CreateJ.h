/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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
