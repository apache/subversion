/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
package org.apache.subversion.javahl;

import java.util.Date;
import java.util.Set;
import java.util.Map;
import java.util.HashSet;
import java.io.IOException;

import org.apache.subversion.javahl.ra.ISVNRa;
import org.apache.subversion.javahl.ra.SVNRaConfigDefault;
import org.apache.subversion.javahl.ra.SVNRaFactory;
import org.apache.subversion.javahl.types.Depth;
import org.apache.subversion.javahl.types.Lock;
import org.apache.subversion.javahl.types.NodeKind;
import org.apache.subversion.javahl.types.Revision;

/**
 * This class is used for testing the SVNReposAccess class
 *
 * More methodes for testing are still needed
 */
public class SVNRATests extends SVNTests
{
    protected OneTest thisTest;

    public SVNRATests()
    {
    }

    public SVNRATests(String name)
    {
        super(name);
    }

    protected void setUp() throws Exception
    {
        super.setUp();

        thisTest = new OneTest();
    }

    /**
     * Test the basic SVNAdmin.create functionality
     * @throws SubversionException
     */
    public void testCreate()
        throws SubversionException, IOException
    {
        assertTrue("repository exists", thisTest.getRepository().exists());
    }

    public void testDatedRev()
        throws SubversionException, IOException
    {
        ISVNRa session = getSession();

        long revision = session.getDatedRevision(new Date());
        assertEquals(revision, 1);
    }

    public void testGetLocks()
        throws SubversionException, IOException
    {
        ISVNRa session = getSession();

        Set<String> iotaPathSet = new HashSet<String>(1);
        String iotaPath = thisTest.getWCPath() + "/iota";
        iotaPathSet.add(iotaPath);

        client.lock(iotaPathSet, "foo", false);

        Map<String, Lock> locks = session.getLocks("iota", Depth.infinity);

        assertEquals(locks.size(), 1);
        Lock lock = locks.get("/iota");
        assertNotNull(lock);
        assertEquals(lock.getOwner(), "jrandom");
    }

    public void testCheckPath()
        throws SubversionException, IOException
    {
        ISVNRa session = getSession();

        NodeKind kind = session.checkPath("iota", Revision.getInstance(1));
        assertEquals(NodeKind.file, kind);

        kind = session.checkPath("iota", Revision.getInstance(0));
        assertEquals(NodeKind.none, kind);

        kind = session.checkPath("A", Revision.getInstance(1));
        assertEquals(NodeKind.dir, kind);
    }
    
    public static ISVNRa getSession(String url, String configDirectory)
    {
        SVNRaConfigDefault config = new SVNRaConfigDefault();
        config.setUsername(USERNAME);
        config.setPassword(PASSWORD);
        config.setPrompt(new DefaultPromptUserPassword());
        config.setConfigDirectory(configDirectory);
        
        ISVNRa raSession = SVNRaFactory.createRaSession(url, null, config);

        assertNotNull("Null session was returned by factory", raSession);

        return raSession;
    }
    
    private ISVNRa getSession()
    {
        return getSession(getTestRepoUrl(), super.conf.getAbsolutePath());
    }
    
    private String getTestRepoUrl()
    {
        return thisTest.getUrl().toASCIIString();
    }
    
    public void testGetLatestRevision() throws Exception
    {
        ISVNRa session = getSession();

        assertEquals(1, session.getLatestRevision());
    }

    public void testGetUUID() throws Exception
    {
        ISVNRa session = getSession();

        /*
         * Test UUID
         * TODO: Test for actual UUID once test dump file has
         * fixed UUID
         */
        assertNotNull(session.getUUID());
    }

    public void testGetUrl() throws Exception
    {
        ISVNRa session = getSession();

        assertEquals(getTestRepoUrl(), session.getUrl());
    }
}
