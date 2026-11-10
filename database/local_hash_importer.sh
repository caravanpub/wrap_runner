#!/bin/bash
LOCKMUTEX=/var/run/local_hash_importer.lck
mkdir $LOCKMUTEX 2> /dev/null

if [ $? -eq 0 ]; then
    trap 'rm -rf "$LOCKMUTEX"; exit $?' INT TERM EXIT
else
    ls -l $LOCKMUTEX | Mail -s "possible stale lock mutex dir for $0" waddell@pbspro.com
    exit 1
fi

if [ ! -d /var/db/wrappers ];then
    mkdir -p /var/db/wrappers
    chmod 700 /var/db/wrappers
    echo created /var/db/wrappers
fi

cd /var/db/wrappers

PATH=/opt/tools/bin:/usr/local/bin:$PATH curl -D headers -f -m 30 -s -S -o newest 'http://fs-01/wrapper/query_node.php?action=newest_id'

if [ $? -ne 0 ];then
    logger -t wrapper could not query newest_id
    echo could not query newest_id
    rm -rf $LOCKMUTEX
    exit 1
fi

#database seems to be working, so purge old records in case something was expired

if [ ! -z "`find /var/db/wrappers/wrapsums -mtime +3`" ];then
    echo list | /opt/tools/bin/gdbmtool -n wrapsums    
fi

#no fingerprints at all for this system
grep 'HTTP/1.1 204' headers > /dev/null
if [ $? -eq 0 ];then
    rm -rf $LOCKMUTEX
    exit 0
fi

#see if we already have the latest rule or our rules are more than a day old (to handle expiration)
N=`cat newest`
if [ ! -z "`echo list | /opt/tools/bin/gdbmtool  wrapsums | grep \"$N\"`" ];then
    if [ ! -z "`find /var/db/wrappers/wrapsums -mtime +1`" ];then
	rm -rf $LOCKMUTEX
	exit 0
    fi
fi

PATH=/opt/tools/bin:/usr/local/bin:$PATH curl -D headers -f -m 30 -s -S -o wrapsums.tmp 'http://fs-01/wrapper/query_node.php?action=export'

if [ $? -ne 0 ];then
    logger -t wrapper export of static fingerprint database failed
    rm -rf $LOCKMUTEX
    exit 1;
fi

#no fingerprints at all for this system
grep '^HTTP/1.1 204' headers > /dev/null
if [ $? -eq 0 ];then
    echo list | /opt/tools/bin/gdbmtool -n wrapsums
else
    /opt/tools/bin/gdbmtool -n -f wrapsums.tmp wrapsums
    if [ $? -ne 0 ];then
	logger -t wrapper import of static fingerprint database failed
	rm -rf $LOCKMUTEX
	exit 1
    fi
fi

chmod 600 wrapsums
rm -rf $LOCKMUTEX
exit 0
