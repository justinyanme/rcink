#include "battery.h"

#include "app_config.h"
#include "log.h"
#include "tl_common.h"
#include "drivers.h"

_attribute_data_retention_ static u32 battery_check_tick = 0;
_attribute_data_retention_ u16 current_battery_mv = 0;
static u8 adc_hw_initialized = 0;

static int app_battery_power_check(void);

void _battery_clear_adc_setting_flag(void) {
    adc_hw_initialized = 0;
}

u16 battery_read_mv(void) {
    const int mv = app_battery_power_check();
    if (mv <= 0) {
        log_debugf("invalid battery reading: %d", mv);
        current_battery_mv = 0;
        return 0;
    }

    current_battery_mv = (u16)mv;
    battery_check_tick = clock_time();
    return current_battery_mv;
}

bool battery_try_update() {
    if (!clock_time_exceed(battery_check_tick, 5000000)) {
        return false;
    }

    battery_check_tick = clock_time();

    const int mv = app_battery_power_check();

    // old mv > 0 && new mv < 2300, alert low battery
    if (mv < 2300 && current_battery_mv > 0) {
        log_debugf("Low battery, %dmv -> %dmv", current_battery_mv, mv);
    }

    if (abs(current_battery_mv - mv) >= 50) {
        log_debugf("Battery %dmv -> %dmv", current_battery_mv, mv);
        current_battery_mv = mv;
        return true;
    }

    return false;
}

_attribute_ram_code_ u8 mv_to_battery_level(const u16 mv) {
    if (mv < 2200) return 0;
    const u16 level = (mv - 2200) / (31 - 22);
    return level > 100 ? 100 : (u8)level;
}

/////////////////////////////////
/// below code mostly copy from battery_check.c
#define ADC_SAMPLE_NUM		8

_attribute_data_retention_ static u16 batt_vol_mv;
extern unsigned short adc_gpio_calib_vref;
extern signed char adc_gpio_calib_vref_offset;
extern unsigned char adc_pre_scale;

/**
 * @brief		vbat detect initialization
 */
_attribute_ram_code_ static void adc_vbat_detect_init(void) {
    // 825x only
    // DFIFO Mode

    /******power off sar adc********/
    adc_power_on_sar_adc(0);

    //telink advice: you must choose one gpio with adc function to output high level(voltage will equal to vbat), then use adc to measure high level voltage
    gpio_set_output_en(GPIO_VBAT_DETECT, 1);
    gpio_write(GPIO_VBAT_DETECT, 1);


    /******set adc sample clk as 4MHz******/
    adc_set_sample_clk(5); //adc sample clk= 24M/(1+5)=4M

    /******set adc L R channel Gain Stage bias current trimming******/
    adc_set_left_right_gain_bias(GAIN_STAGE_BIAS_PER100, GAIN_STAGE_BIAS_PER100);

    //set misc channel en,  and adc state machine state cnt 2( "set" stage and "capture" state for misc channel)
    adc_set_chn_enable_and_max_state_cnt(ADC_MISC_CHN, 2); //set total length for sampling state machine and channel

    //set "capture state" length for misc channel: 240
    //set "set state" length for misc channel: 10
    //adc state machine  period  = 24M/250 = 96K, T = 10.4 uS
    adc_set_state_length(240, 0, 10); //set R_max_mc,R_max_c,R_max_s


    //set misc channel use differential_mode,
    //set misc channel resolution 14 bit,  misc channel differential mode
    //notice that: in differential_mode MSB is sign bit, rest are data,  here BIT(13) is sign bit
    analog_write(anareg_adc_res_m, RES14 | FLD_ADC_EN_DIFF_CHN_M);
    adc_set_ain_chn_misc(ADC_INPUT_PCHN, GND);

    //set misc channel vref 1.2V
    adc_set_ref_voltage(ADC_MISC_CHN, ADC_VREF_1P2V);

    //set misc t_sample 6 cycle of adc clock:  6 * 1/4M
    adc_set_tsample_cycle_chn_misc(SAMPLING_CYCLES_6);

    //set Analog input pre-scaling 1/8
    adc_set_ain_pre_scaler(ADC_PRESCALER_1F8);


    /******power on sar adc********/
    //note: this setting must be set after all other settings
    adc_power_on_sar_adc(1);
}


_attribute_data_retention_ static volatile unsigned int adc_dat_buf[ADC_SAMPLE_NUM]; //size must 16 byte aligned(16/32/64...)

_attribute_data_retention_ static u32 adc_result;

_attribute_ram_code_ static int app_battery_power_check(void) {
    int i, j;

    //when MCU powered up or wakeup from deep/deep with retention, adc need be initialized
    if (!adc_hw_initialized) {
        adc_hw_initialized = 1;
        adc_vbat_detect_init();
    }

    adc_reset_adc_module();

    const u32 t0 = clock_time();
    u16 adc_sample[ADC_SAMPLE_NUM] = {0};


    for (i = 0; i < ADC_SAMPLE_NUM; i++) {
        // dfifo data clear
        adc_dat_buf[i] = 0;
    }
    // wait at least 2 sample cycle(f = 96K, T = 10.4us)
    while (!clock_time_exceed(t0, 25)) {}

    // dfifo setting will lose in suspend/deep, so we need to configure it every time
    adc_config_misc_channel_buf((u16*)adc_dat_buf, ADC_SAMPLE_NUM << 2); //size: ADC_SAMPLE_NUM*4
    dfifo_enable_dfifo2();


    //////////////// get adc sample data and sort these data ////////////////
    for (i = 0; i < ADC_SAMPLE_NUM; i++) {
        while (!adc_dat_buf[i]) {}


        if (adc_dat_buf[i] & BIT(13)) {
            //14 bit resolution, BIT(13) is sign bit, 1 means negative voltage in differential_mode
            adc_sample[i] = 0;
        } else {
            adc_sample[i] = ((u16)adc_dat_buf[i] & 0x1FFF); //BIT(12..0) is valid adc result
        }


        //insert sort
        if (i) {
            if (adc_sample[i] < adc_sample[i - 1]) {
                const u16 temp = adc_sample[i];
                adc_sample[i] = adc_sample[i - 1];
                for (j = i - 1; j >= 0 && adc_sample[j] > temp; j--) {
                    adc_sample[j + 1] = adc_sample[j];
                }
                adc_sample[j + 1] = temp;
            }
        }
    }
    //////////////////////////////////////////////////////////////////////////////


    dfifo_disable_dfifo2(); //misc channel data dfifo disable


    // average the middle 4 of 8 samples
    adc_result = (adc_sample[2] + adc_sample[3] + adc_sample[4] + adc_sample[5]) / 4;

    //////////////// adc sample data convert to voltage(mv) ////////////////
    //                          (Vref, adc_pre_scale)   (BIT<12~0> valid data)
    //			 =  adc_result * Vref * adc_pre_scale / 0x2000 + offset
    //           =  adc_result * Vref*adc_pre_scale >>13 + offset
    if (ADC_INPUT_PCHN == VBAT) {
        extern unsigned short adc_vbat_calib_vref;
        batt_vol_mv = ((adc_result * adc_pre_scale * adc_vbat_calib_vref) >> 13);
    } else {
        batt_vol_mv = ((adc_result * adc_pre_scale * adc_gpio_calib_vref) >> 13) + adc_gpio_calib_vref_offset;
    }

    return batt_vol_mv;
}
