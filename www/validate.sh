#!/bin/bash

# Yes, this script is aching to be rewritten in Python.
# It was never supposed to grow this big!

if [ $# -ne 1 ]; then
  echo "Usage: 1.  ./validate.sh <filename>" >&2
  echo "       2.  ./validate.sh all" >&2
  exit 1
fi

WWWDIR="`dirname \"$0\"`"

# Much of this script is concerned with setting up a local cache of the
# DTD and files required by the DTD, so they are not re-downloaded every time.

ensure ()
{
  BASENAME="`basename \"$1\"`"
  if [ -n "$2" ]; then
    LOCALFILE="$WWWDIR/$2/$BASENAME"
  else
    LOCALFILE="$WWWDIR/$BASENAME"
  fi
  test ! -f "$LOCALFILE" && wget -O "$LOCALFILE" "$1"
}

ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-lat1.ent"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-symbol.ent"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-special.ent"

ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"

# Do not need these, but the URLs are here for uncommenting if you like
#ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml1-frameset.dtd"
#ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"

# If you have ongmls installed, then you almost certainly have a SGML
# declaration for XML installed already, but there is no cross-platform way of
# knowing where.  So, instead, just download one, since we had to download
# other things anyway.
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml.soc"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml1.dcl"

# If you are doing any serious hacking on the web pages, you probably want
# the CSS locally too.
mkdir -p "$WWWDIR/tigris-branding"
mkdir -p "$WWWDIR/tigris-branding/css"
mkdir -p "$WWWDIR/tigris-branding/scripts"
mkdir -p "$WWWDIR/tigris-branding/images"
ensure "http://subversion.tigris.org/branding/css/tigris.css" \
  "tigris-branding/css"
ensure "http://subversion.tigris.org/branding/css/inst.css" \
  "tigris-branding/css"
ensure "http://subversion.tigris.org/branding/css/print.css" \
  "tigris-branding/css"
ensure "http://subversion.tigris.org/branding/scripts/tigris.js" \
  "tigris-branding/scripts"

for f in `sed -n -e 's,.*url(\.\./images/\([^)]*\).*,\1,;tp' -etp -ed -e:p -ep \
  $WWWDIR/tigris-branding/css/*.css`; do
  ensure "http://subversion.tigris.org/branding/images/$f" \
    "tigris-branding/images"
done


export SGML_CATALOG_FILES="$WWWDIR/xhtml.soc"

if [ -z "$XML_VALIDATOR" ]; then
  if [ "`type -p xmllint`" != "" ]; then
    export XML_VALIDATOR="xmllint"
  else
    if [ "`type -p onsgmls`" != "" ]; then
      export XML_VALIDATOR="onsgmls"
    else
      echo "No XML validator found!" >&2
      exit 1
    fi
  fi
fi

echo "Selected XML validator: '$XML_VALIDATOR'"

validate ()
{
  case $XML_VALIDATOR in
    onsgmls)
    SP_CHARSET_FIXED=YES SP_ENCODING=XML \
    onsgmls -wxml -ges "$1"
    ;;
    xmllint)
    xmllint --nonet --noout --valid --catalogs "$1"
    ;;
    *)
    echo "Internal error - unknown XML validator '$XML_VALIDATOR'!" >&2
    exit 1
    ;;
  esac
}

if [ "$1" = "all" ]; then
  WARNFILE=".validation-warnings.$$.tmp"
  for f in "$WWWDIR"/*.html; do
    case $f in
      */project_tools.html)
      RESULT='Skipped'
      ;;
      *)
      validate "$f" 2>"$WARNFILE"
      if [ $? -eq 0 ]; then
        RESULT='\033[32mvalid\033[0m'
      else
        WARNLINES="`cat \"$WARNFILE\" | wc -l`"
        RESULT='\033[31;1mINVALID ('"$WARNLINES"')\033[0m'
      fi
      cat "$WARNFILE"
      ;;
    esac
    echo -e "$f: $RESULT"
  done
  rm -f "$WARNFILE"
  exit 0
fi

validate "$1"

