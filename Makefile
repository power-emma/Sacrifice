CC = gcc
CFLAGS = -fdiagnostics-color=always -g -Wall -Wextra
LDFLAGS = -lncurses -lpthread -lm

# ── GPU acceleration ────────────────────────────────────────────────────────
# Default:     GPU build enabled
# CPU-only:    make GPU=0
# Requires:    sudo pacman -S cuda   (installs nvcc + cuBLAS)
GPU ?= 1
ifeq ($(GPU),1)
	NVCC       ?= /opt/cuda/bin/nvcc
	CUDA_HOSTCXX ?= /usr/bin/g++-14
  CUDA_ARCH   = sm_61          # GTX 1080 compute capability
	NVCCFLAGS   = -O3 -arch=$(CUDA_ARCH) -ccbin=$(CUDA_HOSTCXX) -Wno-deprecated-gpu-targets
  CUDA_OBJS   = nn_gpu.o
  CFLAGS     += -DUSE_CUDA -I/opt/cuda/include
  LDFLAGS    += -L/opt/cuda/lib64 -lcublas -lcudart -Wl,-rpath,/opt/cuda/lib64
else
  CUDA_OBJS   =
endif
# ────────────────────────────────────────────────────────────────────────────

SRCS = main.c rules.c boardchecks.c nn.c puzzles.c input.c output.c tui.c rewards.c gamestate.c puzzles_mt.c
OBJS = $(SRCS:.c=.o) $(CUDA_OBJS)
TARGET = main
TEST_OBJS = test_accuracy.o rules.o boardchecks.o nn.o puzzles.o input.o output.o tui.o rewards.o gamestate.o puzzles_mt.o $(CUDA_OBJS)

all: $(TARGET)

test_puzzles_mt: test_puzzles_mt.o rules.o boardchecks.o nn.o puzzles.o output.o rewards.o gamestate.o puzzles_mt.o tui_stubs.o $(CUDA_OBJS)
	$(CC) $(CFLAGS) -o test_puzzles_mt test_puzzles_mt.o rules.o boardchecks.o nn.o puzzles.o output.o rewards.o gamestate.o puzzles_mt.o tui_stubs.o $(CUDA_OBJS) $(LDFLAGS)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

test_accuracy: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o test_accuracy $(TEST_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(GPU),1)
nn_gpu.o: nn_gpu.cu nn.h
	$(NVCC) $(NVCCFLAGS) -c nn_gpu.cu -o nn_gpu.o
endif

run: all
	./$(TARGET)

test: test_accuracy
	./test_accuracy

clean:
	rm -f $(SRCS:.c=.o) nn_gpu.o $(TARGET) test_accuracy.o test_accuracy

.PHONY: all clean run test

