#!/bin/sh
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.

# Description:
#       This script sets up DHCP server, DHCP network, DHCP macro and
#	assign DHCP macro to set of IP addresses.
#	If the DHCP server is remote or the user doesn't have permission
#	top modify DHCP server, then the user is advised to insert the
#	DHCP macro manually.
#	Currently the support for handling remote DHCP server is not implemented
#
# Files potentially changed on server:
# /etc/inet/dhcpsvc.conf - SMF service information for DHCP
# /var/dhcp - DHCP information is kept in files under /var/ai

. /usr/lib/installadm/installadm-common

DHTADM="/usr/sbin/dhtadm -g"
PNTADM=/usr/sbin/pntadm
DHCPCONFIG=/usr/sbin/dhcpconfig
TMP_DHCP=/tmp/installadm.dhtadm-P.$$
BOOTSRVA="BootSrvA"
BOOTFILE="BootFile"
ROOTPATH="Rootpath"
INCLUDE="Include"
GRUBMENU="GrubMenu"

#
# find_network
#
# Purpose : Given an IP address, figure out which network on this
#	    server it belongs to.
#
# Parameters :
#	$1 - IP address
#
# Returns :
#	Network for IP address passed in.
#
find_network()
{
	ipaddr=$1

	if [ -z "$ipaddr" ] ; then
		return
	fi

	# Iterate through the interfaces to figure what the possible
	# networks are (in case this is a multi-homed server).
	# For each network, use its netmask with the given IP address 
	# to see if resulting network matches.
	ifconfig -a | grep broadcast | awk '{print $2, $4}' | \
		while read t_ipaddr t_netmask ; do

			# get network of this interface
			if_network=`get_network $t_ipaddr $t_netmask`
			if [ -z $if_network ]; then
				continue
			fi

			# get network for passed in ipaddr based
			# on this interfaces's netmask
			ip_network=`get_network $ipaddr $t_netmask`
			if [ -z $ip_network ]; then
				continue
			fi

			# if networks match, this is the network that
			# the passed in ipaddr belongs to.
			if [ "$if_network" = "$ip_network" ] ; then
				echo "$if_network"
				break
			fi
		done
}

