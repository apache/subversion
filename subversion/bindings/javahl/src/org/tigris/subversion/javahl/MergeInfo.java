/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

package org.tigris.subversion.javahl;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.StringTokenizer;

/**
 * Merge history for a path.
 *
 * @since 1.5
 */
public class MergeInfo
{
    /**
     * A mapping of repository-relative paths to a list of revision
     * ranges.
     */
    private Map mergeSources;

    public MergeInfo()
    {
        mergeSources = new HashMap();
    }

    /**
     * Create and populate an instance using the contents of the
     * <code>svn:mergeinfo</code> property.
     * @param mergeInfo <code>svn:mergeinfo</code> property value.
     */
    public MergeInfo(String mergeInfo)
    {
        this();
        this.loadFromMergeInfoProperty(mergeInfo);
    }

    /**
     * Add one or more RevisionRange objects to merge info. If path is already
     * stored, the list of revisions is replaced.
     * @param path The merge source path.
     * @param range List of RevisionRange objects to add.
     * @throws SubversionException If range list contains objects of
     * type other than RevisionRange.
     */
    public void addRevisions(String path, List range)
            throws SubversionException
    {
        for (Iterator iterator = range.iterator(); iterator.hasNext();)
        {
            if (!(iterator.next() instanceof RevisionRange))
                throw new SubversionException(
                      "List must only contain objects of type RevisionRange");
        }
        this.setRevisionList(path, range);
    }

    /**
     * Add a revision range to the merged revisions for a path.  If
     * the path already has associated revision ranges, add the
     * revision range to the existing list.
     * @param path The merge source path.
     * @param range The revision range to add.
     */
    public void addRevisionRange(String path, RevisionRange range)
    {
        List revisions = this.getRevisions(path);
        if (revisions == null)
            revisions = new ArrayList();
        revisions.add(range);
        this.setRevisionList(path, revisions);
    }

    /**
     * Get the merge source paths.
     * @return The merge source paths.
     */
    public String[] getPaths()
    {
        Set pathSet = mergeSources.keySet();
        if (pathSet == null)
            return null;
        return (String []) pathSet.toArray(new String[pathSet.size()]);
    }

    /**
     * Get the revision ranges for the specified path.
     * @param path The merge source path.
     * @return List of RevisionRange objects, or <code>null</code>.
     */
    public List getRevisions(String path)
    {
        if (path == null)
            return null;
        return (List) mergeSources.get(path);
    }

    /**
     * Get the RevisionRange objects for the specified path
     * @param path The merge source path.
     * @return Array of RevisionRange objects, or <code>null</code>.
     */
    public RevisionRange[] getRevisionRange(String path)
    {
        List revisions = this.getRevisions(path);
        if (revisions == null)
            return null;
        return (RevisionRange [])
            revisions.toArray(new RevisionRange[revisions.size()]);
    }

    /**
     * Parse the <code>svn:mergeinfo</code> property to populate the
     * merge source paths and revision ranges of this instance.
     * @param mergeInfo <code>svn:mergeinfo</code> property value.
     */
    public void loadFromMergeInfoProperty(String mergeInfo)
    {
        if (mergeInfo == null)
            return;
        StringTokenizer st = new StringTokenizer(mergeInfo, "\n");
        while (st.hasMoreTokens())
        {
            parseMergeInfoLine(st.nextToken());
        }
    }

    /**
     * Parse a merge source line from a <code>svn:mergeinfo</code>
     * property value (e.g.
     * <code>"/trunk:1-100,104,108,110-115"</code>).
     *
     * @param line A line of merge info for a single merge source.
     */
    private void parseMergeInfoLine(String line)
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
        List rangeList = this.getRevisions(path);
        StringTokenizer st = new StringTokenizer(revisions, ",");
        while (st.hasMoreTokens())
        {
            String revisionElement = st.nextToken();
            RevisionRange range = new RevisionRange(revisionElement);
            if (rangeList == null)
                rangeList = new ArrayList();
            rangeList.add(range);
        }
        if (rangeList != null)
            setRevisionList(path, rangeList);
    }


    /**
     * Add the List object to the map.  This method is only
     * used internally where we know that List contains a
     * type-safe set of RevisionRange objects.
     * @param path The merge source path.
     * @param range List of RevisionRange objects to add.
     */
    private void setRevisionList(String path, List range)
    {
        mergeSources.put(path, range);
    }
}
