// Original Author: Andrej Karpathy
// https://github.com/karpathy/llm.c

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include "thread.h"
#include "thread-sync.h"

////////////////////////////问题出在n_没有重置！！！！！！！！！！！！！！
#define NUM_THREADS 4

// ----------------------------------------------------------------------------
// all the individual layers' forward passes
// B = batch_size, T = sequence_length, C = channels, V = vocab_size

typedef struct {
    float* out;
    int* inp;
    float* wte;
    float* wpe;
    int B, T, C;
    int start, end;
} EncoderThreadArg;


static EncoderThreadArg encoder_args[NUM_THREADS];
void encoder_thread_worker(int id) {
    EncoderThreadArg* arg = &encoder_args[id-1];
    //int BT = arg->B * arg->T;
    for (int idx = arg->start; idx < arg->end; ++idx) {
        int b = idx / arg->T;
        int t = idx % arg->T;
        float* out_bt = arg->out + b * arg->T * arg->C + t * arg->C;
        int ix = arg->inp[b * arg->T + t];
        float* wte_ix = arg->wte + ix * arg->C;
        float* wpe_t = arg->wpe + t * arg->C;
        for (int i = 0; i < arg->C; i++) {
            out_bt[i] = wte_ix[i] + wpe_t[i];
        }
    }
}

void encoder_forward(float* out,
                   int* inp, float* wte, float* wpe,
                   int B, int T, int C) {
    // out is (B,T,C). At each position (b,t), a C-dimensional vector summarizing token & position
    // inp is (B,T) of integers, holding the token ids at each (b,t) position
    // wte is (V,C) of token embeddings, short for "weight token embeddings"
    // wpe is (maxT,C) of position embeddings, short for "weight positional embedding"

    int BT = B * T;
    int chunk = (BT + NUM_THREADS - 1) / NUM_THREADS;
    for (int i = 0; i < NUM_THREADS; ++i) {
        encoder_args[i].out = out;
        encoder_args[i].inp = inp;
        encoder_args[i].wte = wte;
        encoder_args[i].wpe = wpe;
        encoder_args[i].B = B;
        encoder_args[i].T = T;
        encoder_args[i].C = C;
        encoder_args[i].start = i * chunk;
        encoder_args[i].end = (i + 1) * chunk;
        if (encoder_args[i].end > BT) encoder_args[i].end = BT;    
        spawn(encoder_thread_worker);
    }
    join();
    n_ = 0;
}

typedef struct {
    float* out;
    float* mean;
    float* rstd;
    float* inp;
    float* weight;
    float* bias;
    int B, T, C;
    int start, end;
} LayerNormThreadArg;

static LayerNormThreadArg layernorm_args[NUM_THREADS];
void layernorm_thread_worker(int id) {

    if (id < 1 || id > NUM_THREADS) {
        printf("layernorm_thread_worker: invalid id=%d\n", id);
        exit(1);
    }
    LayerNormThreadArg* arg = &layernorm_args[id-1];
    

    //int BT = arg->B * arg->T;
    float eps = 1e-5f;
    for (int idx = arg->start; idx < arg->end; ++idx) {
        int b = idx / arg->T;
        int t = idx % arg->T;
        float* x = arg->inp + b * arg->T * arg->C + t * arg->C;
        float m = 0.0f;
        for (int i = 0; i < arg->C; i++) m += x[i];
        m /= arg->C;
        float v = 0.0f;
        for (int i = 0; i < arg->C; i++) {
            float xshift = x[i] - m;
            v += xshift * xshift;
        }
        v /= arg->C;
        float safe = v + eps;
        if (safe <= 0.0f) safe = eps;
        float s = 1.0f / sqrtf(v + eps);
        float* out_bt = arg->out + b * arg->T * arg->C + t * arg->C;
        for (int i = 0; i < arg->C; i++) {
            float n = (s * (x[i] - m));
            float o = n * arg->weight[i] + arg->bias[i];
            out_bt[i] = o;
        }
        arg->mean[b * arg->T + t] = m;
        arg->rstd[b * arg->T + t] = s;
    }
 
}

