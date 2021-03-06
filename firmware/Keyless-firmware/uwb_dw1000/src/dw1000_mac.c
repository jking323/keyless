 /*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/**
 * @file dw1000_mac.c
 * @author UWB Core <uwbcore@gmail.com>
 * @date 2018
 * @brief Mac initialization
 *
 * @details This is the mac base class which utilizes the functions to do the configurations related to mac layer based on dependencies.
 *
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <os/os.h>
#include <hal/hal_spi.h>
#include <hal/hal_gpio.h>
#include <stats/stats.h>

#include <dpl/dpl_cputime.h>
#include <uwb/uwb_ftypes.h>
#include <uwb/uwb_mac.h>
#include <dw1000/dw1000_regs.h>
#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_hal.h>
#include <dw1000/dw1000_phy.h>
#include <dw1000/dw1000_stats.h>
#include <dw1000/dw1000_mac.h>


#if MYNEWT_VAL(DW1000_MAC_STATS)
STATS_NAME_START(mac_stat_section)
    STATS_NAME(mac_stat_section, tx_bytes)
    STATS_NAME(mac_stat_section, rx_bytes)
    STATS_NAME(mac_stat_section, DFR_cnt)
    STATS_NAME(mac_stat_section, RTO_cnt)
    STATS_NAME(mac_stat_section, ROV_err)
    STATS_NAME(mac_stat_section, TFG_cnt)
    STATS_NAME(mac_stat_section, LDE_err)
    STATS_NAME(mac_stat_section, RX_err)
    STATS_NAME(mac_stat_section, TXBUF_err)
    STATS_NAME(mac_stat_section, PLL_LL_err)
STATS_NAME_END(mac_stat_section)

#define MAC_STATS_INC(__X) STATS_INC(inst->stat, __X)
#define MAC_STATS_INCN(__X, __Y) STATS_INCN(inst->stat, __X, __Y)
#else
#define MAC_STATS_INC(__X) {}
#define MAC_STATS_INCN(__X, __Y) {}
#endif

static void dw1000_interrupt_ev_cb(struct dpl_event *ev);
static void dw1000_irq(void *arg);

//#define DIAGMSG(s,u) printf(s,u)
#ifndef DIAGMSG
#define DIAGMSG(s,u)
#endif

#define NUM_BR 3            //!< Bit Rate
#define NUM_PRF 2           //!< Pulse repitition frequency
#define NUM_PACS 4          //!< Preamble Acquisition Chunk
#define NUM_BW 2            //!< Bandwidths
#define NUM_SFD 2           //!< Start of frame delimiter


//! map the channel number to the index in the configuration arrays below.
//! 0th element is chan 1, 1st is chan 2, 2nd is chan 3, 3rd is chan 4, 4th is chan 5, 5th is chan 7.
static const uint8_t chan_idx[] = {0, 0, 1, 2, 3, 4, 0, 5};

//! Transmit config parameters
static const uint32_t tx_config[] =
{
    RF_TXCTRL_CH1,
    RF_TXCTRL_CH2,
    RF_TXCTRL_CH3,
    RF_TXCTRL_CH4,
    RF_TXCTRL_CH5,
    RF_TXCTRL_CH7,
};

//! Frequency Synthesiser - PLL configuration
static const uint32_t fs_pll_cfg[] =
{
    FS_PLLCFG_CH1,
    FS_PLLCFG_CH2,
    FS_PLLCFG_CH3,
    FS_PLLCFG_CH4,
    FS_PLLCFG_CH5,
    FS_PLLCFG_CH7
};

//! Frequency Synthesiser - PLL tuning
static const uint8_t fs_pll_tune[] =
{
    FS_PLLTUNE_CH1,
    FS_PLLTUNE_CH2,
    FS_PLLTUNE_CH3,
    FS_PLLTUNE_CH4,
    FS_PLLTUNE_CH5,
    FS_PLLTUNE_CH7
};

//! Bandwidth configuration
static const uint8_t rx_config[] =
{
    RF_RXCTRLH_NBW,
    RF_RXCTRLH_WBW
};

typedef struct {
    uint32_t lo32;
    uint16_t target[NUM_PRF];
} agc_cfg_struct ;


static const agc_cfg_struct agc_config =
{
    AGC_TUNE2_VAL,
    { AGC_TUNE1_16M , AGC_TUNE1_64M }  //!< adc target
};

//! DW non-standard SFD length for 110k, 850k and 6.81M
static const uint8_t dwnsSFDlen[] =
{
    DW_NS_SFD_LEN_110K,
    DW_NS_SFD_LEN_850K,
    DW_NS_SFD_LEN_6M8
};

//! SFD Threshold
static const uint16_t sftsh[NUM_BR][NUM_SFD] =
{
    {
        DRX_TUNE0b_110K_STD,
        DRX_TUNE0b_110K_NSTD
    },
    {
        DRX_TUNE0b_850K_STD,
        DRX_TUNE0b_850K_NSTD
    },
    {
        DRX_TUNE0b_6M8_STD,
        DRX_TUNE0b_6M8_NSTD
    }
};

static const uint16_t dtune1[] =
{
    DRX_TUNE1a_PRF16,
    DRX_TUNE1a_PRF64
};

static const uint32_t digital_bb_config[NUM_PRF][NUM_PACS] =
{
    {
        DRX_TUNE2_PRF16_PAC8,
        DRX_TUNE2_PRF16_PAC16,
        DRX_TUNE2_PRF16_PAC32,
        DRX_TUNE2_PRF16_PAC64
    },
    {
        DRX_TUNE2_PRF64_PAC8,
        DRX_TUNE2_PRF64_PAC16,
        DRX_TUNE2_PRF64_PAC32,
        DRX_TUNE2_PRF64_PAC64
    }
};

static const uint16_t lde_replicaCoeff[] =
{
    0, // No preamble code 0
    LDE_REPC_PCODE_1,
    LDE_REPC_PCODE_2,
    LDE_REPC_PCODE_3,
    LDE_REPC_PCODE_4,
    LDE_REPC_PCODE_5,
    LDE_REPC_PCODE_6,
    LDE_REPC_PCODE_7,
    LDE_REPC_PCODE_8,
    LDE_REPC_PCODE_9,
    LDE_REPC_PCODE_10,
    LDE_REPC_PCODE_11,
    LDE_REPC_PCODE_12,
    LDE_REPC_PCODE_13,
    LDE_REPC_PCODE_14,
    LDE_REPC_PCODE_15,
    LDE_REPC_PCODE_16,
    LDE_REPC_PCODE_17,
    LDE_REPC_PCODE_18,
    LDE_REPC_PCODE_19,
    LDE_REPC_PCODE_20,
    LDE_REPC_PCODE_21,
    LDE_REPC_PCODE_22,
    LDE_REPC_PCODE_23,
    LDE_REPC_PCODE_24
};

/**
 * API to configure the mac layer in dw1000
 * @param inst     Pointer to _dw1000_dev_instance_t.
 * @param config   Pointer to dw1000_dev_config_t.
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_mac_config(struct _dw1000_dev_instance_t * inst,
                  struct uwb_dev_config * config)
{
    uint8_t nsSfd_result  = 0;
    uint8_t useDWnsSFD = 0;
    uint8_t chan;
    uint8_t prfIndex;
    uint8_t bw;
    uint16_t reg16;
    uint32_t regval;

    if (config == NULL) {
        config = &inst->uwb_dev.config;
    } else {
        memcpy(&inst->uwb_dev.config, config, sizeof(struct uwb_dev_config));
    }

    chan = config->channel;
    prfIndex = config->prf - DWT_PRF_16M;
    bw = ((chan == 4) || (chan == 7)) ? 1 : 0 ; // Select wide or narrow band
    reg16 = lde_replicaCoeff[config->rx.preambleCodeIndex];

#ifdef DW1000_API_ERROR_CHECK
    assert(config->dataRate <= DWT_BR_6M8);
    assert(config->rx.pacLength <= DWT_PAC64);
    assert((chan >= 1) && (chan <= 7) && (chan != 6));

    assert(((config->prf == DWT_PRF_64M) && (config->tx.preambleCodeIndex >= 9) &&
            (config->tx.preambleCodeIndex <= 24)) ||
           ((config->prf == DWT_PRF_16M) && (config->tx.preambleCodeIndex >= 1) &&
            (config->tx.preambleCodeIndex <= 8)));

    assert(((config->prf == DWT_PRF_64M) && (config->rx.preambleCodeIndex >= 9) &&
            (config->rx.preambleCodeIndex <= 24)) ||
           ((config->prf == DWT_PRF_16M) && (config->rx.preambleCodeIndex >= 1) &&
            (config->rx.preambleCodeIndex <= 8)));

    assert((config->tx.preambleLength == DWT_PLEN_64) || (config->tx.preambleLength == DWT_PLEN_128) ||
           (config->tx.preambleLength == DWT_PLEN_256)|| (config->tx.preambleLength == DWT_PLEN_512) ||
           (config->tx.preambleLength == DWT_PLEN_1024) ||
           (config->tx.preambleLength == DWT_PLEN_1536) ||
           (config->tx.preambleLength == DWT_PLEN_2048) ||
           (config->tx.preambleLength == DWT_PLEN_4096));

    assert((config->rx.phrMode == DWT_PHRMODE_STD) || (config->rx.phrMode == DWT_PHRMODE_EXT));
#endif
    /* Read sysconfig register */
    inst->sys_cfg_reg = SYS_CFG_MASK & dw1000_read_reg(inst, SYS_CFG_ID, 0, sizeof(uint32_t));

    /* For 110 kbps we need a special setup */
    if(config->dataRate == DWT_BR_110K){
        inst->sys_cfg_reg |= SYS_CFG_RXM110K;
        reg16 >>= 3; // lde_replicaCoeff must be divided by 8
    }else{
        inst->sys_cfg_reg &= (~SYS_CFG_RXM110K);
    }

    inst->sys_cfg_reg &= ~SYS_CFG_PHR_MODE_11;
    inst->sys_cfg_reg |= (SYS_CFG_PHR_MODE_11 & (((uint32_t)config->rx.phrMode) << SYS_CFG_PHR_MODE_SHFT));

    if (config->rxauto_enable)
        inst->sys_cfg_reg |=SYS_CFG_RXAUTR;
    else
        inst->sys_cfg_reg &= (~SYS_CFG_RXAUTR);

    /* By default disable dbl-rxbuffer here and reenable later if needed */
    inst->sys_cfg_reg |= SYS_CFG_DIS_DRXB;

    dw1000_write_reg(inst, SYS_CFG_ID, 0, inst->sys_cfg_reg, sizeof(uint32_t));
    /* Set the lde_replicaCoeff */
    dw1000_write_reg(inst, LDE_IF_ID, LDE_REPC_OFFSET, reg16, sizeof(uint16_t));

    dw1000_phy_config_lde(inst, prfIndex);

    /* Configure PLL2/RF PLL block CFG/TUNE (for a given channel) */
    dw1000_write_reg(inst, FS_CTRL_ID, FS_PLLCFG_OFFSET, fs_pll_cfg[chan_idx[chan]], sizeof(uint32_t));
    dw1000_write_reg(inst, FS_CTRL_ID, FS_PLLTUNE_OFFSET, fs_pll_tune[chan_idx[chan]], sizeof(uint8_t));

    /* Configure RF RX blocks (for specified channel/bandwidth) */
    dw1000_write_reg(inst, RF_CONF_ID, RF_RXCTRLH_OFFSET, rx_config[bw], sizeof(uint8_t));

    /* Configure RF TX blocks (for specified channel and PRF)
     * Configure RF TX control */
    dw1000_write_reg(inst, RF_CONF_ID, RF_TXCTRL_OFFSET, tx_config[chan_idx[chan]], sizeof(uint32_t));

    /* Configure the baseband parameters (for specified PRF, bit rate, PAC, and SFD settings) */
    /* DTUNE0 */
    dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE0b_OFFSET, sftsh[config->dataRate][config->rx.sfdType], sizeof(uint16_t));
    /* DTUNE1 */
    dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE1a_OFFSET, dtune1[prfIndex], sizeof(uint16_t));

    if(config->dataRate == DWT_BR_110K){
        dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE1b_OFFSET, DRX_TUNE1b_110K, sizeof(uint16_t));
    }else{
        if(config->tx.preambleLength == DWT_PLEN_64){
            dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE1b_OFFSET, DRX_TUNE1b_6M8_PRE64, sizeof(uint16_t));
            dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE4H_OFFSET, DRX_TUNE4H_PRE64, sizeof(uint16_t));
        }else{
            dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE1b_OFFSET, DRX_TUNE1b_850K_6M8, sizeof(uint16_t));
            dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE4H_OFFSET, DRX_TUNE4H_PRE128PLUS, sizeof(uint16_t));
        }
    }

    /* DTUNE2 */
    dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE2_OFFSET,
                     digital_bb_config[prfIndex][config->rx.pacLength], sizeof(uint32_t));

    /* DTUNE3 (SFD timeout) */
    /* Don't allow 0 - SFD timeout will always be enabled */
    if(config->rx.sfdTimeout == 0)
        config->rx.sfdTimeout= DWT_SFDTOC_DEF;

    dw1000_write_reg(inst, DRX_CONF_ID, DRX_SFDTOC_OFFSET, config->rx.sfdTimeout, sizeof(uint16_t));

    /* Configure AGC parameters */
    dw1000_write_reg(inst, AGC_CTRL_ID, AGC_TUNE2_OFFSET, agc_config.lo32, sizeof(uint32_t));
    dw1000_write_reg(inst, AGC_CTRL_ID, AGC_TUNE1_OFFSET, agc_config.target[prfIndex], sizeof(uint16_t));

    /* Set (non-standard) user SFD for improved performance, */
    if(config->rx.sfdType){
        /* Write non standard (DW) SFD length */
        dw1000_write_reg(inst, USR_SFD_ID, 0x0, dwnsSFDlen[config->dataRate], sizeof(uint8_t));
        nsSfd_result = 3 ;
        useDWnsSFD = 1 ;
    }
    regval =  (CHAN_CTRL_TX_CHAN_MASK & (((uint32_t)chan) << CHAN_CTRL_TX_CHAN_SHIFT)) |                   // Transmit Channel
        (CHAN_CTRL_RX_CHAN_MASK & (((uint32_t)chan) << CHAN_CTRL_RX_CHAN_SHIFT)) |                         // Receive Channel
        (CHAN_CTRL_RXFPRF_MASK & (((uint32_t)config->prf) << CHAN_CTRL_RXFPRF_SHIFT)) |                    // RX PRF
        ((CHAN_CTRL_TNSSFD|CHAN_CTRL_RNSSFD) & (((uint32_t)nsSfd_result) << CHAN_CTRL_TNSSFD_SHIFT)) |     // nsSFD enable RX&TX
        (CHAN_CTRL_DWSFD & (((uint32_t)useDWnsSFD) << CHAN_CTRL_DWSFD_SHIFT)) |                            // Use DW nsSFD
        (CHAN_CTRL_TX_PCOD_MASK & (((uint32_t)config->tx.preambleCodeIndex) << CHAN_CTRL_TX_PCOD_SHIFT)) | // TX Preamble Code
        (CHAN_CTRL_RX_PCOD_MASK & (((uint32_t)config->rx.preambleCodeIndex) << CHAN_CTRL_RX_PCOD_SHIFT)) ; // RX Preamble Code

    dw1000_write_reg(inst, CHAN_CTRL_ID, 0, regval, sizeof(uint32_t)) ;

    /* Set up TX Preamble Size, PRF and Data Rate */
    inst->tx_fctrl = (((uint32_t)(config->tx.preambleLength | config->prf)) << TX_FCTRL_TXPRF_SHFT) |
        (((uint32_t)config->dataRate) << TX_FCTRL_TXBR_SHFT);
    dw1000_write_reg(inst, TX_FCTRL_ID, 0, inst->tx_fctrl, sizeof(uint32_t));
    /* The SFD transmit pattern is initialised by the DW1000 upon a user TX request,
     * but (due to an IC issue) it is not done for an auto-ACK TX.
     * The SYS_CTRL write below works around this issue, by simultaneously initiating
     * and aborting a transmission, which correctly initialises the SFD
     * after its configuration or reconfiguration. */
    /* Request TX start and TRX off at the same time */
    dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, SYS_CTRL_TXSTRT | SYS_CTRL_TRXOFF, sizeof(uint8_t));

    dw1000_mac_framefilter(inst, config->rx.frameFilter);

    if (config->rxauto_enable)
        assert(config->trxoff_enable);

    if(config->dblbuffon_enabled)
        dw1000_set_dblrxbuff(inst, true);

    return inst->uwb_dev.status;
}


