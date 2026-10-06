# PS5Instrumentalizer - build file
#
# Requires the ps5-payload-sdk (https://github.com/ps5-payload-dev/sdk).
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
# If PS5_PAYLOAD_SDK is not set, the SDK extracted in ../.deps is used.
#
# make        - build the stub blob and PS5Instrumentalizer.elf
# make test   - build and run the host unit tests
# make clean  - remove build output

PS5_PAYLOAD_SDK ?= $(abspath ../.deps/sdk/ps5-payload-sdk)

BUILD := build
ELF   := PS5Instrumentalizer.elf

# ---- host tools used for the in-game stub and for the tests ----
HOST_CC   ?= clang
STUB_OBJCOPY ?= llvm-objcopy
LLD       ?= ld.lld

COMMON_INC := -Iinclude -Istub

# ---- in-game stub (freestanding, position independent, PS4 ABI == SysV x86-64) ----
STUB_CFLAGS := --target=x86_64-unknown-none-elf -ffreestanding -fno-builtin -fno-stack-protector \
               -fno-asynchronous-unwind-tables -fno-unwind-tables -fPIE -mno-red-zone -fno-plt \
               -mno-sse -mno-sse2 -mno-avx -O2 -Wall -Wextra -Werror $(COMMON_INC)
STUB_SRCS   := stub/usb_hooks.c stub/translate.c
STUB_OBJS   := $(patsubst stub/%.c,$(BUILD)/stub/%.o,$(STUB_SRCS))

# ---- PS5 payload ----
ifeq ($(wildcard $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk),)
$(error PS5_PAYLOAD_SDK is not set or invalid: $(PS5_PAYLOAD_SDK))
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

PAYLOAD_SRCS := src/main.c src/inject.c src/versions.c src/config.c src/log.c src/crash.c
PAYLOAD_OBJS := $(patsubst src/%.c,$(BUILD)/src/%.o,$(PAYLOAD_SRCS))
PAYLOAD_CFLAGS := -Wall -Wextra -Werror -O2 -g $(COMMON_INC) \
                  -DSTUB_BIN_PATH='"$(abspath $(BUILD))/stub.bin"'

.PHONY: all stub test check-stub clean
.DEFAULT_GOAL := all

all: $(ELF)

# ---- stub ----
stub: $(BUILD)/stub.bin

$(BUILD)/stub/%.o: stub/%.c $(wildcard include/*.h stub/*.h)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(STUB_CFLAGS) -c -o $@ $<

$(BUILD)/stub.elf: $(STUB_OBJS) stub/stub.ld
	$(LLD) -static -no-pie -nostdlib --no-dynamic-linker -z norelro --entry=0 -T stub/stub.ld -o $@ $(STUB_OBJS)

$(BUILD)/stub.bin: $(BUILD)/stub.elf
	$(STUB_OBJCOPY) -O binary $< $@

# ---- payload ----
$(BUILD)/src/%.o: src/%.c $(BUILD)/stub.bin $(wildcard include/*.h src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(PAYLOAD_CFLAGS) -c -o $@ $<

$(ELF): $(PAYLOAD_OBJS)
	$(CC) -o $@ $^ -lkernel_sys -lpthread

# ---- tests ----
test: $(BUILD)/test_translate $(BUILD)/test_stub $(BUILD)/test_support
	$(BUILD)/test_translate
	$(BUILD)/test_stub
	$(BUILD)/test_support

$(BUILD)/test_translate: tests/test_translate.c stub/translate.c $(wildcard include/*.h stub/*.h)
	@mkdir -p $(BUILD)
	$(HOST_CC) -Wall -Wextra -Werror $(COMMON_INC) -o $@ tests/test_translate.c stub/translate.c

$(BUILD)/test_stub: tests/test_stub.c $(BUILD)/stub.bin $(wildcard include/*.h stub/*.h)
	@mkdir -p $(BUILD)
	$(HOST_CC) -Wall -Wextra -Werror $(COMMON_INC) -DSTUB_BIN_PATH='"$(abspath $(BUILD))/stub.bin"' \
		-o $@ tests/test_stub.c

$(BUILD)/test_support: tests/test_support.c src/config.c src/versions.c $(wildcard include/*.h src/*.h)
	@mkdir -p $(BUILD)
	$(HOST_CC) -Wall -Wextra -Werror $(COMMON_INC) -Isrc -o $@ tests/test_support.c src/config.c src/versions.c

clean:
	rm -rf $(BUILD) $(ELF)
