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

import org.apache.subversion.javahl.types.*;
import org.apache.subversion.javahl.callback.*;

import java.util.Date;
import java.util.Map;
import java.util.Set;
import java.io.OutputStream;

/**
 * Encapsulates an RA session object and related operations.
 * @see subversion/include/svn_ra.h#svn_ra_open
 * @since 1.9
 */
public interface ISVNRemote
{
    /**
     * Release the native peer (should not depend on finalize).
     * Also invalidates any existing editors related to this session.
     */
    void dispose();

    /**
     * Cancel the active operation, including any ongoing edits.
     * @throws ClientException
     */
    void cancelOperation() throws ClientException;

    /** Change the URL of the session to point to a new path in the
     * same repository.
     * @throws ClientException
     * @note The operation fails if the URL has a different repository
     *       root than the current session URL.
     */
    void reparent(String url) throws ClientException;

    /**
     * Get the current session URL.
     * @throws ClientException
     */
    String getSessionUrl() throws ClientException;

    /**
     * Return a path relative to the current session URL.
     * @param url Must be a child of the current session URL.
     * @throws ClientException
     */
    String getSessionRelativePath(String url) throws ClientException;

    /**
     * Return a path relative to the repository root URL associated with
     * current session URL.
     * @param url Must be a child of the repository root URL.
     * @throws ClientException
     */
    String getReposRelativePath(String url) throws ClientException;

    /**
     * Get the UUID of the session's repository.
     * @throws ClientException
     */
    String getReposUUID() throws ClientException;

    /**
     * Get the root URL of the session's repository.
     * @throws ClientException
     */
    String getReposRootUrl() throws ClientException;

    /**
     * Get the latest revision number from the session's repository.
     * @throws ClientException
     */
    long getLatestRevision() throws ClientException;

    /**
     * Get the latest revision number at the given time
     * from the session's repository.
     * @throws ClientException
     */
    long getRevisionByDate(Date date) throws ClientException;

    /**
     * Get the latest revision number at the given time, expressed as
     * the number of milliseconds since the epoch, from the session's
     * repository.
     * @throws ClientException
     */
    long getRevisionByTimestamp(long timestamp) throws ClientException;

    /**
     * Change the value of an unversioned property.
     * @param revision The revision to which the propery is attached
     * @param propertyName The name of the propery
     * @param oldValue The previous value of the property (see note below)
     * @param newValue The new value of the property. If <code>newValue</code>
     *        is <code>null</code>, the property will be deleted.
     *
     * @node If the server has Capability.atomic_revprops and
     *       <code>oldValue</code> is not <code>null</code>, and the
     *       present value of the propery is not <code>oldValue</code>
     *       (e.g., if another client changed the property), then
     *       the operation will fail.
     * @note If the server does not adveritse Capability.atomic_revprops,
     *       then <code>oldValue</code> <em>must</em> be <code>null</code>.
     *
     * @throws IllegalArgumentException if <code>oldValue</code> is not
     *         <code>null</code> and the server does not advertise the
     *         atomic_revprops capability.
     * @throws ClientException
     */
    void changeRevisionProperty(long revision,
                                String propertyName,
                                byte[] oldValue,
                                byte[] newValue)
            throws ClientException;

    /**
     * Return the set of unversioned properties set on <code>revision</code>
     * in the session's repository.
     * @throws ClientException
     */
    Map<String, byte[]> getRevisionProperties(long revision)
            throws ClientException;

    /**
     * Return the value of unversioned property <code>propertyName</code>
     * in <code>revision</code> in the session's repository.
     * Returns <code>null</code> if the property does not exist.
     * @throws ClientException
     */
    byte[] getRevisionProperty(long revision, String propertyName)
            throws ClientException;

