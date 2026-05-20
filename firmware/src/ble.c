#include "log.h"
#include "app_att.h"
#include "app_ui.h"
#include "app_config.h"
#include "stack/ble/ble.h"
#include "ble_service.h"

#include "ble.h"

#include <battery.h>


#define 	ADV_IDLE_ENTER_DEEP_TIME			BLE_SYNC_WINDOW_SECONDS
#define 	CONN_IDLE_ENTER_DEEP_TIME			BLE_CONN_IDLE_SECONDS



#define 	MY_DIRECT_ADV_TIME					2000000


#define 	MY_APP_ADV_CHANNEL					BLT_ENABLE_ADV_ALL
#define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_30MS
#define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_35MS
// #define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_1S // change to same with stellar
// #define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_1S + 50

#define		BLE_DEVICE_ADDRESS_TYPE 			BLE_DEVICE_ADDRESS_PUBLIC


_attribute_data_retention_ u8 ota_is_working = 0;
_attribute_data_retention_ own_addr_type_t app_own_address_type = OWN_ADDRESS_PUBLIC;


/**
 * @brief      LinkLayer RX & TX FIFO configuration
 */
/* CAL_LL_ACL_RX_BUF_SIZE(maxRxOct): maxRxOct + 22, then 16 byte align */
#define RX_FIFO_SIZE	64
/* must be: 2^n, (power of 2);at least 4; recommended value: 4, 8, 16 */
#define RX_FIFO_NUM		8


/* CAL_LL_ACL_TX_BUF_SIZE(maxTxOct):  maxTxOct + 10, then 4 byte align */
#define TX_FIFO_SIZE	40
/* must be: (2^n), (power of 2); at least 8; recommended value: 8, 16, 32, other value not allowed. */
#define TX_FIFO_NUM		16


_attribute_data_retention_ u8 blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_rxfifo = {
    RX_FIFO_SIZE,
    RX_FIFO_NUM,
    0,
    0,
    blt_rxfifo_b,
};


_attribute_data_retention_ u8 blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_txfifo = {
    TX_FIFO_SIZE,
    TX_FIFO_NUM,
    0,
    0,
    blt_txfifo_b,
};


/**
 * @brief	BLE Advertising data
 */
RAM u8 tbl_advData[31] = {
    0x02, 0x01, 0x06,
    0x0D, 0x09, 'T', 'L', 'a', 'y', '_', 'X', 'X', 'X', 'X', 'X', 'X',
    0x0B, 0xFF, 0xFF, 0xFF, 0x02,
    0x00, 0x00, 0x00, 0x00,  // data_ver
    0x00, 0x00,              // battery_mV
    0x00,                    // temp_c
    0x00,                    // flags
};

/**
 * @brief	BLE Scan Response Packet data
 */
RAM u8 ble_name[13] = {
    12, // length
    DT_COMPLETE_LOCAL_NAME, 'T', 'L', 'a', 'y', '_',
    // 6 ~ 11: MAC address
    '0', '0', '0', '0', '0', '0'
};

RAM u8 mac_public[6];

RAM u8 PUB_KEY[28] = {
    0x93,0x87,0x5b,0xac,0xe7,0xd5,0x34,0x4b,
    0x92,0x32,0x62,0xe4,0xd4,0xca,0x14,0xcf,
    0x92,0x07,0x3e,0x52,0x30,0x22,0x89,0xcb,
    0x50,0x80,0x4b,0x71,
};

extern u8 my_tempVal[2];
extern u8 my_batVal[1];

// Deep-retention sleep duration after the BLE sync window closes. In PEEK
// mode we want to wake briefly before the next adv burst; otherwise the
// original 60s heartbeat for EPD/timer maintenance is fine.
#if BLE_PM_PEEK_ENABLE
#define BLE_SLEEP_SECONDS (BLE_PEEK_INTERVAL_SECONDS - BLE_PEEK_ADV_SECONDS)
#else
#define BLE_SLEEP_SECONDS 60
#endif

_attribute_data_retention_ u32 advertise_begin_tick;
_attribute_data_retention_ u8 sendTerminate_before_enterDeep = 0;
_attribute_data_retention_ u32 latest_user_event_tick;
_attribute_data_retention_ u32 ble_sync_window_begin_tick;
_attribute_data_retention_ u8 ble_sync_window_closed = 0;

_attribute_ram_code_ bool ble_is_connected(void) {
#if DISABLE_BLE
    return FALSE;
#endif
    return blc_ll_getCurrentState() == BLS_LINK_STATE_CONN;
}

