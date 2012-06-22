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

package org.apache.subversion.javahl.ra;

import org.apache.subversion.javahl.NativeResources;
import org.apache.subversion.javahl.types.Version;

public class SVNRaFactory
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();

        // TODO: Remove below, once NativeResources version check catches up to
        // Ra check
        Version version = NativeResources.getVersion();
        if (!version.isAtLeast(1, 7, 6))
        {
            throw new LinkageError("Native library version must be at least "
                    + "1.7.6, but is only " + version);
        }
    }

    /**
     * Crates RA session for a given url with provided context
     * 
     * @param url
     *            to connect to
     * @param uuid
     *            of the remote repository, can be null if uuid check is not
     *            desired
     * @param config
     *            configuration to use for the session.
     * @return RA session
     */
    public static native ISVNRa createRaSession(String url, String uuid,
            ISVNRaConfig config);
}
