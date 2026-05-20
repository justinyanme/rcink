/********************************************************************************************************
 * @file    app_config.h
 *
 * @brief   This is the header file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#pragma once

#define DEBUG 0

///////////////////////// Feature Configuration////////////////////////////////////////////////
#define BLE_APP_PM_ENABLE								1
#define PM_DEEPSLEEP_RETENTION_ENABLE					1
#define TEST_CONN_CURRENT_ENABLE            			0 	//test connection current, disable UI to have a pure power
#define BLE_APP_SECURITY_ENABLE      					1	//ACL Slave device SMP, strongly recommended enabled
#define BLE_OTA_SERVER_ENABLE							0

/* Flash Protection:
 * 1. Flash protection is enabled by default in SDK. User must enable this function on their final mass production application.
 * 2. User should use "Unlock" command in Telink BDT tool for Flash access during development and debugging phase.
 * 3. Flash protection demonstration in SDK is a reference design based on sample code. Considering that user's final application may
 *    different from sample code, for example, user's final firmware size is bigger, or user have a different OTA design, or user need
 *    store more data in some other area of Flash, all these differences imply that Flash protection reference design in SDK can not
 *    be directly used on user's mass production application without any change. User should refer to sample code, understand the
 *    principles and methods, then change and implement a more appropriate mechanism according to their application if needed.
 */
#define APP_FLASH_PROTECTION_ENABLE						0

///////////////////////// DEBUG  Configuration ////////////////////////////////////////////////
#define DEBUG_GPIO_ENABLE					0
#define UART_PRINT_DEBUG_ENABLE				1
#define APP_LOG_EN							1
#define APP_SMP_LOG_EN						1
#define APP_KEY_LOG_EN						1
#define APP_CONTR_EVENT_LOG_EN				1  //controller event log
#define APP_HOST_EVENT_LOG_EN				1  //host event log
#define APP_OTA_LOG_EN						1
#define APP_FLASH_INIT_LOG_EN				1
#define APP_FLASH_PROT_LOG_EN				1
#define APP_BATT_CHECK_LOG_EN				1


///////////////////////// UI Configuration ////////////////////////////////////////////////////
#define	UI_KEYBOARD_ENABLE								0
#define	UI_LED_ENABLE									1
#define	UI_BUTTON_ENABLE								0


/////////////////// DEEP SAVE FLG //////////////////////////////////
#define USED_DEEP_ANA_REG                   DEEP_ANA_REG0 //u8,can save 8 bit info when deep
#define	LOW_BATT_FLG					    BIT(0) //if 1: low battery
#define CONN_DEEP_FLG	                    BIT(1) //if 1: conn deep, 0: ADV deep


///////////////////////// System Clock  Configuration /////////////////////////////////////////
#define CLOCK_SYS_CLOCK_HZ  								16000000


/////////////////// watchdog  //////////////////////////////
#define MODULE_WATCHDOG_ENABLE		1
#define WATCHDOG_INIT_TIMEOUT		5000  // 500ms


/////////////////////////////////////// PRINT DEBUG INFO ///////////////////////////////////////
#if (UART_PRINT_DEBUG_ENABLE)
#define DEBUG_INFO_TX_PIN           	GPIO_PB1
#define PULL_WAKEUP_SRC_PB1         	PM_PIN_PULLUP_10K
#define PB1_OUTPUT_ENABLE         		1
#define PB1_DATA_OUT                    1
#endif

#if TEST_CONN_CURRENT_ENABLE
#if DEBUG_GPIO_ENABLE || UART_PRINT_DEBUG_ENABLE || UI_KEYBOARD_ENABLE || UI_LED_ENABLE || UI_BUTTON_ENABLE
		#error "If testing current, the above definitions must be disable!!!"
#endif
#endif

#define     TICK_SECONDS_LATER(x)               (clock_time() + (x * sys_tick_per_us * 1000000))

#include "epd/board_blozi.h"
#include "vendor/common/default_config.h"
#define LOG_DEBUG_ENABLE 1
#define AIRTAG_ENABLE 0

#define RAM _attribute_data_retention_ // short version, this is needed to keep the values in ram after sleep

#define FORCE_SLEEP 1
#define MY_RF_POWER_INDEX RF_POWER_P3dBm
#define MTU_SIZE_SETTING 250 // default 23

#define BLE_SYNC_WINDOW_SECONDS 300
#define BLE_CONN_IDLE_SECONDS 120
#define DISABLE_BLE 0

// Periodic BLE peek-advertise after the initial sync window closes.
// When BLE_PM_PEEK_ENABLE is 1, after the 5-min boot window expires the
// device wakes every BLE_PEEK_INTERVAL_SECONDS, advertises for
// BLE_PEEK_ADV_SECONDS, then goes back to deep retention. Lets the user
// reconnect without battery cycle at the cost of ~K/N duty cycle BLE
// current. When 0 (default), the device stays radio-silent after the
// initial window (max battery life, must battery-cycle to reconnect).
#ifndef BLE_PM_PEEK_ENABLE
#define BLE_PM_PEEK_ENABLE 0
#endif
#ifndef BLE_PEEK_INTERVAL_SECONDS
#define BLE_PEEK_INTERVAL_SECONDS 60
#endif
#ifndef BLE_PEEK_ADV_SECONDS
#define BLE_PEEK_ADV_SECONDS 5
#endif
#if BLE_PM_PEEK_ENABLE && (BLE_PEEK_ADV_SECONDS >= BLE_PEEK_INTERVAL_SECONDS)
#error "BLE_PEEK_ADV_SECONDS must be smaller than BLE_PEEK_INTERVAL_SECONDS"
#endif

// select epd type
#define EPD_213
