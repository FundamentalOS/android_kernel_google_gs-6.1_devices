// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based tg4a AMOLED LCD panel driver.
 *
 * Copyright (c) 2023 Google LLC
 */

#include <drm/display/drm_dsc_helper.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <video/mipi_display.h>

#include "trace/dpu_trace.h"
#include "panel/panel-samsung-drv.h"

/**
 * enum tg4a_lhbm_brt - local hbm brightness
 * @LHBM_R_COARSE: red coarse
 * @LHBM_GB_COARSE: green and blue coarse
 * @LHBM_R_FINE: red fine
 * @LHBM_G_FINE: green fine
 * @LHBM_B_FINE: blue fine
 * @LHBM_BRT_LEN: local hbm brightness array length
 */
enum tg4a_lhbm_brt {
	LHBM_R_COARSE,
	LHBM_GB_COARSE,
	LHBM_R_FINE,
	LHBM_G_FINE,
	LHBM_B_FINE,
	LHBM_BRT_LEN
};
#define LHBM_BRT_CMD_LEN (LHBM_BRT_LEN + 1)

/* DSC1.2 */
static const struct drm_dsc_config pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 540,
	.slice_height = 101,
	.simple_422 = false,
	.pic_width = 1080,
	.pic_height = 2424,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 526,
	.block_pred_enable = true,
	.first_line_bpg_offset = 12,
	.initial_offset = 6144,
	.rc_buf_thresh = {
		14, 28, 42, 56,
		70, 84, 98, 105,
		112, 119, 121, 123,
		125, 126
	},
	.rc_range_params = {
		{.range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 2},
		{.range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 0},
		{.range_min_qp = 1, .range_max_qp = 5, .range_bpg_offset = 0},
		{.range_min_qp = 1, .range_max_qp = 6, .range_bpg_offset = 62},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 60},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 58},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 8, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 9, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 10, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 12, .range_bpg_offset = 52},
		{.range_min_qp = 5, .range_max_qp = 13, .range_bpg_offset = 52},
		{.range_min_qp = 7, .range_max_qp = 13, .range_bpg_offset = 52},
		{.range_min_qp = 13, .range_max_qp = 15, .range_bpg_offset = 52}
	},
	.rc_model_size = 8192,
	.flatness_min_qp = 3,
	.flatness_max_qp = 12,
	.initial_scale_value = 32,
	.scale_decrement_interval = 7,
	.scale_increment_interval = 2517,
	.nfl_bpg_offset = 246,
	.slice_bpg_offset = 258,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 540,
	.dsc_version_minor = 2,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};


#define TG4A_WRCTRLD_DIMMING_BIT	0x08
#define TG4A_WRCTRLD_BCTRL_BIT		0x20
#define TG4A_WRCTRLD_LOCAL_HBM_BIT	0x10

#define FREQUENCY_COUNT 2

static const u8 test_key_enable[] = { 0xF0, 0x5A, 0x5A };
static const u8 test_key_disable[] = { 0xF0, 0xA5, 0xA5 };
static const u8 pixel_off[] = { 0x22 };

static const struct exynos_dsi_cmd tg4a_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_ENTER_SLEEP_MODE),
};
static DEFINE_EXYNOS_CMD_SET(tg4a_off);

static const struct exynos_dsi_cmd tg4a_lp_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24),
};
static DEFINE_EXYNOS_CMD_SET(tg4a_lp);

static const struct exynos_dsi_cmd tg4a_lp_night_cmd[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0xB8),
};

static const struct exynos_dsi_cmd tg4a_lp_low_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x01, 0x7E),
};

static const struct exynos_dsi_cmd tg4a_lp_high_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x03, 0x1A),
};

static const struct exynos_binned_lp tg4a_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 252, tg4a_lp_night_cmd,
				12, 12 + 50),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 717, tg4a_lp_low_cmds,
				12, 12 + 50),
	BINNED_LP_MODE_TIMING("high", 4095, tg4a_lp_high_cmds,
				12, 12 + 50),
};

