/************************************************************************************
 *
 *  Copyright (C) 2009-2012 Broadcom Corporation
 *
 *  This program is the proprietary software of Broadcom Corporation and/or its
 *  licensors, and may only be used, duplicated, modified or distributed
 *  pursuant to the terms and conditions of a separate, written license
 *  agreement executed between you and Broadcom (an "Authorized License").
 *  Except as set forth in an Authorized License, Broadcom grants no license
 *  (express or implied), right to use, or waiver of any kind with respect to
 *  the Software, and Broadcom expressly reserves all rights in and to the
 *  Software and all intellectual property rights therein.
 *  IF YOU HAVE NO AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS
 *  SOFTWARE IN ANY WAY, AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE
 *  ALL USE OF THE SOFTWARE.
 *
 *  Except as expressly set forth in the Authorized License,
 *
 *  1.     This program, including its structure, sequence and organization,
 *         constitutes the valuable trade secrets of Broadcom, and you shall
 *         use all reasonable efforts to protect the confidentiality thereof,
 *         and to use this information only in connection with your use of
 *         Broadcom integrated circuit products.
 *
 *  2.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED
 *         "AS IS" AND WITH ALL FAULTS AND BROADCOM MAKES NO PROMISES,
 *         REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY,
 *         OR OTHERWISE, WITH RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY
 *         DISCLAIMS ANY AND ALL IMPLIED WARRANTIES OF TITLE, MERCHANTABILITY,
 *         NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES,
 *         ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 *         CORRESPONDENCE TO DESCRIPTION. YOU ASSUME THE ENTIRE RISK ARISING OUT
 *         OF USE OR PERFORMANCE OF THE SOFTWARE.
 *
 *  3.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL BROADCOM OR
 *         ITS LICENSORS BE LIABLE FOR
 *         (i)   CONSEQUENTIAL, INCIDENTAL, SPECIAL, INDIRECT, OR EXEMPLARY
 *               DAMAGES WHATSOEVER ARISING OUT OF OR IN ANY WAY RELATING TO
 *               YOUR USE OF OR INABILITY TO USE THE SOFTWARE EVEN IF BROADCOM
 *               HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES; OR
 *         (ii)  ANY AMOUNT IN EXCESS OF THE AMOUNT ACTUALLY PAID FOR THE
 *               SOFTWARE ITSELF OR U.S. $1, WHICHEVER IS GREATER. THESE
 *               LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF
 *               ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.
 *
 ************************************************************************************/

/************************************************************************************
 *
 *  Filename:      btif_storage.c
 *
 *  Description:   Stores the local BT adapter and remote device properties in
 *                 NVRAM storage, typically as text files in the
 *                 mobile's filesystem
 *
 *
 *  Data storage directory structure
 *
 * data
 * `-- misc
 *   `-- bluedroid
 *       `-- LOCAL
 *           |-- adapter_info            - Local adapter config
 *           |-- remote_devices          - Remote devices and Timestamp
 *           |-- remote_devclass         - Remote devices' COD
 *           |-- remote_devtype          - Remote devices' type
 *           |-- remote_names            - Remote devices' names
 *           |-- remote_aliases          - Remote devices' Friendly names
 *           `-- remote_services         - Remote devices' services
 *
 *
 * adapter_info - Key/Value
 *     name <space> <Name of Local Bluetooth device>
 *     scan_mode <space> <Scan Mode>
 *     discovery_timeout <space> <Discovery Timeout in seconds>
 *
 * remote_devices - Key/Value
 *     <remote device bd_addr> <space> <Timestamp>
 *
 * remote_devclass - Key/Value
 *    <remote device bd_addr> <space> <Device class>
 *
 * remote_devtype - Key/Value
 *    <remote_device bd_addr><space> <Device Type>
 *
 * remote_names - Key/Value
 *    <remote_device bd_addr> <space> <Bluetooth device Name as reported by the controller>
 *
 * remote_linkkeys - Key/Value
 *     <remote device bd_addr > <space> <LinkKey> <space> <KeyType> <space> <PinLength>
 *
 * remote_aliases - Key/Value
 *     <remote device bd_addr> <space> <Friendy Name>
 *
 * remote_services - Key/Value
 *     <remote_device bd_addr> <space> <List of UUIDs separated by semicolons>
 *
 ***********************************************************************************/
#include <stdlib.h>
#include <time.h>

#include <hardware/bluetooth.h>

