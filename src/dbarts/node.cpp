#include "config.hpp"
#include "node.hpp"

#include <cstring>    // memcpy
#include <algorithm>  // int max

#include <misc/alloca.h>
#include <external/io.h>
#include <misc/linearAlgebra.h>
#include <misc/stats.h>
#include <misc/partition.h>

#include <dbarts/bartFit.hpp>
#include <dbarts/data.hpp>
#include <dbarts/model.hpp>
#include <dbarts/scratch.hpp>
#include "functions.hpp"

using std::uint64_t;
using std::size_t;

namespace dbarts {
  
  double Rule::getSplitValue(const BARTFit& fit) const
  {
    if (variableIndex < 0) return -1000.0;
    if (fit.data.variableTypes[variableIndex] != ORDINAL) return -2000.0;
    
    return fit.sharedScratch.cutPoints[variableIndex][splitIndex];
  }
  
  void Rule::invalidate() {
    variableIndex = DBARTS_INVALID_RULE_VARIABLE;
    splitIndex = DBARTS_INVALID_RULE_VARIABLE;
  }
  
  bool Rule::goesRight(const BARTFit& fit, const xint_t* xt) const
  {
    if (fit.data.variableTypes[variableIndex] == CATEGORICAL) {
      // x is a double, but that is 64 bits wide, and as such we can treat it as
      // a 64 bit integer
      //uint32_t categoryId = static_cast<uint32_t>(*(reinterpret_cast<const uint64_t*>(x + variableIndex)));
      uint32_t categoryId = static_cast<uint32_t>(*(reinterpret_cast<const xint_t*>(xt + variableIndex)));
      
      return categoryGoesRight(categoryId);
    } else {
      return xt[variableIndex] > splitIndex; 
    }
  }
  
  void Rule::copyFrom(const Rule& other)
  {
    if (other.variableIndex == DBARTS_INVALID_RULE_VARIABLE) {
      variableIndex = DBARTS_INVALID_RULE_VARIABLE;
      splitIndex    = DBARTS_INVALID_RULE_VARIABLE;
      return;
    }
    
    variableIndex = other.variableIndex;
    splitIndex    = other.splitIndex;
  }
  
  void Rule::swapWith(Rule& other)
  {
    Rule temp(other);
    other = *this;
    *this = temp;
  }

  bool Rule::equals(const Rule& other) const {
    if (variableIndex != other.variableIndex) return false;
    
    // since is a union of variables of the same width, bit-wise equality is sufficient
    return splitIndex == other.splitIndex;
  }
  
  
  void Node::clearObservations()
  {
    if (!isTop()) {
      observationIndices = NULL;
      numObservations = 0;
    }
    if (!isBottom()) {
      leftChild->clearObservations();
      p.rightChild->clearObservations();
    } else {
      m.average = 0.0;
    }
  }
  
  void Node::clear()
  {
    if (!isBottom()) {
      delete leftChild;
      delete p.rightChild;
      
      leftChild = NULL;
      p.rule.invalidate();
    }
    clearObservations();
  }
  
  Node::Node(size_t* observationIndices, size_t numObservations, size_t numPredictors) :
    parent(NULL), leftChild(NULL), enumerationIndex(BART_INVALID_NODE_ENUM), variablesAvailableForSplit(NULL),
    observationIndices(observationIndices), numObservations(numObservations)
  {
    variablesAvailableForSplit = new bool[numPredictors];
    for (size_t i = 0; i < numPredictors; ++i) variablesAvailableForSplit[i] = true;
  }
  
  Node::Node(const Node& parent, size_t numPredictors, const Node& other) :
    parent(const_cast<Node*>(&parent)), leftChild(NULL), enumerationIndex(other.enumerationIndex), variablesAvailableForSplit(NULL),
    observationIndices(NULL), numObservations(other.numObservations)
  {
    variablesAvailableForSplit = new bool[numPredictors];
    
    observationIndices = const_cast<size_t*>(parent.observationIndices) + (other.observationIndices - other.parent->observationIndices);
    
    if (other.leftChild != NULL) {
      leftChild = new Node(*this, numPredictors, *other.leftChild);
      p.rightChild = new Node(*this, numPredictors, *other.p.rightChild);
      p.rule.copyFrom(other.p.rule);
    } else {
      m.average = other.m.average;
      m.numEffectiveObservations = other.m.numEffectiveObservations;
    }
    
    std::memcpy(variablesAvailableForSplit, other.variablesAvailableForSplit, sizeof(bool) * numPredictors);
  }
  