void layernorm_forward(float* out, float* mean, float* rstd,
                       float* inp, float* weight, float* bias,
                       int B, int T, int C) {

    int BT = B * T;
    int chunk = (BT + NUM_THREADS - 1) / NUM_THREADS;
    for (int i = 0; i < NUM_THREADS; ++i) {
        layernorm_args[i].out = out;
        layernorm_args[i].mean = mean;
        layernorm_args[i].rstd = rstd;
        layernorm_args[i].inp = inp;
        layernorm_args[i].weight = weight;
        layernorm_args[i].bias = bias;
        layernorm_args[i].B = B;
        layernorm_args[i].T = T;
        layernorm_args[i].C = C;
        layernorm_args[i].start = i * chunk;
        layernorm_args[i].end = (i + 1) * chunk;
        if (layernorm_args[i].end > BT) layernorm_args[i].end = BT;    
        spawn(layernorm_thread_worker);
    }

    join();
    n_ = 0;
}


typedef struct {
    float* out;
    float* inp;
    float* weight;
    float* bias;
    int B, T, C, OC;
    int start, end; // 线程负责的起始和结束 (flattened over B*T)
} MatmulThreadArg;

static MatmulThreadArg matmul_args[NUM_THREADS];

void matmul_thread_worker(int id) {
    MatmulThreadArg* arg = &matmul_args[id-1];
    //int BT = arg->B * arg->T;
    for (int idx = arg->start; idx < arg->end; ++idx) {
        int b = idx / arg->T;
        int t = idx % arg->T;
        float* out_bt = arg->out + b * arg->T * arg->OC + t * arg->OC;
        float* inp_bt = arg->inp + b * arg->T * arg->C + t * arg->C;
        for (int o = 0; o < arg->OC; o++) {
            float val = (arg->bias != NULL) ? arg->bias[o] : 0.0f;
            float* wrow = arg->weight + o * arg->C;
            for (int i = 0; i < arg->C; i++) {
                val += inp_bt[i] * wrow[i];
            }
            out_bt[o] = val;
        }
    }
}


void matmul_forward(float* out,
                    float* inp, float* weight, float* bias,
                    int B, int T, int C, int OC) {
    // most of the running time is spent here and in matmul_backward
    // OC is short for "output channels"
    // inp is (B,T,C), weight is (OC, C), bias is (OC)
    // out will be (B,T,OC)

    int BT = B * T;
    int chunk = (BT + NUM_THREADS - 1) / NUM_THREADS;
    for (int i = 0; i < NUM_THREADS; ++i) {
        matmul_args[i].out = out;
        matmul_args[i].inp = inp;
        matmul_args[i].weight = weight;
        matmul_args[i].bias = bias;
        matmul_args[i].B = B;
        matmul_args[i].T = T;
        matmul_args[i].C = C;
        matmul_args[i].OC = OC;
        matmul_args[i].start = i * chunk;
        matmul_args[i].end = (i + 1) * chunk;
        if (matmul_args[i].end > BT) matmul_args[i].end = BT;
        spawn(matmul_thread_worker); 
    }
 
    join();
    n_ = 0;
}


typedef struct {
    float* out;
    float* preatt;
    float* att;
    float* inp;
    int B, T, C, NH;
    int start, end; // 线程负责的起始和结束 (flattened over B*T)
} AttentionThreadArg;

static AttentionThreadArg attention_args[NUM_THREADS];