_attribute_ram_code_ void ble_note_user_activity(void) {
#if !(DISABLE_BLE)
    latest_user_event_tick = clock_time();
#endif
}

static _attribute_ram_code_ void ble_close_sync_window(void) {
#if !(DISABLE_BLE)
    if (ble_sync_window_closed) {
        return;
    }
    ble_sync_window_closed = 1;
    bls_ll_setAdvEnable(BLC_ADV_DISABLE);
    log_debugf("BLE sync window closed after %d seconds", BLE_SYNC_WINDOW_SECONDS);
#endif
}

_attribute_ram_code_ void ble_open_peek_window(void) {
#if !(DISABLE_BLE) && BLE_PM_PEEK_ENABLE
    if (!ble_sync_window_closed) {
        // Initial sync window still running; no need to re-open.
        return;
    }
    // Pretend the window started long enough ago that it will close after
    // BLE_PEEK_ADV_SECONDS, reusing the existing close-window timer in the
    // BLT idle-state callback.
    ble_sync_window_begin_tick =
        clock_time() - (u32)((u32)(BLE_SYNC_WINDOW_SECONDS - BLE_PEEK_ADV_SECONDS) * 1000000UL);
    ble_sync_window_closed = 0;
    sendTerminate_before_enterDeep = 0;
    bls_ll_setAdvEnable(BLC_ADV_ENABLE);
    log_debugf("BLE peek window opened for %d seconds", BLE_PEEK_ADV_SECONDS);
#endif
}

////////////////////////// Callback //////////////////////////

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_ENTER"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 */
void task_sleep_enter(u8 e, u8 *p, int n) {
    (void)e;
    (void)p;
    (void)n;
}


/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_ADV_DURATION_TIMEOUT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 */
void app_switch_to_undirected_adv(u8 e, u8 *p, int n) {
    (void)e;
    (void)p;
    (void)n;
    log_debugf("app_switch_to_undirected_adv");
    if (ble_sync_window_closed) {
        return;
    }
    bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
        ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
        0, NULL,
        MY_APP_ADV_CHANNEL,
        ADV_FP_NONE);

    /* clear resolving list:
     * 1. delete all devices in resolving list.
     * 2. disable address resolution */
    blc_ll_clearResolvingList();

    bls_ll_setAdvEnable(BLC_ADV_ENABLE); //must: set ADV enable
}


/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_CONNECT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 */
void task_connect(u8 e, u8 *p, int n) {
    (void)e;
    (void)n;
    tlk_contr_evt_connect_t *pConnEvt = (tlk_contr_evt_connect_t*)p;
    tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] connect, intA & advA:", pConnEvt->initA, 12);
    bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 99, CONN_TIMEOUT_4S); // 1 S

    ble_note_user_activity();
}


/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_TERMINATE"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 */
void task_terminate(u8 e, u8 *p, int n) //*p is terminate reason
{
    log_debugf("task_terminate, e: %d, p: %p, n: %d", e, p, n);

    tlk_contr_evt_terminate_t *pEvt = (tlk_contr_evt_terminate_t*)p;
    tlkapi_printf(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] disconnect, reason 0x%x\n", pEvt->terminate_reason);

#if (BLE_APP_PM_ENABLE)
    //user has push terminate packet to BLE TX buffer before deepsleep
    if (sendTerminate_before_enterDeep == 1 && !TEST_CONN_CURRENT_ENABLE && FORCE_SLEEP) {
        sendTerminate_before_enterDeep = 2;
        log_debugf("set sendTerminate_before_enterDeep = 2, BLC_ADV_DISABLE");
        bls_ll_setAdvEnable(BLC_ADV_DISABLE); //disable ADV
    }
#endif

    advertise_begin_tick = clock_time();
    if (clock_time_exceed(ble_sync_window_begin_tick, BLE_SYNC_WINDOW_SECONDS * 1000000)) {
        ble_close_sync_window();
    }
}


/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_EXIT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 */
void task_suspend_exit(u8 e, u8 *p, int n) {
    (void)e;
    (void)p;
    (void)n;
    /* set RF power index, user must set it after every suspend wake_up, because relative setting will be reset in suspend */
    rf_set_power_level_index(MY_RF_POWER_INDEX);
}


