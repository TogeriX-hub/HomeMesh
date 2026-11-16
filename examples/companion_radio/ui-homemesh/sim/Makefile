# HomeMesh PC simulator (headless, writes PNG screenshots)
#   make LVGL_DIR=/path/to/lvgl   (LVGL v9.4.0)
#   ./homemesh_sim out/

LVGL_DIR ?= ../../../../../lvgl
UI_DIR   := ..
SRC_DIR  := ../../../../src
BUILD    := build

CFLAGS   := -O1 -g -I. -I$(LVGL_DIR) -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
CXXFLAGS := $(CFLAGS) -std=c++17 -Wall -I$(SRC_DIR)

LVGL_SRC := $(shell find $(LVGL_DIR)/src -name '*.c')
FONT_SRC := $(wildcard $(UI_DIR)/fonts/*.c)
LVGL_OBJ := $(patsubst $(LVGL_DIR)/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SRC))
FONT_OBJ := $(patsubst $(UI_DIR)/fonts/%.c,$(BUILD)/fonts/%.o,$(FONT_SRC))
UI_OBJ   := $(BUILD)/HomeMeshUI.o $(BUILD)/main.o

homemesh_sim: $(LVGL_OBJ) $(FONT_OBJ) $(UI_OBJ)
	$(CXX) -o $@ $^ -lz -lm

$(BUILD)/lvgl/%.o: $(LVGL_DIR)/%.c lv_conf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fonts/%.o: $(UI_DIR)/fonts/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/HomeMeshUI.o: $(UI_DIR)/HomeMeshUI.cpp $(UI_DIR)/HomeMeshUI.h $(UI_DIR)/Model.h $(UI_DIR)/Appointment.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/main.o: main.cpp $(UI_DIR)/HomeMeshUI.h $(UI_DIR)/Model.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) homemesh_sim

.PHONY: clean
