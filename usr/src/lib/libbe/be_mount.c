/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * System includes
 */
#include <assert.h>
#include <errno.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfstab.h>
#include <unistd.h>

#include "libbe.h"
#include "libbe_priv.h"

/* Private function prototypes */
static int be_mount_callback(zfs_handle_t *, void *);
static int be_unmount_callback(zfs_handle_t *, void *);
static int fix_mountpoint_callback(zfs_handle_t *, void *);
static int get_mountpoint_from_vfstab(char *, char *, char *, size_t,
    boolean_t);
static int loopback_mount_shared_fs(zfs_handle_t *, be_mount_data_t *);
static int iter_shared_fs_callback(zfs_handle_t *, void *);
static int zpool_shared_fs_callback(zpool_handle_t *, void *);

/* ********************************************************************	*/
/*			Public Functions				*/
/* ********************************************************************	*/

/*
 * Function:	be_mount
 * Description:	Mounts a BE and its subordinate datasets at a given mountpoint.
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			The following attributes are used by this function:
 *
 *			BE_ATTR_ORIG_BE_NAME		*required
 *			BE_ATTR_MOUNTPOINT		*required
 *			BE_ATTR_MOUNT_FLAGS		*optional
 * Return:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Public
 */
int
be_mount(nvlist_t *be_attrs)
{
	char	*be_name = NULL;
	char	*mountpoint = NULL;
	int	flags = 0;
	int	ret = 0;

	/* Initialize libzfs handle */
	if (!be_zfs_init())
		return (1);

	/* Get original BE name */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_ORIG_BE_NAME, &be_name)
	    != 0) {
		be_print_err(gettext("be_mount: failed to lookup "
		    "BE_ATTR_ORIG_BE_NAME attribute\n"));
		return (1);
	}

	/* Validate original BE name */
	if (!be_valid_be_name(be_name)) {
		be_print_err(gettext("be_mount: invalid BE name %s\n"),
		    be_name);
		return (1);
	}

	/* Get mountpoint */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_MOUNTPOINT, &mountpoint)
	    != 0) {
		be_print_err(gettext("be_mount: failed to lookup "
		    "BE_ATTR_MOUNTPOINT attribute\n"));
		return (1);
	}

	/* Get flags */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_MOUNT_FLAGS, DATA_TYPE_UINT16, &flags, NULL) != 0) {
		be_print_err(gettext("be_mount: failed to lookup "
		    "lookup BE_ATTR_MOUNT_FLAGS attribute\n"));
		return (1);
	}

	ret = _be_mount(be_name, mountpoint, flags);

	be_zfs_fini();

	return (ret);
}

/*
 * Function:	be_unmount
 * Description:	Unmounts a BE and its subordinate datasets.
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			The following attributes are used by this function:
 *
 *			BE_ATTR_ORIG_BE_NAME		*required
 * Return:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Public
 */
int
be_unmount(nvlist_t *be_attrs)
{
	char	*be_name = NULL;
	int	ret = 0;

	/* Initialize libzfs handle */
	if (!be_zfs_init())
		return (1);

	/* Get original BE name */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_ORIG_BE_NAME, &be_name)
	    != 0) {
		be_print_err(gettext("be_unmount: failed to lookup "
		    "BE_ATTR_ORIG_BE_NAME attribute\n"));
		return (1);
	}

	/* Validate original BE name */
	if (!be_valid_be_name(be_name)) {
		be_print_err(gettext("be_unmount: invalid BE name %s\n"),
		    be_name);
		return (1);
	}

	ret = _be_unmount(be_name);

	be_zfs_fini();

	return (ret);
}

/* ********************************************************************	*/
/*			Semi-Private Functions				*/
/* ******************************************************************** */

