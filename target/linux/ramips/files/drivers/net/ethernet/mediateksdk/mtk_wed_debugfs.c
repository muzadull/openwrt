// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021 Felix Fietkau <nbd@nbd.name> */

#include <linux/seq_file.h>
#include <linux/soc/mediatek/mtk_wed.h>
#include "mtk_wed.h"
#include "mtk_wed_regs.h"

struct reg_dump {
	const char *name;
	u16 offset;
	u8 type;
	u8 base;
	u32 mask;
};

enum {
	DUMP_TYPE_END,
	DUMP_TYPE_STRING,
	DUMP_TYPE_WED,
	DUMP_TYPE_WED_RING,
	DUMP_TYPE_WDMA,
	DUMP_TYPE_WDMA_RX,
	DUMP_TYPE_WPDMA_TX,
	DUMP_TYPE_WPDMA_TXFREE,
	DUMP_TYPE_WPDMA_RX,
	DUMP_TYPE_WED_RRO,
	DUMP_TYPE_WED_RING_RX_TYPE1,
	DUMP_TYPE_WED_RING_RX_TYPE2,
};

#define DUMP_END() { .type = DUMP_TYPE_END }
#define DUMP_STR(_str) { _str, 0, DUMP_TYPE_STRING }
#define DUMP_REG(_reg, ...) { #_reg, MTK_##_reg, __VA_ARGS__ }
#define DUMP_REG_MASK(_reg, _mask) { #_mask, MTK_##_reg, DUMP_TYPE_WED, 0, MTK_##_mask }

#define DUMP_RING(_prefix, _base, ...)				\
	{ _prefix " BASE", _base, __VA_ARGS__ },		\
	{ _prefix " CNT",  _base + 0x4, __VA_ARGS__ },	\
	{ _prefix " CIDX", _base + 0x8, __VA_ARGS__ },	\
	{ _prefix " DIDX", _base + 0xc, __VA_ARGS__ },	\
	{ _prefix " Qcnt", _base , __VA_ARGS__ }


#define DUMP_RRO_DATA_RING(_prefix, _base, ...)			\
	{ _prefix " BASE", _base, __VA_ARGS__ },		\
	{ _prefix " CNT",  _base + 0x4, __VA_ARGS__ },	\
	{ _prefix " CIDX", _base + 0x8, __VA_ARGS__ },	\
	{ _prefix " DIDX", _base + 0xc, __VA_ARGS__ }

#define DUMP_WED(_reg) DUMP_REG(_reg, DUMP_TYPE_WED)
#define DUMP_WED_MASK(_reg, _mask) DUMP_REG_MASK(_reg, _mask)
#define DUMP_WED_RING(_base) DUMP_RING(#_base, MTK_##_base, DUMP_TYPE_WED_RING)
#define DUMP_WED_RING_RX_TYPE1(_base) DUMP_RING(#_base, MTK_##_base, DUMP_TYPE_WED_RING_RX_TYPE1)
#define DUMP_WED_RING_RX_TYPE2(_base) DUMP_RING(#_base, MTK_##_base, DUMP_TYPE_WED_RING_RX_TYPE2)
#define DUMP_WED_RRO_DATA_RING(_base) DUMP_RRO_DATA_RING(#_base, MTK_##_base, DUMP_TYPE_WED_RRO)

#define DUMP_WDMA(_reg) DUMP_REG(_reg, DUMP_TYPE_WDMA)
#define DUMP_WDMA_RING(_base) DUMP_RING(#_base, MTK_##_base, DUMP_TYPE_WDMA)
#define DUMP_WDMA_RING_RX(_base) DUMP_RING(#_base, MTK_##_base, DUMP_TYPE_WDMA_RX)

#define DUMP_WPDMA_TX_RING(_n) DUMP_RING("WPDMA_TX" #_n, 0, DUMP_TYPE_WPDMA_TX, _n)
#define DUMP_WPDMA_TXFREE_RING DUMP_RING("WPDMA_RX1", 0, DUMP_TYPE_WPDMA_TXFREE)
#define DUMP_WPDMA_RX_RING(_n)	DUMP_RING("WPDMA_RX" #_n, 0, DUMP_TYPE_WPDMA_RX, _n)
#define DUMP_WED_RRO_RING(_base)DUMP_RING("WED_RRO_MIOD", MTK_##_base, DUMP_TYPE_WED_RING)
#define DUMP_WED_RRO_FDBK(_base)DUMP_RING("WED_RRO_FDBK", MTK_##_base, DUMP_TYPE_WED_RING)
#define CAL_TX_QCNT(_cidx, _didx, _cnt) ((_cidx >= _didx) ? (_cidx - _didx) : (_cidx - _didx + _cnt))
#define CAL_RX_QCNT_TYPE1(_cidx, _didx, _cnt) (_didx > (_cidx & 0xfff) ? (_didx - 1 - (_cidx & 0xfff)) : (_didx - 1 - (_cidx & 0xfff) + _cnt))
#define CAL_RX_QCNT_TYPE2(_cidx, _didx, _cnt) ((_didx > _cidx) ? (_didx - 1 - _cidx) : (_didx - 1 - _cidx + _cnt))


static void
print_reg_val(struct seq_file *s, const char *name, u32 val)
{
	seq_printf(s, "%-32s %08x\n", name, val);
}

