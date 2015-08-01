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
 * @file org_apache_subversion_javahl_types_RevisionRangeList.cpp
 * @brief Implementation of the native methods in the Java class
 * RevisionRangeList.
 */


#include "../include/org_apache_subversion_javahl_types_RevisionRangeList.h"
#include "JNIStackElement.h"
#include "RevisionRangeList.h"
#include "Pool.h"

#include "svn_mergeinfo.h"
#include "svn_private_config.h"

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_types_RevisionRangeList_remove(
    JNIEnv* env, jobject jthis, jobject jeraser,
    jboolean jconsider_inheritance)
{
  JNIEntry(RevisionRangeList, remove);

  SVN::Pool request_pool;

  RevisionRangeList rangelist = RevisionRangeList::create(jthis, request_pool);
  CPPADDR_NULL_PTR(rangelist.get(), NULL);

  RevisionRangeList eraser(jeraser, request_pool);
  CPPADDR_NULL_PTR(eraser.get(), NULL);

  svn_rangelist_t *output;
  SVN_JNI_ERR(svn_rangelist_remove(&output, eraser.get(), rangelist.get(),
                                   bool(jconsider_inheritance),
                                   request_pool.getPool()),
              NULL);
  return RevisionRangeList(output).toList();
}