#define LOG_TAG "BTIF_STORAGE"

#include "btif_api.h"

#include "btif_util.h"
#include "unv.h"
#include "bd.h"

/************************************************************************************
**  Constants & Macros
************************************************************************************/

#define BTIF_STORAGE_PATH_BLUEDROID "/data/misc/bluedroid"

#define BTIF_STORAGE_PATH_ADAPTER_INFO "adapter_info"
#define BTIF_STORAGE_PATH_REMOTE_DEVICES "remote_devices"
#define BTIF_STORAGE_PATH_REMOTE_DEVCLASSES "remote_devclasses"
#define BTIF_STORAGE_PATH_REMOTE_DEVTYPES "remote_devtypes"
#define BTIF_STORAGE_PATH_REMOTE_NAMES "remote_names"
#define BTIF_STORAGE_PATH_REMOTE_LINKKEYS "remote_linkkeys"
#define BTIF_STORAGE_PATH_REMOTE_ALIASES "remote_aliases"
#define BTIF_STORAGE_PATH_REMOTE_SERVICES "remote_services"

#define BTIF_STORAGE_KEY_ADAPTER_NAME "name"
#define BTIF_STORAGE_KEY_ADAPTER_SCANMODE "scan_mode"
#define BTIF_STORAGE_KEY_ADAPTER_DISC_TIMEOUT "discovery_timeout"

/* This is a local property to add a device found */
#define BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP 0xFF

#define BTIF_STORAGE_GET_ADAPTER_PROP(t,v,l,p) \
      {p.type=t;p.val=v;p.len=l; btif_storage_get_adapter_property(&p);}

#define BTIF_STORAGE_GET_REMOTE_PROP(b,t,v,l,p) \
      {p.type=t;p.val=v;p.len=l;btif_storage_get_remote_device_property(b,&p);}

#define STORAGE_BDADDR_STRING_SZ           (18)      /* 00:11:22:33:44:55 */
#define STORAGE_UUID_STRING_SIZE           (36+1)    /* 00001200-0000-1000-8000-00805f9b34fb; */
#define STORAGE_PINLEN_STRING_MAX_SIZE     (2)       /* ascii pinlen max chars */
#define STORAGE_KEYTYPE_STRING_MAX_SIZE    (1)       /* ascii keytype max chars */

#define STORAGE_KEY_TYPE_MAX               (10)


/* <18 char bd addr> <space> LIST< <36 char uuid> <;> > <keytype (dec)> <pinlen> */
#define BTIF_REMOTE_SERVICES_ENTRY_SIZE_MAX (STORAGE_BDADDR_STRING_SZ + 1 +\
                                             STORAGE_UUID_STRING_SIZE*BT_MAX_NUM_UUIDS + \
                                             STORAGE_PINLEN_STRING_MAX_SIZE +\
                                             STORAGE_KEYTYPE_STRING_MAX_SIZE)

#define STORAGE_REMOTE_LINKKEYS_ENTRY_SIZE (LINK_KEY_LEN*2 + 1 + 2 + 1 + 2)

/* currently remote services is the potentially largest entry */
#define BTIF_STORAGE_MAX_LINE_SZ BTIF_REMOTE_SERVICES_ENTRY_SIZE_MAX

/* check against unv max entry size at compile time */
#if (BTIF_STORAGE_ENTRY_MAX_SIZE > UNV_MAXLINE_LENGTH)
#error "btif storage entry size exceeds unv max line size"
#endif

/************************************************************************************
**  Local type definitions
************************************************************************************/
typedef struct {
    uint32_t num_devices;
    bt_bdaddr_t devices[BTM_SEC_MAX_DEVICE_RECORDS];
} btif_bonded_devices_t;

/************************************************************************************
**  Extern variables
************************************************************************************/
extern UINT16 bta_service_id_to_uuid_lkup_tbl [BTA_MAX_SERVICE_ID];

/************************************************************************************
**  Static variables
************************************************************************************/

/************************************************************************************
**  Static functions
************************************************************************************/
/*******************************************************************************
**
** Function         btif_in_make_filename
**
** Description      Internal helper function to create NVRAM file path
**                  from address and filename
**
** Returns          NVRAM file path if successfull, NULL otherwise
**
*******************************************************************************/
static char* btif_in_make_filename(bt_bdaddr_t *bd_addr, char *fname)
{
   static char path[256];
   bdstr_t bdstr;

   if (fname == NULL)return NULL;
   if (bd_addr)
   {
        sprintf(path, "%s/%s/%s", BTIF_STORAGE_PATH_BLUEDROID,
                             bd2str(bd_addr, &bdstr), fname);
   }
   else
   {
        /* local adapter */
        sprintf(path, "%s/LOCAL/%s", BTIF_STORAGE_PATH_BLUEDROID, fname);
   }

   return (char*)path;
}

