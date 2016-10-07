GCC := gcc
GXX := g++

project_path := $(shell pwd)
async_redis_client_project_path := $(shell cd $(project_path)/.. && pwd)
libuv_prefix := $(HOME)/lib/libuv/1.9.1
cxx11_common_path := /home/wangwei/project/org/pp-qq/common
glog_prefix := /usr
gflags_prefix := /usr

BIN := test

C_SRC := 

CXX_SRC := $(async_redis_client_project_path)/src/libuv_timer/timer_manager.cc	

CXX_SRC += $(project_path)/main.cc

CXX_SRC += \
	$(cxx11_common_path)/src/common/utils.cc	\
	$(cxx11_common_path)/src/exception/errno_exception.cc	\
	$(cxx11_common_path)/src/exception/resource_exception.cc	
	
CFLAGS := 

CXXFLAGS := -Wall -pthread -Wno-deprecated-declarations -std=gnu++11
CXXFLAGS += -O0 -ggdb  

# CXXFLAGS += -O2 -DNDEBUG

CXXFLAGS += -I$(async_redis_client_project_path)/src

CXXFLAGS += -I$(cxx11_common_path)/src

CXXFLAGS += 	\
	-I$(glog_prefix)/include \
	-I$(gflags_prefix)/include		

CXXFLAGS += -I$(libuv_prefix)/include
	
LDFLAGS := 	\
	$(libuv_prefix)/lib/libuv.a

LDFLAGS += -pthread -lrt -lglog -lgflags
# LDFLAGS += -pg



### 以下不需要配置 ###
all: 

OBJ_DIR := objs
DEP_DIR := $(OBJ_DIR)/deps
BIN_DIR := bin

C_OBJS := $(C_SRC:.c=.c.o)
CXX_OBJS := $(foreach ccfile,$(CXX_SRC),$(ccfile).o)
ALL_DEPS := $(C_OBJS:.o=.dep)
ALL_DEPS += $(CXX_OBJS:.o=.dep)

C_OBJS := $(foreach var,$(C_OBJS),$(OBJ_DIR)/$(var))
CXX_OBJS := $(foreach var,$(CXX_OBJS),$(OBJ_DIR)/$(var))
ALL_DEPS := $(foreach var,$(ALL_DEPS),$(DEP_DIR)/$(var))

all: $(BIN_DIR)/$(BIN)

include $(ALL_DEPS)

$(DEP_DIR)/%.c.dep: %.c
	@if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@); fi
	@echo [依赖生成]: $< --- $@
	@$(GCC) -E -M -MQ $(OBJ_DIR)/$*.c.o -MQ $@ -MF $@ $< $(CFLAGS)

$(DEP_DIR)/%.cc.dep: %.cc
	@if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@); fi
	@echo [依赖生成]: $< --- $@
	@$(GXX) -E -M -MQ $(OBJ_DIR)/$*.cc.o -MQ $@ -MF $@ $< $(CXXFLAGS)

$(DEP_DIR)/%.cpp.dep: %.cpp
	@if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@); fi
	@echo [依赖生成]: $< --- $@
	@$(GXX) -E -M -MQ $(OBJ_DIR)/$*.cpp.o -MQ $@ -MF $@ $< $(CXXFLAGS)
	
	
$(OBJ_DIR)/%.c.o: %.c
	@if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@); fi
	@echo [编译]: $< --- $@
	@$(GCC) -c $< -o $@ $(CFLAGS)

$(OBJ_DIR)/%.cc.o: %.cc
	@if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@); fi
	@echo [编译]: $< --- $@
	@$(GXX) -c $< -o $@ $(CXXFLAGS)

$(OBJ_DIR)/%.cpp.o: %.cpp
	@if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@); fi
	@echo [编译]: $< --- $@
	@$(GXX) -c $< -o $@ $(CXXFLAGS)
	
$(BIN_DIR)/$(BIN): $(C_OBJS) $(CXX_OBJS)
	@if [ ! -d $(BIN_DIR) ] ; then mkdir -p $(BIN_DIR); fi
	@echo [链接]: $^ --- $@
ifeq ($(strip $(CXX_SRC)),)
	@$(GCC) -o $(BIN_DIR)/$(BIN) $(C_OBJS) $(CXX_OBJS) $(LDFLAGS)
else
	@$(GXX) -o $(BIN_DIR)/$(BIN) $(C_OBJS) $(CXX_OBJS) $(LDFLAGS)
endif

clean:
	-@rm -rv $(BIN_DIR) $(OBJ_DIR)
	
.PHONY: all clean 