/*
 * Function:	_be_mount
 * Description:	
 * Parameters:
 *		be_name - pointer to name of BE to mount.
 *		altroot - pointer to altroot of where to mount BE.
 *		flags - flag indicating special handling for mounting the BE
 * Return:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
_be_mount(char *be_name, char *altroot, int flags)
{
	be_transaction_data_t	bt = { 0 };
	be_mount_data_t	md = { 0 };
	zfs_handle_t	*zhp;
	char		obe_root_ds[MAXPATHLEN];
	char		mountpoint[MAXPATHLEN];
	char		*mp = NULL;
	int		ret;

	if (be_name == NULL || altroot == NULL)
		return (1);

	/* Set be_name as obe_name in bt structure */
	bt.obe_name = be_name;

	/* Find which zpool obe_name lives in */
	if ((ret = zpool_iter(g_zfs, be_find_zpool_callback, &bt)) == 0) {
		be_print_err(gettext("be_mount: failed to "
		    "find zpool for BE (%s)\n"), bt.obe_name);
		return (1);
	} else if (ret < 0) {
		be_print_err(gettext("be_mount: zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		return (1);
	}

	/* Generate string for obe_name's root dataset */
	be_make_root_ds(bt.obe_zpool, bt.obe_name, obe_root_ds,
	    sizeof (obe_root_ds));
	bt.obe_root_ds = obe_root_ds;

	/* Get handle to BE's root dataset */
	if ((zhp = zfs_open(g_zfs, bt.obe_root_ds, ZFS_TYPE_FILESYSTEM)) ==
	    NULL) {
		be_print_err(gettext("be_mount: failed to "
		    "open BE root dataset (%s)\n"), bt.obe_root_ds);
		return (1);
	}

	/* Make sure BE's root dataset isn't already mounted somewhere */
	if (zfs_is_mounted(zhp, &mp)) {
		be_print_err(gettext("be_mount: %s is already mounted "
		    "at %s\n"), bt.obe_name, mp != NULL ? mp : "");
		if (mp != NULL)
			free(mp);
		return (1);
	}

	/*
	 * The BE's root dataset isn't mounted, grab where its mountpoint
	 * property is currently set to.
	 */
	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) != 0) {
		be_print_err(gettext("be_mount: failed to get mountpoint "
		    "of %s\n"), bt.obe_name);
		return (1);
	}

	/*
	 * Set the canmount property for BE's root filesystem to 'noauto' just
	 * incase it's been set to 'on'  We do this so that when we change
	 * its mountpoint, zfs won't immediately try to mount it.
	 */
	if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_CANMOUNT), "noauto")) {
		be_print_err(gettext("be_mount: failed to "
		    "set canmount to 'noauto' (%s)\n"), bt.obe_root_ds);
		return (1);
	}

	/*
	 * First check that the BE's root dataset is set to 'legacy'.  If it's
	 * not, we're in a situation where an unmounted BE has some random
	 * mountpoint set for it.  (This could happen if the system was
	 * rebooted while an inactive BE was mounted).  We need to try to fix
	 * its mountpoints before proceeding.
	 */
	if (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) != 0) {

		/*
		 * Iterate through this BE's children datasets and fix them
		 * if they need fixing.
		 */
		if (zfs_iter_filesystems(zhp, fix_mountpoint_callback,
		    mountpoint) != 0) {
			return (1);
		}

		/* Set the BE's root dataset back to 'legacy' */
		if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
		    ZFS_MOUNTPOINT_LEGACY) != 0) {
			be_print_err(gettext("be_mount: failed to "
			    "set mountpoint for BE's root dataset "
			    "to 'legacy' (%s)\n"), bt.obe_root_ds);
			return (1);
		}
	}

	/* Set mountpoint for BE's root filesystem */
	if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), altroot)) {
		be_print_err(gettext("be_mount: failed to "
		    "set mountpoint of %s to %s: %s\n"), bt.obe_root_ds,
		    altroot, libzfs_error_description(g_zfs));
		return (1);
	}

	/* Mount the BE's root filesystem */
	if (zfs_mount(zhp, NULL, 0)) {
		be_print_err(gettext("be_mount: failed to "
		    "mount dataset %s at %s: %s\n"), bt.obe_root_ds, altroot,
		    libzfs_error_description(g_zfs));
		/*
		 * Set this BE's root filesystem 'mountpoint' property
		 * back to 'legacy'
		 */
		zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
		    ZFS_MOUNTPOINT_LEGACY);

		return (1);
	}

	/* Iterate through BE's children filesystems */
	if (zfs_iter_filesystems(zhp, be_mount_callback, altroot) != 0) {
		be_print_err(gettext("be_mount: failed to "
		    "mount BE (%s) on %s\n"), bt.obe_name, altroot);
		return (1);
	}

	/* Check mount flag to see if we should mount shared file systems */
	if (flags & BE_MOUNT_FLAG_SHARED_FS) {
		/*
		 * Mount all ZFS file systems not under the BE's root dataset
		 */
		md.altroot = altroot;
		md.mount_flags = flags;
		(void) zpool_iter(g_zfs, zpool_shared_fs_callback, &md);

		/* TODO: Mount all non-ZFS file systems - Not supported yet */
	}

	/* TODO: Mount all zones - Not supported yet */

	zfs_close(zhp);

	return (0);
}