  void Node::checkIndices(const BARTFit& fit, const Node& top) {
    if (&top != this) {
      ptrdiff_t offset = observationIndices - top.observationIndices;
      if (offset < 0 || offset > static_cast<ptrdiff_t>(fit.data.numObservations))
        ext_throwError("observationIndices out of range");
      if (numObservations > fit.data.numObservations)
        ext_throwError("num observations greater than data");
      for (size_t i = 0; i < numObservations; ++i)
        if (observationIndices[i] > fit.data.numObservations)
          ext_throwError("observation index at %lu out of range (%lu)", i, observationIndices[i]);
    }
    if (leftChild != NULL) {
      leftChild->checkIndices(fit, top);
      p.rightChild->checkIndices(fit, top);
    }
  }

  
  Node::Node(const Node& parent, size_t numPredictors) :
    parent(const_cast<Node*>(&parent)), leftChild(NULL), enumerationIndex(BART_INVALID_NODE_ENUM),
    variablesAvailableForSplit(NULL), observationIndices(NULL), numObservations(0)
  {
    variablesAvailableForSplit = new bool[numPredictors];
    std::memcpy(variablesAvailableForSplit, this->parent->variablesAvailableForSplit, sizeof(bool) * numPredictors);
  }
  
  Node::~Node()
  {
    if (leftChild != NULL) {
      delete leftChild; leftChild = NULL;
      delete p.rightChild; p.rightChild = NULL;
    }
    delete [] variablesAvailableForSplit; variablesAvailableForSplit = NULL;
  }
    
  void Node::copyFrom(const BARTFit& fit, const Node& other)
  {
    parent = other.parent;
    leftChild = other.leftChild;
    if (leftChild != NULL) {
      p.rightChild = other.p.rightChild;
      p.rule.copyFrom(other.p.rule);
    }
    else {
      m.average = other.m.average;
      m.numEffectiveObservations = other.m.numEffectiveObservations;
    }
    
    enumerationIndex = other.enumerationIndex;
    std::memcpy(variablesAvailableForSplit, other.variablesAvailableForSplit, sizeof(bool) * fit.data.numPredictors);
    
    observationIndices = other.observationIndices;
    numObservations = other.numObservations;
  }
  
  void Node::print(const BARTFit& fit, size_t indentation) const
  {
    ext_printf("%*s", indentation + getDepth(), "");
    ext_printf("n: %lu ", getNumObservations());
    ext_printf("TBN: %u%u%u ", isTop(), isBottom(), childrenAreBottom());
    ext_printf("Avail: ");
    
    for (size_t i = 0; i < fit.data.numPredictors; ++i) ext_printf("%u", variablesAvailableForSplit[i]);
    
    if (!isBottom()) {
      ext_printf(" var: %d ", p.rule.variableIndex);
      
      if (fit.data.variableTypes[p.rule.variableIndex] == CATEGORICAL) {
        ext_printf("CATRule: ");
        for (size_t i = 0; 0 < fit.sharedScratch.numCutsPerVariable[p.rule.variableIndex]; ++i) ext_printf(" %u", (p.rule.categoryDirections >> i) & 1);
      } else {
        ext_printf("ORDRule: (%d)=%f", p.rule.splitIndex, p.rule.getSplitValue(fit));
      }
    } else {
      ext_printf(" ave: %f", m.average);
    }
    ext_printf("\n");
    
    if (!isBottom()) {
      leftChild->print(fit, indentation);
      p.rightChild->print(fit, indentation);
    }
  }
  
  size_t Node::getNumBottomNodes() const
  {
    if (isBottom()) {
      return 1;
    } else {
      return leftChild->getNumBottomNodes() + p.rightChild->getNumBottomNodes();
    }
  }
  
  size_t Node::getNumNotBottomNodes() const
  {
    if (isBottom()) return 0;
    
    return leftChild->getNumNotBottomNodes() + p.rightChild->getNumNotBottomNodes() + 1;
  }
  
  size_t Node::getNumNoGrandNodes() const
  {
    if (isBottom()) return 0;
    if (childrenAreBottom()) return 1;
    return (leftChild->getNumNoGrandNodes() + p.rightChild->getNumNoGrandNodes());
  }
  
