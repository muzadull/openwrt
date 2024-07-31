/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Henry Yen <henry.yen@mediatek.com>
 *         Bo-Cun Chen <bc-bocun.chen@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include "mtk_eth_soc.h"
#include "mtk_eth_dbg.h"

static struct mtk_eth *_eth;

static void mtk_qdma_qos_disable(struct mtk_eth *eth)
{
	qdma_qos_disable();
}

static void mtk_qdma_qos_pppq_enable(struct mtk_eth *eth)
{
	qdma_qos_pppq_ebl(true);
}

 static ssize_t mtk_qmda_debugfs_write_qos(struct file *file, const char __user *buffer,
					   size_t count, loff_t *data)
{
	struct seq_file *m = file->private_data;
	struct mtk_eth *eth = m->private;
	char buf[8];
	int len = count;

	if ((len > 8) || copy_from_user(buf, buffer, len))
		return -EFAULT;

	if (buf[0] == '0') {
		pr_info("HQoS is going to be disabled !\n");
		eth->qos_toggle = 0;
		mtk_qdma_qos_disable(eth);
	} else if (buf[0] == '1') {
		pr_info("HQoS mode is going to be enabled !\n");
		eth->qos_toggle = 1;
	} else if (buf[0] == '2') {
		pr_info("Per-port-per-queue mode is going to be enabled !\n");
		pr_info("PPPQ use qid 0~11 (scheduler 0).\n");
		eth->qos_toggle = 2;
		mtk_qdma_qos_pppq_enable(eth);
	}

	return len;
}

static int mtk_qmda_debugfs_read_qos(struct seq_file *m, void *private)
{
	struct mtk_eth *eth = m->private;

	if (eth->qos_toggle == 0)
		pr_info("HQoS is disabled now!\n");
	else if (eth->qos_toggle == 1)
		pr_info("HQoS is enabled now!\n");
	else if (eth->qos_toggle == 2)
		pr_info("Per-port-per-queue mode is enabled!\n");

	return 0;
}

static int mtk_qmda_debugfs_open_qos(struct inode *inode, struct file *file)
{
	return single_open(file, mtk_qmda_debugfs_read_qos,
			   inode->i_private);
}

int mtk_qdma_debugfs_init(struct mtk_eth *eth)
{
	static const struct file_operations fops_qos = {
		.open = mtk_qmda_debugfs_open_qos,
		.read = seq_read,
		.llseek = seq_lseek,
		.write = mtk_qmda_debugfs_write_qos,
		.release = single_release,
	};

	struct dentry *root;
	long i;
	char name[16], name_symlink[48];
	int ret = 0;

	_eth = eth;

	root = debugfs_lookup("mtk_ppe", NULL);
	if (!root)
		return -ENOMEM;

	debugfs_create_file("qos_toggle", S_IRUGO, root, eth, &fops_qos);

	for (i = 0; i < (!MTK_HAS_CAPS(eth->soc->caps, MTK_QDMA_V1_1) ? 4 : 2); i++) {
		ret = snprintf(name, sizeof(name), "qdma_sch%ld", i);
		if (ret != strlen(name)) {
			ret = -ENOMEM;
			goto err;
		}
		ret = snprintf(name_symlink, sizeof(name_symlink),
			       "/sys/kernel/debug/mtketh/qdma_sch%ld", i);
		if (ret != strlen(name_symlink)) {
			ret = -ENOMEM;
			goto err;
		}
		debugfs_create_symlink(name, root, name_symlink);
	}

	for (i = 0; i < MTK_QDMA_TX_NUM; i++) {
		ret = snprintf(name, sizeof(name), "qdma_txq%ld", i);
		if (ret != strlen(name)) {
			ret = -ENOMEM;
			goto err;
		}
		ret = snprintf(name_symlink, sizeof(name_symlink),
			       "/sys/kernel/debug/mtketh/qdma_txq%ld", i);
		if (ret != strlen(name_symlink)) {
			ret = -ENOMEM;
			goto err;
		}
		debugfs_create_symlink(name, root, name_symlink);
	}

	return 0;

err:
	return ret;
}