/**
 * API to initialize the mac layer.
 * @param inst     Pointer to _dw1000_dev_instance_t.
 * @param config   Pointer to dw1000_dev_config_t.
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_mac_init(struct _dw1000_dev_instance_t * inst, struct uwb_dev_config * config)
{
    /* Configure DW1000 */
    dw1000_mac_config(inst, config);

    dw1000_tasks_init(inst);

#if MYNEWT_VAL(DW1000_MAC_STATS)
    {
        int rc = stats_init(
            STATS_HDR(inst->stat),
            STATS_SIZE_INIT_PARMS(inst->stat, STATS_SIZE_32),
            STATS_NAME_INIT_PARMS(mac_stat_section));
        assert(rc == 0);

#if  MYNEWT_VAL(DW1000_DEVICE_0) && !MYNEWT_VAL(DW1000_DEVICE_1)
        rc = stats_register("mac", STATS_HDR(inst->stat));
#elif  MYNEWT_VAL(DW1000_DEVICE_0) && MYNEWT_VAL(DW1000_DEVICE_1)
        if (inst == hal_dw1000_inst(0))
            rc |= stats_register("mac0", STATS_HDR(inst->stat));
        else
            rc |= stats_register("mac1", STATS_HDR(inst->stat));
#endif
        assert(rc == 0);
    }
#endif

    return inst->uwb_dev.status;
}

/**
 * API to read the supplied RX data from the DW1000's
 * TX buffer.The input parameters are the data length in bytes and a pointer
 * to those data bytes.
 *
 * @param inst              Pointer to _dw1000_dev_instance_t.
 * @param rxFrameLength     This is the total frame length, including the two byte CRC.
 * Note: This is the length of RX message (including the 2 byte CRC) - max is 1023 standard PHR mode allows up to 127 bytes
 * if > 127 is programmed, DWT_PHRMODE_EXT needs to be set in the phrMode configuration.
 *
 * @param rxFrameBytes      Pointer to the user buffer containing the data to send.
 * @param rxBufferOffset    This specifies an offset in the DW1000s TX Buffer where writing of data starts.
 * @return struct uwb_dev_status
 */
struct uwb_dev_status
dw1000_read_rx(struct _dw1000_dev_instance_t * inst,  uint8_t * rxFrameBytes, uint16_t rxBufferOffset, uint16_t rxFrameLength)
{
    dpl_error_t err;
#ifdef DW1000_API_ERROR_CHECK
    assert((config->rx.phrMode && (txFrameLength <= 1023)) || (txFrameLength <= 127));
    assert((txBufferOffset + txFrameLength) <= 1024);
#endif
    MAC_STATS_INCN(rx_bytes, rxFrameLength);

    err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER);
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    dw1000_read(inst, RX_BUFFER_ID, rxBufferOffset, rxFrameBytes, rxFrameLength);

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}

/**
 * API to write the supplied TX data into the DW1000's
 * TX buffer.The input parameters are the data length in bytes and a pointer
 * to those data bytes.
 *
 * @param inst              Pointer to _dw1000_dev_instance_t.
 * @param txFrameLength     This is the length of TX message, excluding the two byte CRC when auto-FCS
 *                          transmission is enabled.
 *                          In standard PHR mode, maximum is 127 (125 with auto-FCS Transmission).
 *                          With DWT_PHRMODE_EXT set in the phrMode configuration, maximum is 1023 (1021 with
 *                          auto-FCS Transmission).
 *
 * @param txFrameBytes      Pointer to the user buffer containing the data to send.
 * @param txBufferOffset    This specifies an offset in the DW1000s TX Buffer where writing of data starts.
 * @return struct uwb_dev_status
 */
struct uwb_dev_status
dw1000_write_tx(struct _dw1000_dev_instance_t * inst,  uint8_t * txFrameBytes, uint16_t txBufferOffset, uint16_t txFrameLength)
{
    int i;
    dpl_error_t err;
#ifdef DW1000_API_ERROR_CHECK
    assert((config->rx.phrMode && (txFrameLength <= 1023)) || (txFrameLength <= 127));
    assert((txBufferOffset + txFrameLength) <= 1024);
#endif
    MAC_STATS_INCN(tx_bytes, txFrameLength);

    err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER);
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    if ((txBufferOffset + txFrameLength) <= 1024){
        dw1000_write(inst, TX_BUFFER_ID, txBufferOffset,  txFrameBytes, txFrameLength);
        /* This is only valid if the offset is 0, and not always then either  */
        if (txBufferOffset == 0) {
            for (i = 0; i< sizeof(inst->uwb_dev.fctrl); i++)
                inst->uwb_dev.fctrl_array[i] =  txFrameBytes[i];
        }
        inst->uwb_dev.status.tx_frame_error = 0;
    }
    else
        inst->uwb_dev.status.tx_frame_error = 1;

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}

/**
 * API to configure the TX frame control register before the transmission of a frame.
 *
 * @param inst              pointer to _dw1000_dev_instance_t.
 * @param txFrameLength     This is the length of TX message, excluding the two byte CRC when auto-FCS
 *                          transmission is enabled.
 *                          In standard PHR mode, maximum is 127 (125 with auto-FCS Transmission).
 *                          With DWT_PHRMODE_EXT set in the phrMode configuration, maximum is 1023 (1021 with
 *                          auto-FCS Transmission).
 *
 * @param txBufferOffset    The offset in the tx buffer to start writing the data.
 * @param ext               Optional pointer to struct uwb_fctrl_ext with additional parameters
 * @return void
 */
void dw1000_write_tx_fctrl(struct _dw1000_dev_instance_t * inst, uint16_t txFrameLength,
                           uint16_t txBufferOffset, struct uwb_fctrl_ext *ext)
{
    dpl_error_t err;
    uint32_t tx_fctrl_reg;
    struct uwb_dev_config * config = &inst->uwb_dev.config;

#ifdef DW1000_API_ERROR_CHECK
    assert((inst->longFrames && ((txFrameLength + 2) <= 1023)) || ((txFrameLength +2) <= 127));
#endif
    err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER);
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    /* Start from current base tx_fctrl or override with ext params? */
    if (ext) {
        tx_fctrl_reg = (((uint32_t)(ext->preambleLength | config->prf)) << TX_FCTRL_TXPRF_SHFT) |
            (((uint32_t)ext->dataRate) << TX_FCTRL_TXBR_SHFT) |
            (((uint32_t)ext->ranging_en_bit) << TX_FCTRL_TR_SHFT);
#if 0
        if(ext->dataRate != config->dataRate) {
            if (ext->dataRate == DWT_BR_110K) {
                dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE1b_OFFSET, DRX_TUNE1b_110K, sizeof(uint16_t));
            } else {
                if(config->tx.preambleLength == DWT_PLEN_64){
                    dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE1b_OFFSET, DRX_TUNE1b_6M8_PRE64, sizeof(uint16_t));
                    dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE4H_OFFSET, DRX_TUNE4H_PRE64, sizeof(uint16_t));
                } else {
                    dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE1b_OFFSET, DRX_TUNE1b_850K_6M8, sizeof(uint16_t));
                    dw1000_write_reg(inst, DRX_CONF_ID, DRX_TUNE4H_OFFSET, DRX_TUNE4H_PRE128PLUS, sizeof(uint16_t));
                }
            }
        }
