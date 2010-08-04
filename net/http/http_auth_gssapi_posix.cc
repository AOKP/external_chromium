// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_gssapi_posix.h"

#include <limits>
#include <string>

#include "base/base64.h"
#include "base/file_path.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/singleton.h"
#include "base/string_util.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"

// These are defined for the GSSAPI library:
// Paraphrasing the comments from gssapi.h:
// "The implementation must reserve static storage for a
// gss_OID_desc object for each constant.  That constant
// should be initialized to point to that gss_OID_desc."
namespace {

static gss_OID_desc GSS_C_NT_USER_NAME_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x01")
};
static gss_OID_desc GSS_C_NT_MACHINE_UID_NAME_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x02")
};
static gss_OID_desc GSS_C_NT_STRING_UID_NAME_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x03")
};
static gss_OID_desc GSS_C_NT_HOSTBASED_SERVICE_X_VAL = {
  6,
  const_cast<char*>("\x2b\x06\x01\x05\x06\x02")
};
static gss_OID_desc GSS_C_NT_HOSTBASED_SERVICE_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x04")
};
static gss_OID_desc GSS_C_NT_ANONYMOUS_VAL = {
  6,
  const_cast<char*>("\x2b\x06\01\x05\x06\x03")
};
static gss_OID_desc GSS_C_NT_EXPORT_NAME_VAL = {
  6,
  const_cast<char*>("\x2b\x06\x01\x05\x06\x04")
};

}  // namespace

gss_OID GSS_C_NT_USER_NAME = &GSS_C_NT_USER_NAME_VAL;
gss_OID GSS_C_NT_MACHINE_UID_NAME = &GSS_C_NT_MACHINE_UID_NAME_VAL;
gss_OID GSS_C_NT_STRING_UID_NAME = &GSS_C_NT_STRING_UID_NAME_VAL;
gss_OID GSS_C_NT_HOSTBASED_SERVICE_X = &GSS_C_NT_HOSTBASED_SERVICE_X_VAL;
gss_OID GSS_C_NT_HOSTBASED_SERVICE = &GSS_C_NT_HOSTBASED_SERVICE_VAL;
gss_OID GSS_C_NT_ANONYMOUS = &GSS_C_NT_ANONYMOUS_VAL;
gss_OID GSS_C_NT_EXPORT_NAME = &GSS_C_NT_EXPORT_NAME_VAL;

