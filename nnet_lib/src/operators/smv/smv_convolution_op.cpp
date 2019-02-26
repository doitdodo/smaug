#include "core/backend.h"
#include "operators/common.h"
#include "operators/smv/smv_convolution_op.h"
#include "operators/smv/smv_convolution_tiling.h"
#include "operators/smv/kernels.h"
#include "utility/debug_stream.h"

namespace smaug {
namespace smv {
namespace conv {

const int kNumPEs = 8;
const int kNumMaccsPerPE = 32;

}  // namespace conv
}  // namespace smv

void SmvConvolutionOp::runNHWC(SmvTiledTensor& inputs,
                               SmvTiledTensor& weights,
                               SmvTiledTensor& outputs) {
    auto inputIdx = inputs.startIndex();
    auto weightIdx = weights.startIndex();
    auto outputIdx = outputs.startIndex();
    for (int N = 0; N < inputs.getShape()[0]; N++) {
        for (int H = 0; H < inputs.getShape()[1]; H++) {
            for (int W = 0; W < weights.getShape()[0]; W++) {
                int inputChanTiles = inputs.getShape()[3];
                int weightChanTiles = weights.getShape()[3];
                int iC = 0, wC = 0;
                while (iC < inputChanTiles && wC < weightChanTiles) {
                    std::cout << "Input: " << inputIdx(N, H, 0, iC)
                              << ", weights: " << weightIdx(W, 0, 0, wC)
                              << ", output: " << outputIdx(N, H, 0, W) << "\n";
                    SmvTensor* inputTile = inputs[inputIdx(N, H, 0, iC)];
                    SmvTensor* weightsTile = weights[weightIdx(W, 0, 0, wC)];
                    SmvTensor* outputTile = outputs[outputIdx(N, H, 0, W)];
                    const TensorShape& inputShape = inputTile->getShape();
                    const TensorShape& weightsShape = weightsTile->getShape();
                    const TensorShape& outputShape = outputTile->getShape();
                    int inputDims[4] = { inputShape[0], inputShape[1],
                                         inputShape[2], inputShape[3] };
                    int weightsDims[4] = { weightsShape[0], weightsShape[1],
                                           weightsShape[2], weightsShape[3] };
                    int outputDims[4] = { outputShape[0], outputShape[1],
                                          outputShape[2], outputShape[3] };
                    smv_conv3d_f32_nhwc_same_padding_vec_fxp(
                            inputTile->data<float>(),
                            weightsTile->data<float>(),
                            outputTile->data<float>(),
                            inputDims,
                            weightsDims,
                            outputDims,
                            inputShape.getPadding(3),
                            weightsShape.getPadding(3),
                            outputShape.getPadding(3),
                            getRowStride(),
                            getColStride(),
                            W,
                            iC,
                            iC == wC);

                    if (inputChanTiles == weightChanTiles) {
                        iC++;
                        wC++;
                    } else if (inputChanTiles == 1) {
                        wC++;
                    } else {
                        iC++;
                    }
                }
            }
        }
    }
}

void SmvConvolutionOp::run() {
    using namespace smaug::smv::conv;
    auto input = getInput<SmvBackend>(Inputs);
    auto kernels = getInput<SmvBackend>(Kernels);
    auto output = getOutput<SmvBackend>(Outputs);
    const TensorShape& inputShape = input->getShape();
    const TensorShape& kernelShape = kernels->getShape();
    const TensorShape& outputShape = output->getShape();
    assert(inputShape.getLayout() == DataLayout::NHWC);
    assert(kernelShape.getLayout() == DataLayout::NHWC);
    assert(outputShape.getLayout() == DataLayout::NHWC);
    dout(2) << *kernels << "\n";

    TilingConfig tileShapes = TilingOptimizer::computeBasicTileShapes(this);
    std::vector<int> inputHalos{ 0, weightRows / 2, weightCols / 2, 0 };
    SmvTiledTensor tiledInputs = TilingOptimizer::generateTiledTensor(
            input, tileShapes.inputs, inputHalos);
    SmvTiledTensor tiledWeights = TilingOptimizer::generateTiledTensor(
            kernels, tileShapes.weights, { 0, 0, 0, 0 });
    SmvTiledTensor tiledOutputs = TilingOptimizer::generateTiledTensor(
            output, tileShapes.outputs, { 0, 0, 0, 0 });

    runNHWC(tiledInputs, tiledWeights, tiledOutputs);
}

}  // namespace smaug