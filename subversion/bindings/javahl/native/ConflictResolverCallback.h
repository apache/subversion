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
 * @file ConflictResolverCallback.h
 * @brief Interface of the class ConflictResolverCallback
 */

#ifndef CONFLICTRESOLVERCALLBACK_H
#define CONFLICTRESOLVERCALLBACK_H

#include <jni.h>
#include "svn_error.h"
#include "svn_wc.h"

/**
 * This class passes conflict resolution events from Subversion to a
 * Java object.
 * @since 1.5
 */
class ConflictResolverCallback
{
 private:
  /**
   * The Java object which handles the conflict resolution
   * events. This is a global reference, because it has to live longer
   * than the SVNClient.onProgress() call.
   */
  jobject m_conflictResolver;

  /**
   * Create a new instance, storing a global reference to the
   * corresponding Java object.
   *
   * @param jconflictResolver Reference to the Java peer.
   */
  ConflictResolverCallback(jobject jconflictResolver);

 public:
  /**
   * Destroy the instance, and delete the global reference to the
   * Java object.
   */
  ~ConflictResolverCallback();

  /** Constructor function called from C JNI glue code. */
  static ConflictResolverCallback *
  makeCConflictResolverCallback(jobject jconflictResolver);

  /**
   * Implementation of the svn_wc_conflict_resolver_func_t API.
   */
  static svn_error_t *
  resolveConflict(svn_wc_conflict_result_t **result,
                  const svn_wc_conflict_description_t *desc,
                  void *baton,
                  apr_pool_t *pool);

 protected:
  /**
   * Delegate to m_conflictResolver.resolve(), which provides the
   * logic for the implementation of the svn_wc_conflict_resolver_func_t
   * API.
   */
  svn_error_t * resolve(svn_wc_conflict_result_t **result,
                        const svn_wc_conflict_description_t *desc,
                        apr_pool_t *pool);

 private:
  /**
   * Convert the Java conflict resolution @a result into the
   * appropriate C representation.
   */
  static svn_wc_conflict_result_t * javaResultToC(jobject result,
                                                  apr_pool_t *pool);

  /**
   * Convert the Java conflict resolution @a choice into the
   * appropriate C enum value.
   */
  static svn_wc_conflict_choice_t javaChoiceToC(jint choice);
};

#endif  // CONFLICTRESOLVERCALLBACK_H
