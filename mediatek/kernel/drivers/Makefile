include $(MTK_PATH_BUILD)/common.mk
#$(call all-subdir-src-or-makefile)
obj-y   +=  keypad/ #for Kernel HAL Structure
obj-y   +=  mtprof/
obj-y   +=  clock/
obj-y   +=  sched/
obj-y   +=  mmp/
obj-y   +=  xlog/
obj-y   +=  uart/
obj-y   +=  eint/
obj-y   +=  boot/
obj-$(CONFIG_ZRAM) +=  mlog/
obj-y   +=  gpu/
obj-y	+=	gpio/
obj-y   +=  aee/
obj-y   +=  wdk/
obj-y   +=  ion_drv/
#obj-y   +=  m4u/
#obj-y   +=  video/
obj-y   +=  sync/
obj-y   +=  pmic_wrap/
ifneq ($(MTK_BATTERY_NO_HAL),yes)
obj-y   += power/
endif
obj-y   +=  usb20/
obj-y   +=  usb11/
obj-y   +=  videocodec/
obj-y   +=  dum-char/
ifneq ($(CONFIG_MTK_FPGA),y)
ifneq ($(MTK_RTC_NO_HAL),yes)
obj-y   +=  rtc/
endif
obj-y   +=  ccci/
obj-y   +=  dual_ccci/
obj-y   +=  eccci/


obj-y   +=  fmradio/
obj-y   +=  gps/
obj-y   +=  hwmon/
obj-y   +=  mhl/
obj-y   +=  almk/


obj-y   +=  bthid/

ifeq ($(MTK_WFD_SUPPORT),yes)
obj-y  += uibc/
endif

obj-y   +=  ccmni/

obj-y   +=  emd_ctl/


# gator-driver is not custom-release and will be removed
obj-$(CONFIG_GATOR_DRIVER)	+= $(call all-subdirs-with-makefile,gator-driver)


obj-y   +=  leds/
obj-y   +=  auxadc/
obj-y   +=  mmp/
obj-y		+=  ram_console/
obj-y   +=  thermal/
obj-y   +=  wmt_ccci/


ifneq ($(CONFIG_MTK_COMBO),)
	subdir-ccflags-y += -D MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
	subdir-ccflags-y += -D MTK_WCN_REMOVE_KERNEL_MODULE

	BUILD_COMBO := no
	BUILD_SOC := no

	ifneq ($(filter MT6620E3,$(MTK_COMBO_CHIP)),)
		BUILD_COMBO := yes
	endif

	ifneq ($(filter MT6628,$(MTK_COMBO_CHIP)),)
		BUILD_COMBO := yes
	endif

	ifneq ($(filter MT6630,$(MTK_COMBO_CHIP)),)
		BUILD_COMBO := yes
	endif

	ifeq ($(BUILD_COMBO), yes)
		obj-y   +=  combo/
		subdir-ccflags-y += -D MTK_WCN_COMBO_CHIP_SUPPORT
	endif

	ifneq ($(filter CONSYS_6572,$(MTK_COMBO_CHIP)),)
		BUILD_SOC := yes
	endif

	ifneq ($(filter CONSYS_6582,$(MTK_COMBO_CHIP)),)
		BUILD_SOC := yes
	endif

	ifneq ($(filter CONSYS_6592,$(MTK_COMBO_CHIP)),)
		BUILD_SOC := yes
	endif

	ifneq ($(filter CONSYS_6571,$(MTK_COMBO_CHIP)),)
		BUILD_SOC := yes
	endif

	ifneq ($(filter CONSYS_8127,$(MTK_COMBO_CHIP)),)
		BUILD_SOC := yes
	endif

	ifeq ($(BUILD_SOC), yes)
		obj-y   +=  conn_soc/
	endif

	obj-y   +=  common_detect/

endif

#ifeq ($(MTK_NFC_SUPPORT),yes)
#obj-y	+= nfc/
#endif

#ifeq ($(CONFIG_MTK_WIFI),m)
#obj-y   +=  conn_soc/
#endif

obj-$(CONFIG_MTK_EXTMEM) +=  exm/

obj-y   +=  accdet/
obj-y   +=  vibrator/
obj-y	+=	pwm/
obj-y   +=  freqhopping/
obj-$(CONFIG_MTK_ETM) += etm/
obj-y   +=      dbg_dump/
endif#!CONFIG_MTK_FPGA
obj-y   += selinux_warning/
