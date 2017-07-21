/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	"LCDB: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>

#define QPNP_LCDB_REGULATOR_DRIVER_NAME		"qcom,qpnp-lcdb-regulator"

/* LCDB */
#define LCDB_STS1_REG			0x08

#define INT_RT_STATUS_REG		0x10
#define VREG_OK_RT_STS_BIT		BIT(0)

#define LCDB_AUTO_TOUCH_WAKE_CTL_REG	0x40
#define EN_AUTO_TOUCH_WAKE_BIT		BIT(7)
#define ATTW_TOFF_TIME_MASK		GENMASK(3, 2)
#define ATTW_TON_TIME_MASK		GENMASK(1, 0)
#define ATTW_TOFF_TIME_SHIFT		2
#define ATTW_MIN_MS			4
#define ATTW_MAX_MS			32

#define LCDB_BST_OUTPUT_VOLTAGE_REG	0x41

#define LCDB_MODULE_RDY_REG		0x45
#define MODULE_RDY_BIT			BIT(7)

#define LCDB_ENABLE_CTL1_REG		0x46
#define MODULE_EN_BIT			BIT(7)
#define HWEN_RDY_BIT			BIT(6)

/* BST */
#define LCDB_BST_PD_CTL_REG		0x47
#define BOOST_DIS_PULLDOWN_BIT		BIT(1)
#define BOOST_PD_STRENGTH_BIT		BIT(0)

#define LCDB_BST_ILIM_CTL_REG		0x4B
#define EN_BST_ILIM_BIT			BIT(7)
#define SET_BST_ILIM_MASK		GENMASK(2, 0)
#define MIN_BST_ILIM_MA			200
#define MAX_BST_ILIM_MA			1600

#define LCDB_PS_CTL_REG			0x50
#define EN_PS_BIT			BIT(7)
#define PS_THRESHOLD_MASK		GENMASK(1, 0)
#define MIN_BST_PS_MA			50
#define MAX_BST_PS_MA			80

#define LCDB_RDSON_MGMNT_REG		0x53
#define NFET_SW_SIZE_MASK		GENMASK(3, 2)
#define NFET_SW_SIZE_SHIFT		2
#define PFET_SW_SIZE_MASK		GENMASK(1, 0)

#define LCDB_BST_VREG_OK_CTL_REG	0x55
#define BST_VREG_OK_DEB_MASK		GENMASK(1, 0)

#define LCDB_SOFT_START_CTL_REG		0x5F

#define LCDB_MISC_CTL_REG		0x60
#define AUTO_GM_EN_BIT			BIT(4)
#define EN_TOUCH_WAKE_BIT		BIT(3)
#define DIS_SCP_BIT			BIT(0)

#define LCDB_PFM_CTL_REG		0x62
#define EN_PFM_BIT			BIT(7)
#define BYP_BST_SOFT_START_COMP_BIT	BIT(0)
#define PFM_HYSTERESIS_SHIFT		4
#define PFM_CURRENT_SHIFT		2

#define LCDB_PWRUP_PWRDN_CTL_REG	0x66

/* LDO */
#define LCDB_LDO_OUTPUT_VOLTAGE_REG	0x71
#define SET_OUTPUT_VOLTAGE_MASK		GENMASK(4, 0)

#define LCDB_LDO_VREG_OK_CTL_REG	0x75
#define VREG_OK_DEB_MASK		GENMASK(1, 0)

#define LCDB_LDO_PD_CTL_REG		0x77
#define LDO_DIS_PULLDOWN_BIT		BIT(1)
#define LDO_PD_STRENGTH_BIT		BIT(0)

#define LCDB_LDO_ILIM_CTL1_REG		0x7B
#define EN_LDO_ILIM_BIT			BIT(7)
#define SET_LDO_ILIM_MASK		GENMASK(2, 0)
#define MIN_LDO_ILIM_MA			110
#define MAX_LDO_ILIM_MA			460
#define LDO_ILIM_STEP_MA		50

#define LCDB_LDO_ILIM_CTL2_REG		0x7C

#define LCDB_LDO_SOFT_START_CTL_REG	0x7F
#define SOFT_START_MASK			GENMASK(1, 0)

/* NCP */
#define LCDB_NCP_OUTPUT_VOLTAGE_REG	0x81

#define LCDB_NCP_VREG_OK_CTL_REG	0x85

#define LCDB_NCP_PD_CTL_REG		0x87
#define NCP_DIS_PULLDOWN_BIT		BIT(1)
#define NCP_PD_STRENGTH_BIT		BIT(0)

#define LCDB_NCP_ILIM_CTL1_REG		0x8B
#define EN_NCP_ILIM_BIT			BIT(7)
#define SET_NCP_ILIM_MASK		GENMASK(1, 0)
#define MIN_NCP_ILIM_MA			260
#define MAX_NCP_ILIM_MA			810

#define LCDB_NCP_ILIM_CTL2_REG		0x8C

#define LCDB_NCP_SOFT_START_CTL_REG	0x8F

/* common for BST/NCP/LDO */
#define MIN_DBC_US			2
#define MAX_DBC_US			32

#define MIN_SOFT_START_US		0
#define MAX_SOFT_START_US		2000

struct ldo_regulator {
	struct regulator_desc		rdesc;
	struct regulator_dev		*rdev;
	struct device_node		*node;

	/* LDO DT params */
	int				pd;
	int				pd_strength;
	int				ilim_ma;
	int				soft_start_us;
	int				vreg_ok_dbc_us;
	int				voltage_mv;
};

struct ncp_regulator {
	struct regulator_desc		rdesc;
	struct regulator_dev		*rdev;
	struct device_node		*node;

	/* NCP DT params */
	int				pd;
	int				pd_strength;
	int				ilim_ma;
	int				soft_start_us;
	int				vreg_ok_dbc_us;
	int				voltage_mv;
};

struct bst_params {
	struct device_node		*node;

	/* BST DT params */
	int				pd;
	int				pd_strength;
	int				ilim_ma;
	int				ps;
	int				ps_threshold;
	int				soft_start_us;
	int				vreg_ok_dbc_us;
	int				voltage_mv;
};

struct qpnp_lcdb {
	struct device			*dev;
	struct platform_device		*pdev;
	struct regmap			*regmap;
	u32				base;

	/* TTW params */
	bool				ttw_enable;
	bool				ttw_mode_sw;

	/* top level DT params */
	bool				force_module_reenable;

	/* status parameters */
	bool				lcdb_enabled;
	bool				settings_saved;

	struct mutex			lcdb_mutex;
	struct mutex			read_write_mutex;
	struct bst_params		bst;
	struct ldo_regulator		ldo;
	struct ncp_regulator		ncp;
};

struct settings {
	u16	address;
	u8	value;
	bool	sec_access;
};

enum lcdb_module {
	LDO,
	NCP,
	BST,
};