/*******************************************************************************
**
** Function         btif_in_adapter_key_from_type
**
** Description      Internal helper function to map a property type
**                  to the NVRAM filename key
**
** Returns          NVRAM filename key if successfull, 'NO_KEY' otherwise
**
*******************************************************************************/
static const char *btif_in_get_adapter_key_from_type(bt_property_type_t type)
{
     switch (type)
     {
        case BT_PROPERTY_BDNAME:
            return BTIF_STORAGE_KEY_ADAPTER_NAME;
        case BT_PROPERTY_ADAPTER_SCAN_MODE:
            return BTIF_STORAGE_KEY_ADAPTER_SCANMODE;
        case BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT:
            return BTIF_STORAGE_KEY_ADAPTER_DISC_TIMEOUT;
        default:
            /* return valid string to avoid passing NULL to NV RAM driver */
            return "NO_KEY";
     }
}

/*******************************************************************************
**
** Function         btif_in_split_uuids_string_to_list
**
** Description      Internal helper function to split the string of UUIDs
**                  read from the NVRAM to an array
**
** Returns          None
**
*******************************************************************************/
static void btif_in_split_uuids_string_to_list(char *str, bt_uuid_t *p_uuid,
                                                uint32_t *p_num_uuid)
{
    char buf[64];
    char *p_start = str;
    char *p_needle;
    uint32_t num = 0;
    do
    {
        p_needle = strchr(p_start, ';');
        if (!p_needle)
            break;
        memset(buf, 0, sizeof(buf));
        strncpy(buf, p_start, (p_needle-p_start));
        string_to_uuid(buf, p_uuid + num);
        num++;
        p_start = ++p_needle;

    } while (*p_start != 0);
    *p_num_uuid = num;
}

/*******************************************************************************
**
** Function         btif_in_str_to_property
**
** Description      Internal helper function to convert the string read from
**                  NVRAM into a property->val. Also sets the property->len.
**                  Assumption is that property->val has enough memory to
**                  store the string fetched from NVRAM
**
** Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
**
*******************************************************************************/
static bt_status_t btif_in_str_to_property(char *value, bt_property_t *property)
{
    bt_status_t status = BT_STATUS_SUCCESS;
    property->len = 0;

    /* if Value is NULL, then just set the property->len to 0 and return.
       This is possible if the entry does not exist  */
    if (value == NULL) {
        status = BT_STATUS_FAIL;
    }
    switch (property->type)
    {
        case BT_PROPERTY_BDNAME:
        case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
        {
            *((char*)property->val) = 0;
            if (value)
            {
                property->len = strlen(value)+1;
                strcpy((char*)property->val, value);
            }
        } break;
        case BT_PROPERTY_ADAPTER_SCAN_MODE:
        case BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT:
        {
            *((uint32_t *)property->val) = 0;
            if (value)
            {
                uint32_t ival;
                property->len = sizeof(uint32_t);
                ival = atoi(value);
                memcpy((uint32_t*)property->val, &ival, sizeof(uint32_t));
            }
        } break;
        case BT_PROPERTY_CLASS_OF_DEVICE:
        case BT_PROPERTY_TYPE_OF_DEVICE:
        {
            *((uint32_t *)property->val) = 0;
            if (value)
            {
                uint32_t ival;
                property->len = sizeof(uint32_t);
                ival = strtol(value, NULL, 16);
                memcpy((uint32_t*)property->val, &ival, sizeof(uint32_t));
            }
        } break;
        case BT_PROPERTY_UUIDS:
        {
            if (value)
            {
                bt_uuid_t *p_uuid = (bt_uuid_t*)property->val;
                uint32_t num_uuids = 0;
                btif_in_split_uuids_string_to_list(value, p_uuid, &num_uuids);
                property->len = num_uuids * sizeof(bt_uuid_t);
            }
        } break;
        default:
        {
            break;
        }
    }
    return status;
}