/*
 * Function:	_be_unmount
 * Description:	Unmount a BE.
 * Parameters:
 *		be_name - pointer to name of BE to unmount.
 * Returns:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
_be_unmount(char *be_name)
{
	be_transaction_data_t	bt = { 0 };
	zfs_handle_t	*zhp;
	char		obe_root_ds[MAXPATHLEN];
	char		mountpoint[MAXPATHLEN];
	char		*mp = NULL;
	int		ret;

	if (be_name == NULL)
		return (1);

	/* Set be_name as obe_name in bt structure */
	bt.obe_name = be_name;

	/* Find which zpool obe_name lives in */
	if ((ret = zpool_iter(g_zfs, be_find_zpool_callback, &bt)) == 0) {
		be_print_err(gettext("be_unmount: failed to "
		    "find zpool for BE (%s)\n"), bt.obe_name);
		return (1);
	} else if (ret < 0) {
		be_print_err(gettext("be_unmount: "
		    "zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		return (1);
	}

	/* Generate string for obe_name's root dataset */
	be_make_root_ds(bt.obe_zpool, bt.obe_name, obe_root_ds,
	    sizeof (obe_root_ds));
	bt.obe_root_ds = obe_root_ds;

	/* Get handle to BE's root dataset */
	if ((zhp = zfs_open(g_zfs, bt.obe_root_ds, ZFS_TYPE_FILESYSTEM)) ==
	    NULL) {
		be_print_err(gettext("be_unmount: failed to "
		    "open BE root dataset (%s)\n"), bt.obe_root_ds);
		return (1);
	}

	/* Make sure BE's root dataset is mounted somewhere */
	if (!zfs_is_mounted(zhp, &mp)) {
		
		be_print_err(gettext("be_unmount: "
		    "(%s) not mounted\n"), bt.obe_name);

		/*
		 * BE is not mounted, make sure its root dataset is set to
		 * 'legacy'.  If its not, we're in a situation where an
		 * unmounted BE has some random mountpoint set for it.  (This
		 * could happen if the system was rebooted while an inactive
		 * BE was mounted).  We need to try to fix its mountpoints.
		 */

		/*
		 * Grab what this BE's mountpoint property is currently set to.
		 */
		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
		    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) != 0) {
			be_print_err(gettext("be_unmount: failed to get "
			    "mountpoint of (%s)\n"), bt.obe_name);
			return (1);
		}

		if (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) != 0) {
			/*
			 * Iterate through this BE's children datasets and fix
			 * them if they need fixing.
			 */
			if (zfs_iter_filesystems(zhp, fix_mountpoint_callback,
			    mountpoint) != 0) {
				return (1);
			}

			/* Set the BE's root dataset back to 'legacy' */
			if (zfs_prop_set(zhp,
			    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
			    ZFS_MOUNTPOINT_LEGACY) != 0) {
				be_print_err(gettext("be_unmount: failed to "
				    "set mountpoint for BE's root "
				    "dataset to 'legacy' (%s)\n"),
				    bt.obe_root_ds);
				return (1);
			}
		}
		return (1);
	}

	/*
	 * If we didn't get a mountpoint from the zfs_is_mounted call,
	 * try and get it from its property.
	 */
	if (mp == NULL) {
		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
		    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) != 0) {
			be_print_err(gettext("be_unmount: failed to "
			    "get mountpoint of (%s)\n"), bt.obe_name);
			return (1);
		}
	} else {
		(void) strlcpy(mountpoint, mp, sizeof (mountpoint));
		free(mp);
	}

	/* If BE mounted as current root, fail */
	if (strcmp(mountpoint, "/") == 0) {
		be_print_err(gettext("be_unmount: "
		    "cannot unmount currently running BE\n"));
		return (1);
	}

	/* TODO: Unmount all zones - Not supported yet */

	/* TODO: Unmount all non-ZFS file systems - Not supported yet */

	/* Unmount all ZFS file systems not under the BE root dataset */
	if (unmount_shared_fs(mountpoint) != 0) {
		be_print_err(gettext("be_unmount: failed to "
		    "unmount shared file systems\n"));
		return (1);
	}

	/* Unmount all children datasets under the BE's root dataset */
	if (zfs_iter_filesystems(zhp, be_unmount_callback, mountpoint) != 0) {
		be_print_err(gettext("be_unmount: failed to "
		    "unmount BE (%s)\n"), bt.obe_name);
		return (1);
	}

	/* Unmount this BE's root filesystem */
	if (zfs_unmount(zhp, NULL, 0) != 0) {
		be_print_err(gettext("be_unmount: failed to "
		    "unmount (%s)\n"), bt.obe_root_ds);
		return (1);
	}

	/* Set canmount property for this BE's root filesystem to noauto */
	if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_CANMOUNT), "noauto")) {
		be_print_err(gettext("be_unmount: failed to "
		    "set canmount to 'noauto' (%s)\n"),
		    bt.obe_root_ds);
		return (1);
	}

	/* Set mountpoint for BE's root dataset back to legacy */
	if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
	    ZFS_MOUNTPOINT_LEGACY)) {
		be_print_err(gettext("be_unmount: failed to "
		    "set mountpoint of %s to 'legacy'\n"), bt.obe_root_ds);
		return (1);
	}

	zfs_close(zhp);

	return (0);
}

