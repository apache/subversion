#!/usr/bin/env python
# -*- mode:Python; tab-width: 4 -*-
"""
The purpose of this script is to find out which users have not
commited changes during the last 14 days (in which case their
file lock is released) or 90 days (commit access revoked). Simply
parses the output of svn's log command and displays how long ago
each author commited a change to the repository.

This script was written by Grzegorz Adam Hankiewicz
(gradha@titanium.sabren.com), and is released into the public
domain without warranty of any kind. Do what you want with it,
but you are responsible for it erasing your whole hard disk. I'm
not hearing you, la la la la la...
"""

import popen2
import sys
import time
import xml.parsers.expat


# Some globals.
LOOKING_FOR = ["gradha", "ruben", "beerfrick", "dbrouard"]
STATISTICS = []
COMMIT_NUMBER = 0
SECONDS_A_DAY = 60 * 60 * 24


class _STATE:
    """Records the parsing state of the input document.

    self.num is a special integer indicating where in the tree we
    are parsing. It will be set to a negative value if the reader
    can stop processing input because the application decided we
    already have all required data.

    self.earliest_date is the oldest date retrieved from the
    input log.

    self.author is a list of strings retrieved from the input log
    with the author data retrieved from the XML file. This value
    is not persistant.

    self.date is a list of strings retrieved from the input log
    with the date data retrieved from the XML file. This value is
    not persistant.
    """
    def __init__(self):
        self.num = 0
        self.earliest_date = ""
        self.author = []
        self.date = []

    def read_and_reset_data(self):
        """f() -> (author, date)

        Retrieves the author and date strings retrieved so far,
        each collapsed into a very long string which has been
        stripped of whitespace. This call also has the side effect
        of deleting the so far parsed data.
        """
        author = "".join(self.author).strip()
        self.author = []
        date = "".join(self.date).strip()
        self.date = []

        return author, date


STATE = _STATE()


def parse_date(text):
    """f(date_string) -> (time_tupe, cropped_date_string)

    Pass a string containing the date as retrieved from Subversion's
    log file. Returns a tuple with the time tuple and a version of
    the date string cropped to day resolution.
    """
    return time.strptime(text[:19], "%Y-%m-%dT%H:%M:%S"), text[:19]

    
def start_element(name, dummy):
    """Processes logentry, author and date XML start tags. Changes state."""
    if STATE.num == 0:
        if name == "logentry":
            global COMMIT_NUMBER
            COMMIT_NUMBER += 1
            STATE.num = 1
    elif STATE.num == 1:
        if name == "author":
            STATE.num = 2
        elif name == "date":
            STATE.num = 3


def end_element(name):
    """Processes logentry, author and date XML end tags. Changes state."""
    if STATE.num == 1 and name == "logentry":
        STATE.num = 0
        process_log_entry()
    elif STATE.num == 2 and name == "author":
        STATE.num = 1
    elif STATE.num == 3 and name == "date":
        STATE.num = 1
        

def char_data(data):
    """If in the correct state, appends data to the global state data."""
    if STATE.num == 2:
        STATE.author.append(data)
    elif STATE.num == 3:
        STATE.date.append(data)


def process_log_entry():
    """Called when a logentry XML end tag is found in the input.

    Retrieves the so far parsed data. If the data correspons to one
    of the looked for authors, it is removed from the list and the
    data are appended to a global list. If this was also the last
    author to look for, signals with a global state change that we
    don't need more XML processing.
    """
    author, date = STATE.read_and_reset_data()
    STATE.earliest_date = date

    if author in LOOKING_FOR:
        LOOKING_FOR.remove(author)
        STATISTICS.append((author, date))

        if len(LOOKING_FOR) == 0:
            STATE.num = -1


def print_statistics():
    """Prints out the retrieved statistics of the authors.

    For authors who have been found in the log, the function outputs
    their last commited change data, and indicates how many days
    ago that was. Not found authors will assigned the earliest log
    date found.
    """
    request = [author for author, date in STATISTICS] + LOOKING_FOR
    request.sort()
    results = obtain_statistics(request)

    for author in request:
        date_text, days, commit = results[author]
        if commit:
            print "Last change from %s on %s, %d days ago" % (author,
                date_text, days)
        else:
            print "No changes from %s since %s, %d days ago" % (author,
                date_text, days)
    return


def obtain_statistics(author_list):
    """f([nicks]) -> {nicks:(statistics_triplet)}

    For a list of nicks, returns a dictionary with the nicks as
    keys and the statistics as values. If a nick is not found in
    the statistics, its value is None. Otherwise, it is a tuple
    with a text string date, the the number of days ago in floating
    point, and a boolean saying whether a commit was found or not
    in the statistics.
    """
    earliest_time, earliest_text = parse_date(STATE.earliest_date)
    current_time = time.localtime()
    current_seconds = time.mktime(current_time)
    statistics = dict(STATISTICS)

    results = {}
    for author in author_list:
        if author in statistics:
            date_time, date_text = parse_date(statistics[author])
            date_seconds = time.mktime(date_time)

            days = (current_seconds - date_seconds) / SECONDS_A_DAY
            results[author] = (date_text[:10], days, 1)
        else:
            earliest_seconds = time.mktime(earliest_time)
            days = (current_seconds - earliest_seconds) / SECONDS_A_DAY
            results[author] = (earliest_text[:10], days, 0)

    return results
    

def update_working_copy():
    """Runs 'svn update' discarding any output."""
    stdout, stdin, stderr = popen2.popen3(["svn", "update"])
    stdin.close()
    stderr.close()
    stdout.readlines()


def obtain_information():
    """Fills data into the STATISTICS and STATE global variables.

    Creates an expat parser, runs the external 'svn log' command
    and connects its output to the XML parsing of expat. Commit
    information about the authors and the whole progress of the
    project will be gathered.
    """
    p = xml.parsers.expat.ParserCreate()
    
    p.StartElementHandler = start_element
    p.EndElementHandler = end_element
    p.CharacterDataHandler = char_data

    stdout, stdin, stderr = popen2.popen3(["svn", "log", "--xml"])
    stdin.close()
    stderr.close()

    try:
        line = stdout.readline()
        while line:
            p.Parse(line)
            if STATE.num < 0:
                print "Aborting!"
                sys.exit(0)
            line = stdout.readline()

    finally:
        stdout.close()


def main():
    """Main entry point of the application.

    First obtains information from the authors, then prints it.
    
    end of the operation, statistics are printed.
    """
    update_working_copy()
    obtain_information()
    print_statistics()
    

if __name__ == "__main__":
    main()