void attention_thread_worker(int id) {
    AttentionThreadArg* arg = &attention_args[id-1];
    //printf("worker: id=%d, arg=%p, NH=%d\n", id-1, arg, arg->NH);
    int C3 = arg->C * 3;
    int hs = arg->C / arg->NH;
    
    float scale = 1.0f / sqrtf((float)hs);

    //int BTNH = arg->B * arg->T * arg->NH;
    int T = arg->T;
    int C = arg->C;
    int NH = arg->NH;

    for (int idx = arg->start; idx < arg->end; ++idx) {
   
        int b = idx / (T * NH);
        int t = (idx / NH) % T;
        int h = idx % NH;

        float* query_t = arg->inp + b * T * C3 + t * C3 + h * hs;
        float* preatt_bth = arg->preatt + b*NH*T*T + h*T*T + t*T;
        float* att_bth = arg->att + b*NH*T*T + h*T*T + t*T;

        // pass 1: query dot key and maxval
        float maxval = -10000.0f;
        for (int t2 = 0; t2 <= t; t2++) {
            float* key_t2 = arg->inp + b * T * C3 + t2 * C3 + h * hs + C; // +C for key
            float val = 0.0f;
            for (int i = 0; i < hs; i++) {
                val += query_t[i] * key_t2[i];
            }
            val *= scale;
            if (val > maxval) maxval = val;
            preatt_bth[t2] = val;
        }

        // pass 2: exp and sum
        float expsum = 0.0f;
        for (int t2 = 0; t2 <= t; t2++) {
            float expv = expf(preatt_bth[t2] - maxval);
            expsum += expv;
            att_bth[t2] = expv;
        }
        float expsum_inv = expsum == 0.0f ? 0.0f : 1.0f / expsum;

        // pass 3: softmax
        for (int t2 = 0; t2 < T; t2++) {
            if (t2 <= t) att_bth[t2] *= expsum_inv;
            else att_bth[t2] = 0.0f;
        }

        // pass 4: accumulate weighted values
        float* out_bth = arg->out + b * T * C + t * C + h * hs;
        for (int i = 0; i < hs; i++) out_bth[i] = 0.0f;
        for (int t2 = 0; t2 <= t; t2++) {
            float* value_t2 = arg->inp + b * T * C3 + t2 * C3 + h * hs + C*2; // +C*2 for value
            float att_btht2 = att_bth[t2];
            for (int i = 0; i < hs; i++) {
                out_bth[i] += att_btht2 * value_t2[i];
            }
        }
    }
}

void attention_forward(float* out, float* preatt, float* att,
                       float* inp,
                       int B, int T, int C, int NH) {
    // input is (B, T, 3C) holding the query, key, value (Q, K, V) vectors
    // preatt, att are (B, NH, T, T). NH = number of heads, T = sequence length
    // that holds the pre-attention and post-attention scores (used in backward)
    // output is (B, T, C)
    // attention is the only layer that mixes information across time
    // every other operation is applied at every (b,t) position independently
    // (and of course, no layer mixes information across batch)


    //printf("NH = %d\n",NH);

    int BTNH = B * T * NH;
    int chunk = (BTNH + NUM_THREADS - 1) / NUM_THREADS;
    for (int i = 0; i < NUM_THREADS; ++i) {
        //printf("forward: i=%d, &attention_args[i]=%p, NH=%d\n", i, &attention_args[i], attention_args[i].NH);
        attention_args[i].out = out;
        attention_args[i].preatt = preatt;
        attention_args[i].att = att;
        attention_args[i].inp = inp;
        attention_args[i].B = B;
        attention_args[i].T = T;
        attention_args[i].C = C;
        attention_args[i].NH = NH;
       // printf("attention_NH = %d\n",attention_args[i].NH);
        attention_args[i].start = i * chunk;
        attention_args[i].end = (i + 1) * chunk;
        if (attention_args[i].end > BTNH) attention_args[i].end = BTNH;
        spawn(attention_thread_worker);///////////////不知道为什么这里spawn的id会那么大
    }
   
    join();
    n_ = 0;
}


typedef struct {
    float* out;
    float* inp;
    int start, end;
} GeluThreadArg;

#define GELU_SCALING_FACTOR sqrtf(2.0f / M_PI)
static GeluThreadArg gelu_args[NUM_THREADS];

void gelu_thread_worker(int id) {
    GeluThreadArg* arg = &gelu_args[id-1];
    for (int i = arg->start; i < arg->end; i++) {
        float x = arg->inp[i];
        float cube = 0.044715f * x * x * x;
        arg->out[i] = 0.5f * x * (1.0f + tanhf(GELU_SCALING_FACTOR * (x + cube)));
    }
}



