#!/usr/bin/env python
# -*- mode:Python; tab-width: 4; coding: ISO-8859-1 -*-
"""
This script is used each weekend to generate a status report which
will be mailed to the translation mailing list. The script extracts
information from the file TRABAJO.

This script was written by Grzegorz Adam Hankiewicz
(gradha@titanium.sabren.com), and is released into the public
domain without warranty of any kind. Do what you want with it,
but you are responsible for it erasing your whole hard disk. I'm
not hearing you, la la la la la...
"""

import author_statistics
import time


SECTIONS = [
    "Relación nombre usuario <-> nombre real:",
    "Ficheros huérfanos con traducción parcial:",
    "Ficheros en proceso de traducción:",
    "Ficheros que han completado al menos una traducción básica:",
    "Ficheros revisados sin verificar el original:",
    "Ficheros revisados verificando el original:"]


def parse_file(input_lines):
    """f([lines]) -> {commiters}, {file_status}

    commiters is a simple dictionary where a tigris user name maps
    to the full person name.

    file_status is a dictionary with xml files as keys. Their data
    is stored as a list of pairs. The first element of the pair is
    the section number and the second element is the string with
    the person(s) who did work on it for that section.
    """
    commiters = {}
    file_status = {}
    section = 0

    # Loop over all lines.
    for line in input_lines:
        line = line.strip()
        # Ignore empty lines or separators.
        if not line or line[:5] == "-----":
            continue

        # Detect section headers.
        for f in range(len(SECTIONS)):
            if SECTIONS[f] == line:
                section = f + 1
                break
        else:
            # Try to parse content of sections.
            if section == 1:
                # Add the nick/name relationship.
                nick, name = line.split(None, 1)
                commiters[nick] = name
            elif section >= 3 and section <= 6:
                # Add the file and authors.
                filename, rest = line.split(":")
                rest = [x.strip() for x in rest.strip().split(",")]
                if filename in file_status:
                    file_status[filename].append((section, rest))
                else:
                    file_status[filename] = [(section, rest)]
            
    return commiters, file_status


def print_header(string):
    """f(string)

    Prints the string underlined with dashes."""
    print string
    print "-" * len(string)

    
def show_commiter_info(commiters):
    """f({commiters})

    Prints a sorted list of people with write access.
    """
    statistics = author_statistics.obtain_statistics(commiters.keys())

    print_header("Usuarios con acceso de escritura al repositorio:")

    results = []
    for nick, full in commiters.iteritems():
        date_text, days, commit = statistics[nick]
        results.append((full, date_text, days, commit))
    results.sort()
    
    for name, date_text, days, commit in results:
        if commit:
            print "Último cambio de", name, "en", date_text, \
                "hace %d días." % days
        else:
            print "Sin cambios de", name, "desde", date_text, \
                "hace %d días." % days
    print


def show_section_work(section, commiters, file_status):
    """f(int, {commiters}, {file_status})

    Displays the status of the files in section. Nicks are replaced
    with the help of the commiters dictionary.
    """
    files = []
    for key, pair in file_status.iteritems():
        for number, nicks in pair:
            if number == section:
                # Extract possible nicks from author part
                authors = []
                for nick in nicks:
                    if nick in commiters:
                        authors.append(commiters[nick])
                    else:
                        authors.append(nick)
                assert(authors)
                authors.sort()
                files.append((key, authors))

    # Print report only if section contains files.
    if files:
        files.sort()
        print_header(SECTIONS[section-1])
        for filename, authors in files:
            print "%s: %s." % (filename, ", ".join(authors))
        print


def main():
    """Main entry point of the application.

    Reads the file TRABAJO, parses it, and generates a report.
    """
    author_statistics.obtain_information()
    file_input = file("TRABAJO", "rt")
    input_lines = file_input.readlines()
    file_input.close()

    commiters, file_status = parse_file(input_lines)

    date = time.strftime("día %d del mes %m del %Y", time.localtime())
    print "Estado de la traducción, a %s\n" % date

    show_commiter_info(commiters)

    show_section_work(3, commiters, file_status)
    show_section_work(4, commiters, file_status)
    show_section_work(5, commiters, file_status)
    show_section_work(6, commiters, file_status)


if __name__ == "__main__":
    main()
