package org.tigris.subversion.javahl;

/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
      *
      * @return path in Subversion format.
      */
    String getLastPath();

    /**
     * List directory entries of a URL.
     *
     * @param path Path to explore.
     * @param descend Recurse into subdirectories if existant.
     * @return Array of Status entries.
     */
    Status[]status(String path, boolean descend, boolean onServer, boolean getAll) throws ClientException;

    /**
     *
     * @param url
     * @param revision
     * @param recurse
     * @return
     */
    DirEntry[]list(String url, Revision revision, boolean recurse) throws ClientException;

    /**
     * Returns the status of a single file in the path.
     *
     * @param path File to gather status.
     * @return a Status with Statis.isVersioned = FALSE
     */
    Status singleStatus(String path, boolean onServer) throws ClientException;

    /**
     * Sets the username.
     */
    void username(String username);

    /**
     * Sets the password.
     */
    void password(String password);

    /**
     * Register callback interface to supply username and password on demand
     * @param prompt the callback interface
     */
    void setPrompt(PromptUserPassword prompt);

    /**
    * Loads the log messages result set, clearing old result sets.
    * This usually requires authentication.
    * You can use the constants Revision::START and
    * Revision::HEAD
    *
    * @param path
    * @param revisionStart
    * @param revisionEnd
    */
    LogMessage[] logMessages(String path, Revision revisionStart, Revision revisionEnd) throws ClientException;

    /**
     * Executes a revision checkout.
     * @param moduleName name of the module to checkout.
     * @param destPath destination directory for checkout.
     * @param revision the revision number to checkout. If the number is -1
     *                 then it will checkout the latest revision.
     * @param recurse whether you want it to checkout files recursively.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void checkout(String moduleName, String destPath, Revision revision, boolean recurse) throws ClientException;

    /**
     * Sets the notification function and baton that the C library
     * uses to send processing information back to the calling program.
     * This must be called before calling the other methods in this class.
     * @param notify function that the SVN library should call when
     *               checking out each file.
     */
    void notification(Notify notify);

    /**
      * Sets a file for deletion.
      * @exception org.tigris.subversion.javahl.ClientException
      */
    void remove(String[] path, String message, boolean force)throws ClientException;

    /**
      * Reverts a file to a pristine state.
      * @exception org.tigris.subversion.javahl.ClientException
      */
    void revert(String path, boolean recurse)throws ClientException;

    /**
     * Adds a file to the repository.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void add(String path, boolean recurse)throws ClientException;

    /**
     * Updates the directory.
     * @param path target file.
     * @param revision the revision number to checkout.
     *                 Revision::HEAD will checkout the
     *                 latest revision.
     * @param recurse recursively update.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void update(String path, Revision revision, boolean recurse) throws ClientException;

    /**
     * Commits changes to the repository. This usually requires
     * authentication, see Auth.
     * @return Returns a long representing the revision. It returns a
     *         -1 if the revision number is invalid.
     * @param path files to commit.
     * @param message log message.
     * @param recurse whether the operation should be done recursively.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    long commit(String[] path, String message, boolean recurse) throws ClientException;

    /**
     * Copies a versioned file with the history preserved.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void copy(String srcPath, String destPath, String Message, Revision revision) throws ClientException;

    /**
     * Moves or renames a file.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void move(String srcPath, String destPath, String message, Revision revision, boolean force) throws ClientException;

    /**
     * Creates a directory directly in a repository or creates a
     * directory on disk and schedules it for addition. If <i>path</i>
     * is a URL then authentication is usually required, see Auth.
     * @param message log message.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void mkdir(String[] path, String message) throws ClientException;

    /**
     * Recursively cleans up a local directory, finishing any
     * incomplete operations, removing lockfiles, etc.
     * @param path a local directory.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void cleanup(String path) throws ClientException;

    /**
     * Removes the 'conflicted' state on a file.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void resolved(String path, boolean recurse) throws ClientException;

    /**
     * Exports the contents of either a subversion repository into a
     * 'clean' directory (meaning a directory with no administrative
     * directories).
     * @exception org.tigris.subversion.javahl.ClientException
     * @param destPath a destination path that must not already exist.
     */
    void doExport(String srcPath, String destPath, Revision revision,boolean force) throws ClientException;

    /**
     * Update local copy to mirror a new url. This excapsulates the
     * svn_client_switch() client method.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void doSwitch(String path, String url, Revision revision, boolean recurse) throws ClientException;

    /**
     * Import file or directory PATH into repository directory URL at
     * head.  This usually requires authentication, see Auth.
     * @param message log message.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void doImport(String path, String url, String message, boolean recurse) throws ClientException;

    /**
     * Merge changes from two paths into a new local path.
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void merge(String path1, Revision revision1, String path2, Revision revision2, String localPath, boolean force, boolean recurse) throws ClientException;

    /**
     * diff display the differences between two paths
     * @exception org.tigris.subversion.javahl.ClientException
     */
    void diff(String target1, Revision revision1, String target2, Revision revision2, String outFileName, boolean recurse) throws ClientException;

    /**
     * Returns the number of properties found.
     */
    PropertyData[] properties(String path) throws ClientException;

    void propertySet(String path, String name, String value, boolean recurse) throws ClientException;

    void propertySet(String path, String name, byte[] value, boolean recurse) throws ClientException;

    void propertyCreate(String path, String name, String value, boolean recurse) throws ClientException;

    void propertyCreate(String path, String name, byte[] value, boolean recurse) throws ClientException;

    PropertyData revProperty(String path, String name, Revision rev) throws ClientException;

    /**
     * get the given property
     */
    PropertyData propertyGet(String path, String name) throws ClientException;

    byte[] fileContent(String path, Revision revision) throws ClientException;

    void relocate(String from, String to, String path, boolean recurse) throws ClientException;

    byte[] blame(String path, Revision revisionStart, Revision revisionEnd, boolean strict) throws ClientException;
}
