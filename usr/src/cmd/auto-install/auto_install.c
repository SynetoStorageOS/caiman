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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <alloca.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <libnvpair.h>
#include <locale.h>
#include <sys/param.h>
#include <sys/types.h>

#include "auto_install.h"
#include <ls_api.h>
#include <orchestrator_api.h>

/*
 * use presence of hidden file to indicate iSCSI boot installation
 * pending code refactoring to make less kludgy
 * see also ict.py
 */
#define	ISCSI_BOOT_INDICATOR_FILE	"/.iscsi_boot"
#define	DEFAULT_HOSTNAME	"solaris"
#define	DEFAULT_ROOT_PASSWORD	"solaris"

static  boolean_t install_done = B_FALSE;
static	boolean_t install_failed = B_FALSE;

/* debug mode - disabled by default */
static	boolean_t debug_mode_enabled = B_FALSE;

int	install_error = 0;
install_params	params;

static boolean_t convert_to_sectors(auto_size_units_t,
    uint64_t, uint64_t *);

void auto_update_progress(om_callback_info_t *, uintptr_t);

static void
usage()
{
	(void) fprintf(stderr,
	    "usage: auto-install -d <diskname> | -p <profile>\n"
	    "\t-i - end installation before Target Instantiation\n"
	    "\t-I - end installation after Target Instantiation\n"
	    "\t-v - run the installer in verbose mode\n");
}

/*
 * enable_debug_mode()
 *
 * Description: Enable/disable debug mode
 *
 * Scope: private
 *
 * Parameters:
 *   enable: B_TRUE - enable debug mode
 *           B_FALSE - disable debug mode
 *
 * Returns: none
 */
static void
enable_debug_mode(boolean_t enable)
{
	debug_mode_enabled = enable;
}

/*
 * is_debug_mode_enabled()
 *
 * Description: Checks, if we run in debug mode
 *
 * Scope: private
 *
 * Parameters: none
 *
 * Returns:
 *   B_TRUE - debug mode enabled
 *   B_FALSE - debug mode disabled
 */
static boolean_t
is_debug_mode_enabled(void)
{
	return (debug_mode_enabled);
}

/*
 * auto_debug_print()
 * Description:	Posts debug message
 */
void
auto_debug_print(ls_dbglvl_t dbg_lvl, char *fmt, ...)
{
	va_list	ap;
	char	buf[MAXPATHLEN + 1] = "";

	va_start(ap, fmt);
	(void) vsnprintf(buf, MAXPATHLEN+1, fmt, ap);
	(void) ls_write_dbg_message("AI", dbg_lvl, buf);
	va_end(ap);
}

/*
 * auto_log_print()
 * Description:	Posts log message
 */
void
auto_log_print(char *fmt, ...)
{
	va_list	ap;
	char	buf[MAXPATHLEN + 1] = "";

	va_start(ap, fmt);
	(void) vsnprintf(buf, MAXPATHLEN+1, fmt, ap);
	(void) ls_write_log_message("AI", buf);
	va_end(ap);
}

/*
 * Callback that gets passed to om_perform_install.
 *
 * Sets the install_done variable when an install is
 * finished. If an install fails, it sets the install_failed
 * variable and also sets the install_error variable to
 * indicate the specific reason for the failure.
 */
void
auto_update_progress(om_callback_info_t *cb_data, uintptr_t app_data)
{
	if (cb_data->curr_milestone == -1) {
		install_error = cb_data->percentage_done;
		install_failed = B_TRUE;
	}

	if (cb_data->curr_milestone == OM_SOFTWARE_UPDATE &&
	    cb_data->percentage_done == 100)
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Transfer completed\n");

	if (cb_data->curr_milestone == OM_POSTINSTAL_TASKS &&
	    cb_data->percentage_done == 100)
		install_done = B_TRUE;
}

/*
 * auto_debug_dump_file()
 * Description: dumps a file using auto_debug_print()
 */
void
auto_debug_dump_file(ls_dbglvl_t level, char *filename)
{
	FILE *file_ptr;
	char buffer[MAXPATHLEN];

	/* Logfile does not exist.  Nothing to print. */
	if (access(filename, F_OK) < 0) {
		return;
	}

	if (access(filename, R_OK) < 0) {
		auto_debug_print(AUTO_DBGLVL_ERR,
		    "ddu errlog %s does not have read permissions.\n");
		return;
	}

	/* Use buffer to set up the command. */
	(void) snprintf(buffer, MAXPATHLEN, "/usr/bin/cat %s", filename);
	if ((file_ptr = popen(buffer, "r")) == NULL) {
		auto_debug_print(AUTO_DBGLVL_ERR,
		    "Error opening ddu errlog %s to dump errors: %s\n",
		    filename, strerror(errno));
		return;
	}

	/* Reuse buffer to get the file data. */
	while (fgets(buffer, MAXPATHLEN, file_ptr) != NULL) {
		auto_debug_print(level, "%s", buffer);
	}
	(void) pclose(file_ptr);
}

/*
 * Create a file that contains the list
 * of packages to be installed or removed.
 *
 * Parameters:
 *   hardcode - if set to B_TRUE, hardcode the list of packages. This is for
 *              testing purposes only, when AI engine is not provided with
 *              AI manifest.
 *
 *   pkg_list_type - specify list of packages to be obtained -
 *                   install or remove.
 *
 *   pkg_list_file - output file where the package list will be saved
 *
 * Returns:
 *	AUTO_INSTALL_SUCCESS for success
 *	AUTO_INSTALL_FAILURE for failure
 *	AUTO_INSTALL_EMPTY_LIST - 'remove' list is empty
 */
