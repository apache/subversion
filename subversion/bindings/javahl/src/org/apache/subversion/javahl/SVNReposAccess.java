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

package org.apache.subversion.javahl;

import java.net.URI;
import java.util.Date;
import java.util.Map;

import org.apache.subversion.javahl.types.Depth;
import org.apache.subversion.javahl.types.Lock;
import org.apache.subversion.javahl.types.NodeKind;
import org.apache.subversion.javahl.types.Revision;
import org.apache.subversion.javahl.types.Version;

/**
 * This class allows direct access to remote repositories through a
 * lower-level API.
 */
public class SVNReposAccess implements ISVNReposAccess
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /**
     * Standard empty contructor, builds just the native peer.
     */
    public SVNReposAccess(URI reposURI)
    {
        cppAddr = ctNative(reposURI.toString());
/*
        // Ensure that Subversion's config file area and templates exist.
        try
        {
            setConfigDirectory(null);
        }
        catch (ClientException suppressed)
        {
            // Not an exception-worthy problem, continue on.
        }
*/
    }

    private long getCppAddr()
    {
        return cppAddr;
    }

    /**
     * Build the native peer
     * @return the adress of the peer
     */
    private native long ctNative(String uri);

     /**
     * release the native peer (should not depend on finalize)
     */
    public native void dispose();

    /**
     * release the native peer (should use dispose instead)
     */
    public native void finalize();

    /**
     * slot for the adress of the native peer. The JNI code is the only user
     * of this member
     */
    protected long cppAddr;

    /**
     * @since 1.0
     */
    public Version getVersion()
    {
        return NativeResources.getVersion();
    }

    public native long getDatedRevision(Date date)
        throws SubversionException;

    public native Map<String, Lock> getLocks(String path, Depth depth)
        throws SubversionException;

    public native NodeKind checkPath(String path, Revision revision)
        throws SubversionException;
}