    /**
     * Return an editor for committing changes to the session's
     * repository, setting the revision properties from
     * <code>revisionProperties</code>. The revisions being committed
     * against are passed to the editor functions. The root of the commit
     * is the session's URL.
     * <p>
     * <code>revisionProperties</code> is a hash mapping property names to
     * property values. The commit log message is expected to be in the
     * {@link Property#REV_LOG} element.  <code>revisionProperties</code>
     * can not contain either of {@link Property#REV_DATE} or
     * {@link Property#REV_AUTHOR}.
     * <p>
     * Before {@link ISVNEditor#complete()} returns, but after the commit
     * has succeeded, it will invoke <code>commitCallback</code> (if not
     * <code>null</code>) with filled-in {@link CommitInfo}.  If
     * <code>commitCallback</code> returns an error, that error will be
     * returned from {@link ISVNEditor#complete()}, otherwise
     * {@link ISVNEditor#complete()} will return successfully (unless it
     * encountered an error before invoking <code>commitCallback</code>).
     * The callback will not be called if the commit was a no-op
     * (i.e., nothing was committed).
     * <p>
     * <code>lockTokens</code>, if not <code>null</code>, is a hash
     * mapping paths (relative to the session's URL) to lock tokens.  The
     * server checks that the correct token is provided for each
     * committed, locked path.  <code>lockTokens</code> must live during
     * the whole commit operation.
     * <p>
     * If <cpde>keepLocks</code> is <cpde>true</code>, then do not release
     * locks on committed objects.  Else, automatically release such
     * locks.
     * <p>
     * The caller may not perform any remote operations using this session
     * before finishing the edit.
     * @throws ClientException
     */
    ISVNEditor getCommitEditor(Map<String, byte[]> revisionProperties,
                               CommitCallback commitCallback,
                               Set<Lock> lockTokens,
                               boolean keepLocks)
            throws ClientException;

    /**
     * Fetch the contents and properties of file <code>path</code> at
     * <code>revision</code>.  <code>revision</code> may be
     * {@link org.apache.subversion.javahl.types.Revision#SVN_INVALID_REVNUM}
     * indicating that the HEAD revision should be
     * used. <code>path</code> is interpreted relative to the
     * session's URL.
     * <p>

     * If <code>revision</code> is
     * {@link org.apache.subversion.javahl.types.Revision#SVN_INVALID_REVNUM}.
     * returns the actual revision that was retrieved; otherwise
     * returns <code>revision</code>.
     * <p>
     * If <code>contents</code> is not <code>null</code>, push the
     * contents of the file into the stream.
     * <p>
     * If <code>properties</code> is not <code>null</code>, set
     * <code>properties</code> to contain the properties of the file. This
     * means <em>all</em> properties: not just ones controlled by the
     * user and stored in the repository, but immutable ones generated
     * by the SCM system itself (e.g. 'wcprops', 'entryprops',
     * etc.). Any existing contents of the <code>properties</code> map
     * will be discarded by calling {@link java.util.Map#clear()}, if the
     * map implementation supports that operation.
     * <p>
     * The implementations of <code>contents</code> and
     * <code>properties</code> may not perform any ISVNRemote
     * operations using this session.
     * @return The revision of the file that was retreived.
     * @throws ClientException
     */
    long getFile(long revision, String path,
                 OutputStream contents,
                 Map<String, byte[]> properties)
            throws ClientException;

    /**
     * Fetch the contents and properties of directory <code>path</code>
     * at <code>revision</code>.  <code>revision</code> may be
     * {@link org.apache.subversion.javahl.types.Revision#SVN_INVALID_REVNUM},
     * indicating that the HEAD revision should be
     * used. <code>path</code> is interpreted relative to the
     * session's URL.
     * <p>
     * If <code>dirents</code> is not <code>null</code>, it will
     * contain all the entries of the directory; the keys will be the
     * entry basenames.  Any existing contente of the
     * <code>dirents</code> collection will be discarded by calling
     * {@link java.util.Map#clear()}, if the collection implementation
     * supports that operation.
     * <p>
     * <code>direntFields</code> controls which portions of the DirEntry
     * objects are filled in. To have them completely filled in, just pass
     * DirEntry.Fields.all, othewise pass a bitwise OR of any of the
     * DirEntry.Fields flags you would like to have.
     * <p>
     * If <code>properties</code> is not <code>null</code>, set
     * <code>properties</code> to contain the properties of the directory.
     * This means <em>all</em> properties: not just ones controlled by the
     * user and stored in the repository, but immutable ones generated
     * by the SCM system itself (e.g. 'wcprops', 'entryprops',
     * etc.). Any existing contents of the <code>properties</code> map
     * will be discarded by calling {@link java.util.Map#clear()}, if the
     * map implementation supports that operation.
     * <p>
     * The implementations of <code>dirents</code> and
     * <code>properties</code> may not perform any ISVNRemote
     * operations using this session.
     * @return The revision of the directory that was retreived.
     * @throws IllegalArgumentException if <code>direntFields</code>
     *         is less than or equal to 0.
     * @throws ClientException
     */
    long getDirectory(long revision, String path,
                      int direntFields,
                      Map<String, DirEntry> dirents,
                      Map<String, byte[]> properties)
            throws ClientException;