  size_t Node::getNumSwappableNodes() const
  {
    if (isBottom() || childrenAreBottom()) return 0;
    if ((leftChild->isBottom()  || leftChild->childrenAreBottom()) &&
        (p.rightChild->isBottom() || p.rightChild->childrenAreBottom())) return 1;
    
    return (leftChild->getNumSwappableNodes() + p.rightChild->getNumSwappableNodes() + 1);
  }
}

// NOTE: below assumes that walk tree on left
namespace {
  using namespace dbarts;
  
  void fillBottomVector(const Node& node, NodeVector& result)
  {
    if (node.isBottom()) {
      result.push_back(const_cast<Node*>(&node));
      return;
    }
    
    fillBottomVector(*node.leftChild, result);
    fillBottomVector(*node.p.rightChild, result);
  }

  void enumerateBottomNodes(Node& node, size_t& index)
  {
    if (node.isBottom()) {
      node.enumerationIndex = index++;
      return;
    }
    
    enumerateBottomNodes(*node.getLeftChild(), index);
    enumerateBottomNodes(*node.getRightChild(), index);
  }
  
  void fillAndEnumerateBottomVector(Node& node, NodeVector& result, size_t& index)
  {
    if (node.isBottom()) {
      result.push_back(&node);
      node.enumerationIndex = index++;
      return;
    }
    
    fillAndEnumerateBottomVector(*node.getLeftChild(), result, index);
    fillAndEnumerateBottomVector(*node.getRightChild(), result, index);
  }
  
  void fillNoGrandVector(const Node& node, NodeVector& result)
  {
    if (node.isBottom()) return;
    if (node.childrenAreBottom()) {
      result.push_back(const_cast<Node*>(&node));
      return;
    }

    fillNoGrandVector(*node.getLeftChild(), result);
    fillNoGrandVector(*node.getRightChild(), result);
  }
  
  void fillNotBottomVector(const Node& node, NodeVector& result)
  {
    if (node.isBottom()) return;
    if (node.childrenAreBottom()) {
      result.push_back(const_cast<Node*>(&node));
      return;
    }
  
    fillNotBottomVector(*node.leftChild, result);
    fillNotBottomVector(*node.p.rightChild, result);
    
    result.push_back(const_cast<Node*>(&node));
  }
  
  void fillSwappableVector(const Node& node, NodeVector& result)
  {
    if (node.isBottom() || node.childrenAreBottom()) return;
    if ((node.leftChild->isBottom()  || node.leftChild->childrenAreBottom()) && 
        (node.p.rightChild->isBottom() || node.p.rightChild->childrenAreBottom())) {
      result.push_back(const_cast<Node*>(&node));
      return;
    }
    
    fillSwappableVector(*node.leftChild, result);
    fillSwappableVector(*node.p.rightChild, result);
    
    result.push_back(const_cast<Node*>(&node));
  }
}
namespace dbarts {
  NodeVector Node::getBottomVector() const
  {
    NodeVector result;
    fillBottomVector(*this, result);
    return result;
  }

  void Node::enumerateBottomNodes()
  {
    size_t index = 0;
    ::enumerateBottomNodes(*this, index);
   }
  
  NodeVector Node::getAndEnumerateBottomVector()
  {
    size_t index = 0;
    NodeVector result;
    fillAndEnumerateBottomVector(*this, result, index);
    return result;
  }
  
  NodeVector Node::getNoGrandVector() const
  {
    NodeVector result;
    fillNoGrandVector(*this, result);
    return result;
  }
  
  NodeVector Node::getNotBottomVector() const
  {
    NodeVector result;
    fillNotBottomVector(*this, result);
    return result;
  }
  
  NodeVector Node::getSwappableVector() const
  {
    NodeVector result;
    fillSwappableVector(*this, result);
    return result;
  }
  
  Node* Node::findBottomNode(const BARTFit& fit, const xint_t* xt) const
  {
    if (isBottom()) return const_cast<Node*>(this);
    
    if (xt[p.rule.variableIndex] > p.rule.splitIndex) return p.rightChild->findBottomNode(fit, xt);
    
    return leftChild->findBottomNode(fit, xt);
  }
}

#include <emmintrin.h>
#include <smmintrin.h>

#define _mm_cmpge_epu16(a, b) \
        _mm_cmpeq_epi16(_mm_max_epu16(a, b), a)