enum pfm_hysteresis {
	PFM_HYST_15MV,
	PFM_HYST_25MV,
	PFM_HYST_35MV,
	PFM_HYST_45MV,
};

enum pfm_peak_current {
	PFM_PEAK_CURRENT_300MA,
	PFM_PEAK_CURRENT_400MA,
	PFM_PEAK_CURRENT_500MA,
	PFM_PEAK_CURRENT_600MA,
};

enum rdson_fet_size {
	RDSON_QUARTER,
	RDSON_HALF,
	RDSON_THREE_FOURTH,
	RDSON_FULLSIZE,
};

enum lcdb_settings_index {
	LCDB_BST_PD_CTL = 0,
	LCDB_RDSON_MGMNT,
	LCDB_MISC_CTL,
	LCDB_SOFT_START_CTL,
	LCDB_PFM_CTL,
	LCDB_PWRUP_PWRDN_CTL,
	LCDB_LDO_PD_CTL,
	LCDB_LDO_SOFT_START_CTL,
	LCDB_NCP_PD_CTL,
	LCDB_NCP_SOFT_START_CTL,
	LCDB_SETTING_MAX,
};

static u32 soft_start_us[] = {
	0,
	500,
	1000,
	2000,
};

static u32 dbc_us[] = {
	2,
	4,
	16,
	32,
};

static u32 ncp_ilim_ma[] = {
	260,
	460,
	640,
	810,
};

#define SETTING(_id, _sec_access)		\
	[_id] = {				\
		.address = _id##_REG,		\
		.sec_access = _sec_access,	\
	}					\

static bool is_between(int value, int min, int max)
{
	if (value < min || value > max)
		return false;
	return true;
}

static int qpnp_lcdb_read(struct qpnp_lcdb *lcdb,
			u16 addr, u8 *value, u8 count)
{
	int rc = 0;

	mutex_lock(&lcdb->read_write_mutex);
	rc = regmap_bulk_read(lcdb->regmap, addr, value, count);
	if (rc < 0)
		pr_err("Failed to read from addr=0x%02x rc=%d\n", addr, rc);
	mutex_unlock(&lcdb->read_write_mutex);

	return rc;
}

static int qpnp_lcdb_write(struct qpnp_lcdb *lcdb,
			u16 addr, u8 *value, u8 count)
{
	int rc;

	mutex_lock(&lcdb->read_write_mutex);
	rc = regmap_bulk_write(lcdb->regmap, addr, value, count);
	if (rc < 0)
		pr_err("Failed to write to addr=0x%02x rc=%d\n", addr, rc);
	mutex_unlock(&lcdb->read_write_mutex);

	return rc;
}

#define SEC_ADDRESS_REG			0xD0
#define SECURE_UNLOCK_VALUE		0xA5
static int qpnp_lcdb_secure_write(struct qpnp_lcdb *lcdb,
					u16 addr, u8 value)
{
	int rc;
	u8 val = SECURE_UNLOCK_VALUE;

	mutex_lock(&lcdb->read_write_mutex);
	rc = regmap_write(lcdb->regmap, lcdb->base + SEC_ADDRESS_REG, val);
	if (rc < 0) {
		pr_err("Failed to unlock register rc=%d\n", rc);
		goto fail_write;
	}
	rc = regmap_write(lcdb->regmap, addr, value);
	if (rc < 0)
		pr_err("Failed to write to addr=0x%02x rc=%d\n", addr, rc);

fail_write:
	mutex_unlock(&lcdb->read_write_mutex);
	return rc;
}

static int qpnp_lcdb_masked_write(struct qpnp_lcdb *lcdb,
				u16 addr, u8 mask, u8 value)
{
	int rc = 0;

	mutex_lock(&lcdb->read_write_mutex);
	rc = regmap_update_bits(lcdb->regmap, addr, mask, value);
	if (rc < 0)
		pr_err("Failed to write addr=0x%02x value=0x%02x rc=%d\n",
			addr, value, rc);
	mutex_unlock(&lcdb->read_write_mutex);

	return rc;
}

static bool is_lcdb_enabled(struct qpnp_lcdb *lcdb)
{
	int rc;
	u8 val = 0;

	rc = qpnp_lcdb_read(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG, &val, 1);
	if (rc < 0)
		pr_err("Failed to read ENABLE_CTL1 rc=%d\n", rc);

	return rc ? false : !!(val & MODULE_EN_BIT);
}

static int dump_status_registers(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u8 sts[6] = {0};

	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_STS1_REG, &sts[0], 6);
	if (rc < 0) {
		pr_err("Failed to write to STS registers rc=%d\n", rc);
	} else {
		rc = qpnp_lcdb_read(lcdb, lcdb->base + LCDB_STS1_REG, sts, 6);
		if (rc < 0)
			pr_err("Failed to read lcdb status rc=%d\n", rc);
		else
			pr_err("STS1=0x%02x STS2=0x%02x STS3=0x%02x STS4=0x%02x STS5=0x%02x, STS6=0x%02x\n",
				sts[0], sts[1], sts[2], sts[3], sts[4], sts[5]);
	}

	return rc;
}

static struct settings lcdb_settings[] = {
	SETTING(LCDB_BST_PD_CTL, false),
	SETTING(LCDB_RDSON_MGMNT, false),
	SETTING(LCDB_MISC_CTL, false),
	SETTING(LCDB_SOFT_START_CTL, false),
	SETTING(LCDB_PFM_CTL, false),
	SETTING(LCDB_PWRUP_PWRDN_CTL, true),
	SETTING(LCDB_LDO_PD_CTL, false),
	SETTING(LCDB_LDO_SOFT_START_CTL, false),
	SETTING(LCDB_NCP_PD_CTL, false),
	SETTING(LCDB_NCP_SOFT_START_CTL, false),
};

static int qpnp_lcdb_save_settings(struct qpnp_lcdb *lcdb)
{
	int i, rc = 0;

	for (i = 0; i < ARRAY_SIZE(lcdb_settings); i++) {
		rc = qpnp_lcdb_read(lcdb, lcdb->base +
				lcdb_settings[i].address,
				&lcdb_settings[i].value, 1);
		if (rc < 0) {
			pr_err("Failed to read lcdb register address=%x\n",
						lcdb_settings[i].address);
			return rc;
		}
	}

	return rc;
}

static int qpnp_lcdb_restore_settings(struct qpnp_lcdb *lcdb)
{
	int i, rc = 0;

	for (i = 0; i < ARRAY_SIZE(lcdb_settings); i++) {
		if (lcdb_settings[i].sec_access)
			rc = qpnp_lcdb_secure_write(lcdb, lcdb->base +
					lcdb_settings[i].address,
					lcdb_settings[i].value);
		else
			rc = qpnp_lcdb_write(lcdb, lcdb->base +
					lcdb_settings[i].address,
					&lcdb_settings[i].value, 1);
		if (rc < 0) {
			pr_err("Failed to write register address=%x\n",
						lcdb_settings[i].address);
			return rc;
		}
	}

	return rc;
}

