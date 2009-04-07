/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <sys/param.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include "installadm.h"

typedef int cmdfunc_t(int, char **, scfutilhandle_t *, const char *);

static cmdfunc_t do_create_service, do_delete_service;
static cmdfunc_t do_list, do_enable, do_disable;
static cmdfunc_t do_create_client, do_delete_client;
static cmdfunc_t do_add, do_remove, do_set, do_help;
static void do_opterr(int, int, const char *);
static char *progname;
static void smf_service_enable_attempt(char *);
static boolean_t check_for_enabled_install_services(scfutilhandle_t *);

char	instance[sizeof (INSTALL_SERVER_FMRI_BASE) +
	    sizeof (INSTALL_SERVER_DEF_INST) + 1];

typedef struct cmd {
	char		*c_name;
	cmdfunc_t	*c_fn;
	const char	*c_usage;
	boolean_t	c_priv_reqd;
} cmd_t;

static cmd_t	cmds[] = {
	{ "create-service",		do_create_service,
	    "\tcreate-service\t[-d] [-u] [-f <bootfile>] [-D <DHCPserver>] \n"
	    "\t\t\t[-n <svcname>] [-i <dhcp_ip_start>] \n"
	    "\t\t\t[-c <count_of_ipaddr>] [-s <srcimage>] <targetdir>",
	    PRIV_REQD							},

	{ "delete-service",	do_delete_service,
	    "\tdelete-service\t[-x] <svcname>",
	    PRIV_REQD							},

	{ "list",	do_list,
	    "\tlist\t[-n <svcname>]",
	    PRIV_NOT_REQD						},

	{ "enable",	do_enable,
	    "\tenable\t<svcname>",
	    PRIV_REQD							},

	{ "disable",	do_disable,
	    "\tdisable\t[-t] <svcname>",
	    PRIV_REQD							},

	{ "create-client",	do_create_client,
	    "\tcreate-client\t[-b <property>=<value>,...] \n"
	    "\t\t\t-e <macaddr> -t <imagepath> -n <svcname>",
	    PRIV_REQD							},

	{ "delete-client",	do_delete_client,
	    "\tdelete-client\t<macaddr>",
	    PRIV_REQD							},

	{ "add",	do_add,
	    "\tadd\t-m <manifest> -n <svcname>",
	    PRIV_REQD							},

	{ "remove",	do_remove,
	    "\tremove\t-m <manifest> -n <svcname>",
	    PRIV_REQD							},

	{ "set",	do_set,
	    "\tset\t-p <name>=<value> -n <svcname>",
	    PRIV_REQD							},

	{ "help",	do_help,
	    "\thelp\t[<subcommand>]",
	    PRIV_NOT_REQD						}
};

static void
usage(void)
{
	int	i;
	cmd_t	*cmdp;

	(void) fprintf(stderr, MSG_INSTALLADM_USAGE);
	for (i = 0; i < sizeof (cmds) / sizeof (cmds[0]); i++) {
		cmdp = &cmds[i];
		if (cmdp->c_usage != NULL)
			(void) fprintf(stderr, "%s\n", gettext(cmdp->c_usage));
	}
	exit(INSTALLADM_FAILURE);
}


int
main(int argc, char *argv[])
{
	int		i;
	cmd_t		*cmdp;
	scfutilhandle_t	*handle;

	(void) setlocale(LC_ALL, "");

	/*
	 * Must have at least one additional argument to installadm
	 */
	if (argc < 2) {
		usage();
	}

	progname = argv[0];
	(void) snprintf(instance, sizeof (instance), "%s:%s",
	    INSTALL_SERVER_FMRI_BASE, INSTALL_SERVER_DEF_INST);
	/*
	 * If it is valid subcommand, call the do_subcommand function
	 * found in cmds. Pass it the subcommand's argc and argv, as
	 * well as the smf handle and the subcommand specific usage.
	 */
	for (i = 0; i < sizeof (cmds) / sizeof (cmds[0]); i++) {
		int ret = 0;
		cmdp = &cmds[i];
		if (strcmp(argv[1], cmdp->c_name) == 0) {
			if ((cmdp->c_priv_reqd) && (geteuid() > 0)) {
				(void) fprintf(stderr, MSG_ROOT_PRIVS_REQD,
				    argv[0], cmdp->c_name);
				exit(INSTALLADM_FAILURE);
			}

			handle = ai_scf_init();
			if (handle == NULL) {
				(void) fprintf(stderr, MSG_AI_SMF_INIT_FAIL);
				exit(INSTALLADM_FAILURE);
			}

			/*
			 * set the umask, for all subcommands to inherit
			 */
			(void) umask(022);

			if (cmdp->c_fn(argc - 1, &argv[1], handle,
			    cmdp->c_usage)) {
				ret = INSTALLADM_FAILURE;
			} else {
				ret = INSTALLADM_SUCCESS;
			}
			/*
			 * Make an attempt to enable the smf service.
			 */
			if (!check_for_enabled_install_services(handle)) {
				smf_service_enable_attempt(instance);
			}
			ai_scf_fini(handle);
			exit(ret);
		}
	}

	/*
	 * Otherwise, give error and print usage
	 */
	(void) fprintf(stderr, MSG_UNKNOWN_SUBCOMMAND,
	    progname, argv[1]);
	usage();

	exit(INSTALLADM_FAILURE);
}

/*
 * get_ip_from_hostname:
 *
 * Description:
 *   Resolves given hostname to IPv4 address. Result is stored as string
 *   into given buffer. If more than one IP address is returned, the first
 *   one is picked.
 *
 * parameters:
 *   name        - simple or fully qualified hostname to be resolved
 *   ip_string   - pointer to string buffer where IP address will
 *                 be stored
 *   buffer_size - size of ip_string
 *
 * return:
 *   0  - success
 *   -1 - resolve process failed - string buffer is left untouched
 */
