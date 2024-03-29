/*
 * Copyright (C) 2012 The Android Open Source Project
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
/******************************************************************************
 *
 *  The original Work has been changed by NXP Semiconductors.
 *
 *  Copyright (C) 2015-2018 NXP Semiconductors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
#include <android-base/stringprintf.h>
#include <base/logging.h>
#include <errno.h>
#include <semaphore.h>
#include "JavaClassConstants.h"
#include "NfcAdaptation.h"
#include "NfcJniUtil.h"
#include "RoutingManager.h"
#include "SyncEvent.h"
#include "config.h"

#include "nfa_api.h"
#include "nfa_rw_api.h"

#if (NXP_EXTNS == TRUE)
using android::base::StringPrintf;

extern bool nfc_debug_enabled;

typedef struct nxp_feature_data {
  SyncEvent NxpFeatureConfigEvt;
  Mutex mMutex;
  tNFA_STATUS wstatus;
  uint8_t rsp_data[255];
  uint8_t rsp_len;
} Nxp_Feature_Data_t;

namespace android {
static Nxp_Feature_Data_t gnxpfeature_conf;
void SetCbStatus(tNFA_STATUS status);
tNFA_STATUS GetCbStatus(void);
static void NxpResponse_Cb(uint8_t event, uint16_t param_len, uint8_t* p_param);
}  // namespace android

namespace android {

void SetCbStatus(tNFA_STATUS status) { gnxpfeature_conf.wstatus = status; }

tNFA_STATUS GetCbStatus(void) { return gnxpfeature_conf.wstatus; }

void NxpPropCmd_OnResponseCallback(uint8_t event, uint16_t param_len,
                                   uint8_t *p_param) {
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
    "NxpPropCmd_OnResponseCallback: Received length data = 0x%x status = "
        "0x%x", param_len, p_param[3]);
  uint8_t oid = p_param[1];
  uint8_t status = NFA_STATUS_FAILED;

  switch (oid) {
  case (0x1A):
  /*FALL_THRU*/
  case (0x1C):
    status = p_param[3];
    break;
  case (0x1B):
    status = p_param[param_len - 1];
    break;
  default:
    LOG(ERROR) << StringPrintf("Propreitary Rsp: OID is not supported");
    break;
  }

  android::SetCbStatus(status);

  android::gnxpfeature_conf.rsp_len = (uint8_t)param_len;
  memcpy(android::gnxpfeature_conf.rsp_data, p_param, param_len);
  SyncEventGuard guard(android::gnxpfeature_conf.NxpFeatureConfigEvt);
  android::gnxpfeature_conf.NxpFeatureConfigEvt.notifyOne();
}

tNFA_STATUS NxpPropCmd_send(uint8_t *pData4Tx, uint8_t dataLen,
                            uint8_t *rsp_len, uint8_t *rsp_buf,
                            uint32_t rspTimeout, tHAL_NFC_ENTRY *halMgr) {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  bool retVal = false;

  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: prop cmd being txed", __func__);

  gnxpfeature_conf.mMutex.lock();

  android::SetCbStatus(NFA_STATUS_FAILED);
  SyncEventGuard guard(android::gnxpfeature_conf.NxpFeatureConfigEvt);

  status =
      NFA_SendRawVsCommand(dataLen, pData4Tx, NxpPropCmd_OnResponseCallback);
  if (status == NFA_STATUS_OK) {
    DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s: Success NFA_SendNxpNciCommand", __func__);

    retVal = android::gnxpfeature_conf.NxpFeatureConfigEvt.wait(
        rspTimeout); /* wait for callback */
    if (retVal == false) {
      android::SetCbStatus(NFA_STATUS_TIMEOUT);
      android::gnxpfeature_conf.rsp_len = 0;
      memset(android::gnxpfeature_conf.rsp_data, 0,
             sizeof(android::gnxpfeature_conf.rsp_data));
    }
  } else {
    LOG(ERROR) << StringPrintf("%s: Failed NFA_SendNxpNciCommand", __func__);
  }
  status = android::GetCbStatus();
  if ((android::gnxpfeature_conf.rsp_len > 3) && (rsp_buf != NULL)) {
    *rsp_len = android::gnxpfeature_conf.rsp_len - 3;
    memcpy(rsp_buf, android::gnxpfeature_conf.rsp_data + 3,
           android::gnxpfeature_conf.rsp_len - 3);
  }
  android::gnxpfeature_conf.mMutex.unlock();
  return status;
}

static void NxpResponse_Cb(uint8_t event, uint16_t param_len,
                           uint8_t* p_param) {
  (void)event;
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(
      "NxpResponse_Cb Received length data = 0x%x status = 0x%x", param_len,
      p_param[3]);

  if (p_param[3] == 0x00) {
    SetCbStatus(NFA_STATUS_OK);
  } else {
    SetCbStatus(NFA_STATUS_FAILED);
  }
  gnxpfeature_conf.rsp_len = (uint8_t)param_len;
  if (param_len > 0 && p_param != NULL) {
    memcpy(gnxpfeature_conf.rsp_data, p_param, param_len);
  }
  SyncEventGuard guard(gnxpfeature_conf.NxpFeatureConfigEvt);
  gnxpfeature_conf.NxpFeatureConfigEvt.notifyOne();
}


/*******************************************************************************
 **
 ** Function:        NxpNfc_Write_Cmd()
 **
 ** Description:     Writes the command to NFCC
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS NxpNfc_Write_Cmd_Common(uint8_t retlen, uint8_t* buffer) {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  SetCbStatus(NFA_STATUS_FAILED);
  SyncEventGuard guard(gnxpfeature_conf.NxpFeatureConfigEvt);
  status = NFA_SendRawVsCommand(retlen, buffer, NxpResponse_Cb);
  if (status == NFA_STATUS_OK) {
    DLOG_IF(INFO, nfc_debug_enabled)
        << StringPrintf("%s: Success NFA_SendRawVsCommand", __func__);
    gnxpfeature_conf.NxpFeatureConfigEvt.wait(); /* wait for callback */
  } else {
    LOG(ERROR) << StringPrintf("%s: Failed NFA_SendRawVsCommand", __func__);
  }
  status = GetCbStatus();
  return status;
}


} /*namespace android*/

#endif