/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/firmware.h>
#include <soc/qcom/subsystem_restart.h>
#include <linux/pm_opp.h>
#include <linux/jiffies.h>

#include "adreno.h"
#include "a6xx_reg.h"
#include "adreno_a6xx.h"
#include "adreno_cp_parser.h"
#include "adreno_trace.h"
#include "kgsl_trace.h"

#define RSC_CMD_OFFSET 2
#define PDC_CMD_OFFSET 4

static void _regwrite(void __iomem *regbase,
		unsigned int offsetwords, unsigned int value)
{
	void __iomem *reg;

	reg = regbase + (offsetwords << 2);
	__raw_writel(value, reg);
}

/*
 * _load_gmu_rpmh_ucode() - Load the ucode into the GPU PDC/RSC blocks
 * PDC and RSC execute GPU power on/off RPMh sequence
 * @device: Pointer to KGSL device
 */
static void _load_gmu_rpmh_ucode(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct gmu_device *gmu = &device->gmu;

	/* Disable SDE clock gating */
	kgsl_gmu_regwrite(device, A6XX_GPU_RSCC_RSC_STATUS0_DRV0, BIT(24));

	/* Setup RSC PDC handshake for sleep and wakeup */
	kgsl_gmu_regwrite(device, A6XX_RSCC_PDC_SLAVE_ID_DRV0, 1);
	kgsl_gmu_regwrite(device, A6XX_RSCC_HIDDEN_TCS_CMD0_DATA, 0);
	kgsl_gmu_regwrite(device, A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR, 0);
	kgsl_gmu_regwrite(device,
			A6XX_RSCC_HIDDEN_TCS_CMD0_DATA + RSC_CMD_OFFSET, 0);
	kgsl_gmu_regwrite(device,
			A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR + RSC_CMD_OFFSET, 0);
	kgsl_gmu_regwrite(device,
			A6XX_RSCC_HIDDEN_TCS_CMD0_DATA + RSC_CMD_OFFSET * 2,
			0x80000000);
	kgsl_gmu_regwrite(device,
			A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR + RSC_CMD_OFFSET * 2,
			0);
	kgsl_gmu_regwrite(device, A6XX_RSCC_OVERRIDE_START_ADDR, 0);
	kgsl_gmu_regwrite(device, A6XX_RSCC_PDC_SEQ_START_ADDR, 0x4520);
	kgsl_gmu_regwrite(device, A6XX_RSCC_PDC_MATCH_VALUE_LO, 0x4510);
	kgsl_gmu_regwrite(device, A6XX_RSCC_PDC_MATCH_VALUE_HI, 0x4514);

	/* Enable timestamp event for v1 only */
	if (adreno_is_a630v1(adreno_dev))
		kgsl_gmu_regwrite(device, A6XX_RSCC_TIMESTAMP_UNIT1_EN_DRV0, 1);

	/* Load RSC sequencer uCode for sleep and wakeup */
	kgsl_gmu_regwrite(device, A6XX_RSCC_SEQ_MEM_0_DRV0, 0xA7A506A0);
	kgsl_gmu_regwrite(device, A6XX_RSCC_SEQ_MEM_0_DRV0 + 1, 0xA1E6A6E7);
	kgsl_gmu_regwrite(device, A6XX_RSCC_SEQ_MEM_0_DRV0 + 2, 0xA2E081E1);
	kgsl_gmu_regwrite(device, A6XX_RSCC_SEQ_MEM_0_DRV0 + 3, 0xE9A982E2);
	kgsl_gmu_regwrite(device, A6XX_RSCC_SEQ_MEM_0_DRV0 + 4, 0x0020E8A8);

	/* Load PDC sequencer uCode for power up and power down sequence */
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_SEQ_MEM_0, 0xFEBEA1E1);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_SEQ_MEM_0 + 1, 0xA5A4A3A2);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_SEQ_MEM_0 + 2, 0x8382A6E0);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_SEQ_MEM_0 + 3, 0xBCE3E284);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_SEQ_MEM_0 + 4, 0x002081FC);

	/* Set TCS commands used by PDC sequence for low power modes */
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS1_CMD_ENABLE_BANK, 7);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS1_CMD_WAIT_FOR_CMPL_BANK, 0);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS1_CONTROL, 0);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS1_CMD0_MSGID, 0x10108);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS1_CMD0_ADDR, 0x30010);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS1_CMD0_DATA, 1);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS1_CMD0_MSGID + PDC_CMD_OFFSET, 0x10108);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS1_CMD0_ADDR + PDC_CMD_OFFSET, 0x30000);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS1_CMD0_DATA + PDC_CMD_OFFSET, 0x0);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS1_CMD0_MSGID + PDC_CMD_OFFSET * 2, 0x10108);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS1_CMD0_ADDR + PDC_CMD_OFFSET * 2, 0x30080);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS1_CMD0_DATA + PDC_CMD_OFFSET * 2, 0x0);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS3_CMD_ENABLE_BANK, 7);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS3_CMD_WAIT_FOR_CMPL_BANK, 0);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS3_CONTROL, 0);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS3_CMD0_MSGID, 0x10108);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS3_CMD0_ADDR, 0x30010);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_TCS3_CMD0_DATA, 2);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS3_CMD0_MSGID + PDC_CMD_OFFSET, 0x10108);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS3_CMD0_ADDR + PDC_CMD_OFFSET, 0x30000);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS3_CMD0_DATA + PDC_CMD_OFFSET, 0x3);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS3_CMD0_MSGID + PDC_CMD_OFFSET * 2, 0x10108);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS3_CMD0_ADDR + PDC_CMD_OFFSET * 2, 0x30080);
	_regwrite(gmu->pdc_reg_virt,
			PDC_GPU_TCS3_CMD0_DATA + PDC_CMD_OFFSET * 2, 0x3);

	/* Setup GPU PDC */
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_SEQ_START_ADDR, 0);
	_regwrite(gmu->pdc_reg_virt, PDC_GPU_ENABLE_PDC, 0x80000001);

	/* ensure no writes happen before the uCode is fully written */
	wmb();
}

