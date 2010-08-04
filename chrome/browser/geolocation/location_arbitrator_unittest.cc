// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/geolocation/location_arbitrator.h"

#include "base/scoped_ptr.h"
#include "chrome/browser/geolocation/fake_access_token_store.h"
#include "chrome/browser/geolocation/location_provider.h"
#include "chrome/browser/geolocation/mock_location_provider.h"
#include "chrome/common/geoposition.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class MockLocationObserver : public GeolocationArbitrator::Delegate {
 public:
  void InvalidateLastPosition() {
    last_position_.accuracy = -1;
    last_position_.error_code = Geoposition::ERROR_CODE_NONE;
    ASSERT_FALSE(last_position_.IsInitialized());
  }
  // Delegate
  virtual void OnLocationUpdate(const Geoposition& position) {
    last_position_ = position;
  }

  Geoposition last_position_;
};

class MockProviderFactory : public GeolocationArbitrator::ProviderFactory {
 public:
  MockProviderFactory()
      : cell_(NULL), gps_(NULL) {
  }
  virtual LocationProviderBase* NewNetworkLocationProvider(
      AccessTokenStore* access_token_store,
      URLRequestContextGetter* context,
      const GURL& url,
      const string16& access_token) {
    return new MockLocationProvider(&cell_);
  }
  virtual LocationProviderBase* NewGpsLocationProvider() {
    return new MockLocationProvider(&gps_);
  }

  // Two location providers, with nice short names to make the tests more
  // readable. Note |gps_| will only be set when there is a high accuracy
  // observer registered (and |cell_| when there's at least one observer of any
  // type).
  MockLocationProvider* cell_;
  MockLocationProvider* gps_;
};

void SetPositionFix(Geoposition* position,
                    double latitude,
                    double longitude,
                    double accuracy,
                    const base::Time& timestamp) {
  ASSERT_TRUE(position);
  position->error_code = Geoposition::ERROR_CODE_NONE;
  position->latitude = latitude;
  position->longitude = longitude;
  position->accuracy = accuracy;
  position->timestamp = timestamp;
  ASSERT_TRUE(position->IsInitialized());
  ASSERT_TRUE(position->IsValidFix());
}

void SetReferencePosition(Geoposition* position) {
  SetPositionFix(position, 51.0, -0.1, 400, base::Time::FromDoubleT(54321.0));
}

double g_fake_time_now_secs = 1;

base::Time GetTimeNow() {
  return base::Time::FromDoubleT(g_fake_time_now_secs);
}

void AdvanceTimeNow(const base::TimeDelta& delta) {
  g_fake_time_now_secs += delta.InSecondsF();
}

class GeolocationLocationArbitratorTest : public testing::Test {
 protected:
  virtual void SetUp() {
    access_token_store_ = new FakeAccessTokenStore;
    providers_ = new MockProviderFactory();
    arbitrator_ = GeolocationArbitrator::Create(access_token_store_.get(),
                                                NULL, GetTimeNow, providers_);
  }

  virtual void TearDown() {
  }

  void CheckLastPositionInfo(double latitude,
                             double longitude,
                             double accuracy) {
    Geoposition geoposition = arbitrator_->GetCurrentPosition();
    EXPECT_TRUE(geoposition.IsValidFix());
    EXPECT_DOUBLE_EQ(latitude, geoposition.latitude);
    EXPECT_DOUBLE_EQ(longitude, geoposition.longitude);
    EXPECT_DOUBLE_EQ(accuracy, geoposition.accuracy);
  }

  base::TimeDelta SwitchOnFreshnessCliff() {
    // Add 1, to ensure it meets any greater-than test.
    return base::TimeDelta::FromMilliseconds(
        GeolocationArbitrator::kFixStaleTimeoutMilliseconds + 1);
  }

  scoped_refptr<FakeAccessTokenStore> access_token_store_;
  scoped_refptr<MockProviderFactory> providers_;
  scoped_refptr<GeolocationArbitrator> arbitrator_;
};

