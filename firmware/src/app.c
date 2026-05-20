/********************************************************************************************************
 * @file    app.c
 *
 * @brief   This is the source file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2022
 *
 * @par     Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#include "drivers.h"
#include "tl_common.h"
#include "app_config.h"
#include "stack/ble/ble.h"

#include "app.h"

#include "ble.h"
#include "led.h"

#include "battery.h"

#include "epd/epd.h"
#include "log.h"
#include "runtime_quotes.h"

/**
 * @brief		user initialization when MCU power on or wake_up from deepSleep mode
 */
_attribute_no_inline_ void user_init_normal(void) {
    //////////////////////////// basic hardware Initialization  Begin //////////////////////////////////

    /* random number generator must be initiated before any BLE stack initialization.
     * When deepSleep retention wakeUp, no need initialize again */
    random_generator_init();

    init_time();

    //	debug init
#if(UART_PRINT_DEBUG_ENABLE)
    tlkapi_debug_init();
    blc_debug_enableStackLog(STK_LOG_DISABLE);
#endif

    blc_readFlashSize_autoConfigCustomFlashSector();

    /* attention that this function must be called after "blc_readFlashSize_autoConfigCustomFlashSector" !!!*/
    blc_app_loadCustomizedParameters_normal();
    runtime_quotes_init();


#if (APP_FLASH_PROTECTION_ENABLE)
	app_flash_protection_operation(FLASH_OP_EVT_APP_INITIALIZATION, 0, 0);
	blc_appRegisterStackFlashOperationCallback(app_flash_protection_operation); //register flash operation callback for stack
#endif
    //////////////////////////// basic hardware Initialization  End //////////////////////////////////

#if !(DISABLE_BLE)
    init_ble();
#endif
}


/**
 * @brief		user initialization when MCU wake_up from deepSleep_retention mode
 */
_attribute_ram_code_ void user_init_deepRetn(void) {
#if (PM_DEEPSLEEP_RETENTION_ENABLE)

    blc_app_loadCustomizedParameters_deepRetn();

    blc_ll_initBasicMCU(); //mandatory
    rf_set_power_level_index(MY_RF_POWER_INDEX);

    blc_ll_recoverDeepRetention();

    /* ADC settings will lost during deepsleep retention mode, so here need clear flag */
    _battery_clear_adc_setting_flag();

    DBG_CHN0_HIGH; //debug

    irq_enable();

#endif
}


#if (APP_FLASH_PROTECTION_ENABLE)

/**
 * @brief      flash protection operation, including all locking & unlocking for application
 * 			   handle all flash write & erase action for this demo code. use should add more more if they have more flash operation.
 * @param[in]  flash_op_evt - flash operation event, including application layer action and stack layer action event(OTA write & erase)
 * 			   attention 1: if you have more flash write or erase action, you should should add more type and process them
 * 			   attention 2: for "end" event, no need to pay attention on op_addr_begin & op_addr_end, we set them to 0 for
 * 			   			    stack event, such as stack OTA write new firmware end event
 * @param[in]  op_addr_begin - operating flash address range begin value
 * @param[in]  op_addr_end - operating flash address range end value
 * 			   attention that, we use: [op_addr_begin, op_addr_end)
 * 			   e.g. if we write flash sector from 0x10000 to 0x20000, actual operating flash address is 0x10000 ~ 0x1FFFF
 * 			   		but we use [0x10000, 0x20000):  op_addr_begin = 0x10000, op_addr_end = 0x20000
 */
_attribute_data_retention_ u16  flash_lockBlock_cmd = 0;
void app_flash_protection_operation(u8 flash_op_evt, u32 op_addr_begin, u32 op_addr_end)
{
	if(flash_op_evt == FLASH_OP_EVT_APP_INITIALIZATION)
	{
		/* ignore "op addr_begin" and "op addr_end" for initialization event
		 * must call "flash protection_init" first, will choose correct flash protection relative API according to current internal flash type in MCU */
		flash_protection_init();

		/* just sample code here, protect all flash area for old firmware and OTA new firmware.
		 * user can change this design if have other consideration */
		u32  app_lockBlock = 0;
#if (BLE_OTA_SERVER_ENABLE)
			u32 multiBootAddress = blc_ota_getCurrentUsedMultipleBootAddress();
			if(multiBootAddress == MULTI_BOOT_ADDR_0x20000){
				app_lockBlock = FLASH_LOCK_FW_LOW_256K;
			}
			else if(multiBootAddress == MULTI_BOOT_ADDR_0x40000){
				/* attention that 512K capacity flash can not lock all 512K area, should leave some upper sector
				 * for system data(SMP storage data & calibration data & MAC address) and user data
				 * will use a approximate value */
				app_lockBlock = FLASH_LOCK_FW_LOW_512K;
			}
#else
			app_lockBlock = FLASH_LOCK_FW_LOW_256K; //just demo value, user can change this value according to application
#endif


		flash_lockBlock_cmd = flash_change_app_lock_block_to_flash_lock_block(app_lockBlock);

		if(blc_flashProt.init_err){
			tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] flash protection initialization error!!!\n");
		}

		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] initialization, lock flash\n");
		flash_lock(flash_lockBlock_cmd);
	}
