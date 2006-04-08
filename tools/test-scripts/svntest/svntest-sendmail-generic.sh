#!/bin/sh

EXEC_PATH="`dirname $0`"
TO_ADDR="$1"
REPLY_TO_ADDR="$2"
SUBJECT="$3"
BODY_FILE="$4"
PAYLOAD_GZIP_FILE="$5"



# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

if [ -z "$TO_ADDR" -o -z "$REPLY_TO_ADDR" -o -z "$SUBJECT" -o -z "$BODY_FILE" ]
then
    $SENDMAIL -t <<EOF
From: $FROM
Subject: ERROR: invalid email
To: $ERROR_TO

Wrong parameters for $0
to: "$TO_ADDR"
reply-to: "$REPLY_TO_ADDR"
subject: "$SUBJECT" 
body: "$BODY_FILE"
payload: "$PAYLOAD_GZIP_FILE"
EOF
    exit 1
fi


# Send the status mail
MAILFILE="/tmp/svntest.$$"
NEXT_PART="NextPart-$$"

$CAT <<EOF > "$MAILFILE"
From: $FROM
Subject: $SUBJECT
Reply-To: $REPLY_TO_ADDR
To: $TO_ADDR
EOF

if [ ! -f "$BODY_FILE" ]
then
    echo "" >> "$MAILFILE"
    echo "Ooops, missing body" >> "$MAILFILE"
    echo "file name: $BODY_FILE" >> "$MAILFILE"

elif [ ! -f "$PAYLOAD_GZIP_FILE" ]
then
    echo "" >> "$MAILFILE"
    $CAT $BODY_FILE >> "$MAILFILE"

else
    $CAT <<EOF >> "$MAILFILE"
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="----------=_$NEXT_PART"

This is a multi-part message in MIME format.
------------=_$NEXT_PART
Content-Type: text/plain; charset=us-ascii
Content-Transfer-Encoding: 8bit

EOF
    $CAT "$BODY_FILE" >> "$MAILFILE"
    $CAT <<EOF >> "$MAILFILE"
------------=_$NEXT_PART
Content-Type: application/x-gzip; name="tests.log.gz"
Content-Transfer-Encoding: base64
Content-Disposition: inline; filename="tests.log.gz"

EOF
    $BASE64 < "$PAYLOAD_GZIP_FILE" >> "$MAILFILE"
    $CAT <<EOF >> "$MAILFILE"
------------=_$NEXT_PART--
EOF
fi

$SENDMAIL -t < "$MAILFILE"
$RM_F "$MAILFILE"
