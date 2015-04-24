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

import java.io.InputStream;
import java.util.Map;

/**
 * Commit/update/status editor interface.
 * <p>
 * <b>This interface is EXPERIMENTAL.
 * It may change or be removed in a future version of JavaHL</b>
 * @see <a href="http://svn.apache.org/repos/asf/subversion/trunk/subversion/include/private/svn_editor.h">svn_editor.h</a>
 *      for all restrictions on driving an editor.
 * @since 1.9
 */
public interface ISVNEditor
{
    /**
     * Release the native peer (should not depend on finalize),
     * and abort the edit if it has not been completed yet.
     */
    void dispose();

    /**
     * Create a new directory at <code>relativePath</code>.
     * The immediate parent of <code>relativePath</code> is expected to exist.
     * <p>
     * For descriptions of <code>properties</code> and
     * <code>replacesRevision</code>, see #addFile().
     * <p>
     * A complete listing of the immediate children of
     * <code>relativePath</code> that will be added subsequently is
     * given in <code>children</code>. <code>children</code> is a
     * collection of const strings, each giving the basename of an
     * immediate child. It is an error to pass <code>null</code> for
     * <code>children</code>; use an empty collection to indicate that
     * the new directory will have no children.
     *
     * @throws ClientException
     */
    void addDirectory(String relativePath,
                      Iterable<String> children,
                      Map<String, byte[]> properties,
                      long replacesRevision)
            throws ClientException;

    /**
     * Create a new file at <code>relativePath</code>.
     * The immediate parent of <code>relativePath</code> is expected to exist.
     * <p>
     * The file's contents are specified in <code>contents</code>
     * which has a checksum matching <code>checksum</code>. Both
     * values must be non-<code>null</code>.
     * <p>
     * Set the properties of the new file to
     * <code>properties</code>. If no properties are being set on the
     * new file, <code>properties</code> must be empty. It is an error
     * to pass <code>null</code> for <code>properties</code>.
     * <p>
     * If this add is expected to replace a previously existing file,
     * symlink or directory at <code>relativePath</code>, the revision
     * number of the node to be replaced must be given in
     * <code>replacesRevision</code>. Otherwise,
     * <code>replacesRevision</code> must be
     * Revision.SVN_INVALID_REVNUM.
     * <p>
     * <b>Note:</b> It is not allowed to call a "delete" followed by
     * an "add" on the same path. Instead, an "add" with
     * <code>replacesRevision</code> set accordingly <em>must</em> be used.
     * <p>
     * <b>Note:</b> The <code>contents</code> stream's lifetime must not
     *      extend beyond the scope of this function. An
     *      implementation <b>must</b> close the stream after
     *      consuming its contents.
     *
     * @throws ClientException
     */
    void addFile(String relativePath,
                 Checksum checksum,
                 InputStream contents,
                 Map<String, byte[]> properties,
                 long replacesRevision)
            throws ClientException;

    /**
     * Create a new symbolic link at <code>relativePath</code>, with a
     * link target of <code>target</code>. The immediate parent of
     * <code>relativePath</code> is expected to exist.
     *
     * For descriptions of <code>properties</code> and
     * <code>replacesRevision</code>, see #addFile().
     *
     * @throws ClientException
     */
    void addSymlink(String relativePath,
                    String target,
                    Map<String, byte[]> properties,
                    long replacesRevision)
            throws ClientException;

    /**
     * Create an "absent" node of kind <code>kind</code> at
     * <code>relativePath</code>. The immediate parent of
     * <code>relativePath</code> is expected to exist.
     *
     * For a description of <code>replacesRevision</code>, see #addFile().
     *
     * @throws ClientException
     */
    void addAbsent(String relativePath,
                   NodeKind kind,
                   long replacesRevision)
            throws ClientException;

