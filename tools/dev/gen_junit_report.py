#!/bin/env python

# $Id$
"""
gen_junit_report.py -- The script is to generate the junit report for
Subversion tests.  The script uses the log file, tests.log created by
"make check" process. It parses the log file and generate the junit
files for each test separately in the specified output directory. The
script can take --log-file and --output-dir arguments.
"""

import sys
import os
import getopt

def xml_encode(data):
    """encode the xml characters in the data"""
    encode = {
      '&': '&amp;',
      '<': '&lt;',
      '>': '&gt;',
      '"': '&quot;',
      "'": '&apos;'
    }
    for char in encode.keys():
        data = data.replace(char, encode[char])
    return data

def start_junit():
    """define the beginning of xml document"""
    head = """<?xml version="1.0" encoding="UTF-8"?>"""
    return head

def start_testsuite(test_name):
    """start testsuite. The value for the attributes are replaced later
    when the junit file handling is concluded"""
    sub_test_name = test_name.replace('.', '-')
    start = """<testsuite time="ELAPSED_%s" tests="TOTAL_%s" name="%s"
    failures="FAIL_%s" errors="FAIL_%s" skipped="SKIP_%s">""" % \
    (test_name, test_name, sub_test_name, test_name, test_name, test_name)
    return start

def junit_testcase_ok(test_name, casename):
    """mark the test case as PASSED"""
    casename = xml_encode(casename)
    sub_test_name = test_name.replace('.', '-')
    case = """<testcase time="ELAPSED_CASE_%s" name="%s" classname="%s"/>""" % \
    (test_name, casename, sub_test_name)
    return case

def junit_testcase_fail(test_name, casename, reason=None):
    """mark the test case as FAILED"""
    casename = xml_encode(casename)
    sub_test_name = test_name.replace('.', '-')
    case = """<testcase time="ELAPSED_CASE_%s" name="%s" classname="%s">
      <failure type="Failed"><![CDATA[%s]]></failure>
    </testcase>""" % (test_name, casename, sub_test_name, reason)
    return case

def junit_testcase_xfail(test_name, casename, reason=None):
    """mark the test case as XFAILED"""
    casename = xml_encode(casename)
    sub_test_name = test_name.replace('.', '-')
    case = """<testcase time="ELAPSED_CASE_%s" name="%s" classname="%s">
      <system-out><![CDATA[%s]]></system-out>
    </testcase>""" % (test_name, casename, sub_test_name, reason)
    return case

def junit_testcase_skip(test_name, casename):
    """mark the test case as SKIPPED"""
    casename = xml_encode(casename)
    sub_test_name = test_name.replace('.', '-')
    case = """<testcase time="ELAPSED_CASE_%s" name="%s" classname="%s">
      <skipped message="Skipped"/>
    </testcase>""" % (test_name, casename, sub_test_name)
    return case

def end_testsuite():
    """mark the end of testsuite"""
    end = """</testsuite>"""
    return end

def update_stat(test_name, junit, count):
    """update the test statistics in the junit string"""
    junit_str = '\n'.join(junit)
    t_count = count[test_name]
    total = float(t_count['pass'] + t_count['fail'] + t_count['skip'])
    elapsed = float(t_count['elapsed'])
    case_time = 0
    if total > 0: # there are tests with no test cases
        case_time = elapsed/total

    total_patt = 'TOTAL_%s' % test_name
    fail_patt = 'FAIL_%s' % test_name
    skip_patt = 'SKIP_%s' % test_name
    elapsed_patt = 'ELAPSED_%s' % test_name
    elapsed_case_patt = 'ELAPSED_CASE_%s' % test_name

    # replace the pattern in junit string with actual statistics
    junit_str = junit_str.replace(total_patt, "%s" % total)
    junit_str = junit_str.replace(fail_patt, "%s" % t_count['fail'])
    junit_str = junit_str.replace(skip_patt, "%s" % t_count['skip'])
    junit_str = junit_str.replace(elapsed_patt, "%.3f" % elapsed)
    junit_str = junit_str.replace(elapsed_case_patt, "%.3f" % case_time)
    return junit_str

