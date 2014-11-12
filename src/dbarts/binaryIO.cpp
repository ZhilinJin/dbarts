#include "binaryIO.hpp"

#include <cstddef>
#include <dbarts/cstdint.hpp>
#include <cstring>
#include <cerrno>

#include <external/alloca.h>
#include <external/binaryIO.h>
#include <external/io.h>

#include <dbarts/bartFit.hpp>
#include <dbarts/control.hpp>
#include <dbarts/data.hpp>
#include <dbarts/endNodeModel.hpp>
#include <dbarts/model.hpp>
#include <dbarts/state.hpp>

#include "tree.hpp"

namespace dbarts {
#define CONTROL_BINARY_RESPONSE 1
#define CONTROL_VERBOSE         2
#define CONTROL_KEEP_TRAINING   4
#define CONTROL_USE_QUANTILES   8
  
  bool writeControl(const Control& control, ext_binaryIO* bio) {
    int errorCode = 0;
    
    uint32_t controlFlags = 0;
    controlFlags += control.responseIsBinary ? CONTROL_BINARY_RESPONSE : 0;
    controlFlags += control.verbose ? CONTROL_VERBOSE : 0;
    controlFlags += control.keepTrainingFits ? CONTROL_KEEP_TRAINING : 0;
    controlFlags += control.useQuantiles ? CONTROL_USE_QUANTILES : 0;
    
    if ((errorCode = ext_bio_writeUnsigned32BitInteger(bio, controlFlags)) != 0) goto write_control_cleanup;
    
    if ((errorCode = ext_bio_writeSizeType(bio, control.numSamples)) != 0) goto write_control_cleanup;
    if ((errorCode = ext_bio_writeSizeType(bio, control.numBurnIn)) != 0) goto write_control_cleanup;
    if ((errorCode = ext_bio_writeSizeType(bio, control.numTrees)) != 0) goto write_control_cleanup;
    if ((errorCode = ext_bio_writeSizeType(bio, control.numThreads)) != 0) goto write_control_cleanup;
    
    if ((errorCode = ext_bio_writeUnsigned32BitInteger(bio, control.treeThinningRate)) != 0) goto write_control_cleanup;
    if ((errorCode = ext_bio_writeUnsigned32BitInteger(bio, control.printEvery)) != 0) goto write_control_cleanup;
    if ((errorCode = ext_bio_writeUnsigned32BitInteger(bio, control.printCutoffs)) != 0) goto write_control_cleanup;
    
write_control_cleanup:
    
    if (errorCode != 0) ext_issueWarning("error writing control object: %s", std::strerror(errorCode));
    
    return errorCode == 0;
  }
  
  bool readControl(Control& control, ext_binaryIO* bio) {
    int errorCode = 0;
    
    uint32_t controlFlags = 0;
    controlFlags += control.responseIsBinary ? CONTROL_BINARY_RESPONSE : 0;
    controlFlags += control.verbose ? CONTROL_VERBOSE : 0;
    controlFlags += control.keepTrainingFits ? CONTROL_KEEP_TRAINING : 0;
    controlFlags += control.useQuantiles ? CONTROL_USE_QUANTILES : 0;
    
    if ((errorCode = ext_bio_readUnsigned32BitInteger(bio, &controlFlags)) != 0) goto read_control_cleanup;
    control.responseIsBinary = (controlFlags & CONTROL_BINARY_RESPONSE) != 0;
    control.verbose = (controlFlags & CONTROL_VERBOSE) != 0;
    control.keepTrainingFits = (controlFlags & CONTROL_KEEP_TRAINING) != 0;
    control.useQuantiles = (controlFlags & CONTROL_USE_QUANTILES) != 0;
    
    if ((errorCode = ext_bio_readSizeType(bio, &control.numSamples)) != 0) goto read_control_cleanup;
    if ((errorCode = ext_bio_readSizeType(bio, &control.numBurnIn)) != 0) goto read_control_cleanup;
    if ((errorCode = ext_bio_readSizeType(bio, &control.numTrees)) != 0) goto read_control_cleanup;
    if ((errorCode = ext_bio_readSizeType(bio, &control.numThreads)) != 0) goto read_control_cleanup;
    
    if ((errorCode = ext_bio_readUnsigned32BitInteger(bio, &control.treeThinningRate)) != 0) goto read_control_cleanup;
    if ((errorCode = ext_bio_readUnsigned32BitInteger(bio, &control.printEvery)) != 0) goto read_control_cleanup;
    if ((errorCode = ext_bio_readUnsigned32BitInteger(bio, &control.printCutoffs)) != 0) goto read_control_cleanup;
    
    control.callback = NULL;
    control.callbackData = NULL;
    
read_control_cleanup:
    
    if (errorCode != 0) ext_issueWarning("error reading control object: %s", std::strerror(errorCode));

    return errorCode == 0;
  }
  
#define DATA_HAS_WEIGHTS      1
#define DATA_HAS_OFFSET       2
#define DATA_HAS_TEST_OFFSET  4
#define DATA_HAS_MAX_NUM_CUTS 8
    
