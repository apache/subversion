#!/bin/sh
HELP="\
Usage: $0 [--remove] [FILE...]

Insert or remove the GCC attribute \"warn_unused_result\" on each function
that returns a Subversion error, in the specified files or, by default,
*.h and *.c in the ./subversion and ./tools trees.
"

LC_ALL=C

# Parse options
REMOVE=
case "$1" in
--remove) REMOVE=1; shift;;
--help)   echo "$HELP"; exit 0;;
--*)      echo "$0: unknown option \"$1\"; try \"--help\""; exit 1;;
esac

# Set the positional parameters to the default files if none specified
if [ $# = 0 ]; then
  set -- `find subversion/ tools/ -name '*.[ch]'`
fi

# A line that declares a function return type of "svn_error_t *" looks like:
# - Possibly leading whitespace, though not often.
# - Possibly "static" or "typedef".
# - The return type "svn_error_t *".
# - Possibly a function or pointer-to-function declarator:
#     - "identifier"
#     - "(identifier)"  (used in some typedefs)
#     - "(*identifier)"
#   with either nothing more, or a "(" next (especially not "," or ";" or "="
#   which all indicate a variable rather than a function).

# Regular expressions for "sed"
# Note: take care in matching back-reference numbers to parentheses
PREFIX="^\( *\| *static  *\| *typedef  *\)"
RET_TYPE="\(svn_error_t *\* *\)"
IDENT="[a-zA-Z_][a-zA-Z0-9_]*"
DECLR="\($IDENT\|( *\(\*\|\) *$IDENT *)\)"
SUFFIX="\($DECLR *\((.*\|\)\|\)$"

# The attribute string to be inserted or removed
ATTRIB_RE="__attribute__((warn_unused_result))"  # regex version of it
ATTRIB_STR="__attribute__((warn_unused_result))"  # plain text version of it

for F do
  if [ -f "$F" ]; then
    # Edit the file, leaving a backup suffixed with a tilde
    mv -f "$F" "$F~"
    if [ $REMOVE ]; then
      sed "s/$PREFIX$ATTRIB_RE $RET_TYPE$SUFFIX/\1\2\3/" < "$F~" > "$F"
    else
      sed "s/$PREFIX$RET_TYPE$SUFFIX/\1$ATTRIB_STR \2\3/" < "$F~" > "$F"
    fi
    # If not changed, put the untouched file back
    if cmp -s "$F" "$F~"; then
      mv -f "$F~" "$F"
    fi
  else
    echo "$0: skipping \"$F\": is not a regular file"
  fi
done
