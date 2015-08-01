/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
 * @file RevisionRangeList.h
 * @brief Interface of the class RevisionRangeList
 */

#ifndef REVISION_RANGE_LIST_H
#define REVISION_RANGE_LIST_H

#include <jni.h>
#include "svn_mergeinfo.h"

#include "Pool.h"

/**
 * A wrapper for svn_rangelist_t
 */
class RevisionRangeList
{
 public:
  /**
   * Create a RevisionRangeList object from a Java list of revision ranges.
   */
  RevisionRangeList(jobject jrangelist, SVN::Pool &pool);

  /**
   * Create a RevisionRangeList object from a Java RevisionRangeList.
   */
  static RevisionRangeList create(jobject jthis, SVN::Pool &pool);

  /**
   * Wrap an svn_rangelist_t.
   */
  explicit RevisionRangeList(svn_rangelist_t* ranges)
    : m_rangelist(ranges)
  {}

  /**
   * Return an svn_rangelist_t.
   */
  const svn_rangelist_t* get() const { return m_rangelist; }

  /**
   * Make a Java list of reivison ranges.
   */
  jobject toList() const;

 private:
  svn_rangelist_t* m_rangelist;
};

#endif  // REVISION_RANGE_LIST_H