  bool writeData(const Data& data, ext_binaryIO* bio) {
    int errorCode = 0;
    uint32_t* variableTypes = NULL;
    
    uint32_t dataFlags = 0;
    dataFlags |= ((data.weights != NULL) ? DATA_HAS_WEIGHTS : 0);
    dataFlags |= ((data.offset != NULL) ?  DATA_HAS_OFFSET : 0);
    dataFlags |= ((data.testOffset != NULL) ? DATA_HAS_TEST_OFFSET : 0);
    dataFlags |= ((data.maxNumCuts != NULL) ? DATA_HAS_MAX_NUM_CUTS : 0);
    
    if ((errorCode = ext_bio_writeUnsigned32BitInteger(bio, dataFlags)) != 0) goto write_data_cleanup;
    
    if ((errorCode = ext_bio_writeSizeType(bio, data.numObservations)) != 0) goto write_data_cleanup;
    if ((errorCode = ext_bio_writeSizeType(bio, data.numPredictors)) != 0) goto write_data_cleanup;
    if ((errorCode = ext_bio_writeSizeType(bio, data.numTestObservations)) != 0) goto write_data_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, data.sigmaEstimate)) != 0) goto write_data_cleanup;
    
    if ((errorCode = ext_bio_writeNDoubles(bio, data.y, data.numObservations)) != 0) goto write_data_cleanup;
    if ((errorCode = ext_bio_writeNDoubles(bio, data.X, data.numObservations * data.numPredictors)) != 0) goto write_data_cleanup;
    if (data.numTestObservations > 0 &&
      (errorCode = ext_bio_writeNDoubles(bio, data.X_test, data.numTestObservations * data.numPredictors)) != 0) goto write_data_cleanup;
    
    if (data.weights != NULL && 
      (errorCode = ext_bio_writeNDoubles(bio, data.weights, data.numObservations)) != 0) goto write_data_cleanup;
    if (data.offset != NULL && 
      (errorCode = ext_bio_writeNDoubles(bio, data.offset, data.numObservations)) != 0) goto write_data_cleanup;
    if (data.testOffset != NULL && 
      (errorCode = ext_bio_writeNDoubles(bio, data.testOffset, data.numTestObservations)) != 0) goto write_data_cleanup;
    
    variableTypes = ext_stackAllocate(data.numPredictors, uint32_t);
    for (size_t i = 0; i < data.numPredictors; ++i) variableTypes[i] = static_cast<uint32_t>(data.variableTypes[i]);
    
    if ((errorCode = ext_bio_writeNUnsigned32BitIntegers(bio, variableTypes, data.numPredictors)) != 0) goto write_data_cleanup;
    
    errorCode = ext_bio_writeNUnsigned32BitIntegers(bio, data.maxNumCuts, data.numPredictors);
    
