/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Portions Copyright 2007-2013 Apple Inc.
 */

#pragma ident	"@(#)auto_vnops.c	1.70	05/12/19 SMI"

#include <mach/mach_types.h>
#include <mach/machine/boolean.h>
#include <mach/host_priv.h>
#include <mach/host_special_ports.h>
#include <mach/vm_map.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/namei.h>
#include <sys/kauth.h>
#include <sys/attr.h>
#include <sys/vnode_if.h>
#include <sys/vfs_context.h>
#include <sys/vm.h>
#include <sys/errno.h>
#include <vfs/vfs_support.h>
#include <sys/uio.h>

#include <kern/assert.h>
#include <kern/host.h>

#include <IOKit/IOLib.h>

#define CONFIG_MACF 1
#include <security/mac_framework.h>

#include "autofs.h"
#include "triggers.h"
#include "triggers_priv.h"
#include "autofs_kern.h"
#include "autofs_protUser.h"

/*
 *  Vnode ops for autofs
 */
static int auto_getattr(struct vnop_getattr_args *);
static int auto_setattr(struct vnop_setattr_args *);
static int auto_lookup(struct vnop_lookup_args *);
static int auto_readdir(struct vnop_readdir_args *);
static int auto_readlink(struct vnop_readlink_args *);
static int auto_pathconf(struct vnop_pathconf_args *);
static int auto_fsctl(struct vnop_ioctl_args *);
static int auto_getxattr(struct vnop_getxattr_args *);
static int auto_listxattr(struct vnop_listxattr_args *);
static int auto_inactive(struct vnop_inactive_args *);
static int auto_reclaim(struct vnop_reclaim_args *);

int (**autofs_vnodeop_p)(void *);

#define VOPFUNC int (*)(void *)

struct vnodeopv_entry_desc autofs_vnodeop_entries[] = {
	{&vnop_default_desc, (VOPFUNC)vn_default_error},
	{&vnop_lookup_desc, (VOPFUNC)auto_lookup},		/* lookup */
	{&vnop_open_desc, (VOPFUNC)nop_open},			/* open	- NOP */
	{&vnop_close_desc, (VOPFUNC)nop_close},			/* close - NOP */
	{&vnop_getattr_desc, (VOPFUNC)auto_getattr},		/* getattr */
	{&vnop_setattr_desc, (VOPFUNC)auto_setattr},		/* setattr */
	{&vnop_fsync_desc, (VOPFUNC)nop_fsync},			/* fsync - NOP */
	{&vnop_readdir_desc, (VOPFUNC)auto_readdir},		/* readdir */
	{&vnop_readlink_desc, (VOPFUNC)auto_readlink},		/* readlink */
	{&vnop_pathconf_desc, (VOPFUNC)auto_pathconf},		/* pathconf */
	{&vnop_ioctl_desc, (VOPFUNC)auto_fsctl},		/* ioctl (really fsctl) */
	{&vnop_getxattr_desc, (VOPFUNC)auto_getxattr},		/* getxattr */
	{&vnop_listxattr_desc, (VOPFUNC)auto_listxattr},	/* listxattr */
	{&vnop_inactive_desc, (VOPFUNC)auto_inactive},		/* inactive */
	{&vnop_reclaim_desc, (VOPFUNC)auto_reclaim},		/* reclaim */
	{NULL, NULL}
};

struct vnodeopv_desc autofsfs_vnodeop_opv_desc =
	{ &autofs_vnodeop_p, autofs_vnodeop_entries };

static int autofs_nobrowse = 0;

/*
 * Returns 1 if a readdir on the vnode will only return names for the
 * vnodes we have, 0 otherwise.
 *
 * XXX - come up with a better name.
 */
int
auto_nobrowse(vnode_t vp)
{
	fnnode_t *fnp = vntofn(vp);
	fninfo_t *fnip = vfstofni(vnode_mount(vp));

	if (fnp == vntofn(fnip->fi_rootvp)) {
		/*
		 * This is the root directory of the mount, so a
		 * readdir on the vnode will only return names for
		 * the vnodes we already have if we've globally
		 * disabled browsing or the map was mounted with
		 * "nobrowse".
		 */
		return (autofs_nobrowse ||
		    (fnip->fi_mntflags & AUTOFS_MNT_NOBROWSE));
	} else {
		/*
		 * This is a subdirectory of the mount; a readdir
		 * on the vnode will always make an upcall, as
		 * we need to enumerate all the subdirectories
		 * generated by the submounts.
		 */
		return (0);
	}
}

static int
auto_getattr(struct vnop_getattr_args *ap)
	/* struct vnop_getattr_args  {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		struct vnode_attr *a_vap;
		vfs_context_t a_context;
	} */
{
	vnode_t vp = ap->a_vp;
	struct vnode_attr *vap = ap->a_vap;

	AUTOFS_DPRINT((4, "auto_getattr vp %p\n", (void *)vp));

	/* XXX - lock the fnnode? */

	auto_get_attributes(vp, vap);

	return (0);
}