#if (BLE_OTA_SERVER_ENABLE)
	else if(flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_BEGIN)
	{
		/* OTA clear old firmware begin event is triggered by stack, in "blc ota_initOtaServer_module", rebooting from a successful OTA.
		 * Software will erase whole old firmware for potential next new OTA, need unlock flash if any part of flash address from
		 * "op addr_begin" to "op addr_end" is in locking block area.
		 * In this sample code, we protect whole flash area for old and new firmware, so here we do not need judge "op addr_begin" and "op addr_end",
		 * must unlock flash */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW begin, unlock flash\n");
		flash_unlock();
	}
	else if(flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_END)
	{
		/* ignore "op addr_begin" and "op addr_end" for END event
		 * OTA clear old firmware end event is triggered by stack, in "blc ota_initOtaServer_module", erasing old firmware data finished.
		 * In this sample code, we need lock flash again, because we have unlocked it at the begin event of clear old firmware */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
	else if(flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN)
	{
		/* OTA write new firmware begin event is triggered by stack, when receive first OTA data PDU.
		 * Software will write data to flash on new firmware area,  need unlock flash if any part of flash address from
		 * "op addr_begin" to "op addr_end" is in locking block area.
		 * In this sample code, we protect whole flash area for old and new firmware, so here we do not need judge "op addr_begin" and "op addr_end",
		 * must unlock flash */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW begin, unlock flash\n");
		flash_unlock();
	}
	else if(flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END)
	{
		/* ignore "op addr_begin" and "op addr_end" for END event
		 * OTA write new firmware end event is triggered by stack, after OTA end or an OTA error happens, writing new firmware data finished.
		 * In this sample code, we need lock flash again, because we have unlocked it at the begin event of write new firmware */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
#endif
	/* add more flash protection operation for your application if needed */
}


#endif


/////////////////////////////////////////////////////////////////////s
// main loop flow
/////////////////////////////////////////////////////////////////////
_attribute_no_inline_ void main_loop(void) {
    ////////////////////////////////////// BLE entry /////////////////////////////////
#if !(DISABLE_BLE)
    blt_sdk_main_loop();
#endif

    handle_time();

    battery_try_update();

	const bool epd_trying_update = epd_try_update();
    const bool epd_is_updating = epd_check_is_updating();

	// led status
	if (check_time_period_reached(TIMER_C_LED)) {
		if (ble_is_connected()) {
			led_blink_color(LED_COLOR_BLUE);
		} else {
			led_blink_color(LED_COLOR_GREEN);
		}
	}

    ////////////////////////////////////// PM Process /////////////////////////////////
    if (epd_trying_update || epd_is_updating) {
        // log_verbosef("epd is updating");
        // seems not need and the level will depends on epd model
        // if epd_update is ongoing enable gpio wakeup to put the display to sleep as fast as possible
        cpu_set_gpio_wakeup(EPD_BUSY, Level_High, 1);
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        // log_verbosef("[%d] set suspend mask: SUSPEND_DISABLE", blc_ll_getCurrentState());
    	return;
    }

	if (AIRTAG_ENABLE && check_time_period_reached(TIMER_C_AIR_TAG)) {
		set_airtag_adv_data();
	}


	// power management code for application
#if (DISABLE_BLE)
	// only sleep after epd update once, current_battery_mv > 0 means epd initialized
	if (current_battery_mv > 0) {
		// just put to sleep if no need ble
		cpu_sleep_wakeup(DEEPSLEEP_MODE_RET_SRAM_LOW32K, PM_WAKEUP_TIMER, TICK_SECONDS_LATER(BLE_SLEEP_SECONDS)); //deepsleep
	}
#else
	blt_pm_proc();
#endif
}
