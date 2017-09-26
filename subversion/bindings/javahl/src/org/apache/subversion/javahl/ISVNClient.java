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
import java.util.Collection;
import java.util.List;
import java.util.Set;
import java.util.Map;

/**
 * This interface is the commom interface for all subversion
 * operations. It is implemented by SVNClient
 *
 * @since 1.7
 */
public interface ISVNClient
{
    /**
     * release the native peer (should not depend on finalize)
     */
    void dispose();

    /**
     * @return Version information about the underlying native libraries.
     */
    public Version getVersion();

    /**
     * @return Runtime version information about the loaded libsvn_client.
     */
    public RuntimeVersion getRuntimeVersion();

    /**
     * @return Extended version information about the underlying
     * native libraries and operating system.
     */
    public VersionExtended getVersionExtended(boolean verbose);

    /**
     * @return The name of the working copy's administrative
     * directory, which is usually <code>.svn</code>.
     * @see <a
     * href="http://svn.apache.org/repos/asf/subversion/trunk/notes/asp-dot-net-hack.txt">
     * Instructions on changing this as a work-around for the behavior of
     * ASP.Net on Windows.</a>
     */
    public String getAdminDirectoryName();

    /**
     * @param name The name of the directory to compare.
     * @return Whether <code>name</code> is that of a working copy
     * administrative directory.
     */
    public boolean isAdminDirectory(String name);

    /**
     * Return the status of the working copy and maybe repository.
     *
     * @param path        Path to explore.
     * @param depth       How deep to recurse into subdirectories.
     * @param onServer    Request status information from server.
     * @param onDisk      Check the working copy for local modifications.
     *                    A value of <code>false</code> only
     *                    has effect when <code>onServer</code> is
     *                    <code>true</code>.
     * @param getAll      get status for uninteresting (unchanged) files.
     * @param noIgnore    get status for normaly ignored files and directories.
     * @param ignoreExternals if externals are ignored during status
     * @param depthAsSticky When set, interpret <code>depth</code> as
     *                      the ambient depth of the working copy.
     * @param changelists changelists to filter by
     * @since 1.9
     */
    void status(String path, Depth depth,
                boolean onServer, boolean onDisk,
                boolean getAll, boolean noIgnore,
                boolean ignoreExternals, boolean depthAsSticky,
                Collection<String> changelists, StatusCallback callback)
            throws ClientException;

    /**
     * Return information about the status of the working copy and
     * maybe repository.
     * <p>
     * Behaves like the 1.9 version with
     *     <code>onDisk = true</code> and
     *     <code>depthAsSticky = false</code>.
     * @deprecated
     */
    @Deprecated
    void status(String path, Depth depth, boolean onServer,
                boolean getAll, boolean noIgnore, boolean ignoreExternals,
                Collection<String> changelists, StatusCallback callback)
            throws ClientException;

    /**
     * Lists the directory entries of a url on the server.
     * @param url             the url to list
     * @param revision        the revision to list
     * @param pegRevision     the revision to interpret url
     * @param patterns        optional glob patterns to filter the result
     * @param depth           the depth to recurse into subdirectories
     * @param direntFields    the fields to retrieve
     * @param fetchLocks      whether to fetch lock information
     * @param includeExternals whether to list external items
     * @param callback        the callback to receive the directory entries
     * @since 1.10
     */
    void list(String url, Revision revision, Revision pegRevision,
              List<String> patterns, Depth depth, int direntFields,
              boolean fetchLocks, boolean includeExternals,
              ListItemCallback callback)
            throws ClientException;

    /**
     * Lists the directory entries of a url on the server.
     * <p>
     * Behaves like the 1.10 version with
     *     <code>patterns = null</code> and
     *     <code>includeExternals = false</code>
     * @deprecated
     */
    @Deprecated
    void list(String url, Revision revision, Revision pegRevision,
              Depth depth, int direntFields, boolean fetchLocks,
              ListCallback callback)
            throws ClientException;

    /**
     * Sets the username used for authentication.
     * @param username The username, ignored if the empty string.  Set
     * to the empty string to clear it.
     * @throws IllegalArgumentException If <code>username</code> is
     * <code>null</code>.
     * @see #password(String)
     */
    void username(String username);

    /**
     * Sets the password used for authentication.
     * @param password The password, ignored if the empty string.  Set
     * to the empty string to clear it.
     * @throws IllegalArgumentException If <code>password</code> is
     * <code>null</code>.
     * @see #username(String)
     */
    void password(String password);

    /**
     * Register callback interface to supply username and password on demand.
     * This callback can also be used to provide theequivalent of the
     * <code>--no-auth-cache</code> and <code>--non-interactive</code> arguments
     * accepted by the command-line client.
     * @param prompt the callback interface
     */
    void setPrompt(AuthnCallback prompt);

    /**
     * Register callback interface to supply username and password on demand.
     * This callback can also be used to provide theequivalent of the
     * <code>--no-auth-cache</code> and <code>--non-interactive</code> arguments
     * accepted by the command-line client.
     * @param prompt the callback interface
     */
    @SuppressWarnings("deprecation")
    void setPrompt(UserPasswordCallback prompt);

    /**
     * Set callbacks for ra_svn tunnel handling.
     * @since 1.9
     */
    void setTunnelAgent(TunnelAgent tunnelAgent);

    /**
     * Retrieve the log messages for an item.
     * <p>
     * Behaves like the 1.10 version with <code>allRevProps = false</code>
     * @deprecated
     */
    @Deprecated
    void logMessages(String path, Revision pegRevision,
                     List<RevisionRange> ranges, boolean stopOnCopy,
                     boolean discoverPath, boolean includeMergedRevisions,
                     Set<String> revProps, long limit,
                     LogMessageCallback callback)
            throws ClientException;


    /**
     * Retrieve the log messages for an item.
     * @param path          path or url to get the log message for.
     * @param pegRevision   revision to interpret path
     * @param ranges        an array of revision ranges to show
     * @param stopOnCopy    do not continue on copy operations
     * @param discoverPath  returns the paths of the changed items in the
     *                      returned objects
     * @param includeMergedRevisions include log messages for revisions which
     *                               were merged.
     * @param revProps      the revprops to retrieve
     * @param allRevProps   if <code>true</code>, ignore the
     *                      <code>revProps</code> parameter and retrieve all
     *                      revision properties
     * @param limit         limit the number of log messages (if 0 or less no
     *                      limit)
     * @param callback      the object to receive the log messages
     * @since 1.10
     */
    void logMessages(String path, Revision pegRevision,
                     List<RevisionRange> ranges, boolean stopOnCopy,
                     boolean discoverPath, boolean includeMergedRevisions,
                     Set<String> revProps, boolean allRevProps,
                     long limit, LogMessageCallback callback)
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
     */
    long checkout(String moduleName, String destPath, Revision revision,
                  Revision pegRevision, Depth depth,
                  boolean ignoreExternals,
                  boolean allowUnverObstructions) throws ClientException;