/* ********************************************************************	*/
/*			Private Functions				*/
/* ********************************************************************	*/

/*
 * Function:	be_mount_callback
 * Description:	Callback function used to iterate through all of a BE's
 *		subordinate file systems and to mount them accordingly.
 * Parameters:
 *		zhp - zfs_handle_t pointer to current file system being
 *			processed.
 *		data - pointer to the altroot of where to mount BE.
 * Returns:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Private
 */
static int
be_mount_callback(zfs_handle_t *zhp, void *data)
{
	zprop_source_t	sourcetype;
	char		*fs_name = (char *)zfs_get_name(zhp);
	char		source[ZFS_MAXNAMELEN];
	char		*altroot = data;
	char		zhp_mountpoint[MAXPATHLEN];
	char		mountpoint[MAXPATHLEN];

	/* Get dataset's mountpoint and source values */
	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, zhp_mountpoint,
	    sizeof (zhp_mountpoint), &sourcetype, source, sizeof (source),
	    B_FALSE) != 0) {
		be_print_err(gettext("be_mount_callback: failed to "
		    "get mountpoint and sourcetype for %s\n"),
		    fs_name);
		return (1);
	}

	/*
	 * Set this filesystem's 'canmount' property to 'noauto' just incase
	 * it's been set 'on'.  We do this so that when we change its
	 * mountpoint zfs won't immediately try to mount it.
	 */
	if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_CANMOUNT), "noauto")) {
		be_print_err(gettext("be_mount_callback: failed to "
		    "set canmount to 'noauto' (%s)\n"), fs_name);
		return (1);
	}

	/*
	 * If the mountpoint is inherited, its mountpoint should
	 * already be set.  If it's not, then explicitly fix-up
	 * the mountpoint now by appending its explicitly set
	 * mountpoint value to the BE mountpoint.
	 */
	if (sourcetype & ZPROP_SRC_INHERITED) {
		/*
		 * If the mountpoint is inherited, its parent should have
		 * already been processed so its current mountpoint value
		 * is what its mountpoint ought to be.
		 */
		(void) strlcpy(mountpoint, zhp_mountpoint, sizeof (mountpoint));
	} else if (sourcetype & ZPROP_SRC_LOCAL) {

		if (strcmp(zhp_mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0) {
			/*
			 * If the mountpoint is set to 'legacy', we need to
			 * dig into this BE's vfstab to figure out where to
			 * mount it, and just mount it via mount(2).
			 */
			if (get_mountpoint_from_vfstab(altroot, fs_name,
			    mountpoint, sizeof (mountpoint), B_TRUE) == 0) {
				
				/* Legacy mount the file system */
				if (mount(fs_name, mountpoint, MS_DATA,
				    MNTTYPE_ZFS, NULL, 0, NULL, 0) != 0) {
					be_print_err(
					    gettext("be_mount_callback: "
					    "failed to mount %s on %s\n"),
					    fs_name, mountpoint);
				}
			} else {
				be_print_err(
				    gettext("be_mount_callback: "
				    "failed to get entry for %s in vfstab, "
				    "skipping ...\n"), fs_name);
			}

			goto next;
		} else {
			/*
			 * Else process dataset with explicitly set mountpoint.
			 */
			(void) snprintf(mountpoint, sizeof (mountpoint),
			    "%s%s", altroot, zhp_mountpoint);
 
			/* Set the new mountpoint for the dataset */
			if (zfs_prop_set(zhp,
			    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
			    mountpoint)) {
				be_print_err(gettext("be_mount_callback: "
				    "failed to set mountpoint for %s to "
				    "%s\n"), fs_name, mountpoint);
				return (1);
			}
		}
	} else {
		be_print_err(gettext("be_mount_callback: "
		    "mountpoint sourcetype of %s is %d, skipping ...\n"),
		    fs_name, sourcetype);

		goto next;
	}

	/* Mount this filesystem */
	if (zfs_mount(zhp, NULL, 0)) {
		be_print_err(gettext("be_mount_callback: failed to "
		    "mount dataset %s at %s: %s\n"), fs_name, mountpoint,
		    libzfs_error_description(g_zfs));
		/*
		 * Set this filesystem's 'mountpoint' property back to what
		 * it was
		 */
		if (sourcetype & ZPROP_SRC_LOCAL &&
		    strcmp(zhp_mountpoint, ZFS_MOUNTPOINT_LEGACY) != 0) {
			zfs_prop_set(zhp,
			    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
			    zhp_mountpoint);
		}

		return (1);
	}

next:
	/* Iterate through this dataset's children and mount them */
	if (zfs_iter_filesystems(zhp, be_mount_callback, altroot)) {
		return (1);
	}

	return (0);
}

