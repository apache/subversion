/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