static int
create_package_list_file(boolean_t hardcode,
    auto_package_list_type_t pkg_list_type, char *pkg_list_file)
{
	FILE *fp;
	char **package_list;
	int i, num_packages = 0;
	int ret = AUTO_INSTALL_SUCCESS;

	if ((fp = fopen(pkg_list_file, "wb")) == NULL) {
		auto_debug_print(AUTO_DBGLVL_ERR,
		    "Couldn't open file %s for storing list of packages\n",
		    pkg_list_file);

		return (AUTO_INSTALL_FAILURE);
	}

	auto_debug_print(AUTO_DBGLVL_INFO,
	    "File %s successfully opened - list of packages to be %s "
	    "will be saved there\n", pkg_list_file,
	    pkg_list_type == AI_PACKAGE_LIST_INSTALL ? "installed" : "removed");

	/*
	 * When invoked in test mode (without AI manifest), lists of packages
	 * are hardcoded
	 */

	if (hardcode) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Hardcoded list of packages will be generated\n");

		if (pkg_list_type == AI_PACKAGE_LIST_INSTALL) {
			if (fputs(AI_TEST_PACKAGE_LIST_INSTALL, fp) == EOF)
				ret = AUTO_INSTALL_FAILURE;
		} else {
			if (fputs(AI_TEST_PACKAGE_LIST_REMOVE, fp) == EOF)
				ret = AUTO_INSTALL_FAILURE;
		}

		(void) fclose(fp);
		return (ret);
	}

	/*
	 * Obtain list of packages to be installed or removed from AI manifest.
	 *
	 * With respect to install list, there are two tags supported for
	 * specifying list of packages in order to keep backward compatibility.
	 * Try new tag first. If it is not specified, then try the old one.
	 */
	if (pkg_list_type == AI_PACKAGE_LIST_INSTALL) {
		package_list = ai_get_manifest_packages(&num_packages,
		    AIM_PACKAGE_INSTALL_NAME);

		if (package_list == NULL) {
			/* If no package list given, use default */
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "No install package list given, using default\n");

			num_packages = 4;
			package_list =
			    malloc((num_packages + 1) * sizeof (char *));
			if (package_list == NULL) {
				auto_debug_print(AUTO_DBGLVL_ERR,
				    "No memory.\n");
				(void) fclose(fp);
				return (AUTO_INSTALL_FAILURE);
			}
			package_list[0] = strdup("pkg:/SUNWcsd");
			package_list[1] = strdup("pkg:/SUNWcs");
			package_list[2] = strdup("pkg:/babel_install");
			package_list[3] = strdup("pkg:/entire");
			package_list[4] = NULL;
		}

		auto_log_print(gettext(
		    "list of packages to be installed is:\n"));
	} else {
		package_list = ai_get_manifest_packages(&num_packages,
		    AIM_PACKAGE_REMOVE_NAME);
		if (package_list == NULL) {
			auto_debug_print(AUTO_DBGLVL_INFO,
			    "List of packages to be removed is empty\n");

			(void) fclose(fp);
			return (AUTO_INSTALL_EMPTY_LIST);
		}

		auto_log_print(gettext(
		    "list of packages to be removed is:\n"));
	}

	/*
	 * Save list of packages to the file
	 */
	for (i = 0; i < num_packages; i++) {
		auto_log_print("%s\n", package_list[i]);

		if (fputs(package_list[i], fp) == EOF) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "Write to %s file failed\n", pkg_list_file);

			ret = AUTO_INSTALL_FAILURE;
			break;
		}

		if (fputs("\n", fp) == EOF) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "Write to %s file failed\n", pkg_list_file);

			ret = AUTO_INSTALL_FAILURE;
			break;
		}
	}

	(void) fclose(fp);
	return (ret);
}

/*
 * Create/delete/preserve vtoc slices as specified
 * in the manifest
 */
static int
auto_modify_target_slices(auto_slice_info *asi, uint8_t install_slice_id)
{
	for (; asi->slice_action[0] != '\0'; asi++) {
		uint64_t slice_size_sec;

		auto_debug_print(AUTO_DBGLVL_INFO,
		    "slice action %s, size=%lld units=%s\n",
		    asi->slice_action, asi->slice_size,
		    CONVERT_UNITS_TO_TEXT(asi->slice_size_units));

		if (!convert_to_sectors(asi->slice_size_units,
		    asi->slice_size, &slice_size_sec)) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "conversion failure from %lld %s to sectors\n",
			    asi->slice_size,
			    CONVERT_UNITS_TO_TEXT(asi->slice_size_units));
			return (AUTO_INSTALL_FAILURE);
		}
		if (strcmp(asi->slice_action, "create") == 0) {
			om_slice_tag_type_t slice_tag;

			if (asi->slice_number == install_slice_id)
				slice_tag = OM_ROOT;
			else
				slice_tag = OM_UNASSIGNED;
			if (!om_create_slice(asi->slice_number, slice_size_sec,
			    slice_tag, asi->on_existing))
				return (AUTO_INSTALL_FAILURE);
		} else if (strcmp(asi->slice_action, "delete") == 0) {
			if (!om_delete_slice(asi->slice_number))
				return (AUTO_INSTALL_FAILURE);
		} else if (strcmp(asi->slice_action, "preserve") == 0) {
			if (!om_preserve_slice(asi->slice_number))
				return (AUTO_INSTALL_FAILURE);
		}
	}
	return (AUTO_INSTALL_SUCCESS);
}

/*
 * convert value to sectors given basic unit size
 * TODO uint64_t overflow check
 */

static boolean_t
convert_to_sectors(auto_size_units_t units, uint64_t src,
    uint64_t *psecs)
{
	if (psecs == NULL)
		return (B_FALSE);
	switch (units) {
		case AI_SIZE_UNITS_SECTORS:
			*psecs = src;
			break;
		case AI_SIZE_UNITS_MEGABYTES:
			*psecs = src*2048;
			break;
		case AI_SIZE_UNITS_GIGABYTES:
			*psecs = src*2048*1024; /* sec=>MB=>GB */
			break;
		case AI_SIZE_UNITS_TERABYTES:
			*psecs = src*2048*1024*1024; /* sec=>MB=>GB=>TB */
			break;
		default:
			return (B_FALSE);
	}
	if (units != AI_SIZE_UNITS_SECTORS)
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "converting from %lld %s to %lld sectors\n",
		    src, CONVERT_UNITS_TO_TEXT(units), *psecs);
	return (B_TRUE);
}

#ifndef	__sparc
/*
 * Create/delete/preserve fdisk partitions as specifed
 * in the manifest
 * Note that the partition size is converted using the units specified
 *	for both create and delete actions
 */
static int
auto_modify_target_partitions(auto_partition_info *api)
{
	for (; api->partition_action[0] != '\0'; api++) {
		uint64_t partition_size_sec;

		auto_debug_print(AUTO_DBGLVL_INFO,
		    "partition action %s, size=%lld units=%s logical? %s\n",
		    api->partition_action, api->partition_size,
		    CONVERT_UNITS_TO_TEXT(api->partition_size_units),
		    api->partition_is_logical ? "yes" : "no");

		if (!convert_to_sectors(api->partition_size_units,
		    api->partition_size, &partition_size_sec)) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "conversion failure from %lld %s to sectors\n",
			    api->partition_size,
			    CONVERT_UNITS_TO_TEXT(api->partition_size_units));
			return (AUTO_INSTALL_FAILURE);
		}
		if (strcmp(api->partition_action, "create") == 0) {
			if (!om_create_partition(api->partition_type,
			    api->partition_start_sector,
			    partition_size_sec, B_FALSE,
			    api->partition_is_logical))
				return (AUTO_INSTALL_FAILURE);
		} else if (strcmp(api->partition_action, "delete") == 0) {
			if (!om_delete_partition(api->partition_number,
			    api->partition_start_sector, partition_size_sec))
				return (AUTO_INSTALL_FAILURE);
		}
	}
	return (AUTO_INSTALL_SUCCESS);
}
#endif

/*
 * Initialize the image area with default publisher
 * Set the nv-list for configuring default publisher to be used
 * with the installation. This passes the publisher name and url along
 * mount point (/a) and action (initilaize pkg image area). The transfer module
 * will use these parameters and calls the appropriate pkg commands to
 * initialize the pkg imag area and setup the default publisher
 */