static const struct exynos_dsi_cmd tg4a_init_cmds[] = {
	/* TE on */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON),

	/* TE2 setting */
	EXYNOS_DSI_CMD0(test_key_enable),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x26, 0xB9),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x00, 0x00, 0x10, 0x00, 0x00,
				0x3D, 0x00, 0x09, 0x90, 0x00, 0x09, 0x90),

	/* CASET: 1080 */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),

	/* PASET: 2424 */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x77),

	/* TODO: b/315722627: update FFC Setting based on next revision of op manual */

	/* VDDD LDO Setting, only for Proto 1.1 and EVT 1.0*/
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1 | PANEL_REV_EVT1, 0xB0, 0x00, 0x58, 0xD7),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1 | PANEL_REV_EVT1, 0xD7, 0x0A),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1 | PANEL_REV_EVT1, 0xB0, 0x00, 0x5B, 0xD7),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1 | PANEL_REV_EVT1, 0xD7, 0x0A),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1 | PANEL_REV_EVT1, 0xFE, 0x80),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1 | PANEL_REV_EVT1, 0xFE, 0x00),

	/* TSP HSYNC setting */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x42, 0xB9),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x19),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x46, 0xB9),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0xB0),

	/* FGZ common setting */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x30, 0x68),
	EXYNOS_DSI_CMD_SEQ(0x68, 0x32, 0xFF, 0x04, 0x08, 0x10, 0x15, 0x29, 0x67, 0xA5),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x1C, 0x62),
	EXYNOS_DSI_CMD_SEQ(0x62, 0x1D, 0x5F),

	/* Set back correct OSC setting, only for Proto 1.1 */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1, 0xB0, 0x00, 0x0C, 0xB5),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1, 0xB5, 0xC0, 0x00, 0x60, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1, 0xF7, 0x2F),

	EXYNOS_DSI_CMD0(test_key_disable),

	/* TODO: b/315722627: Local HBM Gamma Writing */
};
static DEFINE_EXYNOS_CMD_SET(tg4a_init);

static const struct exynos_dsi_cmd tg4a_lhbm_location_cmds[] = {
	EXYNOS_DSI_CMD0(test_key_enable),
	/* global para */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1, 0xB0, 0x00, 0xBC, 0x65),
	/* box location */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1, 0x65, 0x00, 0x00, 0x00, 0x43, 0x79, 0x77),
	/* global para */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1, 0xB0, 0x00, 0xC2, 0x65),
	/* center position set, x: 0x21C, y: 0x6DD, size: 0x64 */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1, 0x65, 0x21, 0xC6, 0xDD,
					0x64, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD0(test_key_disable),
};
static DEFINE_EXYNOS_CMD_SET(tg4a_lhbm_location);

#define LHBM_GAMMA_CMD_SIZE 6

struct tg4a_lhbm_ctl {
	/** @brt_normal: normal LHBM brightness parameters */
	u8 brt_normal[FREQUENCY_COUNT][LHBM_BRT_LEN];
};

/**
 * struct tg4a_panel - panel specific runtime info
 *
 * This struct maintains tg4a panel specific runtime info, any fixed details about panel
 * should most likely go into struct exynos_panel_desc
 */
struct tg4a_panel {
	/** @base: base panel struct */
	struct exynos_panel base;

	/** @local_hbm_gamma: lhbm gamma data */
	struct local_hbm_gamma {
		u8 hs120_cmd[LHBM_GAMMA_CMD_SIZE];
		u8 hs60_cmd[LHBM_GAMMA_CMD_SIZE];
	} local_hbm_gamma;

	/** @lhbm_ctl: lhbm brightness control */
	struct tg4a_lhbm_ctl lhbm_ctl;

	/**
	 * @is_pixel_off: pixel-off command is sent to panel. Only sending normal-on or resetting
	 *		  panel can recover to normal mode after entering pixel-off state.
	 */
	bool is_pixel_off;
};
#define to_spanel(ctx) container_of(ctx, struct tg4a_panel, base)

enum frequency { HS120, HS60 };
static const char *frequency_str[] = { "HS120", "HS60" };

static void read_lhbm_gamma(struct exynos_panel *ctx, u8 *cmd, enum frequency freq) {
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 index = (freq == HS120) ? 0x06 : 0x0B;
	int ret;

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xB0, 0x02, index, 0xBD); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xBD, cmd + 1, LHBM_GAMMA_CMD_SIZE - 1);

	if (ret != (LHBM_GAMMA_CMD_SIZE - 1)) {
		dev_err(ctx->dev, "fail to read LHBM gamma for %s\n", frequency_str[freq]);
		return;
	}

	/* fill in gamma write command 0x6A in offset 0 */
	cmd[0] = 0x6A;
	dev_info(ctx->dev, "%s_gamma: %*ph\n", frequency_str[freq],
		LHBM_GAMMA_CMD_SIZE - 1, cmd + 1);
}

