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

package org.apache.subversion.javahl.types;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.StringTokenizer;

import org.apache.subversion.javahl.SubversionException;

/**
 * Merge history for a path.
 */
public class Mergeinfo implements java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change made to
    // this class.  See the java documentation for when a change is incompatible.
    // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 1L;

    /**
     * A mapping of repository-relative paths to a list of revision
     * ranges.
     */
    private Map<String, List<RevisionRange>> mergeSources;

    public Mergeinfo()
    {
        mergeSources = new HashMap<String, List<RevisionRange>>();
    }

    /**
     * Create and populate an instance using the contents of the
     * <code>svn:mergeinfo</code> property.
     * @param mergeinfo <code>svn:mergeinfo</code> property value.
     */
    public Mergeinfo(String mergeinfo)
    {
        this();
        this.loadFromMergeinfoProperty(mergeinfo);
    }

    /**
     * The three ways to request mergeinfo affecting a given path
     * in {@link org.apache.subversion.javahl.ISVNRemote#getMergeinfo}.
     * @since 1.9
     */
    public static enum Inheritance
    {
        /** Explicit mergeinfo only. */
        explicit,

        /**
         * Explicit mergeinfo, or if that doesn't exist, the inherited
         * mergeinfo from a target's nearest (path-wise, not history-wise)
         * ancestor.
         */
        inherited,

        /**
         * Mergeinfo inherited from a target's nearest (path-wise,
         * not history-wise) ancestor, regardless of whether target
         * has explicit mergeinfo.
         */
        nearest_ancestor;
    }

    /**
     * Add one or more RevisionRange objects to merge info. If the
     * merge source is already stored, the list of revisions is
     * replaced.
     * @param mergeSrc The merge source URL.
     * @param ranges RevisionRange objects to add.
     * @throws SubversionException If range list contains objects of
     * type other than RevisionRange.
     */
    public void addRevisions(String mergeSrc, List<RevisionRange> ranges)
    {
        for (RevisionRange range : ranges)
            addRevisionRange(mergeSrc, range);
    }

    public void addRevisions(String mergeSrc, RevisionRangeList ranges)
    {
        addRevisions(mergeSrc, ranges.getRanges());
    }

    /**
     * Add a revision range to the merged revisions for a path.  If
     * the merge source already has associated revision ranges, add
     * the revision range to the existing list.
     * @param mergeSrc The merge source URL.
     * @param range The revision range to add.
     */
    public void addRevisionRange(String mergeSrc, RevisionRange range)
    {
        List<RevisionRange> revisions = this.getRevisions(mergeSrc);
        if (revisions == null)
            revisions = new ArrayList<RevisionRange>();
        revisions.add(range);
        this.setRevisionList(mergeSrc, revisions);
    }

    /**
     * Get the merge source URLs.
     * @return The merge source URLs.
     */
    public Set<String> getPaths()
    {
        return mergeSources.keySet();
    }

    /**
     * Get the revision ranges for the specified merge source URL.
     * @param mergeSrc The merge source URL, or <code>null</code>.
     * @return List of RevisionRange objects, or <code>null</code>.
     */
    public List<RevisionRange> getRevisions(String mergeSrc)
    {
        if (mergeSrc == null)
            return null;
        return mergeSources.get(mergeSrc);
    }

    /**
     * Get the RevisionRange objects for the specified merge source URL
     * @param mergeSrc The merge source URL, or <code>null</code>.
     * @return Array of RevisionRange objects, or <code>null</code>.
     */
    public List<RevisionRange> getRevisionRange(String mergeSrc)
    {
        return this.getRevisions(mergeSrc);
    }

    /**
     * Like {@link #getReivsionRange}, but returns a {@link RevisionRangeList}.
     */
    public RevisionRangeList getRevisionRangeList(String mergeSrc)
    {
        return new RevisionRangeList(getRevisionRange(mergeSrc));
    }

    /**
     * Parse the <code>svn:mergeinfo</code> property to populate the
     * merge source URLs and revision ranges of this instance.
     * @param mergeinfo <code>svn:mergeinfo</code> property value.
     */
    public void loadFromMergeinfoProperty(String mergeinfo)
    {
        if (mergeinfo == null)
            return;
        StringTokenizer st = new StringTokenizer(mergeinfo, "\n");
        while (st.hasMoreTokens())
        {
            parseMergeinfoLine(st.nextToken());
        }
    }

    /**
     * Parse a merge source line from a <code>svn:mergeinfo</code>
     * property value (e.g.
     * <code>"/trunk:1-100,104,108,110-115"</code>).
     *
     * @param line A line of merge info for a single merge source.
     */
    private void parseMergeinfoLine(String line)
    {
        int colon = line.indexOf(':');
        if (colon > 0)
        {
            String pathElement = line.substring(0, colon);
            String revisions = line.substring(colon + 1);
            parseRevisions(pathElement, revisions);
        }
    }

    /**
     * Parse the revisions in a merge info line into RevisionRange
     * objects and adds each of them to the internal Map
     * (e.g. <code>"1-100,104,108,110-115"</code>)
     *
     * @param path The merge source path.
     * @param revisions A textual representation of the revision ranges.
     */
    private void parseRevisions(String path, String revisions)
    {
        List<RevisionRange> rangeList = this.getRevisions(path);
        StringTokenizer st = new StringTokenizer(revisions, ",");
        while (st.hasMoreTokens())
        {
            String revisionElement = st.nextToken();
            RevisionRange range = new RevisionRange(revisionElement);
            if (rangeList == null)
                rangeList = new ArrayList<RevisionRange>();
            rangeList.add(range);
        }
        if (rangeList != null)
            setRevisionList(path, rangeList);
    }


    /**
     * Add the List object to the map.  This method is only
     * used internally where we know that List contains a
     * type-safe set of RevisionRange objects.
     * @param mergeSrc The merge source URL.
     * @param range List of RevisionRange objects to add.
     */
    private void setRevisionList(String mergeSrc, List<RevisionRange> range)
    {
        mergeSources.put(mergeSrc, range);
    }

    /**
     * Constants to specify which collection of revisions to report in
     * getMergeinfoLog.
     */
    public enum LogKind
    {
        /** Revisions eligible for merging from merge-source to merge-target. */
        eligible,

        /** Revisions already merged from merge-source to merge-target. */
        merged;
    }
}
