// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_IMAGE_DECODER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_IMAGE_DECODER_H_

#include <vector>

#include "chrome/browser/utility_process_host.h"

class ResourceDispatcherHost;

namespace chromeos {

// Decodes an image in a sandboxed process.
class ImageDecoder : public UtilityProcessHost::Client {
 public:
  class Delegate {
   public:
    // Called when image is decoded.
    virtual void OnImageDecoded(const SkBitmap& decoded_image) = 0;

   protected:
    virtual ~Delegate() {}
  };

  ImageDecoder(Delegate* delegate,
               const std::vector<unsigned char>& image_data);

  // Starts image decoding.
  void Start();

 private:
  // It's a reference counted object, so destructor is private.
  virtual ~ImageDecoder() {}

  // Overidden from UtilityProcessHost::Client:
  virtual void OnDecodeImageSucceeded(const SkBitmap& decoded_image);

  // Launches sandboxed process that will decode the image.
  void DecodeImageInSandbox(ResourceDispatcherHost* rdh,
                            const std::vector<unsigned char>& image_data);

  Delegate* delegate_;
  std::vector<unsigned char> image_data_;

  DISALLOW_COPY_AND_ASSIGN(ImageDecoder);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_IMAGE_DECODER_H_

