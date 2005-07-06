package org.tigris.subversion.javahl.tests;

/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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

import org.tigris.subversion.javahl.Status;
import org.tigris.subversion.javahl.NodeKind;
import org.tigris.subversion.javahl.DirEntry;

import java.io.*;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

import junit.framework.Assert;
/**
 * This class describe the expected state of the working copy
 */
public class WC
{
    /**
     * the map of the items of the working copy. The relative path is the key
     * for the map
     */
    Map items = new HashMap();

    /**
     * Generate from the expected state of the working copy a new working copy
     * @param root      the working copy directory
     * @throws IOException
     */
    public void materialize(File root) throws IOException
    {
        // generate all directories first
        Iterator it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            if (item.myContent == null) // is a directory
            {
                File dir = new File(root, item.myPath);
                if (!dir.exists())
                    dir.mkdirs();
            }
        }
        // generate all files with the content in the second run
        it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            if (item.myContent != null) // is a file
            {
                File file = new File(root, item.myPath);
                PrintWriter pw = new PrintWriter(new FileOutputStream(file));
                pw.print(item.myContent);
                pw.close();
            }
        }
    }
    /**
     * Add a new item to the working copy
     * @param path      the path of the item
     * @param content   the content of the item. A null content signifies a
     *                  directory
     * @return          the new Item object
     */
    public Item addItem(String path, String content)
    {
        return new Item(path, content);
    }

    /**
     * Returns the item at a path
     * @param path  the path, where the item is searched
     * @return  the found item
     */
    public Item getItem(String path)
    {
        return (Item) items.get(path);
    }

    /**
     * Remove the item at a path
     * @param path  the path, where the item is removed
     */
    public void removeItem(String path)
    {
        items.remove(path);
    }

    /**
     * Set text (content) status of the item at a path
     * @param path      the path, where the status is set
     * @param status    the new text status
     */
    public void setItemTextStatus(String path, int status)
    {
        ((Item) items.get(path)).textStatus = status;
    }

    /**
     * Set property status of the item at a path
     * @param path      the path, where the status is set
     * @param status    the new property status
     */
    public void setItemPropStatus(String path, int status)
    {
        ((Item) items.get(path)).propStatus = status;
    }

    /**
     * Set the revision number of the item at a path
     * @param path      the path, where the revision number is set
     * @param revision  the new revision number
     */
    public void setItemWorkingCopyRevision(String path, long revision)
    {
        ((Item) items.get(path)).workingCopyRev = revision;
    }

    /**
     * Returns the file content of the item at a path
     * @param path  the path, where the content is retrieved
     * @return  the content of the file
     */
    public String getItemContent(String path)
    {
        return ((Item) items.get(path)).myContent;
    }

    /**
     * Set the file content of the item at a path
     * @param path      the path, where the content is set
     * @param content   the new content
     */
    public void setItemContent(String path, String content)
    {
        // since having no content signals a directory, changes of removing the
        // content or setting a former not set content is not allowed. That
        // would change the type of the item.
        Assert.assertNotNull("cannot unset content", content);
        Item i = (Item) items.get(path);
        Assert.assertNotNull("cannot set content on directory", i.myContent);
        i.myContent = content;
    }

    /**
     * set the flag to check the content of item at a path during next check.
     * @param path      the path, where the flag is set
     * @param check     the flag
     */
    public void setItemCheckContent(String path, boolean check)
    {
        Item i = (Item) items.get(path);
        i.checkContent = check;
    }

    /**
     * Set the expected node kind at a path
     * @param path      the path, where the node kind is set
     * @param nodeKind  the expected node kind
     */
    public void setItemNodeKind(String path, int nodeKind)
    {
        Item i = (Item) items.get(path);
        i.nodeKind = nodeKind;
    }

    /**
     * Set the expected lock state at a path
     * @param path      the path, where the lock state is set
     * @param isLocked  the flag
     */
    public void setItemIsLocked(String path, boolean isLocked)
    {
        Item i = (Item) items.get(path);
        i.isLocked = isLocked;
    }

    /**
     * Set the expected switched flag at a path
     * @param path          the path, where the switch flag is set
     * @param isSwitched    the flag
     */
    public void setItemIsSwitched(String path, boolean isSwitched)
    {
        Item i = (Item) items.get(path);
        i.isSwitched = isSwitched;
    }

    /**
     * Copy an expected working copy state
     * @return the copy of the exiting object
     */
    public WC copy()
    {
        WC c = new WC();
        Iterator it = items.values().iterator();
        while (it.hasNext())
        {
            ((Item) it.next()).copy(c);
        }
        return c;
    }

    /**
     * Check the result of a single file SVNClient.list call
     * @param tested            the result array
     * @param singleFilePath    the path to be checked
     * @throws Exception
     */
    void check(DirEntry[] tested, String singleFilePath) throws Exception
    {
        Assert.assertEquals("not a single dir entry", 1, tested.length);
        Item item = (Item)items.get(singleFilePath);
        Assert.assertNotNull("not found in working copy", item);
        Assert.assertNotNull("not a file", item.myContent);
        Assert.assertEquals("state says file, working copy not",
                tested[0].getNodeKind(),
                item.nodeKind == -1 ? NodeKind.file : item.nodeKind);
    }
    /**
     * Check the result of a directory SVNClient.list call
     * @param tested        the result array
     * @param basePath      the path of the directory
     * @param recursive     the recursive flag of the call
     * @throws Exception
     */
    void check(DirEntry[] tested, String basePath, boolean recursive)
            throws Exception
    {
        // clear the touched flag of all items
        Iterator it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            item.touched = false;
        }

        // normalize directory path
        if (basePath != null && basePath.length() > 0)
        {
            basePath = basePath + "/";
        }
        else
        {
            basePath = "";
        }
        // check all returned DirEntry's
        for (int i = 0; i < tested.length; i++)
        {
            String name = basePath + tested[i].getPath();
            Item item = (Item) items.get(name);
            Assert.assertNotNull("not found in working copy", item);
            if (item.myContent != null)
            {
                Assert.assertEquals("state says file, working copy not",
                        tested[i].getNodeKind(),
                        item.nodeKind == -1 ? NodeKind.file : item.nodeKind);
            }
            else
            {
                Assert.assertEquals("state says dir, working copy not",
                        tested[i].getNodeKind(),
                        item.nodeKind == -1 ? NodeKind.dir : item.nodeKind);
            }
            item.touched = true;
        }

        // all items should have been in items, should had their touched flag
        // set
        it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            if(!item.touched)
            {
                if(item.myPath.startsWith(basePath) &&
                        !item.myPath.equals(basePath))
                {
                    Assert.assertFalse("not found in dir entries", recursive);
                    boolean found = false;
                    for(int i = 0; i < tested.length; i++)
                    {
                        if(tested[i].getNodeKind() == NodeKind.dir)
                        {
                            if(item.myPath.
                                    startsWith(basePath+tested[i].getPath()))
                            {
                                found = true;
                                break;
                            }
                        }
                    }
                    Assert.assertTrue("not found in dir entries", found);
                }
            }
        }
    }

    /**
     * Check the result of a SVNClient.status versus the expected state
     * @param tested            the result to be tested
     * @param workingCopyPath   the path of the working copy
     * @throws Exception
     */
    void check(Status[] tested, String workingCopyPath) throws Exception
    {
        // clear the touched flag of all items
        Iterator it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            item.touched = false;
        }

        String normalizeWCPath =
                workingCopyPath.replace(File.separatorChar, '/');

        // check all result Staus object
        for (int i = 0; i < tested.length; i++)
        {
            String path = tested[i].getPath();
            Assert.assertTrue("status path starts not with working copy path",
                    path.startsWith(normalizeWCPath));

            // we calculate the relative path to the working copy root
            if (path.length() > workingCopyPath.length() + 1)
            {
                Assert.assertEquals("missing '/' in status path",
                        path.charAt(workingCopyPath.length()), '/');
                path = path.substring(workingCopyPath.length() + 1);
            }
            else
                // this is the working copy root itself
                path = "";

            Item item = (Item) items.get(path);
            Assert.assertNotNull("status not found in working copy", item);
            Assert.assertEquals("wrong text status in working copy",
                    item.textStatus, tested[i].getTextStatus());
            if (item.workingCopyRev != -1)
                Assert.assertEquals("wrong revision number in working copy",
                        item.workingCopyRev, tested[i].getRevisionNumber());
            Assert.assertEquals("lock status wrong",
                    item.isLocked, tested[i].isLocked());
            Assert.assertEquals("switch status wrong",
                    item.isSwitched, tested[i].isSwitched());
            Assert.assertEquals("wrong prop status in working copy",
                    item.propStatus, tested[i].getPropStatus());
            if (item.myContent != null)
            {
                Assert.assertEquals("state says file, working copy not",
                        tested[i].getNodeKind(),
                        item.nodeKind == -1 ? NodeKind.file : item.nodeKind);
                if (tested[i].getTextStatus() == Status.Kind.normal ||
                        item.checkContent)
                {
                    File input = new File(workingCopyPath, item.myPath);
                    Reader rd =
                            new InputStreamReader(new FileInputStream(input));
                    StringBuffer buffer = new StringBuffer();
                    int ch;
                    while ((ch = rd.read()) != -1)
                    {
                        buffer.append((char) ch);
                    }
                    rd.close();
                    Assert.assertEquals("content mismatch", buffer.toString(),
                            item.myContent);
                }
            }
            else
            {
                Assert.assertEquals("state says dir, working copy not",
                        tested[i].getNodeKind(),
                        item.nodeKind == -1 ? NodeKind.dir : item.nodeKind);
            }
            item.touched = true;
        }

        // all items which have the touched flag not set, are missing in the
        // result array
        it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            Assert.assertTrue("item in working copy not found in status",
                    item.touched);
        }
    }

    /**
     * internal class to discribe a single working copy item
     */
    public class Item
    {
        /**
         * the content of a file. A directory has a null content
         */
        String myContent;
        /**
         * the relative path of the item
         */
        String myPath;
        /**
         * the text (content) status of the item
         */
        int textStatus = Status.Kind.normal;
        /**
         * the property status of the item.
         */
        int propStatus = Status.Kind.none;
        /**
         * the expected revision number. -1 means do not check.
         */
        long workingCopyRev = -1;
        /**
         * flag if item has been touched. To detect missing items.
         */
        boolean touched;
        /**
         * flag if the content will be checked
         */
        boolean checkContent;
        /**
         * expected node kind. -1 means do not check.
         */
        int nodeKind = -1;
        /**
         * expected locked status
         */
        boolean isLocked;
        /**
         * expected switched status
         */
        boolean isSwitched;

        /**
         * create a new item
         * @param path      the path of the item.
         * @param content   the content of the item. A null signals a directory.
         */
        private Item(String path, String content)
        {
            myPath = path;
            myContent = content;
            items.put(path, this);
        }
        /**
         * copy constructor
         * @param source    the copy source.
         * @param owner     the WC of the copy
         */
        private Item(Item source, WC owner)
        {
            myPath = source.myPath;
            myContent = source.myContent;
            textStatus = source.textStatus;
            propStatus = source.propStatus;
            owner.items.put(myPath, this);
        }
        /**
         * copy this item
         * @param owner the new WC
         * @return  the copied item
         */
        private Item copy(WC owner)
        {
            return new Item(this, owner);
        }
    }
}
