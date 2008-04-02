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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "orchestrator_private.h"

extern boolean_t	whole_disk = B_FALSE; /* assume existing partition */


/* ----------------- definition of private functions ----------------- */

/*
 * is_resized_partition
 * This function checks if partition size changed
 *
 * Input:	pold, pnew - pointer to the old and new partition entries
 *
 * Return:	B_TRUE - partition was resized
 *		B_FALSE - partition size not chnged
 */

static boolean_t
is_resized_partition(partition_info_t *pold, partition_info_t *pnew)
{
	return (pold->partition_size != pnew->partition_size ?
	    B_TRUE : B_FALSE);
}


/*
 * is_changed_partition
 * This function checks if partition changed. It means that
 * [1] size changed OR
 * [2] type changed for used partition (size is not 0)
 *
 * Input:	pold, pnew - pointer to the old and new partition entries
 *
 * Return:	B_TRUE - partition was resized
 *		B_FALSE - partition size not chnged
 */

static boolean_t
is_changed_partition(partition_info_t *pold, partition_info_t *pnew)
{
	return (is_resized_partition(pold, pnew) ||
	    (pold->partition_type != pnew->partition_type &&
	    pnew->partition_size != 0) ? B_TRUE : B_FALSE);
}


/*
 * is_deleted_partition
 * This function checks if partition was deleted
 *
 * Input:	pold, pnew - pointer to the old and new partition entries
 *
 * Return:	B_TRUE - partition was deleted
 *		B_FALSE - partition was not deleted
 */

static boolean_t
is_deleted_partition(partition_info_t *pold, partition_info_t *pnew)
{
	return (pold->partition_size != 0 && pnew->partition_size == 0 ?
	    B_TRUE : B_FALSE);
}


/*
 * is_created_partition
 * This function checks if partition was created
 *
 * Input:	pold, pnew - pointer to the old and new partition entries
 *
 * Return:	B_TRUE - partition was created
 *		B_FALSE - partition already existed
 */

static boolean_t
is_created_partition(partition_info_t *pold, partition_info_t *pnew)
{
	return (pold->partition_size == 0 && pnew->partition_size != 0 ?
	    B_TRUE : B_FALSE);
}


/*
 * is_used_partition
 * This function checks if partition_info_t structure
 * describes used partition entry.
 *
 * Entry is considered to be used if
 * [1] partition size is greater than 0
 * [2] type is not set to unused - id 0 or 100
 *
 * Input:	pentry - pointer to the partition entry
 *
 * Return:	B_TRUE - partition entry is in use
 *		B_FALSE - partition entry is empty
 */

static boolean_t
is_used_partition(partition_info_t *pentry)
{
	if ((pentry->partition_type != 0) && (pentry->partition_type != 100) &&
	    (pentry->partition_size != 0))
		return (B_TRUE);
	else
		return (B_FALSE);
}

/*
 * get_first_used_partition
 * This function will search array of partition_info_t structures
 * and will find the first used entry
 *
 * see is_used_partition() for how emtpy partition is defined
 *
 * Input:	pentry - pointer to the array of partition entries
 *
 * Output:	None
 *
 * Return:	>=0	- index of first used partition entry
 *		-1	- array contains only empty entries
 */

static int
get_first_used_partition(partition_info_t *pentry)
{
	int i;

	for (i = 0; i < OM_NUMPART; i++) {
		if (is_used_partition(&pentry[i]))
			return (i);
	}

	return (-1);
}

/*
 * get_last_used_partition
 * This function will search array of partition_info_t structures
 * and will find the last used entry
 *
 * see is_used_partition() for how empty partition is defined
 *
 * Input:	pentry - pointer to the array of partition entries
 *
 * Output:	None
 *
 * Return:	>=0	- index of first used partition entry
 *		-1	- array contains only empty entries
 */

static int
get_last_used_partition(partition_info_t *pentry)
{
	int i;

	for (i = OM_NUMPART - 1; i >= 0; i--) {
		if (is_used_partition(&pentry[i]))
			return (i);
	}

	return (-1);
}

/*
 * get_next_used_partition
 * This function will search array of partition_info_t structures
 * and will find the next used entry
 *
 * Input:	pentry - pointer to the array of partition entries
 *		current - current index
 *
 * Output:	None
 *
 * Return:	>=0	- index of next used partition entry
 *		-1	- no more used entries
 */

static int
get_next_used_partition(partition_info_t *pentry, int current)
{
	int i;

	for (i = current + 1; i < OM_NUMPART; i++) {
		if (is_used_partition(&pentry[i]))
			return (i);
	}

	return (-1);
}


/*
 * get_previous_used_partition
 * This function will search array of partition_info_t structures
 * and will find the previous used entry
 *
 * Input:	pentry - pointer to the array of partition entries
 *		current - current index
 *
 * Output:	None
 *
 * Return:	>=0	- index of next used partition entry
 *		-1	- no more used entries
 */

static int
get_previous_used_partition(partition_info_t *pentry, int current)
{
	int i;

	for (i = current - 1; i >= 0; i--) {
		if (is_used_partition(&pentry[i]))
			return (i);
	}

	return (-1);
}


/*
 * is_first_used_partition
 * This function checks if index points
 * to the first used partition entry.
 *
 * Input:	pentry	- pointer to the array of partition entries
 *		index	- index of partition to be checked
 *
 * Return:	B_TRUE - partition entry is in use
 *		B_FALSE - partition entry is empty
 */

static boolean_t
is_first_used_partition(partition_info_t *pentry, int index)
{
	return (get_first_used_partition(pentry) == index ?
	    B_TRUE : B_FALSE);
}

/*
 * is_last_used_partition
 * This function checks if index points to
 * the last used partition entry.
 *
 * Input:	pentry	- pointer to the array of partition entries
 *		index	- index of partition to be checked
 *
 * Return:	B_TRUE - partition entry is in use
 *		B_FALSE - partition entry is empty
 */

static boolean_t
is_last_used_partition(partition_info_t *pentry, int index)
{
	return (get_last_used_partition(pentry) == index ?
	    B_TRUE : B_FALSE);
}


/* ----------------- definition of public functions ----------------- */

/*
 * om_get_disk_partition_info
 * This function will return the partition information of the specified disk.
 * Input:	om_handle_t handle - The handle returned by
 *		om_initiate_target_discovery()
 * 		char *diskname - The name of the disk
 * Output:	None
 * Return:	disk_parts_t * - The fdisk partition information for
 *		the disk with diskname will be 	returned. The space will be
 *		allocated here linked and returned to the caller.
 *		NULL - if the partition data can't be returned.
 */
/*ARGSUSED*/
disk_parts_t *
om_get_disk_partition_info(om_handle_t handle, char *diskname)
{
	disk_parts_t	*dp;

	/*
	 * Validate diskname
	 */
	if (diskname == NULL || diskname[0] == '\0') {
		om_set_error(OM_BAD_DISK_NAME);
		return (NULL);
	}

	/*
	 * If the discovery is not yet completed or not started,
	 * return error
	 */
	if (!disk_discovery_done) {
		om_set_error(OM_DISCOVERY_NEEDED);
		return (NULL);
	}

	if (system_disks == NULL) {
		om_set_error(OM_NO_DISKS_FOUND);
		return (NULL);
	}

	/*
	 * Find the disk from the cache using the diskname
	 */
	dp = find_partitions_by_disk(diskname);
	if (dp == NULL) {
		return (NULL);
	}

	/*
	 * copy the data
	 */
	return (om_duplicate_disk_partition_info(handle, dp));
}

/*
 * om_free_disk_partition_info
 * This function will free up the disk information data allocated during
 * om_get_disk_partition_info().
 * Input:	om_handle_t handle - The handle returned by
 *		om_initiate_target_discovery()
 *		disk_parts_t *dinfo - The pointer to disk_parts_t. Usually
 *		returned by om_get_disk_partition_info().
 * Output:	None.
 * Return:	None.
 */
/*ARGSUSED*/
void
om_free_disk_partition_info(om_handle_t handle, disk_parts_t *dpinfo)
{
	if (dpinfo == NULL) {
		return;
	}

	local_free_part_info(dpinfo);
}

/*
 * om_validate_and_resize_disk_partitions
 * This function will check whether the the partition information of the
 * specified disk is valid. If the reuqested space can't be allocated, then
 * suggested partition allocation will be returned.
 * Input:	disk_parts_t *dpart
 * Output:	None
 * Return:	disk_parts_t *, with the corrected values. If the sizes
 *		are valide, the return disk_parts_t structure will have same
 *		data as the the	disk_parts_t structure passed as the input.
 *		NULL, if the partition data is invalid.
 * Note:	If the partition is not valid, the om_errno will be set to
 *		the actual error condition. The error information can be
 *		obtained by calling om_get_errno().
 *
 *		This function checks:
 *		- Whether the total partition space is < disk space
 *		- There is enough space to create a new parition
 *		- if there are holes between partitions, can the new
 *		partition fitted with in any of the holes.
 */
/*ARGSUSED*/
disk_parts_t *
om_validate_and_resize_disk_partitions(om_handle_t handle, disk_parts_t *dpart)
{
	disk_target_t	*dt;
	disk_parts_t	*dp, *new_dp;
	int		i;
	boolean_t	changed = B_FALSE;

	/*
	 * validate the input
	 */
	if (dpart == NULL) {
		om_set_error(OM_INVALID_DISK_PARTITION);
		return (NULL);
	}

	/*
	 * Is the target discovery completed?
	 */
	if (!disk_discovery_done) {
		om_set_error(OM_DISCOVERY_NEEDED);
		return (NULL);
	}

	if (system_disks  == NULL) {
		om_set_error(OM_NO_DISKS_FOUND);
		return (NULL);
	}

	if (dpart->disk_name == NULL) {
		om_set_error(OM_INVALID_DISK_PARTITION);
		return (NULL);
	}

	/*
	 * Find the disk from the cache using the diskname
	 */
	dt = find_disk_by_name(dpart->disk_name);
	if (dt == NULL) {
		om_set_error(OM_BAD_DISK_NAME);
		return (NULL);
	}

	/*
	 * Create the disk_parts_t structure to be returned
	 */
	new_dp = om_duplicate_disk_partition_info(handle, dpart);
	if (new_dp == NULL) {
		return (NULL);
	}

	/*
	 * check if "whole disk" path was selected. It is true if
	 * both following conditions are met:
	 * [1] Only first partition is defined. Rest are left unused
	 *	(size ==0 &&, type == 100)
	 * [2] First partition is Solaris2 and occupies all available space
	 */

	whole_disk = B_TRUE;

	if ((new_dp->pinfo[0].partition_size != dt->dinfo.disk_size) ||
	    (new_dp->pinfo[0].partition_type != SUNIXOS2))
		whole_disk = B_FALSE;

	for (i = 1; i < OM_NUMPART && whole_disk == B_TRUE; i++) {
		if ((new_dp->pinfo[i].partition_size != 0) ||
		    (new_dp->pinfo[i].partition_type != 100)) {
			whole_disk = B_FALSE;
		}
	}

	if (whole_disk) {
		return (new_dp);
	}

	/*
	 * if target disk is empty (there are no partitions defined),
	 * create dummy partition configuration. This allows using
	 * unified code for dealing with partition changes.
	 */

	if (dt->dparts == NULL) {
		om_log_print("disk currently doesn't contain any partition\n");

		dp = om_duplicate_disk_partition_info(handle, dpart);

		if (dp == NULL) {
			om_log_print("Couldn't duplicate partition info\n");
			return (NULL);
		}

		(void) memset(dp->pinfo, 0, sizeof (partition_info_t) *
		    OM_NUMPART);
	} else
		dp = dt->dparts;

	/*
	 * Compare the size and partition type (for used partition)
	 * of each partition to decide whether any of them was changed.
	 */

	for (i = 0; i < OM_NUMPART; i++) {
		if (is_changed_partition(&dp->pinfo[i], &new_dp->pinfo[i])) {
			om_log_print("disk partition info changed\n");
			changed = B_TRUE;
			break;
		}
	}

	if (!changed) {
		/*
		 * No change in the partition table.
		 */
		om_log_print("disk partition info not changed\n");

		/* release partition info if allocated if this function */
		if (dt->dparts == NULL)
			local_free_part_info(dp);

		return (new_dp);
	}

	/*
	 * Finally calculate sector geometry information for changed
	 * partitions
	 */

	om_debug_print(OM_DBGLVL_INFO,
	    "Partition LBA information before recalculation\n");

	for (i = 0; i < OM_NUMPART; i++) {
		om_debug_print(OM_DBGLVL_INFO,
		    "[%d] pos=%d, id=%02X, beg=%lld, size=%lld(%ld MiB)\n", i,
		    new_dp->pinfo[i].partition_id,
		    new_dp->pinfo[i].partition_type,
		    new_dp->pinfo[i].partition_offset_sec,
		    new_dp->pinfo[i].partition_size_sec,
		    new_dp->pinfo[i].partition_size);
	}

	for (i = 0; i < OM_NUMPART; i++) {
		partition_info_t	*p_orig = &dp->pinfo[i];
		partition_info_t	*p_new = &new_dp->pinfo[i];

		/*
		 * If the partition was not resized, skip it, since
		 * other modifications (change of type) don't require
		 * offset & size recalculation
		 */

		if (!is_resized_partition(p_orig, p_new))
			continue;

		/*
		 * If partition is deleted (marked as "UNUSED"),
		 * clear offset and size
		 */

		if (is_deleted_partition(p_orig, p_new)) {
			om_debug_print(OM_DBGLVL_INFO,
			    "Partition pos=%d, type=%02X is deleted\n",
			    p_orig->partition_id,
			    p_orig->partition_type);

			p_new->partition_offset_sec =
			    p_new->partition_size_sec = 0;

			/*
			 * don't clear partition_id - it is "read only"
			 * from orchestrator point of view - modified by GUI
			 */
			continue;
		}

		if (is_created_partition(p_orig, p_new)) {
			om_debug_print(OM_DBGLVL_INFO,
			    "Partition pos=%d, type=%02X is created\n",
			    p_new->partition_id, p_new->partition_type);
		}

		/*
		 * Calculate sector offset information
		 *
		 * Gaps are not allowed for now - partition starts
		 * right after previous used partition
		 *
		 * If this is the first partition, it starts at the
		 * first cylinder - adjust size accordingly
		 */

		if (is_first_used_partition(new_dp->pinfo, i)) {
			p_new->partition_offset_sec = dt->dinfo.disk_cyl_size;
			p_new->partition_size -=
			    dt->dinfo.disk_cyl_size/BLOCKS_TO_MB;

			om_debug_print(OM_DBGLVL_INFO,
			    "%d (%02X) is the first partition - "
			    "will start at the 1st cylinder (sector %lld)\n",
			    i, p_new->partition_type,
			    p_new->partition_offset_sec);
		} else {
			partition_info_t	*p_prev;
			int			previous;

			previous = get_previous_used_partition(new_dp->pinfo,
			    i);

			/*
			 * previous should be always found, since check for
			 * "first used" was done in if() statement above
			 */

			assert(previous != -1);
			p_prev = &new_dp->pinfo[previous];

			p_new->partition_offset_sec =
			    p_prev->partition_offset_sec +
			    p_prev->partition_size_sec;

		}

		/*
		 * user changed partition size in GUI or size
		 * was adjusted above.
		 * Calculate new sector size information from megabytes
		 */

		om_set_part_sec_size_from_mb(p_new);

		/*
		 * If the partition overlaps with subsequent one
		 * which is in use and that partition was not changed,
		 * adjust size accordingly.
		 *
		 * If subsequent used partition was resized as well, its
		 * offset and size will be adjusted in next step, so
		 * don't modify size of current partition.
		 *
		 * If this is the last used partition, adjust its size
		 * so that it fits into available disk space
		 */

		if (!is_last_used_partition(new_dp->pinfo, i)) {
			partition_info_t	*p_next_orig, *p_next_new;
			int			next;

			next = get_next_used_partition(new_dp->pinfo, i);

			/*
			 * next should be always found, since check for
			 * "last used" was done in if() statement above
			 */

			assert(next != -1);
			p_next_orig = &dp->pinfo[next];
			p_next_new = &new_dp->pinfo[next];

			/*
			 * next partition was resized, adjust rather that one,
			 * leave current one as is
			 */

			if (is_resized_partition(p_next_orig, p_next_new))
				continue;

			if ((p_new->partition_offset_sec +
			    p_new->partition_size_sec) >
			    p_next_new->partition_offset_sec) {
				p_new->partition_size_sec =
				    p_next_new->partition_offset_sec -
				    p_new->partition_offset_sec;

				/*
				 * partition sector size was adjusted.
				 * Recalculate size in MiB as well
				 */

				om_set_part_mb_size_from_sec(p_new);

				om_debug_print(OM_DBGLVL_INFO,
				    "Partition %d (ID=%02X) overlaps with "
				    "subsequent partition, "
				    "size will be ajusted to %d MB", i,
				    p_new->partition_type,
				    p_new->partition_size);
			}
		} else if ((p_new->partition_offset_sec +
		    p_new->partition_size_sec) > dt->dinfo.disk_size_sec) {
			p_new->partition_size_sec =
			    dt->dinfo.disk_size_sec -
			    p_new->partition_offset_sec;

			/*
			 * sector size of last used partition was adjusted.
			 * Recalculate size in MiB as well
			 */

			om_set_part_mb_size_from_sec(p_new);

			om_debug_print(OM_DBGLVL_INFO,
			    "Partition %d (ID=%02X) exceeds disk size, "
			    "size will be ajusted to %d MB\n", i,
			    p_new->partition_type,
			    p_new->partition_size);
		}
	}

	om_debug_print(OM_DBGLVL_INFO,
	    "Adjusted partition LBA information\n");

	for (i = 0; i < OM_NUMPART; i++) {
		om_debug_print(OM_DBGLVL_INFO,
		    "[%d] pos=%d, id=%02X, beg=%lld, size=%lld(%ld MiB)\n", i,
		    new_dp->pinfo[i].partition_id,
		    new_dp->pinfo[i].partition_type,
		    new_dp->pinfo[i].partition_offset_sec,
		    new_dp->pinfo[i].partition_size_sec,
		    new_dp->pinfo[i].partition_size);
	}

	/* release partition info if allocated if this function */

	if (dt->dparts == NULL)
		local_free_part_info(dp);

	return (new_dp);
}