/*
 * Function:	be_unmount_callback
 * Description:	Callback function used to iterate through all of a BE's
 *		subordinate file systems and to unmount them.
 * Parameters:
 *		zhp - zfs_handle_t pointer to current file system being
 *			processed.
 *		data - pointer to the mountpoint of where BE is mounted.
 * Returns:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Private
 */
static int
be_unmount_callback(zfs_handle_t *zhp, void *data)
{
	zprop_source_t	sourcetype;
	const char	*fs_name = zfs_get_name(zhp);
	char		source[ZFS_MAXNAMELEN];
	char		*altroot = data;
	char		mountpoint[MAXPATHLEN];
	char		*zhp_mountpoint;
	int		ret = 0;

	/* Iterate down this dataset's children first */
	if (zfs_iter_filesystems(zhp, be_unmount_callback, altroot)) {
		ret = 1;
	}

	/* Is dataset even mounted ? */
	if (!zfs_is_mounted(zhp, NULL))
		goto done;

	/* Unmount this file system */
	if (zfs_unmount(zhp, NULL, 0) != 0) {
		be_print_err(gettext("be_unmount_callback: "
		    "failed to unmount %s: %s\n"), fs_name,
		    libzfs_error_description(g_zfs));
		ret = 1;
	}

	/* Get dataset's current mountpoint and source value */
	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), &sourcetype, source, sizeof (source),
	    B_FALSE) != 0) {
		be_print_err(gettext("be_unmount_callback: "
		    "failed to get mountpoint and sourcetype for %s\n"),
		    fs_name);
		ret = 1;
		goto done;
	}

	if (sourcetype & ZPROP_SRC_INHERITED) {
		/*
		 * If the mountpoint is inherited we don't need to
		 * do anything.  When its parent gets processed
		 * its mountpoint will be set accordingly.
		 */
		goto done;
	} else if (sourcetype & ZPROP_SRC_LOCAL) {

		if (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) == 0) {
			/*
			 * If the mountpoint is set to 'legacy', its already
			 * been unmounted (from above call to zfs_unmount), and
			 * we don't need to do anything else with it.
			 */
			goto done;

		} else {
			/*
			 * Else process dataset with explicitly set mountpoint.
			 */

			/*
			 * Get this dataset's mountpoint relative to
			 * the BE's mountpoint.
			 */
			if (strncmp(mountpoint, altroot, strlen(altroot)) == 0
			    && mountpoint[strlen(altroot)] == '/') {

				zhp_mountpoint = mountpoint +
				    strlen(altroot);

				/* Set this dataset's mountpoint value */
				if (zfs_prop_set(zhp,
				    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
				    zhp_mountpoint)) {
					be_print_err(
					    gettext("be_unmount_callback: "
					    "failed to set mountpoint for "
					    "%s to %s\n"), fs_name,
					    zhp_mountpoint);
					ret = 1;
				}
			} else {
				be_print_err(
				    gettext("be_unmount_callback: "
				    "%s not mounted under BE's altroot %s, "
				    "skipping ...\n"), fs_name, altroot);
				ret = 1;
			}
		}
	} else {
		be_print_err(gettext("be_unmount_callback: "
		    "mountpoint sourcetype of %s is %d, skipping ...\n"),
		    fs_name, sourcetype);
		ret = 1;
	}

done:
	/* Set this filesystem's 'canmount' property to 'noauto' */
	if (zfs_prop_set(zhp, zfs_prop_to_name(ZFS_PROP_CANMOUNT), "noauto")) {
		be_print_err(gettext("be_unmount_callback: "
		    "failed to set canmount to 'noauto' (%s)\n"), fs_name);
		ret = 1;
	}

	return (ret);
}

