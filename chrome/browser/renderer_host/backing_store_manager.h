// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RENDERER_HOST_BACKING_STORE_MANAGER_H_
#define CHROME_BROWSER_RENDERER_HOST_BACKING_STORE_MANAGER_H_
#pragma once

#include <vector>

#include "app/surface/transport_dib.h"
#include "base/basictypes.h"
#include "base/process.h"
#include "gfx/rect.h"
#include "gfx/size.h"

class BackingStore;
class RenderWidgetHost;

// This class manages backing stores in the browsr. Every RenderWidgetHost is
// associated with a backing store which it requests from this class.  The
// hosts don't maintain any references to the backing stores.  These backing
// stores are maintained in a cache which can be trimmed as needed.
class BackingStoreManager {
 public:
  // Returns a backing store which matches the desired dimensions.
  //
  // backing_store_rect
  //   The desired backing store dimensions.
  // Returns a pointer to the backing store on success, NULL on failure.
  static BackingStore* GetBackingStore(RenderWidgetHost* host,
                                       const gfx::Size& desired_size);

  // Makes a backing store which is fully ready for consumption, i.e. the
  // bitmap from the renderer has been copied into the backing store.
  //
  // backing_store_size
  //   The desired backing store dimensions.
  // bitmap_section
  //   The bitmap section from the renderer.
  // bitmap_rect
  //   The rect to be painted into the backing store
  // needs_full_paint
  //   Set if we need to send out a request to paint the view
  //   to the renderer.
  // painted_synchronously
  //   Will be set by the function if the request was processed synchronously,
  //   and the bitmap is done being used. False means that the backing store
  //   will paint the bitmap at a later time and that the TransportDIB can't be
  //   freed (it will be the backing store's job to free it later).
  static void PrepareBackingStore(
      RenderWidgetHost* host,
      const gfx::Size& backing_store_size,
      TransportDIB::Id bitmap,
      const gfx::Rect& bitmap_rect,
      const std::vector<gfx::Rect>& copy_rects,
      bool* needs_full_paint,
      bool* painted_synchronously);

  // Returns a matching backing store for the host.
  // Returns NULL if we fail to find one.
  static BackingStore* Lookup(RenderWidgetHost* host);

  // Removes the backing store for the host.
  static void RemoveBackingStore(RenderWidgetHost* host);

  // Removes all backing stores.
  static void RemoveAllBackingStores();

  // Expires the given backing store. This emulates something getting evicted
  // from the cache for the purpose of testing. Returns true if the host was
  // removed, false if it wasn't found.
  static bool ExpireBackingStoreForTest(RenderWidgetHost* host);

  // Current size in bytes of the backing store cache.
  static size_t MemorySize();

 private:
  // Not intended for instantiation.
  BackingStoreManager() {}

  DISALLOW_COPY_AND_ASSIGN(BackingStoreManager);
};

#endif  // CHROME_BROWSER_RENDERER_HOST_BACKING_STORE_MANAGER_H_
