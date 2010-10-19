// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_SELECT_CONTROL_HANDLER_H_
#define CHROME_BROWSER_AUTOFILL_SELECT_CONTROL_HANDLER_H_
#pragma once

#include "chrome/browser/autofill/autofill_type.h"

class FormGroup;

namespace webkit_glue {
class FormField;
}  // namespace webkit_glue

namespace autofill {

// TODO(jhawkins): This definitely needs tests.

// Fills a select-one control with the appropriate value from |form_group|.
// Finds the matching value for field types that we know contain different
// variations of a value, e.g., (tx, TX, Texas) or credit card expiration
// months, e.g., (04, April).
void FillSelectControl(const FormGroup* form_group,
                       AutoFillType type,
                       webkit_glue::FormField* field);

}  // namespace autofill

#endif  // CHROME_BROWSER_AUTOFILL_SELECT_CONTROL_HANDLER_H_
