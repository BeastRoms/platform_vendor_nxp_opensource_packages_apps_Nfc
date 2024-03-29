/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2018 NXP Semiconductors
 * The original Work has been changed by NXP Semiconductors.
 *
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 *  Manage the listen-mode routing table.
 */
#include <android-base/stringprintf.h>
#include <base/logging.h>
#include <nativehelper/JNIHelp.h>
#include <nativehelper/ScopedLocalRef.h>

#include "JavaClassConstants.h"
#include "RoutingManager.h"
#include "nfa_ce_api.h"
#include "nfa_ee_api.h"
#include "nfc_config.h"
#if (NXP_EXTNS == TRUE)
#include "MposManager.h"
#include "SecureElement.h"
#endif

using android::base::StringPrintf;

extern bool gActivated;
extern SyncEvent gDeactivatedEvent;
extern bool nfc_debug_enabled;

const JNINativeMethod RoutingManager::sMethods[] = {
    {"doGetDefaultRouteDestination", "()I",
     (void*)RoutingManager::
         com_android_nfc_cardemulation_doGetDefaultRouteDestination},
    {"doGetDefaultOffHostRouteDestination", "()I",
     (void*)RoutingManager::
         com_android_nfc_cardemulation_doGetDefaultOffHostRouteDestination},
    {"doGetAidMatchingMode", "()I",
     (void*)
         RoutingManager::com_android_nfc_cardemulation_doGetAidMatchingMode}};

static const int MAX_NUM_EE = 5;
// SCBR from host works only when App is in foreground
static const uint8_t SYS_CODE_PWR_STATE_HOST = 0x01;
#if (NXP_EXTNS != TRUE)
static const uint16_t DEFAULT_SYS_CODE = 0xFEFE;
#else
static RouteInfo_t gRouteInfo;
extern jint nfcManager_getUiccRoute(jint uicc_slot);
extern jint nfcManager_getUiccId(jint uicc_slot);
extern uint16_t sCurrentSelectedUICCSlot;
extern bool isDynamicUiccEnabled;
#endif
RoutingManager::RoutingManager() {
  static const char fn[] = "RoutingManager::RoutingManager()";

  mDefaultOffHostRoute =
      NfcConfig::getUnsigned(NAME_DEFAULT_OFFHOST_ROUTE, 0x00);

  mDefaultFelicaRoute = NfcConfig::getUnsigned(NAME_DEFAULT_NFCF_ROUTE, 0x00);
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
      "%s: Active SE for Nfc-F is 0x%02X", fn, mDefaultFelicaRoute);

  mDefaultEe = NfcConfig::getUnsigned(NAME_DEFAULT_ROUTE, 0x00);
  DLOG_IF(INFO, nfc_debug_enabled)
      << StringPrintf("%s: default route is 0x%02X", fn, mDefaultEe);

  mAidMatchingMode =
      NfcConfig::getUnsigned(NAME_AID_MATCHING_MODE, AID_MATCHING_EXACT_ONLY);

  mDefaultSysCodeRoute =
      NfcConfig::getUnsigned(NAME_DEFAULT_SYS_CODE_ROUTE, 0xC0);

  mDefaultSysCodePowerstate =
      NfcConfig::getUnsigned(NAME_DEFAULT_SYS_CODE_PWR_STATE, 0x19);
#if (NXP_EXTNS != TRUE)
  mDefaultSysCode = DEFAULT_SYS_CODE;
#else
  mDefaultSysCode = 0x00;
#endif
  if (NfcConfig::hasKey(NAME_DEFAULT_SYS_CODE)) {
    std::vector<uint8_t> pSysCode = NfcConfig::getBytes(NAME_DEFAULT_SYS_CODE);
    if (pSysCode.size() == 0x02) {
      mDefaultSysCode = ((pSysCode[0] << 8) | ((int)pSysCode[1] << 0));
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: DEFAULT_SYS_CODE: 0x%02X", __func__, mDefaultSysCode);
    }
  }

  memset(&mEeInfo, 0, sizeof(mEeInfo));
  mReceivedEeInfo = false;
  mSeTechMask = 0x00;
  mIsScbrSupported = false;

  mNfcFOnDhHandle = NFA_HANDLE_INVALID;
}

RoutingManager::~RoutingManager() { NFA_EeDeregister(nfaEeCallback); }

bool RoutingManager::initialize(nfc_jni_native_data* native) {
  static const char fn[] = "RoutingManager::initialize()";
  mNativeData = native;

  tNFA_STATUS nfaStat;
  {
    SyncEventGuard guard(mEeRegisterEvent);
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: try ee register", fn);
    nfaStat = NFA_EeRegister(nfaEeCallback);
    if (nfaStat != NFA_STATUS_OK) {
      LOG(ERROR) << StringPrintf("%s: fail ee register; error=0x%X", fn,
                                 nfaStat);
      return false;
    }
    mEeRegisterEvent.wait();
  }

  mRxDataBuffer.clear();
#if (NXP_EXTNS == TRUE)
    memset(&gRouteInfo, 0x00, sizeof(RouteInfo_t));
    unsigned long tech = 0;
    unsigned long num = 0;
    bool fwdFunctionality = false;
    if ((GetNxpNumValue(NAME_HOST_LISTEN_TECH_MASK, &tech, sizeof(tech))))
        mHostListnTechMask = tech;
    else
        mHostListnTechMask = 0x03;
    LOG(ERROR) << StringPrintf("%s: mHostListnTechMask=0x%X", fn,mHostListnTechMask);

    if ((GetNxpNumValue(NAME_FORWARD_FUNCTIONALITY_ENABLE, &fwdFunctionality, sizeof(fwdFunctionality))))
        mFwdFuntnEnable = fwdFunctionality;
    else
        mFwdFuntnEnable = false;
    LOG(ERROR) << StringPrintf("%s: mFwdFuntnEnable=0x%X", fn,mFwdFuntnEnable);

    if (GetNxpNumValue (NAME_DEFAULT_FELICA_CLT_PWR_STATE, (void*)&num, sizeof(num)))
       mDefaultTechFPowerstate = num;
    else
       mDefaultTechFPowerstate = 0x3F;
    if (GetNxpNumValue (NAME_NXP_DEFAULT_SE, (void*)&num, sizeof(num)))
        mDefaultEe = num;
    else
        mDefaultEe = 0x02;
    mUiccListnTechMask = NfcConfig::getUnsigned(NAME_UICC_LISTEN_TECH_MASK, 0x07);
    if (GetNxpNumValue (NAME_DEFAULT_AID_ROUTE, (void*)&num, sizeof(num)))
        mDefaultIso7816SeID = num;
    else
        mDefaultIso7816SeID = NFA_HANDLE_INVALID;
    if (GetNxpNumValue (NAME_DEFAULT_AID_PWR_STATE, (void*)&num, sizeof(num)))
        mDefaultIso7816Powerstate = num;
    else
        mDefaultIso7816Powerstate = 0xFF;
    if(GetNxpNumValue (NAME_DEFUALT_GSMA_PWR_STATE, (void*)&num, sizeof(num)))
        mDefaultGsmaPowerState = num;
    else
        mDefaultGsmaPowerState = 0x00;
    DLOG_IF(INFO, nfc_debug_enabled)
        << StringPrintf("%s: mDefaultGsmaPowerState %02x)", fn, mDefaultGsmaPowerState);
    LOG(ERROR) << StringPrintf("%s: >>>> mDefaultIso7816SeID=0x%X", fn, mDefaultIso7816SeID);
    LOG(ERROR) << StringPrintf("%s: >>>> mDefaultIso7816Powerstate=0x%X", fn, mDefaultIso7816Powerstate);
#endif
  if ((mDefaultOffHostRoute != 0) || (mDefaultFelicaRoute != 0)) {
    DLOG_IF(INFO, nfc_debug_enabled)
        << StringPrintf("%s: Technology Routing (NfcASe:0x%02x, NfcFSe:0x%02x)",
                        fn, mDefaultOffHostRoute, mDefaultFelicaRoute);
    {
      // Wait for EE info if needed
      SyncEventGuard guard(mEeInfoEvent);
      if (!mReceivedEeInfo) {
        LOG(INFO) << StringPrintf("Waiting for EE info");
        mEeInfoEvent.wait();
      }
    }

    DLOG_IF(INFO, nfc_debug_enabled)
        << StringPrintf("%s: Number of EE is %d", fn, mEeInfo.num_ee);
    for (uint8_t i = 0; i < mEeInfo.num_ee; i++) {
      tNFA_HANDLE eeHandle = mEeInfo.ee_disc_info[i].ee_handle;
      tNFA_TECHNOLOGY_MASK seTechMask = 0;

      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s   EE[%u] Handle: 0x%04x  techA: 0x%02x  techB: "
          "0x%02x  techF: 0x%02x  techBprime: 0x%02x",
          fn, i, eeHandle, mEeInfo.ee_disc_info[i].la_protocol,
          mEeInfo.ee_disc_info[i].lb_protocol,
          mEeInfo.ee_disc_info[i].lf_protocol,
          mEeInfo.ee_disc_info[i].lbp_protocol);
      if ((mDefaultOffHostRoute != 0) &&
          (eeHandle == (mDefaultOffHostRoute | NFA_HANDLE_GROUP_EE))) {
        if (mEeInfo.ee_disc_info[i].la_protocol != 0)
          seTechMask |= NFA_TECHNOLOGY_MASK_A;
      }
      if ((mDefaultFelicaRoute != 0) &&
          (eeHandle == (mDefaultFelicaRoute | NFA_HANDLE_GROUP_EE))) {
        if (mEeInfo.ee_disc_info[i].lf_protocol != 0)
          seTechMask |= NFA_TECHNOLOGY_MASK_F;
      }

      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: seTechMask[%u]=0x%02x", fn, i, seTechMask);
      if (seTechMask != 0x00) {
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
            "Configuring tech mask 0x%02x on EE 0x%04x", seTechMask, eeHandle);

        nfaStat = NFA_CeConfigureUiccListenTech(eeHandle, seTechMask);
        if (nfaStat != NFA_STATUS_OK)
          LOG(ERROR) << StringPrintf(
              "Failed to configure UICC listen technologies.");

        // Set technology routes to UICC if it's there
        nfaStat =
            NFA_EeSetDefaultTechRouting(eeHandle, seTechMask, seTechMask, 0,
                                        seTechMask, seTechMask, seTechMask);

        if (nfaStat != NFA_STATUS_OK)
          LOG(ERROR) << StringPrintf(
              "Failed to configure UICC technology routing.");

        mSeTechMask |= seTechMask;
      }
    }
  }

  // Tell the host-routing to only listen on Nfc-A