namespace net {

// These are encoded using ASN.1 BER encoding.

// This one is used by Firefox's nsAuthGSSAPI class.
gss_OID_desc CHROME_GSS_KRB5_MECH_OID_DESC_VAL = {
  9,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x02")
};

gss_OID_desc CHROME_GSS_C_NT_HOSTBASED_SERVICE_X_VAL = {
  6,
  const_cast<char*>("\x2b\x06\x01\x05\x06\x02")
};

gss_OID_desc CHROME_GSS_C_NT_HOSTBASED_SERVICE_VAL = {
  10,
  const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x04")
};

gss_OID CHROME_GSS_C_NT_HOSTBASED_SERVICE_X =
    &CHROME_GSS_C_NT_HOSTBASED_SERVICE_X_VAL;
gss_OID CHROME_GSS_C_NT_HOSTBASED_SERVICE =
    &CHROME_GSS_C_NT_HOSTBASED_SERVICE_VAL;
gss_OID CHROME_GSS_KRB5_MECH_OID_DESC =
    &CHROME_GSS_KRB5_MECH_OID_DESC_VAL;

// Debugging helpers.
namespace {

std::string DisplayStatus(OM_uint32 major_status,
                          OM_uint32 minor_status) {
  if (major_status == GSS_S_COMPLETE)
    return "OK";
  return StringPrintf("0x%08X 0x%08X", major_status, minor_status);
}

std::string DisplayCode(GSSAPILibrary* gssapi_lib,
                        OM_uint32 status,
                        OM_uint32 status_code_type) {
  const int kMaxDisplayIterations = 8;
  const size_t kMaxMsgLength = 4096;
  // msg_ctx needs to be outside the loop because it is invoked multiple times.
  OM_uint32 msg_ctx = 0;
  std::string rv = StringPrintf("(0x%08X)", status);

  // This loop should continue iterating until msg_ctx is 0 after the first
  // iteration. To be cautious and prevent an infinite loop, it stops after
  // a finite number of iterations as well. As an added sanity check, no
  // individual message may exceed |kMaxMsgLength|, and the final result
  // will not exceed |kMaxMsgLength|*2-1.
  for (int i = 0; i < kMaxDisplayIterations && rv.size() < kMaxMsgLength;
      ++i) {
    OM_uint32 min_stat;
    gss_buffer_desc_struct msg = GSS_C_EMPTY_BUFFER;
    OM_uint32 maj_stat =
        gssapi_lib->display_status(&min_stat, status, status_code_type,
                                   GSS_C_NULL_OID, &msg_ctx, &msg);
    if (maj_stat == GSS_S_COMPLETE) {
      int msg_len = (msg.length > kMaxMsgLength) ?
          static_cast<int>(kMaxMsgLength) :
          static_cast<int>(msg.length);
      if (msg_len > 0 && msg.value != NULL) {
        rv += StringPrintf(" %.*s", msg_len,
                           static_cast<char*>(msg.value));
      }
    }
    gssapi_lib->release_buffer(&min_stat, &msg);
    if (!msg_ctx)
      break;
  }
  return rv;
}

std::string DisplayExtendedStatus(GSSAPILibrary* gssapi_lib,
                                  OM_uint32 major_status,
                                  OM_uint32 minor_status) {
  if (major_status == GSS_S_COMPLETE)
    return "OK";
  std::string major = DisplayCode(gssapi_lib, major_status, GSS_C_GSS_CODE);
  std::string minor = DisplayCode(gssapi_lib, minor_status, GSS_C_MECH_CODE);
  return StringPrintf("Major: %s | Minor: %s", major.c_str(), minor.c_str());
}

// ScopedName releases a gss_name_t when it goes out of scope.
class ScopedName {
 public:
  ScopedName(gss_name_t name,
             GSSAPILibrary* gssapi_lib)
      : name_(name),
        gssapi_lib_(gssapi_lib) {
    DCHECK(gssapi_lib_);
  }

  ~ScopedName() {
    if (name_ != GSS_C_NO_NAME) {
      OM_uint32 minor_status = 0;
      OM_uint32 major_status =
          gssapi_lib_->release_name(&minor_status, &name_);
      if (major_status != GSS_S_COMPLETE) {
        LOG(WARNING) << "Problem releasing name. "
                     << DisplayStatus(major_status, minor_status);
      }
      name_ = GSS_C_NO_NAME;
    }
  }

 private:
  gss_name_t name_;
  GSSAPILibrary* gssapi_lib_;

  DISALLOW_COPY_AND_ASSIGN(ScopedName);
};

// ScopedBuffer releases a gss_buffer_t when it goes out of scope.
class ScopedBuffer {
 public:
  ScopedBuffer(gss_buffer_t buffer,
               GSSAPILibrary* gssapi_lib)
      : buffer_(buffer),
        gssapi_lib_(gssapi_lib) {
    DCHECK(gssapi_lib_);
  }

  ~ScopedBuffer() {
    if (buffer_ != GSS_C_NO_BUFFER) {
      OM_uint32 minor_status = 0;
      OM_uint32 major_status =
          gssapi_lib_->release_buffer(&minor_status, buffer_);
      if (major_status != GSS_S_COMPLETE) {
        LOG(WARNING) << "Problem releasing buffer. "
                     << DisplayStatus(major_status, minor_status);
      }
      buffer_ = GSS_C_NO_BUFFER;
    }
  }

 private:
  gss_buffer_t buffer_;
  GSSAPILibrary* gssapi_lib_;

