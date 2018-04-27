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

import org.apache.subversion.javahl.callback.*;
import org.apache.subversion.javahl.types.*;

import java.io.OutputStream;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;
import java.io.ByteArrayOutputStream;

import java.util.Collection;
import java.util.Collections;
import java.util.Set;
import java.util.List;
import java.util.Map;

/**
 * This is the main client class.  All Subversion client APIs are
 * implemented in this class.  This class is not threadsafe; if you
 * need threadsafe access, use ClientSynchronized.
 */
public class SVNClient implements ISVNClient
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /**
     * Standard empty constructor, builds just the native peer.
     */
    public SVNClient()
    {
        cppAddr = ctNative();

        // Ensure that Subversion's config file area and templates exist.
        try
        {
            setConfigDirectory(null);
        }
        catch (ClientException suppressed)
        {
            // Not an exception-worthy problem, continue on.
        }
    }

    private long getCppAddr()
    {
        return cppAddr;
    }

    /**
     * Build the native peer
     * @return the adress of the peer
     */
    private native long ctNative();

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

    private ClientContext clientContext = new ClientContext();

    public Version getVersion()
    {
        return NativeResources.getVersion();
    }

    public RuntimeVersion getRuntimeVersion()
    {
        return NativeResources.getRuntimeVersion();
    }

    public native VersionExtended getVersionExtended(boolean verbose);

    public native String getAdminDirectoryName();

    public native boolean isAdminDirectory(String name);

    /**
      * @deprecated
      */
    @Deprecated
    public native String getLastPath();

    public native void status(String path, Depth depth,
                              boolean onServer, boolean onDisk,
                              boolean getAll, boolean noIgnore,
                              boolean ignoreExternals, boolean depthAsSticky,
                              Collection<String> changelists,
                              StatusCallback callback)
            throws ClientException;

    @Deprecated
    public void status(String path, Depth depth, boolean onServer,
                       boolean getAll, boolean noIgnore,
                       boolean ignoreExternals,
                       Collection<String> changelists,
                       StatusCallback callback)
            throws ClientException
    {
        status(path, depth, onServer, true, getAll, noIgnore,
               ignoreExternals, false, changelists, callback);
    }

    public native void list(String url, Revision revision, Revision pegRevision,
                            List<String> patterns, Depth depth, int direntFields,
                            boolean fetchLocks, boolean includeExternals,
                            ListItemCallback callback)
            throws ClientException;

    private final class ListCallbackWrapper implements ListItemCallback
    {
        private final ListCallback wrappedCallback;

        ListCallbackWrapper(ListCallback wrappedCallback)
        {
            this.wrappedCallback = wrappedCallback;
        }

        public void doEntry(DirEntry dirent, Lock lock,
                            String externalParentURL,
                            String externalTarget)
        {
            wrappedCallback.doEntry(dirent, lock);
        }
    };

    @Deprecated
    public void list(String url, Revision revision,
                     Revision pegRevision, Depth depth, int direntFields,
                     boolean fetchLocks, ListCallback callback)
            throws ClientException
    {
        list(url, revision, pegRevision, null, depth, direntFields,
             fetchLocks, false, new ListCallbackWrapper(callback));
    }

    public native void username(String username);

    public native void password(String password);

    public native void setPrompt(AuthnCallback prompt);

    @SuppressWarnings("deprecation")
    public native void setPrompt(UserPasswordCallback prompt);

    public native void setTunnelAgent(TunnelAgent tunnelAgent);

    @Deprecated
    public void logMessages(String path, Revision pegRevision,
                            List<RevisionRange> ranges, boolean stopOnCopy,
                            boolean discoverPath, boolean includeMergedRevisions,
                            Set<String> revProps, long limit,
                            LogMessageCallback callback)
            throws ClientException
    {
        logMessages(path, pegRevision, ranges, stopOnCopy, discoverPath,
                    includeMergedRevisions, revProps, false, limit, callback);
    }

    public native void logMessages(String path, Revision pegRevision,
                                   List<RevisionRange> revisionRanges,
                                   boolean stopOnCopy, boolean discoverPath,
                                   boolean includeMergedRevisions,
                                   Set<String> revProps, boolean allRevProps,
                                   long limit, LogMessageCallback callback)
            throws ClientException;

    public native long checkout(String moduleName, String destPath,
                                Revision revision, Revision pegRevision,
                                Depth depth, boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    public void notification2(ClientNotifyCallback notify)
    {
        clientContext.notify = notify;
    }

    public void setConflictResolver(ConflictResolverCallback resolver)
    {
        clientContext.resolver = resolver;
    }

    public void setProgressCallback(ProgressCallback progress)
    {
        clientContext.setProgressCallback(progress);
    }

    public native void remove(Set<String> paths, boolean force,
                              boolean keepLocal,
                              Map<String, String> revpropTable,
                              CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    public native void revert(Set<String> paths, Depth depth,
                              Collection<String> changelists,
                              boolean clearChangelists,
                              boolean metadataOnly)
            throws ClientException;

    public void revert(Set<String> paths, Depth depth,
                       Collection<String> changelists)
            throws ClientException
    {
        revert(paths, depth, changelists, false, false);
    }

    public void revert(String path, Depth depth,
                       Collection<String> changelists)
            throws ClientException
    {
        revert(Collections.singleton(path), depth, changelists, false, false);
    }

    public native void add(String path, Depth depth, boolean force,
                           boolean noIgnores, boolean noAutoProps,
                           boolean addParents)
        throws ClientException;

    public void add(String path, Depth depth, boolean force,
                    boolean noIgnores, boolean addParents)
        throws ClientException
    {
        add(path, depth, force, noIgnores, false, addParents);
    }

    public native long[] update(Set<String> paths, Revision revision,
                                Depth depth, boolean depthIsSticky,
                                boolean makeParents,
                                boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    public native void commit(Set<String> paths, Depth depth, boolean noUnlock,
                              boolean keepChangelist,
                              Collection<String> changelists,
                              Map<String, String> revpropTable,
                              CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    public native void copy(List<CopySource> sources, String destPath,
                            boolean copyAsChild, boolean makeParents,
                            boolean ignoreExternals, boolean metadataOnly,
                            boolean pinExternals,
                            Map<String, List<ExternalItem>> externalsToPin,
                            Map<String, String> revpropTable,
                            CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    public void copy(List<CopySource> sources, String destPath,
                     boolean copyAsChild, boolean makeParents,
                     boolean ignoreExternals,
                     Map<String, String> revpropTable,
                     CommitMessageCallback handler, CommitCallback callback)
            throws ClientException
    {
        copy(sources, destPath, copyAsChild, makeParents, ignoreExternals,
             false, false, null, revpropTable, handler, callback);
    }

    public native void move(Set<String> srcPaths, String destPath,
                            boolean force, boolean moveAsChild,
                            boolean makeParents, boolean metadataOnly,
                            boolean allowMixRev,
                            Map<String, String> revpropTable,
                            CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    /** @deprecated */
    @Deprecated
    public void move(Set<String> srcPaths, String destPath,
                     boolean force, boolean moveAsChild,
                     boolean makeParents,
                     Map<String, String> revpropTable,
                     CommitMessageCallback handler, CommitCallback callback)
        throws ClientException
    {
        move(srcPaths, destPath, force, moveAsChild, makeParents, false, true,
             revpropTable, handler, callback);
    }

    public native void mkdir(Set<String> paths, boolean makeParents,
                             Map<String, String> revpropTable,
                             CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    public native void cleanup(String path,
                               boolean breakLocks,
                               boolean fixRecordedTimestamps,
                               boolean clearDavCache,
                               boolean removeUnusedPristines,
                               boolean includeExternals)
        throws ClientException;

    public void cleanup(String path) throws ClientException
    {
        cleanup(path, true, true, true, true, false);
    }

    public native void resolve(String path, Depth depth,
                               ConflictResult.Choice conflictResult)
        throws SubversionException;

    public native long doExport(String srcPath, String destPath,
                                Revision revision, Revision pegRevision,
                                boolean force, boolean ignoreExternals,
                                boolean ignorKeywords,
                                Depth depth, String nativeEOL)
            throws ClientException;

    public long doExport(String srcPath, String destPath,
                                Revision revision, Revision pegRevision,
                                boolean force, boolean ignoreExternals,
                                Depth depth, String nativeEOL)
            throws ClientException
    {
        return doExport(srcPath, destPath, revision, pegRevision,
                        force, ignoreExternals, false, depth, nativeEOL);
    }

    public native long doSwitch(String path, String url, Revision revision,
                                Revision pegRevision, Depth depth,
                                boolean depthIsSticky, boolean ignoreExternals,
                                boolean allowUnverObstructions,
                                boolean ignoreAncestry)
            throws ClientException;

    public native void doImport(String path, String url, Depth depth,
                                boolean noIgnore, boolean noAutoProps,
                                boolean ignoreUnknownNodeTypes,
                                Map<String, String> revpropTable,
                                ImportFilterCallback importFilterCallback,
                                CommitMessageCallback handler,
                                CommitCallback commitCallback)
            throws ClientException;

    public void doImport(String path, String url, Depth depth, boolean noIgnore,
                         boolean ignoreUnknownNodeTypes,
                         Map<String, String> revpropTable,
                         CommitMessageCallback handler,
                         CommitCallback callback)
            throws ClientException
    {
        doImport(path, url, depth, noIgnore, false, ignoreUnknownNodeTypes,
                 revpropTable, null, handler, callback);
    }

    public native Set<String> suggestMergeSources(String path,
                                                  Revision pegRevision)
            throws SubversionException;

    public native void merge(String path1, Revision revision1, String path2,
                             Revision revision2, String localPath,
                             boolean force, Depth depth,
                             boolean ignoreMergeinfo,
                             boolean diffIgnoreAncestry,
                             boolean dryRun, boolean allowMixedRev,
                             boolean recordOnly)
            throws ClientException;

    public void merge(String path1, Revision revision1, String path2,
                      Revision revision2, String localPath,
                      boolean force, Depth depth,
                      boolean ignoreMergeinfo, boolean diffIgnoreAncestry,
                      boolean dryRun, boolean recordOnly)
            throws ClientException
    {
        merge(path1, revision1, path2, revision2, localPath, force, depth,
              ignoreMergeinfo, diffIgnoreAncestry, dryRun, true, recordOnly);
    }

    public void merge(String path1, Revision revision1, String path2,
                             Revision revision2, String localPath,
                             boolean force, Depth depth,
                             boolean ignoreAncestry, boolean dryRun,
                             boolean recordOnly)
            throws ClientException
    {
        merge(path1, revision1, path2, revision2, localPath, force, depth,
              ignoreAncestry, ignoreAncestry, dryRun, recordOnly);
    }

    public native void merge(String path, Revision pegRevision,
                             List<RevisionRange> revisions, String localPath,
                             boolean force, Depth depth,
                             boolean ignoreMergeinfo,
                             boolean diffIgnoreAncestry,
                             boolean dryRun, boolean allowMixedRev,
                             boolean recordOnly)
            throws ClientException;

    public void merge(String path, Revision pegRevision,
                      List<RevisionRange> revisions, String localPath,
                      boolean force, Depth depth,
                      boolean ignoreMergeinfo, boolean diffIgnoreAncestry,
                      boolean dryRun, boolean recordOnly)
            throws ClientException
    {
        merge(path, pegRevision, revisions, localPath, force, depth,
              ignoreMergeinfo, diffIgnoreAncestry, dryRun, true, recordOnly);
    }

    public void merge(String path, Revision pegRevision,
                      List<RevisionRange> revisions, String localPath,
                      boolean force, Depth depth, boolean ignoreAncestry,
                      boolean dryRun, boolean recordOnly)
            throws ClientException
    {
        merge(path, pegRevision, revisions, localPath, force, depth,
              ignoreAncestry, ignoreAncestry, dryRun, recordOnly);
    }


    /** @deprecated */
    @Deprecated
    public native void mergeReintegrate(String path, Revision pegRevision,
                                        String localPath, boolean dryRun)
            throws ClientException;

    public native Mergeinfo getMergeinfo(String path, Revision pegRevision)
            throws SubversionException;

    public native void getMergeinfoLog(Mergeinfo.LogKind kind, String pathOrUrl,
                                       Revision pegRevision,
                                       String mergeSourceUrl,
                                       Revision srcPegRevision,
                                       Revision srcStartRevision,
                                       Revision srcEndRevision,
                                       boolean discoverChangedPaths, Depth depth,
                                       Set<String> revProps,
                                       LogMessageCallback callback)
        throws ClientException;

    public void getMergeinfoLog(Mergeinfo.LogKind kind, String pathOrUrl,
                                Revision pegRevision,
                                String mergeSourceUrl,
                                Revision srcPegRevision,
                                boolean discoverChangedPaths, Depth depth,
                                Set<String> revProps,
                                LogMessageCallback callback)
        throws ClientException
    {
        Revision unspecified = new Revision(Revision.Kind.unspecified);
        getMergeinfoLog(kind, pathOrUrl, pegRevision, mergeSourceUrl,
                        srcPegRevision, unspecified, unspecified,
                        discoverChangedPaths, depth, revProps, callback);
    }

    public void diff(String target1, Revision revision1, String target2,
                     Revision revision2, String relativeToDir,
                     String outFileName, Depth depth,
                     Collection<String> changelists,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force, boolean copiesAsAdds)
            throws ClientException
    {
        try {
            OutputStream stream = new FileOutputStream(outFileName);
            diff(target1, revision1, target2, revision2, relativeToDir,
                 stream, depth, changelists, ignoreAncestry, noDiffDeleted,
                 force, copiesAsAdds, false, false, null);
        } catch (FileNotFoundException ex) {
            throw ClientException.fromException(ex);
        }
    }

    public void diff(String target1, Revision revision1, String target2,
                     Revision revision2, String relativeToDir,
                     OutputStream stream, Depth depth,
                     Collection<String> changelists,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force, boolean copiesAsAdds,
                     boolean ignoreProps, boolean propsOnly)
            throws ClientException
    {
        diff(target1, revision1, target2, revision2, relativeToDir,
             stream, depth, changelists, ignoreAncestry, noDiffDeleted,
             force, copiesAsAdds, ignoreProps, propsOnly, null);
    }

    public void diff(String target1, Revision revision1, String target2,
                     Revision revision2, String relativeToDir,
                     String outFileName, Depth depth,
                     Collection<String> changelists,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force, boolean copiesAsAdds,
                     boolean ignoreProps, boolean propsOnly,
                     DiffOptions options)
            throws ClientException
    {
        try {
            OutputStream stream = new FileOutputStream(outFileName);
            diff(target1, revision1, target2, revision2, relativeToDir,
                 stream, depth, changelists, ignoreAncestry, noDiffDeleted,
                 force, copiesAsAdds, ignoreProps, propsOnly, options);
        } catch (FileNotFoundException ex) {
            throw ClientException.fromException(ex);
        }
    }

    public native void diff(String target1, Revision revision1, String target2,
                            Revision revision2, String relativeToDir,
                            OutputStream stream, Depth depth,
                            Collection<String> changelists,
                            boolean ignoreAncestry, boolean noDiffDeleted,
                            boolean force, boolean copiesAsAdds,
                            boolean ignoreProps, boolean propsOnly,
                            DiffOptions options)
            throws ClientException;



    public void diff(String target, Revision pegRevision,
                     Revision startRevision, Revision endRevision,
                     String relativeToDir, String outFileName,
                     Depth depth, Collection<String> changelists,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force, boolean copiesAsAdds)
            throws ClientException
    {
        try {
            OutputStream stream = new FileOutputStream(outFileName);
            diff(target, pegRevision, startRevision, endRevision,
                 relativeToDir, stream, depth, changelists, ignoreAncestry,
                 noDiffDeleted, force, copiesAsAdds, false, false, null);
        } catch (FileNotFoundException ex) {
            throw ClientException.fromException(ex);
        }
    }

    public void diff(String target, Revision pegRevision,
                     Revision startRevision, Revision endRevision,
                     String relativeToDir, OutputStream stream,
                     Depth depth, Collection<String> changelists,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force, boolean copiesAsAdds,
                     boolean ignoreProps, boolean propsOnly)
            throws ClientException
    {
        diff(target, pegRevision, startRevision, endRevision, relativeToDir,
             stream, depth, changelists, ignoreAncestry, noDiffDeleted,
             force, copiesAsAdds, ignoreProps, propsOnly, null);
    }

    public void diff(String target, Revision pegRevision,
                     Revision startRevision, Revision endRevision,
                     String relativeToDir, String outFileName,
                     Depth depth, Collection<String> changelists,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force, boolean copiesAsAdds,
                     boolean ignoreProps, boolean propsOnly,
                     DiffOptions options)
            throws ClientException
    {
        try {
            OutputStream stream = new FileOutputStream(outFileName);
            diff(target, pegRevision, startRevision, endRevision, relativeToDir,
                 stream, depth, changelists, ignoreAncestry, noDiffDeleted,
                 force, copiesAsAdds, ignoreProps, propsOnly, options);
        } catch (FileNotFoundException ex) {
            throw ClientException.fromException(ex);
        }
    }

    public native void diff(String target, Revision pegRevision,
                            Revision startRevision, Revision endRevision,
                            String relativeToDir, OutputStream stream,
                            Depth depth, Collection<String> changelists,
                            boolean ignoreAncestry, boolean noDiffDeleted,
                            boolean force, boolean copiesAsAdds,
                            boolean ignoreProps, boolean propsOnly,
                            DiffOptions options)
            throws ClientException;

    public native void diffSummarize(String target1, Revision revision1,
                                     String target2, Revision revision2,
                                     Depth depth, Collection<String> changelists,
                                     boolean ignoreAncestry,
                                     DiffSummaryCallback receiver)
            throws ClientException;

    public native void diffSummarize(String target, Revision pegRevision,
                                     Revision startRevision,
                                     Revision endRevision, Depth depth,
                                     Collection<String> changelists,
                                     boolean ignoreAncestry,
                                     DiffSummaryCallback receiver)
            throws ClientException;

    public native void properties(String path, Revision revision,
                                  Revision pegRevision, Depth depth,
                                  Collection<String> changelists,
                                  ProplistCallback callback)
            throws ClientException;

    public native void properties(String path, Revision revision,
                                  Revision pegRevision, Depth depth,
                                  Collection<String> changelists,
                                  InheritedProplistCallback callback)
            throws ClientException;

    public native void propertySetLocal(Set<String> paths, String name,
                                        byte[] value, Depth depth,
                                        Collection<String> changelists,
                                        boolean force)
            throws ClientException;

    public native void propertySetRemote(String path, long baseRev,
                                         String name, byte[] value,
                                         CommitMessageCallback handler,
                                         boolean force,
                                         Map<String, String> revpropTable,
                                         CommitCallback callback)
            throws ClientException;

    public native byte[] revProperty(String path, String name, Revision rev)
            throws ClientException;

    public native Map<String, byte[]> revProperties(String path, Revision rev)
            throws ClientException;

    public native void setRevProperty(String path, String name, Revision rev,
                                      String value, String originalValue,
                                      boolean force)
            throws ClientException;

    public byte[] propertyGet(String path, String name,
                              Revision revision, Revision pegRevision)
            throws ClientException
    {
        return propertyGet(path, name, revision, pegRevision, null);
    }

    public native byte[] propertyGet(String path, String name,
                                     Revision revision, Revision pegRevision,
                                     Collection<String> changelists)
            throws ClientException;

    public byte[] fileContent(String path, Revision revision,
                              Revision pegRevision)
            throws ClientException
    {
        ByteArrayOutputStream stream = new ByteArrayOutputStream();

        streamFileContent(path, revision, pegRevision, true, false, stream);
        return stream.toByteArray();
    }

    public native Map<String, byte[]>
        streamFileContent(String path,
                          Revision revision, Revision pegRevision,
                          boolean expandKeywords, boolean returnProps,
                          OutputStream stream)
            throws ClientException;

    public void streamFileContent(String path, Revision revision,
                                  Revision pegRevision,
                                  OutputStream stream)
        throws ClientException
    {
        streamFileContent(path, revision, pegRevision, true, false, stream);
    }

    public native void relocate(String from, String to, String path,
                                boolean ignoreExternals)
            throws ClientException;

    public void blame(String path, Revision pegRevision,
                      Revision revisionStart,
                      Revision revisionEnd, boolean ignoreMimeType,
                      boolean includeMergedRevisions,
                      BlameCallback callback)
            throws ClientException
    {
        blame(path, pegRevision, revisionStart, revisionEnd, ignoreMimeType,
              includeMergedRevisions, callback, null);
    }

    public native void blame(String path, Revision pegRevision,
                             Revision revisionStart,
                             Revision revisionEnd, boolean ignoreMimeType,
                             boolean includeMergedRevisions,
                             BlameCallback callback,
                             DiffOptions options)
            throws ClientException;

    public native void setConfigDirectory(String configDir)
            throws ClientException;

    public native String getConfigDirectory()
            throws ClientException;

    public native void setConfigEventHandler(ConfigEvent configHandler)
            throws ClientException;

    public native ConfigEvent getConfigEventHandler()
            throws ClientException;

    public native void cancelOperation()
            throws ClientException;

    public native void addToChangelist(Set<String> paths, String changelist,
                                       Depth depth,
                                       Collection<String> changelists)
            throws ClientException;

    public native void removeFromChangelists(Set<String> paths, Depth depth,
                                             Collection<String> changelists)
            throws ClientException;

    public native void getChangelists(String rootPath,
                                      Collection<String> changelists,
                                      Depth depth, ChangelistCallback callback)
            throws ClientException;

    public native String getVersionInfo(String path, String trailUrl,
                                        boolean lastChanged)
            throws ClientException;

    public native void upgrade(String path)
            throws ClientException;

    /**
     * Enable logging in the JNI-code
     * @param logLevel      the level of information to log (See
     *                      ClientLogLevel)
     * @param logFilePath   path of the log file
     */
    public static native void enableLogging(ClientLogLevel logLevel,
                                            String logFilePath);

    /**
     * enum for the constants of the logging levels.
     */
    public enum ClientLogLevel
    {
        /** Log nothing */
        NoLog,

        /** Log fatal error */
        ErrorLog,

        /** Log exceptions thrown */
        ExceptionLog,

        /** Log the entry and exits of the JNI code */
        EntryLog;
    }

    /**
     * Returns version information of subversion and the javahl binding
     * @return version information
     */
    public static native String version();

    /**
     * Returns the major version of the javahl binding. Same version of the
     * javahl support the same interfaces
     * @return major version number
     */
    public static native int versionMajor();

    /**
     * Returns the minor version of the javahl binding. Same version of the
     * javahl support the same interfaces
     * @return minor version number
     */
    public static native int versionMinor();

    /**
     * Returns the micro (patch) version of the javahl binding. Same version of
     * the javahl support the same interfaces
     * @return micro version number
     */
    public static native int versionMicro();

    public native void lock(Set<String> paths, String comment, boolean force)
            throws ClientException;

    public native void unlock(Set<String> paths, boolean force)
            throws ClientException;

    public native void info(String pathOrUrl, Revision revision,
                            Revision pegRevision, Depth depth,
                            boolean fetchExcluded, boolean fetchActualOnly,
                            boolean includeExternals,
                            Collection<String> changelists,
                            InfoCallback callback)
            throws ClientException;

    public void info2(String pathOrUrl, Revision revision,
                      Revision pegRevision, Depth depth,
                      Collection<String> changelists,
                      InfoCallback callback)
            throws ClientException
    {
        info(pathOrUrl, revision, pegRevision, depth,
             false, true, false, changelists, callback);
    }

    public native void patch(String patchPath, String targetPath,
                             boolean dryRun, int stripCount, boolean reverse,
                             boolean ignoreWhitespace, boolean removeTempfiles,
                             PatchCallback callback)
            throws ClientException;

    public native void vacuum(String wcPath,
                              boolean removeUnversionedItems,
                              boolean removeIgnoredItems,
                              boolean fixRecordedTimestamps,
                              boolean removeUnusedPristines,
                              boolean includeExternals)
            throws ClientException;

    public ISVNRemote openRemoteSession(String pathOrUrl)
            throws ClientException, SubversionException
    {
        return nativeOpenRemoteSession(pathOrUrl, 1);
    }

    public ISVNRemote openRemoteSession(String pathOrUrl, int retryAttempts)
            throws ClientException, SubversionException
    {
        if (retryAttempts <= 0)
            throw new IllegalArgumentException(
                "retryAttempts must be positive");
        return nativeOpenRemoteSession(pathOrUrl, retryAttempts);
    }

    private native ISVNRemote nativeOpenRemoteSession(
        String pathOrUrl, int retryAttempts)
            throws ClientException, SubversionException;

    /**
     * A private class to hold the contextual information required to
     * persist in this object, such as notification handlers.
     */
    private class ClientContext extends OperationContext
        implements ClientNotifyCallback, ConflictResolverCallback
    {
        public ClientNotifyCallback notify = null;
        public ConflictResolverCallback resolver = null;

        public void onNotify(ClientNotifyInformation notifyInfo)
        {
            if (notify != null)
                notify.onNotify(notifyInfo);
        }

        public ConflictResult resolve(ConflictDescriptor conflict)
            throws SubversionException
        {
            if (resolver != null)
                return resolver.resolve(conflict);
            else
                return new ConflictResult(ConflictResult.Choice.postpone,
                                          null);
        }
    }
}