#if (NXP_EXTNS == TRUE)
  nfaStat = NFA_CeSetIsoDepListenTech(NFA_TECHNOLOGY_MASK_A & mHostListnTechMask);
#else
  nfaStat = NFA_CeSetIsoDepListenTech(NFA_TECHNOLOGY_MASK_A);
#endif
  if (nfaStat != NFA_STATUS_OK)
    LOG(ERROR) << StringPrintf("Failed to configure CE IsoDep technologies");

  // Register a wild-card for AIDs routed to the host
  nfaStat = NFA_CeRegisterAidOnDH(NULL, 0, stackCallback);
  if (nfaStat != NFA_STATUS_OK)
    LOG(ERROR) << StringPrintf("Failed to register wildcard AID for DH");

  if (NFC_GetNCIVersion() == NCI_VERSION_2_0) {
    if(mDefaultSysCode != 0x00)
    {
#if (NXP_EXTNS == TRUE)
      uint16_t routeLoc = NFA_HANDLE_INVALID;
#endif
      SyncEventGuard guard(mRoutingEvent);

#if (NXP_EXTNS == TRUE)
      routeLoc = ((mDefaultSysCodeRoute == 0x00) ? ROUTE_LOC_HOST_ID :
        ((mDefaultSysCodeRoute == 0x01 ) ? ROUTE_LOC_ESE_ID : getUiccRouteLocId(mDefaultSysCodeRoute)));
      if(mDefaultSysCodeRoute == 0)
      {
        mDefaultSysCodePowerstate &= 0x11;
      }
#endif

      LOG(ERROR) << StringPrintf("mDefaultSysCodeRoute routeLoc = 0x%x", routeLoc);
      // Register System Code for routing
#if (NXP_EXTNS == TRUE)
      nfaStat = NFA_EeAddSystemCodeRouting(mDefaultSysCode, routeLoc,
                                         mDefaultSysCodePowerstate);
#else
      nfaStat = NFA_EeAddSystemCodeRouting(mDefaultSysCode, mDefaultSysCodeRoute,
                                         mDefaultSysCodePowerstate);
#endif
      if (nfaStat == NFA_STATUS_NOT_SUPPORTED) {
        mIsScbrSupported = false;
        LOG(ERROR) << StringPrintf("%s: SCBR not supported", fn);
      } else if (nfaStat == NFA_STATUS_OK) {
        mIsScbrSupported = true;
        mRoutingEvent.wait();
        DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: Succeed to register system code", fn);
      }  else {
        LOG(ERROR) << StringPrintf("%s: Fail to register system code", fn);
      }
    }
  }
  return true;
}

RoutingManager& RoutingManager::getInstance() {
  static RoutingManager manager;
  return manager;
}

void RoutingManager::enableRoutingToHost() {
  tNFA_STATUS nfaStat;
  tNFA_TECHNOLOGY_MASK techMask;
  tNFA_PROTOCOL_MASK protoMask;
  SyncEventGuard guard(mRoutingEvent);

  // Set default routing at one time when the NFCEE IDs for Nfc-A and Nfc-F are
  // same
  if (mDefaultEe == mDefaultFelicaRoute) {
    // Route Nfc-A/Nfc-F to host if we don't have a SE
    techMask = (mSeTechMask ^ (NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_F));
    if (techMask != 0) {
      nfaStat = NFA_EeSetDefaultTechRouting(mDefaultEe, techMask, 0, 0,
                                            techMask, techMask, techMask);
      if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait();
      else
        LOG(ERROR) << StringPrintf(
            "Fail to set default tech routing for Nfc-A/Nfc-F");
    }
    // Default routing for IsoDep and T3T protocol
    if (mIsScbrSupported)
      protoMask = NFA_PROTOCOL_MASK_ISO_DEP;
    else
      protoMask = (NFA_PROTOCOL_MASK_ISO_DEP | NFA_PROTOCOL_MASK_T3T);

    nfaStat = NFA_EeSetDefaultProtoRouting(
        mDefaultEe, protoMask, 0, 0, protoMask, mDefaultEe ? protoMask : 0,
        mDefaultEe ? protoMask : 0);
    if (nfaStat == NFA_STATUS_OK)
      mRoutingEvent.wait();
    else
      LOG(ERROR) << StringPrintf(
          "Fail to set default proto routing for protocol: 0x%x", protoMask);
  } else {
    // Route Nfc-A to host if we don't have a SE
    techMask = NFA_TECHNOLOGY_MASK_A;
    if ((mSeTechMask & NFA_TECHNOLOGY_MASK_A) == 0) {
      nfaStat = NFA_EeSetDefaultTechRouting(mDefaultEe, techMask, 0, 0,
                                            techMask, techMask, techMask);
      if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait();
      else
        LOG(ERROR) << StringPrintf(
            "Fail to set default tech routing for Nfc-A");
    }
    // Default routing for IsoDep protocol
    protoMask = NFA_PROTOCOL_MASK_ISO_DEP;
    nfaStat = NFA_EeSetDefaultProtoRouting(
        mDefaultEe, protoMask, 0, 0, protoMask, mDefaultEe ? protoMask : 0,
        mDefaultEe ? protoMask : 0);
    if (nfaStat == NFA_STATUS_OK)
      mRoutingEvent.wait();
    else
      LOG(ERROR) << StringPrintf(
          "Fail to set default proto routing for IsoDep");

    // Route Nfc-F to host if we don't have a SE
    techMask = NFA_TECHNOLOGY_MASK_F;
    if ((mSeTechMask & NFA_TECHNOLOGY_MASK_F) == 0) {
      nfaStat = NFA_EeSetDefaultTechRouting(mDefaultFelicaRoute, techMask, 0, 0,
                                            techMask, techMask, techMask);
      if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait();
      else
        LOG(ERROR) << StringPrintf(
            "Fail to set default tech routing for Nfc-F");
    }
    // Default routing for T3T protocol
    if (!mIsScbrSupported) {
      protoMask = NFA_PROTOCOL_MASK_T3T;
      nfaStat =
          NFA_EeSetDefaultProtoRouting(NFC_DH_ID, protoMask, 0, 0, 0, 0, 0);
      if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait();
      else
        LOG(ERROR) << StringPrintf("Fail to set default proto routing for T3T");
    }
  }
}

void RoutingManager::disableRoutingToHost() {
  tNFA_STATUS nfaStat;
  tNFA_TECHNOLOGY_MASK techMask;
  SyncEventGuard guard(mRoutingEvent);

  // Set default routing at one time when the NFCEE IDs for Nfc-A and Nfc-F are
  // same
  if (mDefaultEe == mDefaultFelicaRoute) {
    // Default routing for Nfc-A/Nfc-F technology if we don't have a SE
    techMask = (mSeTechMask ^ (NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_F));
    if (techMask != 0) {
      nfaStat = NFA_EeSetDefaultTechRouting(mDefaultEe, 0, 0, 0, 0, 0, 0);
      if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait();
      else
        LOG(ERROR) << StringPrintf(
            "Fail to set default tech routing for Nfc-A/Nfc-F");
    }
    // Default routing for IsoDep
    nfaStat = NFA_EeSetDefaultProtoRouting(mDefaultEe, 0, 0, 0, 0, 0, 0);
    if (nfaStat == NFA_STATUS_OK)
      mRoutingEvent.wait();
    else
      LOG(ERROR) << StringPrintf(
          "Fail to set default proto routing for IsoDep");
  } else {
    // Default routing for Nfc-A technology if we don't have a SE
    if ((mSeTechMask & NFA_TECHNOLOGY_MASK_A) == 0) {
      nfaStat = NFA_EeSetDefaultTechRouting(mDefaultEe, 0, 0, 0, 0, 0, 0);
      if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait();
      else
        LOG(ERROR) << StringPrintf(
            "Fail to set default tech routing for Nfc-A");
    }
    // Default routing for IsoDep protocol
    nfaStat = NFA_EeSetDefaultProtoRouting(mDefaultEe, 0, 0, 0, 0, 0, 0);
    if (nfaStat == NFA_STATUS_OK)
      mRoutingEvent.wait();
    else
      LOG(ERROR) << StringPrintf(
          "Fail to set default proto routing for IsoDep");

    // Default routing for Nfc-F technology if we don't have a SE
    if ((mSeTechMask & NFA_TECHNOLOGY_MASK_F) == 0) {
      nfaStat =
          NFA_EeSetDefaultTechRouting(mDefaultFelicaRoute, 0, 0, 0, 0, 0, 0);
      if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait();
      else
        LOG(ERROR) << StringPrintf(
            "Fail to set default tech routing for Nfc-F");
    }
    // Default routing for T3T protocol
    if (!mIsScbrSupported) {
      nfaStat = NFA_EeSetDefaultProtoRouting(NFC_DH_ID, 0, 0, 0, 0, 0, 0);
      if (nfaStat == NFA_STATUS_OK)
        mRoutingEvent.wait();
      else
        LOG(ERROR) << StringPrintf("Fail to set default proto routing for T3T");
    }
  }
}

#if(NXP_EXTNS == TRUE)
bool RoutingManager::addAidRouting(const uint8_t* aid, uint8_t aidLen,
                                   int route, int aidInfo, int power) {
#else
bool RoutingManager::addAidRouting(const uint8_t* aid, uint8_t aidLen,
                                   int route, int aidInfo) {
#endif
  static const char fn[] = "RoutingManager::addAidRouting";
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter", fn);
  #if(NXP_EXTNS == TRUE)
  int seId = SecureElement::getInstance().getEseHandleFromGenericId(route);
  if (seId  == NFA_HANDLE_INVALID)
  {
    return false;
  }
  SyncEventGuard guard(mAidAddRemoveEvent);
  tNFA_STATUS nfaStat =
      NFA_EeAddAidRouting(seId, aidLen, (uint8_t*)aid, power, aidInfo);
  #else
  tNFA_STATUS nfaStat =
      NFA_EeAddAidRouting(route, aidLen, (uint8_t*)aid, 0x01, aidInfo);
   #endif
  if (nfaStat == NFA_STATUS_OK) {
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: routed AID", fn);
#if(NXP_EXTNS == TRUE)
    mAidAddRemoveEvent.wait();
#endif
    return true;
  } else {
    LOG(ERROR) << StringPrintf("%s: failed to route AID", fn);
    return false;
  }
}


bool RoutingManager::removeAidRouting(const uint8_t* aid, uint8_t aidLen) {
  static const char fn[] = "RoutingManager::removeAidRouting";
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter", fn);
#if(NXP_EXTNS == TRUE)
  SyncEventGuard guard(mAidAddRemoveEvent);
#endif
  tNFA_STATUS nfaStat = NFA_EeRemoveAidRouting(aidLen, (uint8_t*)aid);
  if (nfaStat == NFA_STATUS_OK) {
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: removed AID", fn);
#if(NXP_EXTNS == TRUE)
    mAidAddRemoveEvent.wait();
#endif
    return true;
  } else {
    LOG(ERROR) << StringPrintf("%s: failed to remove AID", fn);
    return false;
  }
}

bool RoutingManager::commitRouting() {
  static const char fn[] = "RoutingManager::commitRouting";
  tNFA_STATUS nfaStat = 0;
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s", fn);
  {
    SyncEventGuard guard(mEeUpdateEvent);
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat == NFA_STATUS_OK) {
      mEeUpdateEvent.wait();  // wait for NFA_EE_UPDATED_EVT
    }
  }
  return (nfaStat == NFA_STATUS_OK);
}

