.PHONY: all run clean

BUILD_DIR := build
BUILD_TYPE := Debug

all:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR)

run: all
	./$(BUILD_DIR)/window_test

clean:
	rm -rf $(BUILD_DIR)
