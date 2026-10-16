#!/bin/bash
# Note to setup vivado lab:
#   source /home/xilinx/Vivado_Lab/2021.2/settings64.sh

echo "$(date '+%Y-%m-%d %H:%M:%S') 2021.2 $(whoami) $GIT_AUTHOR_EMAIL ${HOSTNAME} reset_PCIe_AL9 $*" >> /home/mu2ehwdev/.Xil/vivado_user_log.txt

NO_OTS_KILL=0
if [[ "x$1" == "xNOKILL" || "x$1" == "x--NOKILL" ]]; then
    echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t No-ots-kill specified."
    NO_OTS_KILL=1
    shift
fi

# Kill any reset_PCIe processes that might be running (excluding current process)
ps=`ps aux`
echo "PID = $$"
reset_pids=`echo "$ps" | grep "reset_PCIe" | grep -v "$$" | grep -v sudo | awk '{print $2}'`
if [ -n "$reset_pids" ]; then
    echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Killing existing reset_PCIe processes: $reset_pids"
    sleep 1
    kill -9 $reset_pids
    sleep 2
fi

lockfile="/tmp/mu2e.lock"
# Attempt to create the lock file atomically using ln
retriedA=0 retriedB=0
while ! ln -s "$$" "$lockfile" 2>/dev/null; do
    # Check if the existing lock file contains a valid PID
    if [ -L "$lockfile" ]; then

        pid=$(readlink "$lockfile")
        ps=`ps aux`
	    # Look for others possibly running already, ignoring the program_all and boot_from_flash scripts that make underlying program calls
        possible_parent=`echo "$ps" | grep -E ':[0-9]* [a-z/]*sh .*([p]rogram_.*AL9\.sh|[b]oot_from_flash.*AL9\.sh)'|grep -v program_all_FPGA|awk '{print$2}'`
        echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t pid=$pid possible_parent=$possible_parent"

        if [ -n "$pid"  ] && kill -0 "$pid" 2>/dev/null; then

            if [ -n "$possible_parent" -a "$possible_parent" = $pid ];then
                echo lock set by valid non-read_dtc_temps parent
                break
            fi

            test $retriedB -gt 30 && { echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Failed to acquire lock."; exit 1; }
            retriedB=$(($retriedB+1))
            echo `date`: Waiting for `echo "$ps" | grep " $pid "`
            sleep 2
            continue
        fi
    fi
    # must be stale , remove and try again
    test $retriedA -gt 0 && { echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Failed to acquire lock."; exit 1; }
    retriedA=$(($retriedA+1))
    echo 'Stale lock encountered - removing and retrying'
    rm -f "$lockfile"
done
# Ensure lock file is removed on exit
trap 'rm -f "$lockfile"' EXIT


SCRIPT_DIR="$(
 cd "$(dirname "$(readlink "$0" || printf %s "$0")")"
 pwd -P
)"
HOSTNAME="$(hostname -f)"

echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t SCRIPT_DIR: ${SCRIPT_DIR}"

RegEx='Xilinx.*704[23]'

lspci | grep "$RegEx" && foundXi=1 || foundXi=0

if [ "$foundXi" = 1 ];then
    TRIES=3
    while expr $TRIES - 1 >/dev/null;do
	echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Found DTC or CFO (Xilinx) card; removing mu2e driver as `whoami`;"


    if [[ $NO_OTS_KILL -eq 0 ]]; then
        echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t first killing any processes that may be using the device."
        retries=8
        pids=`lsof /dev/mu2e* 2>/dev/null | awk '!/^COMMAND/{print$2;}' | uniq`
        while [ -n "$pids" -a $retries -gt 0 ];do
            echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t attempt to kill $pids (which are using /dev/mu2e?) - retries=$retries"; kill -9 $pids
            sleep 3
            retries=`expr $retries - 1`
            pids=`lsof /dev/mu2e* 2>/dev/null | awk '!/^COMMAND/{print$2;}' | uniq`
        done

        killall -9 xdaq.exe
        killall -9 boardreader
        # killall -9 TRACE
    else

        pids=`lsof /dev/mu2e* 2>/dev/null | awk '!/^COMMAND/{print$2;}' | uniq`
        if [ -z "$pids" ]; then
            pid_count=0
        else
            pid_count=$(echo "$pids" | wc -l)
        fi
        echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Skipping xdaq.exe and boardreader kill as per NOKILL option,.. \n==== List of pids holding device ===\n$pids\nTotal PIDs found: $pid_count\n"
    fi

	sleep 3
	rmmod mu2e

	lsmod | grep -q mu2e || break
    done
    lsmod | grep mu2e && { echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t FAILURE - mu2e kernel module failed to unload!"; exit 1; }


    echo
    echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Removing each PCIe Xilinx device on ${HOSTNAME}..."
    echo
    cards=$(lspci | grep "$RegEx")
    test -z "$cards" && echo NO CARDS FOUND
    while read -r line
    do
        echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t $line"
        IFS=' ' read -r -a array <<< "$line"

        # for p in ${array[@]}; do
        #     echo $p
        # done

        echo "1" > /sys/bus/pci/devices/0000:${array[0]}/remove

    done <<EOF
    $(lspci | grep "$RegEx")
EOF
else
    echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t No $RegEx cards found -- no need to unload mu2e kernel module"
fi

echo
echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Rescanning for PCIe devices on ${HOSTNAME}..."
echo

sleep 1
echo "1" > /sys/bus/pci/rescan


echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Now attempt to reload mu2e module via modprobe mu2e"
modprobe mu2e

cd /root

echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Attempting to read firmware version on ${HOSTNAME}..."
source ./setup_pcie_AL9.sh
echo
echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t PCIe Device 0 firmware version on ${HOSTNAME}:"
my_cntl -d 0 read 0x9004 #device 0
echo
echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t PCIe Device 1 firmware version on ${HOSTNAME}:"
my_cntl -d 1 read 0x9004 #device 1
echo

cd - >/dev/null 2>&1
echo
echo -e "$(date +%d%b%y.%T) reset_PCIe_AL9.sh:${LINENO} |  \t Done with ${HOSTNAME} PCIe reset script!"
echo
