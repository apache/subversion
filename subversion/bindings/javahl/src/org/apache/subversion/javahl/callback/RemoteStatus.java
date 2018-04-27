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

package org.apache.subversion.javahl.callback;

import org.apache.subversion.javahl.ISVNRemote;

/**
 * Called for each affected element in a remote status driave.
 * <p>
 * <b>Note:</b> All paths sent to the callback methods are relative to
 * the {@link ISVNRemtoe} session's URL.
 * @see ISVNRemote#status
 * @since 1.9
 */
public interface RemoteStatus
{
    /**
     * A directory was added.
     * @param relativePath The session-relative path of the new directory.
     */
    void addedDirectory(String relativePath);

    /**
     * A file was added.
     * @param relativePath The session-relative path of the new file.
     */
    void addedFile(String relativePath);

    /**
     * A symbolic link was added.
     * @param relativePath The session-relative path of the new symbolic link.
     */
    void addedSymlink(String relativePath);

    /**
     * A directory was modified.
     * @param relativePath The session-relative path of the directory.
     * @param childrenModified The directory contents changed.
     * @param propsModified The directory's properties changed.
     * @param nodeInfo Additional information about the modified directory.
     */
    void modifiedDirectory(String relativePath,
                           boolean childrenModified,
                           boolean propsModified,
                           Entry nodeInfo);

    /**
     * A file was modified.
     * @param relativePath The session-relative path of the directory.
     * @param textModified The file contents changed.
     * @param propsModified The file's properties changed.
     * @param nodeInfo Additional information about the modified file.
     */
    void modifiedFile(String relativePath,
                      boolean textModified,
                      boolean propsModified,
                      Entry nodeInfo);

    /**
     * A symbolic link was modified.
     * @param relativePath The session-relative path of the symlink.
     * @param textModified The link target changed.
     * @param propsModified The symlink's properties changed.
     * @param nodeInfo Additional information about the modified symlink.
     */
    void modifiedSymlink(String relativePath,
                         boolean targetModified,
                         boolean propsModified,
                         Entry nodeInfo);


    /**
     * An entry was deleted.
     * @param relativePath The session-relative path of the entry.
     */
    void deleted(String relativePath);


    /**
     * Contains additional information related to a modification or
     * deletion event.
     */
    public static class Entry
        implements Comparable<Entry>, java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible
        // change made to this class.
        private static final long serialVersionUID = 1L;

        private String uuid;    // The UUID of the repository
        private String author;  // The author of the last change
        private long revision;  // Committed revision number
        private long timestamp; // Commit timestamp (milliseconds from epoch)

        public Entry(String uuid, String author, long revision, long timestamp)
        {
            this.uuid = uuid;
            this.author = author;
            this.revision = revision;
            this.timestamp = timestamp;
        }

        /** @return The UUID of the repository that the node belongs to. */
        public String getUuid() { return uuid; }

        /** @return The author (committer) of the change. */
        public String getLastAuthor() { return author; }

        /** @return The revision number in with the change was committed. */
        public long getCommittedRevision() { return revision; }

        /**
         * @return The timestamp, in milliseconds from the epoch, of
         * the committed revision.
         */
        public long getCommittedTimestamp() { return timestamp; }

        /** Implementation of interface {@link java.lang.Comparable}. */
        public int compareTo(Entry that)
        {
            if (this == that)
                return 0;

            int cmp = uuid.compareTo(that.uuid);
            if (cmp == 0) {
                cmp = author.compareTo(that.author);
                if (cmp == 0) {
                    cmp = (revision < that.revision ? 1
                           : (revision > that.revision ? -1 : 0));
                    if (cmp == 0)
                        cmp = (timestamp < that.timestamp ? 1
                               : (timestamp > that.timestamp ? -1 : 0));
                }
            }
            return cmp;
        }

        @Override
        public boolean equals(Object entry)
        {
            if (this == entry)
                return true;
            if (!super.equals(entry) || getClass() != entry.getClass())
                return false;

            final Entry that = (Entry)entry;
            return (this.uuid == that.uuid
                    && this.author == that.author
                    && this.revision == that.revision
                    && this.timestamp == that.timestamp);
        }

        @Override
        public int hashCode()
        {
            final int factor = 33;
            int hash = ((uuid == null) ? 0 : uuid.hashCode());
            hash = factor * hash + ((author == null) ? 0 : author.hashCode());
            hash = factor * hash + (int)(revision >> 32) & 0xffffffff;
            hash = factor * hash + (int)revision & 0xffffffff;
            hash = factor * hash + (int)(timestamp >> 32) & 0xffffffff;
            hash = factor * hash + (int)timestamp & 0xffffffff;
            return hash;
        }
    }
}