void RoutingManager::onNfccShutdown() {
  static const char fn[] = "RoutingManager:onNfccShutdown";
  if (mDefaultOffHostRoute == 0x00) return;

  tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
  uint8_t actualNumEe = MAX_NUM_EE;
  tNFA_EE_INFO eeInfo[MAX_NUM_EE];

  memset(&eeInfo, 0, sizeof(eeInfo));
  if ((nfaStat = NFA_EeGetInfo(&actualNumEe, eeInfo)) != NFA_STATUS_OK) {
    LOG(ERROR) << StringPrintf("%s: fail get info; error=0x%X", fn, nfaStat);
    return;
  }
  if (actualNumEe != 0) {
    for (uint8_t xx = 0; xx < actualNumEe; xx++) {
      if ((eeInfo[xx].num_interface != 0) &&
          (eeInfo[xx].ee_interface[0] != NCI_NFCEE_INTERFACE_HCI_ACCESS) &&
          (eeInfo[xx].ee_status == NFA_EE_STATUS_ACTIVE)) {
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
            "%s: Handle: 0x%04x Change Status Active to Inactive", fn,
            eeInfo[xx].ee_handle);
        SyncEventGuard guard(mEeSetModeEvent);
        if ((nfaStat = NFA_EeModeSet(eeInfo[xx].ee_handle,
                                     NFA_EE_MD_DEACTIVATE)) == NFA_STATUS_OK) {
          mEeSetModeEvent.wait();  // wait for NFA_EE_MODE_SET_EVT
        } else {
          LOG(ERROR) << StringPrintf("Failed to set EE inactive");
        }
      }
    }
  } else {
    DLOG_IF(INFO, nfc_debug_enabled)
        << StringPrintf("%s: No active EEs found", fn);
  }
}

void RoutingManager::notifyActivated(uint8_t technology) {
  JNIEnv* e = NULL;
  ScopedAttach attach(mNativeData->vm, &e);
  if (e == NULL) {
    LOG(ERROR) << StringPrintf("jni env is null");
    return;
  }

  e->CallVoidMethod(mNativeData->manager,
                    android::gCachedNfcManagerNotifyHostEmuActivated,
                    (int)technology);
  if (e->ExceptionCheck()) {
    e->ExceptionClear();
    LOG(ERROR) << StringPrintf("fail notify");
  }
}

void RoutingManager::notifyDeactivated(uint8_t technology) {
  mRxDataBuffer.clear();
  JNIEnv* e = NULL;
  ScopedAttach attach(mNativeData->vm, &e);
  if (e == NULL) {
    LOG(ERROR) << StringPrintf("jni env is null");
    return;
  }

  e->CallVoidMethod(mNativeData->manager,
                    android::gCachedNfcManagerNotifyHostEmuDeactivated,
                    (int)technology);
  if (e->ExceptionCheck()) {
    e->ExceptionClear();
    LOG(ERROR) << StringPrintf("fail notify");
  }
}

void RoutingManager::handleData(uint8_t technology, const uint8_t* data,
                                uint32_t dataLen, tNFA_STATUS status) {
  if (status == NFC_STATUS_CONTINUE) {
    if (dataLen > 0) {
      mRxDataBuffer.insert(mRxDataBuffer.end(), &data[0],
                           &data[dataLen]);  // append data; more to come
    }
    return;  // expect another NFA_CE_DATA_EVT to come
  } else if (status == NFA_STATUS_OK) {
    if (dataLen > 0) {
      mRxDataBuffer.insert(mRxDataBuffer.end(), &data[0],
                           &data[dataLen]);  // append data
    }
    // entire data packet has been received; no more NFA_CE_DATA_EVT
  } else if (status == NFA_STATUS_FAILED) {
    LOG(ERROR) << StringPrintf("RoutingManager::handleData: read data fail");
    goto TheEnd;
  }

  {
    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL) {
      LOG(ERROR) << StringPrintf("jni env is null");
      goto TheEnd;
    }

    ScopedLocalRef<jobject> dataJavaArray(
        e, e->NewByteArray(mRxDataBuffer.size()));
    if (dataJavaArray.get() == NULL) {
      LOG(ERROR) << StringPrintf("fail allocate array");
      goto TheEnd;
    }

    e->SetByteArrayRegion((jbyteArray)dataJavaArray.get(), 0,
                          mRxDataBuffer.size(), (jbyte*)(&mRxDataBuffer[0]));
    if (e->ExceptionCheck()) {
      e->ExceptionClear();
      LOG(ERROR) << StringPrintf("fail fill array");
      goto TheEnd;
    }

    e->CallVoidMethod(mNativeData->manager,
                      android::gCachedNfcManagerNotifyHostEmuData,
                      (int)technology, dataJavaArray.get());
    if (e->ExceptionCheck()) {
      e->ExceptionClear();
      LOG(ERROR) << StringPrintf("fail notify");
    }
  }
TheEnd:
  mRxDataBuffer.clear();
}

void RoutingManager::stackCallback(uint8_t event,
                                   tNFA_CONN_EVT_DATA* eventData) {
  static const char fn[] = "RoutingManager::stackCallback";
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: event=0x%X", fn, event);
  RoutingManager& routingManager = RoutingManager::getInstance();

  switch (event) {
    case NFA_CE_REGISTERED_EVT: {
      tNFA_CE_REGISTERED& ce_registered = eventData->ce_registered;
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: NFA_CE_REGISTERED_EVT; status=0x%X; h=0x%X", fn,
                          ce_registered.status, ce_registered.handle);
    } break;

    case NFA_CE_DEREGISTERED_EVT: {
      tNFA_CE_DEREGISTERED& ce_deregistered = eventData->ce_deregistered;
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_CE_DEREGISTERED_EVT; h=0x%X", fn, ce_deregistered.handle);
    } break;

    case NFA_CE_ACTIVATED_EVT: {
      routingManager.notifyActivated(NFA_TECHNOLOGY_MASK_A);
    } break;

    case NFA_DEACTIVATED_EVT:
    case NFA_CE_DEACTIVATED_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_DEACTIVATED_EVT, NFA_CE_DEACTIVATED_EVT", fn);
      routingManager.notifyDeactivated(NFA_TECHNOLOGY_MASK_A);
      SyncEventGuard g(gDeactivatedEvent);
      gActivated = false;  // guard this variable from multi-threaded access
      gDeactivatedEvent.notifyOne();
    } break;

    case NFA_CE_DATA_EVT: {
      tNFA_CE_DATA& ce_data = eventData->ce_data;
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: NFA_CE_DATA_EVT; stat=0x%X; h=0x%X; data len=%u",
                          fn, ce_data.status, ce_data.handle, ce_data.len);
      getInstance().handleData(NFA_TECHNOLOGY_MASK_A, ce_data.p_data,
                               ce_data.len, ce_data.status);
    } break;
  }
}

/*******************************************************************************
**
** Function:        nfaEeCallback
**
** Description:     Receive execution environment-related events from stack.
**                  event: Event code.
**                  eventData: Event data.
**
** Returns:         None
**
*******************************************************************************/
void RoutingManager::nfaEeCallback(tNFA_EE_EVT event,
                                   tNFA_EE_CBACK_DATA* eventData) {
  static const char fn[] = "RoutingManager::nfaEeCallback";

  RoutingManager& routingManager = RoutingManager::getInstance();
  if (eventData) routingManager.mCbEventData = *eventData;
#if (NXP_EXTNS == TRUE)
  SecureElement& se = SecureElement::getInstance();
  tNFA_EE_DISCOVER_REQ info = eventData->discover_req;
#endif

  switch (event) {
    case NFA_EE_REGISTER_EVT: {
      SyncEventGuard guard(routingManager.mEeRegisterEvent);
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_REGISTER_EVT; status=%u", fn, eventData->ee_register);
      routingManager.mEeRegisterEvent.notifyOne();
    } break;

    case NFA_EE_MODE_SET_EVT: {
      SyncEventGuard guard(routingManager.mEeSetModeEvent);
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_MODE_SET_EVT; status: 0x%04X  handle: 0x%04X  ", fn,
          eventData->mode_set.status, eventData->mode_set.ee_handle);
      routingManager.mEeSetModeEvent.notifyOne();
#if (NXP_EXTNS == TRUE)
      se.mModeSetNtfstatus = eventData->mode_set.status;
      se.notifyModeSet(eventData->mode_set.ee_handle, !(eventData->mode_set.status),eventData->mode_set.ee_status );
#endif
    } break;
#if (NXP_EXTNS == TRUE)
    case NFA_EE_PWR_LINK_CTRL_EVT:
    {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: NFA_EE_PWR_LINK_CTRL_EVT; status: 0x%04X ", fn,
      eventData->pwr_lnk_ctrl.status);
      se.mPwrCmdstatus = eventData->pwr_lnk_ctrl.status;
      SyncEventGuard guard (se.mPwrLinkCtrlEvent);
      se.mPwrLinkCtrlEvent.notifyOne();
    }
    break;
