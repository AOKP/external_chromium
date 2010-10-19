// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/autofill_xml_parser.h"

#include <string>
#include <vector>

#include "chrome/browser/autofill/autofill_type.h"
#include "third_party/libjingle/overrides/talk/xmllite/qname.h"

AutoFillXmlParser::AutoFillXmlParser()
    : succeeded_(true) {
}

void AutoFillXmlParser::CharacterData(
    buzz::XmlParseContext* context, const char* text, int len) {
}

void AutoFillXmlParser::EndElement(buzz::XmlParseContext* context,
                                   const char* name) {
}

void AutoFillXmlParser::Error(buzz::XmlParseContext* context,
                              XML_Error error_code) {
  succeeded_ = false;
}

AutoFillQueryXmlParser::AutoFillQueryXmlParser(
    std::vector<AutoFillFieldType>* field_types,
    UploadRequired* upload_required)
    : field_types_(field_types),
      upload_required_(upload_required) {
  DCHECK(upload_required_);
}

void AutoFillQueryXmlParser::StartElement(buzz::XmlParseContext* context,
                                          const char* name,
                                          const char** attrs) {
  buzz::QName qname = context->ResolveQName(name, false);
  const std::string &element = qname.LocalPart();
  if (element.compare("autofillqueryresponse") == 0) {
    // Check for the upload required attribute.  If it's not present, we use the
    // default upload rates.
    *upload_required_ = USE_UPLOAD_RATES;
    if (*attrs) {
      buzz::QName attribute_qname = context->ResolveQName(attrs[0], true);
      const std::string &attribute_name = attribute_qname.LocalPart();
      if (attribute_name.compare("uploadrequired") == 0) {
        if (strcmp(attrs[1], "true") == 0)
          *upload_required_ = UPLOAD_REQUIRED;
        else if (strcmp(attrs[1], "false") == 0)
          *upload_required_ = UPLOAD_NOT_REQUIRED;
      }
    }
  } else if (element.compare("field") == 0) {
    if (!attrs[0]) {
      // Missing the "autofilltype" attribute, abort.
      context->RaiseError(XML_ERROR_ABORTED);
      return;
    }

    // Determine the field type from the attribute value.  There should be one
    // attribute (autofilltype) with an integer value.
    AutoFillFieldType field_type = UNKNOWN_TYPE;
    buzz::QName attribute_qname = context->ResolveQName(attrs[0], true);
    const std::string &attribute_name = attribute_qname.LocalPart();

    if (attribute_name.compare("autofilltype") == 0) {
      int value = GetIntValue(context, attrs[1]);
      field_type = static_cast<AutoFillFieldType>(value);
      if (field_type < 0 || field_type > MAX_VALID_FIELD_TYPE) {
        field_type = NO_SERVER_DATA;
      }
    }

    // Record this field type.
    field_types_->push_back(field_type);
  }
}

int AutoFillQueryXmlParser::GetIntValue(buzz::XmlParseContext* context,
                                        const char* attribute) {
  char* attr_end = NULL;
  int value = strtol(attribute, &attr_end, 10);
  if (attr_end != NULL && attr_end == attribute) {
    context->RaiseError(XML_ERROR_SYNTAX);
    return 0;
  }
  return value;
}

AutoFillUploadXmlParser::AutoFillUploadXmlParser(double* positive_upload_rate,
                                                 double* negative_upload_rate)
    : succeeded_(false),
      positive_upload_rate_(positive_upload_rate),
      negative_upload_rate_(negative_upload_rate) {
  DCHECK(positive_upload_rate_);
  DCHECK(negative_upload_rate_);
}

void AutoFillUploadXmlParser::StartElement(buzz::XmlParseContext* context,
                                           const char* name,
                                           const char** attrs) {
  buzz::QName qname = context->ResolveQName(name, false);
  const std::string &element = qname.LocalPart();
  if (element.compare("autofilluploadresponse") == 0) {
    // Loop over all attributes to get the upload rates.
    while (*attrs) {
      buzz::QName attribute_qname = context->ResolveQName(attrs[0], true);
      const std::string &attribute_name = attribute_qname.LocalPart();
      if (attribute_name.compare("positiveuploadrate") == 0) {
        *positive_upload_rate_ = GetDoubleValue(context, attrs[1]);
      } else if (attribute_name.compare("negativeuploadrate") == 0) {
        *negative_upload_rate_ = GetDoubleValue(context, attrs[1]);
      }
      attrs += 2;  // We peeked at attrs[0] and attrs[1], skip past both.
    }
  }
}

double AutoFillUploadXmlParser::GetDoubleValue(buzz::XmlParseContext* context,
                                               const char* attribute) {
  char* attr_end = NULL;
  double value = strtod(attribute, &attr_end);
  if (attr_end != NULL && attr_end == attribute) {
    context->RaiseError(XML_ERROR_SYNTAX);
    return 0.0;
  }
  return value;
}