static void tg4a_lhbm_gamma_read(struct exynos_panel *ctx)
{
	struct tg4a_panel *spanel = to_spanel(ctx);

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_enable);

	read_lhbm_gamma(ctx, spanel->local_hbm_gamma.hs120_cmd, HS120);
	read_lhbm_gamma(ctx, spanel->local_hbm_gamma.hs60_cmd, HS60);

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_disable);
}

static void tg4a_lhbm_gamma_write(struct exynos_panel *ctx)
{
	struct tg4a_panel *spanel = to_spanel(ctx);
	const u8 hs120_cmd = spanel->local_hbm_gamma.hs120_cmd[0];
	const u8 hs60_cmd = spanel->local_hbm_gamma.hs60_cmd[0];

	if (!hs120_cmd && !hs60_cmd) {
		dev_err(ctx->dev, "%s: no lhbm gamma!\n", __func__);
		return;
	}

	dev_dbg(ctx->dev, "%s\n", __func__);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_enable);

	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xAE, 0x6A); /* global para */
	EXYNOS_DCS_BUF_ADD_SET(ctx, spanel->local_hbm_gamma.hs120_cmd); /* write gamma */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xB3, 0x6A); /* global para */
	EXYNOS_DCS_BUF_ADD_SET(ctx, spanel->local_hbm_gamma.hs60_cmd); /* write gamma */

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_disable);
}

static void tg4a_change_frequency(struct exynos_panel *ctx,
					const struct exynos_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!ctx || (vrefresh != 60 && vrefresh != 120))
		return;

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_enable);
	EXYNOS_DCS_BUF_ADD(ctx, 0x83, (vrefresh == 120) ? 0x00 : 0x08);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF7, 0x2F);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_disable);

	dev_info(ctx->dev, "%s: change to %uHz\n", __func__, vrefresh);
	return;
}

static void tg4a_update_wrctrld(struct exynos_panel *ctx)
{
	u8 val = TG4A_WRCTRLD_BCTRL_BIT;

	if (ctx->hbm.local_hbm.enabled)
		val |= TG4A_WRCTRLD_LOCAL_HBM_BIT;

	if (ctx->dimming_on)
		val |= TG4A_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev,
		"%s(wrctrld:%#x, hbm: %d, dimming: %d, local_hbm: %d)\n",
		__func__, val, IS_HBM_ON(ctx->hbm_mode),
		ctx->dimming_on, ctx->hbm.local_hbm.enabled);

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

static int tg4a_set_brightness(struct exynos_panel *ctx, u16 br)
{
	u16 brightness;
	u32 max_brightness;
	struct tg4a_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode && ctx->current_mode->exynos_mode.is_lp_mode) {
		const struct exynos_panel_funcs *funcs;

		/* don't stay at pixel-off state in AOD, or black screen is possibly seen */
		if (spanel->is_pixel_off) {
			EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_ENTER_NORMAL_MODE);
			spanel->is_pixel_off = false;
		}
		funcs = ctx->desc->exynos_panel_func;
		if (funcs && funcs->set_binned_lp)
			funcs->set_binned_lp(ctx, br);
		return 0;
	}

	/* Use pixel off command instead of setting DBV 0 */
	if (!br) {
		if (!spanel->is_pixel_off) {
			EXYNOS_DCS_WRITE_TABLE(ctx, pixel_off);
			spanel->is_pixel_off = true;
			dev_dbg(ctx->dev, "%s: pixel off instead of dbv 0\n", __func__);
		}
		return 0;
	} else if (br && spanel->is_pixel_off) {
		EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_ENTER_NORMAL_MODE);
		spanel->is_pixel_off = false;
	}

	if (!ctx->desc->brt_capability) {
		dev_err(ctx->dev, "no available brightness capability\n");
		return -EINVAL;
	}

	max_brightness = ctx->desc->brt_capability->hbm.level.max;

	if (br > max_brightness) {
		br = max_brightness;
		dev_warn(ctx->dev, "%s: capped to dbv(%d)\n", __func__,
			max_brightness);
	}

	brightness = __builtin_bswap16(br);

	return exynos_dcs_set_brightness(ctx, brightness);
}

