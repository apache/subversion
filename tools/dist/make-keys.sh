#!/bin/sh

# Script to construct a KEYS file by fetching public keys of full
# committers as listed in the COMMITTERS file from people.apache.org.
#
# Based on "a piece of code" posted by danielsh on 22 Mar 2022 in the
# thread "Questions on Release Management Process":
# https://mail-archives.apache.org/mod_mbox/subversion-dev/202203.mbox/%3C20220323035056.GY7687%40tarpaulin.shahaf.local2%3E
#
# Run in the top directory of a checkout of SVN's sources, where the
# COMMITTERS file is located.
#
# This will download a bunch of .asc files and then cat them together
# to form a KEYS file.
#
# Requires curl. (Could be reworked to use wget, too, I suppose.)

if [ ! -f COMMITTERS ]; then
	echo "COMMITTERS file not found."
	exit 1
fi

for availid in $( perl -anE 'say $F[0] if (/^Blanket/../END ACTIVE FULL.*SCRIPTS LOOK FOR IT/ and /@/)' < COMMITTERS )
do
	key_url=https://people.apache.org/keys/committer/${availid}.asc
	
	echo -n "Fetching ${key_url}..."
	curl -sSfO ${key_url} 2> /dev/null

	if [ $? -eq 0 ]; then
		echo " OK"
	else
		echo " MISSING"
	fi
done

cat *.asc > KEYS