    /**
     * Alter the properties of the directory at <code>relativePath</code>.
     * <p>
     * <code>revision</code> specifies the revision at which the
     * receiver should expect to find this node. That is,
     * <code>relativePath</code> at the start of the whole edit and
     * <code>relativePath</code> at <code>revision</code> must lie
     * within the same node-rev (aka location history segment). This
     * information may be used to catch an attempt to alter and
     * out-of-date directory. If the directory does not have a
     * corresponding revision in the repository (e.g. it has not yet
     * been committed), then <code>revision</code> should be
     * Revision.SVN_INVALID_REVNUM.
     * <p>
     * If any changes to the set of children will be made in the
     * future of the edit drive, then <code>children</code>
     * <em>must</em> specify the resulting set of children. See
     * #addDirectory() for the format of <code>children</code>.  If
     * not changes will be made, then NULL may be specified.
     * <p>
     * For a description of <code>properties</code>, see
     * #addFile(). If no changes to the properties will be made
     * (ie. only future changes to the set of children), then
     * <code>properties</code> may be <code>null</code>.
     *
     * @throws ClientException
     */
    void alterDirectory(String relativePath,
                        long revision,
                        Iterable<String> children,
                        Map<String, byte[]> properties)
            throws ClientException;

    /**
     * Alter the contents and/or the properties of the file at
     * <code>relativePath</code> with <code>revision</code> as its
     * expected revision. See #alterDirectory() for more information
     * about <code>revision</code>.
     * <p>
     * If <code>contents</code> is non-<code>null</code>, then the
     * stream will be copied to the file, and its checksum must match
     * <code>checksum</code> (which must also be
     * non-<code>null</code>). If <code>contents</code> is
     * <code>null</code>, then <code>checksum</code> must also be
     * <code>null</code>, and no change will be applied to the file's
     * contents.
     * <p>
     * If <code>properties</code> is non-<code>null</code>, then the
     * properties will be applied.
     * <p>
     * For a description of <code>checksum</code> and
     * <code>contents</code>, see #addFile().
     * <p>
     * This function allows <code>properties</code> to be
     * <code>null</code>, but the parameter is otherwise described by
     * #addFile().
     * <p>
     * <b>Note:</b> The <code>contents</code> stream's lifetime must not
     *      extend beyond the scope of this function. An
     *      implementation <b>must</b> close the stream after
     *      consuming its contents.
     *
     * @throws ClientException
     */
    void alterFile(String relativePath,
                   long revision,
                   Checksum checksum,
                   InputStream contents,
                   Map<String, byte[]> properties)
            throws ClientException;

    /**
     * Alter the target and/or the properties of the symlink at
     * <code>relativePath</code> with <code>revision</code> as its
     * expected revision. See #alterDirectory() for more information
     * about <code>revision</code>.
     * <p>
     * If <code>target</code> is non-<code>null</code>, then the
     * symlink's target will be updated.
     * <p>
     * If <code>properties</code> is non-<code>null</code>, then the
     * properties will be applied.
     * <p>
     * The target and/or the properties must be changed. It is an
     * error to pass <code>null</code> for both <code>target</code>
     * and <code>properties</code>.
     * <p>
     * This function allows <code>properties</code> to be
     * <code>null</code>, but the parameter is otherwise described by
     * #addFile().
     *
     * @throws ClientException
     */
    void alterSymlink(String relativePath,
                      long revision,
                      String target,
                      Map<String, byte[]> properties)
            throws ClientException;

    /**
     * Delete the existing node at <code>relativePath</code>, expected
     * to be identical to revision <code>revision</code> of that path.
     *
     * @throws ClientException
     */
    void delete(String relativePath,
                long revision)
            throws ClientException;

    /**
     * Move the node at <code>sourceRelativePath</code> to
     * <code>destinationRelativePath</code>.
     * <p>
     * For a description of <code>replacesRevision</code>, see #addFile().
     * <p>
     * <b>Note:</b> See the general instructions on paths for this API.
     * Sice the <code>sourceRelativePath</code> argument must generally be
     * able to reference any node in the repository, the implication
     * is that the editor's root must be the repository root.
     *
     * @throws ClientException
     */
    void copy(String sourceRelativePath,
              long sourceRevision,
              String destinationRelativePath,
              long replacesRevision)
            throws ClientException;