static int qpnp_lcdb_ttw_enter(struct qpnp_lcdb *lcdb)
{
	int rc;
	u8 val;

	if (!lcdb->settings_saved) {
		rc = qpnp_lcdb_save_settings(lcdb);
		if (rc < 0) {
			pr_err("Failed to save LCDB settings rc=%d\n", rc);
			return rc;
		}
		lcdb->settings_saved = true;
	}

	val = BOOST_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_BST_PD_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set BST PD rc=%d\n", rc);
		return rc;
	}

	val = (RDSON_HALF << NFET_SW_SIZE_SHIFT) | RDSON_HALF;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_RDSON_MGMNT_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set RDSON MGMT rc=%d\n", rc);
		return rc;
	}

	val = AUTO_GM_EN_BIT | EN_TOUCH_WAKE_BIT | DIS_SCP_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_MISC_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set MISC CTL rc=%d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_SOFT_START_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set LCDB_SOFT_START rc=%d\n", rc);
		return rc;
	}

	val = EN_PFM_BIT | (PFM_HYST_25MV << PFM_HYSTERESIS_SHIFT) |
		     (PFM_PEAK_CURRENT_400MA << PFM_CURRENT_SHIFT) |
				BYP_BST_SOFT_START_COMP_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_PFM_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set PFM_CTL rc=%d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_secure_write(lcdb, lcdb->base + LCDB_PWRUP_PWRDN_CTL_REG,
							val);
	if (rc < 0) {
		pr_err("Failed to set PWRUP_PWRDN_CTL rc=%d\n", rc);
		return rc;
	}

	val = LDO_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_LDO_PD_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set LDO_PD_CTL rc=%d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_LDO_SOFT_START_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set LDO_SOFT_START rc=%d\n", rc);
		return rc;
	}

	val = NCP_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_NCP_PD_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set NCP_PD_CTL rc=%d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_NCP_SOFT_START_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set NCP_SOFT_START rc=%d\n", rc);
		return rc;
	}

	if (lcdb->ttw_mode_sw) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_AUTO_TOUCH_WAKE_CTL_REG,
				EN_AUTO_TOUCH_WAKE_BIT,
				EN_AUTO_TOUCH_WAKE_BIT);
		if (rc < 0)
			pr_err("Failed to enable auto(sw) TTW\n rc = %d\n", rc);
	} else {
		val = HWEN_RDY_BIT;
		rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
							&val, 1);
		if (rc < 0)
			pr_err("Failed to hw_enable lcdb rc= %d\n", rc);
	}

	return rc;
}

static int qpnp_lcdb_ttw_exit(struct qpnp_lcdb *lcdb)
{
	int rc;

	if (lcdb->settings_saved) {
		rc = qpnp_lcdb_restore_settings(lcdb);
		if (rc < 0) {
			pr_err("Failed to restore lcdb settings rc=%d\n", rc);
			return rc;
		}
		lcdb->settings_saved = false;
	}

	return 0;
}

static int qpnp_lcdb_enable(struct qpnp_lcdb *lcdb)
{
	int rc = 0, timeout, delay;
	u8 val = 0;

	if (lcdb->lcdb_enabled)
		return 0;

	if (lcdb->ttw_enable) {
		rc = qpnp_lcdb_ttw_exit(lcdb);
		if (rc < 0) {
			pr_err("Failed to exit TTW mode rc=%d\n", rc);
			return rc;
		}
	}

	val = MODULE_EN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to enable lcdb rc= %d\n", rc);
		goto fail_enable;
	}

	if (lcdb->force_module_reenable) {
		val = 0;
		rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
								&val, 1);
		if (rc < 0) {
			pr_err("Failed to enable lcdb rc= %d\n", rc);
			goto fail_enable;
		}
		val = MODULE_EN_BIT;
		rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
								&val, 1);
		if (rc < 0) {
			pr_err("Failed to disable lcdb rc= %d\n", rc);
			goto fail_enable;
		}
	}

	/* poll for vreg_ok */
	timeout = 10;
	delay = lcdb->bst.soft_start_us + lcdb->ldo.soft_start_us +
					lcdb->ncp.soft_start_us;
	delay += lcdb->bst.vreg_ok_dbc_us + lcdb->ldo.vreg_ok_dbc_us +
					lcdb->ncp.vreg_ok_dbc_us;
	while (timeout--) {
		rc = qpnp_lcdb_read(lcdb, lcdb->base + INT_RT_STATUS_REG,
								&val, 1);
		if (rc < 0) {
			pr_err("Failed to poll for vreg-ok status rc=%d\n", rc);
			break;
		}
		if (val & VREG_OK_RT_STS_BIT)
			break;

		usleep_range(delay, delay + 100);
	}

	if (rc || !timeout) {
		if (!timeout) {
			pr_err("lcdb-vreg-ok status failed to change\n");
			rc = -ETIMEDOUT;
		}
		goto fail_enable;
	}

	lcdb->lcdb_enabled = true;
	pr_debug("lcdb enabled successfully!\n");

	return 0;

fail_enable:
	dump_status_registers(lcdb);
	pr_err("Failed to enable lcdb rc=%d\n", rc);
	return rc;
}

static int qpnp_lcdb_disable(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u8 val;

	if (!lcdb->lcdb_enabled)
		return 0;

	if (lcdb->ttw_enable) {
		rc = qpnp_lcdb_ttw_enter(lcdb);
		if (rc < 0) {
			pr_err("Failed to enable TTW mode rc=%d\n", rc);
			return rc;
		}
		lcdb->lcdb_enabled = false;

		return 0;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
							&val, 1);
	if (rc < 0)
		pr_err("Failed to disable lcdb rc= %d\n", rc);
	else
		lcdb->lcdb_enabled = false;

	return rc;
}

#define MIN_BST_VOLTAGE_MV			4700
#define MAX_BST_VOLTAGE_MV			6250
#define MIN_VOLTAGE_MV				4000
#define MAX_VOLTAGE_MV				6000
#define VOLTAGE_MIN_STEP_100_MV			4000
#define VOLTAGE_MIN_STEP_50_MV			4950
#define VOLTAGE_STEP_100_MV			100
#define VOLTAGE_STEP_50_MV			50
#define VOLTAGE_STEP_50MV_OFFSET		0xA
static int qpnp_lcdb_set_bst_voltage(struct qpnp_lcdb *lcdb,
					int voltage_mv)
{
	int rc = 0;
	u8 val = 0;

	if (voltage_mv < MIN_BST_VOLTAGE_MV)
		voltage_mv = MIN_BST_VOLTAGE_MV;
	else if (voltage_mv > MAX_BST_VOLTAGE_MV)
		voltage_mv = MAX_BST_VOLTAGE_MV;

	val = DIV_ROUND_UP(voltage_mv - MIN_BST_VOLTAGE_MV,
					VOLTAGE_STEP_50_MV);

	rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_BST_OUTPUT_VOLTAGE_REG,
				SET_OUTPUT_VOLTAGE_MASK, val);
	if (rc < 0)
		pr_err("Failed to set boost voltage %d mv rc=%d\n",
			voltage_mv, rc);
	else
		pr_debug("Boost voltage set = %d mv (0x%02x = 0x%02x)\n",
			voltage_mv, LCDB_BST_OUTPUT_VOLTAGE_REG, val);

	return rc;
}