TEST_F(GeolocationLocationArbitratorTest, CreateDestroy) {
  EXPECT_TRUE(access_token_store_);
  EXPECT_TRUE(arbitrator_ != NULL);
  arbitrator_ = NULL;
  SUCCEED();
}

TEST_F(GeolocationLocationArbitratorTest, OnPermissionGranted) {
  EXPECT_FALSE(arbitrator_->HasPermissionBeenGranted());
  arbitrator_->OnPermissionGranted(GURL("http://frame.test"));
  EXPECT_TRUE(arbitrator_->HasPermissionBeenGranted());
  // Can't check the provider has been notified without going through the
  // motions to create the provider (see next test).
  EXPECT_FALSE(providers_->cell_);
  EXPECT_FALSE(providers_->gps_);
}

TEST_F(GeolocationLocationArbitratorTest, NormalUsage) {
  ASSERT_TRUE(access_token_store_);
  ASSERT_TRUE(arbitrator_ != NULL);

  EXPECT_TRUE(access_token_store_->access_token_set_.empty());
  EXPECT_TRUE(access_token_store_->request_);
  MockLocationObserver observer;
  arbitrator_->AddObserver(&observer, GeolocationArbitrator::UpdateOptions());

  EXPECT_TRUE(access_token_store_->access_token_set_.empty());
  ASSERT_TRUE(access_token_store_->request_);

  EXPECT_FALSE(providers_->cell_);
  EXPECT_FALSE(providers_->gps_);
  access_token_store_->NotifyDelegateTokensLoaded();
  ASSERT_TRUE(providers_->cell_);
  EXPECT_TRUE(providers_->gps_);
  EXPECT_TRUE(providers_->cell_->has_listeners());
  EXPECT_EQ(MockLocationProvider::LOW_ACCURACY, providers_->cell_->state_);
  EXPECT_EQ(MockLocationProvider::LOW_ACCURACY, providers_->gps_->state_);
  EXPECT_FALSE(observer.last_position_.IsInitialized());

  SetReferencePosition(&providers_->cell_->position_);
  providers_->cell_->UpdateListeners();
  EXPECT_TRUE(observer.last_position_.IsInitialized());
  EXPECT_EQ(providers_->cell_->position_.latitude,
            observer.last_position_.latitude);

  EXPECT_FALSE(
      providers_->cell_->permission_granted_url_.is_valid());
  EXPECT_FALSE(arbitrator_->HasPermissionBeenGranted());
  GURL frame_url("http://frame.test");
  arbitrator_->OnPermissionGranted(frame_url);
  EXPECT_TRUE(arbitrator_->HasPermissionBeenGranted());
  EXPECT_TRUE(
      providers_->cell_->permission_granted_url_.is_valid());
  EXPECT_EQ(frame_url,
            providers_->cell_->permission_granted_url_);

  EXPECT_TRUE(arbitrator_->RemoveObserver(&observer));
}

TEST_F(GeolocationLocationArbitratorTest, MultipleListener) {
  MockLocationObserver observer1;
  arbitrator_->AddObserver(&observer1, GeolocationArbitrator::UpdateOptions());
  MockLocationObserver observer2;
  arbitrator_->AddObserver(&observer2, GeolocationArbitrator::UpdateOptions());

  access_token_store_->NotifyDelegateTokensLoaded();
  ASSERT_TRUE(providers_->cell_);
  EXPECT_FALSE(observer1.last_position_.IsInitialized());
  EXPECT_FALSE(observer2.last_position_.IsInitialized());

  SetReferencePosition(&providers_->cell_->position_);
  providers_->cell_->UpdateListeners();
  EXPECT_TRUE(observer1.last_position_.IsInitialized());
  EXPECT_TRUE(observer2.last_position_.IsInitialized());

  // Add third observer, and remove the first.
  MockLocationObserver observer3;
  arbitrator_->AddObserver(&observer3, GeolocationArbitrator::UpdateOptions());
  EXPECT_TRUE(arbitrator_->RemoveObserver(&observer1));
  observer1.InvalidateLastPosition();
  observer2.InvalidateLastPosition();
  observer3.InvalidateLastPosition();

  providers_->cell_->UpdateListeners();
  EXPECT_FALSE(observer1.last_position_.IsInitialized());
  EXPECT_TRUE(observer2.last_position_.IsInitialized());
  EXPECT_TRUE(observer3.last_position_.IsInitialized());

  EXPECT_TRUE(arbitrator_->RemoveObserver(&observer2));
  EXPECT_TRUE(arbitrator_->RemoveObserver(&observer3));
}

