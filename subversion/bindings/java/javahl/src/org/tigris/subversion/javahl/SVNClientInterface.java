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
 * This interface is the commom interface for all subversion
 * operations. It is implemented by SVNClient and SVNClientSynchronized
 */
public interface SVNClientInterface
{
    /**
    * release the native peer (should not depend on finalize)
    */
    void dispose();
    /**
      * Returns the last destination path submitted.
      * @deprecated
      * @return path in Subversion format.
      */
    String getLastPath();
    /**
     * List a directory or file of the working copy.
     *
     * @param path      Path to explore.
     * @param descend   Recurse into subdirectories if existant.
     * @param onServer  Request status information from server.
     * @param getAll    get status for uninteristing files (unchanged).
     * @return Array of Status entries.
     */
    Status[]status(String path, boolean descend, boolean onServer,
                   boolean getAll) throws ClientException;
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
    Status[]status(String path, boolean descend, boolean onServer,
                   boolean getAll, boolean noIgnore) throws ClientException;
    /**
     * Lists the directory entries of an url on the server.
     * @param url       the url to list
     * @param revision  the revision to list
     * @param recurse   recurse into subdirectories
     * @return  Array of DirEntry objects.
     */
    DirEntry[]list(String url, Revision revision, boolean recurse)
            throws ClientException;
    /**
     * Returns the status of a single file in the path.
     *
     * @param path      File to gather status.
     * @param onServer  Request status information from the server.
     * @return  the subversion status of the file.
     */
    Status singleStatus(String path, boolean onServer) throws ClientException;
    /**
     * Sets the username used for authentification.
     * @param username  the username
     */
    void username(String username);
    /**
     * Sets the password used for authification.
     * @param password  the password
     */
    void password(String password);
    /**
     * Register callback interface to supply username and password on demand
     * @param prompt the callback interface
     */
    void setPrompt(PromptUserPassword prompt);
    /**
     * Retrieve the log messages for an item
     * @param path          path or url to get the log message for.
     * @param revisionStart first revision to show
     * @param revisionEnd   last revision to show
     * @return array of LogMessages
     */
    LogMessage[] logMessages(String path, Revision revisionStart,
                             Revision revisionEnd) throws ClientException;
    /**
     * Retrieve the log messages for an item
     * @param path          path or url to get the log message for.
     * @param revisionStart first revision to show
     * @param revisionEnd   last revision to show
     * @param stopOnCopy    do not continue on copy operations
     * @return array of LogMessages
     */
    LogMessage[] logMessages(String path, Revision revisionStart,
                             Revision revisionEnd, boolean stopOnCopy)
            throws ClientException;
    /**
     * Executes a revision checkout.
     * @param moduleName name of the module to checkout.
     * @param destPath destination directory for checkout.
     * @param revision the revision to checkout.
     * @param recurse whether you want it to checkout files recursively.
     * @exception ClientException
     */
    long checkout(String moduleName, String destPath, Revision revision,
                  boolean recurse) throws ClientException;
    /**
     * Sets the notification callback used to send processing information back
     * to the calling program.
     * @param notify listener that the SVN library should call on many
     *               file operations.
     */
    void notification(Notify notify);
    /**
     * Sets the commit message handler. This allows more complex commit message
     * with the list of the elements to be commited as input.
     * @param messageHandler    callback for entering commit messages
     *                          if this is set the message parameter is ignored.
     */
    void commitMessageHandler(CommitMessage messageHandler);
    /**
     * Sets a file for deletion.
     * @param path      path or url to be deleted
     * @param message   if path is a url, this will be the commit message.
     * @param force     delete even when there are local modifications.
     * @exception ClientException
     */
    void remove(String[] path, String message, boolean force)
            throws ClientException;
    /**
     * Reverts a file to a pristine state.
     * @param path      path of the file.
     * @param recurse   recurse into subdirectories
     * @exception ClientException
     */
    void revert(String path, boolean recurse)throws ClientException;
    /**
     * Adds a file to the repository.
     * @param path      path to be added.
     * @param recurse   recurse into subdirectories
     * @exception ClientException
     */
    void add(String path, boolean recurse)throws ClientException;
    /**
     * Updates the directory or file from repository
     * @param path target file.
     * @param revision the revision number to checkout.
     *                 Revision.HEAD will checkout the
     *                 latest revision.
     * @param recurse recursively update.
     * @exception ClientException
     */
    long update(String path, Revision revision, boolean recurse)
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
    long commit(String[] path, String message, boolean recurse)
            throws ClientException;
    /**
     * Copies a versioned file with the history preserved.
     * @param srcPath   source path or url
     * @param destPath  destination path or url
     * @param message   commit message if destPath is an url
     * @param revision  source revision
     * @exception ClientException
     */
    void copy(String srcPath, String destPath, String message,
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
    void move(String srcPath, String destPath, String message,
              Revision revision, boolean force) throws ClientException;
    /**
     * Creates a directory directly in a repository or creates a
     * directory on disk and schedules it for addition.
     * @param path      directories to be created
     * @param message   commit message to used if path contains urls
     * @exception ClientException
     */
    void mkdir(String[] path, String message) throws ClientException;
    /**
     * Recursively cleans up a local directory, finishing any
     * incomplete operations, removing lockfiles, etc.
     * @param path a local directory.
     * @exception ClientException
     */
    void cleanup(String path) throws ClientException;
    /**
     * Removes the 'conflicted' state on a file.
     * @param path      path to cleanup
     * @param recurse   recurce into subdirectories
     * @exception ClientException
     */
    void resolved(String path, boolean recurse) throws ClientException;
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
    long doExport(String srcPath, String destPath, Revision revision,
                  boolean force) throws ClientException;
    /**
     * Update local copy to mirror a new url.
     * @param path      the working copy path
     * @param url       the new url for the working copy
     * @param revision  the new base revision of working copy
     * @param recurse   traverse into subdirectories
     * @exception ClientException
     */
    long doSwitch(String path, String url, Revision revision, boolean recurse)
            throws ClientException;
    /**
     * Import a file or directory into a repository directory  at
     * head.
     * @param path      the local path
     * @param url       the target url
     * @param message   the log message.
     * @param recurse   traverse into subdirectories
     * @exception ClientException
     */
    void doImport(String path, String url, String message, boolean recurse)
            throws ClientException;
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
    void merge(String path1, Revision revision1, String path2,
               Revision revision2, String localPath, boolean force,
               boolean recurse) throws ClientException;
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
    void diff(String target1, Revision revision1, String target2,
              Revision revision2, String outFileName, boolean recurse)
            throws ClientException;
    /**
     * Retrieves the properties of an item
     * @param path  the path of the item
     * @return array of property objects
     */
    PropertyData[] properties(String path) throws ClientException;
    /**
     * Sets one property of an item with a String value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    void propertySet(String path, String name, String value, boolean recurse)
            throws ClientException;
    /**
     * Sets one property of an item with a byte array value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    void propertySet(String path, String name, byte[] value, boolean recurse)
            throws ClientException;
    /**
     * Remove one property of an item.
     * @param path      path of the item
     * @param name      name of the property
     * @param recurse   remove the property also on subdirectories
     * @throws ClientException
     */
    void propertyRemove(String path, String name, boolean recurse)
            throws ClientException;
    /**
     * Create and sets one property of an item with a String value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    void propertyCreate(String path, String name, String value, boolean recurse)
            throws ClientException;
    /**
     * Create and sets one property of an item with a byte array value
     * @param path      path of the item
     * @param name      name of the property
     * @param value     new value of the property
     * @param recurse   set property also on the subdirectories
     * @throws ClientException
     */
    void propertyCreate(String path, String name, byte[] value, boolean recurse)
            throws ClientException;
    /**
     * Retrieve one revsision property of one item
     * @param path      path of the item
     * @param name      name of the property
     * @param rev       revision to retrieve
     * @return the Property
     * @throws ClientException
     */
    PropertyData revProperty(String path, String name, Revision rev)
            throws ClientException;
    /**
     * Retrieve one property of one iten
     * @param path      path of the item
     * @param name      name of property
     * @return the Property
     * @throws ClientException
     */
    PropertyData propertyGet(String path, String name) throws ClientException;
    /**
     *  Retrieve the content of a file
     * @param path      the path of the file
     * @param revision  the revision to retrieve
     * @return  the content as byte array
     * @throws ClientException
     */
    byte[] fileContent(String path, Revision revision) throws ClientException;
    /**
     * Rewrite the url's in the working copy
     * @param from      old url
     * @param to        new url
     * @param path      working copy path
     * @param recurse   recurse into subdirectories
     * @throws ClientException
     */
    void relocate(String from, String to, String path, boolean recurse) throws ClientException;
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
    byte[] blame(String path, Revision revisionStart, Revision revisionEnd) throws ClientException;
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
    void blame(String path, Revision revisionStart, Revision revisionEnd, BlameCallback callback) throws ClientException;
    /**
     * Set directory for the configuration information
     * @param configDir     path of the directory
     * @throws ClientException
     */
    void setConfigDirectory(String configDir) throws ClientException;
    /**
     * Get the configuration directory
     * @return  the directory
     * @throws ClientException
     */
    String getConfigDirectory() throws ClientException;
    /**
     * cancel the active operation
     * @throws ClientException
     */
    void cancelOperation() throws ClientException;

    /**
     * Retrieves the working copy information for an item
     * @param path  path of the item
     * @return      the information object
     * @throws ClientException
     */
    Info info(String path) throws ClientException;
}