#define GMU_START_TIMEOUT	100	/* ms */
#define GPU_START_TIMEOUT	100	/* ms */
#define GPU_RESET_TIMEOUT	1	/* ms */
#define GPU_RESET_TIMEOUT_US	10	/* us */

/*
 * timed_poll_check() - polling *gmu* register at given offset until
 * its value changed to match expected value. The function times
 * out and returns after given duration if register is not updated
 * as expected.
 *
 * @device: Pointer to KGSL device
 * @offset: Register offset
 * @expected_ret: expected register value that stops polling
 * @timout: number of jiffies to abort the polling
 * @mask: bitmask to filter register value to match expected_ret
 */
static int timed_poll_check(struct kgsl_device *device,
		unsigned int offset, unsigned int expected_ret,
		unsigned int timeout, unsigned int mask)
{
	unsigned long t;
	unsigned int value;

	t = jiffies + msecs_to_jiffies(timeout);

	do {
		kgsl_gmu_regread(device, offset, &value);
		if ((value & mask) == expected_ret)
			return 0;
		/* Wait 100us to reduce unnecessary AHB bus traffic */
		usleep_range(10, 100);
	} while (!time_after(jiffies, t));

	/* Double check one last time */
	kgsl_gmu_regread(device, offset, &value);
	if ((value & mask) == expected_ret)
		return 0;

	return -EINVAL;
}

/*
 * The lowest 16 bits of this value are the number of XO clock cycles
 * for main hysteresis. This is the first hysteresis. Here we set it
 * to 0x1680 cycles, or 300 us. The highest 16 bits of this value are
 * the number of XO clock cycles for short hysteresis. This happens
 * after main hysteresis. Here we set it to 0xA cycles, or 0.5 us.
 */
#define GMU_PWR_COL_HYST 0x000A1680

/*
 * a6xx_gmu_power_config() - Configure and enable GMU's low power mode
 * setting based on ADRENO feature flags.
 * @device: Pointer to KGSL device
 */
static void a6xx_gmu_power_config(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct gmu_device *gmu = &device->gmu;

	/* Configure registers for idle setting. The setting is cumulative */

	/* Disable GMU WB/RB buffer and caches at boot */
	kgsl_gmu_regwrite(device, A6XX_GMU_SYS_BUS_CONFIG, 0x1);
	kgsl_gmu_regwrite(device, A6XX_GMU_ICACHE_CONFIG, 0x1);
	kgsl_gmu_regwrite(device, A6XX_GMU_DCACHE_CONFIG, 0x1);

	kgsl_gmu_regwrite(device,
		A6XX_GMU_PWR_COL_INTER_FRAME_CTRL,  0x9C40400);

	switch (gmu->idle_level) {
	case GPU_HW_MIN_VOLT:
		kgsl_gmu_regrmw(device, A6XX_GMU_RPMH_CTRL, 0,
				MIN_BW_ENABLE_MASK);
		kgsl_gmu_regrmw(device, A6XX_GMU_RPMH_HYST_CTRL, 0,
				MIN_BW_HYST);
		/* fall through */
	case GPU_HW_NAP:
		kgsl_gmu_regrmw(device, A6XX_GMU_GPU_NAP_CTRL, 0,
				HW_NAP_ENABLE_MASK);
		/* fall through */
	case GPU_HW_IFPC:
		kgsl_gmu_regwrite(device, A6XX_GMU_PWR_COL_INTER_FRAME_HYST,
				GMU_PWR_COL_HYST);
		kgsl_gmu_regrmw(device, A6XX_GMU_PWR_COL_INTER_FRAME_CTRL, 0,
				IFPC_ENABLE_MASK);
		/* fall through */
	case GPU_HW_SPTP_PC:
		kgsl_gmu_regwrite(device, A6XX_GMU_PWR_COL_SPTPRAC_HYST,
				GMU_PWR_COL_HYST);
		kgsl_gmu_regrmw(device, A6XX_GMU_PWR_COL_INTER_FRAME_CTRL, 0,
				SPTP_ENABLE_MASK);
		/* fall through */
	default:
		break;
	}

	/* ACD feature enablement */
	if (ADRENO_FEATURE(adreno_dev, ADRENO_LM) &&
		test_bit(ADRENO_LM_CTRL, &adreno_dev->pwrctrl_flag))
		kgsl_gmu_regrmw(device, A6XX_GMU_BOOT_KMD_LM_HANDSHAKE, 0,
				BIT(10));

	/* Enable RPMh GPU client */
	if (ADRENO_FEATURE(adreno_dev, ADRENO_RPMH))
		kgsl_gmu_regrmw(device, A6XX_GMU_RPMH_CTRL, 0,
				RPMH_ENABLE_MASK);
}

/*
 * a6xx_gmu_start() - Start GMU and wait until FW boot up.
 * @device: Pointer to KGSL device
 */