#endif
    } else {
        tx_fctrl_reg = inst->tx_fctrl;
    }

    /* Add frame length (+2 for CRC) and start-offset */
    tx_fctrl_reg |= ((txFrameLength + 2) & TX_FCTRL_FLE_MASK)  |
        (((uint32_t)txBufferOffset) << TX_FCTRL_TXBOFFS_SHFT);
    dw1000_write_reg(inst, TX_FCTRL_ID, 0, tx_fctrl_reg, sizeof(uint32_t));

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return;
}

/**
 * API to start transmission.
 *
 * @param inst  pointer to _dw1000_dev_instance_t.
 * @return struct uwb_dev_status
 */
struct uwb_dev_status
dw1000_start_tx(struct _dw1000_dev_instance_t * inst)
{
    dw1000_dev_control_t control;
    struct uwb_dev_config *config;
    uint16_t sys_status_reg;
    uint32_t sys_ctrl_reg;
    dpl_error_t err = dpl_sem_pend(&inst->tx_sem,  DPL_TIMEOUT_NEVER); // Released by a SYS_STATUS_TXFRS event
    if (err != DPL_OK) {
        inst->uwb_dev.status.sem_error = 1;
        goto sem_error;
    }

    control = inst->control;
    config = &inst->uwb_dev.config;

    if (config->trxoff_enable){ // force return to idle state
        dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, (uint8_t) SYS_CTRL_TRXOFF, sizeof(uint8_t));
    }

    sys_ctrl_reg = SYS_CTRL_TXSTRT;
    if (control.wait4resp_enabled){
        sys_ctrl_reg |= SYS_CTRL_WAIT4RESP;
    }
    if (control.delay_start_enabled)
        sys_ctrl_reg |= SYS_CTRL_TXDLYS;

    dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, (uint8_t) sys_ctrl_reg, sizeof(uint8_t));
    if (control.delay_start_enabled){
        sys_status_reg = dw1000_read_reg(inst, SYS_STATUS_ID, 3, sizeof(uint16_t)); // Read at offset 3 to get the upper 2 bytes out of 5
        inst->uwb_dev.status.start_tx_error = (sys_status_reg & ((SYS_STATUS_HPDWARN | SYS_STATUS_TXPUTE) >> 24)) != 0;
        if (inst->uwb_dev.status.start_tx_error){
            /*
            * Half Period Delay Warning (HPDWARN) OR Power Up error (TXPUTE). This event status bit relates to the
            * use of delayed transmit and delayed receive functionality. It indicates the delay is more than half
            * a period of the system clock. There is enough time to send but not to power up individual blocks.
            * Typically when the HPDWARN event is detected the host controller will abort the delayed TX/RX by issuing
            * a TRXOFF transceiver off command and then take whatever remedial action is deemed appropriate for the application.
            * Remedial action is cancle send and report error
            */
            dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, (uint8_t) SYS_CTRL_TRXOFF, sizeof(uint8_t));
            err = dpl_sem_release(&inst->tx_sem);
            assert(err == DPL_OK);
        }
    } else {
        inst->uwb_dev.status.start_tx_error = 0;
    }

    /* If dw1000 is instructed to sleep after tx, release
     * the sem as there will not be a TXDONE irq */
    if(inst->control.sleep_after_tx) {
        inst->uwb_dev.status.sleeping = 1;
        err = dpl_sem_release(&inst->tx_sem);
    }

    inst->control.wait4resp_enabled = false;
    inst->control.wait4resp_delay_enabled = false;
    inst->control.delay_start_enabled = false;
    inst->control.autoack_delay_enabled = false;
    inst->control.on_error_continue_enabled = false;
sem_error:
    return inst->uwb_dev.status;
}

/**
 * Wait for transmission to finish
 *
 * @param inst pointer to _dw1000_dev_instance_t.
 * @param timeout timeout in ticks or DPL_TIMEOUT_NEVER
 * @return int
 */
int
dw1000_tx_wait(struct _dw1000_dev_instance_t * inst, uint32_t timeout)
{
    int rc;
    rc = dpl_sem_pend(&inst->tx_sem,  timeout);
    if (rc == DPL_OK) {
        rc = dpl_sem_release(&inst->tx_sem);
    }
    return rc;
}

/**
 * API to specify a time in future to either turn on the receiver to be ready to receive a frame,
 * or to turn on the transmitter and send a frame. The low-order 9-bits of this register are ignored.
 * The delay is in UWB microseconds * 65535.
 *
 * @param inst     Pointer to _dw1000_dev_instance_t.
 * @param delay    Delayed Send or receive Time.
 * @return struct uwb_dev_status
 */
struct uwb_dev_status
dw1000_set_delay_start(struct _dw1000_dev_instance_t * inst, uint64_t dx_time)
{
    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER);
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    inst->control.delay_start_enabled = true;
    dw1000_write_reg(inst, DX_TIME_ID, 1, dx_time >> 8, DX_TIME_LEN-1);

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}


/**
 * API to keep the transceiver in reception mode to keep on receiving the data.
 *
 * @param inst  pointer to _dw1000_dev_instance_t.
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_start_rx(struct _dw1000_dev_instance_t * inst)
{
    uint16_t sys_ctrl;
    uint8_t sys_status;
    dw1000_dev_control_t control;
    struct uwb_dev_config *config;
    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER);
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    control = inst->control;
    config = &inst->uwb_dev.config;
    inst->uwb_dev.status.rx_restarted = 0;

    if (config->trxoff_enable){ // force return to idle state, if in RX state
        uint8_t state = (uint8_t) dw1000_read_reg(inst, SYS_STATE_ID, PMSC_STATE_OFFSET, sizeof(uint8_t));
        if(state != PMSC_STATE_IDLE){
            dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, (uint8_t) SYS_CTRL_TRXOFF, sizeof(uint8_t));
        }
    }

    sys_ctrl = SYS_CTRL_RXENAB;
    if (config->dblbuffon_enabled) {
        dw1000_sync_rxbufptrs(inst);
    }
    if (control.delay_start_enabled)
        sys_ctrl |= SYS_CTRL_RXDLYE;

    if (control.wait4resp_enabled) {
        sys_ctrl |= SYS_CTRL_WAIT4RESP;
    }

    dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, sys_ctrl, sizeof(uint16_t));
    if (control.delay_start_enabled){   // check for errors
        sys_status = dw1000_read_reg(inst, SYS_STATUS_ID, 3, sizeof(uint8_t));  // Read 1 byte at offset 3 to get the 4th byte out of 5
        inst->uwb_dev.status.start_rx_error = (sys_status & (SYS_STATUS_HPDWARN >> 24)) != 0;
        if (inst->uwb_dev.status.start_rx_error){   // if delay has passed do immediate RX on unless DWT_IDLE_ON_DLY_ERR is true
            dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, (uint8_t) SYS_CTRL_TRXOFF, sizeof(uint8_t)); // return to idle state
            if (control.on_error_continue_enabled){
                sys_ctrl &= ~SYS_CTRL_RXDLYE;
                dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, sys_ctrl, sizeof(uint16_t)); // turn on receiver
            }
        }
    }else{
        inst->uwb_dev.status.start_rx_error = 0;
    }

    inst->control.wait4resp_enabled = false;
    inst->control.wait4resp_delay_enabled = false;
    inst->control.delay_start_enabled = false;
    inst->control.autoack_delay_enabled = false;
    inst->control.start_rx_syncbuf_enabled = false;
    inst->control.on_error_continue_enabled = false;

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}

/**
 * API to gracefully turnoff reception mode.
 *
 * @param inst  pointer to _dw1000_dev_instance_t.
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_stop_rx(struct _dw1000_dev_instance_t * inst)
{
    uint32_t mask;

    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_WAIT_FOREVER);
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    mask = dw1000_read_reg(inst, SYS_MASK_ID, 0 , sizeof(uint32_t)) ; // Read set interrupt mask
    dw1000_write_reg(inst, SYS_MASK_ID, 0, 0, sizeof(uint32_t)) ; // Clear interrupt mask - so we don't get any unwanted events
    dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, (uint8_t) SYS_CTRL_TRXOFF, sizeof(uint8_t)); // return to idle state
    dw1000_write_reg(inst, SYS_STATUS_ID, 0, (SYS_STATUS_ALL_TX | SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_GOOD), sizeof(uint32_t));
    dw1000_write_reg(inst, SYS_MASK_ID, 0, mask, sizeof(uint32_t)); // Restore mask to what it was

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}

/**
 * API to enable wait for response feature.
 *
 * @param inst     Pointer to _dw1000_dev_instance_t.
 * @param enable   Enables/disables the wait for response feature.
 * @return struct uwb_dev_status
 */
inline struct uwb_dev_status
dw1000_set_wait4resp(struct _dw1000_dev_instance_t * inst, bool enable)
{
    inst->uwb_dev.status.rx_restarted = 0;
    inst->control.wait4resp_enabled = enable;
    return inst->uwb_dev.status;
}

/**
 * Enable rx regardless of hpdwarning
 *
 * @param inst    Pointer to _dw1000_dev_instance_t.
 * @param enable  weather to continue with rx regardless of error
 *
 */
inline struct uwb_dev_status
dw1000_set_on_error_continue(struct _dw1000_dev_instance_t * inst, bool enable)
{
    inst->control.on_error_continue_enabled = enable;
    return inst->uwb_dev.status;
}

/**
 * Set rxauto disable
 *
 * @param inst    Pointer to _dw1000_dev_instance_t.
 * @param disable  Disable mac-layer auto rx-reenable feature. The default behavior is rxauto enable, this API overrides default behavior
 * on an individual transaction such as in dw1000_rng_request or dw1000_rng_listen
 *
 */
inline struct uwb_dev_status
dw1000_set_rxauto_disable(struct _dw1000_dev_instance_t * inst, bool disable)
{
    inst->control.rxauto_disable = disable;
    return inst->uwb_dev.status;
}


/**
 * API to adjust RX Wait Timeout period.
 *
 * @param inst      pointer to _dw1000_dev_instance_t.
 * @param timeout   Indicates how long the receiver remains on from the RX enable command.The time parameter used here is in 1.0256
 * us (512/499.2MHz) units If set to 0 the timeout is disabled.
 * @return struct uwb_dev_status
 * @brief The Receive Frame Wait Timeout period is a 16-bit field. The units for this parameter are roughly 1??s,
 * (the exact unit is 512 counts of the fundamental 499.2 MHz UWB clock, or 1.026 ??s). When employing the frame wait timeout,
 * RXFWTO should be set to a value greater than the expected RX frame duration and include an allowance for any uncertainly
 * attaching to the expected transmission start time of the awaited frame.
 * When using .rxauto_enable feature it is important to understand the role of rx_timeout, in this situation it is the timeout
 * that actually turns-off the receiver and returns the transeiver to the idle state.
 */
struct uwb_dev_status
dw1000_adj_rx_timeout(struct _dw1000_dev_instance_t * inst, uint16_t timeout)
{
    dw1000_write_reg(inst, RX_FWTO_ID, RX_FWTO_OFFSET, timeout, sizeof(uint16_t));
    return inst->uwb_dev.status;
}