static void tg4a_set_hbm_mode(struct exynos_panel *ctx,
				enum exynos_hbm_mode mode)
{
	ctx->hbm_mode = mode;

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_enable);

	/* FGZ mode setting */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x61, 0x68);

	if (ctx->hbm_mode) {
		if (IS_HBM_ON_IRC_OFF(ctx->hbm_mode)) {
			/* FGZ Mode ON */
			EXYNOS_DCS_BUF_ADD(ctx, 0x68, 0xB0, 0x2C, 0x6A,
						0x80, 0x00, 0x00, 0xF5, 0xC4);
		} else {
			/* FGZ Mode OFF */
			EXYNOS_DCS_BUF_ADD(ctx, 0x68, 0xB0, 0x2C, 0x6A,
						0x80, 0x00, 0x00, 0x00, 0x00);
		}
	} else {
		/* FGZ Mode OFF */
		EXYNOS_DCS_BUF_ADD(ctx, 0x68, 0xB0, 0x2C, 0x6A, 0x80,
						0x00, 0x00, 0x00, 0x00);
	}

	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x01, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, ctx->hbm_mode ? 0x80 : 0x81);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x2E, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, ctx->hbm_mode ? 0x01 : 0x02);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF7, 0x2F);

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_disable);

	dev_info(ctx->dev, "hbm_on=%d hbm_ircoff=%d.\n", IS_HBM_ON(ctx->hbm_mode),
		 IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void tg4a_set_dimming_on(struct exynos_panel *exynos_panel, bool dimming_on)
{
	const struct exynos_panel_mode *pmode = exynos_panel->current_mode;
	exynos_panel->dimming_on = dimming_on;

	if (pmode->exynos_mode.is_lp_mode) {
		dev_warn(exynos_panel->dev, "in lp mode, skip to update\n");
		return;
	}

	tg4a_update_wrctrld(exynos_panel);
}

static void tg4a_set_local_hbm_mode(struct exynos_panel *ctx, bool local_hbm_en)
{
	tg4a_update_wrctrld(ctx);

	if (local_hbm_en) {
		EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x30);
		exynos_panel_send_cmd_set(ctx, &tg4a_lhbm_location_cmd_set);
	} else {
		EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	}
}

static void tg4a_mode_set(struct exynos_panel *ctx,
				const struct exynos_panel_mode *pmode)
{
	tg4a_change_frequency(ctx, pmode);
}

static bool tg4a_is_mode_seamless(const struct exynos_panel *ctx,
					const struct exynos_panel_mode *pmode)
{
	/* seamless mode switch is possible if only changing refresh rate */
	return drm_mode_equal_no_clocks(&ctx->current_mode->mode, &pmode->mode);
}

static void tg4a_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
		struct dentry *panel_root, *csroot;

		if (!ctx)
			return;

		panel_root = debugfs_lookup("panel", root);
		if (!panel_root)
			return;

		csroot = debugfs_lookup("cmdsets", panel_root);
		if (!csroot)
			goto panel_out;

		exynos_panel_debugfs_create_cmdset(ctx, csroot, &tg4a_init_cmd_set, "init");

		dput(csroot);

panel_out:
		dput(panel_root);
	}
}

static void tg4a_panel_init(struct exynos_panel *ctx)
{
	tg4a_lhbm_gamma_read(ctx);
	tg4a_lhbm_gamma_write(ctx);
	exynos_panel_send_cmd_set(ctx, &tg4a_lhbm_location_cmd_set);
}

static void tg4a_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 main = (build_code & 0xE0) >> 3;
	u8 sub = (build_code & 0x0C) >> 2;
	u8 rev = main | sub;

	exynos_panel_get_panel_rev(ctx, rev);
}

static void tg4a_set_lp_mode(struct exynos_panel *ctx,
					const struct exynos_panel_mode *pmode)
{
	exynos_panel_set_lp_mode(ctx, pmode);
}