/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_DATA_LENGTH_EXCHANGE"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 */
void task_dle_exchange(u8 e, u8 *p, int n) {
    tlk_contr_evt_dataLenExg_t *pEvt = (tlk_contr_evt_dataLenExg_t*)p;
    tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] DLE exchange", &pEvt->connEffectiveMaxRxOctets, 4);
}


/**
 * @brief      callback function of Host Event
 * @param[in]  h - Host Event type
 * @param[in]  para - data pointer of event
 * @param[in]  n - data length of event
 * @return     0
 */
int app_host_event_callback(u32 h, u8 *para, int n) {
    const u8 event = h & 0xFF;

    log_debugf("got host event: %d[%d]\n", event, n);

    switch (event) {
    case GAP_EVT_SMP_PAIRING_BEGIN:
        tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring begin:",
            para, sizeof(gap_smp_pairingBeginEvt_t));
        break;
    case GAP_EVT_SMP_PAIRING_SUCCESS:
        tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring success:",
            para, sizeof(gap_smp_pairingSuccessEvt_t));
        break;
    case GAP_EVT_SMP_PAIRING_FAIL:
        tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring fail:",
            para, sizeof(gap_smp_pairingFailEvt_t));
        break;
    case GAP_EVT_ATT_EXCHANGE_MTU:
        tlkapi_send_string_data(APP_HOST_EVENT_LOG_EN, "[APP][MTU] mtu exchange",
            para, sizeof(gap_gatt_mtuSizeExchangeEvt_t));
        break;
    default:
        break;
    }

    return 0;
}

////////////////////////// Callback End //////////////////////////
static void update_ble_name__mac(void) {
    // append the last 3 MAC bytes as hex to the device name
    static const char hex_ascii[] = "0123456789ABCDEF";
    for (int i = 0; i < 3; i++) {
        const u8 b = mac_public[2 - i];
        ble_name[7 + i * 2]     = hex_ascii[b >> 4];
        ble_name[7 + i * 2 + 1] = hex_ascii[b & 0x0f];
    }
}

extern u8 ble_sync_window_closed;

void layout_refresh_adv_payload(void) {
    static const char hex_ascii[] = "0123456789ABCDEF";
    for (int i = 0; i < 3; i++) {
        const u8 b = mac_public[2 - i];
        tbl_advData[10 + i * 2]     = hex_ascii[b >> 4];
        tbl_advData[10 + i * 2 + 1] = hex_ascii[b & 0x0f];
    }

    memcpy(&tbl_advData[21], &layout_status_buf[0], 4);
    tbl_advData[25] = layout_status_buf[6];
    tbl_advData[26] = layout_status_buf[7];
    tbl_advData[27] = layout_status_buf[8];

    u8 flags = layout_status_buf[9];
    if (ble_sync_window_closed) {
        flags &= ~0x01;
    } else {
        flags |= 0x01;
    }
    layout_status_buf[9] = flags;
    tbl_advData[28] = flags;
}

#if AIRTAG_ENABLE
static void write_public_mac_to_flash(const int flash_addr, const u8 public_mac[6]) {
    flash_write_page(flash_addr, 6, (u8 *)public_mac);
}
#endif

void init_ble(void) {
    //////////////////////////// BLE stack Initialization  Begin //////////////////////////////////
    //////////// Controller Initialization  Begin /////////////////////////
    u8 mac_random_static[6];

#if AIRTAG_ENABLE
    mac_public[5] = PUB_KEY[0] | 0b11000000;
    mac_public[4] = PUB_KEY[1];
    mac_public[3] = PUB_KEY[2];
    mac_public[2] = PUB_KEY[3];
    mac_public[1] = PUB_KEY[4];
    mac_public[0] = PUB_KEY[5];

    write_public_mac_to_flash(flash_sector_mac_address, mac_public);
#else
    /* for 512K Flash, flash_sector_mac_address equals to 0x76000, for 1M  Flash, flash_sector_mac_address equals to 0xFF000 */
    blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);
#endif
    tlkapi_send_string_data(APP_LOG_EN, "[APP][INI]Public Address", mac_public, 6);

    update_ble_name__mac();

#if(BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_PUBLIC)
    app_own_address_type = OWN_ADDRESS_PUBLIC;
#elif(BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
    app_own_address_type = OWN_ADDRESS_RANDOM;
    blc_ll_setRandomAddr(mac_random_static);
