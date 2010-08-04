// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "chrome/browser/autofill/autofill_common_unittest.h"
#include "chrome/browser/autofill/credit_card.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Tests credit card summary string generation.  This test simulates a variety
// of different possible summary strings.  Variations occur based on the
// existence of credit card number, month, and year fields.
TEST(CreditCardTest, PreviewSummaryAndObfuscatedNumberStrings) {
  // Case 0: empty credit card.
  CreditCard credit_card0(string16(), 0);
  string16 summary0 = credit_card0.PreviewSummary();
  EXPECT_EQ(string16(), summary0);
  string16 obfuscated0 = credit_card0.ObfuscatedNumber();
  EXPECT_EQ(string16(), obfuscated0);

  // Case 00: Empty credit card with empty strings.
  CreditCard credit_card00(string16(), 0);
  autofill_unittest::SetCreditCardInfo(&credit_card00, "Corporate",
      "John Dillinger", "Visa", "", "", "", "Chicago");
  string16 summary00 = credit_card00.PreviewSummary();
  EXPECT_EQ(string16(), summary00);
  string16 obfuscated00 = credit_card00.ObfuscatedNumber();
  EXPECT_EQ(string16(), obfuscated00);

  // Case 1: No credit card number.
  CreditCard credit_card1(string16(), 0);
  autofill_unittest::SetCreditCardInfo(&credit_card1, "Corporate",
      "John Dillinger", "Visa", "", "01", "2010", "Chicago");
  string16 summary1 = credit_card1.PreviewSummary();
  EXPECT_EQ(string16(), summary1);
  string16 obfuscated1 = credit_card1.ObfuscatedNumber();
  EXPECT_EQ(string16(), obfuscated1);

  // Case 2: No month.
  CreditCard credit_card2(string16(), 0);
  autofill_unittest::SetCreditCardInfo(&credit_card2, "Corporate",
      "John Dillinger", "Visa", "123456789012", "", "2010", "Chicago");
  string16 summary2 = credit_card2.PreviewSummary();
  EXPECT_EQ(string16(ASCIIToUTF16("************9012")), summary2);
  string16 obfuscated2 = credit_card2.ObfuscatedNumber();
  EXPECT_EQ(string16(ASCIIToUTF16("************9012")), obfuscated2);

  // Case 3: No year.
  CreditCard credit_card3(string16(), 0);
  autofill_unittest::SetCreditCardInfo(&credit_card3, "Corporate",
      "John Dillinger", "Visa", "123456789012", "01", "", "Chicago");
  string16 summary3 = credit_card3.PreviewSummary();
  EXPECT_EQ(string16(ASCIIToUTF16("************9012")), summary3);
  string16 obfuscated3 = credit_card3.ObfuscatedNumber();
  EXPECT_EQ(string16(ASCIIToUTF16("************9012")), obfuscated3);

  // Case 4: Have everything.
  CreditCard credit_card4(string16(), 0);
  autofill_unittest::SetCreditCardInfo(&credit_card4, "Corporate",
      "John Dillinger", "Visa", "123456789012", "01", "2010", "Chicago");
  string16 summary4 = credit_card4.PreviewSummary();
  EXPECT_EQ(string16(ASCIIToUTF16("************9012, Exp: 01/2010")), summary4);
  string16 obfuscated4 = credit_card4.ObfuscatedNumber();
  EXPECT_EQ(string16(ASCIIToUTF16("************9012")), obfuscated4);
}

}  // namespace