/*
 * Function:	zpool_shared_fs_callback
 * Description:	Callback function used to iterate through all existing pools
 *		to find and mount all shared filesystems.  This function
 *		processes the pool's "pool data" dataset, then uses
 *		iter_shared_fs_callback to iterate through the pool's
 *		datasets.
 * Parameters:
 *		zlp - zpool_handle_t pointer to the current pool being
 *			looked at.
 *		data - be_mount_data_t pointer
 * Returns:
 *		0 - Success
 *		1 - Failure
 * Scope:
 *		Private
 */
static int
zpool_shared_fs_callback(zpool_handle_t *zlp, void *data)
{
	be_mount_data_t	*md = data;
	zfs_handle_t	*zhp = NULL;
	const char	*zpool = zpool_get_name(zlp);
	char		ds[MAXPATHLEN];

	/*
	 * Get handle to pool's "pool data" dataset
	 */
	if ((zhp = zfs_open(g_zfs, zpool, ZFS_TYPE_FILESYSTEM)) == NULL) {
		be_print_err(gettext("zpool_shared_fs: "
		    "failed to open pool dataset %s: %s\n"), zpool,
		    libzfs_error_description(g_zfs));
		return (1);
	}

	/* Process this pool's "pool data" dataset */
	(void) loopback_mount_shared_fs(zhp, md);

	/* Interate through this pool's children */
	(void) zfs_iter_filesystems(zhp, iter_shared_fs_callback, md);

	zfs_close(zhp);

	return (0);
}

/*
 * Function:	iter_shared_fs_callback
 * Description:	Callback function used to iterate through a pool's datasets
 *		to find and mount all shared filesystems.  It makes sure to
 *		find the BE container dataset of the pool, if it exists, and
 *		does not process and iterate down that path.
 *
 *		Note - This function iterates linearly down the
 *		hierarchical dataset paths and mounts things as it goes
 *		along.  It does not make sure that something deeper down
 *		a dataset path has an interim mountpoint for something
 *		processed earlier.
 *
 * Parameters:
 *		zhp - zfs_handle_t pointer to the current dataset being
 *			processed.
 *		data - be_mount_data_t pointer
 * Returns:
 *		0 - Success
 *		1 - Failure
 * Scope:
 *		Private
 */
static int
iter_shared_fs_callback(zfs_handle_t *zhp, void *data)
{
	be_mount_data_t	*md = data;
	const char	*name = zfs_get_name(zhp);
	char		container_ds[MAXPATHLEN];
	char		tmp_name[MAXPATHLEN];
	char		*pool;

	/* Get the pool's name */
	strlcpy(tmp_name, name, sizeof (tmp_name));
	pool = strtok(tmp_name, "/");

	if (pool) {
		/* Get the name of this pool's container dataset */
		be_make_container_ds(pool, container_ds,
		    sizeof (container_ds));

		/*
		 * If what we're processing is this pool's BE container
		 * dataset, skip it.
		 */
		if (strcmp(name, container_ds) == 0)
			return (0);
	} else {
		/* Getting the pool name failed, return error */
		be_print_err(gettext("iter_shared_fs_callback: "
		    "failed to get pool name from %s\n"), name);
		return (1);
	}

	/* Mount this shared filesystem */
	(void) loopback_mount_shared_fs(zhp, md);
	
	/* Iterate this dataset's children file systems */
	(void) zfs_iter_filesystems(zhp, iter_shared_fs_callback, md);

	return (0);
}

/*
 * Function:	loopback_mount_shared_fs
 * Description:	This function loopback mounts a file system into the altroot
 *		area of the BE being mounted.  Since these are shared file
 *		systems, they are expected to be already mounted for the
 *		current BE, and this function just loopback mounts them into
 *		the BE mountpoint.  If they are not mounted for the current
 *		live system, they are skipped and not mounted into the BE
 *		we're mounting.
 * Parameters:
 *		zhp - zfs_handle_t pointer to the dataset to loopback mount
 *		md - be_mount_data_t pointer
 * Returns:
 *		0 - Success
 *		1 - Failure
 * Scope:
 *		Private
 */