static void
dump_wed_regs(struct seq_file *s, struct mtk_wed_device *dev,
	      const struct reg_dump **regs)
{
	const struct reg_dump **cur_o = regs, *cur;
	bool newline = false;
	u32 val, cnt, cidx, didx;

	while (*cur_o) {
		cur = *cur_o;

		while (cur->type != DUMP_TYPE_END) {
			switch (cur->type) {
			case DUMP_TYPE_STRING:
				seq_printf(s, "%s======== %s:\n",
					   newline ? "\n" : "",
					   cur->name);
				newline = true;
				cur++;
				continue;
			case DUMP_TYPE_WED:
			case DUMP_TYPE_WED_RRO:
				val = wed_r32(dev, cur->offset);
				break;
			case DUMP_TYPE_WDMA:
				if (!strstr(cur->name, "Qcnt"))
					val = wdma_r32(dev, cur->offset);
				if (strstr(cur->name, "CNT")){
					cnt = val;
				}else if (strstr(cur->name, "CIDX"))
					cidx = val;
				else if (strstr(cur->name, "DIDX"))
					didx = val;
				if (strstr(cur->name, "Qcnt"))
					val = CAL_TX_QCNT(cidx, didx, cnt);
				break;
			case DUMP_TYPE_WDMA_RX:
				if (!strstr(cur->name, "Qcnt"))
					val = wdma_r32(dev, cur->offset);
				if (strstr(cur->name, "CNT")){
					cnt = val;
				}else if (strstr(cur->name, "CIDX"))
					cidx = val;
				else if (strstr(cur->name, "DIDX"))
					didx = val;
				if (strstr(cur->name, "Qcnt"))
					val = CAL_RX_QCNT_TYPE2(cidx, didx, cnt);
				break;
			case DUMP_TYPE_WPDMA_TX:
				if (!strstr(cur->name, "Qcnt"))
					val = wpdma_tx_r32(dev, cur->base, cur->offset);
				if (strstr(cur->name, "CNT"))
					cnt = val;
				else if (strstr(cur->name, "CIDX"))
					cidx = val;
				else if (strstr(cur->name, "DIDX"))
					didx = val;
				if (strstr(cur->name, "Qcnt"))
					val = CAL_TX_QCNT(cidx, didx, cnt);
				break;
			case DUMP_TYPE_WPDMA_TXFREE:
				if (!strstr(cur->name, "Qcnt"))
					val = wpdma_txfree_r32(dev, cur->offset);
				if (strstr(cur->name, "CNT"))
					cnt = val;
				else if (strstr(cur->name, "CIDX"))
					cidx = val;
				else if (strstr(cur->name, "DIDX"))
					didx = val;
				if (strstr(cur->name, "Qcnt"))
					val = CAL_RX_QCNT_TYPE2(cidx, didx, cnt);
				break;
			case DUMP_TYPE_WPDMA_RX:
				if (!strstr(cur->name, "Qcnt"))
					val = wpdma_rx_r32(dev, cur->base, cur->offset);
				if (strstr(cur->name, "CNT"))
					cnt = val;
				else if (strstr(cur->name, "CIDX"))
					cidx = val;
				else if (strstr(cur->name, "DIDX"))
					didx = val;
				if (strstr(cur->name, "Qcnt"))
					val = CAL_RX_QCNT_TYPE2(cidx, didx, cnt);
				break;
			case DUMP_TYPE_WED_RING:
				if (!strstr(cur->name, "Qcnt"))
					val = wed_r32(dev, cur->offset);
				if (strstr(cur->name, "CNT"))
					cnt = val;
				else if (strstr(cur->name, "CIDX"))
					cidx = val;
				else if (strstr(cur->name, "DIDX"))
					didx = val;
				if (strstr(cur->name, "Qcnt"))
					val = CAL_TX_QCNT(cidx, didx, cnt);
				break;
			case DUMP_TYPE_WED_RING_RX_TYPE1:
				if (!strstr(cur->name, "Qcnt"))
					val = wed_r32(dev, cur->offset);
				if (strstr(cur->name, "CNT"))
					cnt = val;
				else if (strstr(cur->name, "CIDX"))
					cidx = val;
				else if (strstr(cur->name, "DIDX"))
					didx = val;
				if (strstr(cur->name, "Qcnt"))
					val = CAL_RX_QCNT_TYPE1(cidx, didx, cnt);
				break;
			case DUMP_TYPE_WED_RING_RX_TYPE2:
				if (!strstr(cur->name, "Qcnt"))
					val = wed_r32(dev, cur->offset);
				if (strstr(cur->name, "CNT"))
					cnt = val;
				else if (strstr(cur->name, "CIDX"))
					cidx = val;
				else if (strstr(cur->name, "DIDX"))
					didx = val;
				if (strstr(cur->name, "Qcnt"))
					val = CAL_RX_QCNT_TYPE2(cidx, didx, cnt);
				break;
			}

			if (cur->mask)
				val = (cur->mask & val) >> (ffs(cur->mask) - 1);

			print_reg_val(s, cur->name, val);
			cur++;
		}
		cur_o++;
	}
}


