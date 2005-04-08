#!/bin/sh

WWWDIR="`dirname \"$0\"`"

if [ "$1" = "all" ]; then
  WARNFILE=".validation-warnings.$$.tmp"
  for f in "$WWWDIR"/*.html; do
    if ! grep -F -q '<!DOCTYPE' "$f"; then
      RESULT='No <!DOCTYPE'
    else
      if "$WWWDIR/validate.sh" "$f" 2>"$WARNFILE"; then
        RESULT='\033[32mvalid\033[0m'
      else
        WARNLINES="`cat \"$WARNFILE\" | wc -l`"
        RESULT='\033[31;1mINVALID ('"$WARNLINES"')\033[0m'
      fi
    fi
    echo -e "$f: $RESULT"
  done
  rm -f "$WARNFILE"
  exit 0
fi

export SP_CHARSET_FIXED=YES
export SP_ENCODING=XML

# This path is system specific. Anyone know a good way of making this portable?
export SGML_CATALOG_FILES="/usr/share/OpenSP/xml.soc"

# Much of this script is concerned with setting up a local cache of the
# DTD and files required by the DTD, so they are not re-downloaded every time.

ensure ()
{
  LOCALFILE="$WWWDIR/`basename \"$1\"`"
  test ! -f "$LOCALFILE" && wget -O "$LOCALFILE" "$1"
}

ensure "http://style.tigris.org/tigris_transitional.dtd"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-lat1.ent"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-symbol.ent"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-special.ent"

if test ! -f "$WWWDIR/catalog"; then
  echo '
OVERRIDE YES
PUBLIC "-//Tigris//DTD XHTML 1.0 Transitional//EN" "tigris_transitional.dtd"
PUBLIC "-//W3C//ENTITIES Latin 1 for XHTML//EN" "xhtml-lat1.ent"
PUBLIC "-//W3C//ENTITIES Symbols for XHTML//EN" "xhtml-symbol.ent"
PUBLIC "-//W3C//ENTITIES Special for XHTML//EN" "xhtml-special.ent"
' > "$WWWDIR/catalog"
fi

onsgmls -wxml -wno-inclusion -ges "$1"