static int
configure_ips_init_nv_list(nvlist_t **attr, auto_repo_info_t *repo)
{
	if (nvlist_add_uint32(*attr, TM_ATTR_MECHANISM,
	    TM_PERFORM_IPS) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_ATTR_MECHANISM failed\n");
		return (-1);
	}
	if (nvlist_add_uint32(*attr, TM_IPS_ACTION,
	    TM_IPS_INIT) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TMP_IPS_ACTION failed\n");
		return (-1);
	}
	if (nvlist_add_string(*attr, TM_IPS_INIT_MNTPT,
	    INSTALLED_ROOT_DIR) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_INIT_MNTPT failed\n");
		return (-1);
	}
	if (nvlist_add_string(*attr, TM_IPS_PKG_URL, repo->url) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_PKG_URL failed\n");
		return (-1);
	}

	auto_log_print(gettext("installation will be performed "
	    "from %s (%s)\n"), repo->url, repo->publisher);

	if (nvlist_add_string(*attr, TM_IPS_PKG_AUTH, repo->publisher)
	    != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_PKG_AUTH failed\n");
		return (-1);
	}

	if (nvlist_add_string(*attr, TM_IPS_INIT_RETRY_TIMEOUT,
	    TM_IPS_INIT_TIMEOUT_DEFAULT) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_INIT_RETRY_TIMEOUT failed\n");
		return (-1);
	}

	/*
	 * We need to ask IPS to force creating IPS image, since when
	 * default path is chosen, IPS refuses to create the image.
	 * The reason is that even if we created empty BE to be
	 * populated by IPS, it contains ZFS shared and non-shared
	 * datasets mounted on appropriate mount points. And
	 * IPS complains in the case the target mount point contains
	 * subdirectories.
	 */

	if (nvlist_add_boolean_value(*attr,
	    TM_IPS_IMAGE_CREATE_FORCE, B_TRUE) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_IMAGE_CREATE_FORCE failed\n");
		return (-1);
	}
	return (0);
}

/*
 * configure_ips_addl_publisher_nv_list
 * Set the nv-list for configuring additional publisher(s) to be used
 * with the installation. The nv_list contains the publisher name and url along
 * with mount point (/a) and action (set-publisher). The transfer module
 * will use these parameters and calls the appropriate pkg commands to
 * setup additional publisher.
 */
static int
configure_ips_addl_publisher_nv_list(
    nvlist_t **attr, auto_repo_info_t *repo)
{
	if (nvlist_add_uint32(*attr, TM_ATTR_MECHANISM,
	    TM_PERFORM_IPS) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_ATTR_MECHANISM failed\n");
		return (-1);
	}
	if (nvlist_add_uint32(*attr, TM_IPS_ACTION,
	    TM_IPS_SET_AUTH) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TMP_IPS_ACTION failed\n");
		return (-1);
	}
	if (nvlist_add_string(*attr, TM_IPS_INIT_MNTPT,
	    INSTALLED_ROOT_DIR) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_INIT_MNTPT failed\n");
		return (-1);
	}
	if (nvlist_add_string(*attr, TM_IPS_ALT_URL, repo->url) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_PKG_URL failed\n");
		return (-1);
	}

	auto_log_print(gettext("Using addditional repository "
	    "from %s (%s)\n"), repo->url, repo->publisher);

	if (nvlist_add_string(*attr, TM_IPS_ALT_AUTH, repo->publisher)
	    != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_PKG_AUTH failed\n");
		return (-1);
	}

	return (0);
}

/*
 * configure_ips_mirror_nv_list
 * Set the nv-list for configuring a mirror to either the default repository
 * or any additional repository to be used with the installation. The nv_list
 * contains the publisher name and url along with mount point (/a) and action
 * (set-publisher). The transfer module will use these parameters and calls
 * appropriate pkg commands to setup the mirror.
 */
static int
configure_ips_mirror_nv_list(nvlist_t **attr, char *publisher, char *mirror_url)
{
	if (publisher == NULL || mirror_url == NULL) {
		return (-1);
	}
	auto_log_print(gettext("using mirror at %s for publisher %s\n"),
	    mirror_url, publisher);

	if (nvlist_add_uint32(*attr,
	    TM_ATTR_MECHANISM, TM_PERFORM_IPS) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_ATTR_MECHANISM failed\n");
		return (-1);
	}
	if (nvlist_add_string(*attr,
	    TM_IPS_INIT_MNTPT, INSTALLED_ROOT_DIR) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_INIT_MNTPT failed\n");
		return (-1);
	}
	if (nvlist_add_uint32(*attr,
	    TM_IPS_ACTION, TM_IPS_SET_AUTH) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TMP_IPS_ACTION failed\n");
		return (-1);
	}
	if (nvlist_add_string(*attr,
	    TM_IPS_ALT_URL, mirror_url) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_ALT_URL failed\n");
		return (-1);
	}
	if (nvlist_add_string(*attr,
	    TM_IPS_ALT_AUTH, publisher) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_ALT_AUTH failed\n");
		return (-1);
	}
	if (nvlist_add_string(*attr,
	    TM_IPS_MIRROR_FLAG, TM_IPS_SET_MIRROR) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_MIRROR_FLAG failed\n");
		return (-1);
	}
	auto_log_print(gettext("Using the mirror %s for the publisher %s\n"),
	    mirror_url, publisher);

	return (0);
}

/*
 * Install the target based on the criteria specified in
 * ai.xml.
 *
 * NOTE: ai_validate_manifest() MUST have been called prior
 * to calling this function.
 *
 * RETURNS:
 *	AUTO_INSTALL_SUCCESS on success
 *	AUTO_INSTALL_FAILURE on failure
 */