  DISALLOW_COPY_AND_ASSIGN(ScopedBuffer);
};

namespace {

std::string AppendIfPredefinedValue(gss_OID oid,
                                    gss_OID predefined_oid,
                                    const char* predefined_oid_name) {
  DCHECK(oid);
  DCHECK(predefined_oid);
  DCHECK(predefined_oid_name);
  std::string output;
  if (oid->length != predefined_oid->length)
    return output;
  if (0 != memcmp(oid->elements,
                  predefined_oid->elements,
                  predefined_oid->length))
    return output;

  output += " (";
  output += predefined_oid_name;
  output += ")";
  return output;
}

}  // namespace

std::string DescribeOid(GSSAPILibrary* gssapi_lib, const gss_OID oid) {
  if (!oid)
    return "<NULL>";
  std::string output;
  const size_t kMaxCharsToPrint = 1024;
  OM_uint32 byte_length = oid->length;
  size_t char_length = byte_length / sizeof(char);
  if (char_length > kMaxCharsToPrint) {
    // This might be a plain ASCII string.
    // Check if the first |kMaxCharsToPrint| characters
    // contain only printable characters and are NULL terminated.
    const char* str = reinterpret_cast<const char*>(oid);
    bool is_printable = true;
    size_t str_length = 0;
    for ( ; str_length < kMaxCharsToPrint; ++str_length) {
      if (!str[str_length])
        break;
      if (!isprint(str[str_length])) {
        is_printable = false;
        break;
      }
    }
    if (!str[str_length]) {
      output += StringPrintf("\"%s\"", str);
      return output;
    }
  }
  output = StringPrintf("(%u) \"", byte_length);
  if (!oid->elements) {
    output += "<NULL>";
    return output;
  }
  const unsigned char* elements =
      reinterpret_cast<const unsigned char*>(oid->elements);
  // Don't print more than |kMaxCharsToPrint| characters.
  size_t i = 0;
  for ( ; (i < byte_length) && (i < kMaxCharsToPrint); ++i) {
    output += StringPrintf("\\x%02X", elements[i]);
  }
  if (i >= kMaxCharsToPrint)
    output += "...";
  output += "\"";

  // Check if the OID is one of the predefined values.
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_USER_NAME,
                                    "GSS_C_NT_USER_NAME");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_MACHINE_UID_NAME,
                                    "GSS_C_NT_MACHINE_UID_NAME");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_STRING_UID_NAME,
                                    "GSS_C_NT_STRING_UID_NAME");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_HOSTBASED_SERVICE_X,
                                    "GSS_C_NT_HOSTBASED_SERVICE_X");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_HOSTBASED_SERVICE,
                                    "GSS_C_NT_HOSTBASED_SERVICE");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_ANONYMOUS,
                                    "GSS_C_NT_ANONYMOUS");
  output += AppendIfPredefinedValue(oid,
                                    GSS_C_NT_EXPORT_NAME,
                                    "GSS_C_NT_EXPORT_NAME");

  return output;
}

std::string DescribeBuffer(const gss_buffer_t buffer) {
  if (!buffer)
    return "<NULL>";
  size_t length = buffer->length;
  std::string output(StringPrintf("(%" PRIuS ") ", length));
  if (!buffer->value) {
    output += "<NULL>";
    return output;
  }
  const char* value =
      reinterpret_cast<const char*>(buffer->value);
  bool is_printable = true;
  for (size_t i = 0; i < length; ++i) {
    if (!isprint(value[i])) {
      // Allow the last character to be a '0'.
      if ((i < (length - 1)) && !value[i])
        continue;
      is_printable = false;
      break;
    }
  }
  if (is_printable) {
    output += "\"";
    output += value;
    output += "\"";
  } else {
    output += "[";
    for (size_t i = 0; i < buffer->length; ++i) {
      output += StringPrintf("\\x%02X", value[i] & 0x0FF);
    }
    output += "]";
  }

  return output;
}

