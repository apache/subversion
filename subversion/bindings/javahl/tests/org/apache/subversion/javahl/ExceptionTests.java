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

import org.apache.subversion.javahl.callback.*;
import org.apache.subversion.javahl.types.*;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.ByteArrayOutputStream;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;
import java.text.ParseException;
import java.util.Collection;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.Map;
import java.text.DateFormat;
import java.text.SimpleDateFormat;


/**
 * Tests the basic functionality of javahl binding (inspired by the
 * tests in subversion/tests/cmdline/basic_tests.py).
 */
public class ExceptionTests extends SVNTests
{
    /**
     * Base name of all our tests.
     */
    public final static String testName = "exception_test";

    public ExceptionTests()
    {
        init();
    }

    public ExceptionTests(String name)
    {
        super(name);
        init();
    }

    /**
     * Initialize the testBaseName and the testCounter, if this is the
     * first test of this class.
     */
    private void init()
    {
        if (!testName.equals(testBaseName))
        {
            testCounter = 0;
            testBaseName = testName;
        }
    }

    /**
     * Specific exception class to verify if the marshalling of errors
     * through Subversion is handled properly.
     */
    final class TestException extends RuntimeException
    {
        private static final long serialVersionUID = 1L;

        public TestException(String message)
        {
            super(message);
        }

        public TestException(String message, Throwable cause)
        {
            super(message, cause);
        }
    }

    public void testStatusCallback() throws Exception
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        final TestException theException = new TestException("The Exception");
        boolean handled = false;
        // Test status of non-existent file
        try
        {
            client.status(thisTest.getWorkingCopy() + "/A", Depth.immediates,
                          false, true, true, false, false, false, null,
                          new StatusCallback()
                          {
                            public void doStatus(String path, Status status)
                            {
                                throw new TestException("inner", theException);
                            }
                          });
        }
        catch (ClientException e)
        {
            if (VerifyCause(e, theException))
                handled = true;
            else
                throw e;
        }
        assertTrue(handled);
    }

    public void testInfoCallback() throws Exception
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        final TestException theException = new TestException("The Exception");
        boolean handled = false;
        // Test status of non-existent file
        try
        {
            client.info(thisTest.getWorkingCopy() + "/A", null, null, Depth.immediates,
                        true, true, false, null,
                          new InfoCallback()
                          {
                            public void singleInfo(Info info)
                            {
                                throw new TestException("inner", theException);
                            }
                          });
        }
        catch (ClientException e)
        {
            if (VerifyCause(e, theException))
                handled = true;
            else
                throw e;
        }
        assertTrue(handled);
    }

    public void testListCallback() throws Exception
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        final TestException theException = new TestException("The Exception");
        boolean handled = false;
        // Test status of non-existent file
        try
        {
            client.list(thisTest.getWorkingCopy() + "/A", null, null, Depth.immediates,
                        7, false,
                        new ListCallback()
                          {
                            public void doEntry(DirEntry dirent, Lock lock)
                            {
                                throw new TestException("inner", theException);
                            }
                          });
        }
        catch (ClientException e)
        {
            if (VerifyCause(e, theException))
                handled = true;
            else
                throw e;
        }
        assertTrue(handled);
    }

    public void testBlameCallback() throws Exception
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        final TestException theException = new TestException("The Exception");
        boolean handled = false;
        // Test status of non-existent file
        try
        {
            client.blame(thisTest.getWorkingCopy() + "/iota",
                         Revision.getInstance(1), Revision.getInstance(1),
                         Revision.getInstance(1), false, false,
                         new BlameCallback()
                          {
                            public void singleLine(long lineNum, long revision,
                                                   Map<String, byte[]> revProps, long mergedRevision,
                                                   Map<String, byte[]> mergedRevProps,
                                                   String mergedPath, String line,
                                                   boolean localChange)
                            {
                                throw new TestException("inner", theException);
                            }
                          });
        }
        catch (ClientException e)
        {
            if (VerifyCause(e, theException))
                handled = true;
            else
                throw e;
        }
        assertTrue(handled);
    }

    public void testLogMessageCallback() throws Exception
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        final TestException theException = new TestException("The Exception");
        boolean handled = false;
        // Test status of non-existent file
        try
        {
            List<RevisionRange> ranges = new ArrayList<RevisionRange>(1);
            ranges.add(new RevisionRange(null, null));
            // Testing variant with allRevProps = false
            client.logMessages(thisTest.getWorkingCopy() + "/iota",
                               Revision.getInstance(1), ranges,
                               false, false, false,
                               null, false, 2,
                               new LogMessageCallback()
                               {
                                    public void singleMessage(
                                                  Set<ChangePath> changedPaths,
                                                  long revision,
                                                  Map<String, byte[]> revprops,
                                                  boolean hasChildren)
                                    {
                                        throw new TestException("inner",
                                                                theException);
                                    }
                               });
        }
        catch (ClientException e)
        {
            if (VerifyCause(e, theException))
                handled = true;
            else
                throw e;
        }
        assertTrue(handled);
    }

    public void testDiffSummaryReceiver() throws Exception
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        final TestException theException = new TestException("The Exception");
        boolean handled = false;
        // Test status of non-existent file
        try
        {
            List<RevisionRange> ranges = new ArrayList<RevisionRange>(1);
            ranges.add(new RevisionRange(null, null));
            client.diffSummarize(thisTest.getUrl() + "/A",
                                 Revision.getInstance(1),
                                 thisTest.getUrl() + "/A",
                                 Revision.getInstance(0),
                                 Depth.infinity, null, true,
                                 new DiffSummaryCallback()
                                 {
                                    public void onSummary(
                                                  DiffSummary descriptor)
                                    {
                                        throw new TestException("inner",
                                                                theException);
                                    }
                               });
        }
        catch (ClientException e)
        {
            if (VerifyCause(e, theException))
                handled = true;
            else
                throw e;
        }
        assertTrue(handled);
    }

    public void testNotify() throws Exception
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        final TestException theException = new TestException("The Exception");
        boolean handled = false;
        // Test status of non-existent file
        try
        {
            client.notification2(new ClientNotifyCallback()
                                 {
                                    public void onNotify(ClientNotifyInformation info)
                                    {
                                        throw new TestException("inner",
                                                                theException);
                                    }
                                });

            client.remove(thisTest.getWCPathSet("/A"), false, false,
                          null, null, null);
        }
        catch (ClientException e)
        {
            if (VerifyCause(e, theException))
                handled = true;
            else
                throw e;
        }
        assertTrue(handled);
    }

    /**
     * Verifies if a specific throwable instance is recorded in the exception chain
     */
    private boolean VerifyCause(Throwable caught, Throwable needle)
    {
        if (caught == needle)
            return true;

        Throwable cause = caught.getCause();
        if (cause == null)
            return false;

        return VerifyCause(cause, needle);
    }
}