static int
wed_txinfo_show(struct seq_file *s, void *data)
{
	static const struct reg_dump regs_common[] = {
		DUMP_STR("WED TX"),
		DUMP_WED(WED_TX_MIB(0)),
		DUMP_WED_RING(WED_RING_TX(0)),

		DUMP_WED(WED_TX_MIB(1)),
		DUMP_WED_RING(WED_RING_TX(1)),

		DUMP_STR("WPDMA TX"),
		DUMP_WED(WED_WPDMA_TX_MIB(0)),
		DUMP_WED_RING(WED_WPDMA_RING_TX(0)),
		DUMP_WED(WED_WPDMA_TX_COHERENT_MIB(0)),

		DUMP_WED(WED_WPDMA_TX_MIB(1)),
		DUMP_WED_RING(WED_WPDMA_RING_TX(1)),
		DUMP_WED(WED_WPDMA_TX_COHERENT_MIB(1)),

		DUMP_STR("WPDMA TX"),
		DUMP_WPDMA_TX_RING(0),
		DUMP_WPDMA_TX_RING(1),

		DUMP_STR("WPDMA RX"),
		DUMP_WPDMA_TXFREE_RING,

		DUMP_STR("WED WPDMA RX (TX FREE)"),
		DUMP_WED(WED_WPDMA_RX_MIB(0)),
		DUMP_WED_RING_RX_TYPE1(WED_WPDMA_RING_RX(0)),
		DUMP_WED(WED_WPDMA_RX_MIB(1)),
		DUMP_WED_RING_RX_TYPE1(WED_WPDMA_RING_RX(1)),
		DUMP_WED(WED_WPDMA_RX_COHERENT_MIB),
		DUMP_WED(WED_WPDMA_RX_EXTC_FRE),

		DUMP_STR("WED RX (TX FREE)"),
		DUMP_WED(WED_RX_MIB(0)),
		DUMP_WED_RING_RX_TYPE2(WED_RING_RX(0)),

		DUMP_WED(WED_RX_MIB(1)),
		DUMP_WED_RING_RX_TYPE2(WED_RING_RX(1)),

		DUMP_STR("WED WDMA RX"),
		DUMP_WED(WED_WDMA_RX_MIB(0)),
		DUMP_WED_RING_RX_TYPE1(WED_WDMA_RING_RX(0)),
		DUMP_WED(WED_WDMA_RX_THRES(0)),
		DUMP_WED(WED_WDMA_RX_RECYCLE_MIB(0)),
		DUMP_WED(WED_WDMA_RX_PROCESSED_MIB(0)),

		DUMP_WED(WED_WDMA_RX_MIB(1)),
		DUMP_WED_RING_RX_TYPE1(WED_WDMA_RING_RX(1)),
		DUMP_WED(WED_WDMA_RX_THRES(1)),
		DUMP_WED(WED_WDMA_RX_RECYCLE_MIB(1)),
		DUMP_WED(WED_WDMA_RX_PROCESSED_MIB(1)),

		DUMP_STR("WDMA RX"),
		DUMP_WDMA(WDMA_GLO_CFG),
		DUMP_WDMA_RING_RX(WDMA_RING_RX(0)),
		DUMP_WDMA_RING_RX(WDMA_RING_RX(1)),

		DUMP_END(),
	};

	static const struct reg_dump regs_v3[] = {
		DUMP_STR("Total Free Tx TKID number"),
		DUMP_WED(WED_TX_TKID_STATUS),
		DUMP_END(),
	};

	static const struct reg_dump *regs_com[] = {
		&regs_common[0],
		NULL,
	};

	static const struct reg_dump *regs_new_v3[] = {
		&regs_common[0],
		&regs_v3[0],
		NULL,
	};

	struct mtk_wed_hw *hw = s->private;
	struct mtk_wed_device *dev = hw->wed_dev;
	const struct reg_dump **regs;
	if (!dev)
		return 0;

	switch(dev->hw->version) {
	case 3:
		regs = regs_new_v3;
		break;
	default:
		regs = regs_com;
	}

	dump_wed_regs(s, dev, regs);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wed_txinfo);

