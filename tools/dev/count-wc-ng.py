#!/usr/bin/env python

import os

SKIP = ['deprecated.c',
        'entries.c',
        'entries.h',
        'old-and-busted.c']

TERMS = ['svn_wc_adm_access_t',
         'svn_wc_entry_t',
         'svn_wc__node_',
         'log_accum',
         'svn_wc__wq_add_loggy',
         ]


def get_files_in(path):
  names = os.listdir(path)
  for skip in SKIP:
    try:
      names.remove(skip)
    except ValueError:
      pass
  return [os.path.join(path, fname) for fname in names
          if fname.endswith('.c') or fname.endswith('.h')]


def count_terms_in(path):
  files = get_files_in(path)
  counts = {}
  for term in TERMS:
    counts[term] = 0
    for filepath in get_files_in(path):
      counts[term] += open(filepath).read().count(term)
  return counts


def print_report():
  client = count_terms_in('../../subversion/libsvn_client')
  wc = count_terms_in('../../subversion/libsvn_wc')

  client_total = 0
  wc_total = 0

  FMT = '%22s |%14s |%10s |%6s'
  SEP = '%s+%s+%s+%s' % (23*'-', 15*'-', 11*'-', 7*'-')

  print FMT % ('', 'libsvn_client', 'libsvn_wc', 'Total')
  print SEP
  for term in TERMS:
    print FMT % (term, client[term], wc[term], client[term] + wc[term])
    client_total += client[term]
    wc_total += wc[term]
  print SEP
  print FMT % ('', client_total, wc_total, client_total + wc_total)


if __name__ == '__main__':
  print_report()