#endif

    blc_ll_initBasicMCU();                     //mandatory
    blc_ll_initStandby_module(mac_public);     //mandatory
    blc_ll_initAdvertising_module(mac_public); //legacy advertising module: mandatory for BLE slave
    blc_ll_initConnection_module();            //connection module  mandatory for BLE slave/master
    blc_ll_initSlaveRole_module();             //slave module: 	 mandatory for BLE slave,
    //////////// Controller Initialization  End /////////////////////////


    //////////// Host Initialization  Begin /////////////////////////
    /* Host Initialization */
    /* GAP initialization must be done before any other host feature initialization !!! */
    blc_gap_peripheral_init();                            //gap initialization
    blc_l2cap_register_handler(blc_l2cap_packet_receive); //l2cap initialization
    my_att_init();                                        //gatt initialization
    blc_att_setRxMtuSize(MTU_SIZE_SETTING);               //set MTU size, default MTU is 23 if not call this API

    /* SMP Initialization may involve flash write/erase(when one sector stores too much information,
     *   is about to exceed the sector threshold, this sector must be erased, and all useful information
     *   should re_stored) , so it must be done after battery check */
#if (BLE_APP_SECURITY_ENABLE)
    /* attention: If this API is used, must be called before "blc smp_peripheral_init" when initialization !!! */
    bls_smp_configPairingSecurityInfoStorageAddr(flash_sector_smp_storage);
    blc_smp_peripheral_init();

    /* Hid device on android7.0/7.1 or later version
     * New paring: send security_request immediately after connection complete
     * reConnect:  send security_request 1000mS after connection complete. If master start paring or encryption before 1000mS timeout, slave do not send security_request. */
    blc_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000);
    //if not set, default is:  send "security request" immediately after link layer connection established(regardless of new connection or reconnection)
#else
		blc_smp_setSecurityLevel(No_Security);
#endif


    /* host(GAP/SMP/GATT/ATT) event process: register host event callback and set event mask */
    blc_gap_registerHostEventHandler(app_host_event_callback);
    /* enable some frequently-used host event by default, user can add more host event */
    blc_gap_setEventMask(GAP_EVT_MASK_SMP_PAIRING_BEGIN |
        GAP_EVT_MASK_SMP_PAIRING_SUCCESS |
        GAP_EVT_MASK_SMP_PAIRING_FAIL |
        GAP_EVT_MASK_ATT_EXCHANGE_MTU);
    //////////// Host Initialization  End /////////////////////////

    //////////// Service Initialization  Begin /////////////////////////
#if (BLE_OTA_SERVER_ENABLE)
    ////////////////// OTA relative ////////////////////////
#if (UART_PRINT_DEBUG_ENABLE)
    blc_debug_addStackLog(STK_LOG_OTA_FLOW);
#endif
    blc_ota_initOtaServer_module();

    //blc_ota_setOtaProcessTimeout(30);   //OTA process timeout:  30 seconds
    //blc_ota_setOtaDataPacketTimeout(4);	//OTA data packet timeout:  4 seconds
    blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);
    blc_ota_registerOtaResultIndicationCb(app_ota_end_result);
#endif
    //////////// Service Initialization  End   /////////////////////////

    //////////////////////////// BLE stack Initialization  End //////////////////////////////////


    //////////////////////////// User Configuration for BLE application ////////////////////////////
    ////////////////// config ADV packet /////////////////////
    u8 adv_param_status = BLE_SUCCESS;
#if (BLE_APP_SECURITY_ENABLE)
    u8 bond_number = blc_smp_param_getCurrentBondingDeviceNumber(); //get bonded device number
    smp_param_save_t bondInfo;
    // at least 1 bonding device exist
    if (bond_number) {
        bls_smp_param_loadByIndex(bond_number - 1, &bondInfo); //get the latest bonding device (index: bond_number-1 )
    }

    //set direct ADV
    if (bond_number) {
        /* set direct ADV
         * bondInfo.peer_addr_type & bondInfo.peer_addr is the address in the air packet of "CONNECT_IND" PDU stored in Flash.
         * if peer address is IDA(identity address), bondInfo.peer_addr is OK used here.
         * if peer address is RPA(resolved private address), bondInfo.peer_addr is one RPA peer device has used, it has a correct relation
         * with peer IRK, so it can match to peer device at any time even peer device changes it's RPA. */
        adv_param_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
            ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, app_own_address_type,
            bondInfo.peer_addr_type, bondInfo.peer_addr,
            MY_APP_ADV_CHANNEL,
            ADV_FP_NONE);

        /* If IRK distributed by peer device is valid, peer device may use RPA(resolved private address) at any time,
         * even if it used IDA(identity address) in first pairing phase.
         * So here must add peer IRK to resolving list and enable address resolution, since local device should check if
         * "CONNECT_IND" PDU is sent by the device directed to.
         * attention: local RPA not used, so parameter "local_irk" set to NULL */
        if (blc_app_isIrkValid(bondInfo.peer_irk)) {
            blc_ll_addDeviceToResolvingList(bondInfo.peer_id_adrType, bondInfo.peer_id_addr, bondInfo.peer_irk, NULL);
            blc_ll_setAddressResolutionEnable(1);
        }

        //it is recommended that direct ADV only last for several seconds, then switch to undirected adv
        bls_ll_setAdvDuration(MY_DIRECT_ADV_TIME, 1);
        bls_app_registerEventCallback(BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_undirected_adv);
    } else //set undirected adv