static int
wed_rxinfo_show(struct seq_file *s, void *data)
{
	static const struct reg_dump regs_common[] = {
		DUMP_STR("WED RX INT info"),
		DUMP_WED(WED_PCIE_INT_CTRL),
		DUMP_WED(WED_PCIE_INT_REC),
		DUMP_WED(WED_WPDMA_INT_STA_REC),
		DUMP_WED(WED_WPDMA_INT_MON),
		DUMP_WED(WED_WPDMA_INT_CTRL),
		DUMP_WED(WED_WPDMA_INT_CTRL_TX),
		DUMP_WED(WED_WPDMA_INT_CTRL_RX),
		DUMP_WED(WED_WPDMA_INT_CTRL_TX_FREE),
		DUMP_WED(WED_WPDMA_STATUS),
		DUMP_WED(WED_WPDMA_D_ST),
		DUMP_WED(WED_WPDMA_RX_D_GLO_CFG),

		DUMP_STR("WED RX"),
		DUMP_WED_RING_RX_TYPE2(WED_RING_RX_DATA(0)),
		DUMP_WED_RING_RX_TYPE2(WED_RING_RX_DATA(1)),

		DUMP_STR("WPDMA RX"),
		DUMP_WPDMA_RX_RING(0),
		DUMP_WPDMA_RX_RING(1),

		DUMP_STR("WED WPDMA RX"),
		DUMP_WED_RING_RX_TYPE1(WED_WPDMA_RING_RX_DATA(0)),
		DUMP_WED_RING_RX_TYPE1(WED_WPDMA_RING_RX_DATA(1)),
		DUMP_WED(WED_WPDMA_RX_D_MIB(0)),
		DUMP_WED(WED_WPDMA_RX_D_MIB(1)),
		DUMP_WED(WED_WPDMA_RX_D_RECYCLE_MIB(0)),
		DUMP_WED(WED_WPDMA_RX_D_RECYCLE_MIB(1)),
		DUMP_WED(WED_WPDMA_RX_D_PROCESSED_MIB(0)),
		DUMP_WED(WED_WPDMA_RX_D_PROCESSED_MIB(1)),
		DUMP_WED(WED_WPDMA_RX_D_COHERENT_MIB),
		DUMP_WED(WED_WPDMA_RX_D_ERR_STATUS),


		DUMP_STR("WED WO RRO"),
		DUMP_WED_RRO_RING(WED_RROQM_MIOD_CTRL0),
		DUMP_WED(WED_RROQM_MID_MIB),
		DUMP_WED(WED_RROQM_MOD_MIB),
		DUMP_WED(WED_RROQM_MOD_COHERENT_MIB),
		DUMP_WED_RRO_FDBK(WED_RROQM_FDBK_CTRL0),
		DUMP_WED(WED_RROQM_FDBK_MIB),
		DUMP_WED(WED_RROQM_FDBK_COHERENT_MIB),
		DUMP_WED(WED_RROQM_FDBK_IND_MIB),
		DUMP_WED(WED_RROQM_FDBK_ENQ_MIB),
		DUMP_WED(WED_RROQM_FDBK_ANC_MIB),
		DUMP_WED(WED_RROQM_FDBK_ANC2H_MIB),


		DUMP_STR("WED WDMA TX"),
		DUMP_WED_RING(WED_WDMA_RING_TX),
		DUMP_WED(WED_WDMA_TX_MIB),

		DUMP_STR("WDMA TX"),
		DUMP_WDMA(WDMA_GLO_CFG),
		DUMP_WDMA_RING(WDMA_RING_TX(0)),
		DUMP_WDMA_RING(WDMA_RING_TX(1)),

		DUMP_STR("WED RX BM"),
		DUMP_WED(WED_RX_BM_BASE),
		DUMP_WED(WED_RX_BM_PTR),
		DUMP_WED_MASK(WED_RX_BM_PTR, WED_RX_BM_PTR_HEAD),
		DUMP_WED_MASK(WED_RX_BM_PTR, WED_RX_BM_PTR_TAIL),
		DUMP_END()
	};

	static const struct reg_dump regs_v2[] = {
		DUMP_STR("WED Route QM"),
		DUMP_WED(WED_RTQM_R2H_MIB(0)),
		DUMP_WED(WED_RTQM_R2Q_MIB(0)),
		DUMP_WED(WED_RTQM_Q2H_MIB(0)),
		DUMP_WED(WED_RTQM_R2H_MIB(1)),
		DUMP_WED(WED_RTQM_R2Q_MIB(1)),
		DUMP_WED(WED_RTQM_Q2H_MIB(1)),
		DUMP_WED(WED_RTQM_Q2N_MIB),
		DUMP_WED(WED_RTQM_Q2B_MIB),
		DUMP_WED(WED_RTQM_PFDBK_MIB),

		DUMP_END()
	};

	static const struct reg_dump regs_v3[] = {
		DUMP_STR("WED PG BM"),
		DUMP_WED(WED_RRO_PG_BM_BASE),
		DUMP_WED(WED_RRO_PG_BM_ADD_BASE_H),
		DUMP_WED(WED_RRO_PG_BM_PTR),
		DUMP_WED_MASK(WED_RRO_PG_BM_PTR, WED_RX_BM_PTR_HEAD),
		DUMP_WED_MASK(WED_RRO_PG_BM_PTR, WED_RX_BM_PTR_TAIL),
		DUMP_WED(WED_RRO_PG_BM_STATUS),
		DUMP_WED(WED_RRO_PG_BM_INTF),
		DUMP_WED(WED_RRO_PG_BM_ERR_STATUS),
		DUMP_WED(WED_RRO_PG_BM_OPT_CTRL),
		DUMP_WED(WED_RRO_PG_BM_TOTAL_DMAD),

		DUMP_STR("WED RX RRO DATA"),
		DUMP_WED_MASK(WED_RRO_RX_D_RX_CNT(0), WED_RRO_RX_D_RX_MAX_CNT),
		DUMP_WED_MASK(WED_RRO_RX_D_RX_CNT(0), WED_RRO_RX_D_RX_MAGIC_CNT),
		DUMP_WED_RRO_DATA_RING(WED_RRO_RX_D_RX(0)),
		DUMP_WED_MASK(WED_RRO_RX_D_RX_CNT(1), WED_RRO_RX_D_RX_MAX_CNT),
		DUMP_WED_MASK(WED_RRO_RX_D_RX_CNT(1), WED_RRO_RX_D_RX_MAGIC_CNT),
		DUMP_WED_RRO_DATA_RING(WED_RRO_RX_D_RX(1)),
		DUMP_WED(WED_RRO_RX_D_CFG(0)),
		DUMP_WED(WED_RRO_RX_D_CFG(1)),
		DUMP_WED(WED_RRO_RX_D_CFG(2)),

		DUMP_STR("WED RX MSDU PAGE"),
		DUMP_WED(WED_RRO_MSDU_PG_RING_CFG(0)),
		DUMP_WED(WED_RRO_MSDU_PG_RING_CFG1(0)),
		DUMP_WED(WED_RRO_MSDU_PG_RING_CFG(1)),
		DUMP_WED(WED_RRO_MSDU_PG_RING_CFG1(1)),
		DUMP_WED(WED_RRO_MSDU_PG_RING_CFG(2)),
		DUMP_WED(WED_RRO_MSDU_PG_RING_CFG1(2)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL0(0)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL1(0)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL2(0)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL0(1)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL1(1)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL2(1)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL0(2)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL1(2)),
		DUMP_WED(WED_RRO_MSDU_PG_CTRL2(2)),

		DUMP_STR("WED RX IND CMD"),
		DUMP_WED_MASK(RRO_IND_CMD_SIGNATURE, RRO_IND_CMD_VLD),
		DUMP_WED_MASK(RRO_IND_CMD_SIGNATURE, RRO_IND_CMD_MAGIC_CNT),
		DUMP_WED_MASK(RRO_IND_CMD_SIGNATURE, RRO_IND_CMD_SW_PROC_IDX),
		DUMP_WED_MASK(RRO_IND_CMD_SIGNATURE, RRO_IND_CMD_DMA_IDX),
		DUMP_WED_MASK(WED_IND_CMD_RX_CTRL0,
			      WED_IND_CMD_MAGIC_CNT_PROC_IDX),
		DUMP_WED_MASK(WED_IND_CMD_RX_CTRL0, WED_IND_CMD_MAGIC_CNT),
		DUMP_WED_MASK(WED_IND_CMD_RX_CTRL0,
			      WED_IND_CMD_PREFETCH_FREE_CNT),
		DUMP_WED_MASK(WED_IND_CMD_RX_CTRL0, WED_IND_CMD_PROC_IDX),
		DUMP_WED(WED_IND_CMD_RX_CTRL1),
		DUMP_WED_MASK(WED_IND_CMD_RX_CTRL2, WED_IND_CMD_BASE_M),
		DUMP_WED_MASK(WED_IND_CMD_RX_CTRL2, WED_IND_CMD_MAX_CNT),
		DUMP_WED(WED_RRO_CFG0),
		DUMP_WED_MASK(WED_RRO_CFG1, WED_RRO_CFG1_MAX_WIN_SZ),
		DUMP_WED_MASK(WED_RRO_CFG1, WED_RRO_CFG1_ACK_SN_BASE_M),
		DUMP_WED_MASK(WED_RRO_CFG1, WED_RRO_CFG1_PARTICL_SE_ID),

		DUMP_STR("WED ADDR ELEM"),
		DUMP_WED(WED_ADDR_ELEM_CFG0),
		DUMP_WED_MASK(WED_ADDR_ELEM_CFG1,
			      WED_ADDR_ELEM_PREFETCH_FREE_CNT),
		DUMP_WED_MASK(WED_ADDR_ELEM_CFG1,
			      WED_ADDR_ELEM_PARTICL_SE_ID_BASE_M),

		DUMP_STR("WED Route QM"),
		DUMP_WED(WED_RTQM_ENQ_I2Q_DMAD_CNT),
		DUMP_WED(WED_RTQM_ENQ_I2N_DMAD_CNT),
		DUMP_WED(WED_RTQM_ENQ_I2Q_PKT_CNT),
		DUMP_WED(WED_RTQM_ENQ_I2N_PKT_CNT),
		DUMP_WED(WED_RTQM_ENQ_USED_ENTRY_CNT),
		DUMP_WED(WED_RTQM_ENQ_ERR_CNT),

		DUMP_WED(WED_RTQM_DEQ_DMAD_CNT),
		DUMP_WED(WED_RTQM_DEQ_Q2I_DMAD_CNT),
		DUMP_WED(WED_RTQM_DEQ_PKT_CNT),
		DUMP_WED(WED_RTQM_DEQ_Q2I_PKT_CNT),
		DUMP_WED(WED_RTQM_DEQ_USED_PFDBK_CNT),
		DUMP_WED(WED_RTQM_DEQ_ERR_CNT),

		DUMP_END()
	};

	static const struct reg_dump *regs_new_v2[] = {
		&regs_common[0],
		&regs_v2[0],
		NULL,
	};

	static const struct reg_dump *regs_new_v3[] = {
		&regs_common[0],
		&regs_v3[0],
		NULL,
	};

	struct mtk_wed_hw *hw = s->private;
	struct mtk_wed_device *dev = hw->wed_dev;
	const struct reg_dump **regs;

	if (!dev)
		return 0;

	switch(dev->hw->version) {
	case 2:
		regs = regs_new_v2;
		break;
	case 3:
		regs = regs_new_v3;
		break;
	default:
		return 0;
	}

	dump_wed_regs(s, dev, regs);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wed_rxinfo);

