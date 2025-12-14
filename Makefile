# Name of your result module
obj-m += vcam.o

# all
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

# clean
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean