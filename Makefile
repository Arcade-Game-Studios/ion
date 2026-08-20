.PHONY: all run bundle clean

BUILD_DIR := build
BUILD_TYPE := Debug
APP_NAME ?= Ion
APP_TARGET ?= basic_example

all:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR)

run: all
	./$(BUILD_DIR)/$(APP_TARGET)

run-%: all
	./$(BUILD_DIR)/examples/$*

bundle: all
	./scripts/build_app.sh $(APP_NAME) $(APP_TARGET) $(BUILD_TYPE)

open: bundle
	open $(BUILD_DIR)/$(APP_NAME).app

clean:
	rm -rf $(BUILD_DIR)
