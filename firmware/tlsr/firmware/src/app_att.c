#include "tl_common.h"

#include "stack/ble/ble.h"
#include "app_att.h"

#include <string.h>

#include <battery.h>
#include <log.h>
#include <store/ringlog.h>
#include <store/slot.h>

#include "ble.h"
#include "ble_service.h"
#include "epd/epd.h"
#include "time_config.h"

typedef struct {
    /** Minimum value for the connection event (interval. 0x0006 - 0x0C80 * 1.25 ms) */
    u16 intervalMin;
    /** Maximum value for the connection event (interval. 0x0006 - 0x0C80 * 1.25 ms) */
    u16 intervalMax;
    /** Number of LL latency connection events (0x0000 - 0x03e8) */
    u16 latency;
    /** Connection Timeout (0x000A - 0x0C80 * 10 ms) */
    u16 timeout;
} gap_periConnectParams_t;

static const u16 clientCharacterCfgUUID = GATT_UUID_CLIENT_CHAR_CFG;

#if (BLE_OTA_SERVER_ENABLE)
static const u16 userdesc_UUID = GATT_UUID_CHAR_USER_DESC;
#endif

static const u16 serviceChangeUUID = GATT_UUID_SERVICE_CHANGE;

static const u16 my_primaryServiceUUID = GATT_UUID_PRIMARY_SERVICE;

static const u16 my_characterUUID = GATT_UUID_CHARACTER;

static const u16 my_devServiceUUID = SERVICE_UUID_DEVICE_INFORMATION;

static const u16 my_PnPUUID = CHARACTERISTIC_UUID_PNP_ID;

static const u16 my_devNameUUID = GATT_UUID_DEVICE_NAME;

static const u16 my_gapServiceUUID = SERVICE_UUID_GENERIC_ACCESS;

static const u16 my_appearanceUUID = GATT_UUID_APPEARANCE;

static const u16 my_periConnParamUUID = GATT_UUID_PERI_CONN_PARAM;

static const u16 my_appearance = GAP_APPEARE_UNKNOWN;

static const u16 my_gattServiceUUID = SERVICE_UUID_GENERIC_ATTRIBUTE;

static const gap_periConnectParams_t my_periConnParameters = {20, 40, 0, 1000};

static u16 serviceChangeVal[2] = {0};

static u8 serviceChangeCCC[2] = {0, 0};

static const u8 my_devName[] = {'T', 'P', 'T'};

static const u8 my_PnPtrs[] = {0x02, 0x8a, 0x24, 0x66, 0x82, 0x01, 0x00};

//////////////////////// Battery /////////////////////////////////////////////////
static const u16 my_batServiceUUID = SERVICE_UUID_BATTERY;
static const u16 my_batCharUUID = CHARACTERISTIC_UUID_BATTERY_LEVEL;
static u8 batteryValueInCCC[2] = {0, 0};
RAM u8 my_batVal[1] = {50};

//////////////////////// Temp /////////////////////////////////////////////////
static const u16 my_tempServiceUUID = 0x181A;
static const u16 my_tempCharUUID = 0x2A1F;
static u8 tempValueInCCC[2] = {0, 0};
RAM u8 my_tempVal[2] = {0};

//////////////////////// HID (removed) /////////////////////////////////////////////////////

#if (BLE_OTA_SERVER_ENABLE)
//////////////////////// OTA //////////////////////////////////
static const u8 my_OtaServiceUUID[16] = WRAPPING_BRACES(TELINK_OTA_UUID_SERVICE);
static const u8 my_OtaUUID[16] = WRAPPING_BRACES(TELINK_SPP_DATA_OTA);
_attribute_data_retention_ static u8 my_OtaData = 0x00;
_attribute_data_retention_ static u8 otaDataCCC[2] = {0, 0};
static const u8 my_OtaName[] = {'O', 'T', 'A'};
#endif

// RxTx Char
#define EPD_SERVICE_SERV_UUID 0x1F10
#define EPD_RXTX_UUID 0x1F1F
static const u16 my_RxTx_ServiceUUID = EPD_SERVICE_SERV_UUID;
static const u16 my_RxTxUUID = EPD_RXTX_UUID;
static u8 my_RxTx_Data = 0x00;
static u8 RxTxValueInCCC[2];