void gelu_forward(float* out, float* inp, int N) {
    // (approximate) GeLU elementwise non-linearity in the MLP block of Transformer

    // for (int i = 0; i < N; i++) {
    //     float x = inp[i];
    //     float cube = 0.044715f * x * x * x;
    //     out[i] = 0.5f * x * (1.0f + tanhf(GELU_SCALING_FACTOR * (x + cube)));
    // }
    int chunk = (N + NUM_THREADS - 1) / NUM_THREADS;
    for (int i = 0; i < NUM_THREADS; ++i) {
        gelu_args[i].out = out;
        gelu_args[i].inp = inp;
        gelu_args[i].start = i * chunk;
        gelu_args[i].end = (i + 1) * chunk;
        if (gelu_args[i].end > N) gelu_args[i].end = N;
        spawn(gelu_thread_worker);
    }

    join();
    n_ = 0;
}

typedef struct {
    float* out;
    float* inp1;
    float* inp2;
    int start, end;
} ResidualThreadArg;

static ResidualThreadArg residual_args[NUM_THREADS];

void residual_thread_worker(int id) {
    ResidualThreadArg* arg = &residual_args[id-1];
    for (int i = arg->start; i < arg->end; i++) {
        arg->out[i] = arg->inp1[i] + arg->inp2[i];
    }
}


void residual_forward(float* out, float* inp1, float* inp2, int N) {
    // for (int i = 0; i < N; i++) {
    //     out[i] = inp1[i] + inp2[i];
    // }
    int chunk = (N + NUM_THREADS - 1) / NUM_THREADS;
    for (int i = 0; i < NUM_THREADS; ++i) {
        residual_args[i].out = out;
        residual_args[i].inp1 = inp1;
        residual_args[i].inp2 = inp2;
        residual_args[i].start = i * chunk;
        residual_args[i].end = (i + 1) * chunk;
        if (residual_args[i].end > N) residual_args[i].end = N;
        spawn(residual_thread_worker);
    }

    join();
    n_ = 0;
}


typedef struct {
    float* probs;
    float* logits;
    int B, T, V;
    int start, end;
} SoftmaxThreadArg;

static SoftmaxThreadArg softmax_args[NUM_THREADS];

void softmax_thread_worker(int id) {
    SoftmaxThreadArg* arg = &softmax_args[id-1];
    //int BT = arg->B * arg->T;
    for (int idx = arg->start; idx < arg->end; ++idx) {
        int b = idx / arg->T;
        int t = idx % arg->T;
        float* logits_bt = arg->logits + b * arg->T * arg->V + t * arg->V;
        float* probs_bt = arg->probs + b * arg->T * arg->V + t * arg->V;
        float maxval = -10000.0f;
        for (int i = 0; i < arg->V; i++) {
            if (logits_bt[i] > maxval) maxval = logits_bt[i];
        }
        float sum = 0.0f;
        for (int i = 0; i < arg->V; i++) {
            probs_bt[i] = expf(logits_bt[i] - maxval);
            sum += probs_bt[i];
        }
        if (sum == 0.0f) {
            // 均匀分布或设置默认值
            float uniform = 1.0f / arg->V;
            for (int i = 0; i < arg->V; i++) {
                probs_bt[i] = uniform;
            }
        }else{
            for (int i = 0; i < arg->V; i++) {
                probs_bt[i] /= sum;
            }
        }

    }
}

void softmax_forward(float* probs, float* logits, int B, int T, int V) {
    // output: probs are (B,T,V) of the probabilities (sums to 1.0 in each b,t position)
    // input: logits is (B,T,V) of the unnormalized log probabilities

    int BT = B * T;
    int chunk = (BT + NUM_THREADS - 1) / NUM_THREADS;
    for (int i = 0; i < NUM_THREADS; ++i) {
        softmax_args[i].probs = probs;
        softmax_args[i].logits = logits;
        softmax_args[i].B = B;
        softmax_args[i].T = T;
        softmax_args[i].V = V;
        softmax_args[i].start = i * chunk;
        softmax_args[i].end = (i + 1) * chunk;
        if (softmax_args[i].end > BT) softmax_args[i].end = BT;
        spawn(softmax_thread_worker);
    } 

    join(); 
    n_ = 0;
}

// ----------------------------------------------------------------------------
// GPT-2 model definition

