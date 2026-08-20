.PHONY: all run clean

BUILD_DIR := build
BUILD_TYPE := Debug

all:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR)

run: all
	./$(BUILD_DIR)/my_game

clean:
	rm -rf $(BUILD_DIR)