static int qpnp_lcdb_get_bst_voltage(struct qpnp_lcdb *lcdb,
					int *voltage_mv)
{
	int rc;
	u8 val = 0;

	rc = qpnp_lcdb_read(lcdb, lcdb->base + LCDB_BST_OUTPUT_VOLTAGE_REG,
						&val, 1);
	if (rc < 0) {
		pr_err("Failed to reat BST voltage rc=%d\n", rc);
		return rc;
	}

	val &= SET_OUTPUT_VOLTAGE_MASK;
	*voltage_mv = (val * VOLTAGE_STEP_50_MV) + MIN_BST_VOLTAGE_MV;

	return 0;
}

static int qpnp_lcdb_set_voltage(struct qpnp_lcdb *lcdb,
					int voltage_mv, u8 type)
{
	int rc = 0;
	u16 offset = LCDB_LDO_OUTPUT_VOLTAGE_REG;
	u8 val = 0;

	if (type == BST)
		return qpnp_lcdb_set_bst_voltage(lcdb, voltage_mv);

	if (type == NCP)
		offset = LCDB_NCP_OUTPUT_VOLTAGE_REG;

	if (!is_between(voltage_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV)) {
		pr_err("Invalid voltage %dmv (min=%d max=%d)\n",
			voltage_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV);
		return -EINVAL;
	}

	/* Change the BST voltage to LDO + 100mV */
	if (type == LDO) {
		rc = qpnp_lcdb_set_bst_voltage(lcdb, voltage_mv + 100);
		if (rc < 0) {
			pr_err("Failed to set boost voltage rc=%d\n", rc);
			return rc;
		}
	}

	/* Below logic is only valid for LDO and NCP type */
	if (voltage_mv < VOLTAGE_MIN_STEP_50_MV) {
		val = DIV_ROUND_UP(voltage_mv - VOLTAGE_MIN_STEP_100_MV,
						VOLTAGE_STEP_100_MV);
	} else {
		val = DIV_ROUND_UP(voltage_mv - VOLTAGE_MIN_STEP_50_MV,
						VOLTAGE_STEP_50_MV);
		val += VOLTAGE_STEP_50MV_OFFSET;
	}

	rc = qpnp_lcdb_masked_write(lcdb, lcdb->base + offset,
				SET_OUTPUT_VOLTAGE_MASK, val);
	if (rc < 0)
		pr_err("Failed to set output voltage %d mv for %s rc=%d\n",
			voltage_mv, (type == LDO) ? "LDO" : "NCP", rc);
	else
		pr_debug("%s voltage set = %d mv (0x%02x = 0x%02x)\n",
			(type == LDO) ? "LDO" : "NCP", voltage_mv, offset, val);

	return rc;
}

static int qpnp_lcdb_get_voltage(struct qpnp_lcdb *lcdb,
					u32 *voltage_mv, u8 type)
{
	int rc = 0;
	u16 offset = LCDB_LDO_OUTPUT_VOLTAGE_REG;
	u8 val = 0;

	if (type == BST)
		return qpnp_lcdb_get_bst_voltage(lcdb, voltage_mv);

	if (type == NCP)
		offset = LCDB_NCP_OUTPUT_VOLTAGE_REG;

	rc = qpnp_lcdb_read(lcdb, lcdb->base + offset, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read %s volatge rc=%d\n",
			(type == LDO) ? "LDO" : "NCP", rc);
		return rc;
	}

	if (val < VOLTAGE_STEP_50MV_OFFSET) {
		*voltage_mv = VOLTAGE_MIN_STEP_100_MV +
				(val * VOLTAGE_STEP_100_MV);
	} else {
		*voltage_mv = VOLTAGE_MIN_STEP_50_MV +
			((val - VOLTAGE_STEP_50MV_OFFSET) * VOLTAGE_STEP_50_MV);
	}

	if (!rc)
		pr_debug("%s voltage read-back = %d mv (0x%02x = 0x%02x)\n",
					(type == LDO) ? "LDO" : "NCP",
					*voltage_mv, offset, val);

	return rc;
}

static int qpnp_lcdb_set_soft_start(struct qpnp_lcdb *lcdb,
					u32 ss_us, u8 type)
{
	int rc = 0, i = 0;
	u16 offset = LCDB_LDO_SOFT_START_CTL_REG;
	u8 val = 0;

	if (type == NCP)
		offset = LCDB_NCP_SOFT_START_CTL_REG;

	if (!is_between(ss_us, MIN_SOFT_START_US, MAX_SOFT_START_US)) {
		pr_err("Invalid soft_start_us %d (min=%d max=%d)\n",
			ss_us, MIN_SOFT_START_US, MAX_SOFT_START_US);
		return -EINVAL;
	}

	i = 0;
	while (ss_us > soft_start_us[i])
		i++;
	val = ((i == 0) ? 0 : i - 1) & SOFT_START_MASK;

	rc = qpnp_lcdb_masked_write(lcdb,
			lcdb->base + offset, SOFT_START_MASK, val);
	if (rc < 0)
		pr_err("Failed to write %s soft-start time %d rc=%d",
			(type == LDO) ? "LDO" : "NCP", soft_start_us[i], rc);

	return rc;
}

static int qpnp_lcdb_ldo_regulator_enable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_enable(lcdb);
	if (rc < 0)
		pr_err("Failed to enable lcdb rc=%d\n", rc);
	mutex_unlock(&lcdb->lcdb_mutex);

	return rc;
}

static int qpnp_lcdb_ldo_regulator_disable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_disable(lcdb);
	if (rc < 0)
		pr_err("Failed to disable lcdb rc=%d\n", rc);
	mutex_unlock(&lcdb->lcdb_mutex);

	return rc;
}

static int qpnp_lcdb_ldo_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	return lcdb->lcdb_enabled;
}

static int qpnp_lcdb_ldo_regulator_set_voltage(struct regulator_dev *rdev,
				int min_uV, int max_uV, unsigned int *selector)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	rc = qpnp_lcdb_set_voltage(lcdb, min_uV / 1000, LDO);
	if (rc < 0)
		pr_err("Failed to set LDO voltage rc=%c\n", rc);

	return rc;
}

static int qpnp_lcdb_ldo_regulator_get_voltage(struct regulator_dev *rdev)
{
	int rc = 0;
	u32 voltage_mv = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	rc = qpnp_lcdb_get_voltage(lcdb, &voltage_mv, LDO);
	if (rc < 0) {
		pr_err("Failed to get ldo voltage rc=%d\n", rc);
		return rc;
	}

	return voltage_mv * 1000;
}

