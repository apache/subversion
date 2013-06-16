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

import org.apache.subversion.javahl.ISVNRemote;
import org.apache.subversion.javahl.JNIObject;
import org.apache.subversion.javahl.OperationContext;
import org.apache.subversion.javahl.ClientException;

import java.util.Date;
import java.util.Map;

import static java.util.concurrent.TimeUnit.MILLISECONDS;
import static java.util.concurrent.TimeUnit.NANOSECONDS;

public class RemoteSession extends JNIObject implements ISVNRemote
{
    public native void dispose();

    public void cancelOperation() throws ClientException
    {
        thrownotimplemented("cancelOperation");
    }

    public void reparent(String url) throws ClientException
    {
        thrownotimplemented("reparent");
    }

    public native String getSessionUrl() throws ClientException;

    public String getSessionRelativePath(String url) throws ClientException
    {
        thrownotimplemented("getSessionRelativePath");
        return null;
    }

    public String getRepositoryRelativePath(String url) throws ClientException
    {
        thrownotimplemented("getRepositoryRelativePath");
        return null;
    }

    public native String getReposUUID() throws ClientException;

    public native Revision getLatestRevision() throws ClientException;

    public Revision getRevisionByDate(Date date) throws ClientException
    {
        long timestamp = NANOSECONDS.convert(date.getTime(), MILLISECONDS);
        return getRevisionByTimestamp(timestamp);
    }

    public native Revision getRevisionByTimestamp(long timestamp)
            throws ClientException;

    public native NodeKind checkPath(String path, Revision revision)
            throws ClientException;

    public native Map<String, Lock> getLocks(String path, Depth depth)
            throws ClientException;

    @Override
    public native void finalize() throws Throwable;

    /**
     * This constructor is called from JNI to get an instance call
     * getRaSession method of ISVNClient
     */
    protected RemoteSession(long cppAddr)
    {
        super(cppAddr);
    }

    /*
     * NOTE: This field is accessed from native code for callbacks.
     */
    private RemoteSessionContext sessionContext = new RemoteSessionContext();
    private class RemoteSessionContext extends OperationContext {}


    private void thrownotimplemented(String message)
    {
        throw new RuntimeException("Not implemented: " + message);
    }
}
