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

// STL
#include <iostream>

// ITK
#include "itkImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkCovariantVector.h"

// Submodules
#include <Mask/Mask.h>

#include <ITKHelpers/ITKHelpers.h>

#include <ITKVTKHelpers/ITKVTKHelpers.h>

#include <PatchComparison/SSD.h>

#include <PatchMatch/PatchMatch.h>

#include <PoissonEditing/PoissonEditing.h>

// Custom
#include "BDSInpaintingRings.h"
#include "AcceptanceTestNeighborHistogram.h"
#include "InitializerRandom.h"
#include "Propagator.h"
#include "RandomSearch.h"

int main(int argc, char*argv[])
{
  // Parse the input
  if(argc < 6)
  {
    std::cerr << "Required arguments: image sourceMask.mask targetMask.mask patchRadius output" << std::endl;
    return EXIT_FAILURE;
  }

  std::stringstream ss;
  for(int i = 1; i < argc; ++i)
  {
    ss << argv[i] << " ";
  }

  std::string imageFilename;
  std::string sourceMaskFilename;
  std::string targetMaskFilename;
  unsigned int patchRadius;
  std::string outputFilename;

  ss >> imageFilename >> sourceMaskFilename >> targetMaskFilename >> patchRadius >> outputFilename;

  // Output the parsed values
  std::cout << "imageFilename: " << imageFilename << std::endl
            << "sourceMaskFilename: " << sourceMaskFilename << std::endl
            << "targetMaskFilename: " << targetMaskFilename << std::endl
            << "patchRadius: " << patchRadius << std::endl
            << "outputFilename: " << outputFilename << std::endl;

  typedef itk::Image<itk::CovariantVector<unsigned char, 3>, 2> ImageType;

  // Read the image and the masks
  typedef itk::ImageFileReader<ImageType> ImageReaderType;
  ImageReaderType::Pointer imageReader = ImageReaderType::New();
  imageReader->SetFileName(imageFilename);
  imageReader->Update();

  ImageType* image = imageReader->GetOutput();

  Mask::Pointer sourceMask = Mask::New();
  sourceMask->Read(sourceMaskFilename);

  Mask::Pointer targetMask = Mask::New();
  targetMask->Read(targetMaskFilename);

  // Poisson fill the input image in HSV space
  typedef itk::Image<itk::CovariantVector<float, 3>, 2> HSVImageType;
  HSVImageType::Pointer hsvImage = HSVImageType::New();
  ITKVTKHelpers::ConvertRGBtoHSV(image, hsvImage.GetPointer());

  ITKHelpers::WriteImage(image, "HSV.mha");
  
  typedef PoissonEditing<typename TypeTraits<HSVImageType::PixelType>::ComponentType> PoissonEditingType;

  typename PoissonEditingType::GuidanceFieldType::Pointer zeroGuidanceField =
            PoissonEditingType::GuidanceFieldType::New();
  zeroGuidanceField->SetRegions(hsvImage->GetLargestPossibleRegion());
  zeroGuidanceField->Allocate();
  typename PoissonEditingType::GuidanceFieldType::PixelType zeroPixel;
  zeroPixel.Fill(0);
  ITKHelpers::SetImageToConstant(zeroGuidanceField.GetPointer(), zeroPixel);

  PoissonEditingType::FillImage(hsvImage.GetPointer(), targetMask,
                                zeroGuidanceField.GetPointer(), hsvImage.GetPointer());

  ITKHelpers::WriteImage(image, "PoissonFilled_HSV.mha");

  ITKVTKHelpers::ConvertHSVtoRGB(hsvImage.GetPointer(), image);

  ITKHelpers::WriteRGBImage(image, "PoissonFilled_HSV.png");

  // PatchMatch requires that the target region be specified by valid pixels
  targetMask->InvertData();

  // Setup the patch distance functor
  typedef SSD<ImageType> SSDFunctorType;
  SSDFunctorType ssdFunctor;
  ssdFunctor.SetImage(image);

  // Set acceptance test to histogram threshold
  typedef AcceptanceTestNeighborHistogram<ImageType> AcceptanceTestType;
  AcceptanceTestType acceptanceTest;
  acceptanceTest.SetNeighborHistogramMultiplier(2.0f);

  typedef Propagator<NeighborFunctorType, ProcessFunctorType, AcceptanceTestType> PropagatorType;
  PropagatorType propagator;

  typedef RandomSearch<ImageType> RandomSearchType;
  RandomSearchType randomSearcher;
  
  // Setup the PatchMatch functor. Use a generic (parent class) AcceptanceTest.
  typedef PatchMatch<SSDFunctorType, AcceptanceTest,
                     PropagatorType, RandomSearchType> PatchMatchFunctorType;
  PatchMatchFunctorType patchMatchFunctor;
  patchMatchFunctor.SetPatchRadius(patchRadius);
  patchMatchFunctor.SetPatchDistanceFunctor(&ssdFunctor);
  patchMatchFunctor.SetPropagationFunctor(&propagator);
  patchMatchFunctor.SetRandomSearchFunctor(&randomSearcher);
  patchMatchFunctor.SetIterations(5);
  patchMatchFunctor.SetAcceptanceTest(&acceptanceTest);

  // Here, the source mask and target mask are the same, specifying the classicial
  // "use pixels outside the hole to fill the pixels inside the hole".
  // In an interactive algorith, the user could manually specify a source region,
  // improving the resulting inpainting.
  BDSInpaintingRings<ImageType, PatchMatchFunctorType> bdsInpainting;
  bdsInpainting.SetPatchRadius(patchRadius);
  bdsInpainting.SetImage(image);
  bdsInpainting.SetSourceMask(sourceMask);
  bdsInpainting.SetTargetMask(targetMask);

  bdsInpainting.SetIterations(1);
  //bdsInpainting.SetIterations(4);

  Compositor<ImageType> compositor;
  compositor.SetCompositingMethod(Compositor<ImageType>::AVERAGE);
  bdsInpainting.SetCompositor(&compositor);
  bdsInpainting.SetPatchMatchFunctor(&patchMatchFunctor);
  bdsInpainting.Inpaint();

  ITKHelpers::WriteRGBImage(bdsInpainting.GetOutput(), outputFilename);

  return EXIT_SUCCESS;
}
