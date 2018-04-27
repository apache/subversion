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

import org.apache.subversion.javahl.ISVNEditor;
import org.apache.subversion.javahl.JNIObject;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.NativeResources;

import java.io.InputStream;
import java.util.Map;
import java.util.Set;

/**
 * Implementation of ISVNEditor that drives commits.
 * @since 1.9
 */
public class CommitEditor extends JNIObject implements ISVNEditor
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
        session.disposeEditor(this);
        nativeDispose();
    }

    public native void addDirectory(String relativePath,
                                    Iterable<String> children,
                                    Map<String, byte[]> properties,
                                    long replacesRevision)
            throws ClientException;

    public native void addFile(String relativePath,
                               Checksum checksum,
                               InputStream contents,
                               Map<String, byte[]> properties,
                               long replacesRevision)
            throws ClientException;

    /**
     * <b>Note:</b> Not implemented.
     */
    public native void addSymlink(String relativePath,
                                  String target,
                                  Map<String, byte[]> properties,
                                  long replacesRevision)
            throws ClientException;

    public native void addAbsent(String relativePath,
                                 NodeKind kind,
                                 long replacesRevision)
            throws ClientException;

    public native void alterDirectory(String relativePath,
                                      long revision,
                                      Iterable<String> children,
                                      Map<String, byte[]> properties)
            throws ClientException;

    public native void alterFile(String relativePath,
                                 long revision,
                                 Checksum checksum,
                                 InputStream contents,
                                 Map<String, byte[]> properties)
            throws ClientException;

    /**
     * <b>Note:</b> Not implemented.
     */
    public native void alterSymlink(String relativePath,
                                    long revision,
                                    String target,
                                    Map<String, byte[]> properties)
            throws ClientException;

    public native void delete(String relativePath,
                              long revision)
            throws ClientException;

    public native void copy(String sourceRelativePath,
                            long sourceRevision,
                            String destinationRelativePath,
                            long replacesRevision)
            throws ClientException;

    public native void move(String sourceRelativePath,
                            long sourceRevision,
                            String destinationRelativePath,
                            long replacesRevision)
            throws ClientException;

    public native void complete() throws ClientException;

    public native void abort() throws ClientException;

    /**
     * This factory method called from RemoteSession.getCommitEditor.
     */
    static final
        CommitEditor createInstance(RemoteSession session,
                                    Map<String, byte[]> revisionProperties,
                                    CommitCallback commitCallback,
                                    Set<Lock> lockTokens, boolean keepLocks,
                                    ISVNEditor.ProvideBaseCallback baseCB,
                                    ISVNEditor.ProvidePropsCallback propsCB,
                                    ISVNEditor.GetNodeKindCallback kindCB)
            throws ClientException
    {
        long cppAddr = nativeCreateInstance(
            session, revisionProperties, commitCallback,
            lockTokens, keepLocks, baseCB, propsCB, kindCB);
        return new CommitEditor(cppAddr, session);
    }

    /**
     * This constructor is called from the factory to get an instance.
     */
    protected CommitEditor(long cppAddr, RemoteSession session)
    {
        super(cppAddr);
        this.session = session;
    }

    /** Stores a reference to the session that created this editor. */
    protected RemoteSession session;

    @Override
    public native void finalize() throws Throwable;

    /*
     * Wrapped private native implementation declarations.
     */
    private native void nativeDispose();
    private static final native
        long nativeCreateInstance(RemoteSession session,
                                  Map<String, byte[]> revisionProperties,
                                  CommitCallback commitCallback,
                                  Set<Lock> lockTokens, boolean keepLocks,
                                  ISVNEditor.ProvideBaseCallback baseCB,
                                  ISVNEditor.ProvidePropsCallback propsCB,
                                  ISVNEditor.GetNodeKindCallback kindCB)
            throws ClientException;
}