static int
loopback_mount_shared_fs(zfs_handle_t *zhp, be_mount_data_t *md)
{
	char		zhp_mountpoint[MAXPATHLEN];
	char		mountpoint[MAXPATHLEN];
	char		*mp = NULL;
	int		mflag = 0;
	int		errno;

	/* Check if file system is currently mounted. */
	if (zfs_is_mounted(zhp, &mp)) {
		/*
		 * If we didn't get a mountpoint from the zfs_is_mounted call,
		 * get it from the mountpoint property.
		 */
		if (mp == NULL) {
			if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT,
			    zhp_mountpoint, sizeof (zhp_mountpoint), NULL,
			    NULL, 0, B_FALSE) != 0) {
				be_print_err(
				    gettext("loopback_mount_shared_fs: "
				    "failed to get mountpoint property\n"));
				return (1);
			}
		} else {
			(void) strlcpy(zhp_mountpoint, mp,
			    sizeof (zhp_mountpoint));
			free(mp);
		}

		(void) snprintf(mountpoint, sizeof (mountpoint), "%s%s",
		    md->altroot, zhp_mountpoint);

		/*
		 * Loopback mount this dataset at the altroot.  Mount it
		 * read-write if specified to, otherwise mount it read-only.
		 */
		if (md->mount_flags & BE_MOUNT_FLAG_SHARED_RW) {
			mflag = MS_DATA;
		} else {
			mflag = MS_DATA | MS_RDONLY;
		}

		if (mount(zhp_mountpoint, mountpoint, mflag, MNTTYPE_LOFS,
		    NULL, 0, NULL, 0) != 0) {
			be_print_err(gettext("loopback_mount_shared_fs: "
			    "failed to loopback mount %s at %s: %s\n"),
			    zhp_mountpoint, mountpoint, strerror(errno));
			return (1);
		}
	}

	return (0);
}

/*
 * Function:	unmount_shared_fs
 * Description:	This function iterates through the mnttab and finds all
 *		loopback mount entries that reside within the altroot of
 *		where the BE is mounted, and unmounts it.
 * Parameters:
 *		altroot - location of where the BE that we're unmounting
 *			is currently mounted.
 * Returns:
 *		0 - Success
 *		1 - Failure
 * Scope:
 *		Private
 */
static int
unmount_shared_fs(char *altroot)
{
	FILE		*fp = NULL;
	struct mnttab	*table = NULL;
	struct mnttab	ent;
	struct mnttab	*entp = NULL;
	size_t		size = 0;
	int		read_chunk = 32;
	int		i;
	int		altroot_len;
	int		ret = 0;
	int		errno;

	/* Read in the mnttab into a table */
	if ((fp = fopen(MNTTAB, "r")) == NULL) {
		be_print_err(gettext("unmount_shared_fs: "
		    "failed to open mnttab\n"));
		return (1);
	}

	while (getmntent(fp, &ent) == 0) {
		if (size % read_chunk == 0) {
			table = (struct mnttab *) realloc(table,
			    (size + read_chunk) * sizeof (ent));
		}
		entp = &table[size++];

		/*
		 * Copy over the current mnttab entry into our table,
		 * copying only the fields that we care about.
		 */
		(void) memset(entp, 0, sizeof (*entp));
		entp->mnt_mountp = (char *) strdup(ent.mnt_mountp);
		entp->mnt_fstype = (char *) strdup(ent.mnt_fstype);
	}
	(void) fclose(fp);

	/*
	 * Process the mnttab entries in reverse order, looking for
	 * loopback mount entries mounted under our altroot.
	 */
	altroot_len = strlen(altroot);
	for (i = size; i > 0; i--) {
		entp = &table[i - 1];

		/* If not of type lofs, skip */
		if (strcmp(entp->mnt_fstype, MNTTYPE_LOFS) != 0)
			continue;

		/* If inside the altroot, unmount it */
		if (strncmp(entp->mnt_mountp, altroot, altroot_len) == 0 &&
		    entp->mnt_mountp[altroot_len] == '/') {
			if (umount(entp->mnt_mountp) != 0) {
				be_print_err(gettext("unmount_shared_fs: "
				    "failed to unmount shared file system %s: "
				    "%s\n"), entp->mnt_mountp, strerror(errno));
				ret = 1;
			}
 		}
	}

	return (ret);
}

