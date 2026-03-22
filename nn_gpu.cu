/* nn_gpu.cu - CUDA backend for deep MLP training/inference.
 *
 * This implementation mirrors all NN layers on GPU memory and runs forward /
 * backprop updates with cuBLAS + lightweight custom kernels.
 */

#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <stdio.h>
#include <stdlib.h>

#include "nn.h"

#define CUDA_CHECK(call)                                                       \
    do {                                                                       \
        cudaError_t _e = (call);                                               \
        if (_e != cudaSuccess) {                                               \
            fprintf(stderr, "CUDA error %s:%d - %s\n",                       \
                    __FILE__, __LINE__, cudaGetErrorString(_e));               \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (0)

#define CUBLAS_CHECK(call)                                                     \
    do {                                                                       \
        cublasStatus_t _s = (call);                                            \
        if (_s != CUBLAS_STATUS_SUCCESS) {                                     \
            fprintf(stderr, "cuBLAS error %s:%d - status %d\n",              \
                    __FILE__, __LINE__, (int)_s);                              \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (0)

#define NN_GPU_MAX_BATCH 64

static float *d_weights_layers[NN_TOTAL_LAYERS] = {0};
static float *d_bias_layers[NN_TOTAL_LAYERS] = {0};
static float *d_acts[NN_TOTAL_LAYERS + 1] = {0};
static float *d_deltas[NN_TOTAL_LAYERS] = {0};
static float *d_target = NULL;

static cublasHandle_t g_cublas = NULL;
static int g_gpu_ready = 0;

__global__ void k_bias_sigmoid(float *__restrict__ out,
                               const float *__restrict__ bias,
                               int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        float v = out[i] + bias[i];
        out[i] = 1.0f / (1.0f + __expf(-v));
    }
}

__global__ void k_output_delta(float *__restrict__ delta,
                               const float *__restrict__ out,
                               const float *__restrict__ target,
                               int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        float err = out[i] - target[i];
        delta[i] = 2.0f * err * out[i] * (1.0f - out[i]);
    }
}

__global__ void k_hidden_delta(float *__restrict__ delta,
                               const float *__restrict__ delta_next,
                               const float *__restrict__ weights_next,
                               const float *__restrict__ act,
                               int n)
{
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    if (j < n) {
        float sum = 0.0f;
        for (int k = 0; k < n; k++) {
            sum += weights_next[(size_t)k * n + j] * delta_next[k];
        }
        float a = act[j];
        delta[j] = sum * a * (1.0f - a);
    }
}

extern "C" int nn_gpu_init(NeuralNet *net)
{
    const int n = NN_LAYER_SIZE;
    const size_t w_sz = (size_t)n * n * sizeof(float);
    const size_t v_sz = (size_t)n * sizeof(float);

    if (g_gpu_ready) {
        for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
            CUDA_CHECK(cudaMemcpy(d_weights_layers[l], net->weights_layers[l], w_sz,
                                  cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(d_bias_layers[l], net->bias_layers[l], v_sz,
                                  cudaMemcpyHostToDevice));
        }
        return 1;
    }

    CUDA_CHECK(cudaSetDevice(0));

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        CUDA_CHECK(cudaMalloc(&d_weights_layers[l], w_sz));
        CUDA_CHECK(cudaMalloc(&d_bias_layers[l], v_sz));
    }

    for (int l = 0; l < NN_TOTAL_LAYERS + 1; l++)
        CUDA_CHECK(cudaMalloc(&d_acts[l], v_sz));

    for (int l = 0; l < NN_TOTAL_LAYERS; l++)
        CUDA_CHECK(cudaMalloc(&d_deltas[l], v_sz));

    CUDA_CHECK(cudaMalloc(&d_target, v_sz));

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        CUDA_CHECK(cudaMemcpy(d_weights_layers[l], net->weights_layers[l], w_sz,
                              cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_bias_layers[l], net->bias_layers[l], v_sz,
                              cudaMemcpyHostToDevice));
    }

    CUBLAS_CHECK(cublasCreate(&g_cublas));
    g_gpu_ready = 1;

    fprintf(stderr, "[nn_gpu] Deep MLP GPU ready (%d hidden layers, width %d)\n",
            NN_HIDDEN_LAYERS, NN_LAYER_SIZE);
    return 1;
}

extern "C" void nn_gpu_sync_to_cpu(NeuralNet *net)
{
    if (!g_gpu_ready) return;

    const int n = NN_LAYER_SIZE;
    const size_t w_sz = (size_t)n * n * sizeof(float);
    const size_t v_sz = (size_t)n * sizeof(float);

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        CUDA_CHECK(cudaMemcpy(net->weights_layers[l], d_weights_layers[l], w_sz,
                              cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(net->bias_layers[l], d_bias_layers[l], v_sz,
                              cudaMemcpyDeviceToHost));
    }
}

