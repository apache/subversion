/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2007 CollabNet.  All rights reserved.
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

import java.io.IOException;

import org.tigris.subversion.javahl.Revision;
import org.tigris.subversion.javahl.SubversionException;

/**
 * This class is used for testing the SVNAdmin class
 *
 * More methodes for testing are still needed
 */
public class SVNAdminTests extends SVNTests
{
    public SVNAdminTests()
    {
    }

    public SVNAdminTests(String name)
    {
        super(name);
    }

    /**
     * Test the basic SVNAdmin.create functionality
     * @throws SubversionException
     */
    public void testCreate()
        throws SubversionException, IOException
    {
        OneTest thisTest = new OneTest(false);
        assertTrue("repository exists", thisTest.getRepository().exists());
    }

    public void testSetRevProp()
        throws SubversionException, IOException
    {
        OneTest thisTest = new OneTest(false);
        admin.setRevProp(thisTest.getRepositoryPath(), Revision.getInstance(0),
                         "svn:log", "Initial repository creation", false,
                         false);
    }
}