// Layout service — see docs/tlsr-ble-protocol.md § UUIDs for the derivation.
// Base: 6F636172-1F20-44E0-9B6C-7A_xxxxxxxx_20.
// Bytes in TLSR SDK are LSB-first.
const u8 LAYOUT_SERVICE_UUID[16] = {
    0x20, 0x00, 0x00, 0x00, 0x00, 0x7A, 0x6C, 0x9B,
    0xE0, 0x44, 0x20, 0x1F, 0x72, 0x61, 0x63, 0x6F,
};

#define LAYOUT_CHAR_UUID_BYTES(slot)                                   \
    0x20, (slot), 0x00, 0x00, 0x00, 0x7A, 0x6C, 0x9B,                  \
    0xE0, 0x44, 0x20, 0x1F, 0x72, 0x61, 0x63, 0x6F

#define LAYOUT_CHAR_UUID(slot) { LAYOUT_CHAR_UUID_BYTES(slot) }

const u8 LAYOUT_CHAR_STATUS_UUID[16]    = LAYOUT_CHAR_UUID(0x01);
const u8 LAYOUT_CHAR_XFER_CTL_UUID[16]  = LAYOUT_CHAR_UUID(0x02);
const u8 LAYOUT_CHAR_XFER_DATA_UUID[16] = LAYOUT_CHAR_UUID(0x03);
const u8 LAYOUT_CHAR_XFER_ACK_UUID[16]  = LAYOUT_CHAR_UUID(0x04);
const u8 LAYOUT_CHAR_META_UUID[16]      = LAYOUT_CHAR_UUID(0x05);
const u8 LAYOUT_CHAR_RINGLOG_UUID[16]   = LAYOUT_CHAR_UUID(0x06);

RAM u8 layout_status_buf[16];
RAM u8 layout_meta_buf[32];
RAM u8 layout_xfer_ctl_scratch[20];
RAM u8 layout_xfer_data_scratch[182];
static u8 layout_xfer_ctl_value[1];
static u8 layout_xfer_data_value[1];
static u8 layout_xfer_ack_value[1];
static u8 layout_xfer_ack_cccd[2];
static u8 layout_status_cccd[2];
static ringlog_record_t layout_ringlog_buf;

static void layout_put_u16(u8 *dst, const u16 value)
{
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
}

static void layout_put_u32(u8 *dst, const u32 value)
{
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value >> 16) & 0xFF;
    dst[3] = (value >> 24) & 0xFF;
}

static void layout_att_mark_no_page(void)
{
    layout_put_u32(&layout_status_buf[0], 0xFFFFFFFFUL);
    layout_status_buf[4] = 0x00; // flash_state: no page yet
    layout_status_buf[5] = 0x00; // last_err
}

static void layout_att_load_active_slot(void)
{
    slot_header_t header;
    if (slot_active(&header) == SLOT_NONE) {
        return;
    }

    layout_put_u32(&layout_status_buf[0], header.data_ver);
    layout_status_buf[4] = 0x01; // flash_state: active page present
    layout_status_buf[5] = 0x00; // last_err
}

void layout_att_refresh_status(const u16 battery_mv, const s8 temp_c, const u8 flags)
{
    layout_put_u16(&layout_status_buf[6], battery_mv);
    layout_status_buf[8] = (u8)temp_c;
    layout_status_buf[9] = flags;
}

static void layout_att_init_meta(void)
{
    memset(layout_meta_buf, 0, sizeof(layout_meta_buf));
    layout_put_u16(&layout_meta_buf[0], 0x0001);
    layout_put_u32(&layout_meta_buf[2], BUILD_UNIX_TIMESTAMP);
    layout_put_u16(&layout_meta_buf[6], EPD_WIDTH);
    layout_put_u16(&layout_meta_buf[8], EPD_HEIGHT);
    layout_meta_buf[10] = 2;
    layout_put_u16(&layout_meta_buf[11], 0x0007);
    layout_put_u32(&layout_meta_buf[13], 131072UL);
    layout_put_u16(&layout_meta_buf[17], 185);
}

//// GAP attribute values
static const u8 my_devNameCharVal[5] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(GenericAccess_DeviceName_DP_H), U16_HI(GenericAccess_DeviceName_DP_H),
    U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)
};
static const u8 my_appearanceCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(GenericAccess_Appearance_DP_H), U16_HI(GenericAccess_Appearance_DP_H),
    U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)
};
static const u8 my_periConnParamCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(CONN_PARAM_DP_H), U16_HI(CONN_PARAM_DP_H),
    U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM)
};

