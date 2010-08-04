// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/address_field.h"

#include "base/logging.h"
#include "base/scoped_ptr.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "chrome/browser/autofill/autofill_field.h"

bool AddressField::GetFieldInfo(FieldTypeMap* field_type_map) const {
  AutoFillFieldType address_company;
  AutoFillFieldType address_line1;
  AutoFillFieldType address_line2;
  AutoFillFieldType address_appt_num;
  AutoFillFieldType address_city;
  AutoFillFieldType address_state;
  AutoFillFieldType address_zip;
  AutoFillFieldType address_country;

  switch (type_) {
    case kShippingAddress:
     // Fall through. AutoFill does not support shipping addresses.
    case kGenericAddress:
      address_company = COMPANY_NAME;
      address_line1 = ADDRESS_HOME_LINE1;
      address_line2 = ADDRESS_HOME_LINE2;
      address_appt_num = ADDRESS_HOME_APT_NUM;
      address_city = ADDRESS_HOME_CITY;
      address_state = ADDRESS_HOME_STATE;
      address_zip = ADDRESS_HOME_ZIP;
      address_country = ADDRESS_HOME_COUNTRY;
      break;

    case kBillingAddress:
      address_company = COMPANY_NAME;
      address_line1 = ADDRESS_BILLING_LINE1;
      address_line2 = ADDRESS_BILLING_LINE2;
      address_appt_num = ADDRESS_BILLING_APT_NUM;
      address_city = ADDRESS_BILLING_CITY;
      address_state = ADDRESS_BILLING_STATE;
      address_zip = ADDRESS_BILLING_ZIP;
      address_country = ADDRESS_BILLING_COUNTRY;
      break;

    default:
      NOTREACHED();
      return false;
  }

  bool ok;
  ok = Add(field_type_map, company_, AutoFillType(address_company));
  DCHECK(ok);
  ok = ok && Add(field_type_map, address1_, AutoFillType(address_line1));
  DCHECK(ok);
  ok = ok && Add(field_type_map, address2_, AutoFillType(address_line2));
  DCHECK(ok);
  ok = ok && Add(field_type_map, city_, AutoFillType(address_city));
  DCHECK(ok);
  ok = ok && Add(field_type_map, state_, AutoFillType(address_state));
  DCHECK(ok);
  ok = ok && Add(field_type_map, zip_, AutoFillType(address_zip));
  DCHECK(ok);
  ok = ok && Add(field_type_map, country_, AutoFillType(address_country));
  DCHECK(ok);

  return ok;
}

AddressField* AddressField::Parse(
    std::vector<AutoFillField*>::const_iterator* iter,
    bool is_ecml) {
  DCHECK(iter);
  if (!iter)
    return NULL;

  scoped_ptr<AddressField> address_field(new AddressField);
  std::vector<AutoFillField*>::const_iterator q = *iter;
  string16 pattern;

  // The ECML standard uses 2 letter country codes.  So we will
  // have to remember that this is an ECML form, for when we fill
  // it out.
  address_field->is_ecml_ = is_ecml;

  // Allow address fields to appear in any order.
  while (true) {
    if (ParseCompany(&q, is_ecml, address_field.get()) ||
        ParseAddressLines(&q, is_ecml, address_field.get()) ||
        ParseCity(&q, is_ecml, address_field.get()) ||
        ParseZipCode(&q, is_ecml, address_field.get()) ||
        ParseCountry(&q, is_ecml, address_field.get())) {
      continue;
    } else if ((!address_field->state_ || address_field->state_->IsEmpty()) &&
               address_field->ParseState(&q, is_ecml, address_field.get())) {
      continue;
    } else if (ParseText(&q, ASCIIToUTF16("attention|attn.")) ||
               ParseText(&q, ASCIIToUTF16("province|region|other"))) {
      // We ignore the following:
      // * Attention.
      // * Province/Region/Other.
      continue;
    } else if (*q != **iter && ParseEmpty(&q)) {
      // Ignore non-labeled fields within an address; the page
      // MapQuest Driving Directions North America.html contains such a field.
      // We only ignore such fields after we've parsed at least one other field;
      // otherwise we'd effectively parse address fields before other field
      // types after any non-labeled fields, and we want email address fields to
      // have precedence since some pages contain fields labeled
      // "Email address".
      continue;
    } else {
      // No field found.
      break;
    }
  }

  // If we have identified any address fields in this field then it should be
  // added to the list of fields.
  if (address_field->company_ != NULL ||
      address_field->address1_ != NULL || address_field->address2_ != NULL ||
      address_field->city_ != NULL || address_field->state_ != NULL ||
      address_field->zip_ != NULL || address_field->zip4_ ||
      address_field->country_ != NULL) {
    *iter = q;
    return address_field.release();
  }

  return NULL;
}

