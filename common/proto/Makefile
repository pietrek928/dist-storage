PROTO_FILES := $(wildcard *.proto)
CC_OUT_DIR := cc
GO_OUT_DIR := go
PY_OUT_DIR := py

all: clean cc py

cc: $(PROTO_FILES)
	@mkdir -p $(CC_OUT_DIR)
	protoc --proto_path=. --cpp_out=$(CC_OUT_DIR) --grpc_cpp_out=$(CC_OUT_DIR) --plugin=protoc-gen-grpc_cpp=/usr/bin/grpc_cpp_plugin  $(PROTO_FILES)

# go: $(PROTO_FILES)
# 	@mkdir -p $(GO_OUT_DIR)
# 	protoc --proto_path=. --go_out=$(GO_OUT_DIR) $(PROTO_FILES)

py: $(PROTO_FILES)
	@mkdir -p $(PY_OUT_DIR)
	protoc --proto_path=. --python_out=$(PY_OUT_DIR) --grpc_python_out=$(PY_OUT_DIR) --plugin=protoc-gen-grpc_python=/usr/bin/grpc_python_plugin $(PROTO_FILES)

clean:
	rm -rf $(CC_OUT_DIR) $(GO_OUT_DIR) $(PY_OUT_DIR)

.PHONY: all clean cc go py