    /**
     * Sets the notification callback used to send processing information back
     * to the calling program.
     * @param notify listener that the SVN library should call on many
     *               file operations.
     */
    void notification2(ClientNotifyCallback notify);

    /**
     * Set the conflict resolution callback.
     *
     * @param listener The conflict resolution callback.
     */
    void setConflictResolver(ConflictResolverCallback listener);

    /**
     * Set the progress callback.
     *
     * @param listener The progress callback.
     */
    void setProgressCallback(ProgressCallback listener);

    /**
     * Sets a file for deletion.
     * @param path      path or url to be deleted
     * @param force     delete even when there are local modifications.
     * @param keepLocal only remove the paths from the repository.
     * @param revpropTable A string-to-string mapping of revision properties
     *                     to values which will be set if this operation
     *                     results in a commit.
     * @param handler   the commit message callback
     * @throws ClientException
     */
    void remove(Set<String> path, boolean force, boolean keepLocal,
                Map<String, String> revpropTable, CommitMessageCallback handler,
                CommitCallback callback)
            throws ClientException;

    /**
     * Reverts set of files or directories to a pristine state.
     * @param path      A set of paths to revert.
     * @param depth     the depth to recurse into subdirectories
     * @param changelists changelists to filter by
     * @param clearChangelists If set, will clear changelist association
     *                         from the reverted paths.
     * @param metadataOnly Revert just the metadata (including conflict data)
     *                     and not the working files/dirs
     * @throws ClientException
     * @since 1.9
     */
    void revert(Set<String> paths, Depth depth,
                Collection<String> changelists,
                boolean clearChangelists,
                boolean metadataOnly)
            throws ClientException;

    /**
     * Reverts set of files or directories to a pristine state.
     * <p>
     * Behaves like the 1.9 version with <code>clearChangelists</code>
     * set to <code>false</code>;
     */
    void revert(Set<String> paths, Depth depth,
                Collection<String> changelists)
            throws ClientException;


    /**
     * Reverts a file to a pristine state.
     * @param path      path of the file.
     * @param depth     the depth to recurse into subdirectories
     * @param changelists changelists to filter by
     * @throws ClientException
     */
    void revert(String path, Depth depth, Collection<String> changelists)
            throws ClientException;

    /**
     * Adds a file to the repository.
     * <p>
     * <b>Note:</b> Behaves like the 1.8 version with
     * <code>noAutoProps</code> set to <code>false</code>.
     *
     * @param path      path to be added.
     * @param depth     the depth to recurse into subdirectories
     * @param force     if adding a directory and recurse true and path is a
     *                  directory, all not already managed files are added.
     * @param noIgnores if false, don't add files or directories matching
     *                  ignore patterns
     * @param addParents add any intermediate parents to the working copy
     * @throws ClientException
     */
    void add(String path, Depth depth, boolean force, boolean noIgnores,
             boolean addParents)
        throws ClientException;

    /**
     * Adds a file to the repository.
     * @param path      path to be added.
     * @param depth     the depth to recurse into subdirectories
     * @param force     if adding a directory and recurse true and path is a
     *                  directory, all not already managed files are added.
     * @param noIgnores if false, don't add files or directories matching
     *                  ignore patterns
     * @param noAutoProps if true, ignore any auto-props configuration
     * @param addParents add any intermediate parents to the working copy
     * @throws ClientException
     * @since 1.8
     */
    void add(String path, Depth depth, boolean force,
             boolean noIgnores, boolean noAutoProps, boolean addParents)
        throws ClientException;

    /**
     * Updates the directories or files from repository
     * @param path array of target files.
     * @param revision the revision number to update.
     *                 Revision.HEAD will update to the
     *                 latest revision.
     * @param depth  the depth to recursively update.
     * @param depthIsSticky if set, and depth is not {@link Depth#unknown},
     *                      then also set the ambient depth value to depth.
     * @param ignoreExternals if externals are ignored during update
     * @param allowUnverObstructions allow unversioned paths that obstruct adds
     * @throws ClientException
     */
    long[] update(Set<String> path, Revision revision, Depth depth,
                  boolean depthIsSticky, boolean makeParents,
                  boolean ignoreExternals, boolean allowUnverObstructions)
        throws ClientException;

    /**
     * Commits changes to the repository.
     * @param path            files to commit.
     * @param depth           how deep to recurse in subdirectories
     * @param noUnlock        do remove any locks
     * @param keepChangelist  keep changelist associations after the commit.
     * @param changelists  if non-null, filter paths using changelists
     * @param handler   the commit message callback
     * @param revpropTable A string-to-string mapping of revision properties
     *                     to values which will be set if this operation
     *                     results in a commit.
     * @throws ClientException
     */
    void commit(Set<String> path, Depth depth, boolean noUnlock,
                boolean keepChangelist, Collection<String> changelists,
                Map<String, String> revpropTable, CommitMessageCallback handler,
                CommitCallback callback)
            throws ClientException;