#endif
    case NFA_EE_SET_TECH_CFG_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_SET_TECH_CFG_EVT; status=0x%X", fn, eventData->status);
      SyncEventGuard guard(routingManager.mRoutingEvent);
      routingManager.mRoutingEvent.notifyOne();
    } break;

    case NFA_EE_SET_PROTO_CFG_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_SET_PROTO_CFG_EVT; status=0x%X", fn, eventData->status);
      SyncEventGuard guard(routingManager.mRoutingEvent);
      routingManager.mRoutingEvent.notifyOne();
    } break;

    case NFA_EE_ACTION_EVT: {
      tNFA_EE_ACTION& action = eventData->action;
      if (action.trigger == NFC_EE_TRIG_SELECT)
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
            "%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=select (0x%X)", fn,
            action.ee_handle, action.trigger);
      else if (action.trigger == NFC_EE_TRIG_APP_INIT) {
        tNFC_APP_INIT& app_init = action.param.app_init;
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
            "%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=app-init "
            "(0x%X); aid len=%u; data len=%u",
            fn, action.ee_handle, action.trigger, app_init.len_aid,
            app_init.len_data);
      } else if (action.trigger == NFC_EE_TRIG_RF_PROTOCOL)
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
            "%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=rf protocol (0x%X)", fn,
            action.ee_handle, action.trigger);
      else if (action.trigger == NFC_EE_TRIG_RF_TECHNOLOGY)
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
            "%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=rf tech (0x%X)", fn,
            action.ee_handle, action.trigger);
      else
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
            "%s: NFA_EE_ACTION_EVT; h=0x%X; unknown trigger (0x%X)", fn,
            action.ee_handle, action.trigger);
    } break;

    case NFA_EE_DISCOVER_REQ_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_DISCOVER_REQ_EVT; status=0x%X; num ee=%u", __func__,
          eventData->discover_req.status, eventData->discover_req.num_ee);
      SyncEventGuard guard(routingManager.mEeInfoEvent);
      memcpy(&routingManager.mEeInfo, &eventData->discover_req,
             sizeof(routingManager.mEeInfo));
      routingManager.mReceivedEeInfo = true;
      routingManager.mEeInfoEvent.notifyOne();
#if (NXP_EXTNS == TRUE)
      if(nfcFL.nfcNxpEse && nfcFL.eseFL._ESE_ETSI_READER_ENABLE) {
          MposManager::getInstance().hanldeEtsiReaderReqEvent(&info);
      }
#endif
    } break;

    case NFA_EE_NO_CB_ERR_EVT:
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_NO_CB_ERR_EVT  status=%u", fn, eventData->status);
      break;

    case NFA_EE_ADD_AID_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_ADD_AID_EVT  status=%u", fn, eventData->status);
    #if(NXP_EXTNS == TRUE)
        SyncEventGuard guard(routingManager.mAidAddRemoveEvent);
        routingManager.mAidAddRemoveEvent.notifyOne();
    #endif
    } break;

    case NFA_EE_ADD_SYSCODE_EVT: {
      SyncEventGuard guard(routingManager.mRoutingEvent);
      routingManager.mRoutingEvent.notifyOne();
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_ADD_SYSCODE_EVT  status=%u", fn, eventData->status);
    } break;

    case NFA_EE_REMOVE_SYSCODE_EVT: {
      SyncEventGuard guard(routingManager.mRoutingEvent);
      routingManager.mRoutingEvent.notifyOne();
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_REMOVE_SYSCODE_EVT  status=%u", fn, eventData->status);
    } break;

    case NFA_EE_REMOVE_AID_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_REMOVE_AID_EVT  status=%u", fn, eventData->status);
    #if(NXP_EXTNS == TRUE)
        SyncEventGuard guard(routingManager.mAidAddRemoveEvent);
        routingManager.mAidAddRemoveEvent.notifyOne();
    #endif
    } break;

    case NFA_EE_NEW_EE_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: NFA_EE_NEW_EE_EVT  h=0x%X; status=%u", fn,
          eventData->new_ee.ee_handle, eventData->new_ee.ee_status);
    } break;

    case NFA_EE_UPDATED_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: NFA_EE_UPDATED_EVT", fn);
      SyncEventGuard guard(routingManager.mEeUpdateEvent);
      routingManager.mEeUpdateEvent.notifyOne();
    } break;
#if(NXP_EXTNS == TRUE)
    case NFA_EE_ADD_APDU_EVT:
    {
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: NFA_EE_ADD_APDU_EVT  status=%u", fn, eventData->status);
      SyncEventGuard guard(se.mApduPaternAddRemoveEvent);
      se.mApduPaternAddRemoveEvent.notifyOne();
    }
    break;
    case NFA_EE_REMOVE_APDU_EVT:
    {
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: NFA_EE_REMOVE_APDU_EVT  status=%u", fn, eventData->status);
      SyncEventGuard guard(se.mApduPaternAddRemoveEvent);
      se.mApduPaternAddRemoveEvent.notifyOne();
    }
    break;
#endif
    default:
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: unknown event=%u ????", fn, event);
      break;
  }
}

int RoutingManager::registerT3tIdentifier(uint8_t* t3tId, uint8_t t3tIdLen) {
  static const char fn[] = "RoutingManager::registerT3tIdentifier";

  DLOG_IF(INFO, nfc_debug_enabled)
      << StringPrintf("%s: Start to register NFC-F system on DH", fn);

  if (t3tIdLen != (2 + NCI_RF_F_UID_LEN + NCI_T3T_PMM_LEN)) {
    LOG(ERROR) << StringPrintf("%s: Invalid length of T3T Identifier", fn);
    return NFA_HANDLE_INVALID;
  }

  mNfcFOnDhHandle = NFA_HANDLE_INVALID;

  uint16_t systemCode;
  uint8_t nfcid2[NCI_RF_F_UID_LEN];
  uint8_t t3tPmm[NCI_T3T_PMM_LEN];

  systemCode = (((int)t3tId[0] << 8) | ((int)t3tId[1] << 0));
  memcpy(nfcid2, t3tId + 2, NCI_RF_F_UID_LEN);
  memcpy(t3tPmm, t3tId + 10, NCI_T3T_PMM_LEN);
  {
    SyncEventGuard guard(mRoutingEvent);
    tNFA_STATUS nfaStat = NFA_CeRegisterFelicaSystemCodeOnDH(
        systemCode, nfcid2, t3tPmm, nfcFCeCallback);
    if (nfaStat == NFA_STATUS_OK) {
      mRoutingEvent.wait();
    } else {
      LOG(ERROR) << StringPrintf("%s: Fail to register NFC-F system on DH", fn);
      return NFA_HANDLE_INVALID;
    }
  }
  DLOG_IF(INFO, nfc_debug_enabled)
      << StringPrintf("%s: Succeed to register NFC-F system on DH", fn);

  // Register System Code for routing
  if (mIsScbrSupported) {
    SyncEventGuard guard(mRoutingEvent);
    tNFA_STATUS nfaStat = NFA_EeAddSystemCodeRouting(systemCode, NCI_DH_ID,
                                                     SYS_CODE_PWR_STATE_HOST);
    if (nfaStat == NFA_STATUS_OK) {
      mRoutingEvent.wait();
    }
    if ((nfaStat != NFA_STATUS_OK) || (mCbEventData.status != NFA_STATUS_OK)) {
      LOG(ERROR) << StringPrintf("%s: Fail to register system code on DH", fn);
      return NFA_HANDLE_INVALID;
    }
    DLOG_IF(INFO, nfc_debug_enabled)
        << StringPrintf("%s: Succeed to register system code on DH", fn);
    // add handle and system code pair to the map
    mMapScbrHandle.emplace(mNfcFOnDhHandle, systemCode);
  } else {
    LOG(ERROR) << StringPrintf("%s: SCBR Not supported", fn);
  }

  return mNfcFOnDhHandle;
}

void RoutingManager::deregisterT3tIdentifier(int handle) {
  static const char fn[] = "RoutingManager::deregisterT3tIdentifier";

  DLOG_IF(INFO, nfc_debug_enabled)
      << StringPrintf("%s: Start to deregister NFC-F system on DH", fn);
  {
    SyncEventGuard guard(mRoutingEvent);
    tNFA_STATUS nfaStat = NFA_CeDeregisterFelicaSystemCodeOnDH(handle);
    if (nfaStat == NFA_STATUS_OK) {
      mRoutingEvent.wait();
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
          "%s: Succeeded in deregistering NFC-F system on DH", fn);
    } else {
      LOG(ERROR) << StringPrintf("%s: Fail to deregister NFC-F system on DH",
                                 fn);
    }
  }
  if (mIsScbrSupported) {
    map<int, uint16_t>::iterator it = mMapScbrHandle.find(handle);
    // find system code for given handle
    if (it != mMapScbrHandle.end()) {
      uint16_t systemCode = it->second;
      mMapScbrHandle.erase(handle);
      if (systemCode != 0) {
        SyncEventGuard guard(mRoutingEvent);
        tNFA_STATUS nfaStat = NFA_EeRemoveSystemCodeRouting(systemCode);
        if (nfaStat == NFA_STATUS_OK) {
          mRoutingEvent.wait();
          DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
              "%s: Succeeded in deregistering system Code on DH", fn);
        } else {
          LOG(ERROR) << StringPrintf("%s: Fail to deregister system Code on DH",
                                     fn);
        }
      }
    }
  }
}

void RoutingManager::nfcFCeCallback(uint8_t event,
                                    tNFA_CONN_EVT_DATA* eventData) {
  static const char fn[] = "RoutingManager::nfcFCeCallback";
  RoutingManager& routingManager = RoutingManager::getInstance();

  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: 0x%x", __func__, event);

  switch (event) {
    case NFA_CE_REGISTERED_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: registerd event notified", fn);
      routingManager.mNfcFOnDhHandle = eventData->ce_registered.handle;
      SyncEventGuard guard(routingManager.mRoutingEvent);
      routingManager.mRoutingEvent.notifyOne();
    } break;
    case NFA_CE_DEREGISTERED_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: deregisterd event notified", fn);
      SyncEventGuard guard(routingManager.mRoutingEvent);
      routingManager.mRoutingEvent.notifyOne();
    } break;
    case NFA_CE_ACTIVATED_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: activated event notified", fn);
      routingManager.notifyActivated(NFA_TECHNOLOGY_MASK_F);
    } break;
    case NFA_CE_DEACTIVATED_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: deactivated event notified", fn);
      routingManager.notifyDeactivated(NFA_TECHNOLOGY_MASK_F);
    } break;
    case NFA_CE_DATA_EVT: {
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: data event notified", fn);
      tNFA_CE_DATA& ce_data = eventData->ce_data;
      routingManager.handleData(NFA_TECHNOLOGY_MASK_F, ce_data.p_data,
                                ce_data.len, ce_data.status);
    } break;
    default: {
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s: unknown event=%u ????", fn, event);
    } break;
  }
}

int RoutingManager::registerJniFunctions(JNIEnv* e) {
  static const char fn[] = "RoutingManager::registerJniFunctions";
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s", fn);
  return jniRegisterNativeMethods(
      e, "com/android/nfc/cardemulation/AidRoutingManager", sMethods,
      NELEM(sMethods));
}