void
auto_get_attributes(vnode_t vp, struct vnode_attr *vap)
{
	fnnode_t *fnp = vntofn(vp);

	VATTR_RETURN(vap, va_rdev, 0);
	switch (vnode_vtype(vp)) {
	case VDIR:
		/*
		 * fn_linkcnt doesn't count the "." link, as we're using it
		 * as a count of references to the fnnode from other vnnodes
		 * (so that if it goes to 0, we know no other fnnode refers
		 * to it).
		 */
		VATTR_RETURN(vap, va_nlink, fnp->fn_linkcnt + 1);

		/*
		 * The "size" of a directory is the number of entries
		 * in its list of directory entries, plus 1.
		 * (Solaris added 1 for some reason.)
		 */
		VATTR_RETURN(vap, va_data_size, fnp->fn_direntcnt + 1);
		break;
	case VLNK:
		VATTR_RETURN(vap, va_nlink, fnp->fn_linkcnt);
		VATTR_RETURN(vap, va_data_size, fnp->fn_symlinklen);
		break;
	default:
		VATTR_RETURN(vap, va_nlink, fnp->fn_linkcnt);
		VATTR_RETURN(vap, va_data_size, 0);
		break;
	}
	VATTR_RETURN(vap, va_total_size, roundup(vap->va_data_size, AUTOFS_BLOCKSIZE));
	VATTR_RETURN(vap, va_iosize, AUTOFS_BLOCKSIZE);

	VATTR_RETURN(vap, va_uid, fnp->fn_uid);
	VATTR_RETURN(vap, va_gid, 0);
	VATTR_RETURN(vap, va_mode, fnp->fn_mode);
	/*
	 * Does our caller want the BSD flags?
	 */
	if (VATTR_IS_ACTIVE(vap, va_flags)) {
		/*
		 * If this is the root of a mount, and if the "hide this
		 * from the Finder" mount option is set on that mount,
		 * return the hidden bit, so the Finder won't show it.
		 */
		if (vnode_isvroot(vp)) {
			fninfo_t *fnip = vfstofni(vnode_mount(vp));

			if (fnip->fi_mntflags & AUTOFS_MNT_HIDEFROMFINDER)
				VATTR_RETURN(vap, va_flags, UF_HIDDEN);
			else
				VATTR_RETURN(vap, va_flags, 0);
		} else
			VATTR_RETURN(vap, va_flags, 0);
	}
	vap->va_access_time.tv_sec = fnp->fn_atime.tv_sec;
	vap->va_access_time.tv_nsec = fnp->fn_atime.tv_usec * 1000;
	VATTR_SET_SUPPORTED(vap, va_access_time);
	vap->va_modify_time.tv_sec = fnp->fn_mtime.tv_sec;
	vap->va_modify_time.tv_nsec = fnp->fn_mtime.tv_usec * 1000;
	VATTR_SET_SUPPORTED(vap, va_modify_time);
	vap->va_change_time.tv_sec = fnp->fn_ctime.tv_sec;
	vap->va_change_time.tv_nsec = fnp->fn_ctime.tv_usec * 1000;
	VATTR_SET_SUPPORTED(vap, va_change_time);
	VATTR_RETURN(vap, va_fileid, fnp->fn_nodeid);
	VATTR_RETURN(vap, va_fsid, vfs_statfs(vnode_mount(vp))->f_fsid.val[0]);
	VATTR_RETURN(vap, va_filerev, 0);
	VATTR_RETURN(vap, va_type, vnode_vtype(vp));
}

static int
auto_setattr(struct vnop_setattr_args *ap)
	/* struct vnop_setattr_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		struct vnode_attr *a_vap;
		vfs_context_t a_context;
	} */
{
	vnode_t vp = ap->a_vp;
	struct vnode_attr *vap = ap->a_vap;
	fnnode_t *fnp = vntofn(vp);

	AUTOFS_DPRINT((4, "auto_setattr vp %p\n", (void *)vp));

	/*
	 * Only root can change the attributes.
	 */
	if (!kauth_cred_issuser(vfs_context_ucred(ap->a_context)))
		return (EPERM);

	/*
	 * All you can set are the UID and the permissions; that's to
	 * allow the automounter to give the mount point to the user
	 * on whose behalf we're doing the mount, and make it writable
	 * by them, so we can do AFP and SMB mounts as that user (so
	 * the connection can be authenticated as them).
	 *
	 * We pretend to allow the GID to be set, but we don't actually
	 * set it.
	 */
	VATTR_SET_SUPPORTED(vap, va_uid);
	if (VATTR_IS_ACTIVE(vap, va_uid))
		fnp->fn_uid = vap->va_uid;
	VATTR_SET_SUPPORTED(vap, va_gid);
	VATTR_SET_SUPPORTED(vap, va_mode);
	if (VATTR_IS_ACTIVE(vap, va_mode))
		fnp->fn_mode = vap->va_mode & ALLPERMS;
	return (0);
}

