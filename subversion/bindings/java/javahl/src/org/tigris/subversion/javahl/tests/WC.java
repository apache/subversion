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

public class WC
{
    Map items = new HashMap();

    public void materialize(File root) throws IOException
    {
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

    public Item addItem(String path, String content)
    {
        return new Item(path, content);
    }

    public Item getItem(String path)
    {
        return (Item) items.get(path);
    }

    public void removeItem(String path)
    {
        items.remove(path);
    }

    public void setItemTextStatus(String path, int status)
    {
        ((Item) items.get(path)).textStatus = status;
    }

    public void setItemPropStatus(String path, int status)
    {
        ((Item) items.get(path)).propStatus = status;
    }

    public void setItemWorkingCopyRevision(String path, long revision)
    {
        ((Item) items.get(path)).workingCopyRev = revision;
    }

    public String getItemContent(String path)
    {
        return ((Item) items.get(path)).myContent;
    }

    public void setItemContent(String path, String content)
    {
        Assert.assertNotNull("cannot unset content", content);
        Item i = (Item) items.get(path);
        Assert.assertNotNull("cannot set content on directory", i.myContent);
        i.myContent = content;
    }

    public void setItemCheckContent(String path, boolean check)
    {
        Item i = (Item) items.get(path);
        i.checkContent = check;
    }

    public void setItemNodeKind(String path, int nodeKind)
    {
        Item i = (Item) items.get(path);
        i.nodeKind = nodeKind;
    }

    public void setItemIsLocked(String path, boolean isLocked)
    {
        Item i = (Item) items.get(path);
        i.isLocked = isLocked;
    }

    public void setItemIsSwitched(String path, boolean isSwitched)
    {
        Item i = (Item) items.get(path);
        i.isSwitched = isSwitched;
    }

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

    void check(DirEntry[] tested, String singleFilePath) throws Exception
    {
        Assert.assertEquals("not a single dir entry", 1, tested.length);
        Item item = (Item)items.get(singleFilePath);
        Assert.assertNotNull("not found in working copy", item);
        Assert.assertNotNull("not a file", item.myContent);
        Assert.assertEquals("state says file, working copy not", tested[0].getNodeKind(), item.nodeKind == -1 ? NodeKind.file : item.nodeKind);
    }
    void check(DirEntry[] tested, String basePath, boolean recursive) throws Exception
    {
        Iterator it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            item.touched = false;
        }

        if (basePath != null && basePath.length() > 0)
        {
            basePath = basePath + "/";
        }
        else
        {
            basePath = "";
        }
        for (int i = 0; i < tested.length; i++)
        {
            String name = basePath + tested[i].getPath();
            Item item = (Item) items.get(name);
            Assert.assertNotNull("not found in working copy", item);
            if (item.myContent != null)
            {
                Assert.assertEquals("state says file, working copy not", tested[i].getNodeKind(), item.nodeKind == -1 ? NodeKind.file : item.nodeKind);
            }
            else
            {
                Assert.assertEquals("state says dir, working copy not", tested[i].getNodeKind(), item.nodeKind == -1 ? NodeKind.dir : item.nodeKind);
            }
            item.touched = true;
        }

        it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            if(!item.touched)
            {
                if(item.myPath.startsWith(basePath) && !item.myPath.equals(basePath))
                {
                    Assert.assertFalse("not found in dir entries", recursive);
                    boolean found = false;
                    for(int i = 0; i < tested.length; i++)
                    {
                        if(tested[i].getNodeKind() == NodeKind.dir)
                        {
                            if(item.myPath.startsWith(basePath+tested[i].getPath()))
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

    void check(Status[] tested, String workingCopyPath) throws Exception
    {
        Iterator it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            item.touched = false;
        }

        String normalizeWCPath = workingCopyPath.replace(File.separatorChar, '/');

        for (int i = 0; i < tested.length; i++)
        {
            String path = tested[i].getPath();
            Assert.assertTrue("status path starts not with working copy path", path.startsWith(normalizeWCPath));
            if (path.length() > workingCopyPath.length() + 1)
            {
                Assert.assertEquals("missing '/' in status path", path.charAt(workingCopyPath.length()), '/');
                path = path.substring(workingCopyPath.length() + 1);
            }
            else
                path = "";
            Item item = (Item) items.get(path);
            Assert.assertNotNull("status not found in working copy", item);
            Assert.assertEquals("wrong text status in working copy", item.textStatus, tested[i].getTextStatus());
            if (item.workingCopyRev != -1)
                Assert.assertEquals("wrong revision number in working copy", item.workingCopyRev, tested[i].getRevisionNumber());
            Assert.assertEquals("lock status wrong", item.isLocked, tested[i].isLocked());
            Assert.assertEquals("switch status wrong", item.isSwitched, tested[i].isSwitched());
            Assert.assertEquals("wrong prop status in working copy", item.propStatus, tested[i].getPropStatus());
            if (item.myContent != null)
            {
                Assert.assertEquals("state says file, working copy not", tested[i].getNodeKind(), item.nodeKind == -1 ? NodeKind.file : item.nodeKind);
                if (tested[i].getTextStatus() == Status.Kind.normal || item.checkContent)
                {
                    File input = new File(workingCopyPath, item.myPath);
                    Reader rd = new InputStreamReader(new FileInputStream(input));
                    StringBuffer buffer = new StringBuffer();
                    int ch;
                    while ((ch = rd.read()) != -1)
                    {
                        buffer.append((char) ch);
                    }
                    rd.close();
                    Assert.assertEquals("content mismatch", buffer.toString(), (item.myContent));
                }
            }
            else
            {
                Assert.assertEquals("state says dir, working copy not", tested[i].getNodeKind(), item.nodeKind == -1 ? NodeKind.dir : item.nodeKind);
            }
            item.touched = true;
        }
        it = items.values().iterator();
        while (it.hasNext())
        {
            Item item = (Item) it.next();
            Assert.assertTrue("item in working copy not found in status", item.touched);
        }
    }

    public class Item
    {
        String myContent;
        String myPath;
        int textStatus = Status.Kind.normal;
        int propStatus = Status.Kind.none;
        long workingCopyRev = -1;
        boolean touched;
        boolean checkContent;
        int nodeKind = -1;
        boolean isLocked;
        boolean isSwitched;

        Item(String path, String content)
        {
            myPath = path;
            myContent = content;
            items.put(path, this);
        }

        public Item(Item source, WC owner)
        {
            myPath = source.myPath;
            myContent = source.myContent;
            textStatus = source.textStatus;
            propStatus = source.propStatus;
            owner.items.put(myPath, this);
        }

        Item copy(WC owner)
        {
            return new Item(this, owner);
        }
    }
}