static int
get_ip_from_hostname(char *name, char *ip_string, int buffer_size)
{
	struct hostent	*hp;
	struct in_addr	in;

	hp = gethostbyname(name);
	if (hp == NULL) {
		return (-1);
	} else {
		(void) memcpy(&in.s_addr, hp->h_addr_list[0],
		    sizeof (in.s_addr));

		(void) snprintf(ip_string, buffer_size, "%s", inet_ntoa(in));
	}

	return (0);
}

static int
call_script(char *scriptname, int argc, char *argv[])
{
	int	i;
	char	cmd[BUFSIZ];
	char	cmdargs[BUFSIZ];


	cmdargs[0] = '\0';
	for (i = 0; i < argc; i++) {
		(void) strcat(cmdargs, argv[i]);
		(void) strcat(cmdargs, " ");
	}

	(void) snprintf(cmd, sizeof (cmd), "%s %s",
	    scriptname, cmdargs);

	return (installadm_system(cmd));

}

/*
 * Function:    check_for_enabled_install_services
 * Description:
 *              Check to see if any of the install services are
 *              enabled. If not, the smf install/server service
 *              should be placed in maintenance.
 * Parameters:
 *              handle - scfutilhandle_t * for use with scf calls
 * Return:
 *		B_FALSE - Service not placed in maintenance.
 *		B_TRUE - No enabled services found. SMF service in maint.
 * Scope:
 *              Private
 */
static boolean_t
check_for_enabled_install_services(scfutilhandle_t *handle)
{
	char		*value;
	ai_pg_list_t	*pg_list = NULL;
	ai_pg_list_t	*pg = NULL;
	char		*state;

	/*
	 * Are there any install services still with status "on"?
	 */

	/* get the list of property groups */
	if (ai_get_pgs(handle, &pg_list) != AI_SUCCESS) {
		return (B_FALSE);
	}
	if ((pg = pg_list) == NULL) {
		/*
		 * No property groups for install services. Put smf
		 * instance into maintenance.
		 */
		goto out;
	}
	while (pg != NULL && pg->pg_name != NULL) {
		/*
		 * for each property group read the status
		 */
		if (ai_read_property(handle, pg->pg_name, SERVICE_STATUS,
		    &value) == AI_SUCCESS) {
			if (strcmp(value, STATUS_ON) == 0) {
				/*
				 * At least one is enabled. Return
				 * without putting in maint.
				 */
				free(value);
				ai_free_pg_list(pg_list);
				return (B_FALSE);
			}
		}
		free(value);
		pg = pg->next;
	}
	/*
	 * No enabled install services. Put smf
	 * instance into maintenance.
	 */
	ai_free_pg_list(pg_list);
out:
	state = smf_get_state(instance);
	if (strcmp(state, SCF_STATE_STRING_MAINT) == 0) {
		/*
		 * If the service is already in maintenance don't try
		 * to put it there and don't send the message saying
		 * you're doing so.
		 */
		(void) fprintf(stderr, MSG_SERVER_SMF_DISABLED, instance);
		return (B_FALSE);
	}
	(void) fprintf(stderr, MSG_SERVER_SMF_OFFLINE, instance);

	smf_maintain_instance(instance, SMF_IMMEDIATE);
	/*
	 * Wait for it to really go into maintenance state.
	 */
	do {
		state = smf_get_state(instance);
	} while (strcmp(state, SCF_STATE_STRING_MAINT) != 0);

	(void) fprintf(stderr, MSG_SERVER_SMF_DISABLED, instance);

	return (B_TRUE);
}

/*
 * smf_service_enable_attempt
 * Description:
 *		Attempt to enable the designated smf service.
 *		If the service goes into maintenance mode,
 *		return an error to the caller.
 * Parameters:
 *		instance - The instance to attempt to enable
 * Return:
 *		None
 * Scope:
 *		Private
 */
static void
smf_service_enable_attempt(char *instance)
{
	char		*orig_state = NULL;
	int		enable_tried = 0;

	/*
	 * Check the service status here.
	 * Algorithm:
	 *	If the service is online, everything is OK. return.
	 *	If the service is offline, SMF is settling. Return
	 *	    or we get caught in recursion.
	 * 	If the service is disabled, try to enable it.
	 *	If the service is in maintenance, try to clear it.
	 */
	orig_state = smf_get_state(instance);
	if (orig_state == NULL) {
		(void) smf_enable_instance(instance, 0);
	} else if (strcmp(orig_state, SCF_STATE_STRING_ONLINE) == 0) {
		/*
		 * Instance is online and running.
		 */
		free(orig_state);
		return;
	} else if (strcmp(orig_state, SCF_STATE_STRING_OFFLINE) == 0) {
		free(orig_state);
		return;
	} else if (strcmp(orig_state, SCF_STATE_STRING_DISABLED) == 0) {
		/*
		 * Instance is disabled try to enable it.
		 */
		(void) smf_enable_instance(instance, 0);
	} else if (strcmp(orig_state, SCF_STATE_STRING_MAINT) == 0) {
		(void) smf_restore_instance(instance);
	}
	free(orig_state);

}

/*
 * do_create_service:
 * This function parses the command line arguments and sets up
 * the image, the DNS service, the network configuration for the
 * the clients to boot from this image (/tftpboot) and dhcp if desired.
 * This function calls shell scripts to handle each of the tasks
 */
