// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See "SSPI Sample Application" at
// http://msdn.microsoft.com/en-us/library/aa918273.aspx

#include "net/http/http_auth_sspi_win.h"

#include "base/base64.h"
#include "base/logging.h"
#include "base/singleton.h"
#include "base/string_util.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth.h"

namespace net {

namespace {

int MapAcquireCredentialsStatusToError(SECURITY_STATUS status,
                                       const SEC_WCHAR* package) {
  switch (status) {
    case SEC_E_OK:
      return OK;
    case SEC_E_INSUFFICIENT_MEMORY:
      return ERR_OUT_OF_MEMORY;
    case SEC_E_INTERNAL_ERROR:
      return ERR_UNEXPECTED;
    case SEC_E_NO_CREDENTIALS:
    case SEC_E_NOT_OWNER:
    case SEC_E_UNKNOWN_CREDENTIALS:
      return ERR_INVALID_AUTH_CREDENTIALS;
    case SEC_E_SECPKG_NOT_FOUND:
      // This indicates that the SSPI configuration does not match expectations
      LOG(ERROR) << "Received SEC_E_SECPKG_NOT_FOUND for " << package;
      return ERR_UNSUPPORTED_AUTH_SCHEME;
    default:
      LOG(ERROR) << "Unexpected SECURITY_STATUS " << status;
      return ERR_UNEXPECTED;
  }
}

int AcquireExplicitCredentials(SSPILibrary* library,
                               const SEC_WCHAR* package,
                               const std::wstring& domain,
                               const std::wstring& user,
                               const std::wstring& password,
                               CredHandle* cred) {
  SEC_WINNT_AUTH_IDENTITY identity;
  identity.Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;
  identity.User =
      reinterpret_cast<unsigned short*>(const_cast<wchar_t*>(user.c_str()));
  identity.UserLength = user.size();
  identity.Domain =
      reinterpret_cast<unsigned short*>(const_cast<wchar_t*>(domain.c_str()));
  identity.DomainLength = domain.size();
  identity.Password =
      reinterpret_cast<unsigned short*>(const_cast<wchar_t*>(password.c_str()));
  identity.PasswordLength = password.size();

  TimeStamp expiry;

  // Pass the username/password to get the credentials handle.
  SECURITY_STATUS status = library->AcquireCredentialsHandle(
      NULL,  // pszPrincipal
      const_cast<SEC_WCHAR*>(package),  // pszPackage
      SECPKG_CRED_OUTBOUND,  // fCredentialUse
      NULL,  // pvLogonID
      &identity,  // pAuthData
      NULL,  // pGetKeyFn (not used)
      NULL,  // pvGetKeyArgument (not used)
      cred,  // phCredential
      &expiry);  // ptsExpiry

  return MapAcquireCredentialsStatusToError(status, package);
}

int AcquireDefaultCredentials(SSPILibrary* library, const SEC_WCHAR* package,
                              CredHandle* cred) {
  TimeStamp expiry;

  // Pass the username/password to get the credentials handle.
  // Note: Since the 5th argument is NULL, it uses the default
  // cached credentials for the logged in user, which can be used
  // for a single sign-on.
  SECURITY_STATUS status = library->AcquireCredentialsHandle(
      NULL,  // pszPrincipal
      const_cast<SEC_WCHAR*>(package),  // pszPackage
      SECPKG_CRED_OUTBOUND,  // fCredentialUse
      NULL,  // pvLogonID
      NULL,  // pAuthData
      NULL,  // pGetKeyFn (not used)
      NULL,  // pvGetKeyArgument (not used)
      cred,  // phCredential
      &expiry);  // ptsExpiry

  return MapAcquireCredentialsStatusToError(status, package);
}

}  // anonymous namespace

HttpAuthSSPI::HttpAuthSSPI(SSPILibrary* library,
                           const std::string& scheme,
                           SEC_WCHAR* security_package,
                           ULONG max_token_length)
    : library_(library),
      scheme_(scheme),
      security_package_(security_package),
      max_token_length_(max_token_length) {
  DCHECK(library_);
  SecInvalidateHandle(&cred_);
  SecInvalidateHandle(&ctxt_);
}

HttpAuthSSPI::~HttpAuthSSPI() {
  ResetSecurityContext();
  if (SecIsValidHandle(&cred_)) {
    library_->FreeCredentialsHandle(&cred_);
    SecInvalidateHandle(&cred_);
  }
}

bool HttpAuthSSPI::NeedsIdentity() const {
  return decoded_server_auth_token_.empty();
}

bool HttpAuthSSPI::IsFinalRound() const {
  return !decoded_server_auth_token_.empty();
}

void HttpAuthSSPI::ResetSecurityContext() {
  if (SecIsValidHandle(&ctxt_)) {
    library_->DeleteSecurityContext(&ctxt_);
    SecInvalidateHandle(&ctxt_);
  }
}

bool HttpAuthSSPI::ParseChallenge(HttpAuth::ChallengeTokenizer* tok) {
  // Verify the challenge's auth-scheme.
  if (!tok->valid() ||
      !LowerCaseEqualsASCII(tok->scheme(), StringToLowerASCII(scheme_).c_str()))
    return false;

  tok->set_expect_base64_token(true);
  if (!tok->GetNext()) {
    decoded_server_auth_token_.clear();
    return true;
  }

  std::string encoded_auth_token = tok->value();
  std::string decoded_auth_token;
  bool base64_rv = base::Base64Decode(encoded_auth_token, &decoded_auth_token);
  if (!base64_rv) {
    LOG(ERROR) << "Base64 decoding of auth token failed.";
    return false;
  }
  decoded_server_auth_token_ = decoded_auth_token;
  return true;
}

int HttpAuthSSPI::GenerateAuthToken(const std::wstring* username,
                                    const std::wstring* password,
                                    const std::wstring& spn,
                                    std::string* auth_token) {
  DCHECK((username == NULL) == (password == NULL));

  // Initial challenge.
  if (!IsFinalRound()) {
    int rv = OnFirstRound(username, password);
    if (rv != OK)
      return rv;
  }

  DCHECK(SecIsValidHandle(&cred_));
  void* out_buf;
  int out_buf_len;
  int rv = GetNextSecurityToken(
      spn,
      static_cast<void *>(const_cast<char *>(
          decoded_server_auth_token_.c_str())),
      decoded_server_auth_token_.length(),
      &out_buf,
      &out_buf_len);
  if (rv != OK)
    return rv;

  // Base64 encode data in output buffer and prepend the scheme.
  std::string encode_input(static_cast<char*>(out_buf), out_buf_len);
  std::string encode_output;
  bool base64_rv = base::Base64Encode(encode_input, &encode_output);
  // OK, we are done with |out_buf|
  free(out_buf);
  if (!base64_rv) {
    LOG(ERROR) << "Base64 encoding of auth token failed.";
    return ERR_UNEXPECTED;
  }
  *auth_token = scheme_ + " " + encode_output;
  return OK;
}

int HttpAuthSSPI::OnFirstRound(const std::wstring* username,
                               const std::wstring* password) {
  DCHECK((username == NULL) == (password == NULL));
  DCHECK(!SecIsValidHandle(&cred_));
  int rv = OK;
  if (username) {
    std::wstring domain;
    std::wstring user;
    SplitDomainAndUser(*username, &domain, &user);
    rv = AcquireExplicitCredentials(library_, security_package_, domain,
                                    user, *password, &cred_);
    if (rv != OK)
      return rv;
  } else {
    rv = AcquireDefaultCredentials(library_, security_package_, &cred_);
    if (rv != OK)
      return rv;
  }

  return rv;
}

int HttpAuthSSPI::GetNextSecurityToken(
    const std::wstring& spn,
    const void * in_token,
    int in_token_len,
    void** out_token,
    int* out_token_len) {
  SECURITY_STATUS status;
  TimeStamp expiry;

  DWORD ctxt_attr;
  CtxtHandle* ctxt_ptr;
  SecBufferDesc in_buffer_desc, out_buffer_desc;
  SecBufferDesc* in_buffer_desc_ptr;
  SecBuffer in_buffer, out_buffer;

  if (in_token_len > 0) {
    // Prepare input buffer.
    in_buffer_desc.ulVersion = SECBUFFER_VERSION;
    in_buffer_desc.cBuffers = 1;
    in_buffer_desc.pBuffers = &in_buffer;
    in_buffer.BufferType = SECBUFFER_TOKEN;
    in_buffer.cbBuffer = in_token_len;
    in_buffer.pvBuffer = const_cast<void*>(in_token);
    ctxt_ptr = &ctxt_;
    in_buffer_desc_ptr = &in_buffer_desc;
  } else {
    // If there is no input token, then we are starting a new authentication
    // sequence.  If we have already initialized our security context, then
    // we're incorrectly reusing the auth handler for a new sequence.
    if (SecIsValidHandle(&ctxt_)) {
      LOG(ERROR) << "Cannot restart authentication sequence";
      return ERR_UNEXPECTED;
    }
    ctxt_ptr = NULL;
    in_buffer_desc_ptr = NULL;
  }

  // Prepare output buffer.
  out_buffer_desc.ulVersion = SECBUFFER_VERSION;
  out_buffer_desc.cBuffers = 1;
  out_buffer_desc.pBuffers = &out_buffer;
  out_buffer.BufferType = SECBUFFER_TOKEN;
  out_buffer.cbBuffer = max_token_length_;
  out_buffer.pvBuffer = malloc(out_buffer.cbBuffer);
  if (!out_buffer.pvBuffer)
    return ERR_OUT_OF_MEMORY;

  // This returns a token that is passed to the remote server.
  status = library_->InitializeSecurityContext(
      &cred_,  // phCredential
      ctxt_ptr,  // phContext
      const_cast<wchar_t *>(spn.c_str()),  // pszTargetName
      0,  // fContextReq
      0,  // Reserved1 (must be 0)
      SECURITY_NATIVE_DREP,  // TargetDataRep
      in_buffer_desc_ptr,  // pInput
      0,  // Reserved2 (must be 0)
      &ctxt_,  // phNewContext
      &out_buffer_desc,  // pOutput
      &ctxt_attr,  // pfContextAttr
      &expiry);  // ptsExpiry
  // On success, the function returns SEC_I_CONTINUE_NEEDED on the first call
  // and SEC_E_OK on the second call.  On failure, the function returns an
  // error code.
  if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK) {
    LOG(ERROR) << "InitializeSecurityContext failed " << status;
    ResetSecurityContext();
    free(out_buffer.pvBuffer);
    return ERR_UNEXPECTED;  // TODO(wtc): map error code.
  }
  if (!out_buffer.cbBuffer) {
    free(out_buffer.pvBuffer);
    out_buffer.pvBuffer = NULL;
  }
  *out_token = out_buffer.pvBuffer;
  *out_token_len = out_buffer.cbBuffer;
  return OK;
}