static int a6xx_gmu_start(struct kgsl_device *device)
{
	struct gmu_device *gmu = &device->gmu;

	kgsl_regwrite(device, A6XX_GMU_CX_GMU_WFI_CONFIG, 0x0);
	/* Write 1 first to make sure the GMU is reset */
	kgsl_gmu_regwrite(device, A6XX_GMU_CM3_SYSRESET, 1);

	/* Make sure putting in reset doesn't happen after clearing */
	wmb();

	/* Bring GMU out of reset */
	kgsl_gmu_regwrite(device, A6XX_GMU_CM3_SYSRESET, 0);
	if (timed_poll_check(device,
			A6XX_GMU_CM3_FW_INIT_RESULT,
			0xBABEFACE,
			GMU_START_TIMEOUT,
			0xFFFFFFFF)) {
		dev_err(&gmu->pdev->dev, "GMU doesn't boot\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * a6xx_gmu_hfi_start() - Write registers and start HFI.
 * @device: Pointer to KGSL device
 */
static int a6xx_gmu_hfi_start(struct kgsl_device *device)
{
	struct gmu_device *gmu = &device->gmu;

	kgsl_gmu_regrmw(device, A6XX_GMU_GMU2HOST_INTR_MASK,
			HFI_IRQ_MSGQ_MASK, 0);
	kgsl_gmu_regwrite(device, A6XX_GMU_HFI_CTRL_INIT, 1);

	if (timed_poll_check(device,
			A6XX_GMU_HFI_CTRL_STATUS,
			BIT(0),
			GMU_START_TIMEOUT,
			BIT(0))) {
		dev_err(&gmu->pdev->dev, "GMU HFI init failed\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static uint64_t read_AO_counter(struct kgsl_device *device)
{
	unsigned int l, h, h1;

	kgsl_gmu_regread(device, A6XX_GMU_CX_GMU_ALWAYS_ON_COUNTER_H, &h);
	kgsl_gmu_regread(device, A6XX_GMU_CX_GMU_ALWAYS_ON_COUNTER_L, &l);
	kgsl_gmu_regread(device, A6XX_GMU_CX_GMU_ALWAYS_ON_COUNTER_H, &h1);

	/*
	 * If there's no change in COUNTER_H we have no overflow so return,
	 * otherwise read COUNTER_L again
	 */

	if (h == h1)
		return (uint64_t) l | ((uint64_t) h << 32);

	kgsl_gmu_regread(device, A6XX_GMU_CX_GMU_ALWAYS_ON_COUNTER_L, &l);
	return (uint64_t) l | ((uint64_t) h1 << 32);
}

static int a6xx_rpmh_power_on_gpu(struct kgsl_device *device)
{
	struct gmu_device *gmu = &device->gmu;
	struct device *dev = &gmu->pdev->dev;
	int val;

	kgsl_gmu_regread(device, A6XX_GPU_CC_GX_DOMAIN_MISC, &val);
	if (!(val & 0x1))
		dev_err_ratelimited(&gmu->pdev->dev,
			"GMEM CLAMP IO not set while GFX rail off\n");

	/* RSC wake sequence */
	kgsl_gmu_regwrite(device, A6XX_GMU_RSCC_CONTROL_REQ, BIT(1));

	/* Write request before polling */
	wmb();

	if (timed_poll_check(device,
			A6XX_GMU_RSCC_CONTROL_ACK,
			BIT(1),
			GPU_START_TIMEOUT,
			BIT(1))) {
		dev_err(dev, "Failed to do GPU RSC power on\n");
		return -EINVAL;
	}

	if (timed_poll_check(device,
			A6XX_RSCC_SEQ_BUSY_DRV0,
			0,
			GPU_START_TIMEOUT,
			0xFFFFFFFF))
		goto error_rsc;

	kgsl_gmu_regwrite(device, A6XX_GMU_RSCC_CONTROL_REQ, 0);

	/* Enable the power counter because it was disabled before slumber */
	kgsl_gmu_regwrite(device, A6XX_GMU_CX_GMU_POWER_COUNTER_ENABLE, 1);

	return 0;
error_rsc:
	dev_err(dev, "GPU RSC sequence stuck in waking up GPU\n");
	return -EINVAL;
}

static int a6xx_rpmh_power_off_gpu(struct kgsl_device *device)
{
	struct gmu_device *gmu = &device->gmu;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	int ret;

	/* RSC sleep sequence is different on v1 */
	if (adreno_is_a630v1(adreno_dev))
		kgsl_gmu_regwrite(device, A6XX_RSCC_TIMESTAMP_UNIT1_EN_DRV0, 1);

	kgsl_gmu_regwrite(device, A6XX_GMU_RSCC_CONTROL_REQ, 1);
	/* Make sure the request completes before continuing */
	wmb();

	if (adreno_is_a630v1(adreno_dev))
		ret = timed_poll_check(device,
				A6XX_RSCC_TIMESTAMP_UNIT1_OUTPUT_DRV0,
				BIT(0),
				GPU_START_TIMEOUT,
				BIT(0));
	else
		ret = timed_poll_check(device,
				A6XX_GPU_RSCC_RSC_STATUS0_DRV0,
				BIT(16),
				GPU_START_TIMEOUT,
				BIT(16));

	if (ret) {
		dev_err(&gmu->pdev->dev, "GPU RSC power off fail\n");
		return -ETIMEDOUT;
	}

	/* Read to clear the timestamp valid signal. Don't care what we read. */
	if (adreno_is_a630v1(adreno_dev)) {
		kgsl_gmu_regread(device,
				A6XX_RSCC_TIMESTAMP_UNIT0_TIMESTAMP_L_DRV0,
				&ret);
		kgsl_gmu_regread(device,
				A6XX_RSCC_TIMESTAMP_UNIT0_TIMESTAMP_H_DRV0,
				&ret);
	}

	kgsl_gmu_regwrite(device, A6XX_GMU_RSCC_CONTROL_REQ, 0);

	if (ADRENO_FEATURE(adreno_dev, ADRENO_LM) &&
			test_bit(ADRENO_LM_CTRL, &adreno_dev->pwrctrl_flag))
		kgsl_gmu_regwrite(device, A6XX_GMU_AO_SPARE_CNTL, 0);

	return 0;
}

/*
 * Gmu FW header format:
 * <32-bit start addr> <32-bit size> <32-bit pad0> <32-bit pad1> <Payload>
 */
#define GMU_FW_HEADER_SIZE 4

#define GMU_ITCM_VA_START 0x0
#define GMU_ITCM_VA_END   (GMU_ITCM_VA_START + 0x4000) /* 16 KB */

#define GMU_DTCM_VA_START 0x40000
#define GMU_DTCM_VA_END   (GMU_DTCM_VA_START + 0x4000) /* 16 KB */

#define GMU_ICACHE_VA_START 0x4000
#define GMU_ICACHE_VA_END   (GMU_ICACHE_VA_START + 0x3C000) /* 240 KB */

static int load_gmu_fw(struct kgsl_device *device)
{
	struct gmu_device *gmu = &device->gmu;
	uint32_t *fwptr = gmu->fw_image->hostptr;
	int i, j, ret;
	int start_addr, size_in_bytes, num_dwords, tcm_slot, num_records;

	/* Allocates & maps memory for GMU cached instructions range */
	ret = allocate_gmu_cached_fw(gmu);
	if (ret)
		return ret;

	/*
	 * Read first record. pad0 field of first record contains
	 * number of records in the image.
	 */
	num_records = fwptr[2];
	for (i = 0; i < num_records; i++) {
		start_addr = fwptr[0];
		size_in_bytes = fwptr[1];
		num_dwords = size_in_bytes / sizeof(uint32_t);
		fwptr += GMU_FW_HEADER_SIZE;

		if ((start_addr >= GMU_ITCM_VA_START) &&
				(start_addr < GMU_ITCM_VA_END)) {
			tcm_slot = start_addr / sizeof(uint32_t);

			for (j = 0; j < num_dwords; j++)
				kgsl_gmu_regwrite(device,
					A6XX_GMU_CM3_ITCM_START + tcm_slot + j,
					fwptr[j]);
		} else if ((start_addr >= GMU_DTCM_VA_START) &&
				(start_addr < GMU_DTCM_VA_END)) {
			tcm_slot = (start_addr - GMU_DTCM_VA_START)
				/ sizeof(uint32_t);

			for (j = 0; j < num_dwords; j++)
				kgsl_gmu_regwrite(device,
					A6XX_GMU_CM3_DTCM_START + tcm_slot + j,
					fwptr[j]);
		} else if ((start_addr >= GMU_ICACHE_VA_START) &&
				(start_addr < GMU_ICACHE_VA_END)) {
			if (!is_cached_fw_size_valid(size_in_bytes)) {
				dev_err(&gmu->pdev->dev,
						"GMU firmware size too big\n");
				return -EINVAL;

			}
			memcpy(gmu->cached_fw_image.hostptr,
					fwptr, size_in_bytes);
		}

		fwptr += num_dwords;
	}

	/* Proceed only after the FW is written */
	wmb();
	return 0;
}

/*
 * a6xx_gmu_oob_set() - Set OOB interrupt to GMU.
 * @adreno_dev: Pointer to adreno device
 * @req: Which of the OOB bits to request
 */
int a6xx_gmu_oob_set(struct adreno_device *adreno_dev,
		enum oob_request req)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct gmu_device *gmu = &device->gmu;
	int ret = 0;
	int set, check;

	if (!kgsl_gmu_isenabled(device))
		return 0;

	if (adreno_is_a640(adreno_dev)) {
		set = BIT(30 - req * 2);
		check = BIT(31 - req);

		if ((device->gmu.hfi.version & 0x1F) == 0) {
			/* LEGACY for intermediate oobs */
			set = BIT(req + 16);
			check = BIT(req + 16);
		}

		if (req >= 6) {
			dev_err(&gmu->pdev->dev,
					"OOB_set(0x%x) invalid\n", set);
			return -EINVAL;
		}
	} else {
		set = BIT(req + 16);
		check = BIT(req + 24);
	}

	kgsl_gmu_regwrite(device, A6XX_GMU_HOST2GMU_INTR_SET, set);

	if (timed_poll_check(device,
			A6XX_GMU_GMU2HOST_INTR_INFO,
			check,
			GPU_START_TIMEOUT,
			check)) {
		ret = -ETIMEDOUT;
		dev_err(&gmu->pdev->dev,
			"OOB_set(0x%x) timed out\n", set);
	}

	kgsl_gmu_regwrite(device, A6XX_GMU_GMU2HOST_INTR_CLR, check);

	trace_kgsl_gmu_oob_set(set);
	return ret;
}

/*
 * a6xx_gmu_oob_clear() - Clear a previously set  OOB request.
 * @adreno_dev: Pointer to the adreno device that has the GMU
 * @req: Which of the OOB bits to clear
 */
void a6xx_gmu_oob_clear(struct adreno_device *adreno_dev,
		enum oob_request req)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct gmu_device *gmu = &device->gmu;
	int clear;

	if (!kgsl_gmu_isenabled(device))
		return;

	if (adreno_is_a640(adreno_dev)) {
		clear = BIT(31 - req * 2);
		if (req >= 6) {
			dev_err(&gmu->pdev->dev,
					"OOB_clear(0x%x) invalid\n", clear);
			return;
		}
		/* LEGACY for intermediate oobs */
		if ((device->gmu.hfi.version & 0x1F) == 0)
			clear = BIT(req + 24);
	} else
		clear = BIT(req + 24);

	kgsl_gmu_regwrite(device, A6XX_GMU_HOST2GMU_INTR_SET, clear);
	trace_kgsl_gmu_oob_clear(clear);
}



#define FREQ_VOTE(idx, ack) (((idx) & 0xFF) | (((ack) & 0xF) << 28))
#define BW_VOTE(idx) ((((idx) & 0xFFF) << 12) | ((idx) & 0xFFF))

/*
 * a6xx_gmu_dcvs_nohfi() - request GMU to do DCVS without using HFI
 * @device: Pointer to KGSL device
 * @perf_idx: Index into GPU performance level table defined in
 *	HFI DCVS table message
 * @bw_idx: Index into GPU b/w table defined in HFI b/w table message
 *
 */
static int a6xx_gmu_dcvs_nohfi(struct kgsl_device *device,
		unsigned int perf_idx, unsigned int bw_idx)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	int ret;

	kgsl_gmu_regwrite(device, A6XX_GMU_DCVS_ACK_OPTION, DCVS_ACK_NONBLOCK);

	kgsl_gmu_regwrite(device, A6XX_GMU_DCVS_PERF_SETTING,
			FREQ_VOTE(perf_idx, CLKSET_OPTION_ATLEAST));

	kgsl_gmu_regwrite(device, A6XX_GMU_DCVS_BW_SETTING, BW_VOTE(bw_idx));

	ret = a6xx_gmu_oob_set(adreno_dev, oob_dcvs);
	if (ret == 0)
		kgsl_gmu_regread(device, A6XX_GMU_DCVS_RETURN, &ret);

	a6xx_gmu_oob_clear(adreno_dev, oob_dcvs);

	return ret;
}
static int a6xx_complete_rpmh_votes(struct kgsl_device *device)
{
	int ret = 0;

	if (!kgsl_gmu_isenabled(device))
		return ret;

	ret |= timed_poll_check(device, A6XX_RSCC_TCS0_DRV0_STATUS, BIT(0),
			GPU_RESET_TIMEOUT, BIT(0));
	ret |= timed_poll_check(device, A6XX_RSCC_TCS1_DRV0_STATUS, BIT(0),
			GPU_RESET_TIMEOUT, BIT(0));
	ret |= timed_poll_check(device, A6XX_RSCC_TCS2_DRV0_STATUS, BIT(0),
			GPU_RESET_TIMEOUT, BIT(0));
	ret |= timed_poll_check(device, A6XX_RSCC_TCS3_DRV0_STATUS, BIT(0),
			GPU_RESET_TIMEOUT, BIT(0));

	return ret;
}

#define SPTPRAC_POWERON_CTRL_MASK	0x00778000
#define SPTPRAC_POWEROFF_CTRL_MASK	0x00778001
#define SPTPRAC_POWEROFF_STATUS_MASK	BIT(2)
#define SPTPRAC_POWERON_STATUS_MASK	BIT(3)
#define SPTPRAC_CTRL_TIMEOUT		10 /* ms */
#define A6XX_RETAIN_FF_ENABLE_ENABLE_MASK BIT(11)

/*
 * a6xx_gmu_sptprac_enable() - Power on SPTPRAC
 * @adreno_dev: Pointer to Adreno device
 */
int a6xx_gmu_sptprac_enable(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct gmu_device *gmu = &device->gmu;

	if (!kgsl_gmu_gpmu_isenabled(device))
		return -EINVAL;

	if (!adreno_has_sptprac_gdsc(adreno_dev))
		return 0;

	kgsl_gmu_regwrite(device, A6XX_GMU_GX_SPTPRAC_POWER_CONTROL,
			SPTPRAC_POWERON_CTRL_MASK);

	if (timed_poll_check(device,
			A6XX_GMU_SPTPRAC_PWR_CLK_STATUS,
			SPTPRAC_POWERON_STATUS_MASK,
			SPTPRAC_CTRL_TIMEOUT,
			SPTPRAC_POWERON_STATUS_MASK)) {
		dev_err(&gmu->pdev->dev, "power on SPTPRAC fail\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * a6xx_gmu_sptprac_disable() - Power of SPTPRAC
 * @adreno_dev: Pointer to Adreno device
 */
void a6xx_gmu_sptprac_disable(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct gmu_device *gmu = &device->gmu;

	if (!adreno_has_sptprac_gdsc(adreno_dev))
		return;

	if (!kgsl_gmu_gpmu_isenabled(device))
		return;

	/* Ensure that retention is on */
	kgsl_gmu_regrmw(device, A6XX_GPU_CC_GX_GDSCR, 0,
			A6XX_RETAIN_FF_ENABLE_ENABLE_MASK);

	kgsl_gmu_regwrite(device, A6XX_GMU_GX_SPTPRAC_POWER_CONTROL,
			SPTPRAC_POWEROFF_CTRL_MASK);

	if (timed_poll_check(device,
			A6XX_GMU_SPTPRAC_PWR_CLK_STATUS,
			SPTPRAC_POWEROFF_STATUS_MASK,
			SPTPRAC_CTRL_TIMEOUT,
			SPTPRAC_POWEROFF_STATUS_MASK))
		dev_err(&gmu->pdev->dev, "power off SPTPRAC fail\n");
}

#define SPTPRAC_POWER_OFF	BIT(2)
#define SP_CLK_OFF		BIT(4)
#define GX_GDSC_POWER_OFF	BIT(6)
#define GX_CLK_OFF		BIT(7)
#define is_on(val)		(!(val & (GX_GDSC_POWER_OFF | GX_CLK_OFF)))
/*
 * a6xx_gx_is_on() - Check if GX is on using pwr status register
 * @adreno_dev - Pointer to adreno_device
 * This check should only be performed if the keepalive bit is set or it
 * can be guaranteed that the power state of the GPU will remain unchanged
 */
bool a6xx_gmu_gx_is_on(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	unsigned int val;

	if (!kgsl_gmu_isenabled(device))
		return true;

	kgsl_gmu_regread(device, A6XX_GMU_SPTPRAC_PWR_CLK_STATUS, &val);
	return is_on(val);
}

/*
 * a6xx_gmu_sptprac_is_on() - Check if SPTP is on using pwr status register
 * @adreno_dev - Pointer to adreno_device
 * This check should only be performed if the keepalive bit is set or it
 * can be guaranteed that the power state of the GPU will remain unchanged
 */
bool a6xx_gmu_sptprac_is_on(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	unsigned int val;

	if (!kgsl_gmu_isenabled(device) || !adreno_has_sptprac_gdsc(adreno_dev))
		return true;

	kgsl_gmu_regread(device, A6XX_GMU_SPTPRAC_PWR_CLK_STATUS, &val);
	return !(val & (SPTPRAC_POWER_OFF | SP_CLK_OFF));
}

/*
 * a6xx_gmu_gfx_rail_on() - request GMU to power GPU at given OPP.
 * @device: Pointer to KGSL device
 *
 */
static int a6xx_gmu_gfx_rail_on(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	struct gmu_device *gmu = &device->gmu;
	unsigned int perf_idx = pwr->num_pwrlevels - pwr->default_pwrlevel - 1;
	uint32_t default_opp = gmu->rpmh_votes.gx_votes[perf_idx];

	kgsl_gmu_regwrite(device, A6XX_GMU_BOOT_SLUMBER_OPTION,
			OOB_BOOT_OPTION);
	kgsl_gmu_regwrite(device, A6XX_GMU_GX_VOTE_IDX,
			ARC_VOTE_GET_PRI(default_opp));
	kgsl_gmu_regwrite(device, A6XX_GMU_MX_VOTE_IDX,
			ARC_VOTE_GET_SEC(default_opp));

	return a6xx_gmu_oob_set(adreno_dev, oob_boot_slumber);
}

static bool idle_trandition_complete(unsigned int idle_level,
	unsigned int gmu_power_reg,
	unsigned int sptprac_clk_reg)
{
	if (idle_level != gmu_power_reg)
		return false;

	switch (idle_level) {
	case GPU_HW_IFPC:
		if (is_on(sptprac_clk_reg))
			return false;
		break;
	/* other GMU idle levels can be added here */
	case GPU_HW_ACTIVE:
	default:
		break;
	}
	return true;
}

int a6xx_gmu_wait_for_lowest_idle(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct gmu_device *gmu = &device->gmu;
	unsigned int reg, reg1;
	unsigned long t;
	uint64_t ts1, ts2, ts3;

	if (!kgsl_gmu_isenabled(device))
		return 0;

	ts1 = read_AO_counter(device);

	t = jiffies + msecs_to_jiffies(GMU_IDLE_TIMEOUT);
	do {
		kgsl_gmu_regread(device,
			A6XX_GPU_GMU_CX_GMU_RPMH_POWER_STATE, &reg);
		kgsl_gmu_regread(device,
			A6XX_GMU_SPTPRAC_PWR_CLK_STATUS, &reg1);

		if (idle_trandition_complete(gmu->idle_level, reg, reg1))
			return 0;
		/* Wait 100us to reduce unnecessary AHB bus traffic */
		usleep_range(10, 100);
	} while (!time_after(jiffies, t));

	ts2 = read_AO_counter(device);
	/* Check one last time */

	kgsl_gmu_regread(device, A6XX_GPU_GMU_CX_GMU_RPMH_POWER_STATE, &reg);
	kgsl_gmu_regread(device, A6XX_GMU_SPTPRAC_PWR_CLK_STATUS, &reg1);

	if (idle_trandition_complete(gmu->idle_level, reg, reg1))
		return 0;

	ts3 = read_AO_counter(device);
	WARN(1, "Timeout waiting for lowest idle: %08x %llx %llx %llx %x\n",
		reg, ts1, ts2, ts3, reg1);

	return -ETIMEDOUT;
}

int a6xx_gmu_wait_for_idle(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct gmu_device *gmu = &device->gmu;
	unsigned int status2;
	uint64_t ts1;

	ts1 = read_AO_counter(device);
	if (timed_poll_check(device, A6XX_GPU_GMU_AO_GPU_CX_BUSY_STATUS,
			0, GMU_START_TIMEOUT, CXGXCPUBUSYIGNAHB)) {
		kgsl_gmu_regread(device,
				A6XX_GPU_GMU_AO_GPU_CX_BUSY_STATUS2, &status2);
		dev_err(&gmu->pdev->dev,
				"GMU not idling: status2=0x%x %llx %llx\n",
				status2, ts1, read_AO_counter(device));
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * a6xx_gmu_fw_start() - set up GMU and start FW
 * @device: Pointer to KGSL device
 * @boot_state: State of the GMU being started
 */
static int a6xx_gmu_fw_start(struct kgsl_device *device,
		unsigned int boot_state)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct gmu_device *gmu = &device->gmu;
	struct gmu_memdesc *mem_addr = gmu->hfi_mem;
	int ret;
	unsigned int chipid = 0;

	switch (boot_state) {
	case GMU_RESET:
		/* fall through */
	case GMU_COLD_BOOT:
		/* Turn on TCM retention */
		kgsl_gmu_regwrite(device, A6XX_GMU_GENERAL_7, 1);

		if (!test_and_set_bit(GMU_BOOT_INIT_DONE, &gmu->flags))
			_load_gmu_rpmh_ucode(device);
		else if (boot_state != GMU_RESET) {
			ret = a6xx_rpmh_power_on_gpu(device);
			if (ret)
				return ret;
		}

		if (gmu->load_mode == TCM_BOOT) {
			/* Load GMU image via AHB bus */
			ret = load_gmu_fw(device);
			if (ret)
				return ret;
		} else {
			dev_err(&gmu->pdev->dev, "Unsupported GMU load mode %d\n",
					gmu->load_mode);
			return -EINVAL;
		}
		break;
	case GMU_WARM_BOOT:
		ret = a6xx_rpmh_power_on_gpu(device);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	/* Clear init result to make sure we are getting fresh value */
	kgsl_gmu_regwrite(device, A6XX_GMU_CM3_FW_INIT_RESULT, 0);
	kgsl_gmu_regwrite(device, A6XX_GMU_CM3_BOOT_CONFIG, gmu->load_mode);

	kgsl_gmu_regwrite(device, A6XX_GMU_HFI_QTBL_ADDR,
			mem_addr->gmuaddr);
	kgsl_gmu_regwrite(device, A6XX_GMU_HFI_QTBL_INFO, 1);

	kgsl_gmu_regwrite(device, A6XX_GMU_AHB_FENCE_RANGE_0,
			FENCE_RANGE_MASK);

	/* Pass chipid to GMU FW, must happen before starting GMU */

	/* Keep Core and Major bitfields unchanged */
	chipid = adreno_dev->chipid & 0xFFFF0000;

	/*
	 * Compress minor and patch version into 8 bits
	 * Bit 15-12: minor version
	 * Bit 11-8: patch version
	 */
	chipid = chipid | (ADRENO_CHIPID_MINOR(adreno_dev->chipid) << 12)
			| (ADRENO_CHIPID_PATCH(adreno_dev->chipid) << 8);

	kgsl_gmu_regwrite(device, A6XX_GMU_HFI_SFR_ADDR, chipid);
	init_gmu_log_base(device);

	/* Configure power control and bring the GMU out of reset */
	a6xx_gmu_power_config(device);
	ret = a6xx_gmu_start(device);
	if (ret)
		return ret;

	if (ADRENO_QUIRK(adreno_dev, ADRENO_QUIRK_HFI_USE_REG)) {
		ret = a6xx_gmu_gfx_rail_on(device);
		if (ret) {
			a6xx_gmu_oob_clear(adreno_dev, oob_boot_slumber);
			return ret;
		}
	}

	if (gmu->idle_level < GPU_HW_SPTP_PC) {
		ret = a6xx_gmu_sptprac_enable(adreno_dev);
		if (ret)
			return ret;
	}

	ret = a6xx_gmu_hfi_start(device);
	if (ret)
		return ret;

	/* Make sure the write to start HFI happens before sending a message */
	wmb();
	return ret;
}

/*
 * a6xx_gmu_load_firmware() - Load the ucode into the GPMU RAM & PDC/RSC
 * @device: Pointer to KGSL device
 */
int a6xx_gmu_load_firmware(struct kgsl_device *device)
{
	const struct firmware *fw = NULL;
	const struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct gmu_device *gmu = &device->gmu;
	const struct adreno_gpu_core *gpucore = adreno_dev->gpucore;
	int image_size, ret =  -EINVAL;

	/* there is no GMU */
	if (!kgsl_gmu_isenabled(device))
		return 0;

	/* GMU fw already saved and verified so do nothing new */
	if (gmu->fw_image)
		return 0;

	if (gpucore->gpmufw_name == NULL)
		return -EINVAL;

	ret = request_firmware(&fw, gpucore->gpmufw_name, device->dev);
	if (ret || fw == NULL) {
		KGSL_CORE_ERR("request_firmware (%s) failed: %d\n",
				gpucore->gpmufw_name, ret);
		return ret;
	}

	image_size = PAGE_ALIGN(fw->size);

	ret = allocate_gmu_image(gmu, image_size);

	/* load into shared memory with GMU */
	if (!ret)
		memcpy(gmu->fw_image->hostptr, fw->data, fw->size);

	release_firmware(fw);

	return ret;
}

#define A6XX_STATE_OF_CHILD             (BIT(4) | BIT(5))
#define A6XX_IDLE_FULL_LLM              BIT(0)
#define A6XX_WAKEUP_ACK                 BIT(1)
#define A6XX_IDLE_FULL_ACK              BIT(0)
#define A6XX_VBIF_XIN_HALT_CTRL1_ACKS   (BIT(0) | BIT(1) | BIT(2) | BIT(3))

static int a6xx_llm_glm_handshake(struct kgsl_device *device)
{
	unsigned int val;
	const struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct gmu_device *gmu = &device->gmu;

	if (!ADRENO_FEATURE(adreno_dev, ADRENO_LM) ||
		!test_bit(ADRENO_LM_CTRL, &adreno_dev->pwrctrl_flag))
		return 0;

	kgsl_gmu_regread(device, A6XX_GMU_LLM_GLM_SLEEP_CTRL, &val);
	if (!(val & A6XX_STATE_OF_CHILD)) {
		kgsl_gmu_regrmw(device, A6XX_GMU_LLM_GLM_SLEEP_CTRL, 0, BIT(4));
		kgsl_gmu_regrmw(device, A6XX_GMU_LLM_GLM_SLEEP_CTRL, 0,
				A6XX_IDLE_FULL_LLM);
		if (timed_poll_check(device, A6XX_GMU_LLM_GLM_SLEEP_STATUS,
				A6XX_IDLE_FULL_ACK, GPU_RESET_TIMEOUT,
				A6XX_IDLE_FULL_ACK)) {
			dev_err(&gmu->pdev->dev, "LLM-GLM handshake failed\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void a6xx_isense_disable(struct kgsl_device *device)
{
	unsigned int val;
	const struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	if (!ADRENO_FEATURE(adreno_dev, ADRENO_LM) ||
		!test_bit(ADRENO_LM_CTRL, &adreno_dev->pwrctrl_flag))
		return;

	kgsl_gmu_regread(device, A6XX_GPU_CS_ENABLE_REG, &val);
	if (val) {
		kgsl_gmu_regwrite(device, A6XX_GPU_CS_ENABLE_REG, 0);
		kgsl_gmu_regwrite(device, A6XX_GMU_ISENSE_CTRL, 0);
	}
}

static int a6xx_gmu_suspend(struct kgsl_device *device)
{
	/* Max GX clients on A6xx is 2: GMU and KMD */
	int ret = 0, max_client_num = 2;
	struct gmu_device *gmu = &device->gmu;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	/* do it only if LM feature is enabled */
	/* Disable ISENSE if it's on */
	a6xx_isense_disable(device);

	/* LLM-GLM handshake sequence */
	a6xx_llm_glm_handshake(device);

	/* If SPTP_RAC is on, turn off SPTP_RAC HS */
	a6xx_gmu_sptprac_disable(adreno_dev);

	/* Disconnect GPU from BUS is not needed if CX GDSC goes off later */

	/* Check no outstanding RPMh voting */
	a6xx_complete_rpmh_votes(device);

	if (gmu->gx_gdsc) {
		if (regulator_is_enabled(gmu->gx_gdsc)) {
			/* Switch gx gdsc control from GMU to CPU
			 * force non-zero reference count in clk driver
			 * so next disable call will turn
			 * off the GDSC
			 */
			ret = regulator_enable(gmu->gx_gdsc);
			if (ret)
				dev_err(&gmu->pdev->dev,
					"suspend fail: gx enable\n");

			while ((max_client_num)) {
				ret = regulator_disable(gmu->gx_gdsc);
				if (!regulator_is_enabled(gmu->gx_gdsc))
					break;
				max_client_num -= 1;
			}

			if (!max_client_num)
				dev_err(&gmu->pdev->dev,
					"suspend fail: cannot disable gx\n");
		}
	}

	return ret;
}

/*
 * a6xx_gmu_notify_slumber() - initiate request to GMU to prepare to slumber
 * @device: Pointer to KGSL device
 */
static int a6xx_gmu_notify_slumber(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	struct gmu_device *gmu = &device->gmu;
	int bus_level = pwr->pwrlevels[pwr->default_pwrlevel].bus_freq;
	int perf_idx = gmu->num_gpupwrlevels - pwr->default_pwrlevel - 1;
	int ret, state;

	/* Disable the power counter so that the GMU is not busy */
	kgsl_gmu_regwrite(device, A6XX_GMU_CX_GMU_POWER_COUNTER_ENABLE, 0);

	/* Turn off SPTPRAC if we own it */
	if (gmu->idle_level < GPU_HW_SPTP_PC)
		a6xx_gmu_sptprac_disable(adreno_dev);

	if (!ADRENO_QUIRK(adreno_dev, ADRENO_QUIRK_HFI_USE_REG)) {
		struct hfi_prep_slumber_cmd req = {
			.freq = perf_idx,
			.bw = bus_level,
		};

		ret = hfi_send_req(gmu, H2F_MSG_PREPARE_SLUMBER, &req);
		goto out;
	}

	kgsl_gmu_regwrite(device, A6XX_GMU_BOOT_SLUMBER_OPTION,
			OOB_SLUMBER_OPTION);
	kgsl_gmu_regwrite(device, A6XX_GMU_GX_VOTE_IDX, perf_idx);
	kgsl_gmu_regwrite(device, A6XX_GMU_MX_VOTE_IDX, bus_level);

	ret = a6xx_gmu_oob_set(adreno_dev, oob_boot_slumber);
	a6xx_gmu_oob_clear(adreno_dev, oob_boot_slumber);

	if (!ret) {
		kgsl_gmu_regread(device,
			A6XX_GPU_GMU_CX_GMU_RPMH_POWER_STATE, &state);
		if (state != GPU_HW_SLUMBER) {
			dev_err(&gmu->pdev->dev,
					"Failed to prepare for slumber: 0x%x\n",
					state);
			ret = -EINVAL;
		}
	}

out:
	/* Make sure the fence is in ALLOW mode */
	kgsl_gmu_regwrite(device, A6XX_GMU_AO_AHB_FENCE_CTRL, 0);
	return ret;
}

/*
 * a6xx_rpmh_gpu_pwrctrl() - GPU power control via RPMh/GMU interface
 * @adreno_dev: Pointer to adreno device
 * @mode: requested power mode
 * @arg1: first argument for mode control
 * @arg2: second argument for mode control
 */
int a6xx_gmu_rpmh_gpu_pwrctrl(struct adreno_device *adreno_dev,
		unsigned int mode, unsigned int arg1, unsigned int arg2)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct gmu_device *gmu = &device->gmu;
	int ret;

	switch (mode) {
	case GMU_FW_START:
		ret = a6xx_gmu_fw_start(device, arg1);
		break;
	case GMU_SUSPEND:
		ret = a6xx_gmu_suspend(device);
		break;
	case GMU_FW_STOP:
		if (ADRENO_QUIRK(adreno_dev, ADRENO_QUIRK_HFI_USE_REG))
			a6xx_gmu_oob_clear(adreno_dev, oob_boot_slumber);
		ret = a6xx_rpmh_power_off_gpu(device);
		break;
	case GMU_DCVS_NOHFI:
		ret = a6xx_gmu_dcvs_nohfi(device, arg1, arg2);
		break;
	case GMU_NOTIFY_SLUMBER:
		ret = a6xx_gmu_notify_slumber(device);
		break;
	default:
		dev_err(&gmu->pdev->dev,
				"unsupported GMU power ctrl mode:%d\n", mode);
		ret = -EINVAL;
		break;
	}

	return ret;
}

#define LM_DEFAULT_LIMIT	6000
#define GPU_LIMIT_THRESHOLD_ENABLE	BIT(31)

static uint32_t lm_limit(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);

	if (adreno_dev->lm_limit)
		return adreno_dev->lm_limit;

	if (of_property_read_u32(device->pdev->dev.of_node, "qcom,lm-limit",
		&adreno_dev->lm_limit))
		adreno_dev->lm_limit = LM_DEFAULT_LIMIT;

	return adreno_dev->lm_limit;
}

#define LIMITS_CONFIG(t, s, c, i, a) ( \
		(t & 0xF) | \
		((s & 0xF) << 4) | \
		((c & 0xF) << 8) | \
		((i & 0xF) << 12) | \
		((a & 0xF) << 16))

void a6xx_gmu_enable_lm(struct kgsl_device *device)
{
	int result;
	struct gmu_device *gmu = &device->gmu;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct device *dev = &gmu->pdev->dev;
	struct hfi_lmconfig_cmd cmd;

	kgsl_gmu_regwrite(device, A6XX_GPU_GMU_CX_GMU_PWR_THRESHOLD,
		GPU_LIMIT_THRESHOLD_ENABLE | lm_limit(adreno_dev));
	kgsl_gmu_regwrite(device, A6XX_GMU_AO_SPARE_CNTL, 1);
	kgsl_gmu_regwrite(device, A6XX_GPU_GMU_CX_GMU_ISENSE_CTRL, 0x1);

	gmu->lm_config = LIMITS_CONFIG(1, 1, 1, 0, 0);
	gmu->bcl_config = 0;
	gmu->lm_dcvs_level = 0;

	cmd.limit_conf = gmu->lm_config;
	cmd.bcl_conf = gmu->bcl_config;
	cmd.lm_enable_bitmask = 0;

	if (gmu->lm_dcvs_level <= MAX_GX_LEVELS)
		cmd.lm_enable_bitmask =
			(1 << (gmu->lm_dcvs_level + 1)) - 1;

	result = hfi_send_req(gmu, H2F_MSG_LM_CFG, &cmd);
	if (result)
		dev_err(dev, "Failure enabling limits management:%d\n", result);

}