static int
install_from_manifest()
{
	char *p = NULL;
	auto_disk_info adi;
	auto_swap_device_info adsi;
	auto_dump_device_info addi;
	int status;
	int return_status = AUTO_INSTALL_FAILURE;
	uint8_t install_slice_id;
	int ita = 0;
	int number = 0;
	/*
	 * pointers to heap - free later if not NULL
	 */
	auto_slice_info *asi = NULL;
#ifndef	__sparc
	auto_partition_info *api = NULL;
#endif
	char *diskname = NULL;
	nvlist_t *install_attr = NULL, **transfer_attr = NULL;
	char *proxy = NULL;
	auto_repo_info_t	*default_ips_repo = NULL;
	auto_repo_info_t	*addl_ips_repo = NULL;
	auto_repo_info_t	*rptr;
	auto_mirror_repo_t  *mptr;
	int ret = AUTO_INSTALL_SUCCESS;
	char iscsi_devnam[MAXNAMELEN] = "";

	/*
	 * Start out by getting the install target and
	 * validating that target
	 */
	bzero(&adi, sizeof (auto_disk_info));
	ret = ai_get_manifest_disk_info(&adi);
	if (ret == AUTO_INSTALL_FAILURE) {
		auto_log_print(gettext("disk info manifest error\n"));
		return (AUTO_INSTALL_FAILURE);
	}

	/*
	 * Retrieve device swap information if specified
	 */
	bzero(&adsi, sizeof (auto_swap_device_info));
	ret = ai_get_manifest_swap_device_info(&adsi);
	if (ret == AUTO_INSTALL_FAILURE) {
		auto_log_print(gettext("device swap manifest error\n"));
		return (AUTO_INSTALL_FAILURE);
	}

	/*
	 * Retrieve device dump information if specified
	 */
	bzero(&addi, sizeof (auto_dump_device_info));
	ret = ai_get_manifest_dump_device_info(&addi);
	if (ret == AUTO_INSTALL_FAILURE) {
		auto_log_print(gettext("device dump manifest error\n"));
		return (AUTO_INSTALL_FAILURE);
	}

	/*
	 * grab target slice number
	 */
	install_slice_id = adi.install_slice_number;

	/*
	 * if iSCSI target requested, mount it through iSCSI initiator
	 */
	ret = mount_iscsi_target_if_requested(&adi,
	    iscsi_devnam, sizeof (iscsi_devnam));
	if (ret == -1) {
		auto_log_print(gettext("iSCSI boot target device error\n"));
		return (AUTO_INSTALL_FAILURE);
	}
	/*
	 * if iSCSI device was discovered and mounted,
	 *	write iSCSI boot marker file for ICT reference
	 */
	if (iscsi_devnam[0] == '\0') { /* no iSCSI target mounted */
		/*
		 * make sure indicator file not there from previous run
		 */
		errno = 0;
		if (unlink(ISCSI_BOOT_INDICATOR_FILE) != 0 &&
		    errno != ENOENT) {
			auto_log_print(gettext(
			    "Could not delete " ISCSI_BOOT_INDICATOR_FILE
			    " to indicate no iSCSI boot target\n"));
			return (AUTO_INSTALL_FAILURE);
		}
	} else { /* iSCSI target mounted - indicate for ICT */
		FILE *fd;
		/*
		 * take device name from iSCSI target as selected
		 * install device
		 */
		(void) strncpy(adi.diskname, iscsi_devnam,
		    sizeof (adi.diskname));
		/*
		 * create marker to signal ICT to enable nwam
		 * in service repository
		 */
		fd = fopen(ISCSI_BOOT_INDICATOR_FILE, "w");
		if (fd == NULL) {
			auto_log_print(gettext(
			    "Could not create " ISCSI_BOOT_INDICATOR_FILE
			    " to indicate iSCSI boot target\n"));
			return (AUTO_INSTALL_FAILURE);
		}
		/*
		 * write device name - used for debugging only
		 */
		(void) fputs(iscsi_devnam, fd);
		(void) fclose(fd);
	}

	/*
	 * Initiate target discovery and wait until it is finished
	 */

	if (auto_target_discovery() != AUTO_TD_SUCCESS) {
		auto_log_print(gettext("Automated installation failed in "
		    "Target Discovery module\n"));

		auto_log_print(gettext("Please see previous messages for more "
		    "details\n"));

		return (AUTO_INSTALL_FAILURE);
	}

	/*
	 * given manifest input and discovery information,
	 *	select a target disk for the installation
	 */
	if (auto_select_install_target(&diskname, &adi) != AUTO_TD_SUCCESS) {
		auto_log_print(gettext("ai target device not found\n"));
		return (AUTO_INSTALL_FAILURE);
	}

	auto_log_print(gettext("Disk name selected for installation is %s\n"),
	    diskname);
#ifndef	__sparc
	/*
	 * Configure the partitions as specified in the
	 * manifest
	 */
	api = ai_get_manifest_partition_info(&status);
	if (status != 0) {
		auto_debug_print(AUTO_DBGLVL_ERR,
		    "failed to process manifest due to illegal value\n");
		goto error_ret;
	}
	if (api == NULL)
		auto_log_print(gettext("no manifest partition "
		    "information found\n"));
	else {
		if (auto_modify_target_partitions(api) !=
		    AUTO_INSTALL_SUCCESS) {
			auto_log_print(gettext("failed to modify partition(s) "
			    "specified in the manifest\n"));
			goto error_ret;
		}

		/* we're done with futzing with partitions, free the memory */
		free(api);
		api = NULL; /* don't release later */
	}

	/*
	 * if no partition exists and no partitions were specified in manifest,
	 *	there is no info about partitions for TI,
	 *	so create info table from scratch
	 */
	om_create_target_partition_info_if_absent();

	/* finalize modified partition table for TI to apply to target disk */
	if (!om_finalize_fdisk_info_for_TI()) {
		auto_log_print(gettext("failed to finalize fdisk info\n"));
		return (AUTO_INSTALL_FAILURE);
	}
#endif
	/*
	 * Configure the vtoc slices as specified in the
	 * manifest
	 */
	asi = ai_get_manifest_slice_info(&status);
	if (status != 0) {
		auto_debug_print(AUTO_DBGLVL_ERR,
		    "failed to process manifest due to illegal value\n");
		goto error_ret;
	}
	if (asi == NULL)
		auto_log_print(gettext(
		    "no manifest slice information found\n"));
	else {
		if (auto_modify_target_slices(asi, install_slice_id) !=
		    AUTO_INSTALL_SUCCESS) {
			auto_log_print(gettext(
			    "failed to modify slice(s) specified "
			    "in the manifest\n"));
			goto error_ret;
		}

		/* we're done with futzing with slices, free the memory */
		free(asi);
		asi = NULL;	/* already freed */
	}

	/* finalize modified vtoc for TI to apply to target disk partition */
	if (!om_finalize_vtoc_for_TI(install_slice_id)) {
		auto_log_print(gettext("failed to finalize vtoc info\n"));
		goto error_ret;
	}

	if (nvlist_alloc(&install_attr, NV_UNIQUE_NAME, 0) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "nvlist allocation failed\n");
		goto error_ret;
	}

	if (nvlist_add_uint8(install_attr, OM_ATTR_INSTALL_TYPE,
	    OM_INITIAL_INSTALL) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_INSTALL_TYPE failed\n");
		goto error_ret;
	}

	if (nvlist_add_string(install_attr, OM_ATTR_DISK_NAME,
	    diskname) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_DISK_NAME failed\n");
		goto error_ret;
	}
	free(diskname);
	diskname = NULL;	/* already freed */

	if (nvlist_add_string(install_attr, OM_ATTR_DEFAULT_LOCALE,
	    "C") != 0) {
		auto_log_print(gettext("Setting of OM_ATTR_DEFAULT_LOCALE"
		    " failed\n"));
		goto error_ret;
	}

	/*
	 * If proxy is specified, set the http_proxy environemnet variable for
	 * IPS to use
	 */
	p = ai_get_manifest_http_proxy();
	if (p != NULL) {
		int proxy_len;

		proxy_len = strlen("http_proxy=") + strlen(p) + 1;
		proxy = malloc(proxy_len);
		if (proxy == NULL) {
			auto_debug_print(AUTO_DBGLVL_ERR, "No memory.\n");
			goto error_ret;
		}
		(void) snprintf(proxy, proxy_len, "%s%s", "http_proxy=", p);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting http_proxy environment variable to %s\n", p);
		if (putenv(proxy)) {
			auto_debug_print(AUTO_DBGLVL_INFO,
			    "Setting of http_proxy environment variable failed:"
			    " %s\n", strerror(errno));
			goto error_ret;
		}
	}
	/*
	 * Get the IPS default publisher, mirrors for the default publisher,
	 * additional publishers and mirrors for each additinal publishers.
	 * Based on the data, the space for nv list allocated to perform
	 * Transfer initialization
	 */
	default_ips_repo = ai_get_default_repo_info();
	if (default_ips_repo == NULL) {
		auto_log_print(gettext("IPS default publisher is not "
		    "specified\n"));
		goto error_ret;
	}

	number = 1; /* For the default publisher */
	/*
	 * Count the mirrors
	 */
	for (mptr = default_ips_repo ->mirror_repo; mptr != NULL;
	    mptr = mptr->next_mirror) {
		number++;
	}
	addl_ips_repo = ai_get_additional_repo_info();
	if (addl_ips_repo == NULL) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "No additional IPS publishers specified\n");
	}

	/*
	 * Count the number of additional repos and its mirrors
	 */
	for (rptr =  addl_ips_repo; rptr != NULL; rptr = rptr->next_repo) {
		number++;
		for (mptr = rptr->mirror_repo; mptr != NULL;
		    mptr = mptr->next_mirror) {
			number++;
		}
	}

	/*
	 * Allocate enough pointer space for any possible TM initialization
	 * 	number of publishers and their mirrors
	 *	+ Packages to be installed
	 *	+ Packages to be removed
	 */
	transfer_attr = calloc(number+2, sizeof (nvlist_t *));
	if (transfer_attr == NULL) {
		goto error_ret;
	}

	ita = 0;
	if (nvlist_alloc(&transfer_attr[ita], NV_UNIQUE_NAME, 0) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "nvlist allocation failed\n");
		goto error_ret;
	}
	/*
	 * Initialize the image pkg area and setup default publisher
	 */
	status = configure_ips_init_nv_list(
	    &transfer_attr[ita], default_ips_repo);
	if (status != SUCCESS) {
		goto error_ret;
	}

	/*
	 * Setup the mirrors for the default publisher one at a time
	 */
	for (mptr = default_ips_repo->mirror_repo;
	    mptr != NULL; mptr = mptr->next_mirror) {
		char    *publisher;
		char    *mirror_url;

		ita++;
		publisher = default_ips_repo->publisher;
		mirror_url = mptr->mirror_url;
		if (nvlist_alloc(&transfer_attr[ita], NV_UNIQUE_NAME, 0) != 0) {
			auto_debug_print(AUTO_DBGLVL_INFO,
			    "nvlist allocation failed\n");
			return (-1);
		}
		status = configure_ips_mirror_nv_list(&transfer_attr[ita],
		    publisher, mirror_url);
		if (status != SUCCESS) {
			goto error_ret;
		}
	}

	/*
	 * Configure the additional publisher(s)
	 */
	for (rptr = addl_ips_repo; rptr != NULL; rptr = rptr->next_repo) {
		ita++;
		if (nvlist_alloc(&transfer_attr[ita],
		    NV_UNIQUE_NAME, 0) != 0) {
			auto_debug_print(AUTO_DBGLVL_INFO,
			    "nvlist allocation failed\n");
			goto error_ret;
		}
		status = configure_ips_addl_publisher_nv_list
		    (&transfer_attr[ita], rptr);
		if (status != SUCCESS) {
			goto error_ret;
		}

		/*
		 * Setup mirrors (if any) for each additional publisher
		 */
		for (mptr = rptr->mirror_repo;
		    mptr != NULL; mptr = mptr->next_mirror) {
			char    *publisher;
			char    *mirror_url;

			ita++;
			publisher = rptr->publisher;
			mirror_url = mptr->mirror_url;
			if (nvlist_alloc(&transfer_attr[ita],
			    NV_UNIQUE_NAME, 0) != 0) {
				auto_debug_print(AUTO_DBGLVL_INFO,
				    "nvlist allocation failed\n");
				return (-1);
			}
			status = configure_ips_mirror_nv_list(
			    &transfer_attr[ita], publisher, mirror_url);
			if (status != SUCCESS) {
				goto error_ret;
			}
		}
	}

	/*
	 * Get the list of packages and add it to the nv_list
	 */
	ita++;
	if (nvlist_alloc(&transfer_attr[ita], NV_UNIQUE_NAME, 0) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "nvlist allocation failed\n");
		goto error_ret;
	}
	if (nvlist_add_uint32(transfer_attr[ita], TM_ATTR_MECHANISM,
	    TM_PERFORM_IPS) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_ATTR_MECHANISM failed\n");
		goto error_ret;
	}
	if (nvlist_add_uint32(transfer_attr[ita], TM_IPS_ACTION,
	    TM_IPS_RETRIEVE) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TMP_IPS_ACTION failed\n");
		goto error_ret;
	}
	if (nvlist_add_string(transfer_attr[ita], TM_IPS_INIT_MNTPT,
	    INSTALLED_ROOT_DIR) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_INIT_MNTPT failed\n");
		goto error_ret;
	}

	/*
	 * list out the list of packages to be installed
	 * from the manifest and add it into a file
	 */
	if (create_package_list_file(B_FALSE, AI_PACKAGE_LIST_INSTALL,
	    AUTO_INSTALL_PKG_LIST_FILE) != AUTO_INSTALL_SUCCESS) {
		auto_log_print(gettext("Failed to create a file with list "
		    "of packages to be installed\n"));
		goto error_ret;
	}
	if (nvlist_add_string(transfer_attr[ita], TM_IPS_PKGS,
	    AUTO_INSTALL_PKG_LIST_FILE) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_PKGS failed\n");
		goto error_ret;
	}

	/*
	 * if debug mode enabled, run 'pkg install' in verbose mode
	 */
	if (is_debug_mode_enabled()) {
		if (nvlist_add_boolean_value(transfer_attr[ita],
		    TM_IPS_VERBOSE_MODE, B_TRUE) != 0) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "Setting of TM_IPS_VERBOSE_MODE failed\n");
			goto error_ret;
		}
	}

	/*
	 * Since this operation is optional (list of packages
	 * to be removed might be empty), before we start to
	 * populate nv list with attributes, determine if there
	 * is anything to do.
	 */
	ret = create_package_list_file(B_FALSE, AI_PACKAGE_LIST_REMOVE,
	    AUTO_REMOVE_PKG_LIST_FILE);

	if (ret == AUTO_INSTALL_FAILURE) {
		auto_log_print(gettext("Failed to create a file with list "
		    "of packages to be removed\n"));
		goto error_ret;
	} else if (ret == AUTO_INSTALL_EMPTY_LIST) {
		auto_log_print(gettext("No packages specified to be removed "
		    "from installed system\n"));
	} else {
		/*
		 * allocate nv list
		 */
		ita++;

		if (nvlist_alloc(&transfer_attr[ita], NV_UNIQUE_NAME, 0) != 0) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "nvlist allocation failed\n");
			goto error_ret;
		}

		/* select IPS transfer mechanism */
		if (nvlist_add_uint32(transfer_attr[ita], TM_ATTR_MECHANISM,
		    TM_PERFORM_IPS) != 0) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "Setting of TM_ATTR_MECHANISM failed\n");
			goto error_ret;
		}

		/* specify 'uninstall' action */
		if (nvlist_add_uint32(transfer_attr[ita], TM_IPS_ACTION,
		    TM_IPS_UNINSTALL) != 0) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "Setting of TMP_IPS_ACTION failed\n");
			goto error_ret;
		}

		/*  set target mountpoint */
		if (nvlist_add_string(transfer_attr[ita], TM_IPS_INIT_MNTPT,
		    INSTALLED_ROOT_DIR) != 0) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "Setting of TM_IPS_INIT_MNTPT failed\n");
			goto error_ret;
		}

		/*  provide list of packages to be removed */
		if (nvlist_add_string(transfer_attr[ita], TM_IPS_PKGS,
		    AUTO_REMOVE_PKG_LIST_FILE) != 0) {
			auto_debug_print(AUTO_DBGLVL_ERR,
			    "Setting of TM_IPS_PKGS failed\n");
			goto error_ret;
		}

		/*
		 * if debug mode enabled, run 'pkg uninstall' in verbose mode
		 */
		if (is_debug_mode_enabled()) {
			if (nvlist_add_boolean_value(transfer_attr[ita],
			    TM_IPS_VERBOSE_MODE, B_TRUE) != 0) {
				auto_debug_print(AUTO_DBGLVL_ERR,
				    "Setting of TM_IPS_VERBOSE_MODE failed\n");
				goto error_ret;
			}
		}
	}

	if (nvlist_add_nvlist_array(install_attr, OM_ATTR_TRANSFER,
	    transfer_attr, ita + 1) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_TRANSFER failed\n");
		goto error_ret;
	}

	/* Add requested swap size */
	if (adsi.swap_size >= 0) {
		if (nvlist_add_int32(install_attr, OM_ATTR_SWAP_SIZE,
		    adsi.swap_size) != 0) {
			nvlist_free(install_attr);
			auto_debug_print(AUTO_DBGLVL_INFO,
			    "Setting of OM_ATTR_SWAP_SIZE failed\n");
			return (AUTO_INSTALL_FAILURE);
		}
	}

	/* Add requested dump device size */
	if (addi.dump_size >= 0) {
		if (nvlist_add_int32(install_attr, OM_ATTR_DUMP_SIZE,
		    addi.dump_size) != 0) {
			nvlist_free(install_attr);
			auto_debug_print(AUTO_DBGLVL_INFO,
			    "Setting of OM_ATTR_DUMP_SIZE failed\n");
			return (AUTO_INSTALL_FAILURE);
		}
	}

	status = om_perform_install(install_attr, auto_update_progress);
	if (status == OM_FAILURE) { /* synchronous failure before threading */
		install_error = om_errno;
		install_failed = B_TRUE;
	}
	/* wait for thread to report final status */
	while (!install_done && !install_failed)
		sleep(10);

	/*
	 * If the installation failed, report where or/and why the failure
	 * happened
	 */

	if (install_failed) {
		/*
		 * Check if valid failure code was returned - if not, log only
		 * error code itself instead of descriptive strings
		 */

		if (!om_is_valid_failure_code(install_error)) {
			auto_log_print(gettext("Automated Installation failed"
			    " with unknown error code %d\n"), install_error);
		} else {
			char	*err_str;

			/* Where the failure happened */
			if ((err_str =
			    om_get_failure_source(install_error)) != NULL)
				auto_log_print(gettext("Automated Installation"
				    " failed in %s module\n"), err_str);

			/* Why the failure happened */
			if ((err_str =
			    om_get_failure_reason(install_error)) != NULL)
				auto_log_print(gettext("%s\n"), err_str);
		}
	} else {
		return_status = AUTO_INSTALL_SUCCESS;
	}

