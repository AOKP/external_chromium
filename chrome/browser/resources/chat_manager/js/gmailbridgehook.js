// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Port used for:
// 1. forwarding central user requests from the gmail page to the background.
// 2. forwarding the central user from the background to the gmail page.
var centralJidListenerGmailPort;

// The gmail page div used to funnel events through.
var divGmailHandler;

// The current central roster Jid.
// Requested and cached as early as possible.
var centralRosterJid;

/**
 * Triggered on a user initiated chat request. Forward to extension to be
 * processed by the Chrome central roster.
 * @param {MessageEvent} event the new chat event.
 */
function forwardChatEvent(event) {
  var chatJid = event.data;
  chrome.extension.sendRequest({msg: event.type, jid: chatJid});
}

/**
 * @param {string} eventType the event type.
 * @param {string} chatJid the jid to route the chat event to.
 * TODO(seh): Move into a common JS file and reference from chatbridgehook.js.
 */
function dispatchChatEvent(eventType, chatJid) {
  var chatEvent = document.createEvent('MessageEvent');
  chatEvent.initMessageEvent(eventType, true, true, chatJid);
  divGmailHandler.dispatchEvent(chatEvent);
}

/**
 * Forward central roster Jid to page.
 * @param {string} jid the central roster Jid.
 */
function dispatchCentralJid(jid) {
  dispatchChatEvent(ChatBridgeEventTypes.CENTRAL_USER_UPDATE, jid);
}

/**
 * Setup central roster jid listener.
 * @param {MessageEvent} event the event.
 */
function setupCentralRosterJidListener(event) {
  if (!centralJidListenerGmailPort) {
    if (centralRosterJid) {
      dispatchCentralJid(centralRosterJid);
    }
    centralJidListenerGmailPort = chrome.extension.connect(
        {name: 'centralJidListener'});
    centralJidListenerGmailPort.onMessage.addListener(function(msg) {
      if (msg.eventType == ChatBridgeEventTypes.CENTRAL_USER_UPDATE) {
        centralRosterJid = msg.jid;
      }
      dispatchChatEvent(msg.eventType, msg.jid);
    });
  }
}

/**
 * When the page loads, search for the communication channel div.
 */
function onPageLoaded() {
  divGmailHandler = document.getElementById('mainElement');
  if (divGmailHandler) {
    divGmailHandler.addEventListener(
        ChatBridgeEventTypes.SHOW_CHAT,
        forwardChatEvent, false);
    divGmailHandler.addEventListener(
        ChatBridgeEventTypes.START_VIDEO,
        forwardChatEvent, false);
    divGmailHandler.addEventListener(
        ChatBridgeEventTypes.START_VOICE,
        forwardChatEvent, false);
    divGmailHandler.addEventListener(
        ChatBridgeEventTypes.CENTRAL_USER_WATCHER,
        setupCentralRosterJidListener, false);
  }
}

// Retrieve the initial central roster Jid and cache the result.
chrome.extension.sendRequest(
    {msg: ChatBridgeEventTypes.CENTRAL_USER_WATCHER}, function(response) {
      centralRosterJid = response.jid;

      // The initial centralRosterJid is sent in setupCentralRosterJidListener,
      // but if it's already been called, send it here.
      if (centralJidListenerGmailPort && centralRosterJid) {
        dispatchCentralJid(centralRosterJid);
      }
    }
);

window.addEventListener("load", onPageLoaded, false);