    /**
     * Copy versioned paths with the history preserved.
     *
     * @param sources A list of <code>CopySource</code> objects.
     * @param destPath Destination path or URL.
     * @param copyAsChild Whether to copy <code>srcPaths</code> as
     * children of <code>destPath</code>.
     * @param makeParents Whether to create intermediate parents
     * @param ignoreExternals Whether or not to process external definitions
     *                        as part of this operation.
     * @param metadataOnly Copy just the metadata and not the working files/dirs
     * @param pinExternals Whether or not to pin external definitions as part
     *                     of this operation.
     * @param externalsToPin The set of externals to pin.
     *            Keys are either local absolute paths (when the source of the
     *            copy is the working copy) or URLs within the repository
     *            (when the source is the repository) where an
     *            <code>svn:externals</code> property is defined.
     *            Values are lists of parsed {@link ExternalItem}
     *            objects from each external definitions.
     *            If <code>pinExternals</code> is <code>true</code>, only
     *            the externals in this set will be pinned; if this parameter
     *            is <code>null</code>, all externals will be pinned.
     *            If <code>pinExternals</code> is <code>false</code>,
     *            this parameter will be ignored.
     * @param revpropTable A string-to-string mapping of revision properties
     *                     to values which will be set if this operation
     *                     results in a commit.
     * @param handler   the commit message callback, may be <code>null</code>
     *                  if <code>destPath</code> is not a URL
     * @throws ClientException If the copy operation fails.
     * @throws NullPointerException if the <code>sources</code> list is empty.
     * @since 1.9
     */
    void copy(List<CopySource> sources, String destPath,
              boolean copyAsChild, boolean makeParents,
              boolean ignoreExternals, boolean metadataOnly,
              boolean pinExternals,
              Map<String, List<ExternalItem>> externalsToPin,
              Map<String, String> revpropTable,
              CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    /**
     * Copy versioned paths with the history preserved.
     * <p>
     * Behaves like the 1.9 version with
     *     <code>pinExternals</code> set to <code>false</code> and
     *     <code>externalsToPin</code> set to <code>null</code> and
     *     <code>metadataOnly</code> set to <code>false</code>.
     */
    void copy(List<CopySource> sources, String destPath,
              boolean copyAsChild, boolean makeParents,
              boolean ignoreExternals, Map<String, String> revpropTable,
              CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    /**
     * Move or rename versioned paths.
     *
     * @param srcPaths Source paths or URLs.
     * @param destPath Destination path or URL.
     * @param force Whether to perform the move even if local
     * modifications exist.
     * @param moveAsChild Whether to move <code>srcPaths</code> as
     * children of <code>destPath</code>.
     * @param makeParents Whether to create intermediate parents.
     * @param metadataOnly Move just the metadata and not the working files/dirs
     * @param allowMixRev If true use copy and delete without move tracking
     *                    when a srcPath is mixed-revision, if false return
     *                    an error when a srcPath is mixed-revision.
     * @param revpropTable A string-to-string mapping of revision properties
     *                     to values which will be set if this operation
     *                     results in a commit.
     * @param handler   the commit message callback, may be <code>null</code>
     *                  if <code>destPath</code> is not a URL
     * @throws ClientException If the move operation fails.
     * @since 1.8
     */
    void move(Set<String> srcPaths, String destPath, boolean force,
              boolean moveAsChild, boolean makeParents, boolean metadataOnly,
              boolean allowMixRev, Map<String, String> revpropTable,
              CommitMessageCallback handler, CommitCallback callback)
        throws ClientException;

    /**
     * @deprecated Provided for backward compatibility with 1.7. Passes
     *             metadataOnly false and allowMixRev true.
     */
    @Deprecated
    void move(Set<String> srcPaths, String destPath, boolean force,
              boolean moveAsChild, boolean makeParents,
              Map<String, String> revpropTable,
              CommitMessageCallback handler, CommitCallback callback)
        throws ClientException;

    /**
     * Creates a directory directly in a repository or creates a
     * directory on disk and schedules it for addition.
     * @param path      directories to be created
     * @param makeParents Whether to create intermediate parents
     * @param revpropTable A string-to-string mapping of revision properties
     *                     to values which will be set if this operation
     *                     results in a commit.
     * @param handler   the handler to use if paths contains URLs
     * @throws ClientException
     */
    void mkdir(Set<String> path, boolean makeParents,
               Map<String, String> revpropTable,
               CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    /**
     * Recursively cleans up a local directory, finishing any
     * incomplete operations, removing lockfiles, etc.
     * @param path a local directory.
     * @param breakLocks ### FIXME: Missing docstring in svn_client.h
     * @param clearDavCache ### FIXME: Missing docstring in svn_client.h
     * @param removeUnusedPristines ### FIXME: Missing docstring in svn_client.h
     * @param includeExternals Recurse into externals working copies
     *        and clean them up, too.
     * @throws ClientException
     * @since 1.9
     */
    void cleanup(String path,
                 boolean breakLocks,
                 boolean fixRecordedTimestamps,
                 boolean clearDavCache,
                 boolean removeUnusedPristines,
                 boolean includeExternals)
        throws ClientException;

    /**
     * Recursively cleans up a local directory, finishing any
     * incomplete operations, removing lockfiles, etc.
     * <p>
     * Behaves like the 1.9 version with <code>includeExternals</code>
     * set to <code>false<code>, and the other flags to
     * <code>true</code>.
     * @param path a local directory.
     * @throws ClientException
     */
    void cleanup(String path) throws ClientException;

    /**
     * Resolves the <i>conflicted</i> state on a WC path (or tree).
     * @param path The path to resolve.
     * @param depth How deep to recurse into child paths.
     * @param conflictResult Which version to choose in the event of a
     *                       conflict.
     * @throws SubversionException If an error occurs.
     */
    void resolve(String path, Depth depth, ConflictResult.Choice conflictResult)
        throws SubversionException;

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
     * @since 1.9
     */
    long doExport(String srcPath, String destPath, Revision revision,
                  Revision pegRevision, boolean force,
                  boolean ignoreExternals, boolean ignoreKeywords,
                  Depth depth, String nativeEOL)
            throws ClientException;

    /**
     * Exports the contents of either a subversion repository into a
     * 'clean' directory (meaning a directory with no administrative
     * directories).
     * <p>
     * <b>Note:</b> Behaves like the 1.9 version with
     * ignoreKeywords set to false.
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
     */
    long doExport(String srcPath, String destPath, Revision revision,
                  Revision pegRevision, boolean force, boolean ignoreExternals,
                  Depth depth, String nativeEOL)
            throws ClientException;

    /**
     * Update local copy to mirror a new url.
     * @param path      the working copy path
     * @param url       the new url for the working copy
     * @param revision  the new base revision of working copy
     * @param pegRevision the revision at which to interpret <code>path</code>
     * @param depth     how deep to traverse into subdirectories
     * @param depthIsSticky if set, and depth is not {@link Depth#unknown},
     *                      then also set the ambient depth value to depth.
     * @param ignoreExternals whether to process externals definitions
     * @param allowUnverObstructions allow unversioned paths that obstruct adds
     * @param ignoreAncestry whether to skip common ancestry sanity check between
                             <code>path</code> and <code>url</code>
     * @throws ClientException
     */
    long doSwitch(String path, String url, Revision revision,
                  Revision pegRevision, Depth depth, boolean depthIsSticky,
                  boolean ignoreExternals, boolean allowUnverObstructions,
                  boolean ignoreAncestry)
            throws ClientException;

    /**
     * Import a file or directory into a repository directory  at
     * head.
     * @param path      the local path
     * @param url       the target url
     * @param depth     depth to traverse into subdirectories
     * @param noIgnore  whether to add files matched by ignore patterns
     * @param noAutoProps if true, ignore any auto-props configuration
     * @param ignoreUnknownNodeTypes whether to ignore files which
     *                  the node type is not konwn, just as pipes
     * @param revpropTable A string-to-string mapping of revision properties
     *                     to values which will be set if this operation
     *                     results in a commit.
     * @param messageHandler   the commit message callback
     * @param commitCallback   the commit status callback
     * @throws ClientException
     * @since 1.8
     */
    void doImport(String path, String url, Depth depth,
                  boolean noIgnore, boolean noAutoProps,
                  boolean ignoreUnknownNodeTypes,
                  Map<String, String> revpropTable,
                  ImportFilterCallback importFilterCallback,
                  CommitMessageCallback messageHandler,
                  CommitCallback commitCallback)
            throws ClientException;


    /**
     * Import a file or directory into a repository directory  at
     * head.
     * <p>
     * <b>Note:</b> Behaves like the 1.8 version with noAutoProps
     * set to false and without the filtering option.
     *
     * @param path      the local path
     * @param url       the target url
     * @param depth     depth to traverse into subdirectories
     * @param noIgnore  whether to add files matched by ignore patterns
     * @param ignoreUnknownNodeTypes whether to ignore files which
     *                  the node type is not konwn, just as pipes
     * @param revpropTable A string-to-string mapping of revision properties
     *                     to values which will be set if this operation
     *                     results in a commit.
     * @param handler   the commit message callback
     * @param callback  the commit status callback
     * @throws ClientException
     */
    void doImport(String path, String url, Depth depth,
                  boolean noIgnore, boolean ignoreUnknownNodeTypes,
                  Map<String, String> revpropTable,
                  CommitMessageCallback handler, CommitCallback callback)
            throws ClientException;

    /**
     * Return an ordered list of suggested merge source URLs.
     * @param path The merge target path for which to suggest sources.
     * @param pegRevision Peg revision used to interpret path.
     * @return The list of URLs, empty if there are no suggestions.
     * @throws ClientException If an error occurs.
     */
    Set<String> suggestMergeSources(String path, Revision pegRevision)
            throws SubversionException;

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
     * @param ignoreMergeinfo ignore merge history, treat sources as unrelated
     * @param diffIgnoreAncestry always treat source files as related
     * @param dryRun         do not change anything
     * @param allowMixedRev  allow merging into a mixed-revision working copy
     * @param recordOnly     record mergeinfo but do not run merge
     * @throws ClientException
     * @since 1.9
     */
    void merge(String path1, Revision revision1, String path2,
               Revision revision2, String localPath, boolean force, Depth depth,
               boolean ignoreMergeinfo, boolean diffIgnoreAncestry,
               boolean dryRun, boolean allowMixedRev, boolean recordOnly)
            throws ClientException;

    /**
     * Merge changes from two paths into a new local path.
     * <p>
     * <b>Note:</b> Behaves like the 1.9 version with allowMixedRev
     *              always set to <code>true</code>.
     *
     * @param path1          first path or url
     * @param revision1      first revision
     * @param path2          second path or url
     * @param revision2      second revision
     * @param localPath      target local path
     * @param force          overwrite local changes
     * @param depth          how deep to traverse into subdirectories
     * @param ignoreMergeinfo ignore merge history, treat sources as unrelated
     * @param diffIgnoreAncestry always treat source files as related
     * @param dryRun         do not change anything
     * @param recordOnly     record mergeinfo but do not run merge
     * @throws ClientException
     * @since 1.8
     */
    void merge(String path1, Revision revision1, String path2,
               Revision revision2, String localPath, boolean force, Depth depth,
               boolean ignoreMergeinfo, boolean diffIgnoreAncestry,
               boolean dryRun, boolean recordOnly)
            throws ClientException;

    /**
     * Merge changes from two paths into a new local path.
     * <p>
     * <b>Note:</b> Behaves like the 1.8 version where ignoreAncestry
     * maps to both ignoreMergeinfo and diffIgnoreAncestry
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
     * @param recordOnly     record mergeinfo but do not run merge
     * @throws ClientException
     */
    void merge(String path1, Revision revision1, String path2,
               Revision revision2, String localPath, boolean force, Depth depth,
               boolean ignoreAncestry, boolean dryRun, boolean recordOnly)
            throws ClientException;

    /**
     * Merge set of revisions into a new local path.
     * @param path          path or url
     * @param pegRevision   revision to interpret path
     * @param revisions     revisions to merge; may be null, indicating that
     *                      the optimal range should be determined automatcially
     * @param localPath     target local path
     * @param force         overwrite local changes
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreMergeinfo ignore merge history, treat sources as unrelated
     * @param diffIgnoreAncestry always treat source files as related
     * @param dryRun        do not change anything
     * @param allowMixedRev  allow merging into a mixed-revision working copy
     * @param recordOnly    record mergeinfo but do not run merge
     * @throws ClientException
     * @since 1.9
     */
    void merge(String path, Revision pegRevision, List<RevisionRange> revisions,
               String localPath, boolean force, Depth depth,
               boolean ignoreMergeinfo, boolean diffIgnoreAncestry,
               boolean dryRun, boolean allowMixedRev, boolean recordOnly)
             throws ClientException;

    /**
     * Merge set of revisions into a new local path.
     * <p>
     * <b>Note:</b> Behaves like the 1.9 version with allowMixedRev
     *              always set to <code>true</code>.
     *
     * @param path          path or url
     * @param pegRevision   revision to interpret path
     * @param revisions     revisions to merge; may be null, indicating that
     *                      the optimal range should be determined automatcially
     * @param localPath     target local path
     * @param force         overwrite local changes
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreMergeinfo ignore merge history, treat sources as unrelated
     * @param diffIgnoreAncestry always treat source files as related
     * @param dryRun        do not change anything
     * @param recordOnly    record mergeinfo but do not run merge
     * @throws ClientException
     * @since 1.8
     */
    void merge(String path, Revision pegRevision, List<RevisionRange> revisions,
               String localPath, boolean force, Depth depth,
               boolean ignoreMergeinfo, boolean diffIgnoreAncestry,
               boolean dryRun, boolean recordOnly)
             throws ClientException;

    /**
     * Merge set of revisions into a new local path.
     * <p>
     * <b>Note:</b> Behaves like the 1.8 version where ignoreAncestry
     * maps to both ignoreMergeinfo and diffIgnoreAncestry.
     *
     * @param path          path or url
     * @param pegRevision   revision to interpret path
     * @param revisions     revisions to merge;
     *                      may be null, indicating that the optimal range
     *                      should be determined automatcially (new in 1.8)
     * @param localPath     target local path
     * @param force         overwrite local changes
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param dryRun        do not change anything
     * @param recordOnly    record mergeinfo but do not run merge
     * @throws ClientException
     */
    void merge(String path, Revision pegRevision, List<RevisionRange> revisions,
               String localPath, boolean force, Depth depth,
               boolean ignoreAncestry, boolean dryRun, boolean recordOnly)
             throws ClientException;

    /**
     * Perform a reintegration merge of path into localPath.
     * localPath must be a single-revision, infinite depth,
     * pristine, unswitched working copy -- in other words, it must
     * reflect a single revision tree, the "target".  The mergeinfo on
     * path must reflect that all of the target has been merged into it.
     * Then this behaves like a merge from the target's URL to the
     * localPath.
     *
     * The depth of the merge is always infinity.
     * @param path          path or url
     * @param pegRevision   revision to interpret path
     * @param localPath     target local path
     * @param dryRun        do not change anything
     * @throws ClientException
     * @deprecated Will be removed in a future release
     */
    @Deprecated
    void mergeReintegrate(String path, Revision pegRevision,
                          String localPath, boolean dryRun)
             throws ClientException;

    /**
     * Get mergeinfo for <code>path</code> at <code>pegRevision</code>.
     * @param path WC path or URL.
     * @param pegRevision peg revision at which to get the merge info for
     * <code>path</code>.
     * @return The merge history of <code>path</code>.
     * @throws SubversionException
     */
    Mergeinfo getMergeinfo(String path, Revision pegRevision)
        throws SubversionException;

    /**
     * Retrieve either merged or eligible-to-be-merged revisions.
     * @param kind                   kind of revisions to receive
     * @param pathOrUrl              target of merge
     * @param pegRevision            peg rev for pathOrUrl
     * @param mergeSourceUrl         the source of the merge
     * @param srcPegRevision         peg rev for mergeSourceUrl
     * @param srcStartRevision       lower bound of the source revision range
     * @param srcEndRevision         upper bound of the source revision range
     * @param discoverChangedPaths   return paths of changed items
     * @param depth                  the depth to recurse to
     * @param revProps               the revprops to retrieve
     * @param callback               the object to receive the log messages
     * @since 1.8
     */
    void getMergeinfoLog(Mergeinfo.LogKind kind, String pathOrUrl,
                         Revision pegRevision, String mergeSourceUrl,
                         Revision srcPegRevision,
                         Revision srcStartRevision, Revision srcEndRevision,
                         boolean discoverChangedPaths,
                         Depth depth, Set<String> revProps,
                         LogMessageCallback callback)
        throws ClientException;

    /**
     * Retrieve either merged or eligible-to-be-merged revisions.
     * <p>
     * <b>Note:</b> Behaves like the 1.8 version, with unspecified
     * revision range.
     *
     * @param kind                   kind of revisions to receive
     * @param pathOrUrl              target of merge
     * @param pegRevision            peg rev for pathOrUrl
     * @param mergeSourceUrl         the source of the merge
     * @param srcPegRevision         peg rev for mergeSourceUrl
     * @param discoverChangedPaths   return paths of changed items
     * @param depth                  the depth to recurse to
     * @param revProps               the revprops to retrieve
     * @param callback               the object to receive the log messages
     */
    void getMergeinfoLog(Mergeinfo.LogKind kind, String pathOrUrl,
                         Revision pegRevision, String mergeSourceUrl,
                         Revision srcPegRevision, boolean discoverChangedPaths,
                         Depth depth, Set<String> revProps,
                         LogMessageCallback callback)
        throws ClientException;

    /**
     * Display the differences between two paths
     * @param target1       first path or url
     * @param revision1     first revision
     * @param target2       second path or url
     * @param revision2     second revision
     * @param relativeToDir index path is relative to this path
     * @param outStream     the stream to which difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @param copiesAsAdds  if set, copied files will be shown in their
     *                      entirety, not as diffs from their sources
     * @param ignoreProps   don't show property diffs
     * @param propsOnly     show property changes only
     * @param options       additional options for controlling the output
     * @throws ClientException
     * @since 1.8
     */
    void diff(String target1, Revision revision1, String target2,
              Revision revision2, String relativeToDir, OutputStream outStream,
              Depth depth, Collection<String> changelists,
              boolean ignoreAncestry, boolean noDiffDeleted, boolean force,
              boolean copiesAsAdds, boolean ignoreProps, boolean propsOnly,
              DiffOptions options)
            throws ClientException;

    /**
     * Display the differences between two paths
     * @param target1       first path or url
     * @param revision1     first revision
     * @param target2       second path or url
     * @param revision2     second revision
     * @param relativeToDir index path is relative to this path
     * @param outFileName   file name where difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @param copiesAsAdds  if set, copied files will be shown in their
     *                      entirety, not as diffs from their sources
     * @param ignoreProps   don't show property diffs
     * @param propsOnly     show property changes only
     * @param options       additional options for controlling the output
     * @throws ClientException
     * @since 1.8
     */
    void diff(String target1, Revision revision1, String target2,
              Revision revision2, String relativeToDir, String outFileName,
              Depth depth, Collection<String> changelists,
              boolean ignoreAncestry, boolean noDiffDeleted, boolean force,
              boolean copiesAsAdds, boolean ignoreProps, boolean propsOnly,
              DiffOptions options)
            throws ClientException;

    /**
     * Display the differences between two paths
     * @param target1       first path or url
     * @param revision1     first revision
     * @param target2       second path or url
     * @param revision2     second revision
     * @param relativeToDir index path is relative to this path
     * @param outStream     the stream to which difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @param copiesAsAdds  if set, copied files will be shown in their
     *                      entirety, not as diffs from their sources
     * @param ignoreProps   don't show property diffs
     * @param propsOnly     show property changes only
     * @throws ClientException
     */
    void diff(String target1, Revision revision1, String target2,
              Revision revision2, String relativeToDir, OutputStream outStream,
              Depth depth, Collection<String> changelists,
              boolean ignoreAncestry, boolean noDiffDeleted, boolean force,
              boolean copiesAsAdds, boolean ignoreProps, boolean propsOnly)
            throws ClientException;

    /**
     * Display the differences between two paths
     * @param target1       first path or url
     * @param revision1     first revision
     * @param target2       second path or url
     * @param revision2     second revision
     * @param relativeToDir index path is relative to this path
     * @param outFileName   file name where difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @param copiesAsAdds  if set, copied files will be shown in their
     *                      entirety, not as diffs from their sources
     * @param ignoreProps   don't show property diffs
     * @param propsOnly     show property changes only
     * @throws ClientException
     */
    void diff(String target1, Revision revision1, String target2,
              Revision revision2, String relativeToDir, String outFileName,
              Depth depth, Collection<String> changelists,
              boolean ignoreAncestry, boolean noDiffDeleted, boolean force,
              boolean copiesAsAdds)
            throws ClientException;

    /**
     * Display the differences between two paths.
     * @param target        path or url
     * @param pegRevision   revision tointerpret target
     * @param startRevision first Revision to compare
     * @param endRevision   second Revision to compare
     * @param relativeToDir index path is relative to this path
     * @param outStream     the stream to which difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param changelists  if non-null, filter paths using changelists
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @param copiesAsAdds  if set, copied files will be shown in their
     *                      entirety, not as diffs from their sources
     * @param ignoreProps   don't show property diffs
     * @param propsOnly     show property changes only
     * @param options       additional options for controlling the output
     * @throws ClientException
     * @since 1.8
     */
    void diff(String target, Revision pegRevision, Revision startRevision,
              Revision endRevision, String relativeToDir, OutputStream outStream,
              Depth depth, Collection<String> changelists,
              boolean ignoreAncestry, boolean noDiffDeleted, boolean force,
              boolean copiesAsAdds, boolean ignoreProps, boolean propsOnly,
              DiffOptions options)
            throws ClientException;

    /**
     * Display the differences between two paths.
     * @param target        path or url
     * @param pegRevision   revision tointerpret target
     * @param startRevision first Revision to compare
     * @param endRevision   second Revision to compare
     * @param relativeToDir index path is relative to this path
     * @param outFileName   file name where difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param changelists  if non-null, filter paths using changelists
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @param copiesAsAdds  if set, copied files will be shown in their
     *                      entirety, not as diffs from their sources
     * @param ignoreProps   don't show property diffs
     * @param propsOnly     show property changes only
     * @param options       additional options for controlling the output
     * @throws ClientException
     * @since 1.8
     */
    void diff(String target, Revision pegRevision, Revision startRevision,
              Revision endRevision, String relativeToDir, String outFileName,
              Depth depth, Collection<String> changelists,
              boolean ignoreAncestry, boolean noDiffDeleted, boolean force,
              boolean copiesAsAdds, boolean ignoreProps, boolean propsOnly,
              DiffOptions options)
            throws ClientException;

    /**
     * Display the differences between two paths.
     * @param target        path or url
     * @param pegRevision   revision tointerpret target
     * @param startRevision first Revision to compare
     * @param endRevision   second Revision to compare
     * @param relativeToDir index path is relative to this path
     * @param outStream     the stream to which difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param changelists  if non-null, filter paths using changelists
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @param copiesAsAdds  if set, copied files will be shown in their
     *                      entirety, not as diffs from their sources
     * @param ignoreProps   don't show property diffs
     * @param propsOnly     show property changes only
     * @throws ClientException
     */
    void diff(String target, Revision pegRevision, Revision startRevision,
              Revision endRevision, String relativeToDir,
              OutputStream outStream,
              Depth depth, Collection<String> changelists,
              boolean ignoreAncestry, boolean noDiffDeleted, boolean force,
              boolean copiesAsAdds, boolean ignoreProps, boolean propsOnly)
            throws ClientException;

    /**
     * Display the differences between two paths.
     * @param target        path or url
     * @param pegRevision   revision tointerpret target
     * @param startRevision first Revision to compare
     * @param endRevision   second Revision to compare
     * @param relativeToDir index path is relative to this path
     * @param outFileName   file name where difference are written
     * @param depth         how deep to traverse into subdirectories
     * @param changelists  if non-null, filter paths using changelists
     * @param ignoreAncestry ignore if files are not related
     * @param noDiffDeleted no output on deleted files
     * @param force         diff even on binary files
     * @param copiesAsAdds  if set, copied files will be shown in their
     *                      entirety, not as diffs from their sources
     * @param ignoreProps   don't show property diffs
     * @param propsOnly     show property changes only
     * @throws ClientException
     */
    void diff(String target, Revision pegRevision, Revision startRevision,
              Revision endRevision, String relativeToDir, String outFileName,
              Depth depth, Collection<String> changelists,
              boolean ignoreAncestry, boolean noDiffDeleted, boolean force,
              boolean copiesAsAdds)
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
     * @param changelists  if non-null, filter paths using changelists
     * @param ignoreAncestry Whether to ignore unrelated files during
     * comparison.  False positives may potentially be reported if
     * this parameter <code>false</code>, since a file might have been
     * modified between two revisions, but still have the same
     * contents.
     * @param receiver As each is difference is found, this callback
     * is invoked with a description of the difference.
     *
     * @throws ClientException
     */
    void diffSummarize(String target1, Revision revision1,
                       String target2, Revision revision2,
                       Depth depth, Collection<String> changelists,
                       boolean ignoreAncestry, DiffSummaryCallback receiver)
            throws ClientException;

    /**
     * Produce a diff summary which lists the items changed between
     * path and revision pairs.
     *
     * @param target Path or URL.
     * @param pegRevision Revision at which to interpret
     * <code>target</code>.  If {@link Revision#UNSPECIFIED} or
     * <code>null</code>, behave identically to {@link
     * #diffSummarize(String, Revision, String, Revision, Depth,
     * Collection, boolean, DiffSummaryCallback)}, using
     * <code>path</code> for both of that method's targets.
     * @param startRevision Beginning of range for comparison of
     * <code>target</code>.
     * @param endRevision End of range for comparison of
     * <code>target</code>.
     * @param depth how deep to recurse.
     * @param changelists  if non-null, filter paths using changelists
     * @param ignoreAncestry Whether to ignore unrelated files during
     * comparison.  False positives may potentially be reported if
     * this parameter <code>false</code>, since a file might have been
     * modified between two revisions, but still have the same
     * contents.
     * @param receiver As each is difference is found, this callback
     * is invoked with a description of the difference.
     *
     * @throws ClientException
     */
    void diffSummarize(String target, Revision pegRevision,
                       Revision startRevision, Revision endRevision,
                       Depth depth, Collection<String> changelists,
                       boolean ignoreAncestry, DiffSummaryCallback receiver)
        throws ClientException;

    /**
     * Retrieves the properties of an item
     *
     * @param path        the path of the item
     * @param revision    the revision of the item
     * @param pegRevision the revision to interpret path
     * @param depth       the depth to recurse into subdirectories
     * @param changelists changelists to filter by
     * @param callback    the callback to use to return the properties
     * @throws ClientException
     */
    void properties(String path, Revision revision, Revision pegRevision,
                    Depth depth, Collection<String> changelists,
                    ProplistCallback callback)
            throws ClientException;

    /**
     * Retrieves the properties of an item, including inherited properties.
     *
     * @param path        the path of the item
     * @param revision    the revision of the item
     * @param pegRevision the revision to interpret path
     * @param depth       the depth to recurse into subdirectories
     * @param changelists changelists to filter by
     * @param callback    the callback to use to return the properties
     * @throws ClientException
     * @since 1.8
     */
    void properties(String path, Revision revision, Revision pegRevision,
                    Depth depth, Collection<String> changelists,
                    InheritedProplistCallback callback)
            throws ClientException;

    /**
     * Sets one property of an item with a String value
     *
     * @param paths   paths of the items
     * @param name    name of the property
     * @param value   new value of the property. Set value to
     * <code>null</code> to delete a property
     * @param depth   the depth to recurse into subdirectories
     * @param changelists changelists to filter by
     * @param force   do not check if the value is valid
     * @throws ClientException
     */
    void propertySetLocal(Set<String> paths, String name, byte[] value,
                          Depth depth, Collection<String> changelists,
                          boolean force)
            throws ClientException;

    /**
     * Sets one property of an item with a String value
     *
     * @param paths   paths of the items
     * @param name    name of the property
     * @param value   new value of the property. Set value to <code>
     * null</code> to delete a property
     * @param depth   the depth to recurse into subdirectories
     * @param changelists changelists to filter by
     * @param force   do not check if the value is valid
     * @param revpropTable A string-to-string mapping of revision properties
     *                     to values which will be set if this operation
     *                     results in a commit.
     * @throws ClientException
     */
    void propertySetRemote(String path, long baseRev, String name,
                           byte[] value, CommitMessageCallback handler,
                           boolean force, Map<String, String> revpropTable,
                           CommitCallback callback)
            throws ClientException;

    /**
     * Retrieve one revsision property of one item
     * @param path      path of the item
     * @param name      name of the property
     * @param rev       revision to retrieve
     * @return the Property
     * @throws ClientException
     */
    byte[] revProperty(String path, String name, Revision rev)
            throws ClientException;

    /**
     * Retrieve all revsision properties of one item
     * @param path      path of the item
     * @param rev       revision to retrieve
     * @return the Properties
     * @throws ClientException
     */
    Map<String, byte[]> revProperties(String path, Revision rev)
            throws ClientException;

    /**
     * set one revsision property of one item
     * @param path      path of the item
     * @param name      name of the property
     * @param rev       revision to retrieve
     * @param value     value of the property
     * @param originalValue the original value of the property.
     * @param force     use force to set
     * @throws ClientException
     */
    void setRevProperty(String path, String name, Revision rev, String value,
                        String originalValue, boolean force)
            throws ClientException;

    /**
     * Retrieve one property of one item
     * @param path      path of the item
     * @param name      name of property
     * @param revision  revision of the item
     * @param pegRevision the revision to interpret path
     * @return the Property
     * @throws ClientException
     */
    byte[] propertyGet(String path, String name, Revision revision,
                       Revision pegRevision, Collection<String> changelists)
            throws ClientException;

    byte[] propertyGet(String path, String name, Revision revision,
                       Revision pegRevision)
            throws ClientException;

    /**
     * Retrieve the content of a file
     *
     * Always expands keywords and never returns properties.
     *
     * @param path      the path of the file
     * @param revision  the revision to retrieve
     * @param pegRevision the revision to interpret path
     * @return  the content as byte array
     * @throws ClientException
     */
    byte[] fileContent(String path, Revision revision, Revision pegRevision)
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
     * @param returnProps whether to return the file's own (not inherited)
     *                    properties dalong with the contents
     * @return The file's properties if <code>returnProps</code> is
     *         set (which may yield an empty map), otherwise
     *         <code>null</code>.
     * @throws ClientException
     * @see java.io.PipedOutputStream
     * @see java.io.PipedInputStream
     * @since 1.9
     */
    Map<String, byte[]>
        streamFileContent(String path,
                          Revision revision, Revision pegRevision,
                          boolean expandKeywords, boolean returnProps,
                          OutputStream stream)
        throws ClientException;

    /**
     * Write the file's content to the specified output stream.  If
     * you need an InputStream, use a
     * PipedInputStream/PipedOutputStream combination.
     *
     * Always expands keywords and never returns properties.
     *
     * @param path        the path of the file
     * @param revision    the revision to retrieve
     * @param pegRevision the revision at which to interpret the path
     * @param stream      the stream to write the file's content to
     * @throws ClientException
     * @see java.io.PipedOutputStream
     * @see java.io.PipedInputStream
     */
    void streamFileContent(String path,
                           Revision revision, Revision pegRevision,
                           OutputStream stream)
        throws ClientException;

    /**
     * Rewrite the url's in the working copy
     * @param from      old url
     * @param to        new url
     * @param path      working copy path
     * @param ignoreExternals if externals are ignored during relocate
     * @throws ClientException
     */
    void relocate(String from, String to, String path, boolean ignoreExternals)
            throws ClientException;

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
     * @param options       additional options for controlling the output
     * @throws ClientException
     * @since 1.9
     */
    void blame(String path, Revision pegRevision, Revision revisionStart,
               Revision revisionEnd, boolean ignoreMimeType,
               boolean includeMergedRevisions,
               BlameCallback callback, DiffOptions options)
            throws ClientException;

    /**
     * Retrieve the content together with the author, the revision and the date
     * of the last change of each line
     * <p>
     * Behaves like the 1.9 version with <code>options</code> set to
     * their default values.
     */
    void blame(String path, Revision pegRevision, Revision revisionStart,
               Revision revisionEnd, boolean ignoreMimeType,
               boolean includeMergedRevisions,
               BlameCallback callback)
            throws ClientException;

    /**
     * Set directory for the configuration information, taking the
     * usual steps to ensure that Subversion's config file templates
     * exist in the specified location..  On Windows, setting a
     * non-<code>null</code> value will override lookup of
     * configuration in the registry.
     * @param configDir Path of the directory, or <code>null</code>
     * for the platform's default.
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
     * Set an event handler that will be called every time the
     * configuration is loaded by this client object.
     */
    void setConfigEventHandler(ConfigEvent configHandler)
            throws ClientException;

    /**
     * Return a reference to the installed configuration event
     * handler. The returned value may be <code>null</code>.
     */
    ConfigEvent getConfigEventHandler() throws ClientException;

    /**
     * cancel the active operation
     * @throws ClientException
     */
    void cancelOperation() throws ClientException;

    /**
     * Add paths to a changelist
     * @param paths       paths to add to the changelist
     * @param changelist  changelist name
     * @param depth       the depth to recurse
     * @param changelists changelists to filter by
     */
    void addToChangelist(Set<String> paths, String changelist, Depth depth,
                         Collection<String> changelists)
            throws ClientException;

    /**
     * Remove paths from a changelist
     * @param paths       paths to remove from the changelist
     * @param depth       the depth to recurse
     * @param changelists changelists to filter by
     */
    void removeFromChangelists(Set<String> paths, Depth depth,
                               Collection<String> changelists)
            throws ClientException;

    /**
     * Recursively get the paths which belong to a changelist
     * @param rootPath    the wc path under which to check
     * @param changelists the changelists to look under;
     *     if <code>null</code>, all changelists will be considered.
     * @param depth       the depth to recurse
     * @param callback    the callback to return the changelists through
     */
    void getChangelists(String rootPath, Collection<String> changelists,
                        Depth depth, ChangelistCallback callback)
            throws ClientException;

    /**
     * Lock a working copy item
     * @param path  path of the item
     * @param comment
     * @param force break an existing lock
     * @throws ClientException
     */
    void lock(Set<String> path, String comment, boolean force)
            throws ClientException;

    /**
     * Unlock a working copy item
     * @param path  path of the item
     * @param force break an existing lock
     * @throws ClientException
     */
    void unlock(Set<String> path, boolean force)
            throws ClientException;

    /**
     * Invoke <code>callback</code> to return information
     * <code>pathOrUrl</code> in <code>revision</code>.  The
     * information returned is system-generated metadata, not the sort
     * of "property" metadata created by users.
     * <p>
     * If both revision arguments are either <code>null</code> or
     * {@link Revision#START}, then information will be pulled solely
     * from the working copy; no network connections will be made.
     * <p>
     * Otherwise, information will be pulled from a repository.  The
     * actual node revision selected is determined by the
     * <code>pathOrUrl</code> as it exists in
     * <code>pegRevision</code>.  If <code>pegRevision</code> is
     * {@link Revision#START}, then it defaults to {@link
     * Revision#HEAD} for URLs or {@link Revision#WORKING} for WC
     * targets.
     * <p>
     * If <code>pathOrUrl</code> is not a local path, then if
     * <code>revision</code> is {@link Revision#PREVIOUS} (or some
     * other kind that requires a local path), an error will be
     * returned, because the desired revision cannot be determined.
     * <p>
     * If <code>pathOrUrl</code> is a file, just invoke the callback on it.  If it
     * is a directory, then descend according to <code>depth</code>.
     * <p>
     * @param pathOrUrl     the path or the url of the item
     * @param revision      the revision of the item to return
     * @param pegRevision   the revision to interpret pathOrUrl
     * @param depth         the depth to recurse
     * @param fetchExcluded when <code>true</code>, retrieve
     * information about nodes that are excluded from the working copy
     * @param fetchActualOnly when <code>true</code>, retrieve
     * information about node that are not versioned, but are still
     * tree conflicted.
     * @param includeExternals Recurs into externals directories
     * @param changelists   if non-null, filter paths using changelists
     * @param callback      a callback to receive the infos retrieved
     * @since 1.9
     */
    void info(String pathOrUrl,
              Revision revision, Revision pegRevision, Depth depth,
              boolean fetchExcluded, boolean fetchActualOnly,
              boolean includeExternals,
              Collection<String> changelists, InfoCallback callback)
        throws ClientException;

    /**
     * Retrieve information about repository or working copy items.
     * <p>
     * Behaves like the 1.9 version, with <code>fetchExcluded</code>
     * set to <code>false</code>, <code>fetchActualOnly</code> set to
     * <code>true</code> anf <code>includeExternals</code> set to
     * <code>false</code>.
     */
    void info2(String pathOrUrl, Revision revision, Revision pegRevision,
               Depth depth, Collection<String> changelists,
               InfoCallback callback)
        throws ClientException;

    /**
     * Produce a compact "version number" for a working copy
     * @param path          path of the working copy
     * @param trailUrl      to detect switches of the whole working copy
     * @param lastChanged   last changed rather than current revisions
     * @return      the compact "version number"
     * @throws ClientException
     */
    String getVersionInfo(String path, String trailUrl, boolean lastChanged)
            throws ClientException;

    /**
     * Recursively upgrade a working copy to a new metadata storage format.
     * @param path                  path of the working copy
     * @throws ClientException
     */
    void upgrade(String path)
            throws ClientException;

    /**
     * Apply a unidiff patch.
     * @param patchPath        the path of the patch
     * @param targetPath       the path to be patched
     * @param dryRun           whether to actually modify the local content
     * @param stripCount       how many leading path components should be removed
     * @param reverse          whether to reverse the patch
     * @param ignoreWhitespace whether to ignore whitespace
     * @param removeTempfiles  whether to remove temp files
     * @param callback         a handler to receive information as files are patched
     * @throws ClientException
     */
    void patch(String patchPath, String targetPath, boolean dryRun,
               int stripCount, boolean reverse, boolean ignoreWhitespace,
               boolean removeTempfiles, PatchCallback callback)
            throws ClientException;

    /**
     * Recursively vacuum a working copy, removing unnecessary data.
     * <p>
     * This method will report an error when
     * <code>removeUnversionedItems</code> or
     * <code>removeIgnoredItems</code> are set, and the working copy
     * is already locked. This prevents accidental corruption of the
     * working copy if this method is invoked while another client is
     * performing some other operation on the working copy.
     * @param path The path of the working copy directory.
     * @param removeUnversionedItems Remove unversioned items from the
     *        working copy after it has been successfully cleaned up.
     * @param removeIgnoredItems Remove unversioned items that are
     *        ignored by Subversion, after the working copy has been
     *        successfully cleaned up.
     * @param fixRecordedTimestamps Update timestamps recorded in the
     *        working copy database to their actual on-disk values.
     * @param removeUnusedPristines Remove pristine files that are not
     *        referenced by the working copy.
     * @param includeExternals Recurse into externals working copies
     *        and vacuum them, too.
     * @since 1.9
     */
    void vacuum(String path,
                boolean removeUnversionedItems,
                boolean removeIgnoredItems,
                boolean fixRecordedTimestamps,
                boolean removeUnusedPristines,
                boolean includeExternals)
            throws ClientException;

    /**
     * Open a persistent session to a repository.
     * <p>
     * <b>Note:</b> The session object inherits the progress callback,
     * configuration directory and authentication info.
     *
     * @param pathOrUrl A path in a working copy from which the
     *        session URL is derived, or the URL itself.
     * @throws remote.RetryOpenSession If the session URL was redirected
     * @throws SubversionException If an URL redirect cycle was detected
     * @throws ClientException
     * @since 1.9
     */
    ISVNRemote openRemoteSession(String pathOrUrl)
            throws ClientException, SubversionException;

    /**
     * Open a persistent session to a repository.
     * <p>
     * <b>Note:</b> The session object inherits the progress callback,
     * configuration directory and authentication info.
     *
     * @param pathOrUrl A path in a working copy from which the
     *        session URL is derived, or the URL itself.
     * @param retryAttempts The number of times to retry the operation
     *        if the given URL is redirected.
     * @throws IllegalArgumentException If <code>retryAttempts</code>
     *         is not positive
     * @throws remote.RetryOpenSession If the session URL was redirected
     * @throws SubversionException If an URL redirect cycle was detected
     * @throws ClientException
     * @since 1.9
     */
    ISVNRemote openRemoteSession(String pathOrUrl, int retryAttempts)
            throws ClientException, SubversionException;
}
