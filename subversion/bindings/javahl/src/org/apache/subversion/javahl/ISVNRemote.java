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
     * Cancel the active operation.
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
     * Create a commit editor instance, rooted at the current session URL.
     * @throws ClientException
     */
    ISVNEditor getCommitEditor() throws ClientException;

    /**
     * Return the kind of the node in path at revision.
     * @param path A path relative to the sessionn URL
     * @throws ClientException
     */
    NodeKind checkPath(String path, long revision)
            throws ClientException;

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