std::string DescribeName(GSSAPILibrary* gssapi_lib, const gss_name_t name) {
  OM_uint32 major_status = 0;
  OM_uint32 minor_status = 0;
  gss_buffer_desc_struct output_name_buffer = GSS_C_EMPTY_BUFFER;
  gss_OID_desc output_name_type_desc = GSS_C_EMPTY_BUFFER;
  gss_OID output_name_type = &output_name_type_desc;
  major_status = gssapi_lib->display_name(&minor_status,
                                          name,
                                          &output_name_buffer,
                                          &output_name_type);
  ScopedBuffer scoped_output_name(&output_name_buffer, gssapi_lib);
  if (major_status != GSS_S_COMPLETE) {
    std::string error =
        StringPrintf("Unable to describe name 0x%p, %s",
                     name,
                     DisplayExtendedStatus(gssapi_lib,
                                           major_status,
                                           minor_status).c_str());
    return error;
  }
  int len = output_name_buffer.length;
  std::string description =
      StringPrintf("%*s (Type %s)",
                   len,
                   reinterpret_cast<const char*>(output_name_buffer.value),
                   DescribeOid(gssapi_lib, output_name_type).c_str());
  return description;
}

std::string DescribeContext(GSSAPILibrary* gssapi_lib,
                            const gss_ctx_id_t context_handle) {
  OM_uint32 major_status = 0;
  OM_uint32 minor_status = 0;
  gss_name_t src_name = GSS_C_NO_NAME;
  gss_name_t targ_name = GSS_C_NO_NAME;
  OM_uint32 lifetime_rec = 0;
  gss_OID mech_type = GSS_C_NO_OID;
  OM_uint32 ctx_flags = 0;
  int locally_initiated = 0;
  int open = 0;
  major_status = gssapi_lib->inquire_context(&minor_status,
                                             context_handle,
                                             &src_name,
                                             &targ_name,
                                             &lifetime_rec,
                                             &mech_type,
                                             &ctx_flags,
                                             &locally_initiated,
                                             &open);
  ScopedName(src_name, gssapi_lib);
  ScopedName(targ_name, gssapi_lib);
  if (major_status != GSS_S_COMPLETE) {
    std::string error =
        StringPrintf("Unable to describe context 0x%p, %s",
                     context_handle,
                     DisplayExtendedStatus(gssapi_lib,
                                           major_status,
                                           minor_status).c_str());
    return error;
  }
  std::string source(DescribeName(gssapi_lib, src_name));
  std::string target(DescribeName(gssapi_lib, targ_name));
  std::string description = StringPrintf("Context 0x%p: "
                                         "Source \"%s\", "
                                         "Target \"%s\", "
                                         "lifetime %d, "
                                         "mechanism %s, "
                                         "flags 0x%08X, "
                                         "local %d, "
                                         "open %d",
                                         context_handle,
                                         source.c_str(),
                                         target.c_str(),
                                         lifetime_rec,
                                         DescribeOid(gssapi_lib,
                                                     mech_type).c_str(),
                                         ctx_flags,
                                         locally_initiated,
                                         open);
  return description;
}

}  // namespace

GSSAPISharedLibrary::GSSAPISharedLibrary()
    : initialized_(false),
      gssapi_library_(NULL),
      import_name_(NULL),
      release_name_(NULL),
      release_buffer_(NULL),
      display_name_(NULL),
      display_status_(NULL),
      init_sec_context_(NULL),
      wrap_size_limit_(NULL),
      delete_sec_context_(NULL),
      inquire_context_(NULL) {
}

GSSAPISharedLibrary::~GSSAPISharedLibrary() {
  if (gssapi_library_) {
    base::UnloadNativeLibrary(gssapi_library_);
    gssapi_library_ = NULL;
  }
}

bool GSSAPISharedLibrary::Init() {
  if (!initialized_)
    InitImpl();
  return initialized_;
}

bool GSSAPISharedLibrary::InitImpl() {
  DCHECK(!initialized_);
  gssapi_library_ = LoadSharedLibrary();
  if (gssapi_library_ == NULL)
    return false;
  initialized_ = true;
  return true;
}