write_data_cleanup:
    if (variableTypes != NULL) { ext_stackFree(variableTypes); }

    if (errorCode != 0) ext_issueWarning("error writing data object: %s", std::strerror(errorCode));
    
    return errorCode == 0;
  }
  
  bool readData(Data& data, ext_binaryIO* bio) {
    int errorCode = 0;
    uint32_t* variableTypes = NULL;
    
    uint32_t dataFlags = 0;
    
    if ((errorCode = ext_bio_readNUnsigned32BitIntegers(bio, &dataFlags, 1)) != 0) goto read_data_cleanup;
    
    if ((errorCode = ext_bio_readSizeType(bio, &data.numObservations)) != 0) goto read_data_cleanup;
    if ((errorCode = ext_bio_readSizeType(bio, &data.numPredictors)) != 0) goto read_data_cleanup;
    if ((errorCode = ext_bio_readSizeType(bio, &data.numTestObservations)) != 0) goto read_data_cleanup;
    if ((errorCode = ext_bio_readDouble(bio, &data.sigmaEstimate)) != 0) goto read_data_cleanup;
    
    
    data.y = new double[data.numObservations];
    if ((errorCode = ext_bio_readNDoubles(bio, const_cast<double*>(data.y), data.numObservations)) != 0) goto read_data_cleanup;
    
    data.X = new double[data.numObservations * data.numPredictors];
    if ((errorCode = ext_bio_readNDoubles(bio, const_cast<double*>(data.X), data.numObservations * data.numPredictors)) != 0) goto read_data_cleanup;
    
    if (data.numTestObservations > 0) {
      data.X_test = new double[data.numTestObservations * data.numPredictors];
      if ((errorCode = ext_bio_readNDoubles(bio, const_cast<double*>(data.X_test), data.numTestObservations * data.numPredictors)) != 0) goto read_data_cleanup;
    } else data.X_test = NULL;
    
    if (dataFlags & DATA_HAS_WEIGHTS) {
      data.weights = new double[data.numObservations];
      if ((errorCode = ext_bio_readNDoubles(bio, const_cast<double*>(data.weights), data.numObservations)) != 0) goto read_data_cleanup;
    } else data.weights = NULL;
    
    if (dataFlags & DATA_HAS_OFFSET) {
      data.offset = new double[data.numObservations];
      if ((errorCode = ext_bio_readNDoubles(bio, const_cast<double*>(data.offset), data.numObservations)) != 0) goto read_data_cleanup;
    } else data.offset = NULL;
    
    if (dataFlags & DATA_HAS_TEST_OFFSET) {
      data.testOffset = new double[data.numTestObservations];
      if ((errorCode = ext_bio_readNDoubles(bio, const_cast<double*>(data.testOffset), data.numTestObservations)) != 0) goto read_data_cleanup;
    } else data.testOffset = NULL;
    
    variableTypes = ext_stackAllocate(data.numPredictors, uint32_t);
    if ((errorCode = ext_bio_readNUnsigned32BitIntegers(bio, variableTypes, data.numPredictors)) != 0) goto read_data_cleanup;
    data.variableTypes = new VariableType[data.numPredictors];
    for (size_t i = 0; i < data.numPredictors; ++i) const_cast<VariableType*>(data.variableTypes)[i] = static_cast<VariableType>(variableTypes[i]);
    
    if (dataFlags & DATA_HAS_MAX_NUM_CUTS) {
      data.maxNumCuts = new uint32_t[data.numPredictors];
      if ((errorCode = ext_bio_readNUnsigned32BitIntegers(bio, const_cast<uint32_t*>(data.maxNumCuts), data.numPredictors)) != 0) goto read_data_cleanup; 
    } else data.maxNumCuts = NULL;
    
read_data_cleanup:
    if (variableTypes != NULL) { ext_stackFree(variableTypes); }
      
    if (errorCode != 0) {
      delete [] data.maxNumCuts;
      delete [] data.variableTypes;
      delete [] data.testOffset;
      delete [] data.offset;
      delete [] data.weights;
      delete [] data.X_test;
      delete [] data.X;
      delete [] data.y;
    
      ext_issueWarning("error reading data object: %s", std::strerror(errorCode));
    }
    
    return errorCode == 0;
  }
  
  bool writeModel(const Model& model, ext_binaryIO* bio)
  {
    int errorCode = 0;
    
    if ((errorCode = ext_bio_writeDouble(bio, model.birthOrDeathProbability)) != 0) goto write_model_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, model.swapProbability)) != 0) goto write_model_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, model.changeProbability)) != 0) goto write_model_cleanup;
    
    if ((errorCode = ext_bio_writeDouble(bio, model.birthProbability)) != 0) goto write_model_cleanup;
    
    // this needs some seeerious work
    if ((errorCode = ext_bio_writeNChars(bio, "cgm ", 4)) != 0) goto write_model_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, static_cast<CGMPrior*>(model.treePrior)->base)) != 0) goto write_model_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, static_cast<CGMPrior*>(model.treePrior)->power)) != 0) goto write_model_cleanup;
    
    
    if ((errorCode = ext_bio_writeNChars(bio, "nrml", 4)) != 0) goto write_model_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, static_cast<EndNode::MeanNormalModel*>(model.endNodeModel)->precision)) != 0) goto write_model_cleanup;
    
    if ((errorCode = ext_bio_writeNChars(bio, "chsq", 4)) != 0) goto write_model_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, static_cast<ChiSquaredPrior*>(model.sigmaSqPrior)->degreesOfFreedom)) != 0) goto write_model_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, static_cast<ChiSquaredPrior*>(model.sigmaSqPrior)->scale)) != 0) goto write_model_cleanup;
    