/**
 * API to set Wait Timeout period.
 *
 * @param inst      pointer to _dw1000_dev_instance_t.
 * @param timeout   Indicates how long the receiver remains on from the RX enable command.The time parameter used here is in 1.0256
 * us (512/499.2MHz) units If set to 0 the timeout is disabled.
 * @return struct uwb_dev_status
 * @brief The Receive Frame Wait Timeout period is a 16-bit field. The units for this parameter are roughly 1??s,
 * (the exact unit is 512 counts of the fundamental 499.2 MHz UWB clock, or 1.026 ??s). When employing the frame wait timeout,
 * RXFWTO should be set to a value greater than the expected RX frame duration and include an allowance for any uncertainly
 * attaching to the expected transmission start time of the awaited frame.
 * When using .rxauto_enable feature it is important to understand the role of rx_timeout, in this situation it is the timeout
 * that actually turns-off the receiver and returns the transeiver to the idle state.
 */
struct uwb_dev_status
dw1000_set_rx_timeout(struct _dw1000_dev_instance_t * inst, uint16_t timeout)
{
    uint8_t sys_cfg_reg, new_reg_val;
    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER); // Block if request pending
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    inst->uwb_dev.status.rx_timeout_error = 0;
    sys_cfg_reg = dw1000_read_reg(inst, SYS_CFG_ID, 3, sizeof(uint8_t));

    inst->control.rx_timeout_enabled = timeout > 0;
    if(inst->control.rx_timeout_enabled) {
        dw1000_write_reg(inst, RX_FWTO_ID, RX_FWTO_OFFSET, timeout, sizeof(uint16_t));
        /* Only update sys_cfg if needed */
        new_reg_val = sys_cfg_reg | (SYS_CFG_RXWTOE>>24);
    } else {
        /* Only update sys_cfg if needed */
        new_reg_val = sys_cfg_reg & (~(SYS_CFG_RXWTOE>>24));
    }

    if (sys_cfg_reg != new_reg_val) {
        dw1000_write_reg(inst, SYS_CFG_ID, 3, new_reg_val, sizeof(uint8_t));
    }

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}

static uint16_t
calc_rx_window_timeout(uint64_t rx_start, uint64_t rx_end)
{
    uint32_t timeout = ((rx_end - rx_start) & UWB_DTU_40BMASK) >> 16;
    /* If more than 8.4s away (more than 1/2 period)- the end has likely
     * already passed so set a short timeout as to trigger a timeout */
    if (timeout > 0x7fffff) {
        timeout = 1;
    }
    /* DW1000 can't have a rx-timeout greater than 0xffff */
    if (timeout > 0xffff) {
        timeout = 0xffff;
    }
    return timeout;
}

static uint32_t
update_rx_window_timeout(struct _dw1000_dev_instance_t * inst, uint64_t rel_start)
{
    uint32_t timeout = calc_rx_window_timeout(rel_start, inst->uwb_dev.abs_timeout);
    dw1000_adj_rx_timeout(inst, timeout);
    return timeout;
}

/**
 * Set Absolute rx start and end, in dtu
 *
 * @param dev      pointer to struct uwb_dev.
 * @param rx_start The future time at which the RX will start, in dwt timeunits (uwb usecs << 16).
 * @param rx_end   The future time at which the RX will end, in dwt timeunits.
 *
 * @return struct uwb_dev_status
 *
 * @brief Automatically adjusts the rx_timeout after each received frame to match dx_time_end
 */
struct uwb_dev_status
dw1000_set_rx_window(struct _dw1000_dev_instance_t * inst, uint64_t rx_start, uint64_t rx_end)
{
    uint16_t timeout;
    dw1000_set_delay_start(inst, rx_start);

    /* Calculate initial rx-timeout */
    timeout = calc_rx_window_timeout(rx_start, rx_end);
    inst->control.abs_timeout = 1;
    inst->uwb_dev.abs_timeout = rx_end;

    dw1000_set_rx_timeout(inst, timeout);
    return inst->uwb_dev.status;
}

/**
 * Set Absolute rx end, in dtu
 *
 * @param dev      pointer to struct uwb_dev.
 * @param rx_end   The future time at which the RX will end, in dwt timeunits.
 *
 * @return struct uwb_dev_status
 *
 * @brief Automatically adjusts the rx_timeout after each received frame to match dx_time_end.
 */
struct uwb_dev_status
dw1000_set_abs_timeout(struct _dw1000_dev_instance_t * inst, uint64_t rx_end)
{
    inst->control.abs_timeout = 1;
    inst->uwb_dev.abs_timeout = rx_end;
    return inst->uwb_dev.status;
}

/**
 * API to synchronize rx buffer pointers to make sure that the host/IC buffer pointers are aligned before starting RX.
 *
 * @param inst  pointer to _dw1000_dev_instance_t.
 * @return struct uwb_dev_status
 */
inline struct uwb_dev_status
dw1000_sync_rxbufptrs(struct _dw1000_dev_instance_t * inst)
{
    uint8_t  buff;

    inst->control.start_rx_syncbuf_enabled = 1;
    // Need to make sure that the host/IC buffer pointers are aligned before starting RX
    buff = dw1000_read_reg(inst, SYS_STATUS_ID, 3, sizeof(uint8_t)); // Read 1 byte at offset 3 to get the 4th byte out of 5

    if((buff & (SYS_STATUS_ICRBP >> 24)) !=         // IC side Receive Buffer Pointer
       ((buff & (SYS_STATUS_HSRBP >> 24)) << 1) )   // Host Side Receive Buffer Pointer
        dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_HRBT_OFFSET, 0x01, sizeof(uint8_t)); // We need to swap RX buffer status reg (write one to toggle internally)

    return inst->uwb_dev.status;
}

/**
 * API to read the data from the Accumulator buffer, from an offset location give by offset parameter.
 *
 * NOTE: Because of an internal memory access delay when reading the accumulator the first octet output is a dummy octet
 *       that should be discarded. This is true no matter what sub-index the read begins at.
 *
 * @param inst       Pointer to _dw1000_dev_instance_t.
 * @param buffer     The buffer into which the data will be read.
 * @param length     The length of data to read (in bytes).
 * @param accOffset  The offset in the acc buffer from which to read the data.
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_read_accdata(struct _dw1000_dev_instance_t * inst, uint8_t *buffer, uint16_t accOffset, uint16_t len)
{
    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER); // Block if request pending
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    // Force on the ACC clocks if we are sequenced
    dw1000_phy_sysclk_ACC(inst, true);
    dw1000_read(inst, ACC_MEM_ID, accOffset, buffer, len) ;
    dw1000_phy_sysclk_ACC(inst, false);

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}


/**
 * API to enable the frame filtering - (the default option is to
 * accept any data and ACK frames with correct destination address.
 *
 * @param inst     Pointer to structure _dw1000_dev_instance_t.
 * @param bitmask  Enables/disables the frame filtering options according to
 *      DWT_FF_NOTYPE_EN        0x000   no frame types allowed
 *      DWT_FF_COORD_EN         0x002   behave as coordinator (can receive frames with no destination address (PAN ID has to match))
 *      DWT_FF_BEACON_EN        0x004   beacon frames allowed
 *      DWT_FF_DATA_EN          0x008   data frames allowed
 *      DWT_FF_ACK_EN           0x010   ack frames allowed
 *      DWT_FF_MAC_EN           0x020   mac control frames allowed
 *      DWT_FF_RSVD_EN          0x040   reserved frame types allowed
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_mac_framefilter(struct _dw1000_dev_instance_t * inst, uint16_t enable)
{
    uint32_t sys_cfg_reg;
    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER); // Block if request pending
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    sys_cfg_reg = SYS_CFG_MASK & dw1000_read_reg(inst, SYS_CFG_ID, 0, sizeof(uint32_t)) ; // Read sysconfig register
    inst->uwb_dev.config.rx.frameFilter = enable;
    if(enable > 0){   // Enable frame filtering and configure frame types
        sys_cfg_reg &= ~(SYS_CFG_FF_ALL_EN);  // Clear all
        sys_cfg_reg |= (enable & SYS_CFG_FF_ALL_EN) | SYS_CFG_FFE;
    } else {
        sys_cfg_reg &= ~(SYS_CFG_FFE);
    }

    dw1000_write_reg(inst, SYS_CFG_ID,0, sys_cfg_reg, sizeof(uint32_t));
    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}



/**
 * Enable the auto-ACK feature.
 * NOTE: needs to have frame filtering enabled as well.
 *
 * @param inst    Pointer to _dw1000_dev_instance_t.
 * @param enable  If non-zero the ACK is sent after this delay, max is 255.
 * @return struct uwb_dev_status
 */
struct uwb_dev_status
dw1000_set_autoack(struct _dw1000_dev_instance_t * inst, bool enable)
{
    uint32_t sys_cfg_reg;
    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER); // Block if request pending
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    sys_cfg_reg = SYS_CFG_MASK & dw1000_read_reg(inst, SYS_CFG_ID, 0, sizeof(uint32_t)); // Read sysconfig register

    inst->uwb_dev.config.autoack_enabled = enable;
    if(inst->uwb_dev.config.autoack_enabled){
        sys_cfg_reg |= SYS_CFG_AUTOACK;
        dw1000_write_reg(inst, SYS_CFG_ID,0, sys_cfg_reg, sizeof(uint32_t));
    } else {
        sys_cfg_reg &= ~SYS_CFG_AUTOACK;
        dw1000_write_reg(inst, SYS_CFG_ID,0, sys_cfg_reg, sizeof(uint32_t));
    }

    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}


/**
 * Set the auto-ACK delay. If the delay (parameter) is 0, the ACK will
 * be sent as-soon-as-possable
 * otherwise it will be sent with a programmed delay (in symbols), max is 255.
 * NOTE: needs to have frame filtering enabled as well.
 *
 * @param inst   Pointer to _dw1000_dev_instance_t.
 * @param delay  If non-zero the ACK is sent after this delay, max is 255.
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_set_autoack_delay(struct _dw1000_dev_instance_t * inst, uint8_t delay)
{
    // In preamble symbols
    dw1000_write_reg(inst, ACK_RESP_T_ID, ACK_RESP_T_ACK_TIM_OFFSET, delay, sizeof(uint8_t));
    dw1000_set_autoack(inst, true);

    return inst->uwb_dev.status;
}


/**
 * Wait-for-Response turn-around Time. This 20-bit field is used to configure the turn-around time between TX complete
 * and RX enable when the wait for response function is being used. This function is enabled by the WAIT4RESP control in
 * Register file: 0x0D ??? System Control Register. The time specified by this W4R_TIM parameter is in units of approximately 1 ??s,
 * or 128 system clock cycles. This configuration may be used to save power by delaying the turn-on of the receiver,
 * to align with the response time of the remote system, rather than turning on the receiver immediately after transmission completes.
 *
 * @param inst   Pointer to _dw1000_dev_instance_t.
 * @param delay  The delay is in UWB microseconds.
 *
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_set_wait4resp_delay(struct _dw1000_dev_instance_t * inst, uint32_t delay)
{
    uint32_t ack_resp_reg;
    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER); // Block if request pending
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    /* Read ACK_RESP_T_ID register */
    /* XXX TODO: Rework to only reading / writing 3 bytes */
    ack_resp_reg = dw1000_read_reg(inst, ACK_RESP_T_ID, 0, sizeof(uint32_t));

    inst->control.wait4resp_delay_enabled = delay > 0;
    /* We only need to write to the register if we're setting a new delay or
     * clearing an existing one */
    if (inst->control.wait4resp_delay_enabled || (ack_resp_reg&ACK_RESP_T_W4R_TIM_MASK)) {
        ack_resp_reg &= ~(ACK_RESP_T_W4R_TIM_MASK) ;        // Clear the timer (19:0)
        ack_resp_reg |= (delay & ACK_RESP_T_W4R_TIM_MASK) ; // In UWB microseconds (e.g. turn the receiver on 20uus after TX)
        dw1000_write_reg(inst, ACK_RESP_T_ID, 0, ack_resp_reg, sizeof(uint32_t));
    }
    err = dpl_mutex_release(&inst->mutex);
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}


