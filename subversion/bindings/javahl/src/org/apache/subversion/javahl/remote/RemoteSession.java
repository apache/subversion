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
import org.apache.subversion.javahl.ISVNEditor;
import org.apache.subversion.javahl.ISVNReporter;
import org.apache.subversion.javahl.JNIObject;
import org.apache.subversion.javahl.OperationContext;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.NativeResources;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.io.OutputStream;

import static java.util.concurrent.TimeUnit.MILLISECONDS;
import static java.util.concurrent.TimeUnit.MICROSECONDS;

public class RemoteSession extends JNIObject implements ISVNRemote
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
        if (editorReference != null)
        {
            // Deactivate the open editor
            ISVNEditor ed = editorReference.get();
            if (ed != null)
            {
                ed.dispose();
                editorReference.clear();
            }
            editorReference = null;
        }
        if (reporterReference != null)
        {
            // Deactivate the open reporter
            ISVNReporter rp = reporterReference.get();
            if (rp != null)
            {
                rp.dispose();
                reporterReference.clear();
            }
            reporterReference = null;
        }
        nativeDispose();
    }

    public native void cancelOperation() throws ClientException;

    public native void reparent(String url) throws ClientException;

    public native String getSessionUrl() throws ClientException;

    public native String getSessionRelativePath(String url)
            throws ClientException;

    public native String getReposRelativePath(String url)
            throws ClientException;

    public native String getReposUUID() throws ClientException;

    public native String getReposRootUrl() throws ClientException;

    public native long getLatestRevision() throws ClientException;

    public long getRevisionByDate(Date date) throws ClientException
    {
        long timestamp = MICROSECONDS.convert(date.getTime(), MILLISECONDS);
        return getRevisionByTimestamp(timestamp);
    }

    public native long getRevisionByTimestamp(long timestamp)
            throws ClientException;

    public void changeRevisionProperty(long revision,
                                       String propertyName,
                                       byte[] oldValue,
                                       byte[] newValue)
            throws ClientException
    {
        if (oldValue != null && !hasCapability(Capability.atomic_revprops))
            throw new IllegalArgumentException(
                "oldValue must be null;\n" +
                "The server does not support" +
                " atomic revision property changes");
        nativeChangeRevisionProperty(revision, propertyName,
                                     oldValue, newValue);
    }

    public native Map<String, byte[]> getRevisionProperties(long revision)
            throws ClientException;

    public native byte[] getRevisionProperty(long revision, String propertyName)
            throws ClientException;

    public ISVNEditor getCommitEditor(Map<String, byte[]> revisionProperties,
                                      CommitCallback commitCallback,
                                      Set<Lock> lockTokens, boolean keepLocks,
                                      ISVNEditor.ProvideBaseCallback getBase,
                                      ISVNEditor.ProvidePropsCallback getProps,
                                      ISVNEditor.GetNodeKindCallback getCopyfromKind)
            throws ClientException
    {
        check_inactive(editorReference, reporterReference);
        ISVNEditor ed =
            CommitEditor.createInstance(this, revisionProperties,
                                        commitCallback, lockTokens, keepLocks,
                                        getBase, getProps, getCopyfromKind);
        if (editorReference != null)
            editorReference.clear();
        editorReference = new WeakReference<ISVNEditor>(ed);
        return ed;
    }

    public ISVNEditor getCommitEditor(Map<String, byte[]> revisionProperties,
                                      CommitCallback commitCallback,
                                      Set<Lock> lockTokens, boolean keepLocks)
            throws ClientException
    {
        return getCommitEditor(revisionProperties, commitCallback,
                               lockTokens, keepLocks, null, null, null);
    }

    public long getFile(long revision, String path,
                        OutputStream contents,
                        Map<String, byte[]> properties)
            throws ClientException
    {
        maybe_clear(properties);
        return nativeGetFile(revision, path, contents, properties);
    }

    public long getDirectory(long revision, String path,
                             int direntFields,
                             Map<String, DirEntry> dirents,
                             Map<String, byte[]> properties)
            throws ClientException
    {
        maybe_clear(dirents);
        maybe_clear(properties);
        return nativeGetDirectory(revision, path,
                                  direntFields, dirents, properties);
    }

    public native Map<String, Mergeinfo>
        getMergeinfo(Iterable<String> paths, long revision,
                     Mergeinfo.Inheritance inherit,
                     boolean includeDescendants)
            throws ClientException;

    // TODO: update
    // TODO: switch

    public ISVNReporter status(String statusTarget,
                               long revision, Depth depth,
                               RemoteStatus receiver)
            throws ClientException
    {
        check_inactive(editorReference, reporterReference);
        StateReporter rp = StateReporter.createInstance(this);

        // At this point, the reporter is not active/valid.
        StatusEditor editor = new StatusEditor(receiver);
        nativeStatus(statusTarget, revision, depth, editor, rp);
        // Now it should be valid.

        if (reporterReference != null)
            reporterReference.clear();
        reporterReference = new WeakReference<ISVNReporter>(rp);
        return rp;
    }

    // TODO: diff

    public native void getLog(Iterable<String> paths,
                              long startRevision, long endRevision, int limit,
                              boolean strictNodeHistory, boolean discoverPath,
                              boolean includeMergedRevisions,
                              Iterable<String> revisionProperties,
                              LogMessageCallback callback)
            throws ClientException;

    public native NodeKind checkPath(String path, long revision)
            throws ClientException;

    public native DirEntry stat(String path, long revision)
            throws ClientException;

    public native Map<Long, String>
        getLocations(String path, long pegRevision,
                     Iterable<Long> locationRevisions)
            throws ClientException;

    public native
        void getLocationSegments(String path,
                                 long pegRevision,
                                 long startRevision,
                                 long endRevision,
                                 RemoteLocationSegmentsCallback handler)
            throws ClientException;

    private static class GetLocationSegmentsHandler
        implements RemoteLocationSegmentsCallback
    {
        public List<LocationSegment> locationSegments = null;
        public void doSegment(LocationSegment locationSegment)
        {
            if (locationSegments == null)
                locationSegments = new ArrayList<LocationSegment>();
            locationSegments.add(locationSegment);
        }
    }

    public List<LocationSegment> getLocationSegments(String path,
                                                     long pegRevision,
                                                     long startRevision,
                                                     long endRevision)
            throws ClientException
    {
        final GetLocationSegmentsHandler handler = new GetLocationSegmentsHandler();
        getLocationSegments(path, pegRevision, startRevision, endRevision, handler);
        return handler.locationSegments;
    }

    public native
        void getFileRevisions(String path,
                              long startRevision,
                              long endRevision,
                              boolean includeMergedRevisions,
                              RemoteFileRevisionsCallback handler)
            throws ClientException;

    private static class GetFileRevisionsHandler
        implements RemoteFileRevisionsCallback
    {
        public List<FileRevision> fileRevisions = null;
        public void doRevision(FileRevision fileRevision)
        {
            if (fileRevisions == null)
                fileRevisions = new ArrayList<FileRevision>();
            fileRevisions.add(fileRevision);
        }
    }

    public List<FileRevision> getFileRevisions(String path,
                                               long startRevision,
                                               long endRevision,
                                               boolean includeMergedRevisions)
            throws ClientException
    {
        final GetFileRevisionsHandler handler = new GetFileRevisionsHandler();
        getFileRevisions(path, startRevision, endRevision,
                         includeMergedRevisions, handler);
        return handler.fileRevisions;
    }

    // TODO: lock
    // TODO: unlock
    // TODO: getLock

    public native Map<String, Lock> getLocks(String path, Depth depth)
            throws ClientException;

    // TODO: replayRange
    // TODO: replay
    // TODO: getDeletedRevision
    // TODO: getInheritedProperties

    public boolean hasCapability(Capability capability)
            throws ClientException
    {
        return nativeHasCapability(capability.toString());
    }

    @Override
    public native void finalize() throws Throwable;

    /**
     * This constructor is called from JNI to get an instance.
     */
    protected RemoteSession(long cppAddr)
    {
        super(cppAddr);
    }

    /*
     * Wrapped private native implementation declarations.
     */
    private native void nativeDispose();
    private native void nativeChangeRevisionProperty(long revision,
                                                     String propertyName,
                                                     byte[] oldValue,
                                                     byte[] newValue)
            throws ClientException;
    private native long nativeGetFile(long revision, String path,
                                      OutputStream contents,
                                      Map<String, byte[]> properties)
            throws ClientException;
    private native long nativeGetDirectory(long revision, String path,
                                           int direntFields,
                                           Map<String, DirEntry> dirents,
                                           Map<String, byte[]> properties)
            throws ClientException;
    private native void nativeStatus(String statusTarget,
                                     long revision, Depth depth,
                                     ISVNEditor statusEditor,
                                     ISVNReporter reporter)
            throws ClientException;
    private native boolean nativeHasCapability(String capability)
            throws ClientException;

    /*
     * NOTE: This field is accessed from native code for callbacks.
     */
    private RemoteSessionContext sessionContext = new RemoteSessionContext();
    private class RemoteSessionContext extends OperationContext {}

    /*
     * A reference to the current active editor. We need this in order
     * to dispose/abort the editor when the session is disposed. And
     * furthermore, there can be only one editor or reporter active at
     * any time.
     */
    private WeakReference<ISVNEditor> editorReference;

    /*
     * The commit editor calls this when disposed to clear the
     * reference. Note that this function will be called during our
     * dispose, so make sure they don't step on each others' toes.
     */
    void disposeEditor(ISVNEditor editor)
    {
        if (editorReference == null)
            return;
        ISVNEditor ed = editorReference.get();
        if (ed == null)
            return;
        if (ed != editor)
            throw new IllegalStateException("Disposing unknown editor");
        editorReference.clear();
    }

    /*
     * A reference to the current active reporter. We need this in
     * order to dispose/abort the report when the session is
     * disposed. And furthermore, there can be only one reporter or
     * editor active at any time.
     */
    private WeakReference<ISVNReporter> reporterReference;

    /*
     * The update reporter calls this when disposed to clear the
     * reference. Note that this function will be called during our
     * dispose, so make sure they don't step on each others' toes.
     */
    void disposeReporter(ISVNReporter reporter)
    {
        if (reporterReference == null)
            return;
        ISVNReporter rp = reporterReference.get();
        if (rp == null)
            return;
        if (rp != reporter)
            throw new IllegalStateException("Disposing unknown reporter");
        reporterReference.clear();
    }

    /*
     * Private helper methods.
     */
    private final static<K,V> void maybe_clear(Map<K,V> clearable)
    {
        if (clearable != null && !clearable.isEmpty())
            try {
                clearable.clear();
            } catch (UnsupportedOperationException ex) {
                // ignored
            }
    }

    private final static
        void check_inactive(WeakReference<ISVNEditor> editorReference,
                            WeakReference<ISVNReporter> reporterReference)
    {
        if (editorReference != null && editorReference.get() != null)
            throw new IllegalStateException("An editor is already active");
        if (reporterReference != null && reporterReference.get() != null)
            throw new IllegalStateException("A reporter is already active");
    }
}
