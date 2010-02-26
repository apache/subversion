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

import java.io.OutputStream;

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
     * Standard empty contructor, builds just the native peer.
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

    /** Don't call this function!  Public fucntion for backward compat reasons
      */
    public long getCppAddr()
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

    /**
     * @since 1.0
     */
    public Version getVersion()
    {
        return NativeResources.version;
    }

    /**
     * @since 1.3
     */
    public native String getAdminDirectoryName();

    /**
     * @since 1.3
     */
    public native boolean isAdminDirectory(String name);

    /**
      * @deprecated
      * @since 1.0
      */
    public native String getLastPath();

    /**
     * @since 1.5
     */
    public native void status(String path, int depth, boolean onServer,
                              boolean getAll, boolean noIgnore,
                              boolean ignoreExternals, String[] changelists,
                              StatusCallback callback)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void list(String url, Revision revision,
                            Revision pegRevision, int depth, int direntFields,
                            boolean fetchLocks, ListCallback callback)
            throws ClientException;

    /**
     * @since 1.0
     */
    public native void username(String username);

    /**
     * @since 1.0
     */
    public native void password(String password);

    /**
     * @since 1.0
     */
    public native void setPrompt(UserPasswordCallback prompt);

    /**
     * @since 1.6
     */
    public native void logMessages(String path,
                                   Revision pegRevision,
                                   RevisionRange[] revisionRanges,
                                   boolean stopOnCopy,
                                   boolean discoverPath,
                                   boolean includeMergedRevisions,
                                   String[] revProps,
                                   long limit,
                                   LogMessageCallback callback)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native long checkout(String moduleName, String destPath,
                                Revision revision, Revision pegRevision,
                                int depth, boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    /**
     * @since 1.0
     * @deprecated
     */
    public native void notification(Notify notify);

    /**
     * @since 1.2
     */
    public native void notification2(NotifyCallback notify);

    /**
     * @since 1.5
     */
    public native void setConflictResolver(ConflictResolverCallback listener);

    /**
     * @since 1.5
     */
    public native void setProgressCallback(ProgressCallback listener);

    /**
     * @since 1.0
     */
    public native void commitMessageHandler(CommitMessage messageHandler);

    /**
     * @since 1.5
     */
    public native void remove(String[] path, String message, boolean force,
                              boolean keepLocal,
                              Map<String, String> revpropTable)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void revert(String path, int depth, String[] changelists)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void add(String path, int depth, boolean force,
                           boolean noIgnores, boolean addParents)
        throws ClientException;

    /**
     * @since 1.5
     */
    public long update(String path, Revision revision, int depth,
                       boolean depthIsSticky, boolean ignoreExternals,
                       boolean allowUnverObstructions)
            throws ClientException
    {
        return update(new String[]{path}, revision, depth, depthIsSticky,
                      ignoreExternals, allowUnverObstructions)[0];
    }

    /**
     * @since 1.5
     */
    public native long[] update(String[] path, Revision revision,
                                int depth, boolean depthIsSticky,
                                boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native long commit(String[] path, String message, int depth,
                              boolean noUnlock, boolean keepChangelist,
                              String[] changelists,
                              Map<String, String> revpropTable)
            throws ClientException;

    /**
     * @since 1.7
     */
    public native void copy(CopySource[] sources, String destPath,
                            String message, boolean copyAsChild,
                            boolean makeParents, boolean ignoreExternals,
                            Map<String, String> revpropTable)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void move(String[] srcPaths, String destPath, String message,
                            boolean force, boolean moveAsChild,
                            boolean makeParents,
                            Map<String, String> revpropTable)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void mkdir(String[] path, String message,
                             boolean makeParents,
                             Map<String, String> revpropTable)
            throws ClientException;

    /**
     * @since 1.0
     */
    public native void cleanup(String path)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void resolve(String path, int depth, int conflictResult)
        throws SubversionException;

    /**
     * @since 1.5
     */
    public native long doExport(String srcPath, String destPath,
                                Revision revision, Revision pegRevision,
                                boolean force, boolean ignoreExternals,
                                int depth, String nativeEOL)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native long doSwitch(String path, String url, Revision revision,
                                Revision pegRevision, int depth,
                                boolean depthIsSticky, boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void doImport(String path, String url, String message,
                                int depth, boolean noIgnore,
                                boolean ignoreUnknownNodeTypes,
                                Map<String, String> revpropTable)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native String[] suggestMergeSources(String path,
                                               Revision pegRevision)
            throws SubversionException;

    /**
     * @since 1.5
     */
    public native void merge(String path1, Revision revision1, String path2,
                             Revision revision2, String localPath,
                             boolean force, int depth,
                             boolean ignoreAncestry, boolean dryRun,
                             boolean recordOnly)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void merge(String path, Revision pegRevision,
                             RevisionRange[] revisions, String localPath,
                             boolean force, int depth, boolean ignoreAncestry,
                             boolean dryRun, boolean recordOnly)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void mergeReintegrate(String path, Revision pegRevision,
                                        String localPath, boolean dryRun)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native Mergeinfo getMergeinfo(String path, Revision pegRevision)
            throws SubversionException;

    /**
     * @since 1.7
     */
    public native void getMergeinfoLog(int kind, String pathOrUrl,
                                       Revision pegRevision,
                                       String mergeSourceUrl,
                                       Revision srcPegRevision,
                                       boolean discoverChangedPaths,
                                       int depth,
                                       String[] revprops,
                                       LogMessageCallback callback)
        throws ClientException;

    /**
     * @since 1.7
     */
    public native void diff(String target1, Revision revision1, String target2,
                            Revision revision2, String relativeToDir,
                            String outFileName, int depth,
                            String[] changelists, boolean ignoreAncestry,
                            boolean noDiffDeleted, boolean force,
                            boolean copiesAsAdds)
            throws ClientException;

    /**
     * @since 1.7
     */
    public native void diff(String target, Revision pegRevision,
                            Revision startRevision, Revision endRevision,
                            String relativeToDir, String outFileName,
                            int depth, String[] changelists,
                            boolean ignoreAncestry, boolean noDiffDeleted,
                            boolean force, boolean copiesAsAdds)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void diffSummarize(String target1, Revision revision1,
                                     String target2, Revision revision2,
                                     int depth, String[] changelists,
                                     boolean ignoreAncestry,
                                     DiffSummaryCallback receiver)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void diffSummarize(String target, Revision pegRevision,
                                     Revision startRevision,
                                     Revision endRevision,
                                     int depth, String[] changelists,
                                     boolean ignoreAncestry,
                                     DiffSummaryCallback receiver)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void properties(String path, Revision revision,
                                  Revision pegRevision, int depth,
                                  String[] changelists,
                                  ProplistCallback callback)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void propertySet(String path, String name, String value,
                                   int depth, String[] changelists,
                                   boolean force,
                                   Map<String, String> revpropTable)
            throws ClientException;

    /**
     * @since 1.5
     */
    public void propertyRemove(String path, String name, int depth,
                               String[] changelists)
            throws ClientException
    {
        propertySet(path, name, null, depth, changelists, false, null);
    }

    /**
     * @since 1.5
     */
    public void propertyCreate(String path, String name, String value,
                               int depth, String[] changelists, boolean force)
            throws ClientException
    {
        propertySet(path, name, value, depth, changelists, force, null);
    }

    /**
     * @since 1.0
     */
    public native PropertyData revProperty(String path, String name,
                                           Revision rev)
            throws ClientException;

    /**
     * @since 1.2
     */
    public native PropertyData[] revProperties(String path, Revision rev)
            throws ClientException;

    /**
     * @since 1.6
     */
    public native void setRevProperty(String path, String name, Revision rev,
                                      String value, String originalValue,
                                      boolean force)
            throws ClientException;

    /**
     * @since 1.2
     */
    public PropertyData propertyGet(String path, String name,
                                    Revision revision)
            throws ClientException
    {
        return propertyGet(path, name, revision, revision);
    }

    /**
     * @since 1.2
     */
    public native PropertyData propertyGet(String path, String name,
                                           Revision revision,
                                           Revision pegRevision)
            throws ClientException;

    /**
     * @since 1.2
     */
    public native byte[] fileContent(String path, Revision revision,
                                     Revision pegRevision)
            throws ClientException;

    /**
     * @since 1.0
     */
    public native void streamFileContent(String path, Revision revision,
                                         Revision pegRevision, int bufferSize,
                                         OutputStream stream)
            throws ClientException;

    /**
     * @since 1.0
     */
    public native void relocate(String from, String to, String path,
                                boolean recurse)
            throws ClientException;

    /**
     * @since 1.7
     */
    public native void blame(String path, Revision pegRevision,
                             Revision revisionStart,
                             Revision revisionEnd, boolean ignoreMimeType,
                             boolean includeMergedRevisions,
                             BlameCallback callback)
            throws ClientException;

    /**
     * @since 1.0
     */
    public native void setConfigDirectory(String configDir)
            throws ClientException;

    /**
     * @since 1.0
     */
    public native String getConfigDirectory()
            throws ClientException;

    /**
     * @since 1.0
     */
    public native void cancelOperation()
            throws ClientException;

    /**
     * @deprecated Use {@link #info2(String, Revision, Revision, int, String[],
     *                               InfoCallback)} instead.
     * @since 1.0
     */
    public native Info info(String path)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void addToChangelist(String[] paths, String changelist,
                                       int depth, String[] changelists)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void removeFromChangelists(String[] paths, int depth,
                                             String[] changelists)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void getChangelists(String rootPath, String[] changelists,
                                      int depth, ChangelistCallback callback)
            throws ClientException;

    /**
     * @since 1.2
     */
    public native String getVersionInfo(String path, String trailUrl,
                                        boolean lastChanged)
            throws ClientException;

    /**
     * @since 1.7
     */
    public native void upgrade(String path)
            throws ClientException;

    /**
     * Enable logging in the JNI-code
     * @param logLevel      the level of information to log (See
     *                      ClientLogLevel)
     * @param logFilePath   path of the log file
     */
    public static native void enableLogging(int logLevel, String logFilePath);

    /**
     * class for the constants of the logging levels.
     * The constants are defined in ClientLogLevel because of building
     * reasons
     */
    public static final class LogLevel implements ClientLogLevel
    {
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

    /**
     * @since 1.2
     */
    public native void lock(String[] path, String comment, boolean force)
            throws ClientException;

    /**
     * @since 1.2
     */
    public native void unlock(String[] path, boolean force)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void info2(String pathOrUrl, Revision revision,
                             Revision pegRevision, int depth,
                             String[] changelists, InfoCallback callback)
            throws ClientException;
}