/**
 * API to enable the double receive buffer mode.
 *
 * @param inst    Pointer to _dw1000_dev_instance_t.
 * @param enable  1 to enable, 0 to disable the double buffer mode.
 *
 * @return struct uwb_dev_status
 *
 */
struct uwb_dev_status
dw1000_set_dblrxbuff(struct _dw1000_dev_instance_t * inst, bool enable)
{
    uint32_t sys_cfg_reg;
    dpl_error_t err = dpl_mutex_pend(&inst->mutex,  DPL_TIMEOUT_NEVER); // Block if request pending
    if (err != DPL_OK) {
        inst->uwb_dev.status.mtx_error = 1;
        goto mtx_error;
    }

    sys_cfg_reg = SYS_CFG_MASK & dw1000_read_reg(inst, SYS_CFG_ID, 0, sizeof(uint32_t));

    inst->uwb_dev.config.dblbuffon_enabled = enable;
    if(inst->uwb_dev.config.dblbuffon_enabled)
        sys_cfg_reg &= ~SYS_CFG_DIS_DRXB;
    else
        sys_cfg_reg |= SYS_CFG_DIS_DRXB;
    dw1000_write_reg(inst, SYS_CFG_ID, 0, sys_cfg_reg, sizeof(uint32_t));

    dw1000_sync_rxbufptrs(inst);

    err = dpl_mutex_release(&inst->mutex);       // Read modify write critical section exit
    assert(err == DPL_OK);
mtx_error:
    return inst->uwb_dev.status;
}

/**
 * API for reading carrier integrator value
 *
 * @brief This is used to read the RX carrier integrator value
 * (relating to the frequency offset of the TX node)
 *
 * NOTE: This is a 21-bit signed quantity, the function sign extends the most
 *       significant bit, which is bit #20 (numbering from bit zero) to return
 *       a 32-bit signed integer value.
 *
 * @param inst          Pointer to _dw1000_dev_instance_t.
 *
 * @return int32_t the signed carrier integrator value.
 *                 A positive value means the local RX clock is running faster than the remote TX device.
 */
int32_t
dw1000_read_carrier_integrator(struct _dw1000_dev_instance_t * inst)
{
#define B20_SIGN_EXTEND_TEST (0x00100000UL)
#define B20_SIGN_EXTEND_MASK (0xFFF00000UL)
    uint32_t  regval=0;
    /* Read 3 bytes (21-bit quantity) */
    regval = dw1000_read_reg(inst, DRX_CONF_ID, DRX_CARRIER_INT_OFFSET, DRX_CARRIER_INT_LEN);

    /* Check for a negative number */
    if (regval & B20_SIGN_EXTEND_TEST) {
        /* sign extend bit #20 to whole word */
        regval |= B20_SIGN_EXTEND_MASK;
    } else {
        /* make sure upper bits are clear if not sign extending */
        regval &= DRX_CARRIER_INT_MASK;
    }
    /* cast unsigned value to signed quantity and invert
     * to match dw3000. */
    return -((int32_t) regval);
}

/**
 * API for calculating the clock offset ratio from the carrior integrator value
 *
 * @param inst Pointer to _dw1000_dev_instance_t.
 * @param integrator_val carrier integrator value
 *
 * @return float   the relative clock offset ratio
 */
dpl_float64_t
dw1000_calc_clock_offset_ratio(struct _dw1000_dev_instance_t * inst, int32_t integrator_val)
{
    dpl_float64_t ccor;
    dpl_float64_t fom = DPL_FLOAT64_INIT(DWT_FREQ_OFFSET_MULTIPLIER);
    dpl_float64_t hz_to_ppm;
    if (inst->uwb_dev.config.dataRate == DWT_BR_110K) {
        fom = DPL_FLOAT64_INIT(DWT_FREQ_OFFSET_MULTIPLIER_110KB);
    }

    switch ( inst->uwb_dev.config.channel ) {
    case 1: hz_to_ppm = DPL_FLOAT64_INIT(DWT_HZ_TO_PPM_MULTIPLIER_CHAN_1);break;
    case 2: hz_to_ppm = DPL_FLOAT64_INIT(DWT_HZ_TO_PPM_MULTIPLIER_CHAN_2);break;
    case 3: hz_to_ppm = DPL_FLOAT64_INIT(DWT_HZ_TO_PPM_MULTIPLIER_CHAN_3);break;
    case 4: hz_to_ppm = DPL_FLOAT64_INIT(DWT_HZ_TO_PPM_MULTIPLIER_CHAN_4);break;
    case 5: hz_to_ppm = DPL_FLOAT64_INIT(DWT_HZ_TO_PPM_MULTIPLIER_CHAN_5);break;
    case 7: hz_to_ppm = DPL_FLOAT64_INIT(DWT_HZ_TO_PPM_MULTIPLIER_CHAN_7);break;
    default: assert(0);
    }

    ccor = DPL_FLOAT64_MUL(DPL_FLOAT64_I32_TO_F64(integrator_val), DPL_FLOAT64_MUL(fom, hz_to_ppm));
    ccor = DPL_FLOAT64_DIV(ccor, DPL_FLOAT64_INIT(1.0e6));
    return ccor;
}

/**
 * API for reading time tracking offset
 *
 * @brief This is used to read the integrator of the RX timing recovery loop
 *
 * NOTE: This is a 10-bit signed quantity, the function sign extends the most
 *       significant bit, which is bit #18 (numbering from bit zero) to return
 *       a 32-bit signed integer value.
 *
 * @param inst          Pointer to _dw1000_dev_instance_t.
 *
 * @return int32_t the signed integral part of the RX timing recovery loop.
 *                 A positive value means the local RX clock is running faster than the remote TX device.
 */
int32_t
dw1000_read_time_tracking_offset(struct _dw1000_dev_instance_t * inst)
{
#define B18_SIGN_EXTEND_TEST (0x00040000UL)
#define B18_SIGN_EXTEND_MASK (0xFFFC0000UL)
    uint32_t  regval=0;
    /* Read 3 bytes (19-bit quantity) */
    regval = dw1000_read_reg(inst, RX_TTCKO_ID, 0, 3);

    /* Check for a negative number */
    if (regval & B18_SIGN_EXTEND_TEST) {
        /* sign extend bit #18 to whole word */
        regval |= B18_SIGN_EXTEND_MASK;
    } else {
        /* make sure upper bits are clear if not sign extending */
        regval &= RX_TTCKO_RXTOFS_MASK;
    }
    /* cast unsigned value to signed quantity */
    return (int32_t) regval;
}

/**
 * API for calculating the clock offset ratio from the time tracking offset
 *
 * @param inst Pointer to _dw1000_dev_instance_t.
 * @param integrator_val ttcko
 *
 * @return float   the relative clock offset ratio
 */
dpl_float64_t
dw1000_calc_clock_offset_ratio_ttco(struct _dw1000_dev_instance_t * inst, int32_t ttcko)
{
    int32_t denom = 0x01F00000;
    if (inst->uwb_dev.config.prf != DWT_PRF_16M) {
        denom = 0x01FC0000;
    }
    return DPL_FLOAT64_DIV(DPL_FLOAT64_I32_TO_F64(-ttcko), DPL_FLOAT64_I32_TO_F64(denom));
}

/**
 * API to read the RX signal quality diagnostic data.
 *
 * @param inst          Pointer to _dw1000_dev_instance_t.
 * @param diagnostics   Diagnostic structure pointer, this will contain the diagnostic data read from the DW1000.
 * @return void
 */
void
dw1000_read_rxdiag(struct _dw1000_dev_instance_t * inst, struct _dw1000_dev_rxdiag_t * diag)
{
    /* Read several of the diag parameters together, requires that the struct parameters are in the
     * same order as the registers */
    dw1000_read(inst, RX_TIME_ID, RX_TIME_FP_INDEX_OFFSET, (uint8_t*)&diag->rx_time, sizeof(diag->rx_time));
    dw1000_read(inst, RX_FQUAL_ID, 0, (uint8_t*)&diag->rx_fqual, sizeof(diag->rx_fqual));
    diag->pacc_cnt =  (dw1000_read_reg(inst, RX_FINFO_ID, 0, sizeof(uint32_t)) & RX_FINFO_RXPACC_MASK) >> RX_FINFO_RXPACC_SHIFT;
    // diag->pacc_cnt_nosat =  (dw1000_read_reg(inst, DRX_CONF_ID, RPACC_NOSAT_OFFSET, sizeof(uint16_t)) & RPACC_NOSAT_MASK);
}


/**
 * The DW1000 processing of interrupts in a task context instead of the interrupt context such that other interrupts
 * and high priority tasks are not blocked waiting for the interrupt handler to complete processing.
 * This dw1000 softstack needs to coexists with other stacks and sensors interfaces. Use directive DW1000_DEV_TASK_PRIO to defined
 * the priority of the dw1000_softstack at compile time.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return void
 */
void
dw1000_tasks_init(struct _dw1000_dev_instance_t * inst)
{
    /* Check if the tasks are already initiated */
    if (!dpl_eventq_inited(&inst->uwb_dev.eventq))
    {
        /* Initialise task structures in uwb_dev */
        uwb_task_init(&inst->uwb_dev, dw1000_interrupt_ev_cb);

        /* Enable pull-down on IRQ to not get spurious interrupts when dw1000 is sleeping */
        hal_gpio_irq_init(inst->irq_pin, dw1000_irq, inst, HAL_GPIO_TRIG_RISING, HAL_GPIO_PULL_DOWN);
        hal_gpio_irq_enable(inst->irq_pin);
    }
    /* Setup interrupt mask */
    dw1000_phy_interrupt_mask(inst,          SYS_MASK_MCPLOCK | SYS_MASK_MRXDFR | SYS_MASK_MLDEERR | SYS_MASK_MTXFRB | SYS_MASK_MTXFRS | SYS_MASK_ALL_RX_TO   | SYS_MASK_ALL_RX_ERR | SYS_MASK_MTXBERR, false);
    dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_SLP2INIT | SYS_STATUS_CPLOCK| SYS_STATUS_RXDFR | SYS_STATUS_LDEERR | SYS_STATUS_TXFRB | SYS_STATUS_TXFRS | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | SYS_STATUS_TXBERR, sizeof(uint32_t));
    dw1000_phy_interrupt_mask(inst,          SYS_MASK_MCPLOCK | SYS_MASK_MRXDFR | SYS_MASK_MLDEERR | SYS_MASK_MTXFRB | SYS_MASK_MTXFRS | SYS_MASK_ALL_RX_TO   | SYS_MASK_ALL_RX_ERR | SYS_MASK_MTXBERR, true);
}


/**
 * API for the interrupt request.
 *
 * @param arg  Pointer to the queue of interrupts.
 * @return void
 */
static void
dw1000_irq(void *arg)
{
    dw1000_dev_instance_t * inst = arg;
    inst->uwb_dev.irq_at_ticks = dpl_cputime_get32();
    if (!inst->uwb_dev.status.sleeping) {
        dpl_eventq_put(&inst->uwb_dev.eventq, &inst->uwb_dev.interrupt_ev);
    }
}


/**
 * Check for double buffer overrun error
 *
 * @param inst  Pointer to dw1000_dev_instance_t.
 * @return uint8_t 1 = overrun error has occured, 0 otherwise
 */