static struct regulator_ops qpnp_lcdb_ldo_ops = {
	.enable			= qpnp_lcdb_ldo_regulator_enable,
	.disable		= qpnp_lcdb_ldo_regulator_disable,
	.is_enabled		= qpnp_lcdb_ldo_regulator_is_enabled,
	.set_voltage		= qpnp_lcdb_ldo_regulator_set_voltage,
	.get_voltage		= qpnp_lcdb_ldo_regulator_get_voltage,
};

static int qpnp_lcdb_ncp_regulator_enable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_enable(lcdb);
	if (rc < 0)
		pr_err("Failed to enable lcdb rc=%d\n", rc);
	mutex_unlock(&lcdb->lcdb_mutex);

	return rc;
}

static int qpnp_lcdb_ncp_regulator_disable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_disable(lcdb);
	if (rc < 0)
		pr_err("Failed to disable lcdb rc=%d\n", rc);
	mutex_unlock(&lcdb->lcdb_mutex);

	return rc;
}

static int qpnp_lcdb_ncp_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	return lcdb->lcdb_enabled;
}

static int qpnp_lcdb_ncp_regulator_set_voltage(struct regulator_dev *rdev,
				int min_uV, int max_uV, unsigned int *selector)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	rc = qpnp_lcdb_set_voltage(lcdb, min_uV / 1000, NCP);
	if (rc < 0)
		pr_err("Failed to set LDO voltage rc=%c\n", rc);

	return rc;
}

static int qpnp_lcdb_ncp_regulator_get_voltage(struct regulator_dev *rdev)
{
	int rc;
	u32 voltage_mv = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	rc = qpnp_lcdb_get_voltage(lcdb, &voltage_mv, NCP);
	if (rc < 0) {
		pr_err("Failed to get ncp voltage rc=%d\n", rc);
		return rc;
	}

	return voltage_mv * 1000;
}

static struct regulator_ops qpnp_lcdb_ncp_ops = {
	.enable			= qpnp_lcdb_ncp_regulator_enable,
	.disable		= qpnp_lcdb_ncp_regulator_disable,
	.is_enabled		= qpnp_lcdb_ncp_regulator_is_enabled,
	.set_voltage		= qpnp_lcdb_ncp_regulator_set_voltage,
	.get_voltage		= qpnp_lcdb_ncp_regulator_get_voltage,
};

static int qpnp_lcdb_regulator_register(struct qpnp_lcdb *lcdb, u8 type)
{
	int rc = 0;
	struct regulator_init_data *init_data;
	struct regulator_config cfg = {};
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct device_node *node;

	if (type == LDO) {
		node			= lcdb->ldo.node;
		rdesc			= &lcdb->ldo.rdesc;
		rdesc->ops		= &qpnp_lcdb_ldo_ops;
		rdev			= lcdb->ldo.rdev;
	} else if (type == NCP) {
		node			= lcdb->ncp.node;
		rdesc			= &lcdb->ncp.rdesc;
		rdesc->ops		= &qpnp_lcdb_ncp_ops;
		rdev			= lcdb->ncp.rdev;
	} else {
		pr_err("Invalid regulator type %d\n", type);
		return -EINVAL;
	}

	init_data = of_get_regulator_init_data(lcdb->dev, node, rdesc);
	if (!init_data) {
		pr_err("Failed to get regulator_init_data for %s\n",
					(type == LDO) ? "LDO" : "NCP");
		return -ENOMEM;
	}

	if (init_data->constraints.name) {
		rdesc->owner		= THIS_MODULE;
		rdesc->type		= REGULATOR_VOLTAGE;
		rdesc->name		= init_data->constraints.name;

		cfg.dev = lcdb->dev;
		cfg.init_data = init_data;
		cfg.driver_data = lcdb;
		cfg.of_node = node;

		if (of_get_property(lcdb->dev->of_node, "parent-supply", NULL))
			init_data->supply_regulator = "parent";

		init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_VOLTAGE
				| REGULATOR_CHANGE_STATUS;

		rdev = devm_regulator_register(lcdb->dev, rdesc, &cfg);
		if (IS_ERR(rdev)) {
			rc = PTR_ERR(rdev);
			rdev = NULL;
			pr_err("Failed to register lcdb_%s regulator rc = %d\n",
				(type == LDO) ? "LDO" : "NCP", rc);
			return rc;
		}
	} else {
		pr_err("%s_regulator name missing\n",
				(type == LDO) ? "LDO" : "NCP");
		return -EINVAL;
	}

	return rc;
}