base::NativeLibrary GSSAPISharedLibrary::LoadSharedLibrary() {
  static const char* kLibraryNames[] = {
#if defined(OS_MACOSX)
    "libgssapi_krb5.dylib"  // MIT Kerberos
#else
    "libgssapi_krb5.so.2",  // MIT Kerberos - FC, Suse10, Debian
    "libgssapi.so.4",       // Heimdal - Suse10, MDK
    "libgssapi.so.1"        // Heimdal - Suse9, CITI - FC, MDK, Suse10
#endif
  };
  static size_t num_lib_names = arraysize(kLibraryNames);

  for (size_t i = 0; i < num_lib_names; ++i) {
    const char* library_name = kLibraryNames[i];
    FilePath file_path(library_name);
    base::NativeLibrary lib = base::LoadNativeLibrary(file_path);
    if (lib) {
      // Only return this library if we can bind the functions we need.
      if (BindMethods(lib))
        return lib;
      base::UnloadNativeLibrary(lib);
    }
  }
  LOG(WARNING) << "Unable to find a compatible GSSAPI library";
  return NULL;
}

#define BIND(lib, x) \
  gss_##x##_type x = reinterpret_cast<gss_##x##_type>( \
      base::GetFunctionPointerFromNativeLibrary(lib, "gss_" #x)); \
  if (x == NULL) { \
    LOG(WARNING) << "Unable to bind function \"" << "gss_" #x << "\""; \
    return false; \
  }

bool GSSAPISharedLibrary::BindMethods(base::NativeLibrary lib) {
  DCHECK(lib != NULL);

  BIND(lib, import_name)
  BIND(lib, release_name)
  BIND(lib, release_buffer)
  BIND(lib, display_name)
  BIND(lib, display_status)
  BIND(lib, init_sec_context)
  BIND(lib, wrap_size_limit)
  BIND(lib, delete_sec_context)
  BIND(lib, inquire_context)

  import_name_ = import_name;
  release_name_ = release_name;
  release_buffer_ = release_buffer;
  display_name_ = display_name;
  display_status_ = display_status;
  init_sec_context_ = init_sec_context;
  wrap_size_limit_ = wrap_size_limit;
  delete_sec_context_ = delete_sec_context;
  inquire_context_ = inquire_context;

  return true;
}

#undef BIND

OM_uint32 GSSAPISharedLibrary::import_name(
    OM_uint32* minor_status,
    const gss_buffer_t input_name_buffer,
    const gss_OID input_name_type,
    gss_name_t* output_name) {
  DCHECK(initialized_);
  return import_name_(minor_status, input_name_buffer, input_name_type,
                      output_name);
}

OM_uint32 GSSAPISharedLibrary::release_name(
    OM_uint32* minor_status,
    gss_name_t* input_name) {
  DCHECK(initialized_);
  return release_name_(minor_status, input_name);
}

OM_uint32 GSSAPISharedLibrary::release_buffer(
    OM_uint32* minor_status,
    gss_buffer_t buffer) {
  DCHECK(initialized_);
  return release_buffer_(minor_status, buffer);
}

OM_uint32 GSSAPISharedLibrary::display_name(
    OM_uint32* minor_status,
    const gss_name_t input_name,
    gss_buffer_t output_name_buffer,
    gss_OID* output_name_type) {
  DCHECK(initialized_);
  return display_name_(minor_status,
                       input_name,
                       output_name_buffer,
                       output_name_type);
}

OM_uint32 GSSAPISharedLibrary::display_status(
    OM_uint32* minor_status,
    OM_uint32 status_value,
    int status_type,
    const gss_OID mech_type,
    OM_uint32* message_context,
    gss_buffer_t status_string) {
  DCHECK(initialized_);
  return display_status_(minor_status, status_value, status_type, mech_type,
                         message_context, status_string);
}

OM_uint32 GSSAPISharedLibrary::init_sec_context(
    OM_uint32* minor_status,
    const gss_cred_id_t initiator_cred_handle,
    gss_ctx_id_t* context_handle,
    const gss_name_t target_name,
    const gss_OID mech_type,
    OM_uint32 req_flags,
    OM_uint32 time_req,
    const gss_channel_bindings_t input_chan_bindings,
    const gss_buffer_t input_token,
    gss_OID* actual_mech_type,
    gss_buffer_t output_token,
    OM_uint32* ret_flags,
    OM_uint32* time_rec) {
  DCHECK(initialized_);
  return init_sec_context_(minor_status,
                           initiator_cred_handle,
                           context_handle,
                           target_name,
                           mech_type,
                           req_flags,
                           time_req,
                           input_chan_bindings,
                           input_token,
                           actual_mech_type,
                           output_token,
                           ret_flags,
                           time_rec);
}

OM_uint32 GSSAPISharedLibrary::wrap_size_limit(
    OM_uint32* minor_status,
    const gss_ctx_id_t context_handle,
    int conf_req_flag,
    gss_qop_t qop_req,
    OM_uint32 req_output_size,
    OM_uint32* max_input_size) {
  DCHECK(initialized_);
  return wrap_size_limit_(minor_status,
                          context_handle,
                          conf_req_flag,
                          qop_req,
                          req_output_size,
                          max_input_size);
}

OM_uint32 GSSAPISharedLibrary::delete_sec_context(
    OM_uint32* minor_status,
    gss_ctx_id_t* context_handle,
    gss_buffer_t output_token) {
  // This is called from the owner class' destructor, even if
  // Init() is not called, so we can't assume |initialized_|
  // is set.
  if (!initialized_)
    return 0;
  return delete_sec_context_(minor_status,
                             context_handle,
                             output_token);
}

OM_uint32 GSSAPISharedLibrary::inquire_context(
    OM_uint32* minor_status,
    const gss_ctx_id_t context_handle,
    gss_name_t* src_name,
    gss_name_t* targ_name,
    OM_uint32* lifetime_rec,
    gss_OID* mech_type,
    OM_uint32* ctx_flags,
    int* locally_initiated,
    int* open) {
  DCHECK(initialized_);
  return inquire_context_(minor_status,
                         context_handle,
                         src_name,
                         targ_name,
                         lifetime_rec,
                         mech_type,
                         ctx_flags,
                         locally_initiated,
                         open);
}
GSSAPILibrary* GSSAPILibrary::GetDefault() {
  return Singleton<GSSAPISharedLibrary>::get();
}

ScopedSecurityContext::ScopedSecurityContext(GSSAPILibrary* gssapi_lib)
    : security_context_(GSS_C_NO_CONTEXT),
      gssapi_lib_(gssapi_lib) {
  DCHECK(gssapi_lib_);
}

ScopedSecurityContext::~ScopedSecurityContext() {
  if (security_context_ != GSS_C_NO_CONTEXT) {
    gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
    OM_uint32 minor_status = 0;
    OM_uint32 major_status = gssapi_lib_->delete_sec_context(
        &minor_status, &security_context_, &output_token);
    if (major_status != GSS_S_COMPLETE) {
      LOG(WARNING) << "Problem releasing security_context. "
                   << DisplayStatus(major_status, minor_status);
    }
    security_context_ = GSS_C_NO_CONTEXT;
  }
}

HttpAuthGSSAPI::HttpAuthGSSAPI(GSSAPILibrary* library,
                               const std::string& scheme,
                               gss_OID gss_oid)
    : scheme_(scheme),
      gss_oid_(gss_oid),
      library_(library),
      scoped_sec_context_(library) {
  DCHECK(library_);
}

HttpAuthGSSAPI::~HttpAuthGSSAPI() {
}

bool HttpAuthGSSAPI::Init() {
  if (!library_)
    return false;
  return library_->Init();
}

bool HttpAuthGSSAPI::NeedsIdentity() const {
  return decoded_server_auth_token_.empty();
}

bool HttpAuthGSSAPI::IsFinalRound() const {
  return !NeedsIdentity();
}

bool HttpAuthGSSAPI::ParseChallenge(HttpAuth::ChallengeTokenizer* tok) {
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

int HttpAuthGSSAPI::GenerateAuthToken(const std::wstring* username,
                                      const std::wstring* password,
                                      const std::wstring& spn,
                                      std::string* auth_token) {
  DCHECK(auth_token);
  DCHECK((username == NULL) == (password == NULL));

  if (!IsFinalRound()) {
    int rv = OnFirstRound(username, password);
    if (rv != OK)
      return rv;
  }

  gss_buffer_desc input_token = GSS_C_EMPTY_BUFFER;
  input_token.length = decoded_server_auth_token_.length();
  input_token.value =
      (input_token.length > 0) ?
          const_cast<char*>(decoded_server_auth_token_.data()) :
          NULL;
  gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
  ScopedBuffer scoped_output_token(&output_token, library_);
  int rv = GetNextSecurityToken(spn, &input_token, &output_token);
  if (rv != OK)
    return rv;

  // Base64 encode data in output buffer and prepend the scheme.
  std::string encode_input(static_cast<char*>(output_token.value),
                           output_token.length);
  std::string encode_output;
  bool ok = base::Base64Encode(encode_input, &encode_output);
  if (!ok)
    return ERR_UNEXPECTED;
  *auth_token = scheme_ + " " + encode_output;
  return OK;
}

int HttpAuthGSSAPI::OnFirstRound(const std::wstring* username,
                                 const std::wstring* password) {
  // TODO(cbentzel): Acquire credentials?
  DCHECK((username == NULL) == (password == NULL));
  username_.clear();
  password_.clear();
  if (username) {
    username_ = *username;
    password_ = *password;
  }
  return OK;
}

int HttpAuthGSSAPI::GetNextSecurityToken(const std::wstring& spn,
                                         gss_buffer_t in_token,
                                         gss_buffer_t out_token) {
  // Create a name for the principal
  // TODO(cbentzel): Just do this on the first pass?
  std::string spn_principal = WideToASCII(spn);
  gss_buffer_desc spn_buffer = GSS_C_EMPTY_BUFFER;
  spn_buffer.value = const_cast<char*>(spn_principal.c_str());
  spn_buffer.length = spn_principal.size() + 1;
  OM_uint32 minor_status = 0;
  gss_name_t principal_name = GSS_C_NO_NAME;
  OM_uint32 major_status = library_->import_name(
      &minor_status,
      &spn_buffer,
      CHROME_GSS_C_NT_HOSTBASED_SERVICE,
      &principal_name);
  if (major_status != GSS_S_COMPLETE) {
    LOG(ERROR) << "Problem importing name from "
               << "spn \"" << spn_principal << "\""
               << std::endl
               << DisplayExtendedStatus(library_,
                                        major_status,
                                        minor_status);
    return ERR_UNEXPECTED;
  }
  ScopedName scoped_name(principal_name, library_);

  // Continue creating a security context.
  OM_uint32 req_flags = 0;
  major_status = library_->init_sec_context(
      &minor_status,
      GSS_C_NO_CREDENTIAL,
      scoped_sec_context_.receive(),
      principal_name,
      gss_oid_,
      req_flags,
      GSS_C_INDEFINITE,
      GSS_C_NO_CHANNEL_BINDINGS,
      in_token,
      NULL,  // actual_mech_type
      out_token,
      NULL,  // ret flags
      NULL);
  if (major_status != GSS_S_COMPLETE &&
      major_status != GSS_S_CONTINUE_NEEDED) {
    LOG(ERROR) << "Problem initializing context. "
               << std::endl
               << DisplayExtendedStatus(library_,
                                        major_status,
                                        minor_status)
               << std::endl
               << DescribeContext(library_, scoped_sec_context_.get());
    return ERR_MISSING_AUTH_CREDENTIALS;
  }

  return OK;
}

}  // namespace net
