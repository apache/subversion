package org.tigris.subversion.javahl;

/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
/**
 * This is the main interface class. All subversion commandline client svn &
 * svnversion operation are implemented in this class. This class is not
 * threadsafe. If you need threadsafe access, use SVNClientSynchronized
 */
public class SVNClient implements SVNClientInterface
{
    /**
     * load the needed native library
     */
    static
    {
        /*
         * first try to load the library by the new name.
         * if that fails, try to load the library by the old name.
         */
        try
        {
            System.loadLibrary("svnjavahl-1");
        }
        catch(UnsatisfiedLinkError ex)
        {
            try
            {
                System.loadLibrary("libsvnjavahl-1");
            }
            catch (UnsatisfiedLinkError e)
            {
                System.loadLibrary("svnjavahl");
            }
        }
    }

    /**
     * Standard empty contructor, builds just the native peer.
     */
    public SVNClient()
    {
        cppAddr = ctNative();
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
      * Returns the last destination path submitted.
      * @deprecated
      * @return path in Subversion format.
      */
     public native String getLastPath();
    /**
     * List a directory or file of the working copy.
     *
     * @param path      Path to explore.
     * @param descend   Recurse into subdirectories if existant.
     * @param onServer  Request status information from server.
     * @param getAll    get status for uninteristing files (unchanged).
     * @return Array of Status entries.
     */
    public Status[]status(String path, boolean descend, boolean onServer,
                          boolean getAll) throws ClientException
    {
        return status(path, descend, onServer, getAll, false);
    }
    /**
     * List a directory or file of the working copy.
     *
     * @param path      Path to explore.
     * @param descend   Recurse into subdirectories if existant.
     * @param onServer  Request status information from server.
     * @param getAll    get status for uninteristing files (unchanged).
     * @param noIgnore  get status for normaly ignored files and directories.
     * @return Array of Status entries.
     */
    public native Status[] status(String path, boolean descend,
                                  boolean onServer, boolean getAll,
                                  boolean noIgnore) throws ClientException;

    /**
     * Lists the directory entries of an url on the server.
     * @param url       the url to list
     * @param revision  the revision to list
     * @param recurse   recurse into subdirectories
     * @return  Array of DirEntry objects.
     */
    public native DirEntry[]list(String url, Revision revision, boolean recurse)
            throws ClientException;

    /**
     * Returns the status of a single file in the path.
     *
     * @param path      File to gather status.
     * @param onServer  Request status information from the server.
     * @return  the subversion status of the file.
     */
    public native Status singleStatus(String path, boolean onServer)
            throws ClientException;
    /**
     * Sets the username used for authentification.
     * @param username  the username
     */
    public native void username(String username);
    /**
     * Sets the password used for authification.
     * @param password  the password
     */
    public native void password(String password);
    /**
     * Register callback interface to supply username and password on demand
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
                                    Revision revisionEnd) throws ClientException
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
        return logMessages(path, revisionStart, revisionEnd, stopOnCopy, false);
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
    public native LogMessage[] logMessages(String path, Revision revisionStart,
                                           Revision revisionEnd,
                                           boolean stopOnCopy, boolean discoverPath)
            throws ClientException;
    /**
     * Executes a revision checkout.
     * @param moduleName name of the module to checkout.
     * @param destPath destination directory for checkout.
     * @param revision the revision to checkout.
     * @param recurse whether you want it to checkout files recursively.
     * @exception ClientException
     */
    public native long checkout(String moduleName, String destPath,
                                Revision revision, boolean recurse)
            throws ClientException;
    /**
     * Sets the notification callback used to send processing information back
     * to the calling program.
     * @param notify listener that the SVN library should call on many
     *               file operations.
     */
    public native void notification(Notify notify);
    /**
     * Sets the commit message handler. This allows more complex commit message
     * with the list of the elements to be commited as input.
     * @param messageHandler    callback for entering commit messages
     *                          if this is set the message parameter is ignored.
     */
    public native void commitMessageHandler(CommitMessage messageHandler);
    /**
     * Sets a file for deletion.
     * @param path      path or url to be deleted
     * @param message   if path is a url, this will be the commit message.
     * @param force     delete even when there are local modifications.
     * @exception ClientException
     */
    public native void remove(String[] path, String message, boolean force)
            throws ClientException;
    /**
     * Reverts a file to a pristine state.
     * @param path      path of the file.
     * @param recurse   recurse into subdirectories
     * @exception ClientException
     */
    public native void revert(String path, boolean recurse)
            throws ClientException;
    /**
     * Adds a file to the repository.
     * @param path      path to be added.
     * @param recurse   recurse into subdirectories
     * @exception ClientException
     */
    public native void add(String path, boolean recurse)throws ClientException;
    /**
     * Updates the directory or file from repository
     * @param path target file.
     * @param revision the revision number to checkout.
     *                 Revision.HEAD will checkout the
     *                 latest revision.
     * @param recurse recursively update.
     * @exception ClientException
     */
    public native long update(String path, Revision revision, boolean recurse)
            throws ClientException;
    /**
     * Commits changes to the repository.
     * @param path      files to commit.
     * @param message   log message.
     * @param recurse   whether the operation should be done recursively.
     * @return Returns a long representing the revision. It returns a
     *         -1 if the revision number is invalid.
     * @exception ClientException
     */
    public native long commit(String[] path, String message, boolean recurse)
            throws ClientException;
    /**
     * Copies a versioned file with the history preserved.
     * @param srcPath   source path or url
     * @param destPath  destination path or url
     * @param message   commit message if destPath is an url
     * @param revision  source revision
     * @exception ClientException
     */
    public native void copy(String srcPath, String destPath, String message,
                            Revision revision) throws ClientException;
    /**
     * Moves or renames a file.
     * @param srcPath   source path or url
     * @param destPath  destination path or url
     * @param message   commit message if destPath is an url
     * @param revision  source revision
     * @param force     even with local modifications.
     * @exception ClientException
     */
    public native void move(String srcPath, String destPath, String message,
                            Revision revision, boolean force)
            throws ClientException;
    /**
     * Creates a directory directly in a repository or creates a
     * directory on disk and schedules it for addition.
     * @param path      directories to be created
     * @param message   commit message to used if path contains urls
     * @exception ClientException
     */
    public native void mkdir(String[] path, String message)
            throws ClientException;
    /**
     * Recursively cleans up a local directory, finishing any
     * incomplete operations, removing lockfiles, etc.
     * @param path a local directory.
     * @exception ClientException
     */
    public native void cleanup(String path) throws ClientException;
    /**
     * Removes the 'conflicted' state on a file.
     * @param path      path to cleanup
     * @param recurse   recurce into subdirectories
     * @exception ClientException
     */
    public native void resolved(String path, boolean recurse)
            throws ClientException;
    /**
     * Exports the contents of either a subversion repository into a
     * 'clean' directory (meaning a directory with no administrative
     * directories).
     * @param srcPath   the url of the repository path to be exported
     * @param destPath  a destination path that must not already exist.
     * @param revision  the revsion to be exported
     * @param force     set if it is ok to overwrite local files
     * @exception ClientException
     */
    public native long doExport(String srcPath, String destPath,
                                Revision revision, boolean force)
            throws ClientException;
    /**
     * Update local copy to mirror a new url.
     * @param path      the working copy path
     * @param url       the new url for the working copy
     * @param revision  the new base revision of working copy
     * @param recurse   traverse into subdirectories
     * @exception ClientException
     */
    public native long doSwitch(String path, String url, Revision revision,
                                boolean recurse) throws ClientException;
    /**
     * Import a file or directory into a repository directory  at
     * head.
     * @param path      the local path
     * @param url       the target url
     * @param message   the log message.
     * @param recurse   traverse into subdirectories
     * @exception ClientException
     */
    public native void doImport(String path, String url, String message,
                                boolean recurse) throws ClientException;
    /**
     * Merge changes from two paths into a new local path.
     * @param path1         first path or url
     * @param revision1     first revision
     * @param path2         second path or url
     * @param revision2     second revision
     * @param localPath     target local path
     * @param force         overwrite local changes
     * @param recurse       traverse into subdirectories
     * @exception ClientException
     */
    public native void merge(String path1, Revision revision1, String path2,
                             Revision revision2, String localPath,
                             boolean force, boolean recurse)
            throws ClientException;
    /**
     * Display the differences between two paths
     * @param target1       first path or url
     * @param revision1     first revision
     * @param target2       second path or url
     * @param revision2     second revision
     * @param outFileName   file name where difference are written
     * @param recurse       traverse into subdirectories
     * @exception ClientException
     */
    public native void diff(String target1, Revision revision1, String target2,
                            Revision revision2, String outFileName,
                            boolean recurse) throws ClientException;
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
     */
    public native PropertyData[] properties(String path, Revision revision)
            throws ClientException;
    /**
     * Sets one property of an item with a String value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    public native void propertySet(String path, String name, String value,
                                   boolean recurse) throws ClientException;
    /**
     * Sets one property of an item with a byte array value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    public native void propertySet(String path, String name, byte[] value,
                                   boolean recurse) throws ClientException;
    /**
     * Remove one property of an item.
     * @param path      path of the item
     * @param name      name of the property
     * @param recurse   remove the property also on subdirectories
     * @throws ClientException
     */
    public native void propertyRemove(String path, String name,
                                      boolean recurse) throws ClientException;
    /**
     * Create and sets one property of an item with a String value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    public native void propertyCreate(String path, String name, String value,
                                      boolean recurse) throws ClientException;
    /**
     * Create and sets one property of an item with a byte array value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    public native void propertyCreate(String path, String name, byte[] value,
                                      boolean recurse) throws ClientException;
    /**
     * Retrieve one revsision property of one item
     * @param path      path of the item
     * @param name      name of the property
     * @param rev       revision to retrieve
     * @return the Property
     * @throws ClientException
     */
    public native PropertyData revProperty(String path, String name,
                                           Revision rev) throws ClientException;

    /**
     * Retrieve all revsision properties of one item
     * @param path      path of the item
     * @param rev       revision to retrieve
     * @return the Properties
     * @throws ClientException
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
     *
     */
    public native PropertyData propertyGet(String path, String name,
                                           Revision revision)
            throws ClientException;

    /**
     *  Retrieve the content of a file
     * @param path      the path of the file
     * @param revision  the revision to retrieve
     * @return  the content as byte array
     * @throws ClientException
     */
    public native byte[] fileContent(String path, Revision revision)
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
                                boolean recurse) throws ClientException;
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
    public native byte[] blame(String path, Revision revisionStart,
                               Revision revisionEnd) throws ClientException;
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
    public native void blame(String path, Revision revisionStart,
                             Revision revisionEnd, BlameCallback callback)
            throws ClientException;
    /**
     * Set directory for the configuration information
     * @param configDir     path of the directory
     * @throws ClientException
     */
    public native void setConfigDirectory(String configDir)
            throws ClientException;
    /**
     * Get the configuration directory
     * @return  the directory
     * @throws ClientException
     */
    public native String getConfigDirectory() throws ClientException;
    /**
     * cancel the active operation
     * @throws ClientException
     */
    public native void cancelOperation() throws ClientException;

    /**
     * Retrieves the working copy information for an item
     * @param path  path of the item
     * @return      the information object
     * @throws ClientException
     */
    public native Info info(String path) throws ClientException;

    /**
     *  Produce a compact "version number" for a working copy
     * @param path          path of the working copy
     * @param trailUrl      to detect switches of the whole working copy
     * @param lastChanged   last changed rather than current revisions
     * @return      the compact "version number"
     * @throws ClientException
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
}
