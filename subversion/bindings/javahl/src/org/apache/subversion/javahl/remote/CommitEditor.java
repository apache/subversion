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

import java.io.InputStream;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Implementation of ISVNEditor that drives commits.
 * @since 1.9
 */
public class CommitEditor extends JNIObject implements ISVNEditor
{
    public void dispose()
    {
        session.disposeEditor(this);
        nativeDispose();
    }

    public void addDirectory(String relativePath,
                             Iterable<String> children,
                             Map<String, byte[]> properties,
                             long replacesRevision)
            throws ClientException
    {
        nativeAddDirectory(session, relativePath, children, properties,
                           replacesRevision);
    }

    public void addFile(String relativePath,
                        Checksum checksum,
                        InputStream contents,
                        Map<String, byte[]> properties,
                        long replacesRevision)
            throws ClientException
    {
        nativeAddFile(session, relativePath,
                      checksum, contents, properties,
                      replacesRevision);
    }

    public void addSymlink(String relativePath,
                           String target,
                           Map<String, byte[]> properties,
                           long replacesRevision)
            throws ClientException
    {
        nativeAddSymlink(session, relativePath, target, properties,
                         replacesRevision);
    }

    public void addAbsent(String relativePath,
                          NodeKind kind,
                          long replacesRevision)
            throws ClientException
    {
        nativeAddAbsent(session, relativePath, kind, replacesRevision);
    }

    public void alterDirectory(String relativePath,
                               long revision,
                               Iterable<String> children,
                               Map<String, byte[]> properties)
            throws ClientException
    {
        nativeAlterDirectory(session, relativePath, revision,
                             children, properties);
    }

    public void alterFile(String relativePath,
                          long revision,
                          Checksum checksum,
                          InputStream contents,
                          Map<String, byte[]> properties)
            throws ClientException
    {
        nativeAlterFile(session, relativePath, revision,
                        checksum, contents, properties);
    }

    public void alterSymlink(String relativePath,
                             long revision,
                             String target,
                             Map<String, byte[]> properties)
            throws ClientException
    {
        nativeAlterSymlink(session, relativePath, revision,
                           target, properties);
    }

    public void delete(String relativePath,
                       long revision)
            throws ClientException
    {
        nativeDelete(session, relativePath, revision);
    }

    public void copy(String sourceRelativePath,
                     long sourceRevision,
                     String destinationRelativePath,
                     long replacesRevision)
            throws ClientException
    {
        nativeCopy(session, sourceRelativePath, sourceRevision,
                   destinationRelativePath, replacesRevision);
    }

    public void move(String sourceRelativePath,
                     long sourceRevision,
                     String destinationRelativePath,
                     long replacesRevision)
            throws ClientException
    {
        nativeMove(session, sourceRelativePath, sourceRevision,
                   destinationRelativePath, replacesRevision);
    }

    public void rotate(List<RotatePair> elements) throws ClientException
    {
        nativeRotate(session, elements);
    }

    public void complete() throws ClientException
    {
        nativeComplete(session);
    }

    public void abort() throws ClientException
    {
        nativeAbort(session);
    }

    /**
     * This factory method called from RemoteSession.getCommitEditor.
     */
    static final
        CommitEditor createInstance(RemoteSession session,
                                    Map<String, byte[]> revisionProperties,
                                    CommitCallback commitCallback,
                                    Set<Lock> lockTokens,
                                    boolean keepLocks)
            throws ClientException
    {
        long cppAddr = nativeCreateInstance(session,
                                            revisionProperties, commitCallback,
                                            lockTokens, keepLocks);
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
    private native void nativeAddDirectory(RemoteSession session,
                                           String relativePath,
                                           Iterable<String> children,
                                           Map<String, byte[]> properties,
                                           long replacesRevision)
            throws ClientException;
    private native void nativeAddFile(RemoteSession session,
                                      String relativePath,
                                      Checksum checksum,
                                      InputStream contents,
                                      Map<String, byte[]> properties,
                                      long replacesRevision)
            throws ClientException;
    private native void nativeAddSymlink(RemoteSession session,
                                         String relativePath,
                                         String target,
                                         Map<String, byte[]> properties,
                                         long replacesRevision)
            throws ClientException;
    private native void nativeAddAbsent(RemoteSession session,
                                        String relativePath,
                                        NodeKind kind,
                                        long replacesRevision)
            throws ClientException;
    private native void nativeAlterDirectory(RemoteSession session,
                                             String relativePath,
                                             long revision,
                                             Iterable<String> children,
                                             Map<String, byte[]> properties)
            throws ClientException;
    private native void nativeAlterFile(RemoteSession session,
                                        String relativePath,
                                        long revision,
                                        Checksum checksum,
                                        InputStream contents,
                                        Map<String, byte[]> properties)
            throws ClientException;
    private native void nativeAlterSymlink(RemoteSession session,
                                           String relativePath,
                                           long revision,
                                           String target,
                                           Map<String, byte[]> properties)
            throws ClientException;
    private native void nativeDelete(RemoteSession session,
                                     String relativePath,
                                     long revision)
            throws ClientException;
    private native void nativeCopy(RemoteSession session,
                                   String sourceRelativePath,
                                   long sourceRevision,
                                   String destinationRelativePath,
                                   long replacesRevision)
            throws ClientException;
    private native void nativeMove(RemoteSession session,
                                   String sourceRelativePath,
                                   long sourceRevision,
                                   String destinationRelativePath,
                                   long replacesRevision)
            throws ClientException;
    private native void nativeRotate(RemoteSession session,
                                     List<RotatePair> elements)
            throws ClientException;
    private native void nativeComplete(RemoteSession session)
            throws ClientException;
    private native void nativeAbort(RemoteSession session)
            throws ClientException;
    private static final native
        long nativeCreateInstance(RemoteSession session,
                                  Map<String, byte[]> revisionProperties,
                                  CommitCallback commitCallback,
                                  Set<Lock> lockTokens,
                                  boolean keepLocks)
            throws ClientException;
}