#endif
    {
        adv_param_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
            ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
            0, NULL,
            MY_APP_ADV_CHANNEL,
            ADV_FP_NONE);
    }

    if (adv_param_status != BLE_SUCCESS) {
        tlkapi_printf(APP_LOG_EN, "[APP][INI] ADV parameters error 0x%x!!!\n", adv_param_status);
    }

    ble_sync_window_begin_tick = clock_time();
    ble_sync_window_closed = 0;
    latest_user_event_tick = ble_sync_window_begin_tick;
    layout_refresh_adv_payload();
    ble_sts_t sts_set_adv = bls_ll_setAdvData((u8*)tbl_advData, sizeof(tbl_advData));
    if (sts_set_adv != BLE_SUCCESS) {
        log_debugf("[ERROR] set adv data failed: %d", sts_set_adv);
    }
    bls_ll_setScanRspData((u8*)ble_name, sizeof(ble_name));
    bls_ll_setAdvEnable(BLC_ADV_ENABLE); //ADV enable

    /* set RF power index, user must set it after every suspend wake_up, because relative setting will be reset in suspend */
    rf_set_power_level_index(MY_RF_POWER_INDEX);

    bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &task_connect);
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &task_terminate);
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &task_suspend_exit);
    bls_app_registerEventCallback(BLT_EV_FLAG_DATA_LENGTH_EXCHANGE, &task_dle_exchange);

    ///////////////////// Power Management initialization///////////////////
#if(BLE_APP_PM_ENABLE)
    blc_ll_initPowerManagement_module();

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
    blc_app_setDeepsleepRetentionSramSize(); //select DEEPSLEEP_MODE_RET_SRAM_LOW16K or DEEPSLEEP_MODE_RET_SRAM_LOW32K
    bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
    blc_pm_setDeepsleepRetentionThreshold(95, 95);
    blc_pm_setDeepsleepRetentionEarlyWakeupTiming(270);
#else
    bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN);
    log_verbosef("set suspend mask: 0x%x", SUSPEND_ADV | SUSPEND_CONN);
#endif

    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &task_sleep_enter);
#else
    bls_pm_setSuspendMask (SUSPEND_DISABLE);
    log_verbosef("set suspend mask: 0x%x", SUSPEND_DISABLE);
#endif

    ////////////////////////////////////////////////////////////////////////////////////////////////

    /* Check if any Stack(Controller & Host) Initialization error after all BLE initialization done.
     * attention that code will stuck in "while(1)" if any error detected in initialization, user need find what error happens and then fix it */
    blc_app_checkControllerHostInitialization();

    advertise_begin_tick = clock_time();

    tlkapi_printf(APP_LOG_EN, "[APP][INI] BLE sample init, MTU_SIZE: %d\n", MTU_SIZE_SETTING);
}

_attribute_ram_code_ int RxTxWrite(void *p) {
    ble_note_user_activity();
    return cmd_handler(p);
}

_attribute_data_retention_ u8 _blc_current_state = -1;
/**
 * @brief      power management code for application
 */