#define _mm_cmple_epu16(a, b) _mm_cmpge_epu16(b, a)

#define _mm_cmpgt_epu16(a, b) \
        _mm_xor_si128(_mm_cmple_epu16(a, b), _mm_set1_epi16(-1))

#define _mm_cmplt_epu16(a, b) _mm_cmpgt_epu16(b, a)

#  define countTrailingZeros(_X_) __builtin_ctz(_X_)

namespace {
  using namespace dbarts;

 #define getDataAt(_I_) x[_I_]

#define loadLHComp(_X_) \
  (values = _mm_set_epi16(getDataAt(_X_ + 7), \
                          getDataAt(_X_ + 6), \
                          getDataAt(_X_ + 5), \
                          getDataAt(_X_ + 4), \
                          getDataAt(_X_ + 3), \
                          getDataAt(_X_ + 2), \
                          getDataAt(_X_ + 1), \
                          getDataAt(_X_    )), \
    _mm_cmpgt_epu16(values, _mm_set1_epi16(static_cast<xint_t>(rule.splitIndex))))

#define loadRHComp(_X_) \
  (values = _mm_set_epi16(getDataAt(_X_ - 7), \
                          getDataAt(_X_ - 6), \
                          getDataAt(_X_ - 5), \
                          getDataAt(_X_ - 4), \
                          getDataAt(_X_ - 3), \
                          getDataAt(_X_ - 2), \
                          getDataAt(_X_ - 1), \
                          getDataAt(_X_    )), \
    _mm_cmple_epu16(values, _mm_set1_epi16(static_cast<xint_t>(rule.splitIndex))))
  
  // returns how many observations are on the "left"
  size_t partitionRange(const BARTFit& fit, const Rule& rule, size_t* restrict indices, size_t length) {
    return misc_partitionRange(fit.sharedScratch.x + rule.variableIndex * fit.data.numObservations,
                               rule.splitIndex, indices, length);
    /* size_t lengthOfLeft;
    
    size_t lh = 0, rh = length - 1;
    
    for (size_t i = 0; i < length; ++i) indices[i] = i;
    
    const xint_t* x = fit.sharedScratch.x + rule.variableIndex * fit.data.numObservations; */
    
    /* if (lh + 16 < rh) {
      
      __m128i lh_comp, rh_comp, values;
      uint8_t lh_sub = 0, rh_sub = 0;
      uint16_t lh_mask = 0, rh_mask = 0;
      
      lh_comp = loadLHComp(lh);
      lh_mask = _mm_movemask_epi8(lh_comp);
      rh_comp = loadRHComp(rh);
      rh_mask = _mm_movemask_epi8(rh_comp);
      
      while (true) {
        while (lh_mask == 0 && lh + 16 < rh) {
          lh += 8;
          lh_comp = loadLHComp(lh);
          lh_mask = _mm_movemask_epi8(lh_comp);
          lh_sub = 0;
        }
        while (rh_mask == 0 && lh + 16 < rh) {
          rh -= 8;
          rh_comp = loadRHComp(rh);
          rh_mask = _mm_movemask_epi8(rh_comp);
          rh_sub = 0;
        }
        if (lh + 16 >= rh) {
          lh += lh_sub;
          rh -= rh_sub;
          break;
        }
        
        do {
          int zeros = countTrailingZeros(lh_mask);
          lh_mask >>= zeros;
          lh_sub += zeros / 2;
          
          zeros = countTrailingZeros(rh_mask);
          rh_mask >>= zeros;
          rh_sub += zeros / 2;
          
          indices[rh - rh_sub] = lh + lh_sub;
          indices[lh + lh_sub] = rh - rh_sub;
                    
          lh_mask >>= 2;
          rh_mask >>= 2;
          ++lh_sub;
          ++rh_sub;
          
        } while (lh_mask != 0 && rh_mask != 0);
      }
    } */
    /* while (true) {
      while (x[lh] <= rule.splitIndex && lh < rh) ++lh;
      while (x[rh]  > rule.splitIndex && lh < rh) --rh;
      
      if (lh >= rh) break;
      
      indices[rh] = lh;
      indices[lh] = rh;
      
      ++lh;
      --rh;
    }
    
    lengthOfLeft = x[indices[lh]] <= rule.splitIndex ? lh + 1 : lh; */
    
    /* for (size_t i = 0; i < length; ++i) {
      if (indices[i] >= fit.data.numObservations)
        ext_throwError("partition range: index %lu out of range (%lu); lh: %lu, rh: %lu, lol: %lu", i, indices[i], lh, rh, lengthOfLeft);
      if (i < lengthOfLeft && x[indices[i]] > rule.splitIndex)
        ext_throwError("partition range: observation %lu on left but should be on right (%hu); lh: %lu, rh: %lu, lol: %lu", indices[i], x[indices[i]], lh, rh, lengthOfLeft);
      if (i >= lengthOfLeft && x[indices[i]] <= rule.splitIndex)
        ext_throwError("partition range: observation %lu on left but should be on right (%hu); lh: %lu, rh: %lu, lol: %lu", indices[i], x[indices[i]], lh, rh, lengthOfLeft);
    } */
    
    // return lengthOfLeft;
  }

#undef getDataAt

// #define getDataAt(_I_) fit.sharedScratch.xt[indices[_I_] * fit.data.numPredictors + rule.variableIndex]
#define getDataAt(_I_) x[indices[_I_]]

