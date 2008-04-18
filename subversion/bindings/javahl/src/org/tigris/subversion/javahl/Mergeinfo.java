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
public class Mergeinfo implements java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.  See any of the following, depending upon
    // the Java release.
    // http://java.sun.com/j2se/1.3/docs/guide/serialization/spec/version.doc7.html
    // http://java.sun.com/j2se/1.4/pdf/serial-spec.pdf
    // http://java.sun.com/j2se/1.5.0/docs/guide/serialization/spec/version.html#6678
    // http://java.sun.com/javase/6/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 1L;

    /**
     * A mapping of repository-relative paths to a list of revision
     * ranges.
     */
    private Map mergeSources;

    public Mergeinfo()
    {
        mergeSources = new HashMap();
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
     * Add one or more RevisionRange objects to merge info. If the
     * merge source is already stored, the list of revisions is
     * replaced.
     * @param mergeSrc The merge source URL.
     * @param ranges RevisionRange objects to add.
     * @throws SubversionException If range list contains objects of
     * type other than RevisionRange.
     */
    public void addRevisions(String mergeSrc, RevisionRange[] ranges)
    {
        for (int i = 0; i < ranges.length; i++)
            addRevisionRange(mergeSrc, ranges[i]);
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
        List revisions = this.getRevisions(mergeSrc);
        if (revisions == null)
            revisions = new ArrayList();
        revisions.add(range);
        this.setRevisionList(mergeSrc, revisions);
    }

    /**
     * Get the merge source URLs.
     * @return The merge source URLs.
     */
    public String[] getPaths()
    {
        Set pathSet = mergeSources.keySet();
        if (pathSet == null)
            return null;
        return (String []) pathSet.toArray(new String[pathSet.size()]);
    }

    /**
     * Get the revision ranges for the specified merge source URL.
     * @param mergeSrc The merge source URL, or <code>null</code>.
     * @return List of RevisionRange objects, or <code>null</code>.
     */
    public List getRevisions(String mergeSrc)
    {
        if (mergeSrc == null)
            return null;
        return (List) mergeSources.get(mergeSrc);
    }

    /**
     * Get the RevisionRange objects for the specified merge source URL
     * @param mergeSrc The merge source URL, or <code>null</code>.
     * @return Array of RevisionRange objects, or <code>null</code>.
     */
    public RevisionRange[] getRevisionRange(String mergeSrc)
    {
        List revisions = this.getRevisions(mergeSrc);
        if (revisions == null)
            return null;
        return (RevisionRange [])
            revisions.toArray(new RevisionRange[revisions.size()]);
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
     * @param mergeSrc The merge source URL.
     * @param range List of RevisionRange objects to add.
     */
    private void setRevisionList(String mergeSrc, List range)
    {
        mergeSources.put(mergeSrc, range);
    }
}
