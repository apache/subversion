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
 */

package org.apache.subversion.javahl.callback;

import org.apache.subversion.javahl.ISVNConfig;

/**
 * Interface for configuration-change hooks.
 * @since 1.9
 */
public interface ConfigEvent
{
    /**
     * Called by the native configuration loader every time the
     * configuration is parsed into memory.
     * @params configuration A reference to the in-memory configuration
     *                       data. This object is only valid during the
     *                       call and must not be accessed otherwise.
     */
    void onLoad(ISVNConfig configuration);
}
