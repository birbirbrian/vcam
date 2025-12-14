# Name of your result module (final module name will be vcam.ko)
obj-m += vcam.o

# vcam,ko is link from vcam_core.o and vcam_dev.o
vcam-objs := vcam_core.o vcam_dev.o

# all
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

# clean
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean