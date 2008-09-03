#!/usr/bin/env python

from distutils.core import setup

setup(name='Subversion Python Bindings',
      version='1.0',
      description='Python bindings for the Subversion version control system.',
      author='The Subversion Team',
      author_email='dev@subversion.tigris.org',
      url='http://subversion.tigris.org',
      packages=['csvn', 'csvn.core', 'csvn.ext'],
     )
