#!/bin/bash

# Under GNU GPL
# 2010 niekt0@hysteria.sk

# modify this script to set exact 
# conditions for pressing red_button

MPATH="."	# path to alzheimer
ALARM_TIME=5	# in "loops"
# how long must be server disconnected, before red button is pushed
CHECK_DELAY=6	# check every..
SERVERS=4	# number of servers
TO_IGNORE=0	# how many servers to ignore even if alive
let TRASHOLD=SERVERS-TO_IGNORE-1;	# how many server can die


# verify_ssl(server)
# certificate should be already in servers/
# to get server certificate, you can use for example:
# echo "" |openssl s_client -connect server:443 2>/dev/null | sed -n '/-----BEGIN CERTIFICATE-----/,/-----END CERTIFICATE-----/p' > servers/server
verify_ssl() {
	local tmp=0;
	
#	echo "verify_ssl $1, $tmp, $fail" >&2;
        echo "" |openssl s_client -connect "$1":443 2>/dev/null >/tmp/hook_$$;
        cat /tmp/hook_$$ | sed -n '/-----BEGIN CERTIFICATE-----/,/-----END CERTIFICATE-----/p' > /tmp/hook_$$_cert;
        cat /tmp/hook_$$ | grep '^SSL handshake has read ' >/dev/null && tmp=1;
        cmp /tmp/hook_$$_cert servers/"$1" >/dev/null || tmp=0;
#	echo "verify_ssl2 $1, $tmp, $fail" >&2;

        if [ "$tmp" == 0 ]; then
                let fail=fail+1;
        fi
	rm -f /tmp/hook_"$$"*

}


# this function performs check itself
check_link() {

# I recommend checking several "friendly" servers,
# located at different places, with big fault tolerance
# Use ssh/ssl for checking (when using SSL, don't trust CA !)

# XXX modify me!
# put your servers here

	local fail=0;

	verify_ssl "hysteria.sk";
	verify_ssl "lol";
	verify_ssl "rofl";
	verify_ssl "xixi";

	echo "$fail"	# result

}

red_button () {

	sync;
	sync;

	#umount -a #?

	echo "RED BUTTON!!!"

	#insmod -f "$MPATH"/alzheimer.ko

	# XXX remove comments this when everything works XXX !!!

}


#main loop

alarm_round=0;

for((;;)); do
	failed=$(check_link);

	if [ "$failed" -gt 0 ]; then
		echo "$failed server/s failed!";
		# XXX send mail? 
	fi
		
	if [ "$failed" -gt "$TRASHOLD" ]; then

		logger -s "Alzheimer: Signal lost!";

		let alarm_round=alarm_round+1;
		if [ "$alarm_round" -gt "$ALARM_TIME" ]; then
			logger -s "Pushing red button";
			red_button;
		fi
	else
		alarm_round=0;
	fi

	sleep "$CHECK_DELAY";

done

