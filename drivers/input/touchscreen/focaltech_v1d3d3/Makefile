#
# Makefile for the input core drivers.
#

# Each configuration option enables a list of files.
obj-$(CONFIG_TOUCHSCREEN_FOCALTECH_V1D3D3) += fts_ts.o 
obj-$(CONFIG_TOUCHSCREEN_FOCALTECH_V1D3D3) += focaltech_test/ 
obj-$(CONFIG_TOUCHSCREEN_FOCALTECH_V1D3D3) += focaltech_global/ 
obj-$(CONFIG_TOUCHSCREEN_FOCALTECH_V1D3D3) += focaltech_flash/ 
fts_ts-y += focaltech_core.o focaltech_ctl.o focaltech_ex_fun.o focaltech_gesture.o 
extlibs :=$(PWD)/drivers/input/touchscreen/focaltech_v1d3d3/ft_gesture_32bit_lib_v1.0_20140820.a 
ldflagobj-y += fts_ts.o 
obj-y s-y +=--whole-archive $(extlibs) --no-whole-archive