static uint8_t
dw1000_checkoverrun(dw1000_dev_instance_t * inst)
{
    uint8_t ov = dw1000_read_reg(inst, SYS_STATUS_ID, 2, sizeof(uint8_t)) & (SYS_STATUS_RXOVRR >> 16);
    return (ov!=0);
}

/**
 * Check if IC and Host pointers are equal
 *
 * @param inst  Pointer to dw1000_dev_instance_t.
 * @return uint8_t 1 = host and ic pointers match, 0 otherwise
 */
uint8_t
dw1000_ic_and_host_ptrs_equal(dw1000_dev_instance_t * inst)
{
    uint8_t b = dw1000_read_reg(inst, SYS_STATUS_ID, 3, sizeof(uint8_t));
    /* Check where the receiver is at, and if it's in the same buffer as the host */
    return (uint8_t)((b & (SYS_STATUS_ICRBP >> 24)) == ((b & (SYS_STATUS_HSRBP >> 24)) << 1));
}


/**
 * This is the DW1000's general Interrupt Service Routine. It will process/report the following events:
 *          - RXFCG (through rx_complete_cb callback)
 *          - TXFRS (through tx_complete_cb callback)
 *          - RXRFTO/RXPTO (through rx_timeout_cb callback)
 *          - RXPHE/RXFCE/RXRFSL/RXSFDTO/AFFREJ/LDEERR (through rx_error_cb cbRxErr)
 * For all events, corresponding interrupts are cleared and necessary resets are performed. In addition, in the RXFCG case,
 * received frame information and frame control are read before calling the callback. If double buffering is activated, it
 * will also toggle between reception buffers once the reception callback processing has ended.
 *
 * @param ev  Pointer to the queue of events.
 * @return void
 *
 */
static void
dw1000_interrupt_ev_cb(struct dpl_event *ev)
{
    uint16_t finfo;
    struct uwb_mac_interface * cbs = NULL;
    dw1000_dev_instance_t * inst = dpl_event_get_arg(ev);
    dpl_error_t err = dpl_sem_pend(&inst->uwb_dev.irq_sem,  DPL_TIMEOUT_NEVER);
    if (err != DPL_OK) {
        inst->uwb_dev.status.sem_error = 1;
        goto sem_error_exit;
    }

    /* Read status register */
#if MYNEWT_VAL(DW1000_SYS_STATUS_BACKTRACE_LEN)
    {
        uint32_t irq_utime = dpl_cputime_get32();
#endif
        inst->sys_status = dw1000_read_reg(inst, SYS_STATUS_ID, 0, sizeof(uint32_t));
        /* Check for higher status bits only if needed */
        if (!(inst->sys_status & (SYS_MASK_MCPLOCK | SYS_MASK_MRXDFR | SYS_MASK_MLDEERR | SYS_MASK_MTXFRB | SYS_MASK_MTXFRS | SYS_MASK_ALL_RX_TO | SYS_MASK_ALL_RX_ERR | SYS_MASK_MTXBERR))) {
            inst->sys_status_hi = dw1000_read_reg(inst, SYS_STATUS_ID, 4, sizeof(uint8_t));
        }

#if MYNEWT_VAL(DW1000_SYS_STATUS_BACKTRACE_LEN)
        if(!inst->sys_status_bt_lock) {
            DW1000_SYS_STATUS_BT_ADD(inst, inst->sys_status, irq_utime);
#if MYNEWT_VAL(DW1000_SYS_STATUS_BACKTRACE_HI)
            DW1000_SYS_STATUS_BT_HI(inst, inst->sys_status_hi);
#endif
        }
    }
#endif

    // Set status flags
    inst->uwb_dev.status.rx_error = (inst->sys_status & SYS_STATUS_ALL_RX_ERR) !=0;
    inst->uwb_dev.status.rx_error |= (inst->sys_status_hi & (SYS_STATUS_RXRSCS>>32)) != 0;
    inst->uwb_dev.status.rx_autoframefilt_rej = (inst->sys_status & SYS_STATUS_AFFREJ) !=0;
    inst->uwb_dev.status.rx_timeout_error = (inst->sys_status & SYS_STATUS_ALL_RX_TO) !=0;
    inst->uwb_dev.status.lde_error = (inst->sys_status & SYS_STATUS_LDEDONE) == 0;
    inst->uwb_dev.status.overrun_error = (inst->sys_status & SYS_STATUS_RXOVRR) != 0;
    inst->uwb_dev.status.txbuf_error = (inst->sys_status & SYS_STATUS_TXBERR) != 0;
    inst->uwb_dev.status.autoack_triggered = (inst->sys_status & SYS_STATUS_AAT) != 0;
    inst->uwb_dev.status.rx_prej = (inst->sys_status_hi & (SYS_STATUS_RXPREJ>>32)) != 0;

    /* Clear tx_sem unless this is a TXFRB and not TXFRS */
    if(dpl_sem_get_count(&inst->tx_sem) == 0 && !(
           (inst->sys_status & SYS_STATUS_TXFRB) != 0 &&
           (inst->sys_status & SYS_STATUS_TXFRS) == 0
           )) {
        dpl_error_t err = dpl_sem_release(&inst->tx_sem);
        assert(err == DPL_OK);
    }

    // leading edge detection complete
    if((inst->sys_status & SYS_STATUS_RXFCG)){
        MAC_STATS_INC(DFR_cnt);

        if (inst->uwb_dev.status.overrun_error){
            MAC_STATS_INC(ROV_err);
            /* Overrun flag has been set */
            dw1000_write_reg(inst, SYS_STATUS_ID, 0, (SYS_STATUS_RXOVRR |SYS_STATUS_LDEDONE | SYS_STATUS_RXDFR | SYS_STATUS_RXFCG | SYS_STATUS_RXFCE | SYS_STATUS_RXDFR), sizeof(uint32_t));
            dw1000_phy_forcetrxoff(inst);
            dw1000_phy_rx_reset(inst);
            dw1000_sync_rxbufptrs(inst);
            dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET+1, SYS_CTRL_RXENAB>>8, sizeof(uint8_t));
            goto early_exit;
        }

        // The DW1000 has a bug that render the hardware auto_enable feature useless when used in conjunction with the double buffering.
        // Consequently, we reenable the transeiver in the MAC-layer as early as possable. Note: The default behavior of MAC-Layer
        // is that the transceiver only returns to the IDLE state with a timeout event occured. The MAC-layer should otherwise reenable.

        if (inst->uwb_dev.config.rxauto_enable == 0 && inst->uwb_dev.config.dblbuffon_enabled) {
            if (inst->control.rxauto_disable == false && !inst->uwb_dev.status.autoack_triggered) {
                dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET+1, SYS_CTRL_RXENAB>>8, sizeof(uint8_t));
                inst->uwb_dev.status.rx_restarted = 1;
            }
            inst->control.rxauto_disable = false;
        }

        /* Read frame info - Only the first two bytes of the register are used here. */
        finfo = dw1000_read_reg(inst, RX_FINFO_ID, RX_FINFO_OFFSET, sizeof(uint16_t));
        /* Report frame length - Standard frame length up to 127,
         * extended frame length up to 1023 bytes */
        inst->uwb_dev.frame_len = (finfo & RX_FINFO_RXFL_MASK_1023);

        /* Remove the two appended CRC bytes from frame if data is present */
        if (inst->uwb_dev.frame_len) inst->uwb_dev.frame_len -= 2;

        /* Read the whole frame */
        dw1000_read_rx(inst, inst->uwb_dev.rxbuf, 0,
                       (inst->uwb_dev.frame_len < inst->uwb_dev.rxbuf_size) ?
                       inst->uwb_dev.frame_len : inst->uwb_dev.rxbuf_size);

        /* First two bytes are frame ctrl */
        inst->uwb_dev.fctrl = ((uint16_t)inst->uwb_dev.rxbuf[1]<<8) | inst->uwb_dev.rxbuf[0];

#if MYNEWT_VAL(DW1000_SYS_STATUS_BACKTRACE_LEN)
        if(!inst->sys_status_bt_lock) {
            DW1000_SYS_STATUS_BT_FCTRL(inst, inst->uwb_dev.fctrl);
        }