// the parameters of the model
#define NUM_PARAMETER_TENSORS 16
typedef struct {
    float* wte; // (V, C)
    float* wpe; // (maxT, C)
    float* ln1w; // (L, C)
    float* ln1b; // (L, C)
    float* qkvw; // (L, 3*C, C)
    float* qkvb; // (L, 3*C)
    float* attprojw; // (L, C, C)
    float* attprojb; // (L, C)
    float* ln2w; // (L, C)
    float* ln2b; // (L, C)
    float* fcw; // (L, 4*C, C)
    float* fcb; // (L, 4*C)
    float* fcprojw; // (L, C, 4*C)
    float* fcprojb; // (L, C)
    float* lnfw; // (C)
    float* lnfb; // (C)
} ParameterTensors;

// allocate memory for the parameters and point the individual tensors to the right places
float* malloc_and_point_parameters(ParameterTensors* params, size_t* param_sizes) {
    size_t num_parameters = 0;
    for (size_t i = 0; i < NUM_PARAMETER_TENSORS; i++) {
        num_parameters += param_sizes[i];
    }
    // malloc all parameters all at once
    float* params_memory = (float*)malloc(num_parameters * sizeof(float));
    // assign all the tensors
    float** ptrs[] = {
        &params->wte, &params->wpe, &params->ln1w, &params->ln1b, &params->qkvw, &params->qkvb,
        &params->attprojw, &params->attprojb, &params->ln2w, &params->ln2b, &params->fcw, &params->fcb,
        &params->fcprojw, &params->fcprojb, &params->lnfw, &params->lnfb
    };
    float* params_memory_iterator = params_memory;
    for (size_t i = 0; i < NUM_PARAMETER_TENSORS; i++) {
        *(ptrs[i]) = params_memory_iterator;
        params_memory_iterator += param_sizes[i];
    }
    return params_memory;
}

#define NUM_ACTIVATION_TENSORS 23
typedef struct {
    float* encoded; // (B, T, C)
    float* ln1; // (L, B, T, C)
    float* ln1_mean; // (L, B, T)
    float* ln1_rstd; // (L, B, T)
    float* qkv; // (L, B, T, 3*C)
    float* atty; // (L, B, T, C)
    float* preatt; // (L, B, NH, T, T)
    float* att; // (L, B, NH, T, T)
    float* attproj; // (L, B, T, C)
    float* residual2; // (L, B, T, C)
    float* ln2; // (L, B, T, C)
    float* ln2_mean; // (L, B, T)
    float* ln2_rstd; // (L, B, T)
    float* fch; // (L, B, T, 4*C)
    float* fch_gelu; // (L, B, T, 4*C)
    float* fcproj; // (L, B, T, C)
    float* residual3; // (L, B, T, C)
    float* lnf; // (B, T, C)
    float* lnf_mean; // (B, T)
    float* lnf_rstd; // (B, T)
    float* logits; // (B, T, V)
    float* probs; // (B, T, V)
    float* losses; // (B, T)
} ActivationTensors;

float* malloc_and_point_activations(ActivationTensors* acts, size_t* act_sizes) {
    size_t num_activations = 0;
    for (size_t i = 0; i < NUM_ACTIVATION_TENSORS; i++) {
        num_activations += act_sizes[i];
    }
    float* acts_memory = (float*)malloc(num_activations * sizeof(float));
    float** ptrs[] = {
        &acts->encoded, &acts->ln1, &acts->ln1_mean, &acts->ln1_rstd, &acts->qkv, &acts->atty,
        &acts->preatt, &acts->att, &acts->attproj, &acts->residual2, &acts->ln2, &acts->ln2_mean,
        &acts->ln2_rstd, &acts->fch, &acts->fch_gelu, &acts->fcproj, &acts->residual3, &acts->lnf,
        &acts->lnf_mean, &acts->lnf_rstd, &acts->logits, &acts->probs, &acts->losses
    };
    float* acts_memory_iterator = acts_memory;
    for (size_t i = 0; i < NUM_ACTIVATION_TENSORS; i++) {
        *(ptrs[i]) = acts_memory_iterator;
        acts_memory_iterator += act_sizes[i];
    }
    return acts_memory;
}

typedef struct {
    int max_seq_len; // max sequence length, e.g. 1024
    int vocab_size; // vocab size, e.g. 50257
    int num_layers; // number of layers, e.g. 12
    int num_heads; // number of heads in attention, e.g. 12
    int channels; // number of channels, e.g. 768
} GPT2Config;