static int
mtk_wed_reg_set(void *data, u64 val)
{
	struct mtk_wed_hw *hw = data;

	regmap_write(hw->regs, hw->debugfs_reg, val);

	return 0;
}

static int
mtk_wed_reg_get(void *data, u64 *val)
{
	struct mtk_wed_hw *hw = data;
	unsigned int regval;
	int ret;

	ret = regmap_read(hw->regs, hw->debugfs_reg, &regval);
	if (ret)
		return ret;

	*val = regval;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_regval, mtk_wed_reg_get, mtk_wed_reg_set,
             "0x%08llx\n");

static int
wed_token_txd_show(struct seq_file *s, void *data)
{
	struct mtk_wed_hw *hw = s->private;
	struct mtk_wed_device *dev = hw->wed_dev;
	struct mtk_wed_buf *page_list = dev->tx_buf_ring.pages;
	int token = dev->wlan.token_start;
	u32 val = hw->token_id, size = 1;
	int page_idx = (val - token) / 2;
	int i;

	if (val < token) {
		size = val;
		page_idx = 0;
	}

	for (i = 0; i < size; i += MTK_WED_BUF_PER_PAGE) {
		void *page = page_list[page_idx++].p;
		void *buf;
		int j;

		if (!page)
			break;

		buf = page_to_virt(page);

		for (j = 0; j < MTK_WED_BUF_PER_PAGE; j++) {
			printk("[TXD]:token id = %d\n", token + 2 * (page_idx - 1) + j);
			print_hex_dump(KERN_ERR , "", DUMP_PREFIX_OFFSET, 16, 1, (u8 *)buf, 128, false);
			seq_printf(s, "\n");

			buf += MTK_WED_BUF_SIZE;
		}
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(wed_token_txd);

static int
wed_amsdu_show(struct seq_file *s, void *data)
{
	static const struct reg_dump regs_common[] = {
		DUMP_STR("WED AMDSU INFO"),
		DUMP_WED(WED_MON_AMSDU_FIFO_DMAD),

		DUMP_STR("WED AMDSU ENG0 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(0)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(0)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(0)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(0)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(0)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(0),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(0),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(0),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(0),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(0),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED AMDSU ENG1 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(1)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(1)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(1)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(1)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(1)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(1),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(1),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(1),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(2),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(2),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED AMDSU ENG2 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(2)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(2)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(2)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(2)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(2)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(2),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(2),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(2),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(2),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(2),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED AMDSU ENG3 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(3)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(3)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(3)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(3)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(3)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(3),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(3),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(3),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(3),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(3),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED AMDSU ENG4 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(4)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(4)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(4)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(4)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(4)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(4),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(4),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(4),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(4),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(4),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED AMDSU ENG5 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(5)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(5)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(5)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(5)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(5)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(5),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(5),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(5),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(5),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(5),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED AMDSU ENG6 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(6)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(6)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(6)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(6)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(6)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(6),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(6),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(6),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(6),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(6),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED AMDSU ENG7 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(7)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(7)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(7)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(7)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(7)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(7),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(7),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(7),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(7),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(4),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED AMDSU ENG8 INFO"),
		DUMP_WED(WED_MON_AMSDU_ENG_DMAD(8)),
		DUMP_WED(WED_MON_AMSDU_ENG_QFPL(8)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENI(8)),
		DUMP_WED(WED_MON_AMSDU_ENG_QENO(8)),
		DUMP_WED(WED_MON_AMSDU_ENG_MERG(8)),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(8),
			      WED_AMSDU_ENG_MAX_PL_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT8(8),
			      WED_AMSDU_ENG_MAX_QGPP_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(8),
			      WED_AMSDU_ENG_CUR_ENTRY),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(8),
			      WED_AMSDU_ENG_MAX_BUF_MERGED),
		DUMP_WED_MASK(WED_MON_AMSDU_ENG_CNT9(8),
			      WED_AMSDU_ENG_MAX_MSDU_MERGED),

		DUMP_STR("WED QMEM INFO"),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(0), WED_AMSDU_QMEM_FQ_CNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(0), WED_AMSDU_QMEM_SP_QCNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(1), WED_AMSDU_QMEM_TID0_QCNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(1), WED_AMSDU_QMEM_TID1_QCNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(2), WED_AMSDU_QMEM_TID2_QCNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(2), WED_AMSDU_QMEM_TID3_QCNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(3), WED_AMSDU_QMEM_TID4_QCNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(3), WED_AMSDU_QMEM_TID5_QCNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(4), WED_AMSDU_QMEM_TID6_QCNT),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_CNT(4), WED_AMSDU_QMEM_TID7_QCNT),

		DUMP_STR("WED QMEM HEAD INFO"),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(0), WED_AMSDU_QMEM_FQ_HEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(0), WED_AMSDU_QMEM_SP_QHEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(1), WED_AMSDU_QMEM_TID0_QHEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(1), WED_AMSDU_QMEM_TID1_QHEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(2), WED_AMSDU_QMEM_TID2_QHEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(2), WED_AMSDU_QMEM_TID3_QHEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(3), WED_AMSDU_QMEM_TID4_QHEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(3), WED_AMSDU_QMEM_TID5_QHEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(4), WED_AMSDU_QMEM_TID6_QHEAD),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(4), WED_AMSDU_QMEM_TID7_QHEAD),

		DUMP_STR("WED QMEM TAIL INFO"),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(5), WED_AMSDU_QMEM_FQ_TAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(5), WED_AMSDU_QMEM_SP_QTAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(6), WED_AMSDU_QMEM_TID0_QTAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(6), WED_AMSDU_QMEM_TID1_QTAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(7), WED_AMSDU_QMEM_TID2_QTAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(7), WED_AMSDU_QMEM_TID3_QTAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(8), WED_AMSDU_QMEM_TID4_QTAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(8), WED_AMSDU_QMEM_TID5_QTAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(9), WED_AMSDU_QMEM_TID6_QTAIL),
		DUMP_WED_MASK(WED_MON_AMSDU_QMEM_PTR(9), WED_AMSDU_QMEM_TID7_QTAIL),

		DUMP_STR("WED HIFTXD BUFF NUM"),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(1)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(2)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(3)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(4)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(5)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(6)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(7)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(8)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(9)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(10)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(11)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(12)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_BUFF(13)),

		DUMP_STR("WED HIFTXD MSDU INFO"),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(1)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(2)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(3)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(4)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(5)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(6)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(7)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(8)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(9)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(10)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(11)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(12)),
		DUMP_WED(WED_MON_AMSDU_HIFTXD_FETCH_MSDU(13)),

		DUMP_END()
	};

	static const struct reg_dump *regs[] = {
		&regs_common[0],
		NULL,
	};
	struct mtk_wed_hw *hw = s->private;
	struct mtk_wed_device *dev = hw->wed_dev;

	if (!dev)
		return 0;

	dump_wed_regs(s, dev, regs);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wed_amsdu);

