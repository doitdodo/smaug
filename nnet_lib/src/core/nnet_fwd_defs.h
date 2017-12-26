#ifndef _NNET_FWD_DEFS_H_
#define _NNET_FWD_DEFS_H_

#include <stdlib.h>

// This is the core header of nnet_lib.

extern int NUM_TEST_CASES;
extern int NUM_CLASSES;
extern int INPUT_DIM;
extern float* sigmoid_table;

typedef enum _data_init_mode {
    RANDOM,    // Generate pseudo-random input.
    FIXED,     // Use (mostly) constant values (helpful for debugging).
    READ_FILE  // Read data and weights from files.
} data_init_mode;

// When ping-ponging data between two buffers, use this to indicate which one
// stores the last output (and the next input).
typedef float* result_buf;

typedef enum _pool_type {
    MAX,
    AVG,
} pool_type;

typedef enum _activation_type {
    NO_ACTIVATION,
    RELU,
    RELU_THRESHOLD,
    LRELU,
    ELU,
    SELU,
    TANH,
    SIGMOID,
    SOFTMAX
} activation_type;

typedef enum _input_pp {
    FLATTEN,
    UNFLATTEN,
    NO_PREPROCESSING,
} input_pp;

typedef enum _layer_type {
    // 2D convolutional layer.
    CONV,
    // Pooling layer.
    POOLING,
    // Fully connected layer.
    FC,
    // Batch normalization layer.
    BATCH_NORM,
    // Output label layer, fully connected (the implicit last layer).
    OUTPUT,
    // Input layer. No actual work is done on this layer.
    INPUT,
    // End the network without a FC output layer.  This is mostly used for
    // debugging.
    END
} layer_type;

typedef struct _dims_t {
  int rows;
  int cols;
  int height;
  int align_pad;
} dims_t;

typedef enum _io_req_t {
  IO_NONE = 0,
  IO_DMA = 1,
  IO_ACP = 2,
  IO_CACHE = 3,
} io_req_t;

// Description of a layer in a neural network.
//
// TODO: Due to Aladdin's requirement to specify a word size for arrays, all
// members of a struct must be of the same size (so that they can be
// partitioned at a single granularity). This needs to be fixed so that we can have
// struct members of different sizes.
typedef struct _layer_t {
  // Type of layer.
  layer_type type;

  // Type of activation function.
  activation_type activation;

  dims_t inputs;
  dims_t weights;
  dims_t outputs;

  // Data input/output dimensions on a per iteration basis.
  //
  // These values refer to a single data point or image, so the total size of
  // the layer's output is output_rows * output_cols * NUM_TEST_CASES.  Our
  // convention is that layer i gets its input row/col from layer i-1's output
  // row/col. Depth is the number of feature maps read/written per iteration.
  //
  // Input/output rows/cols:
  //
  //   Conv/pool layers: the dimensions of the input/output images/activations.
  //      Note that the activations are stored in row vector form.
  //   FC layers: input_rows/cols is the size of the weights matrix. Output
  //      cols is the number of input rows for the next layer. Output rows is 1.
  //   Input layer: input rows/cols are the dimensions of each input image.
  //      Output rows/cols are the dimensions of the transformed input to the
  //      next layer.
  //
  // Input/output height:
  //    Conv layers: input height is the number of input feature maps from the
  //      previous layer, and output height is the number of filters (aka number
  //      of output feature maps).
  //    Pool layers: input/output heights are equal to number of input feature maps.
  //    All other layers: 1.

  // For CONV and POOL layers
  int field_stride;

  // CONV layers only.
  int c_padding;

  // POOL layers only.
  pool_type pool;

  // Where are the class predictions stored, hid or hid_temp?
  int result_in_temp;

  input_pp input_preprocessing;

  io_req_t input_req;
  io_req_t output_req;
} layer_t;


// A network is a stack of layers and a layer count.
typedef struct _network_t {
  layer_t* layers;
  int depth;
} network_t;

typedef struct _device_t {
    io_req_t cpu_default_offload;
    io_req_t cpu_pooling_offload;
    io_req_t cpu_activation_func_offload;
    // An implementation can pass any pointer containing architecture specific
    // state that must be shared.
    void* session;
} device_t;

// Wraps a dynamically allocated array (d for data) and its size (number of
// elements, not bytes).
typedef struct _farray_t {
    float* d;
    size_t size;
} farray_t;

typedef struct _iarray_t {
    int* d;
    size_t size;
} iarray_t;

// Possible values of ARCHITECTURE.
//
// This defines the structure of the nnet accelerator - whether it is a
// monolithic block or a collection of multiple blocks.
//
// Allowed values are: MONOLITHIC, COMPOSABLE, SMIV
#define MONOLITHIC 0
#define COMPOSABLE 1
#define SMIV 2
#define EIGEN 3
#define MKLDNN 4

// Convert a layer_type enum to a string
#define LAYER_TYPE_STR(arg) \
  (arg == CONV ? "CONV" : \
   arg == POOLING ? "POOLING" : \
   arg == FC ? "FC" : \
   arg == OUTPUT ? "OUTPUT" : \
   arg == INPUT ? "INPUT" : \
   "UNKNOWN")

