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

/**
 * Implementation of ISVNReporter.
 * @since 1.9
 */
public class StateReporter extends JNIObject implements ISVNReporter
{
    public void dispose() {/* TODO: */}

    public void setPath(String path,
                        long revision,
                        Depth depth,
                        boolean startEmpty,
                        String lockToken)
            throws ClientException
    {
        throw new RuntimeException("Not implemented: setPath");
    }

    public void deletePath(String path) throws ClientException
    {
        throw new RuntimeException("Not implemented: deletePath");
    }

    public void linkPath(String url,
                         String path,
                         long revision,
                         Depth depth,
                         boolean startEmpty,
                         String lockToken)
            throws ClientException
    {
        throw new RuntimeException("Not implemented: linkPath");
    }

    public long finishReport() throws ClientException
    {
        throw new RuntimeException("Not implemented: finishReport");
    }

    public void abortReport() throws ClientException
    {
        throw new RuntimeException("Not implemented: abortReport");
    }

    /**
     * This factory method called from RemoteSession.status and friends.
     */
    static final
        StateReporter createInstance(RemoteSession session)
            throws ClientException
    {
        return null;
    }

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
