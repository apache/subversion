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
package org.tigris.subversion.javahl.tests;

import junit.framework.TestCase;
import org.tigris.subversion.javahl.SVNAdmin;
import java.io.File;

/**
 * This class is used for testing the SVNAdmin class
 *
 * More methodes for testing are still needed
 */
public class SVNAdminTests extends TestCase
{
    /**
     * the objects, which is going to be tested
     */
    SVNAdmin testee;
    /**
     * setup the test
     * @throws Exception
     */
    protected void setUp() throws Exception
    {
        super.setUp();
        testee = new SVNAdmin();
    }

    /**
     * cleanp after the test
     * @throws Exception
     */
    protected void tearDown() throws Exception
    {
        testee.dispose();
        super.tearDown();
    }

    /**
     * Test the basic SVNAdmin.create functionality
     * @throws Throwable
     */
    public void testCreate() throws Throwable
    {
        testee.create("testrep", false, false, null, SVNAdmin.BDB);
        assertTrue("repository exists", new File("testrep").exists());
        removeRepository("testrep");
        assertFalse("repository deleted", new File("testrep").exists());
    }

    /**
     * remove a rempositryl
     * @param pathName      path name of the repository
     * @throws Exception
     */
    protected void removeRepository(String pathName) throws Exception
    {
        File masterDir = new File(pathName);
        removeDirOrFile(masterDir);
    }

    /**
     * remove a file or a directory with its content
     * @param file  the file or directory to be removed
     */
    private void removeDirOrFile(File file)
    {
        if(!file.exists())
        {
            return;
        }
        if(file.isDirectory())
        {
            File[] content = file.listFiles();
            for(int i = 0; i < content.length; i++)
                removeDirOrFile(content[i]);
            file.delete();
        }
        else
            file.delete();
    }
}