static void tg4a_set_nolp_mode(struct exynos_panel *ctx,
				  const struct exynos_panel_mode *pmode)
{
	const struct exynos_panel_mode *current_mode = ctx->current_mode;
	unsigned int vrefresh = current_mode ? drm_mode_vrefresh(&current_mode->mode) : 30;
	unsigned int te_usec = current_mode ? current_mode->exynos_mode.te_usec : 1109;

	if (!is_panel_active(ctx))
		return;

	/* AOD Mode Off Setting */
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_enable);
	EXYNOS_DCS_BUF_ADD(ctx, 0x53, 0x20);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_disable);

	/* backlight control and dimming */
	tg4a_update_wrctrld(ctx);
	tg4a_change_frequency(ctx, pmode);

	DPU_ATRACE_BEGIN("tg4a_wait_one_vblank");
	exynos_panel_wait_for_vsync_done(ctx, te_usec,
			EXYNOS_VREFRESH_TO_PERIOD_USEC(vrefresh));

	/* Additional sleep time to account for TE variability */
	usleep_range(1000, 1010);
	DPU_ATRACE_END("tg4a_wait_one_vblank");

	dev_info(ctx->dev, "exit LP mode\n");
}

static int tg4a_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	struct drm_dsc_picture_parameter_set pps_payload;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_info(ctx->dev, "%s\n", __func__);

	exynos_panel_reset(ctx);

	/* sleep out */
	EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 120, MIPI_DCS_EXIT_SLEEP_MODE);

	/* initial command */
	exynos_panel_send_cmd_set(ctx, &tg4a_init_cmd_set);

	/* frequency */
	tg4a_change_frequency(ctx, pmode);

	tg4a_lhbm_gamma_write(ctx);

	/* DSC related configuration */
	exynos_dcs_compression_mode(ctx, 0x1);
	drm_dsc_pps_payload_pack(&pps_payload, &pps_config);
	EXYNOS_PPS_WRITE_BUF(ctx, &pps_payload);
	/* DSC Enable */
	EXYNOS_DCS_BUF_ADD(ctx, 0xC2, 0x14);
	EXYNOS_DCS_BUF_ADD(ctx, 0x9D, 0x01);

	/* dimming and HBM */
	tg4a_update_wrctrld(ctx);

	if (pmode->exynos_mode.is_lp_mode)
		exynos_panel_set_lp_mode(ctx, pmode);

	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

static int tg4a_panel_probe(struct mipi_dsi_device *dsi)
{
	struct tg4a_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->is_pixel_off = false;

	return exynos_panel_common_init(dsi, &spanel->base);
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 500,
	.te_var = 1,
};

static const u16 WIDTH_MM = 65, HEIGHT_MM = 146;
static const u16 HDISPLAY = 1080, VDISPLAY = 2424;
static const u16 HFP = 44, HSA = 16, HBP = 20;
static const u16 VFP = 10, VSA = 6, VBP = 10;

#define TG4A_DSC {\
	.enabled = true,\
	.dsc_count = 1,\
	.slice_count = 2,\
	.slice_height = 101,\
	.cfg = &pps_config,\
}

static const struct exynos_panel_mode tg4a_modes[] = {
	{
		.mode = {
			.name = "1080x2424@60:60",
			.clock = 170520,
			.hdisplay = HDISPLAY,
			.hsync_start = HDISPLAY + HFP,
			.hsync_end = HDISPLAY + HFP + HSA,
			.htotal = HDISPLAY + HFP + HSA + HBP,
			.vdisplay = VDISPLAY,
			.vsync_start = VDISPLAY + VFP,
			.vsync_end = VDISPLAY + VFP + VSA,
			.vtotal = VDISPLAY + VFP + VSA + VBP,
			.flags = 0,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			/* TODO: b/316356276#comment3 - update TE Pulse width for 60Hz/120Hz */
			.te_usec = 8605,
			.bpc = 8,
			.dsc = TG4A_DSC,
			.underrun_param = &underrun_param,
		},
	},
	{
		.mode = {
			.name = "1080x2424@120:120",
			.clock = 341040,
			.hdisplay = HDISPLAY,
			.hsync_start = HDISPLAY + HFP,
			.hsync_end = HDISPLAY + HFP + HSA,
			.htotal = HDISPLAY + HFP + HSA + HBP,
			.vdisplay = VDISPLAY,
			.vsync_start = VDISPLAY + VFP,
			.vsync_end = VDISPLAY + VFP + VSA,
			.vtotal = VDISPLAY + VFP + VSA + VBP,
			.flags = 0,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			/* TODO: b/316356276#comment3 - update TE Pulse width for 60Hz/120Hz */
			.te_usec = 276,
			.bpc = 8,
			.dsc = TG4A_DSC,
			.underrun_param = &underrun_param,
		},
	},
};

