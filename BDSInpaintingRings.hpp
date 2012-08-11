/*=========================================================================
 *
 *  Copyright David Doria 2012 daviddoria@gmail.com
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#ifndef BDSInpaintingRings_HPP
#define BDSInpaintingRings_HPP

#include "BDSInpaintingRings.h"
#include "BDSInpainting.h" // Composition

// Custom
#include "InitializerRandom.h"
#include "InitializerKnownRegion.h"
#include "InitializerNeighborHistogram.h"
#include "AcceptanceTestNeighborHistogram.h"
#include "Verifier.h"

// Submodules
#include <PatchMatch/PropagatorForwardBackward.h>
#include <PatchMatch/RandomSearch.h>

template <typename TImage>
BDSInpaintingRings<TImage>::BDSInpaintingRings() : InpaintingAlgorithm<TImage>()
{

}

template <typename TImage>
void BDSInpaintingRings<TImage>::Inpaint()
{
  { // Debug only
  ITKHelpers::WriteImage(this->TargetMask.GetPointer(), "BDSInpaintingRings_TargetMask.png");
  ITKHelpers::WriteImage(this->SourceMask.GetPointer(), "BDSInpaintingRings_SourceMask.png");
  }

  // Save the original mask, as we will be modifying the internal masks below
  Mask::Pointer currentTargetMask = Mask::New();
  currentTargetMask->DeepCopyFrom(this->TargetMask);

  // Initialize the image from the original image
  typename TImage::Pointer currentImage = TImage::New();
  ITKHelpers::DeepCopy(this->Image.GetPointer(), currentImage.GetPointer());

  // The pixels from which information is allowed to propagate (everywhere except the target region)
  // (This is computed again at each iteration through the loop)
  Mask::Pointer currentPropagationMask = Mask::New();
  // We trust the information everywhere except in the hole
  currentPropagationMask->DeepCopyFrom(currentTargetMask);
  currentPropagationMask->InvertData();

  ITKHelpers::WriteImage(currentPropagationMask.GetPointer(),
                         "BDSInpaintingRings_InitialPropagationMask.png");

  // Compute the NN-field in the PatchRadius thick region around the target region. This region
  // does not have a trivial (exactly itself) NNField because each patch centered on one of these
  // pixels has some pixels that are in the target region.
  Mask::Pointer expandedTargetMask = Mask::New();
  expandedTargetMask->DeepCopyFrom(this->TargetMask);
  expandedTargetMask->ShrinkHole(this->PatchRadius);
  ITKHelpers::WriteImage(expandedTargetMask.GetPointer(), "BDSInpaintingRings_ExpandedTargetMask.png");

  Mask::Pointer outsideTargetMask = Mask::New();
  ITKHelpers::XORImages(expandedTargetMask.GetPointer(), currentTargetMask.GetPointer(),
                        outsideTargetMask.GetPointer(), this->TargetMask->GetValidValue());
  outsideTargetMask->CopyInformationFrom(this->TargetMask);

  ITKHelpers::WriteImage(outsideTargetMask.GetPointer(), "BDSInpaintingRings_OutsideTargetMask.png");

  // Allocate the initial NNField
  typename PatchMatchHelpers::NNFieldType::Pointer nnField =
    PatchMatchHelpers::NNFieldType::New();
  nnField->SetRegions(currentImage->GetLargestPossibleRegion());
  nnField->Allocate();

  // Create the HSV image
  typedef itk::VectorImage<float, 2> HSVImageType;
  HSVImageType::Pointer hsvImage = HSVImageType::New();
  ITKHelpers::ITKImageToHSVImage(currentImage.GetPointer(), hsvImage.GetPointer());
  ITKHelpers::WriteImage(hsvImage.GetPointer(), "HSV.mha");

  // Setup the patch distance functor
  typedef SSD<TImage> SSDFunctorType;
  SSDFunctorType ssdFunctor;
  ssdFunctor.SetImage(this->Image);

  // Set acceptance test to histogram threshold
  typedef AcceptanceTestNeighborHistogram<HSVImageType> AcceptanceTestType;
  AcceptanceTestType acceptanceTest;
  acceptanceTest.SetImage(hsvImage);
  acceptanceTest.SetRangeMin(0.0f);
  acceptanceTest.SetRangeMax(1.0f);
  acceptanceTest.SetPatchRadius(this->PatchRadius);
  acceptanceTest.SetNeighborHistogramMultiplier(2.0f);

//   typedef ValidMaskValidScoreNeighbors NeighborFunctorType;
//   ValidMaskValidScoreNeighbors neighborFunctor(nnField, this->SourceMask);

  typedef ProcessTargetPixels ProcessFunctorType;
  ProcessFunctorType processFunctor;
  
  typedef PropagatorForwardBackward<SSDFunctorType, NeighborFunctorType,
            ProcessFunctorType, AcceptanceTestType> PropagatorType;
  PropagatorType propagationFunctor;
  propagationFunctor.SetProcessFunctor(&processFunctor);
  propagationFunctor.SetAcceptanceTest(&acceptanceTest);
  //propagationFunctor.SetNeighborFunctor(&neighborFunctor); // This is not necessary with PropagatorForwardBackward as it internally uses Forward and then Backwards neighbors
  propagationFunctor.Propagate(nnField);

  typedef RandomSearch<TImage> RandomSearchType;
  RandomSearchType randomSearcher;

  // Setup the PatchMatch functor. Use a generic (parent class) AcceptanceTest.
  PatchMatch patchMatchFunctor;
  patchMatchFunctor.SetPatchRadius(this->PatchRadius);
  patchMatchFunctor.SetIterations(5);
  patchMatchFunctor.Compute(nnField, propagationFunctor, randomSearcher);

  // Initialize the NNField in the known region
  InitializerKnownRegion initializerKnownRegion;
  initializerKnownRegion.SetSourceMask(this->SourceMask);
  initializerKnownRegion.SetPatchRadius(this->PatchRadius);
  initializerKnownRegion.Initialize(nnField);

  PatchMatchHelpers::WriteNNField(nnField.GetPointer(), "BDSInpaintingRings_KnownRegionNNField.mha");

  // Remove the boundary from the source mask, to give the propagation some breathing room.
  // We just remove a 1 pixel thick boundary around the image, then perform an ExpandHole operation.
  // ExpandHole() only operates on the boundary between Valid and Hole, so if we did not first remove the
  // single pixel boundary nothing would happen to the boundary by the morphological filter.
  // Must do this after the InitializerKnownRegion so that the pixels whose patches are fully in the original
  // source region are initialized properly.
  std::vector<itk::Index<2> > boundaryPixels =
    //ITKHelpers::GetBoundaryPixels(this->SourceMask->GetLargestPossibleRegion(), this->PatchRadius);
    ITKHelpers::GetBoundaryPixels(this->SourceMask->GetLargestPossibleRegion(), 1);
  ITKHelpers::SetPixels(this->SourceMask.GetPointer(), boundaryPixels, this->SourceMask->GetHoleValue());

  ITKHelpers::WriteImage(this->SourceMask.GetPointer(), "BDSInpaintingRings_BoundaryRemovedSourceMask.png");

  this->SourceMask->ExpandHole(this->PatchRadius);
  ITKHelpers::WriteImage(this->SourceMask.GetPointer(), "BDSInpaintingRings_FinalSourceMask.png");

//   ITKHelpers::ScaleAllChannelsTo255(hsvImage.GetPointer());
//   typename TImage::Pointer castedHSVImage = TImage::New();
//   ITKHelpers::CastImage(hsvImage.GetPointer(), castedHSVImage.GetPointer());
//   ITKHelpers::WriteImage(castedHSVImage.GetPointer(), "CastedHSV.mha");

  // Initialize the NNField in the PatchRadius thick ring outside of the target region
  //InitializerRandom<TImage> initializer;

  InitializerRandom<SSDFunctorType> initializer;
  initializer.SetPatchDistanceFunctor(&ssdFunctor);
  initializer.SetTargetMask(outsideTargetMask);
  initializer.SetSourceMask(this->SourceMask);
  initializer.SetPatchRadius(this->PatchRadius);
  initializer.Initialize(nnField);

  PatchMatchHelpers::WriteNNField(nnField.GetPointer(),
                                    "InitializedNNField.mha");

  typedef VerifierNeighborHistogram<HSVImageType> VerifyFunctorType;
  VerifyFunctorType verifyFunctor;
  verifyFunctor.SetImage(hsvImage);
  verifyFunctor.SetNeighborHistogramMultiplier(2.0f);
  verifyFunctor.SetRangeMin(0.0f);
  verifyFunctor.SetRangeMax(1.0f);
  verifyFunctor.SetMatchImage(nnField);
  verifyFunctor.SetPatchRadius(this->PatchRadius);

  Verifier<VerifyFunctorType> verifier;
  verifier.SetMask(outsideTargetMask);
  verifier.SetVerifyFunctor(&verifyFunctor);
  verifier.Verify(nnField);

  PatchMatchHelpers::WriteNNField(nnField.GetPointer(),
                                    "VerifiedNNField.mha");
//   InitializerNeighborHistogram<HSVImageType, SSDFunctorType> initializer;
//   initializer.SetPatchDistanceFunctor(&ssdFunctor);
//   initializer.SetRangeMin(0.0f);
//   initializer.SetRangeMax(1.0f);
//   initializer.SetImage(hsvImage);
//   initializer.SetTargetMask(outsideTargetMask);
//   initializer.SetSourceMask(this->SourceMask);
//   initializer.SetPatchRadius(this->PatchRadius);
    //initializer.SetNeighborHistogramMultiplier(histogramMultiplier);

  float histogramMultiplierInitial = 2.0f;
  float histogramMultiplierStep = 0.2f;
  float histogramMultiplier = histogramMultiplierInitial;

  unsigned int iteration = 0;
  auto testNotVerifiedLambda = [](const Match& queryMatch)
  {
    if(!queryMatch.Verified)
    {
      return true;
    }
    return false;
  };

  //while(PatchMatchHelpers::CountInvalidPixels(nnField.GetPointer(), outsideTargetMask))
  while(PatchMatchHelpers::CountTestedPixels(nnField.GetPointer(), outsideTargetMask, testNotVerifiedLambda))
  {
    std::cout << "There are "
              << PatchMatchHelpers::CountInvalidPixels(nnField.GetPointer(), outsideTargetMask)
              << " pixels remaining." << std::endl;

    acceptanceTest.SetNeighborHistogramMultiplier(histogramMultiplier);

    this->PatchMatchFunctor->SetInitialNNField(nnField);
    this->PatchMatchFunctor->Compute();

    PatchMatchHelpers::WriteNNField(this->PatchMatchFunctor->GetOutput(),
                                    Helpers::GetSequentialFileName("BDSInpaintingRings_PropagatedNNField",
                                                                   iteration, "mha"));

    ITKHelpers::DeepCopy(this->PatchMatchFunctor->GetOutput(), nnField.GetPointer());
    histogramMultiplier += histogramMultiplierStep;
    iteration++;
  }

  PatchMatchHelpers::WriteNNField(this->PatchMatchFunctor->GetOutput(),
                                  "BDSInpaintingRings_BoundaryNNField.mha");
  exit(-1); // TODO: remove this 
  // Keep track of which ring we are on
  unsigned int ringCounter = 0;

  // Perform ring-at-a-time inpainting
  while(currentTargetMask->HasValidPixels())
  {
    // We trust the information everywhere except in the hole
    currentPropagationMask->DeepCopyFrom(currentTargetMask);
    currentPropagationMask->InvertData();

    //ITKHelpers::WriteSequentialImage(currentPropagationMask.GetPointer(),
//     "BDSRings_CurrentPropagationMask", ringCounter, 4, "png");

    // Get the inside boundary of the target region
    Mask::BoundaryImageType::Pointer boundaryImage = Mask::BoundaryImageType::New();
    // In the resulting boundary image, the boundary will be 255.
    Mask::BoundaryImageType::PixelType boundaryValue = 255; 
    currentTargetMask->FindBoundary(boundaryImage, Mask::VALID, boundaryValue);

    //ITKHelpers::WriteSequentialImage(boundaryImage.GetPointer(), "BDSRings_Boundary", ringCounter, 4, "png");

    // Create a mask of just the boundary
    Mask::Pointer boundaryMask = Mask::New();
    Mask::BoundaryImageType::PixelType holeValue = 0;
    Mask::BoundaryImageType::PixelType validValue = boundaryValue;
    boundaryMask->CreateFromImage(boundaryImage.GetPointer(), holeValue, validValue);
    //boundaryMask->Invert(); // Make the thin boundary the only valid pixels in the mask

    //std::cout << "Boundary mask: " << std::endl; boundaryMask->OutputMembers(); // Debug only
    //ITKHelpers::WriteSequentialImage(boundaryMask.GetPointer(),
//     "BDSRings_BoundaryMask", ringCounter, 4, "png");

    // Set the mask to use in the PatchMatch algorithm
    currentTargetMask->DeepCopyFrom(boundaryMask);

    // We set these properties here, but this object is not used here but rather simply
    // passed along to the composition BDSInpainting object below
    BDSInpainting<TImage> internalBDSInpaintingFunctor;
    internalBDSInpaintingFunctor.SetImage(this->Image);
    internalBDSInpaintingFunctor.SetPatchRadius(this->PatchRadius);
    internalBDSInpaintingFunctor.SetTargetMask(boundaryMask);
    internalBDSInpaintingFunctor.SetSourceMask(this->SourceMask);
    internalBDSInpaintingFunctor.Inpaint();

    ITKHelpers::WriteSequentialImage(internalBDSInpaintingFunctor.GetOutput(),
                                     "BDSRings_InpaintedRing", ringCounter, 4, "png");

    // Copy the filled ring into the image for the next iteration
    ITKHelpers::DeepCopy(internalBDSInpaintingFunctor.GetOutput(), currentImage.GetPointer());

    // Reduce the size of the target region (we "enlarge the hole", because
    // the "hole" is considered the valid part of the target mask)
    unsigned int kernelRadius = 1;
    currentTargetMask->ExpandHole(kernelRadius);

    ringCounter++;
  }

  ITKHelpers::DeepCopy(currentImage.GetPointer(), this->Output.GetPointer());
}

#endif
