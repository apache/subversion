/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 * @endcopyright
 */

package org.tigris.subversion.javahl;

import java.io.OutputStream;

import java.util.Map;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Date;
import java.text.ParseException;

/**
 * This is the main client class.  All Subversion client APIs are
 * implemented in this class.  This class is not threadsafe; if you
 * need threadsafe access, use SVNClientSynchronized.
 */
public class SVNClient implements SVNClientInterface
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
    protected native void finalize();

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
     * @deprecated Use {@link #status(String, int, boolean, boolean,
     *                                boolean, boolean, String[],
     *                                StatusCallback)} instead.
     * @since 1.0
     */
    public Status singleStatus(String path, boolean onServer)
            throws ClientException
    {
        Status[] statusArray = status(path, false, onServer, true, false, false);
        if (statusArray == null || statusArray.length == 0)
            return null;
        return status(path, false, onServer, true, false, false)[0];
    }

    /**
     * @deprecated Use {@link #status(String, int, boolean, boolean,
     *                                boolean, boolean, String[],
     *                                StatusCallback)} instead.
     * @since 1.0
     */
    public Status[] status(String path, boolean descend, boolean onServer,
                           boolean getAll)
            throws ClientException
    {
        return status(path, descend, onServer, getAll, false);
    }

    /**
     * @deprecated Use {@link #status(String, int, boolean, boolean,
     *                                boolean, boolean, String[],
     *                                StatusCallback)} instead.
     * @since 1.0
     */
    public Status[] status(String path, boolean descend,
                           boolean onServer, boolean getAll,
                           boolean noIgnore)
            throws ClientException
    {
        return status(path, descend, onServer, getAll, noIgnore, false);
    }

    /**
     * @deprecated Use {@link #status(String, int, boolean, boolean,
     *                                boolean, boolean, String[],
     *                                StatusCallback)} instead.
     * @since 1.2
     */
    public Status[] status(String path, boolean descend, boolean onServer,
                           boolean getAll, boolean noIgnore,
                           boolean ignoreExternals)
            throws ClientException
    {
        MyStatusCallback callback = new MyStatusCallback();

        status(path, Depth.unknownOrImmediates(descend), onServer, getAll,
               noIgnore, ignoreExternals, null, callback);

        return callback.getStatusArray();
    }

    /**
     * @since 1.5
     */
    public native void status(String path, int depth, boolean onServer,
                              boolean getAll, boolean noIgnore,
                              boolean ignoreExternals, String[] changelists,
                              StatusCallback callback)
            throws ClientException;

    /**
     * @deprecated Use {@link #list(String, Revision, Revision, int, int,
     *                              boolean, ListCallback)} instead.
     * @since 1.0
     */
    public DirEntry[] list(String url, Revision revision, boolean recurse)
            throws ClientException
    {
        return list(url, revision, revision, recurse);
    }

    /**
     * @deprecated Use {@link #list(String, Revision, Revision, int, int,
     *                              boolean, ListCallback)} instead.
     * @since 1.2
     */
    public DirEntry[] list(String url, Revision revision,
                                  Revision pegRevision, boolean recurse)
            throws ClientException
    {
        MyListCallback callback = new MyListCallback();

        list(url, revision, pegRevision, Depth.infinityOrImmediates(recurse),
             DirEntry.Fields.all, false, callback);

        return callback.getDirEntryArray();
    }

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
    public native void setPrompt(PromptUserPassword prompt);

    /**
     * @deprecated Use {@link #logMessages(String, Revision, Revision, Revision,
     *                                     boolean, boolean, boolean, String[],
     *                                     long, LogMessageCallback)} instead.
     * @since 1.0
     */
    public LogMessage[] logMessages(String path, Revision revisionStart,
                                    Revision revisionEnd)
            throws ClientException
    {
        return logMessages(path, revisionStart, revisionEnd, true, false);
    }

    /**
     * @deprecated Use {@link #logMessages(String, Revision, Revision, Revision,
     *                                     boolean, boolean, boolean, String[],
     *                                     long, LogMessageCallback)} instead.
     * @since 1.0
     */
    public LogMessage[] logMessages(String path, Revision revisionStart,
                                    Revision revisionEnd, boolean stopOnCopy)
            throws ClientException
    {
        return logMessages(path, revisionStart, revisionEnd,
                           stopOnCopy, false);
    }

    /**
     * @deprecated Use {@link #logMessages(String, Revision, Revision, Revision,
     *                                     boolean, boolean, boolean, String[],
     *                                     long, LogMessageCallback)} instead.
     * @since 1.0
     */
    public LogMessage[] logMessages(String path, Revision revisionStart,
                                    Revision revisionEnd,
                                    boolean stopOnCopy,
                                    boolean discoverPath)
            throws ClientException
    {
        return logMessages(path, revisionStart, revisionEnd, stopOnCopy,
                           discoverPath, 0);
    }

    /**
     * @deprecated Use {@link #logMessages(String, Revision, Revision, Revision,
     *                                     boolean, boolean, boolean, String[],
     *                                     long, LogMessageCallback)} instead.
     * @since 1.2
     */
    public LogMessage[] logMessages(String path, Revision revisionStart,
                                    Revision revisionEnd,
                                    boolean stopOnCopy,
                                    boolean discoverPath,
                                    long limit)
            throws ClientException
    {
        MyLogMessageCallback callback = new MyLogMessageCallback();
        String[] revProps = { "svn:log", "svn:date", "svn:author" };

        logMessages(path, revisionEnd, revisionStart, revisionEnd,
                    stopOnCopy, discoverPath, false, revProps, limit, callback);

        return callback.getMessages();
    }

    /**
     * @deprecated Use {@link #logMessages(String, Revision, RevisionRange[],
     *                                     boolean, boolean, boolean, String[],
     *                                     long, LogMessageCallback)} instead.
     * @since 1.5
     */
    public void logMessages(String path,
                            Revision pegRevision,
                            Revision revisionStart,
                            Revision revisionEnd,
                            boolean stopOnCopy,
                            boolean discoverPath,
                            boolean includeMergedRevisions,
                            String[] revProps,
                            long limit,
                            LogMessageCallback callback)
            throws ClientException
    {
        logMessages(path, pegRevision, toRevisionRange(revisionStart,
                                                       revisionEnd), stopOnCopy,
                    discoverPath, includeMergedRevisions, revProps, limit,
                    callback);
    }

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
     * @deprecated Use {@link #checkout(String, String, Revision, Revision,
     *                                  int, boolean, boolean)} instead.
     * @since 1.0
     */
    public long checkout(String moduleName, String destPath,
                         Revision revision, boolean recurse)
            throws ClientException
    {
        return checkout(moduleName, destPath, revision, revision, recurse,
                        false);
    }

    /**
     * @deprecated Use {@link #checkout(String, String, Revision, Revision,
     *                                  int, boolean, boolean)} instead.
     * @since 1.2
     */
    public long checkout(String moduleName, String destPath,
                         Revision revision, Revision pegRevision,
                         boolean recurse, boolean ignoreExternals)
            throws ClientException
    {
        return checkout(moduleName, destPath, revision, revision,
                        Depth.infinityOrFiles(recurse), ignoreExternals,
                        false);
    }

    /**
     * @since 1.5
     */
    public native long checkout(String moduleName, String destPath,
                                Revision revision, Revision pegRevision,
                                int depth, boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    /**
     * @deprecated Use {@link #notification2(Notify2)} instead.
     * @since 1.0
     */
    public native void notification(Notify notify);

    /**
     * @since 1.2
     */
    public native void notification2(Notify2 notify);

    /**
     * @since 1.5
     */
    public native void setConflictResolver(ConflictResolverCallback listener);

    /**
     * @since 1.5
     */
    public native void setProgressListener(ProgressListener listener);

    /**
     * @since 1.0
     */
    public native void commitMessageHandler(CommitMessage messageHandler);

    /**
     * @deprecated Use {@link #remove(String[], String, boolean, boolean, Map)}
     *             instead.
     * @since 1.0
     */
    public void remove(String[] path, String message, boolean force)
            throws ClientException
    {
        remove(path, message, force, false, null);
    }

    /**
     * @since 1.5
     */
    public native void remove(String[] path, String message, boolean force,
                              boolean keepLocal, Map revpropTable)
            throws ClientException;

    /**
     * @deprecated Use {@link #revert(String, int, String[])} instead.
     * @since 1.0
     */
    public void revert(String path, boolean recurse)
            throws ClientException
    {
        revert(path, Depth.infinityOrEmpty(recurse), null);
    }

    /**
     * @since 1.5
     */
    public native void revert(String path, int depth, String[] changelists)
            throws ClientException;

    /**
     * @deprecated Use {@link #add(String, int, boolean, boolean, boolean)}
     *             instead.
     * @since 1.0
     */
    public void add(String path, boolean recurse)
            throws ClientException
    {
        add(path, recurse, false);
    }

    /**
     * @deprecated Use {@link #add(String, int, boolean, boolean, boolean)}
     *             instead.
     * @since 1.2
     */
    public void add(String path, boolean recurse, boolean force)
            throws ClientException
    {
        add(path, Depth.infinityOrEmpty(recurse), force, false, false);
    }

    /**
     * @since 1.5
     */
    public native void add(String path, int depth, boolean force,
                           boolean noIgnores, boolean addParents)
        throws ClientException;

    /**
     * @deprecated Use {@link #update(String[], Revision, int, boolean,
     *                                boolean, boolean)} instead.
     * @since 1.0
     */
    public long update(String path, Revision revision, boolean recurse)
            throws ClientException
    {
        return update(new String[]{path}, revision, recurse, false)[0];
    }

    /**
     * @deprecated Use {@link #update(String[], Revision, int, boolean,
     *                                boolean, boolean)} instead.
     * @since 1.2
     */
    public long[] update(String[] path, Revision revision,
                         boolean recurse, boolean ignoreExternals)
            throws ClientException
    {
        return update(path, revision, Depth.unknownOrFiles(recurse), false,
                      ignoreExternals, false);
    }

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
     * @deprecated Use {@link #commit(String[], String, int, boolean, boolean,
     *                                String[], Map)} instead.
     * @since 1.0
     */
    public long commit(String[] path, String message, boolean recurse)
            throws ClientException
    {
        return commit(path, message, recurse, false);
    }

    /**
     * @deprecated Use {@link #commit(String[], String, int, boolean, boolean,
     *                                String[], Map)} instead.
     * @since 1.2
     */
    public long commit(String[] path, String message, boolean recurse,
                       boolean noUnlock)
            throws ClientException
    {
        return commit(path, message, Depth.infinityOrEmpty(recurse), noUnlock,
                      false, null, null);
    }

    /**
     * @since 1.5
     */
    public native long commit(String[] path, String message, int depth,
                              boolean noUnlock, boolean keepChangelist,
                              String[] changelists, Map revpropTable)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void copy(CopySource[] sources, String destPath,
                            String message, boolean copyAsChild,
                            boolean makeParents, Map revpropTable)
            throws ClientException;

    /**
     * @deprecated Use {@link #copy(CopySource[], String, String, boolean,
     *                              boolean, Map)} instead.
     * @since 1.0
     */
    public void copy(String srcPath, String destPath, String message,
                     Revision revision)
            throws ClientException
    {
        copy(new CopySource[] { new CopySource(srcPath, revision,
                                               Revision.HEAD) },
             destPath, message, true, false, null);
    }

    /**
     * @since 1.5
     */
    public native void move(String[] srcPaths, String destPath, String message,
                            boolean force, boolean moveAsChild,
                            boolean makeParents, Map revpropTable)
            throws ClientException;

    /**
     * @deprecated Use {@link #move(String[], String, String, boolean, boolean,
     *                              boolean, Map)} instead.
     * @since 1.2
     */
    public void move(String srcPath, String destPath, String message,
                     Revision ignored, boolean force)
            throws ClientException
    {
        move(new String[] { srcPath }, destPath, message, force, true, false,
             null);
    }

    /**
     * @deprecated Use {@link #move(String[], String, String, boolean, boolean,
     *                              boolean, Map)} instead.
     * @since 1.0
     */
    public void move(String srcPath, String destPath, String message,
                     boolean force)
            throws ClientException
    {
        move(new String[] { srcPath }, destPath, message, force, true, false,
             null);
    }

    /**
     * @since 1.5
     */
    public native void mkdir(String[] path, String message,
                             boolean makeParents, Map revpropTable)
            throws ClientException;

    /**
     * @deprecated Use {@link #mkdir(String[], String, boolean, Map)} instead.
     * @since 1.0
     */
    public void mkdir(String[] path, String message)
            throws ClientException
    {
        mkdir(path, message, false, null);
    }

    /**
     * @since 1.0
     */
    public native void cleanup(String path)
            throws ClientException;

    /**
     * @deprecated Use {@link #resolve(String, int, int)} instead.
     * @since 1.0
     */
    public void resolved(String path, boolean recurse)
        throws ClientException
    {
        try
        {
            resolve(path, Depth.infinityOrEmpty(recurse),
                    ConflictResult.chooseMerged);
        }
        catch (SubversionException e)
        {
            throw ClientException.fromException(e);
        }
    }

    /**
     * @since 1.5
     */
    public native void resolve(String path, int depth, int conflictResult)
        throws SubversionException;

    /**
     * @deprecated Use {@link #doExport(String, String, Revision, Revision,
     *                                  boolean, boolean, int, String)} instead.
     * @since 1.0
     */
    public long doExport(String srcPath, String destPath,
                                Revision revision, boolean force)
            throws ClientException
    {
        return doExport(srcPath, destPath, revision, revision, force,
                false, true, null);
    }

    /**
     * @deprecated Use {@link #doExport(String, String, Revision, Revision,
     *                                  boolean, boolean, int, String)} instead.
     * @since 1.2
     */
    public long doExport(String srcPath, String destPath, Revision revision,
                         Revision pegRevision, boolean force,
                         boolean ignoreExternals, boolean recurse,
                         String nativeEOL)
            throws ClientException
    {
        return doExport(srcPath, destPath, revision, pegRevision, force,
                        ignoreExternals, Depth.infinityOrFiles(recurse),
                        nativeEOL);
    }

    /**
     * @since 1.5
     */
    public native long doExport(String srcPath, String destPath,
                                Revision revision, Revision pegRevision,
                                boolean force, boolean ignoreExternals,
                                int depth, String nativeEOL)
            throws ClientException;

    /**
     * @deprecated Use {@link #doSwitch(String, String, Revision, boolean)}
     *             instead.
     * @since 1.0
     */
    public long doSwitch(String path, String url, Revision revision,
                         boolean recurse)
            throws ClientException
    {
        return doSwitch(path, url, revision, Revision.HEAD,
                        Depth.unknownOrFiles(recurse), false, false, false);
    }

    /**
     * @since 1.5
     */
    public native long doSwitch(String path, String url, Revision revision,
                                Revision pegRevision, int depth,
                                boolean depthIsSticky, boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    /**
     * @deprecated Use {@link #doImport(String, String, String, int, boolean,
     *                                  boolean, Map)} instead.
     * @since 1.0
     */
    public void doImport(String path, String url, String message,
                         boolean recurse)
            throws ClientException
    {
        doImport(path, url, message, Depth.infinityOrFiles(recurse),
                 false, false, null);
    }

    /**
     * @since 1.5
     */
    public native void doImport(String path, String url, String message,
                                int depth, boolean noIgnore,
                                boolean ignoreUnknownNodeTypes,
                                Map revpropTable)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native String[] suggestMergeSources(String path,
                                               Revision pegRevision)
            throws SubversionException;

    /**
     * @deprecated Use {@link #merge(String, Revision, String, Revision,
     *                               String, boolean, int, boolean,
     *                               boolean, boolean)} instead.
     * @since 1.0
     */
    public void merge(String path1, Revision revision1, String path2,
                      Revision revision2, String localPath,
                      boolean force, boolean recurse)
            throws ClientException
    {
        merge(path1, revision1, path2, revision2, localPath, force, recurse,
              false, false);
    }

    /**
     * @deprecated Use {@link #merge(String, Revision, String, Revision,
     *                               String, boolean, int, boolean,
     *                               boolean, boolean)} instead.
     * @since 1.2
     */
    public void merge(String path1, Revision revision1, String path2,
                      Revision revision2, String localPath, boolean force,
                      boolean recurse, boolean ignoreAncestry, boolean dryRun)
            throws ClientException
    {
        merge(path1, revision1, path2, revision2, localPath, force,
              Depth.infinityOrFiles(recurse), ignoreAncestry, dryRun, false);
    }

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
     * @deprecated Use {@link #merge(String, Revision, RevisionRange[],
     *                               String, boolean, int, boolean,
     *                               boolean, boolean)} instead.
     * @since 1.2
     */
    public void merge(String path, Revision pegRevision, Revision revision1,
                      Revision revision2, String localPath, boolean force,
                      boolean recurse, boolean ignoreAncestry, boolean dryRun)
           throws ClientException
    {
        merge(path, pegRevision, toRevisionRange(revision1, revision2),
              localPath, force, Depth.infinityOrFiles(recurse), ignoreAncestry,
              dryRun, false);
    }

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
     * @since 1.5
     */
    public native void getMergeinfoLog(int kind, String pathOrUrl,
                                       Revision pegRevision,
                                       String mergeSourceUrl,
                                       Revision srcPegRevision,
                                       boolean discoverChangedPaths,
                                       String[] revprops,
                                       LogMessageCallback callback)
        throws ClientException;

    /**
     * @deprecated Use {@link #diff(String, Revision, String, Revision,
     *                              String, String, int, String[], boolean,
     *                              boolean, boolean)} instead.
     * @since 1.0
     */
    public void diff(String target1, Revision revision1, String target2,
                     Revision revision2, String outFileName,
                     boolean recurse)
            throws ClientException
    {
        diff(target1, revision1, target2, revision2, outFileName, recurse,
             true, false, false);
    }

    /**
     * @deprecated Use {@link #diff(String, Revision, String, Revision,
     *                              String, String, int, String[], boolean,
     *                              boolean, boolean)} instead.
     * @since 1.2
     */
    public void diff(String target1, Revision revision1, String target2,
                     Revision revision2, String outFileName, boolean recurse,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force)
            throws ClientException
    {
        diff(target1, revision1, target2, revision2, null, outFileName,
             Depth.unknownOrFiles(recurse), null, ignoreAncestry, noDiffDeleted,
             force);
    }

    /**
     * @since 1.5
     */
    public native void diff(String target1, Revision revision1, String target2,
                            Revision revision2, String relativeToDir,
                            String outFileName, int depth, String[] changelists,
                            boolean ignoreAncestry, boolean noDiffDeleted,
                            boolean force)
            throws ClientException;

    /**
     * @deprecated Use {@link #diff(String, Revision, Revision, Revision,
     *                              String, String, int, String[], boolean,
     *                              boolean, boolean)} instead.
     * @since 1.2
     */
    public void diff(String target, Revision pegRevision,
                     Revision startRevision, Revision endRevision,
                     String outFileName, boolean recurse,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force)
            throws ClientException
    {
        diff(target, pegRevision, startRevision, endRevision, null,
             outFileName, Depth.unknownOrFiles(recurse), null, ignoreAncestry,
             noDiffDeleted, force);
    }

    /**
     * @since 1.5
     */
    public native void diff(String target, Revision pegRevision,
                            Revision startRevision, Revision endRevision,
                            String relativeToDir, String outFileName,
                            int depth, String[] changelists,
                            boolean ignoreAncestry, boolean noDiffDeleted,
                            boolean force)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void diffSummarize(String target1, Revision revision1,
                                     String target2, Revision revision2,
                                     int depth, String[] changelists,
                                     boolean ignoreAncestry,
                                     DiffSummaryReceiver receiver)
            throws ClientException;

    /**
     * @since 1.5
     */
    public native void diffSummarize(String target, Revision pegRevision,
                                     Revision startRevision,
                                     Revision endRevision,
                                     int depth, String[] changelists,
                                     boolean ignoreAncestry,
                                     DiffSummaryReceiver receiver)
            throws ClientException;

    /**
     * @deprecated Use {@link #properties(String, Revision, Revision,
     *                                    int, String[], ProplistCallback)}
     *             instead.
     * @since 1.0
     */
    public PropertyData[] properties(String path) throws ClientException
    {
        return properties(path, null);
    }

    /**
     * @deprecated Use {@link #properties(String, Revision, Revision,
     *                                    int, String[], ProplistCallback)}
     *             instead.
     * @since 1.2
     */
    public PropertyData[] properties(String path, Revision revision)
            throws ClientException
    {
        return properties(path, revision, revision);
    }

    /**
     * @deprecated Use {@link #properties(String, Revision, Revision,
     *                                    int, String[], ProplistCallback)}
     *             instead.
     * @since 1.2
     */
    public PropertyData[] properties(String path, Revision revision,
                                     Revision pegRevision)
            throws ClientException
    {
        ProplistCallbackImpl callback = new ProplistCallbackImpl();
        properties(path, revision, pegRevision, Depth.empty, null, callback);

        Map propMap = callback.getProperties(path);
        if (propMap == null)
            return new PropertyData[0];
        PropertyData[] props = new PropertyData[propMap.size()];

        Iterator it = propMap.keySet().iterator();
        int i = 0;

        while (it.hasNext())
        {
            String key = (String) it.next();
            props[i] = new PropertyData(path, key, (String) propMap.get(key));
            i++;
        }

        return props;
    }

    /**
     * @since 1.5
     */
    public native void properties(String path, Revision revision,
                                  Revision pegRevision, int depth,
                                  String[] changelists,
                                  ProplistCallback callback)
            throws ClientException;

    /**
     * @deprecated Use {@link #propertySet(String, String, String, int,
     *                                     String[], boolean, Map)} instead.
     * @since 1.0
     */
    public void propertySet(String path, String name, String value,
                            boolean recurse)
            throws ClientException
    {
        propertySet(path, name, value, recurse, false);
    }

    /**
     * @deprecated Use {@link #propertySet(String, String, String, int,
     *                                     String[], boolean, Map)} instead.
     * @since 1.2
     */
    public void propertySet(String path, String name, String value,
                                   boolean recurse, boolean force)
            throws ClientException
    {
        propertySet(path, name, value, Depth.infinityOrEmpty(recurse), null,
                    force, null);
    }

    /**
     * @deprecated Use {@link #propertySet(String, String, String, int,
     *                                     String[], boolean, Map)} instead.
     * @since 1.0
     */
    public void propertySet(String path, String name, byte[] value,
                            boolean recurse)
            throws ClientException
    {
        propertySet(path, name, value, recurse, false);
    }

    /**
     * @deprecated Use {@link #propertySet(String, String, String, int,
     *                                     String[], boolean, Map)} instead.
     * @since 1.2
     */
    public void propertySet(String path, String name, byte[] value,
                            boolean recurse, boolean force)
            throws ClientException
    {
        propertySet(path, name, new String(value), recurse, force);
    }

    /**
     * @since 1.5
     */
    public native void propertySet(String path, String name, String value,
                                   int depth, String[] changelists,
                                   boolean force, Map revpropTable)
            throws ClientException;

    /**
     * @deprecated Use {@link #propertyRemove(String, String, int, String[])}
                   instead.
     * @since 1.0
     */
    public void propertyRemove(String path, String name, boolean recurse)
            throws ClientException
    {
        propertyRemove(path, name, Depth.infinityOrEmpty(recurse), null);
    }

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
     * @deprecated Use {@link #propertyCreate(String, String, String, int,
     *                                        String[], boolean)} instead.
     * @since 1.0
     */
    public void propertyCreate(String path, String name, String value,
                               boolean recurse)
            throws ClientException
    {
        propertyCreate(path, name, value, recurse, false);
    }

    /**
     * @deprecated Use {@link #propertyCreate(String, String, String, int,
     *                                        String[], boolean)} instead.
     * @since 1.2
     */
    public void propertyCreate(String path, String name, String value,
                               boolean recurse, boolean force)
            throws ClientException
    {
        propertySet(path, name, value, recurse, force);
    }

    /**
     * @deprecated Use {@link #propertyCreate(String, String, String, int,
     *                                        String[], boolean)} instead.
     * @since 1.0
     */
    public void propertyCreate(String path, String name, byte[] value,
                               boolean recurse)
            throws ClientException
    {
        propertyCreate(path, name, value, recurse, false);
    }

    /**
     * @deprecated Use {@link #propertyCreate(String, String, String, int,
     *                                        String[], boolean)} instead.
     * @since 1.2
     */
    public void propertyCreate(String path, String name, byte[] value,
                               boolean recurse, boolean force)
            throws ClientException
    {
        propertyCreate(path, name, new String(value), recurse, force);
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
     * @deprecated Use {@link #setRevProperty(String, String, Revision, String,
     *                                        String, boolean)} instead.
     * @since 1.2
     */
    public void setRevProperty(String path, String name, Revision rev,
                               String value, boolean force)
            throws ClientException
    {
        setRevProperty(path, name, rev, value, null, force);
    }

    /**
     * @since 1.6
     */
    public native void setRevProperty(String path, String name, Revision rev,
                                      String value, String originalValue,
                                      boolean force)
            throws ClientException;

    /**
     * @deprecated Use {@link #propertyGet(String, String, Revision)} instead.
     * @since 1.0
     */
    public PropertyData propertyGet(String path, String name)
            throws ClientException
    {
        return propertyGet(path, name, null);
    }

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
     * @deprecated Use {@link #fileContent(String, Revision, Revision)}
     *             instead.
     * @since 1.0
     */
    public byte[] fileContent(String path, Revision revision)
            throws ClientException
    {
        return fileContent(path, revision, revision);
    }

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
     * @deprecated Use {@link #blame(String, Revision, Revision, Revision,
     *                               boolean, boolean, BlameCallback2)}
     *                               instead.
     * @since 1.0
     */
    public byte[] blame(String path, Revision revisionStart,
                        Revision revisionEnd)
            throws ClientException
    {
        BlameCallbackImpl callback = new BlameCallbackImpl();
        blame(path, revisionEnd, revisionStart, revisionEnd, callback);

        StringBuffer sb = new StringBuffer();
        for (int i = 0; i < callback.numberOfLines(); i++)
        {
            BlameCallbackImpl.BlameLine line = callback.getBlameLine(i);
            if (line != null)
            {
                sb.append(line.toString());
                sb.append("\n");
            }
        }
        return sb.toString().getBytes();
    }

    /**
     * @deprecated Use {@link #blame(String, Revision, Revision, Revision,
     *                               boolean, boolean, BlameCallback2)}
     *                               instead.
     * @since 1.0
     */
    public void blame(String path, Revision revisionStart,
                      Revision revisionEnd, BlameCallback callback)
            throws ClientException
    {
        blame(path, revisionEnd, revisionStart, revisionEnd, callback);
    }

    /**
     * @deprecated Use {@link #blame(String, Revision, Revision, Revision,
     *                               boolean, boolean, BlameCallback2)}
     *                               instead.
     * @since 1.2
     */
    public void blame(String path, Revision pegRevision,
                      Revision revisionStart, Revision revisionEnd,
                      BlameCallback callback)
            throws ClientException
    {
        BlameCallbackWrapper cw = new BlameCallbackWrapper(callback);
        blame(path, pegRevision, revisionStart, revisionEnd, false, false, cw);
    }

    /**
     * @since 1.5
     */
    public native void blame(String path, Revision pegRevision,
                             Revision revisionStart,
                             Revision revisionEnd, boolean ignoreMimeType,
                             boolean includeMergedRevisions,
                             BlameCallback2 callback)
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
     * Enable logging in the JNI-code
     * @param logLevel      the level of information to log (See
     *                      SVNClientLogLevel)
     * @param logFilePath   path of the log file
     */
    public static native void enableLogging(int logLevel, String logFilePath);

    /**
     * class for the constants of the logging levels.
     * The constants are defined in SVNClientLogLevel because of building
     * reasons
     */
    public static final class LogLevel implements SVNClientLogLevel
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
     * @deprecated Use {@link #info2(String, Revision, Revision, int, String[],
     *                               InfoCallback)} instead.
     * @since 1.2
     */
    public Info2[] info2(String pathOrUrl, Revision revision,
                         Revision pegRevision, boolean recurse)
            throws ClientException
    {
        MyInfoCallback callback = new MyInfoCallback();
        info2(pathOrUrl, revision, pegRevision,
              Depth.infinityOrEmpty(recurse), null, callback);
        return callback.getInfoArray();
    }

    /**
     * @since 1.5
     */
    public native void info2(String pathOrUrl, Revision revision,
                             Revision pegRevision, int depth,
                             String[] changelists, InfoCallback callback)
            throws ClientException;

    /**
     * A private wrapper function for RevisionRanges.
     * @returns a single-element revision range.
     */
    private RevisionRange[] toRevisionRange(Revision rev1, Revision rev2)
    {
        RevisionRange[] ranges = new RevisionRange[1];
        ranges[0] = new RevisionRange(rev1, rev2);
        return ranges;
    }

    /**
     * A private log message callback implementation used by thin wrappers.
     * Instances of this class are not thread-safe.
     */
    private class MyLogMessageCallback implements LogMessageCallback
    {
        private List messages = new ArrayList();

        public void singleMessage(ChangePath[] changedPaths,
                                  long revision,
                                  Map revprops,
                                  boolean hasChildren)
        {
            String author = (String) revprops.get("svn:author");
            String message = (String) revprops.get("svn:log");
            long timeMicros;

            try {
                LogDate date = new LogDate((String) revprops.get("svn:date"));
                timeMicros = date.getTimeMicros();
            } catch (ParseException ex) {
                timeMicros = 0;
            }

            LogMessage msg = new LogMessage(changedPaths,
                                            revision,
                                            author,
                                            timeMicros,
                                            message);

            /* Filter out the SVN_INVALID_REVNUM message which pre-1.5
               clients won't expect, nor understand. */
            if (revision != Revision.SVN_INVALID_REVNUM)
                messages.add(msg);
        }

        public LogMessage[] getMessages()
        {
            return (LogMessage[]) messages.toArray(
                                            new LogMessage[messages.size()]);
        }
    }

    /**
     * A private info callback implementation used by thin wrappers.
     * Instances of this class are not thread-safe.
     */
    private class MyInfoCallback implements InfoCallback
    {
        private List infos = new ArrayList();

        public void singleInfo(Info2 info)
        {
            infos.add(info);
        }

        public Info2[] getInfoArray()
        {
            return (Info2[]) infos.toArray(new Info2[infos.size()]);
        }
    }

    /**
     * A private status callback implementation used by thin wrappers.
     * Instances of this class are not thread-safe.
     */
    private class MyStatusCallback implements StatusCallback
    {
        private List statuses = new ArrayList();

        public void doStatus(Status status)
        {
            statuses.add(status);
        }

        public Status[] getStatusArray()
        {
            return (Status[]) statuses.toArray(new Status[statuses.size()]);
        }
    }

    /**
     * A private list callback implementation used by thin wrappers.
     * Instances of this class are not thread-safe.
     */
    private class MyListCallback implements ListCallback
    {
        private List dirents = new ArrayList();

        public void doEntry(DirEntry dirent, Lock lock)
        {
            // All of this is meant to retain backward compatibility with
            // the old svn_client_ls-style API.  For further information about
            // what is going on here, see the comments in
            // libsvn_client/list.c:store_dirent().

            if (dirent.getPath().length() == 0)
            {
                if (dirent.getNodeKind() == NodeKind.file)
                {
                    String absPath = dirent.getAbsPath();
                    int lastSeparator = absPath.lastIndexOf('/');
                    String path = absPath.substring(lastSeparator,
                                                    absPath.length());
                    dirent.setPath(path);
                }
                else
                {
                    // It's the requested directory, which we don't want
                    // to add.
                    return;
                }
            }

            dirents.add(dirent);
        }

        public DirEntry[] getDirEntryArray()
        {
            return (DirEntry[]) dirents.toArray(new DirEntry[dirents.size()]);
        }
    }

    /**
     * A private wrapper for compatibility of blame implementations.
     */
    private class BlameCallbackWrapper implements BlameCallback2
    {
        private BlameCallback oldCallback;

        public BlameCallbackWrapper(BlameCallback callback)
        {
            oldCallback = callback;
        }

        public void singleLine(Date date, long revision, String author,
                               Date merged_date, long merged_revision,
                               String merged_author, String merged_path,
                               String line)
        {
            oldCallback.singleLine(date, revision, author, line);
        }
    }
}