typedef struct {
    GPT2Config config;
    // the weights (parameters) of the model, and their sizes
    ParameterTensors params;
    size_t param_sizes[NUM_PARAMETER_TENSORS];
    float* params_memory;
    int num_parameters;
    // gradients of the weights
    ParameterTensors grads;
    float* grads_memory;
    // buffers for the AdamW optimizer
    float* m_memory;
    float* v_memory;
    // the activations of the model, and their sizes
    ActivationTensors acts;
    size_t act_sizes[NUM_ACTIVATION_TENSORS];
    float* acts_memory;
    int num_activations;
    // gradients of the activations
    ActivationTensors grads_acts;
    float* grads_acts_memory;
    // other run state configuration
    int batch_size; // the batch size (B) of current forward pass
    int seq_len; // the sequence length (T) of current forward pass
    int* inputs; // the input tokens for the current forward pass
    int* targets; // the target tokens for the current forward pass
    float mean_loss; // after a forward pass with targets, will be populated with the mean loss
} GPT2;



//读取模型配置、分配内存等
void gpt2_build_from_checkpoint(GPT2 *model, char* checkpoint_path) {

    // read in model from a checkpoint file
    FILE *model_file = fopen(checkpoint_path, "rb");
    if (model_file == NULL) { printf("Error opening model file\n"); exit(1); }
    int model_header[256];
    fread(model_header, sizeof(int), 256, model_file);
    if (model_header[0] != 20240326) { printf("Bad magic model file"); exit(1); }
    if (model_header[1] != 1) { printf("Bad version in model file"); exit(1); }

    // read in hyperparameters
    int maxT, V, L, NH, C;
    model->config.max_seq_len = maxT = model_header[2];
    model->config.vocab_size = V = model_header[3];
    model->config.num_layers = L = model_header[4];
    model->config.num_heads = NH = model_header[5];
    model->config.channels = C = model_header[6];

    // allocate space for all the parameters and read them in
    model->param_sizes[0] = V * C; // wte
    model->param_sizes[1] = maxT * C; // wpe
    model->param_sizes[2] = L * C; // ln1w
    model->param_sizes[3] = L * C; // ln1b
    model->param_sizes[4] = L * (3 * C) * C; // qkvw
    model->param_sizes[5] = L * (3 * C); // qkvb
    model->param_sizes[6] = L * C * C; // attprojw
    model->param_sizes[7] = L * C; // attprojb
    model->param_sizes[8] = L * C; // ln2w
    model->param_sizes[9] = L * C; // ln2b
    model->param_sizes[10] = L * (4 * C) * C; // fcw
    model->param_sizes[11] = L * (4 * C); // fcb
    model->param_sizes[12] = L * C * (4 * C); // fcprojw
    model->param_sizes[13] = L * C; // fcprojb
    model->param_sizes[14] = C; // lnfw
    model->param_sizes[15] = C; // lnfb

    // cound the number of paramaters
    size_t num_parameters = 0;
    for (size_t i = 0; i < NUM_PARAMETER_TENSORS; i++) {
        num_parameters += model->param_sizes[i];
    }
    model->num_parameters = num_parameters;

    // read in all the parameters from file
    model->params_memory = malloc_and_point_parameters(&model->params, model->param_sizes);
    fread(model->params_memory, sizeof(float), num_parameters, model_file);
    fclose(model_file);

    // other inits
    model->acts_memory = NULL;
    model->grads_memory = NULL;
    model->m_memory = NULL;
    model->v_memory = NULL;
    model->grads_acts_memory = NULL;
    model->inputs = NULL;
    model->targets = NULL;
    model->batch_size = 0;
    model->seq_len = 0;
    model->mean_loss = -1.0f; // -1.0f will designate no loss
}