//// GATT attribute values
static const u8 my_serviceChangeCharVal[5] = {
    CHAR_PROP_INDICATE,
    U16_LO(GenericAttribute_ServiceChanged_DP_H), U16_HI(GenericAttribute_ServiceChanged_DP_H),
    U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE)
};

//// device Information  attribute values
static const u8 my_PnCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(DeviceInformation_pnpID_DP_H), U16_HI(DeviceInformation_pnpID_DP_H),
    U16_LO(CHARACTERISTIC_UUID_PNP_ID), U16_HI(CHARACTERISTIC_UUID_PNP_ID)
};

//// HID attribute values (removed)


//// Battery attribute values
static const u8 my_batCharVal[5] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(BATT_LEVEL_INPUT_DP_H), U16_HI(BATT_LEVEL_INPUT_DP_H),
    U16_LO(CHARACTERISTIC_UUID_BATTERY_LEVEL), U16_HI(CHARACTERISTIC_UUID_BATTERY_LEVEL)
};

//// Temp attribute values
static const u8 my_tempCharVal[5] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(TEMP_LEVEL_INPUT_DP_H), U16_HI(TEMP_LEVEL_INPUT_DP_H),
    U16_LO(0x2A1F), U16_HI(0x2A1F)
};

#if (BLE_OTA_SERVER_ENABLE)
//// OTA attribute values
static const u8 my_OtaCharVal[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY | CHAR_PROP_WRITE,
    U16_LO(OTA_CMD_OUT_DP_H), U16_HI(OTA_CMD_OUT_DP_H),
    TELINK_SPP_DATA_OTA,
};
#endif

//// RxTx attribute values
static const u8 my_RxTxCharVal[5] = {
    CHAR_PROP_NOTIFY | CHAR_PROP_WRITE,
    U16_LO(RxTx_CMD_OUT_DP_H), U16_HI(RxTx_CMD_OUT_DP_H),
    U16_LO(EPD_RXTX_UUID), U16_HI(EPD_RXTX_UUID)
};

//// Layout attribute values
static const u8 layout_status_char_val[19] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(DP_H_LAYOUT_STATUS_VAL), U16_HI(DP_H_LAYOUT_STATUS_VAL),
    LAYOUT_CHAR_UUID_BYTES(0x01)
};

static const u8 layout_xfer_ctl_char_val[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_WRITE,
    U16_LO(DP_H_LAYOUT_XFER_CTL_VAL), U16_HI(DP_H_LAYOUT_XFER_CTL_VAL),
    LAYOUT_CHAR_UUID_BYTES(0x02)
};

static const u8 layout_xfer_data_char_val[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP,
    U16_LO(DP_H_LAYOUT_XFER_DATA_VAL), U16_HI(DP_H_LAYOUT_XFER_DATA_VAL),
    LAYOUT_CHAR_UUID_BYTES(0x03)
};

static const u8 layout_xfer_ack_char_val[19] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(DP_H_LAYOUT_XFER_ACK_VAL), U16_HI(DP_H_LAYOUT_XFER_ACK_VAL),
    LAYOUT_CHAR_UUID_BYTES(0x04)
};

static const u8 layout_meta_char_val[19] = {
    CHAR_PROP_READ,
    U16_LO(DP_H_LAYOUT_META_VAL), U16_HI(DP_H_LAYOUT_META_VAL),
    LAYOUT_CHAR_UUID_BYTES(0x05)
};

static const u8 layout_ringlog_char_val[19] = {
    CHAR_PROP_READ,
    U16_LO(DP_H_LAYOUT_RINGLOG_VAL), U16_HI(DP_H_LAYOUT_RINGLOG_VAL),
    LAYOUT_CHAR_UUID_BYTES(0x06)
};

static int layout_ringlog_read(void *p)
{
    (void)p;
    if (ringlog_read(0, &layout_ringlog_buf) != 0) {
        memset(&layout_ringlog_buf, 0, sizeof(layout_ringlog_buf));
        layout_ringlog_buf.ts = RINGLOG_EMPTY_TS;
    }
    return 0;
}

