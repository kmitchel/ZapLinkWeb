CC = gcc
CFLAGS = -Wall -Wextra -I./include -g -D_REENTRANT $(shell pkg-config --cflags avahi-client)
LDFLAGS = -lsqlite3 -lpthread $(shell pkg-config --libs avahi-client)

SRC_DIR = src
OBJ_DIR = build/obj
BIN_DIR = build

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
TARGET = $(BIN_DIR)/zaplinkweb

# Installation paths
INSTALL_DIR = /opt/zaplink
BINDIR = $(INSTALL_DIR)
CONFDIR = $(INSTALL_DIR)
SERVICEFILE = zaplinkweb.service

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN_DIR)

install: $(TARGET)
	@echo "Creating install directory..."
	@mkdir -p $(INSTALL_DIR)
	@echo "Creating zaplink user..."
	@id -u zaplink &>/dev/null || useradd -r -s /usr/sbin/nologin -d $(INSTALL_DIR) zaplink
	@echo "Installing binary..."
	@install -m 755 -o zaplink -g zaplink $(TARGET) $(BINDIR)/zaplinkweb
	@echo "Installing public assets..."
	@mkdir -p $(INSTALL_DIR)/public
	@cp -r public/* $(INSTALL_DIR)/public/
	@chown -R zaplink:zaplink $(INSTALL_DIR)/public
	@echo "Initializing database..."
	@test -f $(INSTALL_DIR)/zaplinkweb.db || install -m 664 -o zaplink -g zaplink zaplinkweb.db $(INSTALL_DIR)/
	@echo "Creating recordings directory..."
	@mkdir -p $(INSTALL_DIR)/recordings
	@chown zaplink:zaplink $(INSTALL_DIR)/recordings
	@echo "Installing systemd service..."
	@# Patch service file for production paths before install
	@sed -e 's|WorkingDirectory=.*|WorkingDirectory=$(INSTALL_DIR)|g' \
	     -e 's|ExecStart=.*|ExecStart=$(BINDIR)/zaplinkweb|g' \
	     -e 's|User=.*|User=zaplink|g' \
	     $(SERVICEFILE) > /etc/systemd/system/$(SERVICEFILE)
	@chmod 644 /etc/systemd/system/$(SERVICEFILE)
	@systemctl daemon-reload
	@echo ""
	@echo "Installation complete!"
	@echo "  Binary: $(BINDIR)/zaplinkweb"
	@echo "  URL: http://localhost:3000"
	@echo ""
	@echo "To start service: sudo systemctl enable --now zaplinkweb"

uninstall:
	@echo "Stopping service..."
	-@systemctl stop zaplinkweb 2>/dev/null || true
	-@systemctl disable zaplinkweb 2>/dev/null || true
	@echo "Removing files..."
	@rm -f /etc/systemd/system/$(SERVICEFILE)
	@rm -f $(BINDIR)/zaplinkweb
	@rm -rf $(INSTALL_DIR)/public
	@# Asking before removing data
	@echo "Note: Database and recordings in $(INSTALL_DIR) were NOT removed."
	@systemctl daemon-reload
	@echo "Uninstall complete."