void gpt2_forward(GPT2 *model, int* inputs, int B, int T) {
    // convenience parameters
    int V = model->config.vocab_size;
    int L = model->config.num_layers;
    int NH = model->config.num_heads;
    int C = model->config.channels;

    // record the current B,T as well
    model->batch_size = B;
    model->seq_len = T;
    // and now allocate the space
    model->act_sizes[0] = B * T * C; // encoded
    model->act_sizes[1] = L * B * T * C; // ln1
    model->act_sizes[2] = L * B * T;  // ln1_mean
    model->act_sizes[3] = L * B * T;  // ln1_rstd
    model->act_sizes[4] = L * B * T * 3*C; // qkv
    model->act_sizes[5] = L * B * T * C;  // atty
    model->act_sizes[6] = L * B * NH * T * T;  // preatt
    model->act_sizes[7] = L * B * NH * T * T;  // att
    model->act_sizes[8] = L * B * T * C; // attproj
    model->act_sizes[9] = L * B * T * C; // residual2
    model->act_sizes[10] = L * B * T * C; // ln2
    model->act_sizes[11] = L * B * T; // ln2_mean
    model->act_sizes[12] = L * B * T; // ln2_rstd
    model->act_sizes[13] = L * B * T * 4*C; // fch
    model->act_sizes[14] = L * B * T * 4*C; // fch_gelu
    model->act_sizes[15] = L * B * T * C; // fcproj
    model->act_sizes[16] = L * B * T * C; // residual3
    model->act_sizes[17] = B * T * C; // lnf
    model->act_sizes[18] = B * T; // lnf_mean
    model->act_sizes[19] = B * T; // lnf_rstd
    model->act_sizes[20] = B * T * V; // logits
    model->act_sizes[21] = B * T * V; // probs
    model->act_sizes[22] = B * T; // losses
    size_t num_activations = 0;
    for (size_t i = 0; i < NUM_ACTIVATION_TENSORS; i++) {
        num_activations += model->act_sizes[i];
    }
    model->num_activations = num_activations;

    if (model->acts_memory) {
        free(model->acts_memory);
        model->acts_memory = NULL;
    }
    model->acts_memory = malloc_and_point_activations(&model->acts, model->act_sizes);

    // also create memory for caching inputs and targets
    if (model->inputs) {
        free(model->inputs);
    }
    model->inputs = (int*)malloc(B * T * sizeof(int));

    // cache the inputs/targets
    memcpy(model->inputs, inputs, B * T * sizeof(int));

    // forward pass
    ParameterTensors params = model->params; // for brevity
    ActivationTensors acts = model->acts;
    float* residual;
    encoder_forward(acts.encoded, inputs, params.wte, params.wpe, B, T, C); // encoding goes into residual[0]
    //printf("step0\n");
    for (int l = 0; l < L; l++) {

        residual = l == 0 ? acts.encoded : acts.residual3 + (l-1) * B * T * C;

        // get the pointers of the weights for this layer
        float* l_ln1w = params.ln1w + l * C;
        float* l_ln1b = params.ln1b + l * C;
        float* l_qkvw = params.qkvw + l * 3*C * C;
        float* l_qkvb = params.qkvb + l * 3*C;
        float* l_attprojw = params.attprojw + l * C * C;
        float* l_attprojb = params.attprojb + l * C;
        float* l_ln2w = params.ln2w + l * C;
        float* l_ln2b = params.ln2b + l * C;
        float* l_fcw = params.fcw + l * 4*C * C;
        float* l_fcb = params.fcb + l * 4*C;
        float* l_fcprojw = params.fcprojw + l * C * 4*C;
        float* l_fcprojb = params.fcprojb + l * C;

        // get the pointers of the activations for this layer
        float* l_ln1 = acts.ln1 + l * B * T * C;
        float* l_ln1_mean = acts.ln1_mean + l * B * T;
        float* l_ln1_rstd = acts.ln1_rstd + l * B * T;
        float* l_qkv = acts.qkv + l * B * T * 3*C;
        float* l_atty = acts.atty + l * B * T * C;
        float* l_preatt = acts.preatt + l * B * NH * T * T;
        float* l_att = acts.att + l * B * NH * T * T;
        float* l_attproj = acts.attproj + l * B * T * C;
        float* l_residual2 = acts.residual2 + l * B * T * C;
        float* l_ln2 = acts.ln2 + l * B * T * C;
        float* l_ln2_mean = acts.ln2_mean + l * B * T;
        float* l_ln2_rstd = acts.ln2_rstd + l * B * T;
        float* l_fch = acts.fch + l * B * T * 4*C;
        float* l_fch_gelu = acts.fch_gelu + l * B * T * 4*C;
        float* l_fcproj = acts.fcproj + l * B * T * C;
        float* l_residual3 = acts.residual3 + l * B * T * C;

        // now do the forward pass
        layernorm_forward(l_ln1, l_ln1_mean, l_ln1_rstd, residual, l_ln1w, l_ln1b, B, T, C);
        //printf("step1\n");
        matmul_forward(l_qkv, l_ln1, l_qkvw, l_qkvb, B, T, C, 3*C);
        //printf("step2\n");
        attention_forward(l_atty, l_preatt, l_att, l_qkv, B, T, C, NH);
        //printf("step3\n");
        matmul_forward(l_attproj, l_atty, l_attprojw, l_attprojb, B, T, C, C);
        //printf("step4\n");
        residual_forward(l_residual2, residual, l_attproj, B*T*C);
        //printf("step5\n");
        layernorm_forward(l_ln2, l_ln2_mean, l_ln2_rstd, l_residual2, l_ln2w, l_ln2b, B, T, C);
        matmul_forward(l_fch, l_ln2, l_fcw, l_fcb, B, T, C, 4*C);
        gelu_forward(l_fch_gelu, l_fch, B*T*4*C);
        matmul_forward(l_fcproj, l_fch_gelu, l_fcprojw, l_fcprojb, B, T, 4*C, C);
        residual_forward(l_residual3, l_residual2, l_fcproj, B*T*C);
    }
    residual = acts.residual3 + (L-1) * B * T * C; // last residual is in residual3
    layernorm_forward(acts.lnf, acts.lnf_mean, acts.lnf_rstd, residual, params.lnfw, params.lnfb, B, T, C);
    matmul_forward(acts.logits, acts.lnf, params.wte, NULL, B, T, C, V);
    softmax_forward(acts.probs, acts.logits, B, T, V);
}

void gpt2_zero_grad(GPT2 *model) {
    if(model->grads_memory != NULL) { memset(model->grads_memory, 0, model->num_parameters * sizeof(float)); }
    if(model->grads_acts_memory != NULL) { memset(model->grads_acts_memory, 0, model->num_activations * sizeof(float)); }
}

void gpt2_free(GPT2 *model) {
    free(model->params_memory);
    free(model->grads_memory);
    free(model->m_memory);
    free(model->v_memory);
    free(model->acts_memory);
    free(model->grads_acts_memory);
    free(model->inputs);
    free(model->targets);
}

int sample_mult(float* probabilities, int n) {
    // sample index from probabilities (they must sum to 1!)
    // coin can be a random number in [0, 1), usually from random_f32()
    float cdf = 0.0f, coin = 0.5f;
    for (int i = 0; i < n; i++) {
        cdf += probabilities[i];
        if (coin < cdf) {
            return i;
        }
    }
    return n - 1; // in case of rounding errors
}

// the GPT-2 end-of-text token id
#define GPT2_EOT 50256

int main(int argc, char** argv) {
    GPT2 model;
    gpt2_build_from_checkpoint(&model, "gpt2_124M.bin");//加载预训练模型

    const int n = 10;  // Token limit.   token上限

    if (argc == 1) {//无token输入，报错
        printf("Provide at least one token.\n");
        exit(1);
    }
    if (argc > n) {//超过token上限，报错
        printf("Tow many tokens.\n");
        exit(1);
    }

    int tokens[n];//初始化token数组

    for (int i = 0; i < n; i++) {
        if (i + 1 < argc) {
            tokens[i] = strtol(argv[i + 1], NULL, 10);
        } else {
            tokens[i] = GPT2_EOT;
        }
    }
//逐步生成后续token
    for (int t = argc - 1; t < n; t++) {
        gpt2_forward(&model, tokens, 1, t);//前向传播计算概率
        // 每次传入当前token序列（长度t），计算下一个token的概率分布。
        float* probs = model.acts.probs + (t-1) * model.config.vocab_size;//采样下一个token
        // 从概率分布中选取下一个token，追加到序列中。
        int next_token = sample_mult(probs, model.config.vocab_size);
        tokens[t] = next_token;

        printf("%d\n", tokens[t]);
        fflush(stdout);
    }

    gpt2_free(&model);

    return 0;
}