write_model_cleanup:
    if (errorCode != 0) ext_issueWarning("error writing model object: %s", std::strerror(errorCode));
    
    return errorCode == 0;
  }
  
  bool readModel(Model& model, ext_binaryIO* bio)
  {
    int errorCode = 0;
    char priorName[4];
    
    if ((errorCode = ext_bio_readDouble(bio, &model.birthOrDeathProbability)) != 0) goto read_model_cleanup;
    if ((errorCode = ext_bio_readDouble(bio, &model.swapProbability)) != 0) goto read_model_cleanup;
    if ((errorCode = ext_bio_readDouble(bio, &model.changeProbability)) != 0) goto read_model_cleanup;
    
    if ((errorCode = ext_bio_readDouble(bio, &model.birthProbability)) != 0) goto read_model_cleanup;
    
    if ((errorCode = ext_bio_readNChars(bio, priorName, 4)) != 0) goto read_model_cleanup;
    if (strncmp(priorName, "cgm ", 4) != 0) { errorCode = EILSEQ; goto read_model_cleanup; }
    
    model.treePrior = new CGMPrior;
    if ((errorCode = ext_bio_readDouble(bio, &static_cast<CGMPrior*>(model.treePrior)->base)) != 0) goto read_model_cleanup;
    if ((errorCode = ext_bio_readDouble(bio, &static_cast<CGMPrior*>(model.treePrior)->power)) != 0) goto read_model_cleanup;
    
    
    if ((errorCode = ext_bio_readNChars(bio, priorName, 4)) != 0) goto read_model_cleanup;
    if (strncmp(priorName, "nrml", 4) != 0) { errorCode = EILSEQ; goto read_model_cleanup; }
    
    model.endNodeModel = EndNode::createMeanNormalModel();
    if ((errorCode = ext_bio_readDouble(bio, &static_cast<EndNode::MeanNormalModel*>(model.endNodeModel)->precision)) != 0) goto read_model_cleanup;
    
    
    if ((errorCode = ext_bio_readNChars(bio, priorName, 4)) != 0) goto read_model_cleanup;
    if (strncmp(priorName, "chsq", 4) != 0) { errorCode = EILSEQ; goto read_model_cleanup; }
    
    model.sigmaSqPrior = new ChiSquaredPrior;
    if ((errorCode = ext_bio_readDouble(bio, &static_cast<ChiSquaredPrior*>(model.sigmaSqPrior)->degreesOfFreedom)) != 0) goto read_model_cleanup;
    if ((errorCode = ext_bio_readDouble(bio, &static_cast<ChiSquaredPrior*>(model.sigmaSqPrior)->scale)) != 0) goto read_model_cleanup;
    
read_model_cleanup:
    
    if (errorCode != 0) {
      delete model.sigmaSqPrior;
      delete model.endNodeModel;
      delete model.treePrior;
      
      ext_issueWarning("error reading model object: %s", std::strerror(errorCode));
    }
    
    return errorCode == 0;
  }
}

/* namespace {
  using namespace dbarts;
  
  int writeNode(const BARTFit& fit, const Node& node, ext_binaryIO* bio, const size_t* treeIndices);
  int readNode(const BARTFit& fit, Node& node, ext_binaryIO* bio, const size_t* treeIndices);
} */