TEST_F(GeolocationLocationArbitratorTest,
       MultipleAddObserverCallsFromSameListener) {
  MockLocationObserver observer;
  arbitrator_->AddObserver(
      &observer, GeolocationArbitrator::UpdateOptions(false));
  access_token_store_->NotifyDelegateTokensLoaded();
  ASSERT_TRUE(providers_->cell_);
  ASSERT_TRUE(providers_->gps_);
  EXPECT_EQ(MockLocationProvider::LOW_ACCURACY, providers_->cell_->state_);
  EXPECT_EQ(MockLocationProvider::LOW_ACCURACY, providers_->gps_->state_);
  arbitrator_->AddObserver(
      &observer, GeolocationArbitrator::UpdateOptions(true));
  EXPECT_EQ(MockLocationProvider::HIGH_ACCURACY, providers_->cell_->state_);
  EXPECT_EQ(MockLocationProvider::HIGH_ACCURACY, providers_->gps_->state_);
  arbitrator_->AddObserver(
      &observer, GeolocationArbitrator::UpdateOptions(false));
  EXPECT_EQ(MockLocationProvider::LOW_ACCURACY, providers_->cell_->state_);
  EXPECT_EQ(MockLocationProvider::LOW_ACCURACY, providers_->gps_->state_);
  EXPECT_TRUE(arbitrator_->RemoveObserver(&observer));
  EXPECT_EQ(MockLocationProvider::STOPPED, providers_->cell_->state_);
  EXPECT_EQ(MockLocationProvider::STOPPED, providers_->gps_->state_);
  EXPECT_FALSE(arbitrator_->RemoveObserver(&observer));
}

TEST_F(GeolocationLocationArbitratorTest, RegistrationAfterFixArrives) {
  MockLocationObserver observer1;
  arbitrator_->AddObserver(&observer1, GeolocationArbitrator::UpdateOptions());

  access_token_store_->NotifyDelegateTokensLoaded();
  ASSERT_TRUE(providers_->cell_);
  EXPECT_FALSE(observer1.last_position_.IsInitialized());
  SetReferencePosition(&providers_->cell_->position_);
  providers_->cell_->UpdateListeners();
  EXPECT_TRUE(observer1.last_position_.IsValidFix());

  MockLocationObserver observer2;
  EXPECT_FALSE(observer2.last_position_.IsValidFix());
  arbitrator_->AddObserver(&observer2, GeolocationArbitrator::UpdateOptions());
  EXPECT_TRUE(observer2.last_position_.IsValidFix());

  EXPECT_TRUE(arbitrator_->RemoveObserver(&observer1));
  EXPECT_TRUE(arbitrator_->RemoveObserver(&observer2));
}


