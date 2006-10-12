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

import java.io.File;
import junit.framework.TestCase;

import org.tigris.subversion.javahl.SubversionException;
import org.tigris.subversion.javahl.SVNAdmin;

/**
 * This class is used for testing the SVNAdmin class
 *
 * More methodes for testing are still needed
 */
public class SVNAdminTests extends TestCase
{
    private static final String REPOS_PATH = "testrep";

    /**
     * the objects, which is going to be tested
     */
    private SVNAdmin testee;

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
    public void testCreate()
        throws SubversionException
    {
        createRepository(REPOS_PATH);
        File reposDir = new File(REPOS_PATH);
        assertTrue("repository exists", reposDir.exists());
        removeRepository(REPOS_PATH);
        assertFalse("repository deleted", reposDir.exists());
    }

    /**
     * Create a repository at <code>reposPath</code>.
     *
     * @param reposPath The path to the repository.
     */
    protected void createRepository(String reposPath)
        throws SubversionException
    {
        testee.create(reposPath, false, false, null, SVNAdmin.BDB);
    }

    /**
     * Delete the repository at <code>reposPath</code>.
     *
     * @param reposPath The path to the repository.
     */
    protected void removeRepository(String reposPath)
    {
        SVNTests.removeDirOrFile(new File(reposPath));
    }
}
