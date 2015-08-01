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

package org.apache.subversion.javahl.remote;

import org.apache.subversion.javahl.types.*;
import org.apache.subversion.javahl.callback.*;

import org.apache.subversion.javahl.JNIObject;
import org.apache.subversion.javahl.ISVNReporter;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.NativeResources;

/**
 * Implementation of ISVNReporter.
 * @since 1.9
 */
public class StateReporter extends JNIObject implements ISVNReporter
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    public void dispose()
    {
        session.disposeReporter(this);
        nativeDispose();
    }

    public native void setPath(String path,
                               long revision,
                               Depth depth,
                               boolean startEmpty,
                               String lockToken)
        throws ClientException;

    public native void deletePath(String path) throws ClientException;

    public native void linkPath(String url,
                                String path,
                                long revision,
                                Depth depth,
                                boolean startEmpty,
                                String lockToken)
        throws ClientException;

    public native long finishReport() throws ClientException;

    public native void abortReport() throws ClientException;

    /**
     * This factory method called from RemoteSession.status and friends.
     */
    static final
        StateReporter createInstance(RemoteSession session)
            throws ClientException
    {
        long cppAddr = nativeCreateInstance();
        return new StateReporter(cppAddr, session);
    }

    @Override
    public native void finalize() throws Throwable;

    /*
     * Wrapped private native implementation declarations.
     */
    private native void nativeDispose();
    private static final native long nativeCreateInstance()
        throws ClientException;

    /**
     * This constructor is called from the factory method.
     */
    protected StateReporter(long cppAddr, RemoteSession session)
    {
        super(cppAddr);
        this.session = session;
    }

    /** Stores a reference to the session that created this reporter. */
    protected RemoteSession session;
}
