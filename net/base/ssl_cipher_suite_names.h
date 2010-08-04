// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_SSL_CIPHER_SUITE_NAMES_H_
#define NET_BASE_SSL_CIPHER_SUITE_NAMES_H_

#include "base/basictypes.h"

namespace net {

// SSLCipherSuiteToStrings returns three strings for a given cipher suite
// number, the name of the key exchange algorithm, the name of the cipher and
// the name of the MAC. The cipher suite number is the number as sent on the
// wire and recorded at
// http://www.iana.org/assignments/tls-parameters/tls-parameters.xml
// If the cipher suite is unknown, the strings are set to "???".
void SSLCipherSuiteToStrings(const char** key_exchange_str,
                             const char** cipher_str, const char** mac_str,
                             uint16 cipher_suite);

// SSLCompressionToString returns the name of the compression algorithm
// specified by |compression_method|, which is the TLS compression id.
// If the algorithm is unknown, |name| is set to "???".
void SSLCompressionToString(const char** name, uint8 compression_method);

}  // namespace net

#endif  // NET_BASE_SSL_CIPHER_SUITE_NAMES_H_