  size_t partitionIndices(const BARTFit& fit, const Rule& rule, size_t* restrict indices, size_t length) {
    return misc_partitionIndices(fit.sharedScratch.x + rule.variableIndex * fit.data.numObservations,
                                 rule.splitIndex, indices, length);
    /* if (length == 0) return 0;
    
    const xint_t* x = fit.sharedScratch.x + rule.variableIndex * fit.data.numObservations;
    
    size_t lengthOfLeft;
    
    size_t lh = 0, rh = length - 1; */
    
    /* if (lh + 16 < rh) {
      
      __m128i lh_comp, rh_comp, values;
      uint8_t lh_sub = 0, rh_sub = 0;
      uint16_t lh_mask = 0, rh_mask = 0;
      
      lh_comp = loadLHComp(lh);
      lh_mask = _mm_movemask_epi8(lh_comp);
      rh_comp = loadRHComp(rh);
      rh_mask = _mm_movemask_epi8(rh_comp);
      
      while (true) {
        while (lh_mask == 0 && lh + 16 < rh) {
          lh += 8;
          lh_comp = loadLHComp(lh);
          lh_mask = _mm_movemask_epi8(lh_comp);
          lh_sub = 0;
        }
        while (rh_mask == 0 && lh + 16 < rh) {
          rh -= 8;
          rh_comp = loadRHComp(rh);
          rh_mask = _mm_movemask_epi8(rh_comp);
          rh_sub = 0;
        }
        if (lh + 16 >= rh) {
          lh += lh_sub;
          rh -= rh_sub;
          break;
        }
        
        do {
          int zeros = countTrailingZeros(lh_mask);
          lh_mask >>= zeros;
          lh_sub += zeros / 2;
          
          zeros = countTrailingZeros(rh_mask);
          rh_mask >>= zeros;
          rh_sub += zeros / 2;
          
          size_t temp = indices[rh - rh_sub];
          indices[rh - rh_sub] = indices[lh + lh_sub];
          indices[lh + lh_sub] = temp;
          
          lh_mask >>= 2;
          rh_mask >>= 2;
          ++lh_sub;
          ++rh_sub;
        } while (lh_mask != 0 && rh_mask != 0);
      }
    } */
   /*  while (true) {
      while (x[indices[lh]] <= rule.splitIndex && lh < rh) ++lh;
      while (x[indices[rh]]  > rule.splitIndex && lh < rh) --rh;
      
      
      if (lh >= rh) break;
      
      size_t temp = indices[rh];
      indices[rh] = indices[lh];
      indices[lh] = temp;
      
      ++lh;
      --rh;
    }
    lengthOfLeft = x[indices[lh]] <= rule.splitIndex ? lh + 1 : lh; */
    
    /* for (size_t i = 0; i < length; ++i) {
      if (indices[i] >= fit.data.numObservations)
        ext_throwError("partition indices: index %lu out of range (%lu); lh: %lu, rh: %lu, lol: %lu", i, indices[i], lh, rh, lengthOfLeft);
      if (i < lengthOfLeft && x[indices[i]] > rule.splitIndex)
        ext_throwError("partition indices: observation %lu on left but should be on right (%hu); lh: %lu, rh: %lu, lol: %lu", indices[i], x[indices[i]], lh, rh, lengthOfLeft);
      if (i >= lengthOfLeft && x[indices[i]] <= rule.splitIndex)
        ext_throwError("partition indices: observation %lu on left but should be on right (%hu); lh: %lu, rh: %lu, lol: %lu", indices[i], x[indices[i]], lh, rh, lengthOfLeft);
    } */
    
    // return lengthOfLeft;
  }

#undef getDataAt

#undef loadLHComp
#undef loadRHComp
  
} // anon namespace