error_ret:	/* free all memory - may have jumped here upon error */
	if (proxy != NULL)
		free(proxy);
#ifndef	__sparc
	if (api != NULL)
		free(api);
#endif
	if (asi != NULL)
		free(asi);
	if (diskname != NULL)
		free(diskname);
	free_repo_info_list(default_ips_repo);
	free_repo_info_list(addl_ips_repo);
	if (install_attr != NULL)
		nvlist_free(install_attr);
	if (transfer_attr != NULL) {
		int i;

		for (i = 0; i <= ita; i++)
			if (transfer_attr[i] != NULL)
				nvlist_free(transfer_attr[i]);
		free(transfer_attr);
	}
	return (return_status);
}

/*
 * Install the target based on the specified diskname
 * or if no diskname is specified, install it based on
 * the criteria specified in ai.xml.
 *
 * Returns
 *	AUTO_INSTALL_SUCCESS on a successful install
 *	AUTO_INSTALL_FAILURE on a failed install
 */
static int
auto_perform_install(char *diskname)
{
	nvlist_t	*install_attr, *transfer_attr[2];
	int 		status;

	/*
	 * No disk specified on command line
	 *  - perform installation based on manifest information instead
	 */

	if (*diskname == '\0')
		return (install_from_manifest());

	/*
	 * Install to disk specified on command line
	 *
	 * Initiate target discovery and wait until it is finished
	 */

	if (auto_target_discovery() != AUTO_TD_SUCCESS) {
		auto_log_print(gettext("Automated installation failed in "
		    "Target Discovery module\n"));

		auto_log_print(gettext("Please see previous messages for more "
		    "details\n"));

		return (AUTO_INSTALL_FAILURE);
	}

	/*
	 * We're installing on the specified diskname
	 * Since this is usually called from a test
	 * program, we hardcode the various system
	 * configuration parameters
	 */

	if (auto_select_install_target(&diskname, NULL) != 0) {
		auto_log_print(gettext("Error: Target disk name %s is "
		    "not valid\n"), diskname);
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_alloc(&install_attr, NV_UNIQUE_NAME, 0) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "nvlist allocation failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_uint8(install_attr, OM_ATTR_INSTALL_TYPE,
	    OM_INITIAL_INSTALL) != 0) {
		nvlist_free(install_attr);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_INSTALL_TYPE failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(install_attr, OM_ATTR_DISK_NAME,
	    diskname) != 0) {
		nvlist_free(install_attr);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_DISK_NAME failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(install_attr, OM_ATTR_ROOT_PASSWORD,
	    om_encrypt_passwd(DEFAULT_ROOT_PASSWORD, "root")) != 0) {
		nvlist_free(install_attr);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_ROOT_PASSWORD failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(install_attr, OM_ATTR_USER_NAME,
	    "fool") != 0) {
		nvlist_free(install_attr);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_USER_NAME failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(install_attr, OM_ATTR_USER_PASSWORD,
	    om_encrypt_passwd("ass", "fool")) != 0) {
		nvlist_free(install_attr);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_USER_PASSWORD failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(install_attr, OM_ATTR_LOGIN_NAME,
	    "fool") != 0) {
		nvlist_free(install_attr);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_LOGIN_NAME failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(install_attr, OM_ATTR_HOST_NAME,
	    DEFAULT_HOSTNAME) != 0) {
		nvlist_free(install_attr);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_HOST_NAME failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(install_attr, OM_ATTR_DEFAULT_LOCALE,
	    "C") != 0) {
		nvlist_free(install_attr);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_DEFAULT_LOCALE failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_alloc(&transfer_attr[0], NV_UNIQUE_NAME, 0) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "nvlist allocation failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_uint32(transfer_attr[0], TM_ATTR_MECHANISM,
	    TM_PERFORM_IPS) != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_ATTR_MECHANISM failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_uint32(transfer_attr[0], TM_IPS_ACTION,
	    TM_IPS_INIT) != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TMP_IPS_ACTION failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(transfer_attr[0], TM_IPS_INIT_MNTPT,
	    INSTALLED_ROOT_DIR) != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_INIT_MNTPT failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(transfer_attr[0], TM_IPS_PKG_URL,
	    "http://ipkg.sfbay:10004") != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_PKG_URL failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(transfer_attr[0], TM_IPS_PKG_AUTH,
	    "ipkg.sfbay") != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_PKG_AUTH failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_alloc(&transfer_attr[1], NV_UNIQUE_NAME, 0) != 0) {
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "nvlist allocation failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_uint32(transfer_attr[1], TM_ATTR_MECHANISM,
	    TM_PERFORM_IPS) != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		nvlist_free(transfer_attr[1]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_ATTR_MECHANISM failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_uint32(transfer_attr[1], TM_IPS_ACTION,
	    TM_IPS_RETRIEVE) != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		nvlist_free(transfer_attr[1]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TMP_IPS_ACTION failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(transfer_attr[1], TM_IPS_INIT_MNTPT,
	    INSTALLED_ROOT_DIR) != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		nvlist_free(transfer_attr[1]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_INIT_MNTPT failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (create_package_list_file(B_TRUE, AI_PACKAGE_LIST_INSTALL,
	    AUTO_INSTALL_PKG_LIST_FILE) != AUTO_INSTALL_SUCCESS) {
		auto_log_print(gettext("Failed to create a file with list "
		    "of packages to be installed\n"));
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_string(transfer_attr[1], TM_IPS_PKGS,
	    AUTO_INSTALL_PKG_LIST_FILE) != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		nvlist_free(transfer_attr[1]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of TM_IPS_PKG_URL failed\n");
		return (AUTO_INSTALL_FAILURE);
	}

	if (nvlist_add_nvlist_array(install_attr, OM_ATTR_TRANSFER,
	    transfer_attr, 2) != 0) {
		nvlist_free(install_attr);
		nvlist_free(transfer_attr[0]);
		nvlist_free(transfer_attr[1]);
		auto_debug_print(AUTO_DBGLVL_INFO,
		    "Setting of OM_ATTR_TRANSFER failed\n");
		return (AUTO_INSTALL_FAILURE);
	}
	status = om_perform_install(install_attr, auto_update_progress);

	while (!install_done && !install_failed)
		sleep(10);

	nvlist_free(install_attr);
	nvlist_free(transfer_attr[0]);
	nvlist_free(transfer_attr[1]);

	if (install_failed || status != OM_SUCCESS)
		return (AUTO_INSTALL_FAILURE);
	else
		return (AUTO_INSTALL_SUCCESS);
}

/*
 * Function:	auto_get_disk_name_from_slice
 * Description: Convert a conventional disk name into the internal canonical
 *		form. Remove the trailing index reference. The return status
 *		reflects whether or not the 'src' name is valid.
 *
 *				src			 dst
 *			---------------------------------------
 *			[/dev/rdsk/]c0t0d0s0	->	c0t0d0
 *			[/dev/rdsk/]c0t0d0p0	->	c0t0d0
 *			[/dev/rdsk/]c0d0s0	->	c0d0
 *			[/dev/rdsk/]c0d0p0	->	c0d0
 *
 * Scope:	public
 * Parameters:	dst	- used to retrieve cannonical form of drive name
 *			  ("" if not valid)
 *		src	- name of drive to be processed (see table above)
 * Return:	 0	- valid disk name
 *		-1	- invalid disk name
 */
static void
auto_get_disk_name_from_slice(char *dst, char *src)
{
	char		name[MAXPATHLEN];
	char		*cp;

	*dst = '\0';

	(void) strcpy(name, src);
	/*
	 * The slice could be like s2 or s10
	 */
	cp = name + strlen(name) - 3;
	if (*cp) {
		if (*cp == 'p' || *cp == 's') {
			*cp = '\0';
		} else {
			cp++;
			if (*cp == 'p' || *cp == 's') {
				*cp = '\0';
			}
		}
	}

	/* It could be full pathname like /dev/dsk/disk_name */
	if ((cp = strrchr(name, '/')) != NULL) {
		cp++;
		(void) strcpy(dst, cp);
	} else {
		/* Just the disk name is provided, so return the name */
		(void) strcpy(dst, name);
	}
}

int
main(int argc, char **argv)
{
	int		opt;
	extern char 	*optarg;
	char		manifestf[MAXNAMELEN];
	char		diskname[MAXNAMELEN];
	char		slicename[MAXNAMELEN];
	int		num_du_pkgs_installed;
	boolean_t	auto_reboot_enabled = B_FALSE;
	nvlist_t	*ls_init_attr = NULL;
	boolean_t	auto_install_failed = B_FALSE;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	manifestf[0] = '\0';
	slicename[0] = '\0';
	while ((opt = getopt(argc, argv, "vd:Iip:")) != -1) {
		switch (opt) {
		case 'd': /* target disk name for testing only */
			(void) strlcpy(slicename, optarg, sizeof (slicename));
			break;
		case 'I': /* break after Target Instantiation for testing */
			om_set_breakpoint(OM_breakpoint_after_TI);
			break;
		case 'i': /* break before Target Instantiation for testing */
			om_set_breakpoint(OM_breakpoint_before_TI);
			break;
		case 'p': /* manifest is provided */
			(void) strlcpy(manifestf, optarg, sizeof (manifestf));
			break;
		case 'v': /* debug verbose mode enabled */
			enable_debug_mode(B_TRUE);
			break;
		default:
			usage();
			exit(AI_EXIT_FAILURE);
		}
	}

	if (manifestf[0] == '\0' && slicename[0] == '\0') {
		usage();
		exit(AI_EXIT_FAILURE);
	}

	/*
	 * initialize logging service - increase verbosity level
	 * if installer was invoked in debug mode
	 * print error messages to stderr, since we don't have
	 * logging service available at this point
	 */
	if (is_debug_mode_enabled()) {
		if (nvlist_alloc(&ls_init_attr, NV_UNIQUE_NAME, 0) != 0) {
			(void) fprintf(stderr,
			    "nvlist allocation failed for ls_init_attrs\n");

			exit(AI_EXIT_FAILURE);
		}

		if (nvlist_add_int16(ls_init_attr, LS_ATTR_DBG_LVL,
		    LS_DBGLVL_INFO) != 0) {
			(void) fprintf(stderr,
			    "Setting LS_ATTR_DBG_LVL failed\n");

			nvlist_free(ls_init_attr);
			exit(AI_EXIT_FAILURE);
		}
	}

	if (ls_init(ls_init_attr) != LS_E_SUCCESS) {
		(void) fprintf(stderr,
		    "Couldn't initialize logging service\n");

		nvlist_free(ls_init_attr);
		exit(AI_EXIT_FAILURE);
	}

	/* release nvlist, since it is no longer needed */
	nvlist_free(ls_init_attr);

	if (manifestf[0] != '\0') {
		char	*ai_auto_reboot;

		/*
		 * Validate the AI manifest. If it validates, set
		 * it up in an in-memory tree so searches can be
		 * done on it in the future to retrieve the values
		 */
		if (ai_create_manifest_image(manifestf) ==
		    AUTO_VALID_MANIFEST) {
			auto_log_print(gettext("%s manifest created\n"),
			    manifestf);
		} else {
			auto_log_print(gettext("Auto install failed. Error "
			    "creating manifest %s\n"), manifestf);
			exit(AI_EXIT_FAILURE_AIM);
		}

		if (ai_setup_manifest_image() == AUTO_VALID_MANIFEST) {
			auto_log_print(gettext(
			    "%s manifest setup and validated\n"), manifestf);
		} else {
			char *setup_err = gettext("Auto install failed. Error "
			    "setting up and validating manifest %s\n");
			auto_log_print(setup_err, manifestf);
			(void) fprintf(stderr, setup_err, manifestf);
			exit(AI_EXIT_FAILURE_AIM);
		}

		/*
		 * Install any drivers required for installation, in the
		 * booted environment.
		 *
		 * Don't fail the whole installation if ai_du_get_and_install()
		 * fails here.  This operation affects only the booted
		 * environment.  It is possible that a package missing here will
		 * already be included in the target install, so let the
		 * installation proceed.  If something critical is still
		 * missing, the target install will fail anyway.
		 *
		 * First boolean: do not honor noinstall flag.
		 * Second boolean: do not update the boot archive.
		 */
		if (ai_du_get_and_install("/", B_FALSE, B_FALSE,
		    &num_du_pkgs_installed) != AUTO_INSTALL_SUCCESS) {
			/* Handle failure or "package not found" statuses. */
			char *du_warning = gettext("Warning: some additional "
			    "driver packages could not be installed\n"
			    "  to booted installation environment.\n"
			    "  These drivers may or may not be required for "
			    "the installation to proceed.\n"
			    "  Will continue anyway...\n");
			auto_log_print(du_warning);
			(void) fprintf(stderr, du_warning);

		} else if (num_du_pkgs_installed > 0) {
			/*
			 * Note: Print no messages if num_du_pkgs_installed = 0
			 * This means no packages and no errors, or no-op.
			 */
			auto_log_print(gettext("Add Drivers: All required "
			    "additional driver packages successfully installed "
			    "to booted installation environment.\n"));
		}

		diskname[0] = '\0';

		/*
		 * Since valid manifest was provided, check if automated reboot
		 * feature is enabled.
		 */

		ai_auto_reboot = ai_get_manifest_element_value(AIM_AUTO_REBOOT);

		if (ai_auto_reboot != NULL) {
			if (strcasecmp(ai_auto_reboot, "true") == 0) {
				auto_log_print(
				    gettext("Auto reboot enabled\n"));

				auto_reboot_enabled = B_TRUE;
			} else {
				auto_log_print(
				    gettext("Auto reboot disabled\n"));
			}
		}
	}

	if (slicename[0] != '\0') {
		auto_get_disk_name_from_slice(diskname, slicename);
	}

	if (auto_perform_install(diskname) != AUTO_INSTALL_SUCCESS) {
		(void) fprintf(stderr, "Automated Installation failed\n");

		auto_install_failed = B_TRUE;
	} else {
		/*
		 * Install additional drivers on target.
		 * First boolean: honor noinstall flag.
		 * Second boolean: update boot archive.
		 */
		if (ai_du_install(INSTALLED_ROOT_DIR, B_TRUE, B_TRUE,
		    &num_du_pkgs_installed) == AUTO_INSTALL_FAILURE) {
			char *tgt_inst_err = gettext("Basic installation was "
			    "successful.  However, there was an error\n");
			auto_log_print(tgt_inst_err, manifestf);
			(void) fprintf(stderr, tgt_inst_err);
			tgt_inst_err = gettext("installing at least one "
			    "additional driver package on target.\n");
			auto_log_print(tgt_inst_err, manifestf);
			(void) fprintf(stderr, tgt_inst_err);
			tgt_inst_err = gettext("Please verify that all driver "
			    "packages required for reboot are installed "
			    "before rebooting.\n");
			auto_log_print(tgt_inst_err, manifestf);
			(void) fprintf(stderr, tgt_inst_err);
			auto_install_failed = B_TRUE;
		}
	}

	if (! auto_install_failed) {

		if (auto_reboot_enabled) {
			printf(gettext("Automated Installation succeeded."
			    " System will be rebooted now\n"));

			auto_log_print(gettext("Automated Installation"
			    " succeeded. System will be rebooted now\n"));
		} else {
			printf(gettext("Automated Installation succeeded. You"
			    " may wish to reboot the system at this time\n"));

			auto_log_print(gettext("Automated Installation"
			    " succeeded. You may wish to reboot the system"
			    " at this time\n"));
		}
	}

	(void) ai_teardown_manifest_state();

	/*
	 * Transfer /tmp/install_log file now that it is complete.
	 * Subsequent messages are not captured in copy of log file
	 * tranfered to destination.
	 */

	if (access(INSTALLED_ROOT_DIR, F_OK) == 0) {
		if (ls_transfer("/", INSTALLED_ROOT_DIR) != LS_E_SUCCESS) {
			auto_log_print(gettext(
			    "Could not transfer log file to the target\n"));
		}
	}

	/*
	 * If the installation failed, abort now and let the user inspect
	 * the system
	 */

	if (auto_install_failed)
		exit(AI_EXIT_FAILURE);

	/*
	 * Unmount installed boot environment
	 */
	if (om_unmount_target_be() != OM_SUCCESS) {
		auto_log_print(gettext(
		    "Could not unmount target boot environment.\n"));

		auto_install_failed = B_TRUE;
	}

	/*
	 * Exit with return codes reflecting the result of the installation:
	 *  AI_EXIT_SUCCESS - installation succeeded, don't reboot automatically
	 *  AI_EXIT_AUTO_REBOOT - installation succeeded, reboot automatically
	 */

	if (auto_install_failed)
		exit(AI_EXIT_FAILURE);

	if (auto_reboot_enabled)
		exit(AI_EXIT_AUTO_REBOOT);

	exit(AI_EXIT_SUCCESS);
}
