#include "stm32g4xx_hal.h"
#include "bsp.h"

static FDCAN_HandleTypeDef hcan;
static volatile uint32_t err_count;

/* 170 MHz kernel clock / 17 = 10 MHz -> 20 tq per bit: sync 1 + seg1 15 + seg2 4 = 500 kbit/s, sample point 80 %.
 * Arithmetic only; bit timing must be confirmed on the bench with a scope/logic analyzer. */
int bsp_can_init(void) {
    __HAL_RCC_FDCAN_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_11 | GPIO_PIN_12; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH; g.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOA, &g);
    hcan.Instance = FDCAN1;
    hcan.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    hcan.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hcan.Init.Mode = FDCAN_MODE_NORMAL;
    hcan.Init.AutoRetransmission = ENABLE;
    hcan.Init.TransmitPause = DISABLE;
    hcan.Init.ProtocolException = DISABLE;
    hcan.Init.NominalPrescaler = 17; hcan.Init.NominalSyncJumpWidth = 4;
    hcan.Init.NominalTimeSeg1 = 15; hcan.Init.NominalTimeSeg2 = 4;
    hcan.Init.StdFiltersNbr = 1; hcan.Init.ExtFiltersNbr = 0;
    hcan.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    if (HAL_FDCAN_Init(&hcan) != HAL_OK) return -1;
    FDCAN_FilterTypeDef f = { .IdType = FDCAN_STANDARD_ID, .FilterIndex = 0, .FilterType = FDCAN_FILTER_MASK,
                              .FilterConfig = FDCAN_FILTER_TO_RXFIFO0, .FilterID1 = 0, .FilterID2 = 0 };   /* accept all std ids */
    if (HAL_FDCAN_ConfigFilter(&hcan, &f) != HAL_OK) return -2;
    HAL_FDCAN_ConfigGlobalFilter(&hcan, FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);
    return HAL_FDCAN_Start(&hcan) == HAL_OK ? 0 : -3;
}

/* Bus-off (TEC > 255) sets CCCR.INIT and the HAL never clears it, so without this the node stays silent until reset.
 * Clearing INIT starts the standard recovery (128 x 11 recessive bits). Polled from send/receive, like the MSPM0 driver. */
static void recover_if_bus_off(void) {
    FDCAN_ProtocolStatusTypeDef ps;
    if (HAL_FDCAN_GetProtocolStatus(&hcan, &ps) == HAL_OK && ps.BusOff) { err_count++; CLEAR_BIT(hcan.Instance->CCCR, FDCAN_CCCR_INIT); }
}

void bsp_can_counters(uint32_t *tec, uint32_t *rec, int *bus_off) {
    FDCAN_ErrorCountersTypeDef ec = {0}; FDCAN_ProtocolStatusTypeDef ps = {0};
    HAL_FDCAN_GetErrorCounters(&hcan, &ec); HAL_FDCAN_GetProtocolStatus(&hcan, &ps);
    *tec = ec.TxErrorCnt; *rec = ec.RxErrorCnt; *bus_off = ps.BusOff != 0;
}

int bsp_can_send(uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (dlc > 8) return -1;
    recover_if_bus_off();
    FDCAN_TxHeaderTypeDef h = { .Identifier = id, .IdType = FDCAN_STANDARD_ID, .TxFrameType = FDCAN_DATA_FRAME,
        .DataLength = ((uint32_t)dlc) << 16, .ErrorStateIndicator = FDCAN_ESI_ACTIVE, .BitRateSwitch = FDCAN_BRS_OFF,
        .FDFormat = FDCAN_CLASSIC_CAN, .TxEventFifoControl = FDCAN_NO_TX_EVENTS, .MessageMarker = 0 };
    if (HAL_FDCAN_AddMessageToTxFifoQ(&hcan, &h, (uint8_t *)data) != HAL_OK) { err_count++; return -1; }
    return 0;
}

int bsp_can_receive(uint32_t *id, uint8_t *data, uint8_t *dlc) {
    if (HAL_FDCAN_GetRxFifoFillLevel(&hcan, FDCAN_RX_FIFO0) == 0) { recover_if_bus_off(); return 0; }
    FDCAN_RxHeaderTypeDef h;
    if (HAL_FDCAN_GetRxMessage(&hcan, FDCAN_RX_FIFO0, &h, data) != HAL_OK) { err_count++; return 0; }
    *id = h.Identifier; *dlc = (uint8_t)(h.DataLength >> 16);
    return 1;
}
uint32_t bsp_can_error_count(void) { return err_count; }
int bsp_can_set_loopback(int on) { (void)on; return -1; }   /* not wired on the STM32 build */