void SplitDomainAndUser(const std::wstring& combined,
                        std::wstring* domain,
                        std::wstring* user) {
  // |combined| may be in the form "user" or "DOMAIN\user".
  // Separatethe two parts if they exist.
  // TODO(cbentzel): I believe user@domain is also a valid form.
  size_t backslash_idx = combined.find(L'\\');
  if (backslash_idx == std::wstring::npos) {
    domain->clear();
    *user = combined;
  } else {
    *domain = combined.substr(0, backslash_idx);
    *user = combined.substr(backslash_idx + 1);
  }
}

int DetermineMaxTokenLength(SSPILibrary* library,
                            const std::wstring& package,
                            ULONG* max_token_length) {
  DCHECK(library);
  DCHECK(max_token_length);
  PSecPkgInfo pkg_info = NULL;
  SECURITY_STATUS status = library->QuerySecurityPackageInfo(
      const_cast<wchar_t *>(package.c_str()), &pkg_info);
  if (status != SEC_E_OK) {
    // The documentation at
    // http://msdn.microsoft.com/en-us/library/aa379359(VS.85).aspx
    // only mentions that a non-zero (or non-SEC_E_OK) value is returned
    // if the function fails. In practice, it appears to return
    // SEC_E_SECPKG_NOT_FOUND for invalid/unknown packages.
    LOG(ERROR) << "Security package " << package << " not found."
               << " Status code: " << status;
    if (status == SEC_E_SECPKG_NOT_FOUND)
      return ERR_UNSUPPORTED_AUTH_SCHEME;
    else
      return ERR_UNEXPECTED;
  }
  int token_length = pkg_info->cbMaxToken;
  status = library->FreeContextBuffer(pkg_info);
  if (status != SEC_E_OK) {
    // The documentation at
    // http://msdn.microsoft.com/en-us/library/aa375416(VS.85).aspx
    // only mentions that a non-zero (or non-SEC_E_OK) value is returned
    // if the function fails, and does not indicate what the failure conditions
    // are.
    LOG(ERROR) << "Unexpected problem freeing context buffer. Status code: "
               << status;
    return ERR_UNEXPECTED;
  }
  *max_token_length = token_length;
  return OK;
}