#endif

        if (inst->uwb_dev.status.lde_error) // retest lde_error condition
            inst->uwb_dev.status.lde_error = (dw1000_read_reg(inst, SYS_STATUS_ID, 1, sizeof(uint8_t))  & (SYS_STATUS_LDEDONE >> 8)) == 0;
        if (inst->uwb_dev.status.lde_error) // LDE error or LDE late
            MAC_STATS_INC(LDE_err);

        inst->uwb_dev.rxtimestamp = dw1000_read_rxtime(inst);
        if (inst->control.abs_timeout) {
            update_rx_window_timeout(inst, inst->uwb_dev.rxtimestamp);
        }

        if (inst->uwb_dev.status.autoack_triggered) {
            /* Because of a previous frame not being received properly, AAT bit can be set upon the proper reception of a frame not requesting for
             * acknowledgement (ACK frame is not actually sent though). If the AAT bit is set, check ACK request bit in frame control to confirm (this
             * implementation works only for IEEE802.15.4-2011 compliant frames).
             * This issue is not documented at the time of writing this code. It should be in next release of DW1000 User Manual (v2.09, from July 2016). */
            if ((inst->uwb_dev.fctrl & UWB_FCTRL_ACK_REQUESTED) == 0){
                /* Clear AAT status bit in callback data register copy and status */
                dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_AAT, sizeof(uint8_t));
                inst->sys_status &= ~SYS_STATUS_AAT;
                inst->uwb_dev.status.autoack_triggered = 0;
            } else {
                /* Clear RX flags in sys_status */
                dw1000_write_reg(inst, SYS_STATUS_ID, 1, (inst->sys_status&(SYS_STATUS_LDEDONE | SYS_STATUS_RXDFR | SYS_STATUS_RXFCG | SYS_STATUS_RXFCE | SYS_STATUS_RXDFR))>>8, sizeof(uint8_t));
            }
        }

        // Collect RX Frame Quality diagnositics
        if(inst->uwb_dev.config.rxdiag_enable)
            dw1000_read_rxdiag(inst, &inst->rxdiag);

        // Toggle the Host side Receive Buffer Pointer
        if (inst->uwb_dev.config.dblbuffon_enabled) {
            // The rxttcko is a poor replacement for the carrier_integrator but
            // better than nothing
            if (inst->uwb_dev.config.rxttcko_enable) {
                inst->uwb_dev.rxttcko = dw1000_read_time_tracking_offset(inst);
            }

            inst->uwb_dev.status.overrun_error = dw1000_checkoverrun(inst);
            if (inst->uwb_dev.status.overrun_error == 0) {
                /* Check where the receiver is at, and if it's in the same buffer as we are,
                 * mask out interrupt flags to avoid spurious interrupts when clearing status bits */
                if (inst->uwb_dev.config.rxauto_enable) {
                    if (dw1000_ic_and_host_ptrs_equal(inst)) {
                        uint8_t mask = dw1000_read_reg(inst, SYS_MASK_ID, 1 , sizeof(uint8_t));
                        dw1000_write_reg(inst, SYS_MASK_ID, 1, 0, sizeof(uint8_t));
                        dw1000_write_reg(inst, SYS_STATUS_ID, 1, (inst->sys_status&(SYS_STATUS_LDEDONE | SYS_STATUS_RXDFR | SYS_STATUS_RXFCG | SYS_STATUS_RXFCE | SYS_STATUS_RXDFR))>>8, sizeof(uint8_t));
                        dw1000_write_reg(inst, SYS_MASK_ID, 1, mask, sizeof(uint8_t));
                    } else {
                        dw1000_write_reg(inst, SYS_STATUS_ID, 1, (inst->sys_status&(SYS_STATUS_LDEDONE | SYS_STATUS_RXDFR | SYS_STATUS_RXFCG | SYS_STATUS_RXFCE | SYS_STATUS_RXDFR))>>8, sizeof(uint8_t));
                    }
                }
                /* Swap buffers */
                dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_HRBT_OFFSET , 0b1, sizeof(uint8_t));
            }else{
                MAC_STATS_INC(ROV_err);
                /* Overrun flag has been set, reset receiver and realign buffers */
                dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_RXOVRR, sizeof(uint32_t));
                dw1000_phy_forcetrxoff(inst);
                dw1000_phy_rx_reset(inst);
                dw1000_sync_rxbufptrs(inst);
                dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET+1, SYS_CTRL_RXENAB>>8, sizeof(uint8_t));
            }
        }else{
            // carrier_integrator only avilable while in single buffer mode.
            inst->uwb_dev.carrier_integrator = dw1000_read_carrier_integrator(inst);
#if MYNEWT_VAL(CIR_ENABLED)
            // Call CIR complete calbacks if present
            if(inst->uwb_dev.config.cir_enable || inst->control.cir_enable) {
                if(!(SLIST_EMPTY(&inst->uwb_dev.interface_cbs))) {
                    SLIST_FOREACH(cbs, &inst->uwb_dev.interface_cbs, next) {
                        if (cbs != NULL && cbs->cir_complete_cb) {
                            if(cbs->cir_complete_cb((struct uwb_dev*)inst,cbs)) continue;
                        }
                    }
                }
                inst->control.cir_enable = false;
            }
#endif
            dw1000_write_reg(inst, SYS_STATUS_ID, 0,
                             inst->sys_status & (SYS_STATUS_LDEDONE | SYS_STATUS_RXPHD | SYS_STATUS_RXDFR |
                                                 SYS_STATUS_RXFCG | SYS_STATUS_RXFCE | SYS_STATUS_RXDFR),
                             sizeof(uint16_t));
            if (inst->control.rxauto_disable == false){
                dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET+1, SYS_CTRL_RXENAB>>8, sizeof(uint8_t));
                inst->uwb_dev.status.rx_restarted = 1;
            }
            inst->control.rxauto_disable = false;

        }

        // Call the corresponding frame services callback if present
        if(!(SLIST_EMPTY(&inst->uwb_dev.interface_cbs))){
            SLIST_FOREACH(cbs, &inst->uwb_dev.interface_cbs, next){
            if (cbs != NULL && cbs->rx_complete_cb)
                if(cbs->rx_complete_cb((struct uwb_dev*)inst,cbs)) continue;
            }
        }
    }

    // Handle TX Frame Begins
    if(inst->sys_status & SYS_STATUS_TXFRB) {
        // Call the corresponding callback if present
        dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_TXFRB, sizeof(uint8_t)); // Clear TX Frame Begins

        if(!(SLIST_EMPTY(&inst->uwb_dev.interface_cbs))){
            SLIST_FOREACH(cbs, &inst->uwb_dev.interface_cbs, next){
            if (cbs!=NULL && cbs->tx_begins_cb)
                if(cbs->tx_begins_cb((struct uwb_dev*)inst,cbs)) break;
            }
        }
    }

    // Handle TX confirmation event
    if(inst->sys_status & SYS_STATUS_TXFRS) {
        MAC_STATS_INC(TFG_cnt);

        dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_ALL_TX, sizeof(uint8_t)); // Clear TX event bits

        if (inst->control.abs_timeout) {
            dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET+1, SYS_CTRL_RXENAB>>8, sizeof(uint8_t));
            update_rx_window_timeout(inst, dw1000_read_txtime(inst));
        }

        if(dpl_sem_get_count(&inst->tx_sem) == 0){
            err = dpl_sem_release(&inst->tx_sem);
            assert(err == DPL_OK);
        }

#if MYNEWT_VAL(DW1000_SYS_STATUS_BACKTRACE_LEN)
        if(!inst->sys_status_bt_lock && !inst->uwb_dev.status.autoack_triggered) {
            /* Assuming the start_tx writes the fctrl at send time */
            DW1000_SYS_STATUS_BT_FCTRL(inst, inst->uwb_dev.fctrl);
        }
#endif

        // Call the corresponding callback if present
        if(!(SLIST_EMPTY(&inst->uwb_dev.interface_cbs))){
            SLIST_FOREACH(cbs, &inst->uwb_dev.interface_cbs, next){
            if (cbs!=NULL && cbs->tx_complete_cb)
                if(cbs->tx_complete_cb((struct uwb_dev*)inst,cbs)) break;
            }
        }
    }
    // Tx buffer error
    if(inst->uwb_dev.status.txbuf_error){
        MAC_STATS_INC(TXBUF_err);
        dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_TXBERR, sizeof(uint32_t));
        if(dpl_sem_get_count(&inst->tx_sem) == 0){
            err = dpl_sem_release(&inst->tx_sem);
            assert(err == DPL_OK);
        }
    }

    // leading edge detection complete
    if(inst->sys_status & SYS_STATUS_LDEERR){
        MAC_STATS_INC(LDE_err);
        dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_LDEERR, sizeof(uint32_t));
    }

    // Handle frame reception/preamble detect timeout events
    if(inst->uwb_dev.status.rx_timeout_error){
        MAC_STATS_INC(RTO_cnt);
        dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_ALL_RX_TO, sizeof(uint32_t)); // Clear RX timeout event bits

        if (inst->control.abs_timeout) {
            /* Absolute timeout active, reactivate receiver if there's still time left */
            uint64_t systime = dw1000_read_systime(inst);
            uint32_t new_timeout = calc_rx_window_timeout(systime, inst->uwb_dev.abs_timeout);
            if (new_timeout > 1) {
                dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET+1, SYS_CTRL_RXENAB>>8, sizeof(uint8_t));
                dw1000_adj_rx_timeout(inst, new_timeout);
            } else {
                inst->control.abs_timeout = false;
            }
        }

        if (!inst->control.abs_timeout) {
            // Because of an issue with receiver restart after error conditions, an RX reset must be applied
            // after any error or timeout event to ensure the next good frame's timestamp is computed correctly.
            // See section "RX Message timestamp" in DW1000 User Manual.
            dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, (uint16_t)SYS_CTRL_TRXOFF, sizeof(uint16_t)) ; // Disable the radio
            dw1000_phy_rx_reset(inst);

            inst->control.cir_enable = false;
            inst->control.rxauto_disable = false;
            inst->control.abs_timeout = false;

            // Call the corresponding frame services callback if present
            if(!(SLIST_EMPTY(&inst->uwb_dev.interface_cbs))){
                SLIST_FOREACH(cbs, &inst->uwb_dev.interface_cbs, next){
                    if (cbs!=NULL && cbs->rx_timeout_cb)
                        if(cbs->rx_timeout_cb((struct uwb_dev*)inst,cbs)) continue;
                }
            }
        }
    }

    // Handle RX errors events
    if(inst->uwb_dev.status.rx_error) {
        MAC_STATS_INC(RX_err);

        // Because of an issue with receiver restart after error conditions, an RX reset must be applied after any error or timeout event to ensure
        // the next good frame's timestamp is computed correctly.
        // See section "RX Message timestamp" in DW1000 User Manual.

        dw1000_write_reg(inst, SYS_STATUS_ID, 0, (SYS_STATUS_ALL_RX_ERR), sizeof(uint32_t)); // Clear RX error event bits

        if (inst->uwb_dev.config.dblbuffon_enabled && inst->uwb_dev.status.overrun_error) {
            MAC_STATS_INC(ROV_err);
            dw1000_phy_rx_reset(inst);
            dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_HRBT_OFFSET, 0b1, sizeof(uint8_t));
            dw1000_sync_rxbufptrs(inst);
        } else {
            dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET, (uint8_t) SYS_CTRL_TRXOFF, sizeof(uint8_t));
            dw1000_phy_rx_reset(inst);
        }
        /* Restart the receiver even if rxauto is not enabled. Timeout remain active if set.
         * NOTE: Because we reset the receiver explicitly above we will need to reenable
         * the receiver even though the auto-enable is on. */
        dw1000_write_reg(inst, SYS_CTRL_ID, SYS_CTRL_OFFSET+1, SYS_CTRL_RXENAB>>8, sizeof(uint8_t));
        if (inst->control.abs_timeout) {
            update_rx_window_timeout(inst, dw1000_read_systime(inst));
        }

        // Call the corresponding frame services callback if present
        if(!(SLIST_EMPTY(&inst->uwb_dev.interface_cbs))){
            SLIST_FOREACH(cbs, &inst->uwb_dev.interface_cbs, next){
            if (cbs!=NULL && cbs->rx_error_cb)
                if(cbs->rx_error_cb((struct uwb_dev*)inst,cbs)) continue;
            }
        }
    }

    /* Clear SLP2INIT event bits */
    if(inst->sys_status & SYS_STATUS_SLP2INIT){
        dw1000_write_reg(inst, SYS_STATUS_ID, 2, SYS_STATUS_SLP2INIT>>16, 1);
    }

    // Handle sleep timer event
    if(inst->sys_status & SYS_STATUS_CLKPLL_LL){
        dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_STATUS_CLKPLL_LL, sizeof(uint32_t));
        MAC_STATS_INC(PLL_LL_err);
    }
    // Handle sleep timer event
    if(inst->sys_status & SYS_MASK_MCPLOCK){
        dw1000_write_reg(inst, SYS_STATUS_ID, 0, SYS_MASK_MCPLOCK, sizeof(uint32_t));

        // restore antenna delay value, these are not preserved during sleep/deepsleep */
        dw1000_phy_set_rx_antennadelay(inst, inst->uwb_dev.rx_antenna_delay);
        dw1000_phy_set_tx_antennadelay(inst, inst->uwb_dev.tx_antenna_delay);

        // Call the corresponding callback if present
        inst->uwb_dev.status.sleeping = 0;
        if(!(SLIST_EMPTY(&inst->uwb_dev.interface_cbs))){
            SLIST_FOREACH(cbs, &inst->uwb_dev.interface_cbs, next){
            if (cbs!=NULL && cbs->sleep_cb)
                if (cbs->sleep_cb((struct uwb_dev*)inst,cbs)) continue;
            }
        }
    }

early_exit:
    dpl_sem_release(&inst->uwb_dev.irq_sem);
sem_error_exit:
    /* Check for possibly missed interrupts occuring whilst we were looking at this one
     * NOTE: Because the interrupt is edge based we will only register an event if the irq pin
     * goes low and then comes back up. If the pin is high now and no event is queued just after
     * swapping rx-buffers this means we didn't have time to finish reading the data
     * from the previous irq until a new one arrived -> queue another irq event for the task */
    if (hal_gpio_read(inst->irq_pin) && !dpl_event_is_queued(ev)) {
        dpl_eventq_put(&inst->uwb_dev.eventq, &inst->uwb_dev.interrupt_ev);
#if MYNEWT_VAL(DW1000_SYS_STATUS_BACKTRACE_LEN)
        if(!inst->sys_status_bt_lock) {
            DW1000_SYS_STATUS_BT_PTR(inst).interrupt_reentry = 1;
        }
#endif
    }

#if MYNEWT_VAL(DW1000_SYS_STATUS_BACKTRACE_LEN)
    if(!inst->sys_status_bt_lock) {
        DW1000_SYS_STATUS_BT_PTR(inst).utime_end = dpl_cputime_get32();
    }
#endif
}


/**
 * API to calculate First Path Power Level (fppl) from an rxdiag structure
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 * @param diag  Pointer to _dw1000_dev_rxdiag_t.
 *
 * @return fppl on success, nan otherwise
 */