AddressType AddressField::FindType() const {
  // This is not a full address, so don't even bother trying to figure
  // out its type.
  if (address1_ == NULL)
    return kGenericAddress;

  // First look at the field name, which itself will sometimes contain
  // "bill" or "ship".  We could check for the ECML type prefixes
  // here, but there's no need to since ECML's prefixes Ecom_BillTo
  // and Ecom_ShipTo contain "bill" and "ship" anyway.
  string16 name = StringToLowerASCII(address1_->name());
  return AddressTypeFromText(name);
}

AddressField::AddressField()
    : company_(NULL),
      address1_(NULL),
      address2_(NULL),
      city_(NULL),
      state_(NULL),
      zip_(NULL),
      zip4_(NULL),
      country_(NULL),
      type_(kGenericAddress),
      is_ecml_(false) {
}

// static
bool AddressField::ParseCompany(
    std::vector<AutoFillField*>::const_iterator* iter,
    bool is_ecml, AddressField* address_field) {
  if (address_field->company_ && !address_field->company_->IsEmpty())
    return false;

  string16 pattern;
  if (is_ecml)
    pattern = GetEcmlPattern(kEcmlShipToCompanyName,
                             kEcmlBillToCompanyName, '|');
  else
    pattern = ASCIIToUTF16("company|business name");

  if (!ParseText(iter, pattern, &address_field->company_))
    return false;

  return true;
}

// static
bool AddressField::ParseAddressLines(
    std::vector<AutoFillField*>::const_iterator* iter,
    bool is_ecml, AddressField* address_field) {
  // We only match the string "address" in page text, not in element names,
  // because sometimes every element in a group of address fields will have
  // a name containing the string "address"; for example, on the page
  // Kohl's - Register Billing Address.html the text element labeled "city"
  // has the name "BILL_TO_ADDRESS<>city".  We do match address labels
  // such as "address1", which appear as element names on various pages (eg
  // AmericanGirl-Registration.html, BloomingdalesBilling.html,
  // EBay Registration Enter Information.html).
  if (address_field->address1_)
    return false;

  string16 pattern;
  if (is_ecml) {
    pattern = GetEcmlPattern(kEcmlShipToAddress1,
                             kEcmlBillToAddress1, '|');
    if (!ParseText(iter, pattern, &address_field->address1_))
      return false;
  } else {
    pattern =
        ASCIIToUTF16("street|address line|address1|street_line1|addr1");
    string16 label_pattern = ASCIIToUTF16("address");

    if (!ParseText(iter, pattern, &address_field->address1_))
      if (!ParseLabelText(iter, label_pattern, &address_field->address1_))
        return false;
  }

  // Some pages (e.g. expedia_checkout.html) have an apartment or
  // suite number at this point.  The occasional page (e.g.
  // Ticketmaster3.html) calls this a unit number.  We ignore this
  // field since we can't fill it yet.
  ParseText(iter, ASCIIToUTF16("suite|unit"));

  // Optionally parse more address lines, which may have empty labels.
  // Some pages have 3 address lines (eg SharperImageModifyAccount.html)
  // Some pages even have 4 address lines (e.g. uk/ShoesDirect2.html)!
  if (is_ecml) {
    pattern = GetEcmlPattern(kEcmlShipToAddress2,
                             kEcmlBillToAddress2, '|');
    if (!ParseEmptyText(iter, &address_field->address2_))
      ParseText(iter, pattern, &address_field->address2_);
  } else {
    pattern = ASCIIToUTF16("address2|street|street_line2|addr2");
    string16 label_pattern = ASCIIToUTF16("address");
    if (!ParseEmptyText(iter, &address_field->address2_))
      if (!ParseText(iter, pattern, &address_field->address2_))
        ParseLabelText(iter, label_pattern, &address_field->address2_);
  }

  // Try for a third line, which we will promptly discard.
  if (address_field->address2_ != NULL) {
    if (is_ecml) {
      pattern = GetEcmlPattern(kEcmlShipToAddress3,
                               kEcmlBillToAddress3, '|');
      ParseText(iter, pattern);
    } else {
      pattern = ASCIIToUTF16("line3");
      ParseLabelText(iter, pattern, NULL);
    }
  }

  return true;
}