namespace dbarts {
  bool writeState(const BARTFit& fit, ext_binaryIO* bio)
  {
    int errorCode = 0;
    
    const State& state(fit.state);
    const Control& control(fit.control);
    const Data& data(fit.data);
    
    if ((errorCode = ext_bio_writeNSizeTypes(bio, state.treeIndices, data.numObservations * control.numTrees)) != 0) goto write_state_cleanup;
        
    for (size_t i = 0; i < control.numTrees; ++i) {
      if ((errorCode = TREE_AT(state.trees, i, fit.scratch.nodeSize)->write(fit, bio)) != 0) goto write_state_cleanup;
      // if ((errorCode = writeNode(fit, *NODE_AT(state.trees, i, fit.scratch.nodeSize), bio, state.treeIndices + i * data.numObservations)) != 0) goto write_state_cleanup;
    }
    
    if ((errorCode = ext_bio_writeNDoubles(bio, state.treeFits, data.numObservations * control.numTrees)) != 0) goto write_state_cleanup;
    if ((errorCode = ext_bio_writeNDoubles(bio, state.totalFits, data.numObservations)) != 0) goto write_state_cleanup;
    if (data.numTestObservations > 0 &&
      (errorCode = ext_bio_writeNDoubles(bio, state.totalTestFits, data.numTestObservations)) != 0) goto write_state_cleanup;
    
    if ((errorCode = ext_bio_writeDouble(bio, state.sigma)) != 0) goto write_state_cleanup;
    if ((errorCode = ext_bio_writeDouble(bio, state.runningTime)) != 0) goto write_state_cleanup;
    
write_state_cleanup:
    if (errorCode != 0) ext_issueWarning("error writing state object: %s", std::strerror(errorCode));
    
    return errorCode == 0;
  }
  
  bool readState(BARTFit& fit, ext_binaryIO* bio)
  {
    int errorCode = 0;
    
    State& state(fit.state);
    const Control& control(fit.control);
    const Data& data(fit.data);
    
    if ((errorCode = ext_bio_readNSizeTypes(bio, state.treeIndices, data.numObservations * control.numTrees)) != 0) goto read_state_cleanup;
    
    for (size_t i = 0; i < control.numTrees; ++i) {
      
      // NODE_AT(state.trees, i, fit.scratch.nodeSize)->observationIndices = state.treeIndices + i * data.numObservations;
      if ((errorCode = TREE_AT(state.trees, i, fit.scratch.nodeSize)->read(fit, bio)) != 0) goto read_state_cleanup;
      // if ((errorCode = readNode(fit, *NODE_AT(state.trees, i, fit.scratch.nodeSize), bio, state.treeIndices + i * data.numObservations)) != 0) goto read_state_cleanup;
    }
    
    if ((errorCode = ext_bio_readNDoubles(bio, state.treeFits, data.numObservations * control.numTrees)) != 0) goto read_state_cleanup;
    if ((errorCode = ext_bio_readNDoubles(bio, state.totalFits, data.numObservations)) != 0) goto read_state_cleanup;
    
    if (data.numTestObservations > 0) {
      if ((errorCode = ext_bio_readNDoubles(bio, state.totalTestFits, data.numTestObservations)) != 0) goto read_state_cleanup;
    }
    
    if ((errorCode = ext_bio_readDouble(bio, &state.sigma)) != 0) goto read_state_cleanup;
    if ((errorCode = ext_bio_readDouble(bio, &state.runningTime)) != 0) goto read_state_cleanup;
    
read_state_cleanup:
    if (errorCode != 0) ext_issueWarning("error reading state object: %s", std::strerror(errorCode));
    
    return errorCode == 0;
  }
}