static int
wed_rtqm_show(struct seq_file *s, void *data)
{
	static const struct reg_dump regs_common[] = {
		DUMP_STR("WED Route QM IGRS0(N2H + Recycle)"),
		DUMP_WED(WED_RTQM_IGRS0_I2HW_DMAD_CNT),
		DUMP_WED(WED_RTQM_IGRS0_I2H_DMAD_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS0_I2H_DMAD_CNT(1)),
		DUMP_WED(WED_RTQM_IGRS0_I2HW_PKT_CNT),
		DUMP_WED(WED_RTQM_IGRS0_I2H_PKT_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS0_I2H_PKT_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS0_FDROP_CNT),


		DUMP_STR("WED Route QM IGRS1(Legacy)"),
		DUMP_WED(WED_RTQM_IGRS1_I2HW_DMAD_CNT),
		DUMP_WED(WED_RTQM_IGRS1_I2H_DMAD_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS1_I2H_DMAD_CNT(1)),
		DUMP_WED(WED_RTQM_IGRS1_I2HW_PKT_CNT),
		DUMP_WED(WED_RTQM_IGRS1_I2H_PKT_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS1_I2H_PKT_CNT(1)),
		DUMP_WED(WED_RTQM_IGRS1_FDROP_CNT),

		DUMP_STR("WED Route QM IGRS2(RRO3.0)"),
		DUMP_WED(WED_RTQM_IGRS2_I2HW_DMAD_CNT),
		DUMP_WED(WED_RTQM_IGRS2_I2H_DMAD_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS2_I2H_DMAD_CNT(1)),
		DUMP_WED(WED_RTQM_IGRS2_I2HW_PKT_CNT),
		DUMP_WED(WED_RTQM_IGRS2_I2H_PKT_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS2_I2H_PKT_CNT(1)),
		DUMP_WED(WED_RTQM_IGRS2_FDROP_CNT),

		DUMP_STR("WED Route QM IGRS3(DEBUG)"),
		DUMP_WED(WED_RTQM_IGRS2_I2HW_DMAD_CNT),
		DUMP_WED(WED_RTQM_IGRS3_I2H_DMAD_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS3_I2H_DMAD_CNT(1)),
		DUMP_WED(WED_RTQM_IGRS3_I2HW_PKT_CNT),
		DUMP_WED(WED_RTQM_IGRS3_I2H_PKT_CNT(0)),
		DUMP_WED(WED_RTQM_IGRS3_I2H_PKT_CNT(1)),
		DUMP_WED(WED_RTQM_IGRS3_FDROP_CNT),

		DUMP_END()
	};

	static const struct reg_dump *regs[] = {
		&regs_common[0],
		NULL,
	};
	struct mtk_wed_hw *hw = s->private;
	struct mtk_wed_device *dev = hw->wed_dev;

	if (!dev)
		return 0;

	dump_wed_regs(s, dev, regs);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wed_rtqm);


