ARCH=arm
CROSS_COMPILE := arm-angstrom-linux-gnueabi-
KDIR := ./linux-4.9.y

obj-m := dht22.o
all:
	          $(MAKE) -Werror -C ${KDIR} M=${PWD} modules
clean:
	          $(MAKE) -C $(KDIR) M=${PWD} clean