/*
 * om_duplicate_disk_partition_info
 * This function will allocate space and copy the disk_parts_t structure
 * passed as a parameter.
 * Input:	om_handle_t handle - The handle returned by
 *              om_initiate_target_discovery()
 *		disk_parts_t * - Pointer to disk_parts_t. Usually the return
 *		value from get_disk_partition_info().
 * Return:	disk_parts_t * - Pointer to disk_parts_t. Space will be
 *		allocated and the data is copied and returned.
 *		NULL, if space cannot be allocated.
 */
/*ARGSUSED*/
disk_parts_t *
om_duplicate_disk_partition_info(om_handle_t handle, disk_parts_t *dparts)
{
	disk_parts_t	*dp;

	if (dparts == NULL) {
		om_set_error(OM_BAD_INPUT);
		return (NULL);
	}

	/*
	 * Allocate space for partitions and copy data
	 */
	dp = (disk_parts_t *)calloc(1, sizeof (disk_parts_t));

	if (dp == NULL) {
		om_set_error(OM_NO_SPACE);
		return (NULL);
	}

	(void) memcpy(dp, dparts, sizeof (disk_parts_t));
	dp->disk_name = strdup(dparts->disk_name);

	return (dp);
}

/*
 * om_set_disk_partition_info
 * This function will save the disk partition information passed by the
 * caller and use it for creating disk partitions during install.
 * This function should be used in conjunction with om_perform_install
 * If om_perform_install is not called, no changes in the disk will be made.
 *
 * Input:	om_handle_t handle - The handle returned by
 *		om_initiate_target_discovery()
 * 		disk_parts_t *dp - The modified disk partitions
 * Output:	None
 * Return:	OM_SUCCESS - If the disk partition infromation is saved
 *		OM_FAILURE - If the data cannot be saved.
 * Note:	If the partition information can't be saved, the om_errno
 *		will be set to the actual error condition. The error
 *		information can be obtained by calling om_get_errno().
 */