namespace dbarts {
  void Node::addObservationsToChildren(const BARTFit& fit, size_t chainNum, const double* y) {
    if (isBottom()) {
      if (isTop()) {
        if (fit.data.weights == NULL) {
          m.average = misc_htm_computeMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, numObservations);
          m.numEffectiveObservations = static_cast<double>(numObservations);
        } else {
          m.average = misc_htm_computeWeightedMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, numObservations, fit.data.weights, &m.numEffectiveObservations);
        }
      } else {
        if (fit.data.weights == NULL) {
          m.average = misc_htm_computeIndexedMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, observationIndices, numObservations);
          m.numEffectiveObservations = static_cast<double>(numObservations);
        } else {
          m.average = misc_htm_computeIndexedWeightedMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, observationIndices, numObservations, fit.data.weights, &m.numEffectiveObservations);
        }
      }
      
      return;
    }
    
    leftChild->clearObservations();
    p.rightChild->clearObservations();
    
    
    if (numObservations > 0) {
      size_t numOnLeft = 0;
    
      numOnLeft = (isTop() ?
                   partitionRange(fit, p.rule, observationIndices, numObservations) :
                   partitionIndices(fit, p.rule, observationIndices, numObservations));
      
      leftChild->observationIndices = observationIndices;
      leftChild->numObservations = numOnLeft;
      p.rightChild->observationIndices = observationIndices + numOnLeft;
      p.rightChild->numObservations = numObservations - numOnLeft;
      
      
      leftChild->addObservationsToChildren(fit, chainNum, y);
      p.rightChild->addObservationsToChildren(fit, chainNum, y);
    }
  }
  
  void Node::addObservationsToChildren(const BARTFit& fit) {
    if (isBottom()) {
      m.average = 0.0;
      return;
    }
    
    leftChild->clearObservations();
    p.rightChild->clearObservations();
    
    if (numObservations > 0) {
      size_t numOnLeft = (isTop() ?
                          partitionRange(fit, p.rule, observationIndices, numObservations) :
                          partitionIndices(fit, p.rule, observationIndices, numObservations));
      
      leftChild->observationIndices = observationIndices;
      leftChild->numObservations = numOnLeft;
      p.rightChild->observationIndices = observationIndices + numOnLeft;
      p.rightChild->numObservations = numObservations - numOnLeft;
    
      leftChild->addObservationsToChildren(fit);
      p.rightChild->addObservationsToChildren(fit);
    }
  }
  
  void Node::setAverage(const BARTFit& fit, size_t chainNum, const double* y)
  {
    leftChild = NULL;
        
    if (isTop()) {
      if (fit.data.weights == NULL) {
        m.average = misc_htm_computeMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, numObservations);
        m.numEffectiveObservations = static_cast<double>(numObservations);
      }
      else m.average = misc_htm_computeWeightedMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, numObservations, fit.data.weights, &m.numEffectiveObservations);
    } else {
      if (fit.data.weights == NULL) {
        m.average = misc_htm_computeIndexedMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, observationIndices, numObservations);
        m.numEffectiveObservations = static_cast<double>(numObservations);
      }
      else m.average = misc_htm_computeIndexedWeightedMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, observationIndices, numObservations, fit.data.weights, &m.numEffectiveObservations);
    }
  }
  
  void Node::setAverages(const BARTFit& fit, size_t chainNum, const double* y)
  {
    if (isBottom()) {
      setAverage(fit, chainNum, y);
      return;
    }
    
    leftChild->setAverages(fit, chainNum, y);
    p.rightChild->setAverages(fit, chainNum, y);
  }

  double Node::computeVariance(const BARTFit& fit, size_t chainNum, const double* y) const
  {
    if (isTop()) {
      if (fit.data.weights == NULL) {
        return misc_htm_computeVarianceForKnownMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, numObservations, getAverage());
      } else {
        return misc_htm_computeWeightedVarianceForKnownMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, numObservations, fit.data.weights, getAverage());
      }
    } else {
      if (fit.data.weights == NULL) {
        return misc_htm_computeIndexedVarianceForKnownMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, observationIndices, numObservations, getAverage());
      } else {
        return misc_htm_computeIndexedWeightedVarianceForKnownMean(fit.threadManager, fit.chainScratch[chainNum].taskId, y, observationIndices, numObservations, fit.data.weights, getAverage());
      }
    }
  }
  
  double Node::drawFromPosterior(ext_rng* rng, const EndNodePrior& endNodePrior, double residualVariance) const
  {
    if (getNumObservations() == 0) return 0.0;
      
    return endNodePrior.drawFromPosterior(rng, getAverage(), getNumEffectiveObservations(), residualVariance);
  }
  
  // these could potentially be multithreaded, but the gains are probably minimal
  void Node::setPredictions(double* y_hat, double prediction) const
  {
    if (isTop()) {
      misc_setVectorToConstant(y_hat, getNumObservations(), prediction);
      return;
    }
    
    misc_setIndexedVectorToConstant(y_hat, observationIndices, getNumObservations(), prediction);
  }
  
  size_t Node::getDepth() const
  {
    size_t result = 0;
    const Node* node = this;

    while (!node->isTop()) {
      ++result;
      node = node->parent;
    }
    
    return result;
  }
  
  size_t Node::getDepthBelow() const
  {
    if (childrenAreBottom()) return 1;
    if (isBottom()) return 0;
    return (1 + std::max(leftChild->getDepthBelow(), p.rightChild->getDepthBelow()));
  }
  
  size_t Node::getNumNodesBelow() const
  {
    if (isBottom()) return 0;
    return 2 + leftChild->getNumNodesBelow() + p.rightChild->getNumNodesBelow();
  }
  
  size_t Node::getNumVariablesAvailableForSplit(size_t numVariables) const {
    return countTrueValues(variablesAvailableForSplit, numVariables);
  }

  void Node::split(const BARTFit& fit, size_t chainNum, const Rule& newRule, const double* y, bool exhaustedLeftSplits, bool exhaustedRightSplits) {
    if (newRule.variableIndex < 0) ext_throwError("error in split: rule not set\n");
    
    p.rule = newRule;
    
    leftChild    = new Node(*this, fit.data.numPredictors);
    p.rightChild = new Node(*this, fit.data.numPredictors);
    
    if (exhaustedLeftSplits)     leftChild->variablesAvailableForSplit[p.rule.variableIndex] = false;
    if (exhaustedRightSplits) p.rightChild->variablesAvailableForSplit[p.rule.variableIndex] = false;
    
    addObservationsToChildren(fit, chainNum, y);
  }
  
  void Node::split(const BARTFit& fit, const Rule& newRule, bool exhaustedLeftSplits, bool exhaustedRightSplits) {
    if (newRule.variableIndex < 0) ext_throwError("error in split: rule not set\n");
    
    p.rule = newRule;
    
    leftChild    = new Node(*this, fit.data.numPredictors);
    p.rightChild = new Node(*this, fit.data.numPredictors);
    
    if (exhaustedLeftSplits)     leftChild->variablesAvailableForSplit[p.rule.variableIndex] = false;
    if (exhaustedRightSplits) p.rightChild->variablesAvailableForSplit[p.rule.variableIndex] = false;
    
    addObservationsToChildren(fit);
  }

  void Node::orphanChildren() {
    // do this w/o clobbering children pointers until details are nailed down
    double numEffectiveObservations = leftChild->m.numEffectiveObservations + p.rightChild->m.numEffectiveObservations;
    
    double average = leftChild->m.average * (leftChild->m.numEffectiveObservations / numEffectiveObservations) +
                     p.rightChild->m.average * (p.rightChild->m.numEffectiveObservations / numEffectiveObservations);
    
    leftChild = NULL;
    m.average = average;
    m.numEffectiveObservations = numEffectiveObservations;
  }
    
  void Node::countVariableUses(uint32_t* variableCounts) const
  {
    if (isBottom()) return;
    
    ++variableCounts[p.rule.variableIndex];
    
    leftChild->countVariableUses(variableCounts);
    p.rightChild->countVariableUses(variableCounts);
  }
}
