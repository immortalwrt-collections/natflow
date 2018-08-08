/*
 * Author: Chen Minqiang <ptpt52@gmail.com>
 *  Date : Fri, 11 May 2018 14:20:56 +0800
 */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/mman.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/highmem.h>
#include <linux/udp.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_helper.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_set.h>
#include "natflow_common.h"

unsigned int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,1=error,2=warn,4=info,8=debug,16=fixme,...,31=all) default=0");

unsigned int disabled = 1;

int natflow_session_init(struct nf_conn *ct, gfp_t gfp)
{
	struct nat_key_t *nk = NULL;
	struct natflow_t *nf;
	struct nf_ct_ext *old, *new = NULL;
	unsigned int newoff = 0, newlen = 0;
	size_t alloc_size;
	size_t var_alloc_len = ALIGN(sizeof(struct natflow_t), __ALIGN_64BITS);

	if (natflow_session_get(ct) != NULL) {
		return 0;
	}

	if (nf_ct_is_confirmed(ct)) {
		return -1;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	if ((ct->status & IPS_SRC_NAT_DONE)) {
		NATFLOW_ERROR(DEBUG_FMT_PREFIX "realloc ct->ext with IPS_SRC_NAT_DONE is not supported for kernel < 4.9\n", DEBUG_ARG_PREFIX);
		return -1;
	}

	/* XXX Call in advance to make sure this time
	 * is the last user to modify ct->ext
	 */
	if (nfct_help(ct) && !nfct_seqadj(ct)) {
		nfct_seqadj_ext_add(ct);
	}
#endif

	if (!ct->ext || !ct->ext->offset[NF_CT_EXT_NAT]) {
		struct nf_conn_nat *nat = nf_ct_ext_add(ct, NF_CT_EXT_NAT, gfp);
		if (nat == NULL) {
			return -1;
		}
	}

	old = ct->ext;
	if (!old) {
		newoff = ALIGN(sizeof(struct nf_ct_ext), NATFLOW_FACTOR);
		if (newoff > NATFLOW_MAX_OFF) {
			NATFLOW_ERROR(DEBUG_FMT_PREFIX "realloc ct->ext->len > %u not supported!\n", DEBUG_ARG_PREFIX, NATFLOW_MAX_OFF);
			return -1;
		}
		newlen = ALIGN(newoff + var_alloc_len, __ALIGN_64BITS) + ALIGN(sizeof(struct nat_key_t), __ALIGN_64BITS);
		alloc_size = ALIGN(newlen, __ALIGN_64BITS);

		new = kzalloc(alloc_size, gfp);
		if (!new) {
			return -1;
		}
		ct->ext = new;
	} else {
		newoff = ALIGN(old->len, NATFLOW_FACTOR);
		if (newoff > NATFLOW_MAX_OFF) {
			NATFLOW_ERROR(DEBUG_FMT_PREFIX "realloc ct->ext->len > %u not supported!\n", DEBUG_ARG_PREFIX, NATFLOW_MAX_OFF);
			return -1;
		}
		newlen = ALIGN(newoff + var_alloc_len, __ALIGN_64BITS) + ALIGN(sizeof(struct nat_key_t), __ALIGN_64BITS);
		alloc_size = ALIGN(newlen, __ALIGN_64BITS);

		new = __krealloc(old, alloc_size, gfp);
		if (!new) {
			return -1;
		}
		if (new != old) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
			kfree_rcu(old, rcu);
			ct->ext = new;
#else
			kfree_rcu(old, rcu);
			rcu_assign_pointer(ct->ext, new);
#endif
		}
		memset((void *)new + newoff, 0, newlen - newoff);
	}

	new->len = newoff / NATFLOW_FACTOR;
	nk = (struct nat_key_t *)((void *)new + newoff);
	nk->magic = NATFLOW_MAGIC;
	nk->ext_magic = (unsigned long)ct & 0xffffffff;
	nf = (struct natflow_t *)((void *)nk + ALIGN(sizeof(struct nat_key_t), __ALIGN_64BITS));

	return 0;
}

struct natflow_t *natflow_session_get(struct nf_conn *ct)
{
	struct nat_key_t *nk;
	struct natflow_t *nf;

	if (!ct->ext) {
		return NULL;
	}
	if (ct->ext->len * NATFLOW_FACTOR > NATFLOW_MAX_OFF) {
		return NULL;
	}

	nk = (struct nat_key_t *)((void *)ct->ext + ct->ext->len * NATFLOW_FACTOR);
	if (nk->magic != NATFLOW_MAGIC || nk->ext_magic != (((unsigned long)ct) & 0xffffffff)) {
		return NULL;
	}

	nf = (struct natflow_t *)((void *)nk + ALIGN(sizeof(struct nat_key_t), __ALIGN_64BITS));

	return nf;
}

const char *const hooknames[] = {
	[NF_INET_PRE_ROUTING] = "PRE",
	[NF_INET_LOCAL_IN] = "IN",
	[NF_INET_FORWARD] = "FD",
	[NF_INET_LOCAL_OUT] = "OUT",
	[NF_INET_POST_ROUTING] = "POST",
};