static int
wed_rro_show(struct seq_file *s, void *data)
{
	static const struct reg_dump regs_common[] = {
		DUMP_STR("RRO/IND CMD CNT"),
		DUMP_WED(WED_RX_IND_CMD_CNT(1)),
		DUMP_WED(WED_RX_IND_CMD_CNT(2)),
		DUMP_WED(WED_RX_IND_CMD_CNT(3)),
		DUMP_WED(WED_RX_IND_CMD_CNT(4)),
		DUMP_WED(WED_RX_IND_CMD_CNT(5)),
		DUMP_WED(WED_RX_IND_CMD_CNT(6)),
		DUMP_WED(WED_RX_IND_CMD_CNT(7)),
		DUMP_WED(WED_RX_IND_CMD_CNT(8)),
		DUMP_WED_MASK(WED_RX_IND_CMD_CNT(9),
			      WED_IND_CMD_MAGIC_CNT_FAIL_CNT),

		DUMP_WED(WED_RX_ADDR_ELEM_CNT(0)),
		DUMP_WED_MASK(WED_RX_ADDR_ELEM_CNT(1),
			      WED_ADDR_ELEM_SIG_FAIL_CNT),
		DUMP_WED(WED_RX_MSDU_PG_CNT(1)),
		DUMP_WED(WED_RX_MSDU_PG_CNT(2)),
		DUMP_WED(WED_RX_MSDU_PG_CNT(3)),
		DUMP_WED(WED_RX_MSDU_PG_CNT(4)),
		DUMP_WED(WED_RX_MSDU_PG_CNT(5)),
		DUMP_WED_MASK(WED_RX_PN_CHK_CNT,
			      WED_PN_CHK_FAIL_CNT),

		DUMP_END()
	};

	static const struct reg_dump *regs[] = {
		&regs_common[0],
		NULL,
	};
	struct mtk_wed_hw *hw = s->private;
	struct mtk_wed_device *dev = hw->wed_dev;

	if (!dev)
		return 0;

	dump_wed_regs(s, dev, regs);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wed_rro);

