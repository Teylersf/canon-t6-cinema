# Included immediately BEFORE upstream include ../Makefile. No ROM constants.
BUILD_DIR ?= build
SRC_DIR ?= $(TOP_DIR)/src
SOUP_ENGINE := $(TOP_DIR)/soup_engine
ML_SRC_EXTRA_OBJS += $(BUILD_DIR)/film_menu.o $(BUILD_DIR)/film_develop.o \
    $(addprefix $(BUILD_DIR)/soup_,se.o cr2.o stock.o look.o jpeg_dec.o jpeg_enc.o \
    wb.o color.o grade.o lut.o anamorphic.o)

$(BUILD_DIR)/soup_%.o: $(SOUP_ENGINE)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CDEPS_FLAGS) -I$(SOUP_ENGINE) -c -o $@ $<

$(BUILD_DIR)/film_menu.o: $(SRC_DIR)/film_menu.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CDEPS_FLAGS) -I$(SOUP_ENGINE) -c -o $@ $<

$(BUILD_DIR)/film_develop.o: $(SRC_DIR)/film_develop.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CDEPS_FLAGS) -I$(SOUP_ENGINE) -c -o $@ $<