_attribute_ram_code_ void blt_pm_proc(void) {
    const u8 new_state = blc_ll_getCurrentState();
    if (_blc_current_state != new_state) {
        log_debugf("blc_ll current state: %d -> %d", _blc_current_state, new_state);
        _blc_current_state = new_state;
    }

#if(BLE_APP_PM_ENABLE)
    if (new_state == BLS_LINK_STATE_IDLE) {
        //PM module can not manage Idle state low power.
        /* user manage BLE Idle state sleep with API "cpu_sleep_wakeup" */
#if (!TEST_CONN_CURRENT_ENABLE && FORCE_SLEEP)   //test connection power, should disable deepSleep
        if (ble_sync_window_closed) {
            log_verbosef("BLE sync window closed; sleep ret 32k with 60s timer");
            cpu_sleep_wakeup(DEEPSLEEP_MODE_RET_SRAM_LOW32K, PM_WAKEUP_TIMER, TICK_SECONDS_LATER(BLE_SLEEP_SECONDS));
        }
        if (sendTerminate_before_enterDeep == 2) {
            log_verbosef("sendTerminate_before_enterDeep == 2, sleep ret 32k with 60s timer");
            //Terminate OK
            analog_write(USED_DEEP_ANA_REG, analog_read(USED_DEEP_ANA_REG) | CONN_DEEP_FLG);
            cpu_sleep_wakeup(DEEPSLEEP_MODE_RET_SRAM_LOW32K, PM_WAKEUP_TIMER, TICK_SECONDS_LATER(BLE_SLEEP_SECONDS)); //deepSleep
        }
#endif
    } else {
        //PM module manage advertising and ACL connection Slave role low power only

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
        bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
#else
        bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN);
        log_verbosef("set suspend mask: 0x%x", SUSPEND_ADV | SUSPEND_CONN);
#endif

#if (!TEST_CONN_CURRENT_ENABLE && FORCE_SLEEP)   //test connection power, should disable deepSleep
        if (!ota_is_working && !blc_ll_isControllerEventPending()) {
            // no controller event pending
            /* Close BLE after the boot sync window expires without a connection. */
            if (blc_ll_getCurrentState() == BLS_LINK_STATE_ADV && !sendTerminate_before_enterDeep &&
                clock_time_exceed(ble_sync_window_begin_tick, ADV_IDLE_ENTER_DEEP_TIME * 1000000)) {
                log_debugf(
                    "close BLE sync window after advertising for %d seconds without being connected",
                    ADV_IDLE_ENTER_DEEP_TIME);
                ble_close_sync_window();
                cpu_sleep_wakeup(DEEPSLEEP_MODE_RET_SRAM_LOW32K, PM_WAKEUP_TIMER, TICK_SECONDS_LATER(BLE_SLEEP_SECONDS)); //deepsleep
            }
            /* enter deepsleep mode after 60 seconds without any UI action(key/voice/led) in connection state. */
            else if (ble_is_connected() &&
                clock_time_exceed(latest_user_event_tick, CONN_IDLE_ENTER_DEEP_TIME * 1000000)) {
                log_verbosef("enter deepsleep mode after %d seconds without any UI action in connection state",
                    CONN_IDLE_ENTER_DEEP_TIME);
                bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN); //push terminate command into BLE TX buffer
                sendTerminate_before_enterDeep = 1;
            }
        }
#endif  //end of !TEST_CONN_CURRENT_ENABLE
    }
#endif  //end of BLE_APP_PM_ENABLE
}

// notify temperature
_attribute_ram_code_ void ble_notify_temp(s16 temp) {
    my_tempVal[0] = temp & 0xFF;
    my_tempVal[1] = temp >> 8;
    blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, TEMP_LEVEL_INPUT_DP_H, my_tempVal, 2);
}

// notify battery level
_attribute_ram_code_ void ble_notify_battery(u8 battery_level) {
    my_batVal[0] = battery_level;
    blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, BATT_LEVEL_INPUT_DP_H, (u8*)my_batVal, 1);
}

_attribute_ram_code_ void update_adv_data(s16 temp, u16 battery_mv) {
    const s8 temp_c = (s8)(temp / 10);
    const u8 flags = layout_status_buf[9];
    layout_att_refresh_status(battery_mv, temp_c, flags);
    layout_refresh_adv_payload();

    ble_sts_t sts_set_adv = bls_ll_setAdvData((u8*)tbl_advData, sizeof(tbl_advData));
    if (sts_set_adv != BLE_SUCCESS) {
        log_debugf("[ERROR] set adv data failed: %d", sts_set_adv);
    }

    u8 battery_level = mv_to_battery_level(battery_mv);
    ble_notify_battery(battery_level);
    ble_notify_temp(temp);

    // log_verbosef("[%d] update adv, temp: %d, battery: %dmv/%d%%", blc_ll_getCurrentState(), temp, battery_mv, battery_level);
}

_attribute_ram_code_ void set_airtag_adv_data() {
    log_verbosef("AIRTAG_ENABLE is disabled");
}
