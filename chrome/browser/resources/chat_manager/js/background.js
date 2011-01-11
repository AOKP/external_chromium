// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var centralRosterJid;
var centralRosterPort;
var centralJidListenerPorts = [];

// Notify all port listeners of an event.
function forwardEventToPortListeners(evtType, chatJid) {
  var listenerPort;
  for (var portIndex in centralJidListenerPorts) {
    listenerPort = centralJidListenerPorts[portIndex];
    listenerPort.postMessage({eventType: evtType, jid: chatJid});
  }
}

// Notify all port listeners of updated central roster jid.
function forwardCentralRosterJidToPortListeners() {
  forwardEventToPortListeners(
      ChatBridgeEventTypes.CENTRAL_USER_UPDATE, centralRosterJid);
}

// Central roster jid changed. Notify all listeners.
function centralJidUpdate(msg) {
  if (centralRosterJid != msg.jid) {
    centralRosterJid = msg.jid;
    forwardCentralRosterJidToPortListeners();
  }
}

// Focus a chat popup.
function focusMole(hostId) {
  findMole(hostId, function(win) {
    chrome.windows.update(win.id, {focused: true});
  });
}

// Find a chat popup from a chat's hostId and executes callback with it.
function findMole(hostId, callback) {
  var matchUrlIdRegExp = new RegExp('[&?]id=' + hostId + '(&|$)', 'i');
  chrome.windows.getAll({populate: true}, function(wins) {
    for (var winIdx = 0, win = wins[winIdx]; win; win = wins[++winIdx]) {
      var tabs = win.tabs;
      for (var tabIdx = 0, tab = tabs[tabIdx]; tab; tab = tabs[++tabIdx]) {
        if ((tab.url).match(matchUrlIdRegExp)) {
          callback(win);
          return;
        }
      }
    }
  });
}

// Listen for content script connections.
chrome.extension.onConnect.addListener(function(port) {
  // New central jid listener.
  // Update with current central roster jid, and add to tracking array.
  if (port.name == 'centralJidListener') {
    centralJidListenerPorts.push(port);
    port.postMessage({eventType: ChatBridgeEventTypes.CENTRAL_USER_UPDATE,
        jid: centralRosterJid});

    // Clear tracking array entry when content script closes.
    port.onDisconnect.addListener(function(port) {
      for (var index = 0; index < centralJidListenerPorts.length; ++index) {
        var listenerPort = centralJidListenerPorts[index];
        if (listenerPort == port) {
          centralJidListenerPorts.splice(index, 1);
          break;
        }
      }
    });
  // New central jid broadcaster.
  // Add listener for jid changes, and track port for forwarding chats.
  } else if (port.name == 'centralJidBroadcaster') {
    if (centralRosterPort != port) {
      if (centralRosterPort) {
        centralRosterPort.onMessage.removeListener(centralJidUpdate);
      }
      centralRosterPort = port;
      centralRosterPort.onMessage.addListener(centralJidUpdate);

      // Clear listener and central roster jid when content script closes
      centralRosterPort.onDisconnect.addListener(function(port) {
        if (centralRosterPort) {
          centralRosterPort.onMessage.removeListener(centralJidUpdate);
          centralRosterPort = null;
          centralJidUpdate({jid: null});
        }
      });
    }
  }
});

// Listen for requests from our content scripts.
chrome.extension.onRequest.addListener(
    function(request, sender, sendResponse) {
  switch (request.msg) {
    case ChatBridgeEventTypes.CENTRAL_USER_WATCHER:
      sendResponse({jid: centralRosterJid});
      break;
    // For new initiated chats, forward to the central roster port.
    case ChatBridgeEventTypes.SHOW_CHAT:
    case ChatBridgeEventTypes.START_VIDEO:
    case ChatBridgeEventTypes.START_VOICE:
      if (centralRosterPort) {
        centralRosterPort.postMessage(
            {eventType: request.msg, jid: request.jid});
      } else {
        // We should not have been forwarded this message. Make sure our
        // listeners are updated with the current central roster jid.
        forwardCentralRosterJidToPortListeners();
      }
      break;
    case ChatBridgeEventTypes.OPENED_MOLE_INCOMING:
      forwardEventToPortListeners(ChatBridgeEventTypes.OPENED_MOLE_OUTGOING,
          request.jid);
      break;
    case ChatBridgeEventTypes.CLOSED_MOLE_INCOMING:
      forwardEventToPortListeners(ChatBridgeEventTypes.CLOSED_MOLE_OUTGOING,
          request.jid);
      break;
    case ChatBridgeEventTypes.MOLE_FOCUSED:
      focusMole(request.jid);
      break;
  }
});
