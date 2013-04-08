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
 * @file svn_ra_svn_private.h
 * @brief Functions used by the server - Internal routines
 */

#ifndef SVN_RA_SVN_PRIVATE_H
#define SVN_RA_SVN_PRIVATE_H

#include "svn_ra_svn.h"
#include "svn_editor.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Set the shim callbacks to be used by @a conn to @a shim_callbacks.
 */
svn_error_t *
svn_ra_svn__set_shim_callbacks(svn_ra_svn_conn_t *conn,
                               svn_delta_shim_callbacks_t *shim_callbacks);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RA_SVN_PRIVATE_H */
