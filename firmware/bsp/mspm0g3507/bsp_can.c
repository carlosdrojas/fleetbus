/* MCAN0 on the MSPM0G3507 as a classic CAN 2.0 node at 500 kbit/s, polled (no CAN interrupts).
 * Call sequence from TI's LP-MSPM0G3507 mcan_* examples (SDK 2.11). NOT YET RUN ON A BOARD.
 *
 * Bit timing (arithmetic, confirm with a scope in P3): CANCLK = HFCLK = 40 MHz crystal, prescaler 2 -> 20 MHz tq clock,
 * 1 sync + 34 seg1 + 5 seg2 = 40 tq = 500 kbit/s, sample point 35/40 = 87.5 % (TI's nominal setting; also the SAE J2284
 * value for 500 kbit/s). DriverLib fields hold "value - 1".
 * FB_CAN_KBPS (CMake cache, default 500) picks 500/250/125 kbit/s by changing only the prescaler (2/4/8); the 40 tq
 * bit and 87.5 % sample point stay the same. Lower rates are for slow bus diodes (1N4007) on the bench.
 *
 * Message RAM (1 KB, byte offsets, 8-byte payload elements = 16 B each):
 *   0x000 no filter elements: the global filter accepts every standard id into RX FIFO0, rejects extended + remote
 *   0x000 RX FIFO0, 14 elements (the TRM maximum per FIFO) -> 0x0E0
 *   0x0E0 TX FIFO, 8 elements                             -> 0x160 */
#include <ti/driverlib/driverlib.h>

#ifndef FB_CAN_KBPS
#define FB_CAN_KBPS 500
#endif
#if FB_CAN_KBPS != 500 && FB_CAN_KBPS != 250 && FB_CAN_KBPS != 125
#error "FB_CAN_KBPS must be 500, 250 or 125"
#endif
#define FB_CAN_PRESCALER (1000 / FB_CAN_KBPS)   /* 40 MHz / (prescaler * 40 tq) */
#include "bsp.h"
#include "bsp_pins.h"

#define RX_FIFO_START 0x000u
#define RX_FIFO_N     14u
#define TX_FIFO_START 0x0E0u
#define TX_FIFO_N     8u
#define ELEM_BYTES    16u
_Static_assert(TX_FIFO_START >= RX_FIFO_START + RX_FIFO_N * ELEM_BYTES, "message RAM regions overlap");
_Static_assert(TX_FIFO_START + TX_FIFO_N * ELEM_BYTES <= 1024u, "message RAM is 1 KB");

static volatile uint32_t err_count;

static void enter_init(void) {
    DL_MCAN_setOpMode(CANFD0, DL_MCAN_OPERATION_MODE_SW_INIT);
    while (DL_MCAN_getOpMode(CANFD0) != DL_MCAN_OPERATION_MODE_SW_INIT) {}
}
static void enter_normal(void) {
    DL_MCAN_setOpMode(CANFD0, DL_MCAN_OPERATION_MODE_NORMAL);
    while (DL_MCAN_getOpMode(CANFD0) != DL_MCAN_OPERATION_MODE_NORMAL) {}
}