TEST_F(GeolocationLocationArbitratorTest, Arbitration) {
  // No position so far
  EXPECT_FALSE(arbitrator_->GetCurrentPosition().IsInitialized());
  MockLocationObserver observer;
  arbitrator_->AddObserver(&observer,
                           GeolocationArbitrator::UpdateOptions(true));
  access_token_store_->NotifyDelegateTokensLoaded();
  ASSERT_TRUE(providers_->cell_);
  ASSERT_TRUE(providers_->gps_);

  SetPositionFix(&providers_->cell_->position_, 1, 2, 150, GetTimeNow());
  providers_->cell_->UpdateListeners();

  // First position available
  EXPECT_TRUE(arbitrator_->GetCurrentPosition().IsValidFix());
  CheckLastPositionInfo(1, 2, 150);

  SetPositionFix(&providers_->gps_->position_, 3, 4, 50, GetTimeNow());
  providers_->gps_->UpdateListeners();

  // More accurate fix available
  CheckLastPositionInfo(3, 4, 50);

  SetPositionFix(&providers_->cell_->position_, 5, 6, 150, GetTimeNow());
  providers_->cell_->UpdateListeners();

  // New fix is available but it's less accurate, older fix should be kept.
  CheckLastPositionInfo(3, 4, 50);

  // Advance time, and notify once again
  AdvanceTimeNow(SwitchOnFreshnessCliff());
  providers_->cell_->UpdateListeners();

  // New fix is available, less accurate but fresher
  CheckLastPositionInfo(5, 6, 150);

  // Advance time, and set a low accuracy position
  AdvanceTimeNow(SwitchOnFreshnessCliff());
  SetPositionFix(&providers_->cell_->position_, 5.676731, 139.629385, 1000,
                 GetTimeNow());
  providers_->cell_->UpdateListeners();
  CheckLastPositionInfo(5.676731, 139.629385, 1000);

  // 15 secs later, step outside. Switches to gps signal.
  AdvanceTimeNow(base::TimeDelta::FromSeconds(15));
  SetPositionFix(&providers_->gps_->position_, 3.5676457, 139.629198, 50,
                 GetTimeNow());
  providers_->gps_->UpdateListeners();
  CheckLastPositionInfo(3.5676457, 139.629198, 50);

  // 5 mins later switch cells while walking. Stay on gps.
  AdvanceTimeNow(base::TimeDelta::FromMinutes(5));
  SetPositionFix(&providers_->cell_->position_, 3.567832, 139.634648, 300,
                 GetTimeNow());
  SetPositionFix(&providers_->gps_->position_, 3.5677675, 139.632314, 50,
                 GetTimeNow());
  providers_->cell_->UpdateListeners();
  providers_->gps_->UpdateListeners();
  CheckLastPositionInfo(3.5677675, 139.632314, 50);

  // Ride train and gps signal degrades slightly. Stay on fresher gps
  AdvanceTimeNow(base::TimeDelta::FromMinutes(5));
  SetPositionFix(&providers_->gps_->position_, 3.5679026, 139.634777, 300,
                 GetTimeNow());
  providers_->gps_->UpdateListeners();
  CheckLastPositionInfo(3.5679026, 139.634777, 300);

  // 14 minutes later
  AdvanceTimeNow(base::TimeDelta::FromMinutes(14));

  // GPS reading misses a beat, but don't switch to cell yet to avoid
  // oscillating.
  SetPositionFix(&providers_->gps_->position_, 3.5659005, 139.682579, 300,
                 GetTimeNow());
  providers_->gps_->UpdateListeners();

  AdvanceTimeNow(base::TimeDelta::FromSeconds(7));
  SetPositionFix(&providers_->cell_->position_, 3.5689579, 139.691420, 1000,
                 GetTimeNow());
  providers_->cell_->UpdateListeners();
  CheckLastPositionInfo(3.5659005, 139.682579, 300);

  // 1 minute later
  AdvanceTimeNow(base::TimeDelta::FromMinutes(1));

  // Enter tunnel. Stay on fresher gps for a moment.
  SetPositionFix(&providers_->cell_->position_, 3.5657078, 139.68922, 300,
                 GetTimeNow());
  providers_->cell_->UpdateListeners();
  SetPositionFix(&providers_->gps_->position_, 3.5657104, 139.690341, 300,
                 GetTimeNow());
  providers_->gps_->UpdateListeners();
  CheckLastPositionInfo(3.5657104, 139.690341, 300);

  // 2 minutes later
  AdvanceTimeNow(base::TimeDelta::FromMinutes(2));
  // Arrive in station. Cell moves but GPS is stale. Switch to fresher cell.
  SetPositionFix(&providers_->cell_->position_, 3.5658700, 139.069979, 1000,
                 GetTimeNow());
  providers_->cell_->UpdateListeners();
  CheckLastPositionInfo(3.5658700, 139.069979, 1000);

  EXPECT_TRUE(arbitrator_->RemoveObserver(&observer));
}

}  // namespace
