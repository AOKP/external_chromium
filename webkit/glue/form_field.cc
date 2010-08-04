// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/form_field.h"

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "third_party/WebKit/WebKit/chromium/public/WebInputElement.h"
#include "third_party/WebKit/WebKit/chromium/public/WebOptionElement.h"
#include "third_party/WebKit/WebKit/chromium/public/WebSelectElement.h"

using WebKit::WebFormControlElement;
using WebKit::WebElement;
using WebKit::WebInputElement;
using WebKit::WebOptionElement;
using WebKit::WebSelectElement;
using WebKit::WebVector;

namespace webkit_glue {

FormField::FormField()
    : size_(0) {
}

// TODO(jhawkins): This constructor should probably be deprecated and the
// functionality moved to FormManager.
FormField::FormField(WebFormControlElement element)
    : size_(0) {
  name_ = element.nameForAutofill();

  // TODO(jhawkins): Extract the field label.  For now we just use the field
  // name.
  label_ = name_;

  form_control_type_ = element.formControlType();
  if (form_control_type_ == ASCIIToUTF16("text")) {
    const WebInputElement& input_element = element.toConst<WebInputElement>();
    value_ = input_element.value();
    size_ = input_element.size();
  } else if (form_control_type_ == ASCIIToUTF16("select-one")) {
    WebSelectElement select_element = element.to<WebSelectElement>();
    value_ = select_element.value();

    // For select-one elements copy option strings.
    WebVector<WebElement> list_items = select_element.listItems();
    option_strings_.reserve(list_items.size());
    for (size_t i = 0; i < list_items.size(); ++i) {
      if (list_items[i].hasTagName("option"))
        option_strings_.push_back(list_items[i].to<WebOptionElement>().value());
    }
  }

  TrimWhitespace(value_, TRIM_LEADING, &value_);
}

FormField::FormField(const string16& label,
                     const string16& name,
                     const string16& value,
                     const string16& form_control_type,
                     int size)
  : label_(label),
    name_(name),
    value_(value),
    form_control_type_(form_control_type),
    size_(size) {
}

bool FormField::operator==(const FormField& field) const {
  // A FormField stores a value, but the value is not part of the identity of
  // the field, so we don't want to compare the values.
  return (label_ == field.label_ &&
          name_ == field.name_ &&
          form_control_type_ == field.form_control_type_ &&
          size_ == field.size_);
}

bool FormField::operator!=(const FormField& field) const {
  return !operator==(field);
}

bool FormField::StrictlyEqualsHack(const FormField& field) const {
  return (label_ == field.label_ &&
          name_ == field.name_ &&
          value_ == field.value_ &&
          form_control_type_ == field.form_control_type_ &&
          size_ == field.size_);
}

std::ostream& operator<<(std::ostream& os, const FormField& field) {
  return os
      << UTF16ToUTF8(field.label())
      << " "
      << UTF16ToUTF8(field.name())
      << " "
      << UTF16ToUTF8(field.value())
      << " "
      << UTF16ToUTF8(field.form_control_type())
      << " "
      << field.size();
}

}  // namespace webkit_glue
