package org.tigris.subversion.javahl.tests;

import junit.framework.TestCase;
import org.tigris.subversion.javahl.SVNAdmin;

import java.io.File;

/**
 * Created by IntelliJ IDEA.
 * User: patrick
 * Date: Jan 27, 2004
 * Time: 8:49:24 AM
 * To change this template use Options | File Templates.
 */
public class SVNAdminTests extends TestCase
{
    SVNAdmin testee;
    protected void setUp() throws Exception
    {
        super.setUp();
        testee = new SVNAdmin();
    }

    protected void tearDown() throws Exception
    {
        super.tearDown();
        testee.dispose();
    }

    public void testCreate() throws Throwable
    {
        testee.create("testrep", false, false, null);
        assertTrue("repository exists", new File("testrep").exists());
        removeRepository("testrep");
        assertFalse("repository deleted", new File("testrep").exists());
    }

    protected void removeRepository(String pathName) throws Exception
    {
        File masterDir = new File(pathName);
        removeDirOrFile(masterDir);
    }

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
