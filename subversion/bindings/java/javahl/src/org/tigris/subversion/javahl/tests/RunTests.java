/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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

import junit.framework.TestResult;
import junit.framework.TestSuite;
import junit.textui.TestRunner;

/**
 * A test runner, and comprehensive test suite definition.
 */
public class RunTests
{
    /**
     * The Subversion JavaHL test suite.
     */
    private static class SVNTestSuite extends TestSuite
    {
        /**
         * Create a conglomerate test suite containing all our test
         * suites.
         *
         * @return The complete test suite.
         */
        public static TestSuite suite()
        {
            TestSuite suite = new SVNTestSuite();
            suite.addTestSuite(SVNAdminTests.class);
            suite.addTestSuite(BasicTests.class);
            return suite;
        }
    }

    /**
     * Main method, will call all tests of all test classes
     * @param args command line arguments
     */
    public static void main(String[] args)
    {
        processArgs(args);
        TestResult testResult = TestRunner.run(SVNTestSuite.suite());
        if (testResult.errorCount() > 0 || testResult.failureCount() > 0)
        {
            System.exit(1);
        }
    }

    /**
     * Retrieve the root directory and the root url from the
     * command-line arguments, and set them on {@link
     * org.tigris.subversion.javahl.tests.SVNTests}.
     *
     * @param args The command line arguments to process.
     */
    private static void processArgs(String[] args)
    {
        if (args == null)
            return;

        for (int i = 0; i < args.length; i++)
        {
            String arg = args[i];
            if ("-d".equals(arg))
            {
                if (i + 1 < args.length)
                {
                    SVNTests.rootDirectoryName = args[++i];
                }
            }
            else if ("-u".equals(arg))
            {
                if (i + 1 < args.length)
                {
                    SVNTests.rootUrl = args[++i];
                }
            }
        }
    }
}
