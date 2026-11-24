export PS4_PAYLOAD_SDK ?= ps4-payload-sdk
include $(PS4_PAYLOAD_SDK)/toolchain/orbis.mk

ELF := webtor-ps4.elf
SRC := main.c

CFLAGS := -O2 -Wall
LDLIBS := -lssl -lcrypto -lz

all: $(ELF)

$(ELF): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(ELF)
