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

import org.apache.subversion.javahl.*;
import org.apache.subversion.javahl.remote.*;
import org.apache.subversion.javahl.types.*;

import java.util.Date;
import java.util.Set;
import java.util.Map;
import java.util.HashSet;
import java.io.IOException;

/**
 * This class is used for testing the SVNReposAccess class
 *
 * More methodes for testing are still needed
 */
public class SVNRemoteTests extends SVNTests
{
    protected OneTest thisTest;

    public SVNRemoteTests()
    {
    }

    public SVNRemoteTests(String name)
    {
        super(name);
    }

    protected void setUp() throws Exception
    {
        super.setUp();

        thisTest = new OneTest();
    }

    public static ISVNRemote getSession(String url, String configDirectory)
    {
        try
        {
            RemoteFactory factory = new RemoteFactory();
            factory.setConfigDirectory(configDirectory);
            factory.setUsername(USERNAME);
            factory.setPassword(PASSWORD);
            factory.setPrompt(new DefaultPromptUserPassword());

            ISVNRemote raSession = factory.openRemoteSession(url);
            assertNotNull("Null session was returned by factory", raSession);
            return raSession;
        }
        catch (Exception ex)
        {
            throw new RuntimeException(ex);
        }
    }

    private ISVNRemote getSession()
    {
        return getSession(getTestRepoUrl(), super.conf.getAbsolutePath());
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

    public void testGetSession_ConfigConstructor() throws Exception
    {
        ISVNRemote session;
        try
        {
            session = new RemoteFactory(
                super.conf.getAbsolutePath(),
                USERNAME, PASSWORD,
                new DefaultPromptUserPassword(), null)
                .openRemoteSession(getTestRepoUrl());
        }
        catch (ClientException ex)
        {
            throw new RuntimeException(ex);
        }
        assertNotNull("Null session was returned by factory", session);
        assertEquals(getTestRepoUrl(), session.getSessionUrl());
    }

    public void testDispose() throws Exception
    {
        ISVNRemote session = getSession();
        session.dispose();
    }

    public void testDatedRev() throws Exception
    {
        ISVNRemote session = getSession();

        long revision = session.getRevisionByDate(new Date());
        assertEquals(revision, 1);
    }

    public void testGetLocks() throws Exception
    {
        ISVNRemote session = getSession();

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

    public void testCheckPath() throws Exception
    {
        ISVNRemote session = getSession();

        NodeKind kind = session.checkPath("iota", 1);
        assertEquals(NodeKind.file, kind);

        kind = session.checkPath("iota", 0);
        assertEquals(NodeKind.none, kind);

        kind = session.checkPath("A", 1);
        assertEquals(NodeKind.dir, kind);
    }

    private String getTestRepoUrl()
    {
        return thisTest.getUrl().toASCIIString();
    }

    public void testGetLatestRevision() throws Exception
    {
        ISVNRemote session = getSession();
        long revision = session.getLatestRevision();
        assertEquals(revision, 1);
    }

    public void testGetUUID() throws Exception
    {
        ISVNRemote session = getSession();

        /*
         * Test UUID
         * TODO: Test for actual UUID once test dump file has
         * fixed UUID
         */
        assertNotNull(session.getReposUUID());
    }

    public void testGetUrl() throws Exception
    {
        ISVNRemote session = getSession();

        assertEquals(getTestRepoUrl(), session.getSessionUrl());
    }

    public void testGetRootUrl() throws Exception
    {
        ISVNRemote session = getSession();
        session.reparent(session.getSessionUrl() + "/A/B/E");
        assertEquals(getTestRepoUrl(), session.getReposRootUrl());
    }

    public void testGetUrl_viaSVNClient() throws Exception
    {
        ISVNRemote session = client.openRemoteSession(getTestRepoUrl());

        assertEquals(getTestRepoUrl(), session.getSessionUrl());
    }

    public void testGetUrl_viaSVNClientWorkingCopy() throws Exception
    {
        ISVNRemote session = client.openRemoteSession(thisTest.getWCPath());

        assertEquals(getTestRepoUrl(), session.getSessionUrl());
    }

    public void testReparent() throws Exception
    {
        ISVNRemote session = getSession();
        String newUrl = session.getSessionUrl() + "/A/B/E";
        session.reparent(newUrl);
        assertEquals(newUrl, session.getSessionUrl());
    }

    public void testGetRelativePath() throws Exception
    {
        ISVNRemote session = getSession();
        String baseUrl = session.getSessionUrl() + "/A/B/E";
        session.reparent(baseUrl);

        String relPath = session.getSessionRelativePath(baseUrl + "/alpha");
        assertEquals("alpha", relPath);

        relPath = session.getReposRelativePath(baseUrl + "/beta");
        assertEquals("A/B/E/beta", relPath);
    }

    public void testGetCommitEditor() throws Exception
    {
        ISVNRemote session = getSession();
        session.getCommitEditor();
    }

    public void testDisposeCommitEditor() throws Exception
    {
        ISVNRemote session = getSession();
        session.getCommitEditor();
        session.dispose();
    }

    public void testHasCapability() throws Exception
    {
        ISVNRemote session = getSession();
        assert(session.hasCapability(ISVNRemote.Capability.depth));
    }
}
