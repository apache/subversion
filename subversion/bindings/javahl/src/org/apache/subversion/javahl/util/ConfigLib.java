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

import org.apache.subversion.javahl.callback.*;

import org.apache.subversion.javahl.SVNUtil;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.NativeResources;
import org.apache.subversion.javahl.SubversionException;

import java.util.List;

/**
 * Provides global configuration knobs and
 * Encapsulates utility functions for authentication credentials management.
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

    //
    // Credentials management
    //

    /** @see SVNUtil.getCredential */
    public SVNUtil.Credential getCredential(String configDir,
                                            SVNUtil.Credential.Kind kind,
                                            String realm)
        throws ClientException, SubversionException
    {
        return nativeGetCredential(configDir, kind.toString(), realm);
    }

    /** @see SVNUtil.removeCredential */
    public SVNUtil.Credential removeCredential(String configDir,
                                               SVNUtil.Credential.Kind kind,
                                               String realm)
        throws ClientException, SubversionException
    {
        return nativeRemoveCredential(configDir, kind.toString(), realm);
    }

    /** @see SVNUtil.searchCredentials */
    public List<SVNUtil.Credential>
        searchCredentials(String configDir,
                          SVNUtil.Credential.Kind kind,
                          String realmPattern,
                          String usernamePattern,
                          String hostnamePattern,
                          String textPattern)
        throws ClientException, SubversionException
    {
        return nativeSearchCredentials(
            configDir,
            (kind != null ? kind.toString() : null),
            realmPattern, usernamePattern, hostnamePattern, textPattern);
    }

    private native SVNUtil.Credential
        nativeGetCredential(String configDir,
                               String kind,
                               String realm)
        throws ClientException, SubversionException;

    private native SVNUtil.Credential
        nativeRemoveCredential(String configDir,
                               String kind,
                               String realm)
        throws ClientException, SubversionException;

    private native List<SVNUtil.Credential>
        nativeSearchCredentials(String configDir,
                                String kind,
                                String realmPattern,
                                String usernamePattern,
                                String hostnamePattern,
                                String textPattern)
        throws ClientException, SubversionException;
}

