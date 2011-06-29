/*
 * libjingle
 * Copyright 2010, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string>
#include "talk/p2p/base/sessionmessages.h"

#include "talk/base/logging.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/stringutils.h"
#include "talk/xmllite/xmlconstants.h"
#include "talk/xmpp/constants.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/p2ptransport.h"
#include "talk/p2p/base/parsing.h"
#include "talk/p2p/base/sessionclient.h"
#include "talk/p2p/base/sessiondescription.h"
#include "talk/p2p/base/transport.h"
#include "talk/xmllite/xmlconstants.h"

namespace cricket {

ActionType ToActionType(const std::string& type) {
  if (type == GINGLE_ACTION_INITIATE)
    return ACTION_SESSION_INITIATE;
  if (type == GINGLE_ACTION_INFO)
    return ACTION_SESSION_INFO;
  if (type == GINGLE_ACTION_ACCEPT)
    return ACTION_SESSION_ACCEPT;
  if (type == GINGLE_ACTION_REJECT)
    return ACTION_SESSION_REJECT;
  if (type == GINGLE_ACTION_TERMINATE)
    return ACTION_SESSION_TERMINATE;
  if (type == GINGLE_ACTION_CANDIDATES)
    return ACTION_TRANSPORT_INFO;
  if (type == JINGLE_ACTION_SESSION_INITIATE)
    return ACTION_SESSION_INITIATE;
  if (type == JINGLE_ACTION_TRANSPORT_INFO)
    return ACTION_TRANSPORT_INFO;
  if (type == JINGLE_ACTION_TRANSPORT_ACCEPT)
    return ACTION_TRANSPORT_ACCEPT;
  if (type == JINGLE_ACTION_SESSION_INFO)
    return ACTION_SESSION_INFO;
  if (type == JINGLE_ACTION_SESSION_ACCEPT)
    return ACTION_SESSION_ACCEPT;
  if (type == JINGLE_ACTION_SESSION_TERMINATE)
    return ACTION_SESSION_TERMINATE;
  if (type == JINGLE_ACTION_TRANSPORT_INFO)
    return ACTION_TRANSPORT_INFO;
  if (type == JINGLE_ACTION_TRANSPORT_ACCEPT)
    return ACTION_TRANSPORT_ACCEPT;
  if (type == GINGLE_ACTION_NOTIFY)
    return ACTION_NOTIFY;
  if (type == GINGLE_ACTION_UPDATE)
    return ACTION_UPDATE;

  return ACTION_UNKNOWN;
}

std::string ToJingleString(ActionType type) {
  switch (type) {
    case ACTION_SESSION_INITIATE:
      return JINGLE_ACTION_SESSION_INITIATE;
    case ACTION_SESSION_INFO:
      return JINGLE_ACTION_SESSION_INFO;
    case ACTION_SESSION_ACCEPT:
      return JINGLE_ACTION_SESSION_ACCEPT;
    // Notice that reject and terminate both go to
    // "session-terminate", but there is no "session-reject".
    case ACTION_SESSION_REJECT:
    case ACTION_SESSION_TERMINATE:
      return JINGLE_ACTION_SESSION_TERMINATE;
    case ACTION_TRANSPORT_INFO:
      return JINGLE_ACTION_TRANSPORT_INFO;
    case ACTION_TRANSPORT_ACCEPT:
      return JINGLE_ACTION_TRANSPORT_ACCEPT;
    default:
      return "";
  }
}

std::string ToGingleString(ActionType type) {
  switch (type) {
    case ACTION_SESSION_INITIATE:
      return GINGLE_ACTION_INITIATE;
    case ACTION_SESSION_INFO:
      return GINGLE_ACTION_INFO;
    case ACTION_SESSION_ACCEPT:
      return GINGLE_ACTION_ACCEPT;
    case ACTION_SESSION_REJECT:
      return GINGLE_ACTION_REJECT;
    case ACTION_SESSION_TERMINATE:
      return GINGLE_ACTION_TERMINATE;
    case ACTION_VIEW:
      return GINGLE_ACTION_VIEW;
    case ACTION_TRANSPORT_INFO:
      return GINGLE_ACTION_CANDIDATES;
    default:
      return "";
  }
}


bool IsJingleMessage(const buzz::XmlElement* stanza) {
  const buzz::XmlElement* jingle = stanza->FirstNamed(QN_JINGLE);
  if (jingle == NULL)
    return false;

  return (jingle->HasAttr(buzz::QN_ACTION) &&
          (jingle->HasAttr(QN_SID)
           // TODO: This works around a bug in old jingle
           // clients that set QN_ID instead of QN_SID.  Once we know
           // there are no clients which have this bug, we can remove
           // this code.
           || jingle->HasAttr(QN_ID)));
}

bool IsGingleMessage(const buzz::XmlElement* stanza) {
  const buzz::XmlElement* session = stanza->FirstNamed(QN_GINGLE_SESSION);
  if (session == NULL)
    return false;

  return (session->HasAttr(buzz::QN_TYPE) &&
          session->HasAttr(buzz::QN_ID)   &&
          session->HasAttr(QN_INITIATOR));
}

bool IsSessionMessage(const buzz::XmlElement* stanza) {
  return (stanza->Name() == buzz::QN_IQ &&
          stanza->Attr(buzz::QN_TYPE) == buzz::STR_SET &&
          (IsJingleMessage(stanza) ||
           IsGingleMessage(stanza)));
}

bool ParseGingleSessionMessage(const buzz::XmlElement* session,
                               SessionMessage* msg,
                               ParseError* error) {
  msg->protocol = PROTOCOL_GINGLE;
  std::string type_string = session->Attr(buzz::QN_TYPE);
  msg->type = ToActionType(type_string);
  msg->sid = session->Attr(buzz::QN_ID);
  msg->initiator = session->Attr(QN_INITIATOR);
  msg->action_elem = session;

  if (msg->type == ACTION_UNKNOWN)
    return BadParse("unknown action: " + type_string, error);

  return true;
}

bool ParseJingleSessionMessage(const buzz::XmlElement* jingle,
                               SessionMessage* msg,
                               ParseError* error) {
  msg->protocol = PROTOCOL_JINGLE;
  std::string type_string = jingle->Attr(buzz::QN_ACTION);
  msg->type = ToActionType(type_string);
  msg->sid = jingle->Attr(QN_SID);
  // TODO: This works around a bug in old jingle clients
  // that set QN_ID instead of QN_SID.  Once we know there are no
  // clients which have this bug, we can remove this code.
  if (msg->sid.empty()) {
    msg->sid = jingle->Attr(buzz::QN_ID);
  }
  msg->initiator = GetXmlAttr(jingle, QN_INITIATOR, buzz::STR_EMPTY);
  msg->action_elem = jingle;

  if (msg->type == ACTION_UNKNOWN)
    return BadParse("unknown action: " + type_string, error);

  return true;
}

bool ParseHybridSessionMessage(const buzz::XmlElement* jingle,
                               SessionMessage* msg,
                               ParseError* error) {
  if (!ParseJingleSessionMessage(jingle, msg, error))
    return false;
  msg->protocol = PROTOCOL_HYBRID;

  return true;
}

bool ParseSessionMessage(const buzz::XmlElement* stanza,
                         SessionMessage* msg,
                         ParseError* error) {
  msg->id = stanza->Attr(buzz::QN_ID);
  msg->from = stanza->Attr(buzz::QN_FROM);
  msg->to = stanza->Attr(buzz::QN_TO);
  msg->stanza = stanza;

  const buzz::XmlElement* jingle = stanza->FirstNamed(QN_JINGLE);
  const buzz::XmlElement* session = stanza->FirstNamed(QN_GINGLE_SESSION);
  if (jingle && session)
    return ParseHybridSessionMessage(jingle, msg, error);
  if (jingle != NULL)
    return ParseJingleSessionMessage(jingle, msg, error);
  if (session != NULL)
    return ParseGingleSessionMessage(session, msg, error);
  return false;
}

buzz::XmlElement* WriteGingleAction(const SessionMessage& msg,
                                    const XmlElements& action_elems) {
  buzz::XmlElement* session = new buzz::XmlElement(QN_GINGLE_SESSION, true);
  session->AddAttr(buzz::QN_TYPE, ToGingleString(msg.type));
  session->AddAttr(buzz::QN_ID, msg.sid);
  session->AddAttr(QN_INITIATOR, msg.initiator);
  AddXmlChildren(session, action_elems);
  return session;
}

buzz::XmlElement* WriteJingleAction(const SessionMessage& msg,
                                    const XmlElements& action_elems) {
  buzz::XmlElement* jingle = new buzz::XmlElement(QN_JINGLE, true);
  jingle->AddAttr(buzz::QN_ACTION, ToJingleString(msg.type));
  jingle->AddAttr(QN_SID, msg.sid);
  // TODO: This works around a bug in old jingle clinets
  // that expected QN_ID instead of QN_SID.  Once we know there are no
  // clients which have this bug, we can remove this code.
  jingle->AddAttr(QN_ID, msg.sid);
  // TODO: Right now, the XMPP server rejects a jingle-only
  // (non hybrid) message with "feature-not-implemented" if there is
  // no initiator.  Fix the server, and then only set the initiator on
  // session-initiate messages here.
  jingle->AddAttr(QN_INITIATOR, msg.initiator);
  AddXmlChildren(jingle, action_elems);
  return jingle;
}

void WriteSessionMessage(const SessionMessage& msg,
                         const XmlElements& action_elems,
                         buzz::XmlElement* stanza) {
  stanza->SetAttr(buzz::QN_TO, msg.to);
  stanza->SetAttr(buzz::QN_TYPE, buzz::STR_SET);

  if (msg.protocol == PROTOCOL_GINGLE) {
    stanza->AddElement(WriteGingleAction(msg, action_elems));
  } else {
    stanza->AddElement(WriteJingleAction(msg, action_elems));
  }
}


TransportParser* GetTransportParser(const TransportParserMap& trans_parsers,
                                    const std::string& name) {
  TransportParserMap::const_iterator map = trans_parsers.find(name);
  if (map == trans_parsers.end()) {
    return NULL;
  } else {
    return map->second;
  }
}

bool ParseCandidates(SignalingProtocol protocol,
                     const buzz::XmlElement* candidates_elem,
                     const TransportParserMap& trans_parsers,
                     const std::string& transport_type,
                     Candidates* candidates,
                     ParseError* error) {
  TransportParser* trans_parser =
      GetTransportParser(trans_parsers, transport_type);
  if (trans_parser == NULL)
    return BadParse("unknown transport type: " + transport_type, error);

  return trans_parser->ParseCandidates(protocol, candidates_elem,
                                       candidates, error);
}

bool ParseGingleTransportInfos(const buzz::XmlElement* action_elem,
                               const ContentInfos& contents,
                               const TransportParserMap& trans_parsers,
                               TransportInfos* tinfos,
                               ParseError* error) {
  TransportInfo tinfo(CN_OTHER, NS_GINGLE_P2P, Candidates());
  if (!ParseCandidates(PROTOCOL_GINGLE, action_elem,
                       trans_parsers, NS_GINGLE_P2P,
                       &tinfo.candidates, error))
    return false;

  bool has_audio = FindContentInfoByName(contents, CN_AUDIO) != NULL;
  bool has_video = FindContentInfoByName(contents, CN_VIDEO) != NULL;

  // If we don't have media, no need to separate the candidates.
  if (!has_audio && !has_audio) {
    tinfos->push_back(tinfo);
    return true;
  }

  // If we have media, separate the candidates.  Create the
  // TransportInfo here to avoid copying the candidates.
  TransportInfo audio_tinfo(CN_AUDIO, NS_GINGLE_P2P, Candidates());
  TransportInfo video_tinfo(CN_VIDEO, NS_GINGLE_P2P, Candidates());
  for (Candidates::iterator cand = tinfo.candidates.begin();
       cand != tinfo.candidates.end(); cand++) {
    if (cand->name() == GINGLE_CANDIDATE_NAME_RTP ||
        cand->name() == GINGLE_CANDIDATE_NAME_RTCP) {
      audio_tinfo.candidates.push_back(*cand);
    } else if (cand->name() == GINGLE_CANDIDATE_NAME_VIDEO_RTP ||
               cand->name() == GINGLE_CANDIDATE_NAME_VIDEO_RTCP) {
      video_tinfo.candidates.push_back(*cand);
    }
  }

  if (has_audio) {
    tinfos->push_back(audio_tinfo);
  }

  if (has_video) {
    tinfos->push_back(video_tinfo);
  }

  return true;
}

bool ParseJingleTransportInfo(const buzz::XmlElement* trans_elem,
                              const ContentInfo& content,
                              const TransportParserMap& trans_parsers,
                              TransportInfos* tinfos,
                              ParseError* error) {
  std::string transport_type = trans_elem->Name().Namespace();
  TransportInfo tinfo(content.name, transport_type, Candidates());
  if (!ParseCandidates(PROTOCOL_JINGLE, trans_elem,
                       trans_parsers, transport_type,
                       &tinfo.candidates, error))
    return false;

  tinfos->push_back(tinfo);
  return true;
}

bool ParseJingleTransportInfos(const buzz::XmlElement* jingle,
                               const ContentInfos& contents,
                               const TransportParserMap trans_parsers,
                               TransportInfos* tinfos,
                               ParseError* error) {
  for (const buzz::XmlElement* pair_elem
           = jingle->FirstNamed(QN_JINGLE_CONTENT);
       pair_elem != NULL;
       pair_elem = pair_elem->NextNamed(QN_JINGLE_CONTENT)) {
    std::string content_name;
    if (!RequireXmlAttr(pair_elem, QN_JINGLE_CONTENT_NAME,
                        &content_name, error))
      return false;

    const ContentInfo* content = FindContentInfoByName(contents, content_name);
    if (!content)
      return BadParse("Unknown content name: " + content_name, error);

    const buzz::XmlElement* trans_elem;
    if (!RequireXmlChild(pair_elem, LN_TRANSPORT, &trans_elem, error))
      return false;

    if (!ParseJingleTransportInfo(trans_elem, *content, trans_parsers,
                                  tinfos, error))
      return false;
  }

  return true;
}

buzz::XmlElement* NewTransportElement(const std::string& name) {
  return new buzz::XmlElement(buzz::QName(true, name, LN_TRANSPORT), true);
}

bool WriteCandidates(SignalingProtocol protocol,
                     const std::string& trans_type,
                     const Candidates& candidates,
                     const TransportParserMap& trans_parsers,
                     XmlElements* elems,
                     WriteError* error) {
  TransportParser* trans_parser = GetTransportParser(trans_parsers, trans_type);
  if (trans_parser == NULL)
    return BadWrite("unknown transport type: " + trans_type, error);

  return trans_parser->WriteCandidates(protocol, candidates, elems, error);
}

bool WriteGingleTransportInfos(const TransportInfos& tinfos,
                               const TransportParserMap& trans_parsers,
                               XmlElements* elems,
                               WriteError* error) {
  for (TransportInfos::const_iterator tinfo = tinfos.begin();
       tinfo != tinfos.end(); ++tinfo) {
    if (!WriteCandidates(PROTOCOL_GINGLE,
                         tinfo->transport_type, tinfo->candidates,
                         trans_parsers, elems, error))
      return false;
  }

  return true;
}

bool WriteJingleTransportInfo(const TransportInfo& tinfo,
                              const TransportParserMap& trans_parsers,
                              XmlElements* elems,
                              WriteError* error) {
  XmlElements candidate_elems;
  if (!WriteCandidates(PROTOCOL_JINGLE,
                       tinfo.transport_type, tinfo.candidates, trans_parsers,
                       &candidate_elems, error))
    return false;

  buzz::XmlElement* trans_elem = NewTransportElement(tinfo.transport_type);
  AddXmlChildren(trans_elem, candidate_elems);
  elems->push_back(trans_elem);
  return true;
}

void WriteJingleContentPair(const std::string name,
                            const XmlElements& pair_elems,
                            XmlElements* elems) {
  buzz::XmlElement* pair_elem = new buzz::XmlElement(QN_JINGLE_CONTENT);
  pair_elem->SetAttr(QN_JINGLE_CONTENT_NAME, name);
  pair_elem->SetAttr(QN_CREATOR, LN_INITIATOR);
  AddXmlChildren(pair_elem, pair_elems);

  elems->push_back(pair_elem);
}

bool WriteJingleTransportInfos(const TransportInfos& tinfos,
                               const TransportParserMap& trans_parsers,
                               XmlElements* elems,
                               WriteError* error) {
  for (TransportInfos::const_iterator tinfo = tinfos.begin();
       tinfo != tinfos.end(); ++tinfo) {
    XmlElements pair_elems;
    if (!WriteJingleTransportInfo(*tinfo, trans_parsers,
                                  &pair_elems, error))
      return false;

    WriteJingleContentPair(tinfo->content_name, pair_elems, elems);
  }

  return true;
}

ContentParser* GetContentParser(const ContentParserMap& content_parsers,
                                const std::string& type) {
  ContentParserMap::const_iterator map = content_parsers.find(type);
  if (map == content_parsers.end()) {
    return NULL;
  } else {
    return map->second;
  }
}

bool ParseContentInfo(SignalingProtocol protocol,
                      const std::string& name,
                      const std::string& type,
                      const buzz::XmlElement* elem,
                      const ContentParserMap& parsers,
                      ContentInfos* contents,
                      ParseError* error) {
  ContentParser* parser = GetContentParser(parsers, type);
  if (parser == NULL)
    return BadParse("unknown application content: " + type, error);

  const ContentDescription* desc;
  if (!parser->ParseContent(protocol, elem, &desc, error))
    return false;

  contents->push_back(ContentInfo(name, type, desc));
  return true;
}

bool ParseContentType(const buzz::XmlElement* parent_elem,
                      std::string* content_type,
                      const buzz::XmlElement** content_elem,
                      ParseError* error) {
  if (!RequireXmlChild(parent_elem, LN_DESCRIPTION, content_elem, error))
    return false;

  *content_type = (*content_elem)->Name().Namespace();
  return true;
}

bool ParseGingleContentInfos(const buzz::XmlElement* session,
                             const ContentParserMap& content_parsers,
                             ContentInfos* contents,
                             ParseError* error) {
  std::string content_type;
  const buzz::XmlElement* content_elem;
  if (!ParseContentType(session, &content_type, &content_elem, error))
    return false;

  if (content_type == NS_GINGLE_VIDEO) {
    // A parser parsing audio or video content should look at the
    // namespace and only parse the codecs relevant to that namespace.
    // We use this to control which codecs get parsed: first audio,
    // then video.
    talk_base::scoped_ptr<buzz::XmlElement> audio_elem(
        new buzz::XmlElement(QN_GINGLE_AUDIO_CONTENT));
    CopyXmlChildren(content_elem, audio_elem.get());
    if (!ParseContentInfo(PROTOCOL_GINGLE, CN_AUDIO, NS_JINGLE_RTP,
                          audio_elem.get(), content_parsers,
                          contents, error))
      return false;

    if (!ParseContentInfo(PROTOCOL_GINGLE, CN_VIDEO, NS_JINGLE_RTP,
                          content_elem, content_parsers,
                          contents, error))
      return false;
  } else if (content_type == NS_GINGLE_AUDIO) {
    if (!ParseContentInfo(PROTOCOL_GINGLE, CN_AUDIO, NS_JINGLE_RTP,
                          content_elem, content_parsers,
                          contents, error))
      return false;
  } else {
    if (!ParseContentInfo(PROTOCOL_GINGLE, CN_OTHER, content_type,
                          content_elem, content_parsers,
                          contents, error))
      return false;
  }
  return true;
}

bool ParseJingleContentInfos(const buzz::XmlElement* jingle,
                             const ContentParserMap& content_parsers,
                             ContentInfos* contents,
                             ParseError* error) {
  for (const buzz::XmlElement* pair_elem
           = jingle->FirstNamed(QN_JINGLE_CONTENT);
       pair_elem != NULL;
       pair_elem = pair_elem->NextNamed(QN_JINGLE_CONTENT)) {
    std::string content_name;
    if (!RequireXmlAttr(pair_elem, QN_JINGLE_CONTENT_NAME,
                        &content_name, error))
      return false;

    std::string content_type;
    const buzz::XmlElement* content_elem;
    if (!ParseContentType(pair_elem, &content_type, &content_elem, error))
      return false;

    if (!ParseContentInfo(PROTOCOL_JINGLE, content_name, content_type,
                          content_elem, content_parsers,
                          contents, error))
      return false;
  }
  return true;
}

buzz::XmlElement* WriteContentInfo(SignalingProtocol protocol,
                                   const ContentInfo& content,
                                   const ContentParserMap& parsers,
                                   WriteError* error) {
  ContentParser* parser = GetContentParser(parsers, content.type);
  if (parser == NULL) {
    BadWrite("unknown content type: " + content.type, error);
    return NULL;
  }

  buzz::XmlElement* elem = NULL;
  if (!parser->WriteContent(protocol, content.description, &elem, error))
    return NULL;

  return elem;
}

bool WriteGingleContentInfos(const ContentInfos& contents,
                             const ContentParserMap& parsers,
                             XmlElements* elems,
                             WriteError* error) {
  if (contents.size() == 1) {
    buzz::XmlElement* elem = WriteContentInfo(
        PROTOCOL_GINGLE, contents.front(), parsers, error);
    if (!elem)
      return false;

    elems->push_back(elem);
  } else if (contents.size() == 2 &&
             contents.at(0).type == NS_JINGLE_RTP &&
             contents.at(1).type == NS_JINGLE_RTP) {
     // Special-case audio + video contents so that they are "merged"
     // into one "video" content.
    buzz::XmlElement* audio = WriteContentInfo(
        PROTOCOL_GINGLE, contents.at(0), parsers, error);
    if (!audio)
      return false;

    buzz::XmlElement* video = WriteContentInfo(
        PROTOCOL_GINGLE, contents.at(1), parsers, error);
    if (!video) {
      delete audio;
      return false;
    }

    CopyXmlChildren(audio, video);
    elems->push_back(video);
    delete audio;
  } else {
    return BadWrite("Gingle protocol may only have one content.", error);
  }

  return true;
}

const TransportInfo* GetTransportInfoByContentName(
    const TransportInfos& tinfos, const std::string& content_name) {
  for (TransportInfos::const_iterator tinfo = tinfos.begin();
       tinfo != tinfos.end(); ++tinfo) {
    if (content_name == tinfo->content_name) {
      return &*tinfo;
    }
  }
  return NULL;
}

bool WriteJingleContentPairs(const ContentInfos& contents,
                             const ContentParserMap& content_parsers,
                             const TransportInfos& tinfos,
                             const TransportParserMap& trans_parsers,
                             XmlElements* elems,
                             WriteError* error) {
  for (ContentInfos::const_iterator content = contents.begin();
       content != contents.end(); ++content) {
    const TransportInfo* tinfo =
        GetTransportInfoByContentName(tinfos, content->name);
    if (!tinfo)
      return BadWrite("No transport for content: " + content->name, error);

    XmlElements pair_elems;
    buzz::XmlElement* elem = WriteContentInfo(
        PROTOCOL_JINGLE, *content, content_parsers, error);
    if (!elem)
      return false;
    pair_elems.push_back(elem);

    if (!WriteJingleTransportInfo(*tinfo, trans_parsers,
                                  &pair_elems, error))
      return false;

    WriteJingleContentPair(content->name, pair_elems, elems);
  }
  return true;
}

bool ParseContentType(SignalingProtocol protocol,
                      const buzz::XmlElement* action_elem,
                      std::string* content_type,
                      ParseError* error) {
  const buzz::XmlElement* content_elem;
  if (protocol == PROTOCOL_GINGLE) {
    if (!ParseContentType(action_elem, content_type, &content_elem, error))
      return false;

    // Internally, we only use NS_JINGLE_RTP.
    if (*content_type == NS_GINGLE_AUDIO ||
        *content_type == NS_GINGLE_VIDEO)
      *content_type = NS_JINGLE_RTP;
  } else {
    const buzz::XmlElement* pair_elem
        = action_elem->FirstNamed(QN_JINGLE_CONTENT);
    if (pair_elem == NULL)
      return BadParse("No contents found", error);

    if (!ParseContentType(pair_elem, content_type, &content_elem, error))
      return false;

    // If there is more than one content type, return an error.
    for (; pair_elem != NULL;
         pair_elem = pair_elem->NextNamed(QN_JINGLE_CONTENT)) {
      std::string content_type2;
      if (!ParseContentType(pair_elem, &content_type2, &content_elem, error))
        return false;

      if (content_type2 != *content_type)
        return BadParse("More than one content type found", error);
    }
  }

  return true;
}

bool ParseSessionInitiate(SignalingProtocol protocol,
                          const buzz::XmlElement* action_elem,
                          const ContentParserMap& content_parsers,
                          const TransportParserMap& trans_parsers,
                          SessionInitiate* init,
                          ParseError* error) {
  init->owns_contents = true;
  if (protocol == PROTOCOL_GINGLE) {
    if (!ParseGingleContentInfos(action_elem, content_parsers,
                                 &init->contents, error))
      return false;

    if (!ParseGingleTransportInfos(action_elem, init->contents, trans_parsers,
                                   &init->transports, error))
      return false;
  } else {
    if (!ParseJingleContentInfos(action_elem, content_parsers,
                                 &init->contents, error))
      return false;

    if (!ParseJingleTransportInfos(action_elem, init->contents, trans_parsers,
                                   &init->transports, error))
      return false;
  }

  return true;
}


bool WriteSessionInitiate(SignalingProtocol protocol,
                          const ContentInfos& contents,
                          const TransportInfos& tinfos,
                          const ContentParserMap& content_parsers,
                          const TransportParserMap& transport_parsers,
                          XmlElements* elems,
                          WriteError* error) {
  if (protocol == PROTOCOL_GINGLE) {
    if (!WriteGingleContentInfos(contents, content_parsers, elems, error))
      return false;

    if (!WriteGingleTransportInfos(tinfos, transport_parsers,
                                   elems, error))
      return false;
  } else {
    if (!WriteJingleContentPairs(contents, content_parsers,
                                 tinfos, transport_parsers,
                                 elems, error))
      return false;
  }

  return true;
}

bool ParseSessionAccept(SignalingProtocol protocol,
                        const buzz::XmlElement* action_elem,
                        const ContentParserMap& content_parsers,
                        const TransportParserMap& transport_parsers,
                        SessionAccept* accept,
                        ParseError* error) {
  return ParseSessionInitiate(protocol, action_elem,
                              content_parsers, transport_parsers,
                              accept, error);
}

bool WriteSessionAccept(SignalingProtocol protocol,
                        const ContentInfos& contents,
                        const TransportInfos& tinfos,
                        const ContentParserMap& content_parsers,
                        const TransportParserMap& transport_parsers,
                        XmlElements* elems,
                        WriteError* error) {
  return WriteSessionInitiate(protocol, contents, tinfos,
                              content_parsers, transport_parsers,
                              elems, error);
}

bool ParseSessionTerminate(SignalingProtocol protocol,
                           const buzz::XmlElement* action_elem,
                           SessionTerminate* term,
                           ParseError* error) {
  if (protocol == PROTOCOL_GINGLE) {
    const buzz::XmlElement* reason_elem = action_elem->FirstElement();
    if (reason_elem != NULL) {
      term->reason = reason_elem->Name().LocalPart();
      const buzz::XmlElement *debug_elem = reason_elem->FirstElement();
      if (debug_elem != NULL) {
        term->debug_reason = debug_elem->Name().LocalPart();
      }
    }
    return true;
  } else {
    const buzz::XmlElement* reason_elem =
        action_elem->FirstNamed(QN_JINGLE_REASON);
    if (reason_elem) {
      reason_elem = reason_elem->FirstElement();
      if (reason_elem) {
        term->reason = reason_elem->Name().LocalPart();
      }
    }
    return true;
  }
}

void WriteSessionTerminate(SignalingProtocol protocol,
                           const SessionTerminate& term,
                           XmlElements* elems) {
  if (protocol == PROTOCOL_GINGLE) {
    elems->push_back(new buzz::XmlElement(
        buzz::QName(true, NS_GINGLE, term.reason)));
  } else {
    if (!term.reason.empty()) {
      buzz::XmlElement* reason_elem = new buzz::XmlElement(QN_JINGLE_REASON);
      reason_elem->AddElement(new buzz::XmlElement(
          buzz::QName(true, NS_JINGLE, term.reason)));
      elems->push_back(reason_elem);
    }
  }
}

bool ParseTransportInfos(SignalingProtocol protocol,
                         const buzz::XmlElement* action_elem,
                         const ContentInfos& contents,
                         const TransportParserMap& trans_parsers,
                         TransportInfos* tinfos,
                         ParseError* error) {
  if (protocol == PROTOCOL_GINGLE) {
    return ParseGingleTransportInfos(
        action_elem, contents, trans_parsers, tinfos, error);
  } else {
    return ParseJingleTransportInfos(
        action_elem, contents, trans_parsers, tinfos, error);
  }
}

bool WriteTransportInfos(SignalingProtocol protocol,
                         const TransportInfos& tinfos,
                         const TransportParserMap& trans_parsers,
                         XmlElements* elems,
                         WriteError* error) {
  if (protocol == PROTOCOL_GINGLE) {
    return WriteGingleTransportInfos(tinfos, trans_parsers,
                                     elems, error);
  } else {
    return WriteJingleTransportInfos(tinfos, trans_parsers,
                                     elems, error);
  }
}

bool ParseSessionNotify(const buzz::XmlElement* action_elem,
                        SessionNotify* notify, ParseError* error) {
  const buzz::XmlElement* notify_elem;
  for (notify_elem = action_elem->FirstNamed(QN_GINGLE_NOTIFY);
      notify_elem != NULL;
      notify_elem = notify_elem->NextNamed(QN_GINGLE_NOTIFY)) {
    // Note that a subsequent notify element for the same user will override a
    // previous.  We don't merge them.
    std::string nick(notify_elem->Attr(QN_GINGLE_NOTIFY_NICK));
    if (nick != buzz::STR_EMPTY) {
      MediaSources sources;
      const buzz::XmlElement* source_elem;
      for (source_elem = notify_elem->FirstNamed(QN_GINGLE_NOTIFY_SOURCE);
          source_elem != NULL;
          source_elem = source_elem->NextNamed(QN_GINGLE_NOTIFY_SOURCE)) {
        std::string ssrc = source_elem->Attr(QN_GINGLE_NOTIFY_SOURCE_SSRC);
        if (ssrc != buzz::STR_EMPTY) {
          std::string mtype = source_elem->Attr(QN_GINGLE_NOTIFY_SOURCE_MTYPE);
          if (mtype == GINGLE_NOTIFY_SOURCE_MTYPE_AUDIO) {
            sources.audio_ssrc = strtoul(ssrc.c_str(), NULL, 10);
          } else if (mtype == GINGLE_NOTIFY_SOURCE_MTYPE_VIDEO) {
            sources.video_ssrc = strtoul(ssrc.c_str(), NULL, 10);
          }
        }
      }

      notify->nickname_to_sources.insert(
          std::pair<std::string, MediaSources>(nick, sources));
    }
  }

  return true;
}

bool GetUriTarget(const std::string& prefix, const std::string& str,
                  std::string* after) {
  size_t pos = str.find(prefix);
  if (pos == std::string::npos)
    return false;

  *after = str.substr(pos + prefix.size(), std::string::npos);
  return true;
}

bool ParseSessionUpdate(const buzz::XmlElement* action_elem,
                        SessionUpdate* update, ParseError* error) {
  // TODO: Parse the update message.
  return true;
}

void WriteSessionView(const SessionView& view, XmlElements* elems) {
  std::vector<VideoViewRequest>::const_iterator it;
  for (it = view.view_requests.begin(); it != view.view_requests.end(); it++) {
    talk_base::scoped_ptr<buzz::XmlElement> view_elem(
        new buzz::XmlElement(QN_GINGLE_VIEW));
    if (view_elem.get() == NULL) {
      return;
    }

    view_elem->SetAttr(QN_GINGLE_VIEW_TYPE, GINGLE_VIEW_TYPE_STATIC);
    view_elem->SetAttr(QN_GINGLE_VIEW_NICK, it->nick_name);
    view_elem->SetAttr(QN_GINGLE_VIEW_MEDIA_TYPE,
        GINGLE_VIEW_MEDIA_TYPE_VIDEO);

    // A 32-bit uint, expressed as decimal, has a max of 10 digits, plus one
    // for the null.
    char str[11];
    int result = talk_base::sprintfn(str, ARRAY_SIZE(str), "%u", it->ssrc);
    if (result < 0 || result >= ARRAY_SIZE(str)) {
      continue;
    }
    view_elem->SetAttr(QN_GINGLE_VIEW_SSRC, str);

    // Include video-specific parameters in a child <params> element.
    talk_base::scoped_ptr<buzz::XmlElement> params_elem(
        new buzz::XmlElement(QN_GINGLE_VIEW_PARAMS));
    if (params_elem.get() == NULL) {
      return;
    }

    result = talk_base::sprintfn(str, ARRAY_SIZE(str), "%u", it->width);
    if (result < 0 || result >= ARRAY_SIZE(str)) {
      continue;
    }
    params_elem->SetAttr(QN_GINGLE_VIEW_PARAMS_WIDTH, str);

    result = talk_base::sprintfn(str, ARRAY_SIZE(str), "%u", it->height);
    if (result < 0 || result >= ARRAY_SIZE(str)) {
      continue;
    }
    params_elem->SetAttr(QN_GINGLE_VIEW_PARAMS_HEIGHT, str);

    result = talk_base::sprintfn(str, ARRAY_SIZE(str), "%u", it->framerate);
    if (result < 0 || result >= ARRAY_SIZE(str)) {
      continue;
    }
    params_elem->SetAttr(QN_GINGLE_VIEW_PARAMS_FRAMERATE, str);

    view_elem->AddElement(params_elem.release());
    elems->push_back(view_elem.release());
  }
}

bool FindSessionRedirect(const buzz::XmlElement* stanza,
                         SessionRedirect* redirect) {
  const buzz::XmlElement* error_elem = GetXmlChild(stanza, LN_ERROR);
  if (error_elem == NULL)
    return false;

  const buzz::XmlElement* redirect_elem =
      error_elem->FirstNamed(QN_GINGLE_REDIRECT);
  if (redirect_elem == NULL)
    redirect_elem = error_elem->FirstNamed(buzz::QN_STANZA_REDIRECT);
  if (redirect_elem == NULL)
    return false;

  if (!GetUriTarget(STR_REDIRECT_PREFIX, redirect_elem->BodyText(),
                    &redirect->target))
    return false;

  return true;
}

}  // namespace cricket
