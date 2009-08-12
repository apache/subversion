#!/bin/sh

BLECH=`pwd`

cd ../../subversion/libsvn_client
CLIENT_ACCESS=`ls *.[ch] | grep -v deprecated | xargs grep svn_wc_adm_access_t | wc -l`
CLIENT_ENTRY=`ls *.[ch] | grep -v deprecated | xargs grep svn_wc_entry_t | wc -l`

cd ../../subversion/libsvn_wc
WC_ACCESS=`ls *.[ch] | grep -v deprecated | xargs grep svn_wc_adm_access_t | wc -l`
WC_ENTRY=`ls *.[ch] | grep -v deprecated | xargs grep svn_wc_entry_t | wc -l`

WC=`expr $WC_ACCESS + $WC_ENTRY`
CLIENT=`expr $CLIENT_ACCESS + $CLIENT_ENTRY`
ENTRY=`expr $WC_ENTRY + $CLIENT_ENTRY`
ACCESS=`expr $WC_ACCESS + $CLIENT_ACCESS`
TOTAL=`expr $ENTRY + $ACCESS`

printf '%15s |%20s |%20s |%10s\n' \
                    " " "svn_wc_adm_access_t" "svn_wc_entry_t" "Total"
echo   "----------------+---------------------+---------------------+-----------"
printf '%15s |%20d |%20d |%10d\n' \
                    "libsvn_client" "$CLIENT_ACCESS" "$CLIENT_ENTRY" "$CLIENT"
printf '%15s |%20d |%20d |%10d\n' \
                    "libsvn_wc" "$WC_ACCESS" "$WC_ENTRY" "$WC"
echo   "----------------+---------------------+---------------------+-----------"
printf '%15s |%20d |%20d |%10d\n' \
                    "Total" "$ACCESS" "$ENTRY" "$TOTAL"


cd $BLECH
