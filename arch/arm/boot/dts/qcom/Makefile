ifeq ($(CONFIG_OF),y)

$(info arm64 TARGET_PRODUCT $(TARGET_PRODUCT))
ifeq ($(TARGET_PRODUCT),CPA8_931)
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-CPA8_931-P0.dtb
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-CPA8_931-P1.dtb
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-CPA8_931-P2.dtb
$(info build A8_931)
endif
ifeq ($(TARGET_PRODUCT),CP3600I)
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-CP3600I-P0.dtb
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-CP3600I-P1.dtb
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-CP3600I-T1.dtb
$(info build 3600I)
endif
ifeq ($(TARGET_PRODUCT),CPA9_9)
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-CPA9_9-P0.dtb
$(info build CPA9_9)
endif
ifeq ($(TARGET_PRODUCT),CPA9S_9)
dtb-$(CONFIG_ARCH_MSM8916) += msm8976-CPA9S_9-P0.dtb
$(info build CPA9S_9)
endif
ifeq ($(TARGET_PRODUCT),FA8976)
dtb-$(CONFIG_ARCH_MSM8916) += msm8976-FA8976-T0.dtb
$(info build FA8976)
endif
ifeq ($(TARGET_PRODUCT),CP8681_A02)
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-CP8681_A02.dtb
$(info build 8681)
endif
ifeq ($(TARGET_PRODUCT),FA8952)
dtb-$(CONFIG_ARCH_MSM8916) += msm8952-fa8952.dtb
$(info build FA8952)
endif
DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif

targets += dtbs
targets += $(addprefix ../, $(DTB_LIST))
endif

$(obj)/../%.dtb: $(src)/%.dts FORCE
	$(call if_changed_dep,dtc)

dtbs: $(addprefix $(obj)/../,$(DTB_LIST))

clean-files := *.dtb