const struct brightness_capability tg4a_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 1200,
		},
		.level = {
			.min = 184,
			.max = 3427,
		},
		.percentage = {
			.min = 0,
			.max = 67,
		},
	},
	.hbm = {
		.nits = {
			.min = 1200,
			.max = 1800,
		},
		.level = {
			.min = 3428,
			.max = 4095,
		},
		.percentage = {
			.min = 67,
			.max = 100,
		},
	},
};

static const struct exynos_panel_mode tg4a_lp_mode = {
	.mode = {
		.name = "1080x2424@30:30",
		.clock = 85260,
		.hdisplay = HDISPLAY,
		.hsync_start = HDISPLAY + HFP,
		.hsync_end = HDISPLAY + HFP + HSA,
		.htotal = HDISPLAY + HFP + HSA + HBP,
		.vdisplay = VDISPLAY,
		.vsync_start = VDISPLAY + VFP,
		.vsync_end = VDISPLAY + VFP + VSA,
		.vtotal = VDISPLAY + VFP + VSA + VBP,
		.flags = 0,
		.width_mm = WIDTH_MM,
		.height_mm = HEIGHT_MM,
	},
	.exynos_mode = {
		.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
		.vblank_usec = 120,
		.te_usec = 1109,
		.bpc = 8,
		.dsc = TG4A_DSC,
		.underrun_param = &underrun_param,
		.is_lp_mode = true,
	}
};

static const struct drm_panel_funcs tg4a_drm_funcs = {
	.disable = exynos_panel_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = tg4a_enable,
	.get_modes = exynos_panel_get_modes,
	.debugfs_init = tg4a_debugfs_init,
};

static const struct exynos_panel_funcs tg4a_exynos_funcs = {
	.set_brightness = tg4a_set_brightness,
	.set_lp_mode = tg4a_set_lp_mode,
	.set_nolp_mode = tg4a_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_dimming_on = tg4a_set_dimming_on,
	.set_hbm_mode = tg4a_set_hbm_mode,
	.set_local_hbm_mode = tg4a_set_local_hbm_mode,
	.is_mode_seamless = tg4a_is_mode_seamless,
	.mode_set = tg4a_mode_set,
	.panel_init = tg4a_panel_init,
	.get_panel_rev = tg4a_get_panel_rev,
	.read_id = exynos_panel_read_ddic_id,
};

const struct exynos_panel_desc google_tg4a = {
	.data_lane_cnt = 4,
	.max_brightness = 4095,
	.min_brightness = 2,
	.dft_brightness = 1290,	/* 140 nits */
	.brt_capability = &tg4a_brightness_capability,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.modes = tg4a_modes,
	.num_modes = ARRAY_SIZE(tg4a_modes),
	.off_cmd_set = &tg4a_off_cmd_set,
	.lp_mode = &tg4a_lp_mode,
	.lp_cmd_set = &tg4a_lp_cmd_set,
	.binned_lp = tg4a_binned_lp,
	.num_binned_lp = ARRAY_SIZE(tg4a_binned_lp),
	.panel_func = &tg4a_drm_funcs,
	.exynos_panel_func = &tg4a_exynos_funcs,
	.reset_timing_ms = {1, 1, 1},
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 0},
		{PANEL_REG_ID_VCI, 10},
	},
	.reg_ctrl_post_enable = {
		{PANEL_REG_ID_VDDD, 5},
	},
	.reg_ctrl_pre_disable = {
		{PANEL_REG_ID_VDDD, 0},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDI, 0},
	},
};

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "google,tg4a", .data = &google_tg4a },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = tg4a_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-tg4a",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Cathy Hsu <cathsu@google.com>, Safayat Ullah <safayat@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google tg4a panel driver");
MODULE_LICENSE("GPL");