/*
#define NODE_HAS_CHILDREN 1
namespace {
  using namespace dbarts;
  
  int writeNode(const BARTFit& fit, const Node& node, ext_binaryIO* bio, const size_t* treeIndices)
  {
    int errorCode = 0;
    
    const Data& data(fit.data);
    
    ptrdiff_t observationOffset = 0;
    unsigned char nodeFlags = 0;
    uint64_t variablesAvailableForSplit = 0;
    
    observationOffset = node.observationIndices - treeIndices;
    if (observationOffset < 0) {
      errorCode = EINVAL; goto write_node_cleanup;
    } else if ((errorCode = ext_bio_writeSizeType(bio, static_cast<size_t>(observationOffset))) != 0) goto write_node_cleanup;
    
    if ((errorCode = ext_bio_writeSizeType(bio, node.enumerationIndex)) != 0) goto write_node_cleanup;
    if ((errorCode = ext_bio_writeSizeType(bio, node.numObservations)) != 0) goto write_node_cleanup;
    
    for (size_t i = 0; i < data.numPredictors; ++i) {
      if (node.variablesAvailableForSplit[i] == true) variablesAvailableForSplit |= 1 << i;
    }
    if ((errorCode = ext_bio_writeUnsigned64BitInteger(bio, variablesAvailableForSplit)) != 0) goto write_node_cleanup;
    
    if (node.leftChild != NULL) {
      nodeFlags += NODE_HAS_CHILDREN;
      
      if ((errorCode = ext_bio_writeChar(bio, *reinterpret_cast<char*>(&nodeFlags))) != 0) goto write_node_cleanup;
      
      if ((errorCode = ext_bio_writeUnsigned32BitInteger(bio, *(reinterpret_cast<uint32_t*>(&node.p.rule.variableIndex)))) != 0) goto write_node_cleanup;
      if ((errorCode = ext_bio_writeUnsigned32BitInteger(bio, node.p.rule.categoryDirections)) != 0) goto write_node_cleanup;
      
      if ((errorCode = writeNode(fit, *node.leftChild, bio, treeIndices))) goto write_node_cleanup;
      if ((errorCode = writeNode(fit, *node.p.rightChild, bio, treeIndices))) goto write_node_cleanup;
    } else {
      if ((errorCode = ext_bio_writeChar(bio, *reinterpret_cast<char*>(&nodeFlags))) != 0) goto write_node_cleanup;
      
      if ((errorCode = fit.model.endNodeModel->writeScratch(node, bio)) != 0) goto write_node_cleanup;
    }
    
write_node_cleanup:
    
    return errorCode;
  }
  
  int readNode(const BARTFit& fit, Node& node, ext_binaryIO* bio, const size_t* treeIndices)
  {
    int errorCode = 0;
    
    const Data& data(fit.data);
    
    size_t observationOffset = 0;
    unsigned char nodeFlags = 0;
    uint64_t variablesAvailableForSplit = 0;
    dbarts::Node* leftChild = NULL;
    dbarts::Node* rightChild = NULL;
    
    if ((errorCode = ext_bio_readSizeType(bio, &observationOffset)) != 0) goto read_node_cleanup;
    if (observationOffset >= data.numObservations) { errorCode = EINVAL; goto read_node_cleanup; }
    node.observationIndices = const_cast<size_t*>(treeIndices) + observationOffset;
    
    if ((errorCode = ext_bio_readSizeType(bio, &node.enumerationIndex)) != 0) goto read_node_cleanup;
    if ((errorCode = ext_bio_readSizeType(bio, &node.numObservations)) != 0) goto read_node_cleanup;
    
    if ((errorCode = ext_bio_readUnsigned64BitInteger(bio, &variablesAvailableForSplit)) != 0) goto read_node_cleanup;
    for (size_t i = 0; i < data.numPredictors; ++i) {
      node.variablesAvailableForSplit[i] = (variablesAvailableForSplit & (1 << i)) != 0;
    }
    
    if ((errorCode = ext_bio_readChar(bio, reinterpret_cast<char*>(&nodeFlags))) != 0) goto read_node_cleanup;
    
    if (nodeFlags > NODE_HAS_CHILDREN) { errorCode = EINVAL; goto read_node_cleanup; }
    
    if (nodeFlags & NODE_HAS_CHILDREN) {
      if ((errorCode = ext_bio_readUnsigned32BitInteger(bio, reinterpret_cast<uint32_t*>(&node.p.rule.variableIndex))) != 0) goto read_node_cleanup;
      if ((errorCode = ext_bio_readUnsigned32BitInteger(bio, &node.p.rule.categoryDirections)) != 0) goto read_node_cleanup;
      
      leftChild = dbarts::Node::create(fit, node);
      node.leftChild = leftChild;
      if ((errorCode = readNode(fit, *leftChild, bio, treeIndices)) != 0) goto read_node_cleanup;
      
      rightChild = dbarts::Node::create(fit, node);
      node.p.rightChild = rightChild;
      if ((errorCode = readNode(fit, *rightChild, bio, treeIndices)) != 0) goto read_node_cleanup;
    } else {
      node.leftChild = NULL;
      
      if ((errorCode = fit.model.endNodeModel->readScratch(node, bio)) != 0) goto read_node_cleanup;
    }
    
read_node_cleanup:

    if (errorCode != 0) {
      delete rightChild;
      delete leftChild;
      
      node.leftChild = NULL;
    }
    
    return errorCode;
  }
}
*/