/*ARGSUSED*/
int
om_set_disk_partition_info(om_handle_t handle, disk_parts_t *dp)
{
	disk_target_t	*dt;
	disk_info_t	di;

	/*
	 * Validate the input
	 */
	if (dp == NULL || dp->disk_name == NULL) {
		om_set_error(OM_BAD_INPUT);
		return (OM_FAILURE);
	}

	/*
	 * Find the disk from the cache using the diskname
	 */
	dt = find_disk_by_name(dp->disk_name);

	if (dt == NULL) {
		return (OM_FAILURE);
	}

	if (dt->dparts == NULL) {
		/*
		 * Log the information that the disk partitions are not defined
		 * before the install started and GUI has defined the partitions
		 * and saving it with orchestrator to be used during install
		 */
		om_log_print("No disk partitions defined prior to install\n");
	}

	/*
	 * If the disk data (partitions and slices) are already committed
	 * before, free the data before saving the new disk data.
	 */
	if (committed_disk_target != NULL) {
		local_free_disk_info(&committed_disk_target->dinfo, B_FALSE);
		local_free_part_info(committed_disk_target->dparts);
		local_free_slice_info(committed_disk_target->dslices);
		free(committed_disk_target);
		committed_disk_target = NULL;
	}

	/*
	 * It looks like the partition information is okay
	 * so take a copy and save it to use during install
	 */
	committed_disk_target =
	    (disk_target_t *)calloc(1, sizeof (disk_target_t));
	if (committed_disk_target == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}

	di = dt->dinfo;
	if (di.disk_name != NULL) {
		committed_disk_target->dinfo.disk_name = strdup(di.disk_name);
	}
	committed_disk_target->dinfo.disk_size = di.disk_size;
	committed_disk_target->dinfo.disk_type = di.disk_type;
	if (di.vendor != NULL) {
		committed_disk_target->dinfo.vendor = strdup(di.vendor);
	}
	committed_disk_target->dinfo.boot_disk = di.boot_disk;
	committed_disk_target->dinfo.label = di.label;
	committed_disk_target->dinfo.removable = di.removable;
	if (di.serial_number != NULL) {
		committed_disk_target->dinfo.serial_number =
		    strdup(di.serial_number);
	}

	if (committed_disk_target->dinfo.disk_name == NULL ||
	    committed_disk_target->dinfo.vendor == NULL ||
	    committed_disk_target->dinfo.serial_number == NULL) {
		goto sdpi_return;
	}
	/*
	 * We are not dealing with slices in Dwarf
	 */
	committed_disk_target->dslices = NULL;

	/*
	 * Copy the partition data from the input
	 */
	committed_disk_target->dparts =
	    om_duplicate_disk_partition_info(handle, dp);
	if (committed_disk_target->dparts == NULL) {
		goto sdpi_return;
	}
	return (OM_SUCCESS);
sdpi_return:
	local_free_disk_info(&committed_disk_target->dinfo, B_FALSE);
	local_free_part_info(committed_disk_target->dparts);
	local_free_slice_info(committed_disk_target->dslices);
	free(committed_disk_target);
	committed_disk_target = NULL;
	return (OM_FAILURE);
}
