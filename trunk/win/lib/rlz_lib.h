// Copyright 2011 Google Inc. All Rights Reserved.
// Use of this source code is governed by an Apache-style license that can be
// found in the COPYING file.
//
// A library to manage RLZ information for access-points shared
// across different client applications.
//
// All functions return true on success and false on error.
// This implemenation is thread safe.
//
// Each prototype mentions the registry access requirements:
//
// HKLM read:  Will work from any process and at any privilege level on Vista.
// HKCU read:  Calls made from the SYSTEM account must pass the current user's
//             SID as the optional 'sid' param. Can be called from low integrity
//             process on Vista.
// HKCU write: Calls made from the SYSTEM account must pass the current user's
//             SID as the optional 'sid' param. Calls require at least medium
//             integrity on Vista (e.g. Toolbar will need to use their broker)
// HKLM write: Calls must be made from an account with admin rights. No SID
//             need be passed when running as SYSTEM.
// Functions which do not access registry will be marked with "no restrictions".

#ifndef RLZ_WIN_LIB_RLZ_LIB_H_
#define RLZ_WIN_LIB_RLZ_LIB_H_

#include "rlz/lib/rlz_lib.h"

#if defined(OS_WIN)
#include "base/memory/scoped_ptr.h"
#include "base/win/registry.h"
#endif

