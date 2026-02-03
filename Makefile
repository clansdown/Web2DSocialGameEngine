# Makefile for Web2D Social Game Engine
# Simple build system alternative to CMake

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -Iclient/include -Iserver/include -Ishared/include
AR = ar
ARFLAGS = rcs

# Directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
LIB_DIR = $(BUILD_DIR)/lib

# Source files
SHARED_SOURCES = $(wildcard shared/src/*/*.cpp)
CLIENT_SOURCES = $(wildcard client/src/*/*.cpp)
SERVER_SOURCES = $(wildcard server/src/*/*.cpp)

# Object files
SHARED_OBJECTS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SHARED_SOURCES))
CLIENT_OBJECTS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CLIENT_SOURCES))
SERVER_OBJECTS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SERVER_SOURCES))

# Library files
SHARED_LIB = $(LIB_DIR)/libweb2d_shared.a
CLIENT_LIB = $(LIB_DIR)/libweb2d_client.a
SERVER_LIB = $(LIB_DIR)/libweb2d_server.a

# Default target
.PHONY: all
all: $(SHARED_LIB) $(CLIENT_LIB) $(SERVER_LIB)

# Create directories
$(OBJ_DIR) $(LIB_DIR):
	mkdir -p $@

# Build shared library
$(SHARED_LIB): $(SHARED_OBJECTS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# Build client library
$(CLIENT_LIB): $(CLIENT_OBJECTS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# Build server library
$(SERVER_LIB): $(SERVER_OBJECTS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# Compile object files
$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

# Help
.PHONY: help
help:
	@echo "Web2D Social Game Engine Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build all libraries (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  help    - Show this help message"
