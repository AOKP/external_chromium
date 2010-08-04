// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_GLUE_PLUGINS_MAC_ACCELERATED_SURFACE_CONTAINER_MANAGER_H_
#define WEBKIT_GLUE_PLUGINS_MAC_ACCELERATED_SURFACE_CONTAINER_MANAGER_H_

#include <OpenGL/OpenGL.h>
#include <map>
#include <vector>

#include "app/gfx/native_widget_types.h"
#include "app/surface/transport_dib.h"
#include "base/basictypes.h"

namespace webkit_glue {
struct WebPluginGeometry;
}

class MacAcceleratedSurfaceContainer;

// Helper class that manages the backing store and on-screen rendering
// of instances of the GPU plugin on the Mac.
class MacAcceleratedSurfaceContainerManager {
 public:
  MacAcceleratedSurfaceContainerManager();

  // Allocates a new "fake" PluginWindowHandle, which is used as the
  // key for the other operations.
  gfx::PluginWindowHandle AllocateFakePluginWindowHandle();

  // Destroys a fake PluginWindowHandle and associated storage.
  void DestroyFakePluginWindowHandle(gfx::PluginWindowHandle id);

  // Sets the size and backing store of the plugin instance.  There are two
  // versions: the IOSurface version is used on systems where the IOSurface
  // API is supported (Mac OS X 10.6 and later); the TransportDIB is used on
  // Mac OS X 10.5 and earlier.
  void SetSizeAndIOSurface(gfx::PluginWindowHandle id,
                           int32 width,
                           int32 height,
                           uint64 io_surface_identifier);
  void SetSizeAndTransportDIB(gfx::PluginWindowHandle id,
                              int32 width,
                              int32 height,
                              TransportDIB::Handle transport_dib);

  // Takes an update from WebKit about a plugin's position and size and moves
  // the plugin accordingly.
  void MovePluginContainer(const webkit_glue::WebPluginGeometry& move);

  // Draws all of the managed plugin containers into the given OpenGL
  // context, which must already be current.
  void Draw(CGLContextObj context);

  // Called by the container to enqueue its OpenGL texture objects for
  // deletion.
  void EnqueueTextureForDeletion(GLuint texture);

 private:
  uint32 current_id_;

  // Maps a "fake" plugin window handle to the corresponding container.
  MacAcceleratedSurfaceContainer* MapIDToContainer(gfx::PluginWindowHandle id);

  // A map that associates plugin window handles with their containers.
  typedef std::map<gfx::PluginWindowHandle, MacAcceleratedSurfaceContainer*>
      PluginWindowToContainerMap;
  PluginWindowToContainerMap plugin_window_to_container_map_;

  // A list of OpenGL textures waiting to be deleted
  std::vector<GLuint> textures_pending_deletion_;

  DISALLOW_COPY_AND_ASSIGN(MacAcceleratedSurfaceContainerManager);
};

#endif  // WEBKIT_GLUE_PLUGINS_MAC_ACCELERATED_SURFACE_CONTAINER_MANAGER_H_