    // TODO: getMergeinfo
    // TODO: update
    // TODO: switch

    /**
     * Ask for a description of the status of a working copy with
     * respect to <code>revision</code> of the session's repository,
     * or the HEAD revision if <code>revision</code> is
     * {@link org.apache.subversion.javahl.types.Revision#SVN_INVALID_REVNUM}.
     * <p>
     * The client begins by providing a <code>statusEditor</code> to
     * the remote session; this editor must contain knowledge of where
     * the change will begin in the working copy.
     * <p>
     * In return, the client receives an {@link ISVNReporter}
     * instance, which it uses to describe its working copy by making
     * calls to its methods.
     * <p>
     * When finished, the client calls {@link ISVNReporter#finishReport}.
     * This results in a complete drive of <code>statusEditor</code>,
     * ending with {@link ISVNEditor#complete()}, to report,
     * essentially, what would be modified in the working copy were
     * the client to perform an update.  <code>statusTarget</code> is
     * an optional single path component that restricts the scope of
     * the status report to an entry in the directory represented by
     * the session's URL, or empty if the entire directory is meant to
     * be examined.
     * <p>
     * Get status as deeply as <code>depth</code> indicates.  If
     * <code>depth</code> is
     * {@link org.apache.subversion.javahl.types.Depth#unknown},
     * get the status down to the ambient depth of the working
     * copy. If <code>depth</code> is deeper than the working copy,
     * include changes that would be needed to populate the working
     * copy to that depth.
     * <p>
     * The caller may not perform any operations using this session
     * before finishing the report, and may not perform any operations
     * using this session from within the editing operations of
     * <code>statusEditor</code>.
     * <p>
     * <b>Note:</b> The reporter provided by this function does
     * <em>not</em> supply copy-from information to the editor
     * methods.
     * <p>
     * <b>Note:</b> In order to prevent pre-1.5 servers from doing
     * more work than needed, and sending too much data back, a
     * pre-1.5 'recurse' directive may be sent to the server, based on
     * <code>depth</code>.
     * @throws ClientException
     */
    ISVNReporter status(String statusTarget,
                        long revision, Depth depth,
                        ISVNEditor statusEditor)
            throws ClientException;

    // TODO: diff

