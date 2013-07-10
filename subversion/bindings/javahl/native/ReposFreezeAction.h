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
 * @file ReposFreezeAction.h
 * @brief Native mirror of the ReposFreezeAction callback interface
 */

#ifndef JAVAHL_REPOS_FREEZE_ACTION_H
#define JAVAHL_REPOS_FREEZE_ACTION_H

#include <apr_pools.h>
#include "svn_error.h"
#include "JNIUtil.h"

class ReposFreezeAction
{
 public:
 ReposFreezeAction(jobject jaction)
    : m_jaction(jaction)
  {}

  static svn_error_t* callback(void* baton, apr_pool_t* pool);

 private:
  svn_error_t* invoke();
  const jobject m_jaction;
};


#endif /* JAVAHL_REPOS_FREEZE_ACTION_H */