def main():
    """main method"""
    try:
        opts, args = getopt.getopt(sys.argv[1:], 'l:d:h',
                                  ['log-file=', 'output-dir=', 'help'])
    except getopt.GetoptError, err:
        usage(err)

    log_file = None
    output_dir = None
    for opt, value in opts:
        if (opt in ('-h', '--help')):
            usage()
        elif (opt in ('-l', '--log-file')):
            log_file = value
        elif (opt in ('-d', '--output-dir')):
            output_dir = value
        else:
            usage('Unable to recognize option')

    if not log_file or not output_dir:
        usage("The options --log-file and --output-dir are mandatory")

    # create junit output directory, if not exists
    if not os.path.exists(output_dir):
        print("Directory '%s' not exists, creating ..." % output_dir)
        try:
            os.makedirs(output_dir)
        except OSError, err:
            sys.stderr.write("ERROR: %s\n" % err)
            sys.exit(1)
    patterns = {
      'start' : 'START:',
      'end' : 'END:',
      'pass' : 'PASS:',
      'skip' : 'SKIP:',
      'fail' : 'FAIL:',
      'xfail' : 'XFAIL:',
      'elapsed' : 'ELAPSED:'
    }

    junit = []
    junit.append(start_junit())
    reason = None
    count = {}
    fp = None
    try:
        fp = open(log_file, 'r')
    except IOError, err:
        sys.stderr.write("ERROR: %s\n" % err)
        sys.exit(1)

    for line in fp.readlines():
        line = line.strip()
        if line.startswith(patterns['start']):
            reason = ""
            test_name = line.split(' ')[1]
            # replace '.' in test name with '_' to avoid confusing class
            # name in test result displayed in the CI user interface
            test_name.replace('.', '_')
            count[test_name] = {
              'pass' : 0,
              'skip' : 0,
              'fail' : 0,
              'xfail' : 0,
              'elapsed' : 0,
              'total' : 0
            }
            junit.append(start_testsuite(test_name))
        elif line.startswith(patterns['end']):
            junit.append(end_testsuite())
        elif line.startswith(patterns['pass']):
            reason = ""
            casename = line.strip(patterns['pass']).strip()
            junit.append(junit_testcase_ok(test_name, casename))
            count[test_name]['pass'] += 1
        elif line.startswith(patterns['skip']):
            reason = ""
            casename = line.strip(patterns['skip']).strip()
            junit.append(junit_testcase_skip(test_name, casename))
            count[test_name]['skip'] += 1
        elif line.startswith(patterns['fail']):
            casename = line.strip(patterns['fail']).strip()
            junit.append(junit_testcase_fail(test_name, casename, reason))
            count[test_name]['fail'] += 1
            reason = ""
        elif line.startswith(patterns['xfail']):
            casename = line.strip(patterns['xfail']).strip()
            junit.append(junit_testcase_xfail(test_name, casename, reason))
            count[test_name]['pass'] += 1
            reason = ""
        elif line.startswith(patterns['elapsed']):
            reason = ""
            elapsed = line.split(' ')[2].strip()
            (hrs, mins, secs) = elapsed.split(':')
            secs_taken = int(hrs)*24 + int(mins)*60 + float(secs)
            count[test_name]['elapsed'] = secs_taken

            junit_str = update_stat(test_name, junit, count)
            test_junit_file = os.path.join(output_dir,
                                           "%s.junit.xml" % test_name)
            w_fp = open (test_junit_file, 'w')
            w_fp.writelines(junit_str)
            w_fp.close()
            junit = []
        elif len(line):
            reason = "%s\n%s" % (reason, line)
    fp.close()

def usage(errorMsg=None):
    script_name = os.path.basename(sys.argv[0])
    sys.stdout.write("""USAGE: %s: [--help|h] --log-file|l --output-dir|d

Options:
  --help|-h       Display help message
  --log-file|l    The log file to parse for generating junit xml files
  --output-dir|d  The directory to create the junit xml file for each
                  test
""" % script_name)
    if errorMsg is not None:
        sys.stderr.write("\nERROR: %s\n" % errorMsg)
        sys.exit(1)
    sys.exit(0)

if __name__ == '__main__':
    main()
