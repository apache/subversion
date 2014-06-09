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
        throws ClientException
    {
        return nativeGetCredential(configDir, kind.toString(), realm);
    }

    /** @see SVNUtil.removeCredential */
    public SVNUtil.Credential removeCredential(String configDir,
                                               SVNUtil.Credential.Kind kind,
                                               String realm)
        throws ClientException
    {
        return nativeRemoveCredential(configDir, kind.toString(), realm);
    }

    /** @see SVNUtil.addCredential */
    public SVNUtil.Credential addCredential(String configDir,
                                            SVNUtil.Credential credential,
                                            boolean replace)
        throws ClientException
    {
        final AuthnCallback.SSLServerCertInfo sci = credential.getServerCertInfo();
        final AuthnCallback.SSLServerCertFailures scf = credential.getServerCertFailures();
        return nativeAddCredential(configDir,
                                   credential.getKind().toString(),
                                   credential.getRealm(),
                                   credential.getUsername(),
                                   credential.getPassword(),
                                   (sci != null ? sci.getHostname() : null),
                                   (sci != null ? sci.getFingerprint() : null),
                                   (sci != null ? sci.getValidFrom() : null),
                                   (sci != null ? sci.getValidUntil() : null),
                                   (sci != null ? sci.getIssuer() : null),
                                   (sci != null ? sci.getDER() : null),
                                   (scf != null ? scf.getFailures() : 0),
                                   credential.getClientCertPassphrase());
    }

    /** @see SVNUtil.searchCredentials */
    public List<SVNUtil.Credential>
        searchCredentials(String configDir,
                          SVNUtil.Credential.Kind kind,
                          String realmPattern,
                          String usernamePattern,
                          String hostnamePattern,
                          String textPattern)
        throws ClientException
    {
        return iterateCredentials(
            false, configDir, kind.toString(), realmPattern,
            usernamePattern, hostnamePattern, textPattern);
    }

    /** @see SVNUtil.deleteCredentials */
    public List<SVNUtil.Credential>
        deleteCredentials(String configDir,
                          SVNUtil.Credential.Kind kind,
                          String realmPattern,
                          String usernamePattern,
                          String hostnamePattern,
                          String textPattern)
        throws ClientException
    {
        return iterateCredentials(
            true, configDir, kind.toString(), realmPattern,
            usernamePattern, hostnamePattern, textPattern);
    }

    private native SVNUtil.Credential
        nativeGetCredential(String configDir,
                               String kind,
                               String realm)
        throws ClientException;

    private native SVNUtil.Credential
        nativeRemoveCredential(String configDir,
                               String kind,
                               String realm)
        throws ClientException;

    private native SVNUtil.Credential
        nativeAddCredential(String configDir,
                            String kind,
                            String realm,
                            String username,
                            String password,
                            String serverCertHostname,
                            String serverCertFingerprint,
                            String serverCertValidFrom,
                            String serverCertValidUntil,
                            String serverCertIssuer,
                            String serverCertDER,
                            int serverCertFailures,
                            String clientCertPassphrase)
        throws ClientException;

    private native List<SVNUtil.Credential>
        iterateCredentials(boolean deleteMatching,
                           String configDir,
                           String kind,
                           String realmPattern,
                           String usernamePattern,
                           String hostnamePattern,
                           String textPattern)
        throws ClientException;
}

