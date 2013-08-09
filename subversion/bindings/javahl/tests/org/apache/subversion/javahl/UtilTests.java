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

import java.io.File;
import java.io.FileOutputStream;
import java.io.ByteArrayOutputStream;
import java.util.Arrays;

import org.junit.Assert;

/**
 * Tests the JavaHL SVNUtil APIs.
 */
public class UtilTests extends SVNTests
{
    /**
     * Base name of all our tests.
     */
    public final static String testName = "util_test";

    public UtilTests()
    {
        init();
    }

    public UtilTests(String name)
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

    private final byte[] fileContentsPrefix = "1\n2\n3\n4".getBytes();
    private final byte[] fileContentsSuffix = "N-3\nN-2\nN-1\nN\n".getBytes();

    private void writeFileContents(File file, String contents) throws Throwable
    {
        FileOutputStream out = new FileOutputStream(file);
        out.write(fileContentsPrefix);
        out.write(contents.getBytes());
        out.write(fileContentsSuffix);
        out.close();
    }

    public void testFileMerge() throws Throwable
    {
        File original = File.createTempFile("merge", ".original", localTmp);
        File modified = File.createTempFile("merge", ".modified", localTmp);
        File latest = File.createTempFile("merge", ".latest", localTmp);

        writeFileContents(original, "\noriginal\n");
        writeFileContents(modified, "\nmodified\n");
        writeFileContents(latest, "\nlatest\n");

        ByteArrayOutputStream result = new ByteArrayOutputStream();
        SVNUtil.FileMerge(original.getAbsolutePath(),
                          modified.getAbsolutePath(),
                          latest.getAbsolutePath(),
                          null,
                          "||||||| base",
                          "<<<<<<< branch",
                          ">>>>>>> local",
                          "=======",
                          SVNUtil.ConflictDisplayStyle.modified_original_latest,
                          result);

        final byte[] expected = ("1\n2\n3\n4\n" +
                                 "<<<<<<< branch\n" +
                                 "modified\n" +
                                 "||||||| base\n" +
                                 "original\n" +
                                 "=======\n" +
                                 "latest\n" +
                                 ">>>>>>> local\n" +
                                 "N-3\nN-2\nN-1\nN\n").getBytes();
        Assert.assertArrayEquals(expected, result.toByteArray());
    }
}
