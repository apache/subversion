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
 * This class provides a threadsafe wrapped for SVNClient
 */
public class SVNClientSynchronized implements SVNClientInterface
{
    /**
     * the wrapped object, which does all the work
     */
    private SVNClient worker;

    /**
     * our class, we synchronize on that.
     */
    static private Class clazz = SVNClientSynchronized.class;

    public SVNClientSynchronized()
    {
        worker = new SVNClient();
    }

    /**
     * release the native peer (should not depend on finalize)
     */
    public void dispose()
    {
        worker.dispose();
    }

    /**
     * Returns the last destination path submitted.
     *
     * @return path in Subversion format.
     */
    public String getLastPath()
    {
        synchronized(clazz)
        {
            return worker.getLastPath();
        }
    }

    /**
     * List directory entries of a URL.
     *
     * @param path Path to explore.
     * @param descend Recurse into subdirectories if existant.
     * @return Array of Status entries.
     */
    public Status[] status(String path, boolean descend, boolean onServer, boolean getAll) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.status(path, descend, onServer, getAll);
        }
    }

    /**
     *
     * @param url
     * @param revision
     * @param recurse
     * @return
     */
    public DirEntry[] list(String url, Revision revision, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.list(url, revision, recurse);
        }
    }

    /**
     * Returns the status of a single file in the path.
     *
     * @param path File to gather status.
     * @return a Status with Statis.isVersioned = FALSE
     */
    public Status singleStatus(String path, boolean onServer) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.singleStatus(path, onServer);
        }
    }

    /**
     * Sets the username.
     */
    public void username(String username)
    {
        synchronized(clazz)
        {
            worker.username(username);
        }
    }

    /**
     * Sets the password.
     */
    public void password(String password)
    {
        synchronized(clazz)
        {
            worker.password(password);
        }
    }

    /**
     * Register callback interface to supply username and password on demand
     * @param prompt the callback interface
     */
    public void setPrompt(PromptUserPassword prompt)
    {
        synchronized(clazz)
        {
            worker.setPrompt(prompt);
        }
    }

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
    public LogMessage[] logMessages(String path, Revision revisionStart, Revision revisionEnd) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.logMessages(path, revisionStart, revisionEnd);
        }
    }

    /**
     * Executes a revision checkout.
     * @param moduleName name of the module to checkout.
     * @param destPath destination directory for checkout.
     * @param revision the revision number to checkout. If the number is -1
     *                 then it will checkout the latest revision.
     * @param recurse whether you want it to checkout files recursively.
     * @exception ClientException
     */
    public void checkout(String moduleName, String destPath, Revision revision, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.checkout(moduleName, destPath, revision, recurse);
        }
    }

    /**
     * Sets the notification function and baton that the C library
     * uses to send processing information back to the calling program.
     * This must be called before calling the other methods in this class.
     * @param notify function that the SVN library should call when
     *               checking out each file.
     */
    public void notification(Notify notify)
    {
        synchronized(clazz)
        {
            worker.notification(notify);
        }
    }

    /**
     * Sets a file for deletion.
     * @exception ClientException
     */
    public void remove(String[] path, String message, boolean force) throws ClientException
    {
        synchronized(clazz)
        {
            worker.remove(path, message, force);
        }
    }

    /**
     * Reverts a file to a pristine state.
     * @exception ClientException
     */
    public void revert(String path, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.revert(path, recurse);
        }
    }

    /**
     * Adds a file to the repository.
     * @exception ClientException
     */
    public void add(String path, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.add(path, recurse);
        }
    }

    /**
     * Updates the directory.
     * @param path target file.
     * @param revision the revision number to checkout.
     *                 Revision::HEAD will checkout the
     *                 latest revision.
     * @param recurse recursively update.
     * @exception ClientException
     */
    public void update(String path, Revision revision, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.update(path, revision, recurse);
        }
    }

    /**
     * Commits changes to the repository. This usually requires
     * authentication, see Auth.
     * @return Returns a long representing the revision. It returns a
     *         -1 if the revision number is invalid.
     * @param path files to commit.
     * @param message log message.
     * @param recurse whether the operation should be done recursively.
     * @exception ClientException
     */
    public long commit(String[] path, String message, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.commit(path, message, recurse);
        }
    }

    /**
     * Copies a versioned file with the history preserved.
     * @exception ClientException
     */
    public void copy(String srcPath, String destPath, String Message, Revision revision) throws ClientException
    {
        synchronized(clazz)
        {
            worker.copy(srcPath, destPath, Message, revision);
        }
    }

    /**
     * Moves or renames a file.
     * @exception ClientException
     */
    public void move(String srcPath, String destPath, String message, Revision revision, boolean force) throws ClientException
    {
        synchronized(clazz)
        {
            worker.move(srcPath, destPath, message, revision, force);
        }
    }

    /**
     * Creates a directory directly in a repository or creates a
     * directory on disk and schedules it for addition. If <i>path</i>
     * is a URL then authentication is usually required, see Auth.
     * @param message log message.
     * @exception ClientException
     */
    public void mkdir(String[] path, String message) throws ClientException
    {
        synchronized(clazz)
        {
            worker.mkdir(path, message);
        }
    }

    /**
     * Recursively cleans up a local directory, finishing any
     * incomplete operations, removing lockfiles, etc.
     * @param path a local directory.
     * @exception ClientException
     */
    public void cleanup(String path) throws ClientException
    {
        synchronized(clazz)
        {
            worker.cleanup(path);
        }
    }

    /**
     * Removes the 'conflicted' state on a file.
     * @exception ClientException
     */
    public void resolved(String path, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.resolved(path,recurse);
        }
    }

    /**
     * Exports the contents of either a subversion repository into a
     * 'clean' directory (meaning a directory with no administrative
     * directories).
     * @exception ClientException
     * @param destPath a destination path that must not already exist.
     */
    public void doExport(String srcPath, String destPath, Revision revision, boolean force) throws ClientException
    {
        synchronized(clazz)
        {
            worker.doExport(srcPath, destPath, revision, force);
        }
    }

    /**
     * Update local copy to mirror a new url. This excapsulates the
     * svn_client_switch() client method.
     * @exception ClientException
     */
    public void doSwitch(String path, String url, Revision revision, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.doSwitch(path, url, revision, recurse);
        }
    }

    /**
     * Import file or directory PATH into repository directory URL at
     * head.  This usually requires authentication, see Auth.
     * @param message log message.
     * @exception ClientException
     */
    public void doImport(String path, String url, String message, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.doImport(path, url, message, recurse);
        }
    }

    /**
     * Merge changes from two paths into a new local path.
     * @exception ClientException
     */
    public void merge(String path1, Revision revision1, String path2, Revision revision2, String localPath, boolean force, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.merge(path1, revision1, path2, revision2, localPath, force, recurse);
        }
    }

    /**
     * diff display the differences between two paths
     * @exception ClientException
     */
    public void diff(String target1, Revision revision1, String target2, Revision revision2, String outFileName, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.diff(target1, revision1, target2, revision2, outFileName, recurse);
        }
    }

    /**
     * Returns the number of properties found.
     */
    public PropertyData[] properties(String path) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.properties(path);
        }
    }

    public void propertySet(String path, String name, String value, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.propertySet(path, name, value, recurse);
        }
    }

    public void propertySet(String path, String name, byte[] value, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.propertySet(path, name, value, recurse);
        }
    }

    public void propertyCreate(String path, String name, String value, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.propertyCreate(path, name, value, recurse);
        }
    }

    public void propertyCreate(String path, String name, byte[] value, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.propertyCreate(path, name, value, recurse);
        }
    }

    public PropertyData revProperty(String path, String name, Revision rev) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.revProperty(path, name, rev);
        }
    }

    /**
     * get the given property
     */
    public PropertyData propertyGet(String path, String name) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.propertyGet(path, name);
        }
    }

    public byte[] fileContent(String path, Revision revision) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.fileContent(path, revision);
        }
    }

    public void relocate(String from, String to, String path, boolean recurse) throws ClientException
    {
        synchronized(clazz)
        {
            worker.relocate(from, to, path, recurse);
        }
    }

    public byte[] blame(String path, Revision revisionStart, Revision revisionEnd, boolean strict) throws ClientException
    {
        synchronized(clazz)
        {
            return worker.blame(path,revisionStart, revisionEnd, strict);
        }
    }

}
