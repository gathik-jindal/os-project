CC = gcc
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c11
LDLIBS ?= -pthread
BIN_DIR ?= bin

all: $(BIN_DIR)/server $(BIN_DIR)/client

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/server: server.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BIN_DIR)/client: client.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

run-server: $(BIN_DIR)/server
	./$(BIN_DIR)/server

run-client: $(BIN_DIR)/client
	./$(BIN_DIR)/client

clean:
	rm -f $(BIN_DIR)/server $(BIN_DIR)/client

.PHONY: all clean run-server run-client