// static
bool AddressField::ParseCountry(
    std::vector<AutoFillField*>::const_iterator* iter,
    bool is_ecml, AddressField* address_field) {
  // Parse a country.  The occasional page (e.g.
  // Travelocity_New Member Information1.html) calls this a "location".
  // Note: ECML standard uses 2 letter country code (ISO 3166)
  if (address_field->country_ && !address_field->country_->IsEmpty())
    return false;

  string16 pattern;
  if (is_ecml)
    pattern = GetEcmlPattern(kEcmlShipToCountry, kEcmlBillToCountry, '|');
  else
    pattern = ASCIIToUTF16("country|location");

  if (!ParseText(iter, pattern, &address_field->country_))
    return false;

  return true;
}

// static
bool AddressField::ParseZipCode(
    std::vector<AutoFillField*>::const_iterator* iter,
    bool is_ecml, AddressField* address_field) {
  // Parse a zip code.  On some UK pages (e.g. The China Shop2.html) this
  // is called a "post code".
  //
  // HACK: Just for the MapQuest driving directions page we match the
  // exact name "1z", which MapQuest uses to label its zip code field.
  // Hopefully before long we'll be smart enough to find the zip code
  // on that page automatically.
  if (address_field->zip_)
    return false;

  // We may be out of fields.
  if (!**iter)
    return false;

  string16 pattern;
  if (is_ecml) {
    pattern = GetEcmlPattern(kEcmlShipToPostalCode,
                             kEcmlBillToPostalCode, '|');
  } else {
    pattern = ASCIIToUTF16("zip|postal|post code|pcode|^1z$");
  }

  AddressType tempType;
  string16 name = (**iter)->name();

  // Note: comparisons using the ecml compliant name as a prefix must be used in
  // order to accommodate Google Checkout. See FormFieldSet::GetEcmlPattern for
  // more detail.
  string16 bill_to_postal_code_field(ASCIIToUTF16(kEcmlBillToPostalCode));
  if (StartsWith(name, bill_to_postal_code_field, false)) {
    tempType = kBillingAddress;
  } else if (StartsWith(name, bill_to_postal_code_field, false)) {
    tempType = kShippingAddress;
  } else {
    tempType = kGenericAddress;
  }

  if (!ParseText(iter, pattern, &address_field->zip_))
    return false;

  address_field->type_ = tempType;
  if (!is_ecml) {
    // Look for a zip+4, whose field name will also often contain
    // the substring "zip".
    ParseText(iter, ASCIIToUTF16("zip|^-$"), &address_field->zip4_);
  }

  return true;
}

// static
bool AddressField::ParseCity(
    std::vector<AutoFillField*>::const_iterator* iter,
    bool is_ecml, AddressField* address_field) {
  // Parse a city name.  Some UK pages (e.g. The China Shop2.html) use
  // the term "town".
  if (address_field->city_)
    return false;

  string16 pattern;
  if (is_ecml)
    pattern = GetEcmlPattern(kEcmlShipToCity, kEcmlBillToCity, '|');
  else
    pattern = ASCIIToUTF16("city|town");

  if (!ParseText(iter, pattern, &address_field->city_))
    return false;

  return true;
}

bool AddressField::ParseState(
    std::vector<AutoFillField*>::const_iterator* iter,
    bool is_ecml, AddressField* address_field) {
  string16 pattern;
  if (is_ecml)
    pattern = GetEcmlPattern(kEcmlShipToStateProv, kEcmlBillToStateProv, '|');
  else
    pattern = ASCIIToUTF16("state|county");

  if (!ParseText(iter, pattern, &address_field->state_))
    return false;

  return true;
}

AddressType AddressField::AddressTypeFromText(const string16 &text) {
  if (text.find(ASCIIToUTF16("same as")) != string16::npos ||
      text.find(ASCIIToUTF16("use my")) != string16::npos)
    // This text could be a checkbox label such as "same as my billing
    // address" or "use my shipping address".
    // ++ It would help if we generally skipped all text that appears
    // after a check box.
    return kGenericAddress;

  // Not all pages say "billing address" and "shipping address" explicitly;
  // for example, Craft Catalog1.html has "Bill-to Address" and
  // "Ship-to Address".
  size_t bill = text.rfind(ASCIIToUTF16("bill"));
  size_t ship = text.rfind(ASCIIToUTF16("ship"));

  if (bill == string16::npos && ship == string16::npos)
    return kGenericAddress;

  if (bill != string16::npos && ship == string16::npos)
    return kBillingAddress;

  if (bill == string16::npos && ship != string16::npos)
    return kShippingAddress;

  if (bill > ship)
    return kBillingAddress;

  return kShippingAddress;
}