static const attribute_t my_Attributes[] = {
    {ATT_END_H - 1, 0, 0, 0, 0, 0, 0, 0}, // total num of attribute

    // 0x0001 - 0x0007  GAP
    {7,ATT_PERMISSIONS_READ, 2, 2, (u8*)(&my_primaryServiceUUID), (u8*)(&my_gapServiceUUID), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_devNameCharVal), (u8*)(&my_characterUUID), (u8*)(my_devNameCharVal), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_devName), (u8*)(&my_devNameUUID), (u8*)(my_devName), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_appearanceCharVal), (u8*)(&my_characterUUID), (u8*)(my_appearanceCharVal), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof (my_appearance), (u8*)(&my_appearanceUUID), (u8*)(&my_appearance), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_periConnParamCharVal), (u8*)(&my_characterUUID), (u8*)(my_periConnParamCharVal), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof (my_periConnParameters), (u8*)(&my_periConnParamUUID), (u8*)(&my_periConnParameters), 0, 0},


    // 0x0008 - 0x000B GATT
    {4,ATT_PERMISSIONS_READ, 2, 2, (u8*)(&my_primaryServiceUUID), (u8*)(&my_gattServiceUUID), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_serviceChangeCharVal), (u8*)(&my_characterUUID), (u8*)(my_serviceChangeCharVal), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof (serviceChangeVal), (u8*)(&serviceChangeUUID), (u8*)(&serviceChangeVal), 0, 0},
    {0,ATT_PERMISSIONS_RDWR, 2, sizeof (serviceChangeCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(serviceChangeCCC), 0, 0},


    // 0x000C - 0x000E  Device Information Service
    {3,ATT_PERMISSIONS_READ, 2, 2, (u8*)(&my_primaryServiceUUID), (u8*)(&my_devServiceUUID), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_PnCharVal), (u8*)(&my_characterUUID), (u8*)(my_PnCharVal), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof (my_PnPtrs), (u8*)(&my_PnPUUID), (u8*)(my_PnPtrs), 0, 0},


    /////////////////////////////////// 4. HID Service (removed) /////////////////////////////////////////////////////////
    // // 0x000F


    ////////////////////////////////////// Battery Service /////////////////////////////////////////////////////
    // 0x002A - 0x002D
    {4,ATT_PERMISSIONS_READ, 2, 2, (u8*)(&my_primaryServiceUUID), (u8*)(&my_batServiceUUID), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_batCharVal), (u8*)(&my_characterUUID), (u8*)(my_batCharVal), 0, 0},               //prop
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_batVal), (u8*)(&my_batCharUUID), (u8*)(my_batVal), 0, 0},                         //value
    {0,ATT_PERMISSIONS_RDWR, 2, sizeof(batteryValueInCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(batteryValueInCCC), 0, 0}, //value

    ////////////////////////////////////// Temp Service /////////////////////////////////////////////////////
    // 002a - 002d
    {4,ATT_PERMISSIONS_READ, 2, 2, (u8*)(&my_primaryServiceUUID), (u8*)(&my_tempServiceUUID), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_tempCharVal), (u8*)(&my_characterUUID), (u8*)(my_tempCharVal), 0, 0},       //prop
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_tempVal), (u8*)(&my_tempCharUUID), (u8*)(my_tempVal), 0, 0},                //value
    {0,ATT_PERMISSIONS_RDWR, 2, sizeof(tempValueInCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(tempValueInCCC), 0, 0}, //value

#if (BLE_OTA_SERVER_ENABLE)
    ////////////////////////////////////// OTA /////////////////////////////////////////////////////
    // 0x002E - 0x0032
    {5,ATT_PERMISSIONS_READ, 2, 16, (u8*)(&my_primaryServiceUUID), (u8*)(&my_OtaServiceUUID), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_OtaCharVal), (u8*)(&my_characterUUID), (u8*)(my_OtaCharVal), 0, 0}, //prop
    {0,ATT_PERMISSIONS_RDWR, 16, sizeof(my_OtaData), (u8*)(&my_OtaUUID), (&my_OtaData), &otaWrite, NULL},     //value
    {0,ATT_PERMISSIONS_RDWR, 2, sizeof(otaDataCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(otaDataCCC), 0, 0}, //value
    {0,ATT_PERMISSIONS_READ, 2, sizeof (my_OtaName), (u8*)(&userdesc_UUID), (u8*)(my_OtaName), 0, 0},
#endif
    ////////////////////////////////////// RxTx ////////////////////////////////////////////////////
    // RxTx Communication
    {4,ATT_PERMISSIONS_READ, 2, 2, (u8*)(&my_primaryServiceUUID), (u8*)(&my_RxTx_ServiceUUID), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(my_RxTxCharVal), (u8*)(&my_characterUUID), (u8*)(my_RxTxCharVal), 0, 0},       //prop
    {0,ATT_PERMISSIONS_WRITE, 2, sizeof(my_RxTx_Data), (u8*)(&my_RxTxUUID), (&my_RxTx_Data), &RxTxWrite, 0},          //value
    {0,ATT_PERMISSIONS_RDWR, 2, sizeof(RxTxValueInCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(RxTxValueInCCC), 0, 0}, //value

    ////////////////////////////////////// Layout service ////////////////////////////////////////////////////
    // 0x1F20: STATUS, XFER_CTL, XFER_DATA, XFER_ACK, META, RINGLOG
    {15,ATT_PERMISSIONS_READ, 2, 16, (u8*)(&my_primaryServiceUUID), (u8*)(LAYOUT_SERVICE_UUID), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(layout_status_char_val), (u8*)(&my_characterUUID), (u8*)(layout_status_char_val), 0, 0},
    {0,ATT_PERMISSIONS_READ, 16, sizeof(layout_status_buf), (u8*)(LAYOUT_CHAR_STATUS_UUID), (u8*)(layout_status_buf), 0, 0},
    {0,ATT_PERMISSIONS_RDWR, 2, sizeof(layout_status_cccd), (u8*)(&clientCharacterCfgUUID), (u8*)(layout_status_cccd), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(layout_xfer_ctl_char_val), (u8*)(&my_characterUUID), (u8*)(layout_xfer_ctl_char_val), 0, 0},
    {0,ATT_PERMISSIONS_RDWR, 16, sizeof(layout_xfer_ctl_value), (u8*)(LAYOUT_CHAR_XFER_CTL_UUID), (u8*)(layout_xfer_ctl_value), &layout_xfer_ctl_write, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(layout_xfer_data_char_val), (u8*)(&my_characterUUID), (u8*)(layout_xfer_data_char_val), 0, 0},
    {0,ATT_PERMISSIONS_RDWR, 16, sizeof(layout_xfer_data_value), (u8*)(LAYOUT_CHAR_XFER_DATA_UUID), (u8*)(layout_xfer_data_value), &layout_xfer_data_write, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(layout_xfer_ack_char_val), (u8*)(&my_characterUUID), (u8*)(layout_xfer_ack_char_val), 0, 0},
    {0,ATT_PERMISSIONS_READ, 16, sizeof(layout_xfer_ack_value), (u8*)(LAYOUT_CHAR_XFER_ACK_UUID), (u8*)(layout_xfer_ack_value), 0, 0},
    {0,ATT_PERMISSIONS_RDWR, 2, sizeof(layout_xfer_ack_cccd), (u8*)(&clientCharacterCfgUUID), (u8*)(layout_xfer_ack_cccd), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(layout_meta_char_val), (u8*)(&my_characterUUID), (u8*)(layout_meta_char_val), 0, 0},
    {0,ATT_PERMISSIONS_READ, 16, sizeof(layout_meta_buf), (u8*)(LAYOUT_CHAR_META_UUID), (u8*)(layout_meta_buf), 0, 0},
    {0,ATT_PERMISSIONS_READ, 2, sizeof(layout_ringlog_char_val), (u8*)(&my_characterUUID), (u8*)(layout_ringlog_char_val), 0, 0},
    {0,ATT_PERMISSIONS_READ, 16, sizeof(layout_ringlog_buf), (u8*)(LAYOUT_CHAR_RINGLOG_UUID), (u8*)(&layout_ringlog_buf), 0, &layout_ringlog_read},
};


/**
 * @brief      Initialize the attribute table
 */
void my_att_init(void) {
    ringlog_init();
    layout_att_init_meta();
    layout_att_mark_no_page();
    layout_att_refresh_status(battery_read_mv(), 0, 0);
    layout_att_load_active_slot();
    log_debugf("set %d attributes", sizeof(my_Attributes) / sizeof(attribute_t) - 1);
    bls_att_setAttributeTable((u8*)my_Attributes);
}