#define ACTIVATION_TYPE_STR(arg) \
    (arg == NO_ACTIVATION ? "NONE" : \
     arg == RELU ? "RELU" : \
     arg == RELU_THRESHOLD ? "RELU_THRESHOLD" : \
     arg == LRELU ? "LRELU" : \
     arg == ELU ? "ELU" : \
     arg == SELU ? "SELU" : \
     arg == TANH ? "TANH" : \
     arg == SIGMOID ?  "SIGMOID" : \
     arg == SOFTMAX ? "SOFTMAX" : "UNKNOWN")


#define STRING(arg) #arg

// Convenience macros to switch between invoking an accelerator (if building a
// binary for gem5) or just calling the kernel function in software.
//
// Usage:
//
//  These macros expand differently based on whether the GEM5_HARNESS macro is
//  defined. If so, then this binary is meant to be run under gem5, invoking
//  accelerators; if not, this binary should run the pure software version of
//  the accelerated kernels.
//
//  If GEM5_HARNESS is defined:
//
//     MAP_ARRAY_TO_ACCEL(myReqCode, myArrayName, myArrayPtr, mySize)
//        ===>   mapArrayToAccelerator(myReqCode, myArrayName, myArrayPtr, mySize)
//
//     INVOKE_KERNEL(myReqCode, kernelFuncName, args...)
//        ===>   invokeAcceleratorAndBlock(myReqCode)
//
//  Otherwise:
//     MAP_ARRAY_TO_ACCEL(myReqCode, myArrayName, myArrayPtr, mySize)
//        expands to nothing
//
//     INVOKE_KERNEL(myReqCode, kernelFuncName, args...)
//        ===>  kernelFuncName(args)
//
#ifdef GEM5_HARNESS

#define MAP_ARRAY_TO_ACCEL(req_code, name, base_addr, size)                    \
    mapArrayToAccelerator(req_code, name, base_addr, size)
#define INVOKE_KERNEL(req_code, kernel_ptr, args...)                           \
    invokeAcceleratorAndBlock(req_code)

#else

#define MAP_ARRAY_TO_ACCEL(req_code, name, base_addr, size)
#define INVOKE_KERNEL(req_code, kernel_ptr, args...) kernel_ptr(args)

#endif

// Simplified version of MAP_ARRAY_TO_ACCEL.
//
// This assumes that the current name of the base pointer is also the name of
// the array in the top level function of the dynamic trace. THIS IS VERY
// IMPORTANT - if the argument passed to a top level function has been renamed in
// the function, then this WILL NOT WORK!
//
// MAP_ARRAY(myReqCode, myArray, mySize)
//    ===>   MAP_ARRAY_TO_ACCEL(myReqCode, "myArray", myArray, mySize)
#define MAP_ARRAY(req_code, name_and_base_addr, size)                          \
    MAP_ARRAY_TO_ACCEL(                                                        \
            req_code, STRING(name_and_base_addr), name_and_base_addr, size)


// Macros for computing the maximum of a group of elements.
//
// Why macros and not functions (or a loop)? A loop takes O(n) cycles to
// compute the maximum, when it could be done in O(log n) time with a tree
// based implementation. But Aladdin regards function calls as a hard
// dependency that it does not optimize across, so we would not get the
// parallelism we expect from the tree. Thus, these must be macros.
//
// I've only implemented a few of these. These are only meant for the pooling
// layers, and we shouldn't need more than a 3x3 pooling layer anyways.
#define max2(A, B) (((A) > (B)) ? (A) : (B))
#define max4(e0, e1, e2, e3) max2(max2(e0, e1), max2(e2, e3))
#define max8(e0, e1, e2, e3, e4, e5, e6, e7)                                   \
    max2(max4(e0, e1, e2, e3), max4(e4, e5, e6, e7))
#define max9(e0, e1, e2, e3, e4, e5, e6, e7, e8)                               \
    max2(max8(e0, e1, e2, e3, e4, e5, e6, e7), e8)

#define min2(A, B) (((A) < (B)) ? (A) : (B))

#define FRAC_CEIL(A, B) ((A) / (B) + ((A) % (B) != 0))

// 2D indexing into a flattened array.
//
// Operation: data[row][col]
#define sub2ind(r, c, n_columns) ((r) * (n_columns) + (c))

// 3D indexing into a flattened array.
//
// Operation: data[height][row][col]
#define sub3ind(h, r, c, n_rows, n_cols)                                       \
    (sub2ind(r, c, n_cols) + (h) * ((n_rows) * (n_cols)))

// 4D indexing into a flattened array.
//
// Operation: data[depth][height][row][col]
//
//                   c
//              ------------
//           r /           /|
//            /           / |
//           /           /  |
//  _     _  ------------   |
//  |     |  |          |   /
//  |     h  |          |  /|
//  |     |  |          | / |
//  d     -  ------------/  |
//  |        |          |   /
//  |        |          |  /
//  |        |          | /
//  -        |-----------/
//
// n_hgt = maximum value of h
// n_rows = maximum value of r
// n_cols = maximum value of c
//
// As an example, this is used to index input feature maps in convolutional
// layers, where depth = number of input images, and height = number of feature
// maps from previous layer.
#define sub4ind(d, h, r, c, n_hgt, n_rows, n_cols)                             \
    (sub3ind(h, r, c, n_rows, n_cols) + (d) * ((n_rows) * (n_cols) * (n_hgt)))