static int qpnp_lcdb_parse_ttw(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u32 temp;
	u8 val = 0;
	struct device_node *node = lcdb->dev->of_node;

	if (of_property_read_bool(node, "qcom,ttw-mode-sw")) {
		lcdb->ttw_mode_sw = true;
		rc = of_property_read_u32(node, "qcom,attw-toff-ms", &temp);
		if (!rc) {
			if (!is_between(temp, ATTW_MIN_MS, ATTW_MAX_MS)) {
				pr_err("Invalid TOFF val %d (min=%d max=%d)\n",
					temp, ATTW_MIN_MS, ATTW_MAX_MS);
					return -EINVAL;
			}
			val = ilog2(temp / 4) << ATTW_TOFF_TIME_SHIFT;
		} else {
			pr_err("qcom,attw-toff-ms not specified for TTW SW mode\n");
			return rc;
		}

		rc = of_property_read_u32(node, "qcom,attw-ton-ms", &temp);
		if (!rc) {
			if (!is_between(temp, ATTW_MIN_MS, ATTW_MAX_MS)) {
				pr_err("Invalid TON value %d (min=%d max=%d)\n",
					temp, ATTW_MIN_MS, ATTW_MAX_MS);
				return -EINVAL;
			}
			val |= ilog2(temp / 4);
		} else {
			pr_err("qcom,attw-ton-ms not specified for TTW SW mode\n");
			return rc;
		}
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_AUTO_TOUCH_WAKE_CTL_REG,
				ATTW_TON_TIME_MASK | ATTW_TOFF_TIME_MASK, val);
		if (rc < 0) {
			pr_err("Failed to write ATTW ON/OFF rc=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

static int qpnp_lcdb_ldo_dt_init(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	struct device_node *node = lcdb->ldo.node;

	/* LDO output voltage */
	lcdb->ldo.voltage_mv = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ldo-voltage-mv",
					&lcdb->ldo.voltage_mv);
	if (!rc && !is_between(lcdb->ldo.voltage_mv, MIN_VOLTAGE_MV,
						MAX_VOLTAGE_MV)) {
		pr_err("Invalid LDO voltage %dmv (min=%d max=%d)\n",
			lcdb->ldo.voltage_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV);
		return -EINVAL;
	}

	/* LDO PD configuration */
	lcdb->ldo.pd = -EINVAL;
	of_property_read_u32(node, "qcom,ldo-pd", &lcdb->ldo.pd);

	lcdb->ldo.pd_strength = -EINVAL;
	of_property_read_u32(node, "qcom,ldo-pd-strength",
					&lcdb->ldo.pd_strength);

	/* LDO ILIM configuration */
	lcdb->ldo.ilim_ma = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ldo-ilim-ma", &lcdb->ldo.ilim_ma);
	if (!rc && !is_between(lcdb->ldo.ilim_ma, MIN_LDO_ILIM_MA,
						MAX_LDO_ILIM_MA)) {
		pr_err("Invalid ilim_ma %d (min=%d, max=%d)\n",
			lcdb->ldo.ilim_ma, MIN_LDO_ILIM_MA,
					MAX_LDO_ILIM_MA);
		return -EINVAL;
	}

	/* LDO soft-start (SS) configuration */
	lcdb->ldo.soft_start_us = -EINVAL;
	of_property_read_u32(node, "qcom,ldo-soft-start-us",
					&lcdb->ldo.soft_start_us);

	return 0;
}

static int qpnp_lcdb_ncp_dt_init(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	struct device_node *node = lcdb->ncp.node;

	/* NCP output voltage */
	lcdb->ncp.voltage_mv = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ncp-voltage-mv",
					&lcdb->ncp.voltage_mv);
	if (!rc && !is_between(lcdb->ncp.voltage_mv, MIN_VOLTAGE_MV,
						MAX_VOLTAGE_MV)) {
		pr_err("Invalid NCP voltage %dmv (min=%d max=%d)\n",
			lcdb->ldo.voltage_mv, MIN_VOLTAGE_MV, MAX_VOLTAGE_MV);
		return -EINVAL;
	}

	/* NCP PD configuration */
	lcdb->ncp.pd = -EINVAL;
	of_property_read_u32(node, "qcom,ncp-pd", &lcdb->ncp.pd);

	lcdb->ncp.pd_strength = -EINVAL;
	of_property_read_u32(node, "qcom,ncp-pd-strength",
					&lcdb->ncp.pd_strength);

	/* NCP ILIM configuration */
	lcdb->ncp.ilim_ma = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ncp-ilim-ma", &lcdb->ncp.ilim_ma);
	if (!rc && !is_between(lcdb->ncp.ilim_ma, MIN_NCP_ILIM_MA,
						MAX_NCP_ILIM_MA)) {
		pr_err("Invalid ilim_ma %d (min=%d, max=%d)\n",
			lcdb->ncp.ilim_ma, MIN_NCP_ILIM_MA, MAX_NCP_ILIM_MA);
		return -EINVAL;
	}

	/* NCP soft-start (SS) configuration */
	lcdb->ncp.soft_start_us = -EINVAL;
	of_property_read_u32(node, "qcom,ncp-soft-start-us",
					&lcdb->ncp.soft_start_us);

	return 0;
}

static int qpnp_lcdb_bst_dt_init(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	struct device_node *node = lcdb->bst.node;

	/* Boost PD  configuration */
	lcdb->bst.pd = -EINVAL;
	of_property_read_u32(node, "qcom,bst-pd", &lcdb->bst.pd);

	lcdb->bst.pd_strength = -EINVAL;
	of_property_read_u32(node, "qcom,bst-pd-strength",
					&lcdb->bst.pd_strength);

	/* Boost ILIM */
	lcdb->bst.ilim_ma = -EINVAL;
	rc = of_property_read_u32(node, "qcom,bst-ilim-ma", &lcdb->bst.ilim_ma);
	if (!rc && !is_between(lcdb->bst.ilim_ma, MIN_BST_ILIM_MA,
						MAX_BST_ILIM_MA)) {
		pr_err("Invalid ilim_ma %d (min=%d, max=%d)\n",
			lcdb->bst.ilim_ma, MIN_BST_ILIM_MA, MAX_BST_ILIM_MA);
			return -EINVAL;
	}

	/* Boost PS configuration */
	lcdb->bst.ps = -EINVAL;
	of_property_read_u32(node, "qcom,bst-ps", &lcdb->bst.ps);

	lcdb->bst.ps_threshold = -EINVAL;
	rc = of_property_read_u32(node, "qcom,bst-ps-threshold-ma",
					&lcdb->bst.ps_threshold);
	if (!rc && !is_between(lcdb->bst.ps_threshold,
				MIN_BST_PS_MA, MAX_BST_PS_MA)) {
		pr_err("Invalid bst ps_threshold %d (min=%d, max=%d)\n",
			lcdb->bst.ps_threshold, MIN_BST_PS_MA, MAX_BST_PS_MA);
		return -EINVAL;
	}

	return 0;
}

static int qpnp_lcdb_init_ldo(struct qpnp_lcdb *lcdb)
{
	int rc = 0, ilim_ma;
	u8 val = 0;

	/* configure parameters only if LCDB is disabled */
	if (!is_lcdb_enabled(lcdb)) {
		if (lcdb->ldo.voltage_mv != -EINVAL) {
			rc = qpnp_lcdb_set_voltage(lcdb,
					lcdb->ldo.voltage_mv, LDO);
			if (rc < 0) {
				pr_err("Failed to set voltage rc=%d\n", rc);
				return rc;
			}
		}

		if (lcdb->ldo.pd != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_LDO_PD_CTL_REG, LDO_DIS_PULLDOWN_BIT,
				lcdb->ldo.pd ? 0 : LDO_DIS_PULLDOWN_BIT);
			if (rc < 0) {
				pr_err("Failed to configure LDO PD rc=%d\n",
								rc);
				return rc;
			}
		}

		if (lcdb->ldo.pd_strength != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_LDO_PD_CTL_REG, LDO_PD_STRENGTH_BIT,
				lcdb->ldo.pd_strength ?
				LDO_PD_STRENGTH_BIT : 0);
			if (rc < 0) {
				pr_err("Failed to configure LDO PD strength %s rc=%d",
						lcdb->ldo.pd_strength ?
						"(strong)" : "(weak)", rc);
				return rc;
			}
		}

		if (lcdb->ldo.ilim_ma != -EINVAL) {
			ilim_ma = lcdb->ldo.ilim_ma - MIN_LDO_ILIM_MA;
			ilim_ma /= LDO_ILIM_STEP_MA;
			val = (ilim_ma & SET_LDO_ILIM_MASK) | EN_LDO_ILIM_BIT;
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					LCDB_LDO_ILIM_CTL1_REG,
					SET_LDO_ILIM_MASK | EN_LDO_ILIM_BIT,
					val);
			if (rc < 0) {
				pr_err("Failed to configure LDO ilim_ma (CTL1=%d) rc=%d",
							val, rc);
				return rc;
			}

			val = ilim_ma & SET_LDO_ILIM_MASK;
			rc = qpnp_lcdb_masked_write(lcdb,
					lcdb->base + LCDB_LDO_ILIM_CTL2_REG,
					SET_LDO_ILIM_MASK, val);
			if (rc < 0) {
				pr_err("Failed to configure LDO ilim_ma (CTL2=%d) rc=%d",
							val, rc);
				return rc;
			}
		}

		if (lcdb->ldo.soft_start_us != -EINVAL) {
			rc = qpnp_lcdb_set_soft_start(lcdb,
					lcdb->ldo.soft_start_us, LDO);
			if (rc < 0) {
				pr_err("Failed to set LDO soft_start rc=%d\n",
									rc);
				return rc;
			}
		}
	}

	rc = qpnp_lcdb_get_voltage(lcdb, &lcdb->ldo.voltage_mv, LDO);
	if (rc < 0) {
		pr_err("Failed to get LDO volatge rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_LDO_VREG_OK_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ldo_vreg_ok rc=%d\n", rc);
		return rc;
	}
	lcdb->ldo.vreg_ok_dbc_us = dbc_us[val & VREG_OK_DEB_MASK];

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_LDO_SOFT_START_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ldo_soft_start_ctl rc=%d\n", rc);
		return rc;
	}
	lcdb->ldo.soft_start_us = soft_start_us[val & SOFT_START_MASK];

	rc = qpnp_lcdb_regulator_register(lcdb, LDO);
	if (rc < 0)
		pr_err("Failed to register ldo rc=%d\n", rc);

	return rc;
}