dpl_float32_t
dw1000_calc_fppl(struct _dw1000_dev_instance_t * inst,
                 struct _dw1000_dev_rxdiag_t * diag)
{
    dpl_float32_t A, N, v, fppl;
    if (diag->pacc_cnt == 0 ||
        (!diag->fp_amp && !diag->fp_amp2 && !diag->fp_amp3)) {
        return DPL_FLOAT32_NAN();
    }
    A = (inst->uwb_dev.config.prf == DWT_PRF_16M) ? DPL_FLOAT32_INIT(113.77f) : DPL_FLOAT32_INIT(121.74f);
#ifdef __KERNEL__
    N = ui32_to_f32(diag->pacc_cnt);
    v = f32_add(f32_add(ui32_to_f32(diag->fp_amp*diag->fp_amp),
                        ui32_to_f32(diag->fp_amp2*diag->fp_amp2)),
                ui32_to_f32(diag->fp_amp3*diag->fp_amp3));
    v = f32_div(v, f32_mul(N, N));
    fppl = f32_sub(f32_mul(DPL_FLOAT32_INIT(10.0), f64_to_f32(log10_soft(f32_to_f64(v)))), A);
#else
    N = (float)(diag->pacc_cnt);
    v = (float)(diag->fp_amp*diag->fp_amp) +
        (float)(diag->fp_amp2*diag->fp_amp2) +
        (float)(diag->fp_amp3*diag->fp_amp3);
    v /= N * N;
    fppl = 10.0f * log10f(v) - A;
#endif
    return fppl;
}

/**
 * API to calculate First Path Power Level from last RX in dBm,
 * which needs config.rxdiag_enable to be set.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return fppl on success, nan otherwise
 */
dpl_float32_t
dw1000_get_fppl(struct _dw1000_dev_instance_t * inst)
{
    if (!inst->uwb_dev.config.rxdiag_enable) {
        return DPL_FLOAT32_NAN();
    }
    return dw1000_calc_fppl(inst, &inst->rxdiag);
}

/**
 * API to calculate rssi from an rxdiag structure
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 * @param diag  Pointer to _dw1000_dev_rxdiag_t.
 *
 * @return rssi on success, nan otherwise
 */
dpl_float32_t
dw1000_calc_rssi(struct _dw1000_dev_instance_t * inst,
                 struct _dw1000_dev_rxdiag_t * diag)
{
    dpl_float32_t rssi, A, B;
    uint32_t pacc_cnt = diag->pacc_cnt;
    uint32_t cir_pwr = diag->cir_pwr;
    if (cir_pwr == 0 || pacc_cnt == 0) {
        return DPL_FLOAT32_NAN();
    }
    B = (inst->uwb_dev.config.prf == DWT_PRF_16M) ? DPL_FLOAT32_INIT(113.77f) : DPL_FLOAT32_INIT(121.74f);
#ifndef __KERNEL__
    A = cir_pwr * 0x20000/(pacc_cnt * pacc_cnt);
    rssi = 10.0f * log10f(A) - B;
#else
    A = ui32_to_f32((cir_pwr * 0x20000));
    A = f32_div(A, ui32_to_f32(pacc_cnt * pacc_cnt));
    A = f32_mul(i32_to_f32(10), f64_to_f32(log10_soft(f32_to_f64(A))));
    rssi = f32_sub(A, B);
#endif
    return rssi;
}

/**
 * API to calculate rssi from last RX in dBm, which needs config.rxdiag_enable to be set.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return rssi on success, nan otherwise
 */
dpl_float32_t
dw1000_get_rssi(struct _dw1000_dev_instance_t * inst)
{
    if (!inst->uwb_dev.config.rxdiag_enable) {
        return DPL_FLOAT32_NAN();
    }
    return dw1000_calc_rssi(inst, &inst->rxdiag);
}

/**
 * API to give a rough estimate of how likely the received packet is
 * line of sight (LOS). Taken from 4.7 of DW1000 manual.
 *
 * @param rssi rssi as calculated by dw1000_calc_rssi
 * @param fppl fppl as calculated by dw1000_calc_fppl
 *
 * @return 1.0 for likely LOS, 0.0 for non-LOS, with a sliding scale in between.
 */
dpl_float32_t
dw1000_estimate_los(dpl_float32_t rssi, dpl_float32_t fppl)
{
    dpl_float32_t d, los;
#ifdef __KERNEL__
    d = soft_abs32(f32_sub(rssi, fppl));
    /* Less than 6dB difference - LOS */
    if (f32_lt(d, DPL_FLOAT32_INIT(6.0f)))  return DPL_FLOAT32_INIT(1.0f);
    /* More than 10dB difference - NLOS */
    if (f32_lt(DPL_FLOAT32_INIT(10.0f), d)) return DPL_FLOAT32_INIT(0.0f);
    /* 1.0 - (d-6)/4.0; */
    los = f32_sub(DPL_FLOAT32_INIT(1.0f),
        f32_div(f32_sub(d, DPL_FLOAT32_INIT(6.0f)), DPL_FLOAT32_INIT(4.0f)));
#else
    d = DPL_FLOAT32_FABS(DPL_FLOAT32_SUB(rssi, fppl));
    /* Less than 6dB difference - LOS */
    if (d < DPL_FLOAT32_INIT(6.0f))  return DPL_FLOAT32_INIT(1.0f);
    /* More than 10dB difference - NLOS */
    if (d > DPL_FLOAT32_INIT(10.0f)) return DPL_FLOAT32_INIT(0.0f);
    /* 1.0 - (d-6)/4.0; */
    los = DPL_FLOAT32_SUB(DPL_FLOAT32_INIT(1.0f),
               DPL_FLOAT32_DIV(d - DPL_FLOAT32_INIT(6.0f),
                               DPL_FLOAT32_INIT(4.0f))
        );
#endif
    return los;
}

/**
 * API to read system time.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 * @return time
 */
inline uint64_t dw1000_read_systime(struct _dw1000_dev_instance_t * inst){
    uint64_t time = ((uint64_t) dw1000_read_reg(inst, SYS_TIME_ID, SYS_TIME_OFFSET, SYS_TIME_LEN)) & 0x0FFFFFFFFFFULL;
    return time;
}


/**
 * API to read system time at lower offset address.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return time
 */
inline uint32_t dw1000_read_systime_lo(struct _dw1000_dev_instance_t * inst){
    uint32_t time = (uint32_t) dw1000_read_reg(inst, SYS_TIME_ID, SYS_TIME_OFFSET, sizeof(uint32_t));
    return time;
}

/**
 * API to read the anadjusted(raw) receive time.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return time
 */

uint64_t dw1000_read_rawrxtime(struct _dw1000_dev_instance_t * inst){
    uint64_t time = (uint64_t)  dw1000_read_reg(inst, RX_TIME_ID, RX_TIME_FP_RAWST_OFFSET, RX_TIME_RX_STAMP_LEN) & 0x0FFFFFFFFFFULL;
    return time;
}

/**
 * API to read receive time. (As adjusted by the LDE)
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return time
 */

inline uint64_t dw1000_read_rxtime(struct _dw1000_dev_instance_t * inst){
    uint64_t time = (uint64_t)  dw1000_read_reg(inst, RX_TIME_ID, RX_TIME_RX_STAMP_OFFSET, RX_TIME_RX_STAMP_LEN) & 0x0FFFFFFFFFFULL;
    return time;
}


/**
 * API to read receive time at lower offset address.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return time
 */
inline uint32_t dw1000_read_rxtime_lo(struct _dw1000_dev_instance_t * inst){
    uint64_t time = (uint32_t) dw1000_read_reg(inst, RX_TIME_ID, RX_TIME_RX_STAMP_OFFSET, sizeof(uint32_t));
    return time;
}

/**
 * API to read transmission time.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return time
 *
 */
inline uint64_t dw1000_read_txrawst(struct _dw1000_dev_instance_t * inst){
    uint64_t time = (uint64_t) dw1000_read_reg(inst, TX_TIME_ID, TX_TIME_TX_RAWST_OFFSET, TX_TIME_TX_STAMP_LEN) & 0x0FFFFFFFFFFULL;
    return time;
}

/**
 * API to read transmission time.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return time
 *
 */
inline uint64_t dw1000_read_txtime(struct _dw1000_dev_instance_t * inst){
    uint64_t time = (uint64_t) dw1000_read_reg(inst, TX_TIME_ID, TX_TIME_TX_STAMP_OFFSET, TX_TIME_TX_STAMP_LEN) & 0x0FFFFFFFFFFULL;
    return time;
}

/**
 * API to read transmit time at lower offset address.
 *
 * @param inst  Pointer to _dw1000_dev_instance_t.
 *
 * @return time
 */
inline uint32_t dw1000_read_txtime_lo(struct _dw1000_dev_instance_t * inst){
    uint32_t time = (uint32_t) dw1000_read_reg(inst, TX_TIME_ID, TX_TIME_TX_STAMP_OFFSET, sizeof(uint32_t));
    return time;
}

/**
 * @fn dwt_configcwmode()
 *
 * @brief this function sets the DW1000 to transmit cw signal at specific channel
 * frequency.
 *
 * input parameters:
 * @param chan - specifies the operating channel (e.g. 1, 2, 3, 4, 5, 6 or 7)
 *
 */
void
dw1000_configcwmode(struct _dw1000_dev_instance_t * inst, uint8_t chan)
{
    int rc;
    if ((chan < 1) || (chan > 7) || (6 == chan)) {
        assert(0);
    }

    /* Lower the speed of the SPI bus before activating CW mode.
     * This is needed because we disable the hiher sysclk and thus
     * dw1000 only support < 2Mbit spi */
    inst->spi_settings.baudrate = inst->spi_baudrate_low;
    rc = hal_spi_disable(inst->spi_num);
    assert(rc == 0);
    rc = hal_spi_config(inst->spi_num, &inst->spi_settings);
    assert(rc == 0);
    rc = hal_spi_enable(inst->spi_num);
    assert(rc == 0);

    /* disable TX/RX RF block sequencing (needed for cw frame mode) */
    dw1000_phy_disable_sequencing(inst);

    /* config RF pll (for a given channel) */
    /* configure PLL2/RF PLL block CFG */
    dw1000_write_reg(inst, FS_CTRL_ID, FS_PLLCFG_OFFSET,
                     fs_pll_cfg[chan_idx[chan]], sizeof(uint32_t));

    /* Configure RF TX blocks (for specified channel and prf) */
    /* Config RF TX control */
    dw1000_write_reg(inst, RF_CONF_ID, RF_TXCTRL_OFFSET,
                     tx_config[chan_idx[chan]], sizeof(uint32_t));

    /* enable RF PLL */
    dw1000_write_reg(inst, RF_CONF_ID, 0, RF_CONF_TXPLLPOWEN_MASK, sizeof(uint32_t));
    dw1000_write_reg(inst, RF_CONF_ID, 0, RF_CONF_TXALLEN_MASK, sizeof(uint32_t));

    /* configure TX clocks */
    dw1000_write_reg(inst, PMSC_ID,PMSC_CTRL0_OFFSET, 0x22, 1);
    dw1000_write_reg(inst, PMSC_ID, 0x1, 0x07, 1);

    /* disable fine grain TX seq */
    dw1000_write_reg(inst, PMSC_ID, PMSC_TXFINESEQ_OFFSET,
                     PMSC_TXFINESEQ_DISABLE, sizeof(uint16_t));

    /* configure CW mode */
    dw1000_write_reg(inst, TX_CAL_ID, TC_PGTEST_OFFSET,
                     TC_PGTEST_CW, TC_PGTEST_LEN);
}
