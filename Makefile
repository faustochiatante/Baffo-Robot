# Makefile for compiling an ev3 project with the following folder structure:
#
#	this_folder/
# 		Makefile (this file)
#		ev3dev-c/
#		libraries/
#			libbluetooth.a
#			libev3dev-c.a
#		include/
# 			bt_client.h
#			messages.h
#			movement.h
#			sensors.h
#	 	source/
#			bt_client.c
#			messages.c
#			movement.c
#			sensors.c
#			main.c
#
# The main executable will be located in the same directory as you ran
# "Make" from. To add new .c files, simply add them to the OBJS variable.

CC 			= arm-linux-gnueabi-gcc
CFLAGS 		= -O2 -g -std=gnu99 -W -Wall -Wno-comment
INCLUDES 	= -I./ev3dev-c/source/ev3 -I./include/
LDFLAGS 	= -L./libraries -lrt -lm -lev3dev-c -lpthread -lbluetooth
BUILD_DIR 	= ./build
SOURCE_DIR 	= ./source
BIN		= ./main

OBJS = \
	$(BUILD_DIR)/lib.o \
	$(BUILD_DIR)/bt.o \
	$(BUILD_DIR)/main.o
#	$(BUILD_DIR)/movement.o \
#	$(BUILD_DIR)/sensors.o \
#	$(BUILD_DIR)/bt_client.o \
#	$(BUILD_DIR)/messages.o \


all: main copy

main: ${OBJS}
	$(CC) $(INCLUDES) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(BIN)

$(OBJS): $(BUILD_DIR)

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c
	$(CC) -c $(SOURCE_DIR)/$*.c $(INCLUDES) -o $(BUILD_DIR)/$*.o

copy:
	sshpass -p 'maker' scp $(BIN) robot@ev3dev.local:/home/robot/

clean:
	rm -f $(BUILD_DIR)/*.o
	rm ./main
	sshpass -p 'maker' ssh robot@ev3dev.local 'rm main'