#define printf_sub4ind(d, h, r, c, n_hgt, n_rows, n_cols)                      \
    printf("sub4ind(%d, %d, %d, %d, %d, %d, %d) = %d\n", d, h, r, c, n_hgt,    \
           n_rows, n_cols, (sub3ind(h, r, c, n_rows, n_cols) +                 \
                            (d) * ((n_rows) * (n_cols) * (n_hgt))))

// Use these convenience macros to cast a raw pointer into a multidimensional
// variable-length array, which lets us use [] notation inside of the ugly
// sub2ind syntax!
//
// Usage:
//   If we have an array like array[5][4]:
//      ARRAY_2D(TYPE, output_name, array, 4);
//
//   If we have an array like array[5][4][3]:
//      ARRAY_3D(TYPE, output_name, array, 4, 3);
//
//   If we have an array like array[5][4][3][2]
//      ARRAY_4D(TYPE, output_name, array, 4, 3, 2);
//
//   And so on...
#define ARRAY_2D(TYPE, output_array_name, input_array_name, DIM_1)             \
    TYPE(*output_array_name)[DIM_1] = (TYPE(*)[DIM_1])input_array_name

#define ARRAY_3D(TYPE, output_array_name, input_array_name, DIM_1, DIM_2)      \
    TYPE(*output_array_name)[DIM_1][DIM_2] =                                   \
        (TYPE(*)[DIM_1][DIM_2])input_array_name

#define ARRAY_4D(                                                              \
    TYPE, output_array_name, input_array_name, DIM_1, DIM_2, DIM_3)            \
        TYPE(*output_array_name)[DIM_1][DIM_2][DIM_3] =                        \
            (TYPE(*)[DIM_1][DIM_2][DIM_3])input_array_name

#define ARRAY_5D(                                                              \
    TYPE, output_array_name, input_array_name, DIM_1, DIM_2, DIM_3, DIM_4)     \
        TYPE(*output_array_name)[DIM_1][DIM_2][DIM_3][DIM_4] =                 \
            (TYPE(*)[DIM_1][DIM_2][DIM_3][DIM_4])input_array_name

#if DEBUG_LEVEL >= 1
  #define INFO_MSG(args...) printf(args)

  #if DEBUG_LEVEL >= 2
    #define PRINT_MSG(args...) printf(args)
    #define PRINT_DEBUG(hid, rows, cols, num_cols)                                 \
        print_debug(hid, rows, cols, num_cols)
    #define PRINT_DEBUG4D(hid, rows, cols, height)                                 \
        print_debug4d(hid, rows, cols, height)

    #if DEBUG_LEVEL >= 3
      #define PRINT_DEBUG_V(hid, rows, cols, num_cols)                               \
          print_debug(hid, rows, cols, num_cols)
      #define PRINT_DEBUG4D_V(hid, rows, cols, height)                               \
          print_debug4d(hid, rows, cols, height)
      #define PRINT_MSG_V(args...) printf(args)
    #else
      #define PRINT_DEBUG_V(hid, rows, cols, num_cols)
      #define PRINT_DEBUG4D_V(hid, rows, cols, height)
      #define PRINT_MSG_V(args...)
    #endif
  #else
    #define PRINT_MSG(args...)
    #define PRINT_DEBUG(hid, rows, cols, num_cols)
    #define PRINT_DEBUG4D(hid, rows, cols, height)
    #define PRINT_DEBUG_V(hid, rows, cols, height)
    #define PRINT_DEBUG4D_V(hid, rows, cols, height)
    #define PRINT_MSG_V(args...)
  #endif
#else
  #define INFO_MSG(args...)
  #define PRINT_DEBUG(hid, rows, cols, num_cols)
  #define PRINT_DEBUG4D(hid, rows, cols, height)
  #define PRINT_MSG(args...)
  #define PRINT_DEBUG_V(hid, rows, cols, height)
  #define PRINT_DEBUG4D_V(hid, rows, cols, height)
  #define PRINT_MSG_V(args...)
#endif

#define CACHELINE_SIZE 64

#define ASSERT_MEMALIGN(ptr, err) \
    assert(err == 0 && "Failed to allocate memory for " #ptr ".\n");

// We have to disable all function inlining at the global level for Aladdin +
// LLVM-Tracer to work, but sometimes we do want to force inline functions
// (otherwise we run into all the issues of function call barriers in Aladdin).
// Add this macro before the function declaration to force inlining on this
// function.
//
// Don't do this except when we're tracing though; usually it is not necessary
// and it generates a lot of compiler warnings.
#ifdef TRACE_MODE
#define ALWAYS_INLINE __attribute__((__always_inline__))
#else
#define ALWAYS_INLINE
#endif

#endif
