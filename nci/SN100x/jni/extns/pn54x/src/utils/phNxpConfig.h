/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef _NXP_CONFIG_H
#define _NXP_CONFIG_H

int GetNxpStrValue(const char* name, char* p_value, unsigned long len);
int GetNxpNumValue(const char* name, void* p_value, unsigned long len);
int GetNxpByteArrayValue(const char* name, char* pValue, long bufflen,
                         long* len);
void resetNxpConfig(void);
int isNxpConfigModified();

#define NAME_NXPLOG_EXTNS_LOGLEVEL "NXPLOG_EXTNS_LOGLEVEL"
#define NAME_NXPLOG_NCIHAL_LOGLEVEL "NXPLOG_NCIHAL_LOGLEVEL"
#define NAME_NXPLOG_NCIX_LOGLEVEL "NXPLOG_NCIX_LOGLEVEL"
#define NAME_NXPLOG_NCIR_LOGLEVEL "NXPLOG_NCIR_LOGLEVEL"
#define NAME_NXPLOG_FWDNLD_LOGLEVEL "NXPLOG_FWDNLD_LOGLEVEL"
#define NAME_NXPLOG_TML_LOGLEVEL "NXPLOG_TML_LOGLEVEL"

#define NAME_MIFARE_READER_ENABLE "MIFARE_READER_ENABLE"
#define NAME_FW_STORAGE "FW_STORAGE"
#define NAME_NXP_ACT_PROP_EXTN "NXP_ACT_PROP_EXTN"
#define NAME_NXP_RF_CONF_BLK_1 "NXP_RF_CONF_BLK_1"
#define NAME_NXP_RF_CONF_BLK_2 "NXP_RF_CONF_BLK_2"
#define NAME_NXP_RF_CONF_BLK_3 "NXP_RF_CONF_BLK_3"
#define NAME_NXP_RF_CONF_BLK_4 "NXP_RF_CONF_BLK_4"
#define NAME_NXP_CORE_CONF_EXTN "NXP_CORE_CONF_EXTN"
#define NAME_NXP_CORE_CONF "NXP_CORE_CONF"
#define NAME_NXP_CORE_MFCKEY_SETTING "NXP_CORE_MFCKEY_SETTING"
#define NAME_NXP_CORE_STANDBY "NXP_CORE_STANDBY"
#define NAME_NXP_DEFAULT_SE "NXP_DEFAULT_SE"
#define NAME_NXP_NFC_CHIP "NXP_NFC_CHIP"
#define NAME_NXP_SWP_RD_START_TIMEOUT "NXP_SWP_RD_START_TIMEOUT"
#define NAME_NXP_SWP_RD_TAG_OP_TIMEOUT "NXP_SWP_RD_TAG_OP_TIMEOUT"
#define NAME_NXP_DEFAULT_NFCEE_TIMEOUT "NXP_DEFAULT_NFCEE_TIMEOUT"
#define NAME_NXP_DEFAULT_NFCEE_DISC_TIMEOUT "NXP_DEFAULT_NFCEE_DISC_TIMEOUT"
#define NAME_NXP_CE_ROUTE_STRICT_DISABLE "NXP_CE_ROUTE_STRICT_DISABLE"
#define NAME_NXP_P61_LS_DEFAULT_INTERFACE "NXP_P61_LS_DEFAULT_INTERFACE"
#define NAME_NXP_P61_JCOP_DEFAULT_INTERFACE "NXP_P61_JCOP_DEFAULT_INTERFACE"
#define NAME_NXP_JCOPDL_AT_BOOT_ENABLE "NXP_JCOPDL_AT_BOOT_ENABLE"
#define NAME_NXP_P61_LTSM_DEFAULT_INTERFACE "NXP_P61_LTSM_DEFAULT_INTERFACE"
#define NAME_NXP_LOADER_SERICE_VERSION "NXP_LOADER_SERVICE_VERSION"
/* default configuration */
#define default_storage_location "/data/nfc"
#if(NXP_EXTNS == TRUE)
#define NAME_NFA_CONFIG_FORMAT              "NFA_CONFIG_FORMAT"
#define NAME_DEFUALT_GSMA_PWR_STATE "DEFUALT_GSMA_PWR_STATE"
#endif

/**
 *  @brief defines the different config files used.
 */

#define config_name_mtp         "libnfc-mtp_default.conf"
#define config_name_mtp1        "libnfc-mtp_rf1.conf"
#define config_name_mtp2        "libnfc-mtp_rf2.conf"
#define config_name_mtp_NQ3XX   "libnfc-mtp-NQ3XX.conf"
#define config_name_mtp_NQ4XX   "libnfc-mtp-NQ4XX.conf"
#define config_name_mtp_SN100   "libnfc-mtp-SN100.conf"
#define config_name_qrd         "libnfc-qrd_default.conf"
#define config_name_qrd1        "libnfc-qrd_rf1.conf"
#define config_name_qrd2        "libnfc-qrd_rf2.conf"
#define config_name_qrd_NQ3XX   "libnfc-qrd-NQ3XX.conf"
#define config_name_qrd_NQ4XX   "libnfc-qrd-NQ4XX.conf"
#define config_name_qrd_SN100   "libnfc-qrd-SN100.conf"
#define config_name_default     "libnfc-nxp_default.conf"

/**
 *  @brief defines the different major number used.
 */
#define FW_MAJOR_NUM_NQ2xx     "10"
#define FW_MAJOR_NUM_NQ3xx     "11"
#define FW_MAJOR_NUM_NQ4xx     "12"

#define FW_MAJOR_NUM_LENGTH    2

/**
 *  @brief defines the maximum length of the target name.
 */

#define MAX_SOC_INFO_NAME_LEN (15)

/**
 *  @brief Defines the type of hardware platform.
 */

#define QRD_HW_PLATFORM  "qrd"
#define MTP_HW_PLATFORM  "mtp"

/**
 *  @brief Defines the path where the hardware platform details are present.
 */

#define SYSFS_HW_PLATFORM_PATH1  "/sys/devices/soc0/hw_platform"
#define SYSFS_HW_PLATFORM_PATH2   "/sys/devices/system/soc/soc0/hw_platform"

/**
 *  @brief Defines the path where the soc_id details are present.
 */

#define SYSFS_SOCID_PATH1    "/sys/devices/soc0/soc_id"
#define SYSFS_SOCID_PATH2    "/sys/devices/system/soc/soc0/id"

/**
 *  @brief Defines the maximum length of the config file name.
 */

#define MAX_DATA_CONFIG_PATH_LEN 64

/**
 *  @brief Defines the NQ chip type.
 */

#define NQ210 "0x48"
#define NQ220 "0x58"

/**
 *  @brief Defines whether debugging is enabled or disabled.
 */

#define DEBUG 0

#endif  // _NXP_CONFIG_H