    /**
     * Invoke <code>callback</code> for each log message from
     * <code>start</code> to <code>end</code>.  <code>start</code> may be greater or less than <code>end</code>;
     * this just controls whether the log messages are processed in descending
     * or ascending revision number order.
     * <p>
     * If <code>start</code> or <code>end</code> is
     * {@link org.apache.subversion.javahl.types.Revision#SVN_INVALID_REVNUM},
     * the HEAD revision is uses for that argument. If eiter is an
     * invaild non-existent revision, an error will be returned.
     * <p>
     * If <code>paths</code> is not <code>null</code> and has one or
     * more elements, then only show revisions in which at least one
     * of <code>paths</code> was changed (i.e., if file, text or props
     * changed; if dir, props changed or an entry was added or
     * deleted).
     * <p>
     * If <code>limit</code> is non-zero only invoke @a receiver on
     * the first code>limit</code> logs.
     * <p>
     * If <code>discoverPath</code> is set, then each call to
     * <code>callback</code> the list of changed paths in that
     * revision.
     * <p>
     * If <code>stopOnCopy</code> is set, copy history will not be
     * traversed (if any exists) when harvesting the revision logs for
     * each path.
     * <p>
     * If <code>includeMergedRevisions</code> is set, log information
     * for revisions which have been merged to @a targets will also be
     * returned.
     * <p>
     * If <code>revisionProperties</code> is <code>null</code>,
     * retrieve all revision properties; otherwise, retrieve only the
     * revision properties contained in the set (i.e. retrieve none if
     * the set is empty).
     * <p>
     * The implementation of <code>callback</code> may not perform any
     * operations using this session. If the invocation of
     * <code>callback</code> throws an exception, the operation will
     * stop.
     * <p>
     * <b>Note:</b> If <code>paths</code> is <code>null</code> or
     * empty, the result depends on the server.  Pre-1.5 servers will
     * send nothing; 1.5 servers will effectively perform the log
     * operation on the root of the repository.  This behavior may be
     * changed in the future to ensure consistency across all
     * pedigrees of server.
     * <p>
     * <b>Note:</b> Pre-1.5 servers do not support custom revprop
     * retrieval; <code>revisionProperties</code> is <code>null</code>
     * or contains a revprop other than svn:author, svn:date, or
     * svn:log, an not-implemented error is returned.
     *
     * @throws ClientException
     */
    void getLog(Iterable<String> paths,
                long startRevision, long endRevision, int limit,
                boolean stopOnCopy, boolean discoverPath,
                boolean includeMergedRevisions,
                Iterable<String> revisionProperties,
                LogMessageCallback callback)
            throws ClientException;

    /**
     * Return the kind of the node in path at revision.
     * @param path A path relative to the sessionn URL
     * @throws ClientException
     */
    NodeKind checkPath(String path, long revision)
            throws ClientException;

    // TODO: stat
    // TODO: getLocations
    // TODO: getLocationSegments
    // TODO: getFileRevisions
    // TODO: lock
    // TODO: unlock
    // TODO: getLock

    /**
     * Return a dictionary containing all locks on or below the given path.
     * @param path A path relative to the sessionn URL
     * @param depth The recursion depth
     * @note It is not considered an error for the path to not exist in HEAD.
     *       Such a search will simply return no locks.
     * @note This functionality is not available in pre-1.2 servers.
     * @throws ClientException
     */
    Map<String, Lock> getLocks(String path, Depth depth)
            throws ClientException;

    // TODO: replayRange
    // TODO: replay
    // TODO: getDeletedRevision
    // TODO: getInheritedProperties

    /**
     * Check if the server associated with this session has
     * the given <code>capability</code>.
     * @throws ClientException
     */
    boolean hasCapability(Capability capability) throws ClientException;

    /**
     * Enumeration of known capabilities of the repository and server.
     */
    public enum Capability
    {
        /**
         * The capability of understanding operation depth.
         * @since 1.5
         */
        depth ("depth"),

        /**
         * The capability of doing the right thing with merge-tracking
         * information.
         * @since 1.5
         */
        mergeinfo ("mergeinfo"),

        /**
         * The capability of retrieving arbitrary revprops in #getLog().
         * @since 1.5
         */
        log_revprops ("log-revprops"),

        /**
         * The capability of replaying a directory in the
         * repository (partial replay).
         * @since 1.5
         */
        partial_replay ("partial-replay"),

        /**
         * The capability of including revision properties in a commit.
         * @since 1.5
         */
        commit_revprops ("commit-revprops"),

        /**
         * The capability of specifying (and atomically verifying) expected
         * preexisting values when modifying revprops.
         * @since 1.7
         */
        atomic_revprops ("atomic-revprops"),

        /**
         * The capability to get inherited properties.
         * @since 1.8
         */
        inherited_props ("inherited-props"),

        /**
         * The capability of a server to automatically ephemeral
         * transaction properties.
         * @since 1.8
         */
        ephemeral_txnprops ("ephemeral-txnprops"),

        /**
         * The capability of a server to walk revisions backwards in
         * #getFileRevisions().
         * @since 1.8
         */
        get_file_revs_reversed ("get-file-revs-reversed");

        private Capability(String token)
        {
            this.token = token;
        }

        public String toString()
        {
            return token;
        }

        private String token;
    }
}