static int qpnp_lcdb_init_ncp(struct qpnp_lcdb *lcdb)
{
	int rc = 0, i = 0;
	u8 val = 0;

	/* configure parameters only if LCDB is disabled */
	if (!is_lcdb_enabled(lcdb)) {
		if (lcdb->ncp.voltage_mv != -EINVAL) {
			rc = qpnp_lcdb_set_voltage(lcdb,
					lcdb->ncp.voltage_mv, NCP);
			if (rc < 0) {
				pr_err("Failed to set voltage rc=%d\n", rc);
				return rc;
			}
		}

		if (lcdb->ncp.pd != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_NCP_PD_CTL_REG, NCP_DIS_PULLDOWN_BIT,
				lcdb->ncp.pd ? 0 : NCP_DIS_PULLDOWN_BIT);
			if (rc < 0) {
				pr_err("Failed to configure NCP PD rc=%d\n",
									rc);
				return rc;
			}
		}

		if (lcdb->ncp.pd_strength != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_NCP_PD_CTL_REG, NCP_PD_STRENGTH_BIT,
				lcdb->ncp.pd_strength ?
				NCP_PD_STRENGTH_BIT : 0);
			if (rc < 0) {
				pr_err("Failed to configure NCP PD strength %s rc=%d",
					lcdb->ncp.pd_strength ?
					"(strong)" : "(weak)", rc);
				return rc;
			}
		}

		if (lcdb->ncp.ilim_ma != -EINVAL) {
			while (lcdb->ncp.ilim_ma > ncp_ilim_ma[i])
				i++;
			val = (i == 0) ? 0 : i - 1;
			val = (lcdb->ncp.ilim_ma & SET_NCP_ILIM_MASK) |
							EN_NCP_ILIM_BIT;
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
						LCDB_NCP_ILIM_CTL1_REG,
				SET_NCP_ILIM_MASK | EN_NCP_ILIM_BIT, val);
			if (rc < 0) {
				pr_err("Failed to configure NCP ilim_ma (CTL1=%d) rc=%d",
								val, rc);
				return rc;
			}
			val = lcdb->ncp.ilim_ma & SET_NCP_ILIM_MASK;
			rc = qpnp_lcdb_masked_write(lcdb,
					lcdb->base + LCDB_NCP_ILIM_CTL2_REG,
					SET_NCP_ILIM_MASK, val);
			if (rc < 0) {
				pr_err("Failed to configure NCP ilim_ma (CTL2=%d) rc=%d",
							val, rc);
				return rc;
			}
		}

		if (lcdb->ncp.soft_start_us != -EINVAL) {
			rc = qpnp_lcdb_set_soft_start(lcdb,
				lcdb->ncp.soft_start_us, NCP);
			if (rc < 0) {
				pr_err("Failed to set NCP soft_start rc=%d\n",
								rc);
				return rc;
			}
		}
	}

	rc = qpnp_lcdb_get_voltage(lcdb, &lcdb->ncp.voltage_mv, NCP);
	if (rc < 0) {
		pr_err("Failed to get NCP volatge rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_NCP_VREG_OK_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ncp_vreg_ok rc=%d\n", rc);
		return rc;
	}
	lcdb->ncp.vreg_ok_dbc_us = dbc_us[val & VREG_OK_DEB_MASK];

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_NCP_SOFT_START_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ncp_soft_start_ctl rc=%d\n", rc);
		return rc;
	}
	lcdb->ncp.soft_start_us = soft_start_us[val & SOFT_START_MASK];

	rc = qpnp_lcdb_regulator_register(lcdb, NCP);
	if (rc < 0)
		pr_err("Failed to register NCP rc=%d\n", rc);

	return rc;
}

static int qpnp_lcdb_init_bst(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u8 val = 0;

	/* configure parameters only if LCDB is disabled */
	if (!is_lcdb_enabled(lcdb)) {
		if (lcdb->bst.pd != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_BST_PD_CTL_REG, BOOST_DIS_PULLDOWN_BIT,
				lcdb->bst.pd ? 0 : BOOST_DIS_PULLDOWN_BIT);
			if (rc < 0) {
				pr_err("Failed to configure BST PD rc=%d\n",
									rc);
				return rc;
			}
		}

		if (lcdb->bst.pd_strength != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_NCP_PD_CTL_REG, BOOST_PD_STRENGTH_BIT,
				lcdb->bst.pd_strength ?
				BOOST_PD_STRENGTH_BIT : 0);
			if (rc < 0) {
				pr_err("Failed to configure NCP PD strength %s rc=%d",
					lcdb->bst.pd_strength ?
					"(strong)" : "(weak)", rc);
				return rc;
			}
		}

		if (lcdb->bst.ilim_ma != -EINVAL) {
			val = (lcdb->bst.ilim_ma / MIN_BST_ILIM_MA) - 1;
			val = (lcdb->bst.ilim_ma & SET_BST_ILIM_MASK) |
							EN_BST_ILIM_BIT;
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_BST_ILIM_CTL_REG,
				SET_BST_ILIM_MASK | EN_BST_ILIM_BIT, val);
			if (rc < 0) {
				pr_err("Failed to configure BST ilim_ma rc=%d",
									rc);
				return rc;
			}
		}

		if (lcdb->bst.ps != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					LCDB_PS_CTL_REG, EN_PS_BIT,
					&lcdb->bst.ps ? EN_PS_BIT : 0);
			if (rc < 0) {
				pr_err("Failed to disable BST PS rc=%d", rc);
				return rc;
			}
		}

		if (lcdb->bst.ps_threshold != -EINVAL) {
			val = (lcdb->bst.ps_threshold - MIN_BST_PS_MA) / 10;
			val = (lcdb->bst.ps_threshold & PS_THRESHOLD_MASK) |
								EN_PS_BIT;
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
						LCDB_PS_CTL_REG,
						PS_THRESHOLD_MASK | EN_PS_BIT,
						val);
			if (rc < 0) {
				pr_err("Failed to configure BST PS threshold rc=%d",
								rc);
				return rc;
			}
		}
	}

	rc = qpnp_lcdb_get_voltage(lcdb, &lcdb->bst.voltage_mv, BST);
	if (rc < 0) {
		pr_err("Failed to get BST volatge rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_BST_VREG_OK_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read bst_vreg_ok rc=%d\n", rc);
		return rc;
	}
	lcdb->bst.vreg_ok_dbc_us = dbc_us[val & VREG_OK_DEB_MASK];

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_SOFT_START_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ncp_soft_start_ctl rc=%d\n", rc);
		return rc;
	}
	lcdb->bst.soft_start_us = (val & SOFT_START_MASK) * 200	+ 200;

	return 0;
}