int RoutingManager::com_android_nfc_cardemulation_doGetDefaultRouteDestination(
    JNIEnv*) {
  return getInstance().mDefaultEe;
}

int RoutingManager::
    com_android_nfc_cardemulation_doGetDefaultOffHostRouteDestination(JNIEnv*) {
  return getInstance().mDefaultOffHostRoute;
}

int RoutingManager::com_android_nfc_cardemulation_doGetAidMatchingMode(
    JNIEnv*) {
  return getInstance().mAidMatchingMode;
}

#if(NXP_EXTNS == TRUE)
/*******************************************************************************
 **
 ** Function:        getUiccRoute
 **
 ** Description:     returns EE Id corresponding to slot number
 **
 ** Returns:         route location
 **
 *******************************************************************************/
static jint getUiccRoute(jint uicc_slot)
{
    LOG(ERROR) << StringPrintf("%s: Enter slot num = %d", __func__,uicc_slot);
    if((uicc_slot == 0x00) || (uicc_slot == 0x01))
    {
        return SecureElement::getInstance().EE_HANDLE_0xF4;
    }
    else if(uicc_slot == 0x02)
    {
        return (RoutingManager::getInstance().getUicc2selected());
    }
    else
    {
        return 0xFF;
    }
}

void RoutingManager::registerProtoRouteEnrty(tNFA_HANDLE     ee_handle,
                                         tNFA_PROTOCOL_MASK  protocols_switch_on,
                                         tNFA_PROTOCOL_MASK  protocols_switch_off,
                                         tNFA_PROTOCOL_MASK  protocols_battery_off,
                                         tNFA_PROTOCOL_MASK  protocols_screen_lock,
                                         tNFA_PROTOCOL_MASK  protocols_screen_off,
                                         tNFA_PROTOCOL_MASK  protocols_screen_off_lock
                                         )
{
    static const char fn [] = "RoutingManager::registerProtoRouteEnrty";
    bool new_entry = true;
    uint8_t i = 0;
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;

    if(gRouteInfo.num_entries == 0)
    {
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter, first entry :%x", fn, ee_handle);
        gRouteInfo.protoInfo[0].ee_handle = ee_handle;
        gRouteInfo.protoInfo[0].protocols_switch_on = protocols_switch_on;
        gRouteInfo.protoInfo[0].protocols_switch_off = protocols_switch_off;
        gRouteInfo.protoInfo[0].protocols_battery_off = protocols_battery_off;
        gRouteInfo.protoInfo[0].protocols_screen_lock = protocols_screen_lock;
        gRouteInfo.protoInfo[0].protocols_screen_off = protocols_screen_off;
        gRouteInfo.protoInfo[0].protocols_screen_off_lock = protocols_screen_off_lock;
        gRouteInfo.num_entries = 1;
    }
    else
    {
        for (i = 0;i < gRouteInfo.num_entries; i++)
        {
            if(gRouteInfo.protoInfo[i].ee_handle == ee_handle)
            {
                DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter, proto handle match found :%x", fn, ee_handle);
                gRouteInfo.protoInfo[i].protocols_switch_on |= protocols_switch_on;
                gRouteInfo.protoInfo[i].protocols_switch_off |= protocols_switch_off;
                gRouteInfo.protoInfo[i].protocols_battery_off |= protocols_battery_off;
                gRouteInfo.protoInfo[i].protocols_screen_lock |= protocols_screen_lock;
                gRouteInfo.protoInfo[i].protocols_screen_off |= protocols_screen_off;
                gRouteInfo.protoInfo[i].protocols_screen_off_lock |= protocols_screen_off_lock;
                new_entry = false;
                break;
            }
        }
        if(new_entry)
        {
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter,new proto handle entry :%x", fn, ee_handle);
            i = gRouteInfo.num_entries;
            gRouteInfo.protoInfo[i].ee_handle = ee_handle;
            gRouteInfo.protoInfo[i].protocols_switch_on = protocols_switch_on;
            gRouteInfo.protoInfo[i].protocols_switch_off = protocols_switch_off;
            gRouteInfo.protoInfo[i].protocols_battery_off = protocols_battery_off;
            gRouteInfo.protoInfo[i].protocols_screen_lock = protocols_screen_lock;
            gRouteInfo.protoInfo[i].protocols_screen_off = protocols_screen_off;
            gRouteInfo.protoInfo[i].protocols_screen_off_lock = protocols_screen_off_lock;
            gRouteInfo.num_entries++;
        }
    }
    for (i = 0;i < gRouteInfo.num_entries; i++)
    {
        {
            SyncEventGuard guard (mRoutingEvent);
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter >>>> ee_handle:%x %x %x %x %x %x %x", fn, gRouteInfo.protoInfo[i].ee_handle,
                                                                    gRouteInfo.protoInfo[i].protocols_switch_on,
                                                                    gRouteInfo.protoInfo[i].protocols_switch_off,
                                                                    gRouteInfo.protoInfo[i].protocols_battery_off,
                                                                    gRouteInfo.protoInfo[i].protocols_screen_lock,
                                                                    gRouteInfo.protoInfo[i].protocols_screen_off,
                                                                    gRouteInfo.protoInfo[i].protocols_screen_off_lock);



            nfaStat = NFA_EeSetDefaultProtoRouting (gRouteInfo.protoInfo[i].ee_handle,
                                                    gRouteInfo.protoInfo[i].protocols_switch_on,
                                                    gRouteInfo.protoInfo[i].protocols_switch_off,
                                                    gRouteInfo.protoInfo[i].protocols_battery_off,
                                                    gRouteInfo.protoInfo[i].protocols_screen_lock,
                                                    gRouteInfo.protoInfo[i].protocols_screen_off,
                                                    gRouteInfo.protoInfo[i].protocols_screen_off_lock);
            if(nfaStat == NFA_STATUS_OK){
                mRoutingEvent.wait ();
                DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("tech routing SUCCESS");
            }
            else{
                DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default tech routing");
            }
        }
    }
}
/*******************************************************************************
 **
 ** Function:        configureEeRegister
 **
 ** Description:     EE register & de-register can be done.
 **
 ** Returns:         None
 **
 *******************************************************************************/
void RoutingManager::configureEeRegister(bool eeReg)
{
    static const char fn [] = "RoutingManager::configureEeRegister";
    tNFA_STATUS nfaStat;
    if(eeReg)
    {
        SyncEventGuard guard (mEeRegisterEvent);
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: try ee register", fn);
        nfaStat = NFA_EeRegister (nfaEeCallback);
        if (nfaStat != NFA_STATUS_OK)
        {
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("%s: fail ee register; error=0x%X", fn, nfaStat);
        }
        mEeRegisterEvent.wait ();
    } else {
         NFA_EeDeregister (nfaEeCallback);
    }
}

