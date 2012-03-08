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
 * @file svn_named_atomics.h
 * @brief Strutures and functions for machine-wide named atomics
 */

#ifndef SVN_NAMED_ATOMICS_H
#define SVN_NAMED_ATOMICS_H

#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** An opaque structure that represents a named, system-wide visible
 * 64 bit integer with atomic access routines.
 */
typedef struct svn_named_atomic__t svn_named_atomic__t;

#define SVN_NAMED_ATOMIC__MAX_NAME_LENGTH 30

/** Find the atomic with the specified @a name and return it in @a *atomic.
 * If no object with that name can be found, the behavior depends on 
 * @a auto_create. If it is @c FALSE, @a *atomic will be set to @c NULL.
 * Otherwise, a new atomic will be created, its value set to 0 and the
 * access structure be returned in @a *atomic.
 * 
 * Note that @a name must be short and should not exceeed 
 * @ref SVN_NAMED_ATOMIC__MAX_NAME_LENGTH characters. The actual limit is
 * implementation-dependent and may change in the future. This function
 * will return an error if the specified name is longer than supported.
 *
 * This function will automatically initialize the shared memory region,
 * if that hadn't been attempted before. Therefore, this may fail with
 * a variety of errors.
 */
svn_error_t *
svn_named_atomic__get(svn_named_atomic__t **atomic,
                      const char *name,
                      svn_boolean_t auto_create);

/** Read the @a atomic and return its current @a *value.
 * An error will be returned if @a atomic is @c NULL.
 */
svn_error_t *
svn_named_atomic__read(apr_int64_t *value,
                       svn_named_atomic__t *atomic);

/** Set the data in @a atomic to @a NEW_VALUE and return its old content
 * in @a OLD_VALUE.  @a OLD_VALUE may be NULL.
 * 
 * An error will be returned if @a atomic is @c NULL.
 */
svn_error_t *
svn_named_atomic__write(apr_int64_t *old_value,
                        apr_int64_t new_value,
                        svn_named_atomic__t *atomic);

/** Add @a delta to the data in @a atomic and return its new value in
 * @a NEW_VALUE.  @a NEW_VALUE may be NULL.
 * 
 * An error will be returned if @a atomic is @c NULL.
 */
svn_error_t *
svn_named_atomic__add(apr_int64_t *new_value,
                      apr_int64_t delta,
                      svn_named_atomic__t *atomic);

/** If the current data in @a atomic equals @a comperand, set it to
 * @a NEW_VALUE.  Return the initial value in @a OLD_VALUE.
 * @a OLD_VALUE may be NULL.
 * 
 * An error will be returned if @a atomic is @c NULL.
 */
svn_error_t *
svn_named_atomic__cmpxchg(apr_int64_t *old_value,
                          apr_int64_t new_value,
                          apr_int64_t comperand,
                          svn_named_atomic__t *atomic);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_NAMED_ATOMICS_H */
