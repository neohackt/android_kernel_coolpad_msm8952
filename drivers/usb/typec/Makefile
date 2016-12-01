# Required flag to configure the core to operate with the Linux kernel
	ccflags-$(CONFIG_FUSB_30X) += -DFSC_PLATFORM_LINUX
	# Optional flag to enable interrupt-driven operation
	ccflags-$(CONFIG_FUSB_30X) += -DFSC_INTERRUPT_TRIGGERED
	# Optional flag to enable debug/hostcomm interface and functionality
        ccflags-$(CONFIG_FUSB_30X) += -DFSC_DEBUG
        ccflags-$(CONFIG_FUSB_30X) += -DFSC_HAVE_SRC
	ccflags-$(CONFIG_FUSB_30X) += -DFSC_HAVE_SNK
	ccflags-$(CONFIG_FUSB_30X) += -DFSC_HAVE_DRP
	ccflags-$(CONFIG_FUSB_30X) += -DFSC_HAVE_ACCMODE
	ccflags-$(CONFIG_FUSB_30X) += -DFSC_HAVE_VDM
	ccflags-$(CONFIG_FUSB_30X) += -DFSC_HAVE_DP

	obj-$(CONFIG_FUSB_30X)     += fusb30x_whole.o
	fusb30x_whole-objs         := Platform_Linux/fusb30x_driver.o \
			              Platform_Linux/fusb30x_global.o \
			              Platform_Linux/platform.o \
			              Platform_Linux/platform_helpers.o \
			              Platform_Linux/hostcomm.o \
			              core/AlternateModes.o \
			              core/core.o \
			              core/fusb30X.o \
			              core/Log.o \
			              core/PDPolicy.o \
			              core/PDProtocol.o \
			              core/TypeC.o \
			              core/vdm/bitfield_translators.o \
			              core/vdm/vdm.o \
			              core/vdm/vdm_callbacks.o \
			              core/vdm/vdm_config.o \
			              core/vdm/DisplayPort/configure.o \
			              core/vdm/DisplayPort/dp.o \
			              core/vdm/DisplayPort/dp_system_stubs.o
