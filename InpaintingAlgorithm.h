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

#ifndef InpaintingAlgorithm_H
#define InpaintingAlgorithm_H

// ITK
#include "itkCovariantVector.h"
#include "itkImage.h"
#include "itkImageRegion.h"
#include "itkVectorImage.h"

// Submodules
#include <Mask/Mask.h>
#include <PatchMatch/PatchMatch.h>
#include <Compositor.h>

/** This class provides an interface which accepts and stores typical paramaters to an inpainting
  * algorithm (masks, image, patch radius, etc). */
template <typename TImage>
class InpaintingAlgorithm
{
public:

  /** Constructor. */
  InpaintingAlgorithm();

  /** Get the resulting inpainted image. */
  TImage* GetOutput();

  /** Set the number of iterations to run. */
  void SetIterations(const unsigned int iterations);

  /** Set the patch radius. */
  void SetPatchRadius(const unsigned int patchRadius);

  /** Set the image to fill. */
  void SetImage(TImage* const image);

  /** Set the mask that indicates where to take source patches from. Source patches
    * are patches that are entirely in the Valid region.*/
  void SetSourceMask(Mask* const mask);

  /** Set the mask that indicates where to fill the image. Pixels in the Hole region should be filled.*/
  void SetTargetMask(Mask* const mask);

protected:

  /** The number of iterations to run. */
  unsigned int Iterations;

  /** The radius of the patches to use for inpainting. */
  unsigned int PatchRadius;

  /** The output image. */
  typename TImage::Pointer Output;

  /** The image to fill. */
  typename TImage::Pointer Image;

  /** The mask where Hole pixels indicate the pixels to fill. */
  Mask::Pointer TargetMask;

  /** The mask where fully 'valid' patches are allowed to be matches. */
  Mask::Pointer SourceMask;
};

#include "InpaintingAlgorithm.hpp"

#endif