static int
do_create_service(
	int argc,
	char *argv[],
	scfutilhandle_t *handle,
	const char *use)
{
	int		opt;
	boolean_t	make_service_default = B_FALSE;
	boolean_t	publish_as_unicast = B_FALSE;
	boolean_t	named_service = B_FALSE;
	boolean_t	named_boot_file = B_FALSE;
	boolean_t	dhcp_setup_needed = B_FALSE;
	boolean_t	create_netimage = B_FALSE;
	boolean_t	use_remote_dhcp_server = B_FALSE;
	boolean_t	create_service = B_FALSE;
	boolean_t	have_sparc = B_FALSE;

	char		*boot_file = NULL;
	char		*ip_start = NULL;
	short		ip_count;
	char		*service_name = NULL;
	char		*source_path = NULL;
	char		*dhcp_server = NULL;
	char		*target_directory = NULL;

	struct stat	stat_buf;
	struct stat 	sb;
	char		cmd[MAXPATHLEN];
	char		mpath[MAXPATHLEN];
	char		bfile[MAXPATHLEN];
	char		server_hostname[DATALEN];
	char		server_ip[DATALEN];
	char		srv_name[MAXPATHLEN];
	char		srv_address[DATALEN] = "unknown";
	char		txt_record[DATALEN];
	char		dhcp_macro[MAXNAMELEN+12]; /* dhcp_macro_<filename> */
	int		size;
	service_data_t	data;
	char		*pg_name;

	while ((opt = getopt(argc, argv, "du:f:n:i:c:s:D")) != -1) {
		switch (opt) {
		/*
		 * Make this service as default
		 * It is not yet supported
		 */
		case 'd':
			make_service_default = B_TRUE;
			break;
		/*
		 * Publish this service as unicast DNS
		 * It is not yet supported
		 */
		case 'u':
			publish_as_unicast = B_TRUE;
			break;
		/*
		 * Create a boot file for this service with the supplied name
		 */
		case 'f':
			named_boot_file = B_TRUE;
			boot_file = optarg;
			break;
		/*
		 * The name of the service is supplied.
		 */
		case 'n':
			if (!validate_service_name(optarg)) {
				(void) fprintf(stderr, MSG_BAD_SERVICE_NAME);
				return (INSTALLADM_FAILURE);
			}
			named_service = B_TRUE;
			service_name = optarg;
			break;
		/*
		 * The starting IP address is supplied.
		 */
		case 'i':
			dhcp_setup_needed = B_TRUE;
			ip_start = optarg;
			break;
		/*
		 * Number of IP addresses to be setup
		 */
		case 'c':
			ip_count = atoi(optarg);
			break;
		/*
		 * Source image is supplied.
		 */
		case 's':
			create_netimage = B_TRUE;
			source_path = optarg;
			break;
		/*
		 * DHCP server is remote
		 */
		case 'D':
			use_remote_dhcp_server = B_TRUE;
			dhcp_server = optarg;
			break;
		default:
			(void) fprintf(stderr, "%s\n", gettext(use));
			return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * The last argument is the target directory.
	 * Strip extra / off the end as they confuse the ln command.
	 */
	target_directory = strip_ending_slashes(argv[optind++]);

	if (target_directory == NULL) {
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	/*
	 * obtain server hostname and resolve it to IP address
	 * If this operation fails, something is wrong with network
	 * configuration - exit
	 */
	if (gethostname(server_hostname, sizeof (server_hostname)) != 0) {
		(void) fprintf(stderr, MSG_GET_HOSTNAME_FAIL);
		return (INSTALLADM_FAILURE);
	}

	/* resolve host name to IP address */
	if (get_ip_from_hostname(server_hostname, server_ip,
	    sizeof (server_ip)) != 0) {
		(void) fprintf(stderr, MSG_GET_HOSTNAME_FAIL);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * if server hostname resolved as loopback address (127.0.0.1),
	 * service can't be correctly created. Print failure message
	 * and exit.
	 */
	if (strcmp(server_ip, LOCALHOST) == 0) {
		(void) fprintf(stderr, MSG_SERVER_RESOLVED_AS_LOOPBACK,
		    server_hostname);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * We don't support DHCP on remote system yet.
	 * So disable DHCP setup
	 */
	if (use_remote_dhcp_server) {
		(void) fprintf(stderr, MSG_REMOTE_DHCP_SETUP);
		dhcp_setup_needed = B_FALSE;
	}
	/*
	 * Check whether target exists
	 * If it doesn't exist, the setup-image script will
	 * create the directory.
	 * If it exists, check whether it has a valid net image
	 */
	if (access(target_directory, F_OK) == 0) {
		if (stat(target_directory, &stat_buf) == 0) {
			char	path[MAXPATHLEN];
			/*
			 * If the directory is empty, then it is okay
			 */
			if (stat_buf.st_nlink > 2) {
				/*
				 * Check whether it has valid file solaris.zlib
				 */
				(void) snprintf(path, sizeof (path), "%s/%s",
				    target_directory,
				    AI_NETIMAGE_REQUIRED_FILE);
				if (access(path, R_OK) != 0) {
					(void) fprintf(stderr,
					    MSG_TARGET_NOT_EMPTY);
					return (INSTALLADM_FAILURE);
				}
				/*
				 * Already have an image. We can't create a
				 * new one w/o removing the old one.
				 * Display error
				 */
				if (create_netimage) {
					(void) fprintf(stderr,
					    MSG_VALID_IMAGE_ERR,
					    target_directory);
					return (INSTALLADM_FAILURE);
				}
			}
		} else {
			(void) fprintf(stderr,
			    MSG_DIRECTORY_ACCESS_ERR,
			    target_directory, errno);
			return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * call the script to create the netimage
	 */
	if (create_netimage) {
		(void) snprintf(cmd, sizeof (cmd), "%s %s %s %s",
		    SETUP_IMAGE_SCRIPT, IMAGE_CREATE,
		    source_path, target_directory);
		if (installadm_system(cmd) != 0) {
			(void) fprintf(stderr, MSG_CREATE_IMAGE_ERR);
			return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * Check whether image is sparc or x86 by checking existence
	 * of key directories
	 */
	(void) snprintf(mpath, sizeof (mpath), "%s/%s", target_directory,
	    "platform/sun4v");
	if ((stat(mpath, &sb) == 0) && S_ISDIR(sb.st_mode)) {
		have_sparc = B_TRUE;
	} else {
		(void) snprintf(mpath, sizeof (mpath), "%s/%s",
		    target_directory, "platform/i86pc");
		if (stat(mpath, &sb) || !S_ISDIR(sb.st_mode)) {
			(void) fprintf(stderr, MSG_UNABLE_TO_DETERMINE_ARCH);
			return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * The net-image is created, now start the service
	 * If the user provided the name of the service, use it
	 */
	txt_record[0] = '\0';
	srv_name[0] = '\0';
	if (named_service) {
		int ret;

		snprintf(cmd, sizeof (cmd), "%s %s %s %s %s",
		    SETUP_SERVICE_SCRIPT, SERVICE_LOOKUP,
		    service_name, INSTALL_TYPE, LOCAL_DOMAIN);
		ret = installadm_system(cmd);
		if (ret != 0) {
			create_service = B_TRUE;
		} else {
			/*
			 * Service already exists. Get the current data,
			 * but we only care about info in txt_record.
			 */
			if (get_service_data(handle, service_name, &data) !=
			    B_TRUE) {
				(void) fprintf(stderr, MSG_SERVICE_DOESNT_EXIST,
				    service_name);
				return (INSTALLADM_FAILURE);
			}
			strlcpy(txt_record, data.txt_record,
			    sizeof (txt_record));
		}
		strlcpy(srv_name, service_name, sizeof (srv_name));
	} else {
		/*
		 * The service is not given as input. We will generate
		 * a service name and start the service.
		 */
		create_service = B_TRUE;
	}

	if (create_service) {
		uint16_t	wsport;

		wsport = get_a_free_tcp_port(handle, START_WEB_SERVER_PORT);
		if (wsport == 0) {
			(void) fprintf(stderr, MSG_CANNOT_FIND_PORT);
			return (INSTALLADM_FAILURE);
		}
		snprintf(txt_record, sizeof (txt_record), "%s=%s:%u",
		    AIWEBSERVER, server_hostname, wsport);
		if (!named_service) {
			snprintf(srv_name, sizeof (srv_name),
			    "_install_service_%u", wsport);
		}

		snprintf(cmd, sizeof (cmd), "%s %s %s %s %s %u %s",
		    SETUP_SERVICE_SCRIPT, SERVICE_REGISTER,
		    srv_name, INSTALL_TYPE,
		    LOCAL_DOMAIN, wsport, txt_record);
		if (installadm_system(cmd) != 0) {
			(void) fprintf(stderr,
			    MSG_REGISTER_SERVICE_FAIL, srv_name);
			return (INSTALLADM_FAILURE);
		}

		/*
		 * save location of service in format <server_ip_address>:<port>
		 * It will be used later for setting service discovery fallback
		 * mechanism
		 */

		snprintf(srv_address, sizeof (srv_address), "%s:%u",
		    server_ip, wsport);
	}

	bfile[0] = '\0';
	if (named_boot_file) {
		strlcpy(bfile, boot_file, sizeof (bfile));
	} else {
		strlcpy(bfile, srv_name, sizeof (bfile));
	}

	/*
	 * Register the information about the service, image and boot file
	 * so that it can be used later
	 */
	pg_name = ai_make_pg_name(srv_name);
	if (pg_name == NULL) {
		(void) fprintf(stderr, MSG_GET_PG_NAME_FAILED, srv_name);
		return (INSTALLADM_FAILURE);
	}
	if (ai_create_pg(handle, pg_name) != AI_SUCCESS) {
		free(pg_name);
		(void) fprintf(stderr, MSG_CREATE_INSTALL_SERVICE_FAILED,
		    srv_name);
		return (INSTALLADM_FAILURE);
	}
	free(pg_name);

	strlcpy(data.svc_name, srv_name, DATALEN);
	strlcpy(data.image_path, target_directory, MAXPATHLEN);
	strlcpy(data.boot_file, bfile, MAXNAMELEN);
	strlcpy(data.txt_record, txt_record, MAX_TXT_RECORD_LEN);
	strlcpy(data.status, STATUS_ON, STATUSLEN);

	if (save_service_data(handle, data) != B_TRUE) {
		(void) fprintf(stderr, MSG_SAVE_SERVICE_PROPS_FAIL,
		    data.svc_name);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * Setup dhcp
	 */
	if (dhcp_setup_needed && create_netimage) {
		snprintf(cmd, sizeof (cmd), "%s %s %s %d",
		    SETUP_DHCP_SCRIPT, DHCP_SERVER, ip_start, ip_count);
		if (installadm_system(cmd) != 0) {
			(void) fprintf(stderr,
			    MSG_CREATE_DHCP_SERVER_ERR);
			return (INSTALLADM_FAILURE);
		}
	}

	if (create_netimage) {
		char	dhcpbfile[MAXPATHLEN];
		char	dhcprpath[MAXPATHLEN];

		snprintf(dhcp_macro, sizeof (dhcp_macro),
		    "dhcp_macro_%s", bfile);

		/*
		 * determine contents of bootfile info passed to dhcp script
		 * as well as rootpath for sparc
		 */
		if (have_sparc) {
			snprintf(dhcpbfile, sizeof (dhcpbfile),
			    "http://%s:%s/%s", server_ip, HTTP_PORT,
			    WANBOOTCGI);
			snprintf(dhcprpath, sizeof (dhcprpath),
			    "http://%s:%s%s", server_ip, HTTP_PORT,
			    target_directory);
		} else {
			strlcpy(dhcpbfile, bfile, sizeof (dhcpbfile));
		}

		snprintf(cmd, sizeof (cmd), "%s %s %s %s %s %s %s",
		    SETUP_DHCP_SCRIPT, DHCP_MACRO, have_sparc?"sparc":"x86",
		    server_ip, dhcp_macro, dhcpbfile,
		    have_sparc?dhcprpath:"x86");
		/*
		 * The setup-dhcp script takes care of printing output for the
		 * user so there is no need to print anything for non-zero
		 * return value.
		 */
		installadm_system(cmd);
	}

	if (dhcp_setup_needed && create_netimage) {
		snprintf(cmd, sizeof (cmd), "%s %s %s %d %s",
		    SETUP_DHCP_SCRIPT, DHCP_ASSIGN,
		    ip_start, ip_count, dhcp_macro);
		if (installadm_system(cmd) != 0) {
			(void) fprintf(stderr,
			    MSG_ASSIGN_DHCP_MACRO_ERR);
		}
	}

	if (use_remote_dhcp_server) {
		/* handle later */
	}

	/*
	 * Perform sparc/x86 specific actions.
	 */
	if (have_sparc) {
		/* sparc only */
		snprintf(cmd, sizeof (cmd), "%s %s %s %s %s",
		    SETUP_SPARC_SCRIPT, SPARC_SERVER, target_directory,
		    srv_name, srv_address);

		if (installadm_system(cmd) != 0) {
			(void) fprintf(stderr, MSG_SETUP_SPARC_FAIL);
			return (INSTALLADM_FAILURE);
		}
	} else {
		/* x86 only */
		snprintf(cmd, sizeof (cmd), "%s %s %s %s %s %s",
		    SETUP_TFTP_LINKS_SCRIPT, TFTP_SERVER, srv_name,
		    srv_address, target_directory, bfile);

		if (installadm_system(cmd) != 0) {
			(void) fprintf(stderr, MSG_CREATE_TFTPBOOT_FAIL);
			return (INSTALLADM_FAILURE);
		}
	}

	return (INSTALLADM_SUCCESS);
}

/*
 * do_delete_service:
 * This function stops the DNS-SD service with the given name
 * If the -x argument is passed, it will remove the image, bootfile from
 * /tftpboot
 */
static int
do_delete_service(
	int argc,
	char *argv[],
	scfutilhandle_t *handle,
	const char *use)
{
	char		cmd[MAXPATHLEN];
	char		*service;
	boolean_t	delete_image = B_FALSE;
	service_data_t	data;

	if (argc != 2 && argc != 3) {
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	if (argc == 3) {
		if (strcmp(argv[1], "-x") != 0) {
			(void) fprintf(stderr, "%s\n", gettext(use));
			return (INSTALLADM_FAILURE);
		}
		delete_image = B_TRUE;
		service = argv[2];
	} else {
		service = argv[1];
	}

	if (!validate_service_name(service)) {
		(void) fprintf(stderr, MSG_BAD_SERVICE_NAME);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * make sure the service exists and get info about service
	 */
	if (get_service_data(handle, service, &data) != B_TRUE) {
		(void) fprintf(stderr, MSG_SERVICE_DOESNT_EXIST,
		    service);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * Delete the property group for the service
	 */
	if (remove_install_service(handle, service) != B_TRUE) {
		(void) fprintf(stderr, MSG_REMOVE_INSTALL_SERVICE_FAILED,
		    service);
		return (INSTALLADM_FAILURE);
	}

	snprintf(cmd, sizeof (cmd), "%s %s %s %s %s",
	    SETUP_SERVICE_SCRIPT, SERVICE_REMOVE,
	    service, INSTALL_TYPE, LOCAL_DOMAIN);
	if (installadm_system(cmd) != 0) {
		/*
		 * Print informational message. This
		 * will happen if service was already stopped.
		 */
		(void) fprintf(stderr,
		    MSG_SERVICE_WASNOT_RUNNING, service);
	}

	if (delete_image) {
		(void) snprintf(cmd, sizeof (cmd), "%s %s %s",
		    SETUP_IMAGE_SCRIPT, IMAGE_DELETE, data.image_path);
		if (installadm_system(cmd) != 0) {
			(void) fprintf(stderr, MSG_DELETE_IMAGE_FAIL,
			    data.image_path);
			return (INSTALLADM_FAILURE);
		}
	}

	return (INSTALLADM_SUCCESS);
}

/*
 * do_list:
 * List A/I services or print service manifests and criteria
 * Parse the command line for service name; if we do not have one, then
 * print a list of installed services; if we have a service name, get the
 * service directory path from that service name; then pass service directory
 * path to list-manifests(1) (if the internal -c option is provided pass it
 * to list-manifests(1) as well).
 */
static int
do_list(int argc, char *argv[], scfutilhandle_t *handle, const char *use)
{
	int		opt;
	char		*port = NULL;
	char		*service_name = NULL;
	boolean_t	print_criteria = B_FALSE;
	char		cmd[MAXPATHLEN];
	int		ret;
	service_data_t	data;

	/*
	 * The -c option is an internal option
	 */
	while ((opt = getopt(argc, argv, "n:c")) != -1) {
		switch (opt) {
		/*
		 * The name of the service is supplied.
		 */
		case 'n':
			if (!validate_service_name(optarg)) {
				(void) fprintf(stderr, MSG_BAD_SERVICE_NAME);
				return (INSTALLADM_FAILURE);
			}
			service_name = optarg;
			break;
		case 'c':
			print_criteria = B_TRUE;
			break;
		default:
			(void) fprintf(stderr, "%s\n", gettext(use));
			return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * Make sure correct option combinations
	 */
	if ((print_criteria == B_TRUE) && (service_name == NULL)) {
		(void) fprintf(stderr, MSG_MISSING_OPTIONS, argv[0]);
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	if (service_name != NULL) {
		/*
		 * Get the list of published manifests from the service
		 */
		/*
		 * Gather the directory location of the service
		 */
		if (get_service_data(handle, service_name, &data) != B_TRUE) {
			(void) fprintf(stderr, MSG_SERVICE_PROP_FAIL);
			return (INSTALLADM_FAILURE);
		}
		/*
		 * txt_record should be of the form
		 * "aiwebserver=<host_ip>:<port>" and the directory location
		 * will be AI_SERVICE_DIR_PATH/<port>
		 */
		port = strrchr(data.txt_record, ':');

		if (port == NULL) {
			(void) fprintf(stderr, MSG_SERVICE_PORT_MISSING,
			    service_name, data.txt_record);
			return (INSTALLADM_FAILURE);
		}

		/*
		 * Exclude colon from string (so advance one character)
		 */
		port++;

		/*
		 * Print criteria if requested
		 */
		if (print_criteria == B_TRUE) {
			(void) snprintf(cmd, sizeof (cmd), "%s %s %s%s",
			    MANIFEST_LIST_SCRIPT, "-c", AI_SERVICE_DIR_PATH,
			    port);
		} else {
			(void) snprintf(cmd, sizeof (cmd), "%s %s%s",
			    MANIFEST_LIST_SCRIPT, AI_SERVICE_DIR_PATH,
			    port);
		}

		ret = installadm_system(cmd);

		/*
		 * Ensure we return an error if ret != 0.
		 * If ret == 1 then the Python handled the error, do not print a
		 * new error.
		 */
		if (ret != 0) {
			if (ret == 256) {
				return (INSTALLADM_FAILURE);
			}
			(void) fprintf(stderr, MSG_SUBCOMMAND_FAILED, argv[0]);
			return (INSTALLADM_FAILURE);
		}

	} else {
		/*
		 * Get the list of services running on this system
		 */

		snprintf(cmd, sizeof (cmd), "%s %s %s %s",
		    SETUP_SERVICE_SCRIPT, SERVICE_LIST,
		    INSTALL_TYPE, LOCAL_DOMAIN);
		ret = installadm_system(cmd);
		if (ret != 0) {
			(void) fprintf(stderr, MSG_LIST_SERVICE_FAIL);
			return (INSTALLADM_FAILURE);
		}
	}

	return (INSTALLADM_SUCCESS);
}

/*
 * do_enable:
 * do_enable will enable the specified service
 */
static int
do_enable(int argc, char *argv[], scfutilhandle_t *handle, const char *use)
{
	char		hostname[MAXHOSTNAMELEN];
	char		*port;
	char		*service_name;
	service_data_t	data;
	char		cmd[MAXPATHLEN];

	if (argc != 2) {
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	if (!validate_service_name(argv[1])) {
		(void) fprintf(stderr, MSG_BAD_SERVICE_NAME);
		return (INSTALLADM_FAILURE);
	}
	service_name = argv[1];

	/*
	 * make sure the service exists
	 */
	if (get_service_data(handle, service_name, &data) != B_TRUE) {
		(void) fprintf(stderr, MSG_SERVICE_DOESNT_EXIST,
		    service_name);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * txt_record should be of the form
	 * "aiwebserver=<host_ip>:<port>" and the directory location
	 * will be AI_SERVICE_DIR_PATH/<port>
	 */
	port = strrchr(data.txt_record, ':');

	if (port == NULL) {
		(void) fprintf(stderr, MSG_SERVICE_PORT_MISSING,
		    service_name, data.txt_record);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * Exclude colon from string (so advance one character)
	 */
	port++;
	snprintf(cmd, sizeof (cmd), "%s %s %s %s %s %s %s",
	    SETUP_SERVICE_SCRIPT, SERVICE_REGISTER,
	    service_name, INSTALL_TYPE,
	    LOCAL_DOMAIN, port, data.txt_record);
	if (installadm_system(cmd) != 0) {
		(void) fprintf(stderr, MSG_REGISTER_SERVICE_FAIL,
		    service_name);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * Update status in service's property group
	 */
	strlcpy(data.status, STATUS_ON, STATUSLEN);
	if (save_service_data(handle, data) != B_TRUE) {
		(void) fprintf(stderr, MSG_SAVE_SERVICE_PROPS_FAIL,
		    service_name);
		return (INSTALLADM_FAILURE);
	}

	return (INSTALLADM_SUCCESS);
}

/*
 * do_disable:
 * 	Disable the specified service and optionally update the service's
 *	properties to reflect the new status.
 *	If the -t flag is specified, the service property group should not
 *	be updated to status=off. If -t is not specified it should be.
 */
static int
do_disable(int argc, char *argv[], scfutilhandle_t *handle, const char *use)
{
	char		cmd[MAXPATHLEN];
	char		*service_name;
	service_data_t	data;
	boolean_t	transient = B_FALSE;
	int		option;

	while ((option = getopt(argc, argv, "t")) != -1) {
		switch (option) {
		case 't':
			transient = B_TRUE;
			break;
		default:
			do_opterr(optopt, option, use);
			return (INSTALLADM_FAILURE);
		}
	}

	service_name = argv[optind++];
	if (service_name == NULL) {
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}
	if (!validate_service_name(service_name)) {
		(void) fprintf(stderr, MSG_BAD_SERVICE_NAME);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * make sure the service exists
	 */
	if (get_service_data(handle, service_name, &data) != B_TRUE) {
		(void) fprintf(stderr, MSG_SERVICE_DOESNT_EXIST,
		    service_name);
		return (INSTALLADM_FAILURE);
	}

	if (strcasecmp(data.status, STATUS_OFF) == 0) {
		(void) fprintf(stderr, MSG_SERVICE_NOT_RUNNING,
		    service_name);
		return (INSTALLADM_FAILURE);
	}

	if (transient == B_FALSE) {
		/*
		 * Update status in service's property group
		 */
		strlcpy(data.status, STATUS_OFF, STATUSLEN);
		if (save_service_data(handle, data) != B_TRUE) {
			(void) fprintf(stderr, MSG_SAVE_SERVICE_PROPS_FAIL,
			    service_name);
			return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * Stop the service
	 */
	snprintf(cmd, sizeof (cmd), "%s %s %s %s %s",
	    SETUP_SERVICE_SCRIPT, SERVICE_REMOVE,
	    service_name, INSTALL_TYPE, LOCAL_DOMAIN);
	if (installadm_system(cmd) != 0) {
		/*
		 * Print informational message. This
		 * will happen if service was already stopped.
		 */
		(void) fprintf(stderr,
		    MSG_SERVICE_WASNOT_RUNNING, service_name);
		return (INSTALLADM_FAILURE);
	}

	return (INSTALLADM_SUCCESS);

}

static int
do_create_client(
	int argc,
	char *argv[],
	scfutilhandle_t *handle,
	const char *use)
{

	int	option;
	int	ret;
	char	*mac_addr = NULL;
	char	*bootargs = NULL;
	char	*imagepath = NULL;
	char	*svcname = NULL;

	while ((option = getopt(argc, argv, ":b:e:n:t:")) != -1) {
		switch (option) {
		case 'b':
			bootargs = optarg;
			break;
		case 'e':
			mac_addr = optarg;
			break;
		case 'n':
			svcname = optarg;
			break;
		case 't':
			imagepath = optarg;
			break;
		default:
			do_opterr(optopt, option, use);
			return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * Make sure required options are there
	 */
	if ((mac_addr == NULL) || (svcname == NULL) || (imagepath == NULL)) {
		(void) fprintf(stderr, MSG_MISSING_OPTIONS, argv[0]);
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	if (!validate_service_name(svcname)) {
		(void) fprintf(stderr, MSG_BAD_SERVICE_NAME);
		return (INSTALLADM_FAILURE);
	}

	ret = call_script(CREATE_CLIENT_SCRIPT, argc-1, &argv[1]);
	if (ret != 0) {
		return (INSTALLADM_FAILURE);
	}
	return (INSTALLADM_SUCCESS);
}


static int
do_delete_client(
	int argc,
	char *argv[],
	scfutilhandle_t *handle,
	const char *use)
{
	int	ret;

	/*
	 * There is one required argument, mac_addr of client
	 */
	if (argc != 2) {
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	ret = call_script(DELETE_CLIENT_SCRIPT, argc-1, &argv[1]);
	if (ret != 0) {
		return (INSTALLADM_FAILURE);
	}

	return (INSTALLADM_SUCCESS);
}

/*
 * do_add:
 * Add manifests to an A/I service
 * Parse command line for criteria manifest and service name; get service
 * directory path from service name; then pass manifest and service directory
 * path to publish-manifest(1)
 */
static int
do_add(int argc, char *argv[], scfutilhandle_t *handle, const char *use)
{
	int	option = NULL;
	char	*port = NULL;
	char	*manifest = NULL;
	char	*svcname = NULL;
	char	cmd[MAXPATHLEN];
	int	ret;
	service_data_t	data;

	/*
	 * Check for valid number of arguments
	 */
	if (argc != 5) {
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	while ((option = getopt(argc, argv, ":n:m:")) != -1) {
		switch (option) {
			case 'n':
				svcname = optarg;
				break;
			case 'm':
				manifest = optarg;
				break;
			default:
				do_opterr(optopt, option, use);
				return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * Make sure required options are there
	 */
	if ((svcname == NULL) || (manifest == NULL)) {
		(void) fprintf(stderr, MSG_MISSING_OPTIONS, argv[0]);
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	if (!validate_service_name(svcname)) {
		(void) fprintf(stderr, MSG_BAD_SERVICE_NAME);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * Gather the directory location of the service
	 */
	if (get_service_data(handle, svcname, &data) != B_TRUE) {
		(void) fprintf(stderr, MSG_SERVICE_PROP_FAIL);
		return (INSTALLADM_FAILURE);
	}
	/*
	 * txt_record should be of the form
	 *	"aiwebserver=<host_ip>:<port>"
	 * and the directory location will be AI_SERVICE_DIR_PATH/<port>
	 */
	port = strrchr(data.txt_record, ':');

	if (port == NULL) {
		(void) fprintf(stderr, MSG_SERVICE_PORT_MISSING,
		    svcname, data.txt_record);
		return (INSTALLADM_FAILURE);
	}
	/*
	 * Exclude colon from string (so advance one character)
	 */
	port++;
	(void) snprintf(cmd, sizeof (cmd), "%s %s %s %s%s",
	    MANIFEST_MODIFY_SCRIPT, "-c",
	    manifest, AI_SERVICE_DIR_PATH, port);

	ret = installadm_system(cmd);

	/*
	 * Ensure we return an error if ret != 0.
	 * If ret == 1 then the Python handled the error, do not print a
	 * new error.
	 */
	if (ret != 0) {
		if (ret == 256) {
			return (INSTALLADM_FAILURE);
		}
		(void) fprintf(stderr, MSG_SUBCOMMAND_FAILED, argv[0]);
		return (INSTALLADM_FAILURE);
	}
	return (INSTALLADM_SUCCESS);
}

/*
 * do_remove:
 * Remove manifests from an A/I service
 * Parse the command line for service name and manifest name (and if
 * provided, internal instance name); then, get the service directory
 * path from the provided service name; then pass the manifest name
 * (instance name if provided) and service directory path to
 * delete-manifest(1)
 */
static int
do_remove(int argc, char *argv[], scfutilhandle_t *handle, const char *use)
{
	int	option;
	char	*port = NULL;
	char	*manifest = NULL;
	char	*serv_instance = NULL;
	char	*svcname = NULL;
	char	cmd[MAXPATHLEN];
	int	ret;
	service_data_t	data;

	/*
	 * Check for valid number of arguments
	 */
	if (argc != 5 && argc != 7) {
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	/*
	 * The -i option is an internal option
	 */
	while ((option = getopt(argc, argv, ":n:m:i")) != -1) {
		switch (option) {
			case 'n':
				svcname = optarg;
				break;
			case 'm':
				manifest = optarg;
				break;
			case 'i':
				serv_instance = optarg;
				break;
			default:
				do_opterr(optopt, option, use);
				return (INSTALLADM_FAILURE);
		}
	}

	/*
	 * Make sure required options are there
	 */
	if ((svcname == NULL) || (manifest == NULL)) {
		(void) fprintf(stderr, MSG_MISSING_OPTIONS, argv[0]);
		(void) fprintf(stderr, "%s\n", gettext(use));
		return (INSTALLADM_FAILURE);
	}

	if (!validate_service_name(svcname)) {
		(void) fprintf(stderr, MSG_BAD_SERVICE_NAME);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * Gather the directory location of the service
	 */
	if (get_service_data(handle, svcname, &data) != B_TRUE) {
		(void) fprintf(stderr, MSG_SERVICE_PROP_FAIL);
		return (INSTALLADM_FAILURE);
	}

	/*
	 * txt_record should be of the form "aiwebserver=<host_ip>:<port>"
	 * and the directory location will be AI_SERVICE_DIR_PATH/<port>
	 */
	port = strrchr(data.txt_record, ':');

	if (port == NULL) {
		(void) fprintf(stderr, MSG_SERVICE_PORT_MISSING,
		    svcname, data.txt_record);
		return (INSTALLADM_FAILURE);
	}
	/*
	 * Exclude colon from string (so advance one character)
	 */
	port++;

	/*
	 * See if we're removing a single instance or a whole manifest
	 */
	if (serv_instance == NULL) {
		(void) snprintf(cmd, sizeof (cmd), "%s %s %s%s",
		    MANIFEST_REMOVE_SCRIPT,
		    manifest, AI_SERVICE_DIR_PATH, port);
	} else {
		(void) snprintf(cmd, sizeof (cmd), "%s %s %s %s %s%s",
		    MANIFEST_REMOVE_SCRIPT,
		    manifest, "-i", serv_instance,
		    AI_SERVICE_DIR_PATH, port);
	}
	ret = installadm_system(cmd);

	/*
	 * Ensure we return an error if ret != 0.
	 * If ret == 1 then the Python handled the error, do not print a
	 * new error.
	 */
	if (ret != 0) {
		if (ret == 256) {
			return (INSTALLADM_FAILURE);
		}
		(void) fprintf(stderr, MSG_SUBCOMMAND_FAILED, argv[0]);
		return (INSTALLADM_FAILURE);
	}

	return (INSTALLADM_SUCCESS);
}

static int
do_set(int argc, char *argv[], scfutilhandle_t *handle, const char *use)
{
	/*
	 * TODO: When this is implemented, ai_change_property may
	 * be the appropriate library function to use.
	 */
	return (INSTALLADM_SUCCESS);
}

static int
do_help(int argc, char *argv[], scfutilhandle_t *handle, const char *use)
{
	int	i;
	int	numcmds;
	cmd_t	*cmdp;

	if (argc == 1) {
		usage();
		return (INSTALLADM_FAILURE);
	}

	numcmds = sizeof (cmds) / sizeof (cmds[0]);
	for (i = 0; i < numcmds; i++) {
		cmdp = &cmds[i];
		if (strcmp(argv[1], cmdp->c_name) == 0) {
			if (cmdp->c_usage != NULL) {
				(void) fprintf(stdout, "%s\n",
				    gettext(cmdp->c_usage));
			} else {
				(void) fprintf(stdout,
				    MSG_OPTION_NOHELP, progname,
				    argv[0], cmdp->c_name);
			}
			return (INSTALLADM_SUCCESS);
		}
	}

	(void) fprintf(stderr, MSG_UNKNOWN_HELPSUBCOMMAND,
	    progname, argv[0], argv[1]);
	usage();
	return (INSTALLADM_FAILURE);
}


static void
do_opterr(int opt, int opterr, const char *usage)
{
	switch (opterr) {
		case ':':
			(void) fprintf(stderr,
			    MSG_OPTION_VALUE_MISSING, opt, gettext(usage));
		break;
		case '?':
		default:
			(void) fprintf(stderr,
			    MSG_OPTION_UNRECOGNIZED, opt, gettext(usage));
		break;
	}
}