/*
 * Function:	get_mountpoint_from_vfstab
 * Description:	This function digs into the vfstab in the given altroot,
 *		and searches for an entry for the fs passed in.  If found,
 *		it returns the mountpoint of that fs in the mountpoint
 *		buffer passed in.  If the get_alt_mountpoint flag is set,
 *		it returns the mountpoint with the altroot prepended.
 * Parameters:
 *		altroot - pointer to the alternate root location
 *		fs - pointer to the file system name to look for in the
 *			vfstab in altroot
 *		mountpoint - pointer to buffer of where the mountpoint of
 *			fs will be returned.
 *		size_mp - size of mountpoint argument
 *		get_alt_mountpoint - flag to indicate whether or not the
 *			mountpoint should be populated with the altroot
 *			prepended.
 * Returns:
 *		0 - Success
 *		1 - Failure
 * Scope:
 *		Private
 */
static int
get_mountpoint_from_vfstab(char *altroot, char *fs, char *mountpoint,
    size_t size_mp, boolean_t get_alt_mountpoint)
{
	struct vfstab	vp;
	FILE		*fp = NULL;
	char		alt_vfstab[MAXPATHLEN];
	int		ret = 0;

	/* Generate path to alternate root vfstab */
	(void) snprintf(alt_vfstab, sizeof (alt_vfstab), "%s/etc/vfstab",
	    altroot);

	/* Open alternate root vfstab */
	if ((fp = fopen(alt_vfstab, "r")) == NULL) {
		be_print_err(gettext("get_mountpoint_from_vfstab: "
		    "failed to open vfstab (%s)\n"), alt_vfstab);
		return (1);
	}

	if (getvfsspec(fp, &vp, fs) == 0) {
		/*
		 * Found entry for fs, grab its mountpoint.
		 * If the flag to prepend the altroot into the mountpoint
		 * is set, prepend it.  Otherwise, just return the mountpoint.
		 */
		if (get_alt_mountpoint) {
			(void) snprintf(mountpoint, size_mp, "%s%s", altroot,
			    vp.vfs_mountp);
		} else {
			(void) strlcpy(mountpoint, vp.vfs_mountp, size_mp);
		}
	} else {
		(void) fclose(fp);
		return (1);
	}

	(void) fclose(fp);

	return (0);
}

/*
 * Function:	fix_mountpoint_callback
 * Description:	This callback function is used to iterate through a BE's
 *		children filesystems to check if its mountpoint is currently
 *		set to be mounted at some specified altroot.  If so, fix it by
 *		removing altroot from the beginning of its mountpoint.
 *
 *		Note - There's no way to tell if a child filesystem's
 *		mountpoint isn't broken, and just happens to begin with
 *		the altroot we're looking for.  In this case, this function
 *		will errantly remove the altroot portion from the beginning
 *		of this filesystem's mountpoint. 
 *
 * Parameters:
 *		zhp - zfs_handle_t pointer to filesystem being processed.
 *		data - altroot of where BE is to be mounted.
 * Returns:
 *		0 - Success
 *		1 - Failure
 * Scope:
 *		Private
 */
static int
fix_mountpoint_callback(zfs_handle_t *zhp, void *data)
{
	zprop_source_t	sourcetype;
	char		source[ZFS_MAXNAMELEN];
	char		mountpoint[MAXPATHLEN];
	char		*zhp_mountpoint = NULL;
	char		*altroot = data;

	/* Get dataset's mountpoint and source values */
	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), &sourcetype, source, sizeof (source),
	    B_FALSE) != 0) {
		be_print_err(gettext("fix_mountpoint_callback: "
		    "failed to get mountpoint and sourcetype for %s\n"),
		    zfs_get_name(zhp));
		return (1);
	}

	/*
	 * If the mountpoint is not inherited and the mountpoint is not
	 * 'legacy', this file system potentially needs its mountpoint
	 * fixed.
	 */
	if (!(sourcetype & ZPROP_SRC_INHERITED) &&
	    strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) != 0) {

		/*
		 * Check if this file system's current mountpoint is
		 * under the altroot we're fixing it against.
		 */
		if (strncmp(mountpoint, altroot, strlen(altroot)) == 0 &&
		    mountpoint[strlen(altroot)] == '/') {

			/*
			 * Get this dataset's mountpoint relative to the
			 * altroot.
			 */
			zhp_mountpoint = mountpoint + strlen(altroot);

			/* Fix this dataset's mountpoint value */
			if (zfs_prop_set(zhp,
			    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
			    zhp_mountpoint)) {
				be_print_err(gettext("fix_mountpoint_callback: "
				    "failed to set mountpoint for %s to %s\n"),
				    zfs_get_name(zhp), zhp_mountpoint);

				return (1);
			}
		}
	}

	/* Iterate through this dataset's children and fix them */
	if (zfs_iter_filesystems(zhp, fix_mountpoint_callback, altroot)) {
		return (1);
	}

	return (0);
}