namespace rlz_lib {

class LibMutex;

// The length of the Machine unique ID in WCHARs, excluding the NULL terminator.
const int kMachineIdLength = 50;


// TODO(thakis): Port these functions.
#if defined(OS_WIN)
// Event storage functions.

// Records an RLZ event.
// Some events can be product-independent (e.g: First search from home page),
// and some can be access point independent (e.g. Pack installed). However,
// product independent events must still include the product which cares about
// that information being reported.
// Access: HKCU write.
bool RLZ_LIB_API RecordProductEvent(Product product, AccessPoint point,
                                    Event event_id);

// Get all the events reported by this product as a CGI string to append to
// the daily ping.
// Access: HKCU read.
bool RLZ_LIB_API GetProductEventsAsCgi(Product product, char* unescaped_cgi,
                                       size_t unescaped_cgi_size);

// Clear all reported events and recorded stateful events of this product.
// This should be called on complete uninstallation of the product.
// Access: HKCU write.
bool RLZ_LIB_API ClearAllProductEvents(Product product);

// Clear an event reported by this product. This should be called after a
// successful ping to the RLZ server.
// Access: HKCU write.
bool RLZ_LIB_API ClearProductEvent(Product product, AccessPoint point,
                                   Event event_id);


// Complex helpers built on top of other functions.

// Parses RLZ related ping response information from the server.
// Updates stored RLZ values and clears stored events accordingly.
// Access: HKCU write.
bool RLZ_LIB_API ParsePingResponse(Product product, const char* response);

// Send the ping with RLZs and events to the PSO server.
// This ping method should be called daily. (More frequent calls will fail).
// Also, if there are no events, the call will succeed only once a week.
//
// product            : The product to ping for.
// access_points      : The access points this product affects. Array must be
//                      terminated with NO_ACCESS_POINT.
// product_signature  : The signature sent with daily pings (e.g. swg, ietb)
// product_brand      : The brand of the pinging product, if any.
// product_id         : The product-specific installation ID (can be NULL).
// product_lang       : The language for the product (used to determine cohort).
// exclude_machine_id : Whether the Machine ID should be explicitly excluded
//                      based on the products privacy policy.
//
// Returns true on successful ping and response, false otherwise.
// Access: HKCU write.
bool RLZ_LIB_API SendFinancialPing(Product product,
                                   const AccessPoint* access_points,
                                   const char* product_signature,
                                   const char* product_brand,
                                   const char* product_id,
                                   const char* product_lang,
                                   bool exclude_machine_id);

// An alternate implementations of SendFinancialPing with the same behavior,
// except the caller can optionally choose to skip the timing check.
bool RLZ_LIB_API SendFinancialPing(Product product,
                                   const AccessPoint* access_points,
                                   const char* product_signature,
                                   const char* product_brand,
                                   const char* product_id,
                                   const char* product_lang,
                                   bool exclude_machine_id,
                                   const bool skip_time_check);



// Clears all product-specifc state from the RLZ registry.
// Should be called during product uninstallation.
// This removes outstanding product events, product financial ping times,
// the product RLS argument (if any), and any RLZ's for access points being
// uninstalled with the product.
// access_points is an array terminated with NO_ACCESS_POINT.
// IMPORTANT: These are the access_points the product is removing as part
// of the uninstallation, not necessarily all the access points passed to
// SendFinancialPing() and GetPingParams().
// access_points can be NULL if no points are being uninstalled.
// No return value - this is best effort. Will assert in debug mode on
// failed attempts.
// Access: HKCU write.
void RLZ_LIB_API ClearProductState(Product product,
                                   const AccessPoint* access_points);

// Gets the unique ID for the machine used for RLZ tracking purposes. This ID
// is derived from the Windows machine SID, and is the string representation of
// a 20 byte hash + a 1 byte checksum.
// Included in financial pings with events, unless explicitly forbidden by the
// calling application.
// Access: HKLM read.
bool GetMachineId(char* buffer, int buffer_size);


// Segment RLZ persistence based on branding information.
// All information for a given product is persisted under keys with the either
// product's name or its access point's name.  This assumes that only
// one instance of the product is installed on the machine, and that only one
// product brand is associated with it.
//
// In some cases, a given product may be using supplementary brands.  The RLZ
// information must be kept separately for each of these brands.  To achieve
// this segmentation, scope all RLZ library calls that deal with supplementary
// brands within the lifetime of an rlz_lib::ProductBranding instance.
//
// For example, to record events for a supplementary brand, do the following:
//
//  {
//    rlz_lib::SupplementaryBranding branding("AAAA");
//    // This call to RecordProductEvent is scoped to the AAAA brand.
//    rlz_lib::RecordProductEvent(rlz_lib::DESKTOP, rlz_lib::GD_DESKBAND,
//                                rlz_lib::INSTALL);
//  }
//
//  // This call to RecordProductEvent is not scoped to any supplementary brand.
//  rlz_lib::RecordProductEvent(rlz_lib::DESKTOP, rlz_lib::GD_DESKBAND,
//                              rlz_lib::INSTALL);
//
// In particular, this affects the recording of stateful events and the sending
// of financial pings.  In the former case, a stateful event recorded while
// scoped to a supplementary brand will be recorded again when scoped to a
// different supplementary brand (or not scoped at all).  In the latter case,
// the time skip check is specific to each supplementary brand.
class SupplementaryBranding {
 public:
  SupplementaryBranding(const wchar_t* brand);
  ~SupplementaryBranding();

  static const std::wstring& GetBrand() { return brand_; }

  static void AppendBrandToString(std::wstring* str);

 private:
  scoped_ptr<LibMutex> lock_;

  static std::wstring brand_;
};

// Initialize temporary HKLM/HKCU registry hives used for testing.
// Testing RLZ requires reading and writing to the Windows registry.  To keep
// the tests isolated from the machine's state, as well as to prevent the tests
// from causing side effects in the registry, HKCU and HKLM are overridden for
// the duration of the tests. RLZ tests don't expect the HKCU and KHLM hives to
// be empty though, and this function initializes the minimum value needed so
// that the test will run successfully.
//
// The two arguments to this function should be the keys that will represent
// the HKLM and HKCU registry hives during the tests.  This function should be
// called *before* the hives are overridden.
void InitializeTempHivesForTesting(const base::win::RegKey& temp_hklm_key,
                                   const base::win::RegKey& temp_hkcu_key);
#endif  // defined(OS_WIN)

}  // namespace rlz_lib

#endif  // RLZ_WIN_LIB_RLZ_LIB_H_