#
# DHCP options should be built and given to dhtadm in a single command
# This function adds the new "name:value" pair to the existing macro
# value passed in as the third argument or constructs a new macro value 
# if there are only two arguments.
#
update_macro_value()
{
	key=$1
	val=$2
	macro_value=""
	if [ $# -eq 3 ]; then
		macro_value=$3
	fi

	if [ X"${macro_value}" = X ]; then
		new_macro_value=":${key}=${val}:"
	else
		# Already there is a : at the end of previous key-value pair
		new_macro_value="${macro_value}${key}=${val}:"
	fi
	echo ${new_macro_value}
}

#
# Create a macro with the value
#
add_macro()
{
	name=$1
	value=$2

	$DHTADM -P > ${TMP_DHCP} 2>&1
	ENTRY=`cat ${TMP_DHCP} | awk '$1 == name && $2 ~ /Macro/ { print $3 }' name="$name"`
	if [ -z "${ENTRY}" ]; then
		$DHTADM -A -m $name -d ${value}
	elif [ "${ENTRY}" != "${value}" ]; then
		$DHTADM -M -m $name -d ${value}
	fi
}

#
# Print instructions for the user how to manually setup the 
# dhcp macro.
#
print_dhcp_macro_info()
{
	caller=$1
	macro=$2
	svr_ipaddr=$3
	bootfile=$4
	rootpath=$5
	macvalue=$6
	sparc=$7

	menu_lst_file="menu.lst.${bootfile}"
	
	echo "If not already configured, please create a DHCP macro"
	echo "named ${macro} with:"
	echo "   Boot server IP (BootSrvA) : ${svr_ipaddr}"
	echo "   Boot file      (BootFile) : ${bootfile}"
	if [ "$sparc" ]; then
		echo "   Root path      (Rootpath) : ${rootpath}"
	elif [ "${caller}" != "client" ]; then
		echo "   GRUB Menu      (GrubMenu) : ${menu_lst_file}"
	fi

	echo "If you are running Sun's DHCP server, use the following"
	echo "command to add the DHCP macro, ${macro}:"
	echo "   $DHTADM -A -m ${macro} -d ${macvalue}"
	if [ ! "$sparc" -a "${caller}" != "client" ]; then
		echo ""
		echo "Additionally, if the site specific symbol GrubMenu"
		echo "is not present, please add it as follows:"
		echo "   $DHTADM -A -s GrubMenu -d Site,150,ASCII,1,0"
	fi
	echo ""
	echo "Note: Be sure to assign client IP address(es) if needed"
	echo "(e.g., if running Sun's DHCP server, run pntadm(1M))."

}

#
# Set up the dhcp macro
#    Both sparc/x86 use boot server address and boot file, but contents of
#    boot file differ between sparc/x86. In addition, sparc uses the dhcp
#    macro symbol, Rootpath,  and x86 uses dhcp macro symbol, GrubMenu,
#    which are defined by ROOTPATH and GRUBMENU, respectively.
# 
setup_dhcp_macro()
{
	caller=$1
	name=$2
	svr_ipaddr=$3
	bootfile=$4
	rootpath=$5
	sparc=$6

	server_name=`uname -n`

	bootfilesave="${bootfile}"
	rootpathsave="${rootpath}"

	$DHTADM -P > ${TMP_DHCP} 2>&1
	dhtstatus=$?
	if [ "$sparc" ]; then
		if [ $dhtstatus -eq 0 ]; then
			# For sparc, bootfile and rootpath are urls,
			# and contain an embedded colon. Enclose bootfile
			# and rootpath in quotes so that the colon
			# doesn't terminate the macro string.
			#
			bootfile="\"${bootfile}\""
			rootpath="\"${rootpath}\""
		else
			# If the macro is going to be printed for the user
			# to enter on the command line, use escaped version
			# of quoted string (backslash gets the quote past
			# the shell).
			#
			bootfile="\\\"${bootfile}\\\""
			rootpath="\\\"${rootpath}\\\""
		fi
	else
		# set menu.lst for x86
		menu_lst_file="menu.lst.${bootfile}"
	fi

	# Construct the value of the macro that will either be passed to
	# add_macro or will be printed out for the user
	#
	mvalue=""
	if [ $dhtstatus -eq 0 ]; then
		mvalue=`update_macro_value ${INCLUDE} ${server_name}`
	fi
	mvalue=`update_macro_value ${BOOTSRVA} ${svr_ipaddr} ${mvalue}`
	mvalue=`update_macro_value ${BOOTFILE} ${bootfile} ${mvalue}`
	if [ "$sparc" ]; then
		mvalue=`update_macro_value ${ROOTPATH} ${rootpath} ${mvalue}`
	else
		mvalue=`update_macro_value ${GRUBMENU} ${menu_lst_file} \
			${mvalue}`
	fi

	if [ $dhtstatus -ne 0 ]; then
		# Tell user how to setup dhcp macro
		#
		echo "\nDetected that DHCP is not set up on this server."
		print_dhcp_macro_info ${caller} ${name} ${svr_ipaddr}\
			${bootfilesave} ${rootpathsave} ${mvalue} ${sparc}
		return 1
	else
		add_macro $name $mvalue
	fi
	return 0
}

#
# create_dhcp_server
# Purpose:
# 	Create the DHCP server if it doesn't exist
# 	Add the network corresponding the ip addresses to be added
#
# Parameters:
#	$1 - starting address of dhcp client ip address to
#	     set up on the dhcp server.
#
# Return:
#	0 - Success
#	1 - Failure
#
create_dhcp_server()
{
	ip_start=$1

	# Figure out which network the given starting dhcp client
	# ip address belongs to.   
	#
	net=`find_network $ip_start`

	if [ -z "$net" ] ; then
		echo "Failed to find network for $ip_start"
		return 1
	fi

	# Create the DHCP table if the DHCP is not enabled
	$DHTADM -P > /dev/null 2>&1
	if [ $? -ne 0 ]; then
		mkdir -p /var/dhcp >/dev/null 2>&1
		echo "Creating DHCP Server"

		$DHCPCONFIG -D -r SUNWfiles -p /var/dhcp
		if [ $? -ne 0 ]; then
			echo "Failed to setup DHCP server"
			return 1
		fi
	fi

	# At this point, either a DHCP server previously existed, or
	# one has been successfully created.

	# Add the site specific option 150 to specify a
	# menu.lst other than default one based on
	# MAC adddress
	$DHTADM -A -s GrubMenu -d Site,150,ASCII,1,0

	#
	# Get the router from netstat
	#
	router=`netstat -rn | awk '/default/ { print $2 }'`

	# If the router found is for the network being configured,
	# configure the network in DHCP with that router.  Otherwise
	# don't use it.
	use_router=0
	if [ X${router} != X ]; then
		router_network=`find_network $router`

		if [ -n $router_network -a "$router_network" = "$net" ]; then
			use_router=1
		fi
	fi

	if [ $use_router -eq 1 ]; then
		$DHCPCONFIG -N ${net} -t ${router}
	else
		echo "Did not find router for network ${net}.  Please configure"
		echo "router for network ${net} in the dhcp server manually."
		$DHCPCONFIG -N ${net}
	fi

	# If the network already exists, ignore the error
	ret=$?
	if [ $ret -eq 255 ]; then
		return 0
	elif [ $ret -ne 0 ]; then
		echo "Failed to add network (${net}) to dhcp server"
		return 1
	fi

	return 0
}

#
# Add the given ip addresses to the DHCP table
#
add_ip_addresses()
{
	ip_start=$1
	ip_count=$2

	n1=`echo $ip_start | cut -d'.' -f1-3`
	last_octet=`echo $ip_start | cut -d'.' -f4`

	# Figure out which network the given starting dhcp client
	# ip address belong to.
	#
	net=`find_network $ip_start`
	if [ -z "$net" ] ; then
		echo "Failed to find network for $ip_start"
		return 1
	fi

	index=0
	while [ $index -lt $ip_count ]; do
		next_addr_octet=`expr $last_octet + $index`
		ip=`echo $n1.$next_addr_octet`
		addr=`$PNTADM -P ${net} | /usr/bin/nawk '{ print $3 }' 2> /dev/null | /usr/bin/grep "^${ip}\$"`
		if [ X"${addr}" != X ]; then
			if [ "$ip" != "$addr" ]; then
				$PNTADM -A ${ip} ${net}
			fi
		fi
		index=`expr $index + 1`
	done

	return 0
}

#
# Assign the dhcp macro to the IP addresses we have added to the DHCP table
#
assign_dhcp_macro()
{
	macro_name=$1
	ip_start=$2
	ip_count=$3

	n1=`echo $ip_start | cut -d'.' -f1-3`
	last_octet=`echo $ip_start | cut -d'.' -f4`
 
	# Figure out which network the given starting dhcp client
	# ip address belong to.
	#
	net=`find_network $ip_start`
	if [ -z "$net" ] ; then
		echo "Failed to find network for $ip_start"
		return 1
	fi

	index=0
	while [ $index -lt $ip_count ]; do
		next_addr_octet=`expr $last_octet + $index`
		ip=`echo $n1.$next_addr_octet`
		addr=`$PNTADM -P ${net} | /usr/bin/nawk '{ print $3 }' 2> /dev/null | /usr/bin/grep "^${ip}\$"`
		if [ X"${addr}" != X ]; then
			if [ "$ip" = "$addr" ]; then
				$PNTADM -D ${ip} ${net}
			fi
		fi
		$PNTADM -A ${ip} -m ${macro_name} ${net}
		index=`expr $index + 1`
	done

	return 0
}
		
#
# This is an internal function
# So we expect only limited use

if [ $# -lt 3 ]; then
	echo "Internal function to manage DHCP services doesn't have enough data"
	exit 1
fi

if [ "$1" = "server" ]; then
	client_ip_start=$2
	ip_count=$3

	create_dhcp_server $client_ip_start
	status=$?
	if [ $status -eq 0 ]; then
		add_ip_addresses $client_ip_start $ip_count	
		status=$?
	fi
elif [ "$1" = "macro" ]; then
	imgtype=$2	# x86 or sparc
	srv_ip=$3
	macro=$4
	boot_file=$5
	rpath=$6

	sparc=
	if [ "${imgtype}" = "sparc" ]; then
		sparc="true"
	fi
	setup_dhcp_macro $1 $macro $srv_ip $boot_file $rpath $sparc
	status=$?
elif [ "$1" = "assign" ]; then
	client_ip_start=$2
	ip_count=$3
	macro=$4

	assign_dhcp_macro $macro $client_ip_start $ip_count	
	status=$?
elif [ "$1" = "client" ]; then
	imgtype=$2	# x86 or sparc
	srv_ip=$3
	macro=$4
	boot_file=$5
	rpath=$6
	client_ip=$7
	
	sparc=
	if [ "${imgtype}" = "sparc" ]; then
		sparc="true"
	fi
	setup_dhcp_macro $1 $macro $srv_ip $boot_file $rpath $sparc
	status=$?
	if [ $status -eq 0 -a "X${client_ip}" != "X" ]; then
		assign_dhcp_macro $macro $client_ip 1	
		status=$?
	fi
else 
	echo " $1 - unsupported DHCP service action"
	exit 1
fi

/bin/rm -f $TMP_DHCP

if [ $status -eq 0 ]; then
	exit 0
else
	exit 1
fi
