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
 * Merge history
 * 
 * 
 * @since 1.5
 */
public class MergeInfo
{
    private Map info;

    public MergeInfo()
    {
        super();
        info = new HashMap();
    }

    /**
     * Construct using the contents of the svn:mergeinfo property
     * @param propertyValue content of svn:mergeinfo property
     */
    public MergeInfo(String propertyValue)
    {
        this();
        this.loadFromMergeInfoProperty(propertyValue);
    }

    /**
     * Add one or more RevisionRange objects to merge info. If path is already
     * stored, the list of revisions is replaced.
     * @param path the merge source path
     * @param range List of RevisionRange objects to add
     * @throws SubversionException if List contains objects of type other than
     * RevisionRange
     */
    public void addRevisions(String path, List range)
            throws SubversionException
    {
        for (Iterator iterator = range.iterator(); iterator.hasNext();)
        {
            if (iterator.next().getClass() != RevisionRange.class)
                throw new SubversionException(
                        "List must only contain objects of type RevisionRange.");
        }
        info.put(path, range);
    }

    /**
     * Add a RevisionRange object to path. If path is already stored, object is
     * added to existing list.
     * @param path the merge source path
     * @param range the RevisionRange to add
     */
    public void addRevisionRange(String path, RevisionRange range)
    {
        List revisions = this.getRevisions(path);
        if (revisions == null)
            revisions = new ArrayList();
        revisions.add(range);
        try
        {
            this.addRevisions(path, revisions);
        }
        catch (SubversionException e)
        {
            // Should be impossible to get here
        }
    }

    /**
     * Get the merge source paths
     * @return the merge source paths
     */
    public String[] getPaths()
    {
        Set pathSet = info.keySet();
        if (pathSet == null)
            return null;
        return (String[]) pathSet.toArray(new String[pathSet.size()]);
    }

    /**
     * Get the RevisionRange objects for the specified path
     * @param path the merge source path
     * @return List of RevisionRange objects or null
     */
    public List getRevisions(String path)
    {
        if (path == null)
            return null;
        return (List) info.get(path);
    }

    /**
     * Get the RevisionRange objects for the specified path
     * @param path the merge source path
     * @return array of RevisionRange objects or null
     */
    public RevisionRange[] getRevisionRange(String path)
    {
        List revisions = this.getRevisions(path);
        if (revisions == null)
            return null;
        return (RevisionRange[]) revisions.toArray(new RevisionRange[revisions
                .size()]);
    }

    /**
     * Takes the svn:mergeinfo property and parses it to populate the merge
     * source paths and revision ranges contained in the property.
     * @param propertyValue the contents of the svn:mergeinfo property
     */
    public void loadFromMergeInfoProperty(String propertyValue)
    {
        if (propertyValue == null)
            return;
        StringTokenizer st = new StringTokenizer(propertyValue, "\n");
        while (st.hasMoreTokens())
        {
            parseMergeInfoLine(st.nextToken());
        }
    }

    /**
     * Parses a given line of the merge info property. Example:
     * /trunk:1-100,104,108,110-115
     * 
     * @param line a line of merge info
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
     * Parses the revisions in a merge info line into RevisionRange objects and
     * adds each of them to the internal Map Example: 1-100,104,108,110-115
     * 
     * @param path the merge source path
     * @param revisions the revision info
     */
    private void parseRevisions(String path, String revisions)
    {
        StringTokenizer st = new StringTokenizer(revisions, ",");
        while (st.hasMoreTokens())
        {
            String revisionElement = st.nextToken();
            RevisionRange range = new RevisionRange(revisionElement);
            this.addRevisionRange(path, range);
        }

    }
}