extern "C" void nn_gpu_free(void)
{
    if (!g_gpu_ready) return;

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        cudaFree(d_weights_layers[l]);
        cudaFree(d_bias_layers[l]);
        cudaFree(d_deltas[l]);
        d_weights_layers[l] = NULL;
        d_bias_layers[l] = NULL;
        d_deltas[l] = NULL;
    }

    for (int l = 0; l < NN_TOTAL_LAYERS + 1; l++) {
        cudaFree(d_acts[l]);
        d_acts[l] = NULL;
    }

    cudaFree(d_target);
    d_target = NULL;

    cublasDestroy(g_cublas);
    g_cublas = NULL;
    g_gpu_ready = 0;
}

extern "C" int nn_gpu_is_ready(void)
{
    return g_gpu_ready;
}

extern "C" void nn_forward_gpu(const float *h_input, float *h_output)
{
    if (!g_gpu_ready) return;

    const int n = NN_LAYER_SIZE;
    const float alpha = 1.0f;
    const float beta = 0.0f;
    const int T = 256;
    const int B = (n + T - 1) / T;

    CUDA_CHECK(cudaMemcpy(d_acts[0], h_input, (size_t)n * sizeof(float),
                          cudaMemcpyHostToDevice));

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        CUBLAS_CHECK(cublasSgemv(g_cublas, CUBLAS_OP_T,
                                 n, n,
                                 &alpha,
                                 d_weights_layers[l], n,
                                 d_acts[l], 1,
                                 &beta,
                                 d_acts[l + 1], 1));

        k_bias_sigmoid<<<B, T>>>(d_acts[l + 1], d_bias_layers[l], n);
        CUDA_CHECK(cudaGetLastError());
    }

    CUDA_CHECK(cudaMemcpy(h_output, d_acts[NN_TOTAL_LAYERS],
                          (size_t)n * sizeof(float),
                          cudaMemcpyDeviceToHost));
}

extern "C" float nn_train_step_gpu(const float *h_input,
                                    const float *h_target,
                                    float lr)
{
    if (!g_gpu_ready) return 0.0f;

    const int n = NN_LAYER_SIZE;
    const int last = NN_TOTAL_LAYERS - 1;
    const float one = 1.0f;
    const float zero = 0.0f;
    const float neg_lr = -lr;

    const int T = 256;
    const int B = (n + T - 1) / T;

    CUDA_CHECK(cudaMemcpy(d_acts[0], h_input, (size_t)n * sizeof(float),
                          cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_target, h_target, (size_t)n * sizeof(float),
                          cudaMemcpyHostToDevice));

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        CUBLAS_CHECK(cublasSgemv(g_cublas, CUBLAS_OP_T,
                                 n, n,
                                 &one,
                                 d_weights_layers[l], n,
                                 d_acts[l], 1,
                                 &zero,
                                 d_acts[l + 1], 1));

        k_bias_sigmoid<<<B, T>>>(d_acts[l + 1], d_bias_layers[l], n);
        CUDA_CHECK(cudaGetLastError());
    }

    k_output_delta<<<B, T>>>(d_deltas[last], d_acts[NN_TOTAL_LAYERS], d_target, n);
    CUDA_CHECK(cudaGetLastError());

    for (int l = last - 1; l >= 0; l--) {
        k_hidden_delta<<<B, T>>>(d_deltas[l],
                                 d_deltas[l + 1],
                                 d_weights_layers[l + 1],
                                 d_acts[l + 1],
                                 n);
        CUDA_CHECK(cudaGetLastError());
    }

    for (int l = 0; l < NN_TOTAL_LAYERS; l++) {
        CUBLAS_CHECK(cublasSger(g_cublas,
                                n, n,
                                &neg_lr,
                                d_acts[l], 1,
                                d_deltas[l], 1,
                                d_weights_layers[l], n));

        CUBLAS_CHECK(cublasSaxpy(g_cublas,
                                 n,
                                 &neg_lr,
                                 d_deltas[l], 1,
                                 d_bias_layers[l], 1));
    }

    return 0.0f;
}

extern "C" float nn_train_step_gpu_batch(const float *input_batch,
                                          const float *target_batch,
                                          int batch_size,
                                          float lr)
{
    if (!g_gpu_ready || batch_size <= 0) return 0.0f;
    if (batch_size > NN_GPU_MAX_BATCH) batch_size = NN_GPU_MAX_BATCH;

    const int n = NN_LAYER_SIZE;
    const float step_lr = lr / (float)batch_size;

    for (int b = 0; b < batch_size; b++) {
        const float *in = input_batch + (size_t)b * n;
        const float *tg = target_batch + (size_t)b * n;
        nn_train_step_gpu(in, tg, step_lr);
    }

    return 0.0f;
}