int bsp_can_init(void) {
    static const DL_MCAN_ClockConfig clk = { .clockSel = DL_MCAN_FCLK_HFCLK, .divider = DL_MCAN_FCLK_DIV_1 };
    static const DL_MCAN_InitParams init = {
        .fdMode = false, .brsEnable = false, .txpEnable = false, .efbi = false, .pxhddisable = false,
        .darEnable = false,                       /* automatic retransmission on (DAR = 0) */
        .wkupReqEnable = false, .autoWkupEnable = false, .emulationEnable = true,
        .tdcEnable = false, .wdcPreload = 255, .tdcConfig = { .tdcf = 0, .tdco = 0 },
    };
    static const DL_MCAN_ConfigParams cfg = {
        .monEnable = false, .asmEnable = false, .tsPrescalar = 15, .tsSelect = 0,
        .timeoutSelect = DL_MCAN_TIMEOUT_SELECT_CONT, .timeoutPreload = 65535, .timeoutCntEnable = false,
        .filterConfig = { .rrfs = true, .rrfe = true, .anfs = 0 /* accept into FIFO0 */, .anfe = 2 /* reject */ },
    };
    static const DL_MCAN_MsgRAMConfigParams ram = {
        .flssa = 0, .lss = 0, .flesa = 0, .lse = 0,
        .txStartAddr = TX_FIFO_START, .txBufNum = 0, .txFIFOSize = TX_FIFO_N, .txBufMode = 0,
        .txBufElemSize = DL_MCAN_ELEM_SIZE_8BYTES,
        .txEventFIFOStartAddr = 0, .txEventFIFOSize = 0, .txEventFIFOWaterMark = 0,
        .rxFIFO0startAddr = RX_FIFO_START, .rxFIFO0size = RX_FIFO_N, .rxFIFO0waterMark = 0, .rxFIFO0OpMode = 0,
        .rxFIFO1startAddr = 0, .rxFIFO1size = 0, .rxFIFO1waterMark = 0, .rxFIFO1OpMode = 0,
        .rxBufStartAddr = 0, .rxBufElemSize = DL_MCAN_ELEM_SIZE_8BYTES,
        .rxFIFO0ElemSize = DL_MCAN_ELEM_SIZE_8BYTES, .rxFIFO1ElemSize = DL_MCAN_ELEM_SIZE_8BYTES,
    };
    static const DL_MCAN_BitTimingParams bit = {
        .nomRatePrescalar = FB_CAN_PRESCALER - 1, .nomTimeSeg1 = 33, .nomTimeSeg2 = 4, .nomSynchJumpWidth = 4,
        .dataRatePrescalar = 1, .dataTimeSeg1 = 33, .dataTimeSeg2 = 4, .dataSynchJumpWidth = 4,   /* unused: FD off */
    };
    DL_MCAN_reset(CANFD0);
    DL_MCAN_enablePower(CANFD0);
    delay_cycles(16);
    DL_GPIO_initPeripheralOutputFunction(PIN_CAN_TX_IOMUX, PIN_CAN_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(PIN_CAN_RX_IOMUX, PIN_CAN_RX_FUNC);
    DL_MCAN_enableModuleClock(CANFD0);
    DL_MCAN_setClockConfig(CANFD0, (DL_MCAN_ClockConfig *)&clk);
    for (uint32_t t = 0; !DL_MCAN_isMemInitDone(CANFD0); t++) if (t > 1000000u) return -1;
    enter_init();
    if (DL_MCAN_init(CANFD0, (DL_MCAN_InitParams *)&init) != 0) return -2;
    if (DL_MCAN_config(CANFD0, (DL_MCAN_ConfigParams *)&cfg) != 0) return -3;
    if (DL_MCAN_setBitTime(CANFD0, (DL_MCAN_BitTimingParams *)&bit) != 0) return -4;
    if (DL_MCAN_msgRAMConfig(CANFD0, (DL_MCAN_MsgRAMConfigParams *)&ram) != 0) return -5;
    enter_normal();
    return 0;
}

/* Bench self-test only (P3): route TX back to RX inside the controller, nothing goes on the wire. */
int bsp_can_set_loopback(int on) {
    enter_init();
    if (on) DL_MCAN_lpbkModeEnable(CANFD0, DL_MCAN_LPBK_MODE_INTERNAL, true);
    else DL_MCAN_lpbkModeEnable(CANFD0, DL_MCAN_LPBK_MODE_INTERNAL, false);
    enter_normal();
    return 0;
}

/* Bus-off (TEC > 255) puts the M_CAN back into init mode; leaving init starts the standard recovery
 * (128 x 11 recessive bits). Polled from send/receive so a node recovers without an interrupt. */
static void recover_if_bus_off(void) {
    DL_MCAN_ProtocolStatus ps;
    DL_MCAN_getProtocolStatus(CANFD0, &ps);
    if (ps.busOffStatus) { err_count++; enter_normal(); }
}

int bsp_can_send(uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (dlc > 8 || id > 0x7FFu) return -1;
    recover_if_bus_off();
    DL_MCAN_TxFIFOStatus st;
    DL_MCAN_getTxFIFOQueStatus(CANFD0, &st);
    if (st.fifoFull) { err_count++; return -1; }
    DL_MCAN_TxBufElement e = { .id = id << 18, .rtr = 0, .xtd = 0, .esi = 0, .dlc = dlc, .brs = 0, .fdf = 0, .efc = 0, .mm = 0 };
    for (uint8_t i = 0; i < dlc; i++) e.data[i] = data[i];
    DL_MCAN_writeMsgRam(CANFD0, DL_MCAN_MEM_TYPE_FIFO, st.putIdx, &e);
    if (DL_MCAN_TXBufAddReq(CANFD0, st.putIdx) != 0) { err_count++; return -1; }
    return 0;
}

int bsp_can_receive(uint32_t *id, uint8_t *data, uint8_t *dlc) {
    DL_MCAN_RxFIFOStatus st = { .num = DL_MCAN_RX_FIFO_NUM_0 };
    DL_MCAN_getRxFIFOStatus(CANFD0, &st);
    if (st.fillLvl == 0) { recover_if_bus_off(); return 0; }
    if (st.msgLost) {           /* RXF0S.RF0L mirrors IR.RF0L and stays set until cleared: count each overflow once */
        err_count++;
        CANFD0->MCANSS.MCAN.MCAN_IR = MCAN_IR_RF0L_MASK;      /* write-1-to-clear; no EOI: this driver is polled */
    }
    DL_MCAN_RxBufElement e;
    DL_MCAN_readMsgRam(CANFD0, DL_MCAN_MEM_TYPE_FIFO, 0, DL_MCAN_RX_FIFO_NUM_0, &e);
    DL_MCAN_writeRxFIFOAck(CANFD0, DL_MCAN_RX_FIFO_NUM_0, st.getIdx);
    *id = (e.id >> 18) & 0x7FFu;
    *dlc = e.dlc > 8 ? 8 : (uint8_t)e.dlc;
    for (uint8_t i = 0; i < *dlc; i++) data[i] = e.data[i];
    return 1;
}

uint32_t bsp_can_error_count(void) { return err_count; }

void bsp_can_counters(uint32_t *tec, uint32_t *rec, int *bus_off) {
    DL_MCAN_ErrCntStatus ec; DL_MCAN_ProtocolStatus ps;
    DL_MCAN_getErrCounters(CANFD0, &ec); DL_MCAN_getProtocolStatus(CANFD0, &ps);
    *tec = ec.transErrLogCnt; *rec = ec.recErrCnt; *bus_off = ps.busOffStatus != 0;
}
