#!/bin/bash

function poll() {
	for genie in ${GENIES//:/ }; do
		echo polling ${genie//,/ }
		netcat -w 1 -U ${genie##*,}
		if [ $? -eq 1 ]; then
			echo banishing ${genie//,/ }
			GENIES=$(sed "s|\<$genie\>||;s/^://;s/:$//;s/::/:/" <<<"$GENIES")
		fi
	done
}

function genie() {
	{
		read name
		read socket

		echo $socket

		if [ "$GENIES" ]; then
			export GENIES=$GENIES:$name,$socket
		else
			export GENIES=$name,$socket
		fi
	} < <($@)
}