    /**
     * Move the node at <code>sourceRelativePath</code> to
     * <code>destinationRelativePath</code>.
     * <p>
     * <code>sourceRevision</code> specifies the revision at which the
     * receiver should expect to find this node.  That is,
     * <code>sourceRelativePath</code> at the start of the whole edit
     * and<code>sourceRelativePath</code> at
     * <code>sourceRevision</code> must lie within the same node-rev
     * (aka history-segment).  This is just like the #delete().
     * <p>
     * For a description of <code>replacesRevision</code>, see #addFile().
     *
     * @throws ClientException
     */
    void move(String sourceRelativePath,
              long sourceRevision,
              String destinationRelativePath,
              long replacesRevision)
            throws ClientException;

    /**
     * Signal that the edit has been completed successfully.
     * After this method is called, the editor is considered closed.
     *
     * @throws ClientException
     */
    void complete() throws ClientException;

    /**
     * Signal that the edit transmission was not successful.
     * After this method is called, the editor is considered closed.
     *
     * @throws ClientException
     */
    void abort() throws ClientException;


    /**
     * Callback interface for providing the base contents of a file
     * that is being modified.
     * @see ISVNRemote.getCommitEditor(Map,CommitCallback,Set,boolean,ISVNEditor.ProvideBaseCallback,ISVNEditor.ProvidePropsCallback,ISVNEditor.GetNodeKindCallback)
     */
    public interface ProvideBaseCallback
    {
        public static class ReturnValue
        {
            /**
             * @param contents The base ({@link Revision#BASE}) contents
             *         of the file.
             * @param revision The base revision number.
             */
            public ReturnValue(InputStream contents, long revision)
            {
                this.contents = contents;
                this.revision = revision;
            }

            final InputStream contents;
            final long revision;
        }

        /**
         * Returns the base contents and revision number of the file.
         * @param reposRelpath The repository path of the file,
         *        relative to the session base URL.
         */
        ReturnValue getContents(String reposRelpath);
    }

    /**
     * Callback interface for providing the base properties of a file
     * or directory that is being modified.
     * @see ISVNRemote.getCommitEditor(Map,CommitCallback,Set,boolean,ISVNEditor.ProvideBaseCallback,ISVNEditor.ProvidePropsCallback,ISVNEditor.GetNodeKindCallback)
     */
    public interface ProvidePropsCallback
    {
        public static class ReturnValue
        {
            /**
             * @param properties The base ({@link Revision#BASE}) properties
             *         of the file or directory.
             * @param revision The base revision number.
             */
            public ReturnValue(Map<String, byte[]> properties, long revision)
            {
                this.properties = properties;
                this.revision = revision;
            }

            final Map<String, byte[]> properties;
            final long revision;
        }

        /**
         * Returns the base properties and revision number of the file
         * or directory.
         * @param reposRelpath The repository path of the file or directory,
         *        relative to the session base URL.
         */
        ReturnValue getProperties(String reposRelpath);
    }

    /**
     * Callback interface for providing the kind of the node that was
     * the source of a copy.
     * @see ISVNRemote.getCommitEditor(Map,CommitCallback,Set,boolean,ISVNEditor.ProvideBaseCallback,ISVNEditor.ProvidePropsCallback,ISVNEditor.GetNodeKindCallback)
     */
    public interface GetNodeKindCallback
    {
        /**
         * Returns the kind of the node that was the source of a copy.
         * @param reposRelpath The repository path of the node,
         *        relative to the session base URL.
         * @param revision The copy-from revision.
         */
        NodeKind getKind(String reposRelpath, long revision);
    }
}