static int
wed_hw_cfg_show(struct seq_file *s, void *data)
{
	static const struct reg_dump regs_common[] = {
		DUMP_STR("WED basic info"),
		DUMP_WED(WED_REV_ID),
		DUMP_WED(WED_CTRL),
		DUMP_WED(WED_CTRL2),
		DUMP_WED(WED_EXT_INT_STATUS),
		DUMP_WED(WED_EXT_INT_MASK),
		DUMP_WED(WED_STATUS),
		DUMP_WED(WED_GLO_CFG),
		DUMP_WED(WED_INT_STATUS),
		DUMP_WED(WED_INT_MASK),
		DUMP_WED(WED_AXI_CTRL),

		DUMP_STR("WED TX buf info"),
		DUMP_WED(WED_BM_STATUS),
		DUMP_WED(WED_TX_BM_BASE),
		DUMP_WED(WED_TX_BM_CTRL),
		DUMP_WED(WED_TX_BM_STATUS),
		DUMP_WED(WED_TX_BM_DYN_THR),
		DUMP_WED(WED_TX_BM_RECYC),
		DUMP_WED(WED_TX_TKID_CTRL),
		DUMP_WED(WED_TX_TKID_TKID),
		DUMP_WED(WED_TX_TKID_DYN_THR),
		DUMP_WED(WED_TX_TKID_INTF),
		DUMP_WED(WED_TX_TKID_RECYC),
		DUMP_WED(WED_TX_FREE_TO_TX_TKID_TKID_MIB),
		DUMP_WED(WED_TX_BM_TO_WDMA_RX_DRV_SKBID_MIB),
		DUMP_WED(WED_TX_TKID_TO_TX_BM_FREE_SKBID_MIB),

		DUMP_STR("WED RX BM info"),
		DUMP_WED(WED_RX_BM_RX_DMAD),
		DUMP_WED(WED_RX_BM_BASE),
		DUMP_WED(WED_RX_BM_INIT_PTR),
		DUMP_WED(WED_RX_BM_PTR),
		DUMP_WED(WED_RX_BM_BLEN),
		DUMP_WED(WED_RX_BM_STS),
		DUMP_WED(WED_RX_BM_INTF2),
		DUMP_WED(WED_RX_BM_INTF),
		DUMP_WED(WED_RX_BM_ERR_STS),

		DUMP_STR("WED RRO QM"),
		DUMP_WED(WED_RROQM_GLO_CFG),
		DUMP_WED(WED_RROQM_MIOD_CTRL0),
		DUMP_WED(WED_RROQM_MIOD_CTRL1),
		DUMP_WED(WED_RROQM_MIOD_CTRL2),
		DUMP_WED(WED_RROQM_MIOD_CTRL3),
		DUMP_WED(WED_RROQM_FDBK_CTRL0),
		DUMP_WED(WED_RROQM_FDBK_CTRL1),
		DUMP_WED(WED_RROQM_FDBK_CTRL2),
		DUMP_WED(WED_RROQM_FDBK_CTRL3),
		DUMP_WED(WED_RROQ_BASE_L),
		DUMP_WED(WED_RROQ_BASE_H),
		DUMP_WED(WED_RROQM_MIOD_CFG),

		DUMP_STR("WED PCI Host Control"),
		DUMP_WED(WED_PCIE_CFG_BASE),
		DUMP_WED(WED_PCIE_CFG_INTM),
		DUMP_WED(WED_PCIE_INT_TRIGGER),
		DUMP_WED(WED_PCIE_INT_REC),
		DUMP_WED(WED_PCIE_INTM_REC),
		DUMP_WED(WED_PCIE_INT_CTRL),

		DUMP_STR("WED_WPDMA basic info"),
		DUMP_WED(WED_WPDMA_STATUS),
		DUMP_WED(WED_WPDMA_INT_STA_REC),
		DUMP_WED(WED_WPDMA_GLO_CFG),
		DUMP_WED(WED_WPDMA_CFG_BASE),
		DUMP_WED(WED_WPDMA_CFG_INT_MASK),
		DUMP_WED(WED_WPDMA_CFG_TX),
		DUMP_WED(WED_WPDMA_CFG_TX_FREE),
		DUMP_WED(WED_WPDMA_CTRL),
		DUMP_WED(WED_WPDMA_RX_GLO_CFG),
		DUMP_WED(WED_WPDMA_RX_RING0),
		DUMP_WED(WED_WPDMA_RX_RING1),

		DUMP_STR("WED_WDMA basic info"),
		DUMP_WED(WED_WDMA_STATUS),
		DUMP_WED(WED_WDMA_INFO),
		DUMP_WED(WED_WDMA_GLO_CFG),
		DUMP_WED(WED_WDMA_RESET_IDX),
		DUMP_WED(WED_WDMA_LOAD_DRV_IDX),
		DUMP_WED(WED_WDMA_LOAD_CRX_IDX),
		DUMP_WED(WED_WDMA_SPR),
		DUMP_WED(WED_WDMA_INT_STA_REC),
		DUMP_WED(WED_WDMA_INT_TRIGGER),
		DUMP_WED(WED_WDMA_INT_CTRL),
		DUMP_WED(WED_WDMA_INT_CLR),
		DUMP_WED(WED_WDMA_CFG_BASE),
		DUMP_WED(WED_WDMA_OFFSET0),
		DUMP_WED(WED_WDMA_OFFSET1),

		DUMP_STR("WDMA basic info"),
		DUMP_WDMA(WDMA_GLO_CFG),
		DUMP_WDMA(WDMA_INT_MASK),
		DUMP_WDMA(WDMA_INT_STATUS),
		DUMP_WDMA(WDMA_INFO),
		DUMP_WDMA(WDMA_FREEQ_THRES),
		DUMP_WDMA(WDMA_INT_STS_GRP0),
		DUMP_WDMA(WDMA_INT_STS_GRP1),
		DUMP_WDMA(WDMA_INT_STS_GRP2),
		DUMP_WDMA(WDMA_INT_GRP1),
		DUMP_WDMA(WDMA_INT_GRP2),
		DUMP_WDMA(WDMA_SCH_Q01_CFG),
		DUMP_WDMA(WDMA_SCH_Q23_CFG),

		DUMP_END()
	};

	static const struct reg_dump *regs[] = {
		&regs_common[0],
		NULL,
	};
	struct mtk_wed_hw *hw = s->private;
	struct mtk_wed_device *dev = hw->wed_dev;

	if (!dev)
		return 0;

	dump_wed_regs(s, dev, regs);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wed_hw_cfg);

void mtk_wed_hw_add_debugfs(struct mtk_wed_hw *hw)
{
	struct dentry *dir;

	snprintf(hw->dirname, sizeof(hw->dirname), "wed%d", hw->index);
	dir = debugfs_create_dir(hw->dirname, NULL);

	hw->debugfs_dir = dir;
	debugfs_create_u32("regidx", 0600, dir, &hw->debugfs_reg);
	debugfs_create_file_unsafe("regval", 0600, dir, hw, &fops_regval);
	debugfs_create_file_unsafe("txinfo", 0400, dir, hw, &wed_txinfo_fops);
	debugfs_create_u32("token_id", 0600, dir, &hw->token_id);
	debugfs_create_file_unsafe("token_txd", 0600, dir, hw, &wed_token_txd_fops);

	if (!mtk_wed_is_v1(hw)) {
		debugfs_create_file_unsafe("rxinfo", 0400, dir, hw,
					   &wed_rxinfo_fops);

		debugfs_create_file_unsafe("cfg", 0600, dir, hw, &wed_hw_cfg_fops);
		wed_wo_mcu_debugfs(hw, dir);
		if (mtk_wed_is_v3_or_greater(hw)) {
			debugfs_create_file_unsafe("amsdu", 0400, dir, hw,
						   &wed_amsdu_fops);
			debugfs_create_file_unsafe("rtqm", 0400, dir, hw,
						   &wed_rtqm_fops);
			debugfs_create_file_unsafe("rro", 0400, dir, hw,
						   &wed_rro_fops);
		}
	}
}
