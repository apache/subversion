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

package org.apache.subversion.javahl.util;

import org.apache.subversion.javahl.SVNUtil;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.NativeResources;

import org.apache.subversion.javahl.callback.*;

/**
 * Provides global configuration knobs.
 * @since 1.9
 */
public class ConfigLib
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /** @see SVNUtil.enableNativeCredentialsStore */
    public native void enableNativeCredentialsStore()
        throws ClientException;

    /** @see SVNUtil.disableNativeCredentialsStore */
    public native void disableNativeCredentialsStore()
        throws ClientException;

    /** @see SVNUtil.isNativeCredentialsStoreEnabled */
    public native boolean isNativeCredentialsStoreEnabled()
        throws ClientException;

    /** @see SVNUtil.setConfigEventHandler */
    public native void setConfigEventHandler(ConfigEvent configHandler)
        throws ClientException;

    /** @see SVNUtil.setConfigEventHandler */
    public native ConfigEvent getConfigEventHandler()
        throws ClientException;
}