class SSPILibraryDefault : public SSPILibrary {
 public:
  SSPILibraryDefault() {}
  virtual ~SSPILibraryDefault() {}

  virtual SECURITY_STATUS AcquireCredentialsHandle(LPWSTR pszPrincipal,
                                                   LPWSTR pszPackage,
                                                   unsigned long fCredentialUse,
                                                   void* pvLogonId,
                                                   void* pvAuthData,
                                                   SEC_GET_KEY_FN pGetKeyFn,
                                                   void* pvGetKeyArgument,
                                                   PCredHandle phCredential,
                                                   PTimeStamp ptsExpiry) {
    return ::AcquireCredentialsHandle(pszPrincipal, pszPackage, fCredentialUse,
                                      pvLogonId, pvAuthData, pGetKeyFn,
                                      pvGetKeyArgument, phCredential,
                                      ptsExpiry);
  }

  virtual SECURITY_STATUS InitializeSecurityContext(PCredHandle phCredential,
                                                    PCtxtHandle phContext,
                                                    SEC_WCHAR* pszTargetName,
                                                    unsigned long fContextReq,
                                                    unsigned long Reserved1,
                                                    unsigned long TargetDataRep,
                                                    PSecBufferDesc pInput,
                                                    unsigned long Reserved2,
                                                    PCtxtHandle phNewContext,
                                                    PSecBufferDesc pOutput,
                                                    unsigned long* contextAttr,
                                                    PTimeStamp ptsExpiry) {
    return ::InitializeSecurityContext(phCredential, phContext, pszTargetName,
                                       fContextReq, Reserved1, TargetDataRep,
                                       pInput, Reserved2, phNewContext, pOutput,
                                       contextAttr, ptsExpiry);
  }

  virtual SECURITY_STATUS QuerySecurityPackageInfo(LPWSTR pszPackageName,
                                                   PSecPkgInfoW *pkgInfo) {
    return ::QuerySecurityPackageInfo(pszPackageName, pkgInfo);
  }

  virtual SECURITY_STATUS FreeCredentialsHandle(PCredHandle phCredential) {
    return ::FreeCredentialsHandle(phCredential);
  }

  virtual SECURITY_STATUS DeleteSecurityContext(PCtxtHandle phContext) {
    return ::DeleteSecurityContext(phContext);
  }

  virtual SECURITY_STATUS FreeContextBuffer(PVOID pvContextBuffer) {
    return ::FreeContextBuffer(pvContextBuffer);
  }

 private:
  friend struct DefaultSingletonTraits<SSPILibraryDefault>;
};

// static
SSPILibrary* SSPILibrary::GetDefault() {
  return Singleton<SSPILibraryDefault>::get();
}

}  // namespace net
