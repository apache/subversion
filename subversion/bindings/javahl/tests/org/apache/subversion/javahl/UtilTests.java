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
    private final static String NL = System.getProperty("line.separator");

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

    private final byte[] fileContentsPrefix = "1\n2\n3\n4\n".getBytes();
    private final byte[] fileContentsSuffix = "N-3\nN-2\nN-1\nN\n".getBytes();

    private void writeFileContents(File file, String contents) throws Throwable
    {
        FileOutputStream out = new FileOutputStream(file);
        out.write(fileContentsPrefix);
        out.write(contents.getBytes());
        out.write(fileContentsSuffix);
        out.close();
    }

    public void testFileDiff() throws Throwable
    {
        File original = File.createTempFile("merge", ".original", localTmp);
        File modified = File.createTempFile("merge", ".modified", localTmp);

        writeFileContents(original, "\noriginal\n\n");
        writeFileContents(modified, "\nmodified\n\n");

        ByteArrayOutputStream result = new ByteArrayOutputStream();
        boolean diffs =  SVNUtil.fileDiff(original.getAbsolutePath(),
                                          modified.getAbsolutePath(),
                                          null, "original", "modified", "UTF-8",
                                          null, result);
        assertTrue(diffs);

        final byte[] expected = ("--- original" + NL +
                                 "+++ modified" + NL +
                                 "@@ -3,7 +3,7 @@" + NL +
                                 " 3\n 4\n \n" +
                                 "-original\n" +
                                 "+modified\n" +
                                 " \n N-3\n N-2\n").getBytes();
        Assert.assertArrayEquals(expected, result.toByteArray());
    }

    public void testFileMerge() throws Throwable
    {
        File original = File.createTempFile("merge", ".original", localTmp);
        File modified = File.createTempFile("merge", ".modified", localTmp);
        File latest = File.createTempFile("merge", ".latest", localTmp);

        writeFileContents(original, "\noriginal\n\n");
        writeFileContents(modified, "\nmodified\n\n");
        writeFileContents(latest, "\nlatest\n\n");

        ByteArrayOutputStream result = new ByteArrayOutputStream();
        boolean conflicts = SVNUtil.fileMerge(original.getAbsolutePath(),
                                              modified.getAbsolutePath(),
                                              latest.getAbsolutePath(),
                                              null,
                                              "||||||| base",
                                              "<<<<<<< branch",
                                              ">>>>>>> local",
                                              "=======",
                                              SVNUtil.ConflictDisplayStyle.modified_original_latest,
                                              result);
        assertTrue(conflicts);

        final byte[] expected = ("1\n2\n3\n4\n\n" +
                                 "<<<<<<< branch" + NL +
                                 "modified\n" +
                                 "||||||| base" + NL +
                                 "original\n" +
                                 "=======" + NL +
                                 "latest\n" +
                                 ">>>>>>> local" + NL +
                                 "\nN-3\nN-2\nN-1\nN\n").getBytes();
        Assert.assertArrayEquals(expected, result.toByteArray());
    }
}