static int
auto_lookup(struct vnop_lookup_args *ap)
	/* struct vnop_lookup_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_dvp;
		vnode_t *a_vpp;
		struct componentname *a_cnp;
		vfs_context_t a_context;
	} */
{
	vnode_t dvp = ap->a_dvp;
	vnode_t *vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	u_long nameiop = cnp->cn_nameiop;
	u_long flags = cnp->cn_flags;
	int namelen = cnp->cn_namelen;
	vfs_context_t context = ap->a_context;
	int pid = vfs_context_pid(context);
	int error = 0;
	fninfo_t *dfnip;
	fnnode_t *dfnp = NULL;
	fnnode_t *fnp = NULL;
	vnode_t vp;
	uint32_t vid;
	char *searchnm;
	int searchnmlen;
	int do_notify = 0;
	struct vnode_attr vattr;
	int node_type;
        boolean_t have_lock = 0;
	int retry_count = 0;

	dfnip = vfstofni(vnode_mount(dvp));
	AUTOFS_DPRINT((3, "auto_lookup: dvp=%p (%s) name=%.*s\n",
	    (void *)dvp, dfnip->fi_map, namelen, cnp->cn_nameptr));

	/* This must be a directory. */
	if (!vnode_isdir(dvp))
		return (ENOTDIR);

	/*
	 * XXX - is this necessary?
	 */
	if (namelen == 0) {
		error = vnode_get(dvp);
		if (error)
			return (error);
		*vpp = dvp;
		return (0);
	}

	/* first check for "." and ".." */
	if (cnp->cn_nameptr[0] == '.') {
		if (namelen == 1) {
			/*
			 * "." requested
			 */

			/*
			 * Thou shalt not rename ".".
			 * (No, the VFS layer doesn't catch this for us.)
			 */
			if ((nameiop == RENAME) && (flags & WANTPARENT) &&
			    (flags & ISLASTCN))
				return (EISDIR);

			error = vnode_get(dvp);
			if (error)
				return (error);
			*vpp = dvp;
			return (0);
		} else if ((namelen == 2) && (cnp->cn_nameptr[1] == '.')) {
			fnnode_t *pdfnp;

			pdfnp = (vntofn(dvp))->fn_parent;
			assert(pdfnp != NULL);

			/*
			 * Since it is legitimate to have the VROOT flag set for the
			 * subdirectories of the indirect map in autofs filesystem,
			 * rootfnnodep is checked against fnnode of dvp instead of
			 * just checking whether VROOT flag is set in dvp
			 */

#if 0
			if (pdfnp == pdfnp->fn_globals->fng_rootfnnodep) {
				vnode_t vp;

				vfs_lock_wait(vnode_mount(dvp));
				if (vnode_mount(dvp)->vfs_flag & VFS_UNMOUNTED) {
					vfs_unlock(vnode_mount(dvp));
					return (EIO);
				}
				vp = vnode_mount(dvp)->mnt_vnodecovered;
				error = vnode_get(vp);	/* XXX - what if it fails? */
				vfs_unlock(vnode_mount(dvp));
				error = VNOP_LOOKUP(vp, nm, vpp, pnp, flags, rdir, cred);
				vnode_put(vp);
				return (error);
			} else {
#else
			{
#endif
				*vpp = fntovn(pdfnp);
				return (vnode_get(*vpp));
			}
		}
	}

	dfnp = vntofn(dvp);
	searchnm = cnp->cn_nameptr;
	searchnmlen = namelen;

	AUTOFS_DPRINT((3, "auto_lookup: dvp=%p dfnp=%p\n", (void *)dvp,
	    (void *)dfnp));

	have_lock = auto_fninfo_lock_shared(dfnip, pid);

top:
	/*
	 * If this vnode is a trigger, then something should
	 * be mounted atop it, and there should be nothing
	 * in this file system below it.  (We shouldn't
	 * normally get here, as we should have resolved
	 * the trigger, but some special processes don't
	 * trigger mounts.)
	 */
	if (dfnp->fn_trigger_info != NULL) {
		error = ENOENT;
		goto fail;
	}

	/*
	 * See if we have done something with this name already, so we
	 * already have it.
	 */
	lck_rw_lock_shared(dfnp->fn_rwlock);
	fnp = auto_search(dfnp, cnp->cn_nameptr, cnp->cn_namelen);
	if (fnp == NULL) {
		/*
		 * No, we don't, so we need to make an upcall to see
		 * if something with that name should exist.
		 *
		 * Drop the writer lock on the directory, so
		 * that we don't block reclaims of autofs
		 * vnodes in that directory while we're
		 * waiting for automountd to respond
		 * (automountd, or some process on which
		 * it depends, might be doing something
		 * that requires the allocation of a
		 * vnode, and that might involve reclaiming
		 * an autofs vnode) or while we're trying
		 * to allocate a vnode for the file or
		 * directory we're looking up.
		 */
		lck_rw_unlock_shared(dfnp->fn_rwlock);

		/*
		 * Check whether this map is in the process of being
		 * unmounted.  If so, return ENOENT; see auto_control_ioctl()
		 * for the reason why this is done.
		 */
		if (dfnip->fi_flags & MF_UNMOUNTING) {
			error = ENOENT;
			goto fail;
		}

		/*
		 * Check whether security policy allows this process
		 * to trigger mount.
		 */
		if (strcmp(dfnip->fi_map, "-hosts") == 0) {
			error = mac_vnode_check_trigger_resolve(context, dvp, cnp);
			if (error != 0)
				goto fail;
		}

		/*
		 * Ask automountd whether something with this
		 * name exists.
		 */
		error = auto_lookup_aux(dfnip, dfnp, searchnm,
		    searchnmlen, context, &node_type);
		if (error != 0)
			goto fail;	/* nope */

		/*
		 * OK, it exists.  We need to create the
		 * fnnode for it, and enter it into the
		 * directory.
		 *
		 * Create the fnnode first, as we must
		 * not grab the writer lock on the
		 * directory, as per the above.
		 */
		error = auto_makefnnode(&fnp, node_type,
		    vnode_mount(dvp), cnp, NULL, dvp, 0,
		    dfnp->fn_globals);
		if (error)
			goto fail;

		/*
		 * Now enter the fnnode in the directory.
		 *
		 * Note that somebody might have created the
		 * name while we weren't holding the lock;
		 * if so, then auto_enter() will return
		 * EEXIST, and will have discarded the
		 * vnode we created and handed us back
		 * an fnnode referring to what they'd
		 * already created.
		 */
		error = auto_enter(dfnp, cnp, &fnp);
		if (error) {
			if (error == EEXIST) {
				/*
				 * We found the name.  Act as if
				 * the auto_search() above succeeded.
				 */
				error = 0;
			} else if (error == EJUSTRETURN) {
				/*
				 * We could not acquire either
				 * a) an iocount on the existing vnode and the
				 * autofs mount point is getting unmounted.
				 * or
				 * b) a reference on the parent directory.
				 *
				 * In either case we no longer have an iocount
				 * on the vnode we created. EJUSTRETURN is used
				 * as a subsitute for ENOMORERETRIES.
				 *
				 * This should ideally redrive the lookup
				 * instead i.e we should be able to return
				 * ERECYCLE and have namei do a limited number
				 * retries but ERECYCLE is not exported and
				 * namei does not impose a limit on the number
				 * of retries it will do.
				 *
				 * We'll just return ENOENT which may have the
				 * unfortunate side effect of getting transient
				 * ENOENT's in a unmount->retrigger window.
				 */
				error = ENOENT;
				goto fail;
			} else {
				/*
				 * We found the name, but couldn't
				 * get an iocount on the vnode for
				 * its fnnode.  That's probably
				 * because it was in the process
				 * of being recycled.  if we havn't already done
				 * it 3 times, Redo the search, as the
				 * directory might have changed.
				 */
				if (++retry_count >= 3) {
					error = ENOENT;
					goto fail;
				}
				error = 0;
				goto top;
			}
		} else {
			/*
			 * We added an entry to the directory,
			 * so we might want to notify
			 * interested parties about that.
			 * XXX
			 */
			do_notify = 1;
		}
	} else {
		/*
		 * Yes, we did.
		 *
		 * We're holding a read lock on that directory, so this
		 * won't be released out from under us.  We'll have to
		 * drop the read lock when we do a vnode_getwithvid() to
		 * allow in-progress reclaims to finish, but that means
		 * such a reclaim could free the fnnode.
		 *
		 * We get the vnode for the fnnode - which will remain
		 * a vnode even if it's reclaimed - and the vnode ID it
		 * had when we created the fnnode.
		 */
		vp = fntovn(fnp);
		vid = fnp->fn_vid;

		/*
		 * Now we can, and must, drop the rwlock to allow
		 * in-progress reclaims to finish.
		 */
		lck_rw_unlock_shared(dfnp->fn_rwlock);

	    	/*
	    	 * Now let's try to get an iocount on the fnnode, so it
		 * doesn't vanish out from under us; this also checks
		 * whether the vnode has been reclaimed out from under
		 * us, and, thus, whether the fnnode has already vanished
		 * out from under us.
	    	 */
		if (vnode_getwithvid(vp, vid) != 0) {
			/*
			 * We failed; the vnode was reclaimed.  The fnnode
			 * is gone; redo the search.
			 */
			error = 0;
			goto top;
		}

		/*
		 * OK, that succeeded, and the vnode is still what it
		 * was when we created the fnnode, so the fnnode is
		 * still there - and, as we're holding an iocount
		 * on the vnode, it's not going away.
		 */
	}

fail:
	auto_fninfo_unlock_shared(dfnip, have_lock);

	if (error) {
		/*
		 * If this is a CREATE operation, and this is the last
		 * component, and the error is ENOENT, make it ENOTSUP,
		 * instead, so that somebody trying to create a file or
		 * directory gets told "sorry, we don't support that".
		 * Do the same for RENAME operations, so somebody trying
		 * to rename a file or directory gets told that.
		 */
		if (error == ENOENT &&
		    (nameiop == CREATE || nameiop == RENAME) &&
		    (flags & ISLASTCN))
			error = ENOTSUP;
		goto done;
	}

	/*
	 * We now have the actual fnnode we're interested in.
	 */
	*vpp = fntovn(fnp);

	/*
	 * If the directory in which we created this is one on which a
	 * readdir will only return names corresponding to the vnodes
	 * we have for it, and somebody cares whether something was
	 * created in it, notify them.
	 *
	 * XXX - defer until we trigger a mount atop it?
	 */
	if (do_notify && vnode_ismonitored(dvp) && auto_nobrowse(dvp)) {
		vfs_get_notify_attributes(&vattr);
		auto_get_attributes(dvp, &vattr);
		vnode_notify(dvp, VNODE_EVENT_WRITE, &vattr);
	}

done:
	AUTOFS_DPRINT((5, "auto_lookup: name=%s *vpp=%p return=%d\n",
	    cnp->cn_nameptr, (void *)*vpp, error));

	return (error);
}

/*
#% readdir	vp	L L L
#
vnop_readdir {
	IN vnode_t vp;
	INOUT struct uio *uio;
	INOUT int *eofflag;
	OUT int *ncookies;
	INOUT u_long **cookies;
	IN vfs_context_t context;
*/

#define MAXDIRBUFSIZE	65536

/*
 * "Transient" fnnodes are fnnodes that don't have anything mounted on
 * them and don't have subdirectories.
 * Those are subject to evaporating in the near term, so we don't
 * return them from a readdir - and don't filter them out from names
 * we get from automountd.
 */
#define IS_TRANSIENT(fnp) \
	 (!vnode_mountedhere(fntovn(fnp)) && (fnp)->fn_direntcnt == 0)

int
auto_readdir(struct vnop_readdir_args *ap)
	/* struct vnop_readdir_args{
		vnode_t a_vp;
		struct uio *a_uio;
		int a_flags;
		int *a_eofflag;
		int *a_numdirent;
		vfs_context_t a_context;
	}*/
{
	vnode_t vp = ap->a_vp;
	struct uio *uiop = ap->a_uio;
	int pid = vfs_context_pid(ap->a_context);
	int64_t return_offset;
	boolean_t return_eof;
	byte_buffer return_buffer;
	mach_msg_type_number_t return_bufcount;
	vm_map_offset_t map_data;
	vm_offset_t data;
	fnnode_t *fnp = vntofn(vp);
	fnnode_t *cfnp, *nfnp;
	struct dirent *dp;
	off_t offset;
	u_int outcount = 0;
	mach_msg_type_number_t count;
        void *outbuf;
	user_ssize_t user_alloc_count;
	u_int alloc_count;
	fninfo_t *fnip = vfstofni(vnode_mount(vp));
	kern_return_t ret;
	int error = 0;
	int reached_max = 0;
	int myeof = 0;
	u_int this_reclen;
        boolean_t have_lock = 0;
	boolean_t needs_put = 0;

        AUTOFS_DPRINT((4, "auto_readdir vp=%p offset=%lld\n",
            (void *)vp, uio_offset(uiop)));
                                                
	if (ap->a_numdirent != NULL)
		*ap->a_numdirent = 0;

	if (ap->a_flags & (VNODE_READDIR_EXTENDED | VNODE_READDIR_REQSEEKOFF))
		return (EINVAL);

	if (ap->a_eofflag != NULL)
		*ap->a_eofflag = 0;

	user_alloc_count = uio_resid(uiop);
	/*
	 * Reject too-small user requests.
	 */
	if (user_alloc_count < (user_ssize_t) DIRENT_RECLEN(1))
		return (EINVAL);
	/*
	 * Trim too-large user requests.
	 */
	if (user_alloc_count > (user_ssize_t) MAXDIRBUFSIZE)
		user_alloc_count = MAXDIRBUFSIZE;
	alloc_count = (u_int)user_alloc_count;

	/*
	 * Make sure the mounted map won't change out from under us.
	 */
	have_lock = auto_fninfo_lock_shared(fnip, pid);

	/*
	 * Make sure the directory we're reading won't change out from
	 * under us while we're scanning it.
	 */
	lck_rw_lock_shared(fnp->fn_rwlock);
	
	if (uio_offset(uiop) >= AUTOFS_DAEMONCOOKIE) {
		/*
		 * If we're in the middle of unmounting the map, we won't
		 * create anything under it in a lookup, so we should
		 * only return directory entries for things that are
		 * already there.
		 */
		if (fnip->fi_flags & MF_UNMOUNTING) {
			if (ap->a_eofflag != NULL)
				*ap->a_eofflag = 1;
			goto done;
		}

again:
		/*
		 * Do readdir of daemon contents only
		 * Drop readers lock and reacquire after reply.
		 */
		lck_rw_unlock_shared(fnp->fn_rwlock);

		count = 0;
		error = auto_readdir_aux(fnip, fnp,
		    uio_offset(uiop), alloc_count,
		    &return_offset, &return_eof, &return_buffer,
		    &return_bufcount);

		/*
		 * reacquire previously dropped lock
		 */
		lck_rw_lock_shared(fnp->fn_rwlock);

		if (error)
			goto done;

		ret = vm_map_copyout(kernel_map, &map_data,
		    (vm_map_copy_t)return_buffer);
		if (ret != KERN_SUCCESS) {
			IOLog("autofs: vm_map_copyout failed, status 0x%08x\n",
			    ret);
			/* XXX - deal with Mach errors */
			error = EIO;
			goto done;
		}
		data = CAST_DOWN(vm_offset_t, map_data);

		if (return_bufcount != 0) {
			struct dirent *odp;	/* next in output buffer */
			struct dirent *cdp;	/* current examined entry */

			/*
			 * Check for duplicates of entries that have
			 * fnnodes, and for illegal values (".", "..",
			 * and anything with a "/" in it), here.
			 */
			dp = (struct dirent *)data;
			odp = dp;
			cdp = dp;
			do {
				this_reclen = RECLEN(cdp);
				cfnp = auto_search(fnp, cdp->d_name,
				    cdp->d_namlen);
				if (cfnp == NULL || IS_TRANSIENT(cfnp)) {
					/*
					 * entry not found in kernel list,
					 * or found but is transient, so
					 * include it in readdir output.
					 *
					 * If we are skipping entries. then
					 * we need to copy this entry to the
					 * correct position in the buffer
					 * to be copied out.
					 */
					if (cdp != odp)
						bcopy(cdp, odp,
						    (size_t)this_reclen);
					odp = nextdp(odp);
					outcount += this_reclen;
					if (ap->a_numdirent)
						++(*ap->a_numdirent);
				} else {
					/*
					 * Entry was found in the kernel
					 * list. If it is the first entry
					 * in this buffer, then just skip it
					 */
					if (odp == dp) {
						dp = nextdp(dp);
						odp = dp;
					}
				}
				count += this_reclen;
				cdp = (struct dirent *)
				    ((char *)cdp + this_reclen);
			} while (count < return_bufcount);

			if (outcount)
				error = uiomove((caddr_t)dp, outcount, uiop);
			uio_setoffset(uiop, return_offset);
		} else {
			if (return_eof == 0) {
				/*
				 * alloc_count not large enough for one
				 * directory entry
				 */
				error = EINVAL;
			}
		}
		vm_deallocate(kernel_map, data, return_bufcount);
		if (return_eof && !error) {
			myeof = 1;
			if (ap->a_eofflag != NULL)
				*ap->a_eofflag = 1;
		}
		if (!error && !myeof && outcount == 0) {
			/*
			 * call daemon with new cookie, all previous
			 * elements happened to be duplicates
			 */
			goto again;
		}
		goto done;
	}

	/*
	 * Not past the "magic" offset, so we return only the entries
	 * we get without talking to the daemon.
	 */
	MALLOC(outbuf, void *, alloc_count, M_AUTOFS, M_WAITOK);
	dp = outbuf;
	if (uio_offset(uiop) == 0) {
		/*
		 * first time: so fudge the . and ..
		 */
		this_reclen = DIRENT_RECLEN(1);
		if (alloc_count < this_reclen) {
			error = EINVAL;
			goto cleanup;
		}
		if (fnp->fn_parent == NULL) {
			error = EINVAL;
			goto cleanup;
		}
		dp->d_ino = (ino_t)fnp->fn_nodeid;
		dp->d_reclen = (uint16_t)this_reclen;
#if 0
		dp->d_type = DT_DIR;
#else
		dp->d_type = DT_UNKNOWN;
#endif
		dp->d_namlen = 1;

		/* use strncpy() to zero out uninitialized bytes */

		(void) strncpy(dp->d_name, ".", DIRENT_NAMELEN(this_reclen));
		outcount += dp->d_reclen;
		dp = nextdp(dp);

		if (ap->a_numdirent)
			++(*ap->a_numdirent);

		this_reclen = DIRENT_RECLEN(2);
		if (alloc_count < outcount + this_reclen) {
			error = EINVAL;
			goto cleanup;
		}
		
		/* Verify that the parent (..) is still valid */
		if (fnp->fn_parentvp && fnp->fn_parentvp != vp) {
			if (vnode_getwithvid(
				fnp->fn_parentvp,
				fnp->fn_parentvid)) {
				/* The parent has been reclaimed. Punt. */
				fnp->fn_parent = NULL;
				fnp->fn_parentvp = NULL;
				error = EINVAL;
				goto cleanup;
			}
			/*hold on to parent until end of this readdir???? */
			needs_put =1;
		}
		dp->d_ino = (ino_t)fnp->fn_parent->fn_nodeid;
		dp->d_reclen = (uint16_t)this_reclen;
		if (needs_put) {
			vnode_put(fnp->fn_parentvp);
		}
#if 0
		dp->d_type = DT_DIR;
#else
		dp->d_type = DT_UNKNOWN;
#endif
		dp->d_namlen = 2;

		/* use strncpy() to zero out uninitialized bytes */

		(void) strncpy(dp->d_name, "..",
		    DIRENT_NAMELEN(this_reclen));
		outcount += dp->d_reclen;
		dp = nextdp(dp);

		if (ap->a_numdirent)
			++(*ap->a_numdirent);
	}

	offset = 2;
	cfnp = fnp->fn_dirents;
	while (cfnp != NULL) {
		nfnp = cfnp->fn_next;
		offset = cfnp->fn_offset;
		/*
		 * XXX - what is this lock protecting against?  We're
		 * holding a read lock on the directory we're reading,
		 * which should keep its fn_dirents list from changing
		 * and thus keep fnnodes in that list from being freed.
		 */
		lck_rw_lock_shared(cfnp->fn_rwlock);
		if ((offset >= uio_offset(uiop)) && !IS_TRANSIENT(cfnp)) {
			int reclen;

			lck_rw_unlock_shared(cfnp->fn_rwlock);

			/*
			 * include node only if its offset is greater or
			 * equal to the one required and isn't
			 * transient
			 */
			reclen = (int)DIRENT_RECLEN(cfnp->fn_namelen);
			if (outcount + reclen > alloc_count) {
				reached_max = 1;
				break;
			}
			dp->d_reclen = (uint16_t)reclen;
			dp->d_ino = (ino_t)cfnp->fn_nodeid;
#if 0
			dp->d_type = vnode_isdir(fntovn(cfnp)) ? DT_DIR : DT_LNK;
#else
			dp->d_type = DT_UNKNOWN;
#endif
			dp->d_namlen = cfnp->fn_namelen;

			/* use strncpy() to zero out uninitialized bytes */

			(void) strncpy(dp->d_name, cfnp->fn_name,
			    DIRENT_NAMELEN(reclen));
			outcount += dp->d_reclen;
			dp = nextdp(dp);

			if (ap->a_numdirent)
				++(*ap->a_numdirent);
		} else
			lck_rw_unlock_shared(cfnp->fn_rwlock);
		cfnp = nfnp;
	}

	if (outcount)
		error = uiomove(outbuf, outcount, uiop);
	if (!error) {
		if (reached_max) {
			/*
			 * This entry did not get added to the buffer on this,
			 * call. We need to add it on the next call therefore
			 * set uio_offset to this entry's offset.  If there
			 * wasn't enough space for one dirent, return EINVAL.
			 */
			uio_setoffset(uiop, offset);
			if (outcount == 0)
				error = EINVAL;
		} else if (auto_nobrowse(vp)) {
			/*
			 * done reading directory entries
			 */
			uio_setoffset(uiop, offset + 1);
			if (ap->a_eofflag != NULL)
				*ap->a_eofflag = 1;
		} else {
			/*
			 * Need to get the rest of the entries from the daemon.
			 */
			uio_setoffset(uiop, AUTOFS_DAEMONCOOKIE);
		}
	}
	
cleanup:
	FREE(outbuf, M_AUTOFS);

done:
	lck_rw_unlock_shared(fnp->fn_rwlock);
	auto_fninfo_unlock_shared(fnip, have_lock);
	AUTOFS_DPRINT((5, "auto_readdir vp=%p offset=%lld eof=%d\n",
	    (void *)vp, uio_offset(uiop), myeof));	
	return (error);
}

static int
auto_readlink(struct vnop_readlink_args *ap)
	/* struct vnop_readlink_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		struct uio *a_uio;
		vfs_context_t a_context;
	} */
{
	vnode_t vp = ap->a_vp;
	uio_t uiop = ap->a_uio;
	fnnode_t *fnp = vntofn(vp);
	struct timeval now;
	int error;

	AUTOFS_DPRINT((4, "auto_readlink: vp=%p\n", (void *)vp));

	if (!vnode_islnk(vp))
		error = EINVAL;
	else {
		microtime(&now);
		fnp->fn_atime = now;
		error = uiomove(fnp->fn_symlink, MIN(fnp->fn_symlinklen,
		    (int)uio_resid(uiop)), uiop);
	}

	AUTOFS_DPRINT((5, "auto_readlink: error=%d\n", error));
	return (error);
}

static int
auto_pathconf(struct vnop_pathconf_args *ap)
	/* struct vnop_pathconf_args {
		struct vnode *a_vp;
		int a_name;
		int *a_retval;
		vfs_context_t a_context;
	} */
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		/* arbitrary limit matching HFS; autofs has no hard limit */
		*ap->a_retval = 32767;
		break;
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		break;
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 200112;		/* _POSIX_CHOWN_RESTRICTED */
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		break;
	case _PC_CASE_SENSITIVE:
		*ap->a_retval = 1;
		break;
	case _PC_CASE_PRESERVING:
		*ap->a_retval = 1;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static int
auto_fsctl(struct vnop_ioctl_args * ap)
	/* struct vnop_ioctl_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		int32_t a_command;
		caddr_t a_data;
		int32_t a_fflag;
		vfs_context_t a_context;
	}; */
{
	vnode_t vp = ap->a_vp;

	/*
	 * The only operation we support is "mark this as having a home
	 * directory mount in progress".
	 */
	if (ap->a_command != AUTOFS_MARK_HOMEDIRMOUNT)
		return (EINVAL);

        /* 
         * <13595777> homedirmounter getting ready to do a mount so we 
         * want to take the mutex
         */
	return (auto_mark_vnode_homedirmount(vp,
                                             vfs_context_pid(ap->a_context),
                                             1));
}      

static int 
auto_getxattr(struct vnop_getxattr_args *ap)
 	/* struct vnop_getxattr_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		char * a_name;
		uio_t a_uio;
		size_t *a_size;
		int a_options;
		vfs_context_t a_context;
	}; */
{
	struct uio *uio = ap->a_uio;

	/* do not support position argument */
	if (uio_offset(uio) != 0)
		return (EINVAL);

	/*
	 * We don't actually offer any extended attributes; we just say
	 * we do, so that nobody wastes our time - or any server's time,
	 * with wildcard maps - looking for ._ files.
	 */
	return (ENOATTR);
}

static int 
auto_listxattr(struct vnop_listxattr_args *ap)
	/* struct vnop_listxattr_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		uio_t a_uio;
		size_t *a_size;
		int a_options;
		vfs_context_t a_context;
	}; */
{
	*ap->a_size = 0;
	
	/* we have no extended attributes, so just return 0 */
	return (0);
}

/*
 * Called when the I/O count (in-progress vnops) is 0, and either
 * the use count (long-term references) is 0 or the vnode is being
 * forcibly disconnected from us (e.g., on a forced unmount, in which
 * case the vnode will be reassociated with deadfs).
 *
 * We just recycle the vnode, which encourages its reclamation; we
 * can't disconnect it until it's actually reclaimed.
 */
static int
auto_inactive(struct vnop_inactive_args *ap)
	/* struct vnop_inactive_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		vfs_context_t a_context;
	} */
{
	AUTOFS_DPRINT((4, "auto_inactive: vp=%p\n", (void *)ap->a_vp));

	vnode_recycle(ap->a_vp);

	AUTOFS_DPRINT((5, "auto_inactive: (exit) vp=%p\n", (void *)ap->a_vp));
	return (0);
}

static int
auto_reclaim(struct vnop_reclaim_args *ap)
	/* struct vnop_reclaim_args {
		struct vnodeop_desc *a_desc;
		vnode_t a_vp;
		vfs_context_t a_context;
	} */
{
	vnode_t vp = ap->a_vp;
	fnnode_t *fnp = vntofn(vp);
	vnode_t pvp = fnp->fn_parentvp;
	int is_symlink = (vnode_vtype(vp) == VLNK) ? 1 : 0;

	AUTOFS_DPRINT((4, "auto_reclaim: vp=%p fn_link=%d\n",
	    (void *)vp, fnp->fn_linkcnt));

	/*
	 * There are no filesystem calls in progress on this vnode, and
	 * none will be made until we're done.
	 *
	 * Thus, it's safe to disconnect this from its parent directory,
	 * if it has one.
	 */
	fnnode_t *dfnp = fnp->fn_parent;
	if (dfnp != NULL) {
		int	needs_put = 0;

		if (pvp && pvp != vp) {
			if (vnode_getwithvid(pvp, fnp->fn_parentvid)) {
				/*
				 * parent has been reclaimed, just release the
				 * reference acquired in auto_lookup.
				 */
				fnp->fn_parent = NULL;
				fnp->fn_parentvp = NULL;
				vnode_rele(pvp);
				goto free_node;
			}
			needs_put = 1;
		}

		lck_rw_lock_exclusive(dfnp->fn_rwlock);
		/*
		 * If there's only one link to this, namely the link to it
		 * from its parent, get rid of it by removing it from
		 * its parent's list of child fnnodes and recycle it;
		 * a subsequent reference to it will recreate it if
		 * the name is still there in the map.
		 *
		 * However, even if there is more than 1 link and we are in
		 * reclaim it just means that the parent is getting reclaimed
		 * before the child. This can happen and autofs can't prevent
		 * it because vflush with FORCECLOSE has already happened
		 * by the time autofs gets to say that it doesn't support
		 * forced unmounts in auto_unmount. The > 1 case is that case
		 * and we have to disconnect ourselves from the parent in that
		 * case as well.
		 */
		if (fnp->fn_linkcnt >= 1) {
			/*
			 * This will drop the write lock on dfnp.
			 */
			auto_disconnect(dfnp, fnp);
		} else if (fnp->fn_linkcnt == 0) {
			/*
			 * Root vnode; we've already removed it from the
			 * "parent" (the master node for all autofs file
			 * systems) - just null out the parent pointer, so
			 * that we don't trip any assertions
			 * in auto_freefnnode().
			 */
			fnp->fn_parent = NULL;
			fnp->fn_parentvp = NULL;
			lck_rw_unlock_exclusive(dfnp->fn_rwlock);
		}

		if (needs_put) {
			vnode_put(pvp);
		}
	}
free_node:
	auto_freefnnode(fnp, is_symlink);
	vnode_clearfsnode(vp);
	AUTOFS_DPRINT((5, "auto_reclaim: (exit) vp=%p freed\n",
	    (void *)vp));
	return (0);
}