static int qpnp_lcdb_hw_init(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u8 val = 0;

	rc = qpnp_lcdb_init_bst(lcdb);
	if (rc < 0) {
		pr_err("Failed to initialize BOOST rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_init_ldo(lcdb);
	if (rc < 0) {
		pr_err("Failed to initialize LDO rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_init_ncp(lcdb);
	if (rc < 0) {
		pr_err("Failed to initialize NCP rc=%d\n", rc);
		return rc;
	}

	if (!is_lcdb_enabled(lcdb)) {
		rc = qpnp_lcdb_read(lcdb, lcdb->base +
				LCDB_MODULE_RDY_REG, &val, 1);
		if (rc < 0) {
			pr_err("Failed to read MODULE_RDY rc=%d\n", rc);
			return rc;
		}
		if (!(val & MODULE_RDY_BIT)) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_MODULE_RDY_REG, MODULE_RDY_BIT,
						MODULE_RDY_BIT);
			if (rc < 0) {
				pr_err("Failed to set MODULE RDY rc=%d\n", rc);
				return rc;
			}
		}
	} else {
		/* module already enabled */
		lcdb->lcdb_enabled = true;
	}

	return 0;
}

static int qpnp_lcdb_parse_dt(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	const char *label;
	struct device_node *temp, *node = lcdb->dev->of_node;

	for_each_available_child_of_node(node, temp) {
		rc = of_property_read_string(temp, "label", &label);
		if (rc < 0) {
			pr_err("Failed to read label rc=%d\n", rc);
			return rc;
		}

		if (!strcmp(label, "ldo")) {
			lcdb->ldo.node = temp;
			rc = qpnp_lcdb_ldo_dt_init(lcdb);
		} else if (!strcmp(label, "ncp")) {
			lcdb->ncp.node = temp;
			rc = qpnp_lcdb_ncp_dt_init(lcdb);
		} else if (!strcmp(label, "bst")) {
			lcdb->bst.node = temp;
			rc = qpnp_lcdb_bst_dt_init(lcdb);
		} else {
			pr_err("Failed to identify label %s\n", label);
			return -EINVAL;
		}
		if (rc < 0) {
			pr_err("Failed to register %s module\n", label);
			return rc;
		}
	}

	lcdb->force_module_reenable = of_property_read_bool(node,
					"qcom,force-module-reenable");

	if (of_property_read_bool(node, "qcom,ttw-enable")) {
		rc = qpnp_lcdb_parse_ttw(lcdb);
		if (rc < 0) {
			pr_err("Failed to parse ttw-params rc=%d\n", rc);
			return rc;
		}
		lcdb->ttw_enable = true;
	}

	return rc;
}

static int qpnp_lcdb_regulator_probe(struct platform_device *pdev)
{
	int rc;
	struct device_node *node;
	struct qpnp_lcdb *lcdb;

	node = pdev->dev.of_node;
	if (!node) {
		pr_err("No nodes defined\n");
		return -ENODEV;
	}

	lcdb = devm_kzalloc(&pdev->dev, sizeof(*lcdb), GFP_KERNEL);
	if (!lcdb)
		return -ENOMEM;

	rc = of_property_read_u32(node, "reg", &lcdb->base);
	if (rc < 0) {
		pr_err("Failed to find reg node rc=%d\n", rc);
		return rc;
	}

	lcdb->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!lcdb->regmap) {
		pr_err("Failed to get the regmap handle rc=%d\n", rc);
		return -EINVAL;
	}

	lcdb->dev = &pdev->dev;
	lcdb->pdev = pdev;
	mutex_init(&lcdb->lcdb_mutex);
	mutex_init(&lcdb->read_write_mutex);

	rc = qpnp_lcdb_parse_dt(lcdb);
	if (rc < 0) {
		pr_err("Failed to parse dt rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_hw_init(lcdb);
	if (rc < 0)
		pr_err("Failed to initialize LCDB module rc=%d\n", rc);
	else
		pr_info("LCDB module successfully registered! lcdb_en=%d ldo_voltage=%dmV ncp_voltage=%dmV bst_voltage=%dmV\n",
			lcdb->lcdb_enabled, lcdb->ldo.voltage_mv,
			lcdb->ncp.voltage_mv, lcdb->bst.voltage_mv);

	return rc;
}

static int qpnp_lcdb_regulator_remove(struct platform_device *pdev)
{
	struct qpnp_lcdb *lcdb = dev_get_drvdata(&pdev->dev);

	mutex_destroy(&lcdb->lcdb_mutex);
	mutex_destroy(&lcdb->read_write_mutex);

	return 0;
}

static const struct of_device_id lcdb_match_table[] = {
	{ .compatible = QPNP_LCDB_REGULATOR_DRIVER_NAME, },
	{ },
};

static struct platform_driver qpnp_lcdb_regulator_driver = {
	.driver		= {
		.name		= QPNP_LCDB_REGULATOR_DRIVER_NAME,
		.of_match_table	= lcdb_match_table,
	},
	.probe		= qpnp_lcdb_regulator_probe,
	.remove		= qpnp_lcdb_regulator_remove,
};

static int __init qpnp_lcdb_regulator_init(void)
{
	return platform_driver_register(&qpnp_lcdb_regulator_driver);
}
arch_initcall(qpnp_lcdb_regulator_init);

static void __exit qpnp_lcdb_regulator_exit(void)
{
	platform_driver_unregister(&qpnp_lcdb_regulator_driver);
}
module_exit(qpnp_lcdb_regulator_exit);

MODULE_DESCRIPTION("QPNP LCDB regulator driver");
MODULE_LICENSE("GPL v2");