void RoutingManager::configureOffHostNfceeTechMask(void)
{
    //static const char fn []           = "RoutingManager::configureOffHostNfceeTechMask";
    tNFA_STATUS       nfaStat         = NFA_STATUS_FAILED;
    uint8_t           seId            = 0x00;
    uint8_t           count           = 0x00;
    tNFA_HANDLE       preferredHandle = SecureElement::getInstance().EE_HANDLE_0xF4;
    tNFA_HANDLE       defaultHandle   = NFA_HANDLE_INVALID;
    tNFA_HANDLE       ee_handleList[nfcFL.nfccFL._NFA_EE_MAX_EE_SUPPORTED];

    //ALOGV("%s: enter", fn);

    if (mDefaultEe & SecureElement::ESE_ID) //eSE
    {
        preferredHandle = ROUTE_LOC_ESE_ID;
    }
    else if (mDefaultEe & SecureElement::UICC_ID) //UICC
    {
        preferredHandle = SecureElement::getInstance().EE_HANDLE_0xF4;
    }
    else if (isDynamicUiccEnabled &&
            (mDefaultEe & SecureElement::UICC2_ID)) //UICC
    {
        preferredHandle = getUicc2selected();
    }

    SecureElement::getInstance().getEeHandleList(ee_handleList, &count);

    for (uint8_t i = 0; ((count != 0 ) && (i < count)); i++)
    {
        seId = SecureElement::getInstance().getGenericEseId(ee_handleList[i]);
        defaultHandle = SecureElement::getInstance().getEseHandleFromGenericId(seId);
        //ALOGV("%s: ee_handleList[%d] : 0x%X", fn, i,ee_handleList[i]);
        if (preferredHandle == defaultHandle)
        {
            break;
        }
        defaultHandle   = NFA_HANDLE_INVALID;
    }

    if((defaultHandle != NFA_HANDLE_INVALID)  &&  (0 != mUiccListnTechMask))
    {
        {
            //SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            nfaStat = NFA_CeConfigureUiccListenTech (defaultHandle, 0x00);
            if (nfaStat == NFA_STATUS_OK)
            {
                 //SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                 LOG(ERROR) << StringPrintf("fail to start UICC listen");
        }
        {
            //SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            nfaStat = NFA_CeConfigureUiccListenTech (defaultHandle, (mUiccListnTechMask & 0x07));
            if(nfaStat == NFA_STATUS_OK)
            {
                 //SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                 LOG(ERROR) << StringPrintf("fail to start UICC listen");
        }
    }

    //ALOGV("%s: exit", fn);
}

bool RoutingManager::setRoutingEntry(int type, int value, int route, int power)
{
    static const char fn [] = "RoutingManager::setRoutingEntry";
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter, >>>>>>>> type:0x%x value =0x%x route:%x power:0x%x", fn, type, value ,route, power);
    unsigned long max_tech_mask = 0x03;
    unsigned long uiccListenTech = 0;

    if (!isDynamicUiccEnabled) {
       if(nfcManager_getUiccRoute(sCurrentSelectedUICCSlot)!=0xFF) {
           max_tech_mask = SecureElement::getInstance().getSETechnology(nfcManager_getUiccRoute(sCurrentSelectedUICCSlot));
       } else {
            max_tech_mask = SecureElement::getInstance().getSETechnology(SecureElement::getInstance().EE_HANDLE_0xF4);
       }
    } else {
        max_tech_mask = SecureElement::getInstance().getSETechnology(SecureElement::getInstance().EE_HANDLE_0xF4);
    }

    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter,max_tech_mask :%lx", fn, max_tech_mask);

    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    tNFA_HANDLE ee_handle = NFA_HANDLE_INVALID;
    uint8_t switch_on_mask = 0x00;
    uint8_t switch_off_mask   = 0x00;
    uint8_t battery_off_mask = 0x00;
    uint8_t screen_lock_mask = 0x00;
    uint8_t screen_off_mask = 0x00;
    uint8_t screen_off_lock_mask = 0x00;
    uint8_t protocol_mask = 0x00;

    ee_handle = ((route == 0x00) ? ROUTE_LOC_HOST_ID : ((route == 0x01) ? ROUTE_LOC_ESE_ID : getUiccRouteLocId(route)));
    if(ee_handle == NFA_HANDLE_INVALID )
    {
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter, handle:%x invalid", fn, ee_handle);
        return nfaStat;
    }

    tNFA_HANDLE ActDevHandle = NFA_HANDLE_INVALID;
    uint8_t count,seId=0;
    uint8_t isSeIDPresent = 0;
    tNFA_HANDLE ee_handleList[SecureElement::MAX_NUM_EE];
    SecureElement::getInstance().getEeHandleList(ee_handleList, &count);


    for (int  i = 0; ((count != 0 ) && (i < count)); i++)
    {
        seId = SecureElement::getInstance().getGenericEseId(ee_handleList[i]);
        ActDevHandle = SecureElement::getInstance().getEseHandleFromGenericId(seId);
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter, ee_handleList[%d]:%x", fn, i,ee_handleList[i]);
        if ((ee_handle != 0x400) &&
            (ee_handle == ActDevHandle))
        {
            isSeIDPresent =1;
            break;
        }
    }

    if((ee_handle == ROUTE_LOC_HOST_ID) && (NFA_SET_PROTOCOL_ROUTING == type))
    {
      power &= (PWR_SWTCH_ON_SCRN_LOCK_MASK | PWR_SWTCH_ON_SCRN_UNLCK_MASK);
      isSeIDPresent = 1;
    }

    max_tech_mask = SecureElement::getInstance().getSETechnology(ee_handle);
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter,max_tech_mask :%lx", fn, max_tech_mask);
    if(NFA_SET_TECHNOLOGY_ROUTING == type)
    {
        /*  Masking with available SE Technologies */
        value &=  max_tech_mask;
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter >>>> max_tech_mask :%lx value :0x%x", fn, max_tech_mask, value);
        switch_on_mask    = (power & 0x01) ? value : 0;
        switch_off_mask   = (power & 0x02) ? value : 0;
        battery_off_mask  = (power & 0x04) ? value : 0;
        screen_off_mask   = (power & 0x10) ? value : 0;
        screen_lock_mask  = (power & 0x08) ? value : 0;
        screen_off_lock_mask = (power & 0x20) ? value : 0;

        if((max_tech_mask != 0x01) && (max_tech_mask == 0x02)) // type B only
        {
            switch_on_mask    &= ~NFA_TECHNOLOGY_MASK_A;
            switch_off_mask   &= ~NFA_TECHNOLOGY_MASK_A;
            battery_off_mask  &= ~NFA_TECHNOLOGY_MASK_A;
            screen_off_mask   &= ~NFA_TECHNOLOGY_MASK_A;
            screen_lock_mask  &= ~NFA_TECHNOLOGY_MASK_A;
            screen_off_lock_mask &= ~NFA_TECHNOLOGY_MASK_A;
        }
        else if((max_tech_mask == 0x01) && (max_tech_mask != 0x02)) // type A only
        {
            switch_on_mask    &= ~NFA_TECHNOLOGY_MASK_B;
            switch_off_mask   &= ~NFA_TECHNOLOGY_MASK_B;
            battery_off_mask  &= ~NFA_TECHNOLOGY_MASK_B;
            screen_off_mask   &= ~NFA_TECHNOLOGY_MASK_B;
            screen_lock_mask  &= ~NFA_TECHNOLOGY_MASK_B;
            screen_off_lock_mask  &= ~NFA_TECHNOLOGY_MASK_B;
        }

        if((mHostListnTechMask) && (mFwdFuntnEnable == true))
        {
            if((max_tech_mask != 0x01) && (max_tech_mask == 0x02))
            {
                {
                    SyncEventGuard guard (mRoutingEvent);
                    if(mCeRouteStrictDisable == 0x01)
                    {
                        nfaStat =  NFA_EeSetDefaultTechRouting (0x400,
                                                                NFA_TECHNOLOGY_MASK_A,
                                                                0,
                                                                0,
                                                                NFA_TECHNOLOGY_MASK_A,
                                                                0,
                                                                0 );
                    }else{
                        nfaStat =  NFA_EeSetDefaultTechRouting (0x400,
                                                                NFA_TECHNOLOGY_MASK_A,
                                                                0, 0, 0, 0,0 );
                    }
                    if (nfaStat == NFA_STATUS_OK)
                       mRoutingEvent.wait ();
                    else
                    {
                        DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set tech routing");
                    }
                }
            }
            else if((max_tech_mask == 0x01) && (max_tech_mask != 0x02))
            {
                {
                    SyncEventGuard guard (mRoutingEvent);
                    if(mCeRouteStrictDisable == 0x01)
                    {
                        nfaStat =  NFA_EeSetDefaultTechRouting (0x400,
                                                               NFA_TECHNOLOGY_MASK_B,
                                                               0,
                                                               0,
                                                               NFA_TECHNOLOGY_MASK_B,
                                                               0,
                                                               0);
                    }else{
                        nfaStat =  NFA_EeSetDefaultTechRouting (0x400,
                                                               NFA_TECHNOLOGY_MASK_B,
                                                               0, 0, 0, 0, 0);
                    }
                    if (nfaStat == NFA_STATUS_OK)
                       mRoutingEvent.wait ();
                    else
                    {
                        DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set tech routing");
                    }
                }
            }
        }
        {
            SyncEventGuard guard (mRoutingEvent);
            nfaStat = NFA_EeSetDefaultTechRouting (ee_handle, switch_on_mask, switch_off_mask, battery_off_mask, screen_lock_mask,
                screen_off_mask, screen_off_lock_mask);
            if(nfaStat == NFA_STATUS_OK){
                mRoutingEvent.wait ();
                DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("tech routing SUCCESS");
            }
            else{
                DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default tech routing");
            }
        }
    }else if(NFA_SET_PROTOCOL_ROUTING == type && isSeIDPresent == 1)
    {
        value &= ~(0xF0);
        while(value)
        {
            if( value & 0x01)
            {
                protocol_mask = NFA_PROTOCOL_MASK_ISO_DEP;
                value &= ~(0x01);
            }
            else if( value & 0x02)
            {
                protocol_mask = NFA_PROTOCOL_MASK_NFC_DEP;
                value &= ~(0x02);
            }
            else if( value & 0x04)
            {
                protocol_mask = NFA_PROTOCOL_MASK_T3T;
                value &= ~(0x04);
            }
            else if( value & 0x08)
            {
                protocol_mask = NFC_PROTOCOL_MASK_ISO7816;
                value &= ~(0x08);
            }

            if(protocol_mask)
            {
                switch_on_mask     = (power & 0x01) ? protocol_mask : 0;
                switch_off_mask    = (power & 0x02) ? protocol_mask : 0;
                battery_off_mask   = (power & 0x04) ? protocol_mask : 0;
                screen_lock_mask   = (power & 0x10) ? protocol_mask : 0;
                screen_off_mask    = (power & 0x08) ? protocol_mask : 0;
                screen_off_lock_mask = (power & 0x20) ? protocol_mask : 0;
                DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter >>>> ee_handle:%x %x %x %x %x %x %x", fn, ee_handle,
                                                                    switch_on_mask,
                                                                    switch_off_mask,
                                                                    battery_off_mask,
                                                                    screen_lock_mask,
                                                                    screen_off_mask,
                                                                    screen_off_lock_mask);

                registerProtoRouteEnrty(ee_handle, switch_on_mask, switch_off_mask,
                    battery_off_mask, screen_lock_mask, screen_off_mask, screen_off_lock_mask);
                protocol_mask = 0;
            }
        }
    }

    uiccListenTech = NfcConfig::getUnsigned(NAME_UICC_LISTEN_TECH_MASK, 0x07);
    if((ActDevHandle != NFA_HANDLE_INVALID)  &&  (0 != uiccListenTech))
    {
         {
               //SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
               nfaStat = NFA_CeConfigureUiccListenTech (ActDevHandle, 0x00);
               if (nfaStat == NFA_STATUS_OK)
               {
                     //SecureElement::getInstance().mUiccListenEvent.wait ();
               }
               else
                     DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("fail to start UICC listen");
         }
         {
               //SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
               nfaStat = NFA_CeConfigureUiccListenTech (ActDevHandle, (uiccListenTech & 0x07));
               if(nfaStat == NFA_STATUS_OK)
               {
                     //SecureElement::getInstance().mUiccListenEvent.wait ();
               }
               else
                     DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("fail to start UICC listen");
         }
    }
    return nfaStat;
}

bool RoutingManager::clearAidTable ()
{
    static const char fn [] = "RoutingManager::clearAidTable";
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter", fn);
    SyncEventGuard guard(RoutingManager::getInstance().mAidAddRemoveEvent);
    tNFA_STATUS nfaStat = NFA_EeRemoveAidRouting(NFA_REMOVE_ALL_AID_LEN, (uint8_t*) NFA_REMOVE_ALL_AID);
    if (nfaStat == NFA_STATUS_OK)
    {
        RoutingManager::getInstance().mAidAddRemoveEvent.wait();
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: removed AID", fn);
        return true;
    } else
    {
        DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("%s: failed to remove AID", fn);
        return false;
    }
}

bool RoutingManager::clearRoutingEntry(int type)
{
    static const char fn [] = "RoutingManager::clearRoutingEntry";
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter, type:0x%x", fn, type );
    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    //tNFA_HANDLE ee_handle = NFA_HANDLE_INVLAID;

    memset(&gRouteInfo, 0x00, sizeof(RouteInfo_t));


    if(NFA_SET_TECHNOLOGY_ROUTING & type)
    {
      {
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultTechRouting (0x400, 0x00, 0x00, 0x00, 0x00, 0x00,0x00);
        if(nfaStat == NFA_STATUS_OK){
            mRoutingEvent.wait ();
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("tech routing SUCCESS");
        }
        else{
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default tech routing");
        }
      }

      {
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultTechRouting (SecureElement::getInstance().EE_HANDLE_0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
        if(nfaStat == NFA_STATUS_OK){
            mRoutingEvent.wait ();
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("tech routing SUCCESS");
        }
        else{
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default tech routing");
        }
      }

      {
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultTechRouting (0x4C0, 0x00, 0x00, 0x00, 0x00, 0x00,0x00);
        if(nfaStat == NFA_STATUS_OK){
            mRoutingEvent.wait ();
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("tech routing SUCCESS");
        }
        else{
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default tech routing");
        }
      }

      {
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultTechRouting (0x481, 0x00, 0x00, 0x00, 0x00, 0x00,0x00);
        if(nfaStat == NFA_STATUS_OK){
            mRoutingEvent.wait ();
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("tech routing SUCCESS");
        }
        else{
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default tech routing");
        }
      }
    }
    if(NFA_SET_PROTOCOL_ROUTING & type)
    {
      {
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultProtoRouting (0x400, 0x00, 0x00, 0x00, 0x00 ,0x00,0x00);
        if(nfaStat == NFA_STATUS_OK){
            mRoutingEvent.wait ();
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("protocol routing SUCCESS");
        }
        else{
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default protocol routing");
        }
      }

      {
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultProtoRouting (SecureElement::getInstance().EE_HANDLE_0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
        if(nfaStat == NFA_STATUS_OK){
            mRoutingEvent.wait ();
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("protocol routing SUCCESS");
        }
        else{
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default protocol routing");
        }
      }

      {
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultProtoRouting (0x4C0, 0x00, 0x00, 0x00, 0x00, 0x00,0x00);
        if(nfaStat == NFA_STATUS_OK){
            mRoutingEvent.wait ();
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("protocol routing SUCCESS");
        }
        else{
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default protocol routing");
        }
      }

      {
        SyncEventGuard guard (mRoutingEvent);
        nfaStat = NFA_EeSetDefaultProtoRouting (0x481, 0x00, 0x00, 0x00, 0x00, 0x00,0x00);
        if(nfaStat == NFA_STATUS_OK){
            mRoutingEvent.wait ();
            DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("protocol routing SUCCESS");
        }
        else{
            DLOG_IF(ERROR, nfc_debug_enabled) << StringPrintf("Fail to set default protocol routing");
        }
      }
    }

    if (NFA_SET_AID_ROUTING & type)
    {
        clearAidTable();
    }
    return nfaStat;
}

/*
 * In NCI2.0 Protocol 7816 routing is replaced with empty AID
 * Routing entry Format :
 *  Type   = [0x12]
 *  Length = 2 [0x02]
 *  Value  = [Route_loc, Power_state]
 * */
void RoutingManager::setEmptyAidEntry(int route) {

    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: enter",__func__);
    uint16_t routeLoc = NFA_HANDLE_INVALID;
    uint8_t power;
    uint8_t count,seId=0;
    uint8_t isDefaultAidRoutePresent = 0;
    unsigned long     check_default_proto_se_id_req = 0;
    tNFA_HANDLE ActDevHandle = NFA_HANDLE_INVALID;
    static const char fn []   = "RoutingManager::checkProtoSeID";

    tNFA_HANDLE ee_handleList[nfcFL.nfccFL._NFA_EE_MAX_EE_SUPPORTED];
    mDefaultIso7816SeID = route;
    if (mDefaultIso7816SeID  == NFA_HANDLE_INVALID)
    {
        LOG(ERROR) << StringPrintf("%s: Invalid routeLoc. Return.", __func__);
        return;
    }
    routeLoc = ((mDefaultIso7816SeID == 0x00) ? ROUTE_LOC_HOST_ID : ((mDefaultIso7816SeID == 0x01 ) ? ROUTE_LOC_ESE_ID : getUiccRouteLocId(mDefaultIso7816SeID)));
    power    = mCeRouteStrictDisable ? mDefaultIso7816Powerstate : (mDefaultIso7816Powerstate & POWER_STATE_MASK);
    if (GetNxpNumValue(NAME_CHECK_DEFAULT_PROTO_SE_ID, &check_default_proto_se_id_req, sizeof(check_default_proto_se_id_req)))
    {
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: CHECK_DEFAULT_PROTO_SE_ID - 0x%2lX  routeLoc = 0x%x",fn,check_default_proto_se_id_req, routeLoc);
    }
    else
    {
      LOG(ERROR) << StringPrintf("%s: CHECK_DEFAULT_PROTO_SE_ID not defined. Taking default value - 0x%2lX",fn,check_default_proto_se_id_req);
    }
    if(check_default_proto_se_id_req == 0x01)
    {
      SecureElement::getInstance().getEeHandleList(ee_handleList, &count);
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: count : %d", fn, count);
      for (int  i = 0; ((count != 0 ) && (i < count)); i++)
      {
        seId = SecureElement::getInstance().getGenericEseId(ee_handleList[i]);
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: seId : %d", fn, seId);
        ActDevHandle = SecureElement::getInstance().getEseHandleFromGenericId(seId);
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: ActDevHandle : 0x%X", fn, ActDevHandle);
        if (routeLoc == ActDevHandle)
        {
          isDefaultAidRoutePresent = 1;
          DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("Default AID route handle active");
        }
        if(isDefaultAidRoutePresent)
        {
          break;
        }
      }
      if(!isDefaultAidRoutePresent)
      {
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("Default AidRoute not present");
        mDefaultIso7816SeID = ROUTE_LOC_HOST_ID;
        routeLoc = mDefaultIso7816SeID;
        if(NFA_GetNCIVersion() != NCI_VERSION_2_0)
        {
          mDefaultIso7816Powerstate &= (PWR_SWTCH_ON_SCRN_UNLCK_MASK | PWR_SWTCH_ON_SCRN_LOCK_MASK);
          power = mDefaultIso7816Powerstate;
        }
      }
    }
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: route %x",__func__,routeLoc);
    if(routeLoc == ROUTE_LOC_HOST_ID) {
      power &= 0x11;
    }
    if(mDefaultGsmaPowerState) {
      if(routeLoc == ROUTE_LOC_HOST_ID)
        power = (mDefaultGsmaPowerState & 0x39);
      else
        power = mDefaultGsmaPowerState;
      DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: gsma  %x",__func__,power);
    }

    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: power %x",__func__,power);
    if(power){
        tNFA_STATUS nfaStat = NFA_EeAddAidRouting(routeLoc, 0, NULL, power, 0x10);
        DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: Status :0x%2x", __func__, nfaStat);
    }else{
        LOG(ERROR) << StringPrintf("%s:Invalid Power State" ,__func__);
    }
}

/* Forward Functionality is to handle either technology which is supported by UICC
 * We are handling it by setting the alternate technology(A/B) to HOST
 * */
void RoutingManager::processTechEntriesForFwdfunctionality(void)
{
    //static const char fn []    = "RoutingManager::processTechEntriesForFwdfunctionality";
    uint32_t techSupportedByUICC = mTechSupportedByUicc1;
    if(!isDynamicUiccEnabled) {
        techSupportedByUICC = (getUiccRoute(sCurrentSelectedUICCSlot) == SecureElement::getInstance().EE_HANDLE_0xF4)?
                mTechSupportedByUicc1 : mTechSupportedByUicc2;
    }
    else {
        techSupportedByUICC = (mDefaultTechASeID == SecureElement::getInstance().EE_HANDLE_0xF4)?
                mTechSupportedByUicc1:mTechSupportedByUicc2;
    }
    //ALOGV("%s: enter", fn);

    switch(mHostListnTechMask)
    {
    case 0x01://Host wants to listen ISO-DEP in A tech only then following cases will arises:-
        //i.Tech A only UICC present(Dont route Tech B to HOST),
        //ii.Tech B only UICC present(Route Tech A to HOST),
        //iii.Tech AB UICC present(Dont route any tech to HOST)
        if(((mTechTableEntries[TECH_B_IDX].routeLoc == SecureElement::getInstance().EE_HANDLE_0xF4) || (mTechTableEntries[TECH_B_IDX].routeLoc == getUicc2selected())) &&
                ((((techSupportedByUICC & NFA_TECHNOLOGY_MASK_B) == 0) && (techSupportedByUICC & NFA_TECHNOLOGY_MASK_A) != 0)))//Tech A only supported UICC
        {
            //Tech A will goto UICC according to previous table
            //Disable Tech B entry as host wants to listen A only
            mTechTableEntries[TECH_B_IDX].enable   = false;
        }
        if(((mTechTableEntries[TECH_A_IDX].routeLoc == SecureElement::getInstance().EE_HANDLE_0xF4) || (mTechTableEntries[TECH_A_IDX].routeLoc == getUicc2selected())) &&
                ((((techSupportedByUICC & NFA_TECHNOLOGY_MASK_A) == 0) && (techSupportedByUICC & NFA_TECHNOLOGY_MASK_B) != 0)))//Tech B only supported UICC
        {
            //Tech B will goto UICC according to previous table
            //Route Tech A to HOST as Host wants to listen A only
            mTechTableEntries[TECH_A_IDX].routeLoc = ROUTE_LOC_HOST_ID;
            /*Allow only (screen On+unlock) and (screen On+lock) power state when routing to HOST*/
            mTechTableEntries[TECH_A_IDX].power    = (mTechTableEntries[TECH_A_IDX].power & HOST_SCREEN_STATE_MASK);
            mTechTableEntries[TECH_A_IDX].enable   = true;
        }
        if((techSupportedByUICC & 0x03) == 0x03)//AB both supported UICC
        {
            //Do Nothing
            //Tech A and Tech B will goto according to previous table
            //HCE A only / HCE-B only functionality wont work in this case
        }
        break;
    case 0x02://Host wants to listen ISO-DEP in B tech only then if Cases: Tech A only UICC present(Route Tech B to HOST), Tech B only UICC present(Dont route Tech A to HOST), Tech AB UICC present(Dont route any tech to HOST)
        if(((mTechTableEntries[TECH_B_IDX].routeLoc == SecureElement::getInstance().EE_HANDLE_0xF4) || (mTechTableEntries[TECH_B_IDX].routeLoc == getUicc2selected())) &&
                ((((techSupportedByUICC & NFA_TECHNOLOGY_MASK_B) == 0) && (techSupportedByUICC & NFA_TECHNOLOGY_MASK_A) != 0)))//Tech A only supported UICC
        {
            //Tech A will goto UICC according to previous table
            //Route Tech B to HOST as host wants to listen B only
            mTechTableEntries[TECH_B_IDX].routeLoc = ROUTE_LOC_HOST_ID;
            /*Allow only (screen On+unlock) and (screen On+lock) power state when routing to HOST*/
            mTechTableEntries[TECH_B_IDX].power    = (mTechTableEntries[TECH_A_IDX].power & HOST_SCREEN_STATE_MASK);
            mTechTableEntries[TECH_B_IDX].enable   = true;
        }
        if(((mTechTableEntries[TECH_A_IDX].routeLoc == SecureElement::getInstance().EE_HANDLE_0xF4) || (mTechTableEntries[TECH_A_IDX].routeLoc == getUicc2selected())) &&
                ((((techSupportedByUICC & NFA_TECHNOLOGY_MASK_A) == 0) && (techSupportedByUICC & NFA_TECHNOLOGY_MASK_B) != 0)))//Tech B only supported UICC
        {
            //Tech B will goto UICC according to previous table
            //Disable Tech A to HOST as host wants to listen B only
            mTechTableEntries[TECH_A_IDX].enable   = false;
        }
        if((techSupportedByUICC & 0x03) == 0x03)//AB both supported UICC
        {
            //Do Nothing
            //Tech A and Tech B will goto UICC
            //HCE A only / HCE-B only functionality wont work in this case
        }
        break;
    case 0x03:
    case 0x07://Host wants to listen ISO-DEP in AB both tech then if Cases: Tech A only UICC present(Route Tech B to HOST), Tech B only UICC present(Route Tech A to HOST), Tech AB UICC present(Dont route any tech to HOST)
        /*If selected EE is UICC and it supports Bonly , then Set Tech A to Host */
        /*Host doesn't support Tech Routing, To enable FWD functionality enabling tech route to Host.*/
        if(((mTechTableEntries[TECH_A_IDX].routeLoc == SecureElement::getInstance().EE_HANDLE_0xF4) || (mTechTableEntries[TECH_A_IDX].routeLoc == getUicc2selected())) &&
                ((((techSupportedByUICC & NFA_TECHNOLOGY_MASK_A) == 0) && (techSupportedByUICC & NFA_TECHNOLOGY_MASK_B) != 0)))
        {
            mTechTableEntries[TECH_A_IDX].routeLoc = ROUTE_LOC_HOST_ID;
            /*Allow only (screen On+unlock) and (screen On+lock) power state when routing to HOST*/
            mTechTableEntries[TECH_A_IDX].power    = (mTechTableEntries[TECH_A_IDX].power & HOST_SCREEN_STATE_MASK);
            mTechTableEntries[TECH_A_IDX].enable   = true;
        }
        /*If selected EE is UICC and it supports Aonly , then Set Tech B to Host*/
        if(((mTechTableEntries[TECH_B_IDX].routeLoc == SecureElement::getInstance().EE_HANDLE_0xF4) || (mTechTableEntries[TECH_B_IDX].routeLoc == getUicc2selected())) &&
                ((((techSupportedByUICC & NFA_TECHNOLOGY_MASK_B) == 0) && (techSupportedByUICC & NFA_TECHNOLOGY_MASK_A) != 0)))
        {
            mTechTableEntries[TECH_B_IDX].routeLoc = ROUTE_LOC_HOST_ID;
            /*Allow only (screen On+unlock) and (screen On+lock) power state when routing to HOST*/
            mTechTableEntries[TECH_B_IDX].power    = (mTechTableEntries[TECH_A_IDX].power & HOST_SCREEN_STATE_MASK);
            mTechTableEntries[TECH_B_IDX].enable   = true;
        }
        if((techSupportedByUICC & 0x03) == 0x03)//AB both supported UICC
        {
            //Do Nothing
            //Tech A and Tech B will goto UICC
            //HCE A only / HCE-B only functionality wont work in this case
        }
        break;
    }
    dumpTables(3);
    //ALOGV("%s: exit", fn);
}

void RoutingManager::dumpTables(int xx)
{


    switch(xx)
    {
    case 1://print only proto table
        LOG(ERROR) << StringPrintf("--------------------Proto Table Entries------------------" );
        for(int xx=0;xx<MAX_PROTO_ENTRIES;xx++)
        {
            LOG(ERROR) << StringPrintf("|Index=%d|RouteLoc=0x%03X|Proto=0x%02X|Power=0x%02X|Enable=0x%01X|",
                    xx,mProtoTableEntries[xx].routeLoc,
                    mProtoTableEntries[xx].protocol,
                    mProtoTableEntries[xx].power,
                    mProtoTableEntries[xx].enable);
        }
        //ALOGV("---------------------------------------------------------" );
        break;
    case 2://print Lmrt proto table
        LOG(ERROR) << StringPrintf("----------------------------------------Lmrt Proto Entries------------------------------------" );
        for(int xx=0;xx<MAX_ROUTE_LOC_ENTRIES;xx++)
        {
            LOG(ERROR) << StringPrintf("|Index=%d|nfceeID=0x%03X|SWTCH-ON=0x%02X|SWTCH-OFF=0x%02X|BAT-OFF=0x%02X|SCRN-LOCK=0x%02X|SCRN-OFF=0x%02X|SCRN-OFF_LOCK=0x%02X",
                    xx,
                    mLmrtEntries[xx].nfceeID,
                    mLmrtEntries[xx].proto_switch_on,
                    mLmrtEntries[xx].proto_switch_off,
                    mLmrtEntries[xx].proto_battery_off,
                    mLmrtEntries[xx].proto_screen_lock,
                    mLmrtEntries[xx].proto_screen_off,
                    mLmrtEntries[xx].proto_screen_off_lock);
        }
        //ALOGV("----------------------------------------------------------------------------------------------" );
        break;
    case 3://print only tech table
        LOG(ERROR) << StringPrintf("--------------------Tech Table Entries------------------" );
        for(int xx=0;xx<MAX_TECH_ENTRIES;xx++)
        {
            LOG(ERROR) << StringPrintf("|Index=%d|RouteLoc=0x%03X|Tech=0x%02X|Power=0x%02X|Enable=0x%01X|",
                   xx,
                    mTechTableEntries[xx].routeLoc,
                    mTechTableEntries[xx].technology,
                    mTechTableEntries[xx].power,
                    mTechTableEntries[xx].enable);
        }
        //ALOGV("--------------------------------------------------------" );
        break;
    case 4://print Lmrt tech table
        LOG(ERROR) << StringPrintf("-----------------------------------------Lmrt Tech Entries------------------------------------" );
        for(int xx=0;xx<MAX_TECH_ENTRIES;xx++)
        {
            LOG(ERROR) << StringPrintf("|Index=%d|nfceeID=0x%03X|SWTCH-ON=0x%02X|SWTCH-OFF=0x%02X|BAT-OFF=0x%02X|SCRN-LOCK=0x%02X|SCRN-OFF=0x%02X|SCRN-OFF_LOCK=0x%02X",
                xx,
                mLmrtEntries[xx].nfceeID,
                mLmrtEntries[xx].tech_switch_on,
                mLmrtEntries[xx].tech_switch_off,
                mLmrtEntries[xx].tech_battery_off,
                mLmrtEntries[xx].tech_screen_lock,
                mLmrtEntries[xx].tech_screen_off,
                mLmrtEntries[xx].tech_screen_off_lock);
        }
        //ALOGV("----------------------------------------------------------------------------------------------" );
        break;
    }
}
/* Based on the features enabled :- nfcFL.nfccFL._NFCC_DYNAMIC_DUAL_UICC, nfcFL.nfccFL._NFC_NXP_STAT_DUAL_UICC_WO_EXT_SWITCH & NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH,
 * Calculate the UICC route location ID.
 * For DynamicDualUicc,Route location is based on the user configuration(6th & 7th bit) of route
 * For StaticDualUicc without External Switch(with DynamicDualUicc enabled), Route location is based on user selection from selectUicc() API
 * For StaticDualUicc(With External Switch), Route location is always ROUTE_LOC_UICC1_ID
 */
uint16_t RoutingManager::getUiccRouteLocId(const int route)
{
	LOG(ERROR) << StringPrintf(" getUiccRouteLocId route %X",
                   route);
    if((route != 0x02 ) &&(route != 0x03))
      return NFA_HANDLE_INVALID;

    if(!isDynamicUiccEnabled)
        return getUiccRoute(sCurrentSelectedUICCSlot);
    else if(isDynamicUiccEnabled)
        return ((route == 0x02 ) ? SecureElement::getInstance().EE_HANDLE_0xF4 : getUicc2selected());
    else /*#if (NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == true)*/
        return SecureElement::getInstance().EE_HANDLE_0xF4;
}
/*******************************************************************************
 **
 ** Function:        getUicc2selected
 **
 ** Description:     returns UICC2 selected in config file
 **
 ** Returns:         route location
 **
 *******************************************************************************/
uint32_t RoutingManager:: getUicc2selected()
{
	LOG(ERROR) << StringPrintf(" getUicc2selected route");
    return (SecureElement::getInstance().muicc2_selected == SecureElement::UICC2_ID)?
                 SecureElement::getInstance().EE_HANDLE_0xF8:
                    SecureElement::getInstance().EE_HANDLE_0xF9;
}

bool RoutingManager::addApduRouting(uint8_t route, uint8_t powerState,const uint8_t* apduData,
     uint8_t apduDataLen ,const uint8_t* apduMask, uint8_t apduMaskLen)
{
    static const char fn [] = "RoutingManager::addApduRouting";
    LOG(ERROR) << StringPrintf("%s: enter, route:%x power:0x%x", fn, route, powerState);

    int seId = SecureElement::getInstance().getEseHandleFromGenericId(route);
    if (seId  == NFA_HANDLE_INVALID)
    {
      return false;
    }
    SyncEventGuard guard(SecureElement::getInstance().mApduPaternAddRemoveEvent);
    tNFA_STATUS nfaStat = NFA_EeAddApduPatternRouting(apduDataLen, (uint8_t*)apduData, apduMaskLen, (uint8_t*)apduMask, seId, powerState);
    if (nfaStat == NFA_STATUS_OK)
    {

        SecureElement::getInstance().mApduPaternAddRemoveEvent.wait();

        LOG(ERROR) << StringPrintf("%s: routed APDU pattern successfully", fn);
    }
    return ((nfaStat == NFA_STATUS_OK)?true:false);
}

bool RoutingManager::removeApduRouting(uint8_t apduDataLen, const uint8_t* apduData)
{
    static const char fn [] = "RoutingManager::removeApduRouting";
    LOG(ERROR) << StringPrintf("%s: enter", fn);
    SyncEventGuard guard(SecureElement::getInstance().mApduPaternAddRemoveEvent);
    tNFA_STATUS nfaStat = NFA_EeRemoveApduPatternRouting(apduDataLen, (uint8_t*) apduData);
    if (nfaStat == NFA_STATUS_OK)
    {
        SecureElement::getInstance().mApduPaternAddRemoveEvent.wait();
        LOG(ERROR) << StringPrintf("%s: removed APDU pattern successfully", fn);
    }
    return ((nfaStat == NFA_STATUS_OK)?true:false);
}
#endif
