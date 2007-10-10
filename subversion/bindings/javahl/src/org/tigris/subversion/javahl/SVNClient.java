/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2007 CollabNet.  All rights reserved.
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
     * @return Version information about the underlying native libraries.
     */
    public Version getVersion()
    {
        return NativeResources.version;
    }

    /**
     * @return The name of the working copy's administrative
     * directory, which is usually <code>.svn</code>.
     * @see <a
     * href="http://svn.collab.net/repos/svn/trunk/notes/asp-dot-net-hack.txt">
     * Instructions on changing this as a work-around for the behavior of
     * ASP.Net on Windows.</a>
     * @since 1.3
     */
    public native String getAdminDirectoryName();

    /**
     * @param name The name of the directory to compare.
     * @return Whether <code>name</code> is that of a working copy
     * administrative directory.
     * @since 1.3
     */
    public native boolean isAdminDirectory(String name);

    /**
      * Returns the last destination path submitted.
      * @deprecated
      * @return path in Subversion format.
      */
     public native String getLastPath();

    /**
     * List a directory or file of the working copy.
     *
     * @param path      Path to explore.
     * @param descend   Recurse into subdirectories if they exist.
     * @param onServer  Request status information from server.
     * @param getAll    get status for uninteristing files (unchanged).
     * @return Array of Status entries.
     */
    public Status[] status(String path, boolean descend, boolean onServer,
                           boolean getAll)
            throws ClientException
    {
        return status(path, descend, onServer, getAll, false);
    }

    /**
     * List a directory or file of the working copy.
     *
     * @param path      Path to explore.
     * @param descend   Recurse into subdirectories they exist.
     * @param onServer  Request status information from server.
     * @param getAll    get status for uninteristing files (unchanged).
     * @param noIgnore  get status for normaly ignored files and directories.
     * @return Array of Status entries.
     */
    public Status[] status(String path, boolean descend,
                           boolean onServer, boolean getAll,
                           boolean noIgnore)
            throws ClientException
    {
        return status(path, descend, onServer, getAll, noIgnore, false);
    }

    /**
     * List a directory or file of the working copy.
     *
     * @param path            Path to explore.
     * @param descend         Recurse into subdirectories if they exist.
     * @param onServer        Request status information from server.
     * @param getAll          get status for uninteristing files (unchanged).
     * @param noIgnore        get status for normaly ignored files and
     * *                      directories.
     * @param ignoreExternals if externals are ignored during status
     * @return Array of Status entries.
     * @since 1.2
     */
    public Status[] status(String path, boolean descend, boolean onServer,
                           boolean getAll, boolean noIgnore,
                           boolean ignoreExternals)
            throws ClientException
    {
        MyStatusCallback callback = new MyStatusCallback();

        status(path, Depth.infinityOrImmediates(descend), onServer, getAll,
               noIgnore, ignoreExternals, callback);

        return callback.getStatusArray();
    }

    /**
     * List a directory or file of the working copy.
     *
     * @param path      Path to explore.
     * @param depth     How deep to recurse into subdirectories.
     * @param onServer  Request status information from server.
     * @param getAll    get status for uninteresting (unchanged) files.
     * @param noIgnore  get status for normaly ignored files and directories.
     * @param ignoreExternals if externals are ignored during status
     * @since 1.5
     */
    public native void status(String path, int depth, boolean onServer,
                              boolean getAll, boolean noIgnore,
                              boolean ignoreExternals, StatusCallback callback)
            throws ClientException;

    /**
     * Lists the directory entries of an url on the server.
     * @param url       the url to list
     * @param revision  the revision to list
     * @param recurse   recurse into subdirectories
     * @return  Array of DirEntry objects.
     */
    public DirEntry[] list(String url, Revision revision, boolean recurse)
            throws ClientException
    {
        return list(url, revision, revision, recurse);
    }

    /**
     * Lists the directory entries of an url on the server.
     *
     * @param url         the url to list
     * @param revision    the revision to list
     * @param pegRevision the revision to interpret url
     * @param recurse     recurse into subdirectories
     * @return Array of DirEntry objects.
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
     * Lists the directory entries of an url on the server.
     * @param url             the url to list
     * @param revision        the revision to list
     * @param pegRevision     the revision to interpret url
     * @param depth           the depth to recurse into subdirectories
     * @param direntFields    the fields to retrieve
     * @param fetchLocks      whether to fetch lock information
     * @param callback        the callback to receive the directory entries
     * @since 1.5
     */
    public native void list(String url, Revision revision,
                            Revision pegRevision, int depth, int direntFields,
                            boolean fetchLocks, ListCallback callback)
            throws ClientException;

    /**
     * Returns the status of a single file in the path.
     *
     * @param path      File to gather status.
     * @param onServer  Request status information from the server.
     * @return  the subversion status of the file.
     */
    public Status singleStatus(String path, boolean onServer)
            throws ClientException
    {
        return status(path, false, onServer, true, false, false)[0];
    }

    /**
     * Sets the user name used for authentification.
     * @param username The user name.
     */
    public native void username(String username);

    /**
     * Sets the password used for authification.
     * @param password  the password
     */
    public native void password(String password);

    /**
     * Register callback interface to supply user name and password on
     * demand.  This callback can also be used to provide the
     * equivalent of the <code>--no-auth-cache</code> and
     * <code>--non-interactive</code> arguments accepted by the
     * command-line client.
     *
     * @param prompt the callback interface
     */
    public native void setPrompt(PromptUserPassword prompt);

    /**
     * Retrieve the log messages for an item
     * @param path          path or url to get the log message for.
     * @param revisionStart first revision to show
     * @param revisionEnd   last revision to show
     * @return array of LogMessages
     */
    public LogMessage[] logMessages(String path, Revision revisionStart,
                                    Revision revisionEnd)
            throws ClientException
    {
        return logMessages(path, revisionStart, revisionEnd, true, false);
    }

    /**
     * Retrieve the log messages for an item
     * @param path          path or url to get the log message for.
     * @param revisionStart first revision to show
     * @param revisionEnd   last revision to show
     * @param stopOnCopy    do not continue on copy operations
     * @return array of LogMessages
     */
    public LogMessage[] logMessages(String path, Revision revisionStart,
                                    Revision revisionEnd, boolean stopOnCopy)
            throws ClientException
    {
        return logMessages(path, revisionStart, revisionEnd,
                           stopOnCopy, false);
    }

    /**
     * Retrieve the log messages for an item
     * @param path          path or url to get the log message for.
     * @param revisionStart first revision to show
     * @param revisionEnd   last revision to show
     * @param stopOnCopy    do not continue on copy operations
     * @param discoverPath
     * @return array of LogMessages
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
     * Retrieve the log messages for an item
     * @param path          path or url to get the log message for.
     * @param revisionStart first revision to show
     * @param revisionEnd   last revision to show
     * @param stopOnCopy    do not continue on copy operations
     * @param discoverPath  returns the paths of the changed items in the
     *                      returned objects
     * @param limit         limit the number of log messages (if 0 or less no
     *                      limit)
     * @return array of LogMessages
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

        logMessages(path, revisionEnd, revisionStart, revisionEnd,
                    stopOnCopy, discoverPath, false, false, limit, callback);

        return callback.getMessages();
    }

    /**
     * Retrieve the log messages for an item
     * @param path          path or url to get the log message for.
     * @param pegRevision   the revision to interpret path
     * @param revisionStart first revision to show
     * @param revisionEnd   last revision to show
     * @param stopOnCopy    do not continue on copy operations
     * @param discoverPath  returns the paths of the changed items in the
     *                      returned objects
     * @param includeMergedRevisions include log messages for revisions which
     *                               were merged.
     * @param omitLogText   supress log message text.
     * @param limit         limit the number of log messages (if 0 or less no
     *                      limit)
     * @since 1.5
     */
    public native void logMessages(String path,
                                   Revision pegRevision,
                                   Revision revisionStart,
                                   Revision revisionEnd,
                                   boolean stopOnCopy,
                                   boolean discoverPath,
                                   boolean includeMergedRevisions,
                                   boolean omitLogText,
                                   long limit,
                                   LogMessageCallback callback)
            throws ClientException;

    /**
     * Executes a revision checkout.
     * @param moduleName name of the module to checkout.
     * @param destPath destination directory for checkout.
     * @param revision the revision to checkout.
     * @param pegRevision the peg revision to interpret the path
     * @param depth how deep to checkout files recursively.
     * @param ignoreExternals if externals are ignored during checkout
     * @param allowUnverObstructions allow unversioned paths that obstruct adds
     * @throws ClientException
     * @since 1.5
     */
    public native long checkout(String moduleName, String destPath,
                                Revision revision, Revision pegRevision,
                                int depth, boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    /**
     * Executes a revision checkout.
     * @param moduleName name of the module to checkout.
     * @param destPath destination directory for checkout.
     * @param revision the revision to checkout.
     * @param pegRevision the peg revision to interpret the path
     * @param recurse whether you want it to checkout files recursively.
     * @param ignoreExternals if externals are ignored during checkout
     * @throws ClientException
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
     * Executes a revision checkout.
     * @param moduleName name of the module to checkout.
     * @param destPath destination directory for checkout.
     * @param revision the revision to checkout.
     * @param recurse whether you want it to checkout files recursively.
     * @throws ClientException
     */
    public long checkout(String moduleName, String destPath,
                         Revision revision, boolean recurse)
            throws ClientException
    {
        return checkout(moduleName, destPath, revision, revision, recurse,
                        false);
    }

    /**
     * Sets the notification callback used to send processing information back
     * to the calling program.
     * @param notify listener that the SVN library should call on many
     *               file operations.
     * @deprecated use notification2 instead
     */
    public native void notification(Notify notify);

    /**
     * Sets the notification callback used to send processing information back
     * to the calling program.
     *
     * @param notify listener that the SVN library should call on many
     *               file operations.
     * @since 1.2
     */
    public native void notification2(Notify2 notify);

    /**
     * @see SVNClientInterface#setConflictResolver(ConflictResolverCallback)
     * @since 1.5
     */
    public native void setConflictResolver(ConflictResolverCallback listener);

    /**
     * Set the progress callback.
     *
     * @param listener The progress callback.
     * @since 1.5
     */
    public native void setProgressListener(ProgressListener listener);

    /**
     * Sets the commit message handler. This allows more complex commit message
     * with the list of the elements to be commited as input.
     * @param messageHandler    callback for entering commit messages if this
     *                          is set the message parameter is ignored.
     */
    public native void commitMessageHandler(CommitMessage messageHandler);

    /**
     * Sets a file for deletion.
     * @param path      path or url to be deleted
     * @param message   if path is a url, this will be the commit message.
     * @param force     delete even when there are local modifications.
     * @throws ClientException
     */
    public void remove(String[] path, String message, boolean force)
            throws ClientException
    {
        remove(path, message, force, false);
    }

    /**
     * Sets a file for deletion.
     * @param path      path or url to be deleted
     * @param message   if path is a url, this will be the commit message.
     * @param force     delete even when there are local modifications.
     * @param keepLocal only remove the paths from the repository.
     * @throws ClientException
     * @since 1.5
     */
    public native void remove(String[] path, String message, boolean force,
                              boolean keepLocal)
            throws ClientException;

    /**
     * Reverts a file to a pristine state.
     * @param path      path of the file.
     * @param recurse   recurse into subdirectories
     * @throws ClientException
     */
    public void revert(String path, boolean recurse)
            throws ClientException
    {
        revert(path, Depth.infinityOrEmpty(recurse));
    }

    /**
     * Reverts a file to a pristine state.
     * @param path      path of the file.
     * @param depth     the depth to recurse into subdirectories
     * @throws ClientException
     * @since 1.5
     */
    public native void revert(String path, int depth)
            throws ClientException;

    /**
     * Adds a file to the repository.
     * @param path      path to be added.
     * @param recurse   recurse into subdirectories
     * @throws ClientException
     */
    public void add(String path, boolean recurse)
            throws ClientException
    {
        add(path, recurse, false);
    }

    /**
     * Adds a file to the repository.
     * @param path      path to be added.
     * @param recurse   recurse into subdirectories
     * @param force     if adding a directory and recurse true and path is a
     *                  directory, all not already managed files are added.
     * @throws ClientException
     * @since 1.2
     */
    public void add(String path, boolean recurse, boolean force)
            throws ClientException
    {
        add(path, Depth.infinityOrEmpty(recurse), force, false, false);
    }

    /**
     * Adds a file to the repository.
     * @param path      path to be added.
     * @param depth     the depth to recurse into subdirectories
     * @param force     if adding a directory and recurse true and path is a
     *                  directory, all not already managed files are added.
     * @param noIgnores if false, don't add files or directories matching
     *                  ignore patterns
     * @param addParents add any intermediate parents to the working copy
     * @throws ClientException
     * @since 1.5
     */
    public native void add(String path, int depth, boolean force,
                           boolean noIgnores, boolean addParents)
        throws ClientException;

    /**
     * Updates the directory or file from repository
     * @param path target file.
     * @param revision the revision number to update.
     *                 Revision.HEAD will update to the
     *                 latest revision.
     * @param recurse recursively update.
     * @throws ClientException
     */
    public long update(String path, Revision revision, boolean recurse)
            throws ClientException
    {
        return update(new String[]{path}, revision,
                      Depth.infinityOrFiles(recurse), false, false)[0];
    }

    /**
     * Updates the directories or files from repository
     * @param path array of target files.
     * @param revision the revision number to update.
     *                 Revision.HEAD will update to the
     *                 latest revision.
     * @param recurse recursively update.
     * @param ignoreExternals externals will be ignore during update
     * @throws ClientException
     * @since 1.2
     */
    public long[] update(String[] path, Revision revision,
                         boolean recurse, boolean ignoreExternals)
            throws ClientException
    {
        return update(path, revision, Depth.infinityOrFiles(recurse),
                      ignoreExternals, false);
    }

    /**
     * Updates the directory or file from repository
     * @param path target file.
     * @param revision the revision number to update.
     *                 Revision.HEAD will update to the
     *                 latest revision.
     * @param depth  the depth to recursively update.
     * @param ignoreExternals externals will be ignore during update
     * @param allowUnverObstructions allow unversioned paths that obstruct adds
     * @throws ClientException
     * @since 1.5
     */
    public long update(String path, Revision revision, int depth,
                       boolean ignoreExternals,
                       boolean allowUnverObstructions)
            throws ClientException
    {
        return update(new String[]{path}, revision, depth,
                      ignoreExternals, allowUnverObstructions)[0];
    }

    /**
     * Updates the directories or files from repository
     * @param path array of target files.
     * @param revision the revision number to update.
     *                 Revision.HEAD will update to the
     *                 latest revision.
     * @param depth  the depth to recursively update.
     * @param ignoreExternals externals will be ignore during update
     * @param allowUnverObstructions allow unversioned paths that obstruct adds
     * @throws ClientException
     * @since 1.5
     */
    public native long[] update(String[] path, Revision revision,
                                int depth, boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    /**
     * @see SVNClientInterface#commit(String[], String, boolean)
     */
    public long commit(String[] path, String message, boolean recurse)
            throws ClientException
    {
        return commit(path, message, recurse, false);
    }

    /**
     * @see SVNClientInterface#commit(String[], String, int, boolean, boolean, String)
     * @since 1.5
     */
    public native long commit(String[] path, String message, int depth,
                              boolean noUnlock, boolean keepChangelist,
                              String changelistName)
            throws ClientException;

    /**
     * Copy versioned paths with the history preserved.
     *
     * @see SVNClientInterface#copy(CopySource[], String, String, boolean, boolean, boolean)
     * @since 1.5
     */
    public native void copy(CopySource[] sources, String destPath,
                            String message, boolean copyAsChild,
                            boolean makeParents, boolean withMergeHistory)
            throws ClientException;

    /**
     * Copy versioned paths with the history preserved (with
     * <code>copyAsChild</code> behavior).
     *
     * @param srcPath   source path or url
     * @param destPath  destination path or url
     * @param message   commit message if destPath is an url
     * @param revision  source revision
     * @throws ClientException
     */
    public void copy(String srcPath, String destPath, String message,
                     Revision revision)
            throws ClientException
    {
        copy(new CopySource[] { new CopySource(srcPath, revision,
                                               Revision.HEAD) },
             destPath, message, true, false, false);
    }

    /**
     * Move or rename versioned paths.
     *
     * @param srcPaths Source paths or URLs.
     * @param destPath Destination path or URL.
     * @param message Commit message.  May be <code>null</code> if
     * <code>destPath</code> is not a URL.
     * @param force Whether to perform the move even if local
     * modifications exist.
     * @param moveAsChild Whether to move <code>srcPaths</code> as
     * children of <code>destPath</code>.
     * @param makeParents Whether to create intermediate parents
     * @throws ClientException If the move operation fails.
     * @see SVNClientInterface#move(String[], String, String, boolean, boolean, boolean, boolean)
     * @since 1.5
     */
    public native void move(String[] srcPaths, String destPath, String message,
                            boolean force, boolean moveAsChild,
                            boolean makeParents, boolean withMergeHistory)
            throws ClientException;

    /**
     * @deprecated Use move() without a Revision parameter.
     * @see SVNClientInterface#move(String[], String, String, boolean, boolean, boolean, boolean)
     * @since 1.2
     */
    public void move(String srcPath, String destPath, String message,
                     Revision ignored, boolean force)
            throws ClientException
    {
        move(new String[] { srcPath }, destPath, message, force, true, false,
             false);
    }

    /**
     * Move or rename versioned paths (with <code>moveAsChild</code>
     * behavior).
     *
     * @param srcPath  source path or url
     * @param destPath destination path or url
     * @param message  commit message if destPath is an url
     * @param force    even with local modifications.
     * @throws ClientException
     * @since 1.2
     */
    public void move(String srcPath, String destPath, String message,
                     boolean force)
            throws ClientException
    {
        move(new String[] { srcPath }, destPath, message, force, true, false,
             false);
    }

    /**
     * Creates a directory directly in a repository or creates a
     * directory on disk and schedules it for addition.
     * @param path      directories to be created
     * @param message   commit message to used if path contains urls
     * @param makeParents Whether to create intermediate parents
     * @throws ClientException
     * @since 1.5
     */
    public native void mkdir(String[] path, String message,
                             boolean makeParents)
            throws ClientException;

    /**
     * Creates a directory directly in a repository or creates a
     * directory on disk and schedules it for addition.
     * @param path      directories to be created
     * @param message   commit message to used if path contains urls
     * @throws ClientException
     */
    public void mkdir(String[] path, String message)
            throws ClientException
    {
        mkdir(path, message, false);
    }

    /**
     * Recursively cleans up a local directory, finishing any
     * incomplete operations, removing lockfiles, etc.
     * @param path a local directory.
     * @throws ClientException
     */
    public native void cleanup(String path)
            throws ClientException;

    /**
     * @see SVNClientInterface#resolved(String, int, int)
     * @since 1.5
     */
    public native void resolved(String path, int depth, int conflictResult)
        throws SubversionException;

    /**
     * @see SVNClientInterface#resolved(String, boolean)
     */
    public void resolved(String path, boolean recurse)
        throws ClientException
    {
        try
        {
            resolved(path, Depth.infinityOrEmpty(recurse),
                     ConflictResolverCallback.Result.chooseMerged);
        }
        catch (SubversionException e)
        {
            throw ClientException.fromException(e);
        }
    }

    /**
     * Exports the contents of either a subversion repository into a
     * 'clean' directory (meaning a directory with no administrative
     * directories).
     * @param srcPath   the url of the repository path to be exported
     * @param destPath  a destination path that must not already exist.
     * @param revision  the revsion to be exported
     * @param force     set if it is ok to overwrite local files
     * @throws ClientException
     */
    public long doExport(String srcPath, String destPath,
                                Revision revision, boolean force)
            throws ClientException
    {
        return doExport(srcPath, destPath, revision, revision, force,
                false, true, null);
    }

    /**
     * Exports the contents of either a subversion repository into a
     * 'clean' directory (meaning a directory with no administrative
     * directories).
     *
     * @param srcPath         the url of the repository path to be exported
     * @param destPath        a destination path that must not already exist.
     * @param revision        the revsion to be exported
     * @param pegRevision     the revision to interpret srcPath
     * @param force           set if it is ok to overwrite local files
     * @param ignoreExternals ignore external during export
     * @param recurse         recurse to subdirectories
     * @param nativeEOL       which EOL characters to use during export
     * @throws ClientException
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
     * Exports the contents of either a subversion repository into a
     * 'clean' directory (meaning a directory with no administrative
     * directories).
     *
     * @param srcPath         the url of the repository path to be exported
     * @param destPath        a destination path that must not already exist.
     * @param revision        the revsion to be exported
     * @param pegRevision     the revision to interpret srcPath
     * @param force           set if it is ok to overwrite local files
     * @param ignoreExternals ignore external during export
     * @param depth           how deep to recurse in subdirectories
     * @param nativeEOL       which EOL characters to use during export
     * @throws ClientException
     * @since 1.5
     */
    public native long doExport(String srcPath, String destPath,
                                Revision revision, Revision pegRevision,
                                boolean force, boolean ignoreExternals,
                                int depth, String nativeEOL)
            throws ClientException;

    /**
     * @see SVNClientInterface#doSwitch(String, String, Revision, int, boolean, boolean)
     * @since 1.5
     */
    public native long doSwitch(String path, String url, Revision revision,
                                int depth, boolean ignoreExternals,
                                boolean allowUnverObstructions)
            throws ClientException;

    /**
     * Update local copy to mirror a new url.
     * @param path      the working copy path
     * @param url       the new url for the working copy
     * @param revision  the new base revision of working copy
     * @param recurse   traverse into subdirectories
     * @throws ClientException
     */
    public long doSwitch(String path, String url, Revision revision,
                         boolean recurse)
            throws ClientException
    {
        return doSwitch(path, url, revision, Depth.infinityOrFiles(recurse),
                        false, false);
    }

    /**
     * Import a file or directory into a repository directory  at
     * head.
     * @param path      the local path
     * @param url       the target url
     * @param message   the log message.
     * @param recurse   traverse into subdirectories
     * @throws ClientException
     */
    public void doImport(String path, String url, String message,
                         boolean recurse)
            throws ClientException
    {
        doImport(path, url, message, Depth.infinityOrFiles(recurse),
                 false, false);
    }

    /**
     * Import a file or directory into a repository directory  at
     * head.
     * @param path      the local path
     * @param url       the target url
     * @param message   the log message.
     * @param depth     depth to traverse into subdirectories
     * @param noIgnore  whether to add files matched by ignore patterns
     * @param ignoreUnknownNodeTypes whether to ignore files which
     *                  the node type is not konwn, just as pipes
     * @throws ClientException
     *
     * @since 1.5
     */
    public native void doImport(String path, String url, String message,
                                int depth, boolean noIgnore,
                                boolean ignoreUnknownNodeTypes)
            throws ClientException;

    /**
     * @see SVNClientInterface#suggestMergeSources(String, Revision)
     */
    public native String[] suggestMergeSources(String path, 
                                               Revision pegRevision)
            throws SubversionException;

    /**
     * Merge changes from two paths into a new local path.
     * @param path1         first path or url
     * @param revision1     first revision
     * @param path2         second path or url
     * @param revision2     second revision
     * @param localPath     target local path
     * @param force         overwrite local changes
     * @param recurse       traverse into subdirectories
     * @throws ClientException
     */
    public void merge(String path1, Revision revision1, String path2,
                      Revision revision2, String localPath,
                      boolean force, boolean recurse)
            throws ClientException
    {
        merge(path1,revision1, path2, revision2, localPath, force, recurse,
              false, false);
    }

    /**
     * Merge changes from two paths into a new local path.
     *
     * @param path1          first path or url
     * @param revision1      first revision
     * @param path2          second path or url
     * @param revision2      second revision
     * @param localPath      target local path
     * @param force          overwrite local changes
     * @param recurse        traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param dryRun         do not change anything
     * @throws ClientException
     * @since 1.2
     */
    public void merge(String path1, Revision revision1, String path2,
                      Revision revision2, String localPath, boolean force,
                      boolean recurse, boolean ignoreAncestry, boolean dryRun)
            throws ClientException
    {
        merge(path1, revision1, path2, revision2, localPath, force,
              Depth.infinityOrFiles(recurse), ignoreAncestry, dryRun);
    }

    /**
     * Merge changes from two paths into a new local path.
     *
     * @param path1          first path or url
     * @param revision1      first revision
     * @param path2          second path or url
     * @param revision2      second revision
     * @param localPath      target local path
     * @param force          overwrite local changes
     * @param depth          how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param dryRun         do not change anything
     * @throws ClientException
     * @since 1.5
     */
    public native void merge(String path1, Revision revision1, String path2,
                             Revision revision2, String localPath,
                             boolean force, int depth,
                             boolean ignoreAncestry, boolean dryRun)
            throws ClientException;

    /**
     * Merge changes from two paths into a new local path.
     *
     * @param path           path or url
     * @param pegRevision    revision to interpret path
     * @param revision1      first revision
     * @param revision2      second revision
     * @param localPath      target local path
     * @param force          overwrite local changes
     * @param recurse        traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param dryRun         do not change anything
     * @throws ClientException
     * @since 1.2
     */
    public void merge(String path, Revision pegRevision, Revision revision1,
                      Revision revision2, String localPath, boolean force,
                      boolean recurse, boolean ignoreAncestry, boolean dryRun)
           throws ClientException
    {
        merge(path, pegRevision, revision1, revision2, localPath, force,
              Depth.infinityOrFiles(recurse), ignoreAncestry, dryRun);
    }

    /**
     * Merge changes from two paths into a new local path.
     *
     * @param path           path or url
     * @param pegRevision    revision to interpret path
     * @param revision1      first revision
     * @param revision2      second revision
     * @param localPath      target local path
     * @param force          overwrite local changes
     * @param depth          how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param dryRun         do not change anything
     * @throws ClientException
     * @since 1.5
     */
    public native void merge(String path, Revision pegRevision,
                             Revision revision1, Revision revision2,
                             String localPath, boolean force, int depth,
                             boolean ignoreAncestry, boolean dryRun)
           throws ClientException;

    /**
     * Merge set of revisions into a new local path.
     * @param path          path or url
     * @param pegRevision   revision to interpret path
     * @param revisions     revisions to merge
     * @param localPath     target local path
     * @param force         overwrite local changes
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param dryRun        do not change anything
     * @throws ClientException
     * @since 1.5
     */
    public void merge(String path, Revision pegRevision,
                      RevisionRange[] revisions, String localPath,
                      boolean force, int depth, boolean ignoreAncestry,
                      boolean dryRun)
            throws ClientException
    {
        for (int i = 0; i < revisions.length; i++)
        {
            Revision from = revisions[i].getFromRevision();
            if (from instanceof Revision.Number)
            {
                long revNum = ((Revision.Number) from).getNumber();
                from = new Revision.Number(revNum - 1);
            }
            this.merge(path, pegRevision, from,
                       revisions[i].getToRevision(), localPath, force, depth,
                       ignoreAncestry, dryRun);
        }
    }

    /**
     * @see SVNClientInterface#getMergeInfo(String, Revision)
     * @since 1.5
     */
    public native MergeInfo getMergeInfo(String path, Revision pegRevision)
            throws SubversionException;

    /**
     * @see SVNClientInterface#getAvailableMerges(String, Revision, String)
     * @since 1.5
     */
    public native RevisionRange[] getAvailableMerges(String path,
                                                     Revision pegRevision,
                                                     String mergeSource)
        throws SubversionException;

    /**
     * Display the differences between two paths
     * @param target1       first path or url
     * @param revision1     first revision
     * @param target2       second path or url
     * @param revision2     second revision
     * @param outFileName   file name where difference are written
     * @param recurse       traverse into subdirectories
     * @throws ClientException
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
     * Display the differences between two paths
     *
     * @param target1        first path or url
     * @param revision1      first revision
     * @param target2        second path or url
     * @param revision2      second revision
     * @param outFileName    file name where difference are written
     * @param recurse        traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted  no output on deleted files
     * @param force          diff even on binary files
     * @throws ClientException
     * @since 1.2
     */
    public void diff(String target1, Revision revision1, String target2,
                     Revision revision2, String outFileName, boolean recurse,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force)
            throws ClientException
    {
        diff(target1, revision1, target2, revision2, outFileName,
             Depth.infinityOrFiles(recurse), ignoreAncestry, noDiffDeleted,
             force);
    }

    /**
     * Display the differences between two paths
     * @param target1       first path or url
     * @param revision1     first revision
     * @param target2       second path or url
     * @param revision2     second revision
     * @param outFileName   file name where difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @throws ClientException
     * @since 1.5
     */
    public native void diff(String target1, Revision revision1, String target2,
                            Revision revision2, String outFileName, int depth,
                            boolean ignoreAncestry, boolean noDiffDeleted,
                            boolean force)
            throws ClientException;

    /**
     * Display the differences between two paths
     *
     * @param target         path or url
     * @param pegRevision    revision tointerpret target
     * @param startRevision  first Revision to compare
     * @param endRevision    second Revision to compare
     * @param outFileName    file name where difference are written
     * @param recurse        traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted  no output on deleted files
     * @param force          diff even on binary files
     * @throws ClientException
     * @since 1.2
     */
    public void diff(String target, Revision pegRevision,
                     Revision startRevision, Revision endRevision,
                     String outFileName, boolean recurse,
                     boolean ignoreAncestry, boolean noDiffDeleted,
                     boolean force)
            throws ClientException
    {
        diff(target, pegRevision, startRevision, endRevision, outFileName,
             Depth.infinityOrFiles(recurse), ignoreAncestry, noDiffDeleted,
             force);
    }

    /**
     * Display the differences between two paths
     * @param target        path or url
     * @param pegRevision   revision tointerpret target
     * @param startRevision first Revision to compare
     * @param endRevision   second Revision to compare
     * @param outFileName   file name where difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @throws ClientException
     * @since 1.5
     */
    public native void diff(String target, Revision pegRevision,
                            Revision startRevision, Revision endRevision,
                            String outFileName, int depth,
                            boolean ignoreAncestry, boolean noDiffDeleted,
                            boolean force)
            throws ClientException;

    /**
     * Produce a diff summary which lists the items changed between
     * path and revision pairs.
     *
     * @param target1 Path or URL.
     * @param revision1 Revision of <code>target1</code>.
     * @param target2 Path or URL.
     * @param revision2 Revision of <code>target2</code>.
     * @param depth how deep to recurse.
     * @param ignoreAncestry Whether to ignore unrelated files during
     * comparison.  False positives may potentially be reported if
     * this parameter <code>false</code>, since a file might have been
     * modified between two revisions, but still have the same
     * contents.
     * @param receiver As each is difference is found, this callback
     * is invoked with a description of the difference.
     *
     * @throws ClientException
     * @since 1.5
     */
    public native void diffSummarize(String target1, Revision revision1,
                                     String target2, Revision revision2,
                                     int depth, boolean ignoreAncestry,
                                     DiffSummaryReceiver receiver)
            throws ClientException;

    /**
     * Produce a diff summary which lists the items changed between
     * path and revision pairs.
     *
     * @param target Path or URL.
     * @param pegRevision Revision at which to interpret
     * <code>target</code>.  If {@link RevisionKind#unspecified} or
     * <code>null</code>, behave identically to {@link
     * #diffSummarize(String, Revision, String, Revision, int,
     * boolean, DiffSummaryReceiver)}, using <code>path</code> for
     * both of that method's targets.
     * @param startRevision Beginning of range for comparsion of
     * <code>target</code>.
     * @param endRevision End of range for comparsion of
     * <code>target</code>.
     * @param depth how deep to recurse.
     * @param ignoreAncestry Whether to ignore unrelated files during
     * comparison.  False positives may potentially be reported if
     * this parameter <code>false</code>, since a file might have been
     * modified between two revisions, but still have the same
     * contents.
     * @param receiver As each is difference is found, this callback
     * is invoked with a description of the difference.
     *
     * @throws ClientException
     * @since 1.5
     */
    public native void diffSummarize(String target, Revision pegRevision,
                                     Revision startRevision,
                                     Revision endRevision,
                                     int depth, boolean ignoreAncestry,
                                     DiffSummaryReceiver receiver)
            throws ClientException;

    /**
     * Retrieves the properties of an item
     * @param path  the path of the item
     * @return array of property objects
     */
    public PropertyData[] properties(String path) throws ClientException
    {
        return properties(path, null);
    }

    /**
     * Retrieves the properties of an item
     *
     * @param path     the path of the item
     * @param revision the revision of the item
     * @return array of property objects
     * @since 1.2
     */
    public PropertyData[] properties(String path, Revision revision)
            throws ClientException
    {
        return properties(path, revision, revision);
    }

    /**
     * Retrieves the properties of an item
     *
     * @param path        the path of the item
     * @param revision    the revision of the item
     * @param pegRevision the revision to interpret path
     * @return array of property objects
     * @since 1.2
     */
    public PropertyData[] properties(String path, Revision revision,
                                     Revision pegRevision)
            throws ClientException
    {
        ProplistCallbackImpl callback = new ProplistCallbackImpl();
        properties(path, revision, pegRevision, Depth.empty, callback);

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
     * Retrieves the properties of an item
     *
     * @param path        the path of the item
     * @param revision    the revision of the item
     * @param pegRevision the revision to interpret path
     * @param depth       the depth to recurse into subdirectories
     * @param callback    the callback to use to return the properties
     * @since 1.5
     */
    public native void properties(String path, Revision revision,
                                  Revision pegRevision, int depth,
                                  ProplistCallback callback)
            throws ClientException;

    /**
     * Sets one property of an item with a String value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    public void propertySet(String path, String name, String value,
                            boolean recurse)
            throws ClientException
    {
        propertySet(path, name, value, recurse, false);
    }

    /**
     * Sets one property of an item with a String value
     *
     * @param path    path of the item
     * @param name    name of the property
     * @param value   new value of the property
     * @param recurse set property also on the subdirectories
     * @param force   do not check if the value is valid
     * @throws ClientException
     * @since 1.2
     */
    public void propertySet(String path, String name, String value,
                                   boolean recurse, boolean force)
            throws ClientException
    {
        propertySet(path, name, value, Depth.infinityOrEmpty(recurse), force);
    }

    /**
     * Sets one property of an item with a byte array value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    public void propertySet(String path, String name, byte[] value,
                            boolean recurse)
            throws ClientException
    {
        propertySet(path, name, value, recurse, false);
    }

    /**
     * Sets one property of an item with a byte array value
     *
     * @param path    path of the item
     * @param name    name of the property
     * @param value   new value of the property
     * @param recurse set property also on the subdirectories
     * @param force   do not check if the value is valid
     * @throws ClientException
     * @since 1.2
     */
    public void propertySet(String path, String name, byte[] value,
                            boolean recurse, boolean force)
            throws ClientException
    {
        propertySet(path, name, new String(value), recurse, force);
    }

    /**
     * Sets one property of an item with a String value
     *
     * @param path    path of the item
     * @param name    name of the property
     * @param value   new value of the property
     * @param depth   the depth to recurse into subdirectories
     * @param force   do not check if the value is valid
     * @throws ClientException
     * @since 1.5
     */
    public native void propertySet(String path, String name, String value,
                                   int depth, boolean force)
            throws ClientException;

    /**
     * Remove one property of an item.
     * @param path      path of the item
     * @param name      name of the property
     * @param recurse   remove the property also on subdirectories
     * @throws ClientException
     */
    public void propertyRemove(String path, String name, boolean recurse)
            throws ClientException
    {
        propertyRemove(path, name, Depth.infinityOrEmpty(recurse));
    }

    /**
     * Remove one property of an item.
     * @param path      path of the item
     * @param name      name of the property
     * @param depth     the depth to recurse into subdirectories
     * @throws ClientException
     * @since 1.5
     */
    public native void propertyRemove(String path, String name,
                                      int depth)
            throws ClientException;

    /**
     * Create and sets one property of an item with a String value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    public void propertyCreate(String path, String name, String value,
                               boolean recurse)
            throws ClientException
    {
        propertyCreate(path, name, value, recurse, false);
    }

    /**
     * Create and sets one property of an item with a String value
     *
     * @param path    path of the item
     * @param name    name of the property
     * @param value   new value of the property
     * @param recurse set property also on the subdirectories
     * @param force   do not check if the value is valid
     * @throws ClientException
     * @since 1.2
     */
    public void propertyCreate(String path, String name, String value,
                               boolean recurse, boolean force)
            throws ClientException
    {
        propertySet(path, name, value, recurse, force);
    }

    /**
     * Create and sets one property of an item with a byte array value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    public void propertyCreate(String path, String name, byte[] value,
                               boolean recurse)
            throws ClientException
    {
        propertyCreate(path, name, value, recurse, false);
    }

    /**
     * Create and sets one property of an item with a byte array value
     *
     * @param path    path of the item
     * @param name    name of the property
     * @param value   new value of the property
     * @param recurse set property also on the subdirectories
     * @param force   do not check if the value is valid
     * @throws ClientException
     * @since 1.2
     */
    public void propertyCreate(String path, String name, byte[] value,
                               boolean recurse, boolean force)
            throws ClientException
    {
        propertyCreate(path, name, new String(value), recurse, force);
    }

    /**
     * Retrieve one revsision property of one item
     * @param path      path of the item
     * @param name      name of the property
     * @param rev       revision to retrieve
     * @return the Property
     * @throws ClientException
     */
    public native PropertyData revProperty(String path, String name,
                                           Revision rev)
            throws ClientException;

    /**
     * Retrieve all revsision properties of one item
     * @param path      path of the item
     * @param rev       revision to retrieve
     * @return the Properties
     * @throws ClientException
     * @since 1.2
     */
    public native PropertyData[] revProperties(String path, Revision rev)
            throws ClientException;

    /**
     * set one revsision property of one item
     * @param path      path of the item
     * @param name      name of the property
     * @param rev       revision to retrieve
     * @param value     value of the property
     * @param force
     * @throws ClientException
     * @since 1.2
     */
    public native void setRevProperty(String path, String name, Revision rev,
                                      String value, boolean force)
            throws ClientException;

    /**
     * Retrieve one property of one iten
     * @param path      path of the item
     * @param name      name of property
     * @return the Property
     * @throws ClientException
     */
    public PropertyData propertyGet(String path, String name)
            throws ClientException
    {
        return propertyGet(path, name, null);
    }

    /**
     * Retrieve one property of one iten
     *
     * @param path     path of the item
     * @param name     name of property
     * @param revision revision of the item
     * @return the Property
     * @throws ClientException
     * @since 1.2
     */
    public PropertyData propertyGet(String path, String name,
                                    Revision revision)
            throws ClientException
    {
        return propertyGet(path, name, revision, revision);
    }

    /**
     * Retrieve one property of one iten
     *
     * @param path     path of the item
     * @param name     name of property
     * @param revision revision of the item
     * @param pegRevision the revision to interpret path
     * @return the Property
     * @throws ClientException
     * @since 1.2
     */
    public native PropertyData propertyGet(String path, String name,
                                           Revision revision,
                                           Revision pegRevision)
            throws ClientException;

    /**
     *  Retrieve the content of a file
     * @param path      the path of the file
     * @param revision  the revision to retrieve
     * @return  the content as byte array
     * @throws ClientException
     */
    public byte[] fileContent(String path, Revision revision)
            throws ClientException
    {
        return fileContent(path, revision, revision);
    }

    /**
     * Retrieve the content of a file
     *
     * @param path        the path of the file
     * @param revision    the revision to retrieve
     * @param pegRevision the revision to interpret path
     * @return the content as byte array
     * @throws ClientException
     * @since 1.2
     */
    public native byte[] fileContent(String path, Revision revision,
                                     Revision pegRevision)
            throws ClientException;

    /**
     * Write the file's content to the specified output stream.  If
     * you need an InputStream, use a
     * PipedInputStream/PipedOutputStream combination.
     *
     * @param path        the path of the file
     * @param revision    the revision to retrieve
     * @param pegRevision the revision at which to interpret the path
     * @param stream      the stream to write the file's content to
     * @throws ClientException
     * @see <a href="http://java.sun.com/j2se/1.4.2/docs/api/java/io/PipedOutputStream.html">PipedOutputStream</a>
     * @see <a href="http://java.sun.com/j2se/1.4.2/docs/api/java/io/PipedInputStream.html">PipedInputStream</a>
     */
    public native void streamFileContent(String path, Revision revision,
                                         Revision pegRevision, int bufferSize,
                                         OutputStream stream)
            throws ClientException;

    /**
     * Rewrite the url's in the working copy
     * @param from      old url
     * @param to        new url
     * @param path      working copy path
     * @param recurse   recurse into subdirectories
     * @throws ClientException
     */
    public native void relocate(String from, String to, String path,
                                boolean recurse)
            throws ClientException;

    /**
     * Return for each line of the file, the author and the revision of the
     * last together with the content.
     * @deprecated
     * @param path          the path
     * @param revisionStart the first revision to show
     * @param revisionEnd   the last revision to show
     * @return  the content together with author and revision of last change
     * @throws ClientException
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
     * Retrieve the content together with the author, the revision and the date
     * of the last change of each line
     * @param path          the path
     * @param revisionStart the first revision to show
     * @param revisionEnd   the last revision to show
     * @param callback      callback to receive the file content and the other
     *                      information
     * @throws ClientException
     */
    public void blame(String path, Revision revisionStart,
                      Revision revisionEnd, BlameCallback callback)
            throws ClientException
    {
        blame(path, revisionEnd, revisionStart, revisionEnd, callback);
    }

    /**
     * Retrieve the content together with the author, the revision and the date
     * of the last change of each line
     * @param path          the path
     * @param pegRevision   the revision to interpret the path
     * @param revisionStart the first revision to show
     * @param revisionEnd   the last revision to show
     * @param callback      callback to receive the file content and the other
     *                      information
     * @throws ClientException
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
     * Retrieve the content together with the author, the revision and the date
     * of the last change of each line
     * @param path          the path
     * @param pegRevision   the revision to interpret the path
     * @param revisionStart the first revision to show
     * @param revisionEnd   the last revision to show
     * @param ignoreMimeType whether or not to ignore the mime-type
     * @param includeMergedRevisions whether or not to include extra merge
     *                      information
     * @param callback      callback to receive the file content and the other
     *                      information
     * @throws ClientException
     * @since 1.5
     */
    public native void blame(String path, Revision pegRevision,
                             Revision revisionStart,
                             Revision revisionEnd, boolean ignoreMimeType,
                             boolean includeMergedRevisions,
                             BlameCallback2 callback)
            throws ClientException;

    /**
     * @see SVNClientInterface#setConfigDirectory(String)
     */
    public native void setConfigDirectory(String configDir)
            throws ClientException;

    /**
     * Get the configuration directory
     * @return  the directory
     * @throws ClientException
     */
    public native String getConfigDirectory()
            throws ClientException;

    /**
     * cancel the active operation
     * @throws ClientException
     */
    public native void cancelOperation()
            throws ClientException;

    /**
     * Retrieves the working copy information for an item
     * @param path  path of the item
     * @return      the information object
     * @throws ClientException
     */
    public native Info info(String path)
            throws ClientException;

    /**
     * Add paths to a changelist
     * @param paths      paths to add to the changelist
     * @param changelist changelist name
     */
    public native void addToChangelist(String[] paths, String changelist)
            throws ClientException;

    /**
     * Remove paths from a changelist
     * @param paths      paths to remove from the changelist
     * @param changelist changelist name
     */
    public native void removeFromChangelist(String[] paths, String changelist)
            throws ClientException;

    /**
     * Recursively get the paths which belong to a changelist
     * @param changelist  changelist name
     * @param rootPath    the wc path under which to check
     */
    public native String[] getChangelist(String changelist, String rootPath)
            throws ClientException;

    /**
     *  Produce a compact "version number" for a working copy
     * @param path          path of the working copy
     * @param trailUrl      to detect switches of the whole working copy
     * @param lastChanged   last changed rather than current revisions
     * @return      the compact "version number"
     * @throws ClientException
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
     * @see SVNClientInterface#commit(String[], String, boolean, boolean)
     * @since 1.2
     */
    public long commit(String[] path, String message, boolean recurse,
                       boolean noUnlock)
            throws ClientException
    {
        return commit(path, message, Depth.infinityOrEmpty(recurse), noUnlock,
                      false, null);
    }

    /**
     * Lock a working copy item
     *
     * @param path  path of the item
     * @param comment
     * @param force break an existing lock
     * @throws ClientException
     * @since 1.2
     */
    public native void lock(String[] path, String comment, boolean force)
            throws ClientException;

    /**
     * Unlock a working copy item
     *
     * @param path  path of the item
     * @param force break an existing lock
     * @throws ClientException
     * @since 1.2
     */
    public native void unlock(String[] path, boolean force)
            throws ClientException;

    /**
     * @see SVNClientInterface#info2(String, Revision, Revision, int, InfoCallback)
     * @since 1.5
     */
    public Info2[] info2(String pathOrUrl, Revision revision,
                         Revision pegRevision, boolean recurse)
            throws ClientException
    {
        MyInfoCallback callback = new MyInfoCallback();
        info2(pathOrUrl, revision, pegRevision,
              Depth.infinityOrEmpty(recurse), callback);
        return callback.getInfoArray();
    }

    /**
     * Retrieve information about repository or working copy items.
     * @param pathOrUrl     the path or the url of the item
     * @param revision      the revision of the item to return
     * @param pegRevision   the revision to interpret pathOrUrl
     * @param depth         the depth to recurse
     * @param callback      a callback to receive the infos retreived
     * @since 1.5
     */
    public native void info2(String pathOrUrl, Revision revision,
                             Revision pegRevision, int depth,
                             InfoCallback callback)
            throws ClientException;

    /**
     * A private log message callback implementation used by thin wrappers.
     * Instances of this class are not thread-safe.
     */
    private class MyLogMessageCallback implements LogMessageCallback
    {
        private List messages = new ArrayList();

        public void singleMessage(ChangePath[] changedPaths,
                                  long revision,
                                  String author,
                                  long timeMicros,
                                  String message,
                                  boolean hasChildren)
        {
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