/*******************************************************************************
**
** Function         btif_in_property_to_str
**
** Description      Internal helper function to convert the property->val
**                  to a string that can be written to the NVRAM
**
** Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
**
*******************************************************************************/
static bt_status_t btif_in_property_to_str(bt_property_t *property, char *value)
{
    switch (property->type)
    {
        case BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP:
        {
           sprintf(value, "%d", (int)time(NULL));
        }break;
        case BT_PROPERTY_BDNAME:
        case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
        {
            strcpy(value, (char*)property->val);
        }break;
        case BT_PROPERTY_ADAPTER_SCAN_MODE:
        case BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT:
        {
           sprintf(value, "%d", *((uint32_t*)property->val));
        }break;
        case BT_PROPERTY_CLASS_OF_DEVICE:
        case BT_PROPERTY_TYPE_OF_DEVICE:
        {
            sprintf(value, "0x%x", *((uint32_t*)property->val));
        }break;
        case BT_PROPERTY_UUIDS:
        {
            uint32_t i;
            char buf[64];
            value[0] = 0;
            for (i=0; i < (property->len)/sizeof(bt_uuid_t); i++)
            {
                bt_uuid_t *p_uuid = (bt_uuid_t*)property->val + i;
                memset(buf, 0, sizeof(buf));
                uuid_to_string(p_uuid, buf);
                strcat(value, buf);
                strcat(value, ";");
            }
            value[strlen(value)] = 0;
        }break;
        default:
        {
           return BT_STATUS_FAIL;
        }
    }
    return BT_STATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         btif_in_get_remote_device_path_from_property
**
** Description      Internal helper function to map a property type
**                  to the NVRAM filename key
**
** Returns          NVRAM filename key if successfull, NULL otherwise
**
*******************************************************************************/
static char* btif_in_get_remote_device_path_from_property(bt_property_type_t type)
{
     switch (type)
     {
         case BT_PROPERTY_BDADDR:
         case BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP:
              return BTIF_STORAGE_PATH_REMOTE_DEVICES;
         case BT_PROPERTY_BDNAME:
              return BTIF_STORAGE_PATH_REMOTE_NAMES;
         case BT_PROPERTY_CLASS_OF_DEVICE:
              return BTIF_STORAGE_PATH_REMOTE_DEVCLASSES;
         case BT_PROPERTY_TYPE_OF_DEVICE:
              return BTIF_STORAGE_PATH_REMOTE_DEVTYPES;
         case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
              return BTIF_STORAGE_PATH_REMOTE_ALIASES;
         case BT_PROPERTY_UUIDS:
              return BTIF_STORAGE_PATH_REMOTE_SERVICES;
         default:
              return NULL;
     }
}

/*******************************************************************************
**
** Function         btif_in_load_device_iter_cb
**
** Description      Internal iterator callback from UNV when loading the
**                  link-keys
**
** Returns
**
*******************************************************************************/

int btif_in_load_device_iter_cb(char *key, char *value, void *userdata)
{
    btif_bonded_devices_t *p_bonded_devices = (btif_bonded_devices_t *)userdata;
    DEV_CLASS dev_class = {0, 0, 0};
    bt_bdaddr_t bd_addr;
    LINK_KEY link_key;
    uint8_t key_type;
    uint8_t pin_length;
    int offset = 0;
    int8_t temp[3];
    uint32_t i;

    memset(temp, 0, sizeof(temp));

    BTIF_TRACE_DEBUG3("%s %s %s", __FUNCTION__, key, value);

    /* convert 32 char linkkey (fixed size) */
    for (i = 0; i < LINK_KEY_LEN; i++)
    {
        memcpy(temp, value + (i * 2), 2);
        link_key[i] = (uint8_t) strtol((const char *)temp, NULL, 16);
        offset+=2;
    }

    /* skip space */
    offset++;

    /* convert decimal keytype (max 2 ascii chars) */
    memset(temp, 0, sizeof(temp));
    memcpy(temp, value + offset, 2);
    key_type = (uint8_t)strtoul((const char *)temp, NULL, 10);

    /* value + space */
    offset+=2;

    /* convert decimal pinlen (max 2 ascii chars) */
    memset(temp, 0, sizeof(temp));
    memcpy(temp, value + offset, 2);
    pin_length = (uint8_t)strtoul((const char *)temp, NULL, 10);

    /* convert bd address (keystring) */
    str2bd(key, &bd_addr);

    /* add extracted information to BTA security manager */
    BTA_DmAddDevice(bd_addr.address, dev_class, link_key, 0, 0, key_type, 0);

     /* Fill in the bonded devices */
    memcpy(&p_bonded_devices->devices[p_bonded_devices->num_devices++], &bd_addr, sizeof(bt_bdaddr_t));

    return 0;
}

/*******************************************************************************
**
** Function         btif_in_fetch_bonded_devices
**
** Description      Internal helper function to fetch the bonded devices
**                  from NVRAM
**
** Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
**
*******************************************************************************/
static bt_status_t btif_in_fetch_bonded_devices(btif_bonded_devices_t *p_bonded_devices)
{
    char *fname;
    int ret;

    memset(p_bonded_devices, 0, sizeof(btif_bonded_devices_t));

    fname = btif_in_make_filename(NULL, BTIF_STORAGE_PATH_REMOTE_LINKKEYS);

    if (fname == NULL)
        return BT_STATUS_FAIL;

    ret = unv_read_key_iter(fname, btif_in_load_device_iter_cb, p_bonded_devices);

    if (ret < 0)
        return BT_STATUS_FAIL;

    return BT_STATUS_SUCCESS;
}

/************************************************************************************
**  Externs
************************************************************************************/

/************************************************************************************
**  Functions
************************************************************************************/

/** functions are synchronous.
 * functions can be called by both internal modules such as BTIF_DM and by external entiries from HAL via BTIF_context_switch
 * For OUT parameters,  caller is expected to provide the memory.
 * Caller is expected to provide a valid pointer to 'property->value' based on the property->type
 */
/*******************************************************************************
**
** Function         btif_storage_get_adapter_property
**
** Description      BTIF storage API - Fetches the adapter property->type
**                  from NVRAM and fills property->val.
**                  Caller should provide memory for property->val and
**                  set the property->val
**
** Returns          BT_STATUS_SUCCESS if the fetch was successful,
**                  BT_STATUS_FAIL otherwise
**
*******************************************************************************/
bt_status_t btif_storage_get_adapter_property(bt_property_t *property)
{
    bt_status_t status;
    char *fname;
    char *value;
    int ret;
    char linebuf[BTIF_STORAGE_MAX_LINE_SZ];

    /* initialize property->len */
    property->len = 0;

    /* Special handling for adapter BD_ADDR and BONDED_DEVICES */
    if (property->type == BT_PROPERTY_BDADDR)
    {
        BD_ADDR addr;
        bt_bdaddr_t *bd_addr = (bt_bdaddr_t*)property->val;
        BTM_GetLocalDeviceAddr(addr);
        bdcpy(bd_addr->address, addr);
        property->len = sizeof(bt_bdaddr_t);

        return BT_STATUS_SUCCESS;
    }
    else if (property->type == BT_PROPERTY_ADAPTER_BONDED_DEVICES)
    {
        btif_bonded_devices_t bonded_devices;

        btif_in_fetch_bonded_devices(&bonded_devices);

        BTIF_TRACE_DEBUG2("%s: Number of bonded devices: %d", __FUNCTION__, bonded_devices.num_devices);

        if (bonded_devices.num_devices > 0)
        {
            property->len = bonded_devices.num_devices * sizeof(bt_bdaddr_t);
            memcpy(property->val, bonded_devices.devices, property->len);
        }

        /* if there are no bonded_devices, then length shall be 0 */
        return BT_STATUS_SUCCESS;
    }
    else if (property->type == BT_PROPERTY_UUIDS)
    {
        /* publish list of local supported services */
        bt_uuid_t *p_uuid = (bt_uuid_t*)property->val;
        uint32_t num_uuids = 0;
        uint32_t i;

        tBTA_SERVICE_MASK service_mask = btif_get_enabled_services_mask();
        BTIF_TRACE_ERROR2("%s service_mask:0x%x", __FUNCTION__, service_mask);
        for (i=0; i < BTA_MAX_SERVICE_ID; i++)
        {
            /* This should eventually become a function when more services are enabled */
            if(service_mask
                &(tBTA_SERVICE_MASK)(1 << i))
            {
                 switch (i) {
                     case BTA_HFP_SERVICE_ID:
                     {
                          uuid16_to_uuid128(UUID_SERVCLASS_AG_HANDSFREE,
                                            p_uuid+num_uuids);
                 num_uuids++;
                          uuid16_to_uuid128(UUID_SERVCLASS_HEADSET_AUDIO_GATEWAY,
                                       p_uuid+num_uuids);
                     num_uuids++;
                     }break;
                     case BTA_A2DP_SERVICE_ID:
                     {
                          uuid16_to_uuid128(UUID_SERVCLASS_AUDIO_SOURCE,
                                            p_uuid+num_uuids);
                          num_uuids++;
                     }break;
                 }
            }
        }
        property->len = (num_uuids)*sizeof(bt_uuid_t);
        return BT_STATUS_SUCCESS;
    }

    /* fall through for other properties */

    /* create filepath */
    fname = btif_in_make_filename(NULL, BTIF_STORAGE_PATH_ADAPTER_INFO);

    if (fname == NULL)
    {
        return BT_STATUS_FAIL;
    }

    ret = unv_create_file(fname);

    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    value = unv_read_key( fname,
                          btif_in_get_adapter_key_from_type(property->type),
                          linebuf, UNV_MAXLINE_LENGTH);

    if (value == NULL)
    {
        /* properties not yet existing, request default values from bta */
        return btif_dm_get_adapter_property(property);
    }
    else
    {
        /* convert to property_t data structure */
        status = btif_in_str_to_property(value, property);
    }

    return status;
}

/*******************************************************************************
**
** Function         btif_storage_set_adapter_property
**
** Description      BTIF storage API - Stores the adapter property
**                  to NVRAM
**
** Returns          BT_STATUS_SUCCESS if the store was successful,
**                  BT_STATUS_FAIL otherwise
**
*******************************************************************************/
bt_status_t btif_storage_set_adapter_property(bt_property_t *property)
{
    char *fname;
    char value[1200];
    int ret;

    fname = btif_in_make_filename(NULL, BTIF_STORAGE_PATH_ADAPTER_INFO);
    if (fname == NULL)
    {
        return BT_STATUS_FAIL;
    }
    ret = unv_create_file(fname);
    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    if (btif_in_property_to_str(property, value) != BT_STATUS_SUCCESS)
    {
        return BT_STATUS_FAIL;
    }
    ret = unv_write_key(fname, btif_in_get_adapter_key_from_type(property->type), value);
    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    return BT_STATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         btif_storage_get_remote_device_property
**
** Description      BTIF storage API - Fetches the remote device property->type
**                  from NVRAM and fills property->val.
**                  Caller should provide memory for property->val and
**                  set the property->val
**
** Returns          BT_STATUS_SUCCESS if the fetch was successful,
**                  BT_STATUS_FAIL otherwise
**
*******************************************************************************/
bt_status_t btif_storage_get_remote_device_property(bt_bdaddr_t *remote_bd_addr,
                                                           bt_property_t *property)
{
    char linebuf[BTIF_STORAGE_MAX_LINE_SZ];
    char *fname;
    char *value;
    int ret;
    bdstr_t bdstr;

    fname = btif_in_make_filename(NULL,
                  btif_in_get_remote_device_path_from_property(property->type));
    if (fname == NULL)
    {
        return BT_STATUS_FAIL;
    }

    ret = unv_create_file(fname);
    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    value = unv_read_key(fname, bd2str(remote_bd_addr, &bdstr), linebuf, BTIF_STORAGE_MAX_LINE_SZ);

    return btif_in_str_to_property(value, property);
}

/*******************************************************************************
**
** Function         btif_storage_set_remote_device_property
**
** Description      BTIF storage API - Stores the remote device property
**                  to NVRAM
**
** Returns          BT_STATUS_SUCCESS if the store was successful,
**                  BT_STATUS_FAIL otherwise
**
*******************************************************************************/
bt_status_t btif_storage_set_remote_device_property(bt_bdaddr_t *remote_bd_addr,
                                                          bt_property_t *property)
{
    char value[1200];
    char *fname;
    bdstr_t bdstr;
    int ret;

    fname = btif_in_make_filename(NULL,
                  btif_in_get_remote_device_path_from_property(property->type));
    if (fname == NULL)
    {
        return BT_STATUS_FAIL;
    }
    ret = unv_create_file(fname);
    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }
    memset(value, 0, sizeof(value));

    if (btif_in_property_to_str(property, value) != BT_STATUS_SUCCESS)
    {
        return BT_STATUS_FAIL;
    }

    ret = unv_write_key(fname, bd2str(remote_bd_addr, &bdstr), value);
    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    return BT_STATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         btif_storage_add_remote_device
**
** Description      BTIF storage API - Adds a newly discovered device to NVRAM
**                  along with the timestamp. Also, stores the various
**                  properties - RSSI, BDADDR, NAME (if found in EIR)
**
** Returns          BT_STATUS_SUCCESS if the store was successful,
**                  BT_STATUS_FAIL otherwise
**
*******************************************************************************/
bt_status_t btif_storage_add_remote_device(bt_bdaddr_t *remote_bdaddr,
                                                 uint32_t num_properties,
                                                 bt_property_t *properties)
{
    uint32_t i = 0;
    /* TODO: If writing a property, fails do we go back undo the earlier
     * written properties? */
    for (i=0; i < num_properties; i++)
    {
        /* Ignore the RSSI as this is not stored in DB */
        if (properties[i].type == BT_PROPERTY_REMOTE_RSSI)
            continue;

        /* BD_ADDR for remote device needs special handling as we also store timestamp */
        if (properties[i].type == BT_PROPERTY_BDADDR)
        {
            bt_property_t addr_prop;
            memcpy(&addr_prop, &properties[i], sizeof(bt_property_t));
            addr_prop.type = BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP;
            btif_storage_set_remote_device_property(remote_bdaddr,
                                                    &addr_prop);
        }
        else
        {
            btif_storage_set_remote_device_property(remote_bdaddr,
                                                    &properties[i]);
        }
    }
    return BT_STATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         btif_storage_add_bonded_device
**
** Description      BTIF storage API - Adds the newly bonded device to NVRAM
**                  along with the link-key, Key type and Pin key length
**
** Returns          BT_STATUS_SUCCESS if the store was successful,
**                  BT_STATUS_FAIL otherwise
**
*******************************************************************************/

bt_status_t btif_storage_add_bonded_device(bt_bdaddr_t *remote_bd_addr,
                                                 LINK_KEY link_key,
                                                 uint8_t key_type,
                                                 uint8_t pin_length)
{
    char value[STORAGE_REMOTE_LINKKEYS_ENTRY_SIZE];
    uint32_t i = 0;
    char *fname;
    bdstr_t bdstr;
    int ret;

    fname = btif_in_make_filename(NULL,
                                  BTIF_STORAGE_PATH_REMOTE_LINKKEYS);
    if (fname == NULL)
    {
        return BT_STATUS_FAIL;
    }
    ret = unv_create_file(fname);

    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    /* check ascii representations doesn't exceed max size */

    if (key_type > STORAGE_KEY_TYPE_MAX)
        return BT_STATUS_FAIL;

    if (pin_length > PIN_CODE_LEN)
        return BT_STATUS_FAIL;

    memset(value, 0, sizeof(value));

    for (i = 0; i < LINK_KEY_LEN; i++)
       sprintf(value + (i * 2), "%2.2X", link_key[i]);

    sprintf(value + (LINK_KEY_LEN*2), " %d %d", key_type, pin_length);

    ret = unv_write_key(fname, bd2str(remote_bd_addr, &bdstr), value);

    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    return BT_STATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         btif_storage_remove_bonded_device
**
** Description      BTIF storage API - Deletes the bonded device from NVRAM
**
** Returns          BT_STATUS_SUCCESS if the deletion was successful,
**                  BT_STATUS_FAIL otherwise
**
*******************************************************************************/
bt_status_t btif_storage_remove_bonded_device(bt_bdaddr_t *remote_bd_addr)
{
    char *fname;
    int ret;
    bdstr_t bdstr;

    fname = btif_in_make_filename(NULL,
                                  BTIF_STORAGE_PATH_REMOTE_LINKKEYS);
    if (fname == NULL)
    {
        return BT_STATUS_FAIL;
    }
    ret = unv_create_file(fname);

    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    ret = unv_remove_key(fname, bd2str(remote_bd_addr, &bdstr));

    if (ret < 0)
    {
        return BT_STATUS_FAIL;
    }

    return BT_STATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         btif_storage_load_bonded_devices
**
** Description      BTIF storage API - Loads all the bonded devices from NVRAM
**                  and adds to the BTA.
**                  Additionally, this API also invokes the adaper_properties_cb
**                  and remote_device_properties_cb for each of the bonded devices.
**
** Returns          BT_STATUS_SUCCESS if successful, BT_STATUS_FAIL otherwise
**
*******************************************************************************/
bt_status_t btif_storage_load_bonded_devices(void)
{
    char *fname;
    btif_bonded_devices_t bonded_devices;
    uint32_t i = 0;
    bt_property_t adapter_props[6];
    uint32_t num_props = 0;
    bt_property_t remote_properties[8];
    bt_bdaddr_t addr;
    bt_bdname_t name, alias;
    bt_scan_mode_t mode;
    uint32_t disc_timeout;
    bt_bdaddr_t *devices_list;
    bt_uuid_t local_uuids[BT_MAX_NUM_UUIDS];
    bt_uuid_t remote_uuids[BT_MAX_NUM_UUIDS];
    uint32_t cod, devtype;

    btif_in_fetch_bonded_devices(&bonded_devices);

    /* Now send the adapter_properties_cb with all adapter_properties */
    {
         memset(adapter_props, 0, sizeof(adapter_props));

         /* BD_ADDR */
         BTIF_STORAGE_GET_ADAPTER_PROP(BT_PROPERTY_BDADDR, &addr, sizeof(addr),
                                      adapter_props[num_props]);
         num_props++;

         /* BD_NAME */
         BTIF_STORAGE_GET_ADAPTER_PROP(BT_PROPERTY_BDNAME, &name, sizeof(name),
                                       adapter_props[num_props]);
         num_props++;

         /* SCAN_MODE */
         /* TODO: At the time of BT on, always report the scan mode as 0 irrespective
          of the scan_mode during the previous enable cycle.
          This needs to be re-visited as part of the app/stack enable sequence
          synchronization */
         mode = BT_SCAN_MODE_NONE;
         adapter_props[num_props].type = BT_PROPERTY_ADAPTER_SCAN_MODE;
         adapter_props[num_props].len = sizeof(mode);
         adapter_props[num_props].val = &mode;
         num_props++;

         /* DISC_TIMEOUT */
         BTIF_STORAGE_GET_ADAPTER_PROP(BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT,
                                      &disc_timeout, sizeof(disc_timeout),
                                      adapter_props[num_props]);
         num_props++;

         /* BONDED_DEVICES */
         devices_list = (bt_bdaddr_t*)malloc(sizeof(bt_bdaddr_t)*bonded_devices.num_devices);
         adapter_props[num_props].type = BT_PROPERTY_ADAPTER_BONDED_DEVICES;
         adapter_props[num_props].len = bonded_devices.num_devices * sizeof(bt_bdaddr_t);
         adapter_props[num_props].val = devices_list;
         for (i=0; i < bonded_devices.num_devices; i++)
         {
             memcpy(devices_list + i, &bonded_devices.devices[i], sizeof(bt_bdaddr_t));
         }
         num_props++;

         /* LOCAL UUIDs */
         BTIF_STORAGE_GET_ADAPTER_PROP(BT_PROPERTY_UUIDS,
                                       local_uuids, sizeof(local_uuids),
                                             adapter_props[num_props]);
         num_props++;

         btif_adapter_properties_evt(BT_STATUS_SUCCESS, num_props, adapter_props);

         free(devices_list);
    }

    BTIF_TRACE_EVENT2("%s: %d bonded devices found", __FUNCTION__, bonded_devices.num_devices);

    {
        for (i = 0; i < bonded_devices.num_devices; i++)
        {
             bt_bdaddr_t *p_remote_addr;

             num_props = 0;
             p_remote_addr = &bonded_devices.devices[i];
             memset(remote_properties, 0, sizeof(remote_properties));
             BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_BDNAME,
                                          &name, sizeof(name),
                                          remote_properties[num_props]);
             num_props++;

             BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_REMOTE_FRIENDLY_NAME,
                                          &alias, sizeof(alias),
                                          remote_properties[num_props]);
             num_props++;

             BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_CLASS_OF_DEVICE,
                                          &cod, sizeof(cod),
                                          remote_properties[num_props]);
             num_props++;

             BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_TYPE_OF_DEVICE,
                                          &devtype, sizeof(devtype),
                                          remote_properties[num_props]);
             num_props++;

             BTIF_STORAGE_GET_REMOTE_PROP(p_remote_addr, BT_PROPERTY_UUIDS,
                                          remote_uuids, sizeof(remote_uuids),
                                          remote_properties[num_props]);
             num_props++;

             btif_remote_properties_evt(BT_STATUS_SUCCESS, p_remote_addr,
                                        num_props, remote_properties);
         }
    }
    return BT_STATUS_SUCCESS;
